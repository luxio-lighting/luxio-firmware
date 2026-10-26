#include "transition.h"
#include "led_encoder.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include "signed_update.h"
#include "script_storage.h"
#include "animation_parameters.h"
#include "gradient.h"
#include "gradient_storage.h"
#include "color_storage.h"
#include "control_error.h"
#include "control_response.h"
#include <memory>
#include <math.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <driver/spi_master.h>
#include <esp_rom_gpio.h>
#include <soc/spi_periph.h>
#include <soc/gpio_sig_map.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include <cJSON.h>
#include "luxio.pb.h"
#include "vendor/nanopb/pb_decode.h"
#include "vendor/nanopb/pb_encode.h"
extern "C" {
#include "vendor/lua/lua.h"
#include "vendor/lua/lauxlib.h"
#include "vendor/lua/lualib.h"
}
#ifndef LUXIO_BUILD
#define LUXIO_BUILD 36
#endif
#ifndef LUXIO_CLOUD_URL
#define LUXIO_CLOUD_URL "http://api.luxio.lighting"
#endif
#ifndef LUXIO_MATTER
#define LUXIO_MATTER 1
#endif
#ifndef LUXIO_INTERNAL_RAM_RADIOS
#define LUXIO_INTERNAL_RAM_RADIOS 0
#endif
#ifndef LUXIO_MATTER_WITHOUT_PSRAM
#define LUXIO_MATTER_WITHOUT_PSRAM 0
#endif
#if LUXIO_MATTER
#include <Matter.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <app/server/OnboardingCodesUtil.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#endif
#if !SOC_WIFI_SUPPORTED || !SOC_BLE_SUPPORTED || !SOC_RMT_SUPPORTED
#error Luxio requires integrated Wi-Fi, Bluetooth LE and RMT.
#endif

static Preferences prefs;
// ESP32's static DRAM region is smaller than its runtime heap. Leave room for
// the Bluetooth controller's fixed allocations alongside Matter.
static luxio_State &state = *new luxio_State{};
static SemaphoreHandle_t stateMutex;
static httpd_handle_t httpServer;
static mbedtls_pk_context identity;
static String publicKey;
static String pairingCode, pairingQr;
static uint8_t (*pixels)[6]; // One allocation contains target, displayed and fade-origin frames.
static uint32_t frameCapacity;
static std::unique_ptr<OutputTransition> transition;
static bool identifyActive = false;
static uint8_t *ledBytes;
static size_t ledByteCapacity;
#if LUXIO_INTERNAL_RAM_RADIOS
static spi_device_handle_t ledSpi;
#endif
static rmt_channel_handle_t ledChannel;
static rmt_encoder_handle_t ledEncoder;
static int activeRelayGpio = -1;
static bool activeRelayInverted, relayPowered;
#ifndef LUXIO_DEFAULT_GPIO
#define LUXIO_DEFAULT_GPIO 16
#endif
static lua_State *animation, *animationThread;
static uint8_t (*animationPixels)[6];
static uint8_t animationPhase;
static bool animationPending, animationFrameReady;
static uint32_t animationFrameAt, luaSliceDeadline, luaCpuRemaining;
static const char *luaBudgetError;
static char *activeAnimationSource;
static size_t luaMemory;
static char animationError[160];
static uint32_t luaDeadline, animationStarted, lastFrame;
static int luaInstructions;
static bool wifiPending, rebootPending;
static uint32_t radioRestartAt;
static String wifiSsid, wifiPassword;
static std::atomic<uint32_t> wifiDisconnectCount{0}, wifiDisconnectReason{0};
static bool wifiScanning, wifiScanPending;
static std::unique_ptr<luxio_WifiNetwork[]> wifiNetworks;
static pb_size_t wifiNetworkCount;
static String wifiScanError;
static uint32_t availableBuild;
static UpdateJob availableUpdate;
static volatile bool checkUpdate;
static bool updateChecking;
static String updateError;

static int hardwareRandom(void *, unsigned char *output, size_t length) { esp_fill_random(output, length); return 0; }
static void stopAnimation() {
  if (animation) lua_close(animation);
  animationThread = nullptr; animationPending = animationFrameReady = false;
  free(animationPixels); animationPixels = nullptr;
  free(activeAnimationSource); activeAnimationSource = nullptr; state.animation_revision = 0;
  state.animation_preview_count = 0; memset(state.animation_preview, 0, sizeof(state.animation_preview));
  state.parameters_count = 0; memset(state.parameters, 0, sizeof(state.parameters));
  animation = nullptr; luaMemory = 0; state.active_color_id[0] = 0; state.script_name[0] = 0; state.active_script_id[0] = 0; state.active_gradient_id[0] = 0; state.has_gradient = false; memset(&state.gradient, 0, sizeof(state.gradient)); state.mode = luxio_Mode_COLOR;
}
static void render() {
  transition->render(pixels, state.config.led_count, state.on ? state.brightness : 0, millis(), identifyActive);
  const auto &relay = state.config.relay;
  const int relayGpio = state.config.has_relay && relay.enabled ? int(relay.gpio) : -1;
  if (relayGpio != activeRelayGpio || relay.inverted != activeRelayInverted) {
    if (activeRelayGpio >= 0) {
      digitalWrite(activeRelayGpio, activeRelayInverted ? HIGH : LOW);
      pinMode(activeRelayGpio, INPUT);
    }
    activeRelayGpio = relayGpio; activeRelayInverted = relay.inverted; relayPowered = false;
    if (activeRelayGpio >= 0) {
      // Set the inactive output latch before enabling output to avoid a pulse.
      gpio_set_level(gpio_num_t(activeRelayGpio), activeRelayInverted ? 1 : 0);
      pinMode(activeRelayGpio, OUTPUT);
    }
  }
  if (activeRelayGpio >= 0) {
    // Finish the off fade before cutting strip power. An off boot stays off.
    const bool powered = state.on || identifyActive || (relayPowered && transition->active);
    if (powered != relayPowered) {
      digitalWrite(activeRelayGpio, powered != activeRelayInverted ? HIGH : LOW);
      relayPowered = powered;
      if (powered) delay(20); // Let the strip power supply settle before data.
    }
  }
  const bool rgbcct = !strcmp(state.config.strip_type, "WS2805");
  const bool rgbw = (!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812"));
  const int channels = rgbcct ? 5 : rgbw ? 4 : 3;
#if LUXIO_INTERNAL_RAM_RADIOS
  const size_t length = encodeLedSpi(transition->displayed, state.config.led_count, channels, ledBytes, !strcmp(state.config.strip_type, "SK6812"));
  spi_transaction_t transaction = {};
  transaction.length = length * 8;
  transaction.tx_buffer = ledBytes;
  ESP_ERROR_CHECK(spi_device_transmit(ledSpi, &transaction));
#else
  size_t position = 0;
  for (uint32_t i = 0; i < state.config.led_count; i++) {
    // WS2805: R,G,B,W1(warm),W2(cool); WS2814: R,G,B,W; SK6812: G,R,B,W; WS2812: G,R,B.
    const bool greenFirst = channels == 3 || !strcmp(state.config.strip_type, "SK6812");
    const int order[5] = {greenFirst ? 1 : 0, greenFirst ? 0 : 1, 2, rgbcct ? 4 : 3, 5};
    for (int c = 0; c < channels; c++) {
      ledBytes[position++] = transition->displayed[i][order[c]];
    }
  }
  // Encode bytes into the peripheral's small symbol memory while transmitting.
  // Keep the payload alive and unchanged until the transaction has finished.
  rmt_transmit_config_t transmit = {};
  ESP_ERROR_CHECK(rmt_transmit(ledChannel, ledEncoder, ledBytes, position, &transmit));
  ESP_ERROR_CHECK(rmt_tx_wait_all_done(ledChannel, 100));
  // EOT holds the output low. Preserve the previous 600 us reset/latch interval.
  delayMicroseconds(600);
#endif
}
static void applyColor() {
  transition->begin(millis());
  const auto &c = state.color;
  for (uint32_t i = 0; i < state.config.led_count; i++) {
    pixels[i][0] = c.red; pixels[i][1] = c.green; pixels[i][2] = c.blue;
    pixels[i][3] = c.white; pixels[i][4] = c.warm_white; pixels[i][5] = c.cool_white;
  }
}
static void *luaAllocator(void *, void *ptr, size_t oldSize, size_t newSize) {
  if (!ptr) oldSize = 0;
  if (!newSize) { free(ptr); luaMemory -= oldSize; return nullptr; }
  if (luaMemory - oldSize + newSize > 131072) return nullptr;
  // Lua creates many small allocations, which the default allocator puts in
  // internal RAM. Keep the VM in PSRAM so Wi-Fi/Matter retain their RX buffers.
  // Do not fall back to internal RAM when a PSRAM-equipped board exhausts it.
  void *result = psramFound() ? ps_realloc(ptr, newSize) : realloc(ptr, newSize);
  if (result) luaMemory = luaMemory - oldSize + newSize;
  return result;
}
static void luaHook(lua_State *L, lua_Debug *debug) {
  if (debug) luaInstructions -= 1000;
  if (luaInstructions <= 0) luaBudgetError = "Animation exceeded its instruction limit (1,000,000 per call)";
  // Checkpoint this task's CPU time, excluding time spent preempted by radios.
  taskYIELD(); TaskStatus_t current;
  vTaskGetInfo(nullptr, &current, pdFALSE, eInvalid);
  const uint32_t now = uint32_t(current.ulRunTimeCounter);
  if (int32_t(now - luaDeadline) >= 0) luaBudgetError = "Animation exceeded its CPU limit (500 ms per call)";
  // Yield instead of raising an error that a script could swallow with pcall.
  // The owner checks hard limits after resume and can destroy the entire VM.
  if (lua_isyieldable(L) && (luaBudgetError || int32_t(now - luaSliceDeadline) >= 0)) {
    lua_yield(L, 0);
    return;
  }
  if (luaBudgetError) luaL_error(L, "%s", luaBudgetError);
}
static int luaRenderWaves(lua_State *L, int, lua_KContext start) {
  // Small, bounded descriptors on the stack; pixel storage is never duplicated.
  struct WaveTerm { lua_Number phase, step, amplitude; };
  struct Wave { lua_Number offset; int count, positive; struct WaveTerm terms[4]; } waves[4] = {0};
  struct MixTerm { lua_Number gain; int wave, power; };
  struct Channel { lua_Number offset; int count, multiply; struct MixTerm terms[4]; } channels[6] = {0};
  luaL_checktype(L, 1, LUA_TTABLE); luaL_checktype(L, 2, LUA_TTABLE);
  size_t waveCount = lua_rawlen(L, 1);
  if (waveCount < 1 || waveCount > 4) return luaL_error(L, "Use 1-4 waves");
  if (lua_rawlen(L, 2) > 6) return luaL_error(L, "Use at most six color channels");
  for (size_t w = 0; w < waveCount; w++) {
      lua_rawgeti(L, 1, w + 1); luaL_checktype(L, -1, LUA_TTABLE);
      lua_getfield(L, -1, "offset"); waves[w].offset = luaL_optnumber(L, -1, 0); lua_pop(L, 1);
      if (!isfinite(waves[w].offset) || waves[w].offset < -4 || waves[w].offset > 4)
          return luaL_error(L, "Wave offset must be finite and between -4 and 4");
      lua_getfield(L, -1, "positive");
      if (!lua_isnil(L, -1) && !lua_isboolean(L, -1)) return luaL_error(L, "positive must be a boolean");
      waves[w].positive = lua_toboolean(L, -1); lua_pop(L, 1);
      size_t length = lua_rawlen(L, -1);
      if (length > 4) return luaL_error(L, "Use at most four sine terms per wave");
      waves[w].count = (int)length;
      for (size_t t = 0; t < length; t++) {
          lua_rawgeti(L, -1, t + 1); luaL_checktype(L, -1, LUA_TTABLE);
          const char *keys[] = {"phase", "step", "amplitude"};
          lua_Number values[3];
          for (int k = 0; k < 3; k++) {
              lua_getfield(L, -1, keys[k]); values[k] = luaL_optnumber(L, -1, k == 2 ? 1 : 0); lua_pop(L, 1);
              lua_Number limit = k == 2 ? 4 : (lua_Number)1e12;
              if (!isfinite(values[k]) || values[k] < -limit || values[k] > limit)
                  return luaL_error(L, "Invalid sine phase, step or amplitude");
          }
          waves[w].terms[t].phase = values[0]; waves[w].terms[t].step = values[1]; waves[w].terms[t].amplitude = values[2];
          lua_pop(L, 1);
      }
      lua_pop(L, 1);
  }
  for (int c = 0; c < 6; c++) {
      lua_rawgeti(L, 2, c + 1);
      if (lua_isnil(L, -1)) { lua_pop(L, 1); continue; }
      luaL_checktype(L, -1, LUA_TTABLE);
      lua_getfield(L, -1, "offset"); channels[c].offset = luaL_optnumber(L, -1, 0); lua_pop(L, 1);
      if (!isfinite(channels[c].offset) || channels[c].offset < -1024 || channels[c].offset > 1024)
          return luaL_error(L, "Channel offset must be finite and between -1024 and 1024");
      lua_getfield(L, -1, "multiply"); lua_Integer multiply = luaL_optinteger(L, -1, 0); lua_pop(L, 1);
      if (multiply < 0 || multiply > (lua_Integer)waveCount) return luaL_error(L, "Invalid modulation wave index");
      channels[c].multiply = (int)multiply;
      size_t length = lua_rawlen(L, -1);
      if (length > 4) return luaL_error(L, "Use at most four terms per color channel");
      channels[c].count = (int)length;
      if (((!strcmp(state.config.strip_type, "WS2805") && c == 3) || ((!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812")) && c >= 4) || (!strcmp(state.config.strip_type, "WS2812") && c >= 3)) && (length || channels[c].offset != 0))
          return luaL_error(L, "Channel unavailable on this strip type");
      for (size_t t = 0; t < length; t++) {
          lua_rawgeti(L, -1, t + 1); luaL_checktype(L, -1, LUA_TTABLE);
          lua_getfield(L, -1, "wave"); lua_Integer wave = luaL_checkinteger(L, -1); lua_pop(L, 1);
          lua_getfield(L, -1, "power"); lua_Integer power = luaL_optinteger(L, -1, 1); lua_pop(L, 1);
          if (wave < 1 || wave > (lua_Integer)waveCount || power < 1 || power > 16)
              return luaL_error(L, "Wave index must exist; power must be an integer from 1 to 16");
          lua_getfield(L, -1, "gain"); lua_Number gain = luaL_optnumber(L, -1, 255); lua_pop(L, 1);
          if (!isfinite(gain) || gain < -1024 || gain > 1024) return luaL_error(L, "Gain must be finite and between -1024 and 1024");
          channels[c].terms[t].wave = (int)wave - 1; channels[c].terms[t].power = (int)power; channels[c].terms[t].gain = gain;
          lua_pop(L, 1);
      }
      lua_pop(L, 1);
  }
  // Finish validation before touching any pixels. Quantize only after mixing.
  for (int i = (int)start; i < state.config.led_count; i++) {
      lua_Number values[4];
      for (size_t w = 0; w < waveCount; w++) {
          lua_Number value = waves[w].offset;
          for (int t = 0; t < waves[w].count; t++) {
              const struct WaveTerm *term = &waves[w].terms[t];
              value += term->amplitude * l_mathop(sin)(term->phase + i * term->step);
          }
          lua_Number minimum = waves[w].positive ? 0 : -1;
          values[w] = value < minimum ? minimum : value > 1 ? 1 : value;
      }
      for (int c = 0; c < 6; c++) {
          lua_Number value = channels[c].offset;
          for (int t = 0; t < channels[c].count; t++) {
              const struct MixTerm *term = &channels[c].terms[t];
              lua_Number base = values[term->wave], powered = 1;
              // Integer powers use multiplies instead of a general pow call.
              for (int p = term->power; p; p >>= 1) { if (p & 1) powered *= base; if (p > 1) base *= base; }
              value += term->gain * powered;
          }
          value = value < 0 ? 0 : value > 255 ? 255 : value;
          if (channels[c].multiply) value *= values[channels[c].multiply - 1];
          animationPixels[i][c] = (uint8_t)(value < 0 ? 0 : value); // Clamp above, then floor by truncation.
      }
      if ((i + 1) % 32 == 0 && i + 1 < state.config.led_count) {
          // Bound uninterrupted work. Lua keeps the input tables alive across
          // this continuation; reparse them on resume rather than save a frame
          // or a descriptor allocation. Only completed frames are published.
          taskYIELD(); TaskStatus_t current;
          vTaskGetInfo(nullptr, &current, pdFALSE, eInvalid);
          const uint32_t now = uint32_t(current.ulRunTimeCounter);
          if (int32_t(now - luaDeadline) >= 0) luaBudgetError = "Animation exceeded its CPU limit (500 ms per call)";
          if (lua_isyieldable(L) && (luaBudgetError || int32_t(now - luaSliceDeadline) >= 0))
              return lua_yieldk(L, 0, i + 1, luaRenderWaves);
          if (luaBudgetError) return luaL_error(L, "%s", luaBudgetError);
      }
  }
  // Native work does not trigger Lua instruction hooks. Check CPU time here
  // so repeated bulk calls cannot postpone cooperative yielding/hard limits.
  luaHook(L, nullptr);
  return 0;
}
static String startAnimation(luxio_Script &script) {
  std::unique_ptr<luxio_AnimationParameter[]> parameters(new (std::nothrow) luxio_AnimationParameter[16]{}); pb_size_t count = 0;
  if (!parameters) return "Not enough memory for animation parameters";
  const char *parameterError = parseAnimationParameters(script.source, parameters.get(), count);
  if (!parameterError) parameterError = updateAnimationParameters(parameters.get(), count, script.parameters, script.parameters_count);
  std::unique_ptr<luxio_Color[]> preview(new (std::nothrow) luxio_Color[8]{}); pb_size_t previewCount = 0;
  if (!preview) return "Not enough memory for animation preview";
  if (!parameterError) parameterError = parseAnimationPreview(script.source, preview.get(), previewCount);
  if (parameterError) return parameterError;
  char *source = script.source;
  script.source = nullptr; // Transfer the decoded source instead of copying it.
  stopAnimation();
  activeAnimationSource = source;
  state.animation_preview_count = previewCount; memcpy(state.animation_preview, preview.get(), sizeof(state.animation_preview));
  animationError[0] = 0;
  state.parameters_count = count; memcpy(state.parameters, parameters.get(), sizeof(state.parameters));
#if LUXIO_INTERNAL_RAM_RADIOS
  animationPixels = static_cast<uint8_t (*)[6]>(heap_caps_calloc(state.config.led_count, 6, MALLOC_CAP_INTERNAL | MALLOC_CAP_IRAM_8BIT));
#else
  animationPixels = static_cast<uint8_t (*)[6]>(psramFound() ? ps_calloc(state.config.led_count, 6) : calloc(state.config.led_count, 6));
#endif
  if (!animationPixels) { stopAnimation(); return "Not enough memory for the animation frame"; }
  animation = lua_newstate(luaAllocator, nullptr);
  if (!animation) { stopAnimation(); return "Unable to allocate Lua engine"; }
  lua_State *L = animation;
  // Library registration allocates too; protect it so low RAM returns an error
  // instead of invoking Lua's panic handler and rebooting the controller.
  lua_pushcfunction(L, [](lua_State *L) -> int {
    animationThread = lua_newthread(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "luxio.loop"); // Keep the coroutine alive until VM teardown.
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
    for (const char *name : {"dofile", "loadfile", "load", "collectgarbage", "print"}) { lua_pushnil(L); lua_setglobal(L, name); }
    lua_pushcfunction(L, [](lua_State *L) -> int { lua_pushinteger(L, state.config.led_count); return 1; }); lua_setglobal(L, "luxio_get_pixel_count");
    lua_pushcfunction(L, [](lua_State *L) -> int {
      lua_pushstring(L, !strcmp(state.config.strip_type, "WS2805") ? "RGBCCT" : (!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812")) ? "RGBW" : "RGB");
      return 1;
    }); lua_setglobal(L, "luxio_get_color_type");
    lua_pushcfunction(L, [](lua_State *L) -> int { lua_pushinteger(L, millis() - animationStarted); return 1; }); lua_setglobal(L, "luxio_millis");
    lua_pushcfunction(L, [](lua_State *L) -> int {
      int index = luaL_checkinteger(L, 1);
      if (index < 0 || index >= int(state.config.led_count)) return luaL_error(L, "Pixel index out of range (zero-based)");
      int values[6];
      for (int c = 0; c < 6; c++) { values[c] = luaL_optinteger(L, c + 2, 0); if (values[c] < 0 || values[c] > 255) return luaL_error(L, "Channels must be 0-255"); }
      if ((!strcmp(state.config.strip_type, "WS2805") && values[3]) || ((!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812")) && (values[4] || values[5])) || (!strcmp(state.config.strip_type, "WS2812") && (values[3] || values[4] || values[5]))) return luaL_error(L, "Channel unavailable on this strip type");
      for (int c = 0; c < 6; c++) animationPixels[index][c] = values[c];
      return 0;
    }); lua_setglobal(L, "luxio_set_pixel_color");
    // BULK_API_BEGIN
    // Bounded native loops write the existing working frame; no extra LED array.
    for (bool ranged : {false, true}) {
      lua_pushboolean(L, ranged);
      lua_pushcclosure(L, [](lua_State *L) -> int {
        const int ranged = lua_toboolean(L, lua_upvalueindex(1));
        const lua_Integer start = ranged ? luaL_checkinteger(L, 1) : 0;
        const lua_Integer length = ranged ? luaL_checkinteger(L, 2) : state.config.led_count;
        if (start < 0 || start > state.config.led_count || length < 0 || length > state.config.led_count - start)
            return luaL_error(L, "Pixel range out of bounds (zero-based start, count)");
        uint8_t values[6];
        for (int c = 0; c < 6; c++) {
            lua_Integer value = luaL_optinteger(L, c + (ranged ? 3 : 1), 0);
            if (value < 0 || value > 255) return luaL_error(L, "Channels must be 0-255");
            values[c] = (uint8_t)value;
        }
        if ((!strcmp(state.config.strip_type, "WS2805") && values[3]) || ((!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812")) && (values[4] || values[5])) || (!strcmp(state.config.strip_type, "WS2812") && (values[3] || values[4] || values[5]))) return luaL_error(L, "Channel unavailable on this strip type");
        for (lua_Integer i = start; i < start + length; i++) memcpy(animationPixels[i], values, 6);
        return 0;
      }, 1);
      lua_setglobal(L, ranged ? "luxio_fill_range" : "luxio_fill");
    }
    lua_pushcclosure(L, [](lua_State *L) -> int {
      lua_Number hue = luaL_checknumber(L, 1), step = luaL_optnumber(L, 2, 0);
      lua_Number saturation = luaL_optnumber(L, 3, 1), value = luaL_optnumber(L, 4, 1);
      if (!isfinite(hue) || !isfinite(step) || !isfinite(saturation) || !isfinite(value) ||
          saturation < 0 || saturation > 1 || value < 0 || value > 1)
          return luaL_error(L, "HSV requires finite hue/step and saturation/value from 0 to 1");
      hue -= l_mathop(floor)(hue); step -= l_mathop(floor)(step);
      const int high = (int)l_mathop(floor)(255 * value);
      const int low = (int)l_mathop(floor)(255 * value * (1 - saturation));
      for (int i = 0; i < state.config.led_count; i++) {
          if (step == 0 && i > 0) { memcpy(animationPixels[i], animationPixels[0], 6); continue; }
          lua_Number h = hue + i * step;
          h = (h - l_mathop(floor)(h)) * 6;
          const int sector = (int)l_mathop(floor)(h);
          const int rising = (int)l_mathop(floor)((high - low) * (h - sector));
          const int up = low + rising, down = high - rising;
          uint8_t rgb[6] = {0};
          switch (sector) {
              case 0: rgb[0] = high; rgb[1] = up; rgb[2] = low; break;
              case 1: rgb[0] = down; rgb[1] = high; rgb[2] = low; break;
              case 2: rgb[0] = low; rgb[1] = high; rgb[2] = up; break;
              case 3: rgb[0] = low; rgb[1] = down; rgb[2] = high; break;
              case 4: rgb[0] = up; rgb[1] = low; rgb[2] = high; break;
              default: rgb[0] = high; rgb[1] = low; rgb[2] = down; break;
          }
          memcpy(animationPixels[i], rgb, 6);
      }
      return 0;
    }, 0); lua_setglobal(L, "luxio_fill_hsv");
    lua_pushcclosure(L, [](lua_State *L) -> int { return luaRenderWaves(L, LUA_OK, 0); }, 0); lua_setglobal(L, "luxio_render_waves");
    // BULK_API_END
    lua_pushcfunction(L, [](lua_State *L) -> int {
      const char *name = luaL_checkstring(L, 1);
      for (size_t i = 0; i < state.parameters_count; ++i) {
        const auto &p = state.parameters[i];
        if (!strcmp(p.name, name)) {
          if (p.boolean) lua_pushboolean(L, p.value != 0); else lua_pushnumber(L, p.value);
          return 1;
        }
      }
      return luaL_error(L, "Unknown animation parameter: %s", name);
    }); lua_setglobal(L, "luxio_get_parameter");
    return 0;
  });
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    String error = lua_tostring(L, -1); stopAnimation(); return error;
  }
  // Compile synchronously so syntax errors still reject SET_SCRIPT. User code,
  // setup(), and loop() all execute cooperatively outside the request handler.
  if (luaL_loadbuffer(animationThread, source, strlen(source), "Luxio animation") != LUA_OK) {
    String error = lua_tostring(animationThread, -1); stopAnimation(); return error;
  }
  lua_sethook(animationThread, luaHook, LUA_MASKCOUNT, 1000);
  luaInstructions = 1000000; luaCpuRemaining = 500000; luaBudgetError = nullptr;
  animationStarted = millis(); animationFrameAt = animationStarted;
  animationPhase = 0; animationPending = true;
  transition->begin(millis());
  state.animation_revision = esp_random() | 1;
  return "";
}
static bool validId(const char *id) {
  if (strlen(id) != 36) return false;
  for (int i = 0; i < 36; i++) if ((i == 8 || i == 13 || i == 18 || i == 23) ? id[i] != '-' : !isxdigit(id[i])) return false;
  return true;
}
struct ScriptPage { uint32_t offset; uint32_t *next; };
// Save output only for a firmware update, never for ordinary lighting changes.
// Stream the Lua source directly to flash: do not allocate another 16 KiB copy.
static bool saveUpdateState() {
  auto snapshot = std::unique_ptr<luxio_Response>(new (std::nothrow) luxio_Response{});
  std::unique_ptr<luxio_Script> script;
  if (!snapshot) return false;
  snapshot->state = &state;
  if (animation && activeAnimationSource && state.mode == luxio_Mode_ANIMATION) {
    script.reset(new (std::nothrow) luxio_Script{});
    if (!script) return false;
    strlcpy(script->id, state.active_script_id, sizeof(script->id));
    strlcpy(script->name, state.script_name, sizeof(script->name));
    script->source = activeAnimationSource; script->mode = luxio_Mode_ANIMATION;
    // Active parameters already live in State; copy them back before booting Lua.
    snapshot->script = script.get();
  }
  File file = LittleFS.open("/startup.pending", "w");
  const uint8_t header[8] = {'L', 'X', 'B', '1', 0, 0, 0, 0};
  if (!file || file.write(header, sizeof(header)) != sizeof(header)) { file.close(); LittleFS.remove("/startup.pending"); return false; }
  struct Writer { File *file; uint32_t crc; } writer{&file, 0xffffffff};
  pb_ostream_t output = {};
  output.state = &writer; output.max_size = 32768;
  output.callback = [](pb_ostream_t *stream, const pb_byte_t *bytes, size_t count) -> bool {
    auto *writer = static_cast<Writer *>(stream->state);
    if (writer->file->write(bytes, count) != count) return false;
    for (size_t i = 0; i < count; i++) {
      writer->crc ^= bytes[i];
      for (int bit = 0; bit < 8; bit++) writer->crc = (writer->crc >> 1) ^ (0xedb88320u & (0u - (writer->crc & 1)));
    }
    return true;
  };
  bool written = pb_encode(&output, luxio_Response_fields, snapshot.get());
  uint32_t crc = ~writer.crc;
  const uint8_t checksum[4] = {uint8_t(crc), uint8_t(crc >> 8), uint8_t(crc >> 16), uint8_t(crc >> 24)};
  written = written && file.seek(4) && file.write(checksum, 4) == 4;
  file.flush(); file.close();
  // Verify the complete payload before atomically replacing the previous boot state.
  File verify = LittleFS.open("/startup.pending", "r");
  written = written && verify.size() == output.bytes_written + 8 && verify.seek(8);
  uint8_t bytes[64]; uint32_t readCrc = 0xffffffff; size_t remaining = output.bytes_written;
  while (written && remaining) {
    size_t count = min(remaining, sizeof(bytes));
    if (verify.read(bytes, count) != count) { written = false; break; }
    for (size_t i = 0; i < count; i++) {
      readCrc ^= bytes[i];
      for (int bit = 0; bit < 8; bit++) readCrc = (readCrc >> 1) ^ (0xedb88320u & (0u - (readCrc & 1)));
    }
    remaining -= count;
  }
  verify.close();
  if (!written || ~readCrc != crc || !LittleFS.rename("/startup.pending", "/startup.pb")) { LittleFS.remove("/startup.pending"); return false; }
  return true;
}

static bool listScripts(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  auto &page = *static_cast<ScriptPage *>(*arg);
  *page.next = 0;
  File directory = LittleFS.open("/scripts");
  if (!directory) return true;
  uint32_t index = 0, count = 0;
  for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
    if (!String(file.name()).endsWith(".pb")) continue;
    if (index++ < page.offset) continue;
    if (count == 64) { *page.next = index - 1; break; }
    ScriptPtr script(new (std::nothrow) luxio_Script{});
    const size_t length = file.size();
    if (!script || !length || length > SCRIPT_RECORD_MAX) return false;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[length]);
    if (!bytes || file.read(bytes.get(), length) != length || !decodeStoredScript(bytes.get(), length, *script, true)) return false;
    if (script->mode == luxio_Mode_GRADIENT) continue;
    script->parameters_count = 0; // Listings contain names/IDs only; GET_SCRIPT reads values.
    if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, luxio_Script_fields, script.get())) return false;
    count++;
  }
  return true;
}
static bool listGradients(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  auto &page = *static_cast<ScriptPage *>(*arg);
  *page.next = 0;
  File directory = LittleFS.open("/gradients");
  if (!directory) return true;
  uint32_t index = 0, count = 0;
  for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
    if (!String(file.name()).endsWith(".pb")) continue;
    if (index++ < page.offset) continue;
    if (count == 64) { *page.next = index - 1; break; }
    std::unique_ptr<luxio_Gradient> gradient(new (std::nothrow) luxio_Gradient{});
    const size_t length = file.size();
    if (!gradient || length > GRADIENT_RECORD_MAX || !length) return false;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[length]);
    if (!bytes || file.read(bytes.get(), length) != length || !decodeStoredGradient(bytes.get(), length, *gradient)) return false;
    gradient->colors_count = 0; gradient->editor_metadata[0] = 0;
    if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, luxio_Gradient_fields, gradient.get())) return false;
    count++;
  }
  return true;
}
static bool listColors(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  auto &page = *static_cast<ScriptPage *>(*arg);
  *page.next = 0;
  File directory = LittleFS.open("/colors");
  if (!directory) return true;
  uint32_t index = 0, count = 0;
  for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
    if (!String(file.name()).endsWith(".pb")) continue;
    if (index++ < page.offset) continue;
    if (count == 64) { *page.next = index - 1; break; }
    std::unique_ptr<luxio_SavedColor> color(new (std::nothrow) luxio_SavedColor{});
    const size_t length = file.size();
    if (!color || length > COLOR_RECORD_MAX || !length) return false;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[length]);
    if (!bytes || file.read(bytes.get(), length) != length || !decodeStoredColor(bytes.get(), length, *color)) return false;
    color->has_color = false;
    if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, luxio_SavedColor_fields, color.get())) return false;
    count++;
  }
  return true;
}
size_t dispatch(const uint8_t *input, size_t length, std::unique_ptr<uint8_t[]> &output, size_t prefix) {
  output.reset();
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  auto *request = new (std::nothrow) luxio_Request{};
  auto *response = new (std::nothrow) luxio_Response{};
  if (!request || !response) { delete request; delete response; xSemaphoreGive(stateMutex); return 0; }
  ScriptPtr responseScript;
  std::unique_ptr<luxio_Gradient> responseGradient;

  auto decoder = pb_istream_from_buffer(input, length);
  bool decoded = pb_decode(&decoder, luxio_Request_fields, request);
  response->id = request->id;
  String error;
  ScriptPage page{request->script_offset, &response->next_script_offset};
  ScriptPage gradientPage{request->gradient_offset, &response->next_gradient_offset};
  ScriptPage colorPage{request->color_offset, &response->next_color_offset};
  if (!decoded) error = !strcmp(PB_GET_ERROR(&decoder), "realloc failed") ? "Not enough memory to decode request" : "Invalid Protobuf request";
  else if (request->operation == luxio_Operation_CREATE_SCRIPT || request->operation == luxio_Operation_UPDATE_SCRIPT ||
      request->operation == luxio_Operation_GET_SCRIPT || request->operation == luxio_Operation_RUN_SCRIPT) {
    responseScript.reset(new (std::nothrow) luxio_Script{});
    if (!responseScript) error = "Not enough memory for this animation operation";
  }
  if (decoded && request->script && request->script->source &&
      strnlen(request->script->source, SCRIPT_SOURCE_MAX + 1) > SCRIPT_SOURCE_MAX) error = "Animation source exceeds 16 KiB";
  if (decoded && (request->operation == luxio_Operation_CREATE_GRADIENT || request->operation == luxio_Operation_UPDATE_GRADIENT ||
      request->operation == luxio_Operation_GET_GRADIENT || request->operation == luxio_Operation_RUN_GRADIENT)) {
    responseGradient.reset(new (std::nothrow) luxio_Gradient{});
    if (!responseGradient) error = "Not enough memory for this gradient operation";
  }
  if (decoded && !error.length()) switch (request->operation) {
    case luxio_Operation_GET_STATE: break;
    case luxio_Operation_SET_COLOR: {
      auto &c = request->color;
      if (!request->has_color || c.red > 255 || c.green > 255 || c.blue > 255 || c.white > 255 || c.warm_white > 255 || c.cool_white > 255) { error = "Invalid color"; break; }
      if ((!strcmp(state.config.strip_type, "WS2805") && c.white) || ((!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812")) && (c.warm_white || c.cool_white)) || (!strcmp(state.config.strip_type, "WS2812") && (c.white || c.warm_white || c.cool_white))) { error = "Channel unavailable on this strip type"; break; }
      stopAnimation(); state.color = c; state.on = true; applyColor(); break;
    }
    case luxio_Operation_SET_BRIGHTNESS:
      if (request->brightness < 1 || request->brightness > 256) error = "Brightness must be 1-256";
      else { if (state.brightness != request->brightness) transition->begin(millis()); state.brightness = request->brightness; }
      break;
    case luxio_Operation_SET_POWER:
      if (state.on != request->on) transition->begin(millis());
      state.on = request->on; break;
    case luxio_Operation_SET_CONFIG: {
      auto &c = request->config;
      String name = c.name; name.trim();
      // Old clients do not know this field; ordinary settings edits retain it.
      if (!c.has_wifi) { c.has_wifi = true; c.wifi = state.config.wifi; }
      if (!c.has_matter) { c.has_matter = true; c.matter = state.config.matter; }
      if (radioRestartAt) { error = "Radio settings are restarting the device"; break; }
      if (!c.has_relay) { c.has_relay = state.config.has_relay; c.relay = state.config.relay; }
      if (!request->has_config || !name.length() || c.led_count < 1 || c.led_count > 1024 || c.channel > 100 || (!GPIO_IS_VALID_OUTPUT_GPIO(c.gpio)) || (strcmp(c.strip_type, "WS2805") && strcmp(c.strip_type, "WS2814") && strcmp(c.strip_type, "WS2812") && strcmp(c.strip_type, "SK6812"))) { error = "Invalid strip configuration"; break; }
      if (c.has_relay && c.relay.enabled && (c.relay.gpio == c.gpio || !GPIO_IS_VALID_OUTPUT_GPIO(c.relay.gpio))) { error = "Relay GPIO must be a separate valid output pin"; break; }
      // Reserve the active serial interfaces and the chip's flash/PSRAM pins.
      const uint32_t pins[] = {c.gpio, c.has_relay && c.relay.enabled ? c.relay.gpio : c.gpio};
      for (uint32_t pin : pins) {
      if (pin == TX || pin == RX) { error = "GPIO is reserved for serial"; break; }
#if CONFIG_IDF_TARGET_ESP32
      if ((pin >= 6 && pin <= 11) || (psramFound() && (pin == 16 || pin == 17))) { error = "GPIO is reserved for flash or PSRAM"; break; }
#elif CONFIG_IDF_TARGET_ESP32S3
      if ((pin >= 26 && pin <= 37) || pin == 19 || pin == 20) { error = "GPIO is reserved for flash, PSRAM, or USB"; break; }
#elif CONFIG_IDF_TARGET_ESP32C3
      if (pin >= 12 && pin <= 19) { error = "GPIO is reserved for flash or USB"; break; }
#elif CONFIG_IDF_TARGET_ESP32C6
      if (pin >= 24 || pin == 12 || pin == 13) { error = "GPIO is reserved for flash or USB"; break; }
#elif CONFIG_IDF_TARGET_ESP32C5
      if ((pin >= 13 && pin <= 18) || (pin >= 20 && pin <= 22)) { error = "GPIO is reserved for flash or USB"; break; }
#endif
      }
      if (error.length()) break;
      size_t needed = c.led_count * 5;
#if LUXIO_INTERNAL_RAM_RADIOS
      needed = c.led_count * 20 + 240;
#endif
      if (needed > ledByteCapacity) {
        auto *replacement = static_cast<uint8_t *>(heap_caps_realloc(ledBytes, needed, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | (LUXIO_INTERNAL_RAM_RADIOS ? MALLOC_CAP_DMA : 0)));
        if (!replacement) { error = "Not enough memory for this LED count"; break; }
        ledBytes = replacement; ledByteCapacity = needed;
      }
      if (c.led_count > frameCapacity) {
#if LUXIO_INTERNAL_RAM_RADIOS
        // SPI DMA reads its separate DRAM payload, so rendering frames can use
        // the SDK's byte-accessible IRAM pool without an ISR reading them.
        auto *replacement = static_cast<uint8_t (*)[6]>(heap_caps_calloc(c.led_count * 3, 6, MALLOC_CAP_INTERNAL | MALLOC_CAP_IRAM_8BIT));
#else
        auto *replacement = static_cast<uint8_t (*)[6]>(psramFound() ? ps_calloc(c.led_count * 3, 6) : calloc(c.led_count * 3, 6));
#endif
        if (!replacement) { error = "Not enough memory for this LED count"; break; }
        memcpy(replacement, pixels, frameCapacity * 6);
        memcpy(replacement + c.led_count, transition->displayed, frameCapacity * 6);
        memcpy(replacement + c.led_count * 2, transition->origin, frameCapacity * 6);
        free(pixels); pixels = replacement; frameCapacity = c.led_count;
        transition->displayed = pixels + frameCapacity; transition->origin = pixels + frameCapacity * 2; transition->capacity = frameCapacity;
      }
      if (c.gpio != state.config.gpio) {
#if LUXIO_INTERNAL_RAM_RADIOS
        gpio_set_level(gpio_num_t(state.config.gpio), 0);
        esp_rom_gpio_connect_out_signal(state.config.gpio, SIG_GPIO_OUT_IDX, false, false);
        gpio_set_direction(gpio_num_t(state.config.gpio), GPIO_MODE_INPUT);
        gpio_set_level(gpio_num_t(c.gpio), 0);
        gpio_set_direction(gpio_num_t(c.gpio), GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(c.gpio, spi_periph_signal[SPI3_HOST].spid_out, false, false);
#else
        rmt_tx_channel_config_t channel = {};
        channel.gpio_num = gpio_num_t(c.gpio); channel.clk_src = RMT_CLK_SRC_DEFAULT;
        channel.resolution_hz = 10000000;
        channel.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
        channel.trans_queue_depth = 1;
        rmt_channel_handle_t replacement = nullptr;
        if (rmt_new_tx_channel(&channel, &replacement) != ESP_OK) { error = "Unable to initialize this GPIO"; break; }
        if (rmt_enable(replacement) != ESP_OK) { rmt_del_channel(replacement); error = "Unable to enable this GPIO"; break; }
        ESP_ERROR_CHECK(rmt_disable(ledChannel)); ESP_ERROR_CHECK(rmt_del_channel(ledChannel));
        ledChannel = replacement;
#endif
      }
      if (activeRelayGpio >= 0 && (!c.relay.enabled || int(c.relay.gpio) != activeRelayGpio || c.relay.inverted != activeRelayInverted)) {
        digitalWrite(activeRelayGpio, activeRelayInverted ? HIGH : LOW);
        pinMode(activeRelayGpio, INPUT); activeRelayGpio = -1; relayPowered = false;
      }
      if (c.gpio != state.config.gpio || c.led_count != state.config.led_count || strcmp(c.strip_type, state.config.strip_type)) {
        // Configuration after first boot must not discard the default gradient.
        // Keep static output whenever its channels remain valid for the new strip.
        if (state.mode == luxio_Mode_GRADIENT && state.has_gradient && validGradient(state.gradient, c.strip_type)) {
          renderGradient(state.gradient, pixels, c.led_count);
        } else {
          stopAnimation();
          if (!validColor(state.color, c.strip_type)) state.color = {0, 210, 255, 0, 0, 0};
          for (uint32_t i = 0; i < c.led_count; i++) {
            pixels[i][0] = state.color.red; pixels[i][1] = state.color.green; pixels[i][2] = state.color.blue;
            pixels[i][3] = state.color.white; pixels[i][4] = state.color.warm_white; pixels[i][5] = state.color.cool_white;
          }
        }
        memset(transition->displayed, 0, frameCapacity * 6); transition->active = false;
      }
      const bool radioChanged = c.wifi.enabled != state.config.wifi.enabled || c.matter.enabled != state.config.matter.enabled;
#if !LUXIO_INTERNAL_RAM_RADIOS
      if (!strcmp(c.strip_type, "SK6812") != !strcmp(state.config.strip_type, "SK6812")) {
        rmt_bytes_encoder_config_t encoding = {};
        encoding.bit0 = {.duration0 = 3, .level0 = 1, .duration1 = 9, .level1 = 0};
        encoding.bit1 = {.duration0 = uint16_t(!strcmp(c.strip_type, "SK6812") ? 6 : 8), .level0 = 1, .duration1 = uint16_t(!strcmp(c.strip_type, "SK6812") ? 6 : 4), .level1 = 0};
        encoding.flags.msb_first = 1;
        ESP_ERROR_CHECK(rmt_bytes_encoder_update_config(ledEncoder, &encoding));
      }
#endif
      strlcpy(c.name, name.c_str(), sizeof(c.name));
      uint8_t saved[luxio_Config_size], previous[luxio_Config_size];
      auto encoder = pb_ostream_from_buffer(saved, sizeof(saved));
      if (!pb_encode(&encoder, luxio_Config_fields, &c)) { error = "Unable to encode settings"; break; }
      const size_t previousSize = prefs.getBytesLength("config");
      const bool unchanged = previousSize == encoder.bytes_written && prefs.getBytes("config", previous, previousSize) == previousSize && !memcmp(previous, saved, previousSize);
      if (!unchanged && prefs.putBytes("config", saved, encoder.bytes_written) != encoder.bytes_written) { error = "Unable to save settings"; break; }
      state.config = c; animationFrameReady = true; // Apply relay changes even during a long frame.
      if (radioChanged) radioRestartAt = millis() + 1000;
      if (httpServer) MDNS.setInstanceName(state.config.name);
      if (httpServer) MDNS.addServiceTxt("luxio", "tcp", "strip", (const char *)c.strip_type);
      break;
    }
    case luxio_Operation_SET_WIFI:
      if (!state.config.wifi.enabled) { error = "Enable Wi-Fi before configuring a network"; break; }
      if (!request->has_wifi || !strlen(request->wifi.ssid)) error = "Wi-Fi SSID is required";
      else { prefs.putString("ssid", request->wifi.ssid); prefs.putString("password", request->wifi.password); wifiSsid = request->wifi.ssid; wifiPassword = request->wifi.password; wifiPending = true; }
      break;
    case luxio_Operation_SCAN_WIFI:
      if (!state.config.wifi.enabled) { error = "Wi-Fi is disabled"; break; }
      if (!wifiScanning) {
        wifiScanning = wifiScanPending = true;
        wifiNetworkCount = 0; wifiScanError = "";
      }
      break;
    case luxio_Operation_SET_GRADIENT:
      if (!request->gradient || !validGradient(*request->gradient, state.config.strip_type)) { error = "Gradient requires 2–8 valid colors for this strip"; break; }
      stopAnimation(); transition->begin(millis());
      state.gradient = *request->gradient; state.gradient.id[0] = 0; state.gradient.name[0] = 0;
      state.has_gradient = true; state.mode = luxio_Mode_GRADIENT; state.on = true;
      renderGradient(state.gradient, pixels, state.config.led_count); break;
    case luxio_Operation_LIST_GRADIENTS: response->gradients.funcs.encode = listGradients; response->gradients.arg = &gradientPage; break;
    case luxio_Operation_CREATE_GRADIENT:
    case luxio_Operation_UPDATE_GRADIENT: {
      if (!request->gradient) { error = "Gradient requires a name and 2–8 valid colors for this strip"; break; }
      auto &gradient = *request->gradient;
      String name = gradient.name; name.trim();
      if (!name.length() || !validGradient(gradient, state.config.strip_type)) { error = "Gradient requires a name and 2–8 valid colors for this strip"; break; }
      strlcpy(gradient.name, name.c_str(), sizeof(gradient.name));
      if (request->operation == luxio_Operation_CREATE_GRADIENT) {
        uint8_t uuid[16]; esp_fill_random(uuid, 16); uuid[6] = (uuid[6] & 15) | 64; uuid[8] = (uuid[8] & 63) | 128;
        snprintf(gradient.id, sizeof(gradient.id), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
      } else if (!validId(gradient.id) || !LittleFS.exists((String("/gradients/") + gradient.id + ".pb").c_str())) { error = "Gradient not found"; break; }
      size_t capacity = 0;
      if (!pb_get_encoded_size(&capacity, luxio_Gradient_fields, &gradient) || capacity + 12 > GRADIENT_RECORD_MAX) { error = "Unable to encode gradient"; break; }
      capacity += 12;
      std::unique_ptr<uint8_t[]> stored(new (std::nothrow) uint8_t[capacity]);
      if (!stored) { error = "Not enough memory for saved gradient"; break; }
      const size_t storedSize = encodeStoredGradient(gradient, stored.get(), capacity);
      if (!storedSize) { error = "Unable to encode gradient"; break; }
      if (LittleFS.totalBytes() - LittleFS.usedBytes() < storedSize + 40960) { error = "Storage is full; delete a saved animation or gradient"; break; }
      String path = String("/gradients/") + gradient.id + ".pb";
      File file = LittleFS.open("/gradients/pending", "w");
      bool written = file && file.write(stored.get(), storedSize) == storedSize;
      file.flush(); file.close();
      File verify = LittleFS.open("/gradients/pending", "r");
      written = written && verify && verify.size() == storedSize && verify.read(stored.get(), storedSize) == storedSize &&
        decodeStoredGradient(stored.get(), storedSize, *responseGradient) && !strcmp(gradient.id, responseGradient->id) &&
        !strcmp(gradient.name, responseGradient->name) && !strcmp(gradient.editor_metadata, responseGradient->editor_metadata) &&
        gradient.colors_count == responseGradient->colors_count && !memcmp(gradient.colors, responseGradient->colors, gradient.colors_count * sizeof(luxio_Color));
      verify.close();
      if (!written || !LittleFS.rename("/gradients/pending", path)) { LittleFS.remove("/gradients/pending"); error = "Unable to save gradient (storage full or write failed)"; break; }
      response->gradient = &gradient; break;
    }
    case luxio_Operation_GET_GRADIENT:
    case luxio_Operation_RUN_GRADIENT:
    case luxio_Operation_DELETE_GRADIENT: {
      if (!validId(request->gradient_id)) { error = "Invalid gradient ID"; break; }
      String path = String("/gradients/") + request->gradient_id + ".pb";
      if (!LittleFS.exists(path)) { error = "Gradient not found"; break; }
      if (request->operation == luxio_Operation_DELETE_GRADIENT) {
        if (!LittleFS.remove(path)) { error = "Unable to delete gradient"; break; }
        if (!strcmp(state.active_gradient_id, request->gradient_id)) { stopAnimation(); applyColor(); }
        break;
      }
      File file = LittleFS.open(path, "r"); size_t size = file.size();
      if (!size || size > GRADIENT_RECORD_MAX) { error = "Invalid saved gradient size"; break; }
      std::unique_ptr<uint8_t[]> stored(new (std::nothrow) uint8_t[size]);
      if (!stored) { error = "Not enough memory for saved gradient"; break; }
      if (!size || size > GRADIENT_RECORD_MAX || file.read(stored.get(), size) != size || !decodeStoredGradient(stored.get(), size, *responseGradient)) { error = "Invalid saved gradient"; break; }
      stored.reset();
      if (request->operation == luxio_Operation_GET_GRADIENT) response->gradient = responseGradient.get();
      else {
        if (!validGradient(*responseGradient, state.config.strip_type)) { error = "Gradient contains channels unavailable on this strip"; break; }
        stopAnimation(); transition->begin(millis());
        state.gradient = *responseGradient; state.has_gradient = true;
        strlcpy(state.active_gradient_id, request->gradient_id, sizeof(state.active_gradient_id));
        state.mode = luxio_Mode_GRADIENT; state.on = true;
        renderGradient(state.gradient, pixels, state.config.led_count);
      }
      break;
    }
    case luxio_Operation_LIST_COLORS: response->colors.funcs.encode = listColors; response->colors.arg = &colorPage; break;
    case luxio_Operation_CREATE_COLOR:
    case luxio_Operation_UPDATE_COLOR: {
      auto &saved_color = request->saved_color;
      String name = saved_color.name; name.trim();
      if (!request->has_saved_color || !name.length() || (!saved_color.has_color || !validColor(saved_color.color, state.config.strip_type))) { error = "Color requires a name and valid channels for this strip"; break; }
      strlcpy(saved_color.name, name.c_str(), sizeof(saved_color.name));
      if (request->operation == luxio_Operation_CREATE_COLOR) {
        uint8_t uuid[16]; esp_fill_random(uuid, 16); uuid[6] = (uuid[6] & 15) | 64; uuid[8] = (uuid[8] & 63) | 128;
        snprintf(saved_color.id, sizeof(saved_color.id), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
      } else if (!validId(saved_color.id) || !LittleFS.exists((String("/colors/") + saved_color.id + ".pb").c_str())) { error = "Color not found"; break; }
      size_t capacity = 0;
      if (!pb_get_encoded_size(&capacity, luxio_SavedColor_fields, &saved_color) || capacity + 12 > COLOR_RECORD_MAX) { error = "Unable to encode color"; break; }
      capacity += 12;
      std::unique_ptr<uint8_t[]> stored(new (std::nothrow) uint8_t[capacity]);
      if (!stored) { error = "Not enough memory for saved color"; break; }
      const size_t storedSize = encodeStoredColor(saved_color, stored.get(), capacity);
      if (!storedSize) { error = "Unable to encode color"; break; }
      if (LittleFS.totalBytes() - LittleFS.usedBytes() < storedSize + 40960) { error = "Storage is full; delete a saved animation or color"; break; }
      String path = String("/colors/") + saved_color.id + ".pb";
      File file = LittleFS.open("/colors/pending", "w");
      bool written = file && file.write(stored.get(), storedSize) == storedSize;
      file.flush(); file.close();
      File verify = LittleFS.open("/colors/pending", "r");
      written = written && verify && verify.size() == storedSize && verify.read(stored.get(), storedSize) == storedSize &&
        decodeStoredColor(stored.get(), storedSize, response->saved_color) && !strcmp(saved_color.id, response->saved_color.id) &&
        !strcmp(saved_color.name, response->saved_color.name) && !memcmp(&saved_color.color, &response->saved_color.color, sizeof(luxio_Color));
      verify.close();
      if (!written || !LittleFS.rename("/colors/pending", path)) { LittleFS.remove("/colors/pending"); error = "Unable to save color (storage full or write failed)"; break; }
      response->has_saved_color = true; response->saved_color = saved_color; break;
    }
    case luxio_Operation_GET_COLOR:
    case luxio_Operation_RUN_COLOR:
    case luxio_Operation_DELETE_COLOR: {
      if (!validId(request->color_id)) { error = "Invalid color ID"; break; }
      String path = String("/colors/") + request->color_id + ".pb";
      if (!LittleFS.exists(path)) { error = "Color not found"; break; }
      if (request->operation == luxio_Operation_DELETE_COLOR) {
        if (!LittleFS.remove(path)) { error = "Unable to delete color"; break; }
        if (!strcmp(state.active_color_id, request->color_id)) { stopAnimation(); applyColor(); }
        break;
      }
      File file = LittleFS.open(path, "r"); size_t size = file.size();
      if (!size || size > COLOR_RECORD_MAX) { error = "Invalid saved color size"; break; }
      std::unique_ptr<uint8_t[]> stored(new (std::nothrow) uint8_t[size]);
      if (!stored) { error = "Not enough memory for saved color"; break; }
      if (!size || size > COLOR_RECORD_MAX || file.read(stored.get(), size) != size || !decodeStoredColor(stored.get(), size, response->saved_color)) { error = "Invalid saved color"; break; }
      stored.reset();
      if (request->operation == luxio_Operation_GET_COLOR) response->has_saved_color = true;
      else {
        if (!validColor(response->saved_color.color, state.config.strip_type)) { error = "Color contains channels unavailable on this strip"; break; }
        stopAnimation(); transition->begin(millis());
        state.color = response->saved_color.color;
        strlcpy(state.active_color_id, request->color_id, sizeof(state.active_color_id));
        state.mode = luxio_Mode_COLOR; state.on = true;
        applyColor();
      }
      break;
    }
    case luxio_Operation_STOP_GRADIENT:
      if (state.mode == luxio_Mode_GRADIENT) { stopAnimation(); applyColor(); }
      break;
    case luxio_Operation_LIST_SCRIPTS: response->scripts.funcs.encode = listScripts; response->scripts.arg = &page; break;
    case luxio_Operation_CREATE_SCRIPT:
    case luxio_Operation_UPDATE_SCRIPT: {
      if (!request->script) { error = "Animation name and source are required"; break; }
      auto &script = *request->script;
      if (script.mode != luxio_Mode_COLOR && script.mode != luxio_Mode_ANIMATION) { error = "Use gradient commands for gradients"; break; }
      // Legacy clients omit mode; their scripts retain animation behavior.
      if (script.mode == luxio_Mode_COLOR) script.mode = luxio_Mode_ANIMATION;
      if (!bool(request->script) || !script.name[0] || !script.source || !script.source[0]) { error = "Animation name and source are required"; break; }
      if (request->operation == luxio_Operation_CREATE_SCRIPT) {
        uint8_t uuid[16]; esp_fill_random(uuid, 16); uuid[6] = (uuid[6] & 15) | 64; uuid[8] = (uuid[8] & 63) | 128;
        snprintf(script.id, sizeof(script.id), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
      } else if (!validId(script.id) || !LittleFS.exists((String("/scripts/") + script.id + ".pb").c_str())) { error = "Animation not found"; break; }
      auto parameters = std::make_unique<luxio_AnimationParameter[]>(16); pb_size_t count = 0;
      const char *parameterError = parseAnimationParameters(script.source, parameters.get(), count);
      if (!parameterError) parameterError = updateAnimationParameters(parameters.get(), count, script.parameters, script.parameters_count);
      if (!parameterError) parameterError = parseAnimationPreview(script.source, script.preview, script.preview_count);
      if (parameterError) { error = parameterError; break; }
      script.parameters_count = count; memcpy(script.parameters, parameters.get(), sizeof(script.parameters));
      const size_t capacity = 24 + 2048 + strlen(script.source);
      std::unique_ptr<uint8_t[]> stored(new (std::nothrow) uint8_t[capacity]);
      if (!stored) { error = "Not enough memory for saved script"; break; }
      const size_t storedSize = encodeStoredScript(script, stored.get(), capacity);
      if (!storedSize) { error = "Unable to encode animation"; break; }
      // LittleFS needs free blocks for atomic replacement and metadata updates.
      if (LittleFS.totalBytes() - LittleFS.usedBytes() < storedSize + 40960) { error = "Storage is full; delete a saved color, gradient, or animation"; break; }
      String path = String("/scripts/") + script.id + ".pb";
      File file = LittleFS.open("/scripts/pending", "w");
      bool written = file && file.write(stored.get(), storedSize) == storedSize;
      file.flush(); file.close();
      // Verify the actual stored bytes before replacing an existing script.
      File verify = LittleFS.open("/scripts/pending", "r");
      written = written && verify && verify.size() == storedSize && verify.read(stored.get(), storedSize) == storedSize &&
        decodeStoredScript(stored.get(), storedSize, *responseScript) && !strcmp(responseScript->source, script.source) &&
        !strcmp(responseScript->id, script.id) && !strcmp(responseScript->name, script.name) && responseScript->mode == script.mode && responseScript->preview_count == script.preview_count && !memcmp(responseScript->preview, script.preview, script.preview_count * sizeof(luxio_Color)) && responseScript->parameters_count == script.parameters_count && !memcmp(responseScript->parameters, script.parameters, sizeof(script.parameters));
      verify.close();
      if (!written || !LittleFS.rename("/scripts/pending", path)) { LittleFS.remove("/scripts/pending"); error = "Unable to save animation (storage full or write failed)"; break; }
      response->script = request->script; break;
    }
    case luxio_Operation_GET_SCRIPT:
      if (!request->script_id[0]) {
        if (!animation || !activeAnimationSource || state.mode != luxio_Mode_ANIMATION) { error = "No animation is running"; break; }
        response->script = responseScript.get();
        auto &script = *responseScript;
        strlcpy(script.id, state.active_script_id, sizeof(script.id));
        strlcpy(script.name, state.script_name, sizeof(script.name));
        script.source = strdup(activeAnimationSource);
        if (!script.source) { error = "Not enough memory for animation source"; break; }
        script.mode = luxio_Mode_ANIMATION;
        script.parameters_count = state.parameters_count; memcpy(script.parameters, state.parameters, sizeof(script.parameters));
        script.preview_count = state.animation_preview_count; memcpy(script.preview, state.animation_preview, sizeof(script.preview));
        break;
      }
      [[fallthrough]];
    case luxio_Operation_RUN_SCRIPT:
    case luxio_Operation_DELETE_SCRIPT: {
      if (!validId(request->script_id)) { error = "Invalid animation ID"; break; }
      String path = String("/scripts/") + request->script_id + ".pb";
      if (!LittleFS.exists(path)) { error = "Animation not found"; break; }
      if (request->operation == luxio_Operation_DELETE_SCRIPT) {
        if (!strcmp(state.active_script_id, request->script_id)) { stopAnimation(); applyColor(); }
        if (!LittleFS.remove(path)) error = "Unable to delete animation";
        break;
      }
      File file = LittleFS.open(path, "r"); size_t size = file.size();
      if (!size || size > SCRIPT_RECORD_MAX) { error = "Invalid saved script size"; break; }
      std::unique_ptr<uint8_t[]> stored(new (std::nothrow) uint8_t[size]);
      if (!stored) { error = "Not enough memory for saved script"; break; }
      if (!size || size > SCRIPT_RECORD_MAX || file.read(stored.get(), size) != size) { error = "Unable to read animation"; break; }
      if (!decodeStoredScript(stored.get(), size, *responseScript)) { error = "Invalid saved animation"; break; }
      stored.reset();
      if (request->operation == luxio_Operation_GET_SCRIPT) response->script = responseScript.get();
      else {
        if (responseScript->mode == luxio_Mode_GRADIENT) { error = "Legacy Lua gradient: migrate it to gradient storage"; break; }
        error = startAnimation(*responseScript);
        if (!error.length()) {
          state.on = true;
          strlcpy(state.active_script_id, request->script_id, sizeof(state.active_script_id));
          state.mode = luxio_Mode_ANIMATION;
          strlcpy(state.script_name, responseScript->name, sizeof(state.script_name));
        } else if (!animation) { applyColor(); }
        response->script = nullptr;
      }
      break;
    }
    case luxio_Operation_SET_ANIMATION_PARAMETERS: {
      if (!animation || state.mode != luxio_Mode_ANIMATION) { error = "No animation is running"; break; }
      if ((!request->script_id[0] && (!bool(request->script) || !request->script->name[0])) ||
          (request->script_id[0] && strcmp(request->script_id, state.active_script_id)) ||
          (bool(request->script) && request->script->name[0] && strcmp(request->script->name, state.script_name))) { error = "The active animation changed"; break; }
      const char *parameterError = updateAnimationParameters(state.parameters, state.parameters_count, request->parameters, request->parameters_count);
      if (parameterError) error = parameterError;
      break;
    }
    case luxio_Operation_SET_SCRIPT:
      if (!bool(request->script) || !request->script->source || !request->script->source[0] || (request->script->mode != luxio_Mode_COLOR && request->script->mode != luxio_Mode_ANIMATION)) { error = "Provide animation Lua source"; break; }
      error = startAnimation(*request->script);
      if (!error.length()) { state.on = true; state.mode = luxio_Mode_ANIMATION; strlcpy(state.script_name, request->script->name, sizeof(state.script_name)); }
      else if (!animation) applyColor();
      break;
    case luxio_Operation_STOP_SCRIPT: if (bool(request->script) && request->script->name[0] && strcmp(request->script->name, state.script_name)) break; if (state.mode == luxio_Mode_ANIMATION) { stopAnimation(); applyColor(); } break;
    case luxio_Operation_GET_PAIRING_CODE:
      if (state.matter_disabled) error = "Matter is unavailable on this device";
      else if (!state.config.matter.enabled) error = "Matter is disabled";
      else if (!state.config.wifi.enabled || WiFi.status() != WL_CONNECTED) error = "Connect to Wi-Fi before using Matter";
      else if (!pairingCode.length()) error = "Matter is not ready";
      else { strlcpy(response->pairing_code, pairingCode.c_str(), sizeof(response->pairing_code)); strlcpy(response->pairing_qr, pairingQr.c_str(), sizeof(response->pairing_qr)); }
      break;
    case luxio_Operation_CHECK_UPDATE:
      if (!state.config.wifi.enabled || WiFi.status() != WL_CONNECTED) { error = "Connect to Wi-Fi before checking for updates"; break; }
      if (!updateChecking) { checkUpdate = true; updateChecking = true; availableBuild = 0; availableUpdate = {}; updateError = ""; }
      break;
    case luxio_Operation_INSTALL_UPDATE: {
      const auto *loader = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "loader");
      const auto *running = esp_ota_get_running_partition();
      esp_app_desc_t loaderDescription{};
      if (rebootPending) error = "Update already starting";
      else if (!availableBuild || !verifySignedUpdate(availableUpdate, max(uint32_t(LUXIO_BUILD), prefs.getUInt("ota-floor", 0)), state.config.channel)) error = "No eligible update; check for updates first";
      else if (!loader || loader->address != 0x10000 || loader->size != LUXIO_LOADER_SIZE || !running || running->address != LUXIO_APP_ADDRESS || running->size != LUXIO_APP_SIZE) error = "Recovery layout required; install via USB first";
      else if (esp_ota_get_partition_description(loader, &loaderDescription) != ESP_OK || strcmp(loaderDescription.project_name, "Recovery") || atoi(loaderDescription.version) < 31) error = "Signed recovery loader required; update via USB first";
      else {
        // Save the current controls before leaving the main application.
        if (!saveUpdateState()) error = "Unable to save startup state";
        else if (prefs.putBytes("ota-job", &availableUpdate, sizeof(availableUpdate)) != sizeof(availableUpdate)) error = "Unable to save update request";
        else if (!prefs.putBool("resume-output", true)) { prefs.remove("ota-job"); error = "Unable to save update resume flag"; }
        else if (esp_ota_set_boot_partition(loader) != ESP_OK) { prefs.remove("ota-job"); prefs.remove("resume-output"); error = "Unable to select recovery loader"; }
        else rebootPending = true;
      }
      break;
    }
    default: error = "Unknown operation";
  }
  response->available_build = availableBuild;
  response->update_checking = updateChecking;
  strlcpy(response->update_error, updateError.c_str(), sizeof(response->update_error));
  if (request->operation == luxio_Operation_GET_STATE || request->operation == luxio_Operation_SCAN_WIFI) {
    response->wifi_scanning = wifiScanning;
    response->wifi_networks_count = wifiNetworkCount;
    response->wifi_networks = wifiNetworks.get(); // Borrow the bounded scan cache under stateMutex.
    strlcpy(response->wifi_scan_error, wifiScanError.c_str(), sizeof(response->wifi_scan_error));
  }
  if (request->operation == luxio_Operation_GET_STATE) {
    strlcpy(response->animation_error, animationError, sizeof(response->animation_error));
    response->has_network_status = true;
    auto &network = response->network_status;
    auto ap = std::make_unique<wifi_ap_record_t>();
    network.connected = esp_wifi_sta_get_ap_info(ap.get()) == ESP_OK;
    if (network.connected) {
      network.rssi = ap->rssi; network.channel = ap->primary;
      strlcpy(network.ssid, WiFi.SSID().c_str(), sizeof(network.ssid));
      const IPAddress gateway = WiFi.gatewayIP();
      if (gateway != IPAddress(0, 0, 0, 0)) strlcpy(network.gateway, gateway.toString().c_str(), sizeof(network.gateway));
      for (uint8_t index = 0; index < 2; index++) {
        const IPAddress dns = WiFi.dnsIP(index);
        if (dns != IPAddress(0, 0, 0, 0)) {
          strlcpy(network.dns[network.dns_count++], dns.toString().c_str(), sizeof(network.dns[0]));
        }
      }
      snprintf(network.bssid, sizeof(network.bssid), "%02x:%02x:%02x:%02x:%02x:%02x", ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    }
    wifi_ps_type_t powerSave = WIFI_PS_NONE;
    esp_wifi_get_ps(&powerSave); network.power_save = powerSave;
    network.uptime_ms = millis();
    network.psram_size = ESP.getPsramSize(); network.psram_free = ESP.getFreePsram();
    network.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    network.internal_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    network.disconnect_count = wifiDisconnectCount.load(); network.last_disconnect_reason = wifiDisconnectReason.load();
  }
  if (error.length()) strlcpy(response->error, error.c_str(), sizeof(response->error));
  // Borrow the state under stateMutex instead of allocating another full copy.
  else { response->state = &state; strlcpy(state.lan_ip, WiFi.localIP().toString().c_str(), sizeof(state.lan_ip)); }
  size_t size = encodeControlResponse(*response, output, prefix);
  if (!size) {
    memset(response, 0, sizeof(*response));
    response->id = request->id;
    strlcpy(response->error, "Response exceeds available memory or saved data is invalid", sizeof(response->error));
    size = encodeControlResponse(*response, output, prefix);
  }
  pb_release(luxio_Request_fields, request);
  delete request; delete response;
  xSemaphoreGive(stateMutex);
  return size;
}

static esp_err_t httpHandler(httpd_req_t *request) {
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "POST, OPTIONS");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Private-Network", "true");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  if (request->method == HTTP_OPTIONS) { httpd_resp_set_status(request, "204 No Content"); return httpd_resp_send(request, nullptr, 0); }
  if (request->content_len > 32768 || request->content_len < 1) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid message size");
  std::unique_ptr<uint8_t[]> input(new (std::nothrow) uint8_t[request->content_len]), output;
  if (!input) { httpd_resp_set_status(request, "503 Service Unavailable"); return httpd_resp_send(request, "Not enough memory", HTTPD_RESP_USE_STRLEN); }
  size_t received = 0;
  while (received < request->content_len) {
    int size = httpd_req_recv(request, reinterpret_cast<char *>(input.get() + received), request->content_len - received);
    if (size <= 0) return ESP_FAIL;
    received += size;
  }
  size_t length = dispatch(input.get(), received, output, 0);
  uint8_t fallback[48]; const uint8_t *payload = output.get();
  if (!length) { length = encodeControlError(input.get(), received, fallback, sizeof(fallback)); payload = fallback; }
  input.reset();
  httpd_resp_set_type(request, "application/x-protobuf");
  return httpd_resp_send(request, reinterpret_cast<const char *>(payload), length);
}
static esp_err_t websocketHandler(httpd_req_t *request) {
  if (request->method == HTTP_GET) return ESP_OK;
  httpd_ws_frame_t packet = {};
  if (httpd_ws_recv_frame(request, &packet, 0) != ESP_OK || !packet.len || packet.len > 32768 || packet.type != HTTPD_WS_TYPE_BINARY) return ESP_FAIL;
  std::unique_ptr<uint8_t[]> input(new (std::nothrow) uint8_t[packet.len]), output; packet.payload = input.get();
  if (!input) return ESP_ERR_NO_MEM;
  if (httpd_ws_recv_frame(request, &packet, packet.len) != ESP_OK) return ESP_FAIL;
  size_t length = dispatch(input.get(), packet.len, output, 0);
  uint8_t fallback[48];
  packet.payload = output.get();
  if (!length) { length = encodeControlError(input.get(), packet.len, fallback, sizeof(fallback)); packet.payload = fallback; }
  input.reset(); packet.len = length;
  return httpd_ws_send_frame(request, &packet);
}
#if LUXIO_MATTER
static MatterEnhancedColorLight matterLight;
// Basic Information reads these strings from the provider, not writable
// attributes. Delegate all other factory data to the SDK's active provider.
static class LuxioDeviceInfo : public chip::DeviceLayer::DeviceInstanceInfoProvider {
public:
  chip::DeviceLayer::DeviceInstanceInfoProvider *original = nullptr;
  CHIP_ERROR GetVendorName(char *buffer, size_t size) override {
    if (size < sizeof("Luxio Lighting")) return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(buffer, "Luxio Lighting", sizeof("Luxio Lighting"));
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetProductName(char *buffer, size_t size) override {
    if (size < sizeof("DIY")) return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(buffer, "DIY", sizeof("DIY"));
    return CHIP_NO_ERROR;
  }
  CHIP_ERROR GetVendorId(uint16_t &value) override { return original->GetVendorId(value); }
  CHIP_ERROR GetProductId(uint16_t &value) override { return original->GetProductId(value); }
  CHIP_ERROR GetPartNumber(char *buffer, size_t size) override { return original->GetPartNumber(buffer, size); }
  CHIP_ERROR GetProductURL(char *buffer, size_t size) override { return original->GetProductURL(buffer, size); }
  CHIP_ERROR GetProductLabel(char *buffer, size_t size) override { return original->GetProductLabel(buffer, size); }
  CHIP_ERROR GetSerialNumber(char *buffer, size_t size) override { return original->GetSerialNumber(buffer, size); }
  CHIP_ERROR GetManufacturingDate(uint16_t &year, uint8_t &month, uint8_t &day) override { return original->GetManufacturingDate(year, month, day); }
  CHIP_ERROR GetHardwareVersion(uint16_t &value) override { return original->GetHardwareVersion(value); }
  CHIP_ERROR GetHardwareVersionString(char *buffer, size_t size) override { return original->GetHardwareVersionString(buffer, size); }
  CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan &value) override { return original->GetRotatingDeviceIdUniqueId(value); }
  CHIP_ERROR GetProductFinish(chip::app::Clusters::BasicInformation::ProductFinishEnum *value) override { return original->GetProductFinish(value); }
  CHIP_ERROR GetProductPrimaryColor(chip::app::Clusters::BasicInformation::ColorEnum *value) override { return original->GetProductPrimaryColor(value); }
} matterDeviceInfo;
#endif
static void cloudTask(void *) {
  uint32_t lastAnnouncement = 0;
#if LUXIO_MATTER
  bool matterStarted = false;
#endif
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    // Start once, only when enabled and Wi-Fi has an address.
#if LUXIO_MATTER
    if (!matterStarted && !state.matter_disabled && state.config.matter.enabled && state.config.wifi.enabled) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      const bool on = state.on; const uint32_t brightness = state.brightness;
      xSemaphoreGive(stateMutex);
      if (!matterLight.begin(on, {0, 0, 254}, constrain((brightness * 254 + 128) / 256, 1, 254), 370)) abort();
      matterLight.onIdentify([](bool enabled) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        identifyActive = enabled;
        xSemaphoreGive(stateMutex);
        return true;
      });
      // Start before registering control callbacks: Matter normalizes its level
      // attribute during startup, but that must not turn a saved "off" state on.
      // This also installs the provider before we read commissioning data below.
      Matter.begin();
      chip::DeviceLayer::PlatformMgr().LockChipStack();
      matterDeviceInfo.original = chip::DeviceLayer::GetDeviceInstanceInfoProvider();
      chip::DeviceLayer::SetDeviceInstanceInfoProvider(&matterDeviceInfo);
      chip::DeviceLayer::PlatformMgr().UnlockChipStack();
      matterLight.onChangeBrightness([](uint8_t brightness) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        uint32_t level = constrain((uint32_t(brightness) * 256 + 127) / 254, 1, 256);
        if (state.brightness != level) transition->begin(millis());
        state.brightness = level; xSemaphoreGive(stateMutex); return true;
      });
      matterLight.onChangeOnOff([](bool on) {
        xSemaphoreTake(stateMutex, portMAX_DELAY); transition->begin(millis()); state.on = on; xSemaphoreGive(stateMutex); return true;
      });
      matterLight.onChangeColorHSV([](espHsvColor_t hsv) {
        hsv.v = 255; auto rgb = espHsvColorToRgbColor(hsv);
        xSemaphoreTake(stateMutex, portMAX_DELAY); stopAnimation(); state.on = true; state.color = {rgb.r, rgb.g, rgb.b, 0, 0, 0}; applyColor(); xSemaphoreGive(stateMutex); return true;
      });
      matterLight.onChangeColorTemperature([](uint16_t mired) {
        xSemaphoreTake(stateMutex, portMAX_DELAY); stopAnimation(); state.on = true;
        uint32_t warm = constrain((int(mired) - 153) * 255 / (500 - 153), 0, 255);
        if (!strcmp(state.config.strip_type, "WS2805")) state.color = {0, 0, 0, 0, warm, 255 - warm};
        else if ((!strcmp(state.config.strip_type, "WS2814") || !strcmp(state.config.strip_type, "SK6812"))) state.color = {0, 0, 0, 255, 0, 0};
        else state.color = {255, uint32_t(255 - warm * 90 / 255), uint32_t(255 - warm * 180 / 255), 0, 0, 0};
        applyColor(); xSemaphoreGive(stateMutex); return true;
      });
      // Read the actual provider payload; unsupported setters cannot personalize it.
      chip::PayloadContents payload;
      if (GetPayloadContents(payload, chip::RendezvousInformationFlag::kOnNetwork) == CHIP_NO_ERROR) {
        char manual[32] = {}, qr[128] = {};
        chip::MutableCharSpan manualSpan(manual), qrSpan(qr);
        GetManualPairingCode(manualSpan, payload); GetQRCode(qrSpan, payload);
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        pairingCode = manual; pairingQr = qr;
        xSemaphoreGive(stateMutex);
      }
      matterStarted = true;
    }
#endif

    if (!lastAnnouncement || uint32_t(millis() - lastAnnouncement) >= 900000) {
      lastAnnouncement = millis();
      struct { char device_id[sizeof(state.device_id)], name[sizeof(state.config.name)]; int32_t build, channel; } snapshot;
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      strlcpy(snapshot.device_id, state.device_id, sizeof(snapshot.device_id));
      strlcpy(snapshot.name, state.config.name, sizeof(snapshot.name));
      snapshot.build = state.build; snapshot.channel = state.config.channel;
      xSemaphoreGive(stateMutex);
      NetworkClient client;
      HTTPClient http; http.setTimeout(8000); http.useHTTP10(true); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
      String base = LUXIO_CLOUD_URL;
      if (http.begin(client, base + "/announcements/challenge")) {
        http.addHeader("Content-Type", "application/json");
        String body = String("{\"deviceId\":\"") + snapshot.device_id + "\"}";
        int status = http.POST(body);
        if (status == 200 && http.getSize() > 0 && http.getSize() <= 128) {
          char reply[129] = {};
          size_t length = http.getSize();
          bool complete = http.getStreamPtr()->readBytes(reply, length) == length;
          http.end(); client.stop();
          cJSON *challenge = complete ? cJSON_Parse(reply) : nullptr;
          auto nonceItem = cJSON_GetObjectItem(challenge, "nonce");
          String nonce = cJSON_IsString(nonceItem) ? nonceItem->valuestring : "";
          cJSON_Delete(challenge); http.end();
          if (nonce.length() == 64) {
            String ip = WiFi.localIP().toString();
            cJSON *payload = cJSON_CreateArray();
            cJSON_AddItemToArray(payload, cJSON_CreateNumber(1)); cJSON_AddItemToArray(payload, cJSON_CreateString(nonce.c_str()));
            cJSON_AddItemToArray(payload, cJSON_CreateString(snapshot.device_id)); cJSON_AddItemToArray(payload, cJSON_CreateString(snapshot.name));
            cJSON_AddItemToArray(payload, cJSON_CreateString(ip.c_str())); cJSON_AddItemToArray(payload, cJSON_CreateNumber(snapshot.build));
            cJSON_AddItemToArray(payload, cJSON_CreateNumber(snapshot.channel));
            char *canonical = cJSON_PrintUnformatted(payload);
            uint8_t digest[32], signature[80]; size_t signatureLength = 0;
            mbedtls_sha256(reinterpret_cast<const uint8_t *>(canonical), strlen(canonical), digest, 0);
            int signedOk = mbedtls_pk_sign(&identity, MBEDTLS_MD_SHA256, digest, 32, signature, sizeof(signature), &signatureLength, hardwareRandom, nullptr);
            free(canonical); cJSON_Delete(payload);
            uint8_t base64[128]; size_t base64Length = 0;
            mbedtls_base64_encode(base64, sizeof(base64), &base64Length, signature, signatureLength); base64[base64Length] = 0;
            if (!signedOk && http.begin(client, base + "/announcements")) {
              cJSON *body = cJSON_CreateObject();
              cJSON_AddStringToObject(body, "deviceId", snapshot.device_id); cJSON_AddStringToObject(body, "publicKey", publicKey.c_str());
              cJSON_AddStringToObject(body, "nonce", nonce.c_str()); cJSON_AddStringToObject(body, "name", snapshot.name);
              cJSON_AddStringToObject(body, "lanIp", ip.c_str()); cJSON_AddNumberToObject(body, "build", snapshot.build);
              cJSON_AddNumberToObject(body, "channel", snapshot.channel); cJSON_AddStringToObject(body, "signature", reinterpret_cast<char *>(base64));
              char *json = cJSON_PrintUnformatted(body); http.addHeader("Content-Type", "application/json"); http.POST(reinterpret_cast<uint8_t *>(json), strlen(json));
              free(json); cJSON_Delete(body);
            }
          }
        }
        http.end();
      }
    }
    if (checkUpdate) {
      checkUpdate = false;
      xSemaphoreTake(stateMutex, portMAX_DELAY); auto channel = state.config.channel; xSemaphoreGive(stateMutex);
      NetworkClient client; HTTPClient http; http.setTimeout(10000); http.useHTTP10(true); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
      String url = String(LUXIO_CLOUD_URL) + "/updates?chip=" CONFIG_IDF_TARGET "&target=" + LUXIO_UPDATE_TARGET + "&build=" + LUXIO_BUILD + "&channel=" + channel;
      String failure; UpdateJob candidate;
      if (http.begin(client, url)) {
        int status = http.GET();
        if (status == 200) {
          int length = http.getSize();
          // Refuse oversized/chunked responses before allocating or parsing.
          std::unique_ptr<char[]> body(length > 0 && length <= 6144 ? new (std::nothrow) char[length + 1] : nullptr);
          bool complete = body && http.getStreamPtr()->readBytes(body.get(), length) == size_t(length);
          if (body) body[length] = 0;
          http.end(); client.stop();
          cJSON *doc = complete ? parseUpdateJson(body.get(), length) : nullptr;
          body.reset();
          if (!doc) failure = "Invalid or oversized update response";
          else if (!cJSON_IsNull(doc) && (!parseSignedUpdate(doc, candidate) ||
              !verifySignedUpdate(candidate, max(uint32_t(LUXIO_BUILD), prefs.getUInt("ota-floor", 0)), channel)))
            failure = "Invalid update signature, incompatible release or older build";
          cJSON_Delete(doc);
        } else if (status < 0) {
          failure = String("Update connection failed: ") + HTTPClient::errorToString(status);
        } else failure = String("Update service returned ") + status;
        http.end();
      } else failure = "Unable to reach update service";
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      availableUpdate = failure.length() ? UpdateJob{} : candidate;
      availableBuild = validUpdateJob(availableUpdate) ? availableUpdate.build : 0;
      updateError = failure; updateChecking = false; xSemaphoreGive(stateMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

extern void startBluetooth();
void setup() {
  Serial.begin(115200); // Binary protocol only; diagnostics must never be written here.
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200); // Also serve boards wired through an external USB/UART bridge.
#endif
  // Keep crypto allocations in PSRAM when available. Device cloud traffic
  // uses signed HTTP and no longer allocates a TLS session.
  // Install once before identity, Wi-Fi and Matter initialize crypto.
  if (psramFound()) mbedtls_platform_set_calloc_free(
    [](size_t count, size_t size) -> void * { return ps_calloc(count, size); }, free);
  stateMutex = xSemaphoreCreateMutex();
  if (!stateMutex) abort();
  prefs.begin("luxio", false);
  if (!LittleFS.begin(true)) abort();
  LittleFS.remove("/startup.pending");
  state = {}; state.has_config = state.has_color = true;
  state.config = {"Luxio", "WS2805", 1, LUXIO_DEFAULT_GPIO, 0}; state.brightness = 16; state.on = true;
  size_t configSize = prefs.getBytesLength("config");
  if (configSize && configSize <= luxio_Config_size) {
    uint8_t bytes[luxio_Config_size];
    if (prefs.getBytes("config", bytes, configSize) == configSize) {
      luxio_Config config = {}; auto decoder = pb_istream_from_buffer(bytes, configSize);
      if (pb_decode(&decoder, luxio_Config_fields, &config) && config.led_count >= 1 && config.led_count <= 1024) state.config = config;
    }
  } else {
    // Older releases stored hardware settings inside State; retain only settings.
    size_t size = prefs.getBytesLength("state");
    if (size && size < 4096) {
      std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]);
      auto legacy = std::unique_ptr<luxio_State>(new (std::nothrow) luxio_State{});
      if (bytes && legacy && prefs.getBytes("state", bytes.get(), size) == size) {
        auto decoder = pb_istream_from_buffer(bytes.get(), size);
        if (pb_decode(&decoder, luxio_State_fields, legacy.get()) && legacy->has_config && legacy->config.led_count >= 1 && legacy->config.led_count <= 1024) state.config = legacy->config;
      }
    }
  }
  // Luxio cyan -> green. A single LED displays the first stop.
  state.mode = luxio_Mode_GRADIENT; state.has_gradient = true; state.gradient.colors_count = 2;
  state.gradient.colors[0] = {0, 210, 255, 0, 0, 0}; state.gradient.colors[1] = {79, 255, 127, 0, 0, 0};
  state.color = state.gradient.colors[0];
  const bool resumeOutput = prefs.getBool("resume-output", false);
  auto releaseStartup = [](luxio_Response *saved) { if (saved) pb_release(luxio_Response_fields, saved); delete saved; };
  std::unique_ptr<luxio_Response, decltype(releaseStartup)> startup(new (std::nothrow) luxio_Response{}, releaseStartup);
  bool restored = false;
  File savedFile = resumeOutput ? LittleFS.open("/startup.pb", "r") : File();
  if (startup && savedFile && savedFile.size() > 8 && savedFile.size() <= 32776) {
    uint8_t header[8] = {}; uint8_t bytes[64]; uint32_t crc = 0xffffffff;
    bool valid = savedFile.read(header, 8) == 8 && !memcmp(header, "LXB1", 4);
    size_t remaining = savedFile.size() - 8;
    while (valid && remaining) {
      size_t count = min(remaining, sizeof(bytes));
      if (savedFile.read(bytes, count) != count) { valid = false; break; }
      for (size_t i = 0; i < count; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
      }
      remaining -= count;
    }
    const uint32_t expected = uint32_t(header[4]) | uint32_t(header[5]) << 8 | uint32_t(header[6]) << 16 | uint32_t(header[7]) << 24;
    if (valid && ~crc == expected && savedFile.seek(8)) {
      pb_istream_t decoder = {}; decoder.state = &savedFile; decoder.bytes_left = savedFile.size() - 8;
      decoder.callback = [](pb_istream_t *stream, pb_byte_t *bytes, size_t count) -> bool {
        File *file = static_cast<File *>(stream->state);
        return bytes ? file->read(bytes, count) == count : file->seek(file->position() + count);
      };
      if (pb_decode(&decoder, luxio_Response_fields, startup.get()) && startup->state && startup->state->has_config &&
          startup->state->config.led_count >= 1 && startup->state->config.led_count <= 1024) {
        state = *startup->state; restored = true;
      }
    }
  }
  savedFile.close();
  if (!restored) startup.reset();
  state.brightness = constrain(state.brightness, 1, 256); state.build = LUXIO_BUILD;
  if (!state.config.has_wifi) { state.config.has_wifi = true; state.config.wifi.enabled = true; }
  if (!state.config.has_matter) { state.config.has_matter = true; state.config.matter.enabled = true; }
  // Size all three frames for this strip rather than reserving 18 KiB for 1024 LEDs.
  frameCapacity = state.config.led_count;
#if LUXIO_INTERNAL_RAM_RADIOS
  pixels = static_cast<uint8_t (*)[6]>(heap_caps_calloc(frameCapacity * 3, 6, MALLOC_CAP_INTERNAL | MALLOC_CAP_IRAM_8BIT));
#else
  pixels = static_cast<uint8_t (*)[6]>(psramFound() ? ps_calloc(frameCapacity * 3, 6) : calloc(frameCapacity * 3, 6));
#endif
  if (!pixels) abort();
  transition = std::make_unique<OutputTransition>(pixels + frameCapacity, pixels + frameCapacity * 2, frameCapacity);
  // Matter is optional at runtime: reserve PSRAM for rendering and controls.
  state.matter_disabled = !LUXIO_MATTER || (!LUXIO_INTERNAL_RAM_RADIOS && !LUXIO_MATTER_WITHOUT_PSRAM &&
    (!psramFound() || ESP.getPsramSize() < 2 * 1024 * 1024 || ESP.getFreePsram() < 1024 * 1024));
#if CONFIG_IDF_TARGET_ESP32
  // GPIO16/17 are PSRAM signals on original ESP32 modules. A configuration from
  // an older image with PSRAM disabled must not drive those pins after detection.
  if (psramFound() && (state.config.gpio == 16 || state.config.gpio == 17)) state.config.gpio = 4;
  if (psramFound() && state.config.has_relay && (state.config.relay.gpio == 16 || state.config.relay.gpio == 17)) state.config.relay.enabled = false;
#endif
  state.animation_preview_count = 0; memset(state.animation_preview, 0, sizeof(state.animation_preview));
  mbedtls_pk_init(&identity);
  uint8_t keyBytes[256]; size_t keyLength = prefs.getBytesLength("identity");
  if (keyLength && keyLength <= sizeof(keyBytes)) {
    prefs.getBytes("identity", keyBytes, keyLength);
    if (mbedtls_pk_parse_key(&identity, keyBytes, keyLength, nullptr, 0, hardwareRandom, nullptr)) abort();
  } else {
    if (mbedtls_pk_setup(&identity, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) || mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(identity), hardwareRandom, nullptr)) abort();
    int size = mbedtls_pk_write_key_der(&identity, keyBytes, sizeof(keyBytes));
    if (size <= 0 || prefs.putBytes("identity", keyBytes + sizeof(keyBytes) - size, size) != size_t(size)) abort();
  }
  memset(keyBytes, 0, sizeof(keyBytes));
  uint8_t pub[128], digest[32], base64[180]; size_t base64Length = 0;
  int pubSize = mbedtls_pk_write_pubkey_der(&identity, pub, sizeof(pub));
  if (pubSize <= 0) abort();
  mbedtls_sha256(pub + sizeof(pub) - pubSize, pubSize, digest, 0);
  for (int i = 0; i < 32; i++) sprintf(state.device_id + i * 2, "%02x", digest[i]);
  mbedtls_base64_encode(base64, sizeof(base64), &base64Length, pub + sizeof(pub) - pubSize, pubSize); base64[base64Length] = 0; publicKey = reinterpret_cast<char *>(base64);
  LittleFS.mkdir("/scripts");
  LittleFS.mkdir("/gradients"); LittleFS.mkdir("/colors"); LittleFS.remove("/colors/pending"); LittleFS.remove("/gradients/pending");
  LittleFS.remove("/scripts/pending"); // Discard an interrupted save; the original remains intact.
  // Use full-frame DMA in the single-core radio profile; ordinary builds use RMT.
  ledByteCapacity = state.config.led_count * 5;
#if LUXIO_INTERNAL_RAM_RADIOS
  ledByteCapacity = state.config.led_count * 20 + 240;
#endif
  ledBytes = static_cast<uint8_t *>(heap_caps_malloc(ledByteCapacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | (LUXIO_INTERNAL_RAM_RADIOS ? MALLOC_CAP_DMA : 0)));
  if (!ledBytes) abort();
#if LUXIO_INTERNAL_RAM_RADIOS
  spi_bus_config_t bus = {};
  bus.mosi_io_num = state.config.gpio;
  bus.miso_io_num = bus.sclk_io_num = bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 1024 * 20 + 240;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
  spi_device_interface_config_t device = {};
  device.clock_speed_hz = 3200000;
  device.spics_io_num = -1;
  device.queue_size = 1;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &device, &ledSpi));
#else
  rmt_tx_channel_config_t channel = {};
  channel.gpio_num = gpio_num_t(state.config.gpio); channel.clk_src = RMT_CLK_SRC_DEFAULT;
  channel.resolution_hz = 10000000;
  channel.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
  channel.trans_queue_depth = 1;
  ESP_ERROR_CHECK(rmt_new_tx_channel(&channel, &ledChannel));
  rmt_bytes_encoder_config_t encoding = {};
  encoding.bit0 = {.duration0 = 3, .level0 = 1, .duration1 = 9, .level1 = 0};
  encoding.bit1 = {.duration0 = uint16_t(!strcmp(state.config.strip_type, "SK6812") ? 6 : 8), .level0 = 1, .duration1 = uint16_t(!strcmp(state.config.strip_type, "SK6812") ? 6 : 4), .level1 = 0};
  encoding.flags.msb_first = 1;
  ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoding, &ledEncoder));
  ESP_ERROR_CHECK(rmt_enable(ledChannel));
#endif
  if (state.mode == luxio_Mode_ANIMATION && startup && startup->script && startup->script->source) {
    startup->script->parameters_count = state.parameters_count;
    memcpy(startup->script->parameters, state.parameters, sizeof(state.parameters));
    String error = startAnimation(*startup->script);
    if (!error.length()) {
      state.mode = luxio_Mode_ANIMATION;
      strlcpy(state.active_script_id, startup->script->id, sizeof(state.active_script_id));
      strlcpy(state.script_name, startup->script->name, sizeof(state.script_name));
    } else { strlcpy(animationError, error.c_str(), sizeof(animationError)); }
  } else if (state.mode == luxio_Mode_ANIMATION) { stopAnimation(); }
  startup.reset();
  if (state.mode != luxio_Mode_ANIMATION) { state.parameters_count = 0; memset(state.parameters, 0, sizeof(state.parameters)); }
  if (state.mode == luxio_Mode_GRADIENT && (!state.has_gradient || !validGradient(state.gradient, state.config.strip_type))) { state.mode = luxio_Mode_COLOR; state.has_gradient = false; }
  if (state.mode == luxio_Mode_GRADIENT) renderGradient(state.gradient, pixels, state.config.led_count);
  else if (state.mode != luxio_Mode_ANIMATION) applyColor();
  render();
  // The strip is externally powered: keep the radio awake for local controls
  // and Matter, and choose the strongest AP across all channels on each join.
  if (state.config.wifi.enabled) {
  wifiNetworks.reset(new (std::nothrow) luxio_WifiNetwork[32]{});
  if (!wifiNetworks) abort();
  WiFi.setSleep(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    wifiDisconnectCount.fetch_add(1);
    wifiDisconnectReason.store(info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#if LUXIO_INTERNAL_RAM_RADIOS
  // Despite its name, this selects sdkconfig's dynamic TX/RX counts instead
  // of Arduino's hard-coded 32-buffer override.
  WiFi.useStaticBuffers(true);
#endif
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  wifiSsid = prefs.getString("ssid"); wifiPassword = prefs.getString("password");
  if (wifiSsid.length()) WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.stack_size = 8192; config.max_uri_handlers = 6; config.lru_purge_enable = true;
  if (httpd_start(&httpServer, &config) != ESP_OK) abort();
  httpd_uri_t api = {}; api.uri = "/api"; api.method = HTTP_POST; api.handler = httpHandler; httpd_register_uri_handler(httpServer, &api);
  api.method = HTTP_OPTIONS; httpd_register_uri_handler(httpServer, &api);
  httpd_uri_t websocket = {}; websocket.uri = "/api/ws"; websocket.method = HTTP_GET; websocket.handler = websocketHandler; websocket.is_websocket = true; httpd_register_uri_handler(httpServer, &websocket);
  xTaskCreate(cloudTask, "luxio-cloud", LUXIO_INTERNAL_RAM_RADIOS ? 8192 : 12288, nullptr, 1, nullptr);
  }
  startBluetooth();
  // Reaching initialized local controls acknowledges the completed request.
  // An offline device must not roll back merely because Matter is waiting for Wi-Fi.
  // No timer, boot path or Matter callback checks for or starts a new update.
  esp_ota_mark_app_valid_cancel_rollback();
  if (prefs.getUInt("ota-floor", 0) < LUXIO_BUILD) prefs.putUInt("ota-floor", LUXIO_BUILD);
  prefs.remove("ota-job");
  if (resumeOutput) prefs.remove("resume-output");
  LittleFS.remove("/startup.pb");
}

void loop() {
  static IPAddress advertisedIp;
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != advertisedIp) {
    // Matter shares the system mDNS responder; never free its services here.
    String hostname = String("luxio-") + String(state.device_id).substring(0, 12);
    if (MDNS.begin(hostname.c_str())) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      MDNS.setInstanceName(state.config.name); MDNS.addService("luxio", "tcp", 80);
      MDNS.addServiceTxt("luxio", "tcp", "id", (const char *)state.device_id);
      MDNS.addServiceTxt("luxio", "tcp", "build", String(LUXIO_BUILD));
      MDNS.addServiceTxt("luxio", "tcp", "strip", (const char *)state.config.strip_type);
      xSemaphoreGive(stateMutex); advertisedIp = WiFi.localIP();
    }
  } else if (WiFi.status() != WL_CONNECTED) advertisedIp = IPAddress();
  static std::unique_ptr<uint8_t[]> input;
  static uint32_t expected = 0, received = 0, header = 0, lastByte = 0;
  if (header && millis() - lastByte > 3000) { input.reset(); received = 0; expected = 0; header = 0; }
  static Stream *controlPort = &Serial;
#if ARDUINO_USB_CDC_ON_BOOT
  // Keep a complete frame on its original transport; switch only between requests.
  if (!header) controlPort = Serial.available() ? static_cast<Stream *>(&Serial) : static_cast<Stream *>(&Serial0);
#endif
  while (controlPort->available()) {
    uint8_t byte = controlPort->read(); lastByte = millis();
    if (header < 4) {
      const uint8_t magic[4] = {0x4c, 0x58, 0x50, 0x31};
      header = byte == magic[header] ? header + 1 : byte == magic[0] ? 1 : 0;
      continue;
    }
    if (header < 8) {
      expected = (expected << 8) | byte; header++;
      if (header == 8) {
        if (expected && expected <= 32768) input.reset(new (std::nothrow) uint8_t[expected]);
        if (!expected || expected > 32768 || !input) { input.reset(); expected = 0; header = 0; received = 0; }
      }
      continue;
    }
    input[received++] = byte;
    if (received == expected) {
      std::unique_ptr<uint8_t[]> output;
      size_t length = dispatch(input.get(), received, output, 0);
      uint8_t fallback[48];
      const uint8_t *payload = output.get();
      if (!length) { length = encodeControlError(input.get(), received, fallback, sizeof(fallback)); payload = fallback; }
      uint8_t prefix[8] = {0x4c, 0x58, 0x50, 0x31, uint8_t(length >> 24), uint8_t(length >> 16), uint8_t(length >> 8), uint8_t(length)};
      controlPort->write(prefix, 8); controlPort->write(payload, length);
      input.reset();
      received = 0; expected = 0; header = 0;
    }
  }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  // ANIMATION_STEP_BEGIN
  if (animation && (animationPending || animationPhase < 2 || uint32_t(millis() - animationFrameAt) >= 33)) {
    if (!animationPending) {
      lua_settop(animationThread, 0);
      lua_getglobal(animationThread, animationPhase == 1 ? "setup" : "loop");
      if (animationPhase == 1 && lua_isnil(animationThread, -1)) {
        lua_pop(animationThread, 1); animationPhase = 2;
        lua_getglobal(animationThread, "loop");
      }
      if (!lua_isfunction(animationThread, -1)) {
        strlcpy(animationError, animationPhase == 1 ? "setup must be a function" : "Define a loop() function", sizeof(animationError));
        stopAnimation(); applyColor();
      } else {
        luaInstructions = 1000000; luaCpuRemaining = 500000; luaBudgetError = nullptr;
        animationPending = true; animationFrameAt = millis();
      }
    }
    if (animation) {
      taskYIELD(); TaskStatus_t before;
      vTaskGetInfo(nullptr, &before, pdFALSE, eInvalid);
      const uint32_t started = uint32_t(before.ulRunTimeCounter);
      luaDeadline = started + luaCpuRemaining; luaSliceDeadline = started + 4000;
      int results = 0;
      const int status = lua_resume(animationThread, nullptr, 0, &results);
      taskYIELD(); TaskStatus_t after;
      vTaskGetInfo(nullptr, &after, pdFALSE, eInvalid);
      const uint32_t spent = uint32_t(after.ulRunTimeCounter) - started;
      if (spent >= luaCpuRemaining) luaBudgetError = "Animation exceeded its CPU limit (500 ms per call)";
      else luaCpuRemaining -= spent;
      if (luaBudgetError || (status != LUA_OK && status != LUA_YIELD)) {
        const char *error = luaBudgetError ? luaBudgetError : lua_tostring(animationThread, -1);
        strlcpy(animationError, error ? error : "Animation failed without an error message", sizeof(animationError));
        stopAnimation(); applyColor();
      } else if (status == LUA_OK) {
        lua_settop(animationThread, 0); animationPending = false;
        if (animationPhase < 2) animationPhase++;
        else { memcpy(pixels, animationPixels, state.config.led_count * 6); animationFrameReady = true; } // Publish only a complete frame.
      }
    }
  }
  // ANIMATION_STEP_END
  xSemaphoreGive(stateMutex);
  if (millis() - lastFrame >= 33) {
    lastFrame = millis(); xSemaphoreTake(stateMutex, portMAX_DELAY);
    // A long strip can take >50 ms to transmit. Do not resend an unchanged
    // frame between every Lua slice; still service fades, power and Identify.
    // ANIMATION_OUTPUT_BEGIN
    if (!animation || animationFrameReady || transition->active || identifyActive || transition->identifying) {
      render(); animationFrameReady = false;
    }
    // ANIMATION_OUTPUT_END
    if (wifiScanning) {
      // Never wait for the radio under the state lock or pause the LED render loop.
      int count;
      const bool starting = wifiScanPending;
      if (wifiScanPending) {
        wifiScanPending = false;
        WiFi.setScanTimeout(15000);
        count = WiFi.scanNetworks(true);
      } else count = WiFi.scanComplete();
      if (count != WIFI_SCAN_RUNNING) {
        if (count < 0) {
          if (!starting) esp_wifi_scan_stop();
          wifiScanError = "Wi-Fi scan failed. Retry when the device is not connecting.";
        } else {
          // Arduino's scan accessors use uint8_t indexes. Keep the strongest
          // distinct names/security modes, omitting hidden (empty) SSIDs.
          for (int i = 0; i < count && i < 256; i++) {
            String ssid = WiFi.SSID(i);
            if (!ssid.length()) continue;
            const int32_t rssi = WiFi.RSSI(i);
            const auto auth = WiFi.encryptionType(i);
            const bool password = auth != WIFI_AUTH_OPEN && auth != WIFI_AUTH_OWE;
            int duplicate = -1;
            for (pb_size_t j = 0; j < wifiNetworkCount; j++) {
              if (ssid == wifiNetworks[j].ssid && password == wifiNetworks[j].password_required) { duplicate = j; break; }
            }
            if (duplicate >= 0) {
              if (wifiNetworks[duplicate].rssi >= rssi) continue;
              for (pb_size_t j = duplicate; j + 1 < wifiNetworkCount; j++) wifiNetworks[j] = wifiNetworks[j + 1];
              wifiNetworkCount--;
            }
            pb_size_t position = 0;
            while (position < wifiNetworkCount && wifiNetworks[position].rssi >= rssi) position++;
            if (position >= 32) continue;
            if (wifiNetworkCount < 32) wifiNetworkCount++;
            for (pb_size_t j = wifiNetworkCount - 1; j > position; j--) wifiNetworks[j] = wifiNetworks[j - 1];
            strlcpy(wifiNetworks[position].ssid, ssid.c_str(), sizeof(wifiNetworks[position].ssid));
            wifiNetworks[position].rssi = rssi;
            wifiNetworks[position].password_required = password;
          }
        }
        WiFi.scanDelete(); wifiScanning = false;
      }
    }
    if (wifiPending && !wifiScanning) { wifiPending = false; WiFi.disconnect(); WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str()); }
    bool restart = rebootPending || (radioRestartAt && int32_t(millis() - radioRestartAt) >= 0);
    xSemaphoreGive(stateMutex);
    if (restart) { delay(200); ESP.restart(); }
  }
  static uint32_t lastBroadcast = 0;
  static uint8_t previousDigest[32] = {};
  if (millis() - lastBroadcast >= 250) {
    lastBroadcast = millis();
    size_t count = 8; int clients[8]; bool subscribed = false;
    if (httpServer && httpd_get_client_list(httpServer, &count, clients) == ESP_OK)
      for (size_t i = 0; i < count; i++) if (httpd_ws_get_fd_info(httpServer, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET) subscribed = true;
    if (!subscribed) { delay(1); return; }
    auto *event = new (std::nothrow) luxio_Response{};
    if (!event) { delay(1); return; }
    std::unique_ptr<uint8_t[]> bytes;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    event->state = &state;
    strlcpy(state.lan_ip, WiFi.localIP().toString().c_str(), sizeof(state.lan_ip));
    const size_t encoded = encodeControlResponse(*event, bytes, 0);
    xSemaphoreGive(stateMutex);
    if (encoded) {
      uint8_t digest[32]; mbedtls_sha256(bytes.get(), encoded, digest, 0);
      if (memcmp(digest, previousDigest, 32)) {
        // Send on the HTTP task so broadcasts cannot interleave headers and
        // payloads with replies or the server's automatic WebSocket PONGs.
        auto *packet = new (std::nothrow) httpd_ws_frame_t{};
        if (packet) {
          packet->type = HTTPD_WS_TYPE_BINARY; packet->payload = bytes.release(); packet->len = encoded;
          const esp_err_t queued = httpd_queue_work(httpServer, [](void *argument) {
            std::unique_ptr<httpd_ws_frame_t> frame(static_cast<httpd_ws_frame_t *>(argument));
            std::unique_ptr<uint8_t[]> payload(frame->payload);
            size_t count = 8; int clients[8];
            if (httpServer && httpd_get_client_list(httpServer, &count, clients) == ESP_OK)
              for (size_t i = 0; i < count; i++) if (httpd_ws_get_fd_info(httpServer, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET) httpd_ws_send_frame_async(httpServer, clients[i], frame.get());
          }, packet);
          if (queued == ESP_OK) memcpy(previousDigest, digest, 32);
          else { delete[] packet->payload; delete packet; }
        }
      }
    }
    delete event;
  }
  delay(1);
}
