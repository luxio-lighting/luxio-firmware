# Firmware qualification

Build 9 includes Matter, manual recovery updates, Identify fade-blink and Luxio Lighting / DIY metadata. S3 Mini hardware passed interrupted/corrupt downloads, manual retry and reset during writing. The user confirmed Apple Home re-pairing and Identify.

Automatic tagged releases remain blocked on physical qualification of Luxio BLE with Matter over Wi-Fi, production attestation/commissioning credentials, conformance, reverse state synchronization, wider board/strip qualification and further resilience tests. Explicit manual workflow publication is available for operator-requested testing, including channel 0 for Setup; this does not resolve those qualification gaps. No full application rollback image is retained. See recovery-updates.md for update behavior.

## Build 23 firmware matrix (2026-10-24)

All five targets compile with Matter enabled using the pinned Arduino ESP32 3.3.3 SDK. Main/recovery binaries pass embedded chip-ID, partition-table, image-size and compiler stack-budget checks. These are build results, not physical board qualification.

| Chip | Main bytes | Recovery bytes | Minimum flash |
| --- | --- | --- | --- |
| esp32 | 2,141,072 | 1,102,608 | 4 MB |
| esp32s3 | 2,290,816 | 1,100,656 | 4 MB |
| esp32c3 | 2,348,768 | 1,180,768 | 4 MB |
| esp32c5 | 3,014,864 | 1,235,408 | 8 MB |
| esp32c6 | 2,989,312 | 1,186,960 | 8 MB |

Validation includes 56 Setup browser tests; OTA API integration tests for per-chip publication, exact target selection, channel isolation and malformed layouts; 45 package cases; the firmware host/native tests; and persisted-update validation for all five target layouts. Actual compiled packages are also checked through the publisher and a local OTA API/MongoDB instance, with artifact downloads served from local files. No production update records are written by that verification.

ESP32/S3 keep their existing recovery layout. C3 uses a larger recovery slot within 4 MB. C5/C6 use an 8 MB layout because their complete Matter application and HTTPS recovery do not fit the 4 MB layout. C2/C61 remain unavailable with the pinned precompiled SDK. The existing public release/hardware qualification restrictions above still apply; no devices were flashed during this matrix work.

## Build 24 Bluetooth integration (2026-10-24)

Luxio owns the BLE radio; Matter commissioning/control stay on IP. The iOS client
implements discovery, encrypted connections, fragmented request/response,
control, Wi-Fi setup and Matter-code retrieval. The five-method pinned SDK
adapter and original ESP32 NimBLE-Arduino transport are described in the README.
All five main/recovery builds pass chip-ID, partition, size and compiler stack
checks with Arduino ESP32 3.3.3; original ESP32 additionally uses NimBLE-Arduino
2.5.1. Final packaged sizes:

| Chip | Main bytes | Recovery bytes |
| --- | --- | --- |
| esp32 | 2,376,576 | 1,102,608 |
| esp32s3 | 2,288,672 | 1,100,656 |
| esp32c3 | 2,344,448 | 1,180,768 |
| esp32c5 | 3,010,464 | 1,235,408 |
| esp32c6 | 2,984,848 | 1,186,960 |

Host tests, iOS transport tests and an iOS simulator Wi-Fi/QR UI test pass.
The reattached Wemos/Lolin S3 Mini was backed up and its application updated to
build 24 without changing the partition layout. Its existing identity and Wi-Fi
connection survived. With macOS pairing accepted, the BLE hardware test passed
fragmented state reads, Matter QR/manual retrieval, Wi-Fi scanning, color and
brightness control, animation upload/read/run/delete, reconnection and lighting
state restoration. Wi-Fi remained accessible with about 38 KB free internal RAM
in the sampled state. The custom Luxio service was advertised without a Matter
BLE service in the observed advertisement.

The strict HTTP Wi-Fi scan latency check failed its two-second bound twice;
separate measurements saw a 21 ms scan-start reply and an 8.3 s state reply during
scanning. Scan results themselves succeeded over BLE. This timing issue remains
unresolved. Cold offline startup, credential provisioning, physical iPhone
pairing, Matter commissioning/fabric removal and other boards still require
qualification. No release was published.

## Build 27 network diagnostics and BLE memory fixes (2026-10-24)

GET_STATE includes the connected SSID, gateway and up to two DNS servers. It does
not return the Wi-Fi password. The shared protocol and bounded Nanopb fields are
updated together. BLE receive/reply buffers are allocated per request and released
on completion, disconnect or partial-frame timeout; no-PSRAM replies use an 8 KiB
workspace and allocation failures return the bounded protocol error.

Physical testing of the combined stack on a GL-C-016WL-D (ESP32-D0WD-V3, 4 MB,
no PSRAM) exposed an Arduino Bluetooth memory-release conflict and insufficient
RAM. Linking Arduino's BT anchor fixes the premature release, but even starting
BLE before Wi-Fi leaves only about 12 KiB before HTTP and Matter initialize.
The original ESP32 public build therefore retains USB/Wi-Fi/Matter and keeps
Luxio BLE disabled. Its NimBLE-Arduino transport is available only for diagnostic
builds with LUXIO_MATTER=0. S3/C3/C5/C6 retain the combined BLE/IP-Matter integration.
The earlier original-ESP32 compilation result did not establish runtime support.

Optional relay control defaults to disabled. Config.relay contains enabled,
gpio and inverted; an omitted message preserves existing settings for older
clients. The relay follows power and Identify, allows the off fade to finish,
and waits 20 ms after enabling power before sending LED data. Pin validation
rejects data-pin conflicts and the chip's reserved serial/flash/PSRAM/USB pins.
GL-C-016WL-D uses GPIO12 without inversion; there is no automatic board detection.

The final original-ESP32 application passed physical USB and HTTP checks on the
GL-C-016WL-D: default-disabled migration, GPIO12 configuration, invalid-pin
rejection, legacy SET_CONFIG preservation, inversion/disable roundtrips, two
reboots with persisted relay settings, 16 framed USB state replies and five HTTP
state reads. SSID/gateway/DNS were present after association and DHCP completed.
The user's identity, Wi-Fi credentials and strip configuration were retained;
its unsaved lighting preview was restored after the reboot tests. GPIO12 remains
explicitly enabled on this test device, independently of the disabled default.
Electrical relay output/visible LED operation still needs user confirmation.

Host validation passes TypeScript checking, all 32 application tests and native
protocol, storage, transition, packaging and target-layout tests. The ESP32 main
image is 2,148,448 bytes; recovery is 1,102,608 bytes. Compiler stack checks pass.
Mobile checks include native Swift codec tests, an iOS settings save/reopen UI
test, an iOS simulator build, and Android unit tests/debug APK build.
