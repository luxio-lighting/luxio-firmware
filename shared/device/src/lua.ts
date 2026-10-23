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
      if ((stripType === 'WS2812' && values.slice(3).some(Boolean)) || (stripType === 'WS2814' && values.slice(4).some(Boolean)) || (stripType === 'WS2805' && values[3]))
        return lauxlib.luaL_error(L, to_luastring('Channel unavailable on this strip type'));
      this.pixels[index] = { red: values[0], green: values[1], blue: values[2], white: values[3], warmWhite: values[4], coolWhite: values[5] };
      return 0;
    });
    lua.lua_setglobal(L, to_luastring('luxio_set_pixel_color'));
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
