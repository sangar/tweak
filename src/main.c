#include <stdio.h>

#include "emit/emit.h"
#include "mc/platform/platform.h"
#include "mc/text/path.h"
#include "source/source.h"

enum { EXIT_FAILED = 1, EXIT_USAGE = 2 };

typedef struct Options {
    String theme;
    String formats;
    String out_dir;
    String name;
    EmitOptions emit;
} Options;

static void print_usage(FILE *stream)
{
    fprintf(stream, "usage: tweak -f FORMATS [-o DIR] [options] THEME\n"
                    "\n"
                    "Translates a tweakcn.com theme into theme code for native apps.\n"
                    "\n"
                    "THEME is a tweakcn theme URL, a theme name or id such as modern-minimal,\n"
                    "a file with the theme's registry JSON or exported CSS, or - for standard input.\n"
                    "\n"
                    "options:\n"
                    "  -f, --format LIST   formats to write, separated by commas, or all\n"
                    "  -o, --out DIR       write the files into DIR; without it, a format that\n"
                    "                      writes one file prints it to standard output\n"
                    "  -n, --name NAME     theme name for generated types and files\n"
                    "      --p3            write colors in Display P3, keeping more of an oklch\n"
                    "                      theme's saturated colors (swift, xcassets, compose, flutter)\n"
                    "      --package NAME  Kotlin package of the compose file\n"
                    "  -h, --help          show this help\n"
                    "  -V, --version       show the version\n"
                    "\n"
                    "formats:\n");
    for (size_t i = 0; i < FORMAT_COUNT; i++) {
        fprintf(stream, "  %-10s %s\n", format_name((Format)i), format_summary((Format)i));
    }
}

static bool usage_error(const char *message, const char *detail)
{
    fprintf(stderr, "tweak: %s%s\n\n", message, detail);
    print_usage(stderr);
    return false;
}

static bool flag_is(const char *argument, const char *short_name, const char *long_name)
{
    String arg = S(argument);
    return (short_name != nullptr && str_equal(arg, S(short_name))) || str_equal(arg, S(long_name));
}

// parse_options fills options from argv, or reports why it cannot and is false.
static bool parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){ .emit = { .gamut = GAMUT_SRGB } };
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        String *value = nullptr;
        if (flag_is(arg, "-f", "--format")) {
            value = &options->formats;
        } else if (flag_is(arg, "-o", "--out")) {
            value = &options->out_dir;
        } else if (flag_is(arg, "-n", "--name")) {
            value = &options->name;
        } else if (flag_is(arg, nullptr, "--package")) {
            value = &options->emit.package;
        } else if (flag_is(arg, nullptr, "--p3")) {
            options->emit.gamut = GAMUT_DISPLAY_P3;
            continue;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            return usage_error("unknown option ", arg);
        } else if (options->theme.len > 0) {
            return usage_error("more than one theme: ", arg);
        } else {
            options->theme = S(arg);
            continue;
        }
        if (i + 1 == argc) {
            return usage_error("missing value for ", arg);
        }
        *value = S(argv[++i]);
    }
    if (options->theme.len == 0) {
        return usage_error("no theme given", "");
    }
    if (options->formats.len == 0) {
        return usage_error("no format given", "");
    }
    return true;
}

// parse_formats reads the comma-separated format list into selected, indexed by Format.
static bool parse_formats(Arena *arena, String list, bool selected[FORMAT_COUNT])
{
    StringList names = str_split(arena, list, ',');
    for (size_t i = 0; i < names.count; i++) {
        String name = str_trim(names.items[i]);
        Format format;
        if (str_equal(name, S("all"))) {
            for (size_t f = 0; f < FORMAT_COUNT; f++) {
                selected[f] = true;
            }
        } else if (format_parse(name, &format)) {
            selected[format] = true;
        } else {
            return usage_error("unknown format ", str_cstr(arena, name));
        }
    }
    return true;
}

[[nodiscard]] static Error write_output(Arena *arena, const Output *output, String out_dir, Err *err)
{
    if (out_dir.len == 0) {
        if (output->count != 1) {
            return err_set(err, ERR_INVALID_ARGUMENT, "these formats write %zu files: give a directory with -o",
                           output->count);
        }
        String contents = output->items[0].contents;
        if (fwrite(contents.data, 1, contents.len, stdout) != contents.len || fflush(stdout) != 0) {
            return err_set(err, ERR_IO, "writing to standard output failed");
        }
        return ERR_OK;
    }
    for (size_t i = 0; i < output->count; i++) {
        String path = path_join(arena, out_dir, output->items[i].path);
        Error error = file_write_atomic(path, output->items[i].contents, 0644, err);
        if (error != ERR_OK) {
            return error;
        }
        printf("%.*s\n", (int)path.len, path.data);
    }
    return ERR_OK;
}

[[nodiscard]] static Error run(Arena *arena, const Options *options, const bool formats[FORMAT_COUNT], Err *err)
{
    Source source;
    Error error = source_load(arena, options->theme, &source, err);
    if (error != ERR_OK) {
        return error;
    }
    Theme theme;
    error = theme_parse(arena, source.text, source.name, &theme, err);
    if (error != ERR_OK) {
        return err_wrap(err, error, "%.*s", (int)options->theme.len, options->theme.data);
    }
    if (options->name.len > 0) {
        theme.name = options->name;
    }
    Output output = { 0 };
    for (size_t f = 0; f < FORMAT_COUNT; f++) {
        if (formats[f]) {
            emit(arena, (Format)f, &theme, &options->emit, &output);
        }
    }
    return write_output(arena, &output, options->out_dir, err);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (flag_is(argv[i], "-h", "--help")) {
            print_usage(stdout);
            return 0;
        }
        if (flag_is(argv[i], "-V", "--version")) {
            printf("tweak %s\n", TWEAK_VERSION);
            return 0;
        }
    }
    Options options;
    if (!parse_options(argc, argv, &options)) {
        return EXIT_USAGE;
    }
    Arena *arena = arena_create(0);
    bool formats[FORMAT_COUNT] = { 0 };
    if (!parse_formats(arena, options.formats, formats)) {
        arena_destroy(arena);
        return EXIT_USAGE;
    }
    Err err = { 0 };
    Error error = run(arena, &options, formats, &err);
    arena_destroy(arena);
    if (error != ERR_OK) {
        fprintf(stderr, "tweak: %s\n", err.msg);
        return EXIT_FAILED;
    }
    return 0;
}
