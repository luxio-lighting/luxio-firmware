#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ARDUINO_CLI=${ARDUINO_CLI:-arduino-cli}
if ! command -v "$ARDUINO_CLI" >/dev/null 2>&1 && [ -x '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' ]; then
  ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
fi
test -f .sdk/arduino-cli.yaml || { echo 'Run tools/bootstrap-firmware.sh first' >&2; exit 1; }
# The sketch contains no declarations; all C++ is in src/. Avoid the legacy
# Intel-only ctags shipped in some Arduino installations on Apple Silicon.
mkdir -p .sdk/ctags
printf '#!/bin/sh\nexit 0\n' > .sdk/ctags/ctags
chmod +x .sdk/ctags/ctags
"$ARDUINO_CLI" --config-file .sdk/arduino-cli.yaml compile \
  --fqbn "${LUXIO_FQBN:-esp32:esp32:lolin_s3_mini:CDCOnBoot=cdc,PartitionScheme=min_spiffs}" \
  --build-path "$PWD/build/main" \
  --build-property 'upload.maximum_size=2621440' \
  --build-property "runtime.tools.ctags.path=$PWD/.sdk/ctags" \
  --build-property "tools.ctags.path=$PWD/.sdk/ctags" \
  --build-property "compiler.c.extra_flags=-DLUA_USE_C89 -I$PWD/Luxio/src/vendor/nanopb" \
  --build-property "compiler.cpp.extra_flags=-DLUA_USE_C89 -I$PWD/Luxio/src/vendor/nanopb -DLUXIO_BUILD=${LUXIO_BUILD:-12} -DLUXIO_MATTER=${LUXIO_MATTER:-1} -DLUXIO_CLOUD_URL=\"${LUXIO_CLOUD_URL:-https://api.luxio.lighting}\"" \
  Luxio
# Guard the actual custom application slot, even when a board profile has another limit.
size=$(wc -c < build/main/Luxio.ino.bin | tr -d ' ')
test "$size" -le 2621440 || { echo "Firmware ($size bytes) exceeds the 2621440-byte application slot" >&2; exit 1; }

# Recovery is a separate factory app. Its partition and request format must match.
cp Luxio/partitions.csv Recovery/partitions.csv
cp Luxio/src/update_job.h Recovery/src/update_job.h
"$ARDUINO_CLI" --config-file .sdk/arduino-cli.yaml compile \
  --fqbn "${LUXIO_FQBN:-esp32:esp32:lolin_s3_mini:CDCOnBoot=cdc,PartitionScheme=min_spiffs}" \
  --build-path "$PWD/build/recovery" \
  --build-property 'upload.maximum_size=1179648' \
  --build-property "runtime.tools.ctags.path=$PWD/.sdk/ctags" \
  --build-property "tools.ctags.path=$PWD/.sdk/ctags" \
  Recovery
size=$(wc -c < build/recovery/Recovery.ino.bin | tr -d ' ')
test "$size" -le 1179648 || { echo "Recovery ($size bytes) exceeds its fixed 1.125 MiB slot" >&2; exit 1; }
# Arduino's merged images put the sketch at 0x10000; this layout needs TWO apps.
# Remove those misleading outputs. Use the explicit flash parts from package-firmware.py.
rm -f build/main/Luxio.ino.merged.bin build/recovery/Recovery.ino.merged.bin
python3 tools/package-firmware.py
