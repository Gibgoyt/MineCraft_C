/* See run/src/entity/store/README.md. */
#ifndef MC_RUN_ENTITY_STORE_INDEX_H
#define MC_RUN_ENTITY_STORE_INDEX_H

#include "arena/index.h"
#include "physics/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Bodies in the world at once, far past the plan's 100. A power of two, for what a handle holds. */
    ENTITY_STORE_CAPACITY = 1024u,
    ENTITY_STORE_SLOT_BITS = 10u,
    ENTITY_STORE_SLOT_MASK = ENTITY_STORE_CAPACITY - 1u,
    ENTITY_STORE_GENERATION_LIMIT = 1u << (32u - ENTITY_STORE_SLOT_BITS),
};
static_assert(1u << ENTITY_STORE_SLOT_BITS == ENTITY_STORE_CAPACITY);

/* Names one body for as long as it lives, wherever the arrays move it, and nothing ever after: a slot that is used again has a new
 * generation. */
typedef uint32_t entity_handle_t;
enum : entity_handle_t { ENTITY_HANDLE_NONE = 0u };

/* What the slot in a handle is for: 0 to ENTITY_STORE_CAPACITY - 1, and the same for the body's life, so that a system keeping
 * something per body can index its own array by it. */
static inline uint32_t entity_handle_slot(entity_handle_t handle) {
    return handle & ENTITY_STORE_SLOT_MASK;
}

/* A flag of a body. */
enum : uint8_t {
    ENTITY_STORE_FLAG_ON_GROUND = 1u << 0u,
    ENTITY_STORE_FLAG_IN_WATER = 1u << 1u,
    ENTITY_STORE_FLAG_IN_LAVA = 1u << 2u,
    ENTITY_STORE_FLAG_CLIMBING = 1u << 3u,
    ENTITY_STORE_FLAG_HIT_WALL = 1u << 4u, /* something stopped it sideways on its last move */
    ENTITY_STORE_FLAG_WAITING = 1u << 5u,  /* a column it is over is not loaded, so it did not move and did not age */
    ENTITY_STORE_FLAG_JUMP = 1u << 6u,     /* entity_store_jump asked; the next moving tick uses it up */
};

/* Whether a body is male or female, and none for a kind that is neither, such as every mob without male: and female: parts in
 * config/mobs/. The same numbers as MOB_SEX_ in tables/mobs/, which run/src/mob/spawn/ asserts. */
enum : uint8_t {
    ENTITY_STORE_SEX_NONE = 0u,
    ENTITY_STORE_SEX_MALE = 1u,
    ENTITY_STORE_SEX_FEMALE = 2u,
    ENTITY_STORE_SEX_COUNT = 3u,
};

/* What a new body starts as. Its position is the middle of the soles of its feet, as a player's is. */
typedef struct entity_store_spawn_t {
    float64_t position[COLLISION_AXES]; /* 0 */
    float32_t half_width;               /* 24 */
    float32_t height;                   /* 28 */
    float32_t yaw;                      /* 32: radians; it faces (cos, sin), as render_camera_t does */
    float32_t health;                   /* 36 */
    uint16_t kind;                      /* 40: whoever spawns it says what this means */
    uint8_t sex;                        /* 42: ENTITY_STORE_SEX_ */
    uint8_t _pad0[5];                   /* 43 */
} entity_store_spawn_t;
static_assert(sizeof(entity_store_spawn_t) == 48);

/* Every body in a world, one array per field. The first count of each array are in use, in no order that means anything: a removal
 * moves the last body into the gap. So the arrays are read and written by index in one go, and a handle is what is kept for later.
 * Anything but the handle tables, the handle array and count may be written by index, which is how an AI or a blow changes a body.
 * Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct entity_store_t {
    float64_t *borrowed_position[COLLISION_AXES]; /* 0 */
    float64_t *borrowed_previous[COLLISION_AXES]; /* 24: position at the start of the last tick */
    float64_t *borrowed_velocity[COLLISION_AXES]; /* 48: blocks per tick */
    float64_t *borrowed_wish[2];                  /* 72: where it wants to walk, x and y, in blocks per tick */
    float64_t *borrowed_fall_distance;            /* 88: how far it has fallen without landing */
    float64_t *borrowed_landed_fall;              /* 96: how far it fell, on the tick it landed; otherwise 0 */
    float32_t *borrowed_half_width;               /* 104 */
    float32_t *borrowed_height;                   /* 112 */
    float32_t *borrowed_yaw;                      /* 120 */
    float32_t *borrowed_health;                   /* 128 */
    float32_t *borrowed_limb_phase;               /* 136: see run/src/entity/animation/ */
    float32_t *borrowed_limb_amount;              /* 144 */
    float32_t *borrowed_head_yaw;                 /* 152: from yaw */
    float32_t *borrowed_head_pitch;               /* 160: up */
    float32_t *borrowed_aim;                      /* 168: 0 to 1: how far its arms are raised to aim */
    float32_t *borrowed_swell;                    /* 176: 0 to 1: how near it is to bursting */
    entity_handle_t *borrowed_handle;             /* 184 */
    uint32_t *borrowed_age;                       /* 192: ticks in loaded columns */
    uint32_t *borrowed_slot_index;                /* 200: ENTITY_STORE_CAPACITY, by slot: where its body is now */
    uint32_t *borrowed_slot_generation;           /* 208: ENTITY_STORE_CAPACITY, by slot: of its next or present body */
    uint32_t *borrowed_free_slots;                /* 216: ENTITY_STORE_CAPACITY, a ring: the longest free goes first */
    uint16_t *borrowed_kind;                      /* 224 */
    uint8_t *borrowed_flags;                      /* 232 */
    uint8_t *borrowed_swing_ticks;                /* 240: ticks of its attack left */
    uint8_t *borrowed_sex;                        /* 248: ENTITY_STORE_SEX_, for as long as it lives */
    uint64_t spawned_count;                       /* 256 */
    uint64_t removed_count;                       /* 264 */
    uint64_t refused_count;                       /* 272: asked for when full */
    uint32_t count;                               /* 280 */
    uint32_t free_head;                           /* 284 */
} entity_store_t;
static_assert(sizeof(entity_store_t) == 288);

/* What a store takes from its arena: each array, with an alignment's slack. */
enum : uint64_t {
    ENTITY_STORE_ARRAY_ALIGNMENT = 64u,
    ENTITY_STORE_BYTES =
        (uint64_t)ENTITY_STORE_CAPACITY * (13ull * sizeof(float64_t) + 10ull * sizeof(float32_t) +
                                           5ull * sizeof(uint32_t) + sizeof(uint16_t) + 3ull * sizeof(uint8_t)) +
        32ull * 64ull,
};

/* An empty store, from the arena. */
void entity_store_start(entity_store_t *store, arena_t *arena);

/* A new body at rest, in the air until a tick says otherwise. If the store is full it counts that and returns ENTITY_HANDLE_NONE: a
 * body nobody spawned is better than one somebody else lost. Aborts on a body or a position that physics could not move. */
entity_handle_t entity_store_spawn(entity_store_t *store, const entity_store_spawn_t *spawn);

/* Whether this body is alive, and where it is now. An index is good only until the next removal. */
bool entity_store_index_of(const entity_store_t *store, entity_handle_t handle, uint32_t *out_index);

/* Takes it out, moving the last body into its index, so a walk over the arrays that removes any goes from the end. False if it was
 * not alive. */
bool entity_store_remove(entity_store_t *store, entity_handle_t handle);

/* The box of the body at this index. */
void entity_store_box(const entity_store_t *store, uint32_t index, collision_box_t *out);

/* Where it wants to walk, until told otherwise: at these blocks per tick on plain ground, once up to speed. Zero stops it. */
void entity_store_wish_set(entity_store_t *store, uint32_t index, float64_t x, float64_t y);

/* It jumps on its next moving tick, if it stands on something; in a fluid it rises instead. */
void entity_store_jump(entity_store_t *store, uint32_t index);

/* One tick of every body, moved through the world by physics as a player's body is: wish, gravity, friction, water, lava, ladders,
 * steps of 0.6 and what a fall adds up to. A body over a column not loaded waits. Physics and map are the sim's. Never waits and never
 * touches the heap. */
void entity_store_tick(entity_store_t *store, physics_t *physics, const world_map_t *map);

#endif
