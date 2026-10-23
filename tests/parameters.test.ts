import assert from 'node:assert/strict';
import { test } from 'node:test';
import { parseParameters, updateParameters } from '../shared/protocol/src/parameters.js';
import { Animation } from '../shared/device/src/lua.js';
import { encodeResponse, decodeResponse } from '../shared/protocol/src/index.js';
import { Device } from '../shared/device/src/device.js';
const source = `-- @parameter Flicker,Boolean,true
-- @parameter Speed,Number,50,-10,100 // comment
local frames = 0
function loop()
 frames = frames + 1
 luxio_set_pixel_color(0, frames, luxio_get_parameter('Speed'), luxio_get_parameter('Flicker') and 255 or 0)
end`;
test('parameter validation and atomic patch', () => {
  const p = parseParameters(source); assert.equal(p.length, 2); assert.equal(p[0].value, 1); assert.equal(p[1].minimum, -10);
  for (const declaration of ['-- @parameter Speed,Number,101,0,100', '-- @parameter Speed,Number,1,0,0', '-- @parameter X,Number,NaN,0,100', '-- @parameter X,Boolean,1', '-- @parameter X,Text,a', '-- @parameter Speed,Number,1,0-100', '-- @parameter X,Number,1e999,0,100']) assert.throws(() => parseParameters(declaration));
  assert.throws(() => parseParameters('-- @parameter X,Boolean,true\n-- @parameter X,Boolean,false'));
  assert.throws(() => parseParameters('function loop() end\n-- @parameter X,Boolean,true'));
  assert.throws(() => parseParameters(Array.from({ length: 17 }, (_, i) => `-- @parameter P${i},Boolean,true`).join('\n')));
  for (const patch of [[{ ...p[1], value: 101 }], [{ ...p[0], value: .5 }], [{ ...p[1], boolean: true }], [{ ...p[1], name: 'Missing' }], [p[0], p[0]], [{ ...p[1], value: NaN }]]) assert.throws(() => updateParameters(p, patch));
  assert.equal(p[1].value, 50);
});
test('live values preserve VM, frames, elapsed time, and saved values', () => {
  const animation = new Animation(source, 1, 'WS2812'); animation.tick(); animation.tick();
  animation.update([{ ...animation.parameters[0], value: 0 }, { ...animation.parameters[1], value: 99 }]);
  assert.deepEqual(animation.tick()[0], { red: 3, green: 99, blue: 0, white: 0, warmWhite: 0, coolWhite: 0 });
  assert.equal(animation.elapsed, 99); animation.close();
  const device = new Device('parameters');
  const saved = device.handle({ id: 1, operation: 7, script: { id: '', name: 'Test', source, parameters: [{ ...parseParameters(source)[1], value: 80 }] } }).script!;
  assert.equal(saved.parameters![1].value, 80);
  assert.equal(device.handle({ id: 2, operation: 10, scriptId: saved.id }).state?.parameters?.[1].value, 80);
  assert.ok(device.handle({ id: 3, operation: 32, scriptId: 'wrong', parameters: [] }).error);
  assert.equal(device.handle({ id: 4, operation: 32, scriptId: saved.id, parameters: [{ ...saved.parameters![1], value: 90 }] }).state?.parameters?.[1].value, 90);
  assert.equal(device.scripts[0].parameters![1].value, 80);
  const rebooted = new Device('parameters', { state: device.state, scripts: device.scripts });
  assert.equal(rebooted.handle({ id: 5, operation: 10, scriptId: saved.id }).state?.parameters?.[1].value, 80);
  device.animation?.close(); rebooted.animation?.close();
});

test('control types survive source parsing, wire transport, saves and playback', () => {
  const source = `-- @parameter Speed,Slider,50,0,100
-- @parameter Spacing,Number,8,1,64
-- @parameter Pulse,Checkbox,true
-- @parameter Legacy,Boolean,false
function loop() end`;
  const parameters = parseParameters(source);
  assert.deepEqual(parameters.map(p => [p.boolean, p.numberInput]), [[false, false], [false, true], [true, false], [true, false]]);
  const device = new Device('control-types');
  const response = device.handle({ id: 1, operation: 7, script: { id: '', name: 'Controls', source, parameters } });
  assert.equal(response.error, undefined);
  assert.deepEqual(decodeResponse(encodeResponse(response)).script?.parameters, parameters);
  const playing = device.handle({ id: 2, operation: 10, scriptId: response.script!.id });
  assert.deepEqual(decodeResponse(encodeResponse(playing)).state?.parameters, parameters);
  // Patches cannot turn a numeric field into a slider or change its bounds.
  assert.equal(updateParameters(parameters, [{ ...parameters[1], numberInput: false, minimum: -100, value: 4 }])[1].numberInput, true);
  for (const source of ['-- @parameter X,Slider,2,0,1', '-- @parameter X,Checkbox,1', '-- @parameter X,Slider,1', '-- @parameter X,Checkbox,true,0,1']) assert.throws(() => parseParameters(source));
  device.animation?.close();
});
