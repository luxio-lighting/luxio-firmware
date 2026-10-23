import assert from 'node:assert/strict';
import { requestHttp, colorSchema, type State, type Gradient, type SavedColor } from '../shared/protocol/src/index.js';
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS to opt into physical strip testing');
let id = 1;
const initial = (await requestHttp(address, { id: id++, operation: 0 })).state!;
assert.ok(initial.build >= 13);
const originalScripts = (await requestHttp(address, { id: id++, operation: 5 })).scripts;
const originalGradients = (await requestHttp(address, { id: id++, operation: 18 })).gradients!;
const originalColors = (await requestHttp(address, { id: id++, operation: 25 })).colors!;
const gradients: Gradient[] = [], colors: SavedColor[] = [];
const red = colorSchema.parse({ red: 255 }), blue = colorSchema.parse({ blue: 255 });
try {
  await requestHttp(address, { id: id++, operation: 2, brightness: 16 });
  for (let i = 0; i < 65; i++) {
    colors.push((await requestHttp(address, { id: id++, operation: 27, savedColor: { id: '', name: `Resource test color ${i}`, color: red } })).savedColor!);
    gradients.push((await requestHttp(address, { id: id++, operation: 20, gradient: { id: '', name: `Resource test gradient ${i}`, colors: [red, blue], editorMetadata: '{"test":true}' } })).gradient!);
  }
  assert.equal((await requestHttp(address, { id: id++, operation: 25 })).colors!.length, originalColors.length + 65);
  assert.equal((await requestHttp(address, { id: id++, operation: 18 })).gradients!.length, originalGradients.length + 65);
  await requestHttp(address, { id: id++, operation: 28, savedColor: { ...colors[0], name: 'Updated color', color: blue } });
  assert.equal((await requestHttp(address, { id: id++, operation: 26, colorId: colors[0].id })).savedColor!.color.blue, 255);
  await requestHttp(address, { id: id++, operation: 21, gradient: { ...gradients[0], name: 'Updated gradient', colors: [blue, red] } });
  assert.equal((await requestHttp(address, { id: id++, operation: 19, gradientId: gradients[0].id })).gradient!.colors[0].blue, 255);
  for (const command of [
    { operation: 30, colorId: colors[0].id }, { operation: 23, gradientId: gradients[0].id },
    { operation: 1, color: red }, { operation: 17, gradient: { id: '', name: '', colors: [red, blue] } },
    { operation: 31, script: { id: '', name: 'Resource RAM test', source: 'function setup() end function loop() for i=0,luxio_get_pixel_count()-1 do luxio_set_pixel_color(i,0,0,255) end end', mode: 2 } }
  ]) {
    await requestHttp(address, { id: id++, operation: 15, on: false });
    const state: State = (await requestHttp(address, { id: id++, ...command })).state!;
    assert.equal(state.on, true); assert.equal(state.brightness, 16);
    await new Promise(resolve => setTimeout(resolve, 600));
  }
  await requestHttp(address, { id: id++, operation: 11, script: { id: '', name: 'Another preview', source: '' } });
  assert.equal((await requestHttp(address, { id: id++, operation: 0 })).state!.mode, 2);
  await requestHttp(address, { id: id++, operation: 11 });
  assert.equal((await requestHttp(address, { id: id++, operation: 0 })).state!.mode, 0);
  await assert.rejects(requestHttp(address, { id: id++, operation: 17, gradient: { id: '', name: '', colors: [red] } }), /2–8/);
  await assert.rejects(requestHttp(address, { id: id++, operation: 10, scriptId: gradients[0].id }), /not found/);
  await assert.rejects(requestHttp(address, { id: id++, operation: 23, gradientId: colors[0].id }), /not found/);
  assert.deepEqual((await requestHttp(address, { id: id++, operation: 5 })).scripts, originalScripts);
  assert.equal((await requestHttp(address, { id: id++, operation: 18 })).gradients!.length, originalGradients.length + 65);
  assert.equal((await requestHttp(address, { id: id++, operation: 25 })).colors!.length, originalColors.length + 65);
  console.log('PASS on ESP32: color/gradient CRUD, 65-item paging, all RAM modes, power/brightness, namespace separation, conditional stop; previews created no files');
} finally {
  for (const color of colors) await requestHttp(address, { id: id++, operation: 29, colorId: color.id });
  for (const gradient of gradients) await requestHttp(address, { id: id++, operation: 22, gradientId: gradient.id });
  await requestHttp(address, { id: id++, operation: 1, color: initial.color });
  if (initial.mode === 1 && initial.gradient) await requestHttp(address, { id: id++, operation: initial.activeGradientId ? 23 : 17, gradientId: initial.activeGradientId, gradient: initial.gradient });
  if (initial.mode === 2 && initial.activeScriptId) await requestHttp(address, { id: id++, operation: 10, scriptId: initial.activeScriptId });
  if (initial.activeColorId) await requestHttp(address, { id: id++, operation: 30, colorId: initial.activeColorId });
  await requestHttp(address, { id: id++, operation: 2, brightness: initial.brightness });
  await requestHttp(address, { id: id++, operation: 15, on: initial.on });
  console.log('Temporary files removed; original output restored.');
}
