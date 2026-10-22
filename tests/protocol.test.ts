import test from 'node:test';
import assert from 'node:assert/strict';
import { FrameDecoder, frame, encodeRequest, decodeRequest, encodeResponse, decodeResponse, colorSchema, MAX_FRAME } from '../shared/protocol/src/index.js';
import { Device } from '../shared/device/src/device.js';
import { Animation } from '../shared/device/src/lua.js';
import { presets } from '../shared/web/presets.js';

test('Protobuf roundtrips independent RGBCCT channels and independent power', () => {
  const request = { id: 42, operation: 1, color: colorSchema.parse({ red: 23, green: 51, blue: 82, warmWhite: 131, coolWhite: 219 }) };
  const decoded = decodeRequest(encodeRequest(request));
  assert.deepEqual(decoded.color, request.color);
  assert.equal(decodeRequest(encodeRequest({ id: 1, operation: 2, brightness: 256 })).brightness, 256);
  assert.equal(decodeRequest(encodeRequest({ id: 2, operation: 15, on: false })).on, false);
  const response = decodeResponse(encodeResponse({ id: 42, error: 'No such script' }));
  assert.equal(response.id, 42); assert.equal(response.error, 'No such script');
});
test('serial/BLE framing survives arbitrary fragmentation and concatenation', () => {
  const a = encodeRequest({ id: 77, operation: 0 });
  const b = encodeRequest({ id: 78, operation: 2, brightness: 90 });
  const bytes = new Uint8Array([...new TextEncoder().encode("ESP-ROM boot\r\n"), ...frame(a), ...frame(b)]);
  for (let chunkSize = 1; chunkSize <= bytes.length; chunkSize++) {
    const decoder = new FrameDecoder(); const received: Uint8Array[] = [];
    for (let offset = 0; offset < bytes.length; offset += chunkSize) received.push(...decoder.push(bytes.slice(offset, offset + chunkSize)));
    assert.deepEqual(received.map(bytes => [...bytes]), [[...a], [...b]]);
  }
  assert.throws(() => new FrameDecoder().push(new Uint8Array([0x4c, 0x58, 0x50, 0x31, 255, 255, 255, 255])), /Invalid frame/);
  assert.throws(() => frame(new Uint8Array(MAX_FRAME + 1)), /Invalid frame/);
});
test('script UUID survives rename; brightness preserves animation; explicit color stops it', () => {
  const device = new Device('test');
  const create = device.handle({ id: 1, operation: 7, script: { id: '', name: 'First', source: presets[0].source } });
  assert.ok(create.script); const id = create.script.id;
  assert.equal(device.handle({ id: 2, operation: 10, scriptId: id }).error, undefined);
  assert.equal(device.state.activeScriptId, id);
  device.handle({ id: 3, operation: 15, on: false });
  assert.equal(device.state.activeScriptId, id);
  assert.ok(device.output().every(pixel => Object.values(pixel).every(value => value === 0)));
  const update = device.handle({ id: 4, operation: 8, script: { ...create.script, name: 'Renamed' } });
  assert.equal(update.script?.id, id);
  device.handle({ id: 5, operation: 1, color: colorSchema.parse({ warmWhite: 200, coolWhite: 10 }) });
  assert.equal(device.state.activeScriptId, ''); assert.equal(device.state.color.red, 0);
  assert.equal(device.state.color.warmWhite, 200); assert.equal(device.state.color.coolWhite, 10);
  device.handle({ id: 6, operation: 9, scriptId: id });
  assert.match(device.handle({ id: 7, operation: 6, scriptId: id }).error!, /not found/);
});
test('unsupported channels and invalid configuration leave the existing state intact', () => {
  const device = new Device('test'); const before = structuredClone(device.state);
  assert.ok(device.handle({ id: 1, operation: 1, color: colorSchema.parse({ white: 10 }) }).error);
  assert.deepEqual(device.state, before);
  assert.ok(device.handle({ id: 2, operation: 3, config: { ...before.config, ledCount: 0 } }).error);
  assert.deepEqual(device.state, before);
});
test('Lua lifecycle, sandbox and runaway budget', () => {
  const animation = new Animation('function setup() n = 0 end\nfunction loop() n = n + 1; luxio_set_pixel_color(0,n,0,0) end', 1, 'WS2812');
  assert.equal(animation.tick()[0].red, 1); assert.equal(animation.tick()[0].red, 2); animation.close();
  assert.throws(() => new Animation('while true do end', 1, 'WS2812'), /instruction budget/);
  const runaway = new Animation('function loop() while true do end end', 1, 'WS2812');
  assert.throws(() => runaway.tick(), /instruction budget/);
  assert.throws(() => new Animation('os.execute("anything")', 1, 'WS2812'), /nil/);
  const unavailable = new Animation('function loop() luxio_set_pixel_color(0,0,0,0,10) end', 1, 'WS2812');
  assert.throws(() => unavailable.tick(), /unavailable/);
});
test('all bundled presets run for multiple frames with valid independent channel values', () => {
  for (const preset of presets) {
    const animation = new Animation(preset.source, 60, 'WS2805');
    for (let tick = 0; tick < 40; tick++) {
      const frame = animation.tick();
      assert.equal(frame.length, 60);
      assert.ok(frame.every(pixel => Object.values(pixel).every(value => Number.isInteger(value) && value >= 0 && value <= 255)), preset.name);
    }
    animation.close();
  }
});

test('Wi-Fi scan protocol preserves negative RSSI, security and asynchronous status', () => {
  assert.equal(decodeRequest(encodeRequest({ id: 81, operation: 16 })).operation, 16);
  const networks = [
    { ssid: 'Home & <guest>', rssi: -39, passwordRequired: true },
    { ssid: 'Café', rssi: -82, passwordRequired: false },
  ];
  const response = decodeResponse(encodeResponse({ id: 81, wifiNetworks: networks, wifiScanning: false }));
  assert.deepEqual(response.wifiNetworks, networks);
  assert.equal(response.wifiScanning, false);
  assert.equal(decodeResponse(encodeResponse({ id: 82, wifiScanning: true })).wifiScanning, true);
  assert.equal(decodeResponse(encodeResponse({ id: 83, wifiScanError: 'Scan failed' })).wifiScanError, 'Scan failed');
});
