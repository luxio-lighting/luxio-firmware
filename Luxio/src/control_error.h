#pragma once
#include "vendor/nanopb/pb_decode.h"
#include "vendor/nanopb/pb_encode.h"
#include <cstring>

// Emergency replies must work even when the Request/Response objects cannot fit.
// Read only the correlation ID; skip optional payloads without allocating them.
inline size_t encodeControlError(const uint8_t *input, size_t length, uint8_t *output, size_t capacity) {
  auto decoder = pb_istream_from_buffer(input, length);
  uint64_t id = 0;
  while (decoder.bytes_left) {
    pb_wire_type_t wire; uint32_t tag; bool eof;
    if (!pb_decode_tag(&decoder, &wire, &tag, &eof)) break;
    if (tag == 1 && wire == PB_WT_VARINT) {
      if (!pb_decode_varint(&decoder, &id)) break;
    } else if (!pb_skip_field(&decoder, wire)) break;
  }
  const char error[] = "Not enough memory for this request";
  auto encoder = pb_ostream_from_buffer(output, capacity);
  return pb_encode_tag(&encoder, PB_WT_VARINT, 1) && pb_encode_varint(&encoder, uint32_t(id)) &&
    pb_encode_tag(&encoder, PB_WT_STRING, 2) && pb_encode_string(&encoder, reinterpret_cast<const pb_byte_t *>(error), sizeof(error) - 1)
    ? encoder.bytes_written : 0;
}
