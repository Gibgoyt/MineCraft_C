/* See run/src/world/map/README.md. */
#ifndef MC_RUN_WORLD_MAP_INDEX_H
#define MC_RUN_WORLD_MAP_INDEX_H

#include "arena/index.h"
#include "assert/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <stdint.h>

enum : uint32_t {
    /* Offsets from the center are stored in 16 bits, and a slot number in 32. */
    WORLD_MAP_RADIUS_MAX = 32767u,
    /* A loaded column is kept this many chunks past the view radius, so that walking back and forth over one line does
     * not stream the same columns out and in again. */
    WORLD_MAP_KEEP_EXTRA = 2u,
};

typedef enum world_map_state_t : uint8_t {
    WORLD_MAP_STATE_EMPTY = 0,
    WORLD_MAP_STATE_PENDING = 1, /* handed out by world_map_stream_take, and no column yet */
    WORLD_MAP_STATE_READY = 2,   /* its column is live */
} world_map_state_t;

/* What the map knows about one place in the table. Zero bytes are an empty slot. */
typedef struct world_map_slot_t {
    int32_t chunk_x;       /* 0 */
    int32_t chunk_y;       /* 4 */
    uint32_t generation;   /* 8: counts every request for this slot, so a new occupant is never taken for the last */
    uint32_t active_index; /* 12: its place in borrowed_active, unless empty */
    uint8_t state;         /* 16: world_map_state_t */
    uint8_t leaving;       /* 17: READY and out of range: see world_map_stream_leaving */
    uint8_t _pad0[6];      /* 18 */
} world_map_slot_t;
static_assert(sizeof(world_map_slot_t) == 24);

typedef struct world_map_offset_t {
    int16_t dx; /* 0 */
    int16_t dy; /* 2 */
} world_map_offset_t;
static_assert(sizeof(world_map_offset_t) == 4);

/* A column to build. */
typedef struct world_map_request_t {
    int32_t chunk_x;     /* 0 */
    int32_t chunk_y;     /* 4 */
    uint32_t slot;       /* 8 */
    uint32_t generation; /* 12 */
} world_map_request_t;
static_assert(sizeof(world_map_request_t) == 16);

/* A column that has gone out of range and is still there. */
typedef struct world_map_leaving_t {
    int32_t chunk_x; /* 0 */
    int32_t chunk_y; /* 4 */
    uint32_t slot;   /* 8 */
} world_map_leaving_t;
static_assert(sizeof(world_map_leaving_t) == 12);

typedef enum world_map_status_t : uint8_t {
    WORLD_MAP_STATUS_OK = 0,
    WORLD_MAP_STATUS_Z_OUT_OF_RANGE = 1, /* outside gameplay's range, whether or not the column is there */
    WORLD_MAP_STATUS_NOT_LOADED = 2,     /* no live column there */
} world_map_status_t;

/* A square table of columns that scrolls without moving anything: a column's slot depends only on its coordinates modulo
 * the side, so when the center moves only the columns that change get touched. One thread at a time, the one holding the
 * section pool. */
typedef struct world_map_t {
    section_pool_t *borrowed_pool;        /* 0 */
    column_t *borrowed_columns;           /* 8: slot_count; untouched where nothing has streamed in */
    world_map_slot_t *borrowed_slots;     /* 16: slot_count */
    uint32_t *borrowed_active;            /* 24: the slots not empty, in no order */
    world_map_offset_t *borrowed_offsets; /* 32: every offset within table_radius, the nearest first */
    uint64_t change_count;                /* 40: counts everything a snapshot of the table could notice */
    int32_t center_x;                     /* 48 */
    int32_t center_y;                     /* 52 */
    int32_t min_x;                        /* 56: the window's lowest corner, center - table_radius */
    int32_t min_y;                        /* 60 */
    uint32_t min_slot_x;                  /* 64: where that corner is in the table */
    uint32_t min_slot_y;                  /* 68 */
    uint32_t table_radius;                /* 72 */
    uint32_t side;                        /* 76: 2 * table_radius + 1 */
    uint32_t slot_count;                  /* 80: side * side */
    uint32_t view_radius;                 /* 84: columns this near are wanted; never more than table_radius */
    uint32_t offset_count;                /* 88 */
    uint32_t view_offset_count;           /* 92: how many of the offsets are within view_radius */
    uint32_t stream_cursor;               /* 96: every offset before this is built, on its way, or blocked */
    uint32_t leaving_cursor;              /* 100 */
    uint32_t active_count;                /* 104 */
    uint32_t pending_count;               /* 108 */
    uint32_t ready_count;                 /* 112 */
    uint32_t leaving_count;               /* 116 */
} world_map_t;
static_assert(sizeof(world_map_t) == 120);

/* What world_map_init takes from its arena, for a table of slot_count, apart from the columns: sizeof(column_t) each. The
 * last term of the sum is the scratch of the sort of distances. */
enum : uint64_t {
    WORLD_MAP_BYTES_PER_SLOT =
        sizeof(world_map_slot_t) + sizeof(uint32_t) + sizeof(world_map_offset_t) + sizeof(uint32_t),
    WORLD_MAP_BYTES_FIXED = 4ull * 64ull, /* room for alignment, and for that scratch in a table of one */
};

/* The largest table radius column_count columns can hold. There must be at least one. */
uint32_t world_map_radius_for(uint64_t column_count);

/* Takes the tables from the arena, which must outlive the map, and costs time and pages in proportion to the table, not
 * to the arena. Aborts as for running out if the arena is too small. The view radius starts at table_radius. */
void world_map_init(world_map_t *map, arena_t *arena, section_pool_t *pool, uint32_t table_radius, int32_t center_x,
                    int32_t center_y);

/* Returns every live column's arrays to the pool. Safe on zero bytes. */
void world_map_deinit(world_map_t *map);

/* Which slot a chunk has in a window, with no division: this runs per voxel. False outside the window, where a chunk
 * has no slot. Both a map and a snapshot of one use this. */
static inline bool world_map_slot_in_window(int32_t min_x, int32_t min_y, uint32_t min_slot_x, uint32_t min_slot_y,
                                            uint32_t side, int32_t chunk_x, int32_t chunk_y, uint32_t *out_slot) {
    const int64_t dx = (int64_t)chunk_x - min_x;
    const int64_t dy = (int64_t)chunk_y - min_y;
    if (dx < 0 || dy < 0 || dx >= (int64_t)side || dy >= (int64_t)side) {
        return false;
    }
    uint32_t slot_x = min_slot_x + (uint32_t)dx;
    uint32_t slot_y = min_slot_y + (uint32_t)dy;
    slot_x = slot_x >= side ? slot_x - side : slot_x;
    slot_y = slot_y >= side ? slot_y - side : slot_y;
    *out_slot = slot_y * side + slot_x;
    return true;
}

static inline bool world_map_slot_of(const world_map_t *map, int32_t chunk_x, int32_t chunk_y, uint32_t *out_slot) {
    return world_map_slot_in_window(map->min_x, map->min_y, map->min_slot_x, map->min_slot_y, map->side, chunk_x,
                                    chunk_y, out_slot);
}

/* The live column there, or nullptr. One that is leaving still counts while it is inside the window. */
static inline column_t *world_map_column_at(const world_map_t *map, int32_t chunk_x, int32_t chunk_y) {
    uint32_t slot = 0;
    if (!world_map_slot_of(map, chunk_x, chunk_y, &slot)) {
        return nullptr;
    }
    const world_map_slot_t *const entry = &map->borrowed_slots[slot];
    if (entry->state != WORLD_MAP_STATE_READY || entry->chunk_x != chunk_x || entry->chunk_y != chunk_y) {
        return nullptr;
    }
    return &map->borrowed_columns[slot];
}

/* Moves the window. Costs one look at each column there is, not at the table, and frees nothing itself: what falls out of
 * range is offered by world_map_stream_leaving. A request now out of range is dropped, and its commit will return nullptr.
 * Each coordinate must be one column_chunk_of can return. */
void world_map_set_center(world_map_t *map, int32_t center_x, int32_t center_y);

/* How far columns are wanted, for a render distance setting. Must not be more than table_radius. */
void world_map_set_view_radius(world_map_t *map, uint32_t view_radius);

/* Up to max of the nearest columns that are wanted and have no column and no request, nearest first by the distance
 * between chunks, circular. Each is then PENDING until committed or cancelled. Returns how many. A place still held by a
 * column that is leaving is skipped, and offered once that column is unloaded. */
uint32_t world_map_stream_take(world_map_t *map, world_map_request_t *out, uint32_t max);

/* For a request that came back: its column, ready to be filled, all air under open sky. nullptr if the request is out of
 * date, so that there is nowhere to put its result and it should be dropped. */
column_t *world_map_stream_commit(world_map_t *map, const world_map_request_t *request);

/* For a request that will never come back. False if it is out of date. If still wanted, it will be offered again. */
bool world_map_stream_cancel(world_map_t *map, const world_map_request_t *request);

/* Offers a column that is out of range, or returns false if there is none. It goes on being offered until unloaded, and
 * stops being offered if the center comes back before then. Whoever needs a last look at it takes it now: this is how an
 * edited column gets saved before it goes. */
bool world_map_stream_leaving(world_map_t *map, world_map_leaving_t *out);

/* Ends a live column and returns its arrays to the pool. If it is still wanted, it will be offered again by
 * world_map_stream_take: this is also what to do with a column that could not be filled. */
void world_map_unload(world_map_t *map, uint32_t slot);

/* One voxel, at world coordinates, or why not, with *out left alone. */
world_map_status_t world_map_block_get(const world_map_t *map, int32_t x, int32_t y, int32_t z, uint16_t *out);
world_map_status_t world_map_light_get(const world_map_t *map, int32_t x, int32_t y, int32_t z, uint8_t *out);

#endif
