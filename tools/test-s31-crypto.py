#!/usr/bin/env python3
"""Host-test the S31 SDK's crypto APIs; requires cmake, Node, Jinja2/jsonschema."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

root = Path(__file__).resolve().parent.parent
os.chdir(root)
base = root / '.sdk/s31/crypto-test'
base.mkdir(parents=True, exist_ok=True)
revision = 'ce3f3485a121c100f58f36d700cb35b060f6e866'
archive = root / '.sdk/s31/downloads/mbedtls.tar.gz'
if not archive.exists():
    urllib.request.urlretrieve(f'https://codeload.github.com/espressif/mbedtls/tar.gz/{revision}', archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == '5f3ed3db01f2b238c0f50529155ecb19319ad65b95d224305debf1470c9e9e83'
source = base / 'source'
if not source.exists():
    with tarfile.open(archive) as bundle:
        bundle.extractall(base, filter='data')
    (base / f'mbedtls-{revision}').rename(source)
    shutil.copytree(source / 'tf-psa-crypto/framework', source / 'framework')
# Host-only wrappers: no ESP hardware accelerators or SDK configuration.
includes = base / 'include'
(includes / 'mbedtls').mkdir(parents=True, exist_ok=True)
(includes / 'sdkconfig.h').write_text('')
port = root / '.sdk/s31/arduino/packages/esp32/tools/esp32-arduino-libs/idf-release_v6.1-9a97f6c5-v2/esp32s31/include/mbedtls/port/include/mbedtls'
for name in ['bignum.h', 'ecp.h']:
    (includes / 'mbedtls' / name).write_text((port / name).read_text().replace('#include_next', '#include'))
subprocess.run(['cmake', '-S', str(source), '-B', str(base / 'build'), '-DENABLE_PROGRAMS=OFF', '-DENABLE_TESTING=OFF', f'-DPython3_EXECUTABLE={sys.executable}', f'-DCMAKE_C_FLAGS=-I{includes}'], check=True)
subprocess.run(['cmake', '--build', str(base / 'build'), '--target', 'tfpsacrypto', '-j4'], check=True)
libraries = [str(base / 'build/tf-psa-crypto' / name) for name in ['core/libtfpsacrypto.a', 'extras/libextras.a', 'drivers/builtin/libbuiltin.a', 'drivers/p256-m/libp256-m.a', 'drivers/everest/libeverest.a', 'drivers/pqcp/libpqcp.a', 'utilities/libutilities.a', 'platform/libplatform.a']]
# Repeat archives to resolve dependencies on both GNU ld and Apple's linker.
(base / 'libraries.json').write_text(json.dumps(libraries * 2))
subprocess.run(['node', 'tools/test-ota-signatures.mjs'], env={**os.environ, 'LUXIO_CRYPTO4': str(base)}, check=True)
production = (root / 'Luxio/src/main.cpp').read_text()
identity = production[production.index('  mbedtls_pk_init(&identity);'):production.index('  LittleFS.mkdir("/scripts");')]
fixture = (root / 'tools/test-s31-identity.cpp').read_text().replace('// PRODUCTION_IDENTITY', identity)
(base / 'identity.cpp').write_text(fixture)
subprocess.run(['c++', '-std=c++17', '-DESP_IDF_VERSION_MAJOR=6', f'-I{source}/tf-psa-crypto/include', f'-I{source}/tf-psa-crypto/drivers/builtin/include', f'-I{base}/build/tf-psa-crypto/include', str(base / 'identity.cpp'), *libraries, *libraries, '-o', str(base / 'identity')], check=True)
subprocess.run([str(base / 'identity')], check=True)
