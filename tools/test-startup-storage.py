#!/usr/bin/env python3
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parent.parent
source = (root / 'Luxio/src/main.cpp').read_text()
save = source[source.index('static bool saveUpdateState()'):source.index('static bool listScripts(')]
start = source.index('  if (!LittleFS.begin(true)) abort();', source.index('void setup()'))
load = source[start:source.index('  // Size all three frames', start)]
cleanup_start = source.index('  prefs.remove("ota-job");', source.index('void setup()'))
cleanup = source[cleanup_start:source.index('\n}', cleanup_start)]
capabilities = source[source.index('static const char supportedStripTypes'):source.index('static int hardwareRandom')]
fixture = capabilities + (root / 'tools/test-startup-storage.cpp').read_text().replace('// PRODUCTION_SAVE', save).replace('// PRODUCTION_LOAD', load).replace('// PRODUCTION_FINISH_BOOT', cleanup)
folder = root / '.sdk/startup-storage'; folder.mkdir(parents=True, exist_ok=True)
cpp = folder / 'test.cpp'; cpp.write_text(fixture)
objects = []
for path in ['Luxio/src/luxio.pb.c', 'Luxio/src/vendor/nanopb/pb_common.c', 'Luxio/src/vendor/nanopb/pb_encode.c', 'Luxio/src/vendor/nanopb/pb_decode.c']:
    obj = folder / (Path(path).name + '.o'); objects.append(str(obj))
    subprocess.run(['cc', '-DPB_ENABLE_MALLOC', '-fsanitize=address,undefined', '-I', str(root / 'Luxio/src/vendor/nanopb'), '-c', str(root / path), '-o', str(obj)], check=True)
binary = folder / 'test'
subprocess.run(['c++', '-std=c++17', '-DPB_ENABLE_MALLOC', '-fsanitize=address,undefined', '-I', str(root / 'Luxio/src'), '-I', str(root / 'Luxio/src/vendor/nanopb'), str(cpp), *objects, '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
