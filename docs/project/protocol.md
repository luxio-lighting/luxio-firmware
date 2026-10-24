# Luxio protocol

`Luxio/luxio.proto` is the source of truth. Field numbers are stable. Color channels are unsigned integers 0–255. Build 11 separates `State.on` (field 9, boolean) from `brightness` (1–256, never zero). `SET_POWER` (operation 15, `Request.on` field 10) changes power without changing brightness or the active mode. `SET_BRIGHTNESS` preserves power. Successful `SET_COLOR` and `RUN_SCRIPT` turn on without changing brightness; saving scripts does not turn on. Strip types are exact strings `WS2805`, `WS2814`, `WS2812`. Config is a complete replacement, with LED/controller count 1–1024 and update channel 0–100. Names are at most 63 UTF-8 bytes on firmware. Board-reserved GPIOs are rejected for the qualified board profile.

## Device transports

- HTTP: `POST /api`, `Content-Type: application/x-protobuf`, raw Request bytes; raw Response bytes, including an error string on operation failure.
- WebSocket: `/api/ws`, binary raw Protobuf frames. Responses echo the request ID. Unsolicited state events use ID 0. Firmware emits changed state at most every 250 ms.
- UART: 115200 baud; `LXP1` ASCII magic, 4-byte unsigned big-endian payload length, then Protobuf payload. Boot text is discarded until the magic appears. Maximum payload 32768 bytes; partial frames expire after 3 seconds.
- BLE: service `e97b1000-3c27-4a20-9df4-7d025bc7e451`; RX characteristic `e97b1001-3c27-4a20-9df4-7d025bc7e451` (write with response), TX `e97b1002-3c27-4a20-9df4-7d025bc7e451` (notify). Subscribe before writing. Use the UART framing across ATT chunks of at most MTU−3 bytes. One request may be in flight; wait for its complete response before the next. Build 24 requires encrypted writes and supports OS-managed Just Works bonding. Luxio owns Bluetooth while Matter uses IP commissioning. Physical qualification of this combined build remains pending.

| Operation | Number | Request payload |
| --- | --- | --- |
| GET_STATE | 0 | none |
| SET_COLOR | 1 | complete Color; omitted channels become zero; stops animation |
| SET_BRIGHTNESS | 2 | brightness; does not stop animation |
| SET_CONFIG | 3 | complete Config |
| SET_WIFI | 4 | Wifi SSID/password; persists then reconnects |
| LIST_SCRIPTS | 5 | none; returns metadata without source |
| GET_SCRIPT | 6 | script_id |
| CREATE_SCRIPT | 7 | Script name/source; firmware generates UUID |
| UPDATE_SCRIPT | 8 | Script id/name/source; UUID remains unchanged |
| DELETE_SCRIPT | 9 | script_id |
| RUN_SCRIPT | 10 | script_id |
| STOP_SCRIPT | 11 | none |
| GET_PAIRING_CODE | 12 | none; returns actual Matter manual/QR payload or an unavailable error |
| CHECK_UPDATE | 13 | none; schedules network check; subsequent state responses expose available_build |
| INSTALL_UPDATE | 14 | none; schedules installation of last eligible checked update |

Response carries current State, correlated ID and optional operation-specific data. Treat a non-empty error as failure. Request IDs should be nonzero. Requests with unsupported color channels must fail rather than silently remap them. Global brightness scales the output; it does not rewrite the stored colors. An explicit color from any transport stops the active Lua animation.

mDNS service is `_luxio._tcp`, port 80. TXT keys: `id` (SHA-256 SPKI fingerprint), `build` (integer text), `strip` (strip type). Instance name is the user-configured light name; host is `luxio-<first 12 fingerprint hex characters>.local`.

## Signed announcements

1. Persist an ECDSA P-256 private key in NVS on first boot. Public key is canonical DER SubjectPublicKeyInfo, encoded as Base64. Device ID is lowercase hexadecimal SHA-256 of that DER.
2. `POST /announcements/challenge` with JSON `{deviceId}`. Server returns a random 64-character hex `nonce`, valid for two minutes and bound to the requesting IP/device ID.
3. Canonical signature bytes are UTF-8 of the compact JSON array `[1,nonce,deviceId,name,lanIp,build,channel]`, with no whitespace. Sign SHA-256 using ECDSA P-256, DER signature encoded Base64.
4. `POST /announcements` with `{deviceId,publicKey,nonce,name,lanIp,build,channel,signature}`. Server validates the fingerprint/signature then atomically consumes the challenge. Success is HTTP 204.
5. Announce on startup and every 900 seconds. Client time is not part of verification. Server supplies `createdAt` and `updatedAt`; expired/consumed challenges cannot replay old announcements.

`GET /devices` returns a JSON array for the caller's WAN address, excluding records with `updatedAt <= now−1h`. Old Device records remain stored. Public responses exclude key data. CORS permits every origin. Proxy addresses must be configured explicitly.

## Updates

`GET /updates?chip=esp32s3&build=1&channel=0` returns the highest integer build greater than the current build whose channel is at most the device's channel, or JSON null. Supported chip identifiers describe integrated-radio targets; they do not imply hardware qualification. S2/P4/H2 are not eligible under the integrated Wi-Fi+Bluetooth requirement.

Registration: `POST /updates` with Bearer publication secret and JSON:

```json
{
  "chip": "esp32s3",
  "build": 123,
  "channel": 100,
  "url": "https://firmware.example/build-123/app.bin",
  "sha256": "<64 lowercase hex characters>",
  "size": 1441408,
  "parts": [
    {"address": 0, "url": "https://firmware.example/build-123/bootloader.bin", "sha256": "<hash>"},
    {"address": 32768, "url": "https://firmware.example/build-123/partitions.bin", "sha256": "<hash>"},
    {"address": 57344, "url": "https://firmware.example/build-123/boot_app0.bin", "sha256": "<hash>"},
    {"address": 65536, "url": "https://firmware.example/build-123/app.bin", "sha256": "<hash>"}
  ]
}
```

Channel defaults to 100. A user-requested update reboots into the fixed recovery loader, which downloads the application into its single main slot and verifies its SHA-256 before selecting it. Browser first-install downloads all recovery-layout flash parts and verifies each hash. Hashes detect corruption; firmware is not cryptographically signed, as specified. Firmware currently permits all server TLS certificates. These decisions are separate from signed device announcements.

## Browser networking

Device CORS/Private Network Access response headers are implemented. They cannot override a browser's HTTPS mixed-content or local-network policies. The localhost Studio-to-device path has been tested; universal HTTPS-hosted Studio support across Chrome/Edge/Safari/Firefox remains unresolved. See [Chrome's Local Network Access documentation](https://developer.chrome.com/blog/local-network-access). No remote-control relay or silent browser-scope reduction has been substituted.

## Device modes (firmware build 5+)

`State.mode` (field 8) reports `COLOR=0`, `GRADIENT=1`, or `ANIMATION=2`. `SET_COLOR`, `STOP_SCRIPT`, deletion of the active script, strip hardware reconfiguration and Lua failure return the device to Color. Brightness/power changes preserve its mode. Firmware boots into Color with its saved solid color, as before.

`Script.mode` (field 4) is saved and returned by script CRUD/listing. Set it to Gradient or Animation before `RUN_SCRIPT`. Gradient executes Lua `setup()` once, retains the resulting per-pixel channels, and releases Lua; `loop()` is never called. Animation continues calling `loop()`. Both keep their active script UUID until stopped or replaced, and transitions retain the 500 ms output fade. Legacy scripts and clients that omit the field retain Animation behavior. Invalid script mode values are rejected. No display-name or source-text guessing is used by firmware to determine mode.

## Manual recovery updates (build 6 onward)

`CHECK_UPDATE` (13) explicitly starts an asynchronous update check; it never installs. `Response.update_checking` (field 9, bool) reports progress and `Response.update_error` (field 10, string) reports a failed check. Poll `GET_STATE` until field 9 becomes false; field 8 then contains the eligible build or zero. `INSTALL_UPDATE` (14) is the separate user-initiated install operation.

The application persists the accepted HTTPS URL, SHA-256, size, build and flash-layout target in one NVS blob, selects the fixed recovery loader, and reboots. The normal Protobuf API is offline during recovery. The loader exposes HTTP `GET /status`, `POST /retry`, and `POST /update` on port 80. Its replacement-update JSON requires `target: "esp32s3-recovery-v1"`, `url`, `sha256`, `size`, and `build`. Normal firmware updates never write the loader, partition table, NVS or script filesystem.

The cloud `/updates` query requires `target=esp32s3-recovery-v1` for ESP32-S3. Only recovery targets matching their chip are accepted; omitted targets and other layouts are rejected. There is no timer or boot-time path that checks for or installs new releases. A loader reboot resumes only the persisted request that the user already accepted.

`GET_STATE` includes read-only `NetworkStatus` diagnostics in response field 21. Fields 11–13 report the currently connected Wi-Fi SSID, gateway, and up to two DNS servers. They are empty when disconnected; the IP address remains in `State.lan_ip`. No Wi-Fi password is returned. Older firmware omits these additive fields.

`Config.relay` (field 6) is an optional message with `enabled` (1), `gpio` (2),
and `inverted` (3). Old saved configurations leave it disabled. SET_CONFIG without
this message retains the existing relay settings; an explicit `enabled=false`
disables it. GPIO0 remains a valid selection. Firmware validates output capability,
reserved pins and conflicts with the LED data pin. Relay settings require build 27.
