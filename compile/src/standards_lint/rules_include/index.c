/* See README.md. */
#include "standards_lint/rules_include/index.h"

#include "standards_lint/catalog/index.h"

#include <string.h>

typedef enum include_match_t : uint8_t {
    INCLUDE_MATCH_EXACT = 0,
    INCLUDE_MATCH_PREFIX = 1,
    INCLUDE_MATCH_SUFFIX = 2,
} include_match_t;

typedef struct include_banned_t {
    const char *pattern;
    const char *rule;
    include_match_t match;
    uint8_t _pad0[7];
} include_banned_t;
static_assert(sizeof(include_banned_t) == 24);

/* plans/CODING_STANDARD.md section 1, the banned libraries and no networking, and section 7, the intrinsics family. */
static const include_banned_t include_banned[] = {
    {"intrin.h", "banned_header:intrinsics", INCLUDE_MATCH_SUFFIX, {0}},
    {"arm_neon.h", "banned_header:intrinsics", INCLUDE_MATCH_EXACT, {0}},
    {"GL/", "banned_header:opengl", INCLUDE_MATCH_PREFIX, {0}},
    {"GLES", "banned_header:opengl", INCLUDE_MATCH_PREFIX, {0}},
    {"vulkan/", "banned_header:vulkan", INCLUDE_MATCH_PREFIX, {0}},
    {"EGL/", "banned_header:egl", INCLUDE_MATCH_PREFIX, {0}},
    {"gbm.h", "banned_header:gbm", INCLUDE_MATCH_EXACT, {0}},
    {"xf86drm", "banned_header:libdrm", INCLUDE_MATCH_PREFIX, {0}},
    {"libdrm/", "banned_header:libdrm", INCLUDE_MATCH_PREFIX, {0}},
    {"X11/", "banned_header:x11", INCLUDE_MATCH_PREFIX, {0}},
    {"xcb/", "banned_header:xcb", INCLUDE_MATCH_PREFIX, {0}},
    {"wayland-", "banned_header:wayland", INCLUDE_MATCH_PREFIX, {0}},
    {"SDL", "banned_header:sdl", INCLUDE_MATCH_PREFIX, {0}},
    {"GLFW/", "banned_header:glfw", INCLUDE_MATCH_PREFIX, {0}},
    {"optix", "banned_header:optix", INCLUDE_MATCH_PREFIX, {0}},
    {"stb_", "banned_header:stb", INCLUDE_MATCH_PREFIX, {0}},
    {"zlib.h", "banned_header:zlib", INCLUDE_MATCH_EXACT, {0}},
    {"png.h", "banned_header:libpng", INCLUDE_MATCH_EXACT, {0}},
    {"yaml.h", "banned_header:libyaml", INCLUDE_MATCH_EXACT, {0}},
    {"sys/socket.h", "banned_header:sockets", INCLUDE_MATCH_EXACT, {0}},
    {"sys/un.h", "banned_header:sockets", INCLUDE_MATCH_EXACT, {0}},
    {"netinet/", "banned_header:sockets", INCLUDE_MATCH_PREFIX, {0}},
    {"arpa/", "banned_header:sockets", INCLUDE_MATCH_PREFIX, {0}},
    {"netdb.h", "banned_header:sockets", INCLUDE_MATCH_EXACT, {0}},
    {"dlfcn.h", "banned_header:dlfcn", INCLUDE_MATCH_EXACT, {0}},
};
enum { INCLUDE_BANNED_COUNT = sizeof(include_banned) / sizeof(include_banned[0]) };

typedef struct include_system_t {
    const char *name;
    uint32_t rank;
    uint32_t _pad0;
} include_system_t;
static_assert(sizeof(include_system_t) == 16);

enum { INCLUDE_RANK_CUDA = 7 };

/* The diagram in plans/REPO_STRUCTURE.md section 11, top to bottom. Render sits above gpu_bridge, which sits above
 * cuda/, so those get the rows between render and terrain. A lower row may never include a higher one. The diagram does
 * not show profiler; instrumentation has to be callable from the job row upward, so it shares that row. */
static const include_system_t include_systems[] = {
    {"main", 1, 0},      {"game", 2, 0},    {"ui", 3, 0},        {"save", 3, 0},
    {"settings", 3, 0},  {"mob", 4, 0},     {"player", 4, 0},    {"entity", 5, 0},
    {"item", 5, 0},      {"physics", 5, 0}, {"render", 6, 0},    {"gpu_bridge", INCLUDE_RANK_CUDA, 0},
    {"terrain", 9, 0},   {"world", 9, 0},   {"display", 10, 0},  {"input", 10, 0},
    {"texture", 10, 0},  {"audio", 10, 0},  {"job", 11, 0},      {"thread", 11, 0},
    {"file", 11, 0},     {"time", 11, 0},   {"math", 11, 0},     {"simd", 11, 0},
    {"profiler", 11, 0}, {"arena", 12, 0},  {"heap", 12, 0},     {"log", 12, 0},
    {"assert", 12, 0},   {"types", 12, 0},  {"calendar", 11, 0},
};
enum { INCLUDE_SYSTEM_COUNT = sizeof(include_systems) / sizeof(include_systems[0]) };

uint32_t lint_rules_include_system_rank(const char *system, uint64_t length) {
    for (uint32_t i = 0; i < INCLUDE_SYSTEM_COUNT; i++) {
        if (lint_text_equal(system, length, include_systems[i].name)) {
            return include_systems[i].rank;
        }
    }
    return 0;
}

static bool include_is_banned(const char *path, const include_banned_t *row) {
    switch (row->match) {
    case INCLUDE_MATCH_EXACT:
        return strcmp(path, row->pattern) == 0;
    case INCLUDE_MATCH_PREFIX:
        return lint_starts_with(path, row->pattern);
    default:
        return lint_ends_with(path, row->pattern);
    }
}

/* Both spellings: GCC falls back to the system directories for #include "immintrin.h". */
static void include_check_banned(const lint_file_t *file, const lint_include_t *include) {
    for (uint32_t i = 0; i < INCLUDE_BANNED_COUNT; i++) {
        /* Section 1's two exceptions: a Unix socket to the X server on this machine, and one to the sound server. */
        const bool local_socket =
            (strcmp(include->path, "sys/socket.h") == 0 || strcmp(include->path, "sys/un.h") == 0) &&
            (lint_path_matches("run/src/display/x11/index.c", file->path) ||
             lint_path_matches("run/src/audio/pulse/index.c", file->path));
        if (!local_socket && include_is_banned(include->path, &include_banned[i])) {
            lint_breach(file->report, file->path, include->line, include_banned[i].rule,
                        "<%s> is not allowed; see plans/CODING_STANDARD.md sections 1 and 7", include->path);
        }
    }
}

static void include_check_angled(const lint_file_t *file, const lint_include_t *include, uint8_t language) {
    if (language == LINT_LANGUAGE_C && lint_starts_with(include->path, "cuda")) {
        lint_breach(file->report, file->path, include->line, "banned_header:cuda_in_c",
                    "<%s>: C reaches CUDA only through run/include/gpu_bridge/", include->path);
    }
    if (language == LINT_LANGUAGE_CUDA && !lint_ends_with(include->path, ".h")) {
        lint_breach(file->report, file->path, include->line, "cuda_cpp_header",
                    "<%s> is a C++ library header; CUDA code stays in the C-like subset", include->path);
    }
}

typedef enum include_root_t : uint8_t {
    INCLUDE_ROOT_NONE = 0, /* generated, or next to the file */
    INCLUDE_ROOT_RUN = 1,
    INCLUDE_ROOT_COMPILE = 2,
    INCLUDE_ROOT_CUDA = 3,
    INCLUDE_ROOT_ASM = 4,
} include_root_t;

static bool include_exists_under(const lint_file_t *file, const char *directory, const char *path) {
    const char *joined = lint_arena_join(file->arena_file, file->root, "/");
    joined = lint_arena_join(file->arena_file, joined, directory);
    joined = lint_arena_join(file->arena_file, joined, path);
    return lint_path_exists(joined);
}

static include_root_t include_root_of_file(const char *path) {
    if (lint_starts_with(path, "run/src/") || lint_starts_with(path, "run/include/")) {
        return INCLUDE_ROOT_RUN;
    }
    if (lint_starts_with(path, "compile/src/") || lint_starts_with(path, "compile/include/")) {
        return INCLUDE_ROOT_COMPILE;
    }
    if (lint_starts_with(path, "cuda/src/") || lint_starts_with(path, "cuda/include/")) {
        return INCLUDE_ROOT_CUDA;
    }
    return INCLUDE_ROOT_NONE;
}

/* The include root that holds the target, the file's own root first, as its include path would find it. */
static include_root_t include_resolve(const lint_file_t *file, const char *target, include_root_t own) {
    if (own == INCLUDE_ROOT_RUN && include_exists_under(file, "run/include/", target)) {
        return INCLUDE_ROOT_RUN;
    }
    if (own == INCLUDE_ROOT_COMPILE && include_exists_under(file, "compile/include/", target)) {
        return INCLUDE_ROOT_COMPILE;
    }
    if (own == INCLUDE_ROOT_CUDA && include_exists_under(file, "cuda/include/", target)) {
        return INCLUDE_ROOT_CUDA;
    }
    if (include_exists_under(file, "run/include/", target)) {
        return INCLUDE_ROOT_RUN;
    }
    if (include_exists_under(file, "compile/include/", target)) {
        return INCLUDE_ROOT_COMPILE;
    }
    if (include_exists_under(file, "cuda/include/", target)) {
        return INCLUDE_ROOT_CUDA;
    }
    return INCLUDE_ROOT_NONE;
}

/* The module directory of a path under <root>/src/ or <root>/include/: world/section for
 * run/src/world/section/index.c. */
static const char *include_module_of_file(const lint_file_t *file) {
    const char *rest = strchr(file->path, '/');
    rest = rest != nullptr ? strchr(rest + 1, '/') : nullptr;
    if (rest == nullptr) {
        return "";
    }
    rest++;
    const char *last_slash = strrchr(rest, '/');
    const uint64_t length = last_slash != nullptr ? (uint64_t)(last_slash - rest) : 0u;
    return lint_arena_text(file->arena_file, rest, length);
}

static const char *include_module_of_target(const lint_file_t *file, const char *target) {
    const char *last_slash = strrchr(target, '/');
    const uint64_t length = last_slash != nullptr ? (uint64_t)(last_slash - target) : 0u;
    return lint_arena_text(file->arena_file, target, length);
}

static uint64_t include_system_length(const char *module) {
    const char *slash = strchr(module, '/');
    return slash != nullptr ? (uint64_t)(slash - module) : strlen(module);
}

static bool include_is_within(const char *inner, const char *outer) {
    const uint64_t length = strlen(outer);
    return strncmp(inner, outer, length) == 0 && (inner[length] == '\0' || inner[length] == '/');
}

/* Parts of one system: a part may include what is above or below it, and a sibling only if its README says so. */
static void include_check_same_system(const lint_file_t *file, const lint_include_t *include, const char *own_module,
                                      const char *target_module, const char *root_name) {
    if (include_is_within(own_module, target_module) || include_is_within(target_module, own_module)) {
        return;
    }
    const char *readme = lint_arena_join(file->arena_file, file->root, "/");
    readme = lint_arena_join(file->arena_file, readme, root_name);
    readme = lint_arena_join(file->arena_file, readme, "/src/");
    readme = lint_arena_join(file->arena_file, readme, own_module);
    readme = lint_arena_join(file->arena_file, readme, "/README.md");
    const char *text = nullptr;
    uint64_t length = 0;
    if (lint_path_exists(readme)) {
        lint_report_t quiet = {0, 0};
        if (lint_read_file(file->arena_file, &quiet, readme, &text, &length) &&
            strstr(text, target_module) != nullptr) {
            return;
        }
    }
    lint_breach(file->report, file->path, include->line, "include_sibling",
                "\"%s\": a sibling part may be included only if %s/src/%s/README.md names %s", include->path, root_name,
                own_module, target_module);
}

static void include_check_quoted(const lint_file_t *file, const lint_include_t *include, uint8_t language) {
    const char *target = include->path;
    if (strstr(target, "..") != nullptr || lint_starts_with(target, "./")) {
        lint_breach(file->report, file->path, include->line, "include_relative",
                    "\"%s\": include from an include root, never by a relative path", target);
        return;
    }
    if (lint_ends_with(target, ".c") || lint_ends_with(target, ".cu") || lint_ends_with(target, ".asm")) {
        lint_breach(file->report, file->path, include->line, "include_source", "\"%s\": never include a source file",
                    target);
        return;
    }
    const include_root_t own = include_root_of_file(file->path);
    const include_root_t found = include_resolve(file, target, own);
    if (language == LINT_LANGUAGE_C && (lint_ends_with(target, ".hu") || found == INCLUDE_ROOT_CUDA)) {
        lint_breach(file->report, file->path, include->line, "include_cuda_from_c",
                    "\"%s\": C never includes a .hu; use run/include/gpu_bridge/", target);
        return;
    }
    const bool is_tests = lint_starts_with(file->path, "tests/") || lint_starts_with(file->path, "bench/");
    if (is_tests || found == INCLUDE_ROOT_NONE) {
        return; /* tests see everything; anything else is generated output or a system header */
    }
    if (found == INCLUDE_ROOT_COMPILE && own != INCLUDE_ROOT_COMPILE) {
        lint_breach(file->report, file->path, include->line, "include_compile_tool",
                    "\"%s\": compile/ runs at build time, and nothing else includes from it", target);
        return;
    }
    const char *target_module = include_module_of_target(file, target);
    const uint64_t target_system_length = include_system_length(target_module);

    if (own == INCLUDE_ROOT_COMPILE) {
        if (found == INCLUDE_ROOT_RUN && !lint_starts_with(target, "types/")) {
            lint_breach(file->report, file->path, include->line, "include_run_from_compile",
                        "\"%s\": compile/ may include only types/ from run/ (plans/REPO_STRUCTURE.md section 11)",
                        target);
        } else if (found == INCLUDE_ROOT_COMPILE) {
            const char *own_module = include_module_of_file(file);
            if (include_system_length(own_module) == target_system_length &&
                strncmp(own_module, target_module, target_system_length) == 0) {
                include_check_same_system(file, include, own_module, target_module, "compile");
            }
        }
        return;
    }
    if (found != INCLUDE_ROOT_RUN) {
        return;
    }
    const uint32_t target_rank = lint_rules_include_system_rank(target_module, target_system_length);
    if (own == INCLUDE_ROOT_CUDA) {
        if (target_rank != 0 && target_rank < INCLUDE_RANK_CUDA) {
            lint_breach(file->report, file->path, include->line, "include_upward",
                        "\"%s\": cuda/ sits below gpu_bridge and may not include upward", target);
        }
        return;
    }
    if (own != INCLUDE_ROOT_RUN) {
        return;
    }
    const char *own_module = include_module_of_file(file);
    const uint64_t own_system_length = include_system_length(own_module);
    if (own_system_length == target_system_length && strncmp(own_module, target_module, own_system_length) == 0) {
        include_check_same_system(file, include, own_module, target_module, "run");
        return;
    }
    const uint32_t own_rank = lint_rules_include_system_rank(own_module, own_system_length);
    if (own_rank != 0 && target_rank != 0 && target_rank < own_rank) {
        lint_breach(file->report, file->path, include->line, "include_upward",
                    "\"%s\": a lower layer never includes an upper one (plans/REPO_STRUCTURE.md section 11)", target);
    }
}

void lint_rules_include_check(const lint_file_t *file, const lint_lexed_t *lexed, uint8_t languages) {
    for (uint32_t i = 0; i < lexed->include_count; i++) {
        const lint_include_t *include = &lexed->includes[i];
        if (include->computed != 0) {
            lint_breach(file->report, file->path, include->line, "include_computed",
                        "an include named by a macro hides its target from every include rule");
            continue;
        }
        include_check_banned(file, include);
        if (include->angled != 0) {
            include_check_angled(file, include, languages);
        }
        /* <game/index.h> finds the same file through the same -I as "game/index.h", so it obeys the same rules. Anything
         * not under an include root, which is every real system header, resolves to nothing and is left alone. */
        include_check_quoted(file, include, languages);
    }
}
