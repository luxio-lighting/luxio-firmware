#pragma once
#include "luxio.pb.h"

// Definitions are derived from the leading Lua comments, never trusted from clients.
const char *parseAnimationParameters(const char *source, luxio_AnimationParameter *parameters, pb_size_t &count);
const char *updateAnimationParameters(luxio_AnimationParameter *parameters, size_t count, const luxio_AnimationParameter *patch, size_t patchCount);

const char *parseAnimationPreview(const char *source, luxio_Color *colors, pb_size_t &count);
