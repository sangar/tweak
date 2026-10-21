#pragma once

#include "mc/text/str.h"

// Color is a CSS color in OKLab with straight alpha, the space tweakcn edits
// in. It can hold colors no screen shows; color_to_rgb maps them into one.
typedef struct Color {
    double lightness;
    double a;
    double b;
    double alpha;
} Color;

// Gamut is an RGB space a platform draws in. Both use the sRGB transfer curve.
typedef enum Gamut {
    GAMUT_SRGB,
    GAMUT_DISPLAY_P3,
} Gamut;

// Rgb is a color in a gamut: gamma-encoded channels and alpha, each in [0, 1].
typedef struct Rgb {
    double red;
    double green;
    double blue;
    double alpha;
} Rgb;

// color_parse reads the CSS color syntaxes tweakcn writes: oklch(), oklab(),
// hsl(), hsla(), rgb(), rgba() in modern and legacy comma syntax, #hex, the
// keywords black, white and transparent, and the bare "H S% L%" triplets of
// a Tailwind 3 export. Anything else, such as var() or a length, is false.
bool color_parse(String text, Color *color);

// color_to_rgb maps color into gamut the way CSS Color 4 does: it lowers
// chroma at constant lightness and hue until clipping changes the color by
// less than a just-noticeable difference.
Rgb color_to_rgb(Color color, Gamut gamut);

// rgb_byte quantises one channel to 0 through 255.
uint8_t rgb_byte(double channel);
// rgb_argb packs the quantised channels as 0xAARRGGBB.
uint32_t rgb_argb(Rgb rgb);
