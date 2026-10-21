#pragma once

#include "mc/text/str.h"

// The naming functions turn a theme or token name such as "modern-minimal"
// or "chart-1" into an identifier, splitting words at anything that is not
// an ASCII letter or digit and where a lower-case letter or digit meets an
// upper-case one. A result that would start with a digit, or be empty, gets
// a letter in front, so every result is a valid identifier.

// naming_pascal gives "ModernMinimal".
String naming_pascal(Arena *arena, String name);
// naming_camel gives "chart1", "cardForeground".
String naming_camel(Arena *arena, String name);
// naming_snake gives "card_foreground".
String naming_snake(Arena *arena, String name);
// naming_type is the theme's type name: "ModernMinimalTheme", without a
// second "Theme" when the name already ends in it.
String naming_type(Arena *arena, String theme_name);
