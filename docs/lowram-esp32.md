# Original ESP32 RAM optimization — 2026-10-25

Release update: build 35 promotes this profile to the original ESP32 release package, including the cooperative animation scheduler. The measurements below are historical qualification records; the peak-memory caveats still apply.

The experimental SPI DMA output eliminated the wrong-color/white flashes reported with Rainbow on the physical strip. Matter and encrypted BLE remain enabled without PSRAM. The worst-case 1,024-LED simultaneous commissioning/update/control workload is **not yet reliable**: it can exhaust memory. This profile is local and unpublished; ordinary releases retain their Matter PSRAM gate and RMT output.

Hardware: GLEDOPTO GL-C-016WL-D, ESP32-D0WD-V3 revision 3.1, 4 MiB flash, no PSRAM, GPIO16 data and GPIO18 non-inverted relay. The physical strip has 60 WS2805 LEDs. Configuring 1,024 exercises full buffer allocation, rendering and waveform transmission, but is not visual validation of a physical 1,024-LED strip.

## Memory and output

| Measurement | Bytes |
| --- | ---: |
| Fixed DRAM, earlier combined-radio diagnostic | 121,376 |
| Fixed DRAM, current SPI DMA profile | 75,764 |
| Fixed DRAM reduction | 45,612 |
| Three rendering frames at 1,024 LEDs, allocated in byte-accessible IRAM | 18,432 |
| DMA waveform allocation at 60 LEDs | 1,440 |
| DMA waveform allocation at 1,024 LEDs | 20,720 |
| Application image after transport fixes | 2,298,896 |
| Application partition capacity | 2,621,440 |

DMA uses `20 * LED count + 240` bytes: four SPI bits per LED bit, up to five channels, and a 600-us low reset tail. SPI runs at 3.2 MHz: zero is 312.5 ns high / 937.5 ns low; one is 625 ns high / 625 ns low. The complete frame is sent in one DMA transaction with no CPU refill deadline during the waveform.

The three rendering frames, Matter metadata and transient cryptographic allocations use ESP-IDF's byte-accessible instruction RAM pool. Lua and DMA storage stay in ordinary DRAM. The SDK requires single-core mode for the IRAM byte-access handler; unaligned accesses incur exception overhead. Allocations grow when LED count increases and reclaim excess capacity on reboot.

Script source strings allocate their actual length instead of reserving 16,385 bytes per structure. Animation startup transfers ownership of the decoded source, and Lua uses a short chunk name rather than retaining another source copy. Library initialization is protected. Build 35 runs Lua source initialization, setup and frames in resumable CPU slices, with separate hard limits; see the animation scheduling section in the main README. Responses borrow the bounded Wi-Fi scan cache under the state mutex instead of embedding another 32-network array. These changes preserve the wire format.

BLE now releases its receive slot before sending the reply, allowing one pending request while notifications are in flight. Previously it cleared `ready` after the final notification and a task delay: a client could already have received the complete reply and get an insufficient-resource error on its next request. The worker no longer clears a newer pending request during completion cleanup.

Concurrent tests also exposed malformed WebSocket frames. Replies run on the HTTP task while broadcasts run on the animation task. Broadcasts now transfer ownership of their encoded payload to an HTTP-server work item, so replies, broadcasts and automatic PONGs all write on the same task. Failed queue submission frees the payload and leaves the digest eligible for retry. `tools/test-websocket-concurrency.ts` exercises replies while brightness changes generate broadcasts, then restores the original brightness.

The profile builds Arduino 3.3.3, ESP-IDF 5.5.1 and esp-matter 1.4.1 from source with native NimBLE. Wi-Fi/BLE buffers, Matter endpoint/event reservations and the cloud task stack are smaller. Matter pools allocate on demand, preserving capacity limits and fabric quotas. All consumers use the same checked header overlay; no precompiled archive is patched. BLE encryption/bonding, signed OTA verification, recovery code and partition layout remain unchanged.

## Validation

On the final image at **60 LEDs**, the simultaneous workload passed 30 signed update checks with HTTP/WebSocket polling and a separate WebSocket regression stress test (426 replies, 60 broadcasts and 60 automatic PONGs). The update test sampled at least 27,656 bytes free DRAM and a 15,348-byte largest free block. Matter commissioning/CASE and 90 reads completed; the temporary fabric was removed. Encrypted BLE completed 524 immediate requests across three connections with no errors or detected restart (minimum sampled free DRAM 26,568 bytes; largest free block 15,348 bytes). All four concurrent test processes exited successfully.


- Full host regression suite and TypeScript checks pass, including sanitizer-backed storage and Protobuf ownership/cleanup tests. A 32-network response round trip verifies the borrowed Wi-Fi cache encoding.
- SPI encoder tests verify channel order, every encoded bit, the reset tail, alignment and buffer bounds at 1, 60 and 1,024 LEDs. Production startup/render/relay tests pass with Matter on/off and DMA on/off.
- All five ordinary firmware targets (ESP32, S3, C3, C5, C6) build, pass stack checks and fit their application/recovery partitions after the shared source/protocol changes.
- On the compact-response DMA image, 30 signed-HTTP update checks with concurrent HTTP/WebSocket polling passed at 1,024 LEDs. Minimum sampled free DRAM was 11,576 bytes; smallest sampled free block was 4,852 bytes.
- The simultaneous encrypted BLE soak completed 273 reads across three connections at 1,024 LEDs. Minimum sampled free DRAM was 13,456 bytes; smallest sampled free block was 7,924 bytes. No restart or animation error was detected.
- Matter commissioning timed out at the CSR request during that first combined run. Device serial output subsequently reported fail-safe cleanup. The device remained responsive. Commissioning succeeded on retry; this timeout must not be counted as a passing combined test.
- Full encrypted BLE integration at 1,024 LEDs passed identity checks, deliberately fragmented requests, pairing-code retrieval, Wi-Fi scanning, color/brightness control, approximately 1.2 KiB script upload/download/run/delete, and reconnect. Matter commissioning succeeded on retry and completed 90 reads; the temporary fabric was removed. Matter reads overlapped the BLE integration checks.
- At 60 LEDs before the BLE handoff fix, 30 updates and Matter commissioning/90 reads passed, but the BLE soak hit an ATT insufficient-resource error with ample heap remaining. Code inspection identified the reply/next-request race described above.
- A second simultaneous commissioning/30-update/BLE run at 1,024 LEDs failed: both HTTP and BLE returned `Not enough memory for this request`, and serial diagnostics reported `PacketBuffer: pool EMPTY` during CASE establishment. Matter retried CASE successfully, completed 90 reads and removed the temporary fabric. This confirms insufficient peak memory margin despite the individual passing workloads; it is not just a visual-output issue.

Heap readings are samples, not worst-case guarantees. Maximum-size BLE transfers, arbitrary 16 KiB Lua programs, a physical 1,024-LED strip and other controller/strip models remain unqualified. Actual OTA installation was not repeated; update checks and signature handling were exercised. The user also confirmed no flashes after the IRAM/response-memory changes. The later BLE/WebSocket fixes do not change the LED output path.

## Flicker investigation and WLED comparison

The preceding ordinary build 32 ran the identical 60-LED Rainbow at brightness 4 without flashes. The optimized single-core profile flashed with both the direct RMT encoder and the preceding SDK byte encoder, including after increasing peripheral memory and using cache-safe priority-3 interrupts. Protocol/heap tests passed even when the strip visibly flashed.

RMT refill starvation under radio load is a working explanation, not a measured waveform diagnosis. [Espressif documents stale transmitted data when refill deadlines are missed](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/api-reference/peripherals/rmt.html#faq). Full-frame DMA removed that dependency, and the user confirmed no flashes with Matter and BLE enabled.

[WLED's multi-strip documentation](https://kno.wled.ge/features/multi-strip/) describes 4,000 LEDs across four 1,000-LED outputs. Its [bus wrapper](https://github.com/wled/WLED/blob/main/wled00/bus_wrapper.h) supports NeoPixelBus RMT and I2S backends. The inspected NeoPixelBus dependency was commit `76afe832f74b0738a3fa1bba0caf389ade9e7693`: its RMT driver uses separate edit/send buffers and 192 symbols; its original ESP32 I2S drivers allocate full DMA waveforms, with parallel lanes sharing that waveform. Luxio adopts the complete-waveform approach using SPI DMA for its single output, not WLED's implementation or exact timings.

The four-bit waveform fits the [WS2805 V0.3 timing table](https://xonstorage.blob.core.windows.net/pdf/worldsemi_ws2805_apr22_xonlink.pdf). A preliminary three-bit/2.5-MHz image was never flashed because its zero-high pulse exceeded that table's limit.

The final image remains installed with the original 60-LED configuration, GPIO16 data, GPIO18 non-inverted relay, brightness 4, power on and Rainbow restored. State was read back and verified after all tests; temporary Matter controller storage was removed. The experimental original-ESP32 profile remains unpublished.

Current application SHA-256: `aff577a3828cfc95ec7041412f0ca8f433eaa09c780a45291a548196af754679`.

## Reproduce

From the repository root, with the pinned Arduino SDK installed and Docker available:

```sh
python3 tools/build-lowram.py --build 32
npm test
npm run check
LUXIO_BUILD_ROOT=.sdk/lowram/stock-matrix LUXIO_BUILD=32 python3 tools/build-firmware.py all
```

See `idf/esp32/README.md` for controlled application-only flashing at **0x130000**. Generated IDF `flash_args` target 0x10000 and must not be used: that is Luxio's recovery partition. The source-built app uses an 80-MHz flash clock matching the installed recovery bootloader. A 40-MHz experiment failed SDK startup. PSRAM boot initialization is disabled so Arduino correctly probes absent PSRAM.
