#!/usr/bin/env python3
"""Run the production render and Matter startup code with fake peripherals.

Keep the functions inline in main.cpp; extract their unchanged bodies for this
host harness rather than maintaining a second implementation of their logic.
"""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parent.parent
source = (root / 'Luxio/src/main.cpp').read_text()
render = source[source.index('static void render()'):source.index('static void applyColor()')]
startup = source[source.index('static void cloudTask(void *)'):source.index('    if (!lastAnnouncement ||')]
startup += '    vTaskDelay(1000);\n  }\n}\n'
hook = source[source.index('static void luaHook('):source.index('static String startAnimation(')]
encoding = source[source.index('  rmt_bytes_encoder_config_t encoding = {};', source.index('void setup()')):source.index('  ESP_ERROR_CHECK(rmt_enable(ledChannel));', source.index('void setup()'))]
fixture = (root / 'tools/test-startup-output.cpp').read_text()
fixture = fixture.replace('// PRODUCTION_RENDER', render).replace('// PRODUCTION_STARTUP', startup).replace('// PRODUCTION_ENCODING', encoding).replace('// PRODUCTION_LUA_HOOK', hook)
(root / '.sdk').mkdir(exist_ok=True)
cpp = root / '.sdk/test-startup-output.cpp'; cpp.write_text(fixture)
for matter, dma in [(0, 0), (0, 1), (1, 0), (1, 1)]:
    binary = root / f'.sdk/test-startup-output-{matter}-{dma}'
    subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', f'-DLUXIO_MATTER={matter}', f'-DLUXIO_INTERNAL_RAM_RADIOS={dma}', '-I', str(root / 'Luxio/src'), '-I', str(root / 'Luxio/src/vendor/nanopb'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
