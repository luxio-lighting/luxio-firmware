/*
 * Enums
 */
enum LedType {
  WS2812,
  SK6812,
};

/*
 * Includes
 */
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTimer.h>
#include <EEvar.h>

#include "AsyncJson.h"
#include "ESPAsyncWebServer.h"
#ifdef ESP32
#include <AsyncTCP.h>
#include <WiFi.h>
#endif
#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#endif

/*
 * Defines
 */

// Software Version
#define VERSION 103

// Platform
#ifdef ESP32
#define PLATFORM "ESP32"
#elif ESP8266
#define PLATFORM "ESP8266"
#endif

// Default Config
#define DEFAULT_LED_COUNT 60
#define DEFAULT_LED_BRIGHTNESS 50
#define DEFAULT_LED_TYPE LedType::SK6812
#ifdef ESP32
#define DEFAULT_LED_PIN 16
#elif ESP8266
#define DEFAULT_LED_PIN 0
#else
#define DEFAULT_LED_PIN 2
#endif

/*
 * Structs
 */

struct APIResponse {
  String err = "";
  JsonDocument result;
};

struct ColorRGBW {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t w = 0;
};

/*
 * Headers
 */
namespace nupnp {
  void sync();
  bool is_syncing = false;
}  // namespace nupnp
namespace ota {
  void sync();
  bool is_syncing = false;
}  // namespace ota
JsonDocument get_full_state();
JsonDocument handle_request(int req_id, String method, JsonVariant params);
void emit_event(String event, JsonDocument &data);
void emit_event(String event);

/*
 * Globals
 */
AsyncWebServer *http_server = NULL;
AsyncWebSocket *http_websocket = NULL;
Adafruit_NeoPixel *strip = NULL;
AsyncTimer timer;
bool debug_enabled = true;

/*
 * Persistent Config
 */

struct Config {
  int led_count = DEFAULT_LED_COUNT;
  int led_pin = DEFAULT_LED_PIN;
  LedType led_type = DEFAULT_LED_TYPE;
  char wifi_ssid[32];
  char wifi_pass[64];
  char name[32];
};
EEvar<Config> config((Config()));

/*
 * Debug
 */

void debug(String nsp, String message) {
  if (debug_enabled == false) {
    return;
  }

  JsonDocument doc;
  doc["debug"] = "[" + nsp + "] " + message;

  // Serialize
  String output;
  serializeJson(doc, output);

  // Print to Serial
  if (Serial) {
    Serial.println(output);
  }

  // Send to SSE
  if (http_websocket != NULL) {
    http_websocket->textAll(output);
  }
}

/*
 * System
 */
namespace sys {

  void emit_state();
  void emit_config();

  void debug(String message) {
    ::debug("system", message);
  }

  String get_device_mac() {
    String mac = WiFi.macAddress();
    mac.toUpperCase();
    return mac;
  }

  String get_device_name() {
    // Get MAC address as XXXXXXXXXXXX
    String mac = get_device_mac();
    mac.replace(":", "");

    // Calculate default name as Luxio-XXXXXX
    String name = "Luxio-" + mac.substring(mac.length() - 6);

    return name;
  }

  String get_id() {
    return get_device_mac();
  }

  String get_name() {
    return config->name;
  }

  void set_name(String name) {
    // Save name to EEPROM
    strncpy(config->name, name.c_str(), sizeof(Config::name));
    config.save();

    // Update MDNS
    MDNS.addServiceTxt("luxio", "tcp", "name", String(config->name));

    // Update nupnp
    timer.setTimeout(nupnp::sync, 1000);

    emit_config();
  }

  void restart() {
    timer.setTimeout(ESP.restart, 1000);
  }

  void factory_reset() {
    // Write 0x00 to the entire EEPROM
    EEPROM.begin(EEPROM.length());
    for (size_t i = 0; i < EEPROM.length(); i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();

    restart();
  }

  void enable_debug() {
    debug_enabled = true;
  }

  void disable_debug() {
    debug_enabled = false;
  }

  JsonDocument get_config() {
    JsonDocument result;

    result["name"] = config->name;

    return result;
  }

  void emit_config() {
    JsonDocument config = get_config();
    emit_event("system.config", config);
  }

  JsonDocument get_state() {
    JsonDocument result;

    result["id"] = get_id();
    result["version"] = VERSION;
    result["platform"] = PLATFORM;
    result["uptime"] = millis() / 1000;
    result["heap_free"] = ESP.getFreeHeap();
    result["flash_size"] = ESP.getFlashChipSize();
    result["flash_speed"] = ESP.getFlashChipSpeed();
    result["flash_mode"] = ESP.getFlashChipMode();
    result["cpu_freq"] = ESP.getCpuFreqMHz();
    result["sdk_version"] = ESP.getSdkVersion();
    result["core_version"] = ESP.getCoreVersion();
    result["reset_reason"] = ESP.getResetReason();
    result["reset_info"] = ESP.getResetInfo();

    return result;
  }

  void emit_state() {
    JsonDocument state = get_state();
    emit_event("system.state", state);
  }

  void setup() {
    // Save the default name, if no name it set
    if (config->name[0] == '\0') {
      strncpy(config->name, sys::get_device_name().c_str(), sizeof(Config::name));
      config.save();
    }

    debug("Name: " + String(config->name));
    debug("Version: " + String(VERSION));

    timer.setInterval([]() { debug("Uptime: " + String(millis() / 1000) + "s"); }, 1000 * 10);
  }

  void loop() {
    timer.handle();
  }

  namespace api {

    APIResponse ping(JsonVariant params) {
      JsonDocument result;
      result.set("pong");

      return APIResponse{
          .result = result,
      };
    }

    APIResponse get_config(JsonVariant params) {
      return APIResponse{
          .result = sys::get_config(),
      };
    }

    APIResponse get_state(JsonVariant params) {
      return APIResponse{
          .result = sys::get_state(),
      };
    }

    APIResponse get_name(JsonVariant params) {
      JsonDocument result;
      result.set(sys::get_name());

      return APIResponse{
          .result = result,
      };
    }

    APIResponse set_name(JsonVariant params) {
      if (!params["name"].is<String>()) {
        return APIResponse{
            .err = "invalid_name",
        };
      }

      String name = params["name"].as<String>();
      if (name.length() < 1 || name.length() > sizeof(Config::name)) {
        return APIResponse{
            .err = "name_out_of_range",
        };
      }

      sys::set_name(name);

      return APIResponse{};
    }

    APIResponse test_echo(JsonVariant params) {
      return APIResponse{
          .result = params,
      };
    }

    APIResponse test_error(JsonVariant params) {
      return APIResponse{
          .err = "test_error",
      };
    }

    APIResponse restart(JsonVariant params) {
      sys::restart();

      return APIResponse{};
    }

    APIResponse factory_reset(JsonVariant params) {
      sys::factory_reset();

      return APIResponse{};
    }

    APIResponse enable_debug(JsonVariant params) {
      sys::enable_debug();

      return APIResponse{};
    }

    APIResponse disable_debug(JsonVariant params) {
      sys::disable_debug();

      return APIResponse{};
    }

  }  // namespace api

}  // namespace sys

namespace serial {

  const int SERIAL_BAUD = 115200;

  String serial_buffer = "";

  void debug(String message) {
    ::debug("serial", message);
  }

  void setup() {
    Serial.begin(SERIAL_BAUD);

    // Wait for Serial to become available
    while (!Serial)
      continue;

    // Print two empty lines to distinguish from previous 'junk' output
    Serial.println();
    Serial.println();
    debug("Hello!");
  }

  void loop() {
    while (Serial.available()) {
      char inChar = (char)Serial.read();
      if (inChar == '\n') {
        // Parse serial_buffer as JSON
        JsonDocument req;
        DeserializationError error = deserializeJson(req, serial_buffer);

        // Reset Serial Buffer
        serial_buffer = "";

        if (error) {
          debug("Received a message, but couldn't be parsed as JSON: " + String(error.f_str()));
          return;
        }

        if (!req["id"].is<int>()) {
          debug("Received a message, but it doesn't contain an ID");
          return;
        }

        if (!req["method"].is<String>()) {
          debug("Received a message, but it doesn't contain a method");
          return;
        }

        if (!req["params"].is<JsonObject>()) {
          debug("Received a message, but it doesn't contain parameters");
          return;
        }

        // Handle the request
        JsonDocument res = handle_request(
            req["id"].as<int>(),
            req["method"].as<String>(),
            req["params"].as<JsonVariant>());

        // Add the ID to the response
        res["id"] = req["id"];

        serializeJson(res, Serial);
        Serial.println();
      } else {
        serial_buffer += inChar;
      }
    }
  }

}  // namespace serial

namespace wifi {

  bool is_connected = false;
  bool is_connected_since_start = false;
  bool is_hotspot = false;

  void emit_state();
  void emit_config();

  WiFiEventHandler onIP;
  WiFiEventHandler onConnected;
  WiFiEventHandler onDisonnected;

  void debug(String message) {
    ::debug("wifi", message);
  }

  void setup() {
    WiFi.mode(WIFI_STA);
    WiFi.hostname(sys::get_device_name());
    WiFi.setAutoReconnect(true);

    onIP = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
      is_connected = true;
      is_connected_since_start = true;

      // Debug
      debug("IP Address: " + event.ip.toString());

      // Emit Event
      JsonDocument doc;
      doc["ip"] = WiFi.localIP().toString();
      emit_event("wifi.ip", doc);
      emit_state();

      // Sync nupnp
      timer.setTimeout(nupnp::sync, 1000);

      // Sync ota
      timer.setTimeout(ota::sync, 5000);
    });

    onConnected = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &event) {
      // Debug
      debug("Connected to Wi-Fi " + String(event.ssid));

      // Emit Event
      JsonDocument doc;
      doc["ssid"] = event.ssid;
      emit_event("wifi.connected", doc);
      emit_state();
    });

    onDisonnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
      is_connected = false;

      // Debug
      debug("Disconnected from Wi-Fi. Reason: " + String(event.reason));

      // Emit Event
      JsonDocument doc;
      doc["reason"] = event.reason;
      emit_event("wifi.disconnected", doc);
      emit_state();

      // Start Hotspot
      if (is_connected_since_start == false && is_hotspot == false) {
        debug("Could not connect. Starting hotspot...");
        WiFi.softAP(sys::get_device_name());
        is_hotspot = true;
      }
    });

    if (strlen(config->wifi_ssid) == 0) {
      debug("No Wi-Fi credentials found. Starting hotspot...");
      WiFi.softAP(sys::get_device_name());
      is_hotspot = true;
    } else {
      debug("Connecting to " + String(config->wifi_ssid) + "...");
      WiFi.begin(config->wifi_ssid, config->wifi_pass);
      WiFi.setSleepMode(WIFI_NONE_SLEEP);
    }
  }

  JsonDocument get_networks() {
    int networksFound = WiFi.scanComplete();
    if (networksFound < 0) {
      networksFound = 0;
    }

    JsonDocument result;
    JsonArray networks = result.to<JsonArray>();

    for (int i = 0; i < networksFound; ++i) {
      JsonObject network = networks.add<JsonObject>();
      network["bssid"] = WiFi.BSSIDstr(i);
      network["ssid"] = WiFi.SSID(i);
      network["rssi"] = WiFi.RSSI(i);

      switch (WiFi.encryptionType(i)) {
        case ENC_TYPE_NONE:
          network["encryption"] = "none";
          break;
        case ENC_TYPE_AUTO:
          network["encryption"] = "auto";
          break;
        case ENC_TYPE_WEP:
          network["encryption"] = "wep";
          break;
        case ENC_TYPE_TKIP:
          network["encryption"] = "tkip";
          break;
        case ENC_TYPE_CCMP:
          network["encryption"] = "ccmp";
          break;
        default:
          network["encryption"] = "unknown";
          break;
      }
    }

    return result;
  }

  JsonDocument get_state() {
    JsonDocument result;

    result["status"] = wifi_station_get_connect_status();
    result["connected"] = WiFi.isConnected();
    result["mac"] = WiFi.macAddress();

    if (result["connected"]) {
      result["ssid"] = WiFi.SSID();
      result["bssid"] = WiFi.BSSIDstr();
      result["rssi"] = WiFi.RSSI();
      result["ip"] = WiFi.localIP().toString();
      result["gateway"] = WiFi.gatewayIP().toString();
      result["subnet"] = WiFi.subnetMask().toString();
      result["dns"] = WiFi.dnsIP().toString();
    }

    return result;
  }

  void emit_state() {
    JsonDocument state = get_state();
    emit_event("wifi.state", state);
  }

  JsonDocument get_config() {
    JsonDocument result;

    result["ssid"] = config->wifi_ssid;

    return result;
  }

  void emit_config() {
    JsonDocument config = get_config();
    emit_event("wifi.config", config);
  }

  namespace api {

    APIResponse get_config(JsonVariant params) {
      return APIResponse{
          .result = wifi::get_config(),
      };
    }

    APIResponse get_state(JsonVariant params) {
      return APIResponse{
          .result = wifi::get_state(),
      };
    }

    APIResponse scan_networks(JsonVariant params) {
      WiFi.scanNetworksAsync([](int networksFound) {
        debug("Found " + String(networksFound) + " networks");

        JsonDocument data = wifi::get_networks();
        emit_event("wifi.networks", data);
      });

      return APIResponse{};
    }

    APIResponse get_networks(JsonVariant params) {
      return APIResponse{
          .result = wifi::get_networks(),
      };
    }

    APIResponse connect(JsonVariant params) {
      if (!params["ssid"].is<String>()) {
        return APIResponse{
            .err = "missing_ssid",
        };
      }

      if (!params["pass"].is<String>()) {
        return APIResponse{
            .err = "missing_pass",
        };
      }

      String ssid = params["ssid"].as<String>();
      String pass = params["pass"].as<String>();

      // Save credentials
      strncpy(config->wifi_ssid, ssid.c_str(), sizeof(Config::wifi_ssid));
      strncpy(config->wifi_pass, pass.c_str(), sizeof(Config::wifi_pass));
      config.save();

      emit_config();

      timer.setTimeout([ssid, pass]() {
        // Disconnect from the current network
        debug("Disconnecting...");
        WiFi.disconnect();

        is_hotspot = false;

        // Connect to the new network
        debug("Connecting to " + ssid + "...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
      },
          500);

      return APIResponse{};
    }

    APIResponse disconnect(JsonVariant params) {
      // Erase saved credentials
      memset(config->wifi_ssid, 0, sizeof(Config::wifi_ssid));
      memset(config->wifi_pass, 0, sizeof(Config::wifi_pass));
      config.save();

      timer.setTimeout([]() {
        // Disconnect from the current network
        debug("Disconnecting...");
        WiFi.disconnect();
      },
          500);

      return APIResponse{};
    }

  }  // namespace api

}  // namespace wifi

namespace http {

  const int PORT = 80;

  void debug(String message) {
    ::debug("http", message);
  }

  void setup() {
    // Websocket
    http_websocket = new AsyncWebSocket("/ws");
    http_websocket->onEvent([](AsyncWebSocket *server,
                                AsyncWebSocketClient *client,
                                AwsEventType type,
                                void *arg,
                                uint8_t *data,
                                size_t len) {
      switch (type) {
        case WS_EVT_CONNECT: {
          Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());

          JsonDocument doc;
          doc["event"] = "full_state";
          doc["data"] = get_full_state();

          // Serialize
          String output;
          serializeJson(doc, output);

          client->text(output);
          break;
        }
        case WS_EVT_DISCONNECT: {
          Serial.printf("WebSocket client #%u disconnected\n", client->id());
          break;
        }
        case WS_EVT_DATA: {
          // Parse data as JSON
          // TODO
          break;
        }
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
          break;
      }
    });

    // Server
    http_server = new AsyncWebServer(PORT);
    http_server->addHandler(http_websocket);
    http_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      debug("GET " + request->url());

      JsonDocument res = get_full_state();

      AsyncResponseStream *response = request->beginResponseStream("application/json");
      serializeJson(res, *response);
      request->send(response);
    });
    http_server->addHandler(new AsyncCallbackJsonWebHandler("/", [](AsyncWebServerRequest *request, JsonVariant &req) {
      debug("POST " + request->url());

      int req_id = 0;
      if (!req["id"].is<int>()) {
        req_id = req["id"].as<int>();
      }

      if (!req["method"].is<String>()) {
        request->send(400, "application/json", "{\"error\": \"invalid_method\"}");
        return;
      }

      // Handle the request
      JsonDocument res = handle_request(
          req_id,
          req["method"].as<String>(),
          req["params"].as<JsonVariant>());

      AsyncResponseStream *response = request->beginResponseStream("application/json");
      serializeJson(res, *response);
      request->send(response);
    }));
    http_server->onNotFound([](AsyncWebServerRequest *request) {
      debug("GET " + request->url() + " — Not Found");
      request->send(404, "application/json", "{\"error\": \"not_found\"}");
    });
    http_server->begin();

    debug("Listening on http://0.0.0.0:" + String(PORT));
  }

  void loop() {
    http_websocket->cleanupClients();
  }

  void emit(JsonDocument &doc) {
    // Serialize
    String output;
    serializeJson(doc, output);

    http_websocket->textAll(output);
  }

}  // namespace http

/*
 * LED
 */
namespace led {

  const int ANIMATE_SPEED = 350;  // Milliseconds
  const int MAX_LED_COUNT = 512;  // TODO: Make dynamic size

  JsonDocument get_config();
  void setup();
  void emit_config();
  JsonDocument get_state();
  void emit_state();
  int get_count();
  void set_count(int count);
  void set_color(ColorRGBW color);

  void debug(String message) {
    ::debug("led", message);
  }

  ColorRGBW pixels_previous[MAX_LED_COUNT];
  ColorRGBW pixels_current[MAX_LED_COUNT];
  ColorRGBW pixels_target[MAX_LED_COUNT];
  ColorRGBW colors_target[MAX_LED_COUNT];

  bool animating = false;
  int animating_start_ms = 0;
  int animating_delta_current = 0;
  int animating_delta_previous = 0;
  ColorRGBW initial_color;

  bool state_on = true;
  int state_brightness = DEFAULT_LED_BRIGHTNESS;
  std::vector<ColorRGBW> state_colors;

  void animate() {
    // Set current pixels to previous pixels
    for (int i = 0; i < config->led_count; i++) {
      pixels_previous[i] = pixels_current[i];
    }

    // Set target pixels to state pixels
    for (int i = 0; i < config->led_count; i++) {
      if (state_on) {
        // Set color
        pixels_target[i] = colors_target[i];

        // Mix color with brightness
        pixels_target[i].r = (double)pixels_target[i].r * (double)state_brightness / (double)255;
        pixels_target[i].g = (double)pixels_target[i].g * (double)state_brightness / (double)255;
        pixels_target[i].b = (double)pixels_target[i].b * (double)state_brightness / (double)255;
        pixels_target[i].w = (double)pixels_target[i].w * (double)state_brightness / (double)255;
      } else {
        pixels_target[i] = ColorRGBW({
            .r = 0,
            .g = 0,
            .b = 0,
            .w = 0,
        });
      }
    }

    // Set animating to true and start time
    animating = true;
    animating_start_ms = millis();
  }

  void animate_step() {
    animating_delta_current = (((double)(millis() - animating_start_ms) / (double)ANIMATE_SPEED)) * 255;

    if (animating_delta_previous != animating_delta_current) {
      animating_delta_previous = animating_delta_current;

      // If animating is finished
      if (animating_delta_current >= 255) {
        // Reset animating variables
        animating = false;
        animating_start_ms = 0;
        animating_delta_previous = 0;
        animating_delta_current = 0;

        // Set previous pixels to target pixels
        for (int i = 0; i < get_count(); i++) {
          pixels_current[i] = pixels_target[i];
        }
      } else {
        // Interpolate pixels
        for (int i = 0; i < get_count(); i++) {
          ColorRGBW pixel_previous = pixels_previous[i];
          ColorRGBW pixel_target = pixels_target[i];

          double deltad = (double)animating_delta_current / (double)255;

          uint8_t mixed_r = (double)pixel_previous.r * (double)(1 - deltad) + (double)pixel_target.r * deltad;
          uint8_t mixed_g = (double)pixel_previous.g * (double)(1 - deltad) + (double)pixel_target.g * deltad;
          uint8_t mixed_b = (double)pixel_previous.b * (double)(1 - deltad) + (double)pixel_target.b * deltad;
          uint8_t mixed_w = (double)pixel_previous.w * (double)(1 - deltad) + (double)pixel_target.w * deltad;

          pixels_current[i] = ColorRGBW{
              .r = mixed_r,
              .g = mixed_g,
              .b = mixed_b,
              .w = mixed_w,
          };
        }
      }

      // Write colors to the strip
      for (int i = 0; i < get_count(); i++) {
        uint32_t color = strip->Color(
            pixels_current[i].r,
            pixels_current[i].g,
            pixels_current[i].b,
            pixels_current[i].w);
        strip->setPixelColor(i, color);
      }
      strip->show();
    }
  }

  void set_color(ColorRGBW color) {
    // Set state
    state_on = true;
    state_colors.clear();
    state_colors.push_back(color);

    // Set target colors
    for (int i = 0; i < get_count(); i++) {
      colors_target[i] = color;
    }

    // Animate
    animate();

    // Emit state
    timer.setTimeout(emit_state, 1);
  }

  void set_gradient(/*ColorRGBW colors[]*/) {
    state_on = true;
    // for (int i = 0; i < get_count(); i++) {
    //   state_colors[i] = colors[i];
    // }

    // Animate
    animate();

    // Emit state
    timer.setTimeout(emit_state, 1);
  }

  int get_count() {
    return config->led_count;
  }

  void set_count(int count) {
    // Save new count
    config->led_count = count;
    config.save();

    // Clear & Setup
    strip->clear();
    strip->show();
    led::setup();

    // Update nupnp
    timer.setTimeout(nupnp::sync, 1000);

    // Emit config
    timer.setTimeout(emit_config, 1);
  }

  void set_on(bool on) {
    state_on = on;

    // Animate
    animate();

    // Emit state
    timer.setTimeout(emit_state, 1);
  }

  void set_brightness(uint8_t brightness) {
    state_on = true;
    state_brightness = brightness;

    // Animate
    animate();

    // Emit state
    timer.setTimeout(emit_state, 1);
  }

  int get_pin() {
    return config->led_pin;
  }

  void set_pin(uint8_t pin) {
    // Save new pin
    config->led_pin = pin;
    config.save();

    // Clear & Setup
    strip->clear();
    strip->show();
    led::setup();

    // Emit config
    timer.setTimeout(emit_config, 1);
  }

  String get_type() {
    if (config->led_type == LedType::SK6812) {
      return "SK6812";
    } else if (config->led_type == LedType::WS2812) {
      return "WS2812";
    } else {
      return "unknown";
    }
  }

  bool set_type(String type) {
    if (type.equals("SK6812")) {
      // Save
      config->led_type = LedType::SK6812;
      config.save();

      // Clear & Setup
      strip->clear();
      strip->show();
      led::setup();

      return true;
    } else if (type.equals("WS2812")) {
      // Save
      config->led_type = LedType::WS2812;
      config.save();

      // Clear & Setup
      strip->clear();
      strip->show();
      led::setup();

      return true;
    } else {
      return false;
    }

    // Emit config
    timer.setTimeout(emit_config, 1);
  }

  JsonDocument get_state() {
    JsonDocument result;

    result["on"] = state_on;
    result["brightness"] = state_brightness;

    JsonArray colors = result["colors"].to<JsonArray>();
    for (int i = 0; i < state_colors.size(); i++) {
      JsonObject pixel = colors.add<JsonObject>();
      pixel["r"] = state_colors[i].r;
      pixel["g"] = state_colors[i].g;
      pixel["b"] = state_colors[i].b;
      pixel["w"] = state_colors[i].w;
    }

    return result;
  }

  void emit_state() {
    JsonDocument state = get_state();
    emit_event("led.state", state);
  }

  JsonDocument get_config() {
    JsonDocument result;

    result["count"] = led::get_count();
    result["pin"] = led::get_pin();
    result["type"] = led::get_type();

    return result;
  }

  void emit_config() {
    JsonDocument config = get_config();
    ::emit_event("led.config", config);
  }

  void setup() {
    int led_count = get_count();
    int led_pin = get_pin();
    int led_type;

    switch (config->led_type) {
      case LedType::SK6812: {
        led_type = NEO_GRBW + NEO_KHZ800;
        initial_color = ColorRGBW{
            .r = 0,
            .g = 0,
            .b = 0,
            .w = 255,
        };
        break;
      }
      case LedType::WS2812:
      default: {
        led_type = NEO_GRB + NEO_KHZ800;
        initial_color = ColorRGBW{
            .r = 255,
            .g = 255,
            .b = 255,
            .w = 0,
        };
        break;
      }
    }

    debug("Initializing LED strip with " + String(led_count) + " LEDs on pin " + String(led_pin) + " and type " + String(led_type));

    strip = new Adafruit_NeoPixel(led_count, led_pin, led_type);
    strip->begin();
    strip->setBrightness(DEFAULT_LED_BRIGHTNESS);

    // Make strip black
    strip->fill(strip->Color(0, 0, 0, 0));
    strip->show();

    // Animate to initial color
    set_color(initial_color);
  }

  void loop() {
    if (animating) {
      animate_step();
    }
  }

  namespace api {

    APIResponse get_state(JsonVariant params) {
      return APIResponse{
          .result = led::get_state(),
      };
    }

    APIResponse get_config(JsonVariant params) {
      return APIResponse{
          .result = led::get_config(),
      };
    }

    APIResponse get_count(JsonVariant params) {
      JsonDocument result;
      result.set(led::get_count());

      return APIResponse{
          .result = result,
      };
    }

    APIResponse set_count(JsonVariant params) {
      if (!params["count"].is<int>()) {
        return APIResponse{
            .err = "invalid_count",
        };
      }

      int count = params["count"].as<int>();
      if (count < 1 || count > MAX_LED_COUNT) {
        return APIResponse{
            .err = "count_out_of_range",
        };
      }

      led::set_count(count);

      return APIResponse{};
    }

    APIResponse get_pin(JsonVariant params) {
      JsonDocument result;
      result.set(led::get_pin());

      return APIResponse{
          .result = result,
      };
    }

    APIResponse set_pin(JsonVariant params) {
      if (!params["pin"].is<int>()) {
        return APIResponse{
            .err = "invalid_pin",
        };
      }

      int pin = params["pin"].as<uint8_t>();
      if (pin < 0 || pin > 255) {
        return APIResponse{
            .err = "pin_out_of_range",
        };
      }

      led::set_pin(pin);

      return APIResponse{};
    }

    APIResponse get_type(JsonVariant params) {
      JsonDocument result;
      result.set(led::get_type());

      return APIResponse{
          .result = result,
      };
    }

    APIResponse set_type(JsonVariant params) {
      if (!params["type"].is<String>()) {
        return APIResponse{
            .err = "invalid_type",
        };
      }

      String type = params["type"].as<String>();

      if (led::set_type(type)) {
        return APIResponse{};
      } else {
        return APIResponse{
            .err = "invalid_type",
        };
      }

      return APIResponse{};
    }

    APIResponse set_on(JsonVariant params) {
      if (!params["on"].is<bool>()) {
        return APIResponse{
            .err = "missing_on",
        };
      }

      bool on = params["on"].as<bool>();
      led::set_on(on);

      return APIResponse{};
    }

    APIResponse set_color(JsonVariant params) {
      if (!params["r"].is<int>() || !params["g"].is<int>() || !params["b"].is<int>()) {
        return APIResponse{
            .err = "invalid_color",
        };
      }

      // The format in params is { r, g, b, w }
      ColorRGBW target_color{
          .r = params["r"].as<uint8_t>(),
          .g = params["g"].as<uint8_t>(),
          .b = params["b"].as<uint8_t>(),
          .w = params["w"].is<uint8_t>()
              ? params["w"].as<uint8_t>()
              : (uint8_t)0,
      };

      led::set_color(target_color);

      return APIResponse{};
    }

    APIResponse set_gradient(JsonVariant params) {
      // The format in params["pixels"] is [[r,g b, w], ...]

      JsonArray colors = params["colors"].as<JsonArray>();
      if ((int)colors.size() < 1 || (int)colors.size() > led::get_count()) {
        return APIResponse{
            .err = "colors_out_of_range",
        };
      }

      // Set state
      state_colors.clear();
      for (int i = 0; i < (int)colors.size(); i++) {
        JsonObject color = colors[i].as<JsonObject>();
        state_colors.push_back(ColorRGBW{
            .r = color["r"].as<uint8_t>(),
            .g = color["g"].as<uint8_t>(),
            .b = color["b"].as<uint8_t>(),
            .w = color["w"].as<uint8_t>(),
        });
      }

      // Interpolate r,g,b,w colors to create a gradient the size of the LED count
      int numColors = colors.size();
      for (int i = 0; i < led::get_count(); i++) {
        float t = (float)i / (led::get_count() - 1);
        int idx1 = floor(t * (numColors - 1));
        int idx2 = ceil(t * (numColors - 1));
        JsonObject color1 = colors[idx1].as<JsonObject>();
        uint8_t color1_r = color1["r"].as<uint8_t>();
        uint8_t color1_g = color1["g"].as<uint8_t>();
        uint8_t color1_b = color1["b"].as<uint8_t>();
        uint8_t color1_w = color1["w"].as<uint8_t>();

        JsonObject color2 = colors[idx2].as<JsonObject>();
        uint8_t color2_r = color2["r"].as<uint8_t>();
        uint8_t color2_g = color2["g"].as<uint8_t>();
        uint8_t color2_b = color2["b"].as<uint8_t>();
        uint8_t color2_w = color2["w"].as<uint8_t>();

        ColorRGBW interpolatedColor;
        interpolatedColor.r = color1_r + (color2_r - color1_r) * (t * (numColors - 1) - idx1);
        interpolatedColor.g = color1_g + (color2_g - color1_g) * (t * (numColors - 1) - idx1);
        interpolatedColor.b = color1_b + (color2_b - color1_b) * (t * (numColors - 1) - idx1);
        interpolatedColor.w = color1_w + (color2_w - color1_w) * (t * (numColors - 1) - idx1);

        colors_target[i] = interpolatedColor;
      }

      led::set_gradient();  // TODO: Pass new colors

      return APIResponse{};
    }

    APIResponse set_brightness(JsonVariant params) {
      if (!params["brightness"].is<int>()) {
        return APIResponse{
            .err = "invalid_brightness",
        };
      }

      int brightness = params["brightness"].as<uint8_t>();
      if (brightness < 10 || brightness > 255) {
        return APIResponse{
            .err = "brightness_out_of_range",
        };
      }

      led::set_brightness(brightness);

      return APIResponse{};
    }

    APIResponse set_animation(JsonVariant params) {
      // TODO

      return APIResponse{
          .err = "not_implemented",
      };
    }

  }  // namespace api

}  // namespace led

namespace mdns {

  void debug(String message) {
    ::debug("mdns", message);
  }

  void setup() {
    if (!MDNS.begin(sys::get_device_name())) {
      debug("Error setting up MDNS responder!");
      return;
    }

    MDNS.addService("luxio", "tcp", http::PORT);
    MDNS.addServiceTxt("luxio", "tcp", "id", sys::get_id());
    MDNS.addServiceTxt("luxio", "tcp", "name", sys::get_name());
    MDNS.addServiceTxt("luxio", "tcp", "version", String(VERSION));

    debug("MDNS responder started");
  }

  void loop() {
    MDNS.update();
  }
}  // namespace mdns

namespace nupnp {

  const String URL = "http://nupnp.luxio.lighting/";
  const int INTERVAL = 1000 * 60 * 5;  // 5 minutes

  JsonDocument body_json;
  String body_string;
  WiFiClient wifi_client;
  HTTPClient http_client;

  void debug(String message) {
    ::debug("nupnp", message);
  }

  void sync() {
    if (wifi::is_connected == false)
      return;

    if (nupnp::is_syncing == true)
      return;

    if (ota::is_syncing == true)
      return;

    is_syncing = true;
    debug("Syncing...");

    // Create body
    body_json["id"] = sys::get_id();
    body_json["platform"] = PLATFORM;
    body_json["address"] = WiFi.localIP().toString();
    body_json["name"] = sys::get_name();
    body_json["version"] = VERSION;
    body_json["pixels"] = led::get_count();
    body_json["wifi_ssid"] = WiFi.SSID();

    // Serialize body
    serializeJson(body_json, body_string);

    // Make the request
    http_client.begin(wifi_client, URL);
    http_client.addHeader("Content-Type", "application/json");
    int http_code = http_client.POST(body_string);

    if (http_code < 0) {
      debug("Error Syncing: " + http_client.errorToString(http_code));
    } else if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_NO_CONTENT) {
      debug("Synced");
    } else {
      debug("Error Syncing. HTTP Status Code: " + String(http_code));
    }

    is_syncing = false;
  }

  void setup() {
    // Create Timer
    timer.setInterval(sync, INTERVAL);
  }

}  // namespace nupnp

namespace ota {

  const int INTERVAL = 1000 * 60 * 60;  // 1 hour
  const String URL = "http://ota.luxio.lighting/?platform=" + String(PLATFORM) + "&id=" + sys::get_id();

  WiFiClient wifi_client;

  void debug(String message) {
    ::debug("ota", message);
  }

  void sync() {
    if (wifi::is_connected == false)
      return;

    if (nupnp::is_syncing == true)
      return;

    if (ota::is_syncing == true)
      return;

    is_syncing = true;
    debug("Checking for updates...");

    t_httpUpdate_return ret = ESPhttpUpdate.update(wifi_client, URL, String(VERSION));
    switch (ret) {
      case HTTP_UPDATE_FAILED: {
        debug("Failed: " + ESPhttpUpdate.getLastErrorString() + " (" + String(ESPhttpUpdate.getLastError()) + ")");
        break;
      }
      case HTTP_UPDATE_NO_UPDATES: {
        debug("No update available");
        break;
      }
      case HTTP_UPDATE_OK: {
        debug("Done");
        break;
      }
    }

    is_syncing = false;
  }

  void setup() {
    // Set callbacks
    ESPhttpUpdate.onStart([]() {
      debug("Start");
    });
    ESPhttpUpdate.onProgress([](int progress, int total) {
      debug("Progress: " + String(progress) + " / " + String(total));
    });
    ESPhttpUpdate.onEnd([]() {
      debug("End");
    });
    ESPhttpUpdate.onError([](int error) {
      debug("Error: " + String(error) + " - " + ESPhttpUpdate.getLastErrorString());
    });

    // Create Timer
    timer.setInterval(sync, INTERVAL);
  }

}  // namespace ota

JsonDocument handle_request(const int req_id, const String method, const JsonVariant params) {
  // Debug
  debug("req:" + String(req_id), method);

  // Assign the correct method
  APIResponse (*fn)(JsonVariant params);
  if (false) {
    // Only for code alignment
  } else if (method.equals("wifi.get_config")) {
    fn = &wifi::api::get_config;
  } else if (method.equals("wifi.get_state")) {
    fn = &wifi::api::get_state;
  } else if (method.equals("wifi.get_networks")) {
    fn = &wifi::api::get_networks;
  } else if (method.equals("wifi.scan_networks")) {
    fn = &wifi::api::scan_networks;
  } else if (method.equals("wifi.connect")) {
    fn = &wifi::api::connect;
  } else if (method.equals("wifi.disconnect")) {
    fn = &wifi::api::disconnect;
  } else if (method.equals("led.get_config")) {
    fn = &led::api::get_config;
  } else if (method.equals("led.get_state")) {
    fn = &led::api::get_state;
  } else if (method.equals("led.get_count")) {
    fn = &led::api::get_count;
  } else if (method.equals("led.set_count")) {
    fn = &led::api::set_count;
  } else if (method.equals("led.get_pin")) {
    fn = &led::api::get_pin;
  } else if (method.equals("led.set_pin")) {
    fn = &led::api::set_pin;
  } else if (method.equals("led.get_type")) {
    fn = &led::api::get_type;
  } else if (method.equals("led.set_type")) {
    fn = &led::api::set_type;
  } else if (method.equals("led.set_on")) {
    fn = &led::api::set_on;
  } else if (method.equals("led.set_color")) {
    fn = &led::api::set_color;
  } else if (method.equals("led.set_gradient")) {
    fn = &led::api::set_gradient;
  } else if (method.equals("led.set_brightness")) {
    fn = &led::api::set_brightness;
  } else if (method.equals("led.set_animation")) {
    fn = &led::api::set_animation;
  } else if (method.equals("system.ping")) {
    fn = &sys::api::ping;
  } else if (method.equals("system.test_error")) {
    fn = &sys::api::test_error;
  } else if (method.equals("system.test_echo")) {
    fn = &sys::api::test_echo;
  } else if (method.equals("system.get_config")) {
    fn = &sys::api::get_config;
  } else if (method.equals("system.get_state")) {
    fn = &sys::api::get_state;
  } else if (method.equals("system.get_name")) {
    fn = &sys::api::get_name;
  } else if (method.equals("system.set_name")) {
    fn = &sys::api::set_name;
  } else if (method.equals("system.restart")) {
    fn = &sys::api::restart;
  } else if (method.equals("system.factory_reset")) {
    fn = &sys::api::factory_reset;
  } else if (method.equals("system.enable_debug")) {
    fn = &sys::api::enable_debug;
  } else if (method.equals("system.disable_debug")) {
    fn = &sys::api::disable_debug;
  } else if (method.equals("get_full_state")) {
    fn = [](JsonVariant params) {
      return APIResponse{
          .result = get_full_state(),
      };
    };
  } else {
    fn = [](JsonVariant params) {
      return APIResponse{
          .err = "unknown_method",
      };
    };
  }

  JsonDocument res;
  APIResponse response = fn(params);
  if (response.err.length() == 0) {
    debug("req:" + String(req_id), "OK");
    res["result"] = response.result;
  } else {
    debug("req:" + String(req_id), "Error: " + response.err);
    res["error"] = response.err;
  }

  return res;
}

void emit_event(String event, JsonDocument &data) {
  JsonDocument doc;
  doc["event"] = event;
  doc["data"] = data;

  // Emit to HTTP
  http::emit(doc);

  // Emit to Serial
  serializeJson(doc, Serial);
  Serial.println();
}

void emit_event(String event) {
  JsonDocument doc;
  doc.to<JsonObject>();
  return emit_event(event, doc);
}

JsonDocument get_full_state() {
  JsonDocument state;

  state["system"]["state"] = sys::get_state();
  state["system"]["config"] = sys::get_config();
  state["wifi"]["state"] = wifi::get_state();
  state["wifi"]["config"] = wifi::get_config();
  state["led"]["state"] = led::get_state();
  state["led"]["config"] = led::get_config();

  return state;
}

/*
 * Setup
 */

void setup() {
  serial::setup();
  sys::setup();
  led::setup();
  wifi::setup();
  http::setup();
  mdns::setup();
  nupnp::setup();
  ota::setup();

  emit_event("system.ready");
}

/*
 * Loop
 */

void loop() {
  serial::loop();
  sys::loop();
  led::loop();
  mdns::loop();
  http::loop();
}