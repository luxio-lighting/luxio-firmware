import { readFile } from 'node:fs/promises';
const token = process.env.UPDATE_PUBLISH_TOKEN;
if (!token || token.length < 32) throw new Error('Set UPDATE_PUBLISH_TOKEN');
const manifest = JSON.parse(await readFile(process.argv[2], 'utf8'));
const targets = JSON.parse(await readFile(new URL('../targets.json', import.meta.url), 'utf8'));
const hardware = targets.find(target => target.chip === manifest.chip);
if (!hardware || manifest.target !== `${manifest.chip}-recovery-v1`)
  throw new Error('Only recovery firmware matching its chip can be published');
const limits = new Map([[hardware.bootloaderAddress, 0x8000 - hardware.bootloaderAddress], [0x8000, 0x1000], [0xe000, 0x2000], [0x10000, hardware.recoverySize], [hardware.applicationAddress, hardware.applicationSize]]);
if (!Array.isArray(manifest.parts) || manifest.parts.length !== limits.size || new Set(manifest.parts.map(part => part.address)).size !== limits.size || manifest.parts.some(part => !limits.has(part.address)))
  throw new Error('Flash parts must match the chip and recovery layout');
if (!Number.isInteger(manifest.build) || manifest.build < 1 || manifest.build > 2147483647 || !Number.isInteger(manifest.size) || manifest.size < 288 || manifest.size > hardware.applicationSize ||
    (manifest.minimumFlashSize !== undefined && manifest.minimumFlashSize !== hardware.minimumFlashSize)) throw new Error('Invalid firmware build, size or flash requirement');
const application = manifest.parts.find(part => part.address === hardware.applicationAddress);
if (application.sha256 !== manifest.sha256) throw new Error('USB and OTA application must match');
const base = process.env.LUXIO_API_URL ?? 'https://api.luxio.lighting';
// Publish only after the hosted artifacts are reachable and match the manifest.
for (const artifact of [...manifest.parts, { address: hardware.applicationAddress, url: manifest.url, sha256: manifest.sha256, size: manifest.size }]) {
  if (typeof artifact.url !== 'string' || !artifact.url.startsWith('https://') || !/^[a-f0-9]{64}$/.test(artifact.sha256)) throw new Error('HTTPS URL and SHA-256 required');
  const result = await fetch(artifact.url, { signal: AbortSignal.timeout(60000) });
  if (!result.ok) throw new Error(`Artifact not available: HTTP ${result.status}`);
  const bytes = await result.arrayBuffer();
  if (!bytes.byteLength || bytes.byteLength > limits.get(artifact.address) || (artifact.size !== undefined && artifact.size !== bytes.byteLength) || (artifact.address === hardware.applicationAddress && bytes.byteLength !== manifest.size))
    throw new Error('Hosted artifact size does not match its partition or manifest');
  if ([hardware.bootloaderAddress, 0x10000, hardware.applicationAddress].includes(artifact.address)) {
    const image = new DataView(bytes);
    if (bytes.byteLength < 24 || image.getUint8(0) !== 0xe9 || image.getUint16(12, true) !== hardware.imageChipId) throw new Error('Hosted image is for a different chip');
  }
  const digest = Buffer.from(await crypto.subtle.digest('SHA-256', bytes)).toString('hex');
  if (digest !== artifact.sha256) throw new Error('Hosted artifact checksum mismatch');
}
const response = await fetch(`${base}/updates`, { method: 'POST', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' }, body: JSON.stringify(manifest), signal: AbortSignal.timeout(15000) });
if (!response.ok) throw new Error(`Update registration failed: HTTP ${response.status} ${await response.text()}`);
console.log(`Registered build ${manifest.build} for ${manifest.chip} on channel ${manifest.channel ?? 100}`);
