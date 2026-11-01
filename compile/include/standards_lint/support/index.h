/* Arena, text, path and report helpers shared by every part of the standards lint. */
#ifndef MC_STANDARDS_LINT_SUPPORT_INDEX_H
#define MC_STANDARDS_LINT_SUPPORT_INDEX_H

#include <stdint.h>

/* A bump allocator over memory the tool's Owner reserved. Running out ends the process: this is a short-lived
 * build tool, and a lint that gave up halfway would look like a pass. */
typedef struct lint_arena_t {
    uint8_t *borrowed_base;
    uint64_t capacity;
    uint64_t used;
} lint_arena_t;
static_assert(sizeof(lint_arena_t) == 24);

void *lint_arena_alloc(lint_arena_t *arena, uint64_t bytes);
char *lint_arena_text(lint_arena_t *arena, const char *text, uint64_t length);
char *lint_arena_join(lint_arena_t *arena, const char *left, const char *right);

/* What the whole run found. A breach is a broken rule, exit status 1. An error means the lint could not look at
 * something, exit status 2. */
typedef struct lint_report_t {
    uint64_t breach_count;
    uint64_t error_count;
} lint_report_t;
static_assert(sizeof(lint_report_t) == 16);

/* line 0 means the breach has no line of its own, such as a missing README. The rule must be in the catalog. */
[[gnu::format(printf, 5, 6)]]
void lint_breach(lint_report_t *report, const char *path, uint32_t line, const char *rule, const char *format, ...);
[[gnu::format(printf, 2, 3)]]
void lint_error(lint_report_t *report, const char *format, ...);

bool lint_text_equal(const char *text, uint64_t length, const char *word);
bool lint_starts_with(const char *text, const char *prefix);
bool lint_ends_with(const char *text, const char *suffix);

/* One '*' stands for exactly one path component. A pattern ending in '/' matches everything below it. */
bool lint_path_matches(const char *pattern, const char *path);
bool lint_path_exists(const char *path);

/* Tiers by path depth, plans/CODING_STANDARD.md section 4. Only run/src and compile/src have them. */
typedef enum lint_tier_t : uint8_t {
    LINT_TIER_NONE = 0,
    LINT_TIER_ROOT = 1,
    LINT_TIER_OWNER = 2,
    LINT_TIER_ARENA = 3,
    LINT_TIER_BORROW = 4,
} lint_tier_t;

/* path is repository-relative and names a file. */
lint_tier_t lint_tier_of_path(const char *path);

/* One source file being checked. */
typedef struct lint_file_t {
    const char *root; /* the repository */
    const char *path; /* relative to root */
    const char *text;
    uint64_t length;
    lint_arena_t *arena_file; /* reset after each file */
    lint_report_t *report;
} lint_file_t;
static_assert(sizeof(lint_file_t) == 48);

/* The file's bytes, NUL terminated, in the arena. On failure it records an error and returns false. */
bool lint_read_file(lint_arena_t *arena, lint_report_t *report, const char *path, const char **out_text,
                    uint64_t *out_length);

#endif
