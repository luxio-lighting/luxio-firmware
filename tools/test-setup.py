#!/usr/bin/env python3
"""Exercise production scan deadlines and preview/commit persistence on the host."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parent.parent
source = (root / 'Luxio/src/main.cpp').read_text()
deadline = source.split('// WIFI_SCAN_DEADLINE_BEGIN')[1].split('// WIFI_SCAN_DEADLINE_END')[0]
start = source.index('      uint8_t saved[luxio_Config_size], previous[luxio_Config_size];')
persistence = source[start:source.index('      const bool nameChanged', start)]
timeout = next(line.strip() for line in source.splitlines() if 'wifiScanTimeoutMs = request->wifi_scan_timeout_ms ?' in line)
pause = source.split('// WIFI_SCAN_PAUSE_CONNECT_BEGIN')[1].split('// WIFI_SCAN_PAUSE_CONNECT_END')[0]
resume = source.split('// WIFI_SCAN_RESUME_CONNECT_BEGIN')[1].split('// WIFI_SCAN_RESUME_CONNECT_END')[0]
fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>
constexpr int WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2;
static uint32_t now, wifiScanStarted, wifiScanTimeoutMs;
static bool wifiScanStopRequested;
static int stopCalls;
static uint32_t millis() { return now; }
static void esp_wifi_scan_stop() { stopCalls++; }
struct {
  struct { bool joined = false; bool connected() { return joined; } } STA;
  bool autoReconnect = true;
  void setAutoReconnect(bool enabled) { autoReconnect = enabled; }
  int result = WIFI_SCAN_RUNNING; int scanComplete() { return result; }
} WiFi;
static bool wifiScanReconnect, wifiPending;
static std::string wifiSsid;
static int disconnectCalls;
static void esp_wifi_disconnect() { assert(!WiFi.autoReconnect && !WiFi.STA.joined); disconnectCalls++; }
struct Request { uint32_t wifi_scan_timeout_ms; bool config_preview; };
static uint32_t constrain(uint32_t n, uint32_t low, uint32_t high) { return std::clamp(n, low, high); }
static int poll(int count) {
  // DEADLINE
  return count;
}
constexpr size_t luxio_Config_size = 32;
constexpr int luxio_Config_fields = 0;
struct Config { uint32_t led_count; };
struct Stream { uint8_t *data; size_t bytes_written; };
static Stream pb_ostream_from_buffer(uint8_t *bytes, size_t) { return {bytes, 0}; }
static bool pb_encode(Stream *stream, int, Config *config) {
  memcpy(stream->data, config, sizeof(*config)); stream->bytes_written = sizeof(*config); return true;
}
struct {
  uint8_t bytes[luxio_Config_size] = {}; size_t size = 0; int writes = 0;
  size_t getBytesLength(const char *) { return size; }
  size_t getBytes(const char *, void *out, size_t n) { memcpy(out, bytes, n); return n; }
  size_t putBytes(const char *, const void *in, size_t n) { memcpy(bytes, in, n); size = n; writes++; return n; }
} prefs;
static std::string persist(Request *request, Config c) {
  std::string error;
  do {
    // PERSISTENCE
  } while (false);
  return error;
}
int main() {
  for (bool joined : {false, true}) for (bool saved : {false, true}) {
    WiFi.STA.joined = joined; WiFi.autoReconnect = true;
    wifiSsid = saved ? "saved-network" : "";
    wifiPending = false; disconnectCalls = 0;
    // PAUSE
    assert(wifiScanReconnect == (!joined && saved));
    assert(disconnectCalls == (!joined && saved ? 1 : 0));
    assert(WiFi.autoReconnect == (joined || !saved));
    // RESUME
    assert(WiFi.autoReconnect && !wifiScanReconnect);
    assert(wifiPending == (!joined && saved));
  }
  for (uint32_t budget : {0U, 1U, 2000U, 8000U, 99999U}) {
    Request data{budget, true}; auto *request = &data;
    // TIMEOUT
    assert(wifiScanTimeoutMs == (budget == 0 ? 8000 : std::clamp(budget, 250U, 8000U)));
    // Includes unsigned millisecond-clock wraparound.
    wifiScanStarted = UINT32_MAX - 100; wifiScanStopRequested = false; stopCalls = 0;
    WiFi.result = WIFI_SCAN_RUNNING;
    now = wifiScanStarted + wifiScanTimeoutMs - 201;
    assert(poll(WIFI_SCAN_RUNNING) == WIFI_SCAN_RUNNING && stopCalls == 0);
    now++; assert(poll(WIFI_SCAN_RUNNING) == WIFI_SCAN_RUNNING && stopCalls == 1);
    now += 20; poll(WIFI_SCAN_RUNNING); assert(stopCalls == 1);
    WiFi.result = 7; now = wifiScanStarted + wifiScanTimeoutMs - 50;
    assert(poll(WIFI_SCAN_RUNNING) == 7); // Partial results survive stopping.
    WiFi.result = WIFI_SCAN_RUNNING;
    assert(poll(WIFI_SCAN_RUNNING) == WIFI_SCAN_FAILED); // Driver cannot wait forever.
  }
  Request request{2000, true};
  for (uint32_t count : {60, 61, 62, 63, 64}) assert(persist(&request, {count}).empty());
  assert(prefs.writes == 0);
  request.config_preview = false; assert(persist(&request, {64}).empty()); assert(prefs.writes == 1);
  assert(persist(&request, {64}).empty()); assert(prefs.writes == 1);
  request.config_preview = true; assert(persist(&request, {72}).empty()); assert(prefs.writes == 1);
  request.config_preview = false; assert(persist(&request, {72}).empty()); assert(prefs.writes == 2);
  puts("PASS: bounded scans, reconnect pause/resume, connected-link preservation, partial results, clock wraparound, RAM-only LED previews, final config persistence");
}
'''.replace('// DEADLINE', deadline).replace('// PERSISTENCE', persistence).replace('// TIMEOUT', timeout).replace('// PAUSE', pause).replace('// RESUME', resume)
folder = root / '.sdk'; folder.mkdir(exist_ok=True)
cpp = folder / 'test-setup.cpp'; cpp.write_text(fixture)
binary = folder / 'test-setup'
subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
