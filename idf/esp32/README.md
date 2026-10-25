# Experimental original-ESP32 RAM profile

This opt-in profile builds Arduino 3.3.3, ESP-IDF 5.5.1 and esp-matter 1.4.1 from source with native NimBLE. It enables Matter over Wi-Fi and keeps Luxio BLE encryption and bonding enabled. Ordinary release builds and their PSRAM gate remain unchanged.

Build from the repository root after running `tools/bootstrap-firmware.sh`:

```sh
python3 tools/build-lowram.py --build 32
```

Docker is required. The tool pins the SDK image and component lockfile, checks security settings, partition capacity and compiler stack reports. This does not publish or flash firmware. For a controlled hardware test, only write `build/Luxio.bin` to the application partition at **0x130000**. Do not use generated IDF `flash_args`: they target the recovery partition at 0x10000.

The profile reduces Wi-Fi/BLE buffers, Matter endpoint/event reservations and the cloud task stack. Matter pool objects allocate on demand, retain their capacity limits and enforce fabric quotas. All Matter consumers compile against the same checked header overlay; no precompiled archive is patched. Smaller buffers can reduce throughput and require hardware qualification.

The profile runs one CPU core so the SDK can use otherwise unused instruction RAM through its byte-access exception handler. Matter metadata, transient cryptographic allocations and the three rendering frames use that pool; Lua stays in ordinary DRAM. Unaligned accesses incur overhead.

LED output uses a complete SPI3 DMA waveform on the configured data GPIO, including the reset interval. It needs `20 * LED count + 240` bytes of DMA-capable DRAM (20,720 bytes for 1,024 WS2805 LEDs). This avoids RMT refill deadlines while both radios are active. Ordinary builds retain their existing RMT output. The user confirmed that this DMA output eliminated the Rainbow flashes on the physical 60-LED WS2805 strip.

The peak 1,024-LED workload with simultaneous Matter commissioning, update checks and BLE/HTTP control still causes allocation failures. Do not treat this as a qualified 1,024-LED combined-radio release. This remains an opt-in, unpublished profile. See `docs/lowram-esp32.md` for the exact hardware test results and limitations.
