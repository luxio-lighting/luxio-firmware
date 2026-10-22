> Historical monorepo documentation. Use the current repository READMEs for paths and commands.

# Luxio next

The v2 implementation lives entirely in this directory. This is a working development implementation, **not a production-ready release**. Read [the validation status and blockers](docs/status.md) before flashing or publishing.

## Run locally

Requires Node.js 22+ (tested with 24).

```sh
npm ci
npm run check
npm test
npm run build
npm run studio     # localhost:5173, or the next free port printed by Vite
npm run setup      # use a second terminal
npm run device     # optional development device at localhost:8080
```

The development device executes real Lua with the same protocol and command arbitration. It does not emulate GPIO, Matter, Bluetooth, Wi-Fi provisioning, or OTA. Its state and identity live in ignored `.device/`. Set `DEVICE_PORT`, `DEVICE_STORAGE`, `DEVICE_LAN_IP`, and optionally `LUXIO_API_URL` as needed. Browser simulation uses Lua 5.3 semantics through Fengari; device firmware uses Lua 5.4.8.

The Studio start/build commands generate `web/vendor/fengari.mjs` with Node-only branches removed. Use these npm commands rather than invoking Vite directly on a fresh checkout.

## Components

| Directory | Purpose |
| --- | --- |
| `firmware/Luxio` | Arduino ESP32 firmware, nanopb, Lua, LED output, device transports, Matter integration |
| `packages/protocol` | Canonical Protobuf schema, TypeScript codec, validation and framing |
| `packages/device` | Development device for client and Lua testing |
| `api.luxio.lighting` | Fastify/MongoDB discovery and update API |
| `setup.luxio.lighting` | Static esptool.js/Web Serial setup |
| `studio.luxio.lighting` | Static Lua editor, simulator, device upload |
| `cli` | `@luxio-lighting/cli` package |
| `apps/ios`, `apps/android` | Native SwiftUI and Kotlin/Compose apps |

## Cloud API

Copy `.env.example` to `.env`, replace the publication secret, and run `docker compose up --build`. The API binds to loopback port 3000; put your HTTPS reverse proxy in front. MongoDB has no public port. Configure `TRUSTED_PROXIES` with exact proxy IPs/CIDRs; arbitrary forwarded headers are not trusted by default. Alternatively run MongoDB separately and `node --env-file=.env --import tsx api.luxio.lighting/src/server.ts`.

`GET /devices` returns devices reporting from the client's WAN address within one hour. Devices obtain a one-use challenge and sign announcements using ECDSA P-256. Freshness changes on every valid report, including unchanged LAN addresses. See [protocol documentation](docs/protocol.md).

## CLI

```sh
npm run build --workspace @luxio-lighting/cli
node cli/dist/index.js discover
node cli/dist/index.js --device 192.168.1.231 state
node cli/dist/index.js --device 192.168.1.231 brightness 16
node cli/dist/index.js --device 192.168.1.231 set-color ff0000
node cli/dist/index.js --device /dev/cu.usbmodem1101 state
node cli/dist/index.js --device 'My light' channels --warm-white 128 --cool-white 64
node cli/dist/index.js --device 'My light' upload animation.lua --name Aurora
node cli/dist/index.js --device 'My light' run SCRIPT_UUID
```

`wifi FILE` reads `{ "ssid": "...", "password": "..." }` from a private local JSON file. Do not put credentials in command-line arguments or source control. The package is prepared for npm but has not been published; `npx @luxio-lighting/cli` is a release target, not a claim about the currently published registry package.

Set a color or an evenly spaced, stationary gradient (start to end of the strip):

```sh
luxio set-color ff0000
luxio set-gradient Ff0000,00ff00,0000ff
luxio --device 192.168.1.231 set-gradient ff0000,00ff00,0000ff
```

Hex colors are case-insensitive; a leading `#` is optional (quote it in your shell). Gradients accept 2–8 colors and require firmware build 5+. They reuse one saved CLI gradient and leave brightness unchanged. `set-color` stops any gradient or animation and enters Color mode; `set-gradient` enters Gradient mode. Both use firmware fades. The existing `set "#FF0000"` command remains an alias.

Select a light with `--device`, set `LUXIO_DEVICE`, or omit both to automatically use the only discovered light. If several lights are discovered, the CLI asks you to select one.

## Firmware

Install the official Arduino CLI, then run `tools/bootstrap-firmware.sh`. It installs Arduino-ESP32 3.3.3 inside `.sdk/`; vendored Lua/nanopb sources are already included. `ARDUINO_CLI` may point to a specific binary. The local test machine reuses an existing read-only Arduino package installation through a symlink; bootstrap deliberately refuses to install through that symlink.

```sh
tools/build-firmware.sh                  # full Matter build; currently fails size gate
LUXIO_MATTER=0 tools/build-firmware.sh   # diagnostic build only
```

Default board: LOLIN S3 Mini, USB CDC enabled, 4 MB flash, 2 MB PSRAM. `LUXIO_FQBN` changes the Arduino board profile and `LUXIO_BUILD` sets the integer build. The included partition table is for 4 MB flash, with two 1,900,544-byte application slots. The build script checks this actual limit. A complete 2.26 MB Matter image currently exceeds it. Do not replace A/B with a single application partition to disguise this blocker. Diagnostic builds must not be registered as product releases.

For the tested S3 Mini, first-install offsets are bootloader `0x0`, partitions `0x8000`, boot_app0 `0xe000`, application `0x10000`. Other chips may use different bootloader offsets. Back up a board before replacing existing firmware. Subsequent application-only USB writes preserve NVS identity/Wi-Fi and script storage. Do not write the application to a guessed active slot after OTA.

Firmware uses zero-based `luxio_set_pixel_color(index, red, green, blue, white, warm_white, cool_white)`, `luxio_get_pixel_count()` and `luxio_millis()`. Channels are 0–255. Unsupported channels must be zero. `setup()` is optional; `loop()` is required. Brightness is applied globally after the animation. Saved scripts have stable UUIDs; limit 32 scripts, 16 KiB source per script, subject to filesystem capacity. The Lua allocator is capped at 128 KiB; each loop has instruction and elapsed-time limits.

To regenerate protocol code after changing the schema:

```sh
node tools/sync-protocol.mjs
python3 -m venv .sdk/venv
.sdk/venv/bin/pip install nanopb==0.4.9.1 grpcio-tools bleak
.sdk/venv/bin/python -m nanopb.generator.nanopb_generator -I packages/protocol -D firmware/Luxio/src packages/protocol/luxio.proto
```

Physical test commands (they briefly change the connected light at low brightness):

```sh
DEVICE_ADDRESS=192.168.1.231 node --import tsx tools/test-device.ts
mkdir -p .sdk/python
.sdk/venv/bin/python -m grpc_tools.protoc -I packages/protocol --python_out=.sdk/python packages/protocol/luxio.proto
.sdk/venv/bin/python tools/test-bluetooth.py --device-id EXPECTED_DEVICE_FINGERPRINT
```

`tools/hardware-cloud.ts` and `tools/test-ota.ts` support isolated physical announcement/OTA tests. The harness requires local TLS key/certificate files in `.sdk/`, serves on port 3443 and uses its own temporary MongoDB. It models a shared WAN address and must never be deployed. Compile a diagnostic sender with `LUXIO_CLOUD_URL=https://YOUR_TEST_HOST:3443`, supply `.sdk/ota-update.bin` and `.sdk/ota-manifest.json`, then run `DEVICE_ADDRESS=... TARGET_BUILD=... node --import tsx tools/test-ota.ts`. Restore the normal cloud URL in the target firmware. A successful build 2→3 test is recorded in [hardware-test.md](docs/hardware-test.md).

## Native apps

For iOS, install XcodeGen, run `xcodegen generate --spec apps/ios/project.yml`, then open `apps/ios/Luxio.xcodeproj`. Select your development team for physical-device signing. Simulator compile: `xcodebuild -project apps/ios/Luxio.xcodeproj -scheme Luxio -sdk iphonesimulator -derivedDataPath apps/ios/DerivedData CODE_SIGNING_ALLOWED=NO build`. The app requires iOS 17+, requests local-network access, and discovers `_luxio._tcp` plus cloud results. A manual-address connection is available.

For Android, open `apps/android` in Android Studio with SDK 36 and Java 21+. Create the local debug key before `./gradlew assembleDebug`:

```sh
mkdir -p .sdk
keytool -genkeypair -keystore .sdk/android-debug.keystore -storepass android -keypass android -alias androiddebugkey -dname 'CN=Android Debug,O=Android,C=US' -keyalg RSA -validity 10000
```

Set `sdk.dir` in ignored `apps/android/local.properties` if the SDK is not otherwise configured. The debug APK is `apps/android/app/build/outputs/apk/debug/app-debug.apk`. Store signing and distribution are not configured. Native Bluetooth setup is deferred to v1.1 as requested.

## Publishing

Workflows under `.github/workflows` assume **this `next` directory becomes the repository root**. GitHub will not run workflows nested inside `next/.github` in a parent repository. Existing v1 folders have not been changed.

- `check.yml`: TypeScript tests/build, native compile checks.
- `web.yml`: builds both static sites and can deploy to separate GitHub Pages repositories using configured repository variables/token. Each site includes its own CNAME. No site has been published by this task.
- `publish-cli.yml`: npm publication on CLI tags, using the repository's npm secret.
- `firmware.yml`: full firmware build and actual A/B size check; release remains blocked on documented qualification gaps.
- `register-update.yml`: verifies hosted firmware hashes before registering a manifest with `UPDATE_PUBLISH_TOKEN`.

Do not enable public firmware registration before the release blockers are resolved. Public endpoints, domain ownership, GitHub repository destinations and release credentials must be configured by the project owner.
