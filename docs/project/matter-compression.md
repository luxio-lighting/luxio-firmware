# Matter compression measurement — 22 October 2026

Historical experiment before the repository split and recovery-loader implementation. The paths below describe that snapshot; retained artifacts now live in `luxio-firmware/.sdk/matter-compression/`. Option B was subsequently selected; see [recovery-updates.md](recovery-updates.md).

The current Matter-enabled firmware compiles and compresses enough to make a full app plus compressed OTA staging partition plausible on the LOLIN S3 Mini's 4 MiB flash. This is a size experiment, not a tested compressed OTA implementation. The board was not flashed, and the production partition table and build-size guard were not changed.

## Results

| Artifact | Bytes | MiB |
| --- | ---: | ---: |
| Application `.bin` | 2,258,000 | 2.1534 |
| XZ payload | 1,197,900 | 1.1424 |
| Packed OTA image, including 328 bytes of headers | 1,198,228 | 1.1427 |
| Application plus packed OTA image | 3,456,228 | 3.2961 |

The packed image is **46.93% smaller**. Decompression reproduced the application byte-for-byte. The packed header's length, MD5 and CRC32, and the copied application header were independently checked.

The application exceeds either existing 1,900,544-byte A/B slot by 357,456 bytes. The two slots together provide 3,801,088 bytes, leaving **344,860 bytes (336.8 KiB)** after storing the app and compressed update. That is space available for partition growth margins, not runtime RAM.

One possible allocation, retaining the current filesystem and data partition addresses, is:

| Region | Offset | Size | Headroom for measured artifact |
| --- | --- | ---: | ---: |
| Main application | `0x10000` | `0x260000` (2,490,368 bytes) | 232,368 bytes |
| Compressed OTA staging | `0x270000` | `0x140000` (1,310,720 bytes) | 112,492 bytes |
| Existing filesystem | `0x3b0000` | `0x40000` | Unchanged |
| Existing core dump area | `0x3f0000` | `0x10000` | Unchanged |

This allocation is illustrative and has not been installed. The enhanced bootloader must also fit before the partition table, or that layout must be adjusted. Future app and compressed-image sizes must each fit their own partition. These measurements exclude signing/encryption padding.

The compressed image also fits within the board's nominal 2 MiB PSRAM, leaving 898,924 bytes before all other allocations. This does not establish available runtime memory or make overwriting a running application safe.

## Build and compression settings

- Current Luxio source, `LUXIO_BUILD=5`, `LUXIO_MATTER=1`.
- Arduino-ESP32 3.3.3, precompiled ESP-IDF 5.5 libraries.
- Board: `esp32:esp32:lolin_s3_mini:CDCOnBoot=cdc,PartitionScheme=min_spiffs`.
- Measurement-only `upload.maximum_size=4194304` override lets the compiler finish despite the current slots being too small. It does not create a valid deployment layout. Do not flash the generated merged image.
- Espressif's `gen_custom_ota.py`: XZ/LZMA2, preset 6, 64 KiB dictionary, CRC32, v3 header and `--add_app_header`.
- Artifacts and build log: `next/.sdk/matter-compression/` (ignored).

Reproduce from `next/`, after the existing firmware SDK setup:

```sh
mkdir -p .sdk/matter-compression
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
"$ARDUINO_CLI" --config-file .sdk/arduino-cli.yaml compile \
  --fqbn 'esp32:esp32:lolin_s3_mini:CDCOnBoot=cdc,PartitionScheme=min_spiffs' \
  --build-path "$PWD/.sdk/matter-compression/build" \
  --build-property 'upload.maximum_size=4194304' \
  --build-property "runtime.tools.ctags.path=$PWD/.sdk/ctags" \
  --build-property "tools.ctags.path=$PWD/.sdk/ctags" \
  --build-property "compiler.c.extra_flags=-DLUA_USE_C89 -I$PWD/firmware/Luxio/src/vendor/nanopb" \
  --build-property "compiler.cpp.extra_flags=-DLUA_USE_C89 -I$PWD/firmware/Luxio/src/vendor/nanopb -DLUXIO_BUILD=5 -DLUXIO_MATTER=1 -DLUXIO_CLOUD_URL=\"https://api.luxio.lighting\"" \
  firmware/Luxio > .sdk/matter-compression/build.log 2>&1
cd .sdk/matter-compression
# The already downloaded, inspected upstream packer is retained here.
../venv/bin/python gen_custom_ota.py -hv v3 --add_app_header -i build/Luxio.ino.bin
../venv/bin/python - <<'PY'
from pathlib import Path
import lzma
app = Path('build/Luxio.ino.bin').read_bytes()
packed = Path('custom_ota_binaries/Luxio.ino.bin.xz.packed').read_bytes()
assert lzma.decompress(packed[328:]) == app
print('Application:', len(app), 'Packed OTA:', len(packed))
PY
```

Recorded SHA-256 values (rebuild timestamps may change the application hash):

```text
Application: 12d59507f6588073aa892674159f9b6b0a084da9ec08ee31f2a51f143aa60851
Packed OTA:  53981d6d657660e2fa5bb0b0a1262c1bb1e6dd13c5b27abc7f2e83a7a78438f4
Packer:      a3b69ce7ddb33fdf92923e1beee958dc8c6791ab412ac96052f322f8843e3eef
```

## Decision implications

Option A, compressed OTA staged in flash, is supported by these size results. Downloading can occur during normal operation; the offline portion then covers bootloader decompression, flashing and restart. Its duration has not been measured. It needs an enhanced bootloader and a partition migration, and a two-partition arrangement cannot roll back to the previous full firmware.

Option B, a small recovery Wi-Fi updater streaming into the main application partition, remains an alternative. Its actual updater size and downtime have not been measured. No OTA architecture has been selected or implemented by this experiment.

This build includes the real Matter implementation but still excludes Luxio's standalone BLE control service. Matter commissioning, BLE coexistence and compliance remain open; successful compilation is not runtime validation. Finishing these features can change the space budget.

Sources: [Espressif bootloader_support_plus 0.4.2](https://components.espressif.com/components/espressif/bootloader_support_plus/versions/0.4.2/readme), [official compression packer](https://raw.githubusercontent.com/espressif/esp-iot-solution/master/tools/cmake_utilities/scripts/gen_custom_ota.py).
