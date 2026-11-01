/* The required form of NASM files, plans/CODING_STANDARD.md section 7. */
#ifndef MC_STANDARDS_LINT_RULES_ASM_INDEX_H
#define MC_STANDARDS_LINT_RULES_ASM_INDEX_H

#include "standards_lint/support/index.h"

/* is_include is true for a .inc file, which has includes to check but no required form of its own. */
void lint_rules_asm_check(const lint_file_t *file, bool is_include);

#endif
