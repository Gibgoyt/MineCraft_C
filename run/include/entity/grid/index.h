/* See run/src/entity/grid/README.md. */
#ifndef MC_RUN_ENTITY_GRID_INDEX_H
#define MC_RUN_ENTITY_GRID_INDEX_H

#include "arena/index.h"
#include "entity/store/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* A power of two. A body is in the cell of its box's middle. */
    ENTITY_GRID_BUCKETS = 2048u,
    /* The published limit on how many others one body is shoved by in a tick, so a crowd costs no more than a few. */
    ENTITY_GRID_PUSHES_MAX = 24u,
};
/* The side of a cell, in blocks: around what a mob's box and its reach come to. */
#define ENTITY_GRID_CELL_BLOCKS 4.0f64

/* One body, as it was when the grid was built. A cache line, since a walk reads little else. */
typedef struct entity_grid_entry_t {
    collision_box_t box;    /* 0 */
    uint64_t cell;          /* 48: its cell's key */
    entity_handle_t handle; /* 56 */
    uint32_t next;          /* 60: the next in its bucket */
} entity_grid_entry_t;
static_assert(sizeof(entity_grid_entry_t) == 64);

/* A map of where the bodies were at one moment, for finding those near a place without looking at all of them. Everything it answers
 * is of that moment, so bodies that move after it cannot change an answer, and nothing depends on the order they moved in. */
typedef struct entity_grid_t {
    entity_grid_entry_t *borrowed_entries; /* 0: ENTITY_STORE_CAPACITY, in the store's order when built */
    uint32_t *borrowed_buckets;            /* 8: ENTITY_GRID_BUCKETS */
    float64_t reach[COLLISION_AXES];       /* 16: the most any box reaches from its middle */
    uint64_t cell_walks;                   /* 40: queries answered by walking cells */
    uint64_t list_walks;                   /* 48: queries so wide that it looked at every entry instead */
    uint64_t pushes;                       /* 56: pairs pushed apart */
    uint32_t count;                        /* 64: entries */
    uint8_t _pad0[4];                      /* 68 */
} entity_grid_t;
static_assert(sizeof(entity_grid_t) == 72);

/* What a grid takes from its arena: its two arrays, with an alignment's slack for each. */
enum : uint64_t {
    ENTITY_GRID_BYTES = (uint64_t)ENTITY_STORE_CAPACITY * sizeof(entity_grid_entry_t) +
                        (uint64_t)ENTITY_GRID_BUCKETS * sizeof(uint32_t) + 2ull * alignof(entity_grid_entry_t),
};

/* An empty grid, from the arena. */
void entity_grid_start(entity_grid_t *grid, arena_t *arena);

/* Takes in where every body in the store is now. Costs time in proportion to how many. */
void entity_grid_build(entity_grid_t *grid, const entity_store_t *store);

/* Every body alive whose box, when built, overlaps box. Two that only touch do not. Writes up to max handles to out, and returns how
 * many there were, which may be more. The same grid and box give the same list. */
uint32_t entity_grid_query(entity_grid_t *grid, const entity_store_t *store, const collision_box_t *box,
                           entity_handle_t *out, uint32_t max);

/* Bodies whose boxes overlapped when built push each other apart, each by the published shove, on velocity. A pair with one waiting
 * for its column is left alone. Each body shoves at most ENTITY_GRID_PUSHES_MAX of those after it. */
void entity_grid_push_apart(entity_grid_t *grid, entity_store_t *store);

#endif
