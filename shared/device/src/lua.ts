import { parsePreview } from '../../protocol/src/preview.js';
import { parseParameters, updateParameters, type AnimationParameter } from '../../protocol/src/parameters.js';
import { lua, lauxlib, lualib, to_luastring, to_jsstring } from 'fengari';
import type { Color } from '../../protocol/src/index.js';

export class Animation {
  private L: any;
  pixels: Color[];
  preview: Color[];
  elapsed = 0;
  parameters: AnimationParameter[];
  constructor(readonly source: string, count: number, stripType: string, parameters: AnimationParameter[] = []) {
    if (new TextEncoder().encode(source).length > 16384) throw new Error('Animation exceeds 16 KiB');
    this.preview = parsePreview(source);
    this.parameters = parseParameters(source, parameters);
    this.pixels = Array.from({ length: count }, () => ({ red: 0, green: 0, blue: 0, white: 0, warmWhite: 0, coolWhite: 0 }));
    this.L = lauxlib.luaL_newstate();
    const L = this.L;
    lualib.luaL_openlibs(L);
    for (const name of ['io', 'os', 'package', 'debug', 'require', 'dofile', 'loadfile', 'load', 'collectgarbage', 'coroutine']) {
      lua.lua_pushnil(L); lua.lua_setglobal(L, to_luastring(name));
    }
    lua.lua_pushcfunction(L, () => { lua.lua_pushinteger(L, count); return 1; });
    lua.lua_setglobal(L, to_luastring('luxio_get_pixel_count'));
    lua.lua_pushcfunction(L, () => { lua.lua_pushstring(L, to_luastring(stripType === 'WS2805' ? 'RGBCCT' : (stripType === 'WS2814' || stripType === 'SK6812') ? 'RGBW' : 'RGB')); return 1; });
    lua.lua_setglobal(L, to_luastring('luxio_get_color_type'));
    lua.lua_pushcfunction(L, () => { lua.lua_pushinteger(L, this.elapsed); return 1; });
    lua.lua_setglobal(L, to_luastring('luxio_millis'));
    lua.lua_pushcfunction(L, () => {
      const name = to_jsstring(lauxlib.luaL_checkstring(L, 1));
      const parameter = this.parameters.find(p => p.name === name);
      if (!parameter) return lauxlib.luaL_error(L, to_luastring(`Unknown animation parameter: ${name}`));
      if (parameter.boolean) lua.lua_pushboolean(L, parameter.value !== 0); else lua.lua_pushnumber(L, parameter.value);
      return 1;
    });
    lua.lua_setglobal(L, to_luastring('luxio_get_parameter'));
    lua.lua_pushcfunction(L, () => {
      const index = lauxlib.luaL_checkinteger(L, 1);
      if (index < 0 || index >= count) return lauxlib.luaL_error(L, to_luastring('Pixel index out of range (zero-based)'));
      const values = [2, 3, 4, 5, 6, 7].map(position => lauxlib.luaL_optinteger(L, position, 0));
      if (values.some(value => value < 0 || value > 255)) return lauxlib.luaL_error(L, to_luastring('Channels must be integers from 0 to 255'));
      if ((stripType === 'WS2812' && values.slice(3).some(Boolean)) || ((stripType === 'WS2814' || stripType === 'SK6812') && values.slice(4).some(Boolean)) || (stripType === 'WS2805' && values[3]))
        return lauxlib.luaL_error(L, to_luastring('Channel unavailable on this strip type'));
      this.pixels[index] = { red: values[0], green: values[1], blue: values[2], white: values[3], warmWhite: values[4], coolWhite: values[5] };
      return 0;
    });
    lua.lua_setglobal(L, to_luastring('luxio_set_pixel_color'));
    for (const ranged of [false, true]) {
      lua.lua_pushcfunction(L, () => {
        const start = ranged ? lauxlib.luaL_checkinteger(L, 1) : 0;
        const length = ranged ? lauxlib.luaL_checkinteger(L, 2) : count;
        if (start < 0 || start > count || length < 0 || length > count - start)
          return lauxlib.luaL_error(L, to_luastring('Pixel range out of bounds (zero-based start, count)'));
        const values = [0, 1, 2, 3, 4, 5].map(c => lauxlib.luaL_optinteger(L, c + (ranged ? 3 : 1), 0));
        if (values.some(value => value < 0 || value > 255)) return lauxlib.luaL_error(L, to_luastring('Channels must be 0-255'));
        if ((stripType === 'WS2812' && values.slice(3).some(Boolean)) || ((stripType === 'WS2814' || stripType === 'SK6812') && values.slice(4).some(Boolean)) || (stripType === 'WS2805' && values[3]))
          return lauxlib.luaL_error(L, to_luastring('Channel unavailable on this strip type'));
        for (let i = start; i < start + length; i++) Object.assign(this.pixels[i], { red: values[0], green: values[1], blue: values[2], white: values[3], warmWhite: values[4], coolWhite: values[5] });
        return 0;
      });
      lua.lua_setglobal(L, to_luastring(ranged ? 'luxio_fill_range' : 'luxio_fill'));
    }
    lua.lua_pushcfunction(L, () => {
      let hue = lauxlib.luaL_checknumber(L, 1), step = lauxlib.luaL_optnumber(L, 2, 0);
      const saturation = lauxlib.luaL_optnumber(L, 3, 1), value = lauxlib.luaL_optnumber(L, 4, 1);
      if (![hue, step, saturation, value].every(Number.isFinite) || saturation < 0 || saturation > 1 || value < 0 || value > 1)
        return lauxlib.luaL_error(L, to_luastring('HSV requires finite hue/step and saturation/value from 0 to 1'));
      hue -= Math.floor(hue); step -= Math.floor(step);
      const high = Math.floor(255 * value), low = Math.floor(255 * value * (1 - saturation));
      for (let i = 0; i < count; i++) {
        if (step === 0 && i > 0) { Object.assign(this.pixels[i], this.pixels[0]); continue; }
        let h = hue + i * step; h = (h - Math.floor(h)) * 6;
        const sector = Math.floor(h), rising = Math.floor((high - low) * (h - sector));
        const up = low + rising, down = high - rising;
        let red: number, green: number, blue: number;
        switch (sector) {
          case 0: [red, green, blue] = [high, up, low]; break;
          case 1: [red, green, blue] = [down, high, low]; break;
          case 2: [red, green, blue] = [low, high, up]; break;
          case 3: [red, green, blue] = [low, down, high]; break;
          case 4: [red, green, blue] = [up, low, high]; break;
          default: [red, green, blue] = [high, low, down]; break;
        }
        Object.assign(this.pixels[i], { red, green, blue, white: 0, warmWhite: 0, coolWhite: 0 });
      }
      return 0;
    });
    lua.lua_setglobal(L, to_luastring('luxio_fill_hsv'));
    lua.lua_pushcfunction(L, () => {
      lauxlib.luaL_checktype(L, 1, lua.LUA_TTABLE); lauxlib.luaL_checktype(L, 2, lua.LUA_TTABLE);
      const waveCount = lua.lua_rawlen(L, 1);
      if (waveCount < 1 || waveCount > 4) return lauxlib.luaL_error(L, to_luastring('Use 1-4 waves'));
      if (lua.lua_rawlen(L, 2) > 6) return lauxlib.luaL_error(L, to_luastring('Use at most six color channels'));
      const waves: {offset: number; positive: boolean; terms: number[][]}[] = [];
      for (let w = 0; w < waveCount; w++) {
        lua.lua_rawgeti(L, 1, w + 1); lauxlib.luaL_checktype(L, -1, lua.LUA_TTABLE);
        lua.lua_getfield(L, -1, to_luastring('offset')); const offset = lauxlib.luaL_optnumber(L, -1, 0); lua.lua_pop(L, 1);
        if (!Number.isFinite(offset) || offset < -4 || offset > 4) return lauxlib.luaL_error(L, to_luastring('Wave offset must be finite and between -4 and 4'));
        lua.lua_getfield(L, -1, to_luastring('positive'));
        if (!lua.lua_isnil(L, -1) && !lua.lua_isboolean(L, -1)) return lauxlib.luaL_error(L, to_luastring('positive must be a boolean'));
        const positive = lua.lua_toboolean(L, -1); lua.lua_pop(L, 1);
        const length = lua.lua_rawlen(L, -1), terms: number[][] = [];
        if (length > 4) return lauxlib.luaL_error(L, to_luastring('Use at most four sine terms per wave'));
        for (let t = 0; t < length; t++) {
          lua.lua_rawgeti(L, -1, t + 1); lauxlib.luaL_checktype(L, -1, lua.LUA_TTABLE);
          const keys = ['phase', 'step', 'amplitude'], values: number[] = [];
          for (let k = 0; k < 3; k++) {
            lua.lua_getfield(L, -1, to_luastring(keys[k])); const value = lauxlib.luaL_optnumber(L, -1, k === 2 ? 1 : 0); lua.lua_pop(L, 1);
            const limit = k === 2 ? 4 : 1e12;
            if (!Number.isFinite(value) || value < -limit || value > limit) return lauxlib.luaL_error(L, to_luastring('Invalid sine phase, step or amplitude'));
            values.push(value);
          }
          terms.push(values); lua.lua_pop(L, 1);
        }
        waves.push({offset, positive, terms}); lua.lua_pop(L, 1);
      }
      const channels: {offset: number; multiply: number; terms: number[][]}[] = [];
      for (let c = 0; c < 6; c++) {
        lua.lua_rawgeti(L, 2, c + 1);
        if (lua.lua_isnil(L, -1)) { channels.push({offset: 0, multiply: 0, terms: []}); lua.lua_pop(L, 1); continue; }
        lauxlib.luaL_checktype(L, -1, lua.LUA_TTABLE);
        lua.lua_getfield(L, -1, to_luastring('offset')); const offset = lauxlib.luaL_optnumber(L, -1, 0); lua.lua_pop(L, 1);
        if (!Number.isFinite(offset) || offset < -1024 || offset > 1024) return lauxlib.luaL_error(L, to_luastring('Channel offset must be finite and between -1024 and 1024'));
        lua.lua_getfield(L, -1, to_luastring('multiply')); const multiply = lauxlib.luaL_optinteger(L, -1, 0); lua.lua_pop(L, 1);
        if (multiply < 0 || multiply > waveCount) return lauxlib.luaL_error(L, to_luastring('Invalid modulation wave index'));
        const length = lua.lua_rawlen(L, -1), terms: number[][] = [];
        if (length > 4) return lauxlib.luaL_error(L, to_luastring('Use at most four terms per color channel'));
        if (((stripType === 'WS2805' && c === 3) || ((stripType === 'WS2814' || stripType === 'SK6812') && c >= 4) || (stripType === 'WS2812' && c >= 3)) && (length || offset !== 0))
          return lauxlib.luaL_error(L, to_luastring('Channel unavailable on this strip type'));
        for (let t = 0; t < length; t++) {
          lua.lua_rawgeti(L, -1, t + 1); lauxlib.luaL_checktype(L, -1, lua.LUA_TTABLE);
          lua.lua_getfield(L, -1, to_luastring('wave')); const wave = lauxlib.luaL_checkinteger(L, -1); lua.lua_pop(L, 1);
          lua.lua_getfield(L, -1, to_luastring('power')); const power = lauxlib.luaL_optinteger(L, -1, 1); lua.lua_pop(L, 1);
          if (wave < 1 || wave > waveCount || power < 1 || power > 16) return lauxlib.luaL_error(L, to_luastring('Wave index must exist; power must be an integer from 1 to 16'));
          lua.lua_getfield(L, -1, to_luastring('gain')); const gain = lauxlib.luaL_optnumber(L, -1, 255); lua.lua_pop(L, 1);
          if (!Number.isFinite(gain) || gain < -1024 || gain > 1024) return lauxlib.luaL_error(L, to_luastring('Gain must be finite and between -1024 and 1024'));
          terms.push([wave - 1, power, gain]); lua.lua_pop(L, 1);
        }
        channels.push({offset, multiply, terms}); lua.lua_pop(L, 1);
      }
      for (let i = 0; i < count; i++) {
        const values = waves.map(w => {
          let value = w.offset;
          for (const [phase, step, amplitude] of w.terms) value += amplitude * Math.sin(phase + i * step);
          return Math.max(w.positive ? 0 : -1, Math.min(1, value));
        });
        const colors = channels.map(c => {
          let value = c.offset;
          for (const [wave, power, gain] of c.terms) {
            let base = values[wave], powered = 1;
            for (let p = power; p; p >>= 1) { if (p & 1) powered *= base; if (p > 1) base *= base; }
            value += gain * powered;
          }
          value = Math.max(0, Math.min(255, value));
          if (c.multiply) value *= values[c.multiply - 1];
          return Math.floor(Math.max(0, value));
        });
        Object.assign(this.pixels[i], {red: colors[0], green: colors[1], blue: colors[2], white: colors[3], warmWhite: colors[4], coolWhite: colors[5]});
      }
      return 0;
    });
    lua.lua_setglobal(L, to_luastring('luxio_render_waves'));

    let remaining = 100000;
    lua.lua_sethook(L, () => {
      remaining -= 1000;
      if (remaining <= 0) lauxlib.luaL_error(L, to_luastring('Animation exceeded its instruction budget'));
    }, lua.LUA_MASKCOUNT, 1000);
    const status = lauxlib.luaL_loadstring(L, to_luastring(source));
    if (status !== lua.LUA_OK || lua.lua_pcall(L, 0, 0, 0) !== lua.LUA_OK) {
      const message = to_jsstring(lua.lua_tostring(L, -1)); this.close(); throw new Error(message);
    }
    this.call('setup', true);
    lua.lua_getglobal(L, to_luastring('loop'));
    const valid = lua.lua_isfunction(L, -1); lua.lua_pop(L, 1);
    if (!valid) { this.close(); throw new Error('Define a loop() function'); }
  }
  call(name: string, optional = false) {
    const L = this.L;
    if (!L) return;
    let remaining = 100000;
    lua.lua_sethook(L, () => {
      remaining -= 1000;
      if (remaining <= 0) lauxlib.luaL_error(L, to_luastring('Animation exceeded its instruction budget'));
    }, lua.LUA_MASKCOUNT, 1000);
    lua.lua_getglobal(L, to_luastring(name));
    if (optional && lua.lua_isnil(L, -1)) { lua.lua_pop(L, 1); return; }
    if (lua.lua_pcall(L, 0, 0, 0) !== lua.LUA_OK) {
      const message = to_jsstring(lua.lua_tostring(L, -1)); this.close(); throw new Error(message);
    }
  }
  update(parameters: AnimationParameter[]) { this.parameters = updateParameters(this.parameters, parameters); }
  tick(milliseconds = 33) { this.elapsed += milliseconds; this.call('loop'); return this.pixels; }
  close() { if (this.L) lua.lua_close(this.L); this.L = null; }
}
