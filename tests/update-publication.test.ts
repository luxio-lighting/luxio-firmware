import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, rm, readFile } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { createHash, generateKeyPairSync } from 'node:crypto';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const targets = JSON.parse(await readFile('targets.json', 'utf8'));
for (const hardware of targets) {
  test(`publication validates ${hardware.chip} artifacts before registration`, async () => {
    await mkdir('.sdk', { recursive: true });
    const directory = await mkdtemp(resolve('.sdk/publication-test-'));
    const keys = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
    await mkdir(resolve(directory, 'tools'));
    for (const file of ['publish-update.mjs', 'ota-signature.mjs']) await writeFile(resolve(directory, 'tools', file), await readFile(resolve('tools', file)));
    await writeFile(resolve(directory, 'targets.json'), JSON.stringify(targets));
    await writeFile(resolve(directory, 'ota-public-key.json'), JSON.stringify({ keyId: 'test-release', pem: keys.publicKey.export({ format: 'pem', type: 'spki' }).toString() }));
    try {
      for (const scenario of ['valid', 'missing target', 'wrong target', 'wrong image', 'wrong offsets', 'wrong size', 'bad digest']) {
        const artifacts: Record<string, string> = {};
        const parts = [hardware.bootloaderAddress, 0x8000, 0xe000, 0x10000, hardware.applicationAddress].map(address => {
          const bytes = Buffer.alloc(address === hardware.applicationAddress ? 288 : 32);
          if ([hardware.bootloaderAddress, 0x10000, hardware.applicationAddress].includes(address)) {
            bytes[0] = 0xe9; bytes.writeUInt16LE(scenario === 'wrong image' ? 99 : hardware.imageChipId, 12);
          }
          const url = `https://firmware.example/${address}.bin`;
          artifacts[url] = bytes.toString('base64');
          return { address, url, size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') };
        });
        const manifest = { chip: hardware.chip, target: `${hardware.chip}-recovery-v1` as string | undefined, build: 23, size: 288, url: parts[4].url, sha256: parts[4].sha256, parts };
        if (scenario === 'missing target') delete manifest.target;
        if (scenario === 'wrong target') manifest.target = 'unknown';
        if (scenario === 'wrong offsets') parts[0].address = 0x3000;
        if (scenario === 'wrong size') parts[0].size++;
        if (scenario === 'bad digest') parts[0].sha256 = '0'.repeat(64);
        const path = resolve(directory, 'manifest.json');
        await writeFile(path, JSON.stringify(manifest));
        const result = spawnSync(process.execPath, ['--input-type=module', '--eval', `
          const { verify } = await import('node:crypto');
          const { otaPayload } = await import(${JSON.stringify(pathToFileURL(resolve(directory, 'tools/ota-signature.mjs')).href)});
          const publicKey = ${JSON.stringify(keys.publicKey.export({ format: 'pem', type: 'spki' }).toString())};
          process.argv[2] = ${JSON.stringify(path)};
          const artifacts = ${JSON.stringify(artifacts)};
          let posts = 0;
          globalThis.fetch = async (url, options) => {
            if (options?.method === 'POST') { posts++; if (!verify('sha256', otaPayload(JSON.parse(options.body)), publicKey, Buffer.from(JSON.parse(options.body).signature, 'base64'))) throw new Error('Bad publication signature'); return new Response(null, { status: 201 }); }
            if (!artifacts[url]) throw new Error('Unexpected download');
            return new Response(Buffer.from(artifacts[url], 'base64'));
          };
          try {
            await import(${JSON.stringify(pathToFileURL(resolve(directory, 'tools/publish-update.mjs')).href)});
            if (${JSON.stringify(scenario)} !== 'valid' || posts !== 1) throw new Error('Invalid release published');
          } catch (error) {
            if (posts) throw new Error('Rejected artifact reached registration');
            throw error;
          }
        `], { encoding: 'utf8', env: { ...process.env, OTA_SIGNING_KEY: keys.privateKey.export({ format: 'pem', type: 'pkcs8' }).toString(), UPDATE_PUBLISH_TOKEN: 'test-only-token-with-at-least-32-characters' } });
        if (scenario === 'valid') assert.equal(result.status, 0, result.stderr);
        else { assert.notEqual(result.status, 0, scenario); assert.doesNotMatch(result.stderr, /Invalid release published|Rejected artifact reached registration/); }
      }
    } finally { await rm(directory, { recursive: true, force: true }); }
  });
}
