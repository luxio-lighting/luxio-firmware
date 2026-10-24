import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createPublicKey } from 'node:crypto';
test('publisher and both firmware programs embed the same P-256 public key', async () => {
  const trust = JSON.parse(await readFile('ota-public-key.json', 'utf8'));
  assert.equal(createPublicKey(trust.pem).asymmetricKeyDetails?.namedCurve, 'prime256v1');
  for (const sketch of ['Luxio', 'Recovery']) {
    const header = await readFile(`${sketch}/src/ota_public_key.h`, 'utf8');
    assert.ok(header.includes(`LUXIO_OTA_KEY_ID[] = "${trust.keyId}"`));
    assert.ok(header.includes(trust.pem));
    assert.doesNotMatch(header, /PRIVATE KEY/);
  }
  assert.equal(await readFile('Luxio/src/signed_update.h', 'utf8'), await readFile('Recovery/src/signed_update.h', 'utf8'));
  assert.equal(await readFile('Luxio/src/update_job.h', 'utf8'), await readFile('Recovery/src/update_job.h', 'utf8'));
});
