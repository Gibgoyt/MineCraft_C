/* The rules for CMakeLists.txt and *.cmake: the compile flags live in one file, and none can be taken away. */
#ifndef MC_STANDARDS_LINT_RULES_CMAKE_INDEX_H
#define MC_STANDARDS_LINT_RULES_CMAKE_INDEX_H

#include "standards_lint/support/index.h"

/* file->path decides whether it is cmake/flags.cmake, the one file allowed to set flags. */
void lint_rules_cmake_check(const lint_file_t *file);

#endif
