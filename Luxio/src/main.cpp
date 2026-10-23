#include "transition.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "update_job.h"
#include "script_storage.h"
#include "animation_parameters.h"
#include "gradient.h"
#include "gradient_storage.h"
#include "color_storage.h"
#include <memory>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <driver/gpio.h>
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
#define LUXIO_BUILD 1
#endif
#ifndef LUXIO_CLOUD_URL
#define LUXIO_CLOUD_URL "https://api.luxio.lighting"
#endif
#ifndef LUXIO_MATTER
#define LUXIO_MATTER 1
#endif
#if LUXIO_MATTER
#include <Matter.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <app/server/OnboardingCodesUtil.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#endif
#if !SOC_WIFI_SUPPORTED || !SOC_BLE_SUPPORTED
#error Luxio requires an ESP32 with integrated Wi-Fi and Bluetooth LE.
#endif

static Preferences prefs;
static luxio_State state = luxio_State_init_zero;
static SemaphoreHandle_t stateMutex;
static httpd_handle_t httpServer;
static mbedtls_pk_context identity;
static String publicKey;
static String pairingCode, pairingQr;
static uint8_t pixels[1024][6];
static OutputTransition transition;
static bool identifyActive = false;
static rmt_data_t *symbols;
static lua_State *animation;
static char *activeAnimationSource;
static size_t luaMemory;
static char animationError[160];
static uint32_t luaDeadline, animationStarted, lastFrame, lastSave;
static int luaInstructions;
static bool dirty, wifiPending, rebootPending;
static String wifiSsid, wifiPassword;
static std::atomic<uint32_t> wifiDisconnectCount{0}, wifiDisconnectReason{0};
static bool wifiScanning, wifiScanPending;
static luxio_WifiNetwork wifiNetworks[32];
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
  free(activeAnimationSource); activeAnimationSource = nullptr; state.animation_revision = 0;
  state.animation_preview_count = 0; memset(state.animation_preview, 0, sizeof(state.animation_preview));
  state.parameters_count = 0; memset(state.parameters, 0, sizeof(state.parameters));
  animation = nullptr; luaMemory = 0; state.active_color_id[0] = 0; state.script_name[0] = 0; state.active_script_id[0] = 0; state.active_gradient_id[0] = 0; state.has_gradient = false; memset(&state.gradient, 0, sizeof(state.gradient)); state.mode = luxio_Mode_COLOR;
}
static void render() {
  transition.render(pixels, state.config.led_count, state.on ? state.brightness : 0, millis(), identifyActive);
  const bool rgbcct = !strcmp(state.config.strip_type, "WS2805");
  const bool rgbw = !strcmp(state.config.strip_type, "WS2814");
  const int channels = rgbcct ? 5 : rgbw ? 4 : 3;
  size_t position = 0;
  for (uint32_t i = 0; i < state.config.led_count; i++) {
    // WS2805: R,G,B,W1(warm),W2(cool); WS2814: R,G,B,W; WS2812: G,R,B.
    const int order[5] = {channels == 3 ? 1 : 0, channels == 3 ? 0 : 1, 2, rgbcct ? 4 : 3, 5};
    for (int c = 0; c < channels; c++) {
      uint8_t value = transition.displayed[i][order[c]];
      for (int bit = 7; bit >= 0; bit--) {
        auto &symbol = symbols[position++];
        symbol.level0 = 1; symbol.duration0 = value & (1 << bit) ? 8 : 3;
        symbol.level1 = 0; symbol.duration1 = value & (1 << bit) ? 4 : 9;
      }
    }
  }
  symbols[position++] = {.duration0 = 3000, .level0 = 0, .duration1 = 3000, .level1 = 0};
  rmtWrite(state.config.gpio, symbols, position, 100);
}
static void applyColor() {
  transition.begin(millis());
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
static void luaHook(lua_State *L, lua_Debug *) {
  luaInstructions -= 1000;
  if (luaInstructions <= 0) luaL_error(L, "Animation exceeded its instruction budget");
  if (int32_t(millis() - luaDeadline) > 0) luaL_error(L, "Animation exceeded its time budget");
}
static String startAnimation(const luxio_Script &script) {
  auto parameters = std::make_unique<luxio_AnimationParameter[]>(16); pb_size_t count = 0;
  const char *parameterError = parseAnimationParameters(script.source, parameters.get(), count);
  if (!parameterError) parameterError = updateAnimationParameters(parameters.get(), count, script.parameters, script.parameters_count);
  auto preview = std::make_unique<luxio_Color[]>(8); pb_size_t previewCount = 0;
  if (!parameterError) parameterError = parseAnimationPreview(script.source, preview.get(), previewCount);
  if (parameterError) return parameterError;
  const size_t sourceSize = strlen(script.source) + 1;
  char *sourceCopy = static_cast<char *>(psramFound() ? ps_malloc(sourceSize) : malloc(sourceSize));
  if (!sourceCopy) return "Unable to allocate animation source";
  memcpy(sourceCopy, script.source, sourceSize);
  stopAnimation();
  activeAnimationSource = sourceCopy;
  state.animation_preview_count = previewCount; memcpy(state.animation_preview, preview.get(), sizeof(state.animation_preview));
  animationError[0] = 0;
  state.parameters_count = count; memcpy(state.parameters, parameters.get(), sizeof(state.parameters));
  const char *source = script.source;
  animation = lua_newstate(luaAllocator, nullptr);
  if (!animation) { stopAnimation(); return "Unable to allocate Lua engine"; }
  lua_State *L = animation;
  luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
  for (const char *name : {"dofile", "loadfile", "load", "collectgarbage", "print"}) { lua_pushnil(L); lua_setglobal(L, name); }
  lua_pushcfunction(L, [](lua_State *L) -> int { lua_pushinteger(L, state.config.led_count); return 1; }); lua_setglobal(L, "luxio_get_pixel_count");
  lua_pushcfunction(L, [](lua_State *L) -> int {
    lua_pushstring(L, !strcmp(state.config.strip_type, "WS2805") ? "RGBCCT" : !strcmp(state.config.strip_type, "WS2814") ? "RGBW" : "RGB");
    return 1;
  }); lua_setglobal(L, "luxio_get_color_type");
  lua_pushcfunction(L, [](lua_State *L) -> int { lua_pushinteger(L, millis() - animationStarted); return 1; }); lua_setglobal(L, "luxio_millis");
  lua_pushcfunction(L, [](lua_State *L) -> int {
    int index = luaL_checkinteger(L, 1);
    if (index < 0 || index >= int(state.config.led_count)) return luaL_error(L, "Pixel index out of range (zero-based)");
    int values[6];
    for (int c = 0; c < 6; c++) { values[c] = luaL_optinteger(L, c + 2, 0); if (values[c] < 0 || values[c] > 255) return luaL_error(L, "Channels must be 0-255"); }
    if ((!strcmp(state.config.strip_type, "WS2805") && values[3]) || (!strcmp(state.config.strip_type, "WS2814") && (values[4] || values[5])) || (!strcmp(state.config.strip_type, "WS2812") && (values[3] || values[4] || values[5]))) return luaL_error(L, "Channel unavailable on this strip type");
    for (int c = 0; c < 6; c++) pixels[index][c] = values[c];
    return 0;
  }); lua_setglobal(L, "luxio_set_pixel_color");
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
  luaInstructions = 100000; luaDeadline = millis() + 30;
  lua_sethook(L, luaHook, LUA_MASKCOUNT, 1000);
  transition.begin(millis());
  animationStarted = millis(); memset(pixels, 0, sizeof(pixels));
  if (luaL_loadstring(L, source) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    String error = lua_tostring(L, -1); stopAnimation(); return error;
  }
  lua_getglobal(L, "setup");
  if (lua_isnil(L, -1)) lua_pop(L, 1);
  else if (lua_pcall(L, 0, 0, 0) != LUA_OK) { String error = lua_tostring(L, -1); stopAnimation(); return error; }
  lua_getglobal(L, "loop"); bool valid = lua_isfunction(L, -1); lua_pop(L, 1);
  if (!valid) { stopAnimation(); return "Define a loop() function"; }
  state.animation_revision = esp_random() | 1;
  return "";
}
static bool validId(const char *id) {
  if (strlen(id) != 36) return false;
  for (int i = 0; i < 36; i++) if ((i == 8 || i == 13 || i == 18 || i == 23) ? id[i] != '-' : !isxdigit(id[i])) return false;
  return true;
}
struct ScriptPage { uint32_t offset; uint32_t *next; };
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
    std::unique_ptr<luxio_Script> script(new (std::nothrow) luxio_Script{});
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
size_t dispatch(const uint8_t *input, size_t length, uint8_t *output) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  auto *request = new luxio_Request{};
  auto *response = new luxio_Response{};
  auto decoder = pb_istream_from_buffer(input, length);
  bool decoded = pb_decode(&decoder, luxio_Request_fields, request);
  response->id = request->id;
  String error;
  ScriptPage page{request->script_offset, &response->next_script_offset};
  ScriptPage gradientPage{request->gradient_offset, &response->next_gradient_offset};
  ScriptPage colorPage{request->color_offset, &response->next_color_offset};
  if (!decoded) error = "Invalid Protobuf request";
  else switch (request->operation) {
    case luxio_Operation_GET_STATE: break;
    case luxio_Operation_SET_COLOR: {
      auto &c = request->color;
      if (!request->has_color || c.red > 255 || c.green > 255 || c.blue > 255 || c.white > 255 || c.warm_white > 255 || c.cool_white > 255) { error = "Invalid color"; break; }
      if ((!strcmp(state.config.strip_type, "WS2805") && c.white) || (!strcmp(state.config.strip_type, "WS2814") && (c.warm_white || c.cool_white)) || (!strcmp(state.config.strip_type, "WS2812") && (c.white || c.warm_white || c.cool_white))) { error = "Channel unavailable on this strip type"; break; }
      stopAnimation(); state.color = c; state.on = true; applyColor(); break;
    }
    case luxio_Operation_SET_BRIGHTNESS:
      if (request->brightness < 1 || request->brightness > 256) error = "Brightness must be 1-256";
      else { if (state.brightness != request->brightness) transition.begin(millis()); state.brightness = request->brightness; dirty = true; }
      break;
    case luxio_Operation_SET_POWER:
      if (state.on != request->on) transition.begin(millis());
      state.on = request->on; dirty = true; break;
    case luxio_Operation_SET_CONFIG: {
      auto &c = request->config;
      String name = c.name; name.trim();
      if (!request->has_config || !name.length() || c.led_count < 1 || c.led_count > 1024 || c.channel > 100 || (!GPIO_IS_VALID_OUTPUT_GPIO(c.gpio)) || (strcmp(c.strip_type, "WS2805") && strcmp(c.strip_type, "WS2814") && strcmp(c.strip_type, "WS2812"))) { error = "Invalid strip configuration"; break; }
      // Flash/PSRAM pins and USB pins cannot be used on this board family.
#if CONFIG_IDF_TARGET_ESP32S3
      if ((c.gpio >= 26 && c.gpio <= 37) || c.gpio == 19 || c.gpio == 20) { error = "GPIO is reserved for flash, PSRAM, or USB"; break; }
#endif
      if (c.gpio != state.config.gpio) {
        if (!rmtInit(c.gpio, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) { error = "Unable to initialize this GPIO"; break; }
        rmtDeinit(state.config.gpio);
      }
      if (c.gpio != state.config.gpio || c.led_count != state.config.led_count || strcmp(c.strip_type, state.config.strip_type)) { stopAnimation(); state.color = {}; memset(pixels, 0, sizeof(pixels)); memset(transition.displayed, 0, sizeof(transition.displayed)); transition.active = false; }
      state.config = c; strlcpy(state.config.name, name.c_str(), sizeof(state.config.name)); dirty = true;
      MDNS.setInstanceName(state.config.name);
      MDNS.addServiceTxt("luxio", "tcp", "strip", (const char *)c.strip_type);
      break;
    }
    case luxio_Operation_SET_WIFI:
      if (!request->has_wifi || !strlen(request->wifi.ssid)) error = "Wi-Fi SSID is required";
      else { prefs.putString("ssid", request->wifi.ssid); prefs.putString("password", request->wifi.password); wifiSsid = request->wifi.ssid; wifiPassword = request->wifi.password; wifiPending = true; }
      break;
    case luxio_Operation_SCAN_WIFI:
      if (!wifiScanning) {
        wifiScanning = wifiScanPending = true;
        wifiNetworkCount = 0; wifiScanError = "";
      }
      break;
    case luxio_Operation_SET_GRADIENT:
      if (!request->has_gradient || !validGradient(request->gradient, state.config.strip_type)) { error = "Gradient requires 2–8 valid colors for this strip"; break; }
      stopAnimation(); transition.begin(millis());
      state.gradient = request->gradient; state.gradient.id[0] = 0; state.gradient.name[0] = 0;
      state.has_gradient = true; state.mode = luxio_Mode_GRADIENT; state.on = true;
      renderGradient(state.gradient, pixels, state.config.led_count); break;
    case luxio_Operation_LIST_GRADIENTS: response->gradients.funcs.encode = listGradients; response->gradients.arg = &gradientPage; break;
    case luxio_Operation_CREATE_GRADIENT:
    case luxio_Operation_UPDATE_GRADIENT: {
      auto &gradient = request->gradient;
      String name = gradient.name; name.trim();
      if (!request->has_gradient || !name.length() || !validGradient(gradient, state.config.strip_type)) { error = "Gradient requires a name and 2–8 valid colors for this strip"; break; }
      strlcpy(gradient.name, name.c_str(), sizeof(gradient.name));
      if (request->operation == luxio_Operation_CREATE_GRADIENT) {
        uint8_t uuid[16]; esp_fill_random(uuid, 16); uuid[6] = (uuid[6] & 15) | 64; uuid[8] = (uuid[8] & 63) | 128;
        snprintf(gradient.id, sizeof(gradient.id), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
      } else if (!validId(gradient.id) || !LittleFS.exists((String("/gradients/") + gradient.id + ".pb").c_str())) { error = "Gradient not found"; break; }
      const size_t storedSize = encodeStoredGradient(gradient, output, 32768);
      if (!storedSize) { error = "Unable to encode gradient"; break; }
      if (LittleFS.totalBytes() - LittleFS.usedBytes() < storedSize + 8192) { error = "Storage is full; delete a saved animation or gradient"; break; }
      String path = String("/gradients/") + gradient.id + ".pb";
      File file = LittleFS.open("/gradients/pending", "w");
      bool written = file && file.write(output, storedSize) == storedSize;
      file.flush(); file.close();
      File verify = LittleFS.open("/gradients/pending", "r");
      written = written && verify && verify.size() == storedSize && verify.read(output, storedSize) == storedSize &&
        decodeStoredGradient(output, storedSize, response->gradient) && !strcmp(gradient.id, response->gradient.id) &&
        !strcmp(gradient.name, response->gradient.name) && !strcmp(gradient.editor_metadata, response->gradient.editor_metadata) &&
        gradient.colors_count == response->gradient.colors_count && !memcmp(gradient.colors, response->gradient.colors, gradient.colors_count * sizeof(luxio_Color));
      verify.close();
      if (!written || !LittleFS.rename("/gradients/pending", path)) { LittleFS.remove("/gradients/pending"); error = "Unable to save gradient (storage full or write failed)"; break; }
      response->has_gradient = true; response->gradient = gradient; break;
    }
    case luxio_Operation_GET_GRADIENT:
    case luxio_Operation_RUN_GRADIENT:
    case luxio_Operation_DELETE_GRADIENT: {
      if (!validId(request->gradient_id)) { error = "Invalid gradient ID"; break; }
      String path = String("/gradients/") + request->gradient_id + ".pb";
      if (!LittleFS.exists(path)) { error = "Gradient not found"; break; }
      if (request->operation == luxio_Operation_DELETE_GRADIENT) {
        if (!LittleFS.remove(path)) { error = "Unable to delete gradient"; break; }
        if (!strcmp(state.active_gradient_id, request->gradient_id)) { stopAnimation(); applyColor(); dirty = true; }
        break;
      }
      File file = LittleFS.open(path, "r"); size_t size = file.size();
      if (!size || size > GRADIENT_RECORD_MAX || file.read(output, size) != size || !decodeStoredGradient(output, size, response->gradient)) { error = "Invalid saved gradient"; break; }
      if (request->operation == luxio_Operation_GET_GRADIENT) response->has_gradient = true;
      else {
        if (!validGradient(response->gradient, state.config.strip_type)) { error = "Gradient contains channels unavailable on this strip"; break; }
        stopAnimation(); transition.begin(millis());
        state.gradient = response->gradient; state.has_gradient = true;
        strlcpy(state.active_gradient_id, request->gradient_id, sizeof(state.active_gradient_id));
        state.mode = luxio_Mode_GRADIENT; state.on = true; dirty = true;
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
      const size_t storedSize = encodeStoredColor(saved_color, output, 32768);
      if (!storedSize) { error = "Unable to encode color"; break; }
      if (LittleFS.totalBytes() - LittleFS.usedBytes() < storedSize + 8192) { error = "Storage is full; delete a saved animation or color"; break; }
      String path = String("/colors/") + saved_color.id + ".pb";
      File file = LittleFS.open("/colors/pending", "w");
      bool written = file && file.write(output, storedSize) == storedSize;
      file.flush(); file.close();
      File verify = LittleFS.open("/colors/pending", "r");
      written = written && verify && verify.size() == storedSize && verify.read(output, storedSize) == storedSize &&
        decodeStoredColor(output, storedSize, response->saved_color) && !strcmp(saved_color.id, response->saved_color.id) &&
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
        if (!strcmp(state.active_color_id, request->color_id)) { stopAnimation(); applyColor(); dirty = true; }
        break;
      }
      File file = LittleFS.open(path, "r"); size_t size = file.size();
      if (!size || size > COLOR_RECORD_MAX || file.read(output, size) != size || !decodeStoredColor(output, size, response->saved_color)) { error = "Invalid saved color"; break; }
      if (request->operation == luxio_Operation_GET_COLOR) response->has_saved_color = true;
      else {
        if (!validColor(response->saved_color.color, state.config.strip_type)) { error = "Color contains channels unavailable on this strip"; break; }
        stopAnimation(); transition.begin(millis());
        state.color = response->saved_color.color;
        strlcpy(state.active_color_id, request->color_id, sizeof(state.active_color_id));
        state.mode = luxio_Mode_COLOR; state.on = true; dirty = true;
        applyColor();
      }
      break;
    }
    case luxio_Operation_STOP_GRADIENT:
      if (state.mode == luxio_Mode_GRADIENT) { stopAnimation(); applyColor(); dirty = true; }
      break;
    case luxio_Operation_LIST_SCRIPTS: response->scripts.funcs.encode = listScripts; response->scripts.arg = &page; break;
    case luxio_Operation_CREATE_SCRIPT:
    case luxio_Operation_UPDATE_SCRIPT: {
      auto &script = request->script;
      if (script.mode != luxio_Mode_COLOR && script.mode != luxio_Mode_ANIMATION) { error = "Use gradient commands for gradients"; break; }
      // Legacy clients omit mode; their scripts retain animation behavior.
      if (script.mode == luxio_Mode_COLOR) script.mode = luxio_Mode_ANIMATION;
      if (!request->has_script || !strlen(script.name) || !strlen(script.source)) { error = "Animation name and source are required"; break; }
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
      const size_t storedSize = encodeStoredScript(script, output, 32768);
      if (!storedSize) { error = "Unable to encode animation"; break; }
      // LittleFS needs free blocks for atomic replacement and metadata updates.
      if (LittleFS.totalBytes() - LittleFS.usedBytes() < storedSize + 8192) { error = "Storage is full; delete a saved color, gradient, or animation"; break; }
      String path = String("/scripts/") + script.id + ".pb";
      File file = LittleFS.open("/scripts/pending", "w");
      bool written = file && file.write(output, storedSize) == storedSize;
      file.flush(); file.close();
      // Verify the actual stored bytes before replacing an existing script.
      File verify = LittleFS.open("/scripts/pending", "r");
      written = written && verify && verify.size() == storedSize && verify.read(output, storedSize) == storedSize &&
        decodeStoredScript(output, storedSize, response->script) && !strcmp(response->script.source, script.source) &&
        !strcmp(response->script.id, script.id) && !strcmp(response->script.name, script.name) && response->script.mode == script.mode && response->script.preview_count == script.preview_count && !memcmp(response->script.preview, script.preview, script.preview_count * sizeof(luxio_Color)) && response->script.parameters_count == script.parameters_count && !memcmp(response->script.parameters, script.parameters, sizeof(script.parameters));
      verify.close();
      if (!written || !LittleFS.rename("/scripts/pending", path)) { LittleFS.remove("/scripts/pending"); error = "Unable to save animation (storage full or write failed)"; break; }
      response->has_script = true; response->script = script; break;
    }
    case luxio_Operation_GET_SCRIPT:
      if (!request->script_id[0]) {
        if (!animation || !activeAnimationSource || state.mode != luxio_Mode_ANIMATION) { error = "No animation is running"; break; }
        response->has_script = true;
        auto &script = response->script;
        strlcpy(script.id, state.active_script_id, sizeof(script.id));
        strlcpy(script.name, state.script_name, sizeof(script.name));
        strlcpy(script.source, activeAnimationSource, sizeof(script.source));
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
        if (!LittleFS.remove(path)) error = "Unable to delete animation"; break;
      }
      File file = LittleFS.open(path, "r"); size_t size = file.size();
      if (!size || size > SCRIPT_RECORD_MAX || file.read(output, size) != size) { error = "Unable to read animation"; break; }
      if (!decodeStoredScript(output, size, response->script)) { error = "Invalid saved animation"; break; }
      if (request->operation == luxio_Operation_GET_SCRIPT) response->has_script = true;
      else {
        if (response->script.mode == luxio_Mode_GRADIENT) { error = "Legacy Lua gradient: migrate it to gradient storage"; break; }
        error = startAnimation(response->script);
        if (!error.length()) {
          state.on = true; dirty = true;
          strlcpy(state.active_script_id, request->script_id, sizeof(state.active_script_id));
          state.mode = luxio_Mode_ANIMATION;
          strlcpy(state.script_name, response->script.name, sizeof(state.script_name));
        } else if (!animation) { applyColor(); }
        memset(&response->script, 0, sizeof(response->script));
      }
      break;
    }
    case luxio_Operation_SET_ANIMATION_PARAMETERS: {
      if (!animation || state.mode != luxio_Mode_ANIMATION) { error = "No animation is running"; break; }
      if ((!request->script_id[0] && (!request->has_script || !request->script.name[0])) ||
          (request->script_id[0] && strcmp(request->script_id, state.active_script_id)) ||
          (request->has_script && request->script.name[0] && strcmp(request->script.name, state.script_name))) { error = "The active animation changed"; break; }
      const char *parameterError = updateAnimationParameters(state.parameters, state.parameters_count, request->parameters, request->parameters_count);
      if (parameterError) error = parameterError;
      break;
    }
    case luxio_Operation_SET_SCRIPT:
      if (!request->has_script || !request->script.source[0] || (request->script.mode != luxio_Mode_COLOR && request->script.mode != luxio_Mode_ANIMATION)) { error = "Provide animation Lua source"; break; }
      error = startAnimation(request->script);
      if (!error.length()) { state.on = true; state.mode = luxio_Mode_ANIMATION; strlcpy(state.script_name, request->script.name, sizeof(state.script_name)); }
      else if (!animation) applyColor();
      break;
    case luxio_Operation_STOP_SCRIPT: if (request->has_script && request->script.name[0] && strcmp(request->script.name, state.script_name)) break; if (state.mode == luxio_Mode_ANIMATION) { stopAnimation(); applyColor(); dirty = true; } break;
    case luxio_Operation_GET_PAIRING_CODE:
      if (!pairingCode.length()) error = "Matter is not ready";
      else { strlcpy(response->pairing_code, pairingCode.c_str(), sizeof(response->pairing_code)); strlcpy(response->pairing_qr, pairingQr.c_str(), sizeof(response->pairing_qr)); }
      break;
    case luxio_Operation_CHECK_UPDATE:
      if (!updateChecking) { checkUpdate = true; updateChecking = true; availableBuild = 0; availableUpdate = {}; updateError = ""; }
      break;
    case luxio_Operation_INSTALL_UPDATE: {
      const auto *loader = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "loader");
      const auto *running = esp_ota_get_running_partition();
      if (rebootPending) error = "Update already starting";
      else if (!availableBuild || !validUpdateJob(availableUpdate)) error = "No eligible update; check for updates first";
      else if (!loader || loader->address != 0x10000 || !running || running->address != 0x130000) error = "Recovery layout required; install via USB first";
      else {
        // Save the current controls before leaving the main application.
        auto persistent = std::make_unique<luxio_State>(state);
        persistent->animation_preview_count = 0; memset(persistent->animation_preview, 0, sizeof(persistent->animation_preview));
        persistent->parameters_count = 0; memset(persistent->parameters, 0, sizeof(persistent->parameters));
        if (!persistent->active_color_id[0]) persistent->color = {};
        if (!persistent->active_gradient_id[0]) persistent->has_gradient = false;
        if (!persistent->active_script_id[0]) persistent->script_name[0] = 0;
        if (!persistent->active_gradient_id[0] && !persistent->active_script_id[0]) persistent->mode = luxio_Mode_COLOR;
        auto saved = pb_ostream_from_buffer(output, 32768);
        if (!pb_encode(&saved, luxio_State_fields, persistent.get()) || prefs.putBytes("state", output, saved.bytes_written) != saved.bytes_written) error = "Unable to save device settings";
        else if (prefs.putBytes("ota-job", &availableUpdate, sizeof(availableUpdate)) != sizeof(availableUpdate)) error = "Unable to save update request";
        else if (esp_ota_set_boot_partition(loader) != ESP_OK) { prefs.remove("ota-job"); error = "Unable to select recovery loader"; }
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
    memcpy(response->wifi_networks, wifiNetworks, wifiNetworkCount * sizeof(luxio_WifiNetwork));
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
      snprintf(network.bssid, sizeof(network.bssid), "%02x:%02x:%02x:%02x:%02x:%02x", ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    }
    wifi_ps_type_t powerSave = WIFI_PS_NONE;
    esp_wifi_get_ps(&powerSave); network.power_save = powerSave;
    network.uptime_ms = millis();
    network.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    network.internal_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    network.disconnect_count = wifiDisconnectCount.load(); network.last_disconnect_reason = wifiDisconnectReason.load();
  }
  if (error.length()) strlcpy(response->error, error.c_str(), sizeof(response->error));
  else { response->has_state = true; response->state = state; strlcpy(response->state.lan_ip, WiFi.localIP().toString().c_str(), sizeof(response->state.lan_ip)); }
  auto encoder = pb_ostream_from_buffer(output, 32768);
  size_t size = pb_encode(&encoder, luxio_Response_fields, response) ? encoder.bytes_written : 0;
  if (!size) {
    memset(response, 0, sizeof(*response));
    response->id = request->id;
    strlcpy(response->error, "Unable to read saved animations", sizeof(response->error));
    encoder = pb_ostream_from_buffer(output, 32768);
    size = pb_encode(&encoder, luxio_Response_fields, response) ? encoder.bytes_written : 0;
  }
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
  auto *input = new uint8_t[request->content_len]; auto *output = new uint8_t[32768];
  size_t received = 0;
  while (received < request->content_len) {
    int size = httpd_req_recv(request, reinterpret_cast<char *>(input + received), request->content_len - received);
    if (size <= 0) { delete[] input; delete[] output; return ESP_FAIL; }
    received += size;
  }
  size_t length = dispatch(input, received, output);
  httpd_resp_set_type(request, "application/x-protobuf");
  auto result = httpd_resp_send(request, reinterpret_cast<char *>(output), length);
  delete[] input; delete[] output; return result;
}
static esp_err_t websocketHandler(httpd_req_t *request) {
  if (request->method == HTTP_GET) return ESP_OK;
  httpd_ws_frame_t packet = {};
  if (httpd_ws_recv_frame(request, &packet, 0) != ESP_OK || packet.len > 32768 || packet.type != HTTPD_WS_TYPE_BINARY) return ESP_FAIL;
  auto *input = new uint8_t[packet.len]; auto *output = new uint8_t[32768]; packet.payload = input;
  if (httpd_ws_recv_frame(request, &packet, packet.len) != ESP_OK) { delete[] input; delete[] output; return ESP_FAIL; }
  size_t length = dispatch(input, packet.len, output);
  packet.payload = output; packet.len = length;
  auto result = httpd_ws_send_frame(request, &packet);
  delete[] input; delete[] output; return result;
}
static void cloudTask(void *) {
  uint32_t lastAnnouncement = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (!lastAnnouncement || uint32_t(millis() - lastAnnouncement) >= 900000) {
      lastAnnouncement = millis();
      auto snapshot = std::make_unique<luxio_State>();
      xSemaphoreTake(stateMutex, portMAX_DELAY); *snapshot = state; xSemaphoreGive(stateMutex);
      NetworkClientSecure tls; tls.setInsecure(); // Deliberately supported for deployments without a clock/trust store.
      HTTPClient http; http.setTimeout(8000);
      String base = LUXIO_CLOUD_URL;
      if (http.begin(tls, base + "/announcements/challenge")) {
        http.addHeader("Content-Type", "application/json");
        String body = String("{\"deviceId\":\"") + snapshot->device_id + "\"}";
        int status = http.POST(body);
        if (status == 200) {
          cJSON *challenge = cJSON_Parse(http.getString().c_str());
          auto nonceItem = cJSON_GetObjectItem(challenge, "nonce");
          String nonce = cJSON_IsString(nonceItem) ? nonceItem->valuestring : "";
          cJSON_Delete(challenge); http.end();
          if (nonce.length() == 64) {
            String ip = WiFi.localIP().toString();
            cJSON *payload = cJSON_CreateArray();
            cJSON_AddItemToArray(payload, cJSON_CreateNumber(1)); cJSON_AddItemToArray(payload, cJSON_CreateString(nonce.c_str()));
            cJSON_AddItemToArray(payload, cJSON_CreateString(snapshot->device_id)); cJSON_AddItemToArray(payload, cJSON_CreateString(snapshot->config.name));
            cJSON_AddItemToArray(payload, cJSON_CreateString(ip.c_str())); cJSON_AddItemToArray(payload, cJSON_CreateNumber(snapshot->build));
            cJSON_AddItemToArray(payload, cJSON_CreateNumber(snapshot->config.channel));
            char *canonical = cJSON_PrintUnformatted(payload);
            uint8_t digest[32], signature[80]; size_t signatureLength = 0;
            mbedtls_sha256(reinterpret_cast<const uint8_t *>(canonical), strlen(canonical), digest, 0);
            int signedOk = mbedtls_pk_sign(&identity, MBEDTLS_MD_SHA256, digest, 32, signature, sizeof(signature), &signatureLength, hardwareRandom, nullptr);
            free(canonical); cJSON_Delete(payload);
            uint8_t base64[128]; size_t base64Length = 0;
            mbedtls_base64_encode(base64, sizeof(base64), &base64Length, signature, signatureLength); base64[base64Length] = 0;
            if (!signedOk && http.begin(tls, base + "/announcements")) {
              cJSON *body = cJSON_CreateObject();
              cJSON_AddStringToObject(body, "deviceId", snapshot->device_id); cJSON_AddStringToObject(body, "publicKey", publicKey.c_str());
              cJSON_AddStringToObject(body, "nonce", nonce.c_str()); cJSON_AddStringToObject(body, "name", snapshot->config.name);
              cJSON_AddStringToObject(body, "lanIp", ip.c_str()); cJSON_AddNumberToObject(body, "build", snapshot->build);
              cJSON_AddNumberToObject(body, "channel", snapshot->config.channel); cJSON_AddStringToObject(body, "signature", reinterpret_cast<char *>(base64));
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
      NetworkClientSecure tls; tls.setInsecure(); HTTPClient http; http.setTimeout(10000);
      String url = String(LUXIO_CLOUD_URL) + "/updates?chip=" CONFIG_IDF_TARGET "&target=" + LUXIO_UPDATE_TARGET + "&build=" + LUXIO_BUILD + "&channel=" + channel;
      String failure; UpdateJob candidate;
      if (http.begin(tls, url)) {
        int status = http.GET();
        if (status == 200) {
          cJSON *doc = cJSON_Parse(http.getString().c_str());
          if (!doc) failure = "Invalid update response";
          else if (!cJSON_IsNull(doc)) {
            auto b = cJSON_GetObjectItem(doc, "build"), u = cJSON_GetObjectItem(doc, "url"), s = cJSON_GetObjectItem(doc, "sha256"), n = cJSON_GetObjectItem(doc, "size"), c = cJSON_GetObjectItem(doc, "channel"), chip = cJSON_GetObjectItem(doc, "chip"), target = cJSON_GetObjectItem(doc, "target");
            if (!cJSON_IsNumber(b) || b->valuedouble != b->valueint || b->valueint <= LUXIO_BUILD || !cJSON_IsNumber(n) || n->valuedouble != n->valueint || n->valueint < 288 || n->valueint > LUXIO_APP_SIZE || !cJSON_IsNumber(c) || c->valuedouble != c->valueint || c->valueint > channel || c->valueint < 0 || !cJSON_IsString(chip) || strcmp(chip->valuestring, CONFIG_IDF_TARGET) || !cJSON_IsString(u) || strlen(u->valuestring) >= sizeof(candidate.url) || !cJSON_IsString(s) || strlen(s->valuestring) != 64 || !cJSON_IsString(target) || strcmp(target->valuestring, LUXIO_UPDATE_TARGET)) failure = "Invalid or incompatible update manifest";
            else {
              candidate.build = b->valueint; candidate.size = n->valueint;
              strlcpy(candidate.url, u->valuestring, sizeof(candidate.url)); strlcpy(candidate.sha256, s->valuestring, sizeof(candidate.sha256)); strlcpy(candidate.target, target->valuestring, sizeof(candidate.target));
              if (!validUpdateJob(candidate)) failure = "Invalid update manifest";
            }
          }
          cJSON_Delete(doc);
        } else if (status < 0) {
          char detail[100] = {};
          int tlsError = tls.lastError(detail, sizeof(detail));
          failure = tlsError ? String("Update TLS error: ") + detail : String("Update connection failed: ") + HTTPClient::errorToString(status);
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
extern void startBluetooth();
void setup() {
  Serial.begin(115200); // Binary protocol only; diagnostics must never be written here.
  // Keep both large TLS buffers and small crypto allocations in PSRAM.
  // Normal calloc still places small objects in internal RAM; a handshake
  // can otherwise starve Wi-Fi even when Lua already uses PSRAM.
  // Install once before identity, Wi-Fi and Matter initialize crypto.
  if (psramFound()) mbedtls_platform_set_calloc_free(
    [](size_t count, size_t size) -> void * { return ps_calloc(count, size); }, free);
  stateMutex = xSemaphoreCreateMutex();
  prefs.begin("luxio", false);
  state.build = LUXIO_BUILD; state.has_config = true; state.has_color = true;
  state.config = {"Luxio", "WS2805", 1, 16, 0}; state.brightness = 16; state.on = true;
  size_t savedSize = prefs.getBytesLength("state");
  if (savedSize && savedSize < 4096) {
    std::unique_ptr<uint8_t[]> saved(new uint8_t[savedSize]); prefs.getBytes("state", saved.get(), savedSize);
    auto decoder = pb_istream_from_buffer(saved.get(), savedSize); pb_decode(&decoder, luxio_State_fields, &state);
    if (state.build < 11) {
      state.on = state.brightness > 0;
      state.brightness = state.brightness ? (state.brightness * 256 + 127) / 255 : 16;
    }
    state.brightness = constrain(state.brightness, 1, 256);
    state.build = LUXIO_BUILD; state.active_script_id[0] = 0;
    if (state.mode != luxio_Mode_GRADIENT || !state.has_gradient || !validGradient(state.gradient, state.config.strip_type)) { state.mode = luxio_Mode_COLOR; state.has_gradient = false; state.active_gradient_id[0] = 0; }
  }
  state.animation_preview_count = 0; memset(state.animation_preview, 0, sizeof(state.animation_preview));
  state.parameters_count = 0; memset(state.parameters, 0, sizeof(state.parameters));
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
  // A fresh filesystem is formatted only when it has no existing mountable content.
  if (!LittleFS.begin(true)) abort();
  LittleFS.mkdir("/scripts");
  LittleFS.mkdir("/gradients"); LittleFS.mkdir("/colors"); LittleFS.remove("/colors/pending"); LittleFS.remove("/gradients/pending");
  LittleFS.remove("/scripts/pending"); // Discard an interrupted save; the original remains intact.
  symbols = reinterpret_cast<rmt_data_t *>(ps_malloc((1024 * 40 + 1) * sizeof(rmt_data_t)));
  if (!symbols) symbols = reinterpret_cast<rmt_data_t *>(malloc((1024 * 40 + 1) * sizeof(rmt_data_t)));
  if (!symbols || !rmtInit(state.config.gpio, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) abort();
  if (state.mode == luxio_Mode_GRADIENT) renderGradient(state.gradient, pixels, state.config.led_count);
  else applyColor();
  render();
  // The strip is externally powered: keep the radio awake for local controls
  // and Matter, and choose the strongest AP across all channels on each join.
  WiFi.setSleep(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    wifiDisconnectCount.fetch_add(1);
    wifiDisconnectReason.store(info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  wifiSsid = prefs.getString("ssid"); wifiPassword = prefs.getString("password");
  if (wifiSsid.length()) WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.stack_size = 8192; config.max_uri_handlers = 6; config.lru_purge_enable = true;
  if (httpd_start(&httpServer, &config) != ESP_OK) abort();
  httpd_uri_t api = {}; api.uri = "/api"; api.method = HTTP_POST; api.handler = httpHandler; httpd_register_uri_handler(httpServer, &api);
  api.method = HTTP_OPTIONS; httpd_register_uri_handler(httpServer, &api);
  httpd_uri_t websocket = {}; websocket.uri = "/api/ws"; websocket.method = HTTP_GET; websocket.handler = websocketHandler; websocket.is_websocket = true; httpd_register_uri_handler(httpServer, &websocket);
#if LUXIO_MATTER
  if (!matterLight.begin(state.on, {0, 0, 254}, constrain((state.brightness * 254 + 128) / 256, 1, 254), 370)) abort();
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
    if (state.brightness != level) transition.begin(millis());
    state.brightness = level; dirty = true; xSemaphoreGive(stateMutex); return true;
  });
  matterLight.onChangeOnOff([](bool on) {
    xSemaphoreTake(stateMutex, portMAX_DELAY); transition.begin(millis()); state.on = on; dirty = true; xSemaphoreGive(stateMutex); return true;
  });
  matterLight.onChangeColorHSV([](espHsvColor_t hsv) {
    hsv.v = 255; auto rgb = espHsvColorToRgbColor(hsv);
    xSemaphoreTake(stateMutex, portMAX_DELAY); stopAnimation(); state.on = true; state.color = {rgb.r, rgb.g, rgb.b, 0, 0, 0}; applyColor(); dirty = true; xSemaphoreGive(stateMutex); return true;
  });
  matterLight.onChangeColorTemperature([](uint16_t mired) {
    xSemaphoreTake(stateMutex, portMAX_DELAY); stopAnimation(); state.on = true;
    uint32_t warm = constrain((int(mired) - 153) * 255 / (500 - 153), 0, 255);
    if (!strcmp(state.config.strip_type, "WS2805")) state.color = {0, 0, 0, 0, warm, 255 - warm};
    else if (!strcmp(state.config.strip_type, "WS2814")) state.color = {0, 0, 0, 255, 0, 0};
    else state.color = {255, uint32_t(255 - warm * 90 / 255), uint32_t(255 - warm * 180 / 255), 0, 0, 0};
    applyColor(); dirty = true; xSemaphoreGive(stateMutex); return true;
  });
  // Read the actual provider payload; unsupported setters cannot personalize it.
  chip::PayloadContents payload;
  if (GetPayloadContents(payload, chip::RendezvousInformationFlag::kOnNetwork) == CHIP_NO_ERROR) {
    char manual[32] = {}, qr[128] = {};
    chip::MutableCharSpan manualSpan(manual), qrSpan(qr);
    GetManualPairingCode(manualSpan, payload); GetQRCode(qrSpan, payload);
    pairingCode = manual; pairingQr = qr;
  }
#endif
  startBluetooth();
  xTaskCreate(cloudTask, "luxio-cloud", 12288, nullptr, 1, nullptr);
  // Reaching initialized controls/Matter acknowledges the completed request.
  // No timer, boot path or Matter callback checks for or starts a new update.
  esp_ota_mark_app_valid_cancel_rollback();
  prefs.remove("ota-job");
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
  static uint8_t *input = new uint8_t[32768];
  static uint8_t *output = new uint8_t[32768];
  static uint32_t expected = 0, received = 0, header = 0, lastByte = 0;
  if (header && millis() - lastByte > 3000) { received = 0; expected = 0; header = 0; }
  while (Serial.available()) {
    uint8_t byte = Serial.read(); lastByte = millis();
    if (header < 4) {
      const uint8_t magic[4] = {0x4c, 0x58, 0x50, 0x31};
      header = byte == magic[header] ? header + 1 : byte == magic[0] ? 1 : 0;
      continue;
    }
    if (header < 8) {
      expected = (expected << 8) | byte; header++;
      if (header == 8 && (!expected || expected > 32768)) { expected = 0; header = 0; received = 0; }
      continue;
    }
    input[received++] = byte;
    if (received == expected) {
      size_t length = dispatch(input, received, output);
      uint8_t prefix[8] = {0x4c, 0x58, 0x50, 0x31, uint8_t(length >> 24), uint8_t(length >> 16), uint8_t(length >> 8), uint8_t(length)};
      Serial.write(prefix, 8); Serial.write(output, length);
      received = 0; expected = 0; header = 0;
    }
  }
  if (millis() - lastFrame >= 33) {
    lastFrame = millis(); xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (animation) {
      luaInstructions = 100000; luaDeadline = millis() + 20; lua_getglobal(animation, "loop");
      if (lua_pcall(animation, 0, 0, 0) != LUA_OK) {
        const char *error = lua_tostring(animation, -1);
        strlcpy(animationError, error ? error : "Animation failed without an error message", sizeof(animationError));
        stopAnimation(); applyColor();
      }
    }
    render();
    if (dirty && millis() - lastSave >= 1000) {
      auto persistent = std::make_unique<luxio_State>(state);
      persistent->animation_preview_count = 0; memset(persistent->animation_preview, 0, sizeof(persistent->animation_preview));
      persistent->parameters_count = 0; memset(persistent->parameters, 0, sizeof(persistent->parameters));
      if (!persistent->active_color_id[0]) persistent->color = {};
      if (!persistent->active_gradient_id[0]) persistent->has_gradient = false;
      if (!persistent->active_script_id[0]) persistent->script_name[0] = 0;
      if (!persistent->active_gradient_id[0] && !persistent->active_script_id[0]) persistent->mode = luxio_Mode_COLOR;
      auto encoder = pb_ostream_from_buffer(output, 32768);
      if (pb_encode(&encoder, luxio_State_fields, persistent.get())) prefs.putBytes("state", output, encoder.bytes_written);
      lastSave = millis(); dirty = false;
    }
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
    bool restart = rebootPending;
    xSemaphoreGive(stateMutex);
    if (restart) { delay(200); ESP.restart(); }
  }
  static uint32_t lastBroadcast = 0;
  static uint8_t previousDigest[32] = {};
  if (millis() - lastBroadcast >= 250) {
    lastBroadcast = millis();
    auto *event = new luxio_Response{};
    xSemaphoreTake(stateMutex, portMAX_DELAY); event->has_state = true; event->state = state; xSemaphoreGive(stateMutex);
    strlcpy(event->state.lan_ip, WiFi.localIP().toString().c_str(), sizeof(event->state.lan_ip));
    static uint8_t bytes[4096]; auto encoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
    if (pb_encode(&encoder, luxio_Response_fields, event)) {
      uint8_t digest[32]; mbedtls_sha256(bytes, encoder.bytes_written, digest, 0);
      if (memcmp(digest, previousDigest, 32)) {
        memcpy(previousDigest, digest, 32);
        size_t count = 8; int clients[8];
        if (httpd_get_client_list(httpServer, &count, clients) == ESP_OK) {
          httpd_ws_frame_t packet = {}; packet.type = HTTPD_WS_TYPE_BINARY; packet.payload = bytes; packet.len = encoder.bytes_written;
          for (size_t i = 0; i < count; i++) if (httpd_ws_get_fd_info(httpServer, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET) httpd_ws_send_frame_async(httpServer, clients[i], &packet);
        }
      }
    }
    delete event;
  }
  delay(1);
}
