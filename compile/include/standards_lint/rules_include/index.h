/* Which headers a file may include: banned libraries, and the direction in plans/REPO_STRUCTURE.md section 11. */
#ifndef MC_STANDARDS_LINT_RULES_INCLUDE_INDEX_H
#define MC_STANDARDS_LINT_RULES_INCLUDE_INDEX_H

#include "standards_lint/lexer/index.h"
#include "standards_lint/support/index.h"

#include <stdint.h>

void lint_rules_include_check(const lint_file_t *file, const lint_lexed_t *lexed, uint8_t languages);

/* The system's row in the section 11 diagram, top row first, or 0 if the diagram does not list it. */
uint32_t lint_rules_include_system_rank(const char *system, uint64_t length);

#endif
