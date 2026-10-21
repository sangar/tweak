#include <math.h>
#include <stdio.h>

#include "emit/emit.h"
#include "emit/naming.h"
#include "source/source.h"
#include "theme/color.h"
#include "theme/theme.h"

// Test is the harness's tally, passed to every test.
typedef struct Test {
    int checks;
    int failures;
    Arena *arena;
} Test;

static void check(Test *t, bool ok, const char *where, int line, const char *what)
{
    t->checks++;
    if (ok) {
        return;
    }
    t->failures++;
    fprintf(stderr, "%s:%d: check failed: %s\n", where, line, what);
}

static void check_str(Test *t, String got, const char *want, const char *where, int line)
{
    t->checks++;
    if (str_equal(got, S(want))) {
        return;
    }
    t->failures++;
    fprintf(stderr, "%s:%d: got \"%.*s\", want \"%s\"\n", where, line, (int)got.len, got.data, want);
}

static void check_contains(Test *t, String text, const char *want, const char *where, int line)
{
    t->checks++;
    if (str_contains(text, S(want))) {
        return;
    }
    t->failures++;
    fprintf(stderr, "%s:%d: output lacks \"%s\"\n", where, line, want);
}

// The CHECK macros record the failing function and line, which only a macro can see.
#define CHECK(t, cond) check((t), (cond), __func__, __LINE__, #cond) // modern-c: allow function-macro
#define CHECK_STR(t, got, want) check_str((t), (got), (want), __func__, __LINE__) // modern-c: allow function-macro
#define CHECK_CONTAINS(t, text, want) check_contains((t), (text), (want), __func__, __LINE__) // modern-c: allow function-macro

// MODERN_MINIMAL is a cut-down registry item as tweakcn serves it at /r/themes/modern-minimal.
static const char MODERN_MINIMAL[] = "{\n"
                                     "  \"name\": \"modern-minimal\",\n"
                                     "  \"type\": \"registry:style\",\n"
                                     "  \"cssVars\": {\n"
                                     "    \"theme\": {\n"
                                     "      \"font-sans\": \"\\\"Inter\\\", sans-serif\",\n"
                                     "      \"font-mono\": \"ui-monospace, monospace\",\n"
                                     "      \"radius\": \"0.5rem\"\n"
                                     "    },\n"
                                     "    \"light\": {\n"
                                     "      \"background\": \"oklch(1.0000 0 0)\",\n"
                                     "      \"primary\": \"oklch(0.6231 0.1880 259.8145)\",\n"
                                     "      \"primary-foreground\": \"oklch(1.0000 0 0)\",\n"
                                     "      \"card-foreground\": \"oklch(0.3211 0 0)\",\n"
                                     "      \"chart-1\": \"oklch(0.6368 0.2078 25.3313)\",\n"
                                     "      \"shadow-sm\": \"0 1px 3px 0px hsl(0 0% 0% / 0.10)\"\n"
                                     "    },\n"
                                     "    \"dark\": {\n"
                                     "      \"background\": \"oklch(0.2046 0 0)\",\n"
                                     "      \"card-foreground\": \"oklch(0.9219 0 0)\"\n"
                                     "    }\n"
                                     "  }\n"
                                     "}\n";

static String hex_of(Arena *arena, Color color)
{
    Rgb rgb = color_to_rgb(color, GAMUT_SRGB);
    String hex = str_format(arena, "%02x%02x%02x", rgb_byte(rgb.red), rgb_byte(rgb.green), rgb_byte(rgb.blue));
    return rgb_byte(rgb.alpha) == 255 ? hex : str_format(arena, "%s%02x", hex.data, rgb_byte(rgb.alpha));
}

static Theme parse_theme(Test *t, const char *text, const char *name)
{
    Theme theme;
    Err err = { 0 };
    Error error = theme_parse(t->arena, S(text), S(name), &theme, &err);
    if (error != ERR_OK) {
        fprintf(stderr, "theme_parse: %s\n", err.msg);
    }
    CHECK(t, error == ERR_OK);
    return theme;
}

static Output emit_one(Test *t, Format format, const Theme *theme, EmitOptions options)
{
    Output output = { 0 };
    emit(t->arena, format, theme, &options, &output);
    return output;
}

static void test_color_parse(Test *t)
{
    static const struct {
        const char *css;
        const char *hex;
    } cases[] = {
        { "oklch(0.6231 0.1880 259.8145)", "3b82f6" },
        { "oklch(62.31% 47% 259.8145deg)", "3b82f6" },
        { "oklch(1 0 none)", "ffffff" },
        { "oklab(0.6231 -0.0332 -0.1850)", "3b82f6" },
        { "hsl(0 0% 0% / 0.05)", "0000000d" },
        { "hsl(210, 40%, 98%)", "f8fafc" },
        { "hsla(210, 40%, 98%, 0.5)", "f8fafc80" },
        { "210 40% 98%", "f8fafc" },
        { "rgb(59 130 246 / 50%)", "3b82f680" },
        { "rgba(59, 130, 246, 0.5)", "3b82f680" },
        { "#abc", "aabbcc" },
        { "#3B82F680", "3b82f680" },
        { "transparent", "00000000" },
    };
    for (size_t i = 0; i < countof(cases); i++) {
        Color color;
        bool parsed = color_parse(S(cases[i].css), &color);
        CHECK(t, parsed);
        if (parsed) {
            CHECK_STR(t, hex_of(t->arena, color), cases[i].hex);
        }
    }
    static const char *const rejected[] = {
        "var(--primary)", "0.375rem", "0", "0.1", "calc(1px + 2px)", "#12345", "oklch(1 0)", "Inter, sans-serif",
        "0 1px 3px 0px hsl(0 0% 0% / 0.05)",
    };
    for (size_t i = 0; i < countof(rejected); i++) {
        Color color;
        CHECK(t, !color_parse(S(rejected[i]), &color));
    }
}

static void test_gamut_mapping(Test *t)
{
    Color red;
    CHECK(t, color_parse(S("#ff0000"), &red));
    Rgb p3 = color_to_rgb(red, GAMUT_DISPLAY_P3);
    CHECK(t, fabs(p3.red - 0.9175) < 0.001 && fabs(p3.green - 0.2003) < 0.001 && fabs(p3.blue - 0.1386) < 0.001);

    // A green beyond both gamuts keeps its hue: it maps to a green, not to whatever clipping leaves.
    Color green;
    CHECK(t, color_parse(S("oklch(0.75 0.4 145)"), &green));
    Rgb srgb = color_to_rgb(green, GAMUT_SRGB);
    Rgb wide = color_to_rgb(green, GAMUT_DISPLAY_P3);
    CHECK(t, srgb.green > srgb.red && srgb.green > srgb.blue);
    CHECK(t, wide.green > wide.red && wide.green > wide.blue);
}

static void test_theme_json(Test *t)
{
    Theme theme = parse_theme(t, MODERN_MINIMAL, "fallback");
    CHECK_STR(t, theme.name, "modern-minimal");
    CHECK(t, theme.color_count == 5);
    CHECK(t, theme_color(&theme, S("shadow-sm")) == nullptr);
    const ThemeColor *background = theme_color(&theme, S("background"));
    CHECK(t, background != nullptr && str_equal(hex_of(t->arena, background->dark), S("171717")));
    const ThemeColor *primary = theme_color(&theme, S("primary"));
    CHECK(t, primary != nullptr && str_equal(hex_of(t->arena, primary->dark), S("3b82f6")));
    CHECK(t, theme.has_radius && theme.radius == 8.0);
    ThemeRadii radii = theme_radii(&theme);
    CHECK(t, radii.sm == 4.0 && radii.md == 6.0 && radii.lg == 8.0 && radii.xl == 12.0);
    CHECK_STR(t, theme.font_sans, "Inter");
    CHECK_STR(t, theme.font_mono, "");
}

static void test_theme_css(Test *t)
{
    Theme theme = parse_theme(t,
                              "@import \"tailwindcss\";\n"
                              "@custom-variant dark (&:is(.dark *));\n"
                              "@layer base {\n"
                              "  :root {\n"
                              "    --background: 0 0% 100%; /* {braces} in a comment */\n"
                              "    --primary: oklch(0.6231 0.1880 259.8145);\n"
                              "    --ring: var(--primary);\n"
                              "    --radius: 6px;\n"
                              "    --font-serif: Georgia, serif;\n"
                              "  }\n"
                              "  .dark {\n"
                              "    --background: oklch(0.2046 0 0);\n"
                              "    --primary: #00c248;\n"
                              "  }\n"
                              "}\n"
                              "@theme inline {\n"
                              "  --color-background: var(--background);\n"
                              "}\n",
                              "globals");
    CHECK_STR(t, theme.name, "globals");
    CHECK(t, theme.color_count == 3);
    CHECK(t, theme_color(&theme, S("color-background")) == nullptr);
    const ThemeColor *ring = theme_color(&theme, S("ring"));
    CHECK(t, ring != nullptr && str_equal(hex_of(t->arena, ring->light), S("3b82f6")) &&
                 str_equal(hex_of(t->arena, ring->dark), S("00c248")));
    CHECK(t, theme.has_radius && theme.radius == 6.0);
    CHECK_STR(t, theme.font_serif, "Georgia");
}

static void test_theme_rejects(Test *t)
{
    Theme theme;
    CHECK(t, theme_parse(t->arena, S("{\"name\": \"x\"}"), S("x"), &theme, nullptr) == ERR_PARSE);
    CHECK(t, theme_parse(t->arena, S("{\"cssVars\": "), S("x"), &theme, nullptr) == ERR_PARSE);
    CHECK(t, theme_parse(t->arena, S(":root { --radius: 1rem; }"), S("x"), &theme, nullptr) == ERR_PARSE);
}

static void test_naming(Test *t)
{
    Arena *a = t->arena;
    CHECK_STR(t, naming_pascal(a, S("modern-minimal")), "ModernMinimal");
    CHECK_STR(t, naming_pascal(a, S("Soft Pop")), "SoftPop");
    CHECK_STR(t, naming_pascal(a, S("2077")), "Tweak2077");
    CHECK_STR(t, naming_pascal(a, S("--")), "Tweak");
    CHECK_STR(t, naming_camel(a, S("sidebar-primary-foreground")), "sidebarPrimaryForeground");
    CHECK_STR(t, naming_camel(a, S("chart-1")), "chart1");
    CHECK_STR(t, naming_snake(a, S("SoftPop 2")), "soft_pop_2");
    CHECK_STR(t, naming_snake(a, S("3d")), "theme_3d");
    CHECK_STR(t, naming_type(a, S("ocean")), "OceanTheme");
    CHECK_STR(t, naming_type(a, S("ocean-theme")), "OceanTheme");
    CHECK_STR(t, emit_length(a, 10.0), "10");
    CHECK_STR(t, emit_length(a, 1.6), "1.6");
    CHECK_STR(t, emit_length(a, 0.0), "0");
}

static void test_source_url(Test *t)
{
    static const struct {
        const char *input;
        const char *url;
    } cases[] = {
        { "https://tweakcn.com/themes/cmlhfpjhw000004l4f8ax3m7k", "https://tweakcn.com/r/themes/cmlhfpjhw000004l4f8ax3m7k" },
        { "https://tweakcn.com/editor/theme?theme=catppuccin&mode=dark", "https://tweakcn.com/r/themes/catppuccin" },
        { "modern-minimal", "https://tweakcn.com/r/themes/modern-minimal" },
        { "https://tweakcn.com/r/themes/claude.json", "https://tweakcn.com/r/themes/claude.json" },
        { "https://example.com/theme.css", "https://example.com/theme.css" },
    };
    for (size_t i = 0; i < countof(cases); i++) {
        String url = { 0 };
        CHECK(t, source_url(t->arena, S(cases[i].input), &url));
        CHECK_STR(t, url, cases[i].url);
    }
    String url;
    CHECK(t, !source_url(t->arena, S("themes/missing.css"), &url));
}

static void test_emit_apple(Test *t)
{
    Theme theme = parse_theme(t, MODERN_MINIMAL, "x");
    Output swift = emit_one(t, FORMAT_SWIFT, &theme, (EmitOptions){ .gamut = GAMUT_SRGB });
    CHECK(t, swift.count == 1);
    CHECK_STR(t, swift.items[0].path, "ModernMinimalTheme.swift");
    String code = swift.items[0].contents;
    CHECK_CONTAINS(t, code, "public enum ModernMinimalTheme {");
    CHECK_CONTAINS(t, code, "public static let cardForeground = ModernMinimalTheme.color(light: (0.2000, 0.2000, "
                            "0.2000, 1.0000), dark: (0.8980, 0.8980, 0.8980, 1.0000))");
    CHECK_CONTAINS(t, code, "public static let lg: CGFloat = 8\n");
    CHECK_CONTAINS(t, code, "public static let sans: String? = \"Inter\"");
    CHECK_CONTAINS(t, code, "public static let mono: String? = nil");
    CHECK_CONTAINS(t, code, "NSColor(srgbRed: c.red");
    Output p3 = emit_one(t, FORMAT_SWIFT, &theme, (EmitOptions){ .gamut = GAMUT_DISPLAY_P3 });
    CHECK_CONTAINS(t, p3.items[0].contents, "NSColor(displayP3Red: c.red");

    Output catalog = emit_one(t, FORMAT_XCASSETS, &theme, (EmitOptions){ .gamut = GAMUT_DISPLAY_P3 });
    CHECK(t, catalog.count == theme.color_count + 1);
    CHECK_STR(t, catalog.items[0].path, "ModernMinimalTheme.xcassets/Contents.json");
    CHECK_STR(t, catalog.items[4].path, "ModernMinimalTheme.xcassets/cardForeground.colorset/Contents.json");
    CHECK_CONTAINS(t, catalog.items[4].contents, "\"color-space\" : \"display-p3\"");
    CHECK_CONTAINS(t, catalog.items[4].contents, "\"value\" : \"dark\"");
}

static void test_emit_android(Test *t)
{
    Theme theme = parse_theme(t, MODERN_MINIMAL, "x");
    Output compose =
        emit_one(t, FORMAT_COMPOSE, &theme, (EmitOptions){ .gamut = GAMUT_SRGB, .package = S("com.example.theme") });
    CHECK_STR(t, compose.items[0].path, "ModernMinimalTheme.kt");
    String code = compose.items[0].contents;
    CHECK_CONTAINS(t, code, "package com.example.theme\n");
    CHECK_CONTAINS(t, code, "data class ModernMinimalColors(");
    CHECK_CONTAINS(t, code, "val ModernMinimalLightColors = ModernMinimalColors(\n    background = Color(0xFFFFFFFF),");
    CHECK_CONTAINS(t, code, "    primary = Color(0xFF3B82F6),");
    CHECK_CONTAINS(t, code, "        onPrimary = primaryForeground,");
    CHECK(t, !str_contains(code, S("error = destructive")));
    CHECK_CONTAINS(t, code, "val radiusLg = 8.dp");
    CHECK_CONTAINS(t, code, "fun ModernMinimalTheme(darkTheme: Boolean = isSystemInDarkTheme()");

    Output resources = emit_one(t, FORMAT_ANDROID, &theme, (EmitOptions){ .gamut = GAMUT_DISPLAY_P3 });
    CHECK(t, resources.count == 2);
    CHECK_STR(t, resources.items[0].path, "values/modern_minimal_theme.xml");
    CHECK_STR(t, resources.items[1].path, "values-night/modern_minimal_theme.xml");
    CHECK_CONTAINS(t, resources.items[0].contents, "<color name=\"card_foreground\">#FF333333</color>");
    CHECK_CONTAINS(t, resources.items[1].contents, "<color name=\"card_foreground\">#FFE5E5E5</color>");
    CHECK_CONTAINS(t, resources.items[0].contents, "<dimen name=\"radius_lg\">8dp</dimen>");
    CHECK(t, !str_contains(resources.items[1].contents, S("dimen")));
}

static void test_emit_flutter_and_json(Test *t)
{
    Theme theme = parse_theme(t, MODERN_MINIMAL, "x");
    Output dart = emit_one(t, FORMAT_FLUTTER, &theme, (EmitOptions){ .gamut = GAMUT_SRGB });
    CHECK_STR(t, dart.items[0].path, "modern_minimal_theme.dart");
    String code = dart.items[0].contents;
    CHECK_CONTAINS(t, code, "class ModernMinimalColors extends ThemeExtension<ModernMinimalColors> {");
    CHECK_CONTAINS(t, code, "    primary: Color(0xFF3B82F6),");
    CHECK_CONTAINS(t, code, "        onPrimary: c.primaryForeground,");
    CHECK(t, !str_contains(code, S("onBackground")));
    CHECK_CONTAINS(t, code, "static const String fontSans = \"Inter\";");
    CHECK_CONTAINS(t, code, "static const String? fontMono = null;");
    CHECK(t, !str_contains(code, S("dart:ui")));
    Output wide = emit_one(t, FORMAT_FLUTTER, &theme, (EmitOptions){ .gamut = GAMUT_DISPLAY_P3 });
    CHECK_CONTAINS(t, wide.items[0].contents, "import 'dart:ui' show ColorSpace;");
    CHECK_CONTAINS(t, wide.items[0].contents, "colorSpace: ColorSpace.displayP3)");

    Output json = emit_one(t, FORMAT_JSON, &theme, (EmitOptions){ .gamut = GAMUT_DISPLAY_P3 });
    CHECK_STR(t, json.items[0].path, "modern_minimal.json");
    CHECK_CONTAINS(t, json.items[0].contents, "\"primary\": \"#3b82f6\"");
    CHECK_CONTAINS(t, json.items[0].contents, "\"xl\": 12\n");
    CHECK_CONTAINS(t, json.items[0].contents, "\"mono\": null");
}

int main(void)
{
    Test t = { .arena = arena_create(0) };
    test_color_parse(&t);
    test_gamut_mapping(&t);
    test_theme_json(&t);
    test_theme_css(&t);
    test_theme_rejects(&t);
    test_naming(&t);
    test_source_url(&t);
    test_emit_apple(&t);
    test_emit_android(&t);
    test_emit_flutter_and_json(&t);
    printf("%d checks, %d failures\n", t.checks, t.failures);
    arena_destroy(t.arena);
    return t.failures != 0 ? 1 : 0;
}
