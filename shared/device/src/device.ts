import { randomUUID } from 'node:crypto';
import { z } from 'zod';
import { colorSchema, configSchema, scriptSchema, type Request, type Response, type State, type Script, type Color } from '../../protocol/src/index.js';
import { Animation } from './lua.js';

export class Device {
  state: State;
  scripts: Script[];
  animation?: Animation;
  constructor(deviceId: string, saved?: { state: State; scripts: Script[] }) {
    this.state = saved?.state ?? { deviceId, build: 1, config: { name: 'Luxio development light', stripType: 'WS2805', ledCount: 60, gpio: 4, channel: 100 }, brightness: 180, color: colorSchema.parse({}), activeScriptId: '', lanIp: '127.0.0.1' };
    this.state.deviceId = deviceId;
    this.state.activeScriptId = ''; this.state.mode = 0;
    this.scripts = saved?.scripts.map(script => scriptSchema.parse(script)) ?? [];
  }
  handle(request: Request): Partial<Response> {
    try {
      switch (request.operation) {
        case 0: break;
        case 1: {
          const color = colorSchema.parse(request.color);
          const type = this.state.config.stripType;
          if ((type === 'WS2812' && (color.white || color.warmWhite || color.coolWhite)) || (type === 'WS2814' && (color.warmWhite || color.coolWhite)) || (type === 'WS2805' && color.white)) throw new Error('Channel unavailable on this strip type');
          this.animation?.close(); this.animation = undefined; this.state.activeScriptId = ''; this.state.mode = 0; this.state.color = color;
          break;
        }
        case 2: this.state.brightness = z.number().int().min(0).max(255).parse(request.brightness); break;
        case 3: {
          const config = configSchema.parse(request.config);
          if (config.stripType !== this.state.config.stripType || config.ledCount !== this.state.config.ledCount || config.gpio !== this.state.config.gpio) {
            this.animation?.close(); this.animation = undefined; this.state.activeScriptId = ''; this.state.mode = 0; this.state.color = colorSchema.parse({});
          }
          this.state.config = config; break;
        }
        case 4: throw new Error('Wi-Fi configuration requires a physical device');
        case 5: return { id: request.id, scripts: this.scripts.map(({ id, name, mode }) => ({ id, name, source: '', mode })) };
        case 6: {
          const script = this.scripts.find(script => script.id === request.scriptId);
          if (!script) throw new Error('Script not found');
          return { id: request.id, script };
        }
        case 7: {
          if (this.scripts.length >= 32) throw new Error('Script storage is full (32 scripts)');
          const script = scriptSchema.parse({ ...request.script, id: randomUUID() });
          if (Buffer.byteLength(script.source) > 16384) throw new Error('Script exceeds 16 KiB');
          this.scripts.push(script); return { id: request.id, script };
        }
        case 8: {
          const script = scriptSchema.parse(request.script);
          if (Buffer.byteLength(script.source) > 16384) throw new Error('Script exceeds 16 KiB');
          const index = this.scripts.findIndex(item => item.id === script.id);
          if (index < 0) throw new Error('Script not found');
          this.scripts[index] = script; return { id: request.id, script };
        }
        case 9: {
          const index = this.scripts.findIndex(script => script.id === request.scriptId);
          if (index < 0) throw new Error('Script not found');
          if (this.state.activeScriptId === request.scriptId) { this.animation?.close(); this.animation = undefined; this.state.activeScriptId = ''; this.state.mode = 0; }
          this.scripts.splice(index, 1); break;
        }
        case 10: {
          const script = this.scripts.find(script => script.id === request.scriptId);
          if (!script) throw new Error('Script not found');
          const animation = new Animation(script.source, this.state.config.ledCount, this.state.config.stripType);
          this.animation?.close(); this.animation = animation; this.state.activeScriptId = script.id;
          this.state.mode = script.mode === 1 ? 1 : 2;
          if (this.state.mode === 1) animation.close();
          break;
        }
        case 11: this.animation?.close(); this.animation = undefined; this.state.activeScriptId = ''; this.state.mode = 0; break;
        case 12: throw new Error('Matter pairing is available only on commissioned-capable physical firmware');
        case 13: case 14: throw new Error('OTA requires a physical device');
        default: throw new Error('Unknown operation');
      }
      return { id: request.id, state: structuredClone(this.state) };
    } catch (error) { return { id: request.id, error: error instanceof Error ? error.message : String(error) }; }
  }
  output(): Color[] {
    const pixels = this.animation?.tick() ?? Array.from({ length: this.state.config.ledCount }, () => this.state.color);
    return pixels.map(pixel => Object.fromEntries(Object.entries(pixel).map(([channel, value]) => [channel, Math.round(value * this.state.brightness / 255)])) as Color);
  }
}
