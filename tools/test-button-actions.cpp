#include "luxio.pb.h"
#include "button_input.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct String : std::string {
  using std::string::string;
  String(std::string value) : std::string(value) {}
  int lastIndexOf(char c) const { auto pos = rfind(c); return pos == npos ? -1 : int(pos); }
  String substring(size_t start) const { return substr(start); }
  String substring(size_t start, size_t end) const { return substr(start, end - start); }
  bool startsWith(const char *s) const { return rfind(s, 0) == 0; }
  bool endsWith(const char *s) const { return size() >= strlen(s) && substr(size() - strlen(s)) == s; }
};
static std::map<std::string, std::vector<std::string>> files;
struct File {
  String path; bool folder = false; size_t index = 0;
  explicit operator bool() const { return !path.empty(); }
  bool isDirectory() { return folder; }
  String name() { return path; }
  void close() {}
  File openNextFile() { auto &entries = files[path]; return index < entries.size() ? File{entries[index++], false} : File{}; }
};
struct { File open(const char *path) { return files.count(path) ? File{path, true} : File{}; } } LittleFS;
static luxio_State state;
static bool factoryResetPending;
static uint32_t radioRestartAt, now;
static int stateMutex, portMAX_DELAY;
static int lockDepth;
static void xSemaphoreTake(int, int) { assert(lockDepth++ == 0); }
static void xSemaphoreGive(int) { assert(--lockDepth == 0); }
static uint32_t millis() { return now; }
static constexpr int INPUT = 0, INPUT_PULLUP = 1, LOW = 0;
static bool down;
static std::map<int, int> modes;
static void pinMode(int gpio, int mode) { modes[gpio] = mode; }
static int digitalRead(int) { return down ? 0 : 1; }
static bool validId(const char *id) { return strlen(id) == 36; }
struct Stream { size_t bytes_written = 1; };
static Stream pb_ostream_from_buffer(uint8_t *, size_t) { return {}; }
static luxio_Request encoded;
static bool pb_encode(Stream *, const pb_msgdesc_t *, const luxio_Request *request) { encoded = *request; return true; }
const pb_msgdesc_t luxio_Request_msg = {};
static std::vector<std::string> commands;
static std::string rejected;
static size_t dispatch(const uint8_t *, size_t, std::unique_ptr<uint8_t[]> &, size_t, bool *accepted) {
  assert(lockDepth == 0);
  std::string id = encoded.operation == luxio_Operation_RUN_COLOR ? encoded.color_id : encoded.operation == luxio_Operation_RUN_GRADIENT ? encoded.gradient_id : encoded.script_id;
  commands.push_back(encoded.operation == luxio_Operation_SET_POWER ? (encoded.on ? "on" : "off") : id);
  *accepted = id.empty() || id != rejected;
  if (*accepted) {
    if (encoded.operation == luxio_Operation_SET_POWER) state.on = encoded.on;
    else {
      state.on = true;
      state.mode = encoded.operation == luxio_Operation_RUN_COLOR ? luxio_Mode_COLOR : encoded.operation == luxio_Operation_RUN_GRADIENT ? luxio_Mode_GRADIENT : luxio_Mode_ANIMATION;
      strcpy(state.active_color_id, encoded.color_id); strcpy(state.active_gradient_id, encoded.gradient_id); strcpy(state.active_script_id, encoded.script_id);
    }
  }
  return 1;
}
static void poll() {
// PRODUCTION_BUTTON
}
static void press(uint32_t duration = 100) {
  now += 100; down = true; poll(); now += 30; poll(); now += duration - 30; poll();
  down = false; poll(); now += 30; poll();
}
int main() {
  state.config.gpio = 16; state.config.led_count = 60; state.config.button = {false, 18, luxio_ButtonPressAction_TOGGLE_POWER};
  state.brightness = 42; state.on = true;
  poll(); press(); assert(commands.empty() && modes.empty());
  state.config.button.enabled = true; poll(); assert(modes[18] == INPUT_PULLUP);
  press(); assert(!state.on && commands.back() == "off");
  press(); assert(state.on && commands.back() == "on");
  auto count = commands.size(); press(2000); assert(!state.on && commands.size() == count + 1 && commands.back() == "off");
  const std::string a = "00000000-0000-4000-8000-000000000001", b = "00000000-0000-4000-8000-000000000002", c = "00000000-0000-4000-8000-000000000003", d = "00000000-0000-4000-8000-000000000004";
  // Deliberately unsorted file iteration; non-library files are ignored.
  files["/colors"] = { b + ".pb", "pending.tmp", a + ".pb", "invalid.pb" };
  files["/gradients"] = {c + ".pb"}; files["/scripts"] = {d + ".pb"};
  state.config.button.press = luxio_ButtonPressAction_CYCLE_SAVED_PRESETS; poll();
  press(); assert(state.on && commands.back() == a);
  press(); assert(commands.back() == b);
  press(); assert(commands.back() == c && state.mode == luxio_Mode_GRADIENT);
  press(); assert(commands.back() == d && state.mode == luxio_Mode_ANIMATION);
  press(); assert(commands.back() == a);
  // Resume from the actual selected preset, including selection by a client.
  state.mode = luxio_Mode_COLOR; strcpy(state.active_color_id, b.c_str()); state.config.led_count = 1;
  press(); assert(commands.back() == d);
  rejected = a; press(); assert(commands.back() == b);
  files.clear(); count = commands.size(); press(); assert(commands.size() == count);
  files["/colors"] = {a + ".pb"}; press(); assert(commands.size() == count + 1 && commands.back() == a);
  state.config.button.enabled = false; poll(); assert(modes[18] == INPUT);
  count = commands.size(); press(); assert(commands.size() == count);
  // Enabling while held does not toggle or switch presets on release.
  down = true; state.config.button.enabled = true; poll(); now += 2000; poll();
  down = false; poll(); now += 30; poll(); assert(commands.size() == count);
  assert(state.brightness == 42);
  puts("PASS: production button GPIO lifecycle, power, long hold, ordered preset cycling, wrap, empty/corrupt files and single LED");
}
