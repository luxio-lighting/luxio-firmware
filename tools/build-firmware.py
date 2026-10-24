#!/usr/bin/env python3
"""Compile and package isolated chip builds using the pinned Arduino SDK."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
os.chdir(root)
matrix = json.loads((root / 'targets.json').read_text())
parser = argparse.ArgumentParser()
parser.add_argument('chip', nargs='?', default='esp32s3', choices=['all'] + [t['chip'] for t in matrix])
args = parser.parse_args()
cli = os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli') or '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
assert (root / '.sdk/arduino-cli.yaml').exists(), 'Run tools/bootstrap-firmware.sh first'
ctags = root / '.sdk/ctags'
ctags.mkdir(parents=True, exist_ok=True)
(ctags / 'ctags').write_text('#!/bin/sh\nexit 0\n')
(ctags / 'ctags').chmod(0o755)
for name in ['partitions.csv', 'src/update_job.h']:
    shutil.copyfile(root / 'Luxio' / name, root / 'Recovery' / name)
failed = []
for target in matrix:
    chip = target['chip']
    if args.chip not in ['all', chip]:
        continue
    matter = os.environ.get('LUXIO_MATTER', '1')
    assert matter in ['0', '1'], 'Invalid LUXIO_MATTER'
    directory = root / 'build' / chip
    directory.mkdir(parents=True, exist_ok=True)
    # A failed rebuild must never leave an apparently current release behind.
    shutil.rmtree(directory / 'release', ignore_errors=True)
    build = os.environ.get('LUXIO_BUILD', '30')
    assert build.isdigit() and 0 < int(build) <= 2147483647, 'Invalid LUXIO_BUILD'
    print(f'Building {chip} ({target["architecture"]}), build {build}', flush=True)
    try:
        subprocess.run([sys.executable, 'tools/build-tls.py', chip], check=True)
        sdk = root / '.sdk/arduino/packages/esp32/tools/esp32-arduino-libs/idf-release_v5.5-f1a1df9b-v3' / chip
        for sketch, folder, capacity in [('Luxio', 'main', target['applicationSize']), ('Recovery', 'recovery', target['recoverySize'])]:
            source = directory / 'sources' / sketch
            shutil.rmtree(source, ignore_errors=True)
            shutil.copytree(root / sketch, source)
            (source / 'partitions.csv').write_text(f"# Name,Type,SubType,Offset,Size,Flags\nnvs,data,nvs,0x9000,0x5000,\notadata,data,ota,0xe000,0x2000,\nloader,app,factory,0x10000,{target['recoverySize']:#x},\napp0,app,ota_0,{target['applicationAddress']:#x},{target['applicationSize']:#x},\nspiffs,data,spiffs,{target['minimumFlashSize']-0x50000:#x},0x40000,\ncoredump,data,coredump,{target['minimumFlashSize']-0x10000:#x},0x10000,\n")
            flags = f'-include {directory}/tls/tls-config.h -DLUA_USE_C89 -DPB_ENABLE_MALLOC -fstack-usage -I{root}/Luxio/src/vendor/nanopb'
            if sketch == 'Luxio' and chip == 'esp32' and matter == '0':
                library = root / '.sdk/user/libraries/NimBLE-Arduino/library.properties'
                assert library.exists() and 'version=2.5.1' in library.read_text(), 'Run tools/bootstrap-firmware.sh to install NimBLE-Arduino 2.5.1'
                flags += ' -DCONFIG_BT_NIMBLE_SM_SC=1 -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0 -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0 -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1'
            cpp = flags + f' -DLUXIO_BUILD={build} -DLUXIO_APP_CAPACITY={target["applicationSize"]} -DLUXIO_APP_OFFSET={target["applicationAddress"]} -DLUXIO_LOADER_CAPACITY={target["recoverySize"]} -DLUXIO_DEFAULT_GPIO={target["defaultGpio"]} -DLUXIO_MATTER={matter} -DLUXIO_CLOUD_URL="{os.environ.get("LUXIO_CLOUD_URL", "https://api.luxio.lighting")}"'
            command = [cli, '--config-file', str(root / '.sdk/arduino-cli.yaml'), 'compile',
                       '--fqbn', target['fqbn'], '--jobs', os.environ.get('LUXIO_JOBS', '2'),
                       '--build-path', str(directory / folder)]
            if sketch == 'Luxio' and matter == '1':
                cpp += ' -DLUXIO_NETWORK_ONLY_MATTER=1'
            libs = (sdk / 'flags/ld_libs').read_text()
            assert '-lmbedtls_2' in libs
            libs = libs.replace('-lmbedtls_2', str(directory / 'tls/libmbedtls_2.a'))
            if sketch == 'Luxio' and chip != 'esp32' and matter == '1':
                # Do not alter the installed SDK or another target's archive.
                # The pinned SDK has no API to relinquish its BLE host without
                # releasing controller RAM. Replace its lifecycle entry points
                # with matter_network_only.cpp, retaining the original class ABI.
                sdk = root / '.sdk/arduino/packages/esp32/tools/esp32-arduino-libs/idf-release_v5.5-f1a1df9b-v3' / chip
                prefix = 'xtensa-esp-elf' if target['architecture'] == 'xtensa' else 'riscv32-esp-elf'
                binaries = list((root / '.sdk/arduino/packages/esp32/tools').glob(f'*/2507/bin/{prefix}-objcopy'))
                assert len(binaries) == 1, 'Pinned ESP32 toolchain is required'
                objcopy = binaries[0]
                nm = objcopy.with_name(prefix + '-nm')
                archive = directory / 'libmatter-network-only.a'
                original = sdk / 'lib/libespressif__esp_matter.a'
                symbols = subprocess.check_output([str(nm), '--defined-only', str(original)], text=True)
                overrides = ['_Init', '_Shutdown', '_OnPlatformEvent', '_SetAdvertisingEnabled', '_SetAdvertisingMode']
                selected = []
                for method in overrides:
                    matches = [line.split()[-1] for line in symbols.splitlines() if ' T ' in line and f'14BLEManagerImpl{len(method)}{method}' in line]
                    assert len(matches) == 1, f'Unsupported SDK BLE ABI: {method}: {matches}'
                    selected += ['--weaken-symbol', matches[0]]
                subprocess.run([str(objcopy), *selected, str(original), str(archive)], check=True)
                assert '-lespressif__esp_matter' in libs
                libs = libs.replace('-lespressif__esp_matter', str(archive))
            link_libraries = directory / f'{folder}-libs'
            link_libraries.write_text(libs)
            command += ['--build-property', f'compiler.c.elf.libs=@{link_libraries}']
            for prop in [f'upload.maximum_size={capacity}', f'runtime.tools.ctags.path={ctags}',
                         f'tools.ctags.path={ctags}', f'compiler.c.extra_flags={flags}', f'compiler.cpp.extra_flags={cpp}']:
                command += ['--build-property', prop]
            subprocess.run(command + [str(source)], check=True)
            assert (directory / folder / f'{sketch}.ino.bin').stat().st_size <= capacity, f'{chip} {sketch} exceeds partition'
            (directory / folder / f'{sketch}.ino.merged.bin').unlink(missing_ok=True)
        subprocess.run([sys.executable, 'tools/check-task-stacks.py', str(directory / 'main')], check=True)
        subprocess.run([sys.executable, 'tools/package-firmware.py', chip], check=True)
    except (subprocess.CalledProcessError, AssertionError) as error:
        failed.append(chip)
        print(f'FAILED {chip}: {error}', file=sys.stderr, flush=True)
if failed:
    sys.exit('Failed targets: ' + ', '.join(failed))
