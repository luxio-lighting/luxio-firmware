#include <Arduino.h>
#ifndef LUXIO_MATTER
#define LUXIO_MATTER 1
#endif
#if CONFIG_IDF_TARGET_ESP32 && !CONFIG_BT_NIMBLE_ENABLED && !LUXIO_MATTER
// The original ESP32 SDK excludes CHIPoBLE and only bundles Bluedroid.
// Keep this transport for diagnostic builds without linked Matter. Runtime
// Matter gating in the public build does not enable this BLE transport.
#include <NimBLEDevice.h>
#include <esp32-hal-bt.h>
#include <esp_heap_caps.h>
#include "control_error.h"
#include "bluetooth_name.h"
#include <freertos/queue.h>
#include "nimble/porting/nimble/include/nimble/nimble_port.h"
#include <memory>

extern size_t dispatch(const uint8_t *, size_t, std::unique_ptr<uint8_t[]> &, size_t);
static NimBLEServer *server;
static NimBLECharacteristic *tx;
static SemaphoreHandle_t bufferMutex;
static uint8_t *receiveBuffer;
static uint32_t header, expected, received, lastByte, session;
static uint16_t connection;
static bool connected, ready, subscribed;
static QueueHandle_t nameQueue;
static ble_npl_event nameEvent;

void setBluetoothName(const char *name) {
  if (!nameQueue) return;
  const BluetoothName next(name);
  xQueueOverwrite(nameQueue, &next);
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &nameEvent);
}

class Notifications : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t value) override {
    xSemaphoreTake(bufferMutex, portMAX_DELAY); subscribed = value & 1; xSemaphoreGive(bufferMutex);
  }
};
static Notifications notificationCallbacks;
class Connections : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    xSemaphoreTake(bufferMutex, portMAX_DELAY);
    connection = info.getConnHandle(); connected = true; subscribed = false; session++;
    free(receiveBuffer); receiveBuffer = nullptr; header = expected = received = 0; ready = false;
    xSemaphoreGive(bufferMutex);
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    xSemaphoreTake(bufferMutex, portMAX_DELAY);
    connected = false; subscribed = false; session++; free(receiveBuffer); receiveBuffer = nullptr; header = expected = received = 0; ready = false;
    xSemaphoreGive(bufferMutex);
    NimBLEDevice::startAdvertising();
  }
};
class Requests : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    auto bytes = characteristic->getValue();
    xSemaphoreTake(bufferMutex, portMAX_DELAY);
    if (ready || !connected || !subscribed) { xSemaphoreGive(bufferMutex); return; }
    if (header && millis() - lastByte > 3000) { free(receiveBuffer); receiveBuffer = nullptr; header = expected = received = 0; }
    for (size_t i = 0; i < bytes.length(); i++) {
      uint8_t byte = bytes[i]; lastByte = millis();
      if (header < 4) {
        const uint8_t magic[4] = {0x4c,0x58,0x50,0x31};
        header = byte == magic[header] ? header + 1 : byte == magic[0] ? 1 : 0; continue;
      }
      if (header < 8) {
        expected = (expected << 8) | byte; header++;
        if (header == 8) {
          if (!expected || expected > 32768) { header = expected = received = 0; break; }
          receiveBuffer = static_cast<uint8_t *>(heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
          if (!receiveBuffer) receiveBuffer = static_cast<uint8_t *>(malloc(expected));
          if (!receiveBuffer) { header = expected = received = 0; break; }
        }
        continue;
      }
      receiveBuffer[received++] = byte;
      if (received == expected) { ready = true; break; }
    }
    xSemaphoreGive(bufferMutex);
  }
};
static Connections connectionCallbacks;
static Requests requestCallbacks;
void startBluetooth(const char *name) {
  // Link Arduino's strong btInUse() so core startup retains BLE controller RAM.
  // NimBLE-Arduino's own link anchor is disabled in this pinned SDK build.
  btStarted();
  bufferMutex = xSemaphoreCreateMutex();
  if (!bufferMutex) abort();
  const BluetoothName initial(name);
  if (!NimBLEDevice::init(initial.value)) abort();
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  server = NimBLEDevice::createServer(); server->setCallbacks(&connectionCallbacks, false);
  auto *service = server->createService("e97b1000-3c27-4a20-9df4-7d025bc7e451");
  auto *rx = service->createCharacteristic("e97b1001-3c27-4a20-9df4-7d025bc7e451", NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
  rx->setCallbacks(&requestCallbacks);
  tx = service->createCharacteristic("e97b1002-3c27-4a20-9df4-7d025bc7e451", NIMBLE_PROPERTY::NOTIFY);
  tx->setCallbacks(&notificationCallbacks);
  if (!server->start()) abort();
  auto *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(service->getUUID()); advertising->enableScanResponse(true);
  NimBLEAdvertisementData scan;
  scan.setName(initial.value, initial.complete);
  if (!advertising->setScanResponseData(scan) || !advertising->start()) abort();
  ble_npl_event_init(&nameEvent, [](ble_npl_event *) {
    BluetoothName next;
    if (xQueueReceive(nameQueue, &next, 0) != pdTRUE) return;
    if (!NimBLEDevice::setDeviceName(next.value)) return;
    NimBLEAdvertisementData scan;
    scan.setName(next.value, next.complete);
    NimBLEDevice::getAdvertising()->setScanResponseData(scan);
  }, nullptr);
  nameQueue = xQueueCreate(1, sizeof(BluetoothName));
  if (!nameQueue) abort();
  xTaskCreate([](void *) {
    for (;;) {
      xSemaphoreTake(bufferMutex, portMAX_DELAY);
      if (!ready) {
        if (header && millis() - lastByte > 3000) { free(receiveBuffer); receiveBuffer = nullptr; header = expected = received = 0; }
        xSemaphoreGive(bufferMutex); vTaskDelay(pdMS_TO_TICKS(10)); continue;
      }
      const uint32_t peerSession = session;
      const uint16_t peer = connection;
      std::unique_ptr<uint8_t[]> allocated;
      size_t length = dispatch(receiveBuffer, received, allocated, 8);
      uint8_t fallback[56];
      uint8_t *output = allocated.get();
      if (!length) { output = fallback; length = encodeControlError(receiveBuffer, received, output + 8, sizeof(fallback) - 8); }
      free(receiveBuffer); receiveBuffer = nullptr;
      memcpy(output, "LXP1", 4);
      output[4] = length >> 24; output[5] = length >> 16; output[6] = length >> 8; output[7] = length;
      // The reply owns its bytes now. Accept one pending request while sending
      // it, so a client receiving the last notification cannot race our cleanup.
      header = expected = received = 0; ready = false;
      xSemaphoreGive(bufferMutex);
      const size_t chunkSize = max(20, int(server->getPeerMTU(peer)) - 3);
      for (size_t offset = 0; offset < length + 8;) {
        xSemaphoreTake(bufferMutex, portMAX_DELAY);
        const bool current = connected && session == peerSession && subscribed;
        xSemaphoreGive(bufferMutex);
        if (!current) break;
        const size_t chunk = min(chunkSize, length + 8 - offset);
        bool notified = false;
        for (int attempt = 0; attempt < 50 && !notified; attempt++) {
          xSemaphoreTake(bufferMutex, portMAX_DELAY);
          const bool stillConnected = connected && session == peerSession;
          xSemaphoreGive(bufferMutex);
          if (!stillConnected) break;
          notified = tx->notify(output + offset, chunk, peer);
          if (!notified) vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!notified) break;
        offset += chunk; vTaskDelay(pdMS_TO_TICKS(15));
      }
      allocated.reset();
    }
  }, "luxio-ble-api", 6144, nullptr, 1, nullptr);
}
#elif CONFIG_IDF_TARGET_ESP32 && !CONFIG_BT_NIMBLE_ENABLED
// Retain the working USB/Wi-Fi/Matter stack until combined BLE fits in RAM.
// Do not link the BLE controller: Arduino must release its reserved memory.
void startBluetooth(const char *) {}
void setBluetoothName(const char *) {}
#endif // CONFIG_IDF_TARGET_ESP32
