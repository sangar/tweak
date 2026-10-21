#pragma once

#include "emit/emit.h"

// android_compose writes <Name>Theme.kt: the tokens as a Jetpack Compose
// color class for light and dark, mapped onto a Material 3 ColorScheme, the
// radius scale as Material Shapes, and a <Name>Theme composable that
// provides both and follows the system's dark theme.
void android_compose(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output);

// android_resources writes values/ and values-night/ resource files with a
// color per token and the radius scale as dimens, always in sRGB.
void android_resources(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output);
