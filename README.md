# Luxio firmware

The ESP32-S3 build includes Matter and a fixed Wi-Fi/HTTPS recovery loader. Firmware checks and installs are explicitly user-initiated. A reboot during an accepted download resumes that same request; it does not select a new release.

```sh
# For a fresh checkout, install Arduino CLI, then:
tools/bootstrap-firmware.sh
LUXIO_BUILD=11 LUXIO_MATTER=1 tools/build-firmware.sh
```

The Arduino SDK is local to this repository in `.sdk/`. The bootstrap creates it from official packages. No shared SDK link or sibling checkout is used. Standard host tools (Arduino CLI, Python 3 and a C++ compiler) must be installed.

`build/release/manifest.local.json` lists exact USB flash parts and SHA-256 checksums. `luxio.bin` is the main application used for network updates. Both image capacities and the exact recovery/application partition layout are enforced. Recovery is the only supported update architecture. The package script deliberately omits Arduino's misleading single-sketch merged image.

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | 20 KiB |
| OTA boot selection | `0xe000` | 8 KiB |
| Factory recovery loader | `0x10000` | 1.125 MiB |
| Matter application | `0x130000` | 2.5 MiB |
| Script filesystem | `0x3b0000` | 256 KiB |
| Core dumps | `0x3f0000` | 64 KiB |

Initial installation requires USB flashing of the bootloader, partition table, erased OTA selection, recovery image and main image at the manifest offsets. Back up flash first. Do not erase all flash: NVS contains Wi-Fi credentials, identity and Matter state, and the filesystem contains scripts. Normal network updates write only the main application; the loader is never replaced over the network.

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

`npm test` also covers Protobuf framing, device modes and Lua presets. `npm run device` starts the local HTTP/WebSocket device simulator; it does not emulate Matter or physical hardware. `npm run sync:protocol` regenerates the local TypeScript schema from `Luxio/luxio.proto`.

Project design notes and historical test records are retained in [docs/project](docs/project/repositories.md).

Saved animations and gradients are compressed on-device with no fixed item-count cap. See [script storage](docs/script-storage.md) for compatibility, pagination, capacity measurements and tests.

## Power and brightness (build 11)

Power is `on: true|false`; brightness is an independent integer from 1 to 256. Changing brightness while off keeps the output off. Setting a color or starting an animation/gradient turns on at the stored brightness. Off preserves the active mode, and power changes fade. Matter levels are mapped from its 1–254 range. Old saved states migrate once: zero brightness becomes off at level 16; other levels retain their relative intensity.

Run `DEVICE_ADDRESS=<ip> DEVICE_SERIAL=<port> node --import tsx tools/test-power-device.ts` for the opt-in physical regression, including reboot persistence. It removes its test script and restores the original output.
