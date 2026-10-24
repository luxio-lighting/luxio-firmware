// Explicit physical recovery test. Requires a test-cloud build on the new layout.
// TARGET_FIRMWARE must be a production application with a higher build number.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFileSync, writeFileSync } from 'node:fs';
// @ts-expect-error Shared publisher module is plain JavaScript.
import { signUpdate } from './ota-signature.mjs';
import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { requestHttp } from '../shared/protocol/src/index.js';
const address = process.env.DEVICE_ADDRESS;
const firmwarePath = process.env.TARGET_FIRMWARE;
const serial = process.env.DEVICE_SERIAL;
const chip = process.env.DEVICE_CHIP;
const origin = process.env.TEST_ORIGIN;
if (!chip || !origin?.startsWith('http://')) throw new Error('Set DEVICE_CHIP and TEST_ORIGIN matching the test firmware build');
if (!address || !firmwarePath || !serial) throw new Error('Set DEVICE_ADDRESS, TARGET_FIRMWARE and DEVICE_SERIAL');
const firmware = readFileSync(firmwarePath);
const build = Number(firmware.subarray(48, 80).toString().split('\0')[0]);
assert.ok(Number.isInteger(build) && build > 0);
const manifest = { chip, target: `${chip}-recovery-v1`, channel: 0, build,
  url: `${origin}/firmware/unused`, size: firmware.length, sha256: createHash('sha256').update(firmware).digest('hex') };
manifest.url = `${origin}/firmware/${manifest.sha256}`;
signUpdate(manifest, process.env.OTA_SIGNING_KEY ?? '');
let tamper = true;
let mode: 'truncate' | 'corrupt' | 'slow' | 'good' = 'truncate';
let updateChecks = 0, downloads = 0;
const server = createServer((req, res) => {
  if (req.url?.startsWith('/updates?')) {
    const url = new URL(req.url, 'http://test');
    assert.equal(url.searchParams.get('target'), manifest.target);
    updateChecks++; const body = JSON.stringify(tamper ? { ...manifest, channel: 100 } : manifest); res.writeHead(200, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) }); res.end(body); return;
  }
  if (req.url === `/firmware/${manifest.sha256}`) {
    downloads++; const selectedMode = mode;
    console.log(`Download ${downloads}: ${selectedMode}`);
    const bytes = Buffer.from(firmware);
    if (selectedMode === 'corrupt') bytes[bytes.length - 80] ^= 1;
    res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length });
    let offset = 0;
    const timer = setInterval(() => {
      if (selectedMode === 'truncate' && offset >= 262144) { clearInterval(timer); res.destroy(); return; }
      const end = Math.min(offset + 8192, bytes.length); res.write(bytes.subarray(offset, end)); offset = end;
      if (offset === bytes.length) { clearInterval(timer); res.end(); }
    }, selectedMode === 'slow' ? 120 : 8);
    res.on('close', () => clearInterval(timer)); return;
  }
  res.writeHead(503); res.end(); // Announcements are tested by the separate real API harness.
});
await new Promise<void>(resolve => server.listen(Number(new URL(origin).port || 80), '0.0.0.0', resolve));
let id = 200;
try {
  const before = (await requestHttp(address, { id: id++, operation: 0 })).state!;
  const scripts = (await requestHttp(address, { id: id++, operation: 5 })).scripts.map(s => s.id).sort();
  assert.ok(build > before.build);
  await new Promise(r => setTimeout(r, 5000));
  assert.equal(updateChecks, 0); assert.equal(downloads, 0);
  await requestHttp(address, { id: id++, operation: 13 });
  let checked;
  for (let i = 0; i < 40; i++) {
    await new Promise(r => setTimeout(r, 500));
    checked = await requestHttp(address, { id: id++, operation: 0 });
    if (!checked.updateChecking) break;
  }
  assert.match(checked?.updateError ?? '', /signature|incompatible/);
  assert.equal(checked?.availableBuild, 0);
  assert.equal(downloads, 0);
  tamper = false;
  await requestHttp(address, { id: id++, operation: 13 });
  for (let i = 0; i < 40; i++) {
    await new Promise(r => setTimeout(r, 500));
    checked = await requestHttp(address, { id: id++, operation: 0 });
    if (!checked.updateChecking) break;
  }
  assert.equal(checked?.updateError, '', 'Explicit update check failed');
  assert.equal(checked?.availableBuild, build);
  await new Promise(r => setTimeout(r, 3000));
  assert.equal(downloads, 0, 'Checking must not install');
  const began = Date.now();
  await requestHttp(address, { id: id++, operation: 14 });
  let status: any;
  for (let i = 0; i < 90; i++) {
    await new Promise(r => setTimeout(r, 1000));
    try { status = await (await fetch(`http://${address}/status`, { signal: AbortSignal.timeout(2000) })).json(); } catch { continue; }
    if (status.error) break;
  }
  assert.match(status?.error ?? '', /interrupted/);
  console.log('PASS: interrupted download leaves recovery online');
  assert.equal((await fetch(`http://${address}/update`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '['.repeat(200) + '0' + ']'.repeat(200) })).status, 400);
  const rollback = { ...manifest, build: before.build }; signUpdate(rollback, process.env.OTA_SIGNING_KEY ?? '');
  for (const invalid of [rollback, { ...manifest, build: build + 1 }, { ...manifest, signature: undefined }, { ...manifest, keyId: 'unknown' }]) {
    assert.equal((await fetch(`http://${address}/update`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(invalid) })).status, 400);
  }
  assert.equal(downloads, 1, 'Invalid signatures must not start downloads');
  console.log('PASS: recovery manual route rejects tampered and unsigned manifests');
  mode = 'corrupt';
  assert.equal((await fetch(`http://${address}/update`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(manifest) })).status, 202);
  for (let i = 0; i < 90; i++) {
    await new Promise(r => setTimeout(r, 1000));
    status = await (await fetch(`http://${address}/status`)).json();
    if (status.error) break;
  }
  assert.match(status.error, /checksum/);
  await new Promise(r => setTimeout(r, 2000)); assert.equal(downloads, 2);
  console.log('PASS: checksum mismatch rejected; no unsolicited retry');
  mode = 'slow';
  assert.equal((await fetch(`http://${address}/retry`, { method: 'POST' })).status, 202);
  for (let i = 0; i < 50; i++) {
    await new Promise(r => setTimeout(r, 500));
    status = await (await fetch(`http://${address}/status`)).json();
    if (status.received > 131072) break;
  }
  assert.ok(status.busy && status.received > 131072 && status.received < firmware.length);
  mode = 'good';
  await promisify(execFile)('.sdk/venv/bin/python', ['-m', 'esptool', '--chip', chip, '--port', serial, '--after', 'hard-reset', 'chip-id']);
  console.log('Reset during partial write; awaiting saved-job recovery…');
  let after;
  for (let i = 0; i < 120; i++) {
    await new Promise(r => setTimeout(r, 1000));
    try { const result = await requestHttp(address, { id: id++, operation: 0 }); if (result.state?.build === build) { after = result.state; break; } } catch { }
  }
  assert.ok(after, 'New firmware did not boot');
  assert.equal(after.deviceId, before.deviceId); assert.deepEqual(after.config, before.config);
  assert.equal(after.brightness, before.brightness); assert.equal(after.on, before.on);
  assert.deepEqual((await requestHttp(address, { id: id++, operation: 5 })).scripts.map(s => s.id).sort(), scripts);
  if (!after.matterDisabled) { const pairing = await requestHttp(address, { id: id++, operation: 12 }); assert.ok(pairing.pairingCode.length >= 11); assert.ok(pairing.pairingQr.startsWith('MT:')); }
  assert.equal(downloads, 4);
  const result = { fromBuild: before.build, toBuild: build, downloads, updateChecks, testSeconds: (Date.now()-began)/1000, identityPreserved: true, settingsPreserved: true, scriptsPreserved: true, matterPairingAvailable: !after.matterDisabled };
  writeFileSync('.sdk/recovery-test-result.json', JSON.stringify(result, null, 2));
  console.log('PASS: reset recovery, verified main boot, preserved data', result);
} finally { server.closeAllConnections(); await new Promise<void>(resolve => server.close(() => resolve())); }
