#pragma once
#include <stdint.h>

// Debounce both edges; a held button at startup must be released before use.
struct ButtonInput {
  enum Event { None, Press, LongPress };
  bool raw = false, stable = false, armed = false, pressed = false, longPressed = false;
  uint32_t changedAt = 0, pressedAt = 0;

  void reset(bool down, uint32_t now) {
    raw = stable = down; armed = !down; pressed = longPressed = false;
    changedAt = pressedAt = now;
  }

  Event update(bool down, uint32_t now) {
    if (down != raw) { raw = down; changedAt = now; }
    if (raw != stable && uint32_t(now - changedAt) >= 30) {
      stable = raw;
      if (stable && armed) { pressed = true; longPressed = false; pressedAt = changedAt; }
      else if (!stable) {
        const bool releasedPress = pressed && !longPressed;
        const bool held = uint32_t(changedAt - pressedAt) >= 1000;
        armed = true; pressed = false;
        if (releasedPress) return held ? LongPress : Press;
      }
    }
    if (stable && raw && pressed && !longPressed && uint32_t(now - pressedAt) >= 1000) {
      longPressed = true;
      return LongPress;
    }
    return None;
  }
};
