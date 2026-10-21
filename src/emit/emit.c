#include "emit/emit.h"

#include "emit/android.h"
#include "emit/apple.h"
#include "emit/flutter.h"
#include "emit/tokens.h"

typedef void (*Emitter)(Arena *arena, const Theme *theme, const EmitOptions *options, Output *output);

typedef struct FormatInfo {
    const char *name;
    const char *summary;
    Emitter emit;
} FormatInfo;

static const FormatInfo FORMATS[FORMAT_COUNT] = {
    [FORMAT_SWIFT] = { "swift", "SwiftUI colors for iOS and macOS, with radii and fonts", apple_swift },
    [FORMAT_XCASSETS] = { "xcassets", "an Xcode asset catalog with a color set per token", apple_asset_catalog },
    [FORMAT_COMPOSE] = { "compose", "a Jetpack Compose Material 3 theme in Kotlin", android_compose },
    [FORMAT_ANDROID] = { "android", "Android color and dimen resources for values and values-night", android_resources },
    [FORMAT_FLUTTER] = { "flutter", "a Flutter ThemeExtension and Material 3 ThemeData", flutter_dart },
    [FORMAT_JSON] = { "json", "plain JSON tokens with sRGB hex colors", tokens_json },
};

void output_add(Arena *arena, Output *output, String path, String contents)
{
    output->items = arena_grow(arena, output->items, &output->capacity, output->count, sizeof *output->items);
    output->items[output->count++] = (OutputFile){ path, contents };
}

String emit_length(Arena *arena, double value)
{
    String text = str_format(arena, "%.2f", value);
    while (str_ends_with(text, S("0"))) {
        text.len--;
    }
    return str_trim_suffix(text, S("."));
}

bool format_parse(String name, Format *format)
{
    for (size_t i = 0; i < FORMAT_COUNT; i++) {
        if (str_equal(name, S(FORMATS[i].name))) {
            *format = (Format)i;
            return true;
        }
    }
    return false;
}

const char *format_name(Format format)
{
    return FORMATS[format].name;
}

const char *format_summary(Format format)
{
    return FORMATS[format].summary;
}

void emit(Arena *arena, Format format, const Theme *theme, const EmitOptions *options, Output *output)
{
    FORMATS[format].emit(arena, theme, options, output);
}
