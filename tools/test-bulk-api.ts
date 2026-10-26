import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { Animation } from '../shared/device/src/lua.js';
const cases = JSON.parse(readFileSync(new URL('./fixtures/bulk-api.json', import.meta.url), 'utf8'));
for (const strip of ['WS2812', 'WS2814', 'WS2805', 'SK6812']) for (const count of [1, 6, 1024]) {
  for (let test = 0; test < cases.length; test++) {
    const animation = new Animation(cases[test].source, count, strip);
    const pixels = animation.tick(33);
    for (let i = 0; i < count; i++) {
      let expected = [0,0,0,0,0,0];
      if (test === 0) expected = i === count-1 ? [44,55,66,0,0,0] : [11,22,33,0,0,0];
      if (test === 1) expected = [...[[255,0,0],[255,255,0],[0,255,0],[0,255,255],[0,0,255],[255,0,255]][i%6],0,0,0];
      if (test === 2) expected = [127,127,127,0,0,0];
      if (test === 3 || test === 8) expected = strip === 'WS2805' ? [0,0,0,0,180,75] : strip === 'WS2812' ? [255,190,105,0,0,0] : [0,0,0,255,0,0];
      if (test === 4) expected = [...[[0,0,255],[0,255,255],[0,255,0],[255,255,0],[255,0,0],[255,0,255]][i%6],0,0,0];
      if (test === 5) expected = [255,0,0,0,0,0];
      if (test === 6) expected = [127,31,0,0,0,0];
      if (test === 7) { const v = 0.5 + 0.25*Math.sin(0.3-i*0.2) + 0.1*Math.sin(-0.7+i*0.4); expected = [Math.floor(255*v),Math.floor(100*v**8),0,0,0,0]; }
      if (test === 9) expected = [11,22,33,0,0,0];
      const actual = pixels[i];
      [actual.red,actual.green,actual.blue,actual.white,actual.warmWhite,actual.coolWhite].forEach((value,c) => assert.ok(Math.abs(value-expected[c]) <= (test===1||test===4||test===7?1:0), `${cases[test].name}, ${strip}, pixel ${i}, channel ${c}`));
    }
    animation.close();
  }
}
console.log('PASS: bulk fill/range/HSV/waves, bounds, rejected writes, white channels and 1/6/1024 pixels');
