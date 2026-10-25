// Opt-in physical-device regression for concurrent reply/broadcast frame writes.
import assert from 'node:assert/strict';
import WebSocket from 'ws';
import { requestHttp, encodeRequest, decodeResponse, operations as op } from '../shared/protocol/src/index.js';
const address = process.env.DEVICE_ADDRESS;
assert.ok(address, 'Set DEVICE_ADDRESS for this physical-device test');
let id = 70000;
const initial = await requestHttp(address, {id: id++, operation: op.GET_STATE});
assert.ok(initial.state);
assert.equal(initial.state.deviceId, process.env.EXPECTED_DEVICE_ID, 'Wrong device; no changes made');
const ws = new WebSocket(`ws://${address}/api/ws`);
let events = 0, replies = 0, pongs = 0, failure: Error | undefined;
ws.on('pong', () => {pongs++;});
const pending = new Map<number, {resolve: () => void; reject: (error: Error) => void}>();
ws.on('message', bytes => {
  try {
    const response = decodeResponse(new Uint8Array(bytes as Buffer));
    assert.equal(response.error, '');
    assert.equal(response.state?.deviceId, initial.state!.deviceId);
    if (!response.id) events++;
    else {
      const waiting = pending.get(response.id);
      assert.ok(waiting, 'Unexpected response ID');
      pending.delete(response.id); replies++; waiting.resolve();
    }
  } catch (error) {
    failure = error as Error;
    for (const waiting of pending.values()) waiting.reject(failure);
    pending.clear();
  }
});
await new Promise<void>((resolve, reject) => {ws.once('open', resolve); ws.once('error', reject);});
const alternate = initial.state.brightness === 256 ? 255 : initial.state.brightness + 1;
try {
  for (let round = 0; round < 60; round++) {
    await requestHttp(address, {id: id++, operation: op.SET_BRIGHTNESS, brightness: round % 2 ? initial.state.brightness : alternate});
    ws.ping();
    const until = Date.now() + 350;
    do {
      if (failure) throw failure;
      const requestId = id++;
      await new Promise<void>((resolve, reject) => {
        const timer = setTimeout(() => {pending.delete(requestId); reject(new Error('WebSocket response timed out'));}, 10000);
        pending.set(requestId, {resolve: () => {clearTimeout(timer); resolve();}, reject: error => {clearTimeout(timer); reject(error);}});
        ws.send(encodeRequest({id: requestId, operation: op.GET_STATE}));
      });
    } while (Date.now() < until);
  }
  if (failure) throw failure;
  assert.ok(events >= 10 && replies >= 60 && pongs >= 50, `Insufficient concurrency: events=${events}, replies=${replies}, pongs=${pongs}`);
} finally {
  ws.close();
  const current = await requestHttp(address, {id: id++, operation: op.GET_STATE});
  if (current.state?.brightness === alternate) {
    await requestHttp(address, {id: id++, operation: op.SET_BRIGHTNESS, brightness: initial.state.brightness});
  }
}
console.log(`PASS: ${replies} WebSocket replies interleaved with ${events} broadcasts and ${pongs} PONGs; brightness restored`);
