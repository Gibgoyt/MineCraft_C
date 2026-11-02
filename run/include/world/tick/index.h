/* See run/src/world/tick/README.md. */
#ifndef MC_RUN_WORLD_TICK_INDEX_H
#define MC_RUN_WORLD_TICK_INDEX_H

#include "arena/index.h"
#include "world/column/index.h"
#include "world/edit/index.h"
#include "world/map/index.h"

#include <stdint.h>

enum : uint32_t {
    /* The wheel of ticks due: a delay must be shorter than this. */
    WORLD_TICK_WHEEL = 64u,
    /* Log records read at a time. */
    WORLD_TICK_RECORDS = 256u,
    /* One voxel's flow level takes half a byte. */
    WORLD_TICK_PAGE_BYTES = SECTION_VOXELS / 2u,
    /* What world_open gives it. A wake waiting costs a node, and a section with flowing fluid in it a page. Neither costs
     * memory until used. */
    WORLD_TICK_NODE_CAPACITY_DEFAULT = 1u << 18,
    WORLD_TICK_PAGE_CAPACITY_DEFAULT = 1u << 14,
    /* Voxels picked in each section, each tick. */
    WORLD_TICK_RANDOM_PER_SECTION_DEFAULT = 3u,
    /* Random ticks reach this many chunks from the map's center, in a square. */
    WORLD_TICK_RANDOM_RADIUS_DEFAULT = 8u,
    /* The most one tick does. What is left waits for the next, and the edit log, at 65536 records, never laps its readers. */
    WORLD_TICK_FIRES_PER_TICK = 16384u,
    WORLD_TICK_WRITES_PER_TICK = 16384u,
    /* Blocks gone by themselves between two looks. More than this at once and the oldest gives nothing. */
    WORLD_TICK_BROKEN = 64u,
};

/* A fluid voxel's level. Water with no level written down is a source, so that water terrain made needs no memory here. */
enum : uint8_t {
    WORLD_TICK_LEVEL_SOURCE = 0u,
    WORLD_TICK_LEVEL_FLOW_MAX = 7u, /* from 1 up: how far from what feeds it */
    WORLD_TICK_LEVEL_FALLING = 8u,  /* fed from above */
};

/* What is known about the column in one slot of the map. Zero bytes mean nothing. */
typedef struct world_tick_column_t {
    uint16_t pages[COLUMN_SECTIONS]; /* 0: a section's page of levels, plus 1, or 0 for none */
    uint32_t generation;             /* 96: the map's, for the load these describe */
} world_tick_column_t;
static_assert(sizeof(world_tick_column_t) == 100);

/* One voxel waiting for its tick. */
typedef struct world_tick_node_t {
    int32_t x;           /* 0 */
    int32_t y;           /* 4 */
    uint32_t next;       /* 8: the next in its list */
    uint32_t generation; /* 12: of the column it was for, so that it is not run on a new one */
    int16_t z;           /* 16 */
    uint8_t _pad0[2];    /* 18 */
} world_tick_node_t;
static_assert(sizeof(world_tick_node_t) == 20);

/* A block with drops that went by itself: washed away, buried by what fell, decayed, or left with nothing under it. */
typedef struct world_tick_broken_t {
    int32_t x;        /* 0 */
    int32_t y;        /* 4 */
    int32_t z;        /* 8 */
    uint16_t block;   /* 12 */
    uint8_t _pad0[2]; /* 14 */
} world_tick_broken_t;
static_assert(sizeof(world_tick_broken_t) == 16);

/* Everything that happens to blocks by itself: flow, falling, decay, growth. One thread at a time, the one holding the map. */
typedef struct world_tick_t {
    world_edit_t *borrowed_edit;           /* 0 */
    world_map_t *borrowed_map;             /* 8 */
    world_tick_column_t *borrowed_columns; /* 16: per slot */
    world_tick_node_t *borrowed_nodes;     /* 24: node_capacity */
    uint32_t *borrowed_set;                /* 32: which voxels have a node: a node's number plus 1, or 0 */
    uint8_t *borrowed_pages;               /* 40: page_capacity pages of levels */
    uint16_t *borrowed_page_next;          /* 48: a list of pages not in use */
    uint16_t *borrowed_page_used;          /* 56: per page, the voxels with a level other than 0 */
    world_edit_record_t *borrowed_log;     /* 64: WORLD_TICK_RECORDS */
    world_tick_broken_t *borrowed_broken;  /* 72: a ring of WORLD_TICK_BROKEN */
    uint64_t seed;                         /* 80 */
    uint64_t now;                          /* 88: ticks run */
    uint64_t log_cursor;                   /* 96 */
    uint64_t fired_count;                  /* 104: ticks that came due and were run */
    uint64_t random_count;                 /* 112: voxels picked at random that do something */
    uint64_t lost_count;                   /* 120: log records that went by unread, whose neighbours were never woken */
    uint64_t node_full_count;              /* 128: wakes dropped for lack of a node */
    uint64_t page_full_count;              /* 136: fluid held back for lack of a page */
    uint64_t no_memory_count;              /* 144: writes the section pool refused */
    uint64_t broken_lost_count;            /* 152: pushed out of a full ring before anyone took them */
    uint32_t wheel_head[WORLD_TICK_WHEEL]; /* 160: per tick modulo the wheel, the first node due, or UINT32_MAX */
    uint32_t wheel_tail[WORLD_TICK_WHEEL]; /* 416 */
    uint32_t node_capacity;                /* 672 */
    uint32_t node_carved;                  /* 676: nodes ever used; the rest are untouched */
    uint32_t node_free;                    /* 680: a list of nodes done with, or UINT32_MAX */
    uint32_t node_live;                    /* 684 */
    uint32_t set_mask;                     /* 688: the size of the set, minus 1 */
    uint32_t page_capacity;                /* 692 */
    uint32_t page_carved;                  /* 696 */
    uint32_t page_live;                    /* 700 */
    uint32_t random_per_section;           /* 704 */
    uint32_t random_radius;                /* 708 */
    uint32_t broken_head;                  /* 712: the oldest in the ring */
    uint32_t broken_count;                 /* 716 */
    uint16_t page_free;                    /* 720: a page's number plus 1, or 0 */
    uint8_t _pad0[6];                      /* 722 */
} world_tick_t;
static_assert(sizeof(world_tick_t) == 728);

/* What world_tick_init takes from its arena: per slot of the map, per node, per page, and besides. */
enum : uint64_t {
    WORLD_TICK_BYTES_PER_SLOT = sizeof(world_tick_column_t),
    WORLD_TICK_BYTES_PER_NODE = sizeof(world_tick_node_t) + 2ull * sizeof(uint32_t),
    WORLD_TICK_BYTES_PER_PAGE = WORLD_TICK_PAGE_BYTES + 2ull * sizeof(uint16_t),
    WORLD_TICK_BYTES_FIXED = 9ull * 64ull + WORLD_TICK_RECORDS * sizeof(world_edit_record_t) +
                             WORLD_TICK_BROKEN * sizeof(world_tick_broken_t),
};

/* node_capacity is a power of two, at least 2, and page_capacity from 1 to 65535. Takes from the arena, which must outlive this,
 * and aborts as for running out if it is too small. Reads the edit log from where it is now: what came before wakes nothing. */
void world_tick_init(world_tick_t *tick, arena_t *arena, world_edit_t *edit, uint32_t node_capacity,
                     uint32_t page_capacity);
void world_tick_deinit(world_tick_t *tick);

/* What random ticks come from. The same seed, the same edits and the same columns give the same world. */
void world_tick_seed_set(world_tick_t *tick, uint64_t seed);

/* A column going away: its levels go back, and anything waiting there is dropped when it comes due. */
void world_tick_column_leaving(world_tick_t *tick, uint32_t slot);

/* One tick of game time: wakes what the edit log says changed, runs what is due, and runs the random ticks. Its writes go through
 * world_edit_block_set, so light and the renderer see them as any other edit. */
void world_tick_step(world_tick_t *tick);

/* The oldest block that went by itself and has not been asked for, which is now the caller's. False if there is none. Whoever puts
 * items in the world rolls its drops: this stays on the block's side of that line. */
bool world_tick_broken_next(world_tick_t *tick, world_tick_broken_t *out);

/* A level from above. Means nothing unless the voxel holds a fluid, and is the level of a source where nothing is known. */
uint8_t world_tick_fluid_level(const world_tick_t *tick, int32_t x, int32_t y, int32_t z);

/* Voxels waiting for a tick. 0 means that nothing will change except at random. */
static inline uint32_t world_tick_pending(const world_tick_t *tick) {
    return tick->node_live;
}

#endif
