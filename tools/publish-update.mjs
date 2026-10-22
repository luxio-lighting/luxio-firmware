import { readFile } from 'node:fs/promises';
const token = process.env.UPDATE_PUBLISH_TOKEN;
if (!token || token.length < 32) throw new Error('Set UPDATE_PUBLISH_TOKEN');
const manifest = JSON.parse(await readFile(process.argv[2], 'utf8'));
if (!['esp32', 'esp32c2', 'esp32c3', 'esp32c5', 'esp32c6', 'esp32c61', 'esp32s3'].includes(manifest.chip) || manifest.target !== `${manifest.chip}-recovery-v1`)
  throw new Error('Only recovery firmware matching its chip can be published');
const base = process.env.LUXIO_API_URL ?? 'https://api.luxio.lighting';
// Publish only after the hosted artifacts are reachable and match the manifest.
for (const artifact of [...manifest.parts, { url: manifest.url, sha256: manifest.sha256 }]) {
  const result = await fetch(artifact.url, { signal: AbortSignal.timeout(60000) });
  if (!result.ok) throw new Error(`Artifact not available: HTTP ${result.status}`);
  const bytes = await result.arrayBuffer();
  const digest = Buffer.from(await crypto.subtle.digest('SHA-256', bytes)).toString('hex');
  if (digest !== artifact.sha256) throw new Error('Hosted artifact checksum mismatch');
}
const response = await fetch(`${base}/updates`, { method: 'POST', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' }, body: JSON.stringify(manifest), signal: AbortSignal.timeout(15000) });
if (!response.ok) throw new Error(`Update registration failed: HTTP ${response.status} ${await response.text()}`);
console.log(`Registered build ${manifest.build} for ${manifest.chip} on channel ${manifest.channel ?? 100}`);
