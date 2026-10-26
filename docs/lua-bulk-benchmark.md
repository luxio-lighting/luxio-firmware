# Bulk animation API and ESP32 measurements

The new `luxio_fill`, `luxio_fill_range` and `luxio_fill_hsv` methods render into
Luxio's existing six-channel animation working buffer. They allocate no extra
pixel array. Each native loop is bounded by the configured strip length (at most
1024 pixels); normal frame publication and the Lua scheduler remain in place.
The full ESP32 app grows from 2,295,376 to 2,296,560 bytes: 1,184 bytes.

- `luxio_fill(r, g, b, w, ww, cw)` fills the entire strip.
- `luxio_fill_range(start, count, r, g, b, w, ww, cw)` fills a bounded range.
- `luxio_fill_hsv(hue, step=0, saturation=1, value=1)` renders a uniform hue or
  spectrum, in turns. Hue and step wrap; saturation/value are 0–1. White channels
  are cleared. Fill channels default to zero and must be integers from 0–255,
  respecting RGB/RGBW/RGBCCT capabilities. Invalid arguments fail before writes.

Rainbow and Colorloop use the native HSV path. Aurora, Ember and Tidal use fill
when Pulse is off, and read parameters/math functions once per frame when it is
on. All five keep their per-pixel fallback for older firmware. Christmas, Knight
Rider, Fireplace, Candle and Cyberpunk keep their existing source and appearance.
The iOS, Android and Studio catalogs agree; mobile native runtimes and the shared
simulators/CLI implement the same methods. Studio's reference, new-animation
example and untouched built-in workspace copies are updated. Customized sources,
parameter values and device files are preserved.

## Method

Device: GLEDOPTO ESP32-D0WD-V3, 240 MHz, no PSRAM. Both versions use float Lua and
identical deterministic timestamps. Counts are 100 and 1024 configured WS2812
pixels. Each measurement has five warmup frames and 60 recorded frames. Task CPU
time includes the Lua API, safety hooks and completed-frame copy, but excludes
radios, physical LED transmission, source compilation and setup. These numbers
are not whole-device FPS.

The first comparison uses the original pre-bulk float scripts/API versus the new
API and presets. Those runs also showed timing shifts for unchanged scripts, so
they do not isolate the effect of calling a bulk method.

The controlled comparison uses the **same VM, compiled updated script and globals
table** for both paths. C functions are retained in the Lua registry while their
globals alternate between the function and nil, selecting the preset's native or
fallback branch. Both paths render each timestamp; their order alternates every
frame. Output is compared byte by byte on the ESP32, allowing at most one channel
step out of 255. The comparison buffer belongs only to the diagnostic harness,
not to the firmware API. These presets have no frame-to-frame simulation state.
This comparison isolates bulk calls; it does not measure parameter-hoisting gains.

## Paired results (2026-10-26)

| Preset | 100 LEDs: Lua → bulk | 1024 LEDs: Lua → bulk | Speedup at 1024 |
| --- | ---: | ---: | ---: |
| Rainbow | 2.391 → 0.201 ms | 23.992 → 1.528 ms | 15.7× |
| Colorloop | 2.183 → 0.093 ms | 21.826 → 0.425 ms | 51.3× |
| Aurora (Pulse off) | 1.526 → 0.087 ms | 15.015 → 0.345 ms | 43.5× |

The paired control cases (unchanged scripts or Pulse on) differ by at most 0.18%
in mean CPU time. They do not use a bulk branch. No comparable speedup is claimed
for those effects. The solid-fill path also applies to Ember and Tidal with Pulse off.
The largest observed pixel-channel difference on the ESP32 is 0 out of 255.

[Paired raw measurements](benchmarks/lua-bulk-paired-esp32.csv) contain all 11
cases at both counts. [Initial before/after measurements](benchmarks/lua-bulk-before-after-esp32.csv)
are retained separately; use the paired table above when attributing speedups to
native methods. Lua allocator samples include functions, source and transient VM
allocations; garbage collection makes them unsuitable as exact per-method RAM costs.
Neither path allocates an additional frame through the bulk API.

## Reproduce

- `python3 tools/benchmark-lua-precision.py --bulk` builds both host versions and
  checks the output of all presets. Results are in `.sdk/bulk-benchmark`.
- Add `--esp32` to build the two application-only diagnostic images. The command
  does not flash hardware.
- `python3 tools/benchmark-lua-precision.py --bulk --paired --esp32` builds the
  single paired diagnostic image in `.sdk/bulk-paired/paired/build`.

Preserve the current application and lighting state, flash only the existing app
partition, capture UART at 115200 through `BENCHMARK_DONE`, then restore the full
application and state. Never flash the diagnostic partition table or bootloader.
The before/after fixtures are versioned separately so results remain reproducible.
Paired RESULT rows contain name/path, count, mean, p50, p95 and maximum CPU
microseconds, current Lua allocator bytes and maximum channel difference. The
last field is a checksum in the older unpaired measurements.

## Validation

Firmware and mobile C runtimes pass sanitizer tests for ranges, channel validity,
white output, hue wrapping, reverse spectra, rejected writes and 1–1024 pixels.
The JavaScript simulators pass the same fixtures. All updated iOS presets and
older-firmware fallback paths pass output/parameter tests across the supported
strip types. Host before/after output differs by at most one channel step; all
unchanged presets are bit-identical at the sampled timestamps.

The full firmware passed all ten current presets plus the six API validation
fixtures at both 100 and 1024 configured WS2805 pixels on the device. Android
assembles successfully, both mobile native runtimes pass sanitizer tests, and
Studio passes all 26 browser tests plus its production build. Firmware tooling
and storage regressions, simulator tests, and TypeScript checks pass.
