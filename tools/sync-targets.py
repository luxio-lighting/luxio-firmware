#!/usr/bin/env python3
"""Explicitly refresh standalone Setup/API snapshots of the firmware matrix."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
source = (root / 'targets.json').read_bytes()
for name in ['setup.luxio.lighting', 'api.luxio.lighting', 'app.luxio.lighting']:
    destination = root.parent / name / 'shared/firmware/targets.json'
    if '--check' in sys.argv:
        assert destination.read_bytes() == source, f'Stale target matrix: {destination}'
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(source)
