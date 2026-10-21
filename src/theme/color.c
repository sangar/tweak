#include "theme/color.h"

#include <math.h>

#include "mc/platform/platform.h"

enum { MAX_CHANNELS = 4 };

// CSS Color 4 gamut mapping: a deltaE OK below JND is invisible, and the
// chroma search stops once its interval is narrower than EPSILON.
static const double JND = 0.02;
static const double EPSILON = 0.0001;
// A channel this far outside [0, 1] is rounding noise, not out of gamut.
static const double GAMUT_TOLERANCE = 0.00001;
// The chroma that 100% stands for in oklch() and oklab().
static const double PERCENT_CHROMA = 0.4;
static const double PI = 3.14159265358979323846;

typedef enum Unit {
    UNIT_NUMBER,
    UNIT_PERCENT,
    UNIT_DEGREES,
} Unit;

typedef struct Argument {
    double value;
    Unit unit;
} Argument;

typedef struct Arguments {
    Argument channels[MAX_CHANNELS];
    size_t count;
    bool has_alpha;
    Argument alpha;
} Arguments;

typedef struct Vec3 {
    double x;
    double y;
    double z;
} Vec3;

static Vec3 mat3_apply(const double m[3][3], Vec3 v)
{
    return (Vec3){
        m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
        m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z,
    };
}

// Björn Ottosson's matrices between linear sRGB, LMS and OKLab.
static const double LINEAR_SRGB_TO_LMS[3][3] = {
    { 0.4122214708, 0.5363325363, 0.0514459929 },
    { 0.2119034982, 0.6806995451, 0.1073969566 },
    { 0.0883024619, 0.2817188376, 0.6299787005 },
};
static const double LMS_CBRT_TO_OKLAB[3][3] = {
    { 0.2104542553, 0.7936177850, -0.0040720468 },
    { 1.9779984951, -2.4285922050, 0.4505937099 },
    { 0.0259040371, 0.7827717662, -0.8086757660 },
};
static const double OKLAB_TO_LMS_CBRT[3][3] = {
    { 1.0, 0.3963377774, 0.2158037573 },
    { 1.0, -0.1055613458, -0.0638541728 },
    { 1.0, -0.0894841775, -1.2914855480 },
};
static const double LMS_TO_LINEAR_SRGB[3][3] = {
    { 4.0767416621, -3.3077115913, 0.2309699292 },
    { -1.2684380046, 2.6097574011, -0.3413193965 },
    { -0.0041960863, -0.7034186147, 1.7076147010 },
};
static const double LINEAR_SRGB_TO_LINEAR_P3[3][3] = {
    { 0.8224621209, 0.1775378791, 0.0 },
    { 0.0331941989, 0.9668058011, 0.0 },
    { 0.0170826307, 0.0723974407, 0.9105199286 },
};
static const double LINEAR_P3_TO_LINEAR_SRGB[3][3] = {
    { 1.2249401763, -0.2249401763, 0.0 },
    { -0.0420569547, 1.0420569547, 0.0 },
    { -0.0196375546, -0.0786360456, 1.0982736002 },
};

static Vec3 linear_srgb_to_oklab(Vec3 rgb)
{
    Vec3 lms = mat3_apply(LINEAR_SRGB_TO_LMS, rgb);
    return mat3_apply(LMS_CBRT_TO_OKLAB, (Vec3){ cbrt(lms.x), cbrt(lms.y), cbrt(lms.z) });
}

static Vec3 oklab_to_linear_srgb(Vec3 lab)
{
    Vec3 lms = mat3_apply(OKLAB_TO_LMS_CBRT, lab);
    return mat3_apply(LMS_TO_LINEAR_SRGB, (Vec3){ lms.x * lms.x * lms.x, lms.y * lms.y * lms.y, lms.z * lms.z * lms.z });
}

static Vec3 oklab_to_linear_gamut(Vec3 lab, Gamut gamut)
{
    Vec3 srgb = oklab_to_linear_srgb(lab);
    return gamut == GAMUT_DISPLAY_P3 ? mat3_apply(LINEAR_SRGB_TO_LINEAR_P3, srgb) : srgb;
}

static Vec3 linear_gamut_to_oklab(Vec3 rgb, Gamut gamut)
{
    Vec3 srgb = gamut == GAMUT_DISPLAY_P3 ? mat3_apply(LINEAR_P3_TO_LINEAR_SRGB, rgb) : rgb;
    return linear_srgb_to_oklab(srgb);
}

static double srgb_decode(double channel)
{
    double magnitude = fabs(channel);
    double linear = magnitude <= 0.04045 ? magnitude / 12.92 : pow((magnitude + 0.055) / 1.055, 2.4);
    return copysign(linear, channel);
}

static double srgb_encode(double channel)
{
    double magnitude = fabs(channel);
    double encoded = magnitude <= 0.0031308 ? magnitude * 12.92 : 1.055 * pow(magnitude, 1.0 / 2.4) - 0.055;
    return copysign(encoded, channel);
}

static double clamp_unit(double value)
{
    return value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
}

static Vec3 clip(Vec3 rgb)
{
    return (Vec3){ clamp_unit(rgb.x), clamp_unit(rgb.y), clamp_unit(rgb.z) };
}

static bool in_unit_range(double value)
{
    return value >= -GAMUT_TOLERANCE && value <= 1.0 + GAMUT_TOLERANCE;
}

static bool in_gamut(Vec3 rgb)
{
    return in_unit_range(rgb.x) && in_unit_range(rgb.y) && in_unit_range(rgb.z);
}

static double delta_e_ok(Vec3 a, Vec3 b)
{
    return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// gamut_map returns linear channels in gamut, following CSS Color 4 section 13.2.
static Vec3 gamut_map(Vec3 lab, Gamut gamut)
{
    if (lab.x >= 1.0) {
        return (Vec3){ 1.0, 1.0, 1.0 };
    }
    if (lab.x <= 0.0) {
        return (Vec3){ 0.0, 0.0, 0.0 };
    }
    Vec3 origin = oklab_to_linear_gamut(lab, gamut);
    if (in_gamut(origin)) {
        return clip(origin);
    }
    double chroma = hypot(lab.y, lab.z);
    double hue_a = lab.y / chroma;
    double hue_b = lab.z / chroma;
    Vec3 clipped = clip(origin);
    if (delta_e_ok(linear_gamut_to_oklab(clipped, gamut), lab) < JND) {
        return clipped;
    }
    double low = 0.0;
    double high = chroma;
    bool low_in_gamut = true;
    while (high - low > EPSILON) {
        double middle = (low + high) / 2.0;
        Vec3 current = { lab.x, middle * hue_a, middle * hue_b };
        Vec3 rgb = oklab_to_linear_gamut(current, gamut);
        if (low_in_gamut && in_gamut(rgb)) {
            low = middle;
            continue;
        }
        clipped = clip(rgb);
        double error = delta_e_ok(linear_gamut_to_oklab(clipped, gamut), current);
        if (error >= JND) {
            high = middle;
            continue;
        }
        if (JND - error < EPSILON) {
            return clipped;
        }
        low_in_gamut = false;
        low = middle;
    }
    return clipped;
}

Rgb color_to_rgb(Color color, Gamut gamut)
{
    Vec3 linear = gamut_map((Vec3){ color.lightness, color.a, color.b }, gamut);
    return (Rgb){
        .red = clamp_unit(srgb_encode(linear.x)),
        .green = clamp_unit(srgb_encode(linear.y)),
        .blue = clamp_unit(srgb_encode(linear.z)),
        .alpha = clamp_unit(color.alpha),
    };
}

uint8_t rgb_byte(double channel)
{
    return (uint8_t)lround(clamp_unit(channel) * 255.0);
}

uint32_t rgb_argb(Rgb rgb)
{
    return (uint32_t)rgb_byte(rgb.alpha) << 24 | (uint32_t)rgb_byte(rgb.red) << 16 | (uint32_t)rgb_byte(rgb.green) << 8 |
           (uint32_t)rgb_byte(rgb.blue);
}

static Color color_from_srgb(double red, double green, double blue, double alpha)
{
    Vec3 lab = linear_srgb_to_oklab((Vec3){ srgb_decode(red), srgb_decode(green), srgb_decode(blue) });
    return (Color){ lab.x, lab.y, lab.z, clamp_unit(alpha) };
}

static bool is_separator(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == '/';
}

static bool parse_argument(String text, Argument *argument)
{
    if (str_equal_ignore_case(text, S("none"))) {
        *argument = (Argument){ 0.0, UNIT_NUMBER };
        return true;
    }
    Unit unit = UNIT_NUMBER;
    if (str_ends_with(text, S("%"))) {
        unit = UNIT_PERCENT;
        text = str_trim_suffix(text, S("%"));
    } else if (str_ends_with(text, S("deg"))) {
        unit = UNIT_DEGREES;
        text = str_trim_suffix(text, S("deg"));
    }
    double value;
    if (text.len == 0 || !float_parse(text, &value) || !isfinite(value)) {
        return false;
    }
    *argument = (Argument){ value, unit };
    return true;
}

// parse_arguments reads three channels and an optional alpha, after a slash
// in modern syntax or as a fourth comma-separated value in legacy syntax.
static bool parse_arguments(String text, Arguments *arguments)
{
    *arguments = (Arguments){ 0 };
    bool after_slash = false;
    bool commas = false;
    size_t i = 0;
    while (i < text.len) {
        char c = text.data[i];
        if (c == '/') {
            if (after_slash) {
                return false;
            }
            after_slash = true;
            i++;
            continue;
        }
        if (is_separator(c)) {
            commas = commas || c == ',';
            i++;
            continue;
        }
        size_t start = i;
        while (i < text.len && !is_separator(text.data[i])) {
            i++;
        }
        Argument argument;
        if (!parse_argument(str_slice(text, start, i), &argument)) {
            return false;
        }
        if (after_slash) {
            if (arguments->has_alpha) {
                return false;
            }
            arguments->alpha = argument;
            arguments->has_alpha = true;
        } else {
            if (arguments->count == MAX_CHANNELS) {
                return false;
            }
            arguments->channels[arguments->count++] = argument;
        }
    }
    if (commas && arguments->count == 4 && !arguments->has_alpha) {
        arguments->alpha = arguments->channels[3];
        arguments->has_alpha = true;
        arguments->count = 3;
    }
    return arguments->count == 3;
}

static double alpha_of(const Arguments *arguments)
{
    if (!arguments->has_alpha) {
        return 1.0;
    }
    double alpha = arguments->alpha.value;
    return clamp_unit(arguments->alpha.unit == UNIT_PERCENT ? alpha / 100.0 : alpha);
}

static double fraction(Argument argument, double percent_scale)
{
    return argument.unit == UNIT_PERCENT ? argument.value / 100.0 * percent_scale : argument.value;
}

static Color oklch_color(const Arguments *arguments)
{
    double lightness = fraction(arguments->channels[0], 1.0);
    double chroma = fmax(fraction(arguments->channels[1], PERCENT_CHROMA), 0.0);
    double hue = arguments->channels[2].value * PI / 180.0;
    return (Color){ lightness, chroma * cos(hue), chroma * sin(hue), alpha_of(arguments) };
}

static Color oklab_color(const Arguments *arguments)
{
    return (Color){
        fraction(arguments->channels[0], 1.0),
        fraction(arguments->channels[1], PERCENT_CHROMA),
        fraction(arguments->channels[2], PERCENT_CHROMA),
        alpha_of(arguments),
    };
}

static double hsl_channel(double n, double hue, double saturation, double lightness)
{
    double k = fmod(n + hue / 30.0, 12.0);
    double a = saturation * fmin(lightness, 1.0 - lightness);
    return lightness - a * fmax(-1.0, fmin(fmin(k - 3.0, 9.0 - k), 1.0));
}

// hsl_color reads saturation and lightness as percentages, with or without the % sign.
static Color hsl_color(const Arguments *arguments)
{
    double hue = fmod(arguments->channels[0].value, 360.0);
    hue = hue < 0.0 ? hue + 360.0 : hue;
    double saturation = clamp_unit(arguments->channels[1].value / 100.0);
    double lightness = clamp_unit(arguments->channels[2].value / 100.0);
    return color_from_srgb(hsl_channel(0.0, hue, saturation, lightness), hsl_channel(8.0, hue, saturation, lightness),
                           hsl_channel(4.0, hue, saturation, lightness), alpha_of(arguments));
}

static double rgb_channel(Argument argument)
{
    return clamp_unit(argument.unit == UNIT_PERCENT ? argument.value / 100.0 : argument.value / 255.0);
}

static Color rgb_color(const Arguments *arguments)
{
    return color_from_srgb(rgb_channel(arguments->channels[0]), rgb_channel(arguments->channels[1]),
                           rgb_channel(arguments->channels[2]), alpha_of(arguments));
}

static bool hex_digit(char c, unsigned *value)
{
    if (c >= '0' && c <= '9') {
        *value = (unsigned)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
        *value = (unsigned)(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
        *value = (unsigned)(c - 'A' + 10);
    } else {
        return false;
    }
    return true;
}

// parse_hex reads #rgb, #rgba, #rrggbb and #rrggbbaa.
static bool parse_hex(String digits, Color *color)
{
    size_t per_channel = digits.len == 3 || digits.len == 4 ? 1 : digits.len == 6 || digits.len == 8 ? 2 : 0;
    if (per_channel == 0) {
        return false;
    }
    double channels[4] = { 0.0, 0.0, 0.0, 1.0 };
    size_t channel_count = digits.len / per_channel;
    for (size_t channel = 0; channel < channel_count; channel++) {
        unsigned value = 0;
        for (size_t i = 0; i < per_channel; i++) {
            unsigned digit;
            if (!hex_digit(digits.data[channel * per_channel + i], &digit)) {
                return false;
            }
            value = value * 16 + digit;
        }
        channels[channel] = per_channel == 1 ? (double)(value * 17) / 255.0 : (double)value / 255.0;
    }
    *color = color_from_srgb(channels[0], channels[1], channels[2], channels[3]);
    return true;
}

static bool parse_function(String name, String body, Color *color)
{
    Arguments arguments;
    if (!parse_arguments(body, &arguments)) {
        return false;
    }
    if (str_equal_ignore_case(name, S("oklch"))) {
        *color = oklch_color(&arguments);
    } else if (str_equal_ignore_case(name, S("oklab"))) {
        *color = oklab_color(&arguments);
    } else if (str_equal_ignore_case(name, S("hsl")) || str_equal_ignore_case(name, S("hsla"))) {
        *color = hsl_color(&arguments);
    } else if (str_equal_ignore_case(name, S("rgb")) || str_equal_ignore_case(name, S("rgba"))) {
        *color = rgb_color(&arguments);
    } else {
        return false;
    }
    return true;
}

// parse_bare_hsl reads the "H S% L%" channels Tailwind 3 themes store without hsl().
static bool parse_bare_hsl(String text, Color *color)
{
    Arguments arguments;
    if (!parse_arguments(text, &arguments) || arguments.channels[1].unit != UNIT_PERCENT ||
        arguments.channels[2].unit != UNIT_PERCENT) {
        return false;
    }
    *color = hsl_color(&arguments);
    return true;
}

bool color_parse(String text, Color *color)
{
    text = str_trim(text);
    if (str_starts_with(text, S("#"))) {
        return parse_hex(str_slice(text, 1, text.len), color);
    }
    if (str_equal_ignore_case(text, S("black"))) {
        *color = (Color){ 0.0, 0.0, 0.0, 1.0 };
        return true;
    }
    if (str_equal_ignore_case(text, S("white"))) {
        *color = (Color){ 1.0, 0.0, 0.0, 1.0 };
        return true;
    }
    if (str_equal_ignore_case(text, S("transparent"))) {
        *color = (Color){ 0.0, 0.0, 0.0, 0.0 };
        return true;
    }
    size_t open;
    if (str_find_char(text, '(', &open)) {
        if (!str_ends_with(text, S(")"))) {
            return false;
        }
        return parse_function(str_trim(str_slice(text, 0, open)), str_slice(text, open + 1, text.len - 1), color);
    }
    return parse_bare_hsl(text, color);
}
