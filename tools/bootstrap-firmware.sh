#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ARDUINO_CLI=${ARDUINO_CLI:-arduino-cli}
command -v "$ARDUINO_CLI" >/dev/null 2>&1 || { echo 'Install the official Arduino CLI or set ARDUINO_CLI to its path.' >&2; exit 1; }
mkdir -p .sdk/arduino .sdk/downloads .sdk/user .sdk/cache
# Do not install through a symlink into a machine-wide Arduino package store.
test ! -L .sdk/arduino/packages || { echo 'Existing SDK uses shared packages. Keep that SDK or use a fresh local .sdk directory.' >&2; exit 1; }
cat > .sdk/arduino-cli.yaml <<EOF
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: $PWD/.sdk/arduino
  downloads: $PWD/.sdk/downloads
  user: $PWD/.sdk/user
build_cache:
  path: $PWD/.sdk/cache
EOF
"$ARDUINO_CLI" --config-file .sdk/arduino-cli.yaml core update-index
"$ARDUINO_CLI" --config-file .sdk/arduino-cli.yaml core install esp32:esp32@3.3.3

# Original ESP32 diagnostic builds with LUXIO_MATTER=0 use NimBLE.
"$ARDUINO_CLI" --config-file .sdk/arduino-cli.yaml lib update-index
"$ARDUINO_CLI" --config-file .sdk/arduino-cli.yaml lib install NimBLE-Arduino@2.5.1
