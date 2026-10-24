#include <cassert>
#include <cstring>
#include <cstdio>
#include "luxio.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
int main() {
  static_assert(sizeof(luxio_Request) < 5000);
  static_assert(sizeof(luxio_Response) < 10000);
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
  puts("PASS: compact control structs, full 16 KB scripts, malformed payload cleanup and release");
}
