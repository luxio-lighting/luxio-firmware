# Manual updates with a recovery loader

The selected architecture is option B: one fixed factory application for Wi-Fi/HTTPS recovery, plus one larger Matter application. Firmware no longer depends on two full A/B application slots. The existing NVS, script filesystem and core-dump addresses are retained.

## Update flow

1. The user chooses **Check for updates** in a light's iOS settings, or runs `luxio check-update`.
2. The main firmware queries the cloud for a newer eligible build with target `esp32s3-recovery-v1`. It does not download or install it.
3. The user chooses **Install update**, or runs `luxio update`.
4. Firmware saves current controls and a single NVS update-request blob, selects the factory loader, and reboots.
5. The loader joins the saved Wi-Fi network, validates the incoming image's chip, Luxio descriptor/build and size, streams it to the main application slot, verifies SHA-256 and the ESP image, and only then selects it for boot.
6. The new application confirms startup and clears the request. The ESP-IDF bootloader's pending-image rollback returns to the factory loader if startup resets before confirmation. There is no previous complete app image to roll back to.

The light's control API and animations are unavailable during download and installation. WS2805 controllers may hold their last output while the ESP32 restarts. The update path does not write NVS credentials/identity, the script filesystem, the recovery loader, or the partition table.

There are **no automatic update checks or automatic installs of new releases**. A reset during an already accepted request resumes that same request. A failed transfer/checksum waits in recovery for manual retry; a firmware startup failure also waits for user intervention.

## Recovery controls

While in recovery, open `http://DEVICE_IP/`. The page shows download status and supports **Retry update** or replacement firmware details. HTTP endpoints:

- `GET /status`: `status`, `error`, `busy`, `received`, `size`, `build`.
- `POST /retry`: retry the persisted request; returns 409 if busy or there is no valid request.
- `POST /update`: JSON with HTTPS `url`, lowercase 64-character `sha256`, byte `size`, integer `build`, and `target: "esp32s3-recovery-v1"`. This explicit action can replace a failed request.

The loader also advertises `_luxio-recovery._tcp` on the LAN. Its Wi-Fi credentials come from the same saved Luxio preferences. If the network credentials are no longer usable, USB recovery is required. The image server must provide an exact Content-Length and a direct HTTPS URL; redirects are deliberately not followed.

The project still permits TLS without certificate verification and unsigned firmware, as specified in the brief. The saved SHA-256 checks transfer integrity; it is not a signature or publisher authentication.

## Flash layout and USB migration

See the firmware repository's `Luxio/partitions.csv`. Factory loader: `0x10000`, size `0x120000`; main app: `0x130000`, size `0x280000`. The loader only writes the latter. Compilation and packaging enforce both capacities.

The initial migration from the old A/B layout is a USB operation. Save a full 4 MiB backup first. Flash the exact files and offsets in `build/release/manifest.local.json`; do not perform a full erase and do not use a padded merged image. The explicit erased `otadata.bin` selects the factory loader, which validates and starts the pre-flashed main app. If a startup loop prevents software USB reset, hold BOOT, press/release RESET, then release BOOT to enter the ROM loader.

Normal update publication uses `luxio.bin` and `target: "esp32s3-recovery-v1"`. Browser first-install manifests include the bootloader, partition table, erased OTA selection, recovery app and main app. The API keeps legacy targets separate so pre-migration A/B devices cannot receive these images. Local package manifests contain filenames; hosting/publication must supply HTTPS URLs. No releases were published during implementation.

## Verification

`luxio-firmware/tools/test-update-job.cpp` checks malformed/oversized/persisted requests with address and undefined-behavior sanitizers. API tests verify target isolation, legacy compatibility and channel/build filtering. Cross-repository TypeScript, native iOS and both firmware builds are checked.

`luxio-firmware/tools/test-recovery-device.ts` is the opt-in hardware test. It expects a test build pointing at the Mac's HTTPS harness and a higher-numbered final firmware. It tests explicit check/install separation, truncated download, corrupt payload, manual retry, reset during writing, rebooted build, identity/settings/script preservation and availability of Matter pairing data. Runtime results are recorded separately in `hardware-test.md`; a successful build alone is not evidence these scenarios passed.

Matter uses the current Arduino SDK's example commissioning and attestation providers. Pairing payloads come from the active provider; unsupported passcode setters are not silently treated as successful. Production factory credentials, controller commissioning, conformance and coexistence with Luxio's standalone BLE service remain unfinished.

The S3 Mini's Matter build needs its PSRAM for reliable HTTPS handshakes. When PSRAM is present, firmware installs the normal `calloc`/`free` allocator for mbedTLS before initializing cryptography, allowing large allocations to use external RAM. The SDK's internal-only allocator failed the physical update check with `SSL - Memory allocation failed`. Boards without PSRAM still require separate memory qualification.
