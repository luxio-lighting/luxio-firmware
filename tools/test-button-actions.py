#!/usr/bin/env python3
"""Run the production physical-button control path against an in-memory filesystem."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parent.parent
source = (root / 'Luxio/src/main.cpp').read_text()
start = source.index('  // Physical controls use')
body = source[start:source.index('  static std::unique_ptr<uint8_t[]> input;', start)]
fixture = (root / 'tools/test-button-actions.cpp').read_text().replace('// PRODUCTION_BUTTON', body)
folder = root / '.sdk/button-actions'; folder.mkdir(parents=True, exist_ok=True)
cpp = folder / 'test.cpp'; cpp.write_text(fixture)
subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', '-I', str(root / 'Luxio/src'), '-I', str(root / 'Luxio/src/vendor/nanopb'), str(cpp), '-o', str(folder / 'test')], check=True)
subprocess.run([str(folder / 'test')], check=True)
