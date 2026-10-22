import { createServer } from 'node:http';
import { mkdir, readFile, rename, writeFile } from 'node:fs/promises';
import { createHash, generateKeyPairSync, sign } from 'node:crypto';
import { networkInterfaces } from 'node:os';
import { resolve } from 'node:path';
import { WebSocketServer, WebSocket } from 'ws';
import { Bonjour } from 'bonjour-service';
import { Device } from './device.js';
import { decodeRequest, encodeResponse, MAX_FRAME } from '../../protocol/src/index.js';

const directory = resolve(process.env.DEVICE_STORAGE ?? '.device');
await mkdir(directory, { recursive: true });
let identity: { publicKey: string; privateKey: string };
try { identity = JSON.parse(await readFile(`${directory}/identity.json`, 'utf8')); }
catch (error: any) {
  if (error.code !== 'ENOENT') throw error;
  const keys = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  identity = { publicKey: keys.publicKey.export({ type: 'spki', format: 'der' }).toString('base64'), privateKey: keys.privateKey.export({ type: 'pkcs8', format: 'pem' }).toString() };
  await writeFile(`${directory}/identity.json`, JSON.stringify(identity), { mode: 0o600, flag: 'wx' });
}
const deviceId = createHash('sha256').update(Buffer.from(identity.publicKey, 'base64')).digest('hex');
let saved;
try { saved = JSON.parse(await readFile(`${directory}/state.json`, 'utf8')); } catch (error: any) { if (error.code !== 'ENOENT') throw error; }
const device = new Device(deviceId, saved);
const port = Number(process.env.DEVICE_PORT ?? 8080);
const lanIp = process.env.DEVICE_LAN_IP ?? Object.values(networkInterfaces()).flat().find(value => value?.family === 'IPv4' && !value.internal)?.address ?? '127.0.0.1';
device.state.lanIp = lanIp;
let queue = Promise.resolve();
const server = createServer(async (request, response) => {
  response.setHeader('Access-Control-Allow-Origin', '*');
  response.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS');
  response.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  response.setHeader('Access-Control-Allow-Private-Network', 'true');
  response.setHeader('Cache-Control', 'no-store');
  if (request.method === 'OPTIONS') { response.writeHead(204).end(); return; }
  if (request.url !== '/api' || request.method !== 'POST') { response.writeHead(404).end(); return; }
  const chunks: Buffer[] = []; let size = 0;
  try {
    for await (const chunk of request) {
      size += chunk.length;
      if (size > MAX_FRAME) { response.writeHead(413).end(); return; }
      chunks.push(chunk);
    }
    const message = decodeRequest(Buffer.concat(chunks));
    queue = queue.then(async () => {
      const result = device.handle(message);
      await writeFile(`${directory}/state.json.tmp`, JSON.stringify({ state: device.state, scripts: device.scripts }), { mode: 0o600 });
      await rename(`${directory}/state.json.tmp`, `${directory}/state.json`);
      response.writeHead(200, { 'Content-Type': 'application/x-protobuf' }).end(encodeResponse(result));
      for (const socket of sockets.clients) if (socket.readyState === WebSocket.OPEN) socket.send(encodeResponse({ id: 0, state: device.state }));
    }).catch(error => { console.error(error); if (!response.headersSent) response.writeHead(500).end(); });
  } catch { response.writeHead(400).end(); }
});
const sockets = new WebSocketServer({ server, path: '/api/ws', maxPayload: MAX_FRAME });
sockets.on('connection', socket => {
  socket.send(encodeResponse({ id: 0, state: device.state }));
  socket.on('message', (bytes, isBinary) => {
    if (!isBinary) { socket.close(1003, 'Binary Protobuf required'); return; }
    queue = queue.then(async () => {
      try {
        const result = device.handle(decodeRequest(new Uint8Array(bytes as Buffer)));
        await writeFile(`${directory}/state.json.tmp`, JSON.stringify({ state: device.state, scripts: device.scripts }), { mode: 0o600 });
        await rename(`${directory}/state.json.tmp`, `${directory}/state.json`);
        socket.send(encodeResponse(result));
        for (const peer of sockets.clients) if (peer.readyState === WebSocket.OPEN) peer.send(encodeResponse({ id: 0, state: device.state }));
      } catch { socket.close(1007, 'Invalid request'); }
    });
  });
});
const animationTimer = setInterval(() => {
  try { device.output(); } catch (error) {
    device.animation?.close(); device.animation = undefined; device.state.activeScriptId = ''; device.state.mode = 0;
    for (const socket of sockets.clients) if (socket.readyState === WebSocket.OPEN) socket.send(encodeResponse({ id: 0, error: String(error), state: device.state }));
  }
}, 33);
const bonjour = new Bonjour();
bonjour.publish({ name: device.state.config.name, type: 'luxio', port, txt: { build: String(device.state.build), strip: device.state.config.stripType, id: deviceId } });
server.listen(port, '0.0.0.0', () => console.log(`Development device: http://127.0.0.1:${port}/api\nDevice ID: ${deviceId}\nThis runs real protocol and Lua logic; it does not emulate Matter or hardware.`));
async function announce() {
  if (!process.env.LUXIO_API_URL) return;
  try {
    const base = process.env.LUXIO_API_URL;
    const challenge = await fetch(`${base}/announcements/challenge`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ deviceId }), signal: AbortSignal.timeout(10000) });
    if (!challenge.ok) throw new Error(`Challenge HTTP ${challenge.status}`);
    const { nonce } = await challenge.json() as { nonce: string };
    const { name, channel } = device.state.config;
    const payload = JSON.stringify([1, nonce, deviceId, name, lanIp, device.state.build, channel]);
    const signature = sign('sha256', Buffer.from(payload), identity.privateKey).toString('base64');
    const result = await fetch(`${base}/announcements`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ nonce, deviceId, publicKey: identity.publicKey, name, lanIp, build: device.state.build, channel, signature }), signal: AbortSignal.timeout(10000) });
    if (!result.ok) throw new Error(`Announcement HTTP ${result.status}`);
  } catch (error) { console.error('Announcement failed:', error); }
}
await announce();
const announcementTimer = setInterval(announce, 900000);
for (const signal of ['SIGTERM', 'SIGINT']) process.on(signal, () => {
  clearInterval(animationTimer); clearInterval(announcementTimer); device.animation?.close();
  sockets.close(); for (const socket of sockets.clients) socket.terminate();
  bonjour.destroy(); server.close();
});
