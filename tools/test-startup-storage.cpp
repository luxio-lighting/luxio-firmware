#include "luxio.pb.h"
#include "vendor/nanopb/pb_encode.h"
#include "vendor/nanopb/pb_decode.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cstdio>
using std::min;
#define LUXIO_BUILD 37
#define LUXIO_DEFAULT_GPIO 4
#define constrain(x, lo, hi) std::min(std::max((x), decltype(x)(lo)), decltype(x)(hi))
static size_t testStrlcpy(char *out, const char *in, size_t n) { size_t size = strlen(in); if (n) { memcpy(out, in, std::min(size, n-1)); out[std::min(size, n-1)] = 0; } return size; }
static std::map<std::string, std::vector<uint8_t>> files;
#define strlcpy testStrlcpy
static size_t writeBudget = SIZE_MAX;
struct File {
  std::string name; size_t cursor = 0; bool valid = false;
  operator bool() const { return valid; }
  size_t size() const { return valid ? files[name].size() : 0; }
  size_t position() const { return cursor; }
  size_t write(const uint8_t *data, size_t n) {
    if (!valid) return 0;
    n = std::min(n, writeBudget); writeBudget -= n;
    auto &bytes = files[name]; if (bytes.size() < cursor + n) bytes.resize(cursor+n);
    memcpy(bytes.data()+cursor, data, n); cursor += n; return n;
  }
  size_t read(uint8_t *data, size_t n) {
    if (!valid) return 0;
    n = std::min(n, size()-cursor); memcpy(data, files[name].data()+cursor, n); cursor += n; return n;
  }
  bool seek(size_t position) { if (!valid || position > size()) return false; cursor=position; return true; }
  void flush() {} void close() { valid=false; }
};
struct {
  bool failRename = false;
  bool begin(bool) { return true; }
  File open(const char *path, const char *mode) {
    if (*mode=='w') files[path].clear();
    return {path, 0, files.count(path)!=0};
  }
  bool remove(const char *path) { return files.erase(path); }
  bool rename(const char *from, const char *to) {
    if (failRename) return false;
    files[to] = files[from]; files.erase(from); return true;
  }
} LittleFS;
static bool resumeOutputFlag = false;
static std::vector<uint8_t> storedConfig;
struct {
  bool getBool(const char *, bool) { return resumeOutputFlag; }
  void remove(const char *key) { if (!strcmp(key, "resume-output")) resumeOutputFlag = false; }
  size_t getBytesLength(const char *key) { return !strcmp(key, "config") ? storedConfig.size() : 0; }
  size_t getBytes(const char *key, void *data, size_t size) { if (strcmp(key, "config") || size != storedConfig.size()) return 0; memcpy(data, storedConfig.data(), size); return size; }
} prefs;
static luxio_State state;
static bool animation, dirty;
static char *activeAnimationSource;
static std::string loadedSource;
// PRODUCTION_SAVE
static void loadState() {
// PRODUCTION_LOAD
  loadedSource = startup && startup->script && startup->script->source ? startup->script->source : "";
}
static void finishBoot(bool resumeOutput) {
// PRODUCTION_FINISH_BOOT
}
int main() {
  loadState();
  assert(state.mode == luxio_Mode_GRADIENT && state.has_gradient && state.gradient.colors_count == 2);
  assert(state.gradient.colors[0].green == 210 && state.gradient.colors[0].blue == 255);
  assert(state.gradient.colors[1].red == 79 && state.gradient.colors[1].green == 255 && state.gradient.colors[1].blue == 127);
  assert(state.config.wifi.enabled && state.config.matter.enabled && state.on);
  state.config.led_count = 1024; strcpy(state.config.strip_type, "SK6812");
  state.brightness = 4; state.on = false; state.config.matter.enabled = false;
  state.mode = luxio_Mode_COLOR; state.has_gradient = false; state.color = {5, 11, 17, 255, 0, 0};
  uint8_t config[luxio_Config_size]; auto encoder = pb_ostream_from_buffer(config, sizeof(config));
  assert(pb_encode(&encoder, luxio_Config_fields, &state.config)); storedConfig.assign(config, config + encoder.bytes_written);
  // Emulate an OTA snapshot from build 36, before reset capability existed.
  state.build = 36; state.factory_reset_supported = false; state.supported_strip_types_count = 0;
  assert(saveUpdateState()); const auto original = files["/startup.pb"];
  // A normal cold boot ignores even a valid old output snapshot.
  memset(&state, 0, sizeof(state)); loadState();
  assert(state.on && state.brightness == 16 && state.mode == luxio_Mode_GRADIENT && state.config.led_count == 1024 && !state.config.matter.enabled);
  resumeOutputFlag = true;
  memset(&state, 0, sizeof(state)); loadState();
  assert(!state.on && state.brightness == 4 && state.config.led_count == 1024 && !state.config.matter.enabled);
  assert(state.build == 37 && state.factory_reset_supported);
  assert(state.supported_strip_types_count == 7);
  for (size_t i = 0; i < 7; i++) assert(!strcmp(state.supported_strip_types[i], supportedStripTypes[i]));
  assert(state.color.white == 255 && state.color.red == 5 && state.mode == luxio_Mode_COLOR);
  state.on = true; state.brightness = 200;
  for (size_t cut = 0; cut < original.size()+4; cut++) {
    writeBudget = cut; assert(!saveUpdateState()); assert(files["/startup.pb"] == original);
  }
  writeBudget = SIZE_MAX; LittleFS.failRename = true;
  assert(!saveUpdateState() && files["/startup.pb"] == original); LittleFS.failRename = false;
  // Maximum-size Lua plus a customized parameter survives the streamed roundtrip.
  std::string source(16384, ' '); source.replace(0, 19, "function loop() end ");
  activeAnimationSource = source.data(); animation = true;
  state.mode = luxio_Mode_ANIMATION; strcpy(state.script_name, "Unsaved animation");
  state.parameters_count = 1; strcpy(state.parameters[0].name, "Speed"); state.parameters[0].value = 73;
  assert(saveUpdateState()); memset(&state, 0, sizeof(state)); loadState();
  assert(loadedSource == source && state.mode == luxio_Mode_ANIMATION && state.on && state.brightness == 200);
  assert(state.parameters_count == 1 && state.parameters[0].value == 73);
  assert(!strcmp(state.script_name, "Unsaved animation"));
  finishBoot(true);
  assert(!resumeOutputFlag && !files.count("/startup.pb"));
  memset(&state, 0, sizeof(state)); loadState();
  assert(state.mode == luxio_Mode_GRADIENT && state.on && state.brightness == 16);
  assert(saveUpdateState()); resumeOutputFlag = true;
  // Corruption and interrupted temporary writes never boot unverified data.
  files["/startup.pb"][12] ^= 1; files["/startup.pending"] = {1,2,3};
  memset(&state, 0, sizeof(state)); loadState();
  assert(loadedSource.empty() && state.mode == luxio_Mode_GRADIENT && state.gradient.colors_count == 2);
  assert(!files.count("/startup.pending"));
  puts("PASS: cold-boot Luxio gradient, update-only static/animation restoration, persistent radio settings, 16 KB Lua, CRC rejection and interrupted-write atomicity");
}
