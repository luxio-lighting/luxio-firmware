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

## Build 32 signed HTTP candidate (2026-10-25)

Device announcements, update checks and recovery downloads now use HTTP.
Announcements retain their challenge/device signatures. Releases carry an ECDSA
P-256/SHA-256 signature over chip, layout, build, channel, size and application
hash. Both main and recovery verify it; recovery also hashes the streamed image
before selecting it to boot. Persisted confirmed builds reject OTA downgrades.
The manual recovery endpoint requires the same signed manifest. Update-response
size and nesting are bounded; URLs are derived from the compiled API origin and
signed digest. Browser Setup and publisher traffic remain HTTPS.

The API changes are committed as `2c57cac` and the server owner confirmed deployment.
The release public key is embedded in both programs; its private key is held in
the firmware repository's encrypted Actions secret. Existing devices require a
one-time USB upgrade of the fixed recovery loader. Job version 1 cannot bypass
the new checks. Build 31 is reserved for local testing, not public publication.

Host regression tests and API tests pass. The production MbedTLS verifier was
exercised with Node signatures for all five chip names, including wrong key,
wrong chip, changed metadata, wrong URL, missing signature and rollback cases.
Physical qualification and final matrix sizes are recorded below when complete.

Local qualification build 32 packages:

| Chip | Application bytes | Recovery bytes |
| --- | ---: | ---: |
| ESP32 | 2,101,984 | 1,029,584 |
| ESP32-S3 | 2,235,360 | 1,014,736 |
| ESP32-C3 | 2,282,656 | 1,085,040 |
| ESP32-C5 | 3,012,784 | 1,143,696 |
| ESP32-C6 | 2,983,152 | 1,091,088 |

The physical GLEDOPTO passed the signed-HTTP recovery harness from local test
build 31 to production application build 32. It rejected altered update-check
metadata, unsigned/tampered/manual rollback manifests and deeply nested JSON.
A truncated transfer stayed in recovery; a corrupt transfer failed its signed
hash check; a reset during a third transfer resumed the persisted job and booted
only the fourth, valid download. Identity, settings and saved scripts survived.
The full install/retry sequence took 68.2 seconds. The first test correctly
refused the SDK's generic loader descriptor; recovery now declares its own name
and build, with a packaging regression test for that exact failure.

Main/recovery package checks and compiler stack budgets pass on all five chips.
Cloud stack frames are 1,504 bytes (Xtensa) / 1,520 bytes (RISC-V), below the
3,072-byte cap. Other board families remain compile-qualified only. Matter and
original-ESP32 BLE eligibility are unchanged; visual output is not user-confirmed.

Build 32 was signed and published on channel 0 by workflow
[40138387220](https://github.com/luxio-lighting/luxio-firmware/actions/runs/40138387220)
from commit `eb48eb8`. All five API manifests, application hashes and recovery
hashes/descriptors were independently fetched and verified after publication.
Linux-built release images are 16 bytes smaller than the local qualification
images above; the firmware source and configuration are the same.

The GLEDOPTO runs production application/recovery build 32 and its original
60-LED Knight Rider animation, brightness, power and GPIO18 relay configuration
were restored. The production 100-LED memory check encountered an HTTP 302
redirect to HTTPS after the API deployment; this is a proxy configuration block,
not a TLS allocation failure. The server owner corrected HTTP routing, and the
final production verification passed: HTTP metadata and binary downloads return
200 for all five chips; every manifest signature and application hash matches.
Both announcement POST routes reach API validation directly without redirects.

The GLEDOPTO then passed 30 production HTTP update checks with Knight Rider
running at 100 LEDs, 227 concurrent HTTP polls and 227 WebSocket replies. There
were no update or animation errors. The lowest sampled internal free heap was
49,708 bytes (48.5 KiB), with a 32,756-byte largest block. The build-30 HTTPS test
previously sampled 6,876 bytes free and a 4,340-byte largest block under the same
100-LED animation/polling workload. These are sampled observations, not a
continuous minimum-heap guarantee. The test restored the original 60 LEDs and
verified that Knight Rider remains active without update/animation errors.

## Original ESP32 radio budget after signed HTTP (2026-10-25)

Isolated diagnostic copies of build 32 (`ef214d7`) were tested on the same
GL-C-016WL-D, ESP32-D0WD-V3, 4 MB flash, no PSRAM. No production feature gates
were changed and no diagnostic was published. Only the application partition
was flashed; recovery, device identity, Wi-Fi and strip/relay configuration
were retained. Logs, diagnostic build script and measurement JSON are locally
under `.sdk/radio32/`.

| Diagnostic | Result | Lowest sampled free internal RAM | Smallest sampled largest block |
| --- | --- | ---: | ---: |
| Published configuration: Matter inactive, BLE excluded | Previous 100-LED workload passed | 49,708 B | 32,756 B |
| Matter active, BLE excluded | 30 update checks with Knight Rider at 100 LEDs, HTTP/WebSocket polling passed; pairing payload available; two additional restarts passed | 21,528 B | 11,252 B |
| BLE active, Matter linked but inactive | Booted, but loading Knight Rider failed with `Not enough memory for this request`; later GET_STATE in color mode reported the values at right | 1,364 B | 276 B |
| BLE and Matter both active | Rebooted during Matter startup; endpoint initialization exhausted the heap, followed by a semaphore assertion | 88–104 B boot-log low-water marks | No useful block remained |
| BLE active, Matter compiled out | 30 update checks with Knight Rider at 100 LEDs and HTTP/WebSocket polling passed; encrypted BLE control remains unqualified | 34,720 B | 18,420 B |
| BLE first, fully stopped/released before Wi-Fi and Matter | Matter initialized, but loading Knight Rider failed with `not enough memory` | Not qualified under the animation workload | Not qualified |

Successful stress runs used `tools/test-memory-device.ts`: 226 HTTP polls and
226 WebSocket replies with Matter; 195 of each with Matter compiled out and
BLE enabled. The latter does not establish successful BLE application traffic.
The stress harness restored 60 LEDs after each successful run. Sampled heap
figures are not continuous minimum-heap guarantees; the combined failure row
instead explicitly reports the allocator low-water marks logged during startup.

The normal PSRAM-probing profile could not link the BLE diagnostics: instruction
RAM overflowed by 1,728 bytes with Matter linked, or 684 bytes with Matter
removed. Setting the diagnostic board option to `PSRAM=disabled` allowed the BLE
variants to link and run on this known no-PSRAM controller. This is not a solution
for a universal PSRAM-detecting release. No SDK archives or linker regions were
modified. Matter-only retained the production PSRAM-probing profile.

With BLE included, removing Matter reduced linked `.data` + `.bss` from 121,376
to 61,116 bytes: a 60,260-byte (58.8 KiB) static saving. A runtime Matter switch
does not recover that memory. The staged diagnostic used a 60-second BLE-only
window, then deleted its API task, deinitialized NimBLE/controller resources,
released controller RAM, and started Wi-Fi followed by Matter. It recovered
enough RAM to start Matter, but retained about 8–9 KiB less than Matter-only.
After the failed animation load it had 39,316 bytes free and a 36,852-byte largest
block; the transient Lua/command allocation peak still did not fit.

Two nearby controllers advertised the same Luxio service. A nonmatching device
was rejected after a read-only fingerprint check; no controls were changed on
it. Subsequent targeted BLE writes failed with ATT `Insufficient Encryption`.
The Mac was locked and could not display an actionable pairing prompt. Unlocking
and successful encrypted BLE requests/reconnection remain required; a memory
stress pass with BLE enabled must not be described as a BLE functional pass.
Matter commissioning, secure sessions and controller interoperability were not
tested. These results support further Matter-only qualification, not enabling
both stacks in the public no-PSRAM build.

After testing, the production build-32 application was restored and its flash
hash verified. HTTP checks confirmed the original identity, 60-LED/GPIO16/GPIO18
configuration, power, brightness and exact Knight Rider source. A production
update check completed without update or animation errors. The public Matter
and BLE gates remain unchanged.

### BLE encryption size comparison (2026-10-25)

Four isolated build-32 variants were compiled with the current Arduino 3.3.3 /
ESP-IDF 5.5.1 SDK and installed NimBLE-Arduino 2.5.1. Every variant includes
Matter, Wi-Fi, the LED driver and one BLE connection, using `PSRAM=disabled` to
fit the diagnostic into instruction RAM. Each used its own copy of NimBLE;
the shared installed library and production sources were not modified. These
images were not flashed or published.

| Change versus encrypted BLE baseline | Linked static DRAM | DRAM saved | Application image bytes saved | IRAM saved |
| --- | ---: | ---: | ---: | ---: |
| Baseline, encrypted writes and bonding | 121,376 B | — | — | — |
| Allow unencrypted writes and disable runtime security/bonding requests | 121,376 B | 0 B | 16 B | 0 B |
| Above, plus compile out legacy and secure-connections pairing | 120,832 B | 544 B | 22,448 B | 0 B |
| Above, plus disable persistent bond storage and set maximum bonds to zero | 119,880 B | 1,496 B | 29,296 B | 0 B |

DRAM is measured from ELF `.dram0.data` + `.dram0.bss`, not the Arduino banner's
nominal remaining-RAM figure. `.iram0.text` remained 127,223 bytes in all four
builds. Image deltas include section alignment and per-build path strings, so
flash savings are approximately 21.9 / 28.6 KiB. Commands, library copies, ELF
files, symbol differences and `sizes.json` are in `.sdk/ble-security32/`.

The compile-out variants set `MYNEWT_VAL_BLE_SM_SC=0` and
`MYNEWT_VAL_BLE_SM_LEGACY=0` and removed the corresponding unconditional
`CONFIG_BT_NIMBLE_SM_*` definitions from their private `nimconfig.h`. Some
configuration mappings test macro presence, so defining a `CONFIG_*` symbol
as zero alone would not disable it. The final variant also removed
`CONFIG_BT_NIMBLE_NVS_PERSIST` and set `CONFIG_BT_NIMBLE_MAX_BONDS=0`.
ELF inspection confirmed removal of the pairing procedure pool, secure pairing
keys and peer/local bond arrays, rather than merely changing GATT permissions.

The measured pairing pool is 400 bytes, its public/private keys total 96 bytes,
and local/peer bond arrays use 264 bytes each. This configuration uses TinyCrypt,
not a TLS session, for pairing. The 4,096-byte NimBLE task stack, 6,144-byte Luxio
BLE API task, radio controller and ordinary packet buffers remain. Static
savings do not measure transient pairing/connection peaks or prove that an
unencrypted diagnostic works at runtime. Removing about 1.5 KiB does not resolve
the earlier combined-stack exhaustion or the staged animation-allocation gap.

Source references: [NimBLE 2.5.1 configuration](https://github.com/h2zero/NimBLE-Arduino/blob/2.5.1/src/nimconfig.h),
[security-manager source](https://raw.githubusercontent.com/h2zero/NimBLE-Arduino/2.5.1/src/nimble/nimble/host/src/ble_sm.c),
and [Espressif security configuration](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/kconfig-reference.html#config-bt-nimble-security-enable).
