#!/usr/bin/env python3
"""Package exact flash parts; never merge blank padding over user NVS/scripts."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
app = root / 'build/main/Luxio.ino.bin'
loader = root / 'build/recovery/Recovery.ino.bin'
raw = app.read_bytes()
assert raw[0] == 0xe9 and raw[80:112].rstrip(b'\0') == b'Luxio'
build = int(raw[48:80].rstrip(b'\0'))
assert 0 < len(raw) <= 0x280000
assert 0 < loader.stat().st_size <= 0x120000
assert (root/'build/main/Luxio.ino.partitions.bin').read_bytes() == (root/'build/recovery/Recovery.ino.partitions.bin').read_bytes()
output = root/'build/release'
output.mkdir(parents=True, exist_ok=True)
parts = []
for path, name, address, capacity in [
    (root/'build/main/Luxio.ino.bootloader.bin', 'bootloader.bin', 0, 0x8000),
    (root/'build/main/Luxio.ino.partitions.bin', 'partitions.bin', 0x8000, 0x1000),
    (loader, 'recovery.bin', 0x10000, 0x120000),
    (app, 'luxio.bin', 0x130000, 0x280000),
]:
    data = path.read_bytes()
    assert len(data) <= capacity
    (output/name).write_bytes(data)
    parts.append(dict(file=name, address=address, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
# Explicitly reset boot selection to factory for a USB layout migration.
# This touches only otadata, never NVS or the filesystem.
blank = bytes([255])*0x2000
(output/'otadata.bin').write_bytes(blank)
parts.append(dict(file='otadata.bin', address=0xe000, size=len(blank), sha256=hashlib.sha256(blank).hexdigest()))
manifest = dict(build=build, chip='esp32s3', target='esp32s3-recovery-v1', channel=100,
                size=len(raw), sha256=hashlib.sha256(raw).hexdigest(), file='luxio.bin', parts=parts)
(output/'manifest.local.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(f'Build {build}: Matter app {len(raw):,} / {0x280000:,}; recovery {loader.stat().st_size:,} / {0x120000:,} bytes')
