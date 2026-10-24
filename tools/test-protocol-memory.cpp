#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "luxio.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "control_error.h"
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
  auto encoder = pb_ostream_from_buffer(bytes, sizeof(bytes));
  assert(pb_encode(&encoder, luxio_Response_fields, &response));
  luxio_Response decoded = luxio_Response_init_zero;
  auto decoder = pb_istream_from_buffer(bytes, encoder.bytes_written);
  assert(pb_decode(&decoder, luxio_Response_fields, &decoded));
  assert(decoded.script && !strcmp(decoded.script->source, script.source));
  pb_release(luxio_Response_fields, &decoded);
  luxio_State state = luxio_State_init_zero;
  state.build = 26;
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
  assert(decoded.state && decoded.state->build == 26 && !strcmp(decoded.state->lan_ip, state.lan_ip));
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
  puts("PASS: compact control structs, full 16 KB scripts, malformed payload cleanup and release");
}
