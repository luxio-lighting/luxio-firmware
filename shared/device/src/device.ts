import { parsePreview } from '../../protocol/src/preview.js';
import { parseParameters } from '../../protocol/src/parameters.js';
import { randomUUID } from 'node:crypto';
import { z } from 'zod';
import { colorSchema, configSchema, scriptSchema, gradientSchema, savedColorSchema, type SavedColor, type Request, type Response, type State, type Script, type Color, type Gradient } from '../../protocol/src/index.js';
import { Animation } from './lua.js';

export class Device {
  state: State;
  scripts: Script[];
  gradients: Gradient[];
  colors: SavedColor[];
  animation?: Animation;
  private animationRevision = 0;
  constructor(deviceId: string, saved?: { state: State; scripts: Script[]; gradients?: Gradient[]; colors?: SavedColor[] }) {
    this.state = saved?.state ?? { deviceId, build: 22, config: { name: 'Luxio development light', stripType: 'WS2805', ledCount: 60, gpio: 4, channel: 100 }, brightness: 180, on: true, color: colorSchema.parse({}), activeScriptId: '', lanIp: '127.0.0.1' };
    if (this.state.on === undefined) {
      this.state.on = this.state.brightness > 0;
      this.state.brightness = this.state.brightness ? Math.round(this.state.brightness * 256 / 255) : 16;
    }
    this.state.build = 22;
    this.state.deviceId = deviceId;
    this.state.parameters = []; this.state.animationPreview = [];
    this.state.scriptName = ''; this.state.activeScriptId = '';
    this.state.activeColorId ||= ''; this.state.activeGradientId ||= '';
    if (!this.state.activeColorId) this.state.color = colorSchema.parse({});
    if (this.state.mode !== 1 || !this.state.activeGradientId || !this.state.gradient) {
      this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.mode = 0;
    }
    this.scripts = saved?.scripts.map(script => script.mode === 1 ? script : scriptSchema.parse(script)) ?? [];
    this.gradients = saved?.gradients?.map(gradient => gradientSchema.parse(gradient)) ?? [];
    this.colors = saved?.colors?.map(color => savedColorSchema.parse(color)) ?? [];
  }
  handle(request: Request): Partial<Response> {
    try {
      switch (request.operation) {
        case 0: break;
        case 1: {
          const color = colorSchema.parse(request.color);
          const type = this.state.config.stripType;
          if ((type === 'WS2812' && (color.white || color.warmWhite || color.coolWhite)) || (type === 'WS2814' && (color.warmWhite || color.coolWhite)) || (type === 'WS2805' && color.white)) throw new Error('Channel unavailable on this strip type');
          this.animation?.close(); this.animation = undefined; this.state.activeColorId = ''; this.state.scriptName = ''; this.state.activeScriptId = ''; this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.mode = 0; this.state.color = color; this.state.on = true;
          break;
        }
        case 2: this.state.brightness = z.number().int().min(1).max(256).parse(request.brightness); break;
        case 15: this.state.on = z.boolean().parse(request.on ?? false); break;
        case 3: {
          const config = configSchema.parse(request.config);
          if (config.stripType !== this.state.config.stripType || config.ledCount !== this.state.config.ledCount || config.gpio !== this.state.config.gpio) {
            this.animation?.close(); this.animation = undefined; this.state.activeColorId = ''; this.state.scriptName = ''; this.state.activeScriptId = ''; this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.mode = 0; this.state.color = colorSchema.parse({});
          }
          this.state.config = config; break;
        }
        case 4: throw new Error('Wi-Fi configuration requires a physical device');
        case 5: {
          const offset = request.scriptOffset ?? 0;
          if (!Number.isInteger(offset) || offset < 0) throw new Error('Invalid animation offset');
          const animations = this.scripts.filter(s => s.mode !== 1);
          return { id: request.id, scripts: animations.slice(offset, offset + 64).map(({ id, name, mode, source }) => ({ id, name, source: '', mode, preview: parsePreview(source) })), nextScriptOffset: offset + 64 < animations.length ? offset + 64 : 0 };
        }
        case 6: {
          if (!request.scriptId) {
            if (!this.animation || this.state.mode !== 2) throw new Error('No animation is running');
            return { id: request.id, state: { ...this.state, parameters: this.animation.parameters }, script: {
              id: this.state.activeScriptId, name: this.state.scriptName ?? '', source: this.animation.source,
              mode: 2, parameters: this.animation.parameters, preview: this.animation.preview
            } };
          }
          const script = this.scripts.find(script => script.id === request.scriptId);
          if (!script) throw new Error('Animation not found');
          return { id: request.id, script };
        }
        case 7: {
          const script = scriptSchema.parse({ ...request.script, id: randomUUID() });
          if (Buffer.byteLength(script.source) > 16384) throw new Error('Animation exceeds 16 KiB');
          script.parameters = parseParameters(script.source, script.parameters);
          script.preview = parsePreview(script.source);
          this.scripts.push(script); return { id: request.id, script };
        }
        case 8: {
          const script = scriptSchema.parse(request.script);
          if (Buffer.byteLength(script.source) > 16384) throw new Error('Animation exceeds 16 KiB');
          const index = this.scripts.findIndex(item => item.id === script.id);
          if (index < 0) throw new Error('Animation not found');
          script.parameters = parseParameters(script.source, script.parameters);
          script.preview = parsePreview(script.source);
          this.scripts[index] = script; return { id: request.id, script };
        }
        case 9: {
          const index = this.scripts.findIndex(script => script.id === request.scriptId);
          if (index < 0) throw new Error('Animation not found');
          if (this.state.activeScriptId === request.scriptId) { this.animation?.close(); this.animation = undefined; this.state.activeColorId = ''; this.state.scriptName = ''; this.state.activeScriptId = ''; this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.mode = 0; }
          this.scripts.splice(index, 1); break;
        }
        case 10: {
          const script = this.scripts.find(script => script.id === request.scriptId);
          if (!script) throw new Error('Animation not found');
          if (script.mode === 1) throw new Error('Legacy Lua gradient: migrate it to gradient storage');
          const animation = new Animation(script.source, this.state.config.ledCount, this.state.config.stripType, script.parameters);
          this.animation?.close(); this.animation = animation; this.state.animationRevision = ++this.animationRevision; this.state.activeScriptId = script.id;
          this.state.activeColorId = ''; this.state.scriptName = script.name; this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.mode = 2; this.state.on = true;
          break;
        }
        case 32: {
          if (!this.animation || this.state.mode !== 2) throw new Error('No animation is running');
          if ((!request.scriptId && !request.script?.name) || (request.scriptId && request.scriptId !== this.state.activeScriptId) || (request.script?.name && request.script.name !== this.state.scriptName)) throw new Error('The active animation changed');
          this.animation.update(request.parameters ?? []); break;
        }
        case 31: {
          const script = scriptSchema.parse({ ...request.script, id: randomUUID(), name: request.script?.name || 'Preview' });
          if (Buffer.byteLength(script.source) > 16384 || Buffer.byteLength(script.name) > 63) throw new Error('Animation exceeds byte limit');
          if (script.mode === 1) throw new Error('Legacy Lua gradient: migrate it to gradient storage');
          const animation = new Animation(script.source, this.state.config.ledCount, this.state.config.stripType, script.parameters);
          this.animation?.close(); this.animation = animation; this.state.animationRevision = ++this.animationRevision;
          this.state.activeScriptId = ''; this.state.activeGradientId = ''; this.state.activeColorId = '';
          this.state.gradient = undefined; this.state.scriptName = request.script?.name ?? ''; this.state.mode = 2; this.state.on = true;
          break;
        }
        case 11: if (request.script?.name && request.script.name !== this.state.scriptName) break;
          if (this.state.mode === 2) { this.animation?.close(); this.animation = undefined; this.state.activeScriptId = ''; this.state.scriptName = ''; this.state.mode = 0; } break;
        case 17: case 20: case 21: case 23: {
          const input = request.operation === 23 ? this.gradients.find(g => g.id === request.gradientId) : request.gradient;
          if (!input) throw new Error('Gradient not found');
          const gradient = gradientSchema.parse({ ...input, ...(request.operation === 17 ? { id: randomUUID(), name: 'Preview' } : request.operation === 20 ? { id: randomUUID() } : {}) });
          if (Buffer.byteLength(gradient.name) > 63 || Buffer.byteLength(gradient.editorMetadata) > 2048) throw new Error('Gradient metadata exceeds byte limit');
          const type = this.state.config.stripType;
          for (const c of gradient.colors) if ((type === 'WS2805' && c.white) || (type === 'WS2814' && (c.warmWhite || c.coolWhite)) || (type === 'WS2812' && (c.white || c.warmWhite || c.coolWhite))) throw new Error('Channel unavailable on this strip type');
          if (request.operation === 20) { this.gradients.push(gradient); return { id: request.id, gradient }; }
          if (request.operation === 21) {
            const index = this.gradients.findIndex(g => g.id === gradient.id);
            if (index < 0) throw new Error('Gradient not found');
            this.gradients[index] = gradient; return { id: request.id, gradient };
          }
          this.animation?.close(); this.animation = undefined; this.state.activeScriptId = ''; this.state.activeColorId = ''; this.state.scriptName = '';
          this.state.activeGradientId = request.operation === 23 ? gradient.id : '';
          this.state.gradient = { ...gradient, id: this.state.activeGradientId, name: request.operation === 23 ? gradient.name : '' };
          this.state.mode = 1; this.state.on = true; break;
        }
        case 18: {
          const offset = request.gradientOffset ?? 0;
          if (!Number.isInteger(offset) || offset < 0) throw new Error('Invalid gradient offset');
          return { id: request.id, gradients: this.gradients.slice(offset, offset + 64).map(({ id, name }) => ({ id, name, colors: [], editorMetadata: '' })), nextGradientOffset: offset + 64 < this.gradients.length ? offset + 64 : 0 };
        }
        case 19: {
          const gradient = this.gradients.find(g => g.id === request.gradientId);
          if (!gradient) throw new Error('Gradient not found');
          return { id: request.id, gradient: structuredClone(gradient) };
        }
        case 22: {
          const index = this.gradients.findIndex(g => g.id === request.gradientId);
          if (index < 0) throw new Error('Gradient not found');
          this.gradients.splice(index, 1);
          if (this.state.activeGradientId === request.gradientId) { this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.mode = 0; }
          break;
        }
        case 24:
          if (this.state.mode === 1) { this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.mode = 0; }
          break;
        case 27: case 28: case 30: {
          const input = request.operation === 30 ? this.colors.find(c => c.id === request.colorId) : request.savedColor;
          if (!input) throw new Error('Color not found');
          const saved = savedColorSchema.parse({ ...input, ...(request.operation === 27 ? { id: randomUUID() } : {}) });
          if (Buffer.byteLength(saved.name) > 63) throw new Error('Name exceeds byte limit');
          const c = saved.color, type = this.state.config.stripType;
          if ((type === 'WS2805' && c.white) || (type === 'WS2814' && (c.warmWhite || c.coolWhite)) || (type === 'WS2812' && (c.white || c.warmWhite || c.coolWhite))) throw new Error('Channel unavailable on this strip type');
          if (request.operation === 27) { this.colors.push(saved); return { id: request.id, savedColor: saved }; }
          if (request.operation === 28) {
            const index = this.colors.findIndex(c => c.id === saved.id);
            if (index < 0) throw new Error('Color not found');
            this.colors[index] = saved; return { id: request.id, savedColor: saved };
          }
          this.animation?.close(); this.animation = undefined; this.state.activeScriptId = ''; this.state.scriptName = '';
          this.state.activeGradientId = ''; this.state.gradient = undefined; this.state.activeColorId = saved.id;
          this.state.color = saved.color; this.state.mode = 0; this.state.on = true; break;
        }
        case 25: {
          const offset = request.colorOffset ?? 0;
          if (!Number.isInteger(offset) || offset < 0) throw new Error('Invalid color offset');
          return { id: request.id, colors: this.colors.slice(offset, offset + 64).map(c => ({ ...c, color: undefined } as unknown as SavedColor)), nextColorOffset: offset + 64 < this.colors.length ? offset + 64 : 0 };
        }
        case 26: {
          const savedColor = this.colors.find(c => c.id === request.colorId);
          if (!savedColor) throw new Error('Color not found');
          return { id: request.id, savedColor: structuredClone(savedColor) };
        }
        case 29: {
          const index = this.colors.findIndex(c => c.id === request.colorId);
          if (index < 0) throw new Error('Color not found');
          this.colors.splice(index, 1); if (this.state.activeColorId === request.colorId) this.state.activeColorId = ''; break;
        }
        case 12: throw new Error('Matter pairing is available only on commissioned-capable physical firmware');
        case 13: case 14: throw new Error('OTA requires a physical device');
        default: throw new Error('Unknown operation');
      }
      this.state.parameters = this.animation?.parameters ?? [];
      this.state.animationPreview = this.animation?.preview ?? [];
      if (!this.animation) this.state.animationRevision = 0;
      return { id: request.id, state: structuredClone(this.state) };
    } catch (error) { return { id: request.id, error: error instanceof Error ? error.message : String(error) }; }
  }
  output(): Color[] {
    const pixels = this.state.mode === 1 && this.state.gradient ? Array.from({ length: this.state.config.ledCount }, (_, i) => {
      const stops = this.state.gradient!.colors;
      const position = i / Math.max(this.state.config.ledCount - 1, 1) * (stops.length - 1);
      const segment = Math.min(Math.floor(position), stops.length - 2), t = position - segment;
      return Object.fromEntries(Object.entries(stops[segment]).map(([channel, value]) => [channel, Math.round(value + (stops[segment + 1][channel as keyof Color] - value) * t)])) as Color;
    }) : this.animation?.tick() ?? Array.from({ length: this.state.config.ledCount }, () => this.state.color);
    return pixels.map(pixel => Object.fromEntries(Object.entries(pixel).map(([channel, value]) => [channel, this.state.on ? Math.round(value * this.state.brightness / 256) : 0])) as Color);
  }
}
