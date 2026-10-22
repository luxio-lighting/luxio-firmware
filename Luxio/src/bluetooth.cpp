#include <Arduino.h>
#ifndef LUXIO_MATTER
#define LUXIO_MATTER 1
#endif
// The Arduino SDK owns NimBLE when Matter is enabled. The standalone transport
// below is enabled in the diagnostic build. Sharing it with CHIPoBLE requires
// the ESP-Matter source build's GATT/lifecycle integration (see docs/status.md).
#if !LUXIO_MATTER
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

extern size_t dispatch(const uint8_t *, size_t, uint8_t *);
// e97b1000-3c27-4a20-9df4-7d025bc7e451; RX 1001, TX 1002.
static const ble_uuid128_t serviceUuid = BLE_UUID128_INIT(0x51,0xe4,0xc7,0x5b,0x02,0x7d,0xf4,0x9d,0x20,0x4a,0x27,0x3c,0x00,0x10,0x7b,0xe9);
static const ble_uuid128_t rxUuid = BLE_UUID128_INIT(0x51,0xe4,0xc7,0x5b,0x02,0x7d,0xf4,0x9d,0x20,0x4a,0x27,0x3c,0x01,0x10,0x7b,0xe9);
static const ble_uuid128_t txUuid = BLE_UUID128_INIT(0x51,0xe4,0xc7,0x5b,0x02,0x7d,0xf4,0x9d,0x20,0x4a,0x27,0x3c,0x02,0x10,0x7b,0xe9);
static uint16_t txHandle, connection = BLE_HS_CONN_HANDLE_NONE;
static uint8_t *receiveBuffer;
static uint32_t header, expected, received, lastByte;
static bool ready, subscribed;
static SemaphoreHandle_t bufferMutex;
static uint8_t ownAddressType;
static void advertise();
static int gapEvent(ble_gap_event *event, void *) {
  xSemaphoreTake(bufferMutex, portMAX_DELAY);
  bool restart = false;
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    if (!event->connect.status) connection = event->connect.conn_handle; else restart = true;
  } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    connection = BLE_HS_CONN_HANDLE_NONE; subscribed = false; ready = false; received = expected = header = 0; restart = true;
  } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE) subscribed = event->subscribe.cur_notify;
  xSemaphoreGive(bufferMutex);
  if (restart) advertise();
  return 0;
}
static void advertise() {
  ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = const_cast<ble_uuid128_t *>(&serviceUuid); fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
  ble_gap_adv_set_fields(&fields);
  ble_hs_adv_fields scan = {}; const char *name = ble_svc_gap_device_name();
  scan.name = reinterpret_cast<const uint8_t *>(name); scan.name_len = strlen(name); scan.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&scan);
  ble_gap_adv_params parameters = {}; parameters.conn_mode = BLE_GAP_CONN_MODE_UND; parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(ownAddressType, nullptr, BLE_HS_FOREVER, &parameters, gapEvent, nullptr);
}
static int writeRequest(uint16_t conn, uint16_t, ble_gatt_access_ctxt *context, void *) {
  if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  xSemaphoreTake(bufferMutex, portMAX_DELAY);
  if (ready || conn != connection || !subscribed) { xSemaphoreGive(bufferMutex); return BLE_ATT_ERR_INSUFFICIENT_RES; }
  if (header && millis() - lastByte > 3000) header = expected = received = 0;
  uint16_t length = OS_MBUF_PKTLEN(context->om); uint8_t chunk[512];
  if (length > sizeof(chunk) || os_mbuf_copydata(context->om, 0, length, chunk)) { xSemaphoreGive(bufferMutex); return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; }
  for (uint16_t i = 0; i < length; i++) {
    uint8_t byte = chunk[i]; lastByte = millis();
    if (header < 4) { const uint8_t magic[4] = {0x4c,0x58,0x50,0x31}; header = byte == magic[header] ? header + 1 : byte == magic[0] ? 1 : 0; continue; }
    if (header < 8) { expected = (expected << 8) | byte; header++; if (header == 8 && (!expected || expected > 32768)) { header = expected = received = 0; xSemaphoreGive(bufferMutex); return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; } continue; }
    receiveBuffer[received++] = byte;
    if (received == expected) { ready = true; break; }
  }
  xSemaphoreGive(bufferMutex); return 0;
}
static ble_gatt_chr_def characteristics[3];
static ble_gatt_svc_def services[2];
void startBluetooth() {
  receiveBuffer = new uint8_t[32768]; bufferMutex = xSemaphoreCreateMutex();
  if (nimble_port_init() != ESP_OK) return;
  ble_svc_gap_init(); ble_svc_gatt_init(); ble_svc_gap_device_name_set("Luxio");
  characteristics[0].uuid = &rxUuid.u; characteristics[0].access_cb = writeRequest; characteristics[0].flags = BLE_GATT_CHR_F_WRITE;
  characteristics[1].uuid = &txUuid.u; characteristics[1].access_cb = writeRequest; characteristics[1].flags = BLE_GATT_CHR_F_NOTIFY; characteristics[1].val_handle = &txHandle;
  services[0].type = BLE_GATT_SVC_TYPE_PRIMARY; services[0].uuid = &serviceUuid.u; services[0].characteristics = characteristics;
  if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) return;
  ble_hs_cfg.sync_cb = []() { if (!ble_hs_util_ensure_addr(0) && !ble_hs_id_infer_auto(0, &ownAddressType)) advertise(); };
  nimble_port_freertos_init([](void *) { nimble_port_run(); nimble_port_freertos_deinit(); });
  xTaskCreate([](void *) {
    auto *output = new uint8_t[32776];
    for (;;) {
      xSemaphoreTake(bufferMutex, portMAX_DELAY);
      if (!ready) { xSemaphoreGive(bufferMutex); vTaskDelay(pdMS_TO_TICKS(10)); continue; }
      const uint16_t peer = connection;
      size_t length = dispatch(receiveBuffer, received, output + 8);
      output[0]=0x4c; output[1]=0x58; output[2]=0x50; output[3]=0x31;
      output[4]=length>>24; output[5]=length>>16; output[6]=length>>8; output[7]=length;
      header = expected = received = 0;
      xSemaphoreGive(bufferMutex);
      const size_t chunkSize = max(20, int(ble_att_mtu(peer)) - 3);
      for (size_t offset = 0; offset < length + 8;) {
        size_t chunk = min(chunkSize, length + 8 - offset);
        bool sent = false;
        for (int attempt = 0; attempt < 50 && !sent; attempt++) {
          if (connection != peer || !subscribed) break;
          auto *mbuf = ble_hs_mbuf_from_flat(output + offset, chunk);
          if (mbuf && ble_gatts_notify_custom(peer, txHandle, mbuf) == 0) sent = true;
          else vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!sent) break;
        offset += chunk; vTaskDelay(pdMS_TO_TICKS(8));
      }
      xSemaphoreTake(bufferMutex, portMAX_DELAY); ready = false; xSemaphoreGive(bufferMutex);
    }
  }, "luxio-ble-api", 6144, nullptr, 1, nullptr);
}
#else
void startBluetooth() {}
#endif
