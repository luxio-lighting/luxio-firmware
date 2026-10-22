import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

test('publication rejects missing, incompatible and mismatched targets before fetching artifacts', async () => {
  await mkdir('.sdk', { recursive: true });
  const directory = await mkdtemp(resolve('.sdk/publication-test-'));
  try {
    for (const target of [undefined, 'ab-v1', 'unknown', 'esp32c3-recovery-v1', 'esp32s3-recovery-v1']) {
      const manifest = { chip: 'esp32s3', target, build: 12, size: 0, url: 'https://firmware.example/app.bin', sha256: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855', parts: [] };
      const path = resolve(directory, 'manifest.json');
      await writeFile(path, JSON.stringify(manifest));
      const result = spawnSync(process.execPath, ['--input-type=module', '--eval', `
        process.argv[2] = ${JSON.stringify(path)};
        let calls = 0;
        globalThis.fetch = async (_url, options) => {
          calls++;
          if (options?.method === 'POST') {
            if (JSON.parse(options.body).target !== 'esp32s3-recovery-v1') throw new Error('Wrong publication target');
            return new Response(null, { status: 201 });
          }
          return new Response(new Uint8Array());
        };
        try {
          await import(${JSON.stringify(pathToFileURL(resolve('tools/publish-update.mjs')).href)});
          if (calls !== 2) throw new Error('Expected artifact verification and publication');
        } catch (error) {
          if (calls !== 0) throw new Error('Invalid target reached the network');
          throw error;
        }
      `], { encoding: 'utf8', env: { ...process.env, UPDATE_PUBLISH_TOKEN: 'test-only-token-with-at-least-32-characters' } });
      if (target === 'esp32s3-recovery-v1') assert.equal(result.status, 0, result.stderr);
      else { assert.notEqual(result.status, 0); assert.match(result.stderr, /Only recovery firmware matching its chip/); }
    }
  } finally { await rm(directory, { recursive: true, force: true }); }
});
