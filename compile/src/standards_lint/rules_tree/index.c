/* See README.md. The walk finds its own work, so a new directory is linted with no list to edit. */
#include "standards_lint/rules_tree/index.h"

#include "standards_lint/catalog/index.h"
#include "standards_lint/graph/index.h"
#include "standards_lint/lexer/index.h"
#include "standards_lint/rules_asm/index.h"
#include "standards_lint/rules_c/index.h"
#include "standards_lint/rules_cmake/index.h"
#include "standards_lint/rules_include/index.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    TREE_LINE_LIMIT = 2000, /* plans/CODING_STANDARD.md section 5 */
    TREE_MAX_DIRECTORIES = 1u << 20,
    TREE_MAX_ENTRIES = 1u << 16,
};

typedef enum tree_kind_t : uint8_t {
    TREE_KIND_OTHER = 0,
    TREE_KIND_C = 1,
    TREE_KIND_H = 2,
    TREE_KIND_CU = 3,
    TREE_KIND_HU = 4,
    TREE_KIND_ASM = 5,
    TREE_KIND_INC = 6,
} tree_kind_t;

static tree_kind_t tree_kind_of(const char *name) {
    if (lint_ends_with(name, ".c")) {
        return TREE_KIND_C;
    }
    if (lint_ends_with(name, ".h")) {
        return TREE_KIND_H;
    }
    if (lint_ends_with(name, ".cu")) {
        return TREE_KIND_CU;
    }
    if (lint_ends_with(name, ".hu")) {
        return TREE_KIND_HU;
    }
    if (lint_ends_with(name, ".asm")) {
        return TREE_KIND_ASM;
    }
    if (lint_ends_with(name, ".inc")) {
        return TREE_KIND_INC;
    }
    return TREE_KIND_OTHER;
}

static bool tree_kind_is_source(tree_kind_t kind) {
    return kind == TREE_KIND_C || kind == TREE_KIND_CU || kind == TREE_KIND_ASM;
}

static bool tree_kind_is_header(tree_kind_t kind) {
    return kind == TREE_KIND_H || kind == TREE_KIND_HU || kind == TREE_KIND_INC;
}

typedef struct tree_t {
    const char *root;
    lint_arena_t *arena_walk;
    lint_arena_t *arena_file;
    lint_report_t *report;
    lint_graph_t *cuda_graph;
    uint64_t file_count;
} tree_t;
static_assert(sizeof(tree_t) == 48);

/* Where the code in one module root belongs. */
typedef struct tree_root_t {
    const char *name;
    tree_kind_t source;
    tree_kind_t header;
    uint8_t _pad0[6];
} tree_root_t;
static_assert(sizeof(tree_root_t) == 16);

static const tree_root_t tree_module_roots[] = {
    {"run", TREE_KIND_C, TREE_KIND_H, {0}},
    {"compile", TREE_KIND_C, TREE_KIND_H, {0}},
    {"cuda", TREE_KIND_CU, TREE_KIND_HU, {0}},
    {"asm", TREE_KIND_ASM, TREE_KIND_INC, {0}},
};
enum { TREE_MODULE_ROOT_COUNT = sizeof(tree_module_roots) / sizeof(tree_module_roots[0]) };

/* Code here is not a module but is held to every other rule. */
static const char *const tree_free_roots[] = {"tests", "bench", "cmake"};
enum { TREE_FREE_ROOT_COUNT = sizeof(tree_free_roots) / sizeof(tree_free_roots[0]) };

/* No closing colon, so "**Public functions** (compile/include/yaml/index.h):" counts as stating the field. */
static const char *const tree_readme_fields[] = {
    "**Purpose",          "**Tier",       "**Owns", "**Lifetime", "**Threads", "**Public functions",
    "**May be called by", "**Depends on",
};
enum { TREE_README_FIELD_COUNT = sizeof(tree_readme_fields) / sizeof(tree_readme_fields[0]) };

/* Other languages' source and headers, and GAS assembly, which would be SIMD outside asm/. */
static const char *const tree_foreign_extensions[] = {".cpp", ".cc",  ".cxx", ".C", ".hpp",
                                                      ".hh",  ".hxx", ".H",   ".s", ".S"};
enum { TREE_FOREIGN_COUNT = sizeof(tree_foreign_extensions) / sizeof(tree_foreign_extensions[0]) };

static const char *tree_join_path(lint_arena_t *arena, const char *directory, const char *name) {
    if (directory[0] == '\0') {
        return lint_arena_text(arena, name, strlen(name));
    }
    const char *with_slash = lint_arena_join(arena, directory, "/");
    return lint_arena_join(arena, with_slash, name);
}

static const char *tree_absolute(const tree_t *tree, const char *relative) {
    const char *with_slash = lint_arena_join(tree->arena_file, tree->root, "/");
    return lint_arena_join(tree->arena_file, with_slash, relative);
}

static bool tree_in_first_component(const char *directory, const char *name) {
    const uint64_t length = strlen(name);
    return strncmp(directory, name, length) == 0 && (directory[length] == '\0' || directory[length] == '/');
}

static const tree_root_t *tree_find_module_root(const char *directory) {
    for (uint32_t i = 0; i < TREE_MODULE_ROOT_COUNT; i++) {
        if (tree_in_first_component(directory, tree_module_roots[i].name)) {
            return &tree_module_roots[i];
        }
    }
    return nullptr;
}

static bool tree_in_free_root(const char *directory) {
    for (uint32_t i = 0; i < TREE_FREE_ROOT_COUNT; i++) {
        if (tree_in_first_component(directory, tree_free_roots[i])) {
            return true;
        }
    }
    return false;
}

/* Where the module path starts in <root>/src/... or <root>/include/..., or nullptr for anything else. */
static const char *tree_module_part(const char *directory, const tree_root_t *root, const char *section) {
    const uint64_t root_length = strlen(root->name);
    const uint64_t section_length = strlen(section);
    if (strncmp(directory + root_length, section, section_length) != 0) {
        return nullptr;
    }
    const char *rest = directory + root_length + section_length;
    return rest[0] != '\0' ? rest : nullptr;
}

static uint32_t tree_count_lines(const char *text, uint64_t length) {
    uint32_t lines = 0;
    for (uint64_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            lines++;
        }
    }
    if (length > 0 && text[length - 1u] != '\n') {
        lines++;
    }
    return lines;
}

/* Reads one code file and runs everything that looks inside it. */
static void tree_check_file_contents(tree_t *tree, const char *path, tree_kind_t kind) {
    const uint64_t mark = tree->arena_file->used;
    lint_file_t file = {.root = tree->root, .path = path, .arena_file = tree->arena_file, .report = tree->report};
    if (lint_read_file(tree->arena_file, tree->report, tree_absolute(tree, path), &file.text, &file.length)) {
        tree->file_count++;
        const uint32_t lines = tree_count_lines(file.text, file.length);
        if (lines > TREE_LINE_LIMIT) {
            lint_breach(tree->report, path, 0, "line_limit",
                        "%u lines; the limit is %u, so split it by making sub-directories", lines, TREE_LINE_LIMIT);
        }
        if (kind == TREE_KIND_ASM || kind == TREE_KIND_INC) {
            lint_rules_asm_check(&file, kind == TREE_KIND_INC);
        } else {
            const uint8_t language =
                (kind == TREE_KIND_CU || kind == TREE_KIND_HU) ? LINT_LANGUAGE_CUDA : LINT_LANGUAGE_C;
            lint_lexed_t lexed;
            lint_lex(tree->arena_file, file.text, file.length, &lexed);
            lint_rules_c_check(&file, &lexed, language, tree->cuda_graph);
            lint_rules_include_check(&file, &lexed, language);
        }
    }
    tree->arena_file->used = mark;
}

/* Build logic is code too: a line in any of these files can undo a flag the rest of the lint relies on. */
static void tree_check_cmake_file(tree_t *tree, const char *path) {
    const uint64_t mark = tree->arena_file->used;
    lint_file_t file = {.root = tree->root, .path = path, .arena_file = tree->arena_file, .report = tree->report};
    if (lint_read_file(tree->arena_file, tree->report, tree_absolute(tree, path), &file.text, &file.length)) {
        lint_rules_cmake_check(&file);
    }
    tree->arena_file->used = mark;
}

/* Section 5: is this kind of file allowed in this directory? */
static void tree_check_location(const tree_t *tree, const char *directory, const char *path, tree_kind_t kind) {
    if (kind == TREE_KIND_H && tree_in_first_component(directory, "cuda")) {
        lint_breach(tree->report, path, 0, "cuda_header_ext", ".hu is the only CUDA header extension");
        return;
    }
    if (kind == TREE_KIND_HU && !tree_in_first_component(directory, "cuda") && !tree_in_free_root(directory)) {
        lint_breach(tree->report, path, 0, "cuda_header_ext", ".hu files live under cuda/include/");
        return;
    }
    if (tree_in_free_root(directory)) {
        return;
    }
    const tree_root_t *root = tree_find_module_root(directory);
    if (root == nullptr) {
        lint_breach(tree->report, path, 0, "file_location",
                    "code lives in run/, compile/, cuda/, asm/, tests/, bench/ or cmake/ (plans/REPO_STRUCTURE.md)");
        return;
    }
    if (kind != root->source && kind != root->header) {
        lint_breach(tree->report, path, 0, "file_location", "this language does not belong under %s/", root->name);
        return;
    }
    const char *in_src = tree_module_part(directory, root, "/src/");
    const char *in_include = tree_module_part(directory, root, "/include/");
    if (in_src == nullptr && in_include == nullptr) {
        lint_breach(tree->report, path, 0, "file_location", "%s/ code lives in %s/src/<path>/ or %s/include/<path>/",
                    root->name, root->name, root->name);
    } else if ((in_src != nullptr && kind == root->header) || (in_include != nullptr && kind == root->source)) {
        lint_breach(tree->report, path, 0, "header_location",
                    "sources live in %s/src/ and headers in the matching %s/include/ path", root->name, root->name);
    }
    const char *module = in_src != nullptr ? in_src : in_include;
    if (module != nullptr && strcmp(root->name, "run") == 0) {
        const char *slash = strchr(module, '/');
        const uint64_t system_length = slash != nullptr ? (uint64_t)(slash - module) : strlen(module);
        if (lint_rules_include_system_rank(module, system_length) == 0) {
            lint_breach(
                tree->report, path, 0, "unknown_system",
                "run/ system '%.*s' is not in the dependency diagram; add it to plans/REPO_STRUCTURE.md section "
                "11 and to compile/src/standards_lint/rules_include/",
                (int32_t)system_length, module);
        }
    }
}

/* Section 5 and REPO_STRUCTURE section 3: what a module's README must state. */
static void tree_check_readme_contents(const tree_t *tree, const char *directory, const char *readme_path) {
    const uint64_t mark = tree->arena_file->used;
    const char *text = nullptr;
    uint64_t length = 0;
    if (lint_read_file(tree->arena_file, tree->report, tree_absolute(tree, readme_path), &text, &length)) {
        for (uint32_t i = 0; i < TREE_README_FIELD_COUNT; i++) {
            if (strstr(text, tree_readme_fields[i]) == nullptr) {
                lint_breach(tree->report, readme_path, 0, "readme_fields", "does not state %s", tree_readme_fields[i]);
            }
        }
        const char *probe = tree_join_path(tree->arena_file, directory, "index.c");
        const lint_tier_t tier = lint_tier_of_path(probe);
        const char *expected = nullptr;
        switch (tier) {
        case LINT_TIER_ROOT:
            expected = "Root";
            break;
        case LINT_TIER_OWNER:
            expected = "Owner";
            break;
        case LINT_TIER_ARENA:
            expected = "Arena";
            break;
        case LINT_TIER_BORROW:
            expected = "Borrow";
            break;
        default:
            break;
        }
        const char *tier_field = strstr(text, "**Tier");
        if (expected != nullptr && tier_field != nullptr) {
            const char *tier_end = strchr(tier_field, '\n');
            const uint64_t tier_length = tier_end != nullptr ? (uint64_t)(tier_end - tier_field) : strlen(tier_field);
            if (memmem(tier_field, tier_length, expected, strlen(expected)) == nullptr) {
                lint_breach(tree->report, readme_path, 0, "readme_tier",
                            "this path's depth makes it the %s tier, and its README must say so", expected);
            }
        }
    }
    tree->arena_file->used = mark;
}

static int32_t tree_compare_names(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

typedef struct tree_listing_t {
    const char **files;
    const char **directories;
    uint32_t file_count;
    uint32_t directory_count;
} tree_listing_t;
static_assert(sizeof(tree_listing_t) == 24);

/* Sorted, so a report reads the same on every run. Symbolic links are not followed: they could form a loop, and what
 * they point at is either inside the walk already or not ours. */
static bool tree_list_directory(tree_t *tree, const char *directory, tree_listing_t *out) {
    const char *absolute = directory[0] == '\0' ? tree->root : tree_absolute(tree, directory);
    DIR *handle = opendir(absolute);
    if (handle == nullptr) {
        lint_error(tree->report, "cannot open directory %s", absolute);
        return false;
    }
    out->files = lint_arena_alloc(tree->arena_file, TREE_MAX_ENTRIES * sizeof(const char *));
    out->directories = lint_arena_alloc(tree->arena_file, TREE_MAX_ENTRIES * sizeof(const char *));
    out->file_count = 0;
    out->directory_count = 0;
    bool ok = true;
    const struct dirent *entry = readdir(handle);
    while (entry != nullptr) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            const char *full = tree_join_path(tree->arena_file, absolute, name);
            struct stat info;
            if (lstat(full, &info) != 0) {
                lint_error(tree->report, "cannot stat %s", full);
                ok = false;
            } else if (out->file_count >= TREE_MAX_ENTRIES || out->directory_count >= TREE_MAX_ENTRIES) {
                lint_error(tree->report, "%s has too many entries", absolute);
                ok = false;
                break;
            } else if (S_ISDIR(info.st_mode)) {
                out->directories[out->directory_count] = lint_arena_text(tree->arena_walk, name, strlen(name));
                out->directory_count++;
            } else if (S_ISREG(info.st_mode)) {
                out->files[out->file_count] = lint_arena_text(tree->arena_file, name, strlen(name));
                out->file_count++;
            }
        }
        entry = readdir(handle);
    }
    closedir(handle);
    qsort(out->files, out->file_count, sizeof(const char *), tree_compare_names);
    qsort(out->directories, out->directory_count, sizeof(const char *), tree_compare_names);
    return ok;
}

static bool tree_skip_directory(const char *parent, const char *name) {
    /* Generated output, agent worktrees and git's own data are not our source. */
    if (parent[0] == '\0' &&
        (strcmp(name, "build") == 0 || strcmp(name, ".claude") == 0 || strcmp(name, ".git") == 0)) {
        return true;
    }
    /* Under tests/, fixtures break the rules on purpose. Stage 02.1's test discovery skips them too. */
    return strcmp(name, "fixtures") == 0 && tree_in_first_component(parent, "tests");
}

static void tree_check_directory(tree_t *tree, const char *directory, const tree_listing_t *listing) {
    uint32_t code_count = 0;
    uint32_t source_count = 0;
    uint32_t header_count = 0;
    bool has_readme = false;
    const char *first_code = nullptr;

    for (uint32_t i = 0; i < listing->file_count; i++) {
        const char *name = listing->files[i];
        const tree_kind_t kind = tree_kind_of(name);
        if (strcmp(name, "README.md") == 0) {
            has_readme = true;
        }
        if (strcmp(name, "CMakeLists.txt") == 0 || lint_ends_with(name, ".cmake")) {
            tree_check_cmake_file(tree, tree_join_path(tree->arena_file, directory, name));
        }
        if (kind == TREE_KIND_OTHER) {
            /* Not compiled by anything and not read by any rule, so it would otherwise pass in silence. */
            for (uint32_t f = 0; f < TREE_FOREIGN_COUNT; f++) {
                if (lint_ends_with(name, tree_foreign_extensions[f])) {
                    lint_breach(tree->report, tree_join_path(tree->arena_file, directory, name), 0, "foreign_source",
                                "this project is C, CUDA and NASM only (plans/CODING_STANDARD.md section 5)");
                }
            }
            continue;
        }
        const char *path = tree_join_path(tree->arena_file, directory, name);
        first_code = first_code != nullptr ? first_code : path;
        code_count++;
        source_count += tree_kind_is_source(kind) ? 1u : 0u;
        header_count += tree_kind_is_header(kind) ? 1u : 0u;
        const char *dot = strrchr(name, '.');
        if (!lint_text_equal(name, (uint64_t)(dot - name), "index")) {
            lint_breach(tree->report, path, 0, "file_name",
                        "every source file is named index.<extension> in its own directory");
        }
        tree_check_location(tree, directory, path, kind);
        tree_check_file_contents(tree, path, kind);
    }

    if (code_count == 0) {
        return;
    }
    if (source_count > 1u || header_count > 1u) {
        lint_breach(tree->report, first_code, 0, "one_module",
                    "a directory holds one module: one source and one header at most");
    }

    const tree_root_t *root = tree_find_module_root(directory);
    const char *in_src = root != nullptr ? tree_module_part(directory, root, "/src/") : nullptr;
    const char *in_include = root != nullptr ? tree_module_part(directory, root, "/include/") : nullptr;

    if (in_src != nullptr) {
        /* Section 5: "A source directory holds only one index.* pair member and a README.md." */
        for (uint32_t i = 0; i < listing->file_count; i++) {
            const char *name = listing->files[i];
            if (strcmp(name, "README.md") != 0 && tree_kind_of(name) == TREE_KIND_OTHER) {
                lint_breach(tree->report, tree_join_path(tree->arena_file, directory, name), 0, "stray_file",
                            "a source directory holds only its index file and README.md");
            }
        }
    }

    const char *readme_path = tree_join_path(tree->arena_file, directory, "README.md");
    if (!has_readme && in_include != nullptr) {
        /* A header's README is the one in the matching source directory, when that exists. */
        const char *twin = lint_arena_join(tree->arena_file, root->name, "/src/");
        twin = lint_arena_join(tree->arena_file, twin, in_include);
        twin = lint_arena_join(tree->arena_file, twin, "/README.md");
        has_readme = lint_path_exists(tree_absolute(tree, twin));
    }
    if (!has_readme) {
        lint_breach(tree->report, readme_path, 0, "readme_missing", "every directory with code has a README.md");
    } else if (in_src != nullptr) {
        tree_check_readme_contents(tree, directory, readme_path);
    }
}

uint64_t lint_rules_tree_check(const char *root, lint_arena_t *arena_walk, lint_arena_t *arena_file,
                               lint_report_t *report) {
    lint_graph_t cuda_graph;
    lint_graph_begin(&cuda_graph, arena_walk);
    tree_t tree = {.root = root,
                   .arena_walk = arena_walk,
                   .arena_file = arena_file,
                   .report = report,
                   .cuda_graph = &cuda_graph,
                   .file_count = 0};

    /* An explicit stack, since recursion is banned here too. */
    const char **pending = lint_arena_alloc(arena_walk, TREE_MAX_DIRECTORIES * sizeof(const char *));
    uint32_t pending_count = 1;
    pending[0] = "";

    while (pending_count > 0) {
        pending_count--;
        const char *directory = pending[pending_count];
        const uint64_t mark = arena_file->used;
        tree_listing_t listing;
        if (tree_list_directory(&tree, directory, &listing)) {
            tree_check_directory(&tree, directory, &listing);
            /* Pushed in reverse, so directories pop in sorted order. */
            for (uint32_t i = listing.directory_count; i > 0; i--) {
                const char *name = listing.directories[i - 1u];
                if (tree_skip_directory(directory, name)) {
                    continue;
                }
                if (pending_count >= TREE_MAX_DIRECTORIES) {
                    lint_error(report, "more than %u directories", TREE_MAX_DIRECTORIES);
                    return tree.file_count;
                }
                pending[pending_count] = tree_join_path(arena_walk, directory, name);
                pending_count++;
            }
        }
        arena_file->used = mark;
    }

    lint_graph_report_cycles(&cuda_graph, arena_file, report, "recursion_cuda");
    return tree.file_count;
}
