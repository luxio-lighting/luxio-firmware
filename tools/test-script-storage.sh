#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .sdk/storage-test
for source in Luxio/src/vendor/heatshrink/heatshrink_encoder.c Luxio/src/vendor/heatshrink/heatshrink_decoder.c Luxio/src/vendor/nanopb/pb_common.c Luxio/src/vendor/nanopb/pb_encode.c Luxio/src/vendor/nanopb/pb_decode.c Luxio/src/luxio.pb.c; do
  cc -g -fsanitize=address,undefined -I Luxio/src/vendor/nanopb -c "$source" -o ".sdk/storage-test/$(basename "$source").o"
done
c++ -std=c++17 -I Luxio/src/vendor/nanopb -g -fsanitize=address,undefined tools/test-script-storage.cpp Luxio/src/script_storage.cpp .sdk/storage-test/*.o -o .sdk/test-script-storage
.sdk/test-script-storage
for source in tools/vendor/littlefs/lfs.c tools/vendor/littlefs/lfs_util.c; do
  cc -g -fsanitize=address,undefined -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR -c "$source" -o ".sdk/storage-test/$(basename "$source").o"
done
c++ -std=c++17 -I Luxio/src/vendor/nanopb -g -fsanitize=address,undefined tools/test-script-filesystem.cpp Luxio/src/script_storage.cpp .sdk/storage-test/*.o -o .sdk/test-script-filesystem
.sdk/test-script-filesystem tools/fixtures/scripts/*.lua
