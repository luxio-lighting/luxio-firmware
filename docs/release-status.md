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


## Standard 4 MB matrix (2026-10-25)

All five supported chips now use complete 4 MB installations. C5/C6 use pinned
source SDK profiles with Thread disabled, Wi-Fi Matter enabled and encrypted
Luxio NimBLE retained. There is one recovery target per chip and no legacy
8 MB layout or migration path, as requested. Setup/API snapshots and packaging
checks enforce the new partition capacities.

Local build 33 package sizes in bytes:

| Chip | Application | Recovery | Application capacity | Recovery capacity |
| --- | ---: | ---: | ---: | ---: |
| ESP32 | 2,102,304 | 1,029,584 | 2,621,440 | 1,179,648 |
| ESP32-S3 | 2,235,632 | 1,014,736 | 2,621,440 | 1,179,648 |
| ESP32-C3 | 2,282,976 | 1,085,056 | 2,490,368 | 1,310,720 |
| ESP32-C5 | 2,487,984 | 1,143,712 | 2,621,440 | 1,179,648 |
| ESP32-C6 | 2,423,040 | 1,091,104 | 2,621,440 | 1,179,648 |

The connected ESP32-C6FH4 revision 0.1 was identified as 4 MB embedded flash,
backed up and installed through USB. Boot, USB control, Wi-Fi scanning and
settings/identity retention across restarts passed. After configuring Wi-Fi,
Matter initialized and returned commissioning data. A 1,024-LED Lua animation
passed 30 signed-HTTP update checks with at least 125,180 bytes free internal
RAM and a 112,628-byte largest free block. The original settings were restored.
A separate network stress test passed 675 WebSocket replies, 61 broadcasts and
60 automatic PONGs. These are sampled heap values, not worst-case guarantees.
No physical 1,024-LED strip, full C6 Matter commissioning, C6 encrypted BLE
control session or C5 hardware was qualified in this run. The GLEDOPTO was
not flashed or reconfigured.

Host regressions, signature/tampering tests, API storage/publication tests,
TypeScript checks and 65 browser installation cases passed. C6 cases include
both 4 MB and 8 MB detected flash installing the same 4 MB layout.


Build 33 was signed and published on channel 0 by [workflow
40187920737](https://github.com/luxio-lighting/luxio-firmware/actions/runs/40187920737).
The first CI attempt exhausted hosted-runner disk space while unpacking IDF;
the successful rerun removes unused preinstalled toolchains on disposable
C5/C6 runners before building. All five public manifests and downloads passed
signature, SHA-256, chip identity, 4 MB image-header and offset checks. HTTPS
installation downloads and direct HTTP update/download routes were verified.
Setup commit `416cbf6` deployed successfully, and its live JavaScript contains
the five 4 MB profiles. API commit `ae79b82` was deployed by the user before
publication, which accepted the new C5/C6 partition layouts.

The C6 then completed a real signed HTTP recovery update from a USB-only
build-32 test baseline to published build 33. The local test baseline reset
its rollback floor to 32 for this test; that change was never committed or
published. The production build restores the floor to 33. The test installed
the exact published bootloader, partition table and recovery image first,
then requested and installed the signed application through recovery.
Identity, configuration, brightness, power and Wi-Fi were retained, and
Matter initialized again. A USB readback SHA-256 matched the public signed
application exactly. The first USB readback had a transport framing error;
the OTA test was repeated successfully with readback retries. The board is
left on the published build, with its original settings. This qualifies the
C6 update path, not C5 hardware or physical 1,024-LED signal quality.


## Build 34 development — included in public build 35

All five targets compile and package inside their existing 4 MB layouts. The C6 was tested with 1024 SK6812 pixels configured, USB control, Wi-Fi/Matter off/on, and cold-boot output. With Matter fully initialized, free internal RAM was 143,752 bytes; Wi-Fi alone 177,668; Wi-Fi off (Matter waiting) 257,288. The user confirmed on 2026-10-25 that their physical SK6812 strip works.

Normal boots show the Luxio gradient and retain hardware/radio settings. Only an explicit OTA saves lighting state, consumed once on the following application boot. CRC/readback, interrupted-write atomicity, 16 KiB animation source, parameter restoration and one-shot consumption pass host tests using the production code. The C6’s entire 256 KiB LittleFS partition was unchanged after repeated brightness/power changes. A complete signed build-34 OTA has not been performed or published.

iOS radio-confirmation and SK6812 simulator UI tests pass; the signed app is installed on the paired iPhone (launch blocked while locked). Android builds and unit tests pass. Setup passes its existing 73 browser tests and an additional SK6812 selection/protocol test. Existing uncommitted mobile work was preserved.


## Public build 35 — cooperative animations (2026-10-25)

The old CPU deadline stopped valid Christmas, Fireplace and Cyberpunk frames at
256 LEDs. Source initialization, setup and frames now run on a retained Lua
coroutine in approximately 4 ms CPU slices. Each call has separate hard limits
of 500 ms CPU and one million instructions; the VM retains its 128 KiB allocation
cap. A sticky failure terminates scripts even when they use `pcall`. Only completed
frames are copied to output. Unchanged frames are not retransmitted between Lua
slices, while power/brightness fades and Identify continue to render.

The staging frame uses six bytes per LED (6 KiB at 1024), allocated only during
animation playback. Syntax errors remain immediate request errors; runtime errors
arrive through `GET_STATE.animation_error` and are now displayed by the iOS app.

Validation:
- Real production Lua code passes sanitizer tests for multi-slice source/setup/
  frames, complete-frame publication, output scheduling, replacement while
  suspended, independent CPU/instruction limits, memory exhaustion and clock wrap.
- Firmware host regressions and TypeScript checks pass. The HTTP fixture test
  required an unsandboxed localhost bind; the remaining storage suite then passed.
- All five ordinary targets build and fit the existing 4 MB layouts. The original
  ESP32 DMA/Matter/BLE profile also builds and passes stack checks (2,304,480 bytes).
- On the GLEDOPTO without PSRAM, all ten iOS preset sources stay active with
  **read-back-verified 256 and 1024 LED configurations**. Long custom setup/frames,
  power changes, runtime errors, infinite source/setup/loop code including `pcall`,
  and allocation-limit failures pass. Saved libraries and original settings are
  restored. HTTP poll timings are in the local verified hardware log.
- USB script upload and polling pass during a long custom animation at 1024 LEDs.
  Matter initialization and pairing-data availability are verified. This test did
  not repeat full Matter commissioning or encrypted BLE traffic qualification.
- iOS refresh regression verifies that runtime errors survive polling and clear
  when the firmware clears them. The signed app builds, installs and launches on
  the user's iPhone.

Only the GLEDOPTO application partition was flashed, after a full backup. Its
final image is the DMA/Matter/BLE profile, SHA-256
`222e9128cfbbc9d0b2385159c02c2fe7aa8407fbdfb3e97bb5ff7f31d6196acd`.
The ordinary ESP32 build was tested first but was replaced because its no-PSRAM
Matter gate does not match this controller's existing experimental profile.
An early preset harness used the wrong configuration opcode; its reported
1024-LED results are invalid. The corrected test asserts the applied count on
every poll; only that rerun supports the 1024-LED preset result above.

The physical strip has fewer LEDs than the maximum configured test count; this
validates allocation, execution and transmission at 1024, not the appearance of a
physical 1024-LED strip. The combined-radio peak-memory caveats in
[the low-RAM profile record](lowram-esp32.md) still apply.


### Publication verification

[Workflow 40200261033](https://github.com/luxio-lighting/luxio-firmware/actions/runs/40200261033)
built and published source commit `5eb10c107f4d4cc92f966f4910d6873803f0fd58`
as build 35 on public channel 0. The original ESP32 package now uses the
source-built DMA/Matter/BLE profile while retaining the Arduino recovery
bootloader. All five complete installation layouts fit 4 MB.

Independent production checks verified all five manifests and their ECDSA
signatures, all 25 part sizes and SHA-256 hashes, bootloader/recovery/application
chip IDs, and application descriptors. HTTP manifest queries and application
downloads completed without redirects and matched the HTTPS manifests.

| Chip | Application bytes | Application SHA-256 |
| --- | ---: | --- |
| esp32 | 2,304,480 | `58041eda3b4f6cc2c781f541a08963d84e0f8bf0befb9cd8830c56c923844213` |
| esp32s3 | 2,241,456 | `52348d1e6792ca15f6164630d68292c966c5a1da02e1a97b99f2c58340d99783` |
| esp32c3 | 2,289,072 | `3e09d53518e7a7f7c93494d7d204a44ff51bd18c84396ea2295e73ceda4c8d8b` |
| esp32c5 | 2,494,400 | `3227d0b0a48e3412c667bbfcaad6dd28cde4801ace0a4a5643d20a4dce4b1b9d` |
| esp32c6 | 2,429,488 | `069a921d3f0fee8abdf1a08d8e1249c5aeeb3a04cf8f2c95c6966f014a1dcb1e` |

Setup and Studio deployments succeeded and their live entry-point asset names
match the built distributions. iOS and Android changes are committed and pushed;
iOS 0.1.51 (52) is installed on the paired iPhone. CLI 3.0.0 source is committed
and pushed, but npm publication was skipped at the user's request. The API
required no code changes for this release.


## Public build 36 — float Lua and bulk rendering (2026-10-26)

[Workflow 40412159459](https://github.com/luxio-lighting/luxio-firmware/actions/runs/40412159459)
built and published source commit `9b465377501ee4a7ea87beed8018bbff823ec579`
on public channel 0 for all five chips. Lua uses float arithmetic, and the new
fill/HSV/wave APIs reduce per-pixel Lua work. Wave rendering yields cooperatively
in bounded chunks to keep control requests responsive. See the
[paired ESP32 benchmarks](lua-wave-benchmark.md) for performance, memory and
hardware-test limits.

The current iOS, Android and Studio presets require the new animation API
directly. There are no older-firmware fallback branches or new migration paths.
Historical implementations are retained only as benchmark/test fixtures.

Independent production verification checked all five public ECDSA signatures,
all 25 part sizes and SHA-256 hashes, bootloader/recovery/application chip IDs,
build descriptors, 4 MB image headers and partition boundaries. HTTP manifest
queries and application downloads completed without redirects and matched HTTPS.

| Chip | Application bytes | Recovery bytes | Application SHA-256 |
| --- | ---: | ---: | --- |
| esp32 | 2,298,800 | 1,029,568 | `0d9d3e342430f4ba3d3715ec4b02064d9d4c20adb4a1274e25d6a3342506a8c8` |
| esp32s3 | 2,236,256 | 1,014,720 | `3645b2c602da6cae10e61dab0e5a0f29ef2037ae48de659f35f3333e65614a16` |
| esp32c3 | 2,282,064 | 1,085,040 | `8ebc887160aea40a4f9638d8194770d86ab1d6ca8071fe8cbd2124c205d96694` |
| esp32c5 | 2,489,472 | 1,143,696 | `151f98b114202239ccafdc0b2b3b996a2dc2ba1b469f46fac3dbf0af89860eea` |
| esp32c6 | 2,422,000 | 1,091,072 | `4974e1a3b74b4b9f3cfeabd3d183141c1f6fe339755dec0c84479effaa534dbf` |

Firmware host tests, iOS host/native tests and three targeted iOS simulator UI
tests passed. Android debug compilation and native sanitizer tests passed. CLI
tests, checks and build passed. Studio checks, build, bulk fixtures and 26 browser
tests passed. The live Studio documentation and JavaScript bundle match its
successful deployment of `acbbafb`.

Related pushed commits: iOS `5c3a248` (including the pending action-error toasts),
Android `ccd00b2`, CLI `d22990f`, and Studio `acbbafb`. No API or Setup code changes
were required. npm publication remains skipped. This publication verification
did not flash devices or repeat the physical testing documented in the benchmark.


## Build 37 factory reset (2026-10-27)

Operation 33 adds an explicitly confirmed factory reset to the shared HTTP, USB
and BLE dispatcher. State advertises support and the response acknowledges
acceptance before the scheduled restart. On startup, the reset erases LittleFS,
crash dumps and NVS before loading settings or starting radios. Application and
recovery images, OTA selection and eFuses are preserved.

All five main/recovery packages pass chip identity, partition, image-size and
compiler stack checks. Firmware host/native tests and TypeScript checking pass.
Production-code extraction tests cover missing confirmation, concurrent restart
or update checks, marker-write failure, duplicate requests, acknowledgement before
erasure, partition boundaries and retry after failed storage erasure. Web tests
exercise successful and failed reset requests over mocked Wi-Fi, USB and BLE.
No physical device was reset; hardware reset qualification remains outstanding.

| Chip | Main bytes | Recovery bytes |
| --- | --- | --- |
| esp32 | 2,300,496 | 1,029,568 |
| esp32s3 | 2,237,840 | 1,014,720 |
| esp32c3 | 2,283,920 | 1,085,040 |
| esp32c5 | 2,491,328 | 1,143,696 |
| esp32c6 | 2,423,856 | 1,091,072 |

Build 37 was published on channel 0 by [workflow 40523831559](https://github.com/luxio-lighting/luxio-firmware/actions/runs/40523831559), from commit `e2ac385`. The table above records the published packages. Independent checks downloaded all 25 parts and verified sizes, SHA-256 hashes, embedded chip IDs, flash addresses and signed manifests. The HTTP update/download path also passed without redirects. No physical device was flashed or reset during this release.

## Build 38 configured Bluetooth names (2026-10-27)

Bluetooth GAP and scan-response names now use the saved device name. Successful
settings renames queue a host-task update, without taking the BLE receive mutex,
restarting advertising or disconnecting the client. The primary advertisement
retains the Luxio service UUID used by web, iOS and Android discovery. The
29-byte scan-response name is shortened at a UTF-8 boundary when necessary;
settings and the control API retain the full name. Both native NimBLE and the
diagnostic NimBLE-Arduino implementation use this behavior.

The build also includes the post-OTA capability restoration fix: loading an older
lighting snapshot no longer clears `factory_reset_supported`.

Host tests exercise the production native initialization, advertising and queued
rename code, including changes while dispatch is locked, coalesced changes,
re-advertising, host resynchronization, empty-name fallback and ASCII/UTF-8 length
boundaries. The full firmware regression suite and TypeScript checks pass. These
checks do not constitute physical Bluetooth qualification.

All five local production builds pass compilation, packaging, image identity,
partition capacity and stack-budget checks. The separate diagnostic original
ESP32/NimBLE-Arduino sources compile, but that older non-Matter profile still
fails linking with a 684-byte IRAM overflow. Rebuilding the previous source with
the same settings produces the identical overflow; it is not a build-38 regression
and that profile is not published. The production original ESP32 uses the
source-built native NimBLE/DMA/Matter profile and passes.

Build 38 was published on channel 0 by [workflow 40533741490](https://github.com/luxio-lighting/luxio-firmware/actions/runs/40533741490), from commit `e3c23fa623e1b37afd91a8d6b1c015677da22f95`. Independent checks downloaded all 25 parts, verified their sizes, SHA-256 hashes, embedded chip IDs and flash addresses, and verified all five signed manifests. The HTTP update/download paths also passed without redirects. No physical device was flashed or changed during this release.

| Target | Published application bytes | SHA-256 |
| --- | ---: | --- |
| esp32 | 2,301,184 | `4c0b1f33c6fda27005cc9c3c1def18f92d4a45aceb1feafbfb057d39e8f2bd93` |
| esp32s3 | 2,238,432 | `d0f35b8e2caafe943f5f23fec01003fc8f468bd10d1a0522e981ab8e3a5fda3b` |
| esp32c3 | 2,284,672 | `54db9bab0cf7508bc7368abf27e57f6bbba8852b041d39393c94cf193904d305` |
| esp32c5 | 2,492,080 | `9b4f992712f731578e43382deff6b46de2ed0e649c5dd8ed65b1dd232a1ccf0f` |
| esp32c6 | 2,424,608 | `513de77c42225aed0716965ac350238cc955f8bb4cb70acaabbce88337f48114` |


## ESP32-S31 (2026-10-29)

Build 39 now compiles an S31 application (3,057,280 bytes) and recovery loader
(1,149,040 bytes) using pinned Arduino 4 / ESP-IDF 6.1. The package requires 8 MB
flash, with loader at 0x10000 (0x180000 bytes), application at 0x190000
(0x620000 bytes), filesystem at 0x7b0000 and core dump at 0x7f0000. The bootloader
starts at 0x2000; image chip ID is 32. All five flash parts pass package checks.
The control stack budget is 2,000 / 4,096 bytes; cloud task frame is 1,520 bytes.
The existing ESP32-S3 build also passes after shared crypto changes.

Firmware regression, packaging, update-layout and both MbedTLS 3/4 signature
checks pass. The actual S31 identity-generation code is host-tested for stable
DER persistence and signing. Web flasher tests verify security-info detection
rather than the ambiguous P4 magic register, S31 SPI flash capacity detection,
8 MB enforcement and USB reconnect. API tests include S31-specific manifests.

This is a development-SDK preview, not hardware qualification. No S31 board
was connected, no device was flashed, and no new binaries were published. The
website lists S31 with an 8 MB minimum. The web app and standalone
Setup can flash it once a matching signed public release has been published.

## Build 39 publication (2026-10-29)

Published all six targets on channel 0 from commit `16b258f9791bed86a8a16fdc6a96fcb3877bf1ab`.
[GitHub Actions run 40902544170](https://github.com/luxio-lighting/luxio-firmware/actions/runs/40902544170) passed every build, crypto check and publication step.

Independent downloads verified all six ECDSA signatures and all thirty flash
parts: SHA-256, size, image chip ID, application build number and flash bounds.
HTTP device endpoints returned the same artifacts without redirects. S31 requires
8 MB; the other five targets require 4 MB. This verifies publication, not physical
S31 hardware operation. No device was flashed during publication.

| Chip | Application bytes | SHA-256 |
| --- | ---: | --- |
| esp32 | 2,301,648 | `d357adf898d127ae7d5c962dced6b49612144d78e82b46d4b6590c9c2b7fc586` |
| esp32s3 | 2,239,168 | `96b472922fc6bf117cbf790701d7d4d9fd82185f1b0685d8de55a5f88259b413` |
| esp32s31 | 3,057,264 | `671a40159f1a52b6004e8e654a5660ee8cd467474cd0613e35dd0ba8904fe3cc` |
| esp32c3 | 2,285,504 | `3968db12ace97633aa9e03bdfa62157db1ed3364e6965d4fa8486b6ca4e9876f` |
| esp32c5 | 2,492,896 | `86aee0ac6c7b66b554f2e4fd49aeb0f76c36f81f533c0d4fa86a2947e05af62a` |
| esp32c6 | 2,425,424 | `3a57bd78fe618ddfc56a0fb0c7bc010ef29725942e5f10c0acf1c2a57358cdfd` |

The API accepts the S31 recovery target. The current web app deployment includes
S31 USB flashing and device-reported LED types; the marketing website lists
ESP32-S31 (8 MB+) without a preview label. WS2811, WS2813 and WS2815 support is
included in this release. Native client and CLI source changes are pushed; this
publication did not submit new mobile store builds.
