#include "gradient_storage.h"
#include "gradient.h"
#include "vendor/nanopb/pb_encode.h"
#include "vendor/nanopb/pb_decode.h"

size_t encodeStoredGradient(const luxio_Gradient &gradient, uint8_t *output, size_t capacity) {
  if (capacity < 12 || strlen(gradient.id) != 36 || !gradient.name[0] || !validGradient(gradient, "")) return 0;
  auto encoder = pb_ostream_from_buffer(output + 12, capacity - 12);
  if (!pb_encode(&encoder, luxio_Gradient_fields, &gradient)) return 0;
  const size_t size = encoder.bytes_written + 12;
  if (size > GRADIENT_RECORD_MAX) return 0;
  memcpy(output, "LXG1", 4);
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

bool decodeStoredGradient(const uint8_t *bytes, size_t size, luxio_Gradient &gradient) {
  memset(&gradient, 0, sizeof(gradient));
  if (size < 12 || size > GRADIENT_RECORD_MAX || memcmp(bytes, "LXG1", 4)) return false;
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
  return pb_decode(&decoder, luxio_Gradient_fields, &gradient) && strlen(gradient.id) == 36 && gradient.name[0] && validGradient(gradient, "");
}
