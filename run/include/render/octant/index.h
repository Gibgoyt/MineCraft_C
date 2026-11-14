/* See run/src/render/octant/README.md. */
#ifndef MC_RUN_RENDER_OCTANT_INDEX_H
#define MC_RUN_RENDER_OCTANT_INDEX_H

#include "arena/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "render/encode/index.h"
#include "tuning/render/budget/index.h"
#include "tuning/render/lod/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Tables of octants, one for each level that the one above is made from: level 0 (the blocks) up to level 2. compile/config/render/lod/
     * index.yaml's octant_levels says how many are used, and may not ask for more. */
    RENDER_OCTANT_LEVELS_MAX = 3u,
    /* A level above the blocks is at most this wide in its own columns (radius_columns_max each way), so its table has this many slots. */
    RENDER_OCTANT_LEVEL_SIDE_MAX = 2u * MC_TUNE_RENDER_LOD_RADIUS_COLUMNS_MAX + 1u,
    /* An octant's handle: 0 is air, the high bit with light << 16 and the block is one block and light all through, and anything else is 1 + the
     * index of its 1536 bytes in the store's blocks. */
    RENDER_OCTANT_AIR = 0u,
    RENDER_OCTANT_UNIFORM = 1u << 31,
    /* What the store's blocks may come to: compile/config/render/budget/index.yaml. */
    RENDER_OCTANT_STORE_MIB = MC_TUNE_RENDER_BUDGET_OCTANT_STORE_MIB,
    RENDER_OCTANT_BLOCK_CAPACITY =
        (uint32_t)(((uint64_t)RENDER_OCTANT_STORE_MIB << 20) / sizeof(render_encode_octant_t)),
};
static_assert(MC_TUNE_RENDER_LOD_OCTANT_LEVELS <= RENDER_OCTANT_LEVELS_MAX);
/* A block's index and the uniform bit never meet. */
static_assert(RENDER_OCTANT_BLOCK_CAPACITY < RENDER_OCTANT_UNIFORM);

/* One column of a level's table: the octants of each of its sections, for the level above. */
typedef struct render_octant_column_t {
    int32_t chunk_x; /* 0: in this level's columns */
    int32_t chunk_y; /* 4 */
    uint32_t
        generation; /* 8: level 0: the map's load; above: the generation the pyramid's level gave the column it was made from */
    uint8_t valid;    /* 12: these are the column's octants */
    uint8_t complete; /* 13: no octant was left out for want of room, so the level above may be made from them */
    uint8_t _pad0[2]; /* 14 */
} render_octant_column_t;
static_assert(sizeof(render_octant_column_t) == 16);

/* A level's octants, slot for slot as that level's own table: the map's for level 0, the pyramid level's above. */
typedef struct render_octant_table_t {
    render_octant_column_t *borrowed_columns; /* 0: slot_capacity */
    uint32_t *borrowed_handles;               /* 8: slot_capacity * sections */
    uint32_t slot_capacity;                   /* 16 */
    uint32_t sections;                        /* 20: in a column of this level: 96 >> level */
} render_octant_table_t;
static_assert(sizeof(render_octant_table_t) == 24);

/* What has happened since the store opened. */
typedef struct render_octant_stats_t {
    uint64_t octants_stored;                              /* 0 */
    uint64_t octants_refused;                             /* 8: no room, so their column is not complete */
    uint64_t columns_assembled[RENDER_OCTANT_LEVELS_MAX]; /* 16: by the level made, 1 to 3, at [level - 1] */
    uint64_t assemble_ns;                                 /* 40: assembling and encoding, on the gpu thread */
    uint64_t bytes_sent;                                  /* 48: of what was assembled */
    uint64_t changed_lost;                                /* 56: times the list of changed columns ran over */
} render_octant_stats_t;
static_assert(sizeof(render_octant_stats_t) == 64);

/* For a test on the game's own threads: the voxel holding this place in each level the store assembles, as it was last sent. */
typedef struct render_octant_watch_t {
    _Atomic int64_t
        seen_ns[RENDER_OCTANT_LEVELS_MAX + 1u]; /* 0: by shift: when a column holding the place was last sent, or 0 */
    _Atomic uint32_t material[RENDER_OCTANT_LEVELS_MAX + 1u]; /* 32: what its voxel was then */
    int32_t x;                                                /* 48: in blocks */
    int32_t y;                                                /* 52 */
    int32_t z;                                                /* 56 */
    _Atomic uint32_t armed;                                   /* 60 */
} render_octant_watch_t;
static_assert(sizeof(render_octant_watch_t) == 64);

/* All of it. The gpu thread, or whichever one thread a run has, calls everything here, but render_octant_watch_* from a test. */
typedef struct render_octant_t {
    render_octant_table_t tables[RENDER_OCTANT_LEVELS_MAX]; /* 0 */
    render_encode_octant_t *borrowed_blocks;                /* 72: block_capacity */
    int32_t *borrowed_changed; /* 80: x, y pairs of level 0 columns: changed_capacity of them */
    uint32_t block_capacity;   /* 88 */
    uint32_t block_carved;     /* 92: never handed out from here on */
    uint32_t block_free;       /* 96: 1 + a block given back, whose first word is the next, or 0 */
    uint32_t blocks_live;      /* 100 */
    uint32_t changed_capacity; /* 104 */
    uint32_t changed_count;    /* 108 */
    uint8_t changed_over;      /* 112: the list ran over: every column may have changed */
    uint8_t
        arriving; /* 113: the upload is part way through a view: level 0's columns may be missing sections it holds */
    uint8_t _pad0[2];      /* 114 */
    uint32_t changes_said; /* 116: level 0 columns said to have changed, all told; wraps */
    uint64_t
        octants_unchanged; /* 120: level 0 sections sent again with the octant the store held: not said to have changed */
    render_octant_stats_t stats; /* 128 */
    render_octant_watch_t watch; /* 192 */
} render_octant_t;
static_assert(sizeof(render_octant_t) == 256);
static_assert(offsetof(render_octant_t, stats) == 128);
static_assert(offsetof(render_octant_t, watch) == 192);

/* One section of a column a level above is made of: the cells hold it, or it is one block and light all through. */
typedef struct render_octant_section_t {
    uint16_t block_uniform; /* 0: unless assembled */
    uint8_t light_uniform;  /* 2 */
    uint8_t assembled;      /* 3: its 4096 voxels are in the cells */
} render_octant_section_t;
static_assert(sizeof(render_octant_section_t) == 4);

/* What init takes from its arena for a map table of this many slots. */
uint64_t render_octant_arena_bytes(uint32_t map_slots);

/* Takes it, all of it zero. The arena must outlive it. */
void render_octant_init(render_octant_t *store, arena_t *arena, uint32_t map_slots);

/* Forgets it all; its memory is its arena's, which goes with the arena. */
void render_octant_deinit(render_octant_t *store);

/* Bytes the store holds now: its tables and the blocks in use. */
uint64_t render_octant_bytes_used(const render_octant_t *store);

/* ---- Level 0, from the upload, as its table changes and its sections come ------------------------------------------- */

/* The map's slot holds this load of a column: if it held another, its octants go, and the new one starts all air as a load does. */
void render_octant_column_set(render_octant_t *store, uint32_t slot, int32_t chunk_x, int32_t chunk_y,
                              uint32_t generation);

/* No column there any more. */
void render_octant_column_clear(render_octant_t *store, uint32_t slot);

/* A section of a level's column is now this. Level 0's is said to have changed, for the level above. */
void render_octant_section_set(render_octant_t *store, uint32_t level, uint32_t slot, uint32_t section,
                               const render_encode_octant_t *octant);

/* Whether the upload is part way through a view, set by it at the end of each of its frames: the columns its table holds may still be waiting
 * for sections, so the level above is not made from them yet. */
void render_octant_arriving_set(render_octant_t *store, bool arriving);

/* The next level 0 column that has changed since this was last asked, in chunks, oldest first. */
bool render_octant_changed_next(render_octant_t *store, int32_t *out_chunk_x, int32_t *out_chunk_y);

/* Whether the list of changed columns ran over since this was last asked, so that every column may have changed. */
bool render_octant_changed_lost_take(render_octant_t *store);

/* ---- The levels above ------------------------------------------------------------------------------------------------ */

/* Every table above level 0 forgotten, as when the pyramid's levels change: their generations start again. */
void render_octant_levels_forget(render_octant_t *store);

/* A column of level + 1 is made from 2 x 2 columns of level; this says whether all four are in that level's table, buffers, now, with all their
 * octants here: each slot in out_slots, x fastest. */
bool render_octant_column_ready(const render_octant_t *store, uint32_t level, const world_buffers_t *buffers,
                                int32_t column_x, int32_t column_y, uint32_t out_slots[4]);

/* From those four, every section of the column above (sections of level + 1): each one block all through, or its 4096 voxels in cells and light
 * at section * 4096. Returns the highest voxel with a block, or -1. */
int32_t render_octant_column_assemble(const render_octant_t *store, uint32_t level, const uint32_t slots[4],
                                      uint16_t *cells, uint8_t *light, render_octant_section_t *sections);

/* A level above 0 starts this column's octants in its table, at its slot, for the generation the pyramid gave it: what was there goes. */
void render_octant_level_column_set(render_octant_t *store, uint32_t level, uint32_t slot, int32_t column_x,
                                    int32_t column_y, uint32_t generation);

/* ---- The watch --------------------------------------------------------------------------------------------------------- */

/* A test, with the threads that write the store stopped: from now on each level the store assembles notes this place. */
void render_octant_watch_arm(render_octant_t *store, int32_t x, int32_t y, int32_t z);

/* The gpu thread: a column of level shift holding the watched place was just sent, its voxel there this block. */
void render_octant_watch_note(render_octant_t *store, uint32_t shift, uint16_t material, int64_t now_ns);

#endif
