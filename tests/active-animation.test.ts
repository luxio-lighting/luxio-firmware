import test from 'node:test';
import assert from 'node:assert/strict';
import { Device } from '../shared/device/src/device.js';

test('active source includes RAM animations and live parameters; revision tracks restarts only', () => {
  const device = new Device('preview-test');
  const source = '-- @parameter Speed,Number,50,0,100\nfunction loop() end';
  assert.match(device.handle({ id: 1, operation: 6 }).error!, /No animation/);
  const started = device.handle({ id: 2, operation: 31, script: { id: '', name: 'Test', source } });
  assert.equal(started.error, undefined);
  const revision = started.state!.animationRevision;
  const snapshot = device.handle({ id: 3, operation: 6 });
  assert.equal(snapshot.script!.source, source);
  assert.equal(snapshot.script!.id, '');
  assert.equal(snapshot.state!.animationRevision, revision);
  device.handle({ id: 4, operation: 32, script: { id: '', name: 'Test', source: '' }, parameters: [{ name: 'Speed', boolean: false, value: 75, defaultValue: 50, minimum: 0, maximum: 100 }] });
  assert.equal(device.handle({ id: 5, operation: 6 }).script!.parameters![0].value, 75);
  assert.equal(device.state.animationRevision, revision);
  device.handle({ id: 6, operation: 31, script: { id: '', name: 'Test', source: source + '\n-- changed' } });
  assert.notEqual(device.state.animationRevision, revision);
  device.handle({ id: 7, operation: 15, on: false });
  assert.equal(device.handle({ id: 8, operation: 6 }).script!.source, source + '\n-- changed');
  device.handle({ id: 9, operation: 11 });
  assert.match(device.handle({ id: 10, operation: 6 }).error!, /No animation/);
  assert.equal(device.state.animationRevision, 0);
});
