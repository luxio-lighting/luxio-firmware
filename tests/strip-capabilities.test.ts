import test from 'node:test';
import assert from 'node:assert/strict';
import { Device } from '../shared/device/src/device.js';
import { Animation } from '../shared/device/src/lua.js';
import { colorSchema, encodeResponse, decodeResponse } from '../shared/protocol/src/index.js';

test('capabilities roundtrip and all new RGB types work without white channels', () => {
  const device = new Device('strip-test');
  const state = decodeResponse(encodeResponse(device.handle({id: 1, operation: 0}))).state!;
  assert.deepEqual(state.supportedStripTypes, ['WS2805','WS2814','WS2812','SK6812','WS2811','WS2813','WS2815']);
  for(const stripType of ['WS2811','WS2813','WS2815']) {
    assert.equal(device.handle({id: 2, operation: 3, config: {...state.config, stripType}}).error ?? '', '');
    assert.equal(device.handle({id: 3, operation: 1, color: colorSchema.parse({red: 255})}).error ?? '', '');
    assert.match(device.handle({id: 4, operation: 1, color: colorSchema.parse({white: 255})}).error, /Channel unavailable/);
    const animation = new Animation(`function loop()
      assert(luxio_get_color_type() == 'RGB')
      assert(not pcall(luxio_set_pixel_color, 0, 0, 0, 0, 255))
      luxio_fill(255, 128, 17)
    end`, 3, stripType);
    try { assert.deepEqual(animation.tick()[0], colorSchema.parse({red:255,green:128,blue:17})); } finally { animation.close(); }
  }
  assert.match(device.handle({id: 5, operation: 3, config: {...state.config, stripType: 'UNKNOWN'}}).error, /Unsupported strip/);
});
