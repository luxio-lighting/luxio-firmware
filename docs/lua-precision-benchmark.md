# Lua float precision benchmark

Firmware Lua uses 32-bit `float` numbers instead of 64-bit `double`, while
preserving its integer configuration (`long` with `LUA_USE_C89`: 32 bits on
ESP32). The C89 compatibility path now honors this choice and uses `sinf`,
`powf`, `strtof`, etc. It previously forced double math functions even when
the number type was configured as float.

The benchmark extracts the allocator, API, instruction hook and cooperative
scheduler directly from `Luxio/src/main.cpp`. Its ten preset sources are a
snapshot of the Studio catalog on 2026-10-26, stored under
`tools/fixtures/animation-presets.json`; they are not bundled in firmware.

## Method

The same original ESP32 runs both diagnostic images, compiled with the same
Arduino ESP32 3.3.3 toolchain and optimization settings. The baseline selects
`LUA_FLOAT_DOUBLE`, including the original C89 double-math fallback. The changed
image selects float. Neither image starts radios or transmits LED data.

Each preset renders 65 frames at both 100 and 1024 configured WS2812 pixels.
The first five frames warm up the VM; the remaining 60 are measured. Animation
timestamps are identical between images, starting at 1000 ms and advancing by
33 ms per frame. Frame time sums FreeRTOS task CPU time across scheduler slices,
including Lua API calls, safety hooks and the completed-frame copy. Runtime
counters are checkpointed at both measurement boundaries. Compilation, setup,
printing, color transitions and LED transmission are excluded.

These results measure animation CPU cost, not complete application FPS or
performance under concurrent Wi-Fi/Matter/BLE traffic. Other ESP32 variants need
their own measurements; the C3/C6 do not have the original ESP32's float FPU.

## ESP32 results (2026-10-26)

Measured on the attached GLEDOPTO ESP32-D0WD-V3 at 240 MHz, without PSRAM.
Times below are mean CPU milliseconds per completed animation frame.

| Preset | 100 LEDs: double → float | 1024 LEDs: double → float | Speedup at 1024 | Lua bytes: double → float |
| --- | ---: | ---: | ---: | ---: |
| Rainbow | 4.88 → 3.64 | 48.91 → 37.03 | 1.32× | 14,206 → 11,346 |
| Colorloop | 4.34 → 3.43 | 44.31 → 34.35 | 1.29× | 14,186 → 11,326 |
| Aurora | 10.81 → 6.34 | 110.17 → 59.76 | 1.84× | 12,643 → 10,335 |
| Ember | 10.78 → 6.33 | 109.93 → 60.57 | 1.82× | 12,643 → 10,335 |
| Tidal | 10.81 → 6.35 | 110.83 → 59.84 | 1.85× | 12,643 → 10,335 |
| Christmas | 14.72 → 14.79 | 150.69 → 158.04 | 0.95× | 13,405 → 10,809 |
| Knight Rider | 4.50 → 3.96 | 45.25 → 39.90 | 1.13× | 14,259 → 11,383 |
| Fireplace | 21.54 → 12.36 | 219.96 → 173.98 | 1.26× | 12,587 → 10,255 |
| Candle | 7.78 → 5.68 | 77.44 → 67.70 | 1.14× | 14,396 → 11,412 |
| Cyberpunk | 20.86 → 18.03 | 212.63 → 216.08 | 0.98× | 14,039 → 11,314 |

Nine of ten presets improve at 100 LEDs; eight improve at 1024. At 1024 LEDs,
Christmas is about 5% slower and Cyberpunk about 2% slower. Christmas also changes
twinkle phases; do not interpret this change as a universal speedup.
Lua allocator use falls by 2,308–2,984 bytes (18–21%) for these presets. This
excludes the separate six-byte-per-pixel animation buffer, which is unchanged.
The diagnostic application binary shrinks from 414,464 to 405,520 bytes.

[Raw measurements](benchmarks/lua-precision-esp32.csv) include p50, p95, maximum,
allocator bytes and frame checksums for both precisions.

The full ESP32 release profile also builds successfully (2,295,376-byte app).
All ten presets ran without reported animation errors at 100 and 1024 LEDs on
the attached device with its existing radio settings and WS2805 configuration.
The original 80-LED Ember preview, parameters, brightness, power and hardware
configuration were restored afterward. This was a control/runtime smoke test,
not a visual confirmation of the physical LEDs. Scheduler sanitizer tests,
firmware tooling/storage regressions and TypeScript checks pass. All vendored
Lua sources also compile with the pinned ESP32 RISC-V compiler; no RISC-V
performance claim is made from the ESP32 measurements.

## Precision

A host-side frame comparison at the same timestamps checks all six stored
channels. Nine presets differ by at most one channel step out of 255. Christmas
uses a precision-sensitive `sin(...) * 43758.5453` hash: float changes which
pixels twinkle when, with differences up to 215 channel steps in this sample.
Its warm twinkle algorithm is preserved, but frames are not identical.

Floats have roughly seven significant decimal digits. `luxio_millis()` still
returns an integer, but converting large elapsed times to float loses precision.
For periodic custom animations, reduce time modulo the period while it is still
an integer, before dividing into seconds. The short benchmark does not establish
long-uptime visual equivalence. Lua allocation sizes also depend on pointer and
integer widths, so host memory figures must not be used as ESP32 estimates.

## Reproduce

Run `python3 tools/benchmark-lua-precision.py` for both host executables, raw
frames, timing CSVs and per-preset output differences. Outputs go under
`.sdk/float-benchmark/{double,float}`. Host timings are not ESP32 timings.

Run `python3 tools/benchmark-lua-precision.py --esp32` after bootstrapping the
pinned Arduino SDK to build both ESP32 diagnostic images. This command never
flashes a device. Preserve the current application and active lighting state
first. Flash only the diagnostic application into the existing app partition;
do not write the diagnostic partition table, bootloader, NVS or filesystem.
Capture UART at 115200 baud through `BENCHMARK_DONE`, then restore the application
and lighting state. Each `RESULT` row contains preset, LED count, mean, p50, p95,
maximum CPU microseconds, Lua allocator bytes and output checksum.

Run `python3 tools/test-animation-scheduler.py` for sanitizer checks and guards
against accidentally reverting either the number type or float math functions.
