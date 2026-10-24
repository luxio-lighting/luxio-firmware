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
GL-C-016WL-D hardware revisions differ: older documentation specifies GPIO12
without inversion; the current manufacturer manual specifies GPIO18 with inversion.
Our physically tested unit requires GPIO18 without inversion. There is no automatic
board detection. Data uses GPIO16 for these mappings.

The final original-ESP32 application passed physical USB and HTTP checks on the
GL-C-016WL-D: default-disabled migration, GPIO12 configuration, invalid-pin
rejection, legacy SET_CONFIG preservation, inversion/disable roundtrips, two
reboots with persisted relay settings, 16 framed USB state replies and five HTTP
state reads. SSID/gateway/DNS were present after association and DHCP completed.
The user's identity, Wi-Fi credentials and strip configuration were retained;
its unsaved lighting preview was restored after the reboot tests. Relay configuration on this test device is explicit, independently of the disabled
default.
The user reported 24 V at the controller input but 0 V at its LED output under
both relay polarities. A temporary diagnostic build confirmed GPIO12 output
enable and GPIO routing, with the output register switching high/low as requested
and its output disabled when Relay was disabled. The diagnostic build is not part
of the release. These GPIO12 checks did not establish visible LED operation; subsequent testing
resolved the issue with GPIO18, as recorded below.

Host validation passes TypeScript checking, all 32 application tests and native
protocol, storage, transition, packaging and target-layout tests. The ESP32 main
image is 2,148,464 bytes; recovery is 1,102,608 bytes. Compiler stack checks pass.
Mobile checks include native Swift codec tests, an iOS settings save/reopen UI
test, an iOS simulator build, and Android unit tests/debug APK build.

Build 27 was published successfully by workflow run 40127940237. Independent API
checks downloaded all five parts for ESP32/S3/C3/C5/C6 and verified build number,
channel, target, size, SHA256, embedded chip ID and flash addresses.

Further controller diagnosis: the user confirmed the same hardware worked with
WLED. A minimal GPIO12-only application read the driven pin back as high, but the
user still measured 0 V at the LED output. The current manual linked from the
[manufacturer product page](https://www.gledopto.com/h-pd-91.html) specifies GPIO18
with inversion, unlike the older manual used initially. The normal build-27
application was restored and verified, and GPIO18 inverted was configured. HTTP
confirmed the settings, power on and restored lighting preview. The user reported
output power following the opposite app state with inversion enabled. GPIO18 was
then set to inversion off and power on; the user confirmed that it now works as
expected. The final tested device configuration is data GPIO16, relay GPIO18,
inversion off. The release default remains relay disabled. Saved system/NVS and storage partitions were backed up
and checked against device MD5 before the diagnostic; they were not overwritten.

## Build 28 runtime Matter policy and low-memory fixes (2026-10-24)

Matter remains compiled into every public target. Startup skips its initialization
unless PSRAM probing finds at least 2 MiB total and 1 MiB free after LED frame
allocation. ESP32/S3/C5 enable probing; the S3 profile still supports QSPI, not OPI.
This threshold is Luxio policy, not a Matter specification requirement. Static
Matter allocations remain linked. Original ESP32 Luxio BLE remains disabled.
The state protocol exposes `matter_disabled`; network diagnostics expose PSRAM
total/free bytes. Updated iOS and Android clients hide unavailable Matter settings.
Older firmware omits the flag and retains its existing mobile controls.

Three LED frames now allocate 18 bytes per configured LED instead of always
reserving 18 KiB. Growing the count allocates replacement frames before releasing
the existing allocation; failure keeps the old frames. Shrinking retains capacity
until reboot. Nanopb allocation failures report insufficient memory instead of
misreporting a valid request as malformed Protobuf.

The GL-C-016WL-D reported no usable PSRAM in an independent probe with PSRAM enabled.
Build 27 reproduced both errors: valid gradients with 1000/1500/2048-byte metadata
failed decoding, and HTTPS update checking failed TLS allocation. Runtime Matter
gating alone fixed gradients but left insufficient memory for TLS RSA processing.
Sizing LED frames for this device's 60 LEDs recovered the additional memory needed.
The final build 28 measured 56,056 bytes free internal RAM and a 51,188-byte largest
block during a state request (build 27: 6,020 and 2,932 bytes).

Physical validation passed three rounds of real iOS gradient fixtures and all
previously failing metadata sizes, each followed by a successful HTTPS update check.
Changing the LED count 60 → 61 → 60 also passed. Identity, Wi-Fi and data/relay
configuration were preserved, and the original lighting state was restored.
The ESP32 application is 2,166,832 bytes; recovery is 1,117,152 bytes. Image and stack
checks, TypeScript checks and all host/native tests pass. iOS protocol tests and the
signed phone build pass; the app is installed and launched on the user's iPhone.
Android unit tests and the debug APK build pass. Other boards, the positive Matter
startup path and large-strip TLS headroom still require physical qualification.

Build 28 was published on channel 0 by workflow run 40130627866 from commit
2dad5bf46859c8531ed74eeafbff4e1a16c8af5d. All five chip builds and publication
succeeded. Independent production API checks downloaded all 25 parts and verified
build/channel/target, sizes, SHA-256 hashes, embedded chip IDs and flash offsets.
Setup and API target snapshots were synchronized; partition layouts are unchanged.

## Build 29 native RMT and Wi-Fi-gated Matter (2026-10-24, unpublished)

Matter eligibility retains build 28's PSRAM policy. Eligible devices now create
and start the endpoint only after Wi-Fi is connected and has an IP address.
Initialization happens once per boot in the cloud worker, before HTTPS work;
reconnections do not recreate the endpoint. Pairing-code reads while offline
report that Wi-Fi is required. Local controls and recovery acknowledgment do not
wait for Matter or a network connection.

LED transmission now uses ESP-IDF's RMT byte encoder directly. This removes the
Arduino RMT wrapper from the output path and replaces the 160-byte-per-LED symbol
buffer with a five-byte-per-LED internal-RAM payload. RGB/RGBW/RGBCCT byte order,
bit timings, end level and the 600 us low reset interval are preserved. The payload
is retained until transmission finishes; configuration changes occur under the
same state mutex. At 100 LEDs, color/fade and transmission buffers total 2,300
bytes instead of 17,804 bytes, excluding fixed driver overhead. Other Arduino
APIs and the pinned precompiled TLS configuration remain in use.

Testing also reproduced an existing Cyberpunk animation wall-clock timeout on
released build 28 during HTTPS work. Lua now checkpoints the task runtime counter
at instruction hooks and enforces 30 ms startup / 20 ms frame CPU budgets rather
than charging time preempted by other tasks. The instruction and memory ceilings
remain unchanged. The SDK must provide microsecond task runtime statistics, as
all five pinned target profiles do. The cloud worker retains its original priority.

Validation: TypeScript and all host/native tests pass. A new harness executes the
production render function, Matter startup prefix and Lua hook with fake hardware:
1/60/100/1024 LEDs, all three channel layouts, relay power, reset timing, offline
startup, association without an IP, reconnection without duplicate startup,
Matter-disabled builds/devices, CPU-counter rollover and instruction exhaustion.
All five final targets compile and pass image, layout and compiler stack checks:

| Chip | Main bytes | Recovery bytes |
| --- | --- | --- |
| esp32 | 2,163,648 | 1,117,152 |
| esp32s3 | 2,294,864 | 1,100,656 |
| esp32c3 | 2,351,488 | 1,180,768 |
| esp32c5 | 3,021,808 | 1,239,648 |
| esp32c6 | 2,991,872 | 1,186,960 |

The GLEDOPTO received the candidate application only; identity, Wi-Fi and saved
storage were preserved. Three gradient/HTTPS rounds and LED count 60 → 100 → 60
passed. The captured Cyberpunk source was restored and remained running through
an update check with CPU accounting, although concurrent state polling briefly
received HTTP 503 under memory pressure. The app subsequently selected Knight
Rider at brightness 4; tests respected that newer selection. Two further HTTPS
checks passed while Knight Rider ran, but the third returned a TLS allocation
error. These results do not qualify fully concurrent animation/app/TLS operation.
Visual LED operation could not be checked by the user, and actual deferred Matter
startup on a PSRAM-equipped board remains unqualified.

Build 29 is committed for further qualification but is not published to the API.
The public channel remains on build 28. Additional TLS/request-memory work is
required before publishing this candidate; the RMT saving alone does not resolve
all no-PSRAM concurrency limits.

## Build 30 response and TLS memory candidate (2026-10-25)

All transports now size Protobuf replies before allocating them, under the state
mutex; the 32 KiB wire limit remains. HTTP/USB/BLE return bounded emergency errors
when allocations fail. WebSocket broadcasts also use exact-sized output. Storage
scratch allocations are scoped to their operation: reads use the stored length,
color/gradient writes use the encoded length, and script writes reserve source
length plus the bounded metadata/header size. Cloud announcements copy only the
fields they send, and completed HTTPS connections close before parsing JSON.

`tools/build-tls.py` rebuilds the 17 TLS record-layer objects from the exact pinned
Espressif Mbed TLS source revision, with SHA-256 archive/header checks and ABI
size/alignment comparisons. All SDK feature switches remain unchanged. RX stays
16 KiB; TX becomes 4 KiB, saving 12 KiB per connection. The installed SDK is not
modified. Dynamic TLS buffers are not enabled: they conflict with the SDK's DTLS
configuration and require broader SDK rebuilding.

TypeScript, host/native regression tests, and all five chip builds pass, including
partition and stack budgets. Tests cover exact-sized replies, full 16 KiB script
responses, callback lists, failed allocations, wire limits and smaller storage
scratch buffers. Final packaged sizes:

| Chip | Main bytes | Recovery bytes |
| --- | --- | --- |
| esp32 | 2,165,408 | 1,117,136 |
| esp32s3 | 2,296,624 | 1,100,624 |
| esp32c3 | 2,352,736 | 1,180,752 |
| esp32c5 | 3,023,056 | 1,239,632 |
| esp32c6 | 2,993,104 | 1,186,944 |

The GLEDOPTO ran build 30 with the response/TLS changes and Knight Rider at
brightness 4. Thirty HTTPS update checks passed with 100 LEDs, 332 HTTP polls,
332 WebSocket replies and active broadcast generation. Lowest sampled free
internal RAM was 6,876 bytes, with a 4,340-byte largest block. The test's final
assertion initially expected repeated identical state broadcasts; only one was
correctly emitted because state did not change. All 30 rounds had completed and
the saved results pass the corrected assertion. LED count returned to 60 and the
current animation was restored. The subsequent smaller storage-scratch adjustment
has host/build coverage; it was not in this physical soak image.

This still leaves too little headroom to enable Bluetooth and Matter together on
this no-PSRAM board. Visual LED output remains unconfirmed by the user. Build 30
is not publicly published; the next change moves advisory checks and signed
announcements to HTTP, retaining independently authenticated HTTPS in recovery.
The current production API redirects HTTP to HTTPS and needs device-route
exceptions managed by the server owner. TLS pinning and signed OTA transport
alternatives were discussed but have not been implemented.
