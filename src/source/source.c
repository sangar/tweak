#include "source/source.h"

#include "mc/platform/platform.h"
#include "mc/text/path.h"

static const char *const TWEAKCN_HOSTS[] = { "https://tweakcn.com/", "https://www.tweakcn.com/" };
static const char *const TWEAKCN_PAGE_PREFIXES[] = { "themes/", "editor/theme?theme=" };
static const char REGISTRY_URL[] = "https://tweakcn.com/r/themes/";

static bool is_theme_id(String text)
{
    if (text.len == 0) {
        return false;
    }
    for (size_t i = 0; i < text.len; i++) {
        char c = text.data[i];
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!word) {
            return false;
        }
    }
    return true;
}

// tweakcn_page_id finds the theme id in a tweakcn.com theme page or editor link.
static bool tweakcn_page_id(String url, String *id)
{
    for (size_t h = 0; h < countof(TWEAKCN_HOSTS); h++) {
        if (!str_starts_with(url, S(TWEAKCN_HOSTS[h]))) {
            continue;
        }
        String path = str_trim_prefix(url, S(TWEAKCN_HOSTS[h]));
        for (size_t p = 0; p < countof(TWEAKCN_PAGE_PREFIXES); p++) {
            if (!str_starts_with(path, S(TWEAKCN_PAGE_PREFIXES[p]))) {
                continue;
            }
            String rest;
            str_cut(str_trim_prefix(path, S(TWEAKCN_PAGE_PREFIXES[p])), '&', id, &rest);
            str_cut(*id, '#', id, &rest);
            return is_theme_id(*id);
        }
    }
    return false;
}

bool source_url(Arena *arena, String input, String *url)
{
    String id;
    if (tweakcn_page_id(input, &id)) {
        *url = str_concat(arena, S(REGISTRY_URL), id);
        return true;
    }
    if (str_starts_with(input, S("https://")) || str_starts_with(input, S("http://"))) {
        *url = input;
        return true;
    }
    if (is_theme_id(input)) {
        *url = str_concat(arena, S(REGISTRY_URL), input);
        return true;
    }
    return false;
}

// url_name is the last path element of url without a query or .json, or the
// theme id of a tweakcn page.
static String url_name(String url)
{
    String id;
    if (tweakcn_page_id(url, &id)) {
        return id;
    }
    String path;
    String rest;
    str_cut(url, '?', &path, &rest);
    return str_trim_suffix(path_base(path), S(".json"));
}

[[nodiscard]] static Error download(Arena *arena, String url, String *text, Err *err)
{
    const char *const argv[] = { "curl", "--fail", "--silent", "--show-error", "--location", "--max-time", "30",
                                 str_cstr(arena, url) };
    ProcessResult result;
    Error error = process_run(arena, strlist_of(arena, countof(argv), argv), S(""), &result, err);
    if (error != ERR_OK) {
        return err_wrap(err, error, "running curl");
    }
    if (result.exit_code != 0) {
        String reason = str_trim(result.stderr_text);
        return err_set(err, ERR_NETWORK, "downloading %.*s: %.*s", (int)url.len, url.data, (int)reason.len, reason.data);
    }
    *text = result.stdout_text;
    return ERR_OK;
}

Error source_load(Arena *arena, String input, Source *source, Err *err)
{
    if (str_equal(input, S("-"))) {
        source->name = S("theme");
        return file_read_all(arena, S("/dev/stdin"), &source->text, err);
    }
    if (file_exists(input)) {
        String base = path_base(input);
        source->name = str_trim_suffix(base, path_ext(base));
        return file_read_all(arena, input, &source->text, err);
    }
    String url;
    if (!source_url(arena, input, &url)) {
        return err_set(err, ERR_NOT_FOUND, "%.*s: no such file", (int)input.len, input.data);
    }
    source->name = url_name(url);
    return download(arena, url, &source->text, err);
}
