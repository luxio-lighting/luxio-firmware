# Pinned SDK backports

`arduino-esp32-3.3.3-hwcdc-2ecad233.patch` backports Espressif's
[HWCDC write fix](https://github.com/espressif/arduino-esp32/commit/2ecad233178bfbba9c584ffd9f67e941d84adbb7)
to Arduino-ESP32 3.3.3. It fixes lost TX interrupt wakeups and partial FIFO
writes, and bounds stalled host writes. This addresses truncated USB replies
observed on the ESP32-S3 after a request had already been applied.

The only adaptation from the upstream commit is the context of a removed,
commented-out debug print (`TX:%u` in 3.3.3, `TX:%lu` upstream). The functional
changes are unchanged. Upstream authorship is retained in the patch; the
modified `HWCDC.cpp` retains its upstream LGPL-2.1-or-later license header.

`tools/build-firmware.py` applies the checked-in patch offline to the local
3.3.3 SDK, before compiling either sketch. It verifies the SDK version, patch
checksum, original source checksum, and resulting source checksum. An already
patched source is accepted; other contents fail the build. The separate S31
SDK is untouched. Other targets using this same 3.3.3 SDK receive the upstream
fix when they compile native USB support. A patch revision define changes
Arduino's compiler flags so builds cannot reuse an unpatched cached core.

| Artifact | SHA-256 |
| --- | --- |
| Original `HWCDC.cpp` | `8238d208409ac8a9e74e5960360a5e59c4f2f85dc2339fd96442a62297d5ff45` |
| Patched `HWCDC.cpp` | `afeebf0de83416b1c899b0037b6c82cbc582709627aee93a22179eac3745be81` |
| Backport patch | `bd9af333f3ba748aed4fe8c017848b1f9961a5a04bbb3bc2d89abe37c9fcfc2a` |
