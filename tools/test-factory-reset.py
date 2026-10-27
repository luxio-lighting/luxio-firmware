#!/usr/bin/env python3
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parent.parent
source = (root / 'Luxio/src/main.cpp').read_text()
start = source.index('  // Complete a confirmed reset')
boot = source[start:source.index('  Serial.begin', start)]
start = source.index('    case luxio_Operation_FACTORY_RESET:')
command = source[start:source.index('    case luxio_Operation_GET_STATE:', start)]
start = source.index('  if (error.length()) strlcpy(response->error', source.index('size_t dispatch('))
reply = source[start:source.index('  size_t size = encodeControlResponse', start)]
fixture = (root / 'tools/test-factory-reset.cpp').read_text().replace('// PRODUCTION_BOOT', boot).replace('// PRODUCTION_COMMAND', command).replace('// PRODUCTION_REPLY', reply)
folder = root / '.sdk/factory-reset'; folder.mkdir(parents=True, exist_ok=True)
cpp = folder / 'test.cpp'; cpp.write_text(fixture)
binary = folder / 'test'
subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', '-I', str(root / 'Luxio/src'), '-I', str(root / 'Luxio/src/vendor/nanopb'), str(cpp), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
