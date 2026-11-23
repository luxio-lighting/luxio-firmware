#!/usr/bin/env python3
"""Compile and package isolated chip builds using the pinned Arduino SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
os.chdir(root)
matrix = json.loads((root / 'targets.json').read_text())
parser = argparse.ArgumentParser()
parser.add_argument('chip', nargs='?', default='esp32s3', choices=['all'] + [t['id'] for t in matrix])
args = parser.parse_args()
cli = os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli') or '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
ctags = root / '.sdk/ctags'
ctags.mkdir(parents=True, exist_ok=True)
(ctags / 'ctags').write_text('#!/bin/sh\nexit 0\n')
(ctags / 'ctags').chmod(0o755)
for name in ['partitions.csv', 'src/update_job.h', 'src/signed_update.h', 'src/ota_public_key.h']:
    shutil.copyfile(root / 'Luxio' / name, root / 'Recovery' / name)
failed = []
for target in matrix:
    chip = target['chip']
    if args.chip not in ['all', target['id']]:
        continue
    matter = os.environ.get('LUXIO_MATTER', '1')
    assert matter in ['0', '1'], 'Invalid LUXIO_MATTER'
    directory = root / os.environ.get('LUXIO_BUILD_ROOT', 'build') / target['id']
    directory.mkdir(parents=True, exist_ok=True)
    # A failed rebuild must never leave an apparently current release behind.
    shutil.rmtree(directory / 'release', ignore_errors=True)
    build = os.environ.get('LUXIO_BUILD', '44')
    assert build.isdigit() and 0 < int(build) <= 2147483647, 'Invalid LUXIO_BUILD'
    print(f'Building {chip} ({target["architecture"]}), build {build}', flush=True)
    try:
        sdk_base = root / ('.sdk/s31' if chip == 'esp32s31' else '.sdk')
        assert (sdk_base / 'arduino-cli.yaml').exists(), f'Run tools/bootstrap-{"s31.py" if chip == "esp32s31" else "firmware.sh"} first'
        if chip != 'esp32s31':
            # Backport the upstream native USB TX fix without upgrading the SDK.
            core = sdk_base / 'arduino/packages/esp32/hardware/esp32/3.3.3'
            assert core.resolve().is_relative_to(sdk_base.resolve()), 'Refusing to patch a shared Arduino SDK'
            assert 'version=3.3.3' in (core / 'platform.txt').read_text().splitlines(), 'Pinned Arduino ESP32 3.3.3 is required'
            hwcdc = core / 'cores/esp32/HWCDC.cpp'
            patch = root / 'tools/patches/arduino-esp32-3.3.3-hwcdc-2ecad233.patch'
            assert hashlib.sha256(patch.read_bytes()).hexdigest() == 'bd9af333f3ba748aed4fe8c017848b1f9961a5a04bbb3bc2d89abe37c9fcfc2a', 'Unexpected HWCDC backport patch'
            original_hash = '8238d208409ac8a9e74e5960360a5e59c4f2f85dc2339fd96442a62297d5ff45'
            patched_hash = 'afeebf0de83416b1c899b0037b6c82cbc582709627aee93a22179eac3745be81'
            installed_hash = hashlib.sha256(hwcdc.read_bytes()).hexdigest()
            assert installed_hash in (original_hash, patched_hash), 'Unknown HWCDC.cpp; reinstall the pinned SDK before building'
            if installed_hash == original_hash:
                with tempfile.TemporaryDirectory(prefix='luxio-hwcdc-') as temporary:
                    staged = Path(temporary) / 'cores/esp32/HWCDC.cpp'
                    staged.parent.mkdir(parents=True)
                    staged.write_bytes(hwcdc.read_bytes())
                    subprocess.run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '--directory', temporary, '--input', str(patch)], check=True)
                    assert hashlib.sha256(staged.read_bytes()).hexdigest() == patched_hash, 'Unexpected HWCDC backport result'
                    hwcdc.write_bytes(staged.read_bytes())
                print('Applied upstream HWCDC TX fix 2ecad233 to Arduino ESP32 3.3.3', flush=True)
        sdk_version = 'idf-release_v6.1-9a97f6c5-v2' if chip == 'esp32s31' else 'idf-release_v5.5-f1a1df9b-v3'
        sdk = sdk_base / 'arduino/packages/esp32/tools/esp32-arduino-libs' / sdk_version / chip
        for sketch, folder, capacity in [('Luxio', 'main', target['applicationSize']), ('Recovery', 'recovery', target['recoverySize'])]:
            source = directory / 'sources' / sketch
            shutil.rmtree(source, ignore_errors=True)
            shutil.copytree(root / sketch, source)
            (source / 'partitions.csv').write_text(f"# Name,Type,SubType,Offset,Size,Flags\nnvs,data,nvs,0x9000,0x5000,\notadata,data,ota,0xe000,0x2000,\nloader,app,factory,0x10000,{target['recoverySize']:#x},\napp0,app,ota_0,{target['applicationAddress']:#x},{target['applicationSize']:#x},\nspiffs,data,spiffs,{target['minimumFlashSize']-0x50000:#x},0x40000,\ncoredump,data,coredump,{target['minimumFlashSize']-0x10000:#x},0x10000,\n")
            if sketch == 'Luxio' and chip == 'esp32' and matter == '1':
                # Publish the same DMA/Matter/BLE profile qualified on the GLEDOPTO.
                lowram = directory / 'sdk'
                subprocess.run([sys.executable, 'tools/build-lowram.py', '--build', build, '--directory', str(lowram)], check=True)
                (directory / folder).mkdir(parents=True, exist_ok=True)
                shutil.copyfile(lowram / 'build/Luxio.bin', directory / folder / 'Luxio.ino.bin')
                shutil.copyfile(lowram / 'build/partition_table/partition-table.bin', directory / folder / 'Luxio.ino.partitions.bin')
                continue
            if sketch == 'Luxio' and target.get('builder') == 'idf':
                subprocess.run([sys.executable, 'tools/build-compact.py', target['id']], check=True)
                continue
            flags = f'-DLUA_USE_C89 -DPB_ENABLE_MALLOC -fstack-usage -I{root}/Luxio/src/vendor/nanopb'
            if chip != 'esp32s31':
                # Change Arduino's core-cache key when adopting the SDK patch.
                flags += ' -DLUXIO_HWCDC_PATCH_2ECAD233=1'
            if sketch == 'Luxio' and chip == 'esp32' and matter == '0':
                library = root / '.sdk/user/libraries/NimBLE-Arduino/library.properties'
                assert library.exists() and 'version=2.5.1' in library.read_text(), 'Run tools/bootstrap-firmware.sh to install NimBLE-Arduino 2.5.1'
                flags += ' -DCONFIG_BT_NIMBLE_SM_SC=1 -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0 -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0 -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1'
            cpp = flags + f' -DLUXIO_BUILD={build} -DLUXIO_APP_CAPACITY={target["applicationSize"]} -DLUXIO_APP_OFFSET={target["applicationAddress"]} -DLUXIO_LOADER_CAPACITY={target["recoverySize"]} -DLUXIO_DEFAULT_GPIO={target["defaultGpio"]} -DLUXIO_UPDATE_LAYOUT="{target["target"]}" -DLUXIO_MATTER={matter} -DLUXIO_CLOUD_URL="{os.environ.get("LUXIO_CLOUD_URL", "http://api.luxio.lighting")}"'
            command = [cli, '--config-file', str(sdk_base / 'arduino-cli.yaml'), 'compile',
                       '--fqbn', target['fqbn'], '--jobs', os.environ.get('LUXIO_JOBS', '2'),
                       '--build-path', str(directory / folder)]
            if sketch == 'Luxio' and matter == '1':
                cpp += ' -DLUXIO_NETWORK_ONLY_MATTER=1'
            libs = (sdk / 'flags/ld_libs').read_text()
            if sketch == 'Luxio' and chip != 'esp32' and matter == '1':
                # Do not alter the installed SDK or another target's archive.
                # The pinned SDK has no API to relinquish its BLE host without
                # releasing controller RAM. Replace its lifecycle entry points
                # with matter_network_only.cpp, retaining the original class ABI.
                prefix = 'xtensa-esp-elf' if target['architecture'] == 'xtensa' else 'riscv32-esp-elf'
                binaries = list((sdk_base / 'arduino/packages/esp32/tools').glob(f'*/{"esp-15.2.0_20251204" if chip == "esp32s31" else "2507"}/bin/{prefix}-objcopy'))
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
            if chip == 'esp32s31':
                for tool, version in [('esp32-arduino-libs', sdk_version), ('riscv32-esp-elf-gcc', 'esp-15.2.0_20251204'), ('esptool_py', '5.3.1')]:
                    command += ['--build-property', f'tools.{tool}.path={sdk_base}/arduino/packages/esp32/tools/{tool}/{version}']
            command += ['--build-property', f'compiler.c.elf.libs=@{link_libraries}']
            for prop in [f'upload.maximum_size={capacity}', f'runtime.tools.ctags.path={ctags}',
                         f'tools.ctags.path={ctags}', f'compiler.c.extra_flags={flags}', f'compiler.cpp.extra_flags={cpp}']:
                command += ['--build-property', prop]
            subprocess.run(command + [str(source)], check=True)
            assert (directory / folder / f'{sketch}.ino.bin').stat().st_size <= capacity, f'{chip} {sketch} exceeds partition'
            (directory / folder / f'{sketch}.ino.merged.bin').unlink(missing_ok=True)
        if chip == 'esp32' and matter == '1':
            # Keep the existing recovery bootloader, already tested with this app.
            shutil.copyfile(directory / 'recovery/Recovery.ino.bootloader.bin', directory / 'main/Luxio.ino.bootloader.bin')
        elif target.get('builder') == 'idf':
            # Both images use the source-built SDK bootloader and identical table.
            shutil.copyfile(directory / 'main/Luxio.ino.bootloader.bin', directory / 'recovery/Recovery.ino.bootloader.bin')
        else:
            subprocess.run([sys.executable, 'tools/check-task-stacks.py', str(directory / 'main')], check=True)
        subprocess.run([sys.executable, 'tools/package-firmware.py', target['id']], check=True)
    except (subprocess.CalledProcessError, AssertionError) as error:
        failed.append(chip)
        print(f'FAILED {chip}: {error}', file=sys.stderr, flush=True)
if failed:
    sys.exit('Failed targets: ' + ', '.join(failed))
