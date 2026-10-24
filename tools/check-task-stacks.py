#!/usr/bin/env python3
"""Check compiler stack-usage reports on both Xtensa and RISC-V."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
folder = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/esp32s3/main'
frames = {}
for report in folder.rglob('main.cpp.su'):
    for line in report.read_text().splitlines():
        signature, size, kind = line.rsplit('\t', 2)
        for name in ['loop()', 'dispatch(', 'startAnimation(', 'cloudTask(']:
            if name in signature:
                assert kind in ['static', 'dynamic,bounded'], f'Unbounded stack: {line}'
                frames[name] = max(frames.get(name, 0), int(size))
assert len(frames) == 4, f'Missing compiler stack reports: {frames}'
control = sum(value for name, value in frames.items() if name != 'cloudTask(')
assert control <= 4096 and frames['cloudTask('] <= 3072, f'Task stack budget exceeded: {frames}'
print(f'PASS: stack frames {frames}; control {control}/4096 bytes')
