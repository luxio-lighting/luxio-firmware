#include "../Luxio/src/script_storage.h"
#include "../Luxio/src/vendor/nanopb/pb_encode.h"
extern "C" {
#include "vendor/littlefs/lfs.h"
}
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  assert(argc > 1);
  std::vector<std::string> sources;
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i], std::ios::binary);
    sources.emplace_back(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    assert(!sources.back().empty() && sources.back().size() <= 16384);
  }
  struct Flash { std::vector<uint8_t> bytes = std::vector<uint8_t>(256 * 1024, 0xff); int failAfter = -1; int writes = 0; } flash;
  lfs_config config{};
  config.context = &flash;
  config.read = [](const lfs_config *c, lfs_block_t b, lfs_off_t off, void *out, lfs_size_t n) {
    auto &f = *static_cast<Flash *>(c->context); assert(b < c->block_count && off + n <= c->block_size);
    memcpy(out, f.bytes.data() + b * c->block_size + off, n); return 0;
  };
  config.prog = [](const lfs_config *c, lfs_block_t b, lfs_off_t off, const void *in, lfs_size_t n) {
    auto &f = *static_cast<Flash *>(c->context); assert(b < c->block_count && off + n <= c->block_size);
    if (f.failAfter >= 0 && f.writes++ >= f.failAfter) return int(LFS_ERR_IO);
    for (size_t i = 0; i < n; ++i) f.bytes[b * c->block_size + off + i] &= static_cast<const uint8_t *>(in)[i];
    return 0;
  };
  config.erase = [](const lfs_config *c, lfs_block_t b) {
    auto &f = *static_cast<Flash *>(c->context);
    if (f.failAfter >= 0 && f.writes++ >= f.failAfter) return int(LFS_ERR_IO);
    memset(f.bytes.data() + b * c->block_size, 0xff, c->block_size); return 0;
  };
  config.sync = [](const lfs_config *) { return 0; };
  config.read_size = 128; config.prog_size = 128; config.block_size = 4096; config.block_count = 64;
  config.cache_size = 512; config.lookahead_size = 128; config.block_cycles = 512;
  lfs_t fs{};
  char sourceBuffer[16385]{};
  luxio_Script script{}, decoded{}; script.source = sourceBuffer;
  strcpy(script.name, "Saved animation or gradient");
  uint8_t bytes[SCRIPT_RECORD_MAX];
  size_t rawTotal = 0, compressedTotal = 0;
  for (const auto &source : sources) {
    strcpy(script.id, "12345678-1234-4234-8234-123456789abc");
    strcpy(script.source, source.c_str()); script.mode = luxio_Mode_ANIMATION;
    auto old = pb_ostream_from_buffer(bytes, sizeof(bytes)); assert(pb_encode(&old, luxio_Script_fields, &script)); rawTotal += old.bytes_written;
    auto size = encodeStoredScript(script, bytes, sizeof(bytes)); assert(size); compressedTotal += size;
    assert(decodeStoredScript(bytes, size, decoded) && !strcmp(decoded.source, source.c_str()));
  }
  printf("Preset records: %zu bytes plain -> %zu compressed (%.1f%% smaller, metadata included)\n", rawTotal, compressedTotal, 100.0 * (rawTotal - compressedTotal) / rawTotal);
  int counts[2] = {};
  for (int compressed = 0; compressed < 2; ++compressed) {
    std::fill(flash.bytes.begin(), flash.bytes.end(), 0xff);
    assert(!lfs_format(&fs, &config) && !lfs_mount(&fs, &config) && !lfs_mkdir(&fs, "/scripts"));
    for (int i = 0; i < 4096; ++i) {
      snprintf(script.id, sizeof(script.id), "12345678-1234-4234-8234-%012d", i);
      strcpy(script.source, sources[i % sources.size()].c_str()); script.mode = i % 2 ? luxio_Mode_ANIMATION : luxio_Mode_GRADIENT;
      size_t size;
      if (compressed) size = encodeStoredScript(script, bytes, sizeof(bytes));
      else { auto old = pb_ostream_from_buffer(bytes, sizeof(bytes)); assert(pb_encode(&old, luxio_Script_fields, &script)); size = old.bytes_written; }
      assert(size);
      if (config.block_count * config.block_size - lfs_fs_size(&fs) * config.block_size < size + 8192) break;
      char path[64]; snprintf(path, sizeof(path), "/scripts/%s.pb", script.id);
      lfs_file_t file{}; assert(!lfs_file_open(&fs, &file, "/scripts/pending", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC));
      auto written = lfs_file_write(&fs, &file, bytes, size);
      int closed = lfs_file_close(&fs, &file);
      if (written != int(size) || closed || lfs_rename(&fs, "/scripts/pending", path)) break;
      counts[compressed]++;
    }
    printf("256 KiB LittleFS, 8 KiB save reserve: %d %s presets\n", counts[compressed], compressed ? "compressed" : "uncompressed");
    assert(counts[compressed] > 32);
    // Every saved item must still be readable at full capacity.
    for (int i = 0; i < counts[compressed]; ++i) {
      char path[64]; snprintf(path, sizeof(path), "/scripts/12345678-1234-4234-8234-%012d.pb", i);
      lfs_file_t file{}; assert(!lfs_file_open(&fs, &file, path, LFS_O_RDONLY));
      auto size = lfs_file_read(&fs, &file, bytes, sizeof(bytes)); assert(!lfs_file_close(&fs, &file));
      assert(size > 0 && decodeStoredScript(bytes, size, decoded));
      assert(!strcmp(decoded.source, sources[i % sources.size()].c_str()));
    }
    assert(!lfs_unmount(&fs));
  }
  assert(counts[1] > counts[0]);
  // Cut power at each write/erase during replacement. Remount must expose
  // either the complete original or complete replacement, never a partial file.
  std::fill(flash.bytes.begin(), flash.bytes.end(), 0xff);
  assert(!lfs_format(&fs, &config) && !lfs_mount(&fs, &config) && !lfs_mkdir(&fs, "/scripts"));
  strcpy(script.id, "12345678-1234-4234-8234-123456789abc"); strcpy(script.source, sources.front().c_str());
  size_t size = encodeStoredScript(script, bytes, sizeof(bytes));
  lfs_file_t file{}; assert(!lfs_file_open(&fs, &file, "/scripts/test.pb", LFS_O_WRONLY | LFS_O_CREAT));
  assert(lfs_file_write(&fs, &file, bytes, size) == int(size) && !lfs_file_close(&fs, &file) && !lfs_unmount(&fs));
  const auto saved = flash.bytes;
  for (int cut = 0; cut < 40; ++cut) {
    flash.bytes = saved; flash.failAfter = -1; flash.writes = 0;
    assert(!lfs_mount(&fs, &config));
    strcpy(script.source, sources.back().c_str()); size = encodeStoredScript(script, bytes, sizeof(bytes));
    flash.failAfter = cut;
    if (!lfs_file_open(&fs, &file, "/scripts/pending", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC)) {
      const auto written = lfs_file_write(&fs, &file, bytes, size);
      const auto closed = lfs_file_close(&fs, &file);
      if (written == int(size) && !closed) (void)lfs_rename(&fs, "/scripts/pending", "/scripts/test.pb");
    }
    lfs_unmount(&fs); flash.failAfter = -1;
    assert(!lfs_mount(&fs, &config));
    (void)lfs_remove(&fs, "/scripts/pending");
    assert(!lfs_file_open(&fs, &file, "/scripts/test.pb", LFS_O_RDONLY));
    const auto length = lfs_file_read(&fs, &file, bytes, sizeof(bytes)); assert(!lfs_file_close(&fs, &file));
    assert(length > 0 && decodeStoredScript(bytes, length, decoded));
    assert(!strcmp(decoded.source, sources.front().c_str()) || !strcmp(decoded.source, sources.back().c_str()));
    assert(!lfs_unmount(&fs));
  }
  puts("PASS: full filesystem readback and atomic replacement across 40 interrupted-write points");
  pb_release(luxio_Script_fields, &decoded);
}
