import protobuf from 'protobufjs';
import { z } from 'zod';
import { schema } from './schema.js';

export const MAX_FRAME = 32768;
export const root = protobuf.parse(schema).root;
export const RequestMessage = root.lookupType('luxio.Request');
export const ResponseMessage = root.lookupType('luxio.Response');
export const operations = root.lookupEnum('luxio.Operation').values;
export const colorSchema = z.object({
  red: z.number().int().min(0).max(255).default(0), green: z.number().int().min(0).max(255).default(0),
  blue: z.number().int().min(0).max(255).default(0), white: z.number().int().min(0).max(255).default(0),
  warmWhite: z.number().int().min(0).max(255).default(0), coolWhite: z.number().int().min(0).max(255).default(0),
});
export const configSchema = z.object({
  name: z.string().trim().min(1).max(63), stripType: z.enum(['WS2805', 'WS2814', 'WS2812']),
  ledCount: z.number().int().min(1).max(1024), gpio: z.number().int().min(0).max(54),
  channel: z.number().int().min(0).max(100),
});
export const scriptSchema = z.object({ id: z.uuid(), name: z.string().trim().min(1).max(63), source: z.string().min(1).max(16384), mode: z.number().int().min(0).max(2).optional().transform(value => value === 1 ? 1 : 2) });
export type Color = z.infer<typeof colorSchema>;
export type Config = z.infer<typeof configSchema>;
export type Script = { id: string; name: string; source: string; mode?: number };
export interface State { deviceId: string; build: number; config: Config; brightness: number; on: boolean; color: Color; activeScriptId: string; lanIp: string; mode?: number }
export interface Request { id: number; operation: number; color?: Color; brightness?: number; on?: boolean; config?: Config; wifi?: { ssid: string; password: string }; script?: Script; scriptId?: string; scriptOffset?: number }
export interface Response { id: number; error: string; state?: State; scripts: Script[]; script?: Script; pairingCode: string; pairingQr: string; availableBuild: number; updateChecking?: boolean; updateError?: string; nextScriptOffset?: number }

export function encodeRequest(request: Request): Uint8Array {
  const error = RequestMessage.verify(request);
  if (error) throw new Error(error);
  const bytes = RequestMessage.encode(request).finish();
  if (bytes.length > MAX_FRAME) throw new Error('Request exceeds 32 KiB');
  return bytes;
}
export function decodeRequest(bytes: Uint8Array): Request {
  if (bytes.length > MAX_FRAME) throw new Error('Request exceeds 32 KiB');
  return RequestMessage.toObject(RequestMessage.decode(bytes), { defaults: true }) as Request;
}
export function encodeResponse(response: Partial<Response>): Uint8Array {
  const bytes = ResponseMessage.encode(response).finish();
  if (bytes.length > MAX_FRAME) throw new Error('Response exceeds 32 KiB');
  return bytes;
}
export function decodeResponse(bytes: Uint8Array): Response {
  if (bytes.length > MAX_FRAME) throw new Error('Response exceeds 32 KiB');
  return ResponseMessage.toObject(ResponseMessage.decode(bytes), { defaults: true }) as Response;
}

// UART/BLE: ASCII LXP1, then a four-byte big-endian size, then Protobuf.
// The magic marker lets clients discard ROM boot diagnostics after USB resets.
export function frame(bytes: Uint8Array): Uint8Array {
  if (!bytes.length || bytes.length > MAX_FRAME) throw new Error('Invalid frame size');
  const result = new Uint8Array(bytes.length + 8);
  result.set([0x4c, 0x58, 0x50, 0x31]);
  new DataView(result.buffer).setUint32(4, bytes.length);
  result.set(bytes, 8);
  return result;
}
export class FrameDecoder {
  private buffer = new Uint8Array(0);
  push(chunk: Uint8Array): Uint8Array[] {
    const joined = new Uint8Array(this.buffer.length + chunk.length);
    joined.set(this.buffer); joined.set(chunk, this.buffer.length);
    this.buffer = joined;
    const messages: Uint8Array[] = [];
    while (this.buffer.length >= 8) {
      if (this.buffer[0] !== 0x4c || this.buffer[1] !== 0x58 || this.buffer[2] !== 0x50 || this.buffer[3] !== 0x31) {
        this.buffer = this.buffer.slice(1); continue;
      }
      const size = new DataView(this.buffer.buffer, this.buffer.byteOffset + 4, 4).getUint32(0);
      if (!size || size > MAX_FRAME) { this.buffer = new Uint8Array(0); throw new Error('Invalid frame size; reconnect transport'); }
      if (this.buffer.length < size + 8) break;
      messages.push(this.buffer.slice(8, size + 8));
      this.buffer = this.buffer.slice(size + 8);
    }
    return messages;
  }
}
export async function requestHttp(address: string, request: Request): Promise<Response> {
  const url = new URL(address.includes('://') ? address : `http://${address}`);
  if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password) throw new Error('Invalid device address');
  url.pathname = '/api'; url.search = ''; url.hash = '';
  const scripts: Script[] = [];
  let offset = request.scriptOffset ?? 0;
  while (true) {
    const response = await fetch(url, {
      method: 'POST', headers: { 'Content-Type': 'application/x-protobuf' },
      body: encodeRequest({ ...request, scriptOffset: offset }) as BodyInit, signal: AbortSignal.timeout(10000),
      ...({ targetAddressSpace: 'local' } as object),
    });
    if (!response.ok) throw new Error(`Device returned HTTP ${response.status}`);
    const result = decodeResponse(new Uint8Array(await response.arrayBuffer()));
    if (result.id !== request.id) throw new Error('Device returned a mismatched request ID');
    if (result.error) throw new Error(result.error);
    if (request.operation !== 5) return result;
    scripts.push(...result.scripts);
    if (!result.nextScriptOffset) return { ...result, scripts, nextScriptOffset: 0 };
    if (result.nextScriptOffset <= offset || !result.scripts.length) throw new Error('Invalid script listing continuation');
    offset = result.nextScriptOffset;
  }
}
