#include "color_storage.h"
#include "color_storage.h"
#include <string.h>
#include "vendor/nanopb/pb_encode.h"
#include "vendor/nanopb/pb_decode.h"

size_t encodeStoredColor(const luxio_SavedColor &color, uint8_t *output, size_t capacity) {
  if (capacity < 12 || strlen(color.id) != 36 || !color.name[0] || (!color.has_color || !validColor(color.color, ""))) return 0;
  auto encoder = pb_ostream_from_buffer(output + 12, capacity - 12);
  if (!pb_encode(&encoder, luxio_SavedColor_fields, &color)) return 0;
  const size_t size = encoder.bytes_written + 12;
  if (size > COLOR_RECORD_MAX) return 0;
  memcpy(output, "LXC1", 4);
  for (int byte = 0; byte < 4; byte++) output[4 + byte] = uint8_t(encoder.bytes_written >> (8 * byte));
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < size; i++) {
    if (i >= 8 && i < 12) continue;
    crc ^= output[i];
    for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  crc = ~crc;
  for (int byte = 0; byte < 4; byte++) output[8 + byte] = uint8_t(crc >> (8 * byte));
  return size;
}

bool decodeStoredColor(const uint8_t *bytes, size_t size, luxio_SavedColor &color) {
  memset(&color, 0, sizeof(color));
  if (size < 12 || size > COLOR_RECORD_MAX || memcmp(bytes, "LXC1", 4)) return false;
  uint32_t length = 0, checksum = 0, crc = 0xffffffff;
  for (int byte = 0; byte < 4; byte++) { length |= uint32_t(bytes[4 + byte]) << (8 * byte); checksum |= uint32_t(bytes[8 + byte]) << (8 * byte); }
  if (length != size - 12) return false;
  for (size_t i = 0; i < size; i++) {
    if (i >= 8 && i < 12) continue;
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  if (checksum != ~crc) return false;
  auto decoder = pb_istream_from_buffer(bytes + 12, size - 12);
  return pb_decode(&decoder, luxio_SavedColor_fields, &color) && strlen(color.id) == 36 && color.name[0] && validColor(color.color, "") && color.has_color;
}
