#!/usr/bin/env python3
"""Build the pinned, Wi-Fi-only Matter C5/C6 SDK and stage normal release artifacts."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
hardware = next(t for t in json.loads((root / 'targets.json').read_text()) if t['id'] == sys.argv[1])
assert hardware['chip'] in ['esp32c5', 'esp32c6']
build = int(os.environ.get('LUXIO_BUILD', '37'))
assert 0 < build <= 2147483647
destination = root / os.environ.get('LUXIO_BUILD_ROOT', 'build') / hardware['id']
project = destination / 'sdk'
project.mkdir(parents=True, exist_ok=True)
shutil.rmtree(project / 'main', ignore_errors=True)
for path in (root / 'idf' / hardware['id']).rglob('*'):
    if path.is_file():
        target = project / path.relative_to(root / 'idf' / hardware['id'])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
for source, target in [(root / 'Luxio/src', project / 'main/luxio'),
                       (root / '.sdk/arduino/packages/esp32/hardware/esp32/3.3.3', project / 'components/arduino')]:
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target, ignore=shutil.ignore_patterns('.git', 'docs', 'tools', 'examples', 'tests'))
shutil.copyfile(destination / 'sources/Luxio/partitions.csv', project / 'partitions.csv')
(project / 'sdkconfig').unlink(missing_ok=True)
cache = root / '.sdk/compact-ccache'
cache.mkdir(parents=True, exist_ok=True)
subprocess.run(['docker', 'run', '--rm', '-v', f'{project}:/project', '-v', f'{cache}:/root/.cache/ccache', '-w', '/project',
    'espressif/idf@sha256:dfa2d076c796769c07c155eba6c672b9f395aec943b2ba3701b73379b5f9e884',
    'idf.py', f'-DIDF_TARGET={hardware["chip"]}', f'-DLUXIO_BUILD={build}', 'build'], check=True)
config = (project / 'sdkconfig').read_text().splitlines()
for setting in ['# CONFIG_OPENTHREAD_ENABLED is not set', 'CONFIG_ENABLE_WIFI_STATION=y',
                '# CONFIG_ENABLE_CHIPOBLE is not set', 'CONFIG_BT_NIMBLE_ENABLED=y',
                'CONFIG_BT_NIMBLE_SECURITY_ENABLE=y', 'CONFIG_BT_NIMBLE_SM_SC=y',
                'CONFIG_BT_NIMBLE_NVS_PERSIST=y', 'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y']:
    assert setting in config, f'Unsafe or unexpected SDK configuration: {setting}'
output = destination / 'main'
output.mkdir(exist_ok=True)
for source, filename in [('Luxio.bin', 'Luxio.ino.bin'), ('bootloader/bootloader.bin', 'Luxio.ino.bootloader.bin'), ('partition_table/partition-table.bin', 'Luxio.ino.partitions.bin')]:
    shutil.copyfile(project / 'build' / source, output / filename)
assert (output / 'Luxio.ino.bin').stat().st_size <= hardware['applicationSize']
subprocess.run([sys.executable, str(root / 'tools/check-task-stacks.py'), str(project / 'build')], check=True)
