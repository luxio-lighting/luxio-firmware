import test from 'node:test';
import assert from 'node:assert/strict';
import { configSchema, encodeRequest, decodeRequest, encodeResponse, decodeResponse } from '../shared/protocol/src/index.js';
import { Device } from '../shared/device/src/device.js';

test('button configuration survives both wire directions and validation', () => {
  for (const press of [0, 1] as const) {
    const config = configSchema.parse({ name: 'Desk', stripType: 'WS2805', ledCount: 60, gpio: 16, channel: 0, button: { enabled: true, gpio: 18, press } });
    assert.deepEqual(decodeRequest(encodeRequest({ id: 1, operation: 3, config })).config?.button, config.button);
    const device = new Device('button-test');
    assert.equal(device.handle({ id: 2, operation: 3, config }).error, undefined);
    assert.deepEqual(decodeResponse(encodeResponse({ state: device.state })).state?.config.button, config.button);
    // Other clients editing the name must not disable a configured physical button.
    const { button, ...legacy } = config;
    assert.equal(device.handle({ id: 3, operation: 3, config: { ...legacy, name: 'Renamed' } }).error, undefined);
    assert.deepEqual(device.state.config.button, button);
    assert.equal(device.handle({ id: 4, operation: 3, config: { ...config, button: { ...button!, enabled: false } } }).error, undefined);
    assert.equal(device.state.config.button?.enabled, false);
  }
});
