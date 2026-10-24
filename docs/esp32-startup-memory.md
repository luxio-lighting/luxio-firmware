# ESP32 startup memory regression (build 25)

On the Gledopto controller's ESP32-D0WD-V3 with 4 MB flash and no PSRAM, public build 24 restarted continuously. The boot log's ELF hash matched the published image. Its backtrace resolved to `operator new[]` in the first pass through `loop()`: startup left 32,624 bytes free, with a largest block of 31,732 bytes, and serial initialization immediately requested a 32,768-byte buffer. A second permanent buffer and full inline script objects would also have exceeded the memory budget.

Serial input is now allocated for the current frame and released after completion or timeout. Output buffers exist only during requests. Control transports use an 8 KiB workspace on constrained heaps and retain 32 KiB with PSRAM or sufficient contiguous RAM. Dispatcher writes and stored-script reads respect that capacity. Allocation failures no longer throw in transport buffer handling. WebSocket events are prepared only when subscribers exist.

Nanopb allocates optional Request/Response script fields only when present. The wire protocol and full 16 KiB script schema are unchanged. This reduces ordinary request/response object storage by about 36 KiB. Decoded allocations are released on completion, including malformed requests. Large animation operations can still exceed a board's available runtime RAM; this fix does not qualify those operations on PSRAM-less boards.

Hardware verification on the attached controller passed boot, USB GET_STATE, Matter pairing data, saving configuration, Wi-Fi scanning, recovery from an incomplete serial frame, and three resets retaining device identity and settings. Firmware build 25 was written only to the application partition. Settings/identity/partition metadata and the filesystem were backed up privately; recovery was not replaced. Wi-Fi association and cloud TLS were not part of this check.

Host regressions cover compact control structures, full-size script round trips, allocation cleanup after truncation, stored animation/gradient/color formats and filesystem resilience. Compiler stack budgets remain enforced for the ESP32 matrix.
