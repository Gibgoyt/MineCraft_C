/* See run/src/simd/README.md. */
#ifndef MC_RUN_SIMD_INDEX_H
#define MC_RUN_SIMD_INDEX_H

#include "arena/index.h"
#include "heap/index.h"

#include <stddef.h>
#include <stdint.h>

/* The kernels themselves keep no state and are callable at any time. This holds the scratch memory their parts borrow. */
typedef struct simd_t {
    arena_t arena_scratch; /* over owned_region; empty between uses */
    heap_region_t owned_region;
    uint8_t _pad0[48];
} simd_t;
static_assert(sizeof(simd_t) == 128);
static_assert(alignof(simd_t) == 64);
static_assert(offsetof(simd_t, arena_scratch) == 0);
static_assert(offsetof(simd_t, owned_region) == 64);
static_assert(offsetof(simd_t, _pad0) == 80);

/* Reserves the scratch memory and runs simd_self_check. On failure logs why, leaves nothing behind and returns false. */
bool simd_init(simd_t *simd);

/* Releases what simd_init reserved. Safe on a zeroed simd_t. */
void simd_deinit(simd_t *simd);

/* Runs every kernel on known bytes and compares it with its scalar reference, so a wrong kernel stops the game at startup
 * and not as a corrupt frame. Logs the first kernel that differs and returns false. */
bool simd_self_check(simd_t *simd);

#endif
