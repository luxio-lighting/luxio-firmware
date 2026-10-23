#include "../Luxio/src/animation_parameters.h"
#include "../Luxio/src/script_storage.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <cstdio>
int main() {
  luxio_AnimationParameter parameters[16]{}; pb_size_t count = 0;
  const char *source = "-- @parameter Flicker,Boolean,true\r\n-- @parameter Speed,Number,50,-10,100 // comment\nfunction loop() end";
  assert(!parseAnimationParameters(source, parameters, count)); assert(count == 2); assert(parameters[0].boolean); assert(parameters[1].minimum == -10);
  for (const char *invalid : {"-- @parameter Speed,Number,101,0,100", "-- @parameter X,Boolean,1", "-- @parameter X,Number,nan,0,100", "-- @parameter X,Number,1,0-100", "-- @parameter X,Number,1,0,0", "-- @parameter X,Number,1e999,0,100", "-- @parameter X,Boolean,true\n-- @parameter X,Boolean,false", "function loop() end\n-- @parameter X,Boolean,true"}) assert(parseAnimationParameters(invalid, parameters, count));
  std::string tooMany;
  for (int i = 0; i < 17; ++i) tooMany += "-- @parameter P" + std::to_string(i) + ",Boolean,true\n";
  assert(parseAnimationParameters(tooMany.c_str(), parameters, count));
  assert(!parseAnimationParameters(source, parameters, count));
  luxio_AnimationParameter patch[2] = {parameters[0], parameters[1]}; patch[0].value = 0; patch[1].value = 101;
  assert(updateAnimationParameters(parameters, count, patch, 2)); assert(parameters[0].value == 1);
  patch[1].value = 99; assert(!updateAnimationParameters(parameters, count, patch, 2)); assert(parameters[0].value == 0); assert(parameters[1].value == 99);
  patch[1].value = NAN; assert(updateAnimationParameters(parameters, count, patch, 2));
  patch[1] = patch[0]; assert(updateAnimationParameters(parameters, count, patch, 2));
  luxio_Script saved{}; strcpy(saved.id, "11111111-1111-4111-8111-111111111111"); strcpy(saved.name, "Parameterized"); strcpy(saved.source, source); saved.mode = luxio_Mode_ANIMATION;
  saved.parameters_count = count; memcpy(saved.parameters, parameters, sizeof(parameters));
  std::vector<uint8_t> bytes(SCRIPT_RECORD_MAX); auto size = encodeStoredScript(saved, bytes.data(), bytes.size()); assert(size);
  luxio_Script decoded{}; assert(decodeStoredScript(bytes.data(), size, decoded)); assert(decoded.parameters_count == 2); assert(decoded.parameters[0].value == 0); assert(decoded.parameters[1].value == 99); assert(!strcmp(decoded.source, source));
  assert(decodeStoredScript(bytes.data(), size, decoded, true)); assert(decoded.source[0] == 0); assert(decoded.parameters[1].value == 99);
  const char *controls = "-- @parameter Speed,Slider,50,0,100\n-- @parameter Spacing,Number,8,1,64\n-- @parameter Pulse,Checkbox,true\n-- @parameter Legacy,Boolean,false\nfunction loop() end";
  assert(!parseAnimationParameters(controls, saved.parameters, saved.parameters_count));
  assert(saved.parameters_count == 4);
  assert(!saved.parameters[0].number_input && !saved.parameters[0].boolean);
  assert(saved.parameters[1].number_input && !saved.parameters[1].boolean);
  assert(saved.parameters[2].boolean && saved.parameters[3].boolean);
  size = encodeStoredScript(saved, bytes.data(), bytes.size()); assert(size);
  assert(decodeStoredScript(bytes.data(), size, decoded));
  assert(decoded.parameters_count == 4 && decoded.parameters[1].number_input);
  assert(!decoded.parameters[0].number_input && decoded.parameters[2].boolean);
  assert(decodeStoredScript(bytes.data(), size, decoded, true) && decoded.parameters[1].number_input);
  for (const char *invalid : {"-- @parameter X,Slider,2,0,1", "-- @parameter X,Checkbox,1", "-- @parameter X,Slider,1", "-- @parameter X,Checkbox,true,0,1"}) assert(parseAnimationParameters(invalid, parameters, count));
  luxio_Color preview[8]{}; pb_size_t previewCount = 0;
  assert(!parseAnimationPreview("-- @preview #Ff0000, 00ff00,0000ff // hint\r\nfunction loop() end", preview, previewCount));
  assert(previewCount == 3 && preview[0].red == 255 && preview[1].green == 255 && preview[2].blue == 255);
  for (const char *invalid : {"-- @preview ff0000", "-- @preview ff0000,", "-- @preview fff,000000", "-- @preview zz0000,000000", "-- @preview ff0000,000000\n-- @preview 000000,ffffff", "function loop() end\n-- @preview ff0000,000000", "-- @preview ff0000,ff0000,ff0000,ff0000,ff0000,ff0000,ff0000,ff0000,ff0000"}) assert(parseAnimationPreview(invalid, preview, previewCount));
  assert(!parseAnimationPreview("function loop() end", preview, previewCount) && previewCount == 0);
  assert(!parseAnimationPreview("-- @preview ff0000,00ff00,0000ff", saved.preview, saved.preview_count));
  size = encodeStoredScript(saved, bytes.data(), bytes.size()); assert(size);
  assert(decodeStoredScript(bytes.data(), size, decoded, true));
  assert(decoded.preview_count == 3 && decoded.preview[1].green == 255 && decoded.source[0] == 0);
  assert(decodeStoredScript(bytes.data(), size, decoded));
  assert(decoded.preview_count == 3 && decoded.preview[2].blue == 255 && !strcmp(decoded.source, source));
  puts("PASS: preview parsing and compressed metadata roundtrip");
  puts("PASS: C++ parameter validation, atomic updates, compressed persistence and metadata readback");
}
