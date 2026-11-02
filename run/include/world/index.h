/* See run/src/world/README.md. */
#ifndef MC_RUN_WORLD_INDEX_H
#define MC_RUN_WORLD_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "machine/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <stddef.h>
#include <stdint.h>

/* The most the two regions may take. Pages cost nothing until touched, so these bound a world, and do not size it. */
typedef struct world_limits_t {
    uint64_t section_bytes; /* 0: arrays of sections with something in them */
    uint64_t column_count;  /* 8: columns in memory at once */
} world_limits_t;
static_assert(sizeof(world_limits_t) == 16);

enum : uint64_t {
    /* Three eighths of the process budget, in config/machine.yaml. At the 6208 bytes of a typical section (see
     * run/src/world/section/README.md) that is about a million sections with arrays. */
    WORLD_SECTION_BYTES_DEFAULT = HEAP_BUDGET_BYTES / 8u * 3u,
    /* A square of the largest render distance around the player. */
    WORLD_COLUMN_COUNT_DEFAULT =
        (2ull * MC_GAME_RENDER_DISTANCE_MAX + 1ull) * (2ull * MC_GAME_RENDER_DISTANCE_MAX + 1ull),
};
static_assert(WORLD_SECTION_BYTES_DEFAULT + WORLD_COLUMN_COUNT_DEFAULT * sizeof(column_t) < HEAP_BUDGET_BYTES);

/* Holds what a world's parts borrow. There is one, under app_t. */
typedef struct world_t {
    arena_t arena_sections;       /* 0: over owned_sections; section_pool takes from it */
    arena_t arena_columns;        /* 64: over owned_columns; for column_array_alloc */
    heap_region_t owned_sections; /* 128 */
    heap_region_t owned_columns;  /* 144 */
    section_pool_t section_pool;  /* 160 */
    uint8_t _pad0[56];            /* 264 */
} world_t;
static_assert(sizeof(world_t) == 320);
static_assert(alignof(world_t) == 64);
static_assert(offsetof(world_t, owned_sections) == 128);
static_assert(offsetof(world_t, section_pool) == 160);
static_assert(offsetof(world_t, _pad0) == 264);

/* The limits a real game runs with. */
world_limits_t world_limits_default(void);

/* Reserves both regions. On failure logs why, leaves nothing behind and returns false. */
bool world_init(world_t *world, world_limits_t limits);

/* Releases what init reserved. Safe on a zeroed world_t. */
void world_deinit(world_t *world);

/* When a world closes: takes back every section and column at once, keeping the regions for the next. Every pointer into
 * either is dead, so no column may be in use. */
void world_reset(world_t *world);

#endif
