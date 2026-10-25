import test from 'node:test';
import assert from 'node:assert/strict';
import { Device } from '../shared/device/src/device.js';
import { encodeRequest, decodeRequest, encodeResponse, decodeResponse, colorSchema } from '../shared/protocol/src/index.js';

test('radio settings preserve explicit false and omitted fields across protobuf and ordinary edits', () => {
  const device = new Device('radio-test');
  assert.deepEqual(device.state.config.wifi, { enabled: true });
  assert.deepEqual(device.state.config.matter, { enabled: true });
  const config = { ...device.state.config, wifi: { enabled: false }, matter: { enabled: false } };
  const request = decodeRequest(encodeRequest({ id: 1, operation: 3, config }));
  const response = decodeResponse(encodeResponse(device.handle(request)));
  assert.equal(response.error, '');
  assert.equal(response.state!.config.wifi!.enabled, false);
  assert.equal(response.state!.config.matter!.enabled, false);
  const { wifi, matter, ...oldConfig } = config;
  device.handle(decodeRequest(encodeRequest({ id: 2, operation: 3, config: { ...oldConfig, name: 'Older client' } })));
  assert.equal(device.state.config.wifi!.enabled, false);
  assert.equal(device.state.config.matter!.enabled, false);
});

test('SK6812 simulator accepts white in colors/gradients and rejects warm/cool white', () => {
  const device = new Device('sk6812-test');
  assert.ok(!device.handle({ id: 1, operation: 3, config: { ...device.state.config, stripType: 'SK6812', ledCount: 1024 } }).error);
  const white = colorSchema.parse({ white: 255 });
  assert.ok(!device.handle({ id: 2, operation: 1, color: white }).error);
  assert.deepEqual(device.state.color, white);
  assert.ok(device.handle({ id: 3, operation: 1, color: colorSchema.parse({ warmWhite: 1 }) }).error);
  assert.ok(!device.handle({ id: 4, operation: 17, gradient: { id: '', name: '', colors: [white, colorSchema.parse({ red: 255 })] } }).error);
  assert.ok(device.handle({ id: 5, operation: 17, gradient: { id: '', name: '', colors: [white, colorSchema.parse({ coolWhite: 1 })] } }).error);
});
