import { signUpdate } from './ota-signature.mjs';
import { readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
const token = process.env.UPDATE_PUBLISH_TOKEN;
if (!token || token.length < 32) throw new Error('Set UPDATE_PUBLISH_TOKEN');
const manifest = JSON.parse(await readFile(process.argv[2], 'utf8'));
const upload = process.argv[3] === '--upload';
const base = (process.env.LUXIO_API_URL ?? 'https://api.luxio.lighting').replace(/\/$/, '');
if (!base.startsWith('https://')) throw new Error('An HTTPS API URL is required');
if (process.env.LUXIO_CHANNEL !== undefined) manifest.channel = Number(process.env.LUXIO_CHANNEL);
if (!Number.isInteger(manifest.channel ?? 100) || (manifest.channel ?? 100) < 0 || (manifest.channel ?? 100) > 100) throw new Error('Invalid publication channel');
// Fail before any network operation if the release signer is missing or mismatched.
signUpdate(manifest, process.env.OTA_SIGNING_KEY ?? '');
const targets = JSON.parse(await readFile(new URL('../targets.json', import.meta.url), 'utf8'));
const hardware = targets.find(target => target.chip === manifest.chip && target.target === manifest.target);
if (!hardware)
  throw new Error('Only recovery firmware matching its chip can be published');
const limits = new Map([[hardware.bootloaderAddress, 0x8000 - hardware.bootloaderAddress], [0x8000, 0x1000], [0xe000, 0x2000], [0x10000, hardware.recoverySize], [hardware.applicationAddress, hardware.applicationSize]]);
if (!Array.isArray(manifest.parts) || manifest.parts.length !== limits.size || new Set(manifest.parts.map(part => part.address)).size !== limits.size || manifest.parts.some(part => !limits.has(part.address)))
  throw new Error('Flash parts must match the chip and recovery layout');
if (!Number.isInteger(manifest.build) || manifest.build < 1 || manifest.build > 2147483647 || !Number.isInteger(manifest.size) || manifest.size < 288 || manifest.size > hardware.applicationSize ||
    (manifest.minimumFlashSize !== undefined && manifest.minimumFlashSize !== hardware.minimumFlashSize)) throw new Error('Invalid firmware build, size or flash requirement');
const application = manifest.parts.find(part => part.address === hardware.applicationAddress);
if (application.sha256 !== manifest.sha256) throw new Error('USB and OTA application must match');
if (upload) {
  const files = new Map();
  // Validate every local artifact before uploading anything. Filenames cannot escape the package directory.
  for (const artifact of [...manifest.parts, { address: hardware.applicationAddress, file: manifest.file, sha256: manifest.sha256, size: manifest.size }]) {
    if (!/^(bootloader|partitions|otadata|recovery|luxio)\.bin$/.test(artifact.file) || !/^[a-f0-9]{64}$/.test(artifact.sha256)) throw new Error('Invalid local firmware artifact');
    const bytes = await readFile(resolve(dirname(process.argv[2]), artifact.file));
    if (!bytes.length || bytes.length > limits.get(artifact.address) || (artifact.size !== undefined && artifact.size !== bytes.length) || (artifact.address === hardware.applicationAddress && bytes.length !== manifest.size)) throw new Error('Local artifact size does not match its partition or manifest');
    if ([hardware.bootloaderAddress, 0x10000, hardware.applicationAddress].includes(artifact.address) &&
        (bytes.length < 24 || bytes[0] !== 0xe9 || bytes.readUInt16LE(12) !== hardware.imageChipId)) throw new Error('Local image is for a different chip');
    if (Buffer.from(await crypto.subtle.digest('SHA-256', bytes)).toString('hex') !== artifact.sha256) throw new Error('Local artifact checksum mismatch');
    files.set(artifact.sha256, bytes);
  }
  for (const [sha256, bytes] of files) {
    const response = await fetch(`${base}/firmware/${sha256}`, { method: 'PUT', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/octet-stream' }, body: bytes, signal: AbortSignal.timeout(120000) });
    if (!response.ok) throw new Error(`Firmware upload failed: HTTP ${response.status} ${await response.text()}`);
  }
  manifest.url = `${base}/firmware/${manifest.sha256}`;
  delete manifest.file;
  for (const part of manifest.parts) { part.url = `${base}/firmware/${part.sha256}`; delete part.file; }
}
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
