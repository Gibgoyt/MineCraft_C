/* See run/src/entity/item_drop/README.md. */
#ifndef MC_RUN_ENTITY_ITEM_DROP_INDEX_H
#define MC_RUN_ENTITY_ITEM_DROP_INDEX_H

#include "arena/index.h"
#include "physics/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Stacks in the world at once. Merging keeps a pile to a few, so this takes a lot of digging apart. */
    ITEM_DROP_CAPACITY = 4096u,
    /* A power of two, for the grid merging looks through. */
    ITEM_DROP_GRID_BUCKETS = 4096u,
    /* The published numbers. */
    ITEM_DROP_STACK_MAX = 64u,
    ITEM_DROP_DESPAWN_TICKS = 6000u,
    ITEM_DROP_PICKUP_DELAY_TICKS = 10u,
    ITEM_DROP_MERGE_EVERY_TICKS = 2u,
    /* What lies still looks at the world this often, each at a tick of its own. */
    ITEM_DROP_REST_EVERY_TICKS = 4u,
};

/* The side of an item's box. Its position is the box's middle. */
#define ITEM_DROP_SIZE 0.25f64
/* How far an item reaches for another, past its own box, sideways. Upward it reaches no further than its box. */
#define ITEM_DROP_MERGE_REACH 0.5f64

/* One stack lying in the world, or on its way there. */
typedef struct item_drop_t {
    float64_t position[COLLISION_AXES]; /* 0: the middle of its box */
    float64_t velocity[COLLISION_AXES]; /* 24: in blocks per tick */
    uint32_t age;                       /* 48: ticks in a loaded column */
    uint32_t id;                        /* 52: which tick it looks at the world on, when it lies still */
    uint16_t item;                      /* 56: into block_drop_items */
    uint16_t count;                     /* 58: 1 to ITEM_DROP_STACK_MAX, and 0 only in the middle of a merge */
    uint16_t pickup_delay;              /* 60: ticks until it can be picked up */
    uint8_t on_ground;                  /* 62 */
    uint8_t _pad0[1];                   /* 63 */
} item_drop_t;
static_assert(sizeof(item_drop_t) == 64);

/* What a store takes from its arena: its three arrays, with an alignment's slack for each. */
enum : uint64_t {
    ITEM_DROP_STORE_BYTES = (uint64_t)ITEM_DROP_CAPACITY * sizeof(item_drop_t) +
                            (uint64_t)ITEM_DROP_GRID_BUCKETS * sizeof(uint32_t) +
                            (uint64_t)ITEM_DROP_CAPACITY * sizeof(uint32_t) + 3ull * alignof(item_drop_t),
};

/* Every stack in a world. Plain numbers, so that the same world and drops give the same bytes. The sim's. */
typedef struct item_drop_store_t {
    item_drop_t
        *borrowed_items; /* 0: the first count of ITEM_DROP_CAPACITY are in use, in no order that means anything */
    uint32_t *borrowed_buckets; /* 8: ITEM_DROP_GRID_BUCKETS, the first stack of each, for merging */
    uint32_t *borrowed_chain;   /* 16: ITEM_DROP_CAPACITY, the next stack in a bucket */
    terrain_rng_t random;       /* 24: how each one scatters */
    uint64_t tick_count;        /* 32 */
    /* Counted in items, so that these add up: dropped is all the others, plus what is on the ground. */
    uint64_t dropped_count;   /* 40 */
    uint64_t picked_up_count; /* 48 */
    uint64_t despawned_count; /* 56: lay too long */
    uint64_t destroyed_count; /* 64: lava and cactus */
    uint64_t lost_count;      /* 72: pushed out of a full store */
    uint64_t merged_count;    /* 80: stacks, not items: those that went wholly into another */
    uint32_t count;           /* 88: stacks in use */
    uint32_t next_id;         /* 92 */
} item_drop_store_t;
static_assert(sizeof(item_drop_store_t) == 96);

/* How many items the taker takes of count, which may be none: a hand that is full or holds something else. On the sim thread. */
typedef uint32_t item_drop_take_t(void *borrowed_context, uint16_t item, uint32_t count);

/* An empty store, from the arena, with scatter from this seed. */
void item_drop_store_start(item_drop_store_t *store, arena_t *arena, uint64_t seed);

/* An item's box, at a place. */
collision_box_t item_drop_box(const float64_t position[COLLISION_AXES]);

/* Count items thrown out of a broken block, around this place, in stacks of at most ITEM_DROP_STACK_MAX. If the store is full, the
 * oldest stack goes to make room for each new one. */
void item_drop_spawn(item_drop_store_t *store, uint16_t item, uint32_t count, const float64_t position[COLLISION_AXES]);

/* One tick of everything: gravity, drag, the world's blocks, fluids, old age and merging. A stack in a column not loaded waits, and
 * does not age. Physics and map are the sim's. Never waits and never touches the heap. */
void item_drop_tick(item_drop_store_t *store, physics_t *physics, const world_map_t *map);

/* Offers the taker each stack whose delay is over and whose box overlaps reach, and removes what it takes. Returns how many items it
 * took. Never waits and never touches the heap. */
uint32_t item_drop_pickup(item_drop_store_t *store, const collision_box_t *reach, item_drop_take_t *take,
                          void *borrowed_context);

/* How many items are on the ground, in all its stacks. */
uint64_t item_drop_on_ground(const item_drop_store_t *store);

#endif
