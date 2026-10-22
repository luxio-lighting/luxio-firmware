#pragma once
#include <stddef.h>
#include <stdint.h>
#include "luxio.pb.h"

// LXS1 records: 24-byte little-endian header, Protobuf metadata, Lua payload.
// Codec 0 is plain UTF-8; codec 1 is Heatshrink (window 10, lookahead 5).
// Legacy plain Protobuf records remain readable. Buffers never exceed 17 KiB.
constexpr size_t SCRIPT_RECORD_MAX = 17408;
size_t encodeStoredScript(const luxio_Script &script, uint8_t *output, size_t capacity);
bool decodeStoredScript(const uint8_t *bytes, size_t size, luxio_Script &script, bool metadataOnly = false);
