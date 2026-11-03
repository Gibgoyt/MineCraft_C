/* See run/src/entity/animation/README.md. */
#ifndef MC_RUN_ENTITY_ANIMATION_INDEX_H
#define MC_RUN_ENTITY_ANIMATION_INDEX_H

#include "entity/store/index.h"
#include "tables/mobs/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Most in a rig: a head, a body and four limbs. */
    ENTITY_ANIMATION_PARTS_MAX = MOB_PARTS_MAX,
    /* How long an attack's swing takes, in ticks. */
    ENTITY_ANIMATION_SWING_TICKS = 6u,
};

/* What a part's numbers are in: a model's pixels, this many to a block. */
#define ENTITY_ANIMATION_PIXELS_PER_BLOCK 16.0f32

/* A part's own axes, and a rig's: forward, then to its left, then up. Forward is where the yaw points. render_entities_item_t's half
 * and yaw mean the same, so pose's reach and yaw can be drawn as they are. */
enum : uint32_t { ENTITY_ANIMATION_FORWARD = 0u, ENTITY_ANIMATION_LEFT = 1u, ENTITY_ANIMATION_UP = 2u };

/* What moves a part besides walking. */
enum : uint8_t {
    ENTITY_ANIMATION_PART_HEAD = MOB_PART_MOVES_HEAD, /* it turns by the head's yaw and pitch */
    ENTITY_ANIMATION_PART_ARM = MOB_PART_MOVES_ARM,   /* it swings to attack and rises to aim */
};

/* One box of a body, as config/mobs/ says it: see tables/mobs/. */
typedef mob_part_t entity_animation_part_t;
static_assert(sizeof(entity_animation_part_t) == 52);

/* A body's parts, and the size of the texture they are unwrapped on. */
typedef struct entity_animation_rig_t {
    const entity_animation_part_t *borrowed_parts; /* 0 */
    uint32_t part_count;                           /* 8 */
    uint16_t texture_width;                        /* 12 */
    uint16_t texture_height;                       /* 14 */
} entity_animation_rig_t;
static_assert(sizeof(entity_animation_rig_t) == 16);

typedef enum entity_animation_rig_id_t : uint8_t {
    ENTITY_ANIMATION_RIG_ZOMBIE = 0,   /* a person's shape, with its arms held out */
    ENTITY_ANIMATION_RIG_SKELETON = 1, /* the same, with thin arms and legs, and arms that swing */
    ENTITY_ANIMATION_RIG_CREEPER = 2,  /* no arms, and four short feet */
    ENTITY_ANIMATION_RIG_COUNT = 3,
} entity_animation_rig_id_t;

/* How a body is moving, at one moment. */
typedef struct entity_animation_state_t {
    float32_t yaw;         /* 0 */
    float32_t limb_phase;  /* 4 */
    float32_t limb_amount; /* 8: 0 to 1 */
    float32_t head_yaw;    /* 12: from yaw */
    float32_t head_pitch;  /* 16: up */
    float32_t swing;       /* 20: how far through an attack, 0 to 1; 0 for none */
    float32_t aim;         /* 24: 0 to 1 */
    float32_t swell;       /* 28: 0 to 1 */
} entity_animation_state_t;
static_assert(sizeof(entity_animation_state_t) == 32);

/* One part, placed in the world. */
typedef struct entity_animation_pose_t {
    float64_t center[COLLISION_AXES]; /* 0 */
    float32_t half[3];                /* 24: blocks, along its own axes, once it has grown with the swell */
    float32_t reach[3]; /* 36: the same with its pitch taken in: all of it, for a box turned by yaw alone */
    float32_t yaw;      /* 48 */
    float32_t pitch;    /* 52 */
} entity_animation_pose_t;
static_assert(sizeof(entity_animation_pose_t) == 56);

/* A rig. Lives as long as the program. */
const entity_animation_rig_t *entity_animation_rig(entity_animation_rig_id_t rig);

/* One tick of how every body in the store moves, from how far it went. After entity_store_tick. A body waiting for its column stands
 * in the stride it was in. Never touches the heap. */
void entity_animation_tick(entity_store_t *store);

/* An attack's swing starts over. */
void entity_animation_swing(entity_store_t *store, uint32_t index);

/* How the body at this index is moving now. */
void entity_animation_state_read(const entity_store_t *store, uint32_t index, entity_animation_state_t *out);

/* Every part of a rig for a body at position, into out, which has room for ENTITY_ANIMATION_PARTS_MAX. Returns how many. */
uint32_t entity_animation_pose(const entity_animation_rig_t *rig, const entity_animation_state_t *state,
                               const float64_t position[COLLISION_AXES], entity_animation_pose_t *out);

#endif
