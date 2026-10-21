#pragma once

#include "emit/emit.h"

// tokens_json writes <name>.json: the color tokens as sRGB hex for light and
// dark, the radius scale and the font names, for any platform tweak has no
// format of its own for.
void tokens_json(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output);
