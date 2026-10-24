import { createPrivateKey, createPublicKey, sign, verify } from 'node:crypto';
import trust from '../ota-public-key.json' with { type: 'json' };

// ASCII, fixed field order, decimal integers, LF separators including the final LF.
// The hash authenticates the downloaded bytes independently of their transport URL.
export function otaPayload(manifest) {
  return Buffer.from(`luxio-ota-v1\n${manifest.keyId}\n${manifest.chip}\n${manifest.target}\n${manifest.build}\n${manifest.channel ?? 100}\n${manifest.size}\n${manifest.sha256}\n`, 'utf8');
}
export function signUpdate(manifest, privatePem) {
  const key = createPrivateKey(privatePem);
  if (key.asymmetricKeyType !== 'ec' || key.asymmetricKeyDetails?.namedCurve !== 'prime256v1' ||
      !createPublicKey(key).equals(createPublicKey(trust.pem))) throw new Error('OTA_SIGNING_KEY does not match the embedded release key');
  manifest.signatureVersion = 1;
  manifest.keyId = trust.keyId;
  manifest.channel ??= 100;
  manifest.signature = sign('sha256', otaPayload(manifest), key).toString('base64');
  if (!verify('sha256', otaPayload(manifest), trust.pem, Buffer.from(manifest.signature, 'base64'))) throw new Error('OTA signature self-check failed');
}
