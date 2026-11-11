/* Calls through function pointers, which GCC's call graph cannot follow: job functions become edges, and every other
 * caller must be in the reviewed list, compile/src/standards_lint/indirect_calls.yaml. */
#ifndef MC_STANDARDS_LINT_INDIRECT_INDEX_H
#define MC_STANDARDS_LINT_INDIRECT_INDEX_H

#include "standards_lint/graph/index.h"
#include "standards_lint/support/index.h"

/* Call after every .ci file is read and before the cycle check. allowlist_path may be nullptr, which is an empty list.
 * Adds an edge from each listed job dispatcher to each function the compiled sources hand to the job system, then
 * reports indirect callers missing from the list and entries that are no longer true. */
void lint_indirect_check(lint_graph_t *graph, const char *allowlist_path, lint_arena_t *arena_walk,
                         lint_arena_t *arena_file, lint_report_t *report);

#endif
