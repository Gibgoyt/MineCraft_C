/* See README.md. */
#include "standards_lint/indirect/index.h"

#include "standards_lint/lexer/index.h"
#include "yaml/index.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    INDIRECT_MAX_ENTRIES = 4096,
    INDIRECT_MAX_TARGETS = 1u << 16,
    INDIRECT_MAX_WRAPPERS = 256,
    INDIRECT_MAX_PASSES = 16,
    INDIRECT_MAX_ARGS = 64,
};

/* One reviewed caller from the list. */
typedef struct indirect_entry_t {
    const char *file;   /* where the function is written, relative to the list's root: a header for a static inline */
    const char *caller; /* the function's own name, without GCC's "unit:" prefix for a static */
    uint32_t line;      /* in the list */
    uint8_t dispatcher; /* under job_dispatchers: its targets are the job functions */
    uint8_t defined;    /* this preset compiled it */
    uint8_t calls;      /* and found it calling through a pointer */
    uint8_t _pad0;
} indirect_entry_t;
static_assert(sizeof(indirect_entry_t) == 24);

/* What a job function argument turned out to be. */
typedef enum indirect_problem_t : uint8_t {
    INDIRECT_PROBLEM_NONE = 0,       /* a name, to look up */
    INDIRECT_PROBLEM_NOT_A_NAME = 1, /* name holds the token that is not one */
    INDIRECT_PROBLEM_FORWARDER = 2,  /* a function that is not static hands its parameter on */
} indirect_problem_t;

/* A function handed to the job system, by name. Judged only once the function that hands it over is known to be
 * compiled in this preset: a token scan also sees code that an #if leaves out. */
typedef struct indirect_target_t {
    const char *name;   /* as written */
    const char *unit;   /* the .ci file's source */
    const char *within; /* the function the call to the job system is in */
    uint32_t line;
    uint32_t node; /* LINT_GRAPH_NONE until resolved */
    indirect_problem_t problem;
    uint8_t _pad0[7];
} indirect_target_t;
static_assert(sizeof(indirect_target_t) == 40);

/* A static function that passes one of its job_function_t parameters on: what its callers give there is a job too. */
typedef struct indirect_wrapper_t {
    const lint_token_t *name;
    uint32_t parameter;
    uint32_t _pad0;
} indirect_wrapper_t;
static_assert(sizeof(indirect_wrapper_t) == 16);

typedef struct indirect_t {
    lint_graph_t *graph;
    lint_arena_t *arena_walk;
    lint_arena_t *arena_file;
    lint_report_t *report;
    const char *allowlist_path;
    const char *root; /* canonical; nullptr without a list */
    indirect_entry_t *entries;
    indirect_target_t *targets;
    uint32_t entry_count;
    uint32_t target_count;
} indirect_t;
static_assert(sizeof(indirect_t) == 72);

/* ---- The list ------------------------------------------------------------------------------------------------------ */

static const char *indirect_yaml_text(indirect_t *state, const yaml_document_t *document, const yaml_node_t *map,
                                      const char *key, bool required) {
    const yaml_node_t *value = yaml_map_get(document, map, key);
    if (value == nullptr || value->kind != YAML_KIND_SCALAR || value->text_length == 0) {
        if (required) {
            lint_error(state->report, "%s:%u: an entry needs a non-empty '%s'", state->allowlist_path, map->line, key);
        }
        return nullptr;
    }
    return lint_arena_text(state->arena_walk, (const char *)value->borrowed_text, value->text_length);
}

static void indirect_read_entries(indirect_t *state, const yaml_document_t *document, const yaml_node_t *list,
                                  bool dispatcher) {
    if (list == nullptr) {
        return;
    }
    if (list->kind != YAML_KIND_SEQ) {
        lint_error(state->report, "%s:%u: expected a list of entries", state->allowlist_path, list->line);
        return;
    }
    for (const yaml_node_t *item = yaml_first_child(document, list); item != nullptr;
         item = yaml_next_sibling(document, item)) {
        if (item->kind != YAML_KIND_MAP) {
            lint_error(state->report, "%s:%u: an entry is a mapping", state->allowlist_path, item->line);
            continue;
        }
        for (const yaml_node_t *field = yaml_first_child(document, item); field != nullptr;
             field = yaml_next_sibling(document, field)) {
            const bool known =
                lint_text_equal((const char *)field->borrowed_key, field->key_length, "file") ||
                lint_text_equal((const char *)field->borrowed_key, field->key_length, "caller") ||
                lint_text_equal((const char *)field->borrowed_key, field->key_length, "why") ||
                (!dispatcher && lint_text_equal((const char *)field->borrowed_key, field->key_length, "pointer"));
            if (!known) {
                lint_error(state->report, "%s:%u: unknown key '%.*s'", state->allowlist_path, field->line,
                           (int32_t)field->key_length, (const char *)field->borrowed_key);
            }
        }
        const char *file = indirect_yaml_text(state, document, item, "file", true);
        const char *caller = indirect_yaml_text(state, document, item, "caller", true);
        const bool has_why = indirect_yaml_text(state, document, item, "why", true) != nullptr;
        const bool has_pointer = dispatcher || indirect_yaml_text(state, document, item, "pointer", true) != nullptr;
        if (file == nullptr || caller == nullptr || !has_why || !has_pointer) {
            continue;
        }
        if (state->entry_count >= INDIRECT_MAX_ENTRIES) {
            lint_error(state->report, "%s: more than %u entries", state->allowlist_path, INDIRECT_MAX_ENTRIES);
            return;
        }
        for (uint32_t e = 0; e < state->entry_count; e++) {
            if (strcmp(state->entries[e].file, file) == 0 && strcmp(state->entries[e].caller, caller) == 0) {
                lint_error(state->report, "%s:%u: %s in %s is listed twice", state->allowlist_path, item->line, caller,
                           file);
            }
        }
        state->entries[state->entry_count] =
            (indirect_entry_t){.file = file, .caller = caller, .line = item->line, .dispatcher = dispatcher ? 1u : 0u};
        state->entry_count++;
    }
}

/* root: is the repository, relative to the list's own directory. */
static void indirect_read_allowlist(indirect_t *state) {
    const char *text = nullptr;
    uint64_t length = 0;
    if (!lint_read_file(state->arena_file, state->report, state->allowlist_path, &text, &length)) {
        return;
    }
    const yaml_storage_t storage = {
        .nodes = lint_arena_alloc(state->arena_file, (length + 1u) * sizeof(yaml_node_t)),
        .strings = lint_arena_alloc(state->arena_file, length + 1u),
        .node_capacity = (uint32_t)length + 1u,
        .string_capacity = (uint32_t)length + 1u,
    };
    yaml_document_t document;
    yaml_error_t error;
    if (!yaml_parse(&document, (const uint8_t *)text, length, storage, &error)) {
        lint_error(state->report, "%s:%u:%u: %s", state->allowlist_path, error.line, error.column, error.message);
        return;
    }
    const yaml_node_t *top = yaml_root(&document);
    const char *root = top != nullptr && top->kind == YAML_KIND_MAP
                           ? indirect_yaml_text(state, &document, top, "root", false)
                           : nullptr;
    if (root == nullptr) {
        lint_error(state->report, "%s: needs root: the repository, relative to this file", state->allowlist_path);
        return;
    }
    for (const yaml_node_t *field = yaml_first_child(&document, top); field != nullptr;
         field = yaml_next_sibling(&document, field)) {
        if (!lint_text_equal((const char *)field->borrowed_key, field->key_length, "root") &&
            !lint_text_equal((const char *)field->borrowed_key, field->key_length, "job_dispatchers") &&
            !lint_text_equal((const char *)field->borrowed_key, field->key_length, "indirect_calls")) {
            lint_error(state->report, "%s:%u: unknown key '%.*s'", state->allowlist_path, field->line,
                       (int32_t)field->key_length, (const char *)field->borrowed_key);
        }
    }
    const char *slash = strrchr(state->allowlist_path, '/');
    const char *directory = slash != nullptr ? lint_arena_text(state->arena_file, state->allowlist_path,
                                                               (uint64_t)(slash - state->allowlist_path))
                                             : ".";
    char canonical[PATH_MAX];
    const char *joined = lint_arena_join(state->arena_file, lint_arena_join(state->arena_file, directory, "/"), root);
    if (realpath(joined, canonical) == nullptr) {
        lint_error(state->report, "%s: root '%s' is not a directory", state->allowlist_path, root);
        return;
    }
    state->root = lint_arena_text(state->arena_walk, canonical, strlen(canonical));
    indirect_read_entries(state, &document, yaml_map_get(&document, top, "job_dispatchers"), true);
    indirect_read_entries(state, &document, yaml_map_get(&document, top, "indirect_calls"), false);
}

/* ---- The job functions --------------------------------------------------------------------------------------------- */

static bool indirect_is_submit(const lint_token_t *token) {
    return (token->length >= 9u && memcmp(token->text, "job_spawn", 9u) == 0) ||
           (token->length >= 10u && memcmp(token->text, "job_submit", 10u) == 0) ||
           (token->length >= 14u && memcmp(token->text, "job_try_submit", 14u) == 0);
}

static bool indirect_same_word(const lint_token_t *left, const lint_token_t *right) {
    return left->length == right->length && memcmp(left->text, right->text, left->length) == 0;
}

static bool indirect_is_opener(const lint_token_t *token) {
    return lint_token_is(token, "(") || lint_token_is(token, "[") || lint_token_is(token, "{");
}

static bool indirect_is_closer(const lint_token_t *token) {
    return lint_token_is(token, ")") || lint_token_is(token, "]") || lint_token_is(token, "}");
}

/* The top-level commas of the group opened at open: starts[k] is the first token of argument k, and the argument ends
 * where the next starts, less one. Returns the count, 0 for (), and sets *out_close to the index of the ')'. */
static uint32_t indirect_arguments(const lint_token_t *code, uint32_t count, uint32_t open, uint32_t *starts,
                                   uint32_t *out_close) {
    uint32_t depth = 0;
    uint32_t argument_count = 0;
    *out_close = count;
    for (uint32_t i = open; i < count; i++) {
        if (indirect_is_opener(&code[i])) {
            depth++;
            if (depth == 1u && i + 1u < count && !lint_token_is(&code[i + 1u], ")")) {
                starts[0] = i + 1u;
                argument_count = 1;
            }
        } else if (indirect_is_closer(&code[i])) {
            depth--;
            if (depth == 0) {
                *out_close = i;
                return argument_count;
            }
        } else if (depth == 1u && lint_token_is(&code[i], ",") && argument_count < INDIRECT_MAX_ARGS) {
            starts[argument_count] = i + 1u;
            argument_count++;
        }
    }
    return argument_count;
}

/* The function whose body is being walked: its name and its parameter list. */
typedef struct indirect_function_t {
    const lint_token_t *name;
    uint32_t open;  /* its '(' */
    uint32_t close; /* its ')' */
    uint8_t is_static;
    uint8_t _pad0[7];
} indirect_function_t;
static_assert(sizeof(indirect_function_t) == 24);

/* The index of the job_function_t parameter of function called word, or UINT32_MAX. */
static uint32_t indirect_job_parameter(const lint_token_t *code, uint32_t count, const indirect_function_t *function,
                                       const lint_token_t *word) {
    uint32_t starts[INDIRECT_MAX_ARGS];
    uint32_t close = 0;
    const uint32_t parameter_count = indirect_arguments(code, count, function->open, starts, &close);
    for (uint32_t p = 0; p < parameter_count; p++) {
        const uint32_t end = p + 1u < parameter_count ? starts[p + 1u] - 1u : close;
        bool typed = false;
        const lint_token_t *last = nullptr;
        for (uint32_t i = starts[p]; i < end; i++) {
            typed = typed || lint_token_is(&code[i], "job_function_t");
            if (code[i].kind == LINT_TOKEN_IDENT) {
                last = &code[i];
            }
        }
        if (typed && last != nullptr && indirect_same_word(last, word)) {
            return p;
        }
    }
    return UINT32_MAX;
}

typedef struct indirect_scan_t {
    indirect_t *state;
    const char *unit;
    const lint_token_t *code;
    indirect_wrapper_t *wrappers;
    uint32_t count;
    uint32_t wrapper_count;
    uint8_t found_wrapper; /* a new one this pass, so the pass must run again */
    uint8_t _pad0[7];
} indirect_scan_t;
static_assert(sizeof(indirect_scan_t) == 48);

/* The name GCC gives the function: the harness's TEST(name) { defines test_body_name. */
static const char *indirect_within(const indirect_scan_t *scan, const indirect_function_t *function) {
    lint_arena_t *arena = scan->state->arena_walk;
    const lint_token_t *word = &scan->code[function->open + 1u];
    if (lint_token_is(function->name, "TEST") && word->kind == LINT_TOKEN_IDENT) {
        return lint_arena_join(arena, "test_body_", lint_arena_text(arena, word->text, word->length));
    }
    return lint_arena_text(arena, function->name->text, function->name->length);
}

static void indirect_add_target(indirect_scan_t *scan, const indirect_function_t *function, const lint_token_t *word,
                                indirect_problem_t problem) {
    indirect_t *state = scan->state;
    if (state->target_count >= INDIRECT_MAX_TARGETS) {
        lint_error(state->report, "more than %u job functions", INDIRECT_MAX_TARGETS);
        return;
    }
    state->targets[state->target_count] = (indirect_target_t){
        .name = lint_arena_text(state->arena_walk, word->text, word->length),
        .unit = scan->unit,
        .within = indirect_within(scan, function),
        .line = word->line,
        .node = LINT_GRAPH_NONE,
        .problem = problem,
    };
    state->target_count++;
}

/* The argument code[first..end) is a job function. It must be a function's name, or a choice between names with ?:, or
 * a job_function_t parameter of a static function, whose own callers are then scanned in turn. */
static void indirect_take_argument(indirect_scan_t *scan, const indirect_function_t *function, uint32_t first,
                                   uint32_t end) {
    const lint_token_t *code = scan->code;
    for (uint32_t i = first; i < end; i++) {
        if (lint_token_is(&code[i], "?")) {
            first = i + 1u;
        }
    }
    for (uint32_t i = first; i < end; i++) {
        const lint_token_t *token = &code[i];
        if (lint_token_is(token, ":") || lint_token_is(token, "(") || lint_token_is(token, ")") ||
            lint_token_is(token, "&")) {
            continue;
        }
        if (token->kind != LINT_TOKEN_IDENT) {
            indirect_add_target(scan, function, token, INDIRECT_PROBLEM_NOT_A_NAME);
            return;
        }
        if (lint_token_is(token, "job_function_t")) {
            continue; /* a cast */
        }
        const uint32_t parameter = indirect_job_parameter(code, scan->count, function, token);
        if (parameter == UINT32_MAX) {
            indirect_add_target(scan, function, token, INDIRECT_PROBLEM_NONE);
            continue;
        }
        if (indirect_is_submit(function->name)) {
            continue; /* the job system's own entry points, handing on what their callers gave */
        }
        if (function->is_static == 0) {
            indirect_add_target(scan, function, token, INDIRECT_PROBLEM_FORWARDER);
            continue;
        }
        bool known = false;
        for (uint32_t w = 0; w < scan->wrapper_count && !known; w++) {
            known =
                indirect_same_word(scan->wrappers[w].name, function->name) && scan->wrappers[w].parameter == parameter;
        }
        if (!known && scan->wrapper_count < INDIRECT_MAX_WRAPPERS) {
            scan->wrappers[scan->wrapper_count] =
                (indirect_wrapper_t){.name = function->name, .parameter = parameter, ._pad0 = 0};
            scan->wrapper_count++;
            scan->found_wrapper = 1;
        }
    }
}

/* A call at code[i] to the job system or to a wrapper: which argument is the job function. */
static uint32_t indirect_function_argument(const indirect_scan_t *scan, uint32_t i, uint32_t argument_count) {
    if (indirect_is_submit(&scan->code[i])) {
        /* Every entry point takes (..., job_function_t function, void *borrowed_argument). */
        return argument_count >= 2u ? argument_count - 2u : UINT32_MAX;
    }
    for (uint32_t w = 0; w < scan->wrapper_count; w++) {
        if (indirect_same_word(scan->wrappers[w].name, &scan->code[i])) {
            return scan->wrappers[w].parameter < argument_count ? scan->wrappers[w].parameter : UINT32_MAX;
        }
    }
    return UINT32_MAX;
}

static bool indirect_is_wrapper(const indirect_scan_t *scan, const lint_token_t *token) {
    for (uint32_t w = 0; w < scan->wrapper_count; w++) {
        if (indirect_same_word(scan->wrappers[w].name, token)) {
            return true;
        }
    }
    return false;
}

/* One walk over the file. Function bodies are found as at file scope: name ( ... ) then {. */
static void indirect_scan_pass(indirect_scan_t *scan) {
    const lint_token_t *code = scan->code;
    const uint32_t count = scan->count;
    uint32_t brace_depth = 0;
    uint32_t paren_depth = 0;
    int64_t candidate = -1;
    int64_t candidate_close = -1;
    uint32_t declaration_start = 0;
    indirect_function_t function = {0};
    bool in_function = false;
    for (uint32_t i = 0; i < count; i++) {
        const lint_token_t *token = &code[i];
        const lint_token_t *previous = i > 0 ? &code[i - 1u] : nullptr;
        if (lint_token_is(token, "(")) {
            if (brace_depth == 0 && paren_depth == 0 && candidate < 0 && previous != nullptr &&
                previous->kind == LINT_TOKEN_IDENT && !lint_token_is(previous, "__attribute__") &&
                !lint_token_is(previous, "alignas") && !lint_token_is(previous, "_Alignas") &&
                !lint_token_is(previous, "typeof") && !lint_token_is(previous, "_Atomic")) {
                candidate = (int64_t)i - 1;
            }
            paren_depth++;
        } else if (lint_token_is(token, ")")) {
            paren_depth = paren_depth > 0 ? paren_depth - 1u : 0u;
            if (brace_depth == 0 && paren_depth == 0 && candidate >= 0 && candidate_close < 0) {
                candidate_close = i;
            }
        } else if (lint_token_is(token, ";") && brace_depth == 0 && paren_depth == 0) {
            candidate = -1;
            candidate_close = -1;
            declaration_start = i + 1u;
        } else if (lint_token_is(token, "{")) {
            if (brace_depth == 0 && candidate >= 0 && candidate_close >= 0 && previous != nullptr &&
                lint_token_is(previous, ")")) {
                bool is_static = false;
                for (uint32_t k = declaration_start; k < (uint32_t)candidate; k++) {
                    is_static = is_static || lint_token_is(&code[k], "static");
                }
                function = (indirect_function_t){.name = &code[candidate],
                                                 .open = (uint32_t)candidate + 1u,
                                                 .close = (uint32_t)candidate_close,
                                                 .is_static = is_static ? 1u : 0u};
                in_function = true;
            }
            candidate = -1;
            candidate_close = -1;
            brace_depth++;
        } else if (lint_token_is(token, "}")) {
            brace_depth = brace_depth > 0 ? brace_depth - 1u : 0u;
            if (brace_depth == 0) {
                in_function = false;
                declaration_start = i + 1u;
            }
        } else if (in_function && token->kind == LINT_TOKEN_IDENT && i + 1u < count &&
                   lint_token_is(&code[i + 1u], "(") &&
                   (indirect_is_submit(token) || indirect_is_wrapper(scan, token)) &&
                   !(previous != nullptr && (previous->kind == LINT_TOKEN_IDENT || lint_token_is(previous, "*")) &&
                     !lint_token_is(previous, "return"))) {
            uint32_t starts[INDIRECT_MAX_ARGS];
            uint32_t close = 0;
            const uint32_t argument_count = indirect_arguments(code, count, i + 1u, starts, &close);
            const uint32_t which = indirect_function_argument(scan, i, argument_count);
            if (which != UINT32_MAX) {
                const uint32_t end = which + 1u < argument_count ? starts[which + 1u] - 1u : close;
                indirect_take_argument(scan, &function, starts[which], end);
            }
        }
    }
}

/* The file's tokens outside preprocessor lines, in the per-file arena. */
static lint_token_t *indirect_code(indirect_t *state, const char *path, uint32_t *out_count) {
    *out_count = 0;
    const char *text = nullptr;
    uint64_t length = 0;
    if (!lint_read_file(state->arena_file, state->report, path, &text, &length)) {
        return nullptr;
    }
    lint_lexed_t lexed;
    lint_lex(state->arena_file, text, length, &lexed);
    lint_token_t *code = lint_arena_alloc(state->arena_file, ((uint64_t)lexed.token_count + 1u) * sizeof(lint_token_t));
    for (uint32_t i = 0; i < lexed.token_count; i++) {
        if (lexed.tokens[i].in_directive == 0) {
            code[*out_count] = lexed.tokens[i];
            (*out_count)++;
        }
    }
    return code;
}

static void indirect_scan_unit(indirect_t *state, const char *unit) {
    const uint64_t mark = state->arena_file->used;
    uint32_t count = 0;
    const lint_token_t *code = indirect_code(state, unit, &count);
    if (code == nullptr) {
        return;
    }
    /* Wrappers found late make earlier calls to them count, so the walk repeats until it finds no new one. Only the
     * last walk's targets are kept. */
    indirect_scan_t scan = {
        .state = state,
        .unit = unit,
        .code = code,
        .wrappers = lint_arena_alloc(state->arena_file, INDIRECT_MAX_WRAPPERS * sizeof(indirect_wrapper_t)),
        .count = count,
        .wrapper_count = 0,
        .found_wrapper = 1,
    };
    const uint32_t first_target = state->target_count;
    for (uint32_t pass = 0; pass < INDIRECT_MAX_PASSES && scan.found_wrapper != 0; pass++) {
        state->target_count = first_target;
        scan.found_wrapper = 0;
        indirect_scan_pass(&scan);
    }
    state->arena_file->used = mark;
}

/* ---- Names as GCC writes them -------------------------------------------------------------------------------------- */

/* GCC names a static "unit:name" and anything else plain "name". A job function is looked for in that order. */
static uint32_t indirect_find_target(const indirect_t *state, const char *unit, const char *name) {
    const char *prefixed = lint_arena_join(state->arena_file, lint_arena_join(state->arena_file, unit, ":"), name);
    const uint32_t node = lint_graph_find(state->graph, prefixed, strlen(prefixed));
    if (node != LINT_GRAPH_NONE) {
        return node;
    }
    const uint32_t plain = lint_graph_find(state->graph, name, strlen(name));
    return plain != LINT_GRAPH_NONE && state->graph->nodes[plain].unit != nullptr ? plain : LINT_GRAPH_NONE;
}

/* A path as the list writes it: relative to its root, after symbolic links. In the per-file arena. */
static const char *indirect_relative(const indirect_t *state, const char *path) {
    char canonical[PATH_MAX];
    const char *found = realpath(path, canonical) != nullptr ? canonical : path;
    const uint64_t root_length = state->root != nullptr ? strlen(state->root) : 0u;
    if (state->root != nullptr && strncmp(found, state->root, root_length) == 0 && found[root_length] == '/') {
        found += root_length + 1u;
    }
    return lint_arena_text(state->arena_file, found, strlen(found));
}

/* The function's own name: GCC's name less any "unit:" prefix. */
static const char *indirect_short_name(const lint_graph_node_t *node) {
    const char *colon = strrchr(node->name, ':');
    return colon != nullptr ? colon + 1 : node->name;
}

/* Whether an entry is about this node: the same name, written in the same file. */
static bool indirect_entry_is(const indirect_t *state, const indirect_entry_t *entry, const lint_graph_node_t *node) {
    return node->source != nullptr && strcmp(entry->caller, indirect_short_name(node)) == 0 &&
           strcmp(entry->file, indirect_relative(state, node->source)) == 0;
}

/* ---- The check ----------------------------------------------------------------------------------------------------- */

static void indirect_resolve_targets(indirect_t *state) {
    const uint64_t mark = state->arena_file->used;
    for (uint32_t t = 0; t < state->target_count; t++) {
        indirect_target_t *target = &state->targets[t];
        const bool compiled = indirect_find_target(state, target->unit, target->within) != LINT_GRAPH_NONE;
        state->arena_file->used = mark;
        if (!compiled) {
            continue; /* code this preset's #if leaves out */
        }
        if (target->problem == INDIRECT_PROBLEM_NOT_A_NAME) {
            lint_breach(state->report, target->unit, target->line, "job_function_unresolved",
                        "'%s' in the function given to a job: pass the function by name, so the call graph can "
                        "follow it",
                        target->name);
            continue;
        }
        if (target->problem == INDIRECT_PROBLEM_FORWARDER) {
            lint_breach(state->report, target->unit, target->line, "job_function_unresolved",
                        "%s hands its job_function_t parameter to the job system, but only a static function's "
                        "callers can be followed; make it static or pass the function by name",
                        target->within);
            continue;
        }
        target->node = indirect_find_target(state, target->unit, target->name);
        state->arena_file->used = mark;
        if (target->node == LINT_GRAPH_NONE) {
            lint_breach(state->report, target->unit, target->line, "job_function_unresolved",
                        "%s is given to the job system, but no compiled function has that name; pass a function by "
                        "name, so the call graph can follow it",
                        target->name);
        }
    }
}

/* Marks which entries name a compiled function, and adds a dispatcher's edges to every job function. */
static void indirect_match_definitions(indirect_t *state) {
    lint_graph_t *graph = state->graph;
    const uint64_t mark = state->arena_file->used;
    const uint32_t node_count = graph->node_count; /* the edges below add no node, but stay clear of any */
    for (uint32_t n = 0; n < node_count; n++) {
        for (uint32_t e = 0; e < state->entry_count; e++) {
            indirect_entry_t *entry = &state->entries[e];
            if (strcmp(entry->caller, indirect_short_name(&graph->nodes[n])) != 0 ||
                !indirect_entry_is(state, entry, &graph->nodes[n])) {
                state->arena_file->used = mark;
                continue;
            }
            state->arena_file->used = mark;
            entry->defined = 1;
            for (uint32_t t = 0; t < state->target_count && entry->dispatcher != 0; t++) {
                const indirect_target_t *target = &state->targets[t];
                if (target->node != LINT_GRAPH_NONE) {
                    const char *from = graph->nodes[n].name;
                    const char *to = graph->nodes[target->node].name;
                    lint_graph_add_edge(graph, from, strlen(from), to, strlen(to), target->unit, target->line);
                }
            }
        }
    }
}

static void indirect_report_unlisted(indirect_t *state) {
    const lint_graph_t *graph = state->graph;
    const uint64_t mark = state->arena_file->used;
    const char *list_name =
        state->allowlist_path != nullptr ? state->allowlist_path : "compile/src/standards_lint/indirect_calls.yaml";
    for (uint32_t n = 0; n < graph->node_count; n++) {
        const lint_graph_node_t *node = &graph->nodes[n];
        if (node->indirect_where == nullptr) {
            continue;
        }
        bool listed = false;
        for (uint32_t e = 0; e < state->entry_count; e++) {
            indirect_entry_t *entry = &state->entries[e];
            if (indirect_entry_is(state, entry, node)) {
                entry->calls = 1;
                listed = true;
            }
            state->arena_file->used = mark;
        }
        if (!listed) {
            const char *file = node->source != nullptr ? indirect_relative(state, node->source) : "(no source)";
            lint_breach(state->report, node->indirect_where, 0, "indirect_call_unlisted",
                        "%s in %s calls through a function pointer, which the call graph cannot follow; list it in %s "
                        "with the pointer's type and why no target can call back into it",
                        indirect_short_name(node), file, list_name);
            state->arena_file->used = mark;
        }
    }
}

/* An entry whose function is not compiled in this preset, such as a fuzzer's outside sanitize, is left alone. */
static void indirect_report_stale(indirect_t *state) {
    const uint64_t mark = state->arena_file->used;
    for (uint32_t e = 0; e < state->entry_count; e++) {
        const indirect_entry_t *entry = &state->entries[e];
        const char *path =
            lint_arena_join(state->arena_file, lint_arena_join(state->arena_file, state->root, "/"), entry->file);
        const char *why = nullptr;
        if (!lint_path_exists(path)) {
            why = "there is no such file";
        } else {
            uint32_t count = 0;
            const lint_token_t *code = indirect_code(state, path, &count);
            /* The harness's TEST(name) defines test_body_name, so that word never appears as written. */
            const char *word = entry->caller;
            if (lint_starts_with(word, "test_body_")) {
                word += strlen("test_body_");
            }
            bool named = false;
            for (uint32_t i = 0; i < count && !named; i++) {
                named = code[i].kind == LINT_TOKEN_IDENT && lint_text_equal(code[i].text, code[i].length, word);
            }
            if (!named) {
                why = "that file has no such function";
            } else if (entry->defined != 0 && entry->calls == 0) {
                why = "it no longer calls through a pointer";
            }
        }
        if (why != nullptr) {
            lint_breach(state->report, state->allowlist_path, entry->line, "indirect_call_stale",
                        "%s in %s: %s, so remove the entry", entry->caller, entry->file, why);
        }
        state->arena_file->used = mark;
    }
}

void lint_indirect_check(lint_graph_t *graph, const char *allowlist_path, lint_arena_t *arena_walk,
                         lint_arena_t *arena_file, lint_report_t *report) {
    indirect_t state = {
        .graph = graph,
        .arena_walk = arena_walk,
        .arena_file = arena_file,
        .report = report,
        .allowlist_path = allowlist_path,
        .root = nullptr,
        .entries = lint_arena_alloc(arena_walk, INDIRECT_MAX_ENTRIES * sizeof(indirect_entry_t)),
        .targets = lint_arena_alloc(arena_walk, INDIRECT_MAX_TARGETS * sizeof(indirect_target_t)),
        .entry_count = 0,
        .target_count = 0,
    };
    const uint64_t mark = arena_file->used;
    if (allowlist_path != nullptr) {
        indirect_read_allowlist(&state);
        arena_file->used = mark;
    }
    /* A source compiled into two targets is scanned once. */
    for (uint32_t u = 0; u < graph->unit_count; u++) {
        bool repeat = false;
        for (uint32_t v = 0; v < u && !repeat; v++) {
            repeat = strcmp(graph->units[u], graph->units[v]) == 0;
        }
        if (!repeat) {
            indirect_scan_unit(&state, graph->units[u]);
        }
    }
    indirect_resolve_targets(&state);
    indirect_match_definitions(&state);
    indirect_report_unlisted(&state);
    if (state.root != nullptr) {
        indirect_report_stale(&state);
    }
}
