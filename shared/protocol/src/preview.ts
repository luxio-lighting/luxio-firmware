import type { Color } from './index.js';

// A display hint only; never execute Lua to discover its palette.
export function parsePreview(source: string): Color[] {
  let header = true;
  let preview: Color[] | undefined;
  for (const raw of source.split('\n')) {
    const line = raw.trim();
    if (line.startsWith('-- @preview')) {
      if (!header) throw new Error('Preview must be at the top of the animation');
      if (preview) throw new Error('Duplicate animation preview');
      if (!line.startsWith('-- @preview ')) throw new Error('Invalid animation preview');
      const colors = line.slice(12).split('//')[0].split(',').map(value => value.trim().replace(/^#/, ''));
      if (colors.length < 2 || colors.length > 8) throw new Error('Preview supports two to eight colors');
      if (colors.some(value => !/^[a-f\d]{6}$/i.test(value))) throw new Error('Preview colors must be six-digit RGB hex');
      preview = colors.map(value => ({ red: parseInt(value.slice(0, 2), 16), green: parseInt(value.slice(2, 4), 16), blue: parseInt(value.slice(4), 16), white: 0, warmWhite: 0, coolWhite: 0 }));
    } else if (line && (!line.startsWith('--') || line.startsWith('--['))) header = false;
  }
  return preview ?? [];
}
