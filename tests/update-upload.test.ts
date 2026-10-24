import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, rm, readFile } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { createHash, generateKeyPairSync } from 'node:crypto';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const targets = JSON.parse(await readFile('targets.json', 'utf8'));
for (const hardware of targets) {
  test(`MongoDB publisher validates and uploads ${hardware.chip}`, async () => {
    await mkdir('.sdk', { recursive: true });
    const directory = await mkdtemp(resolve('.sdk/upload-test-'));
    const keys = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
    await mkdir(resolve(directory, 'tools'));
    for (const file of ['publish-update.mjs', 'ota-signature.mjs']) await writeFile(resolve(directory, 'tools', file), await readFile(resolve('tools', file)));
    await writeFile(resolve(directory, 'targets.json'), JSON.stringify(targets));
    await writeFile(resolve(directory, 'ota-public-key.json'), JSON.stringify({ keyId: 'test-release', pem: keys.publicKey.export({ format: 'pem', type: 'spki' }).toString() }));
    try {
      for (const scenario of ['valid', 'retry', 'wrong chip', 'bad checksum', 'upload failed', 'download failed']) {
        const parts = [];
        for (const [address, file] of [[hardware.bootloaderAddress, 'bootloader.bin'], [0x8000, 'partitions.bin'], [0xe000, 'otadata.bin'], [0x10000, 'recovery.bin'], [hardware.applicationAddress, 'luxio.bin']] as const) {
          const bytes = Buffer.alloc(address === hardware.applicationAddress ? 288 : 32);
          if ([hardware.bootloaderAddress, 0x10000, hardware.applicationAddress].includes(address)) {
            bytes[0] = 0xe9;
            bytes.writeUInt16LE(scenario === 'wrong chip' && file === 'luxio.bin' ? 99 : hardware.imageChipId, 12);
          }
          await writeFile(resolve(directory, file), bytes);
          parts.push({ address, file, size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') });
        }
        const manifest = { chip: hardware.chip, target: `${hardware.chip}-recovery-v1`, build: 24, channel: 100,
          minimumFlashSize: hardware.minimumFlashSize, size: 288, file: 'luxio.bin', sha256: parts[4].sha256, parts };
        if (scenario === 'bad checksum') parts[4].sha256 = manifest.sha256 = '0'.repeat(64);
        const path = resolve(directory, 'manifest.local.json');
        await writeFile(path, JSON.stringify(manifest));
        const result = spawnSync(process.execPath, ['--input-type=module', '--eval', `
          import assert from 'node:assert/strict';
          const { verify } = await import('node:crypto');
          const { otaPayload } = await import(${JSON.stringify(pathToFileURL(resolve(directory, 'tools/ota-signature.mjs')).href)});
          const publicKey = ${JSON.stringify(keys.publicKey.export({ format: 'pem', type: 'spki' }).toString())};
          process.argv[2] = ${JSON.stringify(path)};
          process.argv[3] = '--upload';
          const scenario = ${JSON.stringify(scenario)};
          const stored = new Map();
          let puts = 0, posts = 0, gets = 0;
          globalThis.fetch = async (url, options) => {
            if (options?.method === 'PUT') {
              puts++;
              assert.equal(options.headers.Authorization, 'Bearer test-only-token-with-at-least-32-characters');
              if (scenario === 'upload failed') return new Response('Upload failed', { status: 503 });
              stored.set(url, options.body);
              return new Response('{}', { status: scenario === 'retry' ? 200 : 201 });
            }
            if (options?.method === 'POST') {
              posts++; if (!verify('sha256', otaPayload(JSON.parse(options.body)), publicKey, Buffer.from(JSON.parse(options.body).signature, 'base64'))) throw new Error('Bad publication signature');
              assert.equal(gets, 6);
              const payload = JSON.parse(options.body);
              assert.equal(payload.channel, 0);
              assert.equal(payload.file, undefined);
              assert.equal(payload.chip, ${JSON.stringify(hardware.chip)});
              for (const part of payload.parts) {
                assert.equal(part.file, undefined);
                assert.equal(part.url, 'https://api.luxio.lighting/firmware/' + part.sha256);
              }
              return new Response('{}', { status: scenario === 'retry' ? 200 : 201 });
            }
            gets++;
            if (scenario === 'download failed') return new Response('Corrupt download');
            assert.ok(stored.has(url));
            return new Response(stored.get(url));
          };
          try {
            await import(${JSON.stringify(pathToFileURL(resolve(directory, 'tools/publish-update.mjs')).href)});
            assert.ok(['valid', 'retry'].includes(scenario));
            assert.equal(posts, 1);
            assert.equal(puts, ${new Set(parts.map(part => part.sha256)).size});
          } catch (error) {
            assert.equal(posts, 0, 'Invalid release reached registration');
            if (['wrong chip', 'bad checksum'].includes(scenario)) assert.equal(puts, 0, 'Invalid local package reached upload');
            throw error;
          }
        `], { encoding: 'utf8', env: { ...process.env, OTA_SIGNING_KEY: keys.privateKey.export({ format: 'pem', type: 'pkcs8' }).toString(), UPDATE_PUBLISH_TOKEN: 'test-only-token-with-at-least-32-characters', LUXIO_CHANNEL: '0' } });
        if (['valid', 'retry'].includes(scenario)) assert.equal(result.status, 0, result.stderr);
        else { assert.notEqual(result.status, 0, scenario); assert.doesNotMatch(result.stderr, /Invalid release reached registration|Invalid local package reached upload/); }
      }
    } finally { await rm(directory, { recursive: true, force: true }); }
  });
}
