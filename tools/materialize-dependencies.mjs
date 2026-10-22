// Keep installed command entry points as ordinary files, not symlinks.
import { readdir, realpath, unlink, writeFile } from 'node:fs/promises';
import { basename, dirname, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
const modules = fileURLToPath(new URL('../node_modules', import.meta.url));
const pending = [modules];
while (pending.length) {
  const directory = pending.pop();
  for (const entry of await readdir(directory, { withFileTypes: true }).catch(error => { if (error.code === 'ENOENT') return []; throw error; })) {
    const path = resolve(directory, entry.name);
    if (entry.isDirectory()) pending.push(path);
    if (!entry.isSymbolicLink()) continue;
    if (basename(directory) !== '.bin') throw new Error(`Unexpected dependency symlink: ${path}`);
    const target = await realpath(path);
    if (!target.startsWith(modules + sep)) throw new Error(`Dependency command escapes this repository: ${path}`);
    const local = relative(dirname(path), target).replaceAll("'", "'\\''");
    await unlink(path);
    await writeFile(path, `#!/bin/sh\nexec "$(dirname "$0")"/'${local}' "$@"\n`, { mode: 0o755 });
  }
}
