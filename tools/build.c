// Self-bootstrapping build program. Bootstrap once with:
//     cc tools/build.c -o nob
// Afterwards ./nob rebuilds itself when this file changes.
//
//     ./nob                 optimised binary with debug info   -> ./tweak
//     ./nob test            the tests under AddressSanitizer and UBSan
//     ./nob check           modern-c contract check, then the tests
//     ./nob cross           every target with zig cc           -> out/<target>/tweak
//     ./nob install         ./tweak to $PREFIX/bin (default ~/.local), under $DESTDIR when set
//     ./nob uninstall       remove the installed binary
//     ./nob clean           remove out/ and ./tweak
//
// Host builds compile with $CC, or cc when it is unset. $VERSION sets what
// `tweak --version` prints; it defaults to `git describe` or "dev".

#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

enum { MAX_ARGS = 512, MAX_FILES = 256, PATH_MAX_LEN = 1024, FINGERPRINT_LEN = 4096 };

typedef struct {
    const char *items[MAX_ARGS];
    size_t count;
} Command;

typedef struct {
    char items[MAX_FILES][PATH_MAX_LEN];
    size_t count;
} FileList;

typedef struct {
    const char *name;
    const char *zig_target;
} Target;

static const Target TARGETS[] = {
    { "linux-x86_64", "x86_64-linux-musl" },
    { "linux-aarch64", "aarch64-linux-musl" },
    { "macos-aarch64", "aarch64-macos" },
    { "macos-x86_64", "x86_64-macos" },
};

static const char *const WARNING_FLAGS[] = {
    "-std=c23", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wshadow", "-Wvla",
    "-Wstrict-prototypes", "-Wimplicit-fallthrough", "-Wno-unused-parameter",
};
// A sanitizer finding stops the tests rather than scrolling past.
static const char *const DEBUG_FLAGS[] = { "-g", "-O0", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                           "-fno-omit-frame-pointer" };
static const char *const RELEASE_FLAGS[] = { "-g", "-O2" };
static const char *const INCLUDE_FLAGS[] = { "-Ideps/libmc/include", "-Ideps/libmc/deps/cyaml", "-Ideps/libmc/deps/yyjson",
                                           "-Isrc" };

static const char *const SOURCE_DIRECTORIES[] = { "src/theme", "src/source", "src/emit" };
// libmc is vendored, pinned in deps.lock, and compiled with the project's own flags.
static const char *const LIBMC_SOURCES = "deps/libmc/src";
// libmc's own vendored dependencies compile in its own standard without the
// warning flags: its warnings are upstream's to fix.
static const char *const DEPENDENCY_DIRECTORIES[] = { "deps/libmc/deps/cyaml", "deps/libmc/deps/yyjson" };
static const char *const DEPENDENCY_FLAGS[] = { "-std=c11", "-w" };

static void command_push(Command *command, const char *item)
{
    if (command->count + 1 >= MAX_ARGS) {
        fprintf(stderr, "build: too many arguments\n");
        exit(2);
    }
    command->items[command->count++] = item;
    command->items[command->count] = NULL;
}

static void command_push_many(Command *command, const char *const *items, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        command_push(command, items[i]);
    }
}

static int command_run(const Command *command)
{
    for (size_t i = 0; i < command->count; i++) {
        printf("%s%s", i == 0 ? "" : " ", command->items[i]);
    }
    printf("\n");
    fflush(stdout);
    pid_t pid;
    int status = posix_spawnp(&pid, command->items[0], NULL, NULL, (char *const *)command->items, environ);
    if (status != 0) {
        fprintf(stderr, "build: cannot run %s: %s\n", command->items[0], strerror(status));
        return 1;
    }
    int wait_status = 0;
    if (waitpid(pid, &wait_status, 0) < 0) {
        return 1;
    }
    return WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 1;
}

static void run_or_die(const Command *command)
{
    int status = command_run(command);
    if (status != 0) {
        fprintf(stderr, "build: command failed with status %d\n", status);
        exit(status);
    }
}

static time_t mtime_of(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

static void mkdir_p(const char *path)
{
    char buffer[PATH_MAX_LEN];
    snprintf(buffer, sizeof buffer, "%s", path);
    for (char *p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buffer, 0755);
            *p = '/';
        }
    }
    mkdir(buffer, 0755);
}

static void list_push(FileList *list, const char *path)
{
    if (list->count >= MAX_FILES) {
        fprintf(stderr, "build: too many files\n");
        exit(2);
    }
    snprintf(list->items[list->count++], PATH_MAX_LEN, "%s", path);
}

static void list_files(FileList *list, const char *directory, const char *extension)
{
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        size_t ext_len = strlen(extension);
        if (name_len <= ext_len || strcmp(entry->d_name + name_len - ext_len, extension) != 0) {
            continue;
        }
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        list_push(list, path);
    }
    closedir(dir);
}

// list_subdirectories lists the directories directly below directory.
static void list_subdirectories(FileList *list, const char *directory)
{
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        struct stat st;
        if (entry->d_name[0] != '.' && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            list_push(list, path);
        }
    }
    closedir(dir);
}

static time_t newest_of(const FileList *files)
{
    time_t newest = 0;
    for (size_t i = 0; i < files->count; i++) {
        time_t t = mtime_of(files->items[i]);
        newest = t > newest ? t : newest;
    }
    return newest;
}

static time_t newest_header_mtime(void)
{
    FileList directories = { 0 };
    list_subdirectories(&directories, "deps/libmc/include/mc");
    list_subdirectories(&directories, LIBMC_SOURCES);
    for (size_t i = 0; i < sizeof SOURCE_DIRECTORIES / sizeof *SOURCE_DIRECTORIES; i++) {
        list_push(&directories, SOURCE_DIRECTORIES[i]);
    }
    for (size_t i = 0; i < sizeof DEPENDENCY_DIRECTORIES / sizeof *DEPENDENCY_DIRECTORIES; i++) {
        list_push(&directories, DEPENDENCY_DIRECTORIES[i]);
    }
    FileList headers = { 0 };
    for (size_t i = 0; i < directories.count; i++) {
        list_files(&headers, directories.items[i], ".h");
    }
    return newest_of(&headers);
}

typedef struct {
    bool release;
    bool dependency;
    const Target *target;
    char out_dir[PATH_MAX_LEN];
    char obj_dir[PATH_MAX_LEN];
    // archive is libmc.a, linked after every object so that any linker finds
    // the symbols they need in it.
    char archive[PATH_MAX_LEN];
    time_t headers_mtime;
} Build;

// host_compiler is $CC when set, so one checkout can build with clang and gcc.
static const char *host_compiler(void)
{
    const char *compiler = getenv("CC");
    return compiler != NULL && compiler[0] != '\0' ? compiler : "cc";
}

static void push_compiler(Command *command, const Build *build)
{
    if (build->target != NULL) {
        command_push(command, "zig");
        command_push(command, "cc");
        command_push(command, "-target");
        command_push(command, build->target->zig_target);
    } else {
        command_push(command, host_compiler());
    }
}

// Sanitizers need the host's runtime, so cross builds never use them.
static bool sanitized(const Build *build)
{
    return !build->release && build->target == NULL;
}

static void object_path(char *out, const char *obj_dir, const char *source)
{
    char flat[PATH_MAX_LEN];
    snprintf(flat, sizeof flat, "%s", source);
    for (char *p = flat; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '_';
        }
    }
    flat[strlen(flat) - 2] = '\0';
    snprintf(out, PATH_MAX_LEN, "%s/%s.o", obj_dir, flat);
}

static const char *version(void)
{
    static char text[256];
    const char *set = getenv("VERSION");
    if (set != NULL && set[0] != '\0') {
        return set;
    }
    snprintf(text, sizeof text, "dev");
    FILE *pipe = popen("git describe --tags --always --dirty 2>/dev/null", "r");
    if (pipe != NULL) {
        char line[200];
        if (fgets(line, sizeof line, pipe) != NULL) {
            line[strcspn(line, "\n")] = '\0';
            if (line[0] != '\0') {
                snprintf(text, sizeof text, "%s", line);
            }
        }
        pclose(pipe);
    }
    return text;
}

// push_compile_command pushes everything but the input and output files.
static void push_compile_command(Command *command, const Build *build)
{
    static char version_define[300];
    push_compiler(command, build);
    if (build->dependency) {
        command_push_many(command, DEPENDENCY_FLAGS, sizeof DEPENDENCY_FLAGS / sizeof *DEPENDENCY_FLAGS);
    } else {
        command_push_many(command, WARNING_FLAGS, sizeof WARNING_FLAGS / sizeof *WARNING_FLAGS);
    }
    if (build->release) {
        command_push_many(command, RELEASE_FLAGS, sizeof RELEASE_FLAGS / sizeof *RELEASE_FLAGS);
    } else if (sanitized(build)) {
        command_push_many(command, DEBUG_FLAGS, sizeof DEBUG_FLAGS / sizeof *DEBUG_FLAGS);
    } else {
        command_push(command, "-g");
    }
    command_push_many(command, INCLUDE_FLAGS, sizeof INCLUDE_FLAGS / sizeof *INCLUDE_FLAGS);
    snprintf(version_define, sizeof version_define, "-DTWEAK_VERSION=\"%s\"", version());
    command_push(command, version_define);
}

static void command_text(const Command *command, char *out, size_t size)
{
    size_t at = 0;
    out[0] = '\0';
    for (size_t i = 0; i < command->count && at < size; i++) {
        int n = snprintf(out + at, size - at, "%s%s", i == 0 ? "" : " ", command->items[i]);
        at += n > 0 ? (size_t)n : 0;
    }
}

static bool file_holds(const char *path, const char *text)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    size_t len = strlen(text);
    size_t at = 0;
    bool same = true;
    int c;
    while (same && (c = fgetc(file)) != EOF) {
        same = at < len && (char)c == text[at++];
    }
    fclose(file);
    return same && at == len;
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL || fputs(text, file) < 0 || fclose(file) != 0) {
        fprintf(stderr, "build: cannot write %s\n", path);
        exit(1);
    }
}

// fingerprint describes what shapes objects besides their sources and headers:
// the compiler's version and the compile command, version string included.
static void fingerprint(const Build *build, char *out, size_t size)
{
    Command command = { 0 };
    push_compiler(&command, build);
    command_push(&command, "--version");
    char version_command[FINGERPRINT_LEN];
    command_text(&command, version_command, sizeof version_command);
    char compiler_version[FINGERPRINT_LEN / 2] = "";
    FILE *pipe = popen(version_command, "r");
    if (pipe != NULL) {
        compiler_version[fread(compiler_version, 1, sizeof compiler_version - 1, pipe)] = '\0';
        pclose(pipe);
    }
    Command compile_command = { 0 };
    push_compile_command(&compile_command, build);
    char flags[FINGERPRINT_LEN / 2];
    command_text(&compile_command, flags, sizeof flags);
    snprintf(out, size, "%s%s\n", compiler_version, flags);
}

// discard_stale_objects removes the objects when the fingerprint changed since they were built.
static void discard_stale_objects(const Build *build)
{
    char current[FINGERPRINT_LEN];
    fingerprint(build, current, sizeof current);
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof path, "%s/fingerprint", build->out_dir);
    if (file_holds(path, current)) {
        return;
    }
    Command command = { 0 };
    command_push(&command, "rm");
    command_push(&command, "-rf");
    command_push(&command, build->obj_dir);
    run_or_die(&command);
    mkdir_p(build->obj_dir);
    write_text(path, current);
}

static void compile(const Build *build, const char *source, const char *object)
{
    if (mtime_of(object) > mtime_of(source) && mtime_of(object) > build->headers_mtime) {
        return;
    }
    Command command = { 0 };
    push_compile_command(&command, build);
    command_push(&command, "-c");
    command_push(&command, source);
    command_push(&command, "-o");
    command_push(&command, object);
    run_or_die(&command);
}

static void compile_directory(const Build *build, const char *directory, FileList *objects)
{
    FileList sources = { 0 };
    list_files(&sources, directory, ".c");
    for (size_t i = 0; i < sources.count; i++) {
        char object[PATH_MAX_LEN];
        object_path(object, build->obj_dir, sources.items[i]);
        compile(build, sources.items[i], object);
        list_push(objects, object);
    }
}

// archive_libmc puts libmc and its dependencies in a static archive, so a
// binary links only the modules it calls: tweak never reaches the file
// watcher, so it needs no CoreServices framework and cross-links for macOS.
static void archive_libmc(const Build *build)
{
    FileList libmc_objects = { 0 };
    FileList libmc_areas = { 0 };
    list_subdirectories(&libmc_areas, LIBMC_SOURCES);
    for (size_t i = 0; i < libmc_areas.count; i++) {
        compile_directory(build, libmc_areas.items[i], &libmc_objects);
    }
    Build dependency = *build;
    dependency.dependency = true;
    for (size_t i = 0; i < sizeof DEPENDENCY_DIRECTORIES / sizeof *DEPENDENCY_DIRECTORIES; i++) {
        compile_directory(&dependency, DEPENDENCY_DIRECTORIES[i], &libmc_objects);
    }
    const char *archive = build->archive;
    if (mtime_of(archive) <= newest_of(&libmc_objects)) {
        Command command = { 0 };
        if (build->target != NULL) {
            command_push(&command, "zig");
        }
        command_push(&command, "ar");
        command_push(&command, "rcs");
        command_push(&command, archive);
        for (size_t i = 0; i < libmc_objects.count; i++) {
            command_push(&command, libmc_objects.items[i]);
        }
        remove(archive);
        run_or_die(&command);
    }
}

// compile_program compiles libmc and the project's sources but not main.c or the tests.
static void compile_program(const Build *build, FileList *objects)
{
    mkdir_p(build->obj_dir);
    discard_stale_objects(build);
    for (size_t i = 0; i < sizeof SOURCE_DIRECTORIES / sizeof *SOURCE_DIRECTORIES; i++) {
        compile_directory(build, SOURCE_DIRECTORIES[i], objects);
    }
    archive_libmc(build);
}

static void link_binary(const Build *build, const FileList *objects, const char *binary)
{
    if (mtime_of(binary) > newest_of(objects) && mtime_of(binary) > mtime_of(build->archive)) {
        return;
    }
    Command command = { 0 };
    push_compiler(&command, build);
    if (sanitized(build)) {
        command_push(&command, "-fsanitize=address,undefined");
        command_push(&command, "-fno-sanitize-recover=all");
    }
    for (size_t i = 0; i < objects->count; i++) {
        command_push(&command, objects->items[i]);
    }
    command_push(&command, build->archive);
    command_push(&command, "-lpthread");
    command_push(&command, "-lm");
    command_push(&command, "-o");
    command_push(&command, binary);
    run_or_die(&command);
}

static void build_binary(const Build *build, const char *binary)
{
    FileList objects = { 0 };
    compile_program(build, &objects);
    char object[PATH_MAX_LEN];
    object_path(object, build->obj_dir, "src/main.c");
    compile(build, "src/main.c", object);
    list_push(&objects, object);
    link_binary(build, &objects, binary);
}

static int build_and_run_tests(const Build *build)
{
    FileList objects = { 0 };
    compile_program(build, &objects);
    compile_directory(build, "tests", &objects);
    char binary[PATH_MAX_LEN];
    snprintf(binary, sizeof binary, "%s/test_tweak", build->out_dir);
    link_binary(build, &objects, binary);
    Command run = { 0 };
    command_push(&run, binary);
    return command_run(&run);
}

static void configure(Build *build, bool release, const Target *target)
{
    build->release = release;
    build->dependency = false;
    build->target = target;
    build->headers_mtime = newest_header_mtime();
    snprintf(build->out_dir, sizeof build->out_dir, "out/%s%s%s", target != NULL ? target->name : "",
             target != NULL ? "/" : "", release ? "release" : "debug");
    snprintf(build->obj_dir, sizeof build->obj_dir, "%s/obj", build->out_dir);
    snprintf(build->archive, sizeof build->archive, "%s/libmc.a", build->out_dir);
}

static int run_cross(void)
{
    for (size_t i = 0; i < sizeof TARGETS / sizeof *TARGETS; i++) {
        Build build;
        configure(&build, true, &TARGETS[i]);
        char binary[PATH_MAX_LEN];
        snprintf(binary, sizeof binary, "out/%s/tweak", TARGETS[i].name);
        build_binary(&build, binary);
        printf("built %s\n", binary);
    }
    return 0;
}

static void rebuild_self_if_needed(char **argv)
{
    const char *source = "tools/build.c";
    if (mtime_of(source) <= mtime_of(argv[0])) {
        return;
    }
    printf("build: rebuilding self\n");
    Command command = { 0 };
    command_push(&command, "cc");
    command_push(&command, source);
    command_push(&command, "-o");
    command_push(&command, argv[0]);
    run_or_die(&command);
    execv(argv[0], argv);
    fprintf(stderr, "build: exec failed: %s\n", strerror(errno));
    exit(1);
}

static int run_check(void)
{
    const char *home = getenv("HOME");
    char checker[PATH_MAX_LEN];
    snprintf(checker, sizeof checker, "%s/.agents/tools/modern-c/modern-c", home == NULL ? "" : home);
    Command command = { 0 };
    command_push(&command, "ruby");
    command_push(&command, checker);
    command_push(&command, "check");
    command_push(&command, ".");
    return command_run(&command);
}

// installed_path is $DESTDIR$PREFIX/bin/tweak, PREFIX defaulting to ~/.local.
static void installed_path(char *directory, char *binary)
{
    const char *destdir = getenv("DESTDIR");
    const char *prefix = getenv("PREFIX");
    const char *home = getenv("HOME");
    char base[PATH_MAX_LEN];
    if (prefix != NULL && prefix[0] != '\0') {
        snprintf(base, sizeof base, "%s", prefix);
    } else {
        snprintf(base, sizeof base, "%s/.local", home == NULL ? "" : home);
    }
    snprintf(directory, PATH_MAX_LEN, "%s%s/bin", destdir == NULL ? "" : destdir, base);
    snprintf(binary, PATH_MAX_LEN, "%s/tweak", directory);
}

static int run_install(void)
{
    char directory[PATH_MAX_LEN];
    char installed[PATH_MAX_LEN];
    installed_path(directory, installed);
    Command make_directory = { 0 };
    command_push(&make_directory, "install");
    command_push(&make_directory, "-d");
    command_push(&make_directory, directory);
    run_or_die(&make_directory);
    Command copy = { 0 };
    command_push(&copy, "install");
    command_push(&copy, "-m");
    command_push(&copy, "755");
    command_push(&copy, "tweak");
    command_push(&copy, installed);
    return command_run(&copy);
}

static int run_uninstall(void)
{
    char directory[PATH_MAX_LEN];
    char installed[PATH_MAX_LEN];
    installed_path(directory, installed);
    Command remove_binary = { 0 };
    command_push(&remove_binary, "rm");
    command_push(&remove_binary, "-f");
    command_push(&remove_binary, installed);
    return command_run(&remove_binary);
}

int main(int argc, char **argv)
{
    rebuild_self_if_needed(argv);
    const char *mode = argc > 1 ? argv[1] : "build";

    if (strcmp(mode, "clean") == 0) {
        Command command = { 0 };
        command_push(&command, "rm");
        command_push(&command, "-rf");
        command_push(&command, "out");
        command_push(&command, "tweak");
        return command_run(&command);
    }
    if (strcmp(mode, "cross") == 0) {
        return run_cross();
    }
    if (strcmp(mode, "uninstall") == 0) {
        return run_uninstall();
    }

    Build build;
    configure(&build, strcmp(mode, "test") != 0 && strcmp(mode, "check") != 0, NULL);
    if (strcmp(mode, "build") == 0 || strcmp(mode, "install") == 0) {
        build_binary(&build, "tweak");
        printf("built tweak\n");
        return strcmp(mode, "install") == 0 ? run_install() : 0;
    }
    if (strcmp(mode, "test") == 0) {
        return build_and_run_tests(&build);
    }
    if (strcmp(mode, "check") == 0) {
        int status = run_check();
        return status != 0 ? status : build_and_run_tests(&build);
    }

    fprintf(stderr, "build: unknown mode '%s'\nmodes: build, test, check, cross, install, uninstall, clean\n", mode);
    return 2;
}
