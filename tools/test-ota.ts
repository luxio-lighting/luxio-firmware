// Explicit opt-in physical test; the target must already point at your test cloud.
import assert from 'node:assert/strict';
import { requestHttp } from '../shared/protocol/src/index.js';
const address = process.env.DEVICE_ADDRESS;
const targetBuild = Number(process.env.TARGET_BUILD);
if (!address || !Number.isInteger(targetBuild) || targetBuild < 1) throw new Error('Set DEVICE_ADDRESS and TARGET_BUILD');
let id = 1;
const before = (await requestHttp(address, { id: id++, operation: 0 })).state!;
assert.ok(targetBuild > before.build);
const scripts = (await requestHttp(address, { id: id++, operation: 5 })).scripts.map(script => script.id).sort();
await requestHttp(address, { id: id++, operation: 13 });
let found = false;
for (let attempt = 0; attempt < 30; attempt++) {
  await new Promise(resolve => setTimeout(resolve, 1000));
  const response = await requestHttp(address, { id: id++, operation: 0 });
  if (response.availableBuild === targetBuild) { found = true; break; }
}
assert.ok(found, 'Eligible OTA build was not found');
console.log(`Installing build ${targetBuild} over HTTPS into the inactive slot…`);
await requestHttp(address, { id: id++, operation: 14 });
let after;
for (let attempt = 0; attempt < 90; attempt++) {
  await new Promise(resolve => setTimeout(resolve, 1000));
  try {
    const state = (await requestHttp(address, { id: id++, operation: 0 })).state!;
    if (state.build === targetBuild) { after = state; break; }
  } catch { /* A successful update restarts the network interface. */ }
}
assert.ok(after, 'Device did not boot the target build');
assert.equal(after.deviceId, before.deviceId);
assert.deepEqual(after.config, before.config);
assert.equal(after.brightness, before.brightness);
assert.deepEqual((await requestHttp(address, { id: id++, operation: 5 })).scripts.map(script => script.id).sort(), scripts);
console.log(`PASS: physical OTA ${before.build} → ${after.build}; identity, Wi-Fi, strip settings, brightness and script UUIDs survived`);
