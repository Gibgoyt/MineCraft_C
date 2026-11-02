/* See run/src/assert/README.md. */
#ifndef MC_RUN_ASSERT_INDEX_H
#define MC_RUN_ASSERT_INDEX_H

#include <stdint.h>

/* Writes the failure to stderr and aborts. Called by the macros below. */
[[noreturn]] void assert_fail(const char *file, int32_t line, const char *function, const char *expression);

/* Checked in every preset. For what would corrupt data if it were ever false. */
#define ASSERT_ALWAYS(condition) ((condition) ? (void)0 : assert_fail(__FILE__, __LINE__, __func__, #condition))

/* Checked unless NDEBUG, which only the release and profile presets define. The condition still has to compile there,
 * but is not evaluated, so it must have no side effects. */
#ifdef NDEBUG
#define ASSERT(condition) ((void)sizeof((condition) ? 1 : 0))
#else
#define ASSERT(condition) ASSERT_ALWAYS(condition)
#endif

#endif
