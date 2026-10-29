// Requires an installed SDK; downloads checksum-pinned MbedTLS sources if absent.
// Test keys exist only in this process; production trust files are never modified.
import { createHash, generateKeyPairSync, sign } from 'node:crypto';
import { access, readFile, writeFile, mkdir, copyFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { otaPayload } from './ota-signature.mjs';
const crypto4 = process.env.LUXIO_CRYPTO4;
const root = resolve(crypto4 ? '.sdk/s31/ota-verifier-test' : '.sdk/ota-verifier-test'); await mkdir(root, { recursive: true });
const source = resolve('.sdk/tls/mbedtls-b5d87eaa6748b7a6fa70593178c08b4480e9b71e');
if (!crypto4) {
try { await access(`${source}/library/Makefile`); } catch {
  await mkdir(resolve('.sdk/tls'), { recursive: true });
  const response = await fetch('https://codeload.github.com/espressif/mbedtls/tar.gz/b5d87eaa6748b7a6fa70593178c08b4480e9b71e', { signal: AbortSignal.timeout(120000) });
  if (!response.ok) throw new Error(`MbedTLS source download: HTTP ${response.status}`);
  const bytes = Buffer.from(await response.arrayBuffer());
  if (createHash('sha256').update(bytes).digest('hex') !== 'e6beb4ed862c83daba07f0d8091c0c361c843623b06674b2d8effe00e8e7aa9d') throw new Error('MbedTLS source checksum mismatch');
  const archive = resolve('.sdk/tls/signature-test-source.tar.gz'); await writeFile(archive, bytes);
  execFileSync('tar', ['-xzf', archive, '-C', resolve('.sdk/tls')]);
}
execFileSync('make', ['-C', `${source}/library`, '-j4', 'libmbedcrypto.a'], { stdio: 'inherit' });
}
for (const file of ['signed_update.h', 'update_job.h']) await copyFile(`Luxio/src/${file}`, `${root}/${file}`);
const keys = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const wrongKeys = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const pem = keys.publicKey.export({ format: 'pem', type: 'spki' });
await writeFile(`${root}/ota_public_key.h`, `#pragma once\nconstexpr char LUXIO_OTA_KEY_ID[] = "test-release";\nconstexpr char LUXIO_OTA_PUBLIC_KEY[] = R"KEY(${pem})KEY";\n`);
for (const { chip, target } of JSON.parse(await readFile('targets.json', 'utf8'))) {
  const manifest = { keyId: 'test-release', chip, target, build: 32, channel: 0, size: 123456, sha256: 'a'.repeat(64) };
  const signature = sign('sha256', otaPayload(manifest), keys.privateKey).toString('base64');
  const wrong = sign('sha256', otaPayload(manifest), wrongKeys.privateKey).toString('base64');
  const other = sign('sha256', otaPayload({ ...manifest, chip: 'esp32h2' }), keys.privateKey).toString('base64');
  const otherTarget = 'unsupported-layout';
  const otherLayout = sign('sha256', otaPayload({ ...manifest, target: otherTarget }), keys.privateKey).toString('base64');
  await writeFile(`${root}/vector.h`, `constexpr char TEST_SIGNATURE[] = "${signature}";\nconstexpr char WRONG_KEY_SIGNATURE[] = "${wrong}";\nconstexpr char OTHER_LAYOUT[] = "${otherTarget}";\nconstexpr char OTHER_LAYOUT_SIGNATURE[] = "${otherLayout}";\nconstexpr char OTHER_CHIP_SIGNATURE[] = "${other}";\n`);
  execFileSync('c++', ['-std=c++17', '-fsanitize=address,undefined', `-DCONFIG_IDF_TARGET="${chip}"`, `-DLUXIO_UPDATE_LAYOUT="${target}"`,
    ...(crypto4 ? ['-DESP_IDF_VERSION_MAJOR=6', `-I${crypto4}/source/tf-psa-crypto/include`, `-I${crypto4}/source/tf-psa-crypto/drivers/builtin/include`, `-I${crypto4}/build/tf-psa-crypto/include`] : [`-I${source}/include`]),
    `-I${root}`, crypto4 ? '-I.sdk/s31/arduino/packages/esp32/tools/esp32-arduino-libs/idf-release_v6.1-9a97f6c5-v2/esp32s31/include/espressif__cjson/cJSON' : '-I.sdk/arduino/packages/esp32/tools/esp32-arduino-libs/idf-release_v5.5-f1a1df9b-v3/esp32s3/include/json/cJSON',
    'tools/test-ota-signatures.cpp', ...(crypto4 ? JSON.parse(await readFile(`${crypto4}/libraries.json`, 'utf8')) : [`${source}/library/libmbedcrypto.a`]), '-o', `${root}/verify-${chip}`], { stdio: 'inherit' });
  execFileSync(`${root}/verify-${chip}`, [], { stdio: 'inherit' });
}
