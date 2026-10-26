// Generated with the production Lua allocator, API, hook and frame scheduler.
// No radios or LED output: measurements isolate animation CPU time.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <math.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <chrono>
#include <string>
using String = std::string;
static bool psramFound() { return false; }
static void *ps_realloc(void *p, size_t n) { return realloc(p,n); }
static void *ps_calloc(size_t n, size_t w) { return calloc(n,w); }
static uint32_t esp_random() { return 1; }
#define strlcpy copyString
static void strlcpy(char *to, const char *from, size_t n) { snprintf(to,n,"%s",from); }
struct TaskStatus_t { uint32_t ulRunTimeCounter; };
#define pdFALSE 0
#define eInvalid 0
static void taskYIELD() {}
static void vTaskGetInfo(void *, TaskStatus_t *s,int,int) { s->ulRunTimeCounter=uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
#endif
#include "luxio.pb.h"
#include "animation_parameters.h"
extern "C" {
#include "vendor/lua/lua.h"
#include "vendor/lua/lauxlib.h"
#include "vendor/lua/lualib.h"
}
#define EXPAND_MATH(x) STRING_MATH(x)
#define STRING_MATH(x) #x
static luxio_State state;
static uint32_t benchmarkClock;
static uint32_t benchmarkMillis() { return benchmarkClock; }
struct { void begin(uint32_t) {} } fade;
static auto *transition = &fade;
static uint8_t (*pixels)[6];
static void applyColor() {}
// PRODUCTION_GLOBALS
// Keep identical timestamps for both numeric representations, including slices.
#define millis benchmarkMillis
// PRODUCTION_STOP
// PRODUCTION_RUNTIME
static void step() {
// PRODUCTION_STEP
}
#undef millis
// PRESETS
static int benchmark(FILE *frames) {
  printf("META,number_bytes=%u,integer_bytes=%u,sin=%s\n", unsigned(sizeof(lua_Number)),unsigned(sizeof(lua_Integer)),EXPAND_MATH(l_mathop(sin)));
  for(const auto &preset: presets) for(unsigned count: {100u,1024u}) {
    state={}; state.config.led_count=count; strcpy(state.config.strip_type,"WS2812");
    pixels=static_cast<uint8_t (*)[6]>(calloc(count,6)); assert(pixels);
    luxio_Script script{}; script.source=strdup(preset.source);
    benchmarkClock=0;
    String error=startAnimation(script);
    if(error.length()) { printf("ERROR,%s,%s\n",preset.name,error.c_str());return 1; }
    while(animation && animationPhase<2) step();
    if(!animation) { printf("ERROR,%s,%s\n",preset.name,animationError);return 1; }
    uint32_t times[60]; uint64_t total=0; uint32_t checksum=2166136261u;
    for(unsigned frame=0;frame<65;frame++) {
      benchmarkClock=1000+frame*33;
      animationFrameReady=false;
      uint32_t elapsed=0;
      do {
        TaskStatus_t before{},after{};
        // FreeRTOS accounts the running task at a context switch. Checkpoint
        // both boundaries so sorting/printing/previous-frame work is excluded.
        taskYIELD();
        vTaskGetInfo(nullptr,&before,pdFALSE,eInvalid);
        step();
        taskYIELD();
        vTaskGetInfo(nullptr,&after,pdFALSE,eInvalid);
        elapsed+=uint32_t(after.ulRunTimeCounter-before.ulRunTimeCounter);
        if(!animation) { printf("ERROR,%s,%s\n",preset.name,animationError);return 1; }
      } while(!animationFrameReady);
      if(frame>=5) {
        times[frame-5]=elapsed;total+=elapsed;
        for(unsigned i=0;i<count*6;i++)checksum=(checksum^reinterpret_cast<uint8_t *>(pixels)[i])*16777619u;
        if(frames)fwrite(pixels,6,count,frames);
      }
    }
    std::sort(times,times+60);
    printf("RESULT,%s,%u,%.2f,%u,%u,%u,%u,%u\n",preset.name,count,double(total)/60,times[30],times[56],times[59],unsigned(luaMemory),checksum);
    stopAnimation();free(pixels);pixels=nullptr;
#ifdef ARDUINO
    delay(1);
#endif
  }
  puts("BENCHMARK_DONE");return 0;
}
#ifdef ARDUINO
void setup() {
  Serial.begin(115200);delay(1500);
  printf("DEVICE,%s,%uMHz,psram=%u\n",ESP.getChipModel(),unsigned(ESP.getCpuFreqMHz()),unsigned(ESP.getPsramSize()));
  benchmark(nullptr);
}
void loop() { delay(1000); }
#else
int main(int argc,char **argv) {
  FILE *frames=argc>1?fopen(argv[1],"wb"):nullptr;
  int result=benchmark(frames);
  if(frames)fclose(frames);
  return result;
}
#endif
