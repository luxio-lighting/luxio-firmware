#include <cassert>
#include <cstdio>
#include <vector>
#include "../Luxio/src/led_encoder.h"
struct Symbol { uint32_t val; };
int main() {
  for (size_t count : {size_t(1), size_t(60), size_t(1024)}) {
    std::vector<uint8_t> pixels(count * 6);
    for (size_t i = 0; i < pixels.size(); i++) pixels[i] = uint8_t(i * 37 + 129);
    for (size_t channels : {size_t(3), size_t(4), size_t(5)}) for (bool greenFirst : {false, true}) for (bool longOne : {false, true}) {
      if (channels == 5 && greenFirst) continue;
      const int order3[] = {greenFirst ? 1 : 0, greenFirst ? 0 : 1, 2}, order4[] = {greenFirst ? 1 : 0, greenFirst ? 0 : 1, 2, 3}, order5[] = {0, 1, 2, 4, 5};
      const int *order = channels == 3 ? order3 : channels == 4 ? order4 : order5;
      std::vector<uint8_t> spi(count * channels * 4 + 241, 0xa5);
      const size_t length = encodeLedSpi(reinterpret_cast<const uint8_t (*)[6]>(pixels.data()), count, channels, spi.data(), greenFirst, longOne);
      assert(length % 4 == 0 && length == count * channels * 4 + 240);
      assert(spi.back() == 0xa5);
      for (size_t byte = 0; byte < count * channels; byte++) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; bit++) {
          const unsigned symbol = (spi[byte * 4 + bit / 2] >> (bit % 2 == 0 ? 4 : 0)) & 15;
          assert(symbol == 8 || symbol == (longOne ? 14 : 12));
          value = uint8_t((value << 1) | (symbol != 8));
        }
        assert(value == pixels[(byte / channels) * 6 + order[byte % channels]]);
      }
      for (size_t i = count * channels * 4; i < length; i++) assert(spi[i] == 0);

    }
  }
  puts("PASS: SPI DMA symbols/reset/alignment, RGB/RGBW/RGBCCT ordering, 1/60/1024 LEDs and buffer bounds");
}
