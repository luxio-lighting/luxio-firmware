// Export on the old firmware, then import the same backup after installing build 13.
import { readFile, writeFile } from 'node:fs/promises';
import { requestHttp, colorSchema, type Script, type State, type Gradient } from '../shared/protocol/src/index.js';
const [action, address, file] = process.argv.slice(2);
if (!['export', 'import'].includes(action) || !address || !file) throw new Error('Usage: migrate-gradients.ts export|import ADDRESS BACKUP.json');
let id = 1;
if (action === 'export') {
  const state = (await requestHttp(address, { id: id++, operation: 0 })).state;
  const scripts: Script[] = [];
  for (const item of (await requestHttp(address, { id: id++, operation: 5 })).scripts) {
    const script = (await requestHttp(address, { id: id++, operation: 6, scriptId: item.id })).script;
    if (!script) throw new Error(`Unable to export ${item.name}`);
    scripts.push(script);
  }
  await writeFile(file, JSON.stringify({ state, scripts }, null, 2), { flag: 'wx', mode: 0o600 });
  console.log(`Exported ${scripts.length} items. Keep this backup until migration is verified.`);
} else {
  const backup: { state: State; scripts: Script[] } = JSON.parse(await readFile(file, 'utf8'));
  const state = (await requestHttp(address, { id: id++, operation: 0 })).state;
  if (!state || state.build < 13 || state.deviceId !== backup.state.deviceId) throw new Error('Install build 13 on the backed-up device first');
  const existing: Gradient[] = [];
  for (const item of (await requestHttp(address, { id: id++, operation: 18 })).gradients ?? []) {
    const gradient = (await requestHttp(address, { id: id++, operation: 19, gradientId: item.id })).gradient;
    if (gradient) existing.push(gradient);
  }
  for (const script of backup.scripts.filter(s => s.mode === 1 || (s.name.startsWith('Luxio · ') && s.source.includes('local stops')))) {
    // Accept literal stop lists from the old Luxio generators; never evaluate Lua.
    const match = script.source.match(/local stops\s*=\s*(\{\s*\{[\d,\s{}]+\}\s*\})\s*\n\s*local count = luxio_get_pixel_count\(\)/);
    if (!match || !/function loop\(\)\s*end\s*$/.test(script.source)) {
      console.error(`Preserved unsupported legacy gradient: ${script.name}`); process.exitCode = 1; continue;
    }
    const stops: number[][] = JSON.parse(match[1].replaceAll('{', '[').replaceAll('}', ']'));
    if (stops.length < 2 || stops.length > 8 || stops.some(c => ![3, 6].includes(c.length))) throw new Error(`Invalid stops in ${script.name}`);
    const body = script.source.replace(/^-- luxio-gradient-v1:[^\n]*\n/, '').replace(/\s/g, '');
    const literal = `{${stops.map(c => `{${c.join(',')}}`).join(',')}}`;
    const expressions = Array.from({ length: stops[0].length }, (_, i) => `math.floor(a[${i + 1}]+(b[${i + 1}]-a[${i + 1}])*t+0.5)`).join(',');
    const expected = stops[0].length === 6
      ? `function setup() local stops=${literal} local count=luxio_get_pixel_count() for i=0,count-1 do local position=i/math.max(count-1,1)*(#stops-1) local segment=math.max(1,math.min(math.floor(position)+1,#stops-1)) local t=position-(segment-1) local a,b=stops[segment],stops[math.min(segment+1,#stops)] luxio_set_pixel_color(i,${expressions}) end end function loop() end`
      : `function setup() local stops=${literal} local count=luxio_get_pixel_count() for i=0,count-1 do local position=i/math.max(count-1,1)*(#stops-1) local segment=math.min(math.floor(position)+1,#stops-1) local t=position-(segment-1) local a,b=stops[segment],stops[segment+1] luxio_set_pixel_color(i,${expressions}) end end function loop() end`;
    const normalized = expected.replace(/\s/g, '');
    const cli = normalized.replace(`${expressions})`, `${expressions},0,0,0)`);
    if (stops.some(c => c.length !== stops[0].length) || (body !== normalized && body !== cli)) {
      console.error(`Preserved nonstandard legacy gradient: ${script.name}`); process.exitCode = 1; continue;
    }
    const colors = stops.map(c => colorSchema.parse({ red: c[0], green: c[1], blue: c[2], white: c[3] ?? 0, warmWhite: c[4] ?? 0, coolWhite: c[5] ?? 0 }));
    const editorMetadata = script.source.startsWith('-- luxio-gradient-v1:') ? script.source.split('\n')[0].slice('-- luxio-gradient-v1:'.length) : '';
    if (Buffer.byteLength(editorMetadata) > 2048) throw new Error('Editor metadata too large');
    let gradient = existing.find(g => g.name === script.name && JSON.stringify(g.colors) === JSON.stringify(colors) && (g.editorMetadata ?? '') === editorMetadata);
    if (!gradient) gradient = (await requestHttp(address, { id: id++, operation: 20, gradient: { id: '', name: script.name, colors, editorMetadata } })).gradient;
    if (!gradient) throw new Error(`Unable to create ${script.name}`);
    const verified = (await requestHttp(address, { id: id++, operation: 19, gradientId: gradient.id })).gradient;
    if (!verified || verified.name !== script.name || JSON.stringify(verified.colors) !== JSON.stringify(colors) || verified.editorMetadata !== editorMetadata) throw new Error(`Readback mismatch: ${script.name}`);
    // Only remove the old file when it still matches the exported copy.
    try {
      const old = (await requestHttp(address, { id: id++, operation: 6, scriptId: script.id })).script;
      if (old?.source !== script.source || old?.name !== script.name) throw new Error(`Legacy item changed since backup: ${script.name}`);
      await requestHttp(address, { id: id++, operation: 9, scriptId: script.id });
    } catch (error) { if (!(error instanceof Error) || !['Animation not found', 'Script not found'].includes(error.message)) throw error; }
    if (backup.state.activeScriptId === script.id) {
      await requestHttp(address, { id: id++, operation: 23, gradientId: gradient.id });
      await requestHttp(address, { id: id++, operation: 15, on: backup.state.on });
    }
    existing.push(verified); console.log(`Migrated ${script.name}: ${gradient.id}`);
  }
}
