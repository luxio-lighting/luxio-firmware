import test from 'node:test';
import assert from 'node:assert/strict';
import { Animation } from '../shared/device/src/lua.js';

test('color capabilities select valid channels at the top level, setup and each frame', () => {
  const source = `local color_type = luxio_get_color_type()
function setup() assert(luxio_get_color_type() == color_type) end
function loop()
  assert(luxio_get_color_type() == color_type)
  for i = 0, luxio_get_pixel_count() - 1 do
    if color_type == "RGBCCT" then luxio_set_pixel_color(i, 0, 0, 0, 0, 180, 75)
    elseif color_type == "RGBW" then luxio_set_pixel_color(i, 0, 0, 0, 255)
    elseif color_type == "RGB" then luxio_set_pixel_color(i, 255, 190, 105)
    else error("Unknown color type") end
  end
end`;
  for (const [strip, expected] of [
    ['WS2812', [255, 190, 105, 0, 0, 0]],
    ['SK6812', [0, 0, 0, 255, 0, 0]],
    ['WS2814', [0, 0, 0, 255, 0, 0]],
    ['WS2805', [0, 0, 0, 0, 180, 75]],
  ] as const) for (const count of [1, 60, 1024]) {
    const animation = new Animation(source, count, strip);
    try {
      for (let frame = 0; frame < 3; frame++) {
        const pixels = animation.tick(33);
        assert.equal(pixels.length, count);
        for (const p of pixels) assert.deepEqual([p.red, p.green, p.blue, p.white, p.warmWhite, p.coolWhite], expected);
      }
    } finally { animation.close(); }
  }
});

test('a guarded capability call falls back to RGB on older runtimes', () => {
  const animation = new Animation(`luxio_get_color_type = nil
local color_type = luxio_get_color_type and luxio_get_color_type() or "RGB"
function loop()
  assert(color_type == "RGB")
  luxio_set_pixel_color(0, 255, 190, 105)
end`, 1, 'WS2805');
  try { assert.deepEqual(animation.tick()[0], { red: 255, green: 190, blue: 105, white: 0, warmWhite: 0, coolWhite: 0 }); } finally { animation.close(); }
});
