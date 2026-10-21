#pragma once

#include "theme/theme.h"

// Format is a kind of native theme code that tweak writes.
typedef enum Format {
    FORMAT_SWIFT,
    FORMAT_XCASSETS,
    FORMAT_COMPOSE,
    FORMAT_ANDROID,
    FORMAT_FLUTTER,
    FORMAT_JSON,
    FORMAT_COUNT,
} Format;

typedef struct EmitOptions {
    // gamut is the color space colors are written in. Android resources and
    // JSON hex colors are always sRGB.
    Gamut gamut;
    // package is the Kotlin package of the Compose file; empty for none.
    String package;
} EmitOptions;

// OutputFile is one generated file, its path relative to the output directory.
typedef struct OutputFile {
    String path;
    String contents;
} OutputFile;

typedef struct Output {
    OutputFile *items;
    size_t count;
    size_t capacity;
} Output;

void output_add(Arena *arena, Output *output, String path, String contents);

// emit_length writes a length to two decimals without trailing zeros, such as
// 6, 1.6 or 0.25, a form every target language and resource file reads.
String emit_length(Arena *arena, double value);

// format_parse reads a format by the name format_name gives it.
bool format_parse(String name, Format *format);
const char *format_name(Format format);
// format_summary is a line on what the format writes, for the usage text.
const char *format_summary(Format format);

// emit appends the files of format for theme to output, in the arena.
void emit(Arena *arena, Format format, const Theme *theme, const EmitOptions *options, Output *output);
