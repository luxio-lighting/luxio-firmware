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
    directory = root / 'build' / chip
    directory.mkdir(parents=True, exist_ok=True)
    # A failed rebuild must never leave an apparently current release behind.
    shutil.rmtree(directory / 'release', ignore_errors=True)
    build = os.environ.get('LUXIO_BUILD', '23')
    assert build.isdigit() and 0 < int(build) <= 2147483647, 'Invalid LUXIO_BUILD'
    print(f'Building {chip} ({target["architecture"]}), build {build}', flush=True)
    try:
        for sketch, folder, capacity in [('Luxio', 'main', target['applicationSize']), ('Recovery', 'recovery', target['recoverySize'])]:
            source = directory / 'sources' / sketch
            shutil.rmtree(source, ignore_errors=True)
            shutil.copytree(root / sketch, source)
            (source / 'partitions.csv').write_text(f"# Name,Type,SubType,Offset,Size,Flags\nnvs,data,nvs,0x9000,0x5000,\notadata,data,ota,0xe000,0x2000,\nloader,app,factory,0x10000,{target['recoverySize']:#x},\napp0,app,ota_0,{target['applicationAddress']:#x},{target['applicationSize']:#x},\nspiffs,data,spiffs,{target['minimumFlashSize']-0x50000:#x},0x40000,\ncoredump,data,coredump,{target['minimumFlashSize']-0x10000:#x},0x10000,\n")
            flags = f'-DLUA_USE_C89 -DPB_ENABLE_MALLOC -fstack-usage -I{root}/Luxio/src/vendor/nanopb'
            cpp = flags + f' -DLUXIO_BUILD={build} -DLUXIO_APP_CAPACITY={target["applicationSize"]} -DLUXIO_APP_OFFSET={target["applicationAddress"]} -DLUXIO_LOADER_CAPACITY={target["recoverySize"]} -DLUXIO_DEFAULT_GPIO={target["defaultGpio"]} -DLUXIO_MATTER={os.environ.get("LUXIO_MATTER", "1")} -DLUXIO_CLOUD_URL="{os.environ.get("LUXIO_CLOUD_URL", "https://api.luxio.lighting")}"'
            command = [cli, '--config-file', str(root / '.sdk/arduino-cli.yaml'), 'compile',
                       '--fqbn', target['fqbn'], '--jobs', os.environ.get('LUXIO_JOBS', '2'),
                       '--build-path', str(directory / folder)]
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
