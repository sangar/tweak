#include "emit/naming.h"

typedef enum Case {
    CASE_PASCAL,
    CASE_CAMEL,
    CASE_SNAKE,
} Case;

static bool is_lower(char c)
{
    return c >= 'a' && c <= 'z';
}

static bool is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static char to_upper(char c)
{
    return is_lower(c) ? (char)(c - 'a' + 'A') : c;
}

static char to_lower(char c)
{
    return is_upper(c) ? (char)(c - 'A' + 'a') : c;
}

static String convert(Arena *arena, String name, Case style, String lead)
{
    StringBuilder builder = str_builder_create(arena, name.len + lead.len + 1);
    size_t words = 0;
    bool word_start = true;
    for (size_t i = 0; i < name.len; i++) {
        char c = name.data[i];
        if (!is_lower(c) && !is_upper(c) && !is_digit(c)) {
            word_start = true;
            continue;
        }
        char previous = i > 0 ? name.data[i - 1] : '\0';
        word_start = word_start || (is_upper(c) && (is_lower(previous) || is_digit(previous)));
        if (!word_start) {
            str_builder_append_char(&builder, style == CASE_SNAKE ? to_lower(c) : c);
            continue;
        }
        if (words == 0 && is_digit(c)) {
            str_builder_append(&builder, lead);
            if (style == CASE_SNAKE) {
                str_builder_append_char(&builder, '_');
            }
        } else if (words > 0 && style == CASE_SNAKE) {
            str_builder_append_char(&builder, '_');
        }
        bool upper = style == CASE_PASCAL || (style == CASE_CAMEL && words > 0);
        str_builder_append_char(&builder, upper ? to_upper(c) : to_lower(c));
        words++;
        word_start = false;
    }
    if (words == 0) {
        str_builder_append(&builder, lead);
    }
    return str_builder_finish(&builder);
}

String naming_pascal(Arena *arena, String name)
{
    return convert(arena, name, CASE_PASCAL, S("Tweak"));
}

String naming_camel(Arena *arena, String name)
{
    return convert(arena, name, CASE_CAMEL, S("color"));
}

String naming_snake(Arena *arena, String name)
{
    return convert(arena, name, CASE_SNAKE, S("theme"));
}

String naming_type(Arena *arena, String theme_name)
{
    String pascal = naming_pascal(arena, theme_name);
    return str_ends_with(pascal, S("Theme")) ? pascal : str_concat(arena, pascal, S("Theme"));
}
