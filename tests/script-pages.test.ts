import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { decodeRequest, encodeResponse, requestHttp } from '../shared/protocol/src/index.js';

test('HTTP lists every page and rejects invalid continuations', async () => {
  let broken = false;
  const offsets: number[] = [];
  const server = createServer(async (request, response) => {
    const chunks: Buffer[] = []; for await (const chunk of request) chunks.push(Buffer.from(chunk));
    const message = decodeRequest(Buffer.concat(chunks));
    const offset = message.scriptOffset ?? 0; offsets.push(offset);
    const scripts = Array.from({ length: Math.min(64, 150 - offset) }, (_, i) => ({ id: String(offset + i), name: `Saved ${offset + i}`, source: '', mode: 1 }));
    response.end(encodeResponse({ id: message.id, scripts, nextScriptOffset: broken ? 64 : offset + 64 < 150 ? offset + 64 : 0 }));
  });
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const address = server.address() as { port: number };
    const target = `127.0.0.1:${address.port}`;
    const result = await requestHttp(target, { id: 1, operation: 5 });
    assert.deepEqual(offsets, [0, 64, 128]);
    assert.equal(result.scripts.length, 150); assert.equal(result.scripts[149].id, '149');
    broken = true;
    await assert.rejects(requestHttp(target, { id: 2, operation: 5 }), /Invalid animation listing continuation/);
  } finally { server.close(); }
});
