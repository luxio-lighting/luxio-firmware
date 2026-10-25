#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "luxio.pb.h"
constexpr size_t COLOR_RECORD_MAX = 256;
inline bool validColor(const luxio_Color &c, const char *strip) {
  if (c.red > 255 || c.green > 255 || c.blue > 255 || c.white > 255 || c.warm_white > 255 || c.cool_white > 255) return false;
  return !((!strcmp(strip, "WS2805") && c.white) || ((!strcmp(strip, "WS2814") || !strcmp(strip, "SK6812")) && (c.warm_white || c.cool_white)) || (!strcmp(strip, "WS2812") && (c.white || c.warm_white || c.cool_white)));
}
size_t encodeStoredColor(const luxio_SavedColor &color, uint8_t *output, size_t capacity);
bool decodeStoredColor(const uint8_t *bytes, size_t size, luxio_SavedColor &color);
