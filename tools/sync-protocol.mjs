import { readFile, writeFile } from 'node:fs/promises';
const source = await readFile(new URL('../Luxio/luxio.proto', import.meta.url), 'utf8');
await writeFile(new URL('../shared/protocol/src/schema.ts', import.meta.url), `// Generated from luxio.proto by tools/sync-protocol.mjs.\nexport const schema = ${JSON.stringify(source)};\n`);
await writeFile(new URL('../shared/protocol/luxio.proto', import.meta.url), source);
await writeFile(new URL('../shared/protocol/luxio.options', import.meta.url), await readFile(new URL('../Luxio/luxio.options', import.meta.url)));
