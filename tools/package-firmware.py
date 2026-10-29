#!/usr/bin/env python3
"""Package exact flash parts; never merge blank padding over user NVS/scripts."""
import hashlib
import os
from urllib.parse import urlparse
import json
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
matrix = json.loads((root / 'targets.json').read_text())
identifier = sys.argv[1] if len(sys.argv) > 1 else 'esp32s3'
target = next(t for t in matrix if t['id'] == identifier)
assert target['minimumFlashSize'] in [0x400000, 0x800000], 'Unsupported flash size'
chip = target['chip']
directory = root / os.environ.get('LUXIO_BUILD_ROOT', 'build') / target['id']
app = directory / 'main/Luxio.ino.bin'
loader = directory / 'recovery/Recovery.ino.bin'
raw = app.read_bytes()
assert len(raw) >= 288 and raw[0] == 0xe9 and raw[80:112].rstrip(b'\0') == b'Luxio'
for path in [app, loader, directory / 'main/Luxio.ino.bootloader.bin', directory / 'recovery/Recovery.ino.bootloader.bin']:
    image = path.read_bytes()
    assert len(image) >= 24 and image[0] == 0xe9 and struct.unpack_from('<H', image, 12)[0] == target['imageChipId'], f'Wrong chip image: {path}'
assert (directory / 'main/Luxio.ino.bootloader.bin').read_bytes() == (directory / 'recovery/Recovery.ino.bootloader.bin').read_bytes(), 'Bootloaders differ'
build = int(raw[48:80].rstrip(b'\0'))
recovery = loader.read_bytes()
assert recovery[80:112].rstrip(b'\0') == b'Recovery' and recovery[48:80].rstrip(b'\0') == str(build).encode(), 'Recovery identity/build must match the application package'
assert 0 < len(raw) <= target['applicationSize']
assert 0 < loader.stat().st_size <= target['recoverySize']
assert (directory/'main/Luxio.ino.partitions.bin').read_bytes() == (directory/'recovery/Recovery.ino.partitions.bin').read_bytes()
partitions = (directory/'main/Luxio.ino.partitions.bin').read_bytes()
applications = []
for offset in range(0, len(partitions) - 31, 32):
    magic, kind, subtype, address, size, label, flags = struct.unpack_from('<HBBII16sI', partitions, offset)
    if magic != 0x50aa:
        break
    assert address + size <= target['minimumFlashSize'], 'Partition exceeds target flash'
    if kind == 0:
        applications.append((subtype, address, size, label.rstrip(b'\0')))
assert applications == [(0, 0x10000, target['recoverySize'], b'loader'), (0x10, target['applicationAddress'], target['applicationSize'], b'app0')], 'Only the fixed recovery loader and single main application layout can be packaged'
output = directory/'release'
output.mkdir(parents=True, exist_ok=True)
parts = []
for path, name, address, capacity in [
    (directory/'main/Luxio.ino.bootloader.bin', 'bootloader.bin', target['bootloaderAddress'], 0x8000 - target['bootloaderAddress']),
    (directory/'main/Luxio.ino.partitions.bin', 'partitions.bin', 0x8000, 0x1000),
    (loader, 'recovery.bin', 0x10000, target['recoverySize']),
    (app, 'luxio.bin', target['applicationAddress'], target['applicationSize']),
]:
    data = path.read_bytes()
    assert len(data) <= capacity
    (output/name).write_bytes(data)
    parts.append(dict(file=name, address=address, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
# Start USB installations in the recovery loader, which validates the main app.
# This touches only otadata, never NVS or the filesystem.
blank = bytes([255])*0x2000
(output/'otadata.bin').write_bytes(blank)
parts.append(dict(file='otadata.bin', address=0xe000, size=len(blank), sha256=hashlib.sha256(blank).hexdigest()))
manifest = dict(build=build, chip=chip, target=target['target'], channel=100, minimumFlashSize=target['minimumFlashSize'],
                size=len(raw), sha256=hashlib.sha256(raw).hexdigest(), file='luxio.bin', parts=parts)
(output/'manifest.local.json').write_text(json.dumps(manifest, indent=2)+'\n')
base = os.environ.get('LUXIO_FIRMWARE_BASE_URL')
if base:
    assert urlparse(base).scheme == 'https' and urlparse(base).netloc and not urlparse(base).query and not urlparse(base).fragment, 'Use an HTTPS directory URL'
    hosted = {key: value for key, value in manifest.items() if key not in ['file', 'parts']}
    hosted['url'] = f"{base.rstrip('/')}/{target['id']}/luxio.bin"
    hosted['parts'] = [{**{key: value for key, value in part.items() if key != 'file'}, 'url': f"{base.rstrip('/')}/{target['id']}/{part['file']}"} for part in parts]
    (output / 'manifest.json').write_text(json.dumps(hosted, indent=2) + '\n')
print(f'Build {build}: application {len(raw):,} / {target["applicationSize"]:,}; recovery {loader.stat().st_size:,} / {target["recoverySize"]:,} bytes')
