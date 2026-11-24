#include <cassert>
#include <cstdio>
#include "../Luxio/src/button_input.h"

int main() {
  ButtonInput button;
  button.reset(false, 0);
  // Contact bounce never generates extra presses.
  assert(button.update(true, 100) == ButtonInput::None);
  assert(button.update(false, 110) == ButtonInput::None);
  assert(button.update(true, 120) == ButtonInput::None);
  assert(button.update(true, 150) == ButtonInput::None);
  assert(button.update(false, 200) == ButtonInput::None);
  assert(button.update(true, 210) == ButtonInput::None);
  assert(button.update(false, 220) == ButtonInput::None);
  assert(button.update(false, 250) == ButtonInput::Press);
  assert(button.update(false, 300) == ButtonInput::None);
  // Long hold fires once and never fires a short press on release.
  assert(button.update(true, 400) == ButtonInput::None);
  assert(button.update(true, 430) == ButtonInput::None);
  assert(button.update(true, 1399) == ButtonInput::None);
  assert(button.update(true, 1400) == ButtonInput::LongPress);
  assert(button.update(true, 5000) == ButtonInput::None);
  assert(button.update(false, 5010) == ButtonInput::None);
  assert(button.update(false, 5040) == ButtonInput::None);
  // Boot/config changes while held require release before either action works.
  button.reset(true, 6000);
  assert(button.update(true, 9000) == ButtonInput::None);
  assert(button.update(false, 9010) == ButtonInput::None);
  assert(button.update(false, 9040) == ButtonInput::None);
  assert(button.update(true, 9100) == ButtonInput::None);
  assert(button.update(true, 9130) == ButtonInput::None);
  assert(button.update(false, 9200) == ButtonInput::None);
  assert(button.update(false, 9230) == ButtonInput::Press);
  // millis() wraps after ~49 days.
  button.reset(false, UINT32_MAX - 100);
  assert(button.update(true, UINT32_MAX - 50) == ButtonInput::None);
  assert(button.update(true, UINT32_MAX - 20) == ButtonInput::None);
  assert(button.update(true, 948) == ButtonInput::None);
  assert(button.update(true, 949) == ButtonInput::LongPress);
  assert(button.update(false, 950) == ButtonInput::None);
  assert(button.update(false, 980) == ButtonInput::None);
  // A delayed poll on release still recognizes a hold, never a short press.
  button.reset(false, 1000);
  button.update(true, 1100); button.update(true, 1130);
  assert(button.update(false, 2200) == ButtonInput::None);
  assert(button.update(false, 2230) == ButtonInput::LongPress);
  // Reset cancels an in-progress press.
  button.reset(false, 1000);
  button.update(true, 1100); button.update(true, 1130);
  button.reset(false, 1150);
  assert(button.update(false, 1200) == ButtonInput::None);
  puts("PASS: button debounce, short press, long press, boot hold, reset and timer wrap");
}
