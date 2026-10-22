// Opt-in hardware regression. Creates only its own script and restores the light.
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { readFile } from 'node:fs/promises';
import { requestHttp, colorSchema, type State } from '../shared/protocol/src/index.js';
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS');
let id = 1, scriptId: string | undefined;
const current = (await requestHttp(address, { id: id++, operation: 0 })).state!;
assert.equal(current.build, 11);
const initial: State = process.env.RESTORE_STATE_FILE ? JSON.parse(await readFile(process.env.RESTORE_STATE_FILE, 'utf8')).state : current;
assert.equal(current.deviceId, initial.deviceId); assert.deepEqual(current.config, initial.config);
const original = (await requestHttp(address, { id: id++, operation: 5 })).scripts;
try {
  await requestHttp(address, { id: id++, operation: 15, on: false });
  for (const brightness of [1, 256, 16]) {
    const state: State = (await requestHttp(address, { id: id++, operation: 2, brightness })).state!;
    assert.equal(state.on, false); assert.equal(state.brightness, brightness);
  }
  for (const brightness of [0, 257]) await assert.rejects(requestHttp(address, { id: id++, operation: 2, brightness }), /1-256/);
  let state = (await requestHttp(address, { id: id++, operation: 1, color: colorSchema.parse({ red: 255 }) })).state!;
  assert.equal(state.on, true); assert.equal(state.brightness, 16);
  await new Promise(resolve => setTimeout(resolve, 650));
  for (const mode of [1, 2]) {
    await requestHttp(address, { id: id++, operation: 15, on: false });
    const script = { id: scriptId ?? '', name: 'Temporary power regression', source: mode === 1 ? 'function setup() for i=0,luxio_get_pixel_count()-1 do luxio_set_pixel_color(i,0,128,255) end end function loop() error("Gradient loop must not run") end' : 'function setup() end function loop() for i=0,luxio_get_pixel_count()-1 do luxio_set_pixel_color(i,0,255,0) end end', mode };
    scriptId = (await requestHttp(address, { id: id++, operation: scriptId ? 8 : 7, script })).script!.id;
    assert.equal((await requestHttp(address, { id: id++, operation: 0 })).state!.on, false);
    state = (await requestHttp(address, { id: id++, operation: 10, scriptId })).state!;
    assert.equal(state.on, true); assert.equal(state.brightness, 16); assert.equal(state.mode, mode);
    await new Promise(resolve => setTimeout(resolve, 650));
    state = (await requestHttp(address, { id: id++, operation: 15, on: false })).state!;
    assert.equal(state.on, false); assert.equal(state.brightness, 16); assert.equal(state.mode, mode); assert.equal(state.activeScriptId, scriptId);
    state = (await requestHttp(address, { id: id++, operation: 15, on: true })).state!;
    assert.equal(state.brightness, 16); assert.equal(state.mode, mode);
  }
  await requestHttp(address, { id: id++, operation: 15, on: false });
  await assert.rejects(requestHttp(address, { id: id++, operation: 10, scriptId: '11111111-1111-4111-8111-111111111111' }), /not found/);
  await requestHttp(address, { id: id++, operation: 8, script: { id: scriptId!, name: 'Temporary power regression', source: 'not valid lua', mode: 2 } });
  await assert.rejects(requestHttp(address, { id: id++, operation: 10, scriptId }));
  assert.equal((await requestHttp(address, { id: id++, operation: 0 })).state!.on, false);
  if (process.env.DEVICE_SERIAL) {
    await requestHttp(address, { id: id++, operation: 2, brightness: 23 });
    await new Promise(resolve => setTimeout(resolve, 1300));
    await promisify(execFile)('.sdk/venv/bin/python', ['-m', 'esptool', '--chip', 'esp32s3', '--port', process.env.DEVICE_SERIAL, '--after', 'hard-reset', 'chip-id']);
    let rebooted: State | undefined;
    const deadline = Date.now() + 60000;
    while (Date.now() < deadline) {
      try { rebooted = (await requestHttp(address, { id: id++, operation: 0 })).state; if (rebooted) break; } catch {}
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
    assert.ok(rebooted, 'Device did not reconnect after reboot');
    assert.equal(rebooted.on, false); assert.equal(rebooted.brightness, 23);
    assert.equal((await requestHttp(address, { id: id++, operation: 1, color: colorSchema.parse({ red: 255 }) })).state!.brightness, 23);
    console.log('PASS: off and brightness survive reboot independently; auto-on retains the saved brightness');
  }
  console.log('PASS: separate power, 1–256 brightness, off-state level changes, color/gradient/animation auto-on, preserved levels/modes, invalid command rejection');
} finally {
  if (scriptId) await requestHttp(address, { id: id++, operation: 9, scriptId });
  await requestHttp(address, { id: id++, operation: 1, color: initial.color });
  await requestHttp(address, { id: id++, operation: 2, brightness: initial.build < 11 ? (initial.brightness ? Math.round(initial.brightness * 256 / 255) : 16) : initial.brightness });
  if (initial.activeScriptId) await requestHttp(address, { id: id++, operation: 10, scriptId: initial.activeScriptId });
  await requestHttp(address, { id: id++, operation: 15, on: initial.build < 11 ? initial.brightness > 0 : initial.on });
  assert.deepEqual((await requestHttp(address, { id: id++, operation: 5 })).scripts.map(s => s.id).sort(), original.map(s => s.id).sort());
  console.log('PASS: original library and output restored');
}
