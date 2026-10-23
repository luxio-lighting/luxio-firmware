import test from 'node:test';
import assert from 'node:assert/strict';
import { parsePreview } from '../shared/protocol/src/preview.js';
import { Device } from '../shared/device/src/device.js';
import { encodeResponse, decodeResponse } from '../shared/protocol/src/index.js';

test('preview accepts ordered hex colors and rejects malformed or misplaced annotations', () => {
  const colors = parsePreview('-- @preview #Ff0000, 00ff00,0000ff // hint\r\nfunction loop() end');
  assert.deepEqual(colors.map(c => [c.red,c.green,c.blue]), [[255,0,0],[0,255,0],[0,0,255]]);
  assert.equal(parsePreview('function loop() end').length, 0);
  for (const source of ['-- @preview ff0000','-- @preview ff0000,','-- @preview fff,000000','-- @preview zz0000,000000','-- @preview ff0000,000000\n-- @preview 000000,ffffff','function loop() end\n-- @preview ff0000,000000', '-- @preview ' + Array(9).fill('ff0000').join(',')]) assert.throws(() => parsePreview(source));
});

test('preview survives CRUD, listings and RAM playback without controlling the LEDs', () => {
  const device = new Device('preview-test');
  const source = '-- @preview ff0000,00ff00,0000ff\nfunction loop() luxio_set_pixel_color(0,1,2,3) end';
  const preview = parsePreview(source);
  const saved = device.handle({id:1, operation:7, script:{id:'',name:'Preview',source,preview:[]}}).script!;
  assert.deepEqual(saved.preview, preview);
  const listing = decodeResponse(encodeResponse(device.handle({id:2,operation:5})));
  assert.equal(listing.scripts[0].source, ''); assert.deepEqual(listing.scripts[0].preview, preview);
  device.handle({id:3,operation:10,scriptId:saved.id});
  assert.deepEqual(device.state.animationPreview, preview);
  const response = decodeResponse(encodeResponse(device.handle({id:4,operation:0})));
  assert.deepEqual(response.state!.animationPreview,preview);
  device.state.brightness = 256;
  const pixel = device.output()[0]; assert.deepEqual([pixel.red,pixel.green,pixel.blue],[1,2,3]);
  const invalid = device.handle({id:5,operation:31,script:{id:'',name:'Invalid',source:'-- @preview bad\nfunction loop() end'}});
  assert.ok(invalid.error); assert.deepEqual(device.state.animationPreview,preview);
  device.handle({id:6,operation:31,script:{id:'',name:'RAM',source}});
  assert.equal(device.state.activeScriptId,''); assert.deepEqual(device.state.animationPreview,preview);
  device.handle({id:7,operation:31,script:{id:'',name:'No hint',source:'function loop() end'}});
  assert.deepEqual(device.state.animationPreview,[]);
  device.handle({id:8,operation:11}); assert.deepEqual(device.state.animationPreview,[]);
});
