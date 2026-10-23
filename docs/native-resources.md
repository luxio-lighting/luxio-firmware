# Colors, gradients, and animations — build 13

All transports use the same Protobuf operations: HTTP `POST /api`, WebSocket, and framed USB. The filesystem partition remains 256 KiB; no repartitioning is needed.

| Content | Live content (RAM only) | Saved files | List / Get / Create / Update / Delete / Run |
|---|---|---|---|
| Solid RGBCCT color | `SET_COLOR` (1), `Request.color` | `/colors/<uuid>.pb` | 25 / 26 / 27 / 28 / 29 / 30 |
| Gradient | `SET_GRADIENT` (17), `Request.gradient` | `/gradients/<uuid>.pb` | 18 / 19 / 20 / 21 / 22 / 23 |
| Lua animation | `SET_SCRIPT` (31), `Request.script.source` | `/scripts/<uuid>.pb` | 5 / 6 / 7 / 8 / 9 / 10 |

Live commands never create or replace library files. Their content is excluded from later NVS settings saves, including brightness changes and firmware-update preparation. It is lost on reboot. Create/Update save only; Run selects a saved resource. Saving does not change power or playback. Successful live/Run commands turn the strip on at the existing brightness. Power is independent; brightness stays 1–256. All output changes retain the existing fade transition.

Colors use `SavedColor { id, name, color }` in request field 14; color IDs use field 15. Gradients contain 2–8 evenly spaced six-channel color stops, with optional non-executable editor JSON (at most 2048 UTF-8 bytes). Gradients are interpolated directly in C++; no Lua VM runs. Animations contain up to 16 KiB of Lua source and are always animations (mode 2; omitted mode normalizes to 2). Unsupported channels are rejected for the configured strip type.

All saved items have a UUID and a 1–63-byte name. Create assigns a UUID; Update requires the existing UUID. Each list returns up to 64 names/IDs, without content. Animation paging uses request/response fields 9/11; gradient paging uses 13/17; color paging uses 16/20. Zero next-offset means complete. Restart a listing if the library changes while paging.

`STOP_GRADIENT` (24) and `STOP_SCRIPT` (11) return their respective active mode to the current solid color. `STOP_SCRIPT` optionally accepts `Request.script.name` as an ownership guard: a different active animation name is left untouched. Studio uses a unique name for each RAM preview. Use power-off to turn off a solid color.

State reports `active_color_id`, `active_gradient_id`, and `active_script_id` only for saved resources. A RAM animation still reports mode 2 and `script_name`; a RAM gradient reports its current stops in `State.gradient`. Saved gradients/colors can restore after reboot. Lua execution itself does not resume on boot.

## Storage

Colors use `LXC1`, gradients `LXG1`: four-byte magic, little-endian payload length, CRC-32, and the Protobuf payload. CRC covers the header and body except the checksum field. Lua retains the losslessly compressed `LXS1` format. All libraries share free filesystem space; no fixed quotas reserve unused capacity. Saving writes a temporary file, flushes and verifies it, then atomically renames it. An 8 KiB reserve protects replacement operations. No automatic eviction occurs.

## Existing Lua gradients

Before updating an older device:

```sh
node --import tsx tools/migrate-gradients.ts export 192.168.1.231 .sdk/library-backup.json
```

After installing build 13:

```sh
node --import tsx tools/migrate-gradients.ts import 192.168.1.231 .sdk/library-backup.json
```

The importer converts literal stop lists from legacy Luxio gradients (including older static presets that predate the Gradient mode flag), preserves editor metadata, verifies the new file, and only then deletes an unchanged legacy copy. It never evaluates Lua. Unsupported legacy sources stay on disk and are reported. Keep the backup. Lua-gradient playback is no longer supported; ordinary animations are unchanged.

## Checks

`npm test` covers C++ interpolation at 1/60/1024 pixels, all six channels, CRC corruption/truncation, independent CRUD, RAM-only modes, power/brightness and pagination. The existing compressed Lua filesystem test injects interrupted writes. `DEVICE_ADDRESS=<ip> node --import tsx tools/test-native-resources-device.ts` checks all three modes and 65-item color/gradient paging on hardware, removes temporary files, and restores output.

## Animation parameters (build 21)

Leading Lua comments declare controls: `-- @parameter Flicker,Checkbox,true` or `-- @parameter Speed,Slider,50,0,100`. Up to 16 unique, case-sensitive names are allowed (1–32 ASCII letters/digits/spaces/underscores, starting with a letter). Slider and Number ranges are finite, minimum < maximum, and bounded to ±1e9; defaults must be in range. Checkbox defaults are exactly `true` or `false`. Declarations after Lua code are rejected. An optional `//` comment may follow a declaration. `Number` shows a numeric input instead of a slider. `Boolean` remains a legacy alias for `Checkbox`; older firmware understands Boolean and Number but rejects Slider and Checkbox.

`luxio_get_parameter(name)` returns a Lua Boolean or Number. Read it inside `loop()` for live changes. An unknown name raises a runtime error. The firmware parses declarations on save/start, rejects invalid overrides, and derives definitions itself instead of trusting client-supplied ranges.

`AnimationParameter` carries name (1), checkbox flag (2; false means numeric), value (3), default (4), minimum (5), maximum (6), and number-input flag (7; false means slider for numeric values). Numeric fields use Protobuf double; Boolean values are represented as 0/1. `Script.parameters` (field 5) stores overrides in the compressed record metadata. `State.parameters` (field 14) reports the active definitions and values. Metadata listings omit parameters; GET_SCRIPT retrieves the full saved animation.

`SET_ANIMATION_PARAMETERS` (32) accepts a partial `Request.parameters` (field 17) patch. Supply `Request.script_id` for a saved animation or `Request.script.name` matching the RAM preview. A stale/missing guard, unknown name, duplicate, wrong type, or out-of-range value rejects the whole patch. This operation does not restart Lua, reset its timer, change power/brightness, or write flash. Values are never included in NVS state. Explicit UPDATE_SCRIPT saves parameter values with the source without restarting the current animation. The next RUN_SCRIPT uses those saved values.

Native records support up to 20 KiB including metadata; Lua source remains limited to 16 KiB. Existing records without values use the declared defaults. Host tests cover both parsers, live VM continuity, invalid patches, and compressed metadata round-trips.

Physical regression: `DEVICE_ADDRESS=<ip> node --import tsx tools/test-parameters-device.ts`. Add `DEVICE_SERIAL=<port>` to use USB instead of HTTP. The test removes its temporary animation and restores the starting output. When a RAM-only animation is active, provide `RESTORE_ANIMATION_SOURCE=<lua-file>`; the test refuses to replace it without source for restoration. Build packaging checks compiled stack-frame budgets; large state snapshots stay on the heap so parameter metadata does not exhaust task stacks.

## Network diagnostics (build 15)

`GET_STATE` includes `networkStatus`: AP association (`connected`), RSSI in dBm,
BSSID, channel, ESP-IDF power-save mode (`0` means disabled), uptime in
milliseconds, free internal RAM and its largest free block in bytes, plus the
number of Wi-Fi disconnect events and the last ESP-IDF disconnect reason.
Association is not proof of a working LAN connection; compare USB diagnostics
with HTTP reachability when investigating a stall. No credentials are returned.

The powered strip disables Wi-Fi modem sleep and scans all channels when
joining, sorting matching access points by signal strength. Existing clients
ignore the new response field. This does not alter pairing or saved libraries.

Build 16 allocates Lua's entire VM heap in PSRAM when present, retaining its
128 KiB limit and refusing to spill into internal RAM if PSRAM is exhausted.
Boards without PSRAM retain the normal allocator. This prevents small Lua
objects from competing with Wi-Fi/Matter for internal RAM. The failing S3 Mini
had approximately 23 KiB free internally before Ember and 8 KiB after starting
it, at which point HTTP timed out even with strong RSSI and modem sleep disabled.

## Animation runtime diagnostics (build 17)

`GET_STATE` reports `animationError` separately from request errors. A runtime
failure stops Lua, restores the current solid color and retains a bounded error
message until another animation starts or the device restarts. Polling does not
clear it. Diagnostics remain in Protobuf; USB never receives textual log output.

`DEVICE_ADDRESS=<ip> node --import tsx tools/test-animation-runtime-device.ts`
checks runtime errors, infinite loops and memory exhaustion on hardware. Start
an animation first; for a RAM animation, provide `RESTORE_ANIMATION_SOURCE`.
The test restores that animation and its controls without saving test files.

Build 18 places all mbedTLS allocations in PSRAM on equipped boards, including
small crypto objects. The earlier `calloc` override only moved large buffers;
repeated cloud update checks still reduced internal free RAM to roughly 8 KiB
and stalled LAN requests while Lua continued running. There is no fallback to
internal RAM when PSRAM is exhausted. Boards without PSRAM keep the SDK allocator.

`DEVICE_ADDRESS=<ip> node --import tsx tools/test-network-device.ts` monitors the
currently playing animation for ten minutes (override with `TEST_SECONDS`, at
least 180). It checks playback continuity, errors, reboots, disconnects and
internal memory while reading libraries, pausing requests and checking for cloud
updates. It never installs an update or modifies the library.

S3 Mini verification, build 18 (2026-10-22): Ember remained active for 600 seconds
through 509 state checks, periodic library reads, a 60-second idle interval and
19 cloud update checks. No disconnect, reboot, Lua error or HTTP failure occurred;
minimum sampled internal free RAM was 20,204 bytes, maximum state-request latency
99 ms. Separate hardware checks verified runtime-error retention, runaway-loop
termination, memory-limit termination and restoration of the original animation.
The earlier spontaneous Ember stop was not reproduced; its cause remains
unconfirmed. Runtime diagnostics are retained for the next occurrence.

## Animation preview (build 19)

One optional leading comment, `-- @preview ff460c,d21400,ffc823`, declares a
visual gradient hint with two to eight RGB hex stops. Six hex digits are required;
case, surrounding whitespace and an optional `#` are accepted. A trailing `//`
comment is allowed. Duplicate/malformed declarations or declarations after Lua
code are rejected before replacing the current animation or saved file.

The firmware derives `Script.preview` (field 6) from source on save and includes
it in compressed metadata and source-free listings. `State.animationPreview`
(field 15) exposes the running palette for both saved and RAM animations. It
clears on stop, color/gradient changes and runtime failure. RAM previews are not
persisted to NVS. The annotation never changes LED output or uses white channels.
Missing annotations produce an empty palette; clients should use a neutral hint.

`DEVICE_ADDRESS=<ip> node --import tsx tools/test-preview-device.ts` verifies RAM
playback, compressed metadata listings/readback, atomic rejection of malformed
updates and clearing on mode changes. Supply `RESTORE_ANIMATION_SOURCE` for an
active RAM animation. The test removes temporary records and restores output.
