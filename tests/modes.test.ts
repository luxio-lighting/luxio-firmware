import test from 'node:test';
import assert from 'node:assert/strict';
import { Device } from '../shared/device/src/device.js';
import { encodeResponse, decodeResponse, colorSchema } from '../shared/protocol/src/index.js';

test('firmware modes separate held gradients from animated Lua and preserve brightness arbitration', () => {
  const device = new Device('test');
  const source = 'function setup() luxio_set_pixel_color(0,255,0,0) end function loop() luxio_set_pixel_color(0,0,0,255) end';
  const gradient = device.handle({ id: 1, operation: 20, gradient: { id: '', name: 'Still', colors: [colorSchema.parse({ red: 255 }), colorSchema.parse({ blue: 255 })] } }).gradient!;
  const moving = device.handle({ id: 2, operation: 7, script: { id: '', name: 'Moving', source, mode: 2 } }).script!;
  device.handle({ id: 3, operation: 2, brightness: 256 });
  device.handle({ id: 4, operation: 23, gradientId: gradient.id });
  assert.equal(device.state.mode, 1);
  assert.equal(decodeResponse(encodeResponse({ state: device.state })).state!.mode, 1);
  for (let frame = 0; frame < 10; frame++) assert.equal(device.output()[0].red, 255, 'Gradient must never execute loop');
  device.handle({ id: 5, operation: 2, brightness: 16 });
  assert.equal(device.state.mode, 1);
  assert.equal(device.output()[0].red, 16);
  device.handle({ id: 6, operation: 10, scriptId: moving.id });
  assert.equal(device.state.mode, 2);
  assert.equal(device.output()[0].blue, 16);
  device.handle({ id: 7, operation: 15, on: false });
  assert.equal(device.state.mode, 2);
  device.handle({ id: 8, operation: 1, color: colorSchema.parse({ warmWhite: 255 }) });
  assert.equal(device.state.mode, 0); assert.equal(device.state.activeScriptId, '');
  device.handle({ id: 9, operation: 23, gradientId: gradient.id });
  device.handle({ id: 10, operation: 24 });
  assert.equal(device.state.mode, 0);
  device.handle({ id: 11, operation: 23, gradientId: gradient.id });
  device.handle({ id: 12, operation: 22, gradientId: gradient.id });
  assert.equal(device.state.mode, 0);
});

test('legacy scripts remain animations; invalid modes fail; mode metadata survives rename and listing', () => {
  const device = new Device('test');
  const source = 'function setup() end function loop() end';
  const legacy = device.handle({ id: 1, operation: 7, script: { id: '', name: 'Legacy', source } }).script!;
  assert.equal(legacy.mode, 2);
  device.handle({ id: 2, operation: 10, scriptId: legacy.id });
  assert.equal(device.state.mode, 2);
  assert.ok(device.handle({ id: 3, operation: 7, script: { id: '', name: 'Bad mode', source, mode: 9 } }).error);
  assert.ok(device.handle({ id: 4, operation: 8, script: { ...legacy, mode: 1 } }).error);
  const renamed = device.handle({ id: 5, operation: 8, script: { ...legacy, name: 'Renamed', mode: 2 } }).script!;
  assert.equal(renamed.id, legacy.id);
  assert.equal(device.handle({ id: 6, operation: 5 }).scripts![0].mode, 2);
  const restored = new Device('test', { state: structuredClone(device.state), scripts: device.scripts });
  assert.equal(restored.state.mode, 0); assert.equal(restored.scripts[0].mode, 2);
});

test('large animation and gradient libraries are paged without the old 32-item cap', () => {
  const device = new Device('large-library');
  for (let i = 0; i < 1000; i++) {
    const created = device.handle({ id: i + 1, operation: 7, script: { id: '', name: `Saved ${i} `.padEnd(63, 'x'), source: 'function setup() end function loop() end', mode: 2 } });
    assert.ok(created.script, created.error);
  }
  const ids: string[] = [];
  let offset = 0;
  do {
    const page = decodeResponse(encodeResponse(device.handle({ id: 1001, operation: 5, scriptOffset: offset })));
    assert.ok(page.scripts.length <= 64);
    assert.ok(page.scripts.every(script => !script.source));
    ids.push(...page.scripts.map(script => script.id));
    if (page.nextScriptOffset) assert.ok(page.nextScriptOffset > offset);
    offset = page.nextScriptOffset ?? 0;
  } while (offset);
  assert.equal(ids.length, 1000);
  assert.equal(new Set(ids).size, 1000);
  assert.equal(device.handle({ id: 1002, operation: 5, scriptOffset: 1000 }).scripts!.length, 0);
});


test('power is independent of brightness, and successful color/gradient/animation commands turn on', () => {
  const device = new Device('power');
  const source = 'function setup() luxio_set_pixel_color(0,255,0,0) end function loop() luxio_set_pixel_color(0,0,255,0) end';
  for (const level of [1, 16, 128, 255, 256]) {
    assert.equal(device.handle({ id: 1, operation: 2, brightness: level }).error, undefined);
    device.handle({ id: 2, operation: 15, on: false });
    assert.equal(device.state.on, false); assert.equal(device.state.brightness, level);
    device.handle({ id: 3, operation: 2, brightness: level });
    assert.equal(device.state.on, false, 'Changing brightness does not change power');
    assert.ok(device.output().every(pixel => Object.values(pixel).every(value => value === 0)));
    device.handle({ id: 4, operation: 1, color: colorSchema.parse({ red: 255 }) });
    assert.equal(device.state.on, true); assert.equal(device.state.brightness, level);
    assert.equal(device.output()[0].red, Math.round(255 * level / 256));
    for (const mode of [1, 2]) {
      device.handle({ id: 5, operation: 15, on: false });
      const script = mode === 2 ? device.handle({ id: 6, operation: 7, script: { id: '', name: 'Power test', source, mode } }).script! : device.handle({ id: 6, operation: 20, gradient: { id: '', name: 'Power test', colors: [colorSchema.parse({ red: 255 }), colorSchema.parse({ blue: 255 })] } }).gradient!;
      assert.equal(device.state.on, false, 'Saving does not turn on');
      device.handle({ id: 7, operation: mode === 2 ? 10 : 23, scriptId: script.id, gradientId: script.id });
      assert.equal(device.state.on, true); assert.equal(device.state.brightness, level);
      device.handle({ id: 8, operation: 15, on: false });
      assert.equal(mode === 2 ? device.state.activeScriptId : device.state.activeGradientId, script.id); assert.equal(device.state.mode, mode);
      device.handle({ id: 9, operation: 15, on: true });
      assert.equal(device.state.brightness, level); assert.equal(mode === 2 ? device.state.activeScriptId : device.state.activeGradientId, script.id);
    }
  }
  device.handle({ id: 10, operation: 15, on: false });
  for (const brightness of [0, 257, -1, 1.5]) assert.ok(device.handle({ id: 11, operation: 2, brightness }).error);
  assert.ok(device.handle({ id: 12, operation: 1, color: colorSchema.parse({ white: 1 }) }).error);
  assert.ok(device.handle({ id: 13, operation: 10, scriptId: 'missing' }).error);
  const invalid = device.handle({ id: 14, operation: 7, script: { id: '', name: 'Invalid', source: 'not lua', mode: 2 } }).script!;
  assert.ok(device.handle({ id: 15, operation: 10, scriptId: invalid.id }).error);
  assert.equal(device.state.on, false); assert.equal(device.state.brightness, 256);
  const restored = new Device('power', { state: structuredClone(device.state), scripts: device.scripts });
  assert.equal(restored.state.on, false); assert.equal(restored.state.brightness, 256);
  assert.equal(decodeResponse(encodeResponse({ state: device.state })).state!.on, false);
  device.handle({ id: 16, operation: 15, on: true });
  assert.equal(decodeResponse(encodeResponse({ state: device.state })).state!.on, true);
});
