# Luxio firmware

The ESP32-S3 build includes Matter and a fixed Wi-Fi/HTTPS recovery loader. Firmware checks and installs are explicitly user-initiated. A reboot during an accepted download resumes that same request; it does not select a new release.

```sh
# For a fresh checkout, install Arduino CLI, then:
tools/bootstrap-firmware.sh
LUXIO_BUILD=22 LUXIO_MATTER=1 tools/build-firmware.sh
```

The Arduino SDK is local to this repository in `.sdk/`. The bootstrap creates it from official packages. No shared SDK link or sibling checkout is used. Standard host tools (Arduino CLI, Python 3 and a C++ compiler) must be installed.

`build/release/manifest.local.json` lists exact USB flash parts and SHA-256 checksums. `luxio.bin` is the main application used for network updates. Both image capacities and the exact recovery/application partition layout are enforced. Recovery is the only supported update architecture. The package script deliberately omits Arduino's misleading single-sketch merged image.

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | 20 KiB |
| OTA boot selection | `0xe000` | 8 KiB |
| Factory recovery loader | `0x10000` | 1.125 MiB |
| Matter application | `0x130000` | 2.5 MiB |
| Library filesystem | `0x3b0000` | 256 KiB |
| Core dumps | `0x3f0000` | 64 KiB |

Initial installation requires USB flashing of the bootloader, partition table, erased OTA selection, recovery image and main image at the manifest offsets. Back up flash first. Do not erase all flash: NVS contains Wi-Fi credentials, identity and Matter state, and the filesystem contains saved colors, gradients, and animations. Normal network updates write only the main application; the loader is never replaced over the network.

The recovery web page is at the device's LAN IP while updating, with `/status`, `POST /retry` and `POST /update`. A replacement update requires `url` (HTTPS), `sha256`, `size`, `build`, and `target: "esp32s3-recovery-v1"`. It validates the target, image size, chip, Luxio app descriptor/build, SHA-256 and ESP image checksum before boot selection. Failed transfers stay in recovery for manual retry. The bootloader's pending-image rollback returns to recovery if the new application resets before confirming startup; there is no previous full application to restore.

Matter is real SDK integration, but commissioning/conformance and sharing the Matter BLE host with Luxio's standalone BLE service remain unfinished. See [release status](docs/release-status.md).

## Verification and protocol generation

```sh
npm ci
npm run check
npm test
python3 -m venv --copies .sdk/venv
.sdk/venv/bin/python -m pip install --cache-dir .sdk/pip-cache -r tools/requirements.txt
.sdk/venv/bin/python -m nanopb.generator.nanopb_generator -I Luxio -D Luxio/src Luxio/luxio.proto
```

Hardware tests live in `tools/` and are opt-in. For recovery testing, use `TARGET_FIRMWARE=build/release/luxio.bin` along with `DEVICE_ADDRESS` and `DEVICE_SERIAL`; the installed test build must target the harness's local HTTPS URL. Private backups and test evidence remain in `.sdk/` and must not be published.

## Development simulator and project records

`npm test` also covers Protobuf framing, device modes and Lua execution. `npm run device` starts the local HTTP/WebSocket device simulator; it does not emulate Matter or physical hardware. `npm run sync:protocol` regenerates the local TypeScript schema from `Luxio/luxio.proto`.

Project design notes and historical test records are retained in [docs/project](docs/project/repositories.md).

Build 13 stores colors, native gradients, and compressed Lua animations in separate directories within the existing 256 KiB filesystem. Client-supplied live content stays in RAM. There is no fixed item-count cap. See [native resources](docs/native-resources.md) for the API and [animation storage](docs/script-storage.md) for compatibility, pagination, capacity measurements and tests.

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

Preset catalogs belong to the mobile apps and Studio. Firmware accepts client-supplied Lua and parameter declarations; it does not bundle or select built-in animations. Files under `tools/fixtures/` are storage-test inputs only.

## Lua color capabilities (build 22)

`luxio_get_color_type()` returns `"RGB"`, `"RGBW"`, or `"RGBCCT"` from the configured strip type (WS2812, WS2814, or WS2805 respectively). It is available at the top level, in `setup()`, and in `loop()`. It reports configured channels; it does not detect the attached hardware. Older firmware can use `luxio_get_color_type and luxio_get_color_type() or "RGB"` to fall back to RGB output.
