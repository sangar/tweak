#pragma once

#include "emit/emit.h"

// flutter_dart writes <name>_theme.dart: the tokens as a ThemeExtension with
// light and dark values, and light and dark ThemeData built on a Material 3
// ColorScheme filled from them.
void flutter_dart(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output);
