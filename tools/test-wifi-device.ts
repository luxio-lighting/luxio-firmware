// Opt-in radio regression; does not change credentials, light state or scripts.
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { requestHttp, encodeRequest, decodeResponse, frame, operations, type Response } from '../shared/protocol/src/index.js';
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS');
let id = 1;
const before = (await requestHttp(address, { id: id++, operation: operations.GET_STATE })).state!;
assert.ok(before.build >= 12);
const library = (await requestHttp(address, { id: id++, operation: operations.LIST_SCRIPTS })).scripts;
for (const transport of process.env.DEVICE_SERIAL ? ['http', 'usb'] : ['http']) {
  const start = Date.now();
  let result: Response;
  if (transport === 'usb') {
    const bytes = frame(encodeRequest({ id: id++, operation: operations.SCAN_WIFI }));
    const { stdout } = await promisify(execFile)('.sdk/venv/bin/python', ['-c', `
import serial, sys, time, struct, base64
with serial.Serial(sys.argv[1], 115200, timeout=1) as port:
    port.write(base64.b64decode(sys.argv[2]))
    data = bytearray()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        data.extend(port.read(port.in_waiting or 1))
        start = data.find(b'LXP1')
        if start < 0 or len(data) < start + 8: continue
        length = struct.unpack('>I', data[start+4:start+8])[0]
        if len(data) >= start + 8 + length:
            print(base64.b64encode(data[start+8:start+8+length]).decode())
            break
    else: raise RuntimeError('USB response timed out')
`, process.env.DEVICE_SERIAL!, Buffer.from(bytes).toString('base64')]);
    result = decodeResponse(Buffer.from(stdout.trim(), 'base64'));
  } else result = await requestHttp(address, { id: id++, operation: operations.SCAN_WIFI });
  assert.equal(result.error, ''); assert.equal(result.wifiScanning, true);
  assert.ok(Date.now() - start < 2000, 'SCAN_WIFI must respond before the radio scan completes');
  assert.equal((await requestHttp(address, { id: id++, operation: operations.SCAN_WIFI })).wifiScanning, true);
  let polls = 0;
  do {
    assert.ok(Date.now() - start < 20000, 'Scan did not finish');
    await new Promise(resolve => setTimeout(resolve, 300));
    result = await requestHttp(address, { id: id++, operation: operations.GET_STATE });
    assert.deepEqual(result.state, before, 'Scan changed device controls or connection');
    polls++;
  } while (result.wifiScanning);
  assert.equal(result.wifiScanError, '');
  const networks = result.wifiNetworks!;
  assert.ok(networks.length > 0 && networks.length <= 32, 'Expected nearby networks');
  assert.equal(new Set(networks.map(n => JSON.stringify([n.ssid, n.passwordRequired]))).size, networks.length);
  networks.forEach((network, i) => {
    assert.ok(Buffer.byteLength(network.ssid) > 0 && Buffer.byteLength(network.ssid) <= 32);
    assert.ok(Number.isInteger(network.rssi) && network.rssi < 0);
    assert.equal(typeof network.passwordRequired, 'boolean');
    if (i) assert.ok(networks[i - 1].rssi >= network.rssi);
  });
  assert.deepEqual((await requestHttp(address, { id: id++, operation: operations.GET_STATE })).wifiNetworks, networks);
  console.log(`PASS ${transport}: ${networks.length} networks, ${polls} responsive polls, ${Date.now() - start} ms; sorted, deduplicated, cached, controls preserved`);
}
assert.deepEqual((await requestHttp(address, { id: id++, operation: operations.LIST_SCRIPTS })).scripts, library);
console.log('PASS: script library preserved');
