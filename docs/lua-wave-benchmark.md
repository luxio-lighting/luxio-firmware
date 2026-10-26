# Wave rendering and remaining preset optimizations

`luxio_render_waves(waves, channels)` renders up to four combined sine waves
into the existing animation working frame. Each wave can sum four sine terms;
each of the six output channels can mix four wave terms with integer powers
1–16 and optional brightness modulation. Powers use multiplication rather than
`pow`. Colors are quantized once, after mixing and modulation. Missing channels
are cleared, and strip capabilities are validated before changing any pixels.

The same API is implemented in firmware, both mobile native runtimes, Studio,
the simulator and CLI. Studio's reference describes all defaults, bounds, units,
channel behavior and examples. The descriptors are ordinary reusable Lua tables,
created outside `loop()` by the presets, with phases updated per frame. No second
pixel array is allocated. Native parsing uses bounded stack descriptors, and
checks the scheduler's CPU deadline every 32 pixels. It can yield and resume
within a render call; only the completed frame is published. Repeated calls, including
through `pcall`, still yield and respect the hard animation time limit.

## Presets

- Aurora, Ember and Tidal: a traveling brightness wave, with live Speed, Spacing
  and Pulse controls. Pulse off retains the uniform native fill.
- Fireplace: three sine terms produce heat, mapped to RGB with powers 1, 3 and 8.
- Cyberpunk: two positive waves produce cyan/magenta bands; a third modulates
  their combined color. Clipping still happens before shimmer.
- Candle: one spatial brightness wave retains the original flame modulation;
  dedicated warm-white/RGBW behavior is preserved.
- Christmas: bulk-fill the background, then write only positive twinkles.
  Compute the eighth power with multiplications. The original phase hash is
  retained; there is no per-pixel Lua cache.
- Knight Rider: bulk-clear the frame, draw the head, then traverse only the
  visible trail. A decay multiplier replaces per-pixel exponentials. The
  one-pixel behavior and both sweep directions are retained.

The frozen benchmark fixture retains each original per-pixel path as a
measurement reference. Shipped build-36 app and Studio presets call the new
API directly and contain no older-firmware fallback. Rainbow and Colorloop keep
using `luxio_fill_hsv`. Custom Studio sources and device-backed scripts are separate from the shipped
preset catalog. No legacy-device migration is required for this release.

## Method

GLEDOPTO ESP32-D0WD-V3, 240 MHz, no PSRAM, Arduino 3.3.3, float Lua. The paired
harness uses the same VM, compiled script, tables and timestamps for both paths.
It temporarily removes/restores the four bulk globals to choose the fallback
or optimized branch and alternates which path runs first at each timestamp.

Each case runs at 100 and 1024 configured WS2812 pixels: five warmup frames,
then 60 measured frames, at deterministic timestamps spaced 33 ms apart.
Task CPU time includes API parsing, scheduler checks, Lua execution and completed
frame copy. It excludes radios, physical LED transmission and compilation.
These are CPU costs, not whole-device frame rates. Pixel output is compared
byte-for-byte on the ESP32, with a maximum permitted difference of 1/255.

Compare paths within each row. Lua/allocator and binary placement affect timings
between builds, so prior-run timings are not used as this run's baseline.
The initial fill/HSV measurements remain in [the earlier report](lua-bulk-benchmark.md).

## Results

| Preset | 100 LEDs: Lua → optimized | 1024 LEDs: Lua → optimized | Speedup at 1024 |
| --- | ---: | ---: | ---: |
| Aurora | 5.496 → 0.475 ms | 56.337 → 3.430 ms | 16.4× |
| Ember | 5.496 → 0.475 ms | 56.337 → 3.430 ms | 16.4× |
| Tidal | 5.497 → 0.476 ms | 56.338 → 3.430 ms | 16.4× |
| Christmas | 14.929 → 7.425 ms | 154.503 → 77.397 ms | 2.0× |
| Knight Rider | 4.201 → 1.951 ms | 42.724 → 17.600 ms | 2.4× |
| Fireplace | 9.875 → 1.045 ms | 145.438 → 38.779 ms | 3.8× |
| Candle | 6.668 → 0.909 ms | 80.087 → 15.727 ms | 5.1× |
| Cyberpunk | 14.959 → 0.872 ms | 169.367 → 18.883 ms | 9.0× |
| Rainbow | 4.239 → 0.210 ms | 42.919 → 1.537 ms | 27.9× |
| Colorloop | 4.010 → 0.101 ms | 41.135 → 0.433 ms | 95.0× |
| Aurora (Pulse off) | 1.532 → 0.082 ms | 15.178 → 0.343 ms | 44.2× |

Largest sampled output difference: **1/255**. Christmas and Knight Rider matched exactly in this run.
[Raw paired measurements](benchmarks/lua-waves-paired-esp32.csv) include all
44 results, percentiles, maxima and allocator samples.


## Memory and firmware size

The full ESP32 application is 2,298,800 bytes, an increase of 2,240 bytes over
the preceding fill/HSV build. It fits the 4 MB device layout and its 0x280000-byte
application slot. The largest compiler-reported animation callback stack frame
is 720 bytes; the repository's stack-budget check passes. This is a stack-frame
measurement, not total task-stack consumption.

The renderer adds no persistent native frame allocation. Lua descriptor tables
and the longer scripts have a small fixed cost independent of LED count. Lua
allocator samples in the raw benchmark include bytecode, tables and transient
allocations; they are not exact incremental RAM costs.

## Validation and reproduction

- Firmware scheduler sanitizer tests cover output, range/type rejection, atomic
  rejected writes, channel capability checks, and repeated native calls through
  `pcall` reaching the CPU limit across cooperative slices.
- Both mobile C runtimes and all four JavaScript runtime copies run the same
  bulk fixtures at 1, 6 and 1024 pixels across all four strip types.
- Shipped presets are checked against the optimized paths in the frozen benchmark fixture. The
  Swift, Kotlin and Studio production catalogs contain identical Lua sources; the
  frozen benchmark fixture retains both paths for comparison.
- Studio's 26 browser tests cover playback, docs, and preservation of customized
  workspace sources; Android builds and TypeScript checks pass.
- The full firmware passes all ten presets and ten bulk API fixtures on the
  GLEDOPTO at 100 and 1024 configured WS2805 pixels, with its normal radios
  present. USB control remains responsive while switching animations. An early
  whole-frame native loop caused a USB upload timeout at 1024 pixels; bounded
  continuations fix that without allocating a second frame. Nine additional
  Fireplace/Candle/Cyberpunk switches at 1024 pixels pass afterward. The final
  firmware is installed locally; the optimized Ember preview is running with
  the original 80 LEDs, brightness, power, parameters and hardware settings.
  USB and Wi-Fi status requests both pass.

Generate the host comparison with:

```sh
python3 tools/benchmark-lua-precision.py --waves
```

Add `--esp32` to build the diagnostic image in `.sdk/waves-paired/paired/build`.
The build command does not flash. Preserve the current application and lighting
state, flash only the existing app partition at `0x130000`, capture UART through
`BENCHMARK_DONE`, then restore the full application and original state. Never
flash the diagnostic bootloader/partition table or erase the entire device.

The harness and preset snapshot are versioned in `tools/benchmark-lua-bulk.cpp`
and `tools/fixtures/animation-presets-waves.json`. RESULT rows contain case name,
count, mean/p50/p95/max CPU microseconds, current Lua allocator bytes, and maximum
output-channel difference. The comparison buffer is diagnostic-only.
