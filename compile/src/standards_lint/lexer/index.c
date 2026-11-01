/* See README.md. */
#include "standards_lint/lexer/index.h"

#include <string.h>

static bool lexer_is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool lexer_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool lexer_is_ident_part(char c) {
    return lexer_is_ident_start(c) || lexer_is_digit(c);
}

bool lint_token_is(const lint_token_t *token, const char *text) {
    return lint_text_equal(token->text, token->length, text);
}

/* The position after a preprocessing number. A sign belongs to it only straight after an exponent letter, which is
 * e or E in a decimal number and p or P in a hexadecimal one. */
static uint64_t lexer_number_end(const char *text, uint64_t length, uint64_t start) {
    const bool hex = start + 1u < length && text[start] == '0' && (text[start + 1u] == 'x' || text[start + 1u] == 'X');
    uint64_t i = start;
    while (i < length) {
        const char c = text[i];
        if ((c == '+' || c == '-') && i > start) {
            const char before = text[i - 1u];
            const bool exponent = hex ? (before == 'p' || before == 'P') : (before == 'e' || before == 'E');
            if (!exponent) {
                break;
            }
            i++;
        } else if (lexer_is_ident_part(c) || c == '.') {
            i++;
        } else if (c == '\'' && i + 1u < length && lexer_is_ident_part(text[i + 1u])) {
            i += 2u; /* C23 digit separator */
        } else {
            break;
        }
    }
    return i;
}

/* The position after a quoted literal. An unterminated one stops at the end of its line. */
static uint64_t lexer_quoted_end(const char *text, uint64_t length, uint64_t start, char quote) {
    uint64_t i = start + 1u;
    while (i < length && text[i] != '\n') {
        if (text[i] == '\\' && i + 1u < length) {
            i += 2u;
        } else if (text[i] == quote) {
            return i + 1u;
        } else {
            i++;
        }
    }
    return i;
}

void lint_lex(lint_arena_t *arena, const char *text, uint64_t length, lint_lexed_t *out) {
    /* A file can never hold more tokens than bytes, and the arena only touches what gets written. */
    lint_token_t *tokens = lint_arena_alloc(arena, (length + 1u) * sizeof(lint_token_t));
    lint_include_t *includes = lint_arena_alloc(arena, (length / 8u + 1u) * sizeof(lint_include_t));
    uint32_t token_count = 0;
    uint32_t include_count = 0;
    uint32_t line = 1;
    uint64_t i = 0;
    bool at_line_start = true;
    bool in_directive = false;
    bool comment_pending = false;

    while (i < length) {
        const char c = text[i];
        if (c == '\n') {
            line++;
            i++;
            at_line_start = true;
            in_directive = false;
            continue;
        }
        if (c == '\\' && i + 1u < length && text[i + 1u] == '\n') {
            line++;
            i += 2u;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            i++;
            continue;
        }
        if (c == '/' && i + 1u < length && text[i + 1u] == '/') {
            while (i < length && text[i] != '\n') {
                i++;
            }
            comment_pending = true;
            continue;
        }
        if (c == '/' && i + 1u < length && text[i + 1u] == '*') {
            i += 2u;
            while (i < length && !(text[i] == '*' && i + 1u < length && text[i + 1u] == '/')) {
                if (text[i] == '\n') {
                    line++;
                }
                i++;
            }
            i = i < length ? i + 2u : length;
            comment_pending = true;
            continue;
        }

        if (c == '#' && at_line_start) {
            at_line_start = false;
            in_directive = true;
            i++;
            while (i < length && (text[i] == ' ' || text[i] == '\t')) {
                i++;
            }
            const uint64_t name_start = i;
            while (i < length && lexer_is_ident_part(text[i])) {
                i++;
            }
            const uint64_t name_length = i - name_start;
            if (lint_text_equal(text + name_start, name_length, "include") ||
                lint_text_equal(text + name_start, name_length, "include_next") ||
                lint_text_equal(text + name_start, name_length, "embed")) {
                while (i < length && (text[i] == ' ' || text[i] == '\t')) {
                    i++;
                }
                if (i < length && (text[i] == '"' || text[i] == '<')) {
                    const bool angled = text[i] == '<';
                    const char closer = angled ? '>' : '"';
                    const uint64_t path_start = i + 1u;
                    uint64_t path_end = path_start;
                    while (path_end < length && text[path_end] != closer && text[path_end] != '\n') {
                        path_end++;
                    }
                    includes[include_count].path = lint_arena_text(arena, text + path_start, path_end - path_start);
                    includes[include_count].line = line;
                    includes[include_count].angled = angled ? 1u : 0u;
                    memset(includes[include_count]._pad0, 0, sizeof(includes[include_count]._pad0));
                    include_count++;
                    i = path_end < length && text[path_end] == closer ? path_end + 1u : path_end;
                }
                comment_pending = false; /* a comment above an include does not describe what follows it */
                continue;
            }
            if (name_length > 0) {
                /* Directive names are tokens too, so a rule can see "pragma pack". */
                tokens[token_count] = (lint_token_t){
                    .text = text + name_start,
                    .length = (uint32_t)name_length,
                    .line = line,
                    .kind = LINT_TOKEN_IDENT,
                    .in_directive = 1,
                    .comment_before = comment_pending ? 1u : 0u,
                };
                token_count++;
                comment_pending = false;
            }
            continue;
        }
        at_line_start = false;

        uint64_t end;
        lint_token_kind_t kind;
        if (lexer_is_ident_start(c)) {
            end = i + 1u;
            while (end < length && lexer_is_ident_part(text[end])) {
                end++;
            }
            kind = LINT_TOKEN_IDENT;
        } else if (lexer_is_digit(c) || (c == '.' && i + 1u < length && lexer_is_digit(text[i + 1u]))) {
            end = lexer_number_end(text, length, i);
            kind = LINT_TOKEN_NUMBER;
        } else if (c == '"') {
            end = lexer_quoted_end(text, length, i, '"');
            kind = LINT_TOKEN_STRING;
        } else if (c == '\'') {
            end = lexer_quoted_end(text, length, i, '\'');
            kind = LINT_TOKEN_CHAR;
        } else {
            const bool two =
                i + 1u < length && ((c == '-' && text[i + 1u] == '>') || (c == ':' && text[i + 1u] == ':'));
            end = i + (two ? 2u : 1u);
            kind = LINT_TOKEN_PUNCT;
        }
        tokens[token_count] = (lint_token_t){
            .text = text + i,
            .length = (uint32_t)(end - i),
            .line = line,
            .kind = kind,
            .in_directive = in_directive ? 1u : 0u,
            .comment_before = comment_pending ? 1u : 0u,
        };
        token_count++;
        comment_pending = false;
        i = end;
    }

    out->tokens = tokens;
    out->includes = includes;
    out->token_count = token_count;
    out->include_count = include_count;
}
