#include "../Luxio/src/transition.h"
#include <cassert>
#include <cstdio>

int main() {
  OutputTransition fade;
  uint8_t pixels[1024][6] = {};
  pixels[0][0] = 255;
  pixels[1023][4] = 255;
  fade.begin(100);
  fade.render(pixels, 1024, 255, 100);
  assert(fade.displayed[0][0] == 0);
  fade.render(pixels, 1024, 255, 350);
  assert(fade.displayed[0][0] == 128 && fade.displayed[1023][4] == 128);
  assert(fade.displayed[0][3] == 0 && fade.displayed[0][4] == 0);
  fade.render(pixels, 1024, 255, 600);
  assert(fade.displayed[0][0] == 255 && !fade.active);
  // Interrupt a color fade with another command: preserve the visible frame.
  pixels[0][0] = 0; pixels[0][2] = 255;
  fade.begin(600); fade.render(pixels, 1024, 255, 850);
  const auto red = fade.displayed[0][0], blue = fade.displayed[0][2];
  fade.begin(850); pixels[0][1] = 255;
  fade.render(pixels, 1024, 255, 850);
  assert(fade.displayed[0][0] == red && fade.displayed[0][2] == blue && fade.displayed[0][1] == 0);
  fade.render(pixels, 1024, 255, 1350);
  assert(fade.displayed[0][0] == 0 && fade.displayed[0][1] == 255 && fade.displayed[0][2] == 255);
  // Power off fades monotonically and reaches exact black.
  fade.begin(1400);
  int previous = 255;
  for (int t = 0; t <= 500; t += 10) {
    fade.render(pixels, 1024, 0, 1400 + t);
    assert(fade.displayed[0][1] <= previous); previous = fade.displayed[0][1];
  }
  assert(previous == 0);
  fade.begin(2000); fade.render(pixels, 1024, 16, 2500);
  assert(fade.displayed[0][1] == 16);
  // Ordinary animation frames pass through after the transition completes.
  pixels[0][1] = 0; fade.render(pixels, 1024, 16, 2533);
  assert(fade.displayed[0][1] == 0);
  // Unsigned millis wraparound still completes a fade.
  fade.begin(UINT32_MAX - 100); fade.render(pixels, 1024, 255, 399);
  assert(!fade.active && fade.displayed[0][2] == 255);
  // Identify must be visible while off, without changing the underlying frame.
  OutputTransition identify;
  identify.render(pixels, 1024, 0, 10000, true);
  assert(identify.displayed[0][0] == 0);
  int previousPulse = 0;
  for (uint32_t t = 0; t <= 500; t += 10) {
    identify.render(pixels, 1024, 0, 10000 + t, true);
    assert(identify.displayed[0][0] >= previousPulse);
    previousPulse = identify.displayed[0][0];
  }
  for (uint32_t i = 0; i < 1024; i++) for (int c = 0; c < 6; c++)
    assert(identify.displayed[i][c] == (c < 3 ? 32 : 0));
  for (uint32_t t = 510; t <= 1000; t += 10) {
    identify.render(pixels, 1024, 0, 10000 + t, true);
    assert(identify.displayed[0][0] <= previousPulse);
    previousPulse = identify.displayed[0][0];
  }
  assert(previousPulse == 0);
  identify.render(pixels, 1024, 0, 11500, true);
  assert(identify.displayed[0][0] == 32);
  identify.render(pixels, 1024, 0, 11500, false);
  assert(identify.displayed[0][0] == 32); // Stop begins at the actual displayed frame.
  identify.render(pixels, 1024, 0, 12000, false);
  assert(identify.displayed[0][0] == 0);
  // Keep current animation/color changes, including independent white channels.
  pixels[0][0] = 200; pixels[0][4] = 80;
  identify.render(pixels, 1024, 128, 13000);
  identify.render(pixels, 1024, 128, 13000, true);
  assert(identify.displayed[0][0] == 100 && identify.displayed[0][4] == 40);
  identify.render(pixels, 1024, 128, 13250, true);
  const auto interruptedRed = identify.displayed[0][0];
  pixels[0][0] = 0; pixels[0][1] = 100;
  identify.render(pixels, 1024, 128, 13250, false);
  assert(identify.displayed[0][0] == interruptedRed);
  identify.render(pixels, 1024, 128, 13750);
  assert(identify.displayed[0][0] == 0 && identify.displayed[0][1] == 50 && identify.displayed[0][4] == 40);
  assert(pixels[0][1] == 100 && pixels[0][4] == 80);
  // Identify timing, like normal fading, survives the millis counter wrapping.
  identify.render(pixels, 1024, 0, UINT32_MAX - 100, true);
  identify.render(pixels, 1024, 0, 399, true);
  assert(identify.displayed[0][0] == 32);
  identify.render(pixels, 1024, 0, 899, true);
  assert(identify.displayed[0][0] == 0);
  puts("PASS: Identify fade-blink, off-state visibility, all pixels, smooth stop, live color/animation restoration and clock wraparound");
  puts("PASS: output fades, interrupted transitions, independent channels, brightness, power off, animation frames, and clock wraparound");
}
