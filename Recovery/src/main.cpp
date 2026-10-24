#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_image_format.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include "update_job.h"

static Preferences prefs;
static WebServer server(80);
static SemaphoreHandle_t mutex;
static UpdateJob job;
static bool queued, busy;
static String status = "Ready", failure;
static uint32_t received;
static const esp_partition_t *application;

void setup() {
  // Recovery serves HTTP only; USB programming remains available through the ROM.
  mutex = xSemaphoreCreateMutex();
  prefs.begin("luxio", false);
  application = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
  const auto *running = esp_ota_get_running_partition();
  if (!running || running->address != 0x10000 || running->size != LUXIO_LOADER_SIZE || running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY || !application || application->address != LUXIO_APP_ADDRESS || application->size != LUXIO_APP_SIZE) abort();
  bool pending = prefs.isKey("ota-job");
  if (pending && prefs.getBytesLength("ota-job") == sizeof(job)) prefs.getBytes("ota-job", &job, sizeof(job));
  if (pending && !validUpdateJob(job)) failure = "Saved update request is invalid. Submit a new update.";
  // A fresh USB installation boots the loader once, then the verified app.
  // Never boot a partial application while an update request is outstanding.
  esp_ota_img_states_t previousState;
  bool failedBoot = esp_ota_get_state_partition(application, &previousState) == ESP_OK && (previousState == ESP_OTA_IMG_ABORTED || previousState == ESP_OTA_IMG_INVALID);
  if (!pending && !failedBoot && esp_ota_set_boot_partition(application) == ESP_OK) ESP.restart();
  queued = pending && validUpdateJob(job) && !failedBoot;
  if (failedBoot) failure = "The new firmware did not start. Retry or submit another update.";
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  String ssid = prefs.getString("ssid"), password = prefs.getString("password");
  if (ssid.length()) WiFi.begin(ssid.c_str(), password.c_str());
  server.on("/", HTTP_GET, [] {
    server.send(200, "text/html", R"HTML(<!doctype html><meta name="viewport" content="width=device-width"><title>Luxio recovery</title><style>body{font:17px system-ui;background:#211b20;color:white;max-width:560px;margin:40px auto;padding:20px}button,input{font:inherit;padding:12px;margin:8px 0;box-sizing:border-box;width:100%}pre{white-space:pre-wrap}</style><h1>Luxio recovery</h1><p>Your light is offline while its firmware updates. Keep it powered on.</p><pre id="state"></pre><button id="retry">Retry update</button><details><summary>Use another firmware update</summary><form id="update"><input name="url" placeholder="Firmware HTTPS URL" required><input name="sha256" placeholder="SHA-256" required><input name="size" type="number" placeholder="Size in bytes" required><input name="build" type="number" placeholder="Build" required><button>Install firmware</button></form></details><script>async function refresh(){try{let s=await(await fetch('/status')).json();document.getElementById('state').textContent=s.status+'\n'+s.received+' / '+s.size+' bytes\n'+s.error;document.getElementById('retry').disabled=s.busy}catch{document.getElementById('state').textContent='Reconnecting…'}}document.getElementById('retry').onclick=async()=>{let r=await fetch('/retry',{method:'POST'});if(!r.ok)alert(await r.text());refresh()};document.getElementById('update').onsubmit=async e=>{e.preventDefault();let d=Object.fromEntries(new FormData(e.target));d.size=Number(d.size);d.build=Number(d.build);d.target=(await(await fetch('/status')).json()).target;let r=await fetch('/update',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});if(!r.ok)alert(await r.text());refresh()};refresh();setInterval(refresh,1500)</script>)HTML");
  });
  server.on("/status", HTTP_GET, [] {
    xSemaphoreTake(mutex, portMAX_DELAY);
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "target", LUXIO_UPDATE_TARGET);
    cJSON_AddStringToObject(doc, "status", status.c_str()); cJSON_AddStringToObject(doc, "error", failure.c_str());
    cJSON_AddBoolToObject(doc, "busy", busy || queued); cJSON_AddNumberToObject(doc, "received", received); cJSON_AddNumberToObject(doc, "size", job.size); cJSON_AddNumberToObject(doc, "build", job.build);
    xSemaphoreGive(mutex);
    char *json = cJSON_PrintUnformatted(doc); server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", json); free(json); cJSON_Delete(doc);
  });
  server.on("/retry", HTTP_POST, [] {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool valid = !busy && !queued && validUpdateJob(job);
    if (valid) { queued = true; failure = ""; }
    xSemaphoreGive(mutex);
    server.send(valid ? 202 : 409, "text/plain", valid ? "Retry requested" : "No failed update to retry");
  });
  server.on("/update", HTTP_POST, [] {
    String body = server.arg("plain"); UpdateJob replacement;
    cJSON *doc = body.length() <= 2048 ? cJSON_Parse(body.c_str()) : nullptr;
    auto url = cJSON_GetObjectItem(doc, "url"), sha = cJSON_GetObjectItem(doc, "sha256"), size = cJSON_GetObjectItem(doc, "size"), build = cJSON_GetObjectItem(doc, "build"), target = cJSON_GetObjectItem(doc, "target");
    bool valid = cJSON_IsString(url) && strlen(url->valuestring) < sizeof(replacement.url) && cJSON_IsString(sha) && strlen(sha->valuestring) == 64 && cJSON_IsNumber(size) && size->valuedouble == size->valueint && size->valueint >= 288 && size->valueint <= LUXIO_APP_SIZE && cJSON_IsNumber(build) && build->valuedouble == build->valueint && build->valueint > 0 && cJSON_IsString(target) && !strcmp(target->valuestring, LUXIO_UPDATE_TARGET);
    if (valid) { replacement.size = size->valueint; replacement.build = build->valueint; strlcpy(replacement.url, url->valuestring, sizeof(replacement.url)); strlcpy(replacement.sha256, sha->valuestring, sizeof(replacement.sha256)); strlcpy(replacement.target, target->valuestring, sizeof(replacement.target)); valid = validUpdateJob(replacement); }
    cJSON_Delete(doc);
    xSemaphoreTake(mutex, portMAX_DELAY);
    int code = !valid ? 400 : busy || queued ? 409 : 202;
    if (code == 202) {
      if (prefs.putBytes("ota-job", &replacement, sizeof(replacement)) != sizeof(replacement)) code = 500;
      else { job = replacement; queued = true; failure = ""; }
    }
    xSemaphoreGive(mutex);
    server.send(code, "text/plain", code == 202 ? "Update requested" : code == 409 ? "An update is running" : "Unable to accept update request");
  });
  server.begin();
  xTaskCreate([](void *) {
    for (;;) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      bool start = queued && WiFi.status() == WL_CONNECTED;
      UpdateJob current = job;
      if (start) { queued = false; busy = true; received = 0; status = "Downloading firmware"; failure = ""; }
      else if (queued) status = "Waiting for Wi-Fi";
      xSemaphoreGive(mutex);
      if (!start) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
      String error;
      NetworkClientSecure tls; tls.setInsecure(); // Same trust policy as Luxio; SHA-256 is checked against the saved manifest.
      HTTPClient http; http.setTimeout(10000); http.useHTTP10(true); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
      esp_ota_handle_t handle = 0;
      bool opened = false, success = false;
      uint32_t total = 0;
      if (!validUpdateJob(current)) error = "Invalid saved update";
      else if (!http.begin(tls, current.url) || http.GET() != 200) error = "Firmware server unavailable. Retry when it is reachable.";
      else if (http.getSize() != int(current.size)) error = "Firmware length does not match the update request";
      else {
        auto *stream = http.getStreamPtr(); uint8_t buffer[2048];
        // Validate the chip and application name before erasing any main-app sectors.
        const size_t prefixLength = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
        size_t prefix = stream->readBytes(buffer, prefixLength);
        esp_image_header_t imageHeader{}; esp_app_desc_t description{};
        if (prefix == prefixLength) { memcpy(&imageHeader, buffer, sizeof(imageHeader)); memcpy(&description, buffer + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(description)); }
        if (prefix != prefixLength || imageHeader.magic != ESP_IMAGE_HEADER_MAGIC || imageHeader.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID || description.magic_word != ESP_APP_DESC_MAGIC_WORD || strncmp(description.project_name, "Luxio", sizeof(description.project_name)) || strnlen(description.version, sizeof(description.version)) == sizeof(description.version) || String(description.version) != String(current.build)) error = "Not a Luxio application image for this chip";
        else if (esp_ota_begin(application, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) error = "Unable to begin application write";
        else {
          opened = true;
          mbedtls_sha256_context hash; mbedtls_sha256_init(&hash); mbedtls_sha256_starts(&hash, 0);
          if (esp_ota_write(handle, buffer, prefix) != ESP_OK) error = "Flash write failed";
          else { mbedtls_sha256_update(&hash, buffer, prefix); total = prefix; }
          uint32_t lastData = millis();
          while (!error.length() && total < current.size) {
            int count = stream->readBytes(buffer, min(sizeof(buffer), size_t(current.size - total)));
            if (count <= 0) {
              if (!http.connected() || millis() - lastData >= 15000) error = "Download interrupted. Retry to resume recovery.";
              vTaskDelay(pdMS_TO_TICKS(10)); continue;
            }
            lastData = millis();
            if (esp_ota_write(handle, buffer, count) != ESP_OK) { error = "Flash write failed"; break; }
            mbedtls_sha256_update(&hash, buffer, count); total += count;
            xSemaphoreTake(mutex, portMAX_DELAY); received = total; xSemaphoreGive(mutex);
            vTaskDelay(1);
          }
          uint8_t digest[32]; char actual[65]; mbedtls_sha256_finish(&hash, digest); mbedtls_sha256_free(&hash);
          for (int i = 0; i < 32; i++) sprintf(actual + 2 * i, "%02x", digest[i]);
          if (!error.length() && strcmp(actual, current.sha256)) error = "Firmware checksum mismatch. Submit a correct update or retry.";
          if (!error.length()) {
            esp_err_t result = esp_ota_end(handle); opened = false;
            if (result != ESP_OK) error = "Firmware image validation failed";
            else if (esp_ota_set_boot_partition(application) != ESP_OK) error = "Unable to select the new application";
            else success = true;
          }
        }
      }
      if (opened) esp_ota_abort(handle);
      http.end();
      xSemaphoreTake(mutex, portMAX_DELAY); busy = false; failure = error; status = success ? "Restarting Luxio" : "Update failed; recovery is ready"; xSemaphoreGive(mutex);
      // The job stays in NVS until the new main app finishes initializing.
      // A power cut during writing returns here; only that user-requested job resumes.
      if (success) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); }
    }
  }, "luxio-recovery", 12288, nullptr, 1, nullptr);
}

void loop() {
  static bool advertised = false;
  if (WiFi.status() == WL_CONNECTED && !advertised) {
    String name = "luxio-recovery-" + WiFi.macAddress().substring(12); name.replace(":", ""); name.toLowerCase();
    if (MDNS.begin(name.c_str())) { MDNS.addService("luxio-recovery", "tcp", 80); advertised = true; }
  }
  server.handleClient();
  delay(2);
}
