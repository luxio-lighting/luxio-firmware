#pragma once
#include <stdint.h>
#include <string.h>
#include "luxio.pb.h"

// Gradients are data, not programs. Rendering never starts a Lua VM.
inline bool validGradient(const luxio_Gradient &gradient, const char *strip) {
  if (gradient.colors_count < 2 || gradient.colors_count > 8) return false;
  for (pb_size_t i = 0; i < gradient.colors_count; i++) {
    const auto &c = gradient.colors[i];
    if (c.red > 255 || c.green > 255 || c.blue > 255 || c.white > 255 || c.warm_white > 255 || c.cool_white > 255) return false;
    if ((!strcmp(strip, "WS2805") && c.white) || (!strcmp(strip, "WS2814") && (c.warm_white || c.cool_white)) ||
        (!strcmp(strip, "WS2812") && (c.white || c.warm_white || c.cool_white))) return false;
  }
  return true;
}

inline void renderGradient(const luxio_Gradient &gradient, uint8_t pixels[][6], uint32_t count) {
  if (gradient.colors_count < 2 || gradient.colors_count > 8 || !count) return;
  const uint32_t denominator = count > 1 ? count - 1 : 1;
  for (uint32_t i = 0; i < count; i++) {
    const uint32_t position = i * (gradient.colors_count - 1);
    const uint32_t segment = position / denominator < gradient.colors_count - 1 ? position / denominator : gradient.colors_count - 2;
    const uint32_t fraction = position - segment * denominator;
    const auto &a = gradient.colors[segment], &b = gradient.colors[segment + 1];
    const uint32_t first[6] = {a.red, a.green, a.blue, a.white, a.warm_white, a.cool_white};
    const uint32_t last[6] = {b.red, b.green, b.blue, b.white, b.warm_white, b.cool_white};
    for (uint32_t channel = 0; channel < 6; channel++)
      pixels[i][channel] = (first[channel] * (denominator - fraction) + last[channel] * fraction + denominator / 2) / denominator;
  }
}
