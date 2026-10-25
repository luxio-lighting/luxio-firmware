#!/usr/bin/env python3
"""Build the opt-in original ESP32 profile with a source-built IDF/Matter SDK.

Does not flash or publish. The ordinary five-chip release build is unchanged.
Requires Docker and the pinned Arduino core installed by bootstrap-firmware.sh.
"""
import argparse
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=int, required=True)
parser.add_argument('--directory', default='.sdk/lowram/esp32')
args = parser.parse_args()
assert 0 < args.build <= 2147483647
directory = (root / args.directory).resolve()
assert directory != root / '.sdk' and directory.is_relative_to(root / '.sdk'), 'Keep experimental builds in .sdk'
arduino = root / '.sdk/arduino/packages/esp32/hardware/esp32/3.3.3'
assert arduino.is_dir(), 'Run tools/bootstrap-firmware.sh first'
directory.mkdir(parents=True, exist_ok=True)
# Discard stale project sources/Kconfig when reusing a build directory.
shutil.rmtree(directory / 'main', ignore_errors=True)
for path in (root / 'idf/esp32').rglob('*'):
    if path.is_file():
        target = directory / path.relative_to(root / 'idf/esp32')
        target.parent.mkdir(parents=True, exist_ok=True)
        # Preserve the locked component hashes; fail if resolution cannot satisfy them.
        shutil.copyfile(path, target)
for source, target in [(root / 'Luxio/src', directory / 'main/luxio'),
                       (arduino, directory / 'components/arduino')]:
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target, ignore=shutil.ignore_patterns('.git', 'docs', 'tools', 'examples', 'tests'))
shutil.copyfile(root / 'Luxio/partitions.csv', directory / 'partitions.csv')
# Reset generated settings so stale menuconfig values cannot override this profile.
(directory / 'sdkconfig').unlink(missing_ok=True)
cache = root / '.sdk/lowram/ccache'
cache.mkdir(parents=True, exist_ok=True)
image = 'espressif/idf@sha256:dfa2d076c796769c07c155eba6c672b9f395aec943b2ba3701b73379b5f9e884'
subprocess.run(['docker', 'run', '--rm', '-v', f'{directory}:/project',
                '-v', f'{cache}:/root/.cache/ccache', '-w', '/project', image,
                'idf.py', '-DIDF_TARGET=esp32', f'-DLUXIO_BUILD={args.build}', 'build'], check=True)
config = (directory / 'sdkconfig').read_text()
for value in ['CONFIG_BT_NIMBLE_ENABLED=y', 'CONFIG_BT_NIMBLE_SECURITY_ENABLE=y',
              'CONFIG_BT_NIMBLE_SM_SC=y', 'CONFIG_BT_NIMBLE_NVS_PERSIST=y',
              'CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y', 'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1',
              'CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=2',
              'CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=2', '# CONFIG_SPIRAM_BOOT_HW_INIT is not set',
              'CONFIG_ESP_MATTER_MEM_ALLOC_MODE_IRAM_8BIT=y', 'CONFIG_MBEDTLS_IRAM_8BIT_MEM_ALLOC=y',
              'CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY=y', 'CONFIG_FREERTOS_UNICORE=y',
              'CONFIG_RMT_TX_ISR_CACHE_SAFE=y', 'CONFIG_RMT_ENCODER_FUNC_IN_IRAM=y']:
    assert value in config.splitlines(), f'Configuration was not applied: {value}'
assert '# CONFIG_ENABLE_CHIPOBLE is not set' in config
binary = directory / 'build/Luxio.bin'
assert binary.stat().st_size <= 0x280000, 'Application exceeds the signed-OTA partition'
subprocess.run(['python3', str(root / 'tools/check-task-stacks.py'), str(directory / 'build')], check=True)
print(f'Built {binary} ({binary.stat().st_size:,} bytes).')
print('Application-only test image: offset 0x130000. Do not use IDF flash_args: preserve the signed recovery loader.')
