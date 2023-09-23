/*
 * Defines
 */
#define DEBUG 1
#define VERSION 33
#ifdef ARDUINO_ESP8266_WEMOS_D1MINI
  #define PLATFORM "WEMOS"
  #define DATAPIN D3
#else
  #define DATAPIN 12
  #define PLATFORM "LUXIO"
#endif
#define SERIAL_BAUDRATE 115200
#define NUM_PIXELS_DEFAULT 60
#define NUM_PIXELS_MAX 255
#define ROM_SIZE 512
#define ROM_CHECKBYTE1_ADDR 0
#define ROM_CHECKBYTE2_ADDR 1
#define ROM_DEVICENAME_ADDR 2
#define ROM_DEVICENAME_LENGTH  30
#define ROM_NUM_PIXELS_ADDR 33
#define ROM_WIFI_SSID_ADDR 100
#define ROM_WIFI_SSID_LENGTH 32
#define ROM_WIFI_PASS_ADDR 132
#define ROM_WIFI_PASS_LENGTH 32
#define ROM_HARDWARE_REV_ADDR 511
#define NUPNP_INTERVAL 300000
#define OTA_INTERVAL 300000
#define FADE_SPEED 300
#define FASTLED_INTERRUPT_RETRY_COUNT 0
#define OTA_HOST "ota.luxio.lighting"
#define OTA_PORT 443
#define OTA_PATH "/"
#define NUPNP_URL "https://nupnp.luxio.lighting/"
#define DEFAULT_COLOR "444444"

/*
 * EEPROM Table:
 * 0-1     | Check bytes - to validate EEPROM has been touched by this program
 * 2-32    | Name of the device
 * 33      | Pixel count
 * 100-131 | Wi-Fi SSID
 * 132-163 | Wi-Fi Password
 * TODO: 
 * 511     | Hardware rev. (Soft-lockbit)
 */

/*
 * Includes
 */
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h> // v5
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266httpUpdate.h>
#include <FastLED.h>
#include <EEPROM.h>
#include <SimpleTimer.h> // v1

/*
 * Class initialization
 */
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
SimpleTimer timerOta;
SimpleTimer timerNupnp;

char wifiSsid[ROM_WIFI_SSID_LENGTH];
char wifiPass[ROM_WIFI_PASS_LENGTH];

bool wifiConnecting;
bool wifiConnected;
bool wifiConnectedPrev;
bool wifiConnectedInitial;
bool wifiApActive;
unsigned long wifiConnectingSince;

String apIp;
String localIp;
String localMac;
char deviceName[ROM_DEVICENAME_LENGTH];
int numPixels;

String name = "";
String mode = "";
String gradient_pixels = "";
String gradient_source = "";

String effect = "";
unsigned long effect_prev_ms = 0;

bool fading_pixels = false;
bool fading_brightness = false;

unsigned long fade_pixels_start_ms = 0; 
unsigned long fade_brightness_start_ms = 0; 
int fade_pixels_prev_delta = 0;
int fade_brightness_prev_delta = 0;

bool shouldRestart = false;

bool on = true;

float brightness_f = 1;
int brightness = 255;
int brightness_state = 255;
int brightness_previous = 255;
int brightness_current = 255;
int brightness_target = 255;

/*
 * Effect properties
 */
uint16_t effect_rainbow_f;
uint16_t effect_knightrider_f;
bool effect_knightrider_direction;
uint16_t effect_colorcycle_hue;

/*
 * Pixel arrays
 */
CRGB previousColors[NUM_PIXELS_MAX];
CRGB currentColors[NUM_PIXELS_MAX];
CRGB targetColors[NUM_PIXELS_MAX];

/*
 * ==================================
 *               SETUP
 * ==================================
 */
void setup() {
  setup_serial();
  setup_pins();
  setup_eeprom(); 
  setup_numleds(); 
  setup_leds();
  setup_mac(); 
  setup_name();
  setup_wifi(); 
  setup_ota();
  setup_timers();
  setup_mdns();
  setup_webserver();
  
  setColor(DEFAULT_COLOR);
  initWifi();
}

void setup_leds() {
  for( int i = 0; i < NUM_PIXELS_MAX; i++ ) {
    previousColors[i] = CRGB::Black;
    currentColors[i] = CRGB::Black;
  }
  FastLED.addLeds<NEOPIXEL, DATAPIN>(currentColors, numPixels);
  FastLED.show();
}

void setup_serial() {
  Serial.begin(SERIAL_BAUDRATE);
  debug("setup", "  Firmware Version: " + String(VERSION));
  debug("setup", "serial end");
}

void setup_pins() {
  debug("setup", "pins start");
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  
  debug("setup", "pins end");
}

void setup_eeprom() {
  debug("setup", "eeprom start");
  EEPROM.begin(ROM_SIZE);
  byte c1 = EEPROM.read(ROM_CHECKBYTE1_ADDR);
  byte c2 = EEPROM.read(ROM_CHECKBYTE2_ADDR);

  // Wipe EEPROM if never touched before
  if( c1 != 100 
   || c2 != 200 ) {
    debug("setup", "  EEPROM checkbytes missing, cleared the EEPROM");

    for (int i = 0 ; i < ROM_SIZE; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(ROM_CHECKBYTE1_ADDR, 100);
    EEPROM.write(ROM_CHECKBYTE2_ADDR, 200);
    EEPROM.commit();
  }
  debug("setup", "eeprom end");
}

void setup_mac() {
  debug("setup", "mac start");
  byte localMacByte[6];
  WiFi.macAddress( localMacByte );
  localMac = macToString( localMacByte );
  debug("setup", "  MAC: " + localMac);
  debug("setup", "mac end");
}

void setup_name() {
  debug("setup", "name start");
  String macStr = "Luxio-";

  unsigned char mac[6];
  WiFi.macAddress(mac);
  
  for (int i = 3; i < 6; i++) {
    String part = String(mac[i], 16);
    if( part.length() < 2 ) {
      part = String("0") + part;
    }
    part.toUpperCase();
    macStr += part;
  }

  macStr.toCharArray(deviceName, 15);
  
  // get name from memory
  for( int i = ROM_DEVICENAME_ADDR; i < ROM_DEVICENAME_LENGTH + ROM_DEVICENAME_ADDR; i++ ) {
    byte c = EEPROM.read(i);
    if( c == 0 ) break;
    name += (char)c;
  }

  if( name.length() == 0 ) {
    name = (String)deviceName;
  }
  debug("setup", "  Name: " + name);
  debug("setup", "name end");
}

void setup_numleds() {
  debug("setup", "numleds start");
  numPixels = EEPROM.read(ROM_NUM_PIXELS_ADDR);
  if( numPixels == 0 ) {
    numPixels = (uint16_t)NUM_PIXELS_DEFAULT;
  }
  
  debug("setup", "  NumPixels: " + String(numPixels));
  debug("setup", "numleds end");
}

void setup_wifi() {
  debug("setup", "wifi start");
  for( int i = 0; i < ROM_WIFI_SSID_LENGTH; i++ ) {
    byte c = EEPROM.read(ROM_WIFI_SSID_ADDR + i);
    if( c == 0 ) break;
    wifiSsid[i] = (char)c;
  }
  
  for( int i = 0; i < ROM_WIFI_PASS_LENGTH; i++ ) {
    byte c = EEPROM.read(ROM_WIFI_PASS_ADDR + i);
    if( c == 0 ) break;
    wifiPass[i] = (char)c;
  }
  debug("setup", "  SSID: " + (String)wifiSsid);
  debug("setup", "  Password: " + (String)wifiPass);
  debug("setup", "wifi end");
}

void setup_ota() {
  debug("setup", "ota start");
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(deviceName);
  //ArduinoOTA.setPassword("5f4dcc3b5aa765d61d8327deb882cf99");
  ArduinoOTA.onStart([]() {
    debug("ota", "Started");
  });
  ArduinoOTA.onEnd([]() {
    debug("ota", "Ended");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Local OTA: Progress %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Local OTA: Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) debug("ota", "Auth Failed");
    else if (error == OTA_BEGIN_ERROR) debug("ota", "Begin Failed");
    else if (error == OTA_CONNECT_ERROR) debug("ota", "Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) debug("ota", "Receive Failed");
    else if (error == OTA_END_ERROR) debug("ota", "End Failed");
  });
  ArduinoOTA.begin();  
  debug("setup", "ota end");
}

void setup_timers() {
  debug("setup", "timers start");
  timerNupnp.setInterval(NUPNP_INTERVAL);
  timerOta.setInterval(OTA_INTERVAL);  
  debug("setup", "timers end");
}

void setup_mdns() {
  debug("setup", "mdns start");
  MDNS.addService("luxio", "tcp", 80);
  MDNS.addServiceTxt("luxio", "tcp", "id", localMac);
  MDNS.addServiceTxt("luxio", "tcp", "version", String(VERSION));
  MDNS.addServiceTxt("luxio", "tcp", "name", name);
  debug("setup", "mdns end");
}

void setup_webserver() {
  debug("setup", "webserver start");
  httpUpdater.setup(&server);
  server.on("/", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/", HTTP_GET, _onWebserverGetIndex);
  server.on("/state", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/state", HTTP_GET, _onWebserverGetState);  
  server.on("/on", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/on", HTTP_PUT, _onWebserverPutOn);
  server.on("/gradient", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/gradient", HTTP_PUT, _onWebserverPutGradient);  
  server.on("/effect", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/effect", HTTP_PUT, _onWebserverPutEffect);  
  server.on("/brightness", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/brightness", HTTP_PUT, _onWebserverPutBrightness);  
  server.on("/name", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/name", HTTP_PUT, _onWebserverPutName);  
  server.on("/pixels", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/pixels", HTTP_PUT, _onWebserverPutPixels);  
  server.on("/restart", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/restart", HTTP_PUT, _onWebserverPutRestart);
  server.on("/network", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/network", HTTP_GET, _onWebserverGetNetwork);
  server.on("/network", HTTP_PUT, _onWebserverPutNetwork);
  server.on("/eeprom", HTTP_OPTIONS, _onWebserverOptions);
  server.on("/eeprom", HTTP_GET, _onWebserverGetEeprom);
  server.begin();
  debug("setup", "webserver end");
}


/*
 * ==================================
 *                LOOP
 * ==================================
 */

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  wifiConnectedPrev = wifiConnected;
  wifiConnected = ( WiFi.status() == WL_CONNECTED );

  // first time connected to Wi-Fi
  if( wifiConnected && wifiConnecting && !wifiConnectedInitial ) {
    debug("wifi", "Connected for the first time since boot!");
    
    wifiConnectedInitial = true;
    wifiConnecting = false;
    
    localIp = WiFi.localIP().toString();
    debug("wifi", "  LAN IP: " + localIp);

    checkOta();
    postNupnp();
  }

  if(timerOta.isReady()) {
    timerOta.reset();
    checkOta();
  }

  if(timerNupnp.isReady()) {
    timerNupnp.reset();
    postNupnp();
  }

  // Wi-Fi dropped, create a hotspot
  if( !wifiApActive && wifiConnectedInitial && !wifiConnected && wifiConnectedPrev ) {
    debug("wifi", "  Connection dropped, creating a hotspot...");
    
    wifiApActive = true;
    WiFi.softAP(deviceName);
  }

  unsigned long currentMillis = millis();
  int wait = 1000 * 30;

  if( !wifiApActive && !wifiConnected && wifiConnecting && currentMillis - wifiConnectingSince >= wait) {
    debug("wifi", "  Connection timeout, creating a hotspot...");
    
    wifiApActive = true;
    WiFi.softAP(deviceName);    
  }

  // wifi is now connected, kill the hotspot
  if( wifiApActive && wifiConnected ) {
    debug("wifi", "  Connected, killing the hotspot...");
    
    wifiApActive = false;
    WiFi.softAPdisconnect(true);
  }

  if( mode == "effect" ) {
    if( effect == "rainbow" ) {
      effect_rainbow();
    } else if( effect == "knightrider" ) {
      effect_knightrider();
    } else if( effect == "colorcycle" ) {
      effect_colorcycle();
    }
  }
  
  if( fading_pixels ) {
    fadePixelsStep();
  }

  if( fading_brightness ) {
    fadeBrightnessStep();
  }

  if( shouldRestart ) {
    ESP.restart();
  }
}

/*
 * ==================================
 *             WEBSERVER
 * ==================================
 */

void _onWebserverGetIndex() {
  debug("webserver", "GET /");
  server.send(200, "text/html", "Luxio v" + String(VERSION) + " (" + name + ") @ " + PLATFORM);
}

void _onWebserverGetState() {
  debug("webserver", "GET /state");

  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  // generic
  root["id"] = localMac;
  root["platform"] = PLATFORM;
  root["version"] = VERSION;
  root["name"] = name;
  root["mode"] = mode;
  root["pixels"] = numPixels;
  root["on"] = on;
  root["brightness"] = brightness_f;
  JsonArray& effects_array = jsonBuffer.createArray();
  effects_array.add("rainbow");
  effects_array.add("colorcycle");
  effects_array.add("knightrider");
  root["effects"] = effects_array;

  // pixels
  if( mode == "effect" ) {
    root["effect"] = effect;    
    root["gradient_source"] = (char*)NULL;
    root["gradient_pixels"] = (char*)NULL;
  } else {
    root["effect"] = (char*)NULL;
  
    // gradient
    JsonArray& gradient_source_array = jsonBuffer.createArray();
    for( int i = 0; i < gradient_source.length(); i = i + 7 ) {
      gradient_source_array.add( gradient_source.substring(i, i+6) );
    }
    root["gradient_source"] = gradient_source_array;

    JsonArray& gradient_pixels_array = jsonBuffer.createArray();
    for( int i = 0; i < gradient_pixels.length(); i = i + 7 ) {
      gradient_pixels_array.add( gradient_pixels.substring(i, i+6) );
    }
    root["gradient_pixels"] = gradient_pixels_array;
  }

  // wifi
  root["wifi_ssid"] = wifiSsid;
  root["wifi_ip_lan"] = localIp;
  root["wifi_ip_ap"] = apIp;
  root["wifi_connected"] = wifiConnected;
  root["wifi_ap"] = wifiApActive;

  String json;
  root.printTo(json);
  sendJson(json);
}

void _onWebserverPutOn() {
  debug("webserver", "PUT /on");

  String body = server.arg("plain");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  if (!root.success()) return server.send(400);

  bool value = root.get<bool>("value");
  
  sendOk();
  setOn(value);
}

void _onWebserverPutBrightness() {
  debug("webserver", "PUT /brightness");

  String body = server.arg("plain");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  if (!root.success()) return server.send(400);

  float value = root.get<float>("value");
  if( value < 0 ) return server.send(400);
  if( value > 1 ) return server.send(400);

  sendOk();
  setBrightness( value );
}

void _onWebserverPutGradient() {
  debug("webserver", "PUT /gradient");

  String body = server.arg("plain");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  if (!root.success()) return server.send(400);

  JsonArray& source = root.get<JsonVariant>("source");
  String sourceString = "";
  int sourceSize = source.size();
  for( int i = 0; i < sourceSize; i++ ) {
    if( i > numPixels ) continue;
    String color = source.get<String>(i);
    sourceString += color;
    sourceString += ",";
  }
  sourceString.remove( sourceString.length() - 1 );
  
  JsonArray& pixels = root.get<JsonVariant>("pixels");
  String pixelsString = "";
  int pixelsSize = pixels.size();
  for( int i = 0; i < pixelsSize; i++ ) {
    if( i > numPixels ) continue;
    String color = pixels.get<String>(i);
    pixelsString += color;
    pixelsString += ",";
  }
  pixelsString.remove( pixelsString.length() - 1 );
  
  sendOk();
  setGradient( sourceString + ";" + pixelsString );
}

void _onWebserverPutEffect() {
  debug("webserver", "PUT /effect");

  String body = server.arg("plain");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  if (!root.success()) return server.send(400);

  String value = root.get<String>("value");
  if( value != "rainbow" && value != "colorcycle" && value != "knightrider" )
     return server.send(400);

  sendOk();  
  setEffect(value);
}

void _onWebserverPutName() {
  debug("webserver", "PUT /name");

  String body = server.arg("plain");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  if (!root.success()) return server.send(400);

  String value = root.get<String>("value");
  debug("webserver", "New name: " + value);
  
  sendOk();  
  setName(value);
}

void _onWebserverPutRestart() {
  debug("webserver", "PUT /restart");

  shouldRestart = true;
  sendOk();
}

void _onWebserverPutPixels() {
  debug("webserver", "PUT /pixels");

  String body = server.arg("plain");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  if (!root.success()) return server.send(400);

  int value = root.get<int>("value");
  if( value < 0 ) return server.send(400);
  if( value > 255 ) return server.send(400);

  sendOk();  
  setNumPixels(value);
}

void _onWebserverGetNetwork() {
  debug("webserver", "GET /network");

  DynamicJsonBuffer jsonBuffer;
  JsonArray& root = jsonBuffer.createArray();

  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    JsonObject& networkObject = jsonBuffer.createObject();
    networkObject["ssid"] = WiFi.SSID(i);
    networkObject["rssi"] = WiFi.RSSI(i);

    int encryptionType = WiFi.encryptionType(i);
    switch( encryptionType ) {
      case 2:
        networkObject["security"] = "wpa-tkip";
        break;
      case 4:
        networkObject["security"] = "wpa-ccmp";
        break;
      case 5:
        networkObject["security"] = "wep";
        break;
      case 7: 
        networkObject["security"] = false;
        break;
      case 8:
        networkObject["security"] = "auto";
        break;
    }
    root.add( networkObject );    
  }

  String json;
  root.printTo(json);
  sendJson(json);
}

void _onWebserverPutNetwork() {
  debug("webserver", "PUT /network");

  String body = server.arg("plain");
  
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(body);
  if (!root.success()) {
    server.send(400);
    return;
  }

  String ssid = root["ssid"];
  String pass = root["pass"];

  sendOk();
  setWifi( ssid, pass );
}

void _onWebserverGetEeprom() {
  debug("webserver", "GET /eeprom");

  DynamicJsonBuffer jsonBuffer;
  JsonArray& root = jsonBuffer.createArray();

  for( int i = 0; i < ROM_SIZE; i++ ) {
    int b = EEPROM.read(i);
    root.add(b);    
  }

  String json;
  root.printTo(json);
  sendJson(json);
}

void _onWebserverOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
  server.send(200);
}

void sendOk() {
  sendJson("\"ok\"");
}

void sendJson( String json ) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

/*
 * ==================================
 *             LEDS STATE
 * ==================================
 */
void setMode( String mode_ ) {
  mode = mode_;
}

void setOn( bool on_ ) {
  if( on != on_ ) {
    on = on_;

    brightness = ( on ) ? brightness_state : 0;
    brightness_target = brightness;
    
    startFadingBrightness();
  }
}

void setBrightness(float bri) {
  brightness_f = bri;
  brightness = (int)(brightness_f * 255);
  brightness_state = brightness;
  brightness_target = brightness;

  if( brightness > 0 ) on = true;

  startFadingBrightness();
}

void setColor( String color_ ) {

  String body = "";
  body += color_;
  body += ";";

  for( int i = 0; i < numPixels; i++ ) {
    body += color_ + ",";
  }

  body.remove( body.length() - 1 );
  
  setGradient( body );
}

void setGradient( String body ) {
  setMode("gradient");

  gradient_pixels = "";
  gradient_source = "";

  int state = 0;
  for( int i = 0; i < body.length(); i++ ) {
    char c = body.charAt(i);
    if( c == ';' ) {
      state = 1;
    } else {
      if( state == 0 ) {
        gradient_source += c;
      } else {
        gradient_pixels += c;        
      }
    }
    
  }
  
  String hex;
  for (int i = 0; i < numPixels; i++) {
    hex = gradient_pixels.substring( i*7, (i+1)*7 ).substring(0,6);
    targetColors[i] = hexToColor( hex );
  }
  
  startFading();
  
}

void setEffect( String effect_ ) {
  setMode("effect");
  effect = effect_;

  effect_rainbow_f = 0;
  effect_knightrider_f = 0;
  effect_knightrider_direction = true;
  effect_colorcycle_hue = 0;
  
  startFading();
}

void startFading() {

  for (int i = 0; i < numPixels; i++) {
    previousColors[i] = currentColors[i];
  }
  
  fading_pixels = true;
  fade_pixels_start_ms = millis();
}

void startFadingBrightness() {

  for (int i = 0; i < numPixels; i++) {
    previousColors[i] = currentColors[i];
  }

  brightness_previous = brightness_current;
  
  fading_brightness = true;
  fade_brightness_start_ms = millis();
}

/*
 * ==================================
 *             SETTINGS
 * ==================================
 */

void setName( String newName ) {

  if( newName.length() == 0 ) {
    newName = (String)deviceName;
  }
  
  newName = newName.substring(0, ROM_DEVICENAME_LENGTH );

  if( newName != name ) {
    int i;
    for( i = 0; i < newName.length(); i++ ) {
      EEPROM.write( ROM_DEVICENAME_ADDR + i, newName.charAt(i));
    }
    EEPROM.write( ROM_DEVICENAME_ADDR + i, 0);
    EEPROM.commit();

    name = newName;
  }

  postNupnp();
  
}

void setNumPixels( int newNumPixels ) {
  EEPROM.write(ROM_NUM_PIXELS_ADDR, newNumPixels);
  EEPROM.commit();
}

/*
 * ==================================
 *               WIFI
 * ==================================
 */

void setWifi( String ssid, String pass ) {
  String newSsid = ssid.substring(0, ROM_WIFI_SSID_LENGTH);
  String newPass = pass.substring(0, ROM_WIFI_PASS_LENGTH);
  
  int i;
  for( i = 0; i < newSsid.length(); i++ ) {
    EEPROM.write( ROM_WIFI_SSID_ADDR + i, newSsid.charAt(i));
  }
  EEPROM.write( ROM_WIFI_SSID_ADDR + i, 0);
  for( i = 0; i < newPass.length(); i++ ) {
    EEPROM.write( ROM_WIFI_PASS_ADDR + i, newPass.charAt(i));
  }
  EEPROM.write( ROM_WIFI_PASS_ADDR + i, 0);
  EEPROM.commit();

  newSsid.toCharArray( wifiSsid, ROM_WIFI_SSID_LENGTH );
  newPass.toCharArray( wifiPass, ROM_WIFI_PASS_LENGTH );
  
  initWifi();

}

void initWifi() {
  debug("wifi", "init start");

  wifiConnecting = false;
  wifiConnected = false;
  wifiConnectedPrev = false;
  wifiConnectedInitial = false;
  wifiApActive = false;

  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
  WiFi.hostname(deviceName);

  if( wifiSsid[0] != 0 ) {
    debug("wifi", "  Credentials found, connecting...");
    wifiConnecting = true;
    wifiConnectingSince = millis();
    WiFi.begin(wifiSsid, wifiPass);
  } else {
    debug("wifi", "  No credentials found, creating a hotspot...");
    wifiApActive = true;
    WiFi.softAP(deviceName); 
  }

  localIp = WiFi.localIP().toString();
  debug("wifi", "  LAN IP: " + localIp);

  apIp = WiFi.softAPIP().toString();
  debug("wifi", "  AP IP: " + apIp);
  
  debug("wifi", "init end");
}

/*
 * ==================================
 *               NUPNP
 * ==================================
 */
void postNupnp() {
  if( WiFi.status() != WL_CONNECTED ) return;
  
  debug("nupnp", "syncing... ");

  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  root["id"] = localMac;
  root["platform"] = PLATFORM;
  root["address"] = localIp;
  root["name"] = name;
  root["version"] = VERSION;
  root["pixels"] = numPixels;
  root["wifi_ssid"] = wifiSsid;

  String body;
  root.printTo(body);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, NUPNP_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  int httpCode = http.POST(body);
  if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_NO_CONTENT) {
        Serial.println("[nupnp] synced");
      } else {
        Serial.printf("[nupnp] http code: %d\n", httpCode);
      }
  } else {
    Serial.printf("[nupnp] error: %s\n", http.errorToString(httpCode).c_str());
  }
}

/*
 * ==================================
 *                OTA
 * ==================================
 */
void checkOta() {
  WiFiClientSecure client;
  client.setInsecure();
  
  t_httpUpdate_return ret = ESPhttpUpdate.update(client,
    String(OTA_HOST),
    OTA_PORT,
    String(OTA_PATH) + "?platform=" + String(PLATFORM) + "&id=" + String(localMac),
    String(VERSION)
  );
  switch(ret) {
    case HTTP_UPDATE_FAILED:
        debug("cloud-ota", "Failed");
        Serial.print("  Error code: ");
        Serial.println(ESPhttpUpdate.getLastError());
        Serial.print("  Error message: ");
        Serial.println(ESPhttpUpdate.getLastErrorString());
        break;
    case HTTP_UPDATE_NO_UPDATES:
        debug("cloud-ota", "No update available");
        break;
    case HTTP_UPDATE_OK:
        debug("cloud-ota", "Done");
        break;
  }
  
}

/*
 * ==================================
 *              EFFECTS
 * ==================================
 */
void effect_rainbow() {

  unsigned long currentMillis = millis();
  int wait = 30;

  if (currentMillis - effect_prev_ms >= wait) {
    effect_prev_ms = currentMillis;
    effect_rainbow_f = ++effect_rainbow_f % 256;

    CHSV chsv;
    CRGB crgb;

    int h;
    int s;
    int v;
    
    for(int i = 0; i< numPixels; i++) {

      h = ((i * 256 / numPixels) + effect_rainbow_f) & 255;
      s = 255;
      v = 255;
      
      chsv = CHSV( h, s, v );
      hsv2rgb_rainbow( chsv, crgb );

      if( fading_pixels ) {
        targetColors[i] = crgb;
      } else {
        currentColors[i] = crgb;
      }
      
    }
    
    if( !fading_pixels ) {
      FastLED.show();
    }
  }

}

void effect_knightrider() {
  unsigned long currentMillis = millis();
  int wait = 25;

  if (currentMillis - effect_prev_ms >= wait) {
    effect_prev_ms = currentMillis;

    if( effect_knightrider_direction ) {
      effect_knightrider_f++;
    } else {
      effect_knightrider_f--;
    }

    if( effect_knightrider_f >= numPixels || effect_knightrider_f == 0 ) {
      effect_knightrider_direction = !effect_knightrider_direction;
    }

    CRGB crgb;
    for(int i = 0; i< numPixels; i++) {
      if( effect_knightrider_f == i
       || effect_knightrider_f == i - 1
       || effect_knightrider_f == i + 1) {
        crgb = CRGB(255, 0, 0);
      } else {
        crgb = CRGB(0, 0, 0);
      }

      if( fading_pixels ) {
        targetColors[i] = crgb;
      } else {
        currentColors[i] = crgb;
      }
    }
    
    if( !fading_pixels ) {
      FastLED.show();
    }
  }
}

void effect_colorcycle() {
  unsigned long currentMillis = millis();
  int wait = 60;

  if (currentMillis - effect_prev_ms >= wait) {
    effect_prev_ms = currentMillis;
    effect_colorcycle_hue = effect_colorcycle_hue + 1;
    effect_colorcycle_hue = effect_colorcycle_hue % 255;

    CHSV chsv = CHSV( effect_colorcycle_hue, 255, 255);
    CRGB crgb;
    hsv2rgb_rainbow( chsv, crgb );
    for(int i = 0; i< numPixels; i++) {
      if( fading_pixels ) {
        targetColors[i] = crgb;
      } else {
        currentColors[i] = crgb;
      }
    }
    
    if( !fading_pixels ) {
      FastLED.show();
    }
  }
}

/*
 * ==================================
 *              HELPERS
 * ==================================
 */

void fadePixelsStep() {

  int delta = ( ((double)(millis() - fade_pixels_start_ms) / (double)FADE_SPEED) ) * 255;
  
  if( fade_pixels_prev_delta != delta ) {
    fade_pixels_prev_delta = delta;
  
    if( delta > 255 ) {
      fading_pixels = false;
      fade_pixels_start_ms = 0;
      fade_pixels_prev_delta = 0;

      for (int i = 0; i < numPixels; i++) {
        previousColors[i] = targetColors[i];
        currentColors[i] = targetColors[i];
      }
      
    } else {
      
      for (int i = 0; i < numPixels; i++) {
        currentColors[i] = getPixelFadeColor(i, delta);
      }
      
    }
    
    FastLED.show();
  }
  
}

void fadeBrightnessStep() {

  int delta = ( ((double)(millis() - fade_brightness_start_ms) / (double)FADE_SPEED) ) * 255;

  if( fade_brightness_prev_delta != delta ) {
    fade_brightness_prev_delta = delta;

    if( delta > 255 ) {
      fading_brightness = false;
      fade_brightness_start_ms = 0;
      fade_brightness_prev_delta = 0;
      brightness_current = brightness;
      brightness_previous = brightness;
      brightness_target = brightness;

      FastLED.setBrightness( brightness );

      /* HACK
      if( brightness == 0 ) {
        for (int i = 0; i < numPixels; i++) {
          currentColors[i] = CRGB::Black;
        }        
      }
      */
    } else {
      brightness_current = (double)brightness_previous * (double)(( 255 - delta )/(double)255) + (double)brightness_target * (double)(delta/(double)255);      
      FastLED.setBrightness( brightness_current );
    }
    FastLED.show();
  }
}

CRGB getPixelFadeColor( int i, int delta ) {
  CRGB previousColor = previousColors[i]; 
  CRGB targetColor = targetColors[i];

  double deltad = (double)delta/(double)255;

  double mixed_r = (double)previousColor.r * (double)(1-deltad) + (double)targetColor.r * deltad;
  double mixed_g = (double)previousColor.g * (double)(1-deltad) + (double)targetColor.g * deltad;
  double mixed_b = (double)previousColor.b * (double)(1-deltad) + (double)targetColor.b * deltad;
  
  return CRGB((uint8_t)mixed_r, (uint8_t)mixed_g, (uint8_t)mixed_b);
}

CRGB hexToColor( String hex ) {
  int number = (int) strtol( &hex[0], NULL, 16);

  int r = number >> 16;
  int g = number >> 8 & 0xFF;
  int b = number & 0xFF;

  return CRGB(r, g, b);
}

String macToString(byte ar[]){
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%02X", ar[i]);
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

void debug(String c, String v) {
  Serial.print("[" + c + "] ");
  Serial.println(v);
}

void debug(String c, int v) {
  Serial.print("[" + c + "] ");
  Serial.println(v);
}
