#pragma once
#include <stddef.h>
#include <stdint.h>
#include "luxio.pb.h"
#include "vendor/nanopb/pb_decode.h"
#include <memory>

// LXS1 records: 24-byte little-endian header, Protobuf metadata, Lua payload.
// Codec 0 is plain UTF-8; codec 1 is Heatshrink (window 10, lookahead 5).
// Legacy plain Protobuf records remain readable. Buffers never exceed 20 KiB.
constexpr size_t SCRIPT_SOURCE_MAX = 16384;
// Decoded strings belong to nanopb; deleting the outer C struct is insufficient.
struct ScriptDeleter {
  void operator()(luxio_Script *script) const {
    if (script) pb_release(luxio_Script_fields, script);
    delete script;
  }
};
using ScriptPtr = std::unique_ptr<luxio_Script, ScriptDeleter>;
constexpr size_t SCRIPT_RECORD_MAX = 20480;
size_t encodeStoredScript(const luxio_Script &script, uint8_t *output, size_t capacity);
// The destination must be zero-initialized or contain a previous owned decode.
// Release it with pb_release(luxio_Script_fields, &script), including on failure.
bool decodeStoredScript(const uint8_t *bytes, size_t size, luxio_Script &script, bool metadataOnly = false);
