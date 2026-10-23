import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { requestHttp, type Script } from '../shared/protocol/src/index.js';

const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS for this opt-in hardware test');
let id = 250000;
const initial = (await requestHttp(address, { id: id++, operation: 0 })).state!;
assert.equal(initial.mode, 2, 'Start the animation to restore before running this test');
let original: Script;
if (initial.activeScriptId) {
  original = (await requestHttp(address, { id: id++, operation: 6, scriptId: initial.activeScriptId })).script!;
} else {
  if (!process.env.RESTORE_ANIMATION_SOURCE) throw new Error('Set RESTORE_ANIMATION_SOURCE to restore the active RAM animation');
  original = { id: '', name: initial.scriptName!, source: await readFile(process.env.RESTORE_ANIMATION_SOURCE, 'utf8'), mode: 2 };
}
const before = (await requestHttp(address, { id: id++, operation: 5 })).scripts.map(item => item.id).sort();
try {
  // No flash writes: each deliberately invalid animation is only a RAM preview.
  for (const [source, error] of [
    ['function loop() error("runtime regression sentinel") end', /runtime regression sentinel/],
    ['function loop() while true do end end', /(instruction|time) budget/],
    ['function loop() local x = string.rep("x", 200000) end', /memory/],
  ] as const) {
    await requestHttp(address, { id: id++, operation: 31, script: { id: '', name: 'Runtime regression', source, mode: 2 } });
    let reply;
    for (let poll = 0; poll < 20; poll++) {
      await new Promise(resolve => setTimeout(resolve, 100));
      reply = await requestHttp(address, { id: id++, operation: 0 });
      if (reply.state!.mode !== 2) break;
    }
    assert.equal(reply!.state!.mode, 0, 'Invalid animation must terminate and leave HTTP responsive');
    assert.match(reply!.animationError!, error);
    assert.equal((await requestHttp(address, { id: id++, operation: 0 })).animationError, reply!.animationError, 'Error must survive polling');
    console.log(`PASS: ${error}`);
  }
  await assert.rejects(requestHttp(address, { id: id++, operation: 31, script: { id: '', name: 'Startup regression', source: 'while true do end', mode: 2 } }), /(instruction|time) budget/);
  console.log('PASS: startup infinite loop rejected');
} finally {
  if (initial.activeScriptId) await requestHttp(address, { id: id++, operation: 10, scriptId: initial.activeScriptId });
  else await requestHttp(address, { id: id++, operation: 31, script: { ...original, parameters: initial.parameters } });
  if (initial.activeScriptId && initial.parameters?.length) await requestHttp(address, { id: id++, operation: 32, scriptId: initial.activeScriptId, parameters: initial.parameters });
  await requestHttp(address, { id: id++, operation: 15, on: initial.on });
  const restored = await requestHttp(address, { id: id++, operation: 0 });
  assert.equal(restored.animationError, '');
  assert.equal(restored.state!.mode, 2);
  assert.equal(restored.state!.scriptName, initial.scriptName);
  assert.equal(restored.state!.activeScriptId, initial.activeScriptId);
  assert.equal(restored.state!.brightness, initial.brightness);
  assert.equal(restored.state!.on, initial.on);
  assert.deepEqual((await requestHttp(address, { id: id++, operation: 5 })).scripts.map(item => item.id).sort(), before);
  console.log('PASS: original animation restored and saved animations unchanged');
}
