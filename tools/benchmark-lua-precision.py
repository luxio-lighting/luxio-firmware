#!/usr/bin/env python3
"""Generate precision and bulk API benchmarks from production firmware code.

--bulk compares old/new presets; --bulk --paired toggles native/fallback paths
in one VM. Host mode writes CSV and (for unpaired runs) raw frames for
comparison. --esp32 prepares and compiles application-only diagnostic images;
it NEVER flashes. These images omit radios and LED output. Preserve and restore
the device's application and active state when running them on hardware.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import subprocess

root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser()
parser.add_argument('--waves', action='store_true', help='Benchmark wave renderer with paired native/fallback paths')
parser.add_argument('--esp32', action='store_true')
parser.add_argument('--bulk', action='store_true', help='Compare pre-bulk float firmware/presets with the bulk API and updated presets')
parser.add_argument('--paired', action='store_true', help='With --bulk, alternate native/fallback paths in one VM; excludes parameter-hoisting gains')
args=parser.parse_args()
if args.waves: args.bulk = args.paired = True
assert not args.paired or args.bulk, '--paired requires --bulk'
source=(root/'Luxio/src/main.cpp').read_text()
fixture=(root/'tools/benchmark-lua-precision.cpp').read_text()
if args.paired:
 start=fixture.index('static int benchmark(FILE *frames)');end=fixture.index('#ifdef ARDUINO\nvoid setup()',start)
 fixture=fixture[:start]+(root/'tools/benchmark-lua-bulk.cpp').read_text()+fixture[end:]
for key,start,end in [
 ('GLOBALS','static lua_State *animation','static bool wifiPending'),
 ('STOP','static void stopAnimation()','static void render()'),
 ('RUNTIME','static void *luaAllocator(','static bool validId('),
 ('STEP','  // ANIMATION_STEP_BEGIN','  // ANIMATION_STEP_END')]:
 fixture=fixture.replace('// PRODUCTION_'+key,source[source.index(start):source.index(end)])
base=root/('.sdk/waves-paired' if args.waves else '.sdk/bulk-paired' if args.paired else '.sdk/bulk-benchmark' if args.bulk else '.sdk/float-benchmark')
variants=['paired'] if args.paired else ['before','after'] if args.bulk else ['double','float']
for precision in variants:
 presets=json.loads((root/'tools/fixtures'/('animation-presets-waves.json' if args.waves else 'animation-presets-bulk.json' if precision in ['after','paired'] else 'animation-presets.json')).read_text())
 if args.bulk:
  pulse=next(p for p in presets if p['name']=='Aurora')
  presets.append({'name':'Aurora (Pulse off)', 'source':pulse['source'].replace('Pulse,Checkbox,true','Pulse,Checkbox,false')})
 compiled=fixture
 if args.bulk and precision=='before':
  start=compiled.index('    // BULK_API_BEGIN');end=compiled.index('    // BULK_API_END')+len('    // BULK_API_END')
  compiled=compiled[:start]+compiled[end:]
 compiled=compiled.replace('// PRESETS','static const struct { const char *name, *source; } presets[] = {\n'+',\n'.join('{'+json.dumps(p['name'])+','+json.dumps(p['source'])+'}' for p in presets)+'\n};')
 folder=base/precision;folder.mkdir(parents=True,exist_ok=True)
 sketch=folder/'Benchmark';sketch.mkdir(exist_ok=True)
 src=sketch/'src';src.mkdir(exist_ok=True)
 shutil.copytree(root/'Luxio/src/vendor/lua',src/'vendor/lua',dirs_exist_ok=True)
 shutil.copytree(root/'Luxio/src/vendor/nanopb',src/'vendor/nanopb',dirs_exist_ok=True)
 for name in ['luxio.pb.h','animation_parameters.h','animation_parameters.cpp']:
  shutil.copyfile(root/'Luxio/src'/name,src/name)
 config=src/'vendor/lua/luaconf.h'
 text=config.read_text()
 if precision=='double':text=text.replace('#define LUA_FLOAT_DEFAULT\tLUA_FLOAT_FLOAT','#define LUA_FLOAT_DEFAULT\tLUA_FLOAT_DOUBLE')
 config.write_text(text)
 (src/'benchmark.cpp').write_text(compiled)
 (sketch/'Benchmark.ino').write_text('// Entry points are in src/benchmark.cpp.\n')
 if args.esp32:
  shutil.copyfile(root/'Luxio/partitions.csv',sketch/'partitions.csv')
  cli=os.environ.get('ARDUINO_CLI','/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli')
  subprocess.run([cli,'--config-file',str(root/'.sdk/arduino-cli.yaml'),'compile','--fqbn','esp32:esp32:esp32:FlashSize=4M,FlashMode=dio,PSRAM=disabled','--jobs','4','--build-path',str(folder/'build'),'--build-property',f'runtime.tools.ctags.path={root}/.sdk/ctags','--build-property',f'tools.ctags.path={root}/.sdk/ctags','--build-property',f'compiler.c.extra_flags=-DLUA_USE_C89 -I{src}/vendor/nanopb','--build-property',f'compiler.cpp.extra_flags=-DLUA_USE_C89 -I{src}/vendor/nanopb',str(sketch)],check=True)
 else:
  paths=list((src/'vendor/lua').glob('*.c'))
  def compile_file(path):
   obj=folder/(path.stem+'.o')
   subprocess.run(['cc','-Os','-DLUA_USE_C89','-c',str(path),'-o',str(obj)],check=True)
   return str(obj)
  with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_file,paths))
  binary=folder/'benchmark-host'
  subprocess.run(['c++','-std=c++17','-Os','-DLUA_USE_C89','-I',str(src),'-I',str(src/'vendor/nanopb'),str(src/'benchmark.cpp'),str(src/'animation_parameters.cpp'),*objects,'-o',str(binary)],check=True)
  with (folder/'host.csv').open('w') as output:subprocess.run([str(binary),str(folder/'frames.bin')],stdout=output,check=True)
 print(f'{precision}: {folder}',flush=True)
if not args.esp32 and not args.paired:
 baseline=(base/variants[0]/'frames.bin').read_bytes()
 changed=(base/variants[1]/'frames.bin').read_bytes()
 assert len(baseline)==len(changed)==len(presets)*(100+1024)*6*60
 offset=0
 for preset in presets:
  for count in [100,1024]:
   length=count*6*60
   differences=[abs(a-b) for a,b in zip(baseline[offset:offset+length],changed[offset:offset+length])]
   print(f'OUTPUT_DIFF,{preset["name"]},{count},max={max(differences)},mean={sum(differences)/length:.4f}')
   if args.bulk: assert max(differences)<=1, f'{preset["name"]}: output changed by more than one channel step'
   offset+=length
