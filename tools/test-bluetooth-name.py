#!/usr/bin/env python3
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
source = (root / 'Luxio/src/bluetooth.cpp').read_text()
start = source.index('static QueueHandle_t nameQueue;')
declarations = source[start:source.index('static void advertise();', start)]
start = source.index('static void advertise() {')
advertise = source[start:source.index('static int writeRequest', start)]
start = source.index('  ble_svc_gap_init();')
initialize = source[start:source.index('  characteristics[0]', start)]
main = (root / 'Luxio/src/main.cpp').read_text()
assert 'startBluetooth(state.config.name);' in main
assert main.index('prefs.putBytes("config", saved') < main.index('if (nameChanged) setBluetoothName(state.config.name);')
fixture = (root / 'tools/test-bluetooth-name.cpp').read_text().replace('// PRODUCTION_DECLARATIONS', declarations).replace('// PRODUCTION_ADVERTISE', advertise).replace('// PRODUCTION_INITIALIZE', initialize)
folder = root / '.sdk/bluetooth-name'; folder.mkdir(parents=True, exist_ok=True)
cpp = folder / 'test.cpp'; cpp.write_text(fixture)
binary = folder / 'test'
subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', '-I', str(root / 'Luxio/src'), str(cpp), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
