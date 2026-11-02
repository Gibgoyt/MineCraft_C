/* See run/src/main/README.md. */
#ifndef MC_RUN_MAIN_INDEX_H
#define MC_RUN_MAIN_INDEX_H

#include <stdint.h>

/* The one root of ownership: plans/CODING_STANDARD.md section 4. There is one of these, on main's stack, and every heap
 * block in the program traces back to it through struct fields. Each later system adds its field here. */
typedef struct app_t {
    uint32_t cpu_required; /* CPU_FEATURE_* the build uses */
    uint32_t cpu_present;  /* CPU_FEATURE_* this machine has */
} app_t;
static_assert(sizeof(app_t) == 8);

/* The exit codes of minecraft_c. */
enum {
    MAIN_EXIT_OK = 0,
    MAIN_EXIT_USAGE = 2,
    MAIN_EXIT_CPU_MISMATCH = 3,
    MAIN_EXIT_LEAK = 4,
};

#endif
