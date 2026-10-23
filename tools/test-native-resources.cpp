#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "../Luxio/src/gradient.h"
#include "../Luxio/src/gradient_storage.h"
#include "../Luxio/src/color_storage.h"
int main() {
  luxio_Gradient gradient{};
  strcpy(gradient.id, "12345678-1234-4234-8234-123456789012"); strcpy(gradient.name, "Evening");
  gradient.colors_count = 3;
  gradient.colors[0] = {255, 0, 0, 0, 50, 0}; gradient.colors[1] = {0, 255, 0, 0, 0, 100}; gradient.colors[2] = {0, 0, 255, 0, 0, 255};
  assert(validGradient(gradient, "WS2805")); assert(!validGradient(gradient, "WS2812"));
  uint8_t pixels[1024][6]{};
  for (int count : {1, 60, 1024}) {
    renderGradient(gradient, pixels, count);
    assert(pixels[0][0] == 255 && pixels[0][4] == 50);
    if (count > 1) assert(pixels[count - 1][2] == 255 && pixels[count - 1][5] == 255);
    for (int i = 0; i < count; i++) for (int c = 0; c < 6; c++) {
      const double position = double(i) / (count > 1 ? count - 1 : 1) * 2;
      const int segment = position < 1 ? 0 : 1;
      const double t = position - segment;
      const auto &a = gradient.colors[segment], &b = gradient.colors[segment + 1];
      const uint32_t first[] = {a.red,a.green,a.blue,a.white,a.warm_white,a.cool_white};
      const uint32_t last[] = {b.red,b.green,b.blue,b.white,b.warm_white,b.cool_white};
      assert(pixels[i][c] == int(first[c] + (double(last[c]) - first[c]) * t + 0.5));
    }
  }
  uint8_t bytes[4096]; luxio_Gradient decoded{};
  memset(gradient.editor_metadata, 'x', 2048);
  const size_t size = encodeStoredGradient(gradient, bytes, sizeof(bytes)); assert(size);
  assert(decodeStoredGradient(bytes, size, decoded)); assert(!strcmp(decoded.editor_metadata, gradient.editor_metadata));
  for (size_t i = 0; i < size; i++) { bytes[i] ^= 1; assert(!decodeStoredGradient(bytes, size, decoded)); bytes[i] ^= 1; }
  assert(!decodeStoredGradient(bytes, size - 1, decoded));
  gradient.colors_count = 1; assert(!encodeStoredGradient(gradient, bytes, sizeof(bytes)));
  gradient.colors_count = 9; assert(!validGradient(gradient, ""));
  gradient.colors_count = 2; gradient.colors[0].red = 256; assert(!validGradient(gradient, ""));
  luxio_SavedColor color{}, read{}; strcpy(color.id, "12345678-1234-4234-8234-123456789012"); strcpy(color.name, "Red"); color.has_color = true; color.color.red = 255;
  const size_t colorSize = encodeStoredColor(color, bytes, sizeof(bytes)); assert(colorSize && colorSize < 128);
  assert(decodeStoredColor(bytes, colorSize, read)); assert(read.color.red == 255);
  for (size_t i = 0; i < colorSize; i++) { bytes[i] ^= 1; assert(!decodeStoredColor(bytes, colorSize, read)); bytes[i] ^= 1; }
  assert(!decodeStoredGradient(bytes, colorSize, decoded));
  color.has_color = false; assert(!encodeStoredColor(color, bytes, sizeof(bytes)));
  puts("PASS: native interpolation, RGBCCT validation, separate CRC-protected color/gradient records");
}
