import assert from 'node:assert/strict';
import { requestHttp } from '../shared/protocol/src/index.js';

const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS for this opt-in hardware test');
const duration = Number(process.env.TEST_SECONDS || 600);
assert.ok(Number.isFinite(duration) && duration >= 180);
let id = 280000;
const initial = await requestHttp(address, { id: id++, operation: 0 });
assert.equal(initial.state!.mode, 2, 'Start an animation before testing');
assert.ok(initial.networkStatus, 'Network diagnostics required');
const started = Date.now();
let checks = 0, updates = 0, minimumFree = Infinity, maximumLatency = 0;
let previousUptime = initial.networkStatus.uptimeMs;
let idleDone = false;
let nextUpdate = 0;
while (Date.now() - started < duration * 1000) {
  const elapsed = Math.round((Date.now() - started) / 1000);
  if (!idleDone && elapsed >= 120) {
    console.log('Pausing requests for 60 seconds');
    await new Promise(resolve => setTimeout(resolve, 60000));
    idleDone = true;
  }
  const began = Date.now();
  const reply = await requestHttp(address, { id: id++, operation: 0 });
  const latency = Date.now() - began;
  const state = reply.state!;
  const network = reply.networkStatus!;
  assert.equal(state.mode, 2, reply.animationError || 'Animation stopped without a Lua error');
  assert.equal(state.scriptName, initial.state!.scriptName);
  assert.equal(state.activeScriptId, initial.state!.activeScriptId);
  assert.equal(state.brightness, initial.state!.brightness);
  assert.equal(state.on, initial.state!.on);
  assert.equal(reply.animationError, '');
  assert.ok(network.uptimeMs >= previousUptime, 'Device restarted');
  previousUptime = network.uptimeMs;
  assert.equal(network.disconnectCount, initial.networkStatus.disconnectCount);
  minimumFree = Math.min(minimumFree, network.internalFree);
  maximumLatency = Math.max(maximumLatency, latency);
  assert.ok(network.internalFree >= 12000, `Internal RAM fell to ${network.internalFree} bytes`);
  if (checks++ % 10 === 0) {
    for (const operation of [5, 18, 25]) await requestHttp(address, { id: id++, operation });
    console.log(JSON.stringify({ elapsed, checks, updates, minimumFree, maximumLatency, animation: state.scriptName }));
  }
  if (elapsed >= nextUpdate && !reply.updateChecking) {
    // Check only: never download/install firmware or reboot the device.
    await requestHttp(address, { id: id++, operation: 13 });
    updates++;
    nextUpdate = elapsed + 30;
  }
  await new Promise(resolve => setTimeout(resolve, 1000));
}
console.log(JSON.stringify({ result: 'PASS', seconds: Math.round((Date.now() - started) / 1000), checks, updates, minimumFree, maximumLatency }));
