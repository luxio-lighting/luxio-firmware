#!/usr/bin/env python3
"""Rebuild the pinned SDK's TLS record layer with 16 KiB RX / 4 KiB TX.

Only buffer lengths change. DTLS, crypto configuration and public struct layouts
remain identical to the bundled SDK; all TLS translation units are rebuilt.
Archives and headers in the installed SDK are never modified.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

root = Path(__file__).resolve().parent.parent
matrix = json.loads((root / 'targets.json').read_text())
parser = argparse.ArgumentParser()
parser.add_argument('chip', choices=[t['chip'] for t in matrix])
args = parser.parse_args()
target = next(t for t in matrix if t['chip'] == args.chip)
version = 'idf-release_v5.5-f1a1df9b-v3'
revision = 'b5d87eaa6748b7a6fa70593178c08b4480e9b71e' # esp-idf f1a1df9b2e submodule
sha256 = 'e6beb4ed862c83daba07f0d8091c0c361c843623b06674b2d8effe00e8e7aa9d'
sdk = root / '.sdk/arduino/packages/esp32/tools/esp32-arduino-libs' / version / args.chip
output = root / 'build' / args.chip / 'tls'
output.mkdir(parents=True, exist_ok=True)
cache = root / '.sdk/tls'; cache.mkdir(exist_ok=True)
archive = cache / f'{revision}.tar.gz'
if not archive.exists():
    with urllib.request.urlopen(f'https://codeload.github.com/espressif/mbedtls/tar.gz/{revision}', timeout=60) as response:
        data = response.read()
    assert hashlib.sha256(data).hexdigest() == sha256, 'Unexpected TLS source archive'
    archive.write_bytes(data)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == sha256, 'TLS source archive checksum mismatch'
source = cache / f'mbedtls-{revision}'
if not source.exists():
    with tarfile.open(archive) as bundle:
        bundle.extractall(cache, filter='data')
# Pin both source and installed SDK headers. A different ABI must fail closed.
for name in ['include/mbedtls/ssl.h', 'library/ssl_misc.h']:
    assert (source / name).read_bytes() == (sdk / 'include/mbedtls/mbedtls' / name).read_bytes(), f'SDK TLS header mismatch: {name}'
config = output / 'tls-config.h'
config.write_text('''#pragma once
#include "sdkconfig.h"
// Buffer-length-only override; retain every SDK feature and public ABI.
#undef CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN
#define CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN 1
#define CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN 4096
''')
prefix = f'xtensa-{args.chip}-elf' if target['architecture'] == 'xtensa' else 'riscv32-esp-elf'
binaries = list((root / '.sdk/arduino/packages/esp32/tools').glob(f'*/2507/bin/{prefix}-gcc'))
assert len(binaries) == 1, 'Pinned ESP32 toolchain is required'
compiler = binaries[0]; ar = compiler.with_name(prefix + '-ar'); objcopy = compiler.with_name(prefix + '-objcopy')
original = sdk / 'lib/libmbedtls_2.a'
result = output / 'libmbedtls_2.a'
stamp = output / 'profile.json'
fingerprint = hashlib.sha256(Path(__file__).read_bytes() + original.read_bytes() + config.read_bytes()).hexdigest()
if result.exists() and stamp.exists() and json.loads(stamp.read_text()).get('fingerprint') == fingerprint:
    print(f'{args.chip}: cached TLS 16384-byte RX / 4096-byte TX'); raise SystemExit(0)
result.unlink(missing_ok=True); stamp.unlink(missing_ok=True)
flags = [str(compiler), '-c', '-Os', f'@{sdk}/flags/c_flags', f'@{sdk}/flags/defines',
         '-iprefix', str(sdk / 'include') + '/', f'@{sdk}/flags/includes', f'-I{sdk}/dio_qspi/include',
         f'-I{source}/include', f'-I{source}/library']
if args.chip == 'esp32':
    flags += ['-mfix-esp32-psram-cache-issue', '-mfix-esp32-psram-cache-strategy=memw']
# Verify exposed TLS types have the same size and alignment in both profiles.
# No feature macros are changed, so their members and order also remain intact.
probe = output / 'abi.c'
probe.write_text('''#include "common.h"
#include <mbedtls/ssl.h>
#include "ssl_misc.h"
const unsigned luxio_tls_abi[] = {
sizeof(mbedtls_ssl_context), _Alignof(mbedtls_ssl_context),
sizeof(mbedtls_ssl_config), _Alignof(mbedtls_ssl_config),
sizeof(mbedtls_ssl_session), _Alignof(mbedtls_ssl_session),
sizeof(mbedtls_ssl_handshake_params), _Alignof(mbedtls_ssl_handshake_params),
sizeof(mbedtls_ssl_transform), _Alignof(mbedtls_ssl_transform)};
''')
for profile in ['stock', 'small']:
    command = flags + (['-include', str(config)] if profile == 'small' else [])
    subprocess.run(command + [str(probe), '-o', str(output / f'{profile}.o')], check=True)
    subprocess.run([str(objcopy), '-O', 'binary', '--only-section=.rodata.luxio_tls_abi', str(output / f'{profile}.o'), str(output / f'{profile}.abi')], check=True)
assert (output / 'stock.abi').read_bytes() and (output / 'stock.abi').read_bytes() == (output / 'small.abi').read_bytes(), 'TLS ABI changed'
members = subprocess.check_output([str(ar), 't', str(original)], text=True).splitlines()
sources = [source / 'library' / member.removesuffix('.obj') for member in members if member != 'net_sockets.c.obj' and (source / 'library' / member.removesuffix('.obj')).exists()]
assert len(sources) == 17 and any(p.name == 'ssl_tls.c' for p in sources), 'Unexpected SDK TLS archive contents'
# The SDK port objects (debug, platform time and sockets) do not depend on record sizes.
assert set(members) - {p.name + '.obj' for p in sources} == {'mbedtls_debug.c.obj', 'esp_platform_time.c.obj', 'net_sockets.c.obj'}
def compile_source(path):
    subprocess.run(flags + ['-include', str(config), str(path), '-o', str(output / (path.name + '.obj'))], check=True)
with ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(compile_source, sources))
shutil.copyfile(original, result)
subprocess.run([str(ar), 'r', str(result), *[str(output / (p.name + '.obj')) for p in sources]], check=True)
stamp.write_text(json.dumps(dict(fingerprint=fingerprint, sdk=version, source=revision, sourceSha256=sha256, rx=16384, tx=4096, dynamic=False), indent=2) + '\n')
print(f'{args.chip}: rebuilt 17 TLS objects; identical ABI; 16384-byte RX / 4096-byte TX')
