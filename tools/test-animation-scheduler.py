#!/usr/bin/env python3
"""Exercise actual firmware Lua startup, hook and scheduler with the real Lua VM."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parent.parent
source=(root/'Luxio/src/main.cpp').read_text()
fixture=(root/'tools/test-animation-scheduler.cpp').read_text()
sections={
'GLOBALS':source[source.index('static lua_State *animation'):source.index('static bool wifiPending')],
'STOP':source[source.index('static void stopAnimation()'):source.index('static void render()')],
'RUNTIME':source[source.index('static void *luaAllocator('):source.index('static bool validId(')],
'OUTPUT':source[source.index('    // ANIMATION_OUTPUT_BEGIN'):source.index('    // ANIMATION_OUTPUT_END')],
'STEP':source[source.index('  // ANIMATION_STEP_BEGIN'):source.index('  // ANIMATION_STEP_END')],
}
for key,value in sections.items():fixture=fixture.replace('// PRODUCTION_'+key,value)
folder=root/'.sdk/animation-scheduler';folder.mkdir(parents=True,exist_ok=True)
cpp=folder/'test.cpp';cpp.write_text(fixture)
objects=[]
for path in (root/'Luxio/src/vendor/lua').glob('*.c'):
 obj=folder/(path.name+'.o');objects.append(str(obj))
 if obj.exists() and obj.stat().st_mtime >= path.stat().st_mtime: continue
 subprocess.run(['cc','-O1','-DLUA_USE_C89','-fsanitize=address,undefined','-c',str(path),'-o',str(obj)],check=True)
binary=folder/'test'
subprocess.run(['c++','-std=c++17','-O1','-fsanitize=address,undefined','-I',str(root/'Luxio/src'),'-I',str(root/'Luxio/src/vendor/nanopb'),str(cpp),str(root/'Luxio/src/animation_parameters.cpp'),*objects,'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
