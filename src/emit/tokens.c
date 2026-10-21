#include "emit/tokens.h"

#include "emit/naming.h"
#include "mc/encoding/json.h"

// append_hex writes #rrggbb, or #rrggbbaa when the color is translucent, as CSS does.
static void append_hex(StringBuilder *out, Color color)
{
    Rgb rgb = color_to_rgb(color, GAMUT_SRGB);
    str_builder_append_format(out, "\"#%02x%02x%02x", rgb_byte(rgb.red), rgb_byte(rgb.green), rgb_byte(rgb.blue));
    if (rgb_byte(rgb.alpha) != 255) {
        str_builder_append_format(out, "%02x", rgb_byte(rgb.alpha));
    }
    str_builder_append_char(out, '"');
}

static void append_colors(StringBuilder *out, const Theme *theme, bool dark)
{
    str_builder_append_format(out, "    \"%s\": {\n", dark ? "dark" : "light");
    for (size_t i = 0; i < theme->color_count; i++) {
        const ThemeColor *color = &theme->colors[i];
        str_builder_append(out, S("      "));
        json_append_quoted(out, color->name);
        str_builder_append(out, S(": "));
        append_hex(out, dark ? color->dark : color->light);
        str_builder_append(out, i + 1 < theme->color_count ? S(",\n") : S("\n"));
    }
    str_builder_append_format(out, "    }%s\n", dark ? "" : ",");
}

static void append_font(StringBuilder *out, const char *name, String family, bool last)
{
    str_builder_append_format(out, "    \"%s\": ", name);
    if (family.len > 0) {
        json_append_quoted(out, family);
    } else {
        str_builder_append(out, S("null"));
    }
    str_builder_append(out, last ? S("\n") : S(",\n"));
}

static void append_radius(Arena *arena, StringBuilder *out, const char *name, double value, bool last)
{
    String number = emit_length(arena, value);
    str_builder_append_format(out, "    \"%s\": %.*s%s\n", name, (int)number.len, number.data, last ? "" : ",");
}

void tokens_json(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output)
{
    StringBuilder out = str_builder_create(arena, 8192);
    str_builder_append(&out, S("{\n  \"name\": "));
    json_append_quoted(&out, theme->name);
    str_builder_append(&out, S(",\n  \"colors\": {\n"));
    append_colors(&out, theme, false);
    append_colors(&out, theme, true);
    str_builder_append(&out, S("  },\n"));
    if (theme->has_radius) {
        ThemeRadii radii = theme_radii(theme);
        str_builder_append(&out, S("  \"radius\": {\n"));
        append_radius(arena, &out, "sm", radii.sm, false);
        append_radius(arena, &out, "md", radii.md, false);
        append_radius(arena, &out, "lg", radii.lg, false);
        append_radius(arena, &out, "xl", radii.xl, true);
        str_builder_append(&out, S("  },\n"));
    }
    str_builder_append(&out, S("  \"fonts\": {\n"));
    append_font(&out, "sans", theme->font_sans, false);
    append_font(&out, "serif", theme->font_serif, false);
    append_font(&out, "mono", theme->font_mono, true);
    str_builder_append(&out, S("  }\n}\n"));
    output_add(arena, output, str_concat(arena, naming_snake(arena, theme->name), S(".json")), str_builder_finish(&out));
}
