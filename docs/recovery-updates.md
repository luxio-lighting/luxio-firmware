# Manual updates with a recovery loader

Recovery is the only update architecture: one fixed factory application for Wi-Fi/HTTP recovery and one Matter application. NVS, the script filesystem and core dumps have separate partitions.

## Update flow

1. The user chooses **Check for updates** in a light's iOS settings, or runs `luxio check-update`.
2. The main firmware queries the cloud for a newer eligible build with target `<chip>-recovery-v1`. It does not download or install it.
3. The user chooses **Install update**, or runs `luxio update`.
4. Firmware saves current controls and a single NVS update-request blob, selects the factory loader, and reboots.
5. The loader verifies the persisted ECDSA signature and rollback floor, joins the saved Wi-Fi network, validates the incoming image's chip, Luxio descriptor/build and size, streams it to the main application slot, verifies SHA-256 and the ESP image, and only then selects it for boot.
6. The new application confirms startup, persists its build as the rollback floor, and clears the request. The ESP-IDF bootloader's pending-image rollback returns to the factory loader if startup resets before confirmation. There is no previous complete app image to roll back to.

The light's control API and animations are unavailable during download and installation. WS2805 controllers may hold their last output while the ESP32 restarts. The update path does not write NVS credentials/identity, the script filesystem, the recovery loader, or the partition table.

There are **no automatic update checks or automatic installs of new releases**. A reset during an already accepted request resumes that same request. A failed transfer/checksum waits in recovery for manual retry; a firmware startup failure also waits for user intervention.

## Recovery controls

While in recovery, open `http://DEVICE_IP/`. The page shows download status and supports **Retry update** or a replacement signed release manifest. HTTP endpoints:

- `GET /status`: `status`, `error`, `busy`, `received`, `size`, `build`, `target`.
- `POST /retry`: retry the persisted request; returns 409 if busy or there is no valid request.
- `POST /update`: the signed release JSON, containing `signatureVersion: 1`, `keyId`, base64 DER ECDSA `signature`, `chip`, `target`, `build`, `channel`, `size` and lowercase `sha256`. It can replace a failed request only after signature, chip, layout and rollback validation. Unsigned manual installs are rejected.

Downloads use the compiled HTTP API origin and `/firmware/<signed sha256>`;
response URLs cannot redirect the loader to arbitrary LAN or Internet hosts.
Exact Content-Length is required and redirects are disabled. Update-check JSON
is bounded to 6 KiB before allocation; discovery challenges to 128 bytes.

The canonical signed UTF-8 text is `luxio-ota-v1`, key ID, chip, target, build,
channel, size and lowercase SHA-256, each separated and terminated by LF. Integer
fields are decimal. Main checks channel eligibility and signature before offering
an update. Recovery repeats signature verification before downloading or erasing.
It streams into the single main slot, but makes it bootable only after the entire
signed hash and ESP image validate. There is no space to stage a second full main
image. Interrupted/corrupt transfers stay in recovery and support retry.

Both programs trust the committed release public key, not a key received over
HTTP. Signing is done in the publisher with `OTA_SIGNING_KEY`. Existing loaders
must be upgraded over USB once. The job blob is version 2; version 1 jobs cannot
be reinterpreted as signed requests. Main refuses installation through a loader
older than build 31. Recovery requires a build above the confirmed NVS floor (and
at least its own build), including manual submissions. This is software OTA
rollback protection, not protection against physical flash replacement.

The loader advertises `_luxio-recovery._tcp` and uses saved Wi-Fi preferences.
If credentials are unusable, USB recovery is required. Public-key authentication
has no certificate expiration, but replacing the trust key requires a USB loader
upgrade. HTTP reveals metadata and can be blocked by a network attacker.

## Flash layout and USB installation

See `targets.json` and the target table in README.md. The factory loader starts at `0x10000` on every target; its capacity, the main application offset/capacity and minimum flash size are chip-specific. `Luxio/partitions.csv` is the ESP32/S3 reference layout. The build generates each target’s partition table in its isolated source directory. Firmware and recovery use the same compiled capacities, and packaging verifies the actual partition binaries. The loader only writes the main slot.

Initial installation uses USB. Save a full flash backup before replacing existing firmware. Flash the exact files and offsets in `build/<chip>/release/manifest.local.json`; do not perform a full erase and do not use a padded merged image. The explicit erased `otadata.bin` selects the factory loader, which validates and starts the pre-flashed main app. If a startup loop prevents software USB reset, hold BOOT, press/release RESET, then release BOOT to enter the ROM loader.

Normal update publication uses `luxio.bin` and `target: "<chip>-recovery-v1"`. Browser first-install manifests include the bootloader, partition table, erased OTA selection, recovery app and main app. The API requires an explicit recovery target matching the chip and never serves releases for any other layout. Missing or mismatched targets are rejected. Setup requests `<chip>-recovery-v1` for every supported chip; a chip without a published recovery package has no installable release. Local package manifests contain filenames; hosting/publication must supply HTTPS URLs. Publishing requires both the release signing key and the API publication token.

## Verification

`python3 tools/test-update-targets.py` runs `tools/test-update-job.cpp` for every chip/layout and checks malformed/oversized/persisted requests with address and undefined-behavior sanitizers. API tests verify recovery-only publication and lookup, rejection of missing or mismatched targets, exclusion of old database records, and channel/build filtering. Packaging checks the actual binary partition table for exactly one factory loader and one main application. `python3 tools/test-package-firmware.py` verifies valid packaging and rejection of incompatible layouts. Cross-repository TypeScript, native iOS and both firmware builds are checked.

`tools/test-recovery-device.ts` is the opt-in hardware test. It expects a test build pointing at the Mac's HTTP harness (`TEST_ORIGIN`, matching `LUXIO_CLOUD_URL`) and `OTA_SIGNING_KEY` and a higher-numbered final firmware. It tests explicit check/install separation, truncated download, corrupt payload, manual retry, reset during writing, rebooted build, identity/settings/script preservation and availability of Matter pairing data on supported hardware. Runtime results are recorded separately in `hardware-test.md`; a successful build alone is not evidence these scenarios passed.

Matter uses the current Arduino SDK's example commissioning and attestation providers. Pairing payloads come from the active provider; unsupported passcode setters are not silently treated as successful. Production factory credentials, controller commissioning, conformance and coexistence with Luxio's standalone BLE service remain unfinished.

The optional build-30 TLS record-buffer experiment is no longer linked. Device
cloud traffic has no TLS handshake; crypto allocations use PSRAM when available.
Matter remains gated on usable PSRAM and Wi-Fi. HTTP does not by itself enable
Matter or BLE on boards that previously lacked sufficient memory.
