#pragma once
#include <stdint.h>
#include <string.h>

// Keep the last displayed frame so an interrupted fade starts without a jump.
// Colors stay independent; brightness is applied before the output crossfade.
struct OutputTransition {
  static constexpr uint32_t duration = 500;
  uint8_t displayed[1024][6] = {};
  uint8_t origin[1024][6] = {};
  uint32_t started = 0;
  bool active = false;
  bool identifying = false;
  uint32_t identifyStarted = 0;

  void begin(uint32_t now) {
    memcpy(origin, displayed, sizeof(origin));
    started = now;
    active = true;
  }

  void render(const uint8_t pixels[][6], uint32_t count, uint8_t brightness, uint32_t now, bool identify = false) {
    if (identify != identifying) {
      begin(now);
      identifying = identify;
      identifyStarted = now;
    }
    uint32_t elapsed = now - started;
    if (elapsed >= duration) active = false;
    float t = active ? float(elapsed) / duration : 1.0f;
    float blend = t * t * (3.0f - 2.0f * t);
    // Fade into a modest RGB-white pulse, including when the saved light is off.
    // The animation and brightness underneath keep running unchanged.
    uint8_t identifyLevel = 32;
    uint32_t identifyElapsed = now - identifyStarted;
    if (identifying && identifyElapsed >= duration) {
      uint32_t phase = (identifyElapsed - duration) % (2 * duration);
      float pulse = float(phase < duration ? duration - phase : phase - duration) / duration;
      identifyLevel = uint8_t(32 * pulse * pulse * (3.0f - 2.0f * pulse) + 0.5f);
    }
    for (uint32_t i = 0; i < count; i++) {
      for (int c = 0; c < 6; c++) {
        uint8_t target = (uint16_t(pixels[i][c]) * brightness + 127) / 255;
        if (identifying) target = c < 3 ? identifyLevel : 0;
        displayed[i][c] = active ? uint8_t(origin[i][c] + (int(target) - origin[i][c]) * blend + 0.5f) : target;
      }
    }
  }
};
