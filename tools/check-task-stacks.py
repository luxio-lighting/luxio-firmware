#!/usr/bin/env python3
"""Check compiled ESP32-S3 stack frames before packaging firmware."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent.parent
objdump = sorted((root / '.sdk/arduino/packages/esp32/tools/esp-x32').glob('*/bin/xtensa-esp32s3-elf-objdump'))[-1]
assembly = subprocess.check_output([str(objdump), '-d', '-C', str(root / 'build/main/Luxio.ino.elf')], text=True)
frames = {}
for function in ['loop()', 'dispatch(unsigned char const*, unsigned int, unsigned char*)', 'startAnimation(_luxio_Script const&)', 'cloudTask(void*)']:
    match = re.search(r'^[0-9a-f]+ <' + re.escape(function) + r'>:\n[^\n]*entry\s+a1,\s+(0x[0-9a-f]+|\d+)', assembly, re.MULTILINE)
    if not match:
        raise SystemExit(f'Cannot determine stack frame for {function}')
    frames[function] = int(match.group(1), 0)
# Leave at least half of the 8 KiB control task stack for Lua/codec/library calls.
control = sum(value for name, value in frames.items() if name != 'cloudTask(void*)')
if control > 4096 or frames['cloudTask(void*)'] > 3072:
    raise SystemExit(f'Task stack budget exceeded: {frames}')
print(f'PASS: control stack frames {control}/4096 bytes; cloud frame {frames["cloudTask(void*)"]}/3072 bytes')
