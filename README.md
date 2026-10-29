# Luxio firmware

The firmware matrix includes Matter and a fixed Wi-Fi recovery loader. Firmware checks and installs are explicitly user-initiated. A reboot during an accepted download resumes that same request; it does not select a new release.

```sh
# For a fresh checkout, install Arduino CLI, then:
tools/bootstrap-firmware.sh
LUXIO_BUILD=38 tools/build-firmware.sh all
# Or build just one target:
LUXIO_BUILD=38 tools/build-firmware.sh esp32
```

The Arduino SDK is local to this repository in `.sdk/`. The bootstrap creates it from official packages. No shared SDK link or sibling checkout is used. Standard host tools (Arduino CLI, Python 3 and a C++ compiler) must be installed.

`build/<chip>/release/manifest.local.json` lists exact USB flash parts and SHA-256 checksums. `luxio.bin` is the main application used for network updates. Both image capacities and the target’s exact recovery/application partition layout are enforced. Recovery is the only supported update architecture. The package script deliberately omits Arduino's misleading single-sketch merged image.

The existing ESP32/S3 layout remains:

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | 20 KiB |
| OTA boot selection | `0xe000` | 8 KiB |
| Factory recovery loader | `0x10000` | 1.125 MiB |
| Main application | `0x130000` | 2.5 MiB |
| Library filesystem | `0x3b0000` | 256 KiB |
| Core dumps | `0x3f0000` | 64 KiB |

Initial installation requires USB flashing of the bootloader, partition table, erased OTA selection, recovery image and main image at the manifest offsets. Back up flash first. Do not erase all flash: NVS contains Wi-Fi credentials, identity and Matter state, and the filesystem contains saved colors, gradients, and animations. Normal network updates write only the main application; the loader is never replaced over the network.

The recovery web page is at the device's LAN IP while updating, with `/status`, `POST /retry` and `POST /update`. A replacement update requires `url` (HTTPS), `sha256`, `size`, `build`, and `target: "<chip>-recovery-v1"`. It validates the target, image size, chip, Luxio app descriptor/build, SHA-256 and ESP image checksum before boot selection. Failed transfers stay in recovery for manual retry. The bootloader's pending-image rollback returns to recovery if the new application resets before confirming startup; there is no previous full application to restore.

Matter uses Wi-Fi/IP; Luxio exclusively owns Bluetooth for discovery, setup and control on all six targets. From build 35, original ESP32 releases use the tested source-built DMA/Matter/BLE profile. Hardware qualification and remaining peak-memory limits are recorded in [release status](docs/release-status.md) and [the low-RAM profile](docs/lowram-esp32.md).


## Matter memory policy

All public target binaries include Matter. The source-built ESP32/C5/C6 profiles allow Matter without PSRAM; all profiles wait for Wi-Fi and respect the persistent Enable Matter setting. The precompiled S3/C3 profiles retain the build-28 memory gate described below.

On those precompiled profiles, startup enables Matter only when usable
PSRAM is detected with at least 2 MiB total and 1 MiB free after the LED frames are
allocated. This is a conservative Luxio policy, not a universal Matter hardware
minimum. It is based on detected memory, not the CPU architecture or chip family.
Eligibility is decided at boot; changing the LED count does not start or stop
Matter until the next boot. Eligible devices initialize Matter once Wi-Fi is
connected and has an IP address. Initialization runs in the cloud worker before
HTTPS requests, avoiding simultaneous startup/handshake allocations. Reconnection
does not recreate Matter; a temporary Wi-Fi loss does not shut down an existing
Matter instance. Offline USB/BLE controls remain available on their supported targets. Boards without usable PSRAM retain Luxio controls and updates.
Skipping initialization saves runtime allocations but cannot reclaim Matter's
linked static RAM. Physical Matter qualification with this policy remains pending.

`State.matter_disabled` reports that decision. Updated mobile clients hide Matter
settings when true; its absent/false value preserves compatibility with older
firmware. `GET_PAIRING_CODE` returns an unavailable error when disabled.
`NetworkStatus.psram_size` and `psram_free` report detected total/free bytes.

Original ESP32 modules with detected PSRAM reserve GPIO16/17 for that RAM. On those
modules only, a saved/default data pin of 16 or 17 is moved to GPIO4 and a relay on
16/17 is disabled. Check the board wiring before enabling output. The tested
GLEDOPTO reports no usable PSRAM and retains data GPIO16 and relay GPIO18.

## Target matrix

`targets.json` is the source of truth for local builds and the CI matrix. Run `python3 tools/sync-targets.py` to refresh the committed snapshots in sibling web app, Setup and API repositories, or add `--check` to check drift. Each repository builds independently from its committed files.

| Target | CPU | Minimum flash | Bootloader | Main application | Recovery capacity | App capacity | Initial LED GPIO |
| --- | --- | --- | --- | --- | --- | --- | --- |
| esp32 | Xtensa | 4 MB | 0x1000 | 0x130000 | 1.125 MiB | 2.5 MiB | 16 |
| esp32s3 | Xtensa | 4 MB | 0x0 | 0x130000 | 1.125 MiB | 2.5 MiB | 16 |
| esp32s31 | RISC-V | 8 MB | 0x2000 | 0x190000 | 1.5 MiB | 6.125 MiB | 4 |
| esp32c3 | RISC-V | 4 MB | 0x0 | 0x150000 | 1.25 MiB | 2.375 MiB | 4 |
| esp32c5 | RISC-V | 4 MB | 0x2000 | 0x130000 | 1.125 MiB | 2.5 MiB | 4 |
| esp32c6 | RISC-V | 4 MB | 0x0 | 0x130000 | 1.125 MiB | 2.5 MiB | 4 |

The existing ESP32/S3/C3/C5/C6 profiles fit the complete installation—including recovery, settings, filesystem and core dump—within 4 MB. ESP32-S31 requires 8 MB. C5/C6 build ESP-IDF 5.5, Arduino 3.3.3 and Matter 1.4.1 from pinned sources in Docker, omitting Thread while retaining Matter over Wi-Fi and encrypted Luxio BLE. Build them with `python3 tools/build-firmware.py esp32c5` or `esp32c6` after SDK bootstrap; Docker must be available. C3 uses a larger recovery slot and a slightly smaller application slot. Each chip has one `<chip>-recovery-v1` target. No legacy 8 MB layout or migration path is maintained. The build stages independent source/partition copies under `build/<id>/sources/`; it never changes another target's partition table. The checked-in sketch partition CSVs remain the ESP32/S3 reference layout.

The 256 KiB filesystem and 64 KiB core-dump partition occupy the final 320 KiB of each profile's minimum flash size. Larger physical flash chips can use these images; smaller ones are rejected by Setup. ESP32, S3 and C5 probe PSRAM at startup. S3 uses QSPI PSRAM (including the Lolin S3 Mini); OPI PSRAM is not enabled by this profile. Basic lighting and updates do not require PSRAM. All three LED frames are allocated for the configured strip length and grow when it increases; configuration is rejected if allocation fails. A smaller count releases the excess capacity on reboot. The exact LED capacity, Matter heap headroom, USB reset behavior and board wiring still require hardware qualification. Default pins are starting values, not automatic board identification.

ESP32 uses UART0 through an external USB bridge. S3/C3/C5/C6 serve both native USB Serial/JTAG and UART0; requests and replies stay on the same transport.

C2/C61 are not build targets: the pinned SDK does not ship their static libraries (C2 also lacks the current RMT output path). S2 lacks BLE; H2/P4 lack integrated Wi-Fi. Setup and the API reject these targets explicitly.

To prepare publishable manifests after building, set `LUXIO_FIRMWARE_BASE_URL` to an HTTPS directory containing this release, then run `python3 tools/package-firmware.py <chip>`. Upload each target's files under `<base>/<chip>/`, including `manifest.json`, and register it using `node tools/publish-update.mjs build/<chip>/release/manifest.json`. Registration verifies hosted hashes, embedded chip IDs, offsets and sizes before contacting the API. The local manifest with `file` entries is not an API payload. Use a distinct immutable base directory for every build.

Builds default to channel 100 in their manifests. Public channel 0 publication remains a separate release decision and hardware qualification is not implied by compilation. No command in the matrix build flashes a device, uploads files or registers an OTA update.

The **Firmware** GitHub workflow now has explicit manual inputs for `build`, `publish` and `channel`. On `main`, selecting `publish=true` uploads all five successful build packages through the API into MongoDB, verifies the public downloads, and registers the releases. Set `UPDATE_PUBLISH_TOKEN` in repository Actions secrets to the API's configured token. Select channel `0` to make a build available to Setup; `100` is the default development channel. Use a new increasing build number for changed binaries; identical publication retries are safe. Pull requests never publish, and automatic tagged releases retain their qualification gate. Manual publication does not imply hardware qualification.

For the same operation locally, run `LUXIO_CHANNEL=0 node tools/publish-update.mjs build/<chip>/release/manifest.local.json --upload` with `UPDATE_PUBLISH_TOKEN` and `OTA_SIGNING_KEY` set. The upload endpoint uses immutable SHA-256 URLs at `https://api.luxio.lighting/firmware/<sha256>`; no external binary host is needed. `LUXIO_API_URL` overrides the HTTPS API origin. All local files are validated before the first upload, and all downloads are validated before metadata registration.

## Verification and protocol generation

```sh
npm ci
npm run check
npm test
python3 -m venv --copies .sdk/venv
.sdk/venv/bin/python -m pip install --cache-dir .sdk/pip-cache -r tools/requirements.txt
.sdk/venv/bin/python -m nanopb.generator.nanopb_generator -I Luxio -D Luxio/src Luxio/luxio.proto
```

Hardware tests live in `tools/` and are opt-in. For recovery testing, use `TARGET_FIRMWARE=build/esp32s3/release/luxio.bin` along with `DEVICE_ADDRESS` and `DEVICE_SERIAL`; the installed test build must target the harness's local HTTPS URL. Private backups and test evidence remain in `.sdk/` and must not be published.

## Development simulator and project records

`npm test` also covers Protobuf framing, device modes and Lua execution. `npm run device` starts the local HTTP/WebSocket device simulator; it does not emulate Matter or physical hardware. `npm run sync:protocol` regenerates the local TypeScript schema from `Luxio/luxio.proto`.

Project design notes and historical test records are retained in [docs/project](docs/project/repositories.md).

Build 13 stores colors, native gradients, and compressed Lua animations in separate directories within the existing 256 KiB filesystem. Client-supplied live content stays in RAM. There is no fixed item-count cap. See [native resources](docs/native-resources.md) for the API and [animation storage](docs/script-storage.md) for compatibility, pagination, capacity measurements and tests.

## LED output memory

Original ESP32 releases from build 35 use DMA output; see [the profile record](docs/lowram-esp32.md) for its buffer accounting. The following build-29 RMT accounting applies to the other targets.

LED output uses ESP-IDF's RMT byte encoder directly instead of Arduino's expanded
symbol buffer. The ISR reads a small internal-RAM payload of at most five bytes
per LED, with the same channel order, bit timing and 600 microsecond low reset
interval. Transmissions complete before the payload is reused or resized.
The peripheral's fixed symbol memory is separate from this byte buffer.

Together with the three six-byte color/fade frames, buffers use 23 bytes per LED:
2,300 bytes for 100 LEDs, versus 17,804 bytes in build 28. This saves 15,504 bytes
of payload/frame allocations at that count, before driver overhead. Allocations
grow with the configured count; shrinking releases capacity on reboot. The RMT
payload stays in internal RAM for ISR access; color/fade frames prefer PSRAM.
Arduino remains for APIs where replacing the wrapper has no demonstrated RAM
benefit. The precompiled TLS library configuration is unchanged.

## Animation scheduling (build 35)

Lua source initialization, `setup()` and `loop()` run as resumable coroutines.
An instruction hook yields after approximately 4 ms of task CPU time, letting
USB, network commands and LED output progress between slices. A frame that needs
more time finishes at a lower frame rate instead of being stopped at the old
20 ms deadline. Native Lua C functions cannot yield midway through a call.
Time spent preempted by other tasks or waiting between slices is not charged.

Each call still has independent hard limits of 500 ms of CPU time, 1,000,000
instructions and a 128 KiB VM allocation cap. Exceeding a limit stops the script,
including loops wrapped in `pcall`. Errors remain in `GET_STATE.animation_error`;
the iOS Animation page displays them. Syntax errors reject the request immediately;
source initialization, setup and frame errors arrive asynchronously in state polls.

Only completed frames reach the LEDs. The working frame costs another six bytes
per configured LED (6 KiB at 1024 LEDs), allocated only while an animation runs
and freed when it stops. Scripts need no changes to use cooperative execution.
`loop()` still returns once per completed frame; read `luxio_millis()` once at the
start of a frame when every pixel should use the same animation timestamp.

Run `python3 tools/test-animation-scheduler.py` for real-Lua sanitizer tests of
slicing, atomic frame publication, replacement, limits, cleanup and counter wrap.
Firmware Lua uses 32-bit floats and float math functions, including in C89
compatibility builds; integer widths are unchanged. See the
[precision benchmark](docs/lua-precision-benchmark.md) for device measurements,
precision tradeoffs and reproduction instructions.
The [bulk animation API and benchmark](docs/lua-bulk-benchmark.md) cover native
fill, range fill and HSV rendering, including compatibility fallbacks.
For an opt-in device regression, run `DEVICE_ADDRESS=<ip> ANIMATION_PRESETS=<preset-json> node --import tsx tools/test-animation-runtime-device.ts`. The JSON can be produced
by the iOS preset exporter; omitting it tests custom frames and errors only.
The test temporarily changes LED count to 256 and 1024, then restores the active
animation, settings, brightness and power. It leaves saved libraries unchanged.
The [wave renderer and remaining presets](docs/lua-wave-benchmark.md) add native
wave mixing, modulation, and optimized twinkle/scanner rendering.

## Power and brightness (build 11)

Power is `on: true|false`; brightness is an independent integer from 1 to 256. Changing brightness while off keeps the output off. Setting a color or starting an animation/gradient turns on at the stored brightness. Off preserves the active mode, and power changes fade. Matter levels are mapped from its 1–254 range. Old saved states migrate once: zero brightness becomes off at level 16; other levels retain their relative intensity.

Run `DEVICE_ADDRESS=<ip> DEVICE_SERIAL=<port> node --import tsx tools/test-power-device.ts` for the opt-in physical regression, including reboot persistence. It removes its test animation and restores the original output.

## Wi-Fi discovery (build 12)

Send `SCAN_WIFI` (operation 16) over USB or the HTTP/WebSocket Protobuf API. It returns immediately with `wifiScanning: true`; poll `GET_STATE` until that becomes false. `wifiNetworks` contains `{ ssid, rssi, passwordRequired }` entries, with RSSI in dBm (negative values; higher is stronger). `wifiScanError` reports a failed scan separately from command errors. Repeating `SCAN_WIFI` while scanning joins the current scan; sending it after completion starts a fresh scan.

Results contain up to 32 distinct SSID/security pairs, strongest first. Duplicate access points use the strongest signal. Hidden SSIDs are omitted; Setup offers manual entry. Open and OWE networks do not require a password. Results are cached in RAM until the next scan. Scanning is asynchronous, keeps the current connection and LED rendering active, and delays a pending Wi-Fi connection change until the scan finishes.

Run `DEVICE_ADDRESS=<ip> DEVICE_SERIAL=<port> node --import tsx tools/test-wifi-device.ts` to verify USB/HTTP scanning, polling, cached results, signal ordering and unchanged controls/library on a real board.

## Active animation snapshots (build 20)

GET_SCRIPT (6) with an empty `script_id` returns the currently running source,
name/ID, preview and current parameter values. This includes unsaved SET_SCRIPT
animations; a saved script subsequently edited on disk does not change the running
snapshot. With an ID, GET_SCRIPT still reads that saved file. No active animation
returns `No animation is running`. Power-off preserves the active animation.

State `animation_revision` (field 16) changes on each successful start and clears
when the animation stops. Parameter and brightness changes retain it. Clients can
fetch a snapshot when the revision changes and run a local preview without
polling or streaming source/pixels per frame. Source is retained in PSRAM on
PSRAM-equipped boards, released with the VM, and never persisted by this feature.

Animation control declarations support `Slider`, `Number` (numeric input), and `Checkbox` in build 21. `Boolean` remains a legacy alias. The protocol carries the numeric control style in parameter field 7.

Preset catalogs belong to the mobile apps and Studio. Firmware accepts client-supplied Lua and parameter declarations; it does not bundle or select built-in animations. Files under `tools/fixtures/` are test and benchmark inputs only.

## Lua color capabilities (build 22)

`luxio_get_color_type()` returns `"RGB"`, `"RGBW"`, or `"RGBCCT"` from the configured strip type (WS2812, WS2814/SK6812, or WS2805 respectively). It is available at the top level, in `setup()`, and in `loop()`. It reports configured channels; it does not detect the attached hardware. Older firmware can use `luxio_get_color_type and luxio_get_color_type() or "RGB"` to fall back to RGB output.

## Luxio Bluetooth with Matter over Wi-Fi

All six build targets expose the Luxio service
`e97b1000-3c27-4a20-9df4-7d025bc7e451`, including before Wi-Fi is configured.
RX `1001` accepts writes with response and requires encryption; TX `1002` sends
notifications. Subscribe first, then send an LXP1-framed Protobuf request in
chunks no larger than MTU−3 (maximum 512). Wait for the complete response before
sending another request. The displayless light uses Just Works bonding; the OS
handles the pairing prompt. This encrypts the link but is not an ownership or
passkey authentication scheme.

The iOS app can discover/control the light, scan networks (SCAN_WIFI), submit
credentials (SET_WIFI), and fetch the Matter QR/manual code (GET_PAIRING_CODE)
over this connection. Matter's QR payload advertises on-network commissioning;
connect the light to Wi-Fi before adding it to a Matter controller. No Matter
BLE service is advertised. Existing fabrics and Wi-Fi credentials are preserved.

The pinned Arduino 3.3.3 SDK provides NimBLE on S3/C3/C5/C6. Original ESP32
releases from build 35 use the source-built network-only Matter profile with
Luxio BLE enabled. Earlier stock builds disabled BLE because of RAM exhaustion.
Diagnostic Arduino builds with `LUXIO_MATTER=0` use
`bluetooth_esp32.cpp` and NimBLE-Arduino 2.5.1, installed by
`tools/bootstrap-firmware.sh`, with one peripheral connection. The explicit
Arduino BT link anchor prevents controller RAM being released before NimBLE
starts. Light state uses the heap to leave static DRAM for the radio.
For the precompiled S3/C3 profiles, `tools/build-firmware.py` creates a target-local copy of
the precompiled Matter archive and weakens five lifecycle/advertising methods.
`matter_network_only.cpp` provides their replacements: it initializes Matter's
logical BleLayer ABI but never initializes, advertises, handles radio events or
reclaims the Luxio Bluetooth host. Unsupported Matter BLE advertising still
allows IP commissioning in the SDK. The installed SDK archive is not modified.
Method symbols are checked at build time; SDK upgrades require reviewing this
adapter. C5/C6 compile the same adapter against the source SDK with CHIPoBLE disabled. Use the repository build script, not a direct Arduino IDE compile.

Input buffers are allocated for each frame and released after processing,
disconnect, or a three-second partial-frame timeout. Output buffers exist only
during a reply: 32 KiB with PSRAM, 8 KiB without it. Output allocation failures return
the same bounded Protobuf error as USB; there are no permanent 64 KiB BLE buffers.
The buffers use PSRAM when available. Disconnects invalidate partially received
requests and outstanding responses so a reused connection handle cannot receive
another session's reply. All BLE commands use the existing serialized dispatcher.

Physical regression: generate `.sdk/python/luxio_pb2.py` from `Luxio/luxio.proto`,
then run `.sdk/venv/bin/python tools/test-bluetooth.py --device-id <fingerprint>`.
Use a dedicated test light. The test checks identity before changing output and
restores the previous mode, brightness and power. It also checks Wi-Fi scanning,
pairing payload access and reconnects. Build/test success alone does not qualify
BLE pairing, offline startup, Wi-Fi provisioning or Matter interoperability on
physical boards.

## Optional strip power relay

Build 27 adds `Config.relay` (`enabled`, `gpio`, `inverted`). It defaults to
disabled, so existing controllers keep their current behavior. Enable Relay in
mobile settings, choose its GPIO, and enable inversion only for an active-low
relay. GL-C-016WL-D has different hardware revisions: the older manual specifies
GPIO12 without inversion, while the current manufacturer manual specifies GPIO18
with inversion. Our physically tested unit instead requires GPIO18 with inversion
off; the user confirmed normal LED operation with that configuration. Data remains
GPIO16. Check the controller revision and verify that app On supplies strip power;
the model name alone does not identify the relay wiring or polarity. The relay follows power and
Identify, stays on through an off fade, and allows 20 ms for power to settle
before LED data. A disabled relay pin is released as an input after being driven
inactive. Reserved pins and a shared relay/data GPIO are rejected.

An omitted relay message in SET_CONFIG preserves the current relay configuration,
so older clients can still edit other settings. To disable an existing relay,
send an explicit relay message with enabled=false.

GLEDOPTO references: [current manufacturer product page and manual](https://www.gledopto.com/h-pd-91.html),
[older GPIO12 manual](https://www.gledopto.eu/mediafiles/anleitungen/7002-gl-c-016wl-d-eng.pdf).

## Response and update memory (build 32)

Replies allocate their encoded size on demand across HTTP, WebSocket, USB and
BLE, retaining the 32 KiB protocol limit. Device announcements, update checks
and OTA downloads use HTTP. They do not allocate TLS handshake or record buffers.
The build uses the SDK libraries directly again; the optional build-30 TLS
experiment is no longer part of the firmware build.

Release authentication uses ECDSA P-256/SHA-256. Both main and recovery embed
`ota-public-key.json`'s public key and reject unsigned/tampered manifests. Recovery
hashes the entire image while streaming and selects it for boot only after the
signed hash matches. The image must also match chip, application name and build.
Confirmed build numbers are persisted to reject OTA downgrades. HTTP metadata
is visible and an attacker can suppress updates; signatures prevent substituting
untrusted firmware, not denial of service.

Existing devices need a one-time USB upgrade of both main and recovery; older
loaders do not verify signatures. No eFuse settings are changed. USB provisioning
remains a trusted operation. See [recovery-updates.md](docs/recovery-updates.md).

The publisher signs using the `OTA_SIGNING_KEY` GitHub Actions secret and refuses
a key that does not match the embedded trust anchor. Keep an offline backup of
the signing key. It has no certificate expiration. Replacing this key requires
upgrading the fixed recovery loader over USB; do not silently rotate it in CI.
Browser Setup, uploads and publication retain HTTPS URLs and bearer credentials
are never sent by the publisher over HTTP.

An opt-in device test runs an animation at 100 LEDs while repeatedly checking for
updates and polling HTTP/WebSocket controls:

```sh
DEVICE_ADDRESS=192.0.2.10 EXPECTED_BUILD=32 node --import tsx tools/test-memory-device.ts
```

Build 35 includes the build-34 additions: `Enable Wi-Fi` and `Enable Matter` settings (both default enabled), SK6812 RGBW support, and update-only lighting-state restoration. A radio setting change restarts the device to reclaim all associated SDK tasks/buffers. Wi-Fi credentials and Matter fabrics are retained; Matter additionally waits for a connected Wi-Fi network. Wi-Fi-off boots skip the HTTP server, cloud worker and scan cache.

Hardware settings, radio preferences and Wi-Fi credentials remain persistent. Lighting controls do not write flash: normal boots (including a radio-setting restart) show the Luxio cyan-to-green gradient at brightness 16/256 with power on. Only an explicit firmware update saves the current color/gradient, brightness, power, animation source and parameters, including unsaved previews. The next boot restores that snapshot once, then clears it; animation playback starts again from its setup function. Snapshots are streamed, CRC-checked and atomically replaced in LittleFS. Re-saving identical settings does not rewrite NVS.

## Release build 35

The original ESP32 release now uses the source-built DMA/Matter/BLE profile from
`idf/esp32`, through `tools/build-lowram.py`. The release builder preserves the
existing Arduino recovery bootloader and verifies matching partition tables.
S3/C3 and C5/C6 keep their existing builders. All packages retain 4 MB layouts.
See [profile validation](docs/lowram-esp32.md) for measured peak-memory limits.


## Release build 36

Lua uses 32-bit floats and float math functions. Native fill, range, HSV and wave
rendering write the existing pixel buffer; the wave renderer yields within long
frames so controls remain responsive. The current app and Studio presets require
this API directly, without older-firmware fallbacks. All eight remaining presets
are optimized, including dedicated white output for Candle. See the
[paired ESP32 benchmarks](docs/lua-wave-benchmark.md).

Historical Lua implementations in benchmark fixtures are reference workloads,
not compatibility code in the shipped presets. The signed recovery update flow
and 4 MB layouts remain the release format for all five chips.


## Release build 37

Factory Reset is available over HTTP, Bluetooth and USB using operation 33.
Clients must send `factory_reset_confirmed=true`; supported devices advertise
`factory_reset_supported` in State and acknowledge with `factory_reset_accepted`.
The device schedules a restart after acknowledging the request. Before loading
user state or starting radios, it erases the user filesystem, crash dumps and NVS
(settings, Wi-Fi credentials, device identity, Bluetooth bonds and Matter fabrics).
Firmware, OTA selection and eFuses remain intact. The persistent reset marker is
erased last with NVS so a failed filesystem erase is retried on the next boot.
Reconnecting after reset creates a new device identity and requires setup again.

The web app requires confirmation and removes cached connections only after the
device acknowledges. Older firmware keeps the action disabled. Native mobile
apps have no Factory Reset control.

## Release build 38

Bluetooth discovery now advertises the saved device name at startup and refreshes
it after a successful settings rename, without disconnecting the active client.
The Luxio service UUID remains in the advertising packet, so names without a
Luxio prefix remain discoverable. Names longer than the 29-byte scan-response
limit are shortened at a UTF-8 character boundary; the full name remains in
settings and the control API. This build also restores firmware capability flags
after loading pre-update lighting state.

## Supported LED strips

The firmware reports `State.supported_strip_types` (field 19) with its state over HTTP, WebSocket, USB and Bluetooth. Settings clients use this list, not a fixed menu. If an older device omits it, clients retain only the configured type until its firmware is updated. Simulators report their own supported list.

| Type | Channels / wire order | Signalling |
| --- | --- | --- |
| WS2805 | RGBCCT / RGB warm cool | Existing five-channel profile |
| WS2814 | RGBW / RGBW | Existing four-channel profile |
| SK6812 | RGBW / GRBW | Existing four-channel profile |
| WS2812 (including WS2812B) | RGB / GRB | Existing RGB profile |
| WS2811 | RGB / RGB | 800 kHz family; older 400 kHz variants are not supported |
| WS2813 | RGB / GRB | Backup-data strip, reset low at least 600 µs |
| WS2815 | RGB / GRB | Backup-data strip, reset low at least 600 µs |

For grouped strips such as common 12 V WS2811 strips, LED Count means addressable controllers/groups, not individual emitters. Follow the strip manufacturer's voltage and backup-data wiring instructions. WS2811/WS2813/WS2815 have encoder and protocol coverage; physical qualification requires those strips.


### ESP32-S31

Run `python3 tools/bootstrap-s31.py`, then `python3 tools/build-firmware.py esp32s31`.
Use Python 3.11 or newer for bootstrap. The isolated `.sdk/s31/` installation pins
Arduino 4 commit `d1ae6ac9498601920b21f7f03b11e56ba3cf30d7` and IDF 6.1 libraries
`idf-release_v6.1-9a97f6c5-v2`; downloads are SHA-256 checked. Stable targets keep
Arduino 3.3.3. CI builds the application and recovery and checks both image IDs,
partition capacities and stack budgets. The target is RISC-V, image chip ID 32;
it must never use an ESP32-S3 or ESP32-P4 image.

S31 requires at least 8 MB flash and defaults to GPIO4. GPIO33/34 are reserved for
USB Serial/JTAG and GPIO58/59 for UART. Chip validity checks reject nonexistent
GPIO29/41. Its flash and in-package PSRAM use dedicated pins. Clients permit
GPIO0–61, while firmware applies the actual chip's pin restrictions.

The application includes Matter over Wi-Fi, Luxio BLE and the same signed manual
recovery update flow. ESP-IDF 6 uses PSA key generation and MbedTLS 4's signing
API; cloud identity remains DER P-256 and signed updates retain their existing
format. The browser uses esptool-js 0.7.0 to identify S31 using GET_SECURITY_INFO,
checks the 8 MB minimum and verifies image chip IDs and SHA-256 before writing.
No public image is available until a successful package is explicitly published.
Hardware qualification is still required; this is development-SDK support.


S31 validation: the application is 3,057,280 bytes and recovery is 1,149,040
bytes in build 39. Both pass chip-ID, partition-size and stack checks. Host tests
against the pinned MbedTLS 4 sources verify Node-generated OTA signatures,
tamper rejection, PSA identity generation, DER save/reload and signing.
Run `tools/test-s31-crypto.py` with CMake, Node, Jinja2 3.1.6 and jsonschema 4.26.0
installed; CI runs this test for S31. Hardware accelerators, radios, boot and
physical flashing still require testing on an S31 board.
