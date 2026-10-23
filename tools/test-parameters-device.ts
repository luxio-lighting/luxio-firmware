import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { requestHttp as send, encodeRequest, decodeResponse, frame, type Request } from '../shared/protocol/src/index.js';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
async function requestHttp(address: string, request: Request) {
  console.log(`Request ${request.id}: operation ${request.operation}`);
  let response;
  if (process.env.DEVICE_SERIAL) {
    const { stdout } = await promisify(execFile)('.sdk/venv/bin/python', ['-c', `
import serial,sys,time
p=serial.Serial(sys.argv[1],115200,timeout=.1)
p.dtr=True;p.rts=False;time.sleep(.15)
data=bytes.fromhex(sys.argv[2])
for offset in range(0,len(data),128):
 p.write(data[offset:offset+128]);p.flush();time.sleep(.01)
received=b'';deadline=time.monotonic()+15
while time.monotonic()<deadline:
 received+=p.read(4096)
 start=received.find(b'LXP1')
 if start>=0 and len(received)>=start+8:
  size=int.from_bytes(received[start+4:start+8],'big')
  if size>32768:raise SystemExit('Invalid frame size')
  if len(received)>=start+8+size:
   print(received[start+8:start+8+size].hex());p.close();sys.exit(0)
p.close();raise SystemExit('USB request timed out')
`, process.env.DEVICE_SERIAL, Buffer.from(frame(encodeRequest(request))).toString('hex')], { maxBuffer: 131072 });
    response = decodeResponse(Buffer.from(stdout.trim(), 'hex'));
    assert.equal(response.id, request.id);
    if (response.error) throw new Error(response.error);
  } else response = await send(address, request);
  console.log(`Reply ${request.id}: operation ${request.operation}`);
  return response;
}
import { parseParameters } from '../shared/protocol/src/parameters.js';
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS for this opt-in hardware test');
let id = 140000;
const initial = process.env.RESTORE_STATE_FILE ? JSON.parse(await readFile(process.env.RESTORE_STATE_FILE, 'utf8')).state : (await requestHttp(address, { id: id++, operation: 0 })).state!;
// A RAM-only animation cannot be read back from the device. Require its source
// before changing anything so the hardware test can restore the actual output.
let restoreSource: string | undefined;
if (initial.mode === 2 && !initial.activeScriptId) {
  if (!process.env.RESTORE_ANIMATION_SOURCE) throw new Error('Set RESTORE_ANIMATION_SOURCE to restore the active RAM animation');
  restoreSource = await readFile(process.env.RESTORE_ANIMATION_SOURCE, 'utf8');
}
const before = (await requestHttp(address, { id: id++, operation: 5 })).scripts;
const current = (await requestHttp(address, { id: id++, operation: 0 })).state!;
assert.ok(current.build >= 14); assert.equal(current.deviceId, initial.deviceId); assert.deepEqual(current.config, initial.config);
const source = `-- @parameter Flicker,Boolean,true
-- @parameter Speed,Number,50,0,100
local frames = 0
function loop()
  frames = frames + 1
  local speed = luxio_get_parameter('Speed')
  local flicker = luxio_get_parameter('Flicker')
  assert(type(speed) == 'number' and type(flicker) == 'boolean')
  if speed == 75 then
    assert(frames > 40 and luxio_millis() > 1500, 'Parameter update restarted animation')
  end
  for i = 0, luxio_get_pixel_count() - 1 do
    luxio_set_pixel_color(i, 20, flicker and 20 or 0, math.floor(speed / 4))
  end
end`;
let savedId = '';
try {
  await requestHttp(address, { id: id++, operation: 2, brightness: Math.min(initial.brightness, 18) });
  const saved = (await requestHttp(address, { id: id++, operation: 7, script: { id: '', name: 'Parameter regression', source } })).script!;
  savedId = saved.id; assert.equal(saved.parameters!.length, 2); assert.equal(saved.parameters![1].value, 50);
  await requestHttp(address, { id: id++, operation: 10, scriptId: saved.id });
  await new Promise(resolve => setTimeout(resolve, 2100));
  const patch = saved.parameters!.map(p => ({ ...p, value: p.boolean ? 0 : 75 }));
  await requestHttp(address, { id: id++, operation: 32, scriptId: saved.id, parameters: patch });
  await new Promise(resolve => setTimeout(resolve, 150));
  let state = (await requestHttp(address, { id: id++, operation: 0 })).state!;
  assert.equal(state.mode, 2, 'Live updates must preserve Lua VM and timer'); assert.equal(state.parameters![0].value, 0); assert.equal(state.parameters![1].value, 75);
  const unchanged = (await requestHttp(address, { id: id++, operation: 6, scriptId: saved.id })).script!;
  assert.equal(unchanged.parameters![0].value, 1); assert.equal(unchanged.parameters![1].value, 50);
  await assert.rejects(requestHttp(address, { id: id++, operation: 32, scriptId: saved.id, parameters: patch.map(p => ({ ...p, value: p.boolean ? 1 : 1000 })) }), /parameter/i);
  await assert.rejects(requestHttp(address, { id: id++, operation: 32, scriptId: 'wrong', parameters: [] }), /changed/);
  state = (await requestHttp(address, { id: id++, operation: 0 })).state!; assert.equal(state.parameters![0].value, 0); assert.equal(state.parameters![1].value, 75);
  const persisted = patch.map(p => ({ ...p, value: p.boolean ? 0 : 80 }));
  await requestHttp(address, { id: id++, operation: 8, script: { ...saved, parameters: persisted } });
  const readback = (await requestHttp(address, { id: id++, operation: 6, scriptId: saved.id })).script!;
  assert.deepEqual(readback.parameters, persisted);
  assert.equal((await requestHttp(address, { id: id++, operation: 0 })).state!.parameters![1].value, 75, 'Saving must not restart current playback');
  await requestHttp(address, { id: id++, operation: 11 });
  state = (await requestHttp(address, { id: id++, operation: 10, scriptId: saved.id })).state!;
  assert.deepEqual(state.parameters, persisted);
  const maxSource = Array.from({ length: 16 }, (_, i) => `-- @parameter P${i},Number,${i},-100,100`).join('\n') + '\nfunction loop() end';
  const maxSaved = (await requestHttp(address, { id: id++, operation: 8, script: { ...saved, source: maxSource, parameters: parseParameters(maxSource).map(p => ({ ...p, value: 99.5 })) } })).script!;
  assert.equal(maxSaved.parameters!.length, 16);
  const maxReadback = (await requestHttp(address, { id: id++, operation: 6, scriptId: saved.id })).script!;
  assert.equal(maxReadback.parameters![15].value, 99.5);
  assert.ok((await requestHttp(address, { id: id++, operation: 5 })).scripts.every(item => !item.parameters?.length), 'Listings remain compact');
  await assert.rejects(requestHttp(address, { id: id++, operation: 7, script: { id: '', name: 'Invalid parameters', source: '-- @parameter X,Boolean,true\n-- @parameter X,Boolean,false\nfunction loop() end' } }), /Duplicate/);
  const previewName = `Parameter preview ${Date.now()}`;
  state = (await requestHttp(address, { id: id++, operation: 31, script: { id: '', name: previewName, source, parameters: parseParameters(source) } })).state!;
  assert.equal(state.activeScriptId, '');
  state = (await requestHttp(address, { id: id++, operation: 32, script: { id: '', name: previewName, source: '' }, parameters: [{ ...state.parameters![1], value: 40 }] })).state!;
  assert.equal(state.parameters![1].value, 40);
  assert.equal((await requestHttp(address, { id: id++, operation: 5 })).scripts.length, before.length + 1);
  console.log('PASS: ESP32 typed defaults, uninterrupted live updates, atomic validation, explicit persistence, 16 parameters, compact listings, and RAM previews');
} finally {
  if (savedId) await requestHttp(address, { id: id++, operation: 9, scriptId: savedId });
  await requestHttp(address, { id: id++, operation: 2, brightness: initial.brightness });
  if (restoreSource) await requestHttp(address, { id: id++, operation: 31, script: { id: '', name: initial.scriptName ?? 'Restored animation', source: restoreSource, mode: 2, parameters: initial.parameters } });
  else if (initial.mode === 2 && initial.activeScriptId) await requestHttp(address, { id: id++, operation: 10, scriptId: initial.activeScriptId });
  else if (initial.mode === 1 && initial.gradient) await requestHttp(address, initial.activeGradientId ? { id: id++, operation: 23, gradientId: initial.activeGradientId } : { id: id++, operation: 17, gradient: initial.gradient });
  else await requestHttp(address, initial.activeColorId ? { id: id++, operation: 30, colorId: initial.activeColorId } : { id: id++, operation: 1, color: initial.color });
  await requestHttp(address, { id: id++, operation: 15, on: initial.on });
  assert.deepEqual((await requestHttp(address, { id: id++, operation: 5 })).scripts.map(s => s.id).sort(), before.map(s => s.id).sort());
  console.log('Restored original output, brightness, power, and animation library');
}
