/* See README.md. */
#include "standards_lint/rules_c/index.h"

#include "standards_lint/catalog/index.h"

#include <string.h>

/* ---- Identifiers ------------------------------------------------------------------------------------------------ */

static bool rules_token_starts(const lint_token_t *token, const char *prefix) {
    const uint64_t length = strlen(prefix);
    return token->length >= length && memcmp(token->text, prefix, length) == 0;
}

static bool rules_word_allowed_here(const lint_word_t *row, const char *path) {
    if (row->allowed_paths == nullptr) {
        return false;
    }
    for (uint32_t i = 0; row->allowed_paths[i] != nullptr; i++) {
        if (lint_path_matches(row->allowed_paths[i], path)) {
            return true;
        }
    }
    return false;
}

static bool rules_token_contains(const lint_token_t *token, const char *needle) {
    const uint64_t length = strlen(needle);
    for (uint64_t i = 0; i + length <= token->length; i++) {
        if (memcmp(token->text + i, needle, length) == 0) {
            return true;
        }
    }
    return false;
}

static void rules_check_tier(const lint_file_t *file, const lint_token_t *token, lint_tier_t tier) {
    const bool heap_call = lint_token_is(token, "heap_reserve") || lint_token_is(token, "heap_release");
    const bool arena_alloc = rules_token_contains(token, "arena_alloc");
    if (tier == LINT_TIER_ROOT && (heap_call || arena_alloc)) {
        lint_breach(file->report, file->path, token->line, "tier_root_alloc",
                    "%.*s: the Root tier, run/src/main/, declares app_t and calls _init; it may not allocate",
                    (int32_t)token->length, token->text);
    } else if (tier == LINT_TIER_ARENA && heap_call) {
        lint_breach(file->report, file->path, token->line, "tier_arena_heap",
                    "%.*s: depth 2 is the Arena tier; it allocates only from an arena_t given by its parent",
                    (int32_t)token->length, token->text);
    } else if (tier == LINT_TIER_BORROW && (heap_call || arena_alloc)) {
        lint_breach(file->report, file->path, token->line, "tier_borrow_alloc",
                    "%.*s: depth 3 and below is the Borrow tier; it may not allocate at all", (int32_t)token->length,
                    token->text);
    }
}

static void rules_check_identifiers(const lint_file_t *file, const lint_lexed_t *lexed, uint8_t language) {
    const lint_tier_t tier = lint_tier_of_path(file->path);
    for (uint32_t i = 0; i < lexed->token_count; i++) {
        const lint_token_t *token = &lexed->tokens[i];
        if (token->kind != LINT_TOKEN_IDENT) {
            continue;
        }
        const bool member =
            i > 0 && (lint_token_is(&lexed->tokens[i - 1u], ".") || lint_token_is(&lexed->tokens[i - 1u], "->"));
        const lint_word_t *row = lint_catalog_find_word(token->text, token->length);
        if (row != nullptr && (row->languages & language) != 0 && !(row->member_ok != 0 && member) &&
            !rules_word_allowed_here(row, file->path)) {
            lint_breach(file->report, file->path, token->line, row->rule, "%s %s", row->word, row->why);
        }
        if (lint_token_is(token, "asm") || lint_token_is(token, "__asm__") || lint_token_is(token, "__asm")) {
            lint_breach(file->report, file->path, token->line, "inline_asm",
                        "inline assembly is banned; write a batch kernel in asm/ (plans/CODING_STANDARD.md section 7)");
        }
        if (rules_token_starts(token, "_mm_") || rules_token_starts(token, "_mm256_") ||
            rules_token_starts(token, "_mm512_") || rules_token_starts(token, "__m64") ||
            rules_token_starts(token, "__m128") || rules_token_starts(token, "__m256") ||
            rules_token_starts(token, "__m512") || rules_token_starts(token, "__builtin_ia32_")) {
            lint_breach(file->report, file->path, token->line, "intrinsic",
                        "%.*s: intrinsics are banned; all SIMD lives in asm/ (plans/CODING_STANDARD.md section 7)",
                        (int32_t)token->length, token->text);
        }
        if (rules_token_starts(token, "int_fast") || rules_token_starts(token, "uint_fast") ||
            rules_token_starts(token, "int_least") || rules_token_starts(token, "uint_least")) {
            lint_breach(file->report, file->path, token->line, "int_fast_least",
                        "%.*s says nothing about its size; use the sized types (plans/CODING_STANDARD.md section 2)",
                        (int32_t)token->length, token->text);
        }
        if (lint_token_is(token, "__builtin_shuffle") || lint_token_is(token, "__builtin_shufflevector") ||
            lint_token_is(token, "__builtin_convertvector") || lint_token_is(token, "__vector") ||
            lint_token_is(token, "vector_size") || lint_token_is(token, "__vector_size__")) {
            lint_breach(file->report, file->path, token->line, "vector_extension",
                        "%.*s: GCC vector extensions are banned; all SIMD lives in asm/", (int32_t)token->length,
                        token->text);
        }
        if (tier != LINT_TIER_NONE) {
            rules_check_tier(file, token, tier);
        }
    }
}

/* ---- Attributes and pragmas ------------------------------------------------------------------------------------- */

/* Only #pragma once is allowed. Anything else is either a named breach or can switch a check off:
 * "#pragma GCC diagnostic ignored" would defeat -Werror=padded and -Wframe-larger-than= for the rest of the file. */
static void rules_check_pragma(const lint_file_t *file, const lint_lexed_t *lexed, uint32_t i) {
    const lint_token_t *token = &lexed->tokens[i];
    const lint_token_t *first = i + 1u < lexed->token_count && lexed->tokens[i + 1u].in_directive != 0 &&
                                        lexed->tokens[i + 1u].line == token->line
                                    ? &lexed->tokens[i + 1u]
                                    : nullptr;
    const lint_token_t *second = first != nullptr && i + 2u < lexed->token_count &&
                                         lexed->tokens[i + 2u].in_directive != 0 &&
                                         lexed->tokens[i + 2u].line == token->line
                                     ? &lexed->tokens[i + 2u]
                                     : nullptr;
    if (first != nullptr && lint_token_is(first, "once")) {
        return;
    }
    if (first != nullptr && lint_token_is(first, "pack")) {
        lint_breach(file->report, file->path, token->line, "pragma_pack",
                    "#pragma pack is banned; write the padding out (plans/CODING_STANDARD.md section 3)");
        return;
    }
    if (second != nullptr &&
        ((lint_token_is(first, "GCC") && (lint_token_is(second, "target") || lint_token_is(second, "optimize") ||
                                          lint_token_is(second, "ivdep") || lint_token_is(second, "unroll"))) ||
         (lint_token_is(first, "omp") && lint_token_is(second, "simd")))) {
        lint_breach(file->report, file->path, token->line, "simd_target",
                    "#pragma %.*s %.*s changes code generation outside the build flags", (int32_t)first->length,
                    first->text, (int32_t)second->length, second->text);
        return;
    }
    lint_breach(file->report, file->path, token->line, "pragma_other",
                "only #pragma once is allowed; other pragmas can switch off the checks the build relies on");
}

/* <% %> <: :> %: are { } [ ] #, and a %:pragma or %:include is not a directive to the lexer. They have no use here. */
static void rules_check_digraphs(const lint_file_t *file, const lint_lexed_t *lexed) {
    for (uint32_t i = 0; i + 1u < lexed->token_count; i++) {
        const lint_token_t *token = &lexed->tokens[i];
        const lint_token_t *next = &lexed->tokens[i + 1u];
        if (token->kind != LINT_TOKEN_PUNCT || next->kind != LINT_TOKEN_PUNCT ||
            token->text + token->length != next->text) {
            continue;
        }
        const char a = token->text[token->length - 1u];
        const char b = next->text[0];
        if ((a == '<' && (b == '%' || b == ':')) || (a == '%' && (b == '>' || b == ':')) || (a == ':' && b == '>')) {
            lint_breach(file->report, file->path, token->line, "digraph", "digraphs are banned; write the plain form");
        }
    }
}

/* A backslash and a newline inside a word join it back together for the compiler, so "in\<newline>t" is int. */
static void rules_check_line_splices(const lint_file_t *file) {
    uint32_t line = 1;
    for (uint64_t i = 0; i < file->length; i++) {
        if (file->text[i] == '\n') {
            line++;
        }
        if (i == 0 || i + 2u >= file->length || file->text[i] != '\\' || file->text[i + 1u] != '\n') {
            continue;
        }
        const char before = file->text[i - 1u];
        const char after = file->text[i + 2u];
        const bool before_word = (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
                                 (before >= '0' && before <= '9') || before == '_';
        const bool after_word = (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') ||
                                (after >= '0' && after <= '9') || after == '_';
        if (before_word && after_word) {
            lint_breach(
                file->report, file->path, line, "line_splice_in_token",
                "a backslash-newline splits a word; the lint would read two words where the compiler reads one");
        }
    }
}

static void rules_check_attributes(const lint_file_t *file, const lint_lexed_t *lexed) {
    for (uint32_t i = 0; i < lexed->token_count; i++) {
        const lint_token_t *token = &lexed->tokens[i];
        if (token->kind != LINT_TOKEN_IDENT) {
            continue;
        }
        const bool attribute_word = lint_token_is(token, "packed") || lint_token_is(token, "__packed__") ||
                                    lint_token_is(token, "target") || lint_token_is(token, "__target__") ||
                                    lint_token_is(token, "optimize") || lint_token_is(token, "__optimize__");
        const bool in_attribute_list =
            attribute_word && i >= 1u &&
            (lint_token_is(&lexed->tokens[i - 1u], "(") || lint_token_is(&lexed->tokens[i - 1u], ",") ||
             lint_token_is(&lexed->tokens[i - 1u], "::"));
        if (in_attribute_list) {
            /* Walk back to what opened this list. A local named packed in a call does not count; only an attribute does. */
            bool in_attribute = lint_token_is(&lexed->tokens[i - 1u], "::");
            for (uint32_t back = i; back > 0 && !in_attribute && i - back < 64u; back--) {
                const lint_token_t *before = &lexed->tokens[back - 1u];
                if (lint_token_is(before, "__attribute__") || lint_token_is(before, "__attribute")) {
                    in_attribute = true;
                }
                if (lint_token_is(before, ";") || lint_token_is(before, "{") || lint_token_is(before, "}")) {
                    break;
                }
            }
            if (in_attribute) {
                if (lint_token_is(token, "packed") || lint_token_is(token, "__packed__")) {
                    lint_breach(file->report, file->path, token->line, "packed",
                                "packed is banned; write the padding out (plans/CODING_STANDARD.md section 3)");
                }
                if (lint_token_is(token, "target") || lint_token_is(token, "__target__") ||
                    lint_token_is(token, "optimize") || lint_token_is(token, "__optimize__")) {
                    lint_breach(file->report, file->path, token->line, "simd_target",
                                "per-function target and optimize attributes defeat the build flags");
                }
            }
        }
        if (token->in_directive != 0 && lint_token_is(token, "pragma")) {
            rules_check_pragma(file, lexed, i);
        }
    }
}

/* ---- Number literals -------------------------------------------------------------------------------------------- */

/* Whether a preprocessing number is a floating literal, and where its suffix starts. */
static bool rules_float_suffix(const lint_token_t *token, const char **out_suffix, uint32_t *out_length) {
    const char *text = token->text;
    const uint32_t length = token->length;
    const bool hex = length > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    uint32_t i = hex ? 2u : 0u;
    bool is_float = false;
    while (i < length) {
        const char c = text[i];
        const bool digit = (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
        if (digit || c == '\'') {
            i++;
        } else if (c == '.') {
            is_float = true;
            i++;
        } else if ((hex && (c == 'p' || c == 'P')) || (!hex && (c == 'e' || c == 'E'))) {
            is_float = true;
            i++;
            if (i < length && (text[i] == '+' || text[i] == '-')) {
                i++;
            }
            while (i < length && text[i] >= '0' && text[i] <= '9') {
                i++;
            }
            break;
        } else {
            break;
        }
    }
    *out_suffix = text + i;
    *out_length = length - i;
    return is_float;
}

static void rules_check_literals(const lint_file_t *file, const lint_lexed_t *lexed, uint8_t language) {
    const bool shared_header =
        lint_starts_with(file->path, "run/include/gpu_bridge/") || lint_starts_with(file->path, "run/include/types/");
    for (uint32_t i = 0; i < lexed->token_count; i++) {
        const lint_token_t *token = &lexed->tokens[i];
        if (token->kind != LINT_TOKEN_NUMBER) {
            continue;
        }
        const char *suffix = nullptr;
        uint32_t suffix_length = 0;
        if (!rules_float_suffix(token, &suffix, &suffix_length)) {
            continue;
        }
        const bool in_macro =
            i >= 2u && lint_token_is(&lexed->tokens[i - 1u], "(") &&
            (lint_token_is(&lexed->tokens[i - 2u], "F32") || lint_token_is(&lexed->tokens[i - 2u], "F64"));
        if (in_macro) {
            continue;
        }
        const bool sized =
            lint_text_equal(suffix, suffix_length, "f32") || lint_text_equal(suffix, suffix_length, "F32") ||
            lint_text_equal(suffix, suffix_length, "f64") || lint_text_equal(suffix, suffix_length, "F64");
        if (shared_header) {
            lint_breach(file->report, file->path, token->line, "literal_shared_header",
                        "%.*s: a header compiled by both GCC and nvcc writes F32(x) or F64(x)", (int32_t)token->length,
                        token->text);
        } else if (language == LINT_LANGUAGE_CUDA) {
            if (suffix_length != 0 && !lint_text_equal(suffix, suffix_length, "F")) {
                lint_breach(file->report, file->path, token->line, "literal_cuda_suffix",
                            "%.*s: CUDA writes 1.0F or 1.0; f32 and f64 suffixes crash nvcc (Stage 01.1)",
                            (int32_t)token->length, token->text);
            }
        } else if (suffix_length == 0) {
            lint_breach(file->report, file->path, token->line, "literal_unsuffixed_float",
                        "%.*s is a double; write f32 or f64", (int32_t)token->length, token->text);
        } else if (!sized) {
            lint_breach(file->report, file->path, token->line, "literal_wrong_suffix",
                        "%.*s: the only float suffixes are f32 and f64", (int32_t)token->length, token->text);
        }
    }
}

/* ---- Structure: braces, structs, statics, goto ---------------------------------------------------------------- */

typedef enum rules_brace_t : uint8_t {
    RULES_BRACE_BLOCK = 0,
    RULES_BRACE_STRUCT = 1,
    RULES_BRACE_ENUM = 2,
    RULES_BRACE_FUNCTION = 3,
    RULES_BRACE_EXTERN = 4, /* extern "C" { adds no scope */
} rules_brace_t;

enum { RULES_MAX_BRACES = 256 };

/* One definition found while walking a file. */
typedef struct rules_definition_t {
    const lint_token_t *name;
} rules_definition_t;
static_assert(sizeof(rules_definition_t) == 8);

typedef struct rules_walk_t {
    const lint_file_t *file;
    lint_token_t *code; /* tokens outside directives */
    rules_definition_t *functions;
    const lint_token_t **struct_names;
    const lint_token_t *current_function;
    lint_graph_t *cuda_graph;
    uint32_t code_count;
    uint32_t function_count;
    uint32_t struct_name_count;
    uint32_t brace_depth;
    uint32_t paren_depth;
    int32_t function_candidate;
    int32_t pending_brace_index;
    rules_brace_t pending_brace_kind;
    uint8_t pending_brace_typedef;
    uint8_t member_has_enum;
    uint8_t bracket_depth;
    rules_brace_t braces[RULES_MAX_BRACES];
    uint8_t brace_typedef[RULES_MAX_BRACES];
} rules_walk_t;
static_assert(sizeof(rules_walk_t) == 80 + 2u * RULES_MAX_BRACES);

static bool rules_at_file_scope(const rules_walk_t *walk) {
    for (uint32_t i = 0; i < walk->brace_depth && i < RULES_MAX_BRACES; i++) {
        if (walk->braces[i] != RULES_BRACE_EXTERN) {
            return false;
        }
    }
    return true;
}

static rules_brace_t rules_top_brace(const rules_walk_t *walk) {
    if (walk->brace_depth == 0 || walk->brace_depth > RULES_MAX_BRACES) {
        return RULES_BRACE_BLOCK;
    }
    return walk->braces[walk->brace_depth - 1u];
}

/* The index after the group opened at open_index. */
static uint32_t rules_skip_group(const rules_walk_t *walk, uint32_t open_index, const char *opener,
                                 const char *closer) {
    uint32_t depth = 0;
    uint32_t i = open_index;
    while (i < walk->code_count) {
        if (lint_token_is(&walk->code[i], opener)) {
            depth++;
        } else if (lint_token_is(&walk->code[i], closer)) {
            depth--;
            if (depth == 0) {
                return i + 1u;
            }
        }
        i++;
    }
    return walk->code_count;
}

static bool rules_is_attribute_like(const lint_token_t *token) {
    return lint_token_is(token, "__attribute__") || lint_token_is(token, "__attribute") ||
           lint_token_is(token, "alignas") || lint_token_is(token, "_Alignas") || lint_token_is(token, "typeof") ||
           lint_token_is(token, "typeof_unqual") || lint_token_is(token, "_Atomic") ||
           lint_token_is(token, "static_assert") || lint_token_is(token, "_Static_assert");
}

/* The index after attributes and alignas(...) at i, including C23 [[...]]. */
static uint32_t rules_skip_attributes(const rules_walk_t *walk, uint32_t i) {
    while (i < walk->code_count) {
        const lint_token_t *token = &walk->code[i];
        if (rules_is_attribute_like(token) && i + 1u < walk->code_count && lint_token_is(&walk->code[i + 1u], "(")) {
            i = rules_skip_group(walk, i + 1u, "(", ")");
        } else if (lint_token_is(token, "[") && i + 1u < walk->code_count && lint_token_is(&walk->code[i + 1u], "[")) {
            i = rules_skip_group(walk, i, "[", "]");
        } else {
            return i;
        }
    }
    return i;
}

/* Looks at a declaration of a variable with static storage: the rule is that nothing owned hides in one. */
static void rules_check_static_declaration(const rules_walk_t *walk, uint32_t start) {
    bool has_pointer = false;
    bool pointer_is_const = false;
    const lint_token_t *name = nullptr;
    uint32_t i = start;
    while (i < walk->code_count) {
        const lint_token_t *token = &walk->code[i];
        if (token->kind == LINT_TOKEN_IDENT) {
            if (rules_is_attribute_like(token) && i + 1u < walk->code_count &&
                lint_token_is(&walk->code[i + 1u], "(")) {
                i = rules_skip_group(walk, i + 1u, "(", ")");
                continue;
            }
            if (lint_token_is(token, "const")) {
                pointer_is_const = has_pointer;
            } else if (!lint_token_is(token, "static") && !lint_token_is(token, "thread_local") &&
                       !lint_token_is(token, "_Thread_local") && !lint_token_is(token, "volatile") &&
                       !lint_token_is(token, "restrict")) {
                name = token;
            }
            i++;
        } else if (lint_token_is(token, "*")) {
            has_pointer = true;
            pointer_is_const = false;
            i++;
        } else if (lint_token_is(token, "{")) {
            i = rules_skip_group(walk, i, "{", "}");
        } else if (lint_token_is(token, "(")) {
            if (i + 1u >= walk->code_count || !lint_token_is(&walk->code[i + 1u], "*")) {
                return; /* a function, whatever it returns */
            }
            /* A pointer to a function or an array: (*name)(...) */
            has_pointer = true;
            pointer_is_const = false;
            i += 2u;
            while (i < walk->code_count && walk->code[i].kind == LINT_TOKEN_IDENT) {
                if (lint_token_is(&walk->code[i], "const")) {
                    pointer_is_const = true;
                } else {
                    name = &walk->code[i];
                }
                i++;
            }
            break;
        } else {
            break; /* '=', ';', '[' or ',' ends the declarator */
        }
    }
    if (!has_pointer || pointer_is_const || name == nullptr) {
        return;
    }
    const lint_file_t *file = walk->file;
    const lint_token_t *first = &walk->code[start];
    if (!rules_token_starts(name, "borrowed_")) {
        lint_breach(
            file->report, file->path, name->line, "owning_static",
            "%.*s: a pointer with static storage must be named borrowed_ and be allowlisted; nothing owned hides "
            "in a static (plans/CODING_STANDARD.md section 4)",
            (int32_t)name->length, name->text);
        return;
    }
    if (!lint_catalog_borrowed_static_allowed(file->path)) {
        lint_breach(file->report, file->path, name->line, "borrowed_not_allowlisted",
                    "%.*s: this file is not on the borrowed_ static allowlist in compile/src/standards_lint/catalog/",
                    (int32_t)name->length, name->text);
    }
    if (first->comment_before == 0) {
        lint_breach(file->report, file->path, name->line, "borrowed_no_comment",
                    "%.*s: a borrowed_ static needs a comment above it saying why", (int32_t)name->length, name->text);
    }
}

static bool rules_is_lower_type_name(const lint_token_t *token) {
    if (token->length < 3u || token->text[token->length - 2u] != '_' || token->text[token->length - 1u] != 't') {
        return false;
    }
    for (uint32_t i = 0; i < token->length; i++) {
        const char c = token->text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

/* At the struct, union or enum keyword: remembers the brace that opens the body, if this is a definition. */
static void rules_note_tagged_type(rules_walk_t *walk, uint32_t i, bool is_enum) {
    uint32_t next = rules_skip_attributes(walk, i + 1u);
    if (next < walk->code_count && walk->code[next].kind == LINT_TOKEN_IDENT) {
        next = rules_skip_attributes(walk, next + 1u);
    }
    if (is_enum && next < walk->code_count && lint_token_is(&walk->code[next], ":")) {
        /* C23 fixed underlying type: enum name : uint8_t { */
        next++;
        while (next < walk->code_count && walk->code[next].kind == LINT_TOKEN_IDENT) {
            next++;
        }
    }
    if (walk->cuda_graph != nullptr && !is_enum && next < walk->code_count && lint_token_is(&walk->code[next], ":")) {
        lint_breach(walk->file->report, walk->file->path, walk->code[i].line, "cuda_inheritance",
                    "inheritance is outside the C-like subset for CUDA (plans/CODING_STANDARD.md section 8)");
    }
    if (next >= walk->code_count || !lint_token_is(&walk->code[next], "{")) {
        return;
    }
    walk->pending_brace_index = (int32_t)next;
    walk->pending_brace_kind = is_enum ? RULES_BRACE_ENUM : RULES_BRACE_STRUCT;
    walk->pending_brace_typedef = (i > 0 && lint_token_is(&walk->code[i - 1u], "typedef")) ? 1u : 0u;
    /* A struct inside a struct is part of its layout, and the outer one's assert covers it. */
    const bool nested = rules_top_brace(walk) == RULES_BRACE_STRUCT;
    if (!is_enum && !nested && walk->pending_brace_typedef == 0) {
        lint_breach(walk->file->report, walk->file->path, walk->code[i].line, "struct_no_typedef",
                    "all structs are typedef'ed: typedef struct name_t { ... } name_t;");
    }
}

/* Whether the file holds static_assert(<word>(<type>...), or static_assert(<word>(<type>, <field>... with a field. */
static bool rules_has_layout_assert(const rules_walk_t *walk, const char *word, const lint_token_t *type,
                                    const lint_token_t *field) {
    const uint32_t needed = field != nullptr ? 7u : 5u;
    for (uint32_t i = 0; i + needed <= walk->code_count; i++) {
        const lint_token_t *t = &walk->code[i];
        if (!(lint_token_is(t, "static_assert") || lint_token_is(t, "_Static_assert")) || !lint_token_is(&t[1], "(") ||
            !lint_token_is(&t[2], word) || !lint_token_is(&t[3], "(") || t[4].length != type->length ||
            memcmp(t[4].text, type->text, type->length) != 0) {
            continue;
        }
        if (field == nullptr) {
            return true;
        }
        if (lint_token_is(&t[5], ",") && t[6].length == field->length &&
            memcmp(t[6].text, field->text, field->length) == 0) {
            return true;
        }
    }
    return false;
}

static void rules_require_offsetof(const rules_walk_t *walk, const lint_token_t *type, const lint_token_t *field) {
    if (!rules_has_layout_assert(walk, "offsetof", type, field)) {
        lint_breach(walk->file->report, walk->file->path, field->line, "struct_no_offsetof_assert",
                    "%.*s is shared with CUDA, NASM or files, so it needs static_assert(offsetof(%.*s, %.*s) == N) "
                    "(plans/CODING_STANDARD.md section 3)",
                    (int32_t)type->length, type->text, (int32_t)type->length, type->text, (int32_t)field->length,
                    field->text);
    }
}

/* Section 3, rule 4, for the struct whose body closed at close_index: its alignof, and the offsetof of every field. A
 * named inner struct is one field. An unnamed one's fields belong to the outer struct, so they are read as its own. */
static void rules_check_shared_struct(const rules_walk_t *walk, uint32_t close_index, const lint_token_t *type) {
    if (!rules_has_layout_assert(walk, "alignof", type, nullptr)) {
        lint_breach(walk->file->report, walk->file->path, type->line, "struct_no_alignof_assert",
                    "%.*s is shared with CUDA, NASM or files, so it needs static_assert(alignof(%.*s) == N) "
                    "(plans/CODING_STANDARD.md section 3)",
                    (int32_t)type->length, type->text, (int32_t)type->length, type->text);
    }
    uint32_t open_index = close_index;
    uint32_t depth = 1;
    while (open_index > 0 && depth > 0) {
        open_index--;
        depth += lint_token_is(&walk->code[open_index], "}") ? 1u : 0u;
        depth -= lint_token_is(&walk->code[open_index], "{") ? 1u : 0u;
    }
    const lint_token_t *field = nullptr;
    uint32_t i = open_index + 1u;
    while (i < close_index) {
        const lint_token_t *token = &walk->code[i];
        if (lint_token_is(token, "{")) {
            const uint32_t after = rules_skip_attributes(walk, rules_skip_group(walk, i, "{", "}"));
            /* Named: carry on at its name. Unnamed: walk into it. */
            i = walk->code[after].kind == LINT_TOKEN_IDENT ? after : i + 1u;
            field = nullptr;
        } else if (lint_token_is(token, "}")) {
            field = nullptr;
            i++;
        } else if (lint_token_is(token, "(") && i + 2u < close_index && lint_token_is(&walk->code[i + 1u], "*")) {
            /* A function pointer: (*name)(arguments) */
            field = &walk->code[i + 2u];
            i = rules_skip_group(walk, i, "(", ")");
        } else if (lint_token_is(token, "(")) {
            i = rules_skip_group(walk, i, "(", ")");
        } else if (lint_token_is(token, "[")) {
            i = i + 1u < close_index && lint_token_is(&walk->code[i + 1u], "[") ? rules_skip_attributes(walk, i)
                                                                                : rules_skip_group(walk, i, "[", "]");
        } else if (lint_token_is(token, ";") || lint_token_is(token, ",")) {
            if (field != nullptr) {
                rules_require_offsetof(walk, type, field);
            }
            field = nullptr;
            i++;
        } else if (token->kind == LINT_TOKEN_IDENT && rules_is_attribute_like(token) &&
                   lint_token_is(&walk->code[i + 1u], "(")) {
            i = rules_skip_group(walk, i + 1u, "(", ")");
        } else {
            if (token->kind == LINT_TOKEN_IDENT) {
                field = token;
            }
            i++;
        }
    }
}

/* The struct or enum body that just closed at index i, with the typedef name after it. */
static void rules_close_tagged_type(rules_walk_t *walk, uint32_t i, rules_brace_t kind, bool had_typedef) {
    const uint32_t after = rules_skip_attributes(walk, i + 1u);
    if (!had_typedef || after >= walk->code_count || walk->code[after].kind != LINT_TOKEN_IDENT) {
        return;
    }
    const lint_token_t *name = &walk->code[after];
    if (!rules_is_lower_type_name(name)) {
        lint_breach(walk->file->report, walk->file->path, name->line, "type_name",
                    "%.*s: type names are lower_snake_case_t", (int32_t)name->length, name->text);
    }
    if (kind == RULES_BRACE_STRUCT && rules_top_brace(walk) != RULES_BRACE_STRUCT) {
        walk->struct_names[walk->struct_name_count] = name;
        walk->struct_name_count++;
        if (lint_catalog_struct_is_shared(walk->file->path)) {
            rules_check_shared_struct(walk, i, name);
        }
    }
}

static void rules_check_goto(const rules_walk_t *walk, uint32_t i) {
    const lint_file_t *file = walk->file;
    const lint_token_t *token = &walk->code[i];
    const bool cleanup_label = i + 1u < walk->code_count && rules_token_starts(&walk->code[i + 1u], "cleanup");
    bool in_init = false;
    if (walk->current_function != nullptr) {
        const lint_token_t *function = walk->current_function;
        in_init = function->length > 5u && memcmp(function->text + function->length - 5u, "_init", 5u) == 0;
    }
    if (!cleanup_label || !in_init) {
        lint_breach(file->report, file->path, token->line, "goto_cleanup_only",
                    "goto is only for 'goto cleanup' in an _init function (plans/CODING_STANDARD.md section 4)");
    }
}

static void rules_open_brace(rules_walk_t *walk, uint32_t i) {
    rules_brace_t kind = RULES_BRACE_BLOCK;
    uint8_t had_typedef = 0;
    if (walk->pending_brace_index == (int32_t)i) {
        kind = walk->pending_brace_kind;
        had_typedef = walk->pending_brace_typedef;
        walk->pending_brace_index = -1;
    } else if (i >= 2u && walk->code[i - 1u].kind == LINT_TOKEN_STRING &&
               lint_token_is(&walk->code[i - 2u], "extern")) {
        kind = RULES_BRACE_EXTERN;
    } else if (walk->paren_depth == 0 && rules_at_file_scope(walk) && walk->function_candidate >= 0 && i >= 1u &&
               lint_token_is(&walk->code[i - 1u], ")")) {
        kind = RULES_BRACE_FUNCTION;
        walk->current_function = &walk->code[walk->function_candidate];
        walk->functions[walk->function_count].name = walk->current_function;
        walk->function_count++;
    }
    if (walk->brace_depth < RULES_MAX_BRACES) {
        walk->braces[walk->brace_depth] = kind;
        walk->brace_typedef[walk->brace_depth] = had_typedef;
    }
    walk->brace_depth++;
    walk->function_candidate = -1;
    walk->member_has_enum = 0;
}

static void rules_close_brace(rules_walk_t *walk, uint32_t i) {
    if (walk->brace_depth == 0) {
        return; /* #if branches can unbalance what a lexer sees; the compiler judges real errors */
    }
    const rules_brace_t kind = rules_top_brace(walk);
    const bool had_typedef = walk->brace_depth <= RULES_MAX_BRACES && walk->brace_typedef[walk->brace_depth - 1u] != 0;
    walk->brace_depth--;
    if (kind == RULES_BRACE_FUNCTION) {
        walk->current_function = nullptr;
    } else if (kind == RULES_BRACE_STRUCT || kind == RULES_BRACE_ENUM) {
        rules_close_tagged_type(walk, i, kind, had_typedef);
    }
}

static void rules_check_token(rules_walk_t *walk, uint32_t i) {
    const lint_file_t *file = walk->file;
    const lint_token_t *token = &walk->code[i];
    const lint_token_t *previous = i > 0 ? &walk->code[i - 1u] : nullptr;
    const lint_token_t *next = i + 1u < walk->code_count ? &walk->code[i + 1u] : nullptr;
    const rules_brace_t top = rules_top_brace(walk);

    if (lint_token_is(token, "[")) {
        walk->bracket_depth++;
    } else if (lint_token_is(token, "]") && walk->bracket_depth > 0) {
        walk->bracket_depth--;
    }

    if (lint_token_is(token, "(")) {
        /* name( directly in a struct body is a method or a constructor. (*name)( is a function pointer field. */
        if (walk->cuda_graph != nullptr && top == RULES_BRACE_STRUCT && walk->paren_depth == 0 &&
            walk->bracket_depth == 0 && previous != nullptr && previous->kind == LINT_TOKEN_IDENT &&
            !rules_is_attribute_like(previous) && next != nullptr && !lint_token_is(next, "*")) {
            lint_breach(
                file->report, file->path, token->line, "cuda_struct_method",
                "%.*s: structs have no methods in the C-like subset for CUDA (plans/CODING_STANDARD.md section 8)",
                (int32_t)previous->length, previous->text);
        }
        if (walk->paren_depth == 0 && rules_at_file_scope(walk) && walk->function_candidate < 0 &&
            previous != nullptr && previous->kind == LINT_TOKEN_IDENT && !rules_is_attribute_like(previous)) {
            walk->function_candidate = (int32_t)i - 1;
        }
        if (walk->cuda_graph != nullptr && walk->current_function != nullptr && previous != nullptr &&
            previous->kind == LINT_TOKEN_IDENT) {
            lint_graph_add_edge(walk->cuda_graph, walk->current_function->text, walk->current_function->length,
                                previous->text, previous->length, file->path, previous->line);
        }
        walk->paren_depth++;
    } else if (lint_token_is(token, ")")) {
        if (walk->paren_depth > 0) {
            walk->paren_depth--;
        }
    } else if (lint_token_is(token, "{")) {
        rules_open_brace(walk, i);
    } else if (lint_token_is(token, "}")) {
        rules_close_brace(walk, i);
    } else if (lint_token_is(token, ";")) {
        if (walk->paren_depth == 0) {
            walk->function_candidate = -1;
            walk->member_has_enum = 0;
        }
    } else if (lint_token_is(token, "struct") || lint_token_is(token, "union")) {
        if (walk->paren_depth == 0) {
            rules_note_tagged_type(walk, i, false);
        }
    } else if (lint_token_is(token, "enum")) {
        walk->member_has_enum = 1;
        if (walk->paren_depth == 0) {
            rules_note_tagged_type(walk, i, true);
        }
    } else if (lint_token_is(token, "goto")) {
        rules_check_goto(walk, i);
    } else if (lint_token_is(token, "static") && walk->paren_depth == 0 &&
               !(previous != nullptr && lint_token_is(previous, "["))) {
        rules_check_static_declaration(walk, i);
    } else if (top == RULES_BRACE_STRUCT && walk->paren_depth == 0) {
        if ((lint_token_is(token, "bool") || lint_token_is(token, "_Bool")) && next != nullptr &&
            !lint_token_is(next, "(") && !lint_token_is(next, "*")) {
            lint_breach(file->report, file->path, token->line, "bool_in_struct",
                        "struct fields hold truth values as uint8_t (plans/CODING_STANDARD.md section 2)");
        }
        if (lint_token_is(token, ":") && walk->member_has_enum == 0) {
            lint_breach(file->report, file->path, token->line, "bitfield",
                        "bitfields are banned; use shifts and masks on a uintN_t (plans/CODING_STANDARD.md section 3)");
        }
    }

    /* A file-scope declaration that does not start with static can be a global just the same. */
    const bool statement_start = previous == nullptr || lint_token_is(previous, ";") || lint_token_is(previous, "}") ||
                                 (lint_token_is(previous, "{") && walk->brace_depth > 0 && top == RULES_BRACE_EXTERN);
    if (statement_start && rules_at_file_scope(walk) && walk->paren_depth == 0 && token->kind == LINT_TOKEN_IDENT &&
        !lint_token_is(token, "static") && !lint_token_is(token, "typedef") && !lint_token_is(token, "extern") &&
        !lint_token_is(token, "static_assert") && !lint_token_is(token, "_Static_assert")) {
        rules_check_static_declaration(walk, i);
    }
}

static void rules_check_structure(const lint_file_t *file, const lint_lexed_t *lexed, lint_graph_t *cuda_graph) {
    lint_arena_t *arena = file->arena_file;
    rules_walk_t *walk = lint_arena_alloc(arena, sizeof(rules_walk_t));
    memset(walk, 0, sizeof(rules_walk_t));
    walk->file = file;
    walk->cuda_graph = cuda_graph;
    walk->function_candidate = -1;
    walk->pending_brace_index = -1;
    walk->code = lint_arena_alloc(arena, ((uint64_t)lexed->token_count + 1u) * sizeof(lint_token_t));
    for (uint32_t i = 0; i < lexed->token_count; i++) {
        if (lexed->tokens[i].in_directive == 0) {
            walk->code[walk->code_count] = lexed->tokens[i];
            walk->code_count++;
        }
    }
    walk->functions = lint_arena_alloc(arena, ((uint64_t)walk->code_count + 1u) * sizeof(rules_definition_t));
    walk->struct_names = lint_arena_alloc(arena, ((uint64_t)walk->code_count + 1u) * sizeof(const lint_token_t *));

    for (uint32_t i = 0; i < walk->code_count; i++) {
        rules_check_token(walk, i);
    }

    /* Section 3, rule 4: every struct has a static_assert on its sizeof, in the same file. */
    for (uint32_t s = 0; s < walk->struct_name_count; s++) {
        const lint_token_t *name = walk->struct_names[s];
        bool found = false;
        for (uint32_t i = 0; i + 4u < lexed->token_count && !found; i++) {
            const lint_token_t *t = &lexed->tokens[i];
            found = (lint_token_is(t, "static_assert") || lint_token_is(t, "_Static_assert")) &&
                    lint_token_is(&t[1], "(") && lint_token_is(&t[2], "sizeof") && lint_token_is(&t[3], "(") &&
                    t[4].length == name->length && memcmp(t[4].text, name->text, name->length) == 0;
        }
        if (!found) {
            lint_breach(file->report, file->path, name->line, "struct_no_sizeof_assert",
                        "%.*s needs static_assert(sizeof(%.*s) == N) (plans/CODING_STANDARD.md section 3)",
                        (int32_t)name->length, name->text, (int32_t)name->length, name->text);
        }
    }

    /* Section 4, Pairing: every x_init has an x_deinit in the same file. */
    for (uint32_t f = 0; f < walk->function_count; f++) {
        const lint_token_t *name = walk->functions[f].name;
        if (name->length <= 5u || memcmp(name->text + name->length - 5u, "_init", 5u) != 0) {
            continue;
        }
        const uint32_t stem = name->length - 5u;
        bool found = false;
        for (uint32_t g = 0; g < walk->function_count && !found; g++) {
            const lint_token_t *other = walk->functions[g].name;
            found = other->length == stem + 7u && memcmp(other->text, name->text, stem) == 0 &&
                    memcmp(other->text + stem, "_deinit", 7u) == 0;
        }
        if (!found) {
            lint_breach(file->report, file->path, name->line, "init_deinit_pair",
                        "%.*s has no %.*s_deinit in this file (plans/CODING_STANDARD.md section 4)",
                        (int32_t)name->length, name->text, (int32_t)stem, name->text);
        }
    }
}

void lint_rules_c_check(const lint_file_t *file, const lint_lexed_t *lexed, uint8_t languages,
                        lint_graph_t *cuda_graph) {
    rules_check_identifiers(file, lexed, languages);
    rules_check_attributes(file, lexed);
    rules_check_digraphs(file, lexed);
    rules_check_line_splices(file);
    rules_check_literals(file, lexed, languages);
    rules_check_structure(file, lexed, languages == LINT_LANGUAGE_CUDA ? cuda_graph : nullptr);
}
