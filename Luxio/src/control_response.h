#pragma once
#include <memory>
#include <new>
#include "luxio.pb.h"
#include "vendor/nanopb/pb_encode.h"

// Call with stateMutex held: borrowed state and filesystem callbacks must stay
// stable between sizing and encoding. A transport may reserve its frame prefix.
inline size_t encodeControlResponse(luxio_Response &response, std::unique_ptr<uint8_t[]> &output, size_t prefix, const char **error = nullptr) {
  output.reset();
  if (error) *error = nullptr;
  size_t size = 0;
  if (prefix > 8) {
    if (error) *error = "Invalid response prefix";
    return 0;
  }
  if (!pb_get_encoded_size(&size, luxio_Response_fields, &response) || !size) {
    if (error) *error = "Unable to size response protobuf";
    return 0;
  }
  if (size > 32768) {
    if (error) *error = "Response exceeds 32 KiB";
    return 0;
  }
  output.reset(new (std::nothrow) uint8_t[size + prefix]);
  if (!output) {
    if (error) *error = "Not enough memory for response buffer";
    return 0;
  }
  auto encoder = pb_ostream_from_buffer(output.get() + prefix, size);
  if (!pb_encode(&encoder, luxio_Response_fields, &response) || encoder.bytes_written != size) {
    if (error) *error = "Response protobuf changed or failed encoding";
    output.reset(); return 0;
  }
  return size;
}
