#include "luxio.pb.h"
#include "animation_parameters.h"
extern "C" {
#include "vendor/lua/lua.h"
#include "vendor/lua/lauxlib.h"
#include "vendor/lua/lualib.h"
}
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
using String = std::string;
static luxio_State state;
static uint32_t wallTime, cpuTime, cpuStep = 500;
static uint32_t millis() { return wallTime; }
static uint32_t esp_random() { return 1; }
static bool psramFound() { return false; }
static void *ps_realloc(void *p, size_t n) { return realloc(p,n); }
static void *ps_calloc(size_t n, size_t w) { return calloc(n,w); }
#define strlcpy copyString
static void strlcpy(char *to, const char *from, size_t n) { snprintf(to,n,"%s",from); }
struct { bool active=false, identifying=false; void begin(uint32_t) {} } fade;
static bool identifyActive;
static unsigned transmissions;
static void render() { transmissions++; }
static auto *transition = &fade;
static uint8_t (*pixels)[6];
static size_t frameCapacity;
static void applyColor() { for(size_t i=0;i<frameCapacity;i++) { pixels[i][0]=state.color.red; for(int c=1;c<6;c++)pixels[i][c]=0; } }
struct TaskStatus_t { uint32_t ulRunTimeCounter; };
#define pdFALSE 0
#define eInvalid 0
// CPU and wall clocks are deliberately independent. Each instruction hook costs
// a fixed simulated CPU interval; an arbitrary network pause must not spend it.
static void taskYIELD() { cpuTime += cpuStep; }
static void vTaskGetInfo(void *, TaskStatus_t *s,int,int) { s->ulRunTimeCounter=cpuTime; }
// PRODUCTION_GLOBALS
// PRODUCTION_STOP
// PRODUCTION_RUNTIME
static void step() {
// PRODUCTION_STEP
}
static void output() {
// PRODUCTION_OUTPUT
}
int main() {
 for(size_t count: {1,256,1024}) {
  state={}; state.config.led_count=count; strcpy(state.config.strip_type,"WS2812"); state.color.red=7;
  frameCapacity=count; pixels=static_cast<uint8_t (*)[6]>(calloc(count,6));
  // These loops intentionally take many slices. No partial output is published.
  luxio_Script script{}; script.source=strdup("function setup() for j=1,12000 do local x=j*j end end\nfunction loop() for i=0,luxio_get_pixel_count()-1 do luxio_set_pixel_color(i,123,45,67); for j=1,100 do local x=j*j end end end");
  assert(startAnimation(script).empty() && !script.source);
  unsigned slices=0;
  while(!pixels[0][0]) {
   wallTime+=37; // Paused wall time is not animation CPU time.
   const uint32_t before=cpuTime; step(); slices++;
   assert(cpuTime-before<=6500 && animation && !animationError[0]);
   for(size_t i=0;i<count;i++)assert(pixels[i][0]==pixels[0][0]);
   assert(slices<500);
  }
  assert(slices>2 && pixels[count-1][2]==67);
  assert(animationFrameReady);output();assert(!animationFrameReady);
  unsigned sent=transmissions;output();assert(transmissions==sent);
  transition->active=true;output();assert(transmissions==++sent);transition->active=false;
  identifyActive=true;output();assert(transmissions==++sent);identifyActive=false;
  transition->identifying=true;output();assert(transmissions==++sent);transition->identifying=false;
  // Replacement while suspended must discard both its VM and unfinished frame.
  wallTime+=34;step();
  luxio_Script replacement{}; replacement.source=strdup("function loop() luxio_set_pixel_color(0,2,3,4) end");
  assert(startAnimation(replacement).empty());
  for(int i=0;i<4;i++){wallTime+=34;step();}
  assert(pixels[0][0]==2 && pixels[0][2]==4);
  stopAnimation(); assert(!animation && !animationPixels && luaMemory==0);
  // Hard limits survive slices, pcall, and startup/setup failures.
  for(const char *source: {"while true do end", "function setup() while true do end end function loop() end", "function loop() while true do pcall(function() while true do end end) end end", "function loop() error('bad frame') end", "local value=42"}) {
   luxio_Script bad{};bad.source=strdup(source); assert(startAnimation(bad).empty());
   unsigned turns=0;
   while(animation){wallTime+=34;step();assert(++turns<500);}
   assert(animationError[0] && !animationPixels && luaMemory==0);
   assert(pixels[0][0]==7);
  }
  // Independently exercise both hard limits; neither may reset on resume.
  for(uint32_t cost: {0u, 5000u}) {
   cpuStep=cost;
   luxio_Script bad{};bad.source=strdup("function loop() while true do end end");
   assert(startAnimation(bad).empty());
   unsigned turns=0;
   while(animation){wallTime+=34;step();assert(++turns<500);}
   assert(strstr(animationError,cost ? "CPU limit" : "instruction limit"));
   assert(!animationPixels && luaMemory==0);
  }
  cpuStep=500;
  // Real allocator cap, including source/setup execution, remains enforced.
  luxio_Script memory{};memory.source=strdup("function loop() local x=string.rep('a',200000) end");
  assert(startAnimation(memory).empty());for(int i=0;i<5 && animation;i++){wallTime+=34;step();}
  assert(!animation && animationError[0] && luaMemory==0);
  free(pixels);
 }
 // Counters wrapping must not make a normal coroutine fail immediately.
 wallTime=UINT32_MAX-20;cpuTime=UINT32_MAX-1500;state.config.led_count=1;frameCapacity=1;pixels=static_cast<uint8_t (*)[6]>(calloc(1,6));
 luxio_Script wrap{};wrap.source=strdup("function loop() for i=1,3000 do local x=i*i end luxio_set_pixel_color(0,99,0,0) end");assert(startAnimation(wrap).empty());
 for(int i=0;i<30 && pixels[0][0]!=99;i++){wallTime+=5;step();assert(animation);}
 assert(pixels[0][0]==99);stopAnimation();free(pixels);
 puts("PASS: cooperative startup/frames, atomic output, replacement, CPU/instruction/memory limits and clock wrap");
}
