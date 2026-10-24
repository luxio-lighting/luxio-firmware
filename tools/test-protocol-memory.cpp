#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "luxio.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "control_error.h"
#include "control_response.h"
static bool failOutputAllocation = false;
static size_t outputAllocation = 0;
void *operator new[](size_t size, const std::nothrow_t &) noexcept {
  outputAllocation = size;
  if (failOutputAllocation) return nullptr;
  try { return ::operator new[](size); } catch (...) { return nullptr; }
}
int main() {
  static_assert(sizeof(luxio_Request) < 2000);
  static_assert(sizeof(luxio_Response) < 3000);
  uint8_t bytes[32768];
  luxio_Script script = luxio_Script_init_zero;
  memset(script.source, 'x', 16384); script.source[16384] = 0;
  strcpy(script.name, "large script");
  luxio_Request request = luxio_Request_init_zero;
  request.id = 42; request.operation = luxio_Operation_SET_SCRIPT;
  for (int round = 0; round < 50; round++) {
    request.script = round % 2 ? &script : nullptr;
    auto encoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
    assert(pb_encode(&encoder, luxio_Request_fields, &request));
    luxio_Request decoded = luxio_Request_init_zero;
    auto decoder = pb_istream_from_buffer(bytes, encoder.bytes_written);
    assert(pb_decode(&decoder, luxio_Request_fields, &decoded));
    assert(decoded.id == 42);
    assert(bool(decoded.script) == bool(request.script));
    if (decoded.script) assert(!strcmp(decoded.script->source, script.source));
    pb_release(luxio_Request_fields, &decoded);
    assert(!decoded.script);
    if (request.script) {
      decoder = pb_istream_from_buffer(bytes, encoder.bytes_written - 100);
      assert(!pb_decode(&decoder, luxio_Request_fields, &decoded));
      pb_release(luxio_Request_fields, &decoded);
      assert(!decoded.script);
    }
  }
  luxio_Response response = luxio_Response_init_zero;
  response.id = 42; response.script = &script;
  response.has_network_status = true;
  response.network_status.connected = true;
  memset(response.network_status.ssid, 's', 32);
  strcpy(response.network_status.gateway, "192.0.2.1");
  response.network_status.dns_count = 2;
  strcpy(response.network_status.dns[0], "192.0.2.1");
  strcpy(response.network_status.dns[1], "1.1.1.1");
  auto encoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
  assert(pb_encode(&encoder, luxio_Response_fields, &response));
  // Full scripts must not depend on the old 8 KiB low-memory transport cap.
  for (size_t prefix : {size_t(0), size_t(8)}) {
    std::unique_ptr<uint8_t[]> exact;
    const size_t size = encodeControlResponse(response, exact, prefix);
    assert(size == encoder.bytes_written && size > 16384);
    assert(outputAllocation == size + prefix);
    assert(!memcmp(exact.get() + prefix, bytes, size));
    if (prefix) { memset(exact.get(), 0xa5, prefix); assert(!memcmp(exact.get() + prefix, bytes, size)); }
  }
  luxio_Response decoded = luxio_Response_init_zero;
  auto decoder = pb_istream_from_buffer(bytes, encoder.bytes_written);
  assert(pb_decode(&decoder, luxio_Response_fields, &decoded));
  assert(decoded.script && !strcmp(decoded.script->source, script.source));
  assert(decoded.has_network_status && decoded.network_status.connected);
  assert(strlen(decoded.network_status.ssid) == 32);
  assert(!strcmp(decoded.network_status.gateway, "192.0.2.1"));
  assert(decoded.network_status.dns_count == 2 && !strcmp(decoded.network_status.dns[1], "1.1.1.1"));
  pb_release(luxio_Response_fields, &decoded);
  luxio_Config relayConfig = luxio_Config_init_zero;
  assert(!relayConfig.has_relay && !relayConfig.relay.enabled);
  relayConfig.has_relay = true; relayConfig.relay.enabled = true;
  relayConfig.relay.gpio = 0; relayConfig.relay.inverted = true;
  auto relayEncoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
  assert(pb_encode(&relayEncoder, luxio_Config_fields, &relayConfig));
  luxio_Config relayDecoded = luxio_Config_init_zero;
  auto relayDecoder = pb_istream_from_buffer(bytes, relayEncoder.bytes_written);
  assert(pb_decode(&relayDecoder, luxio_Config_fields, &relayDecoded));
  assert(relayDecoded.has_relay && relayDecoded.relay.enabled && relayDecoded.relay.gpio == 0 && relayDecoded.relay.inverted);
  relayConfig.relay = luxio_RelayConfig_init_zero;
  relayEncoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
  assert(pb_encode(&relayEncoder, luxio_Config_fields, &relayConfig));
  relayDecoder = pb_istream_from_buffer(bytes, relayEncoder.bytes_written);
  relayDecoded = luxio_Config_init_zero;
  assert(pb_decode(&relayDecoder, luxio_Config_fields, &relayDecoded));
  assert(relayDecoded.has_relay && !relayDecoded.relay.enabled); // Explicit disable survives the wire.
  luxio_State state = luxio_State_init_zero;
  state.build = 28; state.matter_disabled = true;
  strcpy(state.lan_ip, "192.0.2.5");
  luxio_Gradient gradient = luxio_Gradient_init_zero;
  memset(gradient.editor_metadata, 'x', 2048);
  gradient.colors_count = 2;
  gradient.colors[0].red = 255; gradient.colors[1].blue = 255;
  response = luxio_Response_init_zero;
  response.id = 42; response.state = &state; response.gradient = &gradient;
  encoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
  assert(pb_encode(&encoder, luxio_Response_fields, &response));
  decoded = luxio_Response_init_zero;
  decoder = pb_istream_from_buffer(bytes, encoder.bytes_written);
  assert(pb_decode(&decoder, luxio_Response_fields, &decoded));
  assert(decoded.state && decoded.state->build == 28 && decoded.state->matter_disabled && !strcmp(decoded.state->lan_ip, state.lan_ip));
  assert(decoded.gradient && !strcmp(decoded.gradient->editor_metadata, gradient.editor_metadata));
  pb_release(luxio_Response_fields, &decoded);
  assert(!decoded.state && !decoded.gradient);
  for (uint32_t id : {uint32_t(0), uint32_t(42), UINT32_MAX}) {
    request = luxio_Request_init_zero;
    request.id = id; request.gradient = &gradient;
    encoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
    assert(pb_encode(&encoder, luxio_Request_fields, &request));
    luxio_Request incoming = luxio_Request_init_zero;
    decoder = pb_istream_from_buffer(bytes, encoder.bytes_written);
    assert(pb_decode(&decoder, luxio_Request_fields, &incoming));
    assert(incoming.gradient && !strcmp(incoming.gradient->editor_metadata, gradient.editor_metadata));
    pb_release(luxio_Request_fields, &incoming);
    uint8_t emergency[48];
    const size_t size = encodeControlError(bytes, encoder.bytes_written, emergency, sizeof(emergency));
    assert(size > 0 && size <= sizeof(emergency));
    decoded = luxio_Response_init_zero;
    decoder = pb_istream_from_buffer(emergency, size);
    assert(pb_decode(&decoder, luxio_Response_fields, &decoded));
    assert(decoded.id == id && !strcmp(decoded.error, "Not enough memory for this request"));
    assert(!encodeControlError(bytes, encoder.bytes_written, emergency, 1));
  }
  std::unique_ptr<uint8_t[]> exact;
  response = luxio_Response_init_zero;
  response.id = 42; response.state = &state;
  size_t size = encodeControlResponse(response, exact, 0);
  assert(size > 0 && size < 128); // Small status replies stay small.
  assert(outputAllocation == size);
  failOutputAllocation = true;
  assert(!encodeControlResponse(response, exact, 0) && !exact);
  failOutputAllocation = false;
  assert(!encodeControlResponse(response, exact, 9) && !exact);
  // Callback listings run for sizing and encoding with stable pagination.
  response.scripts.arg = &script;
  response.scripts.funcs.encode = [](pb_ostream_t *out, const pb_field_t *field, void *const *arg) {
    return pb_encode_tag_for_field(out, field) && pb_encode_submessage(out, luxio_Script_fields, *arg);
  };
  size = encodeControlResponse(response, exact, 0);
  assert(size > 16384);
  decoded = luxio_Response_init_zero;
  decoder = pb_istream_from_buffer(exact.get(), size);
  assert(pb_decode(&decoder, luxio_Response_fields, &decoded) && decoded.id == 42);
  pb_release(luxio_Response_fields, &decoded);
  response.scripts.funcs.encode = [](pb_ostream_t *out, const pb_field_t *field, void *const *arg) {
    for (int i = 0; i < 3; i++) if (!pb_encode_tag_for_field(out, field) || !pb_encode_submessage(out, luxio_Script_fields, *arg)) return false;
    return true;
  };
  assert(!encodeControlResponse(response, exact, 0) && !exact); // Wire limit still enforced.
  response.scripts.funcs.encode = [](pb_ostream_t *, const pb_field_t *, void *const *) { return false; };
  assert(!encodeControlResponse(response, exact, 0) && !exact);
  puts("PASS: compact control structs, full 16 KB scripts, malformed payload cleanup and release");
}
