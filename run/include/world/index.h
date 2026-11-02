/* See run/src/world/README.md. */
#ifndef MC_RUN_WORLD_INDEX_H
#define MC_RUN_WORLD_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "machine/index.h"
#include "world/column/index.h"
#include "world/edit/index.h"
#include "world/light/index.h"
#include "world/map/index.h"
#include "world/section/index.h"
#include "world/tick/index.h"

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
    /* What the column region gives one slot of the map. */
    WORLD_COLUMN_BYTES_PER_SLOT = sizeof(column_t) + WORLD_MAP_BYTES_PER_SLOT + WORLD_EDIT_BYTES_PER_SLOT +
                                  WORLD_EDIT_SNAPSHOT_BYTES_PER_SLOT + WORLD_LIGHT_BYTES_PER_SLOT +
                                  WORLD_TICK_BYTES_PER_SLOT,
    /* And what it holds besides, whatever the map's size. */
    WORLD_COLUMN_BYTES_FIXED = WORLD_MAP_BYTES_FIXED + WORLD_EDIT_BYTES_FIXED + WORLD_EDIT_SNAPSHOT_BYTES_FIXED +
                               WORLD_EDIT_LOG_CAPACITY_DEFAULT * sizeof(world_edit_record_t) +
                               WORLD_EDIT_SNAPSHOT_SECTIONS_DEFAULT * WORLD_EDIT_SNAPSHOT_BYTES_PER_SECTION +
                               WORLD_LIGHT_BYTES_FIXED +
                               WORLD_LIGHT_WAVE_CAPACITY_DEFAULT * WORLD_LIGHT_BYTES_PER_WAVE + WORLD_TICK_BYTES_FIXED +
                               WORLD_TICK_NODE_CAPACITY_DEFAULT * WORLD_TICK_BYTES_PER_NODE +
                               WORLD_TICK_PAGE_CAPACITY_DEFAULT * WORLD_TICK_BYTES_PER_PAGE,
};
static_assert(WORLD_SECTION_BYTES_DEFAULT + WORLD_COLUMN_COUNT_DEFAULT * WORLD_COLUMN_BYTES_PER_SLOT +
                  WORLD_COLUMN_BYTES_FIXED <
              HEAP_BUDGET_BYTES);

/* Holds what a world's parts borrow. There is one, under app_t. map, edit, snapshot, light and tick are zero bytes until world_open. */
typedef struct world_t {
    arena_t arena_sections;         /* 0: over owned_sections; section_pool takes from it */
    arena_t arena_columns;          /* 64: over owned_columns; the column table, and what world_open puts beside it */
    heap_region_t owned_sections;   /* 128 */
    heap_region_t owned_columns;    /* 144 */
    section_pool_t section_pool;    /* 160 */
    world_map_t map;                /* 264 */
    world_edit_t edit;              /* 384 */
    uint8_t _pad0[32];              /* 480 */
    world_edit_snapshot_t snapshot; /* 512 */
    world_light_t light;            /* 1088 */
    world_tick_t tick;              /* 1312 */
    uint8_t _pad1[8];               /* 2040 */
} world_t;
static_assert(sizeof(world_t) == 2048);
static_assert(alignof(world_t) == 64);
static_assert(offsetof(world_t, owned_sections) == 128);
static_assert(offsetof(world_t, section_pool) == 160);
static_assert(offsetof(world_t, map) == 264);
static_assert(offsetof(world_t, edit) == 384);
static_assert(offsetof(world_t, snapshot) == 512);
static_assert(offsetof(world_t, light) == 1088);
static_assert(offsetof(world_t, tick) == 1312);

/* The limits a real game runs with. */
world_limits_t world_limits_default(void);

/* Reserves both regions. On failure logs why, leaves nothing behind and returns false. */
bool world_init(world_t *world, world_limits_t limits);

/* Releases what init reserved, and with it any world still open: nothing in one outlives the regions. Safe on a zeroed
 * world_t. */
void world_deinit(world_t *world);

/* Takes back every section and column at once, keeping the regions for the next world. Every pointer into either is dead, so
 * no column may be in use, and no world open: world_close is what calls this for one that was. */
void world_reset(world_t *world);

/* What world_open takes from arena_columns, for a table of this radius. */
uint64_t world_open_bytes(uint32_t table_radius);

/* Starts the map, the edit log, the snapshots, light and ticks, with a table of table_radius, centered on that chunk. Costs time and pages
 * in proportion to the table. If it will not fit the limits given to world_init, logs why, changes nothing and returns false.
 * The view radius starts at table_radius: world_map_set_view_radius narrows it. */
bool world_open(world_t *world, uint32_t table_radius, int32_t center_x, int32_t center_y);

/* Ends all of that, and every column in it, leaving the regions empty for the next. Does nothing if none is open. The reader
 * of the snapshots must have stopped. */
void world_close(world_t *world);

#endif
