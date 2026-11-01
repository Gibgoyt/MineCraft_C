/* Splits C and CUDA source into tokens, so that comments and strings can never trigger a rule. */
#ifndef MC_STANDARDS_LINT_LEXER_INDEX_H
#define MC_STANDARDS_LINT_LEXER_INDEX_H

#include "standards_lint/support/index.h"

#include <stdint.h>

typedef enum lint_token_kind_t : uint8_t {
    LINT_TOKEN_IDENT = 0,
    LINT_TOKEN_NUMBER = 1,
    LINT_TOKEN_STRING = 2,
    LINT_TOKEN_CHAR = 3,
    LINT_TOKEN_PUNCT = 4,
} lint_token_kind_t;

typedef struct lint_token_t {
    const char *text; /* into the source; not NUL terminated */
    uint32_t length;
    uint32_t line;
    lint_token_kind_t kind;
    uint8_t in_directive;   /* part of a preprocessor line */
    uint8_t comment_before; /* a comment sits between the previous token and this one */
    uint8_t _pad0[5];
} lint_token_t;
static_assert(sizeof(lint_token_t) == 24);

typedef struct lint_include_t {
    const char *path; /* NUL terminated, in the arena */
    uint32_t line;
    uint8_t angled;
    uint8_t _pad0[3];
} lint_include_t;
static_assert(sizeof(lint_include_t) == 16);

typedef struct lint_lexed_t {
    lint_token_t *tokens;
    lint_include_t *includes;
    uint32_t token_count;
    uint32_t include_count;
} lint_lexed_t;
static_assert(sizeof(lint_lexed_t) == 24);

void lint_lex(lint_arena_t *arena, const char *text, uint64_t length, lint_lexed_t *out);

bool lint_token_is(const lint_token_t *token, const char *text);

#endif
