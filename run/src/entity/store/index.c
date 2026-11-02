/* See README.md. The Arena tier: its arrays come once from the arena its Owner gives it, and a tick takes nothing. */
#include "entity/store/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <math.h>
#include <string.h>

/* The published numbers, in blocks and ticks, which a player's body in run/src/player/movement/ has too. */
#define ENTITY_STORE_GRAVITY 0.08f64
#define ENTITY_STORE_AIR_DRAG_Z 0.98f64
#define ENTITY_STORE_AIR_FRICTION 0.91f64
#define ENTITY_STORE_GROUND_SLIP 0.6f64
#define ENTITY_STORE_ICE_SLIP 0.98f64
#define ENTITY_STORE_BLUE_ICE_SLIP 0.989f64
#define ENTITY_STORE_JUMP_SPEED 0.42f64
#define ENTITY_STORE_STEP_HEIGHT 0.6f64
#define ENTITY_STORE_SMALLEST_SPEED 0.003f64
#define ENTITY_STORE_FLUID_GRAVITY 0.02f64
#define ENTITY_STORE_FLUID_RISE 0.04f64
#define ENTITY_STORE_WATER_DRAG 0.8f64
#define ENTITY_STORE_LAVA_DRAG 0.5f64
#define ENTITY_STORE_FLUID_HOP_SPEED 0.3f64
#define ENTITY_STORE_CLIMB_SIDEWAYS_MAX 0.15f64
#define ENTITY_STORE_CLIMB_FALL_MAX 0.15f64
#define ENTITY_STORE_CLIMB_UP_SPEED 0.2f64
/* How much of a wish goes on each tick, on plain ground: what friction takes at that speed, so that it holds that speed. */
#define ENTITY_STORE_GROUND_PUSH (1.0f64 - ENTITY_STORE_GROUND_SLIP * ENTITY_STORE_AIR_FRICTION)
/* Off the ground: a published zombie's 0.02 of air against its 0.23 of ground. */
#define ENTITY_STORE_LOOSE_PUSH (0.02f64 / 0.23f64)
/* How far under its feet the block it walks on is looked for: enough for a slab's top, and for a full block's. */
#define ENTITY_STORE_UNDER_FEET 0.5f64
/* A box's edge a hair inside it: a body standing beside a fluid is not in it. */
#define ENTITY_STORE_TOUCH_INSET 0.001f64

typedef struct entity_store_touch_t {
    uint8_t water; /* 0 */
    uint8_t lava;  /* 1 */
    uint8_t climb; /* 2 */
} entity_store_touch_t;
static_assert(sizeof(entity_store_touch_t) == 3);

static void *entity_store_array(arena_t *arena, uint64_t element_bytes) {
    return arena_alloc_array(arena, ENTITY_STORE_CAPACITY, element_bytes, ENTITY_STORE_ARRAY_ALIGNMENT);
}

void entity_store_start(entity_store_t *store, arena_t *arena) {
    memset(store, 0, sizeof(*store));
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        store->borrowed_position[a] = entity_store_array(arena, sizeof(float64_t));
        store->borrowed_previous[a] = entity_store_array(arena, sizeof(float64_t));
        store->borrowed_velocity[a] = entity_store_array(arena, sizeof(float64_t));
    }
    store->borrowed_wish[0] = entity_store_array(arena, sizeof(float64_t));
    store->borrowed_wish[1] = entity_store_array(arena, sizeof(float64_t));
    store->borrowed_fall_distance = entity_store_array(arena, sizeof(float64_t));
    store->borrowed_landed_fall = entity_store_array(arena, sizeof(float64_t));
    store->borrowed_half_width = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_height = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_yaw = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_health = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_limb_phase = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_limb_amount = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_head_yaw = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_head_pitch = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_aim = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_swell = entity_store_array(arena, sizeof(float32_t));
    store->borrowed_handle = entity_store_array(arena, sizeof(entity_handle_t));
    store->borrowed_age = entity_store_array(arena, sizeof(uint32_t));
    store->borrowed_slot_index = entity_store_array(arena, sizeof(uint32_t));
    store->borrowed_slot_generation = entity_store_array(arena, sizeof(uint32_t));
    store->borrowed_free_slots = entity_store_array(arena, sizeof(uint32_t));
    store->borrowed_kind = entity_store_array(arena, sizeof(uint16_t));
    store->borrowed_flags = entity_store_array(arena, sizeof(uint8_t));
    store->borrowed_swing_ticks = entity_store_array(arena, sizeof(uint8_t));
    for (uint32_t slot = 0u; slot < ENTITY_STORE_CAPACITY; slot++) {
        store->borrowed_slot_index[slot] = ENTITY_STORE_CAPACITY;
        store->borrowed_slot_generation[slot] = 1u; /* so that no handle is ever ENTITY_HANDLE_NONE */
        store->borrowed_free_slots[slot] = slot;
    }
}

entity_handle_t entity_store_spawn(entity_store_t *store, const entity_store_spawn_t *spawn) {
    if (store->count == ENTITY_STORE_CAPACITY) {
        store->refused_count++;
        return ENTITY_HANDLE_NONE;
    }
    ASSERT_ALWAYS(spawn->half_width > 0.0f32 && spawn->height > 0.0f32);
    ASSERT_ALWAYS((float64_t)spawn->half_width * 2.0f64 <= PHYSICS_BOX_SIZE_MAX &&
                  (float64_t)spawn->height <= PHYSICS_BOX_SIZE_MAX);
    const uint32_t index = store->count;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        ASSERT_ALWAYS(isfinite(spawn->position[a]));
        store->borrowed_position[a][index] = spawn->position[a];
        store->borrowed_previous[a][index] = spawn->position[a];
        store->borrowed_velocity[a][index] = 0.0f64;
    }
    /* The free slots are in a ring with as many in it as there are not bodies, so what is taken is always there. */
    const uint32_t slot = store->borrowed_free_slots[store->free_head];
    store->free_head = (store->free_head + 1u) & ENTITY_STORE_SLOT_MASK;
    store->borrowed_wish[0][index] = 0.0f64;
    store->borrowed_wish[1][index] = 0.0f64;
    store->borrowed_fall_distance[index] = 0.0f64;
    store->borrowed_landed_fall[index] = 0.0f64;
    store->borrowed_half_width[index] = spawn->half_width;
    store->borrowed_height[index] = spawn->height;
    store->borrowed_yaw[index] = spawn->yaw;
    store->borrowed_health[index] = spawn->health;
    store->borrowed_limb_phase[index] = 0.0f32;
    store->borrowed_limb_amount[index] = 0.0f32;
    store->borrowed_head_yaw[index] = 0.0f32;
    store->borrowed_head_pitch[index] = 0.0f32;
    store->borrowed_aim[index] = 0.0f32;
    store->borrowed_swell[index] = 0.0f32;
    const entity_handle_t handle = (store->borrowed_slot_generation[slot] << ENTITY_STORE_SLOT_BITS) | slot;
    store->borrowed_handle[index] = handle;
    store->borrowed_age[index] = 0u;
    store->borrowed_kind[index] = spawn->kind;
    store->borrowed_flags[index] = 0u;
    store->borrowed_swing_ticks[index] = 0u;
    store->borrowed_slot_index[slot] = index;
    store->count = index + 1u;
    store->spawned_count++;
    return handle;
}

bool entity_store_index_of(const entity_store_t *store, entity_handle_t handle, uint32_t *out_index) {
    const uint32_t index = store->borrowed_slot_index[entity_handle_slot(handle)];
    if (index >= store->count || store->borrowed_handle[index] != handle) {
        return false;
    }
    *out_index = index;
    return true;
}

/* Every array but the handle tables: the last body into the gap. */
static void entity_store_move_body(entity_store_t *store, uint32_t to, uint32_t from) {
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        store->borrowed_position[a][to] = store->borrowed_position[a][from];
        store->borrowed_previous[a][to] = store->borrowed_previous[a][from];
        store->borrowed_velocity[a][to] = store->borrowed_velocity[a][from];
    }
    store->borrowed_wish[0][to] = store->borrowed_wish[0][from];
    store->borrowed_wish[1][to] = store->borrowed_wish[1][from];
    store->borrowed_fall_distance[to] = store->borrowed_fall_distance[from];
    store->borrowed_landed_fall[to] = store->borrowed_landed_fall[from];
    store->borrowed_half_width[to] = store->borrowed_half_width[from];
    store->borrowed_height[to] = store->borrowed_height[from];
    store->borrowed_yaw[to] = store->borrowed_yaw[from];
    store->borrowed_health[to] = store->borrowed_health[from];
    store->borrowed_limb_phase[to] = store->borrowed_limb_phase[from];
    store->borrowed_limb_amount[to] = store->borrowed_limb_amount[from];
    store->borrowed_head_yaw[to] = store->borrowed_head_yaw[from];
    store->borrowed_head_pitch[to] = store->borrowed_head_pitch[from];
    store->borrowed_aim[to] = store->borrowed_aim[from];
    store->borrowed_swell[to] = store->borrowed_swell[from];
    store->borrowed_handle[to] = store->borrowed_handle[from];
    store->borrowed_age[to] = store->borrowed_age[from];
    store->borrowed_kind[to] = store->borrowed_kind[from];
    store->borrowed_flags[to] = store->borrowed_flags[from];
    store->borrowed_swing_ticks[to] = store->borrowed_swing_ticks[from];
    store->borrowed_slot_index[entity_handle_slot(store->borrowed_handle[to])] = to;
}

bool entity_store_remove(entity_store_t *store, entity_handle_t handle) {
    uint32_t index = 0u;
    if (!entity_store_index_of(store, handle, &index)) {
        return false;
    }
    const uint32_t slot = entity_handle_slot(handle);
    const uint32_t last = store->count - 1u;
    if (index != last) {
        entity_store_move_body(store, index, last);
    }
    store->count = last;
    store->removed_count++;
    store->borrowed_slot_index[slot] = ENTITY_STORE_CAPACITY;
    const uint32_t next = store->borrowed_slot_generation[slot] + 1u;
    store->borrowed_slot_generation[slot] = next == ENTITY_STORE_GENERATION_LIMIT ? 1u : next;
    /* As many are free now as there are not bodies: the one given back goes last in the ring. */
    const uint32_t free_count = ENTITY_STORE_CAPACITY - last;
    store->borrowed_free_slots[(store->free_head + free_count - 1u) & ENTITY_STORE_SLOT_MASK] = slot;
    return true;
}

void entity_store_box(const entity_store_t *store, uint32_t index, collision_box_t *out) {
    const float64_t half = (float64_t)store->borrowed_half_width[index];
    const float64_t x = store->borrowed_position[0][index], y = store->borrowed_position[1][index];
    const float64_t z = store->borrowed_position[2][index];
    *out = (collision_box_t){
        .min = {x - half, y - half, z},
        .max = {x + half, y + half, z + (float64_t)store->borrowed_height[index]},
    };
}

void entity_store_wish_set(entity_store_t *store, uint32_t index, float64_t x, float64_t y) {
    store->borrowed_wish[0][index] = x;
    store->borrowed_wish[1][index] = y;
}

void entity_store_jump(entity_store_t *store, uint32_t index) {
    store->borrowed_flags[index] |= ENTITY_STORE_FLAG_JUMP;
}

/* ---- One body ------------------------------------------------------------------------------------------------------ */

/* A hair inside its box, since physics walks it right up to the edge of what is loaded as to a wall, and it must not wait there. */
static bool entity_store_is_loaded(const world_map_t *map, const collision_box_t *box) {
    for (uint32_t corner = 0u; corner < 4u; corner++) {
        const float64_t x =
            (corner & 1u) != 0u ? box->max[0] - ENTITY_STORE_TOUCH_INSET : box->min[0] + ENTITY_STORE_TOUCH_INSET;
        const float64_t y =
            (corner & 2u) != 0u ? box->max[1] - ENTITY_STORE_TOUCH_INSET : box->min[1] + ENTITY_STORE_TOUCH_INSET;
        if (world_map_column_at(map, column_chunk_of((int32_t)floor(x)), column_chunk_of((int32_t)floor(y))) ==
            nullptr) {
            return false;
        }
    }
    return true;
}

/* What it is in. A ladder or vine counts only in the cell of its feet, so it can step off the top and away. */
static entity_store_touch_t entity_store_touches(const world_map_t *map, const collision_box_t *box,
                                                 const float64_t position[COLLISION_AXES]) {
    entity_store_touch_t touch = {0u, 0u, 0u};
    const int32_t x_from = (int32_t)floor(box->min[0] + ENTITY_STORE_TOUCH_INSET);
    const int32_t y_from = (int32_t)floor(box->min[1] + ENTITY_STORE_TOUCH_INSET);
    const int32_t z_from = (int32_t)floor(box->min[2] + ENTITY_STORE_TOUCH_INSET);
    const int32_t x_to = (int32_t)floor(box->max[0] - ENTITY_STORE_TOUCH_INSET);
    const int32_t y_to = (int32_t)floor(box->max[1] - ENTITY_STORE_TOUCH_INSET);
    const int32_t z_to = (int32_t)floor(box->max[2] - ENTITY_STORE_TOUCH_INSET);
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
    uint16_t feet = BLOCK_ID_AIR;
    if (collision_cell_read(map, (int32_t)floor(position[0]), (int32_t)floor(position[1]), (int32_t)floor(position[2]),
                            &feet)) {
        touch.climb = feet == BLOCK_ID_LADDER || feet == BLOCK_ID_VINE ? 1u : 0u;
    }
    return touch;
}

static float64_t entity_store_slip_under(const world_map_t *map, const float64_t position[COLLISION_AXES]) {
    uint16_t under = BLOCK_ID_AIR;
    if (!collision_cell_read(map, (int32_t)floor(position[0]), (int32_t)floor(position[1]),
                             (int32_t)floor(position[2] - ENTITY_STORE_UNDER_FEET), &under)) {
        return ENTITY_STORE_GROUND_SLIP;
    }
    if (under == BLOCK_ID_ICE || under == BLOCK_ID_PACKED_ICE) {
        return ENTITY_STORE_ICE_SLIP;
    }
    return under == BLOCK_ID_BLUE_ICE ? ENTITY_STORE_BLUE_ICE_SLIP : ENTITY_STORE_GROUND_SLIP;
}

static float64_t entity_store_limit(float64_t value, float64_t limit) {
    return value > limit ? limit : (value < -limit ? -limit : value);
}

/* A wall stopped it on the ground: if it is low enough to walk up, up, along and back down goes further than the wall let it. Only
 * what lands on something counts, so this never walks out into the air at a raised height. */
static void entity_store_try_step(physics_t *physics, const world_map_t *map, const collision_box_t *box,
                                  const float64_t delta[COLLISION_AXES], collision_move_t *result) {
    const float64_t rise_by[COLLISION_AXES] = {0.0f64, 0.0f64, ENTITY_STORE_STEP_HEIGHT};
    collision_move_t rise;
    physics_move(physics, map, box, rise_by, &rise);
    collision_box_t over = *box;
    over.min[2] += rise.moved[2];
    over.max[2] += rise.moved[2];
    const float64_t along_by[COLLISION_AXES] = {delta[0], delta[1], 0.0f64};
    collision_move_t along;
    physics_move(physics, map, &over, along_by, &along);
    over.min[0] += along.moved[0];
    over.max[0] += along.moved[0];
    over.min[1] += along.moved[1];
    over.max[1] += along.moved[1];
    const float64_t lower_by[COLLISION_AXES] = {0.0f64, 0.0f64, delta[2] - rise.moved[2]};
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
static void entity_store_move(physics_t *physics, const world_map_t *map, const collision_box_t *box, bool on_ground,
                              float64_t position[COLLISION_AXES], float64_t velocity[COLLISION_AXES],
                              collision_move_t *out) {
    const float64_t delta[COLLISION_AXES] = {velocity[0], velocity[1], velocity[2]};
    physics_move(physics, map, box, delta, out);
    /* Not on the way up: a jump is not a step. */
    const bool blocked = out->hit_x != 0u || out->hit_y != 0u;
    if (blocked && delta[2] <= 0.0f64 && (on_ground || out->hit_z_down != 0u)) {
        entity_store_try_step(physics, map, box, delta, out);
    }
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        position[a] += out->moved[a];
    }
    velocity[0] = out->hit_x != 0u ? 0.0f64 : velocity[0];
    velocity[1] = out->hit_y != 0u ? 0.0f64 : velocity[1];
    velocity[2] = out->hit_z_down != 0u || out->hit_z_up != 0u ? 0.0f64 : velocity[2];
}

/* Swimming into a bank that ends just above: hop out. Under it, the bank is still in the way and nothing happens. */
static bool entity_store_can_hop(physics_t *physics, const world_map_t *map, const entity_store_t *store,
                                 uint32_t index, const float64_t position[COLLISION_AXES], float64_t x, float64_t y) {
    const float64_t half = (float64_t)store->borrowed_half_width[index];
    const collision_box_t hop = {
        .min = {position[0] + x - half, position[1] + y - half, position[2] + ENTITY_STORE_STEP_HEIGHT},
        .max = {position[0] + x + half, position[1] + y + half,
                position[2] + ENTITY_STORE_STEP_HEIGHT + (float64_t)store->borrowed_height[index]},
    };
    return physics_box_is_clear(physics, map, &hop);
}

/* A tick in water or lava. */
static void entity_store_tick_fluid(const entity_store_t *store, physics_t *physics, const world_map_t *map,
                                    uint32_t index, const collision_box_t *box, bool on_ground, bool jump,
                                    float64_t drag, float64_t position[COLLISION_AXES],
                                    float64_t velocity[COLLISION_AXES], collision_move_t *moved) {
    velocity[0] += store->borrowed_wish[0][index] * ENTITY_STORE_LOOSE_PUSH;
    velocity[1] += store->borrowed_wish[1][index] * ENTITY_STORE_LOOSE_PUSH;
    velocity[2] += jump ? ENTITY_STORE_FLUID_RISE : 0.0f64;
    const float64_t tried_x = velocity[0], tried_y = velocity[1];
    entity_store_move(physics, map, box, on_ground, position, velocity, moved);
    velocity[0] *= drag;
    velocity[1] *= drag;
    velocity[2] = velocity[2] * drag - ENTITY_STORE_FLUID_GRAVITY;
    if ((moved->hit_x != 0u || moved->hit_y != 0u) &&
        entity_store_can_hop(physics, map, store, index, position, tried_x, tried_y)) {
        velocity[2] = ENTITY_STORE_FLUID_HOP_SPEED;
    }
}

/* A tick on the ground, in the air or on a ladder. */
static void entity_store_tick_land(const entity_store_t *store, physics_t *physics, const world_map_t *map,
                                   uint32_t index, const collision_box_t *box, bool on_ground, bool jump, bool climbing,
                                   float64_t position[COLLISION_AXES], float64_t velocity[COLLISION_AXES],
                                   collision_move_t *moved) {
    const float64_t slip = on_ground ? entity_store_slip_under(map, position) : ENTITY_STORE_GROUND_SLIP;
    /* A slippery floor gives less grip, by the cube of what it keeps, as published. */
    const float64_t grip = ENTITY_STORE_GROUND_PUSH * (ENTITY_STORE_GROUND_SLIP * ENTITY_STORE_GROUND_SLIP *
                                                       ENTITY_STORE_GROUND_SLIP / (slip * slip * slip));
    const float64_t push = on_ground ? grip : ENTITY_STORE_LOOSE_PUSH;
    velocity[0] += store->borrowed_wish[0][index] * push;
    velocity[1] += store->borrowed_wish[1][index] * push;
    velocity[2] = jump && on_ground ? ENTITY_STORE_JUMP_SPEED : velocity[2];
    if (climbing) {
        velocity[0] = entity_store_limit(velocity[0], ENTITY_STORE_CLIMB_SIDEWAYS_MAX);
        velocity[1] = entity_store_limit(velocity[1], ENTITY_STORE_CLIMB_SIDEWAYS_MAX);
        velocity[2] = velocity[2] < -ENTITY_STORE_CLIMB_FALL_MAX ? -ENTITY_STORE_CLIMB_FALL_MAX : velocity[2];
    }
    entity_store_move(physics, map, box, on_ground, position, velocity, moved);
    if (climbing && (jump || moved->hit_x != 0u || moved->hit_y != 0u)) {
        velocity[2] = ENTITY_STORE_CLIMB_UP_SPEED;
    }
    const float64_t friction = on_ground ? slip * ENTITY_STORE_AIR_FRICTION : ENTITY_STORE_AIR_FRICTION;
    velocity[0] *= friction;
    velocity[1] *= friction;
    velocity[2] = (velocity[2] - ENTITY_STORE_GRAVITY) * ENTITY_STORE_AIR_DRAG_Z;
}

static void entity_store_tick_one(entity_store_t *store, physics_t *physics, const world_map_t *map, uint32_t index) {
    float64_t position[COLLISION_AXES], velocity[COLLISION_AXES];
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        position[a] = store->borrowed_position[a][index];
        velocity[a] = store->borrowed_velocity[a][index];
        store->borrowed_previous[a][index] = position[a];
    }
    store->borrowed_landed_fall[index] = 0.0f64;
    const uint8_t flags = store->borrowed_flags[index];
    collision_box_t box;
    entity_store_box(store, index, &box);
    if (!entity_store_is_loaded(map, &box)) {
        /* It keeps what it knew and what it was asked, for when the column comes. */
        store->borrowed_flags[index] =
            (uint8_t)((flags & (ENTITY_STORE_FLAG_ON_GROUND | ENTITY_STORE_FLAG_JUMP)) | ENTITY_STORE_FLAG_WAITING);
        return;
    }
    store->borrowed_age[index]++;

    const entity_store_touch_t touch = entity_store_touches(map, &box, position);
    const bool in_fluid = touch.water != 0u || touch.lava != 0u;
    const bool climbing = touch.climb != 0u && !in_fluid;
    const bool on_ground = (flags & ENTITY_STORE_FLAG_ON_GROUND) != 0u;
    const bool jump = (flags & ENTITY_STORE_FLAG_JUMP) != 0u;
    const float64_t z_before = position[2];
    collision_move_t moved;
    if (in_fluid) {
        const float64_t drag = touch.lava != 0u ? ENTITY_STORE_LAVA_DRAG : ENTITY_STORE_WATER_DRAG;
        entity_store_tick_fluid(store, physics, map, index, &box, on_ground, jump, drag, position, velocity, &moved);
    } else {
        entity_store_tick_land(store, physics, map, index, &box, on_ground, jump, climbing, position, velocity, &moved);
    }
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        /* What is left of a stop is not worth a move through the world. */
        velocity[a] = fabs(velocity[a]) < ENTITY_STORE_SMALLEST_SPEED ? 0.0f64 : velocity[a];
        store->borrowed_position[a][index] = position[a];
        store->borrowed_velocity[a][index] = velocity[a];
    }

    const float64_t dropped = z_before - position[2];
    if (in_fluid || climbing) {
        store->borrowed_fall_distance[index] = 0.0f64; /* water and ladders break a fall */
    } else {
        store->borrowed_fall_distance[index] += dropped > 0.0f64 ? dropped : 0.0f64;
        if (moved.hit_z_down != 0u) {
            store->borrowed_landed_fall[index] = store->borrowed_fall_distance[index];
            store->borrowed_fall_distance[index] = 0.0f64;
        }
    }
    uint32_t after = moved.hit_z_down != 0u ? ENTITY_STORE_FLAG_ON_GROUND : 0u;
    after |= touch.water != 0u ? ENTITY_STORE_FLAG_IN_WATER : 0u;
    after |= touch.lava != 0u ? ENTITY_STORE_FLAG_IN_LAVA : 0u;
    after |= climbing ? ENTITY_STORE_FLAG_CLIMBING : 0u;
    after |= moved.hit_x != 0u || moved.hit_y != 0u ? ENTITY_STORE_FLAG_HIT_WALL : 0u;
    store->borrowed_flags[index] = (uint8_t)after;
}

void entity_store_tick(entity_store_t *store, physics_t *physics, const world_map_t *map) {
    for (uint32_t i = 0u; i < store->count; i++) {
        entity_store_tick_one(store, physics, map, i);
    }
}
