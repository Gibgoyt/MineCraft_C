/* See README.md. */
#include "standards_lint/rules_cmake/index.h"

#include <string.h>
#include <strings.h>

/* CMake commands ignore case, so these are compared without it. */
static const char *const cmake_flag_commands[] = {
    "set_source_files_properties", "target_compile_options",  "target_compile_definitions",
    "add_compile_options",         "add_compile_definitions", "add_definitions",
};
enum { CMAKE_FLAG_COMMAND_COUNT = sizeof(cmake_flag_commands) / sizeof(cmake_flag_commands[0]) };

/* Properties and variables that carry flags. Each is a prefix, because of the per-config forms such as _FLAGS_RELEASE.
 * _FLAGS catches a name built from a variable, such as CMAKE_${lang}_FLAGS. */
static const char *const cmake_flag_prefixes[] = {
    "COMPILE_OPTIONS",         "COMPILE_FLAGS",
    "COMPILE_DEFINITIONS",     "CMAKE_C_FLAGS",
    "CMAKE_CUDA_FLAGS",        "CMAKE_ASM_NASM_FLAGS",
    "CMAKE_C_COMPILE_",        "CMAKE_CUDA_COMPILE_",
    "CMAKE_ASM_NASM_COMPILE_", "CMAKE_C_COMPILER_LAUNCHER",
    "C_COMPILER_LAUNCHER",     "_FLAGS",
};
enum { CMAKE_FLAG_PREFIX_COUNT = sizeof(cmake_flag_prefixes) / sizeof(cmake_flag_prefixes[0]) };

/* The variables cmake/flags.cmake builds. Everyone else may read them, as ${MC_C_FLAGS}, and never write them. */
static const char *const cmake_flag_variables[] = {"MC_C_FLAGS", "MC_CUDA_FLAGS", "MC_NASM_FLAGS", "MC_LINK_FLAGS"};
enum { CMAKE_FLAG_VARIABLE_COUNT = sizeof(cmake_flag_variables) / sizeof(cmake_flag_variables[0]) };

/* Each switches off a warning or a rule of plans/CODING_STANDARD.md sections 6, 7 or 9. -w- is NASM's -Wno-. */
static const char *const cmake_weak_prefixes[] = {"-Wno-", "-w-", "-fpack-struct", "-mavx512"};
enum { CMAKE_WEAK_PREFIX_COUNT = sizeof(cmake_weak_prefixes) / sizeof(cmake_weak_prefixes[0]) };
static const char *const cmake_weak_words[] = {
    "-w",
    "-fpermissive",
    "-ffast-math",
    "-Ofast",
    "-funsafe-math-optimizations",
    "-ftree-vectorize",
    "-ftree-slp-vectorize",
    "-fopenmp-simd",
    "--diag-suppress",
};
enum { CMAKE_WEAK_WORD_COUNT = sizeof(cmake_weak_words) / sizeof(cmake_weak_words[0]) };

static bool cmake_word_starts(const char *word, uint64_t length, const char *prefix) {
    const uint64_t prefix_length = strlen(prefix);
    return length >= prefix_length && memcmp(word, prefix, prefix_length) == 0;
}

static bool cmake_is_delimiter(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '(' || c == ')' || c == ';' ||
           c == ':' || c == '<' || c == '>' || c == ',' || c == '=' || c == '$' || c == '{' || c == '}';
}

/* The text with its comments turned into spaces, so line numbers and strings stay as they were. A # inside a string is
 * not a comment. #[[ and #[=[ open a comment that runs to the matching ]] or ]=]. */
static char *cmake_without_comments(const lint_file_t *file) {
    char *text = lint_arena_text(file->arena_file, file->text, file->length);
    uint64_t i = 0;
    bool in_string = false;
    while (i < file->length) {
        const char c = text[i];
        if (in_string) {
            i += c == '\\' ? 2u : 1u;
            in_string = c != '"';
        } else if (c == '"') {
            in_string = true;
            i++;
        } else if (c != '#') {
            i++;
        } else {
            uint64_t open = i + 1u;
            bool bracket = open < file->length && text[open] == '[';
            uint32_t equals = 0;
            if (bracket) {
                open++;
                while (open < file->length && text[open] == '=') {
                    equals++;
                    open++;
                }
                bracket = open < file->length && text[open] == '[';
            }
            uint64_t stop = i;
            if (bracket) {
                /* Past the closing ], the equals signs and the last ]. Unclosed, it runs to the end of the file. */
                stop = open + 1u;
                bool closed = false;
                while (stop < file->length && !closed) {
                    closed = text[stop] == ']' && stop + equals + 1u < file->length &&
                             strspn(text + stop + 1u, "=") == equals && text[stop + equals + 1u] == ']';
                    stop++;
                }
                stop = closed ? stop + equals + 1u : file->length;
            } else {
                while (stop < file->length && text[stop] != '\n') {
                    stop++;
                }
            }
            for (uint64_t at = i; at < stop; at++) {
                text[at] = text[at] == '\n' ? '\n' : ' ';
            }
            i = stop;
        }
    }
    return text;
}

static void cmake_check_word(const lint_file_t *file, const char *word, uint64_t length, bool in_braces,
                             const char *previous, uint64_t previous_length, uint32_t line) {
    const int32_t shown = (int32_t)length;
    for (uint32_t i = 0; i < CMAKE_WEAK_PREFIX_COUNT; i++) {
        if (cmake_word_starts(word, length, cmake_weak_prefixes[i])) {
            lint_breach(file->report, file->path, line, "cmake_weakening_flag",
                        "%.*s switches off a check the build relies on (plans/CODING_STANDARD.md section 9)", shown,
                        word);
        }
    }
    for (uint32_t i = 0; i < CMAKE_WEAK_WORD_COUNT; i++) {
        if (lint_text_equal(word, length, cmake_weak_words[i])) {
            lint_breach(file->report, file->path, line, "cmake_weakening_flag",
                        "%.*s switches off a check the build relies on (plans/CODING_STANDARD.md section 9)", shown,
                        word);
        }
    }
    if (strcmp(file->path, "cmake/flags.cmake") == 0) {
        return;
    }
    bool sets_flags = false;
    for (uint32_t i = 0; i < CMAKE_FLAG_COMMAND_COUNT && !sets_flags; i++) {
        sets_flags = strlen(cmake_flag_commands[i]) == length && strncasecmp(word, cmake_flag_commands[i], length) == 0;
    }
    for (uint32_t i = 0; i < CMAKE_FLAG_PREFIX_COUNT && !sets_flags; i++) {
        sets_flags = cmake_word_starts(word, length, cmake_flag_prefixes[i]);
    }
    for (uint32_t i = 0; i < CMAKE_FLAG_VARIABLE_COUNT && !sets_flags; i++) {
        sets_flags = !in_braces && lint_text_equal(word, length, cmake_flag_variables[i]);
    }
    /* set_property(SOURCE x PROPERTY ...), the other way to reach one file's flags. */
    sets_flags = sets_flags || (lint_text_equal(word, length, "SOURCE") && previous_length == 12u &&
                                strncasecmp(previous, "set_property", 12u) == 0);
    if (sets_flags) {
        lint_breach(file->report, file->path, line, "cmake_flags_outside",
                    "%.*s: compile flags are set only in cmake/flags.cmake, for every source alike "
                    "(plans/CODING_STANDARD.md section 9)",
                    shown, word);
    }
}

void lint_rules_cmake_check(const lint_file_t *file) {
    const char *const text = cmake_without_comments(file);
    const char *previous = "";
    uint64_t previous_length = 0;
    uint32_t line = 1;
    uint64_t i = 0;
    while (i < file->length) {
        if (cmake_is_delimiter(text[i])) {
            line += text[i] == '\n' ? 1u : 0u;
            i++;
            continue;
        }
        const uint64_t start = i;
        while (i < file->length && !cmake_is_delimiter(text[i])) {
            i++;
        }
        const bool in_braces = start > 0 && text[start - 1u] == '{';
        cmake_check_word(file, text + start, i - start, in_braces, previous, previous_length, line);
        previous = text + start;
        previous_length = i - start;
    }
}
