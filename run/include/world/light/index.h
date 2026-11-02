/* See run/src/world/light/README.md. */
#ifndef MC_RUN_WORLD_LIGHT_INDEX_H
#define MC_RUN_WORLD_LIGHT_INDEX_H

#include "arena/index.h"
#include "world/column/index.h"
#include "world/edit/index.h"
#include "world/map/index.h"

#include <stdint.h>

enum : uint32_t {
    /* Log records read at a time, which light together. */
    WORLD_LIGHT_RECORDS = 256u,
    /* Levels of light, 0 to 15. */
    WORLD_LIGHT_LEVELS = SECTION_LIGHT_MAX + 1u,
};
enum : uint64_t {
    /* What world_open gives one pass. One block taken away can darken at most about 330 000 voxels: a diamond of radius 14 at
     * every height below it. A new column can raise about 1.5 million. Past this a pass stops and everything is worked out
     * again, which is correct and slow. Costs no pages until used. */
    WORLD_LIGHT_WAVE_CAPACITY_DEFAULT = 1ull << 21,
};

typedef enum world_light_status_t : uint8_t {
    WORLD_LIGHT_STATUS_OK = 0,
    /* The section pool has no room for a light array, so some voxels are wrong. It is put right by the first call to
     * world_light_update after something has gone back to the pool. */
    WORLD_LIGHT_STATUS_NO_MEMORY = 1,
} world_light_status_t;

/* Sky and block light of every column it has been told of, always the one answer to the rules in the README, whatever order
 * columns and edits came in. One thread at a time, the one holding the map. */
typedef struct world_light_t {
    world_edit_t *borrowed_edit;         /* 0 */
    world_map_t *borrowed_map;           /* 8 */
    uint32_t *borrowed_lit;              /* 16: per slot, the map's generation of the column lit there, or 0 */
    uint64_t *borrowed_touched;          /* 24: per slot, the sections this call wrote */
    uint32_t *borrowed_touched_slots;    /* 32: the slots with any */
    uint64_t *borrowed_wave;             /* 40: voxels gone dark or to look at; see light_wave_push */
    uint64_t *borrowed_nodes;            /* 48: voxels getting brighter, in lists by level */
    uint32_t *borrowed_node_next;        /* 56 */
    uint8_t *borrowed_import;            /* 64: SECTION_VOXELS, one section's light on its way in */
    world_edit_record_t *borrowed_log;   /* 72: WORLD_LIGHT_RECORDS */
    uint64_t wave_capacity;              /* 80: of the wave, and of the nodes */
    uint64_t log_cursor;                 /* 88 */
    uint64_t rebuild_count;              /* 96: times everything was worked out again */
    uint64_t overflow_count;             /* 104: of those, because a pass ran out of room */
    uint64_t lost_count;                 /* 112: of those, because the log went past the cursor */
    uint64_t voxels_written;             /* 120: for tests, and for what a pass costs */
    uint64_t failed_live_bytes;          /* 128: what the pool held when the last call refused ended */
    uint32_t bucket[WORLD_LIGHT_LEVELS]; /* 136: the first node of each level, or none */
    uint32_t wave_count;                 /* 200 */
    uint32_t node_used;                  /* 204 */
    uint32_t node_free;                  /* 208: a list of nodes done with */
    uint32_t touched_count;              /* 212 */
    uint8_t overflowed;                  /* 216: this pass ran out of room */
    uint8_t stale;                       /* 217: some voxels are wrong for lack of memory */
    uint8_t refused;                     /* 218: this call met a full pool */
    uint8_t _pad0[5];                    /* 219 */
} world_light_t;
static_assert(sizeof(world_light_t) == 224);

/* What world_light_init takes from its arena: per slot of the map, per unit of wave capacity, and besides. */
enum : uint64_t {
    WORLD_LIGHT_BYTES_PER_SLOT = sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t),
    WORLD_LIGHT_BYTES_PER_WAVE = sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint32_t),
    WORLD_LIGHT_BYTES_FIXED = 16ull * 64ull + SECTION_VOXELS + WORLD_LIGHT_RECORDS * sizeof(world_edit_record_t),
};

/* Takes from the arena, which must outlive this, and aborts as for running out if it is too small. wave_capacity is at least 1
 * and below 2^32 - 1. The log is read from where it now is. */
void world_light_init(world_light_t *light, arena_t *arena, world_edit_t *edit, uint64_t wave_capacity);
void world_light_deinit(world_light_t *light);

/* For a column just committed and filled: gives it its light, from the sky, from its own blocks and from the columns beside it
 * that are lit, and gives them what it adds. Until this is called it counts as not there, for it and for its neighbours.
 * Whatever light it held is forgotten. Calling it twice for one column does nothing. */
world_light_status_t world_light_column_added(world_light_t *light, uint32_t slot);

/* For a column about to be unloaded, before world_map_unload: takes what it gave away out of the columns beside it. Does
 * nothing for a column that was never lit. */
world_light_status_t world_light_column_leaving(world_light_t *light, uint32_t slot);

/* Brings light up to date with every edit in the log since the last call, and takes the LIGHT flags, which those edits set. An
 * edit to a column not lit yet needs nothing: it is seen when the column is. */
world_light_status_t world_light_update(world_light_t *light);

/* For whoever wrote blocks of a lit column's sections directly, past the log. Those are the only writes the log does not tell
 * of. section_mask is bits of a column, as in world_edit_sections_mark. */
world_light_status_t world_light_sections_replaced(world_light_t *light, uint32_t slot, uint64_t section_mask);

/* Works out all light in every lit column from nothing, needing no queue and no memory but the sections'. Slow, and what the
 * functions above fall back on when they must. Every section of those columns is flagged for RENDER. */
world_light_status_t world_light_rebuild(world_light_t *light);

#endif
