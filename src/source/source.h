#pragma once

#include "mc/core/error.h"
#include "mc/text/str.h"

// Source is the text of a theme and the name it goes by when the text names none.
typedef struct Source {
    String text;
    String name;
} Source;

// source_url is the address to download for input: the theme's registry JSON
// for a tweakcn.com theme page, editor link or bare theme name or id, and any
// other http or https URL as it is. A value that is none of these is false.
bool source_url(Arena *arena, String input, String *url);

// source_load reads the theme that input names: "-" for standard input, a
// file, or what source_url downloads. Input that names no file is a theme
// name when it is only letters, digits, dashes and underscores, and
// ERR_NOT_FOUND otherwise. Downloading runs curl; a failed download is
// ERR_NETWORK.
[[nodiscard]] Error source_load(Arena *arena, String input, Source *source, Err *err);
