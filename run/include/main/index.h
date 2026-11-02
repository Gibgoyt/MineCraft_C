/* See run/src/main/README.md. */
#ifndef MC_RUN_MAIN_INDEX_H
#define MC_RUN_MAIN_INDEX_H

#include "display/index.h"
#include "gpu_bridge/index.h"
#include "input/index.h"
#include "job/index.h"
#include "math/index.h"
#include "simd/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "world/index.h"

#include <stdint.h>

/* The one root of ownership: plans/CODING_STANDARD.md section 4. There is one of these, on main's stack, and every heap
 * block in the program traces back to it through struct fields. Each later system adds its field here. */
typedef struct app_t {
    job_system_t job; /* first: its alignment is what gives app_t its own */
    simd_t simd;
    texture_t texture;
    world_t world;                 /* nothing runs in it until game does (Stage 08.4) */
    terrain_t terrain;             /* the same */
    gpu_bridge_t gpu_bridge;       /* the default run and --headless start it, unless gpu.enabled says otherwise */
    display_t display;             /* only --test-pattern starts it, for now */
    display_offscreen_t offscreen; /* only --headless starts it */
    input_t input;                 /* only --test-pattern starts it, and only once display_init said OK */
    uint32_t cpu_required;         /* CPU_FEATURE_* the build uses */
    uint32_t cpu_present;          /* CPU_FEATURE_* this machine has */
    math_t math;
    uint8_t _pad0[48];
} app_t;
static_assert(sizeof(app_t) == 3904);
static_assert(alignof(app_t) == 64);

/* The exit codes of minecraft_c. */
typedef enum main_exit_t : uint8_t {
    MAIN_EXIT_OK = 0,
    MAIN_EXIT_USAGE = 2,
    MAIN_EXIT_CPU_MISMATCH = 3, /* the early guard in asm/src/cpuid/ uses this number too */
    MAIN_EXIT_LEAK = 4,
    MAIN_EXIT_RUNTIME = 5,         /* the core runtime would not start, or did not do what it was given */
    MAIN_EXIT_DISPLAY_REFUSED = 6, /* the display is not ours to take: nothing on it was changed */
    MAIN_EXIT_DISPLAY_ERROR = 7,   /* KMS failed partway; the console was given back */
    MAIN_EXIT_HEADLESS = 8,        /* --headless: a bad script, or a screenshot that could not be written */
} main_exit_t;

#endif
