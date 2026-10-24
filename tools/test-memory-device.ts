import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import WebSocket from 'ws';
import { requestHttp, encodeRequest, decodeResponse, operations as op } from '../shared/protocol/src/index.js';
let id = 30000;
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS for this opt-in physical-device test');
const initial = await requestHttp(address, {id: id++, operation: op.GET_STATE});
if (process.env.EXPECTED_BUILD) assert.equal(initial.state?.build, Number(process.env.EXPECTED_BUILD));
assert.equal(initial.state?.mode, 2);
assert.ok(initial.state, 'Device state required');
await mkdir('.sdk', {recursive: true});
const ws = new WebSocket(`ws://${address}/api/ws`);
let events = 0, replies = 0, websocketError = '';
ws.on('message', bytes => {
  const response = decodeResponse(new Uint8Array(bytes as Buffer));
  if (response.error) websocketError = response.error;
  if (response.id) replies++; else events++;
});
await new Promise<void>((resolve, reject) => {ws.once('open', resolve); ws.once('error', reject);});
const active = (await requestHttp(address, {id: id++, operation: op.GET_SCRIPT})).script!;
const results: unknown[] = [];
try {
  await requestHttp(address, {id: id++, operation: op.SET_CONFIG, config: {...initial.state!.config, ledCount: 100}});
  await requestHttp(address, {id: id++, operation: op.SET_SCRIPT, script: active});
  let minFree = Infinity, minBlock = Infinity, polls = 0;
  for (let round = 0; round < 30; round++) {
    const checked = await requestHttp(address, {id: id++, operation: op.CHECK_UPDATE});
    assert.ok(checked.updateChecking);
    let complete = false;
    for (let n = 0; n < 150; n++) {
      ws.send(encodeRequest({id: id++, operation: op.GET_STATE}));
      const response = await requestHttp(address, {id: id++, operation: op.GET_STATE});
      assert.equal(response.state?.mode, 2, response.animationError);
      assert.equal(response.animationError, '');
      assert.equal(response.state?.scriptName, initial.state?.scriptName, 'Lighting selection changed; stopping test');
      assert.equal(response.state?.config.ledCount, 100);
      assert.equal(response.updateError, '');
      assert.equal(websocketError, '');
      assert.ok(response.networkStatus);
      minFree = Math.min(minFree, response.networkStatus!.internalFree);
      minBlock = Math.min(minBlock, response.networkStatus!.internalLargestBlock);
      polls++;
      if (!response.updateChecking) {complete = true; break;}
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    assert.ok(complete, 'Update did not complete');
    const result = {round: round + 1, minFree, minBlock, polls, events, replies};
    results.push(result); console.log(JSON.stringify(result));
    await new Promise(resolve => setTimeout(resolve, 200));
  }
  assert.ok(events >= 1 && replies > 30);
} finally {
  ws.close();
  const current = await requestHttp(address, {id: id++, operation: op.GET_STATE});
  // Restore only the LED count changed by this test; preserve newer app controls.
  if (current.state?.config.ledCount === 100) {
    const currentScript = current.state.mode === 2 ? (await requestHttp(address, {id: id++, operation: op.GET_SCRIPT})).script : undefined;
    await requestHttp(address, {id: id++, operation: op.SET_CONFIG, config: {...current.state.config, ledCount: initial.state!.config.ledCount}});
    if (currentScript) await requestHttp(address, {id: id++, operation: op.SET_SCRIPT, script: currentScript});
    else if (current.state.mode === 1 && current.state.gradient) await requestHttp(address, {id: id++, operation: op.SET_GRADIENT, gradient: current.state.gradient});
    else await requestHttp(address, {id: id++, operation: op.SET_COLOR, color: current.state.color});
    await requestHttp(address, {id: id++, operation: op.SET_POWER, on: current.state.on});
  }
  await writeFile('.sdk/memory-device-results.json', JSON.stringify(results, null, 2));
}
console.log('PASS: 30 signed-HTTP update checks, 100 LEDs, active animation, concurrent HTTP and WebSocket polling/broadcasts');
