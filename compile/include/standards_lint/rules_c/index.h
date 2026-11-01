/* The token rules for C and CUDA source. */
#ifndef MC_STANDARDS_LINT_RULES_C_INDEX_H
#define MC_STANDARDS_LINT_RULES_C_INDEX_H

#include "standards_lint/graph/index.h"
#include "standards_lint/lexer/index.h"
#include "standards_lint/support/index.h"

#include <stdint.h>

/* languages is LINT_LANGUAGE_C or LINT_LANGUAGE_CUDA. For CUDA, cuda_graph also receives the file's calls. */
void lint_rules_c_check(const lint_file_t *file, const lint_lexed_t *lexed, uint8_t languages,
                        lint_graph_t *cuda_graph);

#endif
