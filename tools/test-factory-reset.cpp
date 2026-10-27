#include "luxio.pb.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
static bool marker, failMarker, rebootPending, updateChecking, factoryResetPending;
static uint32_t radioRestartAt, factoryResetAt, now = 100;
static uint32_t millis() { return now; }
static std::vector<std::string> events;
static bool userFiles = true, crashData = true, userNvs = true;
static bool missingFiles, missingDump;
static std::string failAt;
struct Partition { std::string name; size_t size; };
static Partition files{"spiffs", 0x40000}, dump{"coredump", 0x10000};
constexpr int ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 130, ESP_PARTITION_SUBTYPE_DATA_COREDUMP = 3;
static const Partition *esp_partition_find_first(int type, int subtype, const char *name) {
  assert(type == ESP_PARTITION_TYPE_DATA);
  if (subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS) { assert(std::string(name) == "spiffs"); return missingFiles ? nullptr : &files; }
  assert(subtype == ESP_PARTITION_SUBTYPE_DATA_COREDUMP && std::string(name) == "coredump");
  return missingDump ? nullptr : &dump;
}
static int esp_partition_erase_range(const Partition *part, size_t offset, size_t size) {
  assert((part == &files || part == &dump) && offset == 0 && size == part->size);
  events.push_back(part->name);
  if (failAt == part->name) return -1;
  if (part == &files) userFiles = false; else crashData = false;
  return 0;
}
static int nvs_flash_deinit() { events.push_back("nvs_deinit"); return failAt == "nvs_deinit" ? -1 : 0; }
static int nvs_flash_erase() { events.push_back("nvs_erase"); if (failAt == "nvs_erase") return -1; userNvs = false; marker = false; return 0; }
static int nvs_flash_init() { events.push_back("nvs_init"); return 0; }
#define ESP_ERROR_CHECK(value) do { if ((value) != 0) throw std::runtime_error("Flash failure"); } while (0)
#define abort() throw std::runtime_error("Abort")
struct Preferences {
  bool begin(const char *name, bool readOnly) { assert(std::string(name) == "luxio" && !readOnly); return true; }
  bool getBool(const char *key, bool fallback) { assert(std::string(key) == "factory-reset" && !fallback); return marker; }
  size_t putBool(const char *key, bool value) { assert(std::string(key) == "factory-reset" && value); if (failMarker) return 0; marker = true; return 1; }
  void end() {}
} prefs;
static std::string error;
static luxio_Request input;
static luxio_Response output;
static void command() {
  auto *request = &input; auto *response = &output;
  output = {}; error.clear();
  switch (request->operation) {
// PRODUCTION_COMMAND
    default: assert(false);
  }
}
static void boot() {
// PRODUCTION_BOOT
}
int main() {
  boot(); assert(events.empty() && userFiles && crashData && userNvs);
  input.operation = luxio_Operation_FACTORY_RESET;
  command(); assert(!error.empty() && !marker && !factoryResetPending && !output.factory_reset_accepted);
  input.factory_reset_confirmed = true;
  for (int busy = 0; busy < 3; busy++) {
    rebootPending = busy == 0; radioRestartAt = busy == 1 ? 10 : 0; updateChecking = busy == 2;
    command(); assert(!error.empty() && !marker && !output.factory_reset_accepted);
  }
  rebootPending = false; radioRestartAt = 0; updateChecking = false;
  failMarker = true; command(); assert(!error.empty() && !factoryResetPending && !output.factory_reset_accepted);
  failMarker = false; command();
  assert(error.empty() && output.factory_reset_accepted && factoryResetPending && marker && factoryResetAt == 1100);
  assert(events.empty() && userFiles && crashData && userNvs); // Acknowledge before any erasure.
  now = 500; command(); assert(error.empty() && output.factory_reset_accepted && factoryResetAt == 1100);
  // Fail each erase stage before NVS completion, then simulate another boot.
  for (const auto &stage : {"spiffs", "coredump", "nvs_deinit", "nvs_erase"}) {
    marker = userFiles = crashData = userNvs = true; events.clear(); failAt = stage;
    bool failed = false; try { boot(); } catch (const std::runtime_error &) { failed = true; }
    assert(failed && marker && userNvs); // Marker survives; user-state boot never proceeds.
    failAt.clear(); events.clear(); boot();
    assert(!marker && !userFiles && !crashData && !userNvs);
    assert((events == std::vector<std::string>{"spiffs", "coredump", "nvs_deinit", "nvs_erase", "nvs_init"}));
    events.clear(); boot(); assert(events.empty());
  }
  marker = true; missingFiles = true;
  bool failed = false; try { boot(); } catch (const std::runtime_error &) { failed = true; }
  assert(failed && marker); missingFiles = false;
  missingDump = true; events.clear(); boot();
  assert((events == std::vector<std::string>{"spiffs", "nvs_deinit", "nvs_erase", "nvs_init"}));
  puts("PASS: factory reset requires confirmation, acknowledges before restart, clears user partitions only, and retries failed erasure");
}
