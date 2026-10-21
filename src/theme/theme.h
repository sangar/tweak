#pragma once

#include "mc/core/error.h"
#include "theme/color.h"

// ThemeColor is one color token in both appearances.
typedef struct ThemeColor {
    // name is the custom property without its dashes, such as "card-foreground".
    String name;
    Color light;
    // dark is the light color when the theme gives the token no dark value.
    Color dark;
} ThemeColor;

// Theme is what of a tweakcn theme carries over to native apps: the color
// tokens, the corner radius and the font families. Its strings live in the
// arena given to theme_parse.
typedef struct Theme {
    String name;
    // colors are in the order the theme declares them.
    ThemeColor *colors;
    size_t color_count;
    bool has_radius;
    // radius is in CSS pixels, which apps use as points or dp.
    double radius;
    // The fonts are the first family of each stack that names a typeface
    // rather than a generic such as sans-serif; empty when there is none.
    String font_sans;
    String font_serif;
    String font_mono;
} Theme;

// theme_parse reads a theme as tweakcn serves it at /r/themes/<id>, a shadcn
// registry item whose cssVars hold the theme, light and dark variables, or
// as its code panel exports it, CSS with :root and .dark blocks. A variable
// that is var() of another is resolved. name is the theme's name when the
// text gives none. Text with no color token is ERR_PARSE.
[[nodiscard]] Error theme_parse(Arena *arena, String text, String name, Theme *theme, Err *err);

// ThemeRadii is shadcn's corner radius scale, in CSS pixels, derived from
// the theme's radius as tweakcn's @theme block derives --radius-sm to
// --radius-xl. A radius that would be negative is 0.
typedef struct ThemeRadii {
    double sm;
    double md;
    double lg;
    double xl;
} ThemeRadii;

ThemeRadii theme_radii(const Theme *theme);

// theme_color returns the token called name, or nullptr when the theme has none.
const ThemeColor *theme_color(const Theme *theme, String name);
