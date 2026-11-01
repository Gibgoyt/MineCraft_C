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

/* The label a line opens with, without its colon. Returns false if it has none. */
static bool asm_line_label(const char *text, uint64_t length, const char **out_name, uint64_t *out_length) {
    uint64_t i = 0;
    while (i < length && asm_is_space(text[i])) {
        i++;
    }
    const uint64_t start = i;
    while (i < length && !asm_is_space(text[i])) {
        i++;
    }
    if (i == start || text[i - 1u] != ':') {
        return false;
    }
    *out_name = text + start;
    *out_length = i - start - 1u;
    return true;
}

/* The target of a jump: the first operand after the mnemonic, past any short or near. */
static void asm_jump_target(const char *mnemonic, uint64_t mnemonic_length, const char *line_end, const char **out_name,
                            uint64_t *out_length) {
    const char *p = mnemonic + mnemonic_length;
    const char *start = p;
    uint64_t length = 0;
    for (uint32_t words = 0; words < 2u; words++) {
        while (p < line_end && asm_is_space(*p)) {
            p++;
        }
        start = p;
        while (p < line_end && !asm_is_space(*p)) {
            p++;
        }
        length = (uint64_t)(p - start);
        if (!lint_text_equal(start, length, "short") && !lint_text_equal(start, length, "near")) {
            break;
        }
    }
    *out_name = start;
    *out_length = length;
}

enum { ASM_MAX_DIRTY_TARGETS = 4096 };

/* A label that a jump reaches with YMM state live. Local labels repeat between functions, so the function is part of it. */
typedef struct asm_dirty_label_t {
    const char *name;
    uint64_t length;
    uint32_t function;
    uint8_t _pad0[4];
} asm_dirty_label_t;
static_assert(sizeof(asm_dirty_label_t) == 24);

typedef struct asm_dirty_labels_t {
    asm_dirty_label_t *items;
    uint32_t count;
    uint8_t overflowed; /* too many to remember, so every label counts as dirty: strict, never silently clean */
    uint8_t _pad0[3];
} asm_dirty_labels_t;
static_assert(sizeof(asm_dirty_labels_t) == 16);

static bool asm_dirty_has(const asm_dirty_labels_t *labels, uint32_t function, const char *name, uint64_t length) {
    if (labels->overflowed != 0) {
        return true;
    }
    for (uint32_t i = 0; i < labels->count; i++) {
        const asm_dirty_label_t *item = &labels->items[i];
        if (item->function == function && item->length == length && memcmp(item->name, name, length) == 0) {
            return true;
        }
    }
    return false;
}

static void asm_dirty_add(asm_dirty_labels_t *labels, uint32_t function, const char *name, uint64_t length) {
    if (asm_dirty_has(labels, function, name, length)) {
        return;
    }
    if (labels->count >= ASM_MAX_DIRTY_TARGETS) {
        labels->overflowed = 1;
        return;
    }
    labels->items[labels->count] =
        (asm_dirty_label_t){.name = name, .length = length, .function = function, ._pad0 = {0}};
    labels->count++;
}

/* The length of a line's code, without its comment. */
static uint64_t asm_code_length(const char *line, const char *line_end) {
    const char *comment = memchr(line, ';', (uint64_t)(line_end - line));
    return (uint64_t)((comment != nullptr ? comment : line_end) - line);
}

/* One walk over the file, tracking whether YMM state is live at each line. A ret after a jump is not the end of the code
 * that used YMM: the jump may land past that ret, or before it. So labels learned from jumps feed the next walk, and the
 * caller repeats it until no new label appears. Only the last walk reports. */
static void asm_flow_pass(const lint_file_t *file, asm_dirty_labels_t *labels, bool report) {
    const char *const end = file->text + file->length;
    const char *line = file->text;
    uint32_t line_number = 1;
    uint32_t function = 0;
    bool ymm_dirty = false;

    while (line < end) {
        const char *line_end = memchr(line, '\n', (uint64_t)(end - line));
        if (line_end == nullptr) {
            line_end = end;
        }
        const uint64_t code_length = asm_code_length(line, line_end);
        const char *word = nullptr;
        uint64_t word_length = 0;
        asm_first_instruction(line, code_length, &word, &word_length);

        const char *label = nullptr;
        uint64_t label_length = 0;
        if (asm_line_label(line, code_length, &label, &label_length)) {
            if (label[0] != '.') {
                /* A new function. */
                ymm_dirty = false;
                function++;
            } else if (asm_dirty_has(labels, function, label, label_length)) {
                ymm_dirty = true;
            }
        }
        /* Register names ymm0 to ymm15 end in digits, so look for the stem. */
        if (memmem(line, code_length, "ymm", 3u) != nullptr || memmem(line, code_length, "YMM", 3u) != nullptr) {
            ymm_dirty = true;
        }
        if (asm_has_word(word, word_length, "vzeroupper")) {
            ymm_dirty = false;
        }
        if (word_length > 1u && asm_lower(word[0]) == 'j') {
            if (ymm_dirty) {
                const char *target = nullptr;
                uint64_t target_length = 0;
                asm_jump_target(word, word_length, line + code_length, &target, &target_length);
                asm_dirty_add(labels, function, target, target_length);
            }
            /* Nothing falls through a jmp, so what follows is only as dirty as the jumps that reach its label. */
            if (word_length == 3u && asm_has_word(word, word_length, "jmp")) {
                ymm_dirty = false;
            }
        }
        if (asm_has_word(word, word_length, "ret") && word_length == 3u) {
            if (ymm_dirty && report) {
                lint_breach(
                    file->report, file->path, line_number, "asm_vzeroupper",
                    "YMM registers are live at this ret; vzeroupper first (plans/CODING_STANDARD.md section 7)");
            }
            ymm_dirty = false;
        }
        line = line_end + 1;
        line_number++;
    }
}

static void asm_check_vzeroupper(const lint_file_t *file) {
    asm_dirty_labels_t labels = {
        .items = lint_arena_alloc(file->arena_file, ASM_MAX_DIRTY_TARGETS * sizeof(asm_dirty_label_t)),
        .count = 0,
        .overflowed = 0,
        ._pad0 = {0},
    };
    /* The count only grows and has a ceiling, so this ends. */
    uint32_t before = 0;
    do {
        before = labels.count;
        asm_flow_pass(file, &labels, false);
    } while (labels.count != before && labels.overflowed == 0);
    asm_flow_pass(file, &labels, true);
}

void lint_rules_asm_check(const lint_file_t *file, bool is_include) {
    const char *const end = file->text + file->length;
    const char *line = file->text;
    uint32_t line_number = 1;
    bool has_default_rel = false;
    bool has_gnu_stack = false;

    asm_check_vzeroupper(file);

    while (line < end) {
        const char *line_end = memchr(line, '\n', (uint64_t)(end - line));
        if (line_end == nullptr) {
            line_end = end;
        }
        const uint64_t code_length = asm_code_length(line, line_end);

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
        if (memmem(line, code_length, "zmm", 3u) != nullptr || memmem(line, code_length, "ZMM", 3u) != nullptr ||
            asm_has_word(line, code_length, "mm") || asm_has_word(word, word_length, "emms")) {
            lint_breach(file->report, file->path, line_number, "asm_banned_register",
                        "no AVX-512 and no MMX on this hardware baseline (plans/CODING_STANDARD.md section 7)");
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
