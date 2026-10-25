#pragma once
#include <stddef.h>
#include <stdint.h>

// SPI at 3.2 MHz: each LED bit becomes 1000 (zero) or 1100 (one).
// High/low times are 312.5/937.5 ns and 625/625 ns, with 1.25 us per bit.
// One DMA transaction includes the complete frame and at least 600 us low.
// The caller provides count * channels * 4 + 240 bytes of DMA-capable storage.
static size_t encodeLedSpi(const uint8_t pixels[][6], size_t count, size_t channels, uint8_t *bytes, bool grbw = false) {
  size_t position = 0;
  for (size_t i = 0; i < count; i++) {
    for (size_t c = 0; c < channels; c++) {
      size_t source = c;
      if ((channels == 3 || grbw) && c < 2) source = 1 - c;
      else if (channels == 5 && c >= 3) source++;
      const uint8_t value = pixels[i][source];
      for (int bit = 7; bit > 0; bit -= 2) {
        bytes[position++] = ((value & (1u << bit)) ? 0xc0 : 0x80) |
                            ((value & (1u << (bit - 1))) ? 0x0c : 0x08);
      }
    }
  }
  const size_t total = position + 240;
  while (position < total) bytes[position++] = 0;
  return total;
}
