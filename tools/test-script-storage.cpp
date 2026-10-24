#include "../Luxio/src/script_storage.h"
#include "../Luxio/src/vendor/nanopb/pb_encode.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>

int main(int argc, char **argv) {
  luxio_Script script{}, decoded{};
  strcpy(script.id, "12345678-1234-4234-8234-123456789abc");
  strcpy(script.name, "Underwater 🌊");
  script.mode = luxio_Mode_GRADIENT;
  uint8_t bytes[SCRIPT_RECORD_MAX] = {};
  if (argc == 3) {
    std::ifstream file(argv[1], std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assert(!source.empty() && source.size() <= 16384);
    memcpy(script.source, source.data(), source.size());
    const auto size = encodeStoredScript(script, bytes, sizeof(bytes));
    assert(size && decodeStoredScript(bytes, size, decoded));
    assert(!strcmp(script.source, decoded.source));
    std::ofstream out(argv[2], std::ios::binary); out.write(reinterpret_cast<char *>(bytes), size);
    printf("%zu -> %zu bytes (including name, UUID and header)\n", source.size(), size);
    return 0;
  }
  const char *source = "function setup()\n local count = luxio_get_pixel_count()\n for i = 0, count - 1 do\n  luxio_set_pixel_color(i, 0, 120, 255)\n end\nend\nfunction loop() end\n";
  strcpy(script.source, source);
  auto size = encodeStoredScript(script, bytes, sizeof(bytes));
  assert(size && decodeStoredScript(bytes, size, decoded));
  assert(!strcmp(decoded.source, source) && !strcmp(decoded.id, script.id) && !strcmp(decoded.name, script.name) && decoded.mode == script.mode);
  assert(decodeStoredScript(bytes, size, decoded, true) && !decoded.source[0] && decoded.mode == script.mode);
  for (size_t i = 0; i < size; ++i) {
    bytes[i] ^= 0x80;
    assert(!decodeStoredScript(bytes, size, decoded));
    bytes[i] ^= 0x80;
    assert(!decodeStoredScript(bytes, i, decoded));
  }
  // Legacy records remain usable, including metadata-only listing.
  auto encoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
  assert(pb_encode(&encoder, luxio_Script_fields, &script));
  assert(decodeStoredScript(bytes, encoder.bytes_written, decoded) && !strcmp(decoded.source, source));
  assert(decodeStoredScript(bytes, encoder.bytes_written, decoded, true) && !decoded.source[0]);
  // Maximum-size repetitive and incompressible source, plus tiny fallback.
  std::mt19937 random(12345);
  for (size_t length : {size_t(1), size_t(31), size_t(1024), size_t(16384)}) {
    for (bool repetitive : {false, true}) {
      for (size_t i = 0; i < length; ++i) script.source[i] = repetitive ? 'x' : char(1 + random() % 255);
      script.source[length] = 0;
      std::vector<uint8_t> sized(24 + 2048 + length);
      size = encodeStoredScript(script, sized.data(), sized.size());
      assert(size && size <= length + 152);
      memcpy(bytes, sized.data(), size);
      assert(!encodeStoredScript(script, sized.data(), 23));
      assert(!encodeStoredScript(script, sized.data(), 24 + length));
      assert(decodeStoredScript(bytes, size, decoded));
      assert(!memcmp(script.source, decoded.source, length + 1));
      if (repetitive && length >= 1024) assert(size < length / 2);
      if (!repetitive && length == 16384) assert(bytes[4] == 0);
    }
  }
  // Malformed records must never overrun buffers or hang.
  for (int trial = 0; trial < 3000; ++trial) {
    const size_t length = random() % sizeof(bytes);
    for (size_t i = 0; i < length; ++i) bytes[i] = uint8_t(random());
    (void)decodeStoredScript(bytes, length, decoded);
  }
  script.source[0] = 0;
  assert(!encodeStoredScript(script, bytes, sizeof(bytes)));
  memset(script.source, 'x', sizeof(script.source));
  assert(!encodeStoredScript(script, bytes, sizeof(bytes)));
  assert(!decodeStoredScript(bytes, SCRIPT_RECORD_MAX + 1, decoded));
  puts("PASS: compressed/plain/legacy records, exact text and mode, metadata listing, 16 KiB limit, corruption, truncation and malformed input");
}
