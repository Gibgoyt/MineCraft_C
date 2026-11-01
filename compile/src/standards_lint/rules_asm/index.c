/* See README.md. Checks lines, not tokens: NASM's words like "int" are instructions, so the C rules do not apply. */
#include "standards_lint/rules_asm/index.h"

#include <string.h>

static char asm_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool asm_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

/* Whether the word occurs in text, ignoring case, between non-word characters. */
static bool asm_has_word(const char *text, uint64_t length, const char *word) {
    const uint64_t word_length = strlen(word);
    for (uint64_t i = 0; i + word_length <= length; i++) {
        bool same = true;
        for (uint64_t j = 0; j < word_length && same; j++) {
            same = asm_lower(text[i + j]) == word[j];
        }
        if (same) {
            const bool starts = i == 0 || !((asm_lower(text[i - 1u]) >= 'a' && asm_lower(text[i - 1u]) <= 'z') ||
                                            text[i - 1u] == '_' || (text[i - 1u] >= '0' && text[i - 1u] <= '9'));
            const char after = i + word_length < length ? asm_lower(text[i + word_length]) : ' ';
            const bool ends = !((after >= 'a' && after <= 'z') || after == '_');
            if (starts && ends) {
                return true;
            }
        }
    }
    return false;
}

/* The mnemonic of a line: its first word after an optional label. */
static void asm_first_instruction(const char *text, uint64_t length, const char **out_word, uint64_t *out_length) {
    uint64_t i = 0;
    while (i < length && asm_is_space(text[i])) {
        i++;
    }
    uint64_t start = i;
    while (i < length && !asm_is_space(text[i])) {
        i++;
    }
    if (i > start && text[i - 1u] == ':') {
        while (i < length && asm_is_space(text[i])) {
            i++;
        }
        start = i;
        while (i < length && !asm_is_space(text[i])) {
            i++;
        }
    }
    *out_word = text + start;
    *out_length = i - start;
}

void lint_rules_asm_check(const lint_file_t *file, bool is_include) {
    const char *const end = file->text + file->length;
    const char *line = file->text;
    uint32_t line_number = 1;
    bool has_default_rel = false;
    bool has_gnu_stack = false;
    bool ymm_dirty = false;

    while (line < end) {
        const char *line_end = memchr(line, '\n', (uint64_t)(end - line));
        if (line_end == nullptr) {
            line_end = end;
        }
        const char *comment = memchr(line, ';', (uint64_t)(line_end - line));
        const uint64_t code_length = (uint64_t)((comment != nullptr ? comment : line_end) - line);

        const char *word = nullptr;
        uint64_t word_length = 0;
        asm_first_instruction(line, code_length, &word, &word_length);

        if (lint_text_equal(word, word_length, "%include")) {
            const char *close = memchr(word, '"', code_length - (uint64_t)(word - line));
            const char *last =
                close != nullptr ? memchr(close + 1, '"', (uint64_t)(line + code_length - close - 1)) : nullptr;
            const bool is_inc = last != nullptr && (uint64_t)(last - close) > 4u && memcmp(last - 4, ".inc", 4u) == 0;
            if (!is_inc) {
                lint_breach(file->report, file->path, line_number, "asm_include_ext",
                            "NASM includes end in .inc (plans/CODING_STANDARD.md section 5)");
            }
        }
        if (asm_has_word(line, code_length, "default") && asm_has_word(line, code_length, "rel")) {
            has_default_rel = true;
        }
        if (memmem(line, code_length, ".note.GNU-stack", strlen(".note.GNU-stack")) != nullptr) {
            has_gnu_stack = true;
        }
        /* Register names ymm0 to ymm15 end in digits, so look for the stem. */
        if (memmem(line, code_length, "ymm", 3u) != nullptr || memmem(line, code_length, "YMM", 3u) != nullptr) {
            ymm_dirty = true;
        }
        if (asm_has_word(word, word_length, "vzeroupper")) {
            ymm_dirty = false;
        }
        if (asm_has_word(word, word_length, "ret") && word_length == 3u) {
            if (ymm_dirty) {
                lint_breach(
                    file->report, file->path, line_number, "asm_vzeroupper",
                    "YMM registers are live at this ret; vzeroupper first (plans/CODING_STANDARD.md section 7)");
            }
            ymm_dirty = false;
        }
        line = line_end + 1;
        line_number++;
    }

    if (is_include) {
        return;
    }
    if (!has_default_rel) {
        lint_breach(file->report, file->path, 0, "asm_default_rel",
                    "needs 'default rel' (plans/CODING_STANDARD.md section 7)");
    }
    if (!has_gnu_stack) {
        lint_breach(
            file->report, file->path, 0, "asm_gnu_stack",
            "needs section .note.GNU-stack, or the stack turns executable (plans/CODING_STANDARD.md section 7)");
    }
}
