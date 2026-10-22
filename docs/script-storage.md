# Saved animations and gradients

Build 10 removes the 32-script cap. Both kinds of saved item use the same 256 KiB LittleFS partition; capacity depends on their source size and compressibility. Source remains limited to 16 KiB per item. There is no partition change.

New and edited scripts use the `LXS1` record format: a 24-byte little-endian header, Protobuf metadata (UUID, name, mode), and losslessly compressed Lua. The header contains codec, metadata length, source length, payload length and CRC-32. Codec 1 uses vendored Heatshrink 0.4.1 with window 10 / lookahead 5; codec 0 stores plain source when compression would not save bytes. The checksum covers the header and stored contents except its own four bytes. Existing plain Protobuf files remain readable and convert when edited. API clients still send and receive ordinary Lua source.

Writes use a temporary file, flush, readback/decode verification and atomic rename. At least 8 KiB plus the new record size must remain free before a save starts. An interrupted update leaves the original or complete replacement. Startup removes an abandoned temporary file. Storage-full errors ask users to delete saved items; existing items are not evicted.

`LIST_SCRIPTS` returns at most 64 metadata entries. Request field 9 (`script_offset`) starts at zero; response field 11 (`next_script_offset`) is zero at the end. Continue with that offset otherwise. Offsets apply to an unchanged library; restart a listing after adding/deleting items. Updated HTTP helpers, CLI (HTTP/UART), iOS and Android clients collect all pages. Older clients that do not follow continuations see only the first page.

The iOS app saves named gradient copies, lists custom animations and gradients, supports playing/stopping and deleting them, and shows search for larger libraries. Pull to refresh after uploads from another client. Editing the color wheel continues to use its separate working gradient rather than overwriting a named save. Lua animations can be uploaded through Studio or the CLI.

## Validation — 22 October 2026

- C++ sanitizer tests: exact text round-trip, raw fallback, legacy compatibility, metadata-only reads, maximum-size input, corruption, truncation and randomized malformed records.
- LittleFS host model: all records readable at capacity; replacement safe across 40 injected write/erase failures. Nine representative presets used 4,874 bytes as plain records versus 3,255 bytes compressed (33.2% reduction including metadata). Capacity in this model was 65 versus 90, including the save reserve; this is not an exact prediction of the ESP32 filesystem implementation.
- Physical LOLIN S3 Mini / build 10: 140 saved items at capacity (nine existing plus 131 temporary mixed presets), all sources/modes read back exactly, paginated listing complete, oversized replacement at capacity preserved the original, animation/gradient playback and a 16 KiB source update worked. Temporary records were removed and the original nine-item library, color and brightness restored. Capacity varies with actual scripts and names.
- The nine existing scripts were then converted and verified after reboot: UUIDs, names and Lua source are unchanged. Three legacy unspecified modes normalized to their existing Animation behavior when saved. Identity, strip settings, output and available Matter pairing data were preserved.
- iOS UI test: saved, searched, played and deleted a named gradient in a 130-item fixture library. Luxio 0.1.6 (7) was installed on the iPhone 16 Pro.
- A full flash backup and script export were saved privately before the application-only flash. Partition layout, NVS and recovery loader were preserved.
- Protocol tests cover 1,000 metadata items, HTTP continuation validation, a 151-item CLI listing and native Swift pagination. Android, setup and Studio builds include the same continuation fields.

Run `npm test` for host tests. For the opt-in physical test, run `DEVICE_ADDRESS=<ip> node --import tsx tools/test-script-storage-device.ts`; it fills storage with temporary items, exercises playback at low brightness, and restores the original library/output afterward.
