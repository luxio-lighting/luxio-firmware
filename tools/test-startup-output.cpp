#include "luxio.pb.h"
#include "transition.h"
#include "led_encoder.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define ESP_ERROR_CHECK(call) assert((call) == 0)
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define WL_CONNECTED 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(ms) (ms)
#define constrain(x, lo, hi) std::min(std::max((x), decltype(x)(lo)), decltype(x)(hi))
using gpio_num_t = int;
struct rmt_transmit_config_t {};
struct Symbol { unsigned duration0, level0, duration1, level1; };
struct rmt_bytes_encoder_config_t { Symbol bit0, bit1; struct { bool msb_first; } flags; };
static luxio_State state;
static int stateMutex, lockDepth;
static void xSemaphoreTake(int, int) { assert(lockDepth++ == 0); }
static void xSemaphoreGive(int) { assert(--lockDepth == 0); }
static uint32_t millis() { return 10000; }
static bool identifyActive, dirty, activeRelayInverted, relayPowered;
static int activeRelayGpio = -1, pinLevel = -1;
static void digitalWrite(int, int value) { pinLevel = value; }
static void gpio_set_level(int, int value) { pinLevel = value; }
static void pinMode(int, int) {}
static void delay(int) {}
static uint8_t (*pixels)[6], *ledBytes;
static OutputTransition *transition;
static int ledChannel, ledEncoder;
static std::vector<uint8_t> transmitted;
static bool txFinished;
static int rmt_transmit(int, int, const void *data, size_t length, const rmt_transmit_config_t *) {
  transmitted.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + length);
  txFinished = false; return 0;
}
static int rmt_tx_wait_all_done(int, int timeout) { assert(timeout >= 50); txFinished = true; return 0; }
static void delayMicroseconds(int us) { assert(txFinished && us >= 600); }
static int rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *c, int *) {
  assert(c->flags.msb_first);
  assert(c->bit0.duration0 == 3 && c->bit0.duration1 == 9 && c->bit0.level0 == 1 && c->bit0.level1 == 0);
  assert(c->bit1.duration0 == (!strcmp(state.config.strip_type, "SK6812") ? 6 : 8) && c->bit1.duration1 == (!strcmp(state.config.strip_type, "SK6812") ? 6 : 4) && c->bit1.level0 == 1 && c->bit1.level1 == 0); return 0;
}
static int ledSpi;
struct spi_transaction_t { size_t length; const void *tx_buffer; };
static int spi_device_transmit(int, const spi_transaction_t *transaction) {
  const auto *bytes = static_cast<const uint8_t *>(transaction->tx_buffer);
  const size_t channels = !strcmp(state.config.strip_type, "WS2805") ? 5 : (!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812")) ? 4 : 3;
  const size_t count = state.config.led_count * channels;
  assert(transaction->length % 32 == 0 && transaction->length / 8 == count * 4 + 240);
  transmitted.clear();
  for (size_t i = 0; i < count; i++) {
    uint8_t value = 0;
    for (int bit = 0; bit < 8; bit++) {
      const unsigned symbol = (bytes[i * 4 + bit / 2] >> (bit % 2 == 0 ? 4 : 0)) & 15;
      assert(symbol == 8 || symbol == 12);
      value = uint8_t((value << 1) | (symbol == 12));
    }
    transmitted.push_back(value);
  }
  for (size_t i = count * 4; i < transaction->length / 8; i++) assert(bytes[i] == 0);
  txFinished = true;
  return 0;
}
// PRODUCTION_RENDER

static int step, starts;
static bool disabledScenario;
static std::string pairingCode, pairingQr;
using IPAddress = int;
struct FakeWifi {
  int status() { return step == 2 || step == 4 || step == 6 ? WL_CONNECTED : 0; }
  int localIP() { return step >= 4 ? 123 : 0; }
} WiFi;
struct Finished {};
static void vTaskDelay(int) {
  // Offline, then associated without DHCP, then offline again, then online.
  assert(starts == (LUXIO_MATTER && !disabledScenario && step >= 4 ? 1 : 0));
  if (step++ == 6) throw Finished{};
}
static void stopAnimation() {}
static void applyColor() {}
struct espHsvColor_t { uint8_t h, s, v; };
struct Rgb { uint8_t r, g, b; };
static Rgb espHsvColorToRgbColor(espHsvColor_t) { return {1, 2, 3}; }
struct FakeEndpoint {
  bool begin(bool on, espHsvColor_t, uint32_t brightness, int) {
    assert(WiFi.status() == WL_CONNECTED && WiFi.localIP() != 0);
    assert(!on && brightness == 127 && lockDepth == 0); return true;
  }
  template<class T> void onIdentify(T) {}
  template<class T> void onChangeBrightness(T) {}
  template<class T> void onChangeOnOff(T) {}
  template<class T> void onChangeColorHSV(T) {}
  template<class T> void onChangeColorTemperature(T) {}
} matterLight;
struct { void begin() { assert(lockDepth == 0); starts++; } } Matter;
struct { void *original; } matterDeviceInfo;
namespace chip {
struct PayloadContents {};
struct MutableCharSpan { char *data; template<size_t N> MutableCharSpan(char (&s)[N]):data(s){} };
enum class RendezvousInformationFlag { kOnNetwork };
namespace DeviceLayer {
struct Manager { void LockChipStack(){} void UnlockChipStack(){} };
static Manager &PlatformMgr() { static Manager m; return m; }
static void *GetDeviceInstanceInfoProvider() { return nullptr; }
static void SetDeviceInstanceInfoProvider(void *) {}
}
}
#define CHIP_NO_ERROR 0
static int GetPayloadContents(chip::PayloadContents &, chip::RendezvousInformationFlag) { return 0; }
static void GetManualPairingCode(chip::MutableCharSpan &s, chip::PayloadContents &) { strcpy(s.data, "12345678901"); }
static void GetQRCode(chip::MutableCharSpan &s, chip::PayloadContents &) { strcpy(s.data, "MT:test"); }
// PRODUCTION_STARTUP

int main() {
  for (int driver : {LUXIO_INTERNAL_RAM_RADIOS}) {
  ledSpi = driver;
  for (uint32_t count : {1, 60, 100, 1024}) {
    std::vector<uint8_t> frames(count * 18), packed(count * 20 + 240);
    pixels = reinterpret_cast<uint8_t (*)[6]>(frames.data()); ledBytes = packed.data();
    OutputTransition fade(pixels + count, pixels + count * 2, count); transition = &fade;
    for (uint32_t i = 0; i < count; i++) for (int c = 0; c < 6; c++) pixels[i][c] = (i + c + 1) % 256;
    state.on = true; state.brightness = 256; state.config.led_count = count;
    state.config.has_relay = true; state.config.relay.enabled = true; state.config.relay.gpio = 18;
    for (auto strip : {"WS2805", "WS2814", "WS2812", "SK6812"}) {
      strcpy(state.config.strip_type, strip);
      // PRODUCTION_ENCODING
      render();
      int channels = !strcmp(strip, "WS2805") ? 5 : (!strcmp(strip, "WS2814") || !strcmp(strip, "SK6812")) ? 4 : 3;
      assert(transmitted.size() == count * channels && pinLevel == HIGH);
      for (uint32_t i = 0; i < count; i++) {
        assert(transmitted[i * channels] == pixels[i][channels == 3 || !strcmp(strip, "SK6812") ? 1 : 0]);
        assert(transmitted[i * channels + 1] == pixels[i][channels == 3 || !strcmp(strip, "SK6812") ? 0 : 1]);
        assert(transmitted[i * channels + 2] == pixels[i][2]);
        if (channels == 4) assert(transmitted[i * channels + 3] == pixels[i][3]);
        if (channels == 5) assert(transmitted[i * channels + 3] == pixels[i][4] && transmitted[i * channels + 4] == pixels[i][5]);
      }
    }
    state.on = false; render(); assert(pinLevel == LOW);
    for (uint8_t value : transmitted) assert(value == 0);
  }
  }
  for (bool disabled : {false, true}) for (bool wifi : {false, true}) for (bool matter : {false, true}) {
    state.config.wifi.enabled = wifi; state.config.matter.enabled = matter;
    state.matter_disabled = disabled; disabledScenario = disabled || !wifi || !matter;
    state.on = false; state.brightness = 128; starts = step = lockDepth = 0;
    pairingCode.clear(); pairingQr.clear();
    try { cloudTask(nullptr); } catch (Finished &) {}
    assert(starts == (LUXIO_MATTER && !disabledScenario ? 1 : 0));
    assert(pairingCode.empty() == (!LUXIO_MATTER || disabledScenario));
    assert(!state.on && state.brightness == 128 && lockDepth == 0);
  }
  puts("PASS: RGB/RGBW/RGBCCT output at 1/60/100/1024 LEDs, relay, reset timing, Wi-Fi/IP-gated one-time Matter startup");
}
