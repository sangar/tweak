#include "theme/theme.h"

#include <math.h>

#include "mc/encoding/json.h"
#include "mc/platform/platform.h"

enum { MAX_VAR_DEPTH = 16, CSS_PX_PER_REM = 16 };

static const char *const LIGHT_SELECTORS[] = { ":root", ".light", "html" };
static const char *const DARK_SELECTORS[] = { ".dark", ":root.dark", "html.dark" };
static const char *const GENERIC_FAMILIES[] = {
    "serif",     "sans-serif", "monospace",    "cursive",    "fantasy",    "system-ui",          "ui-serif",
    "ui-sans-serif", "ui-monospace", "ui-rounded", "math", "emoji", "-apple-system", "BlinkMacSystemFont",
};

typedef struct Var {
    String name;
    String value;
} Var;

// Vars are the custom properties of one appearance, in declaration order; a
// later declaration of a name replaces the earlier value, as in CSS.
typedef struct Vars {
    Var *items;
    size_t count;
    size_t capacity;
} Vars;

static void vars_put(Arena *arena, Vars *vars, String name, String value)
{
    for (size_t i = 0; i < vars->count; i++) {
        if (str_equal(vars->items[i].name, name)) {
            vars->items[i].value = value;
            return;
        }
    }
    vars->items = arena_grow(arena, vars->items, &vars->capacity, vars->count, sizeof *vars->items);
    vars->items[vars->count++] = (Var){ name, value };
}

// lookup finds name in vars, then in fallback when that is not nullptr.
static bool lookup(const Vars *vars, const Vars *fallback, String name, String *value)
{
    for (size_t i = 0; i < vars->count; i++) {
        if (str_equal(vars->items[i].name, name)) {
            *value = vars->items[i].value;
            return true;
        }
    }
    return fallback != nullptr && lookup(fallback, nullptr, name, value);
}

// resolve follows var(--name) and var(--name, default) references; a reference
// to nothing, or a chain too long to be anything but a cycle, is empty.
static String resolve(const Vars *vars, const Vars *fallback, String value)
{
    for (size_t depth = 0; depth < MAX_VAR_DEPTH; depth++) {
        value = str_trim(value);
        if (!str_starts_with(value, S("var(")) || !str_ends_with(value, S(")"))) {
            return value;
        }
        String reference;
        String default_value;
        bool has_default = str_cut(str_slice(value, 4, value.len - 1), ',', &reference, &default_value);
        reference = str_trim_prefix(str_trim(reference), S("--"));
        if (!lookup(vars, fallback, reference, &value)) {
            if (!has_default) {
                return S("");
            }
            value = default_value;
        }
    }
    return S("");
}

static bool selector_lists(String selector, const char *const *names, size_t name_count)
{
    String rest = selector;
    while (rest.len > 0) {
        String item;
        str_cut(rest, ',', &item, &rest);
        for (size_t i = 0; i < name_count; i++) {
            if (str_equal(str_trim(item), S(names[i]))) {
                return true;
            }
        }
    }
    return false;
}

static void parse_declarations(Arena *arena, String body, Vars *vars)
{
    String rest = body;
    while (rest.len > 0) {
        String declaration;
        str_cut(rest, ';', &declaration, &rest);
        String name;
        String value;
        if (!str_cut(declaration, ':', &name, &value)) {
            continue;
        }
        name = str_trim(name);
        if (str_starts_with(name, S("--"))) {
            vars_put(arena, vars, str_slice(name, 2, name.len), str_trim(value));
        }
    }
}

static bool block_end(String css, size_t open, size_t *close)
{
    size_t depth = 0;
    for (size_t i = open; i < css.len; i++) {
        if (css.data[i] == '{') {
            depth++;
        } else if (css.data[i] == '}' && --depth == 0) {
            *close = i;
            return true;
        }
    }
    return false;
}

// parse_rules collects the variables of :root and .dark rules, also inside
// @layer blocks as shadcn's globals.css has them. Other rules are skipped.
static void parse_rules(Arena *arena, String css, Vars *light, Vars *dark)
{
    size_t start = 0;
    while (start < css.len) {
        size_t end = start;
        while (end < css.len && css.data[end] != '{' && css.data[end] != ';') {
            end++;
        }
        if (end == css.len) {
            return;
        }
        if (css.data[end] == ';') {
            start = end + 1;
            continue;
        }
        size_t close;
        if (!block_end(css, end, &close)) {
            return;
        }
        String selector = str_trim(str_slice(css, start, end));
        String body = str_slice(css, end + 1, close);
        if (str_starts_with(selector, S("@layer"))) {
            parse_rules(arena, body, light, dark);
        } else if (selector_lists(selector, DARK_SELECTORS, countof(DARK_SELECTORS))) {
            parse_declarations(arena, body, dark);
        } else if (selector_lists(selector, LIGHT_SELECTORS, countof(LIGHT_SELECTORS))) {
            parse_declarations(arena, body, light);
        }
        start = close + 1;
    }
}

static String strip_comments(Arena *arena, String css)
{
    StringBuilder builder = str_builder_create(arena, css.len);
    String rest = css;
    size_t open;
    while (str_find(rest, S("/*"), &open)) {
        str_builder_append(&builder, str_slice(rest, 0, open));
        size_t close;
        String comment = str_slice(rest, open + 2, rest.len);
        if (!str_find(comment, S("*/"), &close)) {
            return str_builder_finish(&builder);
        }
        rest = str_slice(comment, close + 2, comment.len);
    }
    str_builder_append(&builder, rest);
    return str_builder_finish(&builder);
}

static void add_mapping(Arena *arena, Vars *vars, const Node *mapping)
{
    if (mapping == nullptr || mapping->kind != NODE_MAPPING) {
        return;
    }
    for (size_t i = 0; i < mapping->entry_count; i++) {
        const Node *value = mapping->entries[i].value;
        if (value->kind == NODE_STRING || value->kind == NODE_INT || value->kind == NODE_FLOAT) {
            vars_put(arena, vars, mapping->entries[i].key, value->text);
        }
    }
}

static void push_color(Arena *arena, Theme *theme, size_t *capacity, ThemeColor color)
{
    theme->colors = arena_grow(arena, theme->colors, capacity, theme->color_count, sizeof *theme->colors);
    theme->colors[theme->color_count++] = color;
}

static void collect_colors(Arena *arena, Theme *theme, const Vars *light, const Vars *dark)
{
    size_t capacity = 0;
    for (size_t i = 0; i < light->count; i++) {
        String name = light->items[i].name;
        ThemeColor color = { .name = str_copy(arena, name) };
        if (!color_parse(resolve(light, nullptr, light->items[i].value), &color.light)) {
            continue;
        }
        // A light declaration also holds in dark mode, where its var()
        // references resolve to the dark values, as they would in CSS.
        String dark_value;
        if (!lookup(dark, nullptr, name, &dark_value)) {
            dark_value = light->items[i].value;
        }
        if (!color_parse(resolve(dark, light, dark_value), &color.dark)) {
            color.dark = color.light;
        }
        push_color(arena, theme, &capacity, color);
    }
    for (size_t i = 0; i < dark->count; i++) {
        String name = dark->items[i].name;
        String unused_value;
        ThemeColor color = { .name = str_copy(arena, name) };
        if (lookup(light, nullptr, name, &unused_value) ||
            !color_parse(resolve(dark, light, dark->items[i].value), &color.dark)) {
            continue;
        }
        color.light = color.dark;
        push_color(arena, theme, &capacity, color);
    }
}

// parse_length_px reads px, rem and em lengths, a rem or em being 16 px.
static bool parse_length_px(String text, double *px)
{
    double scale = 1.0;
    if (str_ends_with(text, S("rem"))) {
        scale = CSS_PX_PER_REM;
        text = str_trim_suffix(text, S("rem"));
    } else if (str_ends_with(text, S("em"))) {
        scale = CSS_PX_PER_REM;
        text = str_trim_suffix(text, S("em"));
    } else {
        text = str_trim_suffix(text, S("px"));
    }
    double value;
    if (!float_parse(text, &value)) {
        return false;
    }
    *px = value * scale;
    return true;
}

static String unquote(String text)
{
    if (text.len >= 2 && (text.data[0] == '"' || text.data[0] == '\'') && text.data[text.len - 1] == text.data[0]) {
        return str_slice(text, 1, text.len - 1);
    }
    return text;
}

// typeface is the first family of a font stack, or empty when that is a
// generic family, which on a device means its system font.
static String typeface(Arena *arena, const Vars *vars, const char *name)
{
    String stack;
    if (!lookup(vars, nullptr, S(name), &stack)) {
        return S("");
    }
    String first;
    String rest;
    str_cut(resolve(vars, nullptr, stack), ',', &first, &rest);
    first = unquote(str_trim(first));
    for (size_t i = 0; i < countof(GENERIC_FAMILIES); i++) {
        if (str_equal_ignore_case(first, S(GENERIC_FAMILIES[i]))) {
            return S("");
        }
    }
    return str_copy(arena, first);
}

Error theme_parse(Arena *arena, String text, String name, Theme *theme, Err *err)
{
    *theme = (Theme){ 0 };
    Vars light = { 0 };
    Vars dark = { 0 };
    String trimmed = str_trim(text);
    if (str_starts_with(trimmed, S("{"))) {
        Node *root;
        Error error = json_parse(arena, trimmed, &root, err);
        if (error != ERR_OK) {
            return err_wrap(err, error, "theme JSON");
        }
        const Node *css_vars = node_get(root, S("cssVars"));
        if (css_vars == nullptr) {
            return err_set(err, ERR_PARSE, "not a tweakcn theme: the JSON has no cssVars");
        }
        add_mapping(arena, &light, node_get(css_vars, S("theme")));
        add_mapping(arena, &light, node_get(css_vars, S("light")));
        add_mapping(arena, &dark, node_get(css_vars, S("dark")));
        name = node_get_string(root, S("name"), name);
    } else {
        parse_rules(arena, strip_comments(arena, text), &light, &dark);
    }
    collect_colors(arena, theme, &light, &dark);
    if (theme->color_count == 0) {
        return err_set(err, ERR_PARSE, "no colors found: expected tweakcn theme JSON, or CSS with :root and .dark blocks");
    }
    theme->name = str_copy(arena, name);
    String radius;
    theme->has_radius = lookup(&light, nullptr, S("radius"), &radius) &&
                        parse_length_px(resolve(&light, nullptr, radius), &theme->radius);
    theme->font_sans = typeface(arena, &light, "font-sans");
    theme->font_serif = typeface(arena, &light, "font-serif");
    theme->font_mono = typeface(arena, &light, "font-mono");
    return ERR_OK;
}

const ThemeColor *theme_color(const Theme *theme, String name)
{
    for (size_t i = 0; i < theme->color_count; i++) {
        if (str_equal(theme->colors[i].name, name)) {
            return &theme->colors[i];
        }
    }
    return nullptr;
}

// hundredths rounds away the binary noise of rem arithmetic, so 0.35rem - 4px is 1.6, not 1.5999999999999996.
static double hundredths(double px)
{
    return round(fmax(px, 0.0) * 100.0) / 100.0;
}

ThemeRadii theme_radii(const Theme *theme)
{
    double radius = theme->radius;
    return (ThemeRadii){
        .sm = hundredths(radius - 4.0),
        .md = hundredths(radius - 2.0),
        .lg = hundredths(radius),
        .xl = hundredths(radius + 4.0),
    };
}
