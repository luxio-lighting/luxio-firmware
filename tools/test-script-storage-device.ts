// Opt-in hardware test. Creates/removes only its own scripts and restores output.
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { readFile, writeFile, readdir } from 'node:fs/promises';
import { requestHttp, encodeRequest, decodeResponse, type Script, type State } from '../shared/protocol/src/index.js';
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS to the physical device');
let id = 1;
const initial = (await requestHttp(address, { id: id++, operation: 0 })).state!;
assert.ok(initial.build >= 10);
const original = (await requestHttp(address, { id: id++, operation: 5 })).scripts;
const sources = await Promise.all((await readdir('tools/fixtures/scripts')).filter(name => name.endsWith('.lua')).sort().map(name => readFile(`tools/fixtures/scripts/${name}`, 'utf8')));
const created: Script[] = [];
await writeFile('.sdk/storage10-test-before.json', JSON.stringify({ initial, original }));
try {
  let full = false;
  for (let i = 0; i < 512; i++) {
    try {
      const script = (await requestHttp(address, { id: id++, operation: 7, script: { id: '', name: `Storage test ${i}`, source: sources[i % sources.length], mode: 2 } })).script!;
      created.push(script);
      await writeFile('.sdk/storage10-test-created.json', JSON.stringify(created));
    } catch (error) {
      assert.match(String(error), /storage is full|storage full/); full = true; break;
    }
  }
  assert.ok(full, 'Expected to reach storage capacity within 512 items');
  assert.ok(created.length + original.length > 64, 'Expected more than one page');
  const all = (await requestHttp(address, { id: id++, operation: 5 })).scripts;
  assert.equal(all.length, created.length + original.length);
  assert.equal(new Set(all.map(s => s.id)).size, all.length);
  // Inspect a single wire page (requestHttp intentionally collects all pages).
  const raw = await fetch(`http://${address}/api`, { method: 'POST', body: encodeRequest({ id: id++, operation: 5 }) as BodyInit });
  const first = decodeResponse(new Uint8Array(await raw.arrayBuffer()));
  assert.equal(first.scripts.length, 64); assert.equal(first.nextScriptOffset, 64);
  for (const item of created) {
    const saved: Script = (await requestHttp(address, { id: id++, operation: 6, scriptId: item.id })).script!;
    assert.equal(saved.source, item.source); assert.equal(saved.name, item.name); assert.equal(saved.mode, item.mode);
  }
  // A too-large replacement at capacity must leave the existing file intact.
  const target = created[0];
  const large = '--' + Array.from(randomBytes(16382), byte => String.fromCharCode(33 + byte % 90)).join('');
  await assert.rejects(requestHttp(address, { id: id++, operation: 8, script: { ...target, source: large } }), /storage is full|storage full/);
  assert.equal((await requestHttp(address, { id: id++, operation: 6, scriptId: target.id })).script!.source, target.source);
  console.log(`PASS: ${all.length} saved scripts (${original.length} existing + ${created.length} test), pagination, exact source/mode readback and full-storage replacement safety`);
  await requestHttp(address, { id: id++, operation: 2, brightness: 16 });
  for (const item of created.slice(-2)) {
    const state: State = (await requestHttp(address, { id: id++, operation: 10, scriptId: item.id })).state!;
    assert.equal(state.mode, item.mode); assert.equal(state.activeScriptId, item.id);
  }
  // Free test records, then prove maximum-size source and a rename survive.
  for (const item of created.slice(1)) {
    await requestHttp(address, { id: id++, operation: 9, scriptId: item.id });
    created.splice(created.indexOf(item), 1);
  }
  await requestHttp(address, { id: id++, operation: 8, script: { ...target, name: 'Storage test renamed', source: large } });
  const renamed = (await requestHttp(address, { id: id++, operation: 6, scriptId: target.id })).script!;
  assert.equal(renamed.source, large); assert.equal(renamed.name, 'Storage test renamed');
  console.log('PASS: animations/gradients run; 16 KiB source and rename round-trip');
} finally {
  for (const item of created) {
    try { await requestHttp(address, { id: id++, operation: 9, scriptId: item.id }); }
    catch (error) { console.error(`Cleanup failed for ${item.id}: ${error}`); }
  }
  if (initial.activeScriptId) await requestHttp(address, { id: id++, operation: 10, scriptId: initial.activeScriptId });
  else await requestHttp(address, { id: id++, operation: 1, color: initial.color });
  await requestHttp(address, { id: id++, operation: 2, brightness: initial.brightness });
  await requestHttp(address, { id: id++, operation: 15, on: initial.on });
  const after = (await requestHttp(address, { id: id++, operation: 5 })).scripts;
  assert.deepEqual(after.map(s => s.id).sort(), original.map(s => s.id).sort());
  console.log('PASS: test records removed; original library, output and brightness restored');
}
