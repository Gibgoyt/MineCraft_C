/* Walks every directory of the repository, applies the file and directory rules, and runs the source rules. */
#ifndef MC_STANDARDS_LINT_RULES_TREE_INDEX_H
#define MC_STANDARDS_LINT_RULES_TREE_INDEX_H

#include "standards_lint/support/index.h"

#include <stdint.h>

/* Returns the number of source files it looked at. */
uint64_t lint_rules_tree_check(const char *root, lint_arena_t *arena_walk, lint_arena_t *arena_file,
                               lint_report_t *report);

#endif
