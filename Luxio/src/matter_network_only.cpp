// The pinned Arduino SDK includes CHIPoBLE in its precompiled Matter library.
// tools/build-firmware.py weakens ONLY these five SDK definitions in a private
// archive copy. Keep the Matter BleLayer ABI, but never start/stop the radio or
// process Bluetooth events: bluetooth.cpp exclusively owns NimBLE and GATT.
// Disabling advertising alone would still initialize Matter's NimBLE host.
#include <sdkconfig.h>
#ifndef LUXIO_MATTER
#define LUXIO_MATTER 1
#endif
#if LUXIO_MATTER
#ifndef LUXIO_NETWORK_ONLY_MATTER
#error Use tools/build-firmware.sh to build Matter with Luxio Bluetooth.
#endif
#include <platform/internal/CHIPDeviceLayerInternal.h>
#include <platform/internal/BLEManager.h>
#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
namespace chip::DeviceLayer::Internal {
CHIP_ERROR BLEManagerImpl::_Init() {
  mFlags.ClearAll();
  mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_NotSupported;
  mNumGAPCons = 0;
  memset(mCons, 0, sizeof(mCons));
  memset(mDeviceName, 0, sizeof(mDeviceName));
  return BleLayer::Init(this, this, &DeviceLayer::SystemLayer());
}
void BLEManagerImpl::_Shutdown() { BleLayer::Shutdown(); }
void BLEManagerImpl::_OnPlatformEvent(const ChipDeviceEvent *) {}
CHIP_ERROR BLEManagerImpl::_SetAdvertisingEnabled(bool enabled) {
  return enabled ? CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE : CHIP_NO_ERROR;
}
CHIP_ERROR BLEManagerImpl::_SetAdvertisingMode(BLEAdvertisingMode) {
  return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}
} // namespace chip::DeviceLayer::Internal
#endif
#endif
