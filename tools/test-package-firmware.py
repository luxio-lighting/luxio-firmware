#!/usr/bin/env python3
"""Verify packaging accepts only the fixed recovery layout, without flashing."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
(root / '.sdk').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='package-test-', dir=root / '.sdk') as directory:
    fixture = Path(directory)
    (fixture / 'tools').mkdir()
    shutil.copy(root / 'tools/package-firmware.py', fixture / 'tools/package-firmware.py')
    (fixture / 'build/main').mkdir(parents=True)
    (fixture / 'build/recovery').mkdir()
    app = bytearray(288)
    app[0] = 0xe9
    app[48:49] = b'1'
    app[80:85] = b'Luxio'
    (fixture / 'build/main/Luxio.ino.bin').write_bytes(app)
    (fixture / 'build/main/Luxio.ino.bootloader.bin').write_bytes(bytes(64))
    (fixture / 'build/recovery/Recovery.ino.bin').write_bytes(bytes(64))
    valid = [(0, 0x10000, 0x120000, b'loader'), (0x10, 0x130000, 0x280000, b'app0')]
    for name, applications, succeeds in [
        ('recovery', valid, True),
        ('two application slots', [(0x10, 0x10000, 0x1d0000, b'app0'), (0x11, 0x1e0000, 0x1d0000, b'app1')], False),
        ('additional slot', valid + [(0x11, 0x400000, 0x280000, b'app1')], False),
        ('missing loader', valid[1:], False),
        ('wrong application offset', [valid[0], (0x10, 0x140000, 0x280000, b'app0')], False),
        ('wrong application capacity', [valid[0], (0x10, 0x130000, 0x270000, b'app0')], False),
    ]:
        table = b''.join(struct.pack('<HBBII16sI', 0x50aa, 0, subtype, address, size, label, 0) for subtype, address, size, label in applications) + bytes([255]) * 32
        (fixture / 'build/main/Luxio.ino.partitions.bin').write_bytes(table)
        (fixture / 'build/recovery/Recovery.ino.partitions.bin').write_bytes(table)
        result = subprocess.run([sys.executable, str(fixture / 'tools/package-firmware.py')], capture_output=True, text=True)
        assert (result.returncode == 0) == succeeds, (name, result.stdout, result.stderr)
        if succeeds:
            manifest = json.loads((fixture / 'build/release/manifest.local.json').read_text())
            assert manifest['target'] == 'esp32s3-recovery-v1'
            assert manifest['sha256'] == hashlib.sha256(app).hexdigest()
            assert sorted(part['address'] for part in manifest['parts']) == [0, 0x8000, 0xe000, 0x10000, 0x130000]
        else:
            assert 'Only the fixed recovery loader' in result.stderr, result.stderr
        print(f'PASS: {name}')
