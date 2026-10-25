#include "script_storage.h"
#include <cstring>
#include <memory>
#include "vendor/nanopb/pb_encode.h"
#include "vendor/nanopb/pb_decode.h"
extern "C" {
#include "vendor/heatshrink/heatshrink_encoder.h"
#include "vendor/heatshrink/heatshrink_decoder.h"
}

// Include header and metadata in the checksum, excluding the checksum itself.
static uint32_t recordChecksum(const uint8_t *bytes, size_t size) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < size; ++i) {
    if (i >= 20 && i < 24) continue;
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

size_t encodeStoredScript(const luxio_Script &script, uint8_t *output, size_t capacity) {
  const size_t sourceSize = script.source ? strnlen(script.source, SCRIPT_SOURCE_MAX + 1) : 0;
  const size_t nameSize = strnlen(script.name, sizeof(script.name));
  if (capacity < 24 || !sourceSize || sourceSize > SCRIPT_SOURCE_MAX ||
      !nameSize || nameSize >= sizeof(script.name) || strnlen(script.id, sizeof(script.id)) != 36) return 0;
  auto metadata = pb_ostream_from_buffer(output + 24, capacity - 24);
  if (!pb_encode_tag(&metadata, PB_WT_STRING, 1) || !pb_encode_string(&metadata, reinterpret_cast<const uint8_t *>(script.id), 36) ||
      !pb_encode_tag(&metadata, PB_WT_STRING, 2) || !pb_encode_string(&metadata, reinterpret_cast<const uint8_t *>(script.name), nameSize) ||
      !pb_encode_tag(&metadata, PB_WT_VARINT, 4) || !pb_encode_varint(&metadata, script.mode)) return 0;
  if (script.parameters_count > 16) return 0;
  for (size_t i = 0; i < script.parameters_count; ++i) {
    if (!pb_encode_tag(&metadata, PB_WT_STRING, 5) || !pb_encode_submessage(&metadata, luxio_AnimationParameter_fields, &script.parameters[i])) return 0;
  }
  if (script.preview_count > 8) return 0;
  for (size_t i = 0; i < script.preview_count; ++i) {
    if (!pb_encode_tag(&metadata, PB_WT_STRING, 6) || !pb_encode_submessage(&metadata, luxio_Color_fields, &script.preview[i])) return 0;
  }
  if (metadata.bytes_written > 2048 || sourceSize > capacity - 24 - metadata.bytes_written) return 0;
  uint8_t *payload = output + 24 + metadata.bytes_written;
  std::unique_ptr<heatshrink_encoder, decltype(&heatshrink_encoder_free)> encoder(heatshrink_encoder_alloc(10, 5), heatshrink_encoder_free);
  size_t consumed = 0, written = 0;
  bool compressed = bool(encoder);
  while (compressed) {
    size_t sunk = 0, polled = 0;
    if (consumed < sourceSize) {
      if (heatshrink_encoder_sink(encoder.get(), reinterpret_cast<uint8_t *>(const_cast<char *>(script.source)) + consumed, sourceSize - consumed, &sunk) < 0) return 0;
      consumed += sunk;
    }
    const auto finished = consumed == sourceSize ? heatshrink_encoder_finish(encoder.get()) : HSER_FINISH_MORE;
    if (heatshrink_encoder_poll(encoder.get(), payload + written, sourceSize - written, &polled) < 0) return 0;
    written += polled;
    if (written >= sourceSize) { compressed = false; break; }
    if (finished == HSER_FINISH_DONE) break;
    if (!sunk && !polled && consumed < sourceSize) return 0;
  }
  if (!compressed) { memcpy(payload, script.source, sourceSize); written = sourceSize; }
  memcpy(output, "LXS1", 4);
  uint32_t fields[] = {compressed ? 1u : 0u, uint32_t(metadata.bytes_written), uint32_t(sourceSize), uint32_t(written), 0};
  for (size_t i = 0; i < 5; ++i) for (int byte = 0; byte < 4; ++byte) output[4 + i * 4 + byte] = uint8_t(fields[i] >> (byte * 8));
  const size_t size = 24 + metadata.bytes_written + written;
  const uint32_t checksum = recordChecksum(output, size);
  for (int byte = 0; byte < 4; ++byte) output[20 + byte] = uint8_t(checksum >> (byte * 8));
  return size;
}

bool decodeStoredScript(const uint8_t *bytes, size_t size, luxio_Script &script, bool metadataOnly) {
  pb_release(luxio_Script_fields, &script);
  memset(&script, 0, sizeof(script));
  if (!size || size > SCRIPT_RECORD_MAX) return false;
  if (size < 4 || memcmp(bytes, "LXS1", 4)) {
    auto legacy = pb_istream_from_buffer(bytes, size);
    if (!pb_decode(&legacy, luxio_Script_fields, &script) || strlen(script.id) != 36 || !script.name[0] || !script.source || !script.source[0] || strnlen(script.source, SCRIPT_SOURCE_MAX + 1) > SCRIPT_SOURCE_MAX) return false;
    if (metadataOnly) { free(script.source); script.source = nullptr; }
    return true;
  }
  if (size < 24) return false;
  uint32_t fields[5] = {};
  for (size_t i = 0; i < 5; ++i) for (int byte = 0; byte < 4; ++byte) fields[i] |= uint32_t(bytes[4 + i * 4 + byte]) << (byte * 8);
  const auto codec = fields[0], metaSize = fields[1], sourceSize = fields[2], payloadSize = fields[3];
  if (codec > 1 || !metaSize || metaSize > 2048 || !sourceSize || sourceSize > SCRIPT_SOURCE_MAX ||
      !payloadSize || payloadSize > SCRIPT_SOURCE_MAX || size != 24 + metaSize + payloadSize || fields[4] != recordChecksum(bytes, size)) return false;
  auto metadata = pb_istream_from_buffer(bytes + 24, metaSize);
  if (!pb_decode(&metadata, luxio_Script_fields, &script) || strlen(script.id) != 36 || !script.name[0] || script.source || script.mode > luxio_Mode_ANIMATION) return false;
  if (metadataOnly) return true;
  script.source = static_cast<char *>(malloc(sourceSize + 1));
  if (!script.source) return false;
  const auto *payload = bytes + 24 + metaSize;
  if (codec == 0) {
    if (sourceSize != payloadSize) return false;
    memcpy(script.source, payload, sourceSize);
  } else {
    std::unique_ptr<heatshrink_decoder, decltype(&heatshrink_decoder_free)> decoder(heatshrink_decoder_alloc(256, 10, 5), heatshrink_decoder_free);
    if (!decoder) return false;
    size_t consumed = 0, written = 0;
    while (true) {
      size_t sunk = 0, polled = 0;
      if (consumed < payloadSize) {
        if (heatshrink_decoder_sink(decoder.get(), const_cast<uint8_t *>(payload) + consumed, payloadSize - consumed, &sunk) < 0) return false;
        consumed += sunk;
      }
      if (heatshrink_decoder_poll(decoder.get(), reinterpret_cast<uint8_t *>(script.source) + written, sourceSize + 1 - written, &polled) < 0) return false;
      written += polled;
      if (written > sourceSize) return false;
      if (consumed == payloadSize && heatshrink_decoder_finish(decoder.get()) == HSDR_FINISH_DONE) break;
      if (!sunk && !polled) return false;
    }
    if (written != sourceSize) return false;
  }
  script.source[sourceSize] = 0;
  return strlen(script.source) == sourceSize;
}
