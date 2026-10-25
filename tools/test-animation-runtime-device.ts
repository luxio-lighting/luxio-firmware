import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { requestHttp, type Script } from '../shared/protocol/src/index.js';

const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS for this opt-in hardware test');
let id = 250000;
const initial = (await requestHttp(address, { id: id++, operation: 0 })).state!;
assert.equal(initial.mode, 2, 'Start the animation to restore before running this test');
const original = (await requestHttp(address, { id: id++, operation: 6, scriptId: initial.activeScriptId || '' })).script!;
assert.ok(original?.source, 'The active animation source must be available for restoration');
const before = (await requestHttp(address, { id: id++, operation: 5 })).scripts.map(item => item.id).sort();
try {
  if (process.env.ANIMATION_PRESETS) {
    const presets = JSON.parse(await readFile(process.env.ANIMATION_PRESETS, 'utf8')) as (Script & { isGradient: boolean })[];
    for (const ledCount of [256, 1024]) {
      const configured = await requestHttp(address, { id: id++, operation: 3, config: { ...initial.config, ledCount } });
      assert.equal(configured.state!.config.ledCount, ledCount);
      for (const preset of presets.filter(p => !p.isGradient)) {
        await requestHttp(address, { id: id++, operation: 31, script: { id: '', name: preset.name, source: preset.source, mode: 2 } });
        let slowest = 0;
        for (let poll = 0; poll < 12; poll++) {
          await new Promise(resolve => setTimeout(resolve, 250));
          const started = performance.now();
          const reply = await requestHttp(address, { id: id++, operation: 0 });
          slowest = Math.max(slowest, performance.now() - started);
          assert.equal(reply.state!.config.ledCount, ledCount);
          assert.equal(reply.state!.mode, 2, `${preset.name}, ${ledCount} LEDs: ${reply.animationError}`);
          assert.equal(reply.animationError, '');
          assert.equal(reply.state!.scriptName, preset.name);
        }
        console.log(`PASS: ${preset.name}, ${ledCount} LEDs; slowest HTTP poll ${slowest.toFixed(0)} ms`);
      }
    }
  }
  // A custom animation deliberately exceeds the former per-frame budget.
  const slow = 'function setup() for i=1,60000 do local n=i*i end end function loop() for i=0,luxio_get_pixel_count()-1 do for j=1,60 do local n=j*j end luxio_set_pixel_color(i,0,40,80) end end';
  await requestHttp(address, { id: id++, operation: 31, script: { id: '', name: 'Long custom frame', source: slow, mode: 2 } });
  for (const on of [false, true, false, true]) {
    await new Promise(resolve => setTimeout(resolve, 500));
    const started = performance.now();
    const reply = await requestHttp(address, { id: id++, operation: 15, on });
    assert.equal(reply.state!.mode, 2); assert.equal(reply.state!.on, on);
    assert.ok(performance.now() - started < 2000, 'Power commands must not wait for an entire animation call');
  }
  const healthy = await requestHttp(address, { id: id++, operation: 0 });
  assert.equal(healthy.animationError, ''); assert.equal(healthy.state!.mode, 2);
  console.log('PASS: long custom setup/frames and power changes; memory', JSON.stringify({ free: healthy.networkStatus?.internalFree, largest: healthy.networkStatus?.internalLargestBlock, psram: healthy.networkStatus?.psramSize }));
  // No library writes: each deliberately invalid animation is only a RAM preview.
  for (const [source, error] of [
    ['function loop() error("runtime regression sentinel") end', /runtime regression sentinel/],
    ['function loop() while true do end end', /(instruction|CPU) limit/],
    ['function loop() local x = string.rep("x", 200000) end', /memory/],
    ['while true do end', /(instruction|CPU) limit/],
    ['function setup() while true do end end function loop() end', /(instruction|CPU) limit/],
    ['function loop() while true do pcall(function() while true do end end) end end', /(instruction|CPU) limit/],
  ] as const) {
    await requestHttp(address, { id: id++, operation: 31, script: { id: '', name: 'Runtime regression', source, mode: 2 } });
    let reply;
    for (let poll = 0; poll < 150; poll++) {
      await new Promise(resolve => setTimeout(resolve, 100));
      reply = await requestHttp(address, { id: id++, operation: 0 });
      if (reply.state!.mode !== 2) break;
    }
    assert.equal(reply!.state!.mode, 0, 'Invalid animation must terminate and leave HTTP responsive');
    assert.match(reply!.animationError!, error);
    assert.equal((await requestHttp(address, { id: id++, operation: 0 })).animationError, reply!.animationError, 'Error must survive polling');
    console.log(`PASS: ${error}`);
  }

} finally {
  await requestHttp(address, { id: id++, operation: 3, config: initial.config });
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
  assert.deepEqual(restored.state!.config, initial.config);
  assert.deepEqual((await requestHttp(address, { id: id++, operation: 5 })).scripts.map(item => item.id).sort(), before);
  console.log('PASS: original animation restored and saved animations unchanged');
}
