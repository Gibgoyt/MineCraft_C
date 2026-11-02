/* See README.md. Allocates nothing: the only memory a tick uses is what physics reserved for a move. */
#include "player/movement/index.h"

#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <math.h>
#include <string.h>

/* Every number below is per tick at 20 Hz. README.md has what they add up to in blocks per second. */
#define MOVEMENT_GRAVITY 0.08f64
#define MOVEMENT_AIR_DRAG_Z 0.98f64
#define MOVEMENT_GROUND_ACCELERATION 0.1f64
#define MOVEMENT_GROUND_FRICTION 0.546f64
#define MOVEMENT_AIR_ACCELERATION 0.02f64
#define MOVEMENT_AIR_FRICTION 0.91f64
#define MOVEMENT_INPUT_SCALE 0.98f64
#define MOVEMENT_SPRINT_FACTOR 1.3f64
#define MOVEMENT_CROUCH_FACTOR 0.3f64
#define MOVEMENT_JUMP_SPEED 0.42f64
#define MOVEMENT_SPRINT_JUMP_BOOST 0.2f64
#define MOVEMENT_STEP_HEIGHT 0.6f64
#define MOVEMENT_EDGE_STEP 0.05f64
#define MOVEMENT_SMALLEST_SPEED 0.003f64

#define MOVEMENT_FLUID_ACCELERATION 0.02f64
#define MOVEMENT_FLUID_GRAVITY 0.02f64
#define MOVEMENT_FLUID_RISE 0.04f64
#define MOVEMENT_WATER_DRAG 0.8f64
#define MOVEMENT_LAVA_DRAG 0.5f64
#define MOVEMENT_FLUID_HOP_SPEED 0.3f64

#define MOVEMENT_CLIMB_SIDEWAYS_MAX 0.15f64
#define MOVEMENT_CLIMB_FALL_MAX 0.15f64
#define MOVEMENT_CLIMB_UP_SPEED 0.2f64

/* A box's edge a hair inside it: a body standing beside a fluid is not in it. */
#define MOVEMENT_TOUCH_INSET 0.001f64
#define MOVEMENT_EYE_EASE 0.5f64

typedef struct movement_touch_t {
    uint8_t water;      /* 0 */
    uint8_t lava;       /* 1 */
    uint8_t climb;      /* 2 */
    uint8_t eyes_water; /* 3 */
} movement_touch_t;
static_assert(sizeof(movement_touch_t) == 4);

/* What one tick asks for, once its keys are made sense of. */
typedef struct movement_wish_t {
    float64_t forward_x; /* 0: where it looks, level */
    float64_t forward_y; /* 8 */
    float64_t x;         /* 16: where it wants to go, with no more length when diagonal */
    float64_t y;         /* 24 */
    float64_t height;    /* 32: of the body this tick */
} movement_wish_t;
static_assert(sizeof(movement_wish_t) == 40);

void player_movement_start(player_movement_t *movement, float64_t x, float64_t y, float64_t z) {
    memset(movement, 0, sizeof(*movement));
    movement->position[0] = x;
    movement->position[1] = y;
    movement->position[2] = z;
    memcpy(movement->previous, movement->position, sizeof(movement->previous));
    movement->eye_height = PLAYER_MOVEMENT_EYE_STANDING;
    movement->previous_eye = PLAYER_MOVEMENT_EYE_STANDING;
    movement->mode = PLAYER_MOVEMENT_MODE_AIR;
}

void player_movement_box(const float64_t position[3], float64_t height, collision_box_t *out) {
    const float64_t half = PLAYER_MOVEMENT_WIDTH * 0.5f64;
    *out = (collision_box_t){
        .min = {position[0] - half, position[1] - half, position[2]},
        .max = {position[0] + half, position[1] + half, position[2] + height},
    };
}

static void movement_box_moved(const collision_box_t *box, float64_t x, float64_t y, float64_t z,
                               collision_box_t *out) {
    *out = (collision_box_t){
        .min = {box->min[0] + x, box->min[1] + y, box->min[2] + z},
        .max = {box->max[0] + x, box->max[1] + y, box->max[2] + z},
    };
}

/* What it is in. A ladder or vine counts only in the cell of its feet, so it can step off the top and away. */
static movement_touch_t movement_touches(const player_movement_t *movement, const world_map_t *map, float64_t height) {
    movement_touch_t touch = {0u, 0u, 0u, 0u};
    collision_box_t box;
    player_movement_box(movement->position, height, &box);
    const int32_t x_from = (int32_t)floor(box.min[0] + MOVEMENT_TOUCH_INSET);
    const int32_t y_from = (int32_t)floor(box.min[1] + MOVEMENT_TOUCH_INSET);
    const int32_t z_from = (int32_t)floor(box.min[2] + MOVEMENT_TOUCH_INSET);
    const int32_t x_to = (int32_t)floor(box.max[0] - MOVEMENT_TOUCH_INSET);
    const int32_t y_to = (int32_t)floor(box.max[1] - MOVEMENT_TOUCH_INSET);
    const int32_t z_to = (int32_t)floor(box.max[2] - MOVEMENT_TOUCH_INSET);
    for (int32_t x = x_from; x <= x_to; x++) {
        for (int32_t y = y_from; y <= y_to; y++) {
            for (int32_t z = z_from; z <= z_to; z++) {
                uint16_t block = BLOCK_ID_AIR;
                if (collision_cell_read(map, x, y, z, &block)) {
                    touch.water = block == BLOCK_ID_WATER ? 1u : touch.water;
                    touch.lava = block == BLOCK_ID_LAVA ? 1u : touch.lava;
                }
            }
        }
    }
    const int32_t feet_x = (int32_t)floor(movement->position[0]);
    const int32_t feet_y = (int32_t)floor(movement->position[1]);
    uint16_t feet = BLOCK_ID_AIR;
    if (collision_cell_read(map, feet_x, feet_y, (int32_t)floor(movement->position[2]), &feet)) {
        touch.climb = feet == BLOCK_ID_LADDER || feet == BLOCK_ID_VINE ? 1u : 0u;
    }
    uint16_t eyes = BLOCK_ID_AIR;
    const int32_t eyes_z = (int32_t)floor(movement->position[2] + movement->eye_height);
    if (collision_cell_read(map, feet_x, feet_y, eyes_z, &eyes)) {
        touch.eyes_water = eyes == BLOCK_ID_WATER ? 1u : 0u;
    }
    return touch;
}

static float64_t movement_edge_less(float64_t distance) {
    if (fabs(distance) <= MOVEMENT_EDGE_STEP) {
        return 0.0f64;
    }
    return distance > 0.0f64 ? distance - MOVEMENT_EDGE_STEP : distance + MOVEMENT_EDGE_STEP;
}

/* Sneaking at the edge of something: goes only as far as leaves ground within a step below it. */
static void movement_keep_on_edge(physics_t *physics, const world_map_t *map, const collision_box_t *box, float64_t *x,
                                  float64_t *y) {
    while (*x != 0.0f64) {
        collision_box_t at;
        movement_box_moved(box, *x, 0.0f64, -MOVEMENT_STEP_HEIGHT, &at);
        if (!physics_box_is_clear(physics, map, &at)) {
            break;
        }
        *x = movement_edge_less(*x);
    }
    while (*y != 0.0f64) {
        collision_box_t at;
        movement_box_moved(box, *x, *y, -MOVEMENT_STEP_HEIGHT, &at);
        if (!physics_box_is_clear(physics, map, &at)) {
            break;
        }
        *y = movement_edge_less(*y);
    }
}

/* A wall stopped it on the ground: if it is low enough to walk up, up, along and back down goes further than the wall let it.
 * Only what lands on something counts, so this never walks out into the air at a raised height. */
static void movement_try_step(physics_t *physics, const world_map_t *map, const collision_box_t *box,
                              const float64_t delta[COLLISION_AXES], collision_move_t *result) {
    const float64_t rise_by[COLLISION_AXES] = {0.0f64, 0.0f64, MOVEMENT_STEP_HEIGHT};
    collision_move_t rise;
    physics_move(physics, map, box, rise_by, &rise);
    collision_box_t raised;
    movement_box_moved(box, 0.0f64, 0.0f64, rise.moved[2], &raised);
    const float64_t along_by[COLLISION_AXES] = {delta[0], delta[1], 0.0f64};
    collision_move_t along;
    physics_move(physics, map, &raised, along_by, &along);
    collision_box_t over;
    movement_box_moved(&raised, along.moved[0], along.moved[1], 0.0f64, &over);
    const float64_t lower_by[COLLISION_AXES] = {0.0f64, 0.0f64, -rise.moved[2] + delta[2]};
    collision_move_t lower;
    physics_move(physics, map, &over, lower_by, &lower);
    const float64_t stepped = along.moved[0] * along.moved[0] + along.moved[1] * along.moved[1];
    const float64_t walked = result->moved[0] * result->moved[0] + result->moved[1] * result->moved[1];
    if (lower.hit_z_down == 0u || stepped <= walked) {
        return;
    }
    *result = (collision_move_t){
        .moved = {along.moved[0], along.moved[1], rise.moved[2] + lower.moved[2]},
        .hit_x = along.hit_x,
        .hit_y = along.hit_y,
        .hit_z_down = 1u,
    };
}

/* Moves by velocity. What stopped it stops its velocity too. */
static void movement_move(player_movement_t *movement, physics_t *physics, const world_map_t *map, float64_t height,
                          bool keep_on_edge, collision_move_t *out) {
    collision_box_t box;
    player_movement_box(movement->position, height, &box);
    float64_t delta[COLLISION_AXES] = {movement->velocity[0], movement->velocity[1], movement->velocity[2]};
    if (keep_on_edge) {
        movement_keep_on_edge(physics, map, &box, &delta[0], &delta[1]);
    }
    physics_move(physics, map, &box, delta, out);
    /* Not on the way up: a jump is not a step. */
    const bool blocked = out->hit_x != 0u || out->hit_y != 0u;
    if (blocked && delta[2] <= 0.0f64 && (movement->on_ground != 0u || out->hit_z_down != 0u)) {
        movement_try_step(physics, map, &box, delta, out);
    }
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        movement->position[a] += out->moved[a];
    }
    movement->velocity[0] = out->hit_x != 0u ? 0.0f64 : movement->velocity[0];
    movement->velocity[1] = out->hit_y != 0u ? 0.0f64 : movement->velocity[1];
    movement->velocity[2] = out->hit_z_down != 0u || out->hit_z_up != 0u ? 0.0f64 : movement->velocity[2];
    movement->on_ground = out->hit_z_down;
}

static void movement_tick_fluid(player_movement_t *movement, physics_t *physics, const world_map_t *map,
                                const player_intent_t *intent, const movement_wish_t *wish, float64_t drag) {
    movement->velocity[0] += wish->x * MOVEMENT_FLUID_ACCELERATION;
    movement->velocity[1] += wish->y * MOVEMENT_FLUID_ACCELERATION;
    movement->velocity[2] += intent->jump != 0u ? MOVEMENT_FLUID_RISE : 0.0f64;
    movement->velocity[2] -= intent->sneak != 0u ? MOVEMENT_FLUID_RISE : 0.0f64;
    const float64_t tried_x = movement->velocity[0], tried_y = movement->velocity[1];
    collision_move_t moved;
    movement_move(movement, physics, map, wish->height, false, &moved);
    movement->velocity[0] *= drag;
    movement->velocity[1] *= drag;
    movement->velocity[2] = movement->velocity[2] * drag - MOVEMENT_FLUID_GRAVITY;
    /* Swimming into a bank that ends just above: hop out. Under it, the bank is still in the way and nothing happens. */
    if (moved.hit_x != 0u || moved.hit_y != 0u) {
        collision_box_t box;
        player_movement_box(movement->position, wish->height, &box);
        collision_box_t hop;
        movement_box_moved(&box, tried_x, tried_y, MOVEMENT_STEP_HEIGHT, &hop);
        if (physics_box_is_clear(physics, map, &hop)) {
            movement->velocity[2] = MOVEMENT_FLUID_HOP_SPEED;
        }
    }
}

static float64_t movement_limit(float64_t value, float64_t limit) {
    return value > limit ? limit : (value < -limit ? -limit : value);
}

static void movement_tick_land(player_movement_t *movement, physics_t *physics, const world_map_t *map,
                               const player_intent_t *intent, const movement_wish_t *wish, bool climbing) {
    const bool on_ground = movement->on_ground != 0u;
    const bool crouched = movement->crouched != 0u, sprinting = movement->sprinting != 0u;
    const float64_t pace = crouched ? MOVEMENT_CROUCH_FACTOR : (sprinting ? MOVEMENT_SPRINT_FACTOR : 1.0f64);
    const float64_t push = (on_ground ? MOVEMENT_GROUND_ACCELERATION : MOVEMENT_AIR_ACCELERATION) * pace;
    movement->velocity[0] += wish->x * push;
    movement->velocity[1] += wish->y * push;
    if (intent->jump != 0u && on_ground) {
        movement->velocity[2] = MOVEMENT_JUMP_SPEED;
        if (sprinting) {
            movement->velocity[0] += wish->forward_x * MOVEMENT_SPRINT_JUMP_BOOST;
            movement->velocity[1] += wish->forward_y * MOVEMENT_SPRINT_JUMP_BOOST;
        }
    }
    if (climbing) {
        movement->velocity[0] = movement_limit(movement->velocity[0], MOVEMENT_CLIMB_SIDEWAYS_MAX);
        movement->velocity[1] = movement_limit(movement->velocity[1], MOVEMENT_CLIMB_SIDEWAYS_MAX);
        const float64_t lowest = crouched ? 0.0f64 : -MOVEMENT_CLIMB_FALL_MAX;
        movement->velocity[2] = movement->velocity[2] < lowest ? lowest : movement->velocity[2];
    }
    const bool keep_on_edge = crouched && on_ground && movement->velocity[2] <= 0.0f64;
    collision_move_t moved;
    movement_move(movement, physics, map, wish->height, keep_on_edge, &moved);
    if (climbing && (intent->jump != 0u || moved.hit_x != 0u || moved.hit_y != 0u)) {
        movement->velocity[2] = MOVEMENT_CLIMB_UP_SPEED;
    }
    const float64_t friction = on_ground ? MOVEMENT_GROUND_FRICTION : MOVEMENT_AIR_FRICTION;
    movement->velocity[0] *= friction;
    movement->velocity[1] *= friction;
    movement->velocity[2] = (movement->velocity[2] - MOVEMENT_GRAVITY) * MOVEMENT_AIR_DRAG_Z;
}

static void movement_wish_make(const player_intent_t *intent, float64_t height, movement_wish_t *out) {
    out->forward_x = (float64_t)cosf(intent->yaw);
    out->forward_y = (float64_t)sinf(intent->yaw);
    out->height = height;
    const float64_t ahead = (float64_t)intent->forward, across = (float64_t)intent->right;
    const float64_t length = sqrt(ahead * ahead + across * across);
    const float64_t scale = length > 0.0f64 ? MOVEMENT_INPUT_SCALE / length : 0.0f64;
    out->x = (ahead * out->forward_x + across * out->forward_y) * scale;
    out->y = (ahead * out->forward_y - across * out->forward_x) * scale;
}

void player_movement_tick(player_movement_t *movement, physics_t *physics, const world_map_t *map,
                          const player_intent_t *intent) {
    memcpy(movement->previous, movement->position, sizeof(movement->previous));
    movement->previous_eye = movement->eye_height;
    movement->landed_fall = 0.0f64;
    const int32_t chunk_x = column_chunk_of((int32_t)floor(movement->position[0]));
    const int32_t chunk_y = column_chunk_of((int32_t)floor(movement->position[1]));
    if (world_map_column_at(map, chunk_x, chunk_y) == nullptr) {
        /* The world has not got here yet. Anything it did now would be a guess, and it would fall through the ground. */
        memset(movement->velocity, 0, sizeof(movement->velocity));
        movement->waiting_for_column = 1u;
        return;
    }
    movement->waiting_for_column = 0u;

    /* Still crouched for as long as there is no room to stand. */
    collision_box_t standing;
    player_movement_box(movement->position, PLAYER_MOVEMENT_HEIGHT_STANDING, &standing);
    const bool crouched =
        intent->sneak != 0u || (movement->crouched != 0u && !physics_box_is_clear(physics, map, &standing));
    movement->crouched = crouched ? 1u : 0u;
    const float64_t height = crouched ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
    const movement_touch_t touch = movement_touches(movement, map, height);
    const bool in_fluid = touch.water != 0u || touch.lava != 0u;
    const bool climbing = touch.climb != 0u && !in_fluid;
    movement->sprinting = intent->sprint != 0u && intent->forward > 0 && !crouched && !in_fluid ? 1u : 0u;

    /* Slower than this is rounding, and it would keep a body that has stopped from ever being still. */
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        movement->velocity[a] = fabs(movement->velocity[a]) < MOVEMENT_SMALLEST_SPEED ? 0.0f64 : movement->velocity[a];
    }

    movement_wish_t wish;
    movement_wish_make(intent, height, &wish);
    const float64_t z_before = movement->position[2];
    if (in_fluid) {
        movement_tick_fluid(movement, physics, map, intent, &wish,
                            touch.lava != 0u ? MOVEMENT_LAVA_DRAG : MOVEMENT_WATER_DRAG);
    } else {
        movement_tick_land(movement, physics, map, intent, &wish, climbing);
    }

    const float64_t dropped = z_before - movement->position[2];
    if (in_fluid || climbing) {
        movement->fall_distance = 0.0f64; /* water and ladders break a fall */
    } else {
        movement->fall_distance += dropped > 0.0f64 ? dropped : 0.0f64;
        if (movement->on_ground != 0u) {
            movement->landed_fall = movement->fall_distance;
            movement->fall_distance = 0.0f64;
        }
    }
    if (touch.lava != 0u) {
        movement->mode = PLAYER_MOVEMENT_MODE_LAVA;
    } else if (touch.water != 0u) {
        movement->mode = PLAYER_MOVEMENT_MODE_WATER;
    } else if (climbing) {
        movement->mode = PLAYER_MOVEMENT_MODE_CLIMB;
    } else {
        movement->mode = movement->on_ground != 0u ? PLAYER_MOVEMENT_MODE_GROUND : PLAYER_MOVEMENT_MODE_AIR;
    }
    movement->eyes_under_water = touch.eyes_water;
    const float64_t eye_wanted = crouched ? PLAYER_MOVEMENT_EYE_CROUCHED : PLAYER_MOVEMENT_EYE_STANDING;
    movement->eye_height += (eye_wanted - movement->eye_height) * MOVEMENT_EYE_EASE;
}
