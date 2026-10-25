#!/usr/bin/env python3
"""Verify per-chip image identity, flash offsets and the fixed recovery layout."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
(root / '.sdk').mkdir(exist_ok=True)
matrix = json.loads((root / 'targets.json').read_text())
with tempfile.TemporaryDirectory(prefix='package-test-', dir=root / '.sdk') as directory:
    fixture = Path(directory)
    (fixture / 'tools').mkdir()
    shutil.copy(root / 'targets.json', fixture / 'targets.json')
    shutil.copy(root / 'tools/package-firmware.py', fixture / 'tools/package-firmware.py')
    for target in matrix:
        assert target['minimumFlashSize'] == 0x400000, 'Every supported board must fit 4 MB flash'
        assert target['applicationAddress'] + target['applicationSize'] == 0x3b0000
        chip = target['chip']
        build = fixture / 'build' / target['id']
        (build / 'main').mkdir(parents=True)
        (build / 'recovery').mkdir()
        app = bytearray(288)
        app[0] = 0xe9
        struct.pack_into('<H', app, 12, target['imageChipId'])
        app[48:49] = b'1'
        app[80:85] = b'Luxio'
        recovery = bytearray(app)
        recovery[80:112] = b'Recovery'.ljust(32, b'\0')
        boot = app[:24] + bytes(40)
        valid = [(0, 0x10000, target['recoverySize'], b'loader'), (0x10, target['applicationAddress'], target['applicationSize'], b'app0')]
        for name, applications, wrong_image, succeeds in [
            ('recovery', valid, None, True),
            ('two application slots', [(0x10, 0x10000, 0x1d0000, b'app0'), (0x11, 0x1e0000, 0x1d0000, b'app1')], None, False),
            ('additional slot', valid + [(0x11, 0x400000, 0x280000, b'app1')], None, False),
            ('missing loader', valid[1:], None, False),
            ('wrong application offset', [valid[0], (0x10, 0x140000, 0x280000, b'app0')], None, False),
            ('wrong application capacity', [valid[0], (0x10, 0x130000, 0x270000, b'app0')], None, False),
            ('foreign main image', valid, 'main/Luxio.ino.bin', False),
            ('foreign recovery image', valid, 'recovery/Recovery.ino.bin', False),
            ('generic loader descriptor', valid, None, False),
            ('mismatched loader build', valid, None, False),
            ('foreign bootloader', valid, 'main/Luxio.ino.bootloader.bin', False),
        ]:
            for path, content in [('main/Luxio.ino.bin', app), ('main/Luxio.ino.bootloader.bin', boot), ('recovery/Recovery.ino.bootloader.bin', boot), ('recovery/Recovery.ino.bin', recovery)]:
                data = bytearray(content)
                if path == 'recovery/Recovery.ino.bin' and name == 'generic loader descriptor': data[80:112] = b'arduino-lib-builder'.ljust(32, b'\0')
                if path == 'recovery/Recovery.ino.bin' and name == 'mismatched loader build': data[48:80] = b'2'.ljust(32, b'\0')
                if path == wrong_image:
                    struct.pack_into('<H', data, 12, 99)
                (build / path).write_bytes(data)
            table = b''.join(struct.pack('<HBBII16sI', 0x50aa, 0, subtype, address, size, label, 0) for subtype, address, size, label in applications) + bytes([255]) * 32
            (build / 'main/Luxio.ino.partitions.bin').write_bytes(table)
            (build / 'recovery/Recovery.ino.partitions.bin').write_bytes(table)
            result = subprocess.run([sys.executable, str(fixture / 'tools/package-firmware.py'), target['id']], capture_output=True, text=True, env={**os.environ, 'LUXIO_FIRMWARE_BASE_URL': 'https://firmware.example/build-1'})
            assert (result.returncode == 0) == succeeds, (chip, name, result.stdout, result.stderr)
            if succeeds:
                manifest = json.loads((build / 'release/manifest.local.json').read_text())
                assert manifest['chip'] == chip and manifest['target'] == target['target']
                assert manifest['sha256'] == hashlib.sha256(app).hexdigest()
                assert sorted(part['address'] for part in manifest['parts']) == [target['bootloaderAddress'], 0x8000, 0xe000, 0x10000, target['applicationAddress']]
                assert manifest['minimumFlashSize'] == target['minimumFlashSize']
                hosted = json.loads((build / 'release/manifest.json').read_text())
                assert hosted['url'] == f'https://firmware.example/build-1/{target['id']}/luxio.bin'
                assert 'file' not in hosted and all('file' not in part and part['url'].startswith(f'https://firmware.example/build-1/{target['id']}/') for part in hosted['parts'])
            print(f'PASS: {chip}: {name}')
