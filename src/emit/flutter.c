#include "emit/flutter.h"

#include "emit/material.h"
#include "emit/naming.h"
#include "mc/encoding/json.h"

typedef struct Field {
    String name;
    const ThemeColor *color;
} Field;

static void append_dart_color(StringBuilder *out, Color color, Gamut gamut)
{
    Rgb rgb = color_to_rgb(color, gamut);
    if (gamut == GAMUT_DISPLAY_P3) {
        str_builder_append_format(out,
                                  "Color.from(alpha: %.4f, red: %.4f, green: %.4f, blue: %.4f, colorSpace: "
                                  "ColorSpace.displayP3)",
                                  rgb.alpha, rgb.red, rgb.green, rgb.blue);
    } else {
        str_builder_append_format(out, "Color(0x%08X)", rgb_argb(rgb));
    }
}

static void append_palette(StringBuilder *out, const Field *fields, size_t count, String colors_type, bool dark, Gamut gamut)
{
    str_builder_append_format(out, "  static const %s = %.*s(\n", dark ? "dark" : "light", (int)colors_type.len,
                              colors_type.data);
    for (size_t i = 0; i < count; i++) {
        str_builder_append_format(out, "    %.*s: ", (int)fields[i].name.len, fields[i].name.data);
        append_dart_color(out, dark ? fields[i].color->dark : fields[i].color->light, gamut);
        str_builder_append(out, S(",\n"));
    }
    str_builder_append(out, S("  );\n\n"));
}

static void append_number(Arena *arena, StringBuilder *out, const char *name, double value)
{
    String number = emit_length(arena, value);
    str_builder_append_format(out, "  static const double %s = %.*s;\n", name, (int)number.len, number.data);
}

static void append_font(Arena *arena, StringBuilder *out, const char *name, String family)
{
    if (family.len == 0) {
        str_builder_append_format(out, "  static const String? %s = null;\n", name);
        return;
    }
    String literal = json_quote(arena, family);
    str_builder_append_format(out, "  static const String %s = %.*s;\n", name, (int)literal.len, literal.data);
}

static void append_extension(StringBuilder *out, const Field *fields, size_t count, String colors_type, Gamut gamut)
{
    int type_len = (int)colors_type.len;
    const char *type = colors_type.data;
    str_builder_append_format(out,
                              "@immutable\n"
                              "class %.*s extends ThemeExtension<%.*s> {\n"
                              "  const %.*s({\n",
                              type_len, type, type_len, type, type_len, type);
    for (size_t i = 0; i < count; i++) {
        str_builder_append_format(out, "    required this.%.*s,\n", (int)fields[i].name.len, fields[i].name.data);
    }
    str_builder_append(out, S("  });\n\n"));
    for (size_t i = 0; i < count; i++) {
        str_builder_append_format(out, "  final Color %.*s;\n", (int)fields[i].name.len, fields[i].name.data);
    }
    str_builder_append(out, S("\n"));
    append_palette(out, fields, count, colors_type, false, gamut);
    append_palette(out, fields, count, colors_type, true, gamut);

    str_builder_append_format(out, "  @override\n  %.*s copyWith({\n", type_len, type);
    for (size_t i = 0; i < count; i++) {
        str_builder_append_format(out, "    Color? %.*s,\n", (int)fields[i].name.len, fields[i].name.data);
    }
    str_builder_append_format(out, "  }) {\n    return %.*s(\n", type_len, type);
    for (size_t i = 0; i < count; i++) {
        int len = (int)fields[i].name.len;
        const char *name = fields[i].name.data;
        str_builder_append_format(out, "      %.*s: %.*s ?? this.%.*s,\n", len, name, len, name, len, name);
    }
    str_builder_append(out, S("    );\n  }\n\n"));

    str_builder_append_format(out,
                              "  @override\n"
                              "  %.*s lerp(%.*s? other, double t) {\n"
                              "    if (other == null) return this;\n"
                              "    return %.*s(\n",
                              type_len, type, type_len, type, type_len, type);
    for (size_t i = 0; i < count; i++) {
        int len = (int)fields[i].name.len;
        const char *name = fields[i].name.data;
        str_builder_append_format(out, "      %.*s: Color.lerp(%.*s, other.%.*s, t)!,\n", len, name, len, name, len, name);
    }
    str_builder_append(out, S("    );\n  }\n}\n\n"));
}

static void append_theme_class(Arena *arena, StringBuilder *out, const Theme *theme, String type, String colors_type)
{
    int type_len = (int)colors_type.len;
    const char *colors = colors_type.data;
    str_builder_append_format(out, "abstract final class %.*s {\n", (int)type.len, type.data);
    if (theme->has_radius) {
        ThemeRadii radii = theme_radii(theme);
        str_builder_append(out, S("  // shadcn's radius-sm to radius-xl, lg being the theme's radius.\n"));
        append_number(arena, out, "radiusSm", radii.sm);
        append_number(arena, out, "radiusMd", radii.md);
        append_number(arena, out, "radiusLg", radii.lg);
        append_number(arena, out, "radiusXl", radii.xl);
        str_builder_append(out, S("\n"));
    }
    str_builder_append(out, S("  // Font family names; null means the platform font. Bundle them as\n"
                              "  // assets or load them with the google_fonts package.\n"));
    append_font(arena, out, "fontSans", theme->font_sans);
    append_font(arena, out, "fontSerif", theme->font_serif);
    append_font(arena, out, "fontMono", theme->font_mono);
    str_builder_append_format(out,
                              "\n"
                              "  static ThemeData light() => _build(%.*s.light, Brightness.light);\n"
                              "  static ThemeData dark() => _build(%.*s.dark, Brightness.dark);\n\n"
                              "  static ThemeData _build(%.*s c, Brightness brightness) {\n"
                              "    final base = brightness == Brightness.dark ? const ColorScheme.dark() : const "
                              "ColorScheme.light();\n"
                              "    return ThemeData(\n"
                              "      colorScheme: base.copyWith(\n",
                              type_len, colors, type_len, colors, type_len, colors);
    for (size_t i = 0; i < MATERIAL_ROLE_COUNT; i++) {
        const MaterialRole *role = &MATERIAL_ROLES[i];
        if (role->flutter == nullptr || theme_color(theme, S(role->token)) == nullptr) {
            continue;
        }
        String token = naming_camel(arena, S(role->token));
        str_builder_append_format(out, "        %s: c.%.*s,\n", role->flutter, (int)token.len, token.data);
    }
    str_builder_append(out, S("      ),\n"));
    if (theme_color(theme, S("background")) != nullptr) {
        str_builder_append(out, S("      scaffoldBackgroundColor: c.background,\n"));
    }
    str_builder_append(out, S("      fontFamily: fontSans,\n"
                              "      extensions: [c],\n"
                              "    );\n"
                              "  }\n"
                              "}\n"));
}

void flutter_dart(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output)
{
    String type = naming_type(arena, theme->name);
    String colors_type = str_concat(arena, str_trim_suffix(type, S("Theme")), S("Colors"));
    Field *fields = arena_push(arena, arena_size_mul(theme->color_count, sizeof *fields));
    for (size_t i = 0; i < theme->color_count; i++) {
        fields[i] = (Field){ naming_camel(arena, theme->colors[i].name), &theme->colors[i] };
    }
    StringBuilder out = str_builder_create(arena, 16384);
    str_builder_append_format(&out, "// Generated by tweak from the tweakcn theme \"%.*s\".\n\n", (int)theme->name.len,
                              theme->name.data);
    if (options->gamut == GAMUT_DISPLAY_P3) {
        str_builder_append(&out, S("import 'dart:ui' show ColorSpace;\n\n"));
    }
    str_builder_append(&out, S("import 'package:flutter/material.dart';\n\n"));
    append_extension(&out, fields, theme->color_count, colors_type, options->gamut);
    append_theme_class(arena, &out, theme, type, colors_type);
    String file = str_concat(arena, naming_snake(arena, type), S(".dart"));
    output_add(arena, output, file, str_builder_finish(&out));
}
