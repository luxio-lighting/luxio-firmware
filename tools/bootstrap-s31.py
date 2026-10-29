#!/usr/bin/env python3
"""Install the pinned Arduino 4 / IDF 6.1 S31 SDK independently of stable targets."""
import hashlib
import json
import platform
from pathlib import Path
import shutil
import tarfile
import urllib.request
import zipfile

root = Path(__file__).resolve().parent.parent
base = root / '.sdk/s31'
cache = base / 'downloads'
cache.mkdir(parents=True, exist_ok=True)
revision = 'd1ae6ac9498601920b21f7f03b11e56ba3cf30d7'
archive = cache / 'arduino.tar.gz'
if not archive.exists():
    urllib.request.urlretrieve(f'https://codeload.github.com/espressif/arduino-esp32/tar.gz/{revision}', archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == 'd2ccee65ca56810991125ed7dff98aa735e134a0f5a5e4be7af8640467216cf5', 'Arduino source checksum mismatch'
core = base / 'arduino/packages/esp32/hardware/esp32/4.0.0'
if not core.exists():
    with tarfile.open(archive) as bundle:
        bundle.extractall(cache, filter='data')
    core.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(cache / f'arduino-esp32-{revision}'), core)
metadata = json.loads((core / 'package/package_esp32_index.template.json').read_text())['packages'][0]
host = {('Darwin', 'arm64'): 'arm64-apple-darwin', ('Darwin', 'x86_64'): 'x86_64-apple-darwin', ('Linux', 'x86_64'): 'x86_64-pc-linux-gnu', ('Linux', 'aarch64'): 'aarch64-linux-gnu'}[(platform.system(), platform.machine())]
for name in ['esp32-arduino-libs', 'riscv32-esp-elf-gcc', 'esptool_py']:
    dependency = next(t for t in metadata['platforms'][0]['toolsDependencies'] if t['name'] == name)
    tool = next(t for t in metadata['tools'] if t['name'] == name and t['version'] == dependency['version'])
    asset = next(a for a in tool['systems'] if a['host'] == host)
    destination = base / 'arduino/packages/esp32/tools' / name / tool['version']
    if (destination / '.luxio-complete').exists():
        continue
    path = cache / asset['archiveFileName']
    if not path.exists():
        print(f'Downloading {name} {tool["version"]}', flush=True)
        urllib.request.urlretrieve(asset['url'], path)
    with path.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    assert digest == asset['checksum'].split(':')[1].lower(), f'Checksum mismatch: {name}'
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True, exist_ok=True)
    if name == 'esp32-arduino-libs':
        with zipfile.ZipFile(path) as bundle:
            # The other chips keep their stable SDK. Extract only S31 libraries.
            for entry in bundle.infolist():
                parts = Path(entry.filename).parts
                if len(parts) < 2 or parts[1] != 'esp32s31':
                    continue
                relative = Path(*parts[1:])
                assert '..' not in relative.parts
                output = destination / relative
                if entry.is_dir():
                    output.mkdir(parents=True, exist_ok=True)
                else:
                    output.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.open(entry) as source, output.open('wb') as target:
                        shutil.copyfileobj(source, target)
        assert (destination / 'esp32s31/flags/ld_libs').exists(), 'Missing S31 libraries'
    else:
        staging = cache / f'unpack-{name}'
        shutil.rmtree(staging, ignore_errors=True)
        staging.mkdir()
        with tarfile.open(path) as bundle:
            bundle.extractall(staging, filter='data')
        children = list(staging.iterdir())
        assert len(children) == 1 and children[0].is_dir(), 'Unexpected tool archive structure'
        for child in children[0].iterdir():
            shutil.move(str(child), destination / child.name)
        children[0].rmdir()
        staging.rmdir()
    (destination / '.luxio-complete').touch()
config = base / 'arduino-cli.yaml'
config.write_text(f'directories:\n  data: {base}/arduino\n  downloads: {cache}\n  user: {base}/user\n')
print(f'S31 SDK ready: {config}')
