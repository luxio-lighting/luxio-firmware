import test from 'node:test';
import assert from 'node:assert/strict';
import { Device } from '../shared/device/src/device.js';
import { colorSchema, encodeResponse, decodeResponse } from '../shared/protocol/src/index.js';
const red = colorSchema.parse({ red: 255 }), blue = colorSchema.parse({ blue: 255 });
const script = { id: '', name: 'RAM animation', source: 'function setup() end function loop() luxio_set_pixel_color(0,0,0,255) end', mode: 2 };
test('all live modes run without library writes, turn on, and preserve brightness', () => {
  const d = new Device('ram');
  for (const request of [{ operation: 1, color: red }, { operation: 17, gradient: { id: '', name: '', colors: [red, blue] } }, { operation: 31, script }]) {
    d.state.on = false; d.state.brightness = 16;
    assert.equal(d.handle({ id: 1, ...request }).error, undefined);
    assert.equal(d.state.on, true); assert.equal(d.state.brightness, 16);
    assert.equal(d.scripts.length + d.gradients.length + d.colors.length, 0);
    assert.equal(d.state.activeScriptId, ''); assert.equal(d.state.activeGradientId, ''); assert.equal(d.state.activeColorId, '');
    assert.equal(d.output().length, 60);
  }
  d.handle({ id: 2, operation: 11, script: { ...script, name: 'Other preview' } });
  assert.equal(d.state.mode, 2, 'Conditional stop cannot stop another preview');
  d.handle({ id: 3, operation: 11, script }); assert.equal(d.state.mode, 0);
});
test('colors and gradients have independent CRUD, namespaces and paginated metadata listings', () => {
  const d = new Device('library');
  for (let i = 0; i < 150; i++) {
    assert.ok(d.handle({ id: i, operation: 27, savedColor: { id: '', name: `Color ${i}`, color: red } }).savedColor);
    assert.ok(d.handle({ id: i, operation: 20, gradient: { id: '', name: `Gradient ${i}`, colors: [red, blue] } }).gradient);
  }
  for (const operation of [18, 25]) {
    const ids: string[] = []; let offset = 0;
    do {
      const result = decodeResponse(encodeResponse(d.handle({ id: 1, operation, gradientOffset: offset, colorOffset: offset })));
      const items = operation === 18 ? result.gradients! : result.colors!;
      assert.ok(items.length <= 64); ids.push(...items.map(i => i.id));
      offset = (operation === 18 ? result.nextGradientOffset : result.nextColorOffset) ?? 0;
    } while (offset);
    assert.equal(new Set(ids).size, 150);
  }
  const color = d.colors[0], gradient = d.gradients[0];
  assert.ok(d.handle({ id: 1, operation: 10, scriptId: gradient.id }).error);
  assert.ok(d.handle({ id: 1, operation: 23, gradientId: color.id }).error);
  assert.equal(d.handle({ id: 1, operation: 28, savedColor: { ...color, name: 'Changed', color: blue } }).savedColor?.name, 'Changed');
  assert.equal(d.handle({ id: 1, operation: 21, gradient: { ...gradient, name: 'Changed', colors: [blue, red] } }).gradient?.name, 'Changed');
  d.handle({ id: 1, operation: 30, colorId: color.id }); assert.equal(d.state.activeColorId, color.id); assert.equal(d.state.color.blue, 255);
  d.handle({ id: 1, operation: 23, gradientId: gradient.id }); assert.equal(d.state.activeGradientId, gradient.id); assert.equal(d.animation, undefined);
  const before = structuredClone(d.state);
  assert.ok(d.handle({ id: 1, operation: 17, gradient: { ...gradient, colors: [red] } }).error);
  assert.deepEqual(d.state, before);
  d.handle({ id: 1, operation: 29, colorId: color.id }); assert.equal(d.gradients.length, 150);
  d.handle({ id: 1, operation: 22, gradientId: gradient.id }); assert.equal(d.state.mode, 0);
  assert.equal(d.colors.length, 149); assert.equal(d.gradients.length, 149);
});
