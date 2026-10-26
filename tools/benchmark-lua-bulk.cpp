// Paired measurement body used by benchmark-lua-precision.py --bulk --paired.
// Both paths share the same Lua VM, globals table, compiled script and timestamps.
static int benchmark(FILE *) {
  printf("META,number_bytes=%u,integer_bytes=%u,paired=same-VM\n", unsigned(sizeof(lua_Number)),unsigned(sizeof(lua_Integer)));
  for(const auto &preset: presets) for(unsigned count: {100u,1024u}) {
    state={};state.config.led_count=count;strcpy(state.config.strip_type,"WS2812");
    pixels=static_cast<uint8_t (*)[6]>(calloc(count,6));assert(pixels);
    std::unique_ptr<uint8_t[]> reference(new uint8_t[count*6]);
    luxio_Script script{};script.source=strdup(preset.source);benchmarkClock=0;
    String error=startAnimation(script);
    if(error.length()){printf("ERROR,%s,%s\n",preset.name,error.c_str());return 1;}
    // Without setup(), entering phase 2 can start a sliced loop immediately.
    // Finish that call before comparing two fresh invocations at one timestamp.
    while(animation && (animationPhase<2 || animationPending))step();
    if(!animation){printf("ERROR,%s,%s\n",preset.name,animationError);return 1;}
    const char *names[]={"luxio_fill","luxio_fill_range","luxio_fill_hsv","luxio_render_waves"};
    for(const char *name:names){lua_getglobal(animation,name);lua_setfield(animation,LUA_REGISTRYINDEX,name);}
    uint32_t times[2][60];uint64_t total[2]={0,0};unsigned maxDifference=0;
    for(unsigned frame=0;frame<65;frame++) {
      // Alternate which implementation runs first, reducing cache-order bias.
      for(unsigned pass=0;pass<2;pass++) {
        unsigned mode=(frame+pass)%2;
        for(const char *name:names){
          if(mode)lua_getfield(animation,LUA_REGISTRYINDEX,name);else lua_pushnil(animation);
          lua_setglobal(animation,name);
        }
        benchmarkClock=1000+frame*33;animationFrameAt=benchmarkClock-33;animationFrameReady=false;
        uint32_t elapsed=0;
        do {
          TaskStatus_t before{},after{};
          taskYIELD();vTaskGetInfo(nullptr,&before,pdFALSE,eInvalid);
          step();
          taskYIELD();vTaskGetInfo(nullptr,&after,pdFALSE,eInvalid);
          elapsed+=uint32_t(after.ulRunTimeCounter-before.ulRunTimeCounter);
          if(!animation){printf("ERROR,%s,%s\n",preset.name,animationError);return 1;}
        } while(!animationFrameReady);
        if(frame>=5){times[mode][frame-5]=elapsed;total[mode]+=elapsed;}
        if(!pass)memcpy(reference.get(),pixels,count*6);
        else if(frame>=5)for(unsigned i=0;i<count*6;i++)maxDifference=std::max(maxDifference,unsigned(abs(int(reference[i])-reinterpret_cast<uint8_t *>(pixels)[i])));
      }
    }
    if(maxDifference>1){printf("ERROR,%s,output difference %u\n",preset.name,maxDifference);return 1;}
    for(unsigned mode=0;mode<2;mode++) {
      std::sort(times[mode],times[mode]+60);
      printf("RESULT,%s (%s),%u,%.2f,%u,%u,%u,%u,%u\n",preset.name,mode?"bulk":"fallback",count,double(total[mode])/60,times[mode][29],times[mode][56],times[mode][59],unsigned(luaMemory),maxDifference);
    }
    stopAnimation();free(pixels);pixels=nullptr;
#ifdef ARDUINO
    delay(1);
#endif
  }
  puts("BENCHMARK_DONE");return 0;
}
