/* Every rule the lint can report, and the table of banned identifiers with where each one is still allowed. */
#ifndef MC_STANDARDS_LINT_CATALOG_INDEX_H
#define MC_STANDARDS_LINT_CATALOG_INDEX_H

#include <stdint.h>

enum {
    LINT_LANGUAGE_C = 1,
    LINT_LANGUAGE_CUDA = 2,
    LINT_LANGUAGE_BOTH = 3,
};

typedef struct lint_word_t {
    const char *word;
    const char *rule;
    const char *why;
    const char *const *allowed_paths; /* patterns for lint_path_matches, ended by nullptr; nullptr for none */
    uint8_t languages;
    uint8_t member_ok; /* x.word and x->word are field names, not this construct */
    uint8_t _pad0[6];
} lint_word_t;
static_assert(sizeof(lint_word_t) == 40);

/* The row for an identifier, or nullptr. */
const lint_word_t *lint_catalog_find_word(const char *text, uint64_t length);

bool lint_catalog_has_rule(const char *rule);
uint32_t lint_catalog_rule_count(void);
const char *lint_catalog_rule_at(uint32_t index);

/* True if this file may hold a borrowed_ static. See plans/CODING_STANDARD.md section 4. */
bool lint_catalog_borrowed_static_allowed(const char *path);

#endif
