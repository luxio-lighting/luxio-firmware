#!/usr/bin/env python3
"""Exercise persisted OTA jobs against every compiled chip/layout contract."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
os.chdir(root)
(root / '.sdk').mkdir(exist_ok=True)
for target in json.loads((root / 'targets.json').read_text()):
    chip = target['chip']
    binary = root / '.sdk' / f'test-update-job-{chip}'
    subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                    f'-DCONFIG_IDF_TARGET="{chip}"',
                    f'-DLUXIO_APP_CAPACITY={target["applicationSize"]}',
                    f'-DLUXIO_APP_OFFSET={target["applicationAddress"]}',
                    f'-DLUXIO_LOADER_CAPACITY={target["recoverySize"]}',
                    'tools/test-update-job.cpp', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print(f'PASS: {chip} recovery target and application capacity')
