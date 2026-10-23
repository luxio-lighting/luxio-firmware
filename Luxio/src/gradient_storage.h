#pragma once
#include <stddef.h>
#include <stdint.h>
#include "luxio.pb.h"

constexpr size_t GRADIENT_RECORD_MAX = 4096;
size_t encodeStoredGradient(const luxio_Gradient &gradient, uint8_t *output, size_t capacity);
bool decodeStoredGradient(const uint8_t *bytes, size_t size, luxio_Gradient &gradient);
