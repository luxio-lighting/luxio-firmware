# Manual updates with a recovery loader

Recovery is the only update architecture: one fixed factory application for Wi-Fi/HTTPS recovery and one Matter application. NVS, the script filesystem and core dumps have separate partitions.

## Update flow

1. The user chooses **Check for updates** in a light's iOS settings, or runs `luxio check-update`.
2. The main firmware queries the cloud for a newer eligible build with target `<chip>-recovery-v1`. It does not download or install it.
3. The user chooses **Install update**, or runs `luxio update`.
4. Firmware saves current controls and a single NVS update-request blob, selects the factory loader, and reboots.
5. The loader joins the saved Wi-Fi network, validates the incoming image's chip, Luxio descriptor/build and size, streams it to the main application slot, verifies SHA-256 and the ESP image, and only then selects it for boot.
6. The new application confirms startup and clears the request. The ESP-IDF bootloader's pending-image rollback returns to the factory loader if startup resets before confirmation. There is no previous complete app image to roll back to.

The light's control API and animations are unavailable during download and installation. WS2805 controllers may hold their last output while the ESP32 restarts. The update path does not write NVS credentials/identity, the script filesystem, the recovery loader, or the partition table.

There are **no automatic update checks or automatic installs of new releases**. A reset during an already accepted request resumes that same request. A failed transfer/checksum waits in recovery for manual retry; a firmware startup failure also waits for user intervention.

## Recovery controls

While in recovery, open `http://DEVICE_IP/`. The page shows download status and supports **Retry update** or replacement firmware details. HTTP endpoints:

- `GET /status`: `status`, `error`, `busy`, `received`, `size`, `build`, `target`. The replacement-update form reads this target instead of assuming S3.
- `POST /retry`: retry the persisted request; returns 409 if busy or there is no valid request.
- `POST /update`: JSON with HTTPS `url`, lowercase 64-character `sha256`, byte `size`, integer `build`, and `target: "<chip>-recovery-v1"`. This explicit action can replace a failed request.

The loader also advertises `_luxio-recovery._tcp` on the LAN. Its Wi-Fi credentials come from the same saved Luxio preferences. If the network credentials are no longer usable, USB recovery is required. The image server must provide an exact Content-Length and a direct HTTPS URL; redirects are deliberately not followed.

The project still permits TLS without certificate verification and unsigned firmware, as specified in the brief. The saved SHA-256 checks transfer integrity; it is not a signature or publisher authentication.

## Flash layout and USB installation

See `targets.json` and the target table in README.md. The factory loader starts at `0x10000` on every target; its capacity, the main application offset/capacity and minimum flash size are chip-specific. `Luxio/partitions.csv` is the ESP32/S3 reference layout. The build generates each target’s partition table in its isolated source directory. Firmware and recovery use the same compiled capacities, and packaging verifies the actual partition binaries. The loader only writes the main slot.

Initial installation uses USB. Save a full flash backup before replacing existing firmware. Flash the exact files and offsets in `build/<chip>/release/manifest.local.json`; do not perform a full erase and do not use a padded merged image. The explicit erased `otadata.bin` selects the factory loader, which validates and starts the pre-flashed main app. If a startup loop prevents software USB reset, hold BOOT, press/release RESET, then release BOOT to enter the ROM loader.

Normal update publication uses `luxio.bin` and `target: "<chip>-recovery-v1"`. Browser first-install manifests include the bootloader, partition table, erased OTA selection, recovery app and main app. The API requires an explicit recovery target matching the chip and never serves releases for any other layout. Missing or mismatched targets are rejected. Setup requests `<chip>-recovery-v1` for every supported chip; a chip without a published recovery package has no installable release. Local package manifests contain filenames; hosting/publication must supply HTTPS URLs. No releases were published during implementation.

## Verification

`python3 tools/test-update-targets.py` runs `tools/test-update-job.cpp` for every chip/layout and checks malformed/oversized/persisted requests with address and undefined-behavior sanitizers. API tests verify recovery-only publication and lookup, rejection of missing or mismatched targets, exclusion of old database records, and channel/build filtering. Packaging checks the actual binary partition table for exactly one factory loader and one main application. `python3 tools/test-package-firmware.py` verifies valid packaging and rejection of incompatible layouts. Cross-repository TypeScript, native iOS and both firmware builds are checked.

`tools/test-recovery-device.ts` is the opt-in hardware test. It expects a test build pointing at the Mac's HTTPS harness and a higher-numbered final firmware. It tests explicit check/install separation, truncated download, corrupt payload, manual retry, reset during writing, rebooted build, identity/settings/script preservation and availability of Matter pairing data. Runtime results are recorded separately in `hardware-test.md`; a successful build alone is not evidence these scenarios passed.

Matter uses the current Arduino SDK's example commissioning and attestation providers. Pairing payloads come from the active provider; unsupported passcode setters are not silently treated as successful. Production factory credentials, controller commissioning, conformance and coexistence with Luxio's standalone BLE service remain unfinished.

The S3 Mini's Matter build needs its PSRAM for reliable HTTPS handshakes. When PSRAM is present, firmware installs the normal `calloc`/`free` allocator for mbedTLS before initializing cryptography, allowing large allocations to use external RAM. The SDK's internal-only allocator failed the physical update check with `SSL - Memory allocation failed`. Boards without PSRAM still require separate memory qualification.
