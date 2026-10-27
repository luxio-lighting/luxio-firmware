#include "bluetooth_name.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

// Exercise the production queue, host callback and advertising code with a
// fake radio. Calling GAP synchronously while dispatch holds its mutex fails.
struct Queue { BluetoothName value; bool pending = false; } queue;
using QueueHandle_t = Queue *;
struct ble_npl_event { void (*callback)(ble_npl_event *); bool queued = false; };
struct ble_uuid128_t { uint8_t bytes[16]; };
static const ble_uuid128_t serviceUuid = {{0x51,0xe4,0xc7,0x5b,0x02,0x7d,0xf4,0x9d,0x20,0x4a,0x27,0x3c,0x00,0x10,0x7b,0xe9}};
struct ble_hs_adv_fields {
  int flags = 0; ble_uuid128_t *uuids128 = nullptr; int num_uuids128 = 0, uuids128_is_complete = 0;
  const uint8_t *name = nullptr; size_t name_len = 0; bool name_is_complete = false;
};
struct ble_gap_adv_params { int conn_mode, disc_mode; };
constexpr int pdTRUE = 1, BLE_HS_ADV_F_DISC_GEN = 2, BLE_HS_ADV_F_BREDR_UNSUP = 4;
constexpr int BLE_GAP_CONN_MODE_UND = 1, BLE_GAP_DISC_MODE_GEN = 2, BLE_HS_FOREVER = -1;
static uint8_t ownAddressType;
static bool dispatchLocked, synced, complete;
static std::string gapName, scannedName;
static int starts, scanUpdates;
static const ble_uuid128_t *advertisedService;
int gapEvent(void *, void *) { return 0; }
QueueHandle_t xQueueCreate(int count, size_t size) { assert(count == 1 && size == sizeof(BluetoothName)); return &queue; }
void xQueueOverwrite(QueueHandle_t q, const BluetoothName *name) { q->value = *name; q->pending = true; }
int xQueueReceive(QueueHandle_t q, BluetoothName *name, int) { if (!q->pending) return 0; *name = q->value; q->pending = false; return pdTRUE; }
void *nimble_port_get_dflt_eventq() { return &queue; }
void ble_npl_eventq_put(void *, ble_npl_event *event) { event->queued = true; }
void ble_npl_event_init(ble_npl_event *event, void (*callback)(ble_npl_event *), void *) { event->callback = callback; }
void ble_svc_gap_init() {}
void ble_svc_gatt_init() {}
int ble_svc_gap_device_name_set(const char *name) { assert(!dispatchLocked && strlen(name) <= 31); gapName = name; return 0; }
const char *ble_svc_gap_device_name() { return gapName.c_str(); }
bool ble_hs_synced() { return synced; }
void ble_gap_adv_set_fields(const ble_hs_adv_fields *fields) { advertisedService = fields->uuids128; assert(fields->num_uuids128 == 1 && fields->uuids128_is_complete); }
int ble_gap_adv_rsp_set_fields(const ble_hs_adv_fields *fields) {
  assert(!dispatchLocked && fields->name_len <= 29);
  scannedName.assign(reinterpret_cast<const char *>(fields->name), fields->name_len);
  complete = fields->name_is_complete; scanUpdates++; return 0;
}
void ble_gap_adv_start(uint8_t, void *, int, ble_gap_adv_params *, int (*)(void *, void *), void *) { starts++; }

// PRODUCTION_DECLARATIONS
// PRODUCTION_ADVERTISE
void start(const char *name) {
// PRODUCTION_INITIALIZE
}

int main() {
  setBluetoothName("Before startup"); // Safe before BLE initialization.
  start("Kitchen ceiling");
  synced = true; advertise();
  assert(gapName == "Kitchen ceiling" && scannedName == gapName && complete);
  assert(advertisedService == &serviceUuid && starts == 1);

  dispatchLocked = true;
  setBluetoothName("Living room");
  setBluetoothName("Bedroom"); // The latest successful rename wins.
  assert(gapName == "Kitchen ceiling" && scanUpdates == 1 && nameEvent.queued);
  dispatchLocked = false; nameEvent.queued = false; nameEvent.callback(&nameEvent);
  assert(gapName == "Bedroom" && scannedName == gapName && complete);
  assert(starts == 1 && scanUpdates == 2 && advertisedService == &serviceUuid);
  nameEvent.callback(&nameEvent); // Empty event must not undo the rename.
  assert(scanUpdates == 2);
  advertise(); // Disconnect/reconnect keeps the configured name and UUID.
  assert(scannedName == "Bedroom" && advertisedService == &serviceUuid);

  synced = false; setBluetoothName("Desk"); nameEvent.callback(&nameEvent);
  assert(gapName == "Desk" && scannedName == "Bedroom");
  synced = true; advertise();
  assert(scannedName == "Desk");

  for (size_t length : {size_t(28), size_t(29), size_t(30), size_t(63)}) {
    const std::string original(length, 'A');
    setBluetoothName(original.c_str()); nameEvent.callback(&nameEvent);
    assert(scannedName == original.substr(0, 29) && complete == (length <= 29));
  }
  for (const std::string character : {std::string("é"), std::string("灯"), std::string("💡")}) {
    for (size_t prefix = 25; prefix <= 29; prefix++) {
      const std::string original = std::string(prefix, 'A') + character;
      setBluetoothName(original.c_str()); nameEvent.callback(&nameEvent);
      assert(scannedName == (original.size() <= 29 ? original : std::string(prefix, 'A')));
      assert(complete == (original.size() <= 29));
    }
  }
  for (const char *missing : {"", static_cast<const char *>(nullptr)}) {
    setBluetoothName(missing); nameEvent.callback(&nameEvent);
    assert(scannedName == "Luxio" && complete);
  }
  std::cout << "Bluetooth saved-name, queued rename, discovery UUID and UTF-8 boundary tests passed\n";
}
