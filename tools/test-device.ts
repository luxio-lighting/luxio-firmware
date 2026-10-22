import assert from 'node:assert/strict';
import { WebSocket } from 'ws';
import { requestHttp, colorSchema, decodeResponse } from '../shared/protocol/src/index.js';
import { presets } from '../shared/web/presets.js';
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS to the physical device to test');
let id = 1;
const initial = (await requestHttp(address, { id: id++, operation: 0 })).state!;
let scriptId: string | undefined;
const socket = new WebSocket(`ws://${address}/api/ws`);
const events: any[] = [];
socket.on('message', bytes => events.push(decodeResponse(new Uint8Array(bytes as Buffer))));
await new Promise<void>((resolve, reject) => { socket.once('open', resolve); socket.once('error', reject); });
try {
  assert.equal(initial.config.stripType, 'WS2805'); assert.equal(initial.config.gpio, 16); assert.equal(initial.config.ledCount, 60);
  await requestHttp(address, { id: id++, operation: 2, brightness: 16 });
  for (const channel of ['red', 'green', 'blue', 'warmWhite', 'coolWhite']) {
    const color = colorSchema.parse({ [channel]: 255 });
    const response = await requestHttp(address, { id: id++, operation: 1, color });
    assert.deepEqual(response.state!.color, color);
    await new Promise(resolve => setTimeout(resolve, 350));
  }
  await assert.rejects(requestHttp(address, { id: id++, operation: 1, color: colorSchema.parse({ white: 255 }) }), /unavailable/);
  await assert.rejects(requestHttp(address, { id: id++, operation: 3, config: { ...initial.config, gpio: 26 } }), /reserved/);
  const created = await requestHttp(address, { id: id++, operation: 7, script: { id: '', name: 'Integration test', source: presets[0].source } });
  assert.ok(created.script?.id); scriptId = created.script.id;
  const renamed = await requestHttp(address, { id: id++, operation: 8, script: { ...created.script, name: 'Renamed integration test' } });
  assert.equal(renamed.script!.id, scriptId);
  const read = await requestHttp(address, { id: id++, operation: 6, scriptId });
  assert.equal(read.script!.source, presets[0].source);
  const listed = await requestHttp(address, { id: id++, operation: 5 });
  assert.ok(listed.scripts.some(script => script.id === scriptId && script.name === 'Renamed integration test' && !script.source));
  await requestHttp(address, { id: id++, operation: 10, scriptId });
  await new Promise(resolve => setTimeout(resolve, 1000));
  assert.equal((await requestHttp(address, { id: id++, operation: 0 })).state!.activeScriptId, scriptId, 'Animation must still be running after repeated Lua loops');
  const dimmed = await requestHttp(address, { id: id++, operation: 2, brightness: 8 });
  assert.equal(dimmed.state!.activeScriptId, scriptId, 'Brightness must preserve animation');
  const colored = await requestHttp(address, { id: id++, operation: 1, color: colorSchema.parse({ warmWhite: 128, coolWhite: 64 }) });
  assert.equal(colored.state!.activeScriptId, '');
  await requestHttp(address, { id: id++, operation: 9, scriptId }); scriptId = undefined;
  await new Promise(resolve => setTimeout(resolve, 500));
  assert.ok(events.some(event => event.id === 0 && event.state), 'Expected unsolicited WebSocket state updates');
  console.log('PASS: HTTP, independent channels, configuration validation, script CRUD/UUID, Lua execution, brightness arbitration, and WebSocket updates.');
} finally {
  if (scriptId) await requestHttp(address, { id: id++, operation: 9, scriptId }).catch(() => {});
  await requestHttp(address, { id: id++, operation: 1, color: initial.color });
  await requestHttp(address, { id: id++, operation: 2, brightness: initial.brightness });
  if (initial.activeScriptId) await requestHttp(address, { id: id++, operation: 10, scriptId: initial.activeScriptId });
  socket.close();
}
