/* See README.md. The Arena tier: its arrays come once from the arena its Owner gives it, and a tick takes nothing. */
#include "entity/item_drop/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <math.h>
#include <string.h>

/* The published numbers, in blocks and ticks. */
#define ITEM_DROP_GRAVITY 0.04f64
#define ITEM_DROP_DRAG 0.98f64
#define ITEM_DROP_FRICTION_GROUND 0.6f64
#define ITEM_DROP_FRICTION_ICE 0.98f64
#define ITEM_DROP_FRICTION_BLUE_ICE 0.989f64
#define ITEM_DROP_THROW_ACROSS 0.1f64
#define ITEM_DROP_THROW_UP 0.2f64
#define ITEM_DROP_SCATTER 0.25f64
/* Under this speed squared, sideways, it counts as lying still. */
#define ITEM_DROP_REST_SPEED_SQUARED 1.0e-5f64
#define ITEM_DROP_WATER_DRAG 0.99f64
#define ITEM_DROP_WATER_RISE 0.0005f64
#define ITEM_DROP_WATER_RISE_MAX 0.06f64
/* How far above its box's bottom a water cell has to reach to carry it. */
#define ITEM_DROP_WATER_DEPTH 0.1f64
/* How fast it leaves a block that has come down on it. */
#define ITEM_DROP_PUSH_SPEED 0.2f64
/* A cactus's box is its whole cell here, so what lies against it only touches it. */
#define ITEM_DROP_TOUCH 0.01f64

enum : uint32_t { ITEM_DROP_GRID_NONE = UINT32_MAX };

void item_drop_store_start(item_drop_store_t *store, arena_t *arena, uint64_t seed) {
    memset(store, 0, sizeof(*store));
    store->borrowed_items = arena_alloc_array(arena, ITEM_DROP_CAPACITY, sizeof(item_drop_t), alignof(item_drop_t));
    store->borrowed_buckets = arena_alloc_array(arena, ITEM_DROP_GRID_BUCKETS, sizeof(uint32_t), alignof(uint32_t));
    store->borrowed_chain = arena_alloc_array(arena, ITEM_DROP_CAPACITY, sizeof(uint32_t), alignof(uint32_t));
    store->random = terrain_rng_start(seed);
}

collision_box_t item_drop_box(const float64_t position[COLLISION_AXES]) {
    const float64_t half = ITEM_DROP_SIZE / 2.0f64;
    return (collision_box_t){
        .min = {position[0] - half, position[1] - half, position[2] - half},
        .max = {position[0] + half, position[1] + half, position[2] + half},
    };
}

/* The last one takes its place, so a walk over the stacks that removes any must go from the end. */
static void item_drop_remove(item_drop_store_t *store, uint32_t index) {
    store->count--;
    store->borrowed_items[index] = store->borrowed_items[store->count];
}

/* ---- Spawning ------------------------------------------------------------------------------------------------------ */

static void item_drop_evict_oldest(item_drop_store_t *store) {
    uint32_t oldest = 0u;
    for (uint32_t i = 1u; i < store->count; i++) {
        if (store->borrowed_items[i].age > store->borrowed_items[oldest].age) {
            oldest = i;
        }
    }
    store->lost_count += store->borrowed_items[oldest].count;
    item_drop_remove(store, oldest);
}

/* From -reach up to just below reach. */
static float64_t item_drop_random_around(terrain_rng_t *random, float64_t reach) {
    return (terrain_rng_next_f64(random) * 2.0f64 - 1.0f64) * reach;
}

void item_drop_spawn(item_drop_store_t *store, uint16_t item, uint32_t count,
                     const float64_t position[COLLISION_AXES]) {
    ASSERT_ALWAYS(item < BLOCK_DROP_ITEM_COUNT);
    for (uint32_t left = count; left != 0u;) {
        const uint32_t stack = left > ITEM_DROP_STACK_MAX ? ITEM_DROP_STACK_MAX : left;
        left -= stack;
        if (store->count == ITEM_DROP_CAPACITY) {
            item_drop_evict_oldest(store);
        }
        item_drop_t *const drop = &store->borrowed_items[store->count];
        store->count++;
        memset(drop, 0, sizeof(*drop));
        /* Inside the cell it came out of, as its box is less than half a cell wide. */
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            drop->position[a] = position[a] + item_drop_random_around(&store->random, ITEM_DROP_SCATTER);
        }
        drop->velocity[COLLISION_AXIS_X] = item_drop_random_around(&store->random, ITEM_DROP_THROW_ACROSS);
        drop->velocity[COLLISION_AXIS_Y] = item_drop_random_around(&store->random, ITEM_DROP_THROW_ACROSS);
        drop->velocity[COLLISION_AXIS_Z] = ITEM_DROP_THROW_UP;
        drop->id = store->next_id;
        store->next_id++;
        drop->item = item;
        drop->count = (uint16_t)stack;
        drop->pickup_delay = ITEM_DROP_PICKUP_DELAY_TICKS;
        store->dropped_count += stack;
    }
}

/* ---- One stack's tick ---------------------------------------------------------------------------------------------- */

static int32_t item_drop_cell_of(float64_t at) {
    return (int32_t)floor(at);
}

/* A move would take the edge of what is loaded for a wall, and pushing it out of that would make no sense, so a box over that edge
 * waits with the rest. One that only touches the edge, having been stopped there, is not over it. */
static bool item_drop_is_loaded(const world_map_t *map, const collision_box_t *box) {
    for (uint32_t corner = 0u; corner < 4u; corner++) {
        const float64_t x = (corner & 1u) != 0u ? box->max[0] - COLLISION_EPSILON : box->min[0];
        const float64_t y = (corner & 2u) != 0u ? box->max[1] - COLLISION_EPSILON : box->min[1];
        if (world_map_column_at(map, column_chunk_of(item_drop_cell_of(x)), column_chunk_of(item_drop_cell_of(y))) ==
            nullptr) {
            return false;
        }
    }
    return true;
}

/* Whether anything it touches ends it. */
static bool item_drop_is_destroyed(const world_map_t *map, const collision_box_t *box) {
    const int32_t x0 = item_drop_cell_of(box->min[0] - ITEM_DROP_TOUCH);
    const int32_t x1 = item_drop_cell_of(box->max[0] + ITEM_DROP_TOUCH);
    const int32_t y0 = item_drop_cell_of(box->min[1] - ITEM_DROP_TOUCH);
    const int32_t y1 = item_drop_cell_of(box->max[1] + ITEM_DROP_TOUCH);
    const int32_t z0 = item_drop_cell_of(box->min[2] - ITEM_DROP_TOUCH);
    const int32_t z1 = item_drop_cell_of(box->max[2] + ITEM_DROP_TOUCH);
    for (int32_t x = x0; x <= x1; x++) {
        for (int32_t y = y0; y <= y1; y++) {
            for (int32_t z = z0; z <= z1; z++) {
                uint16_t block = BLOCK_ID_AIR;
                if (collision_cell_read(map, x, y, z, &block) && (block == BLOCK_ID_LAVA || block == BLOCK_ID_CACTUS)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* What is under the box's bottom, for how it slides. */
static float64_t item_drop_friction_under(const world_map_t *map, const item_drop_t *drop, const collision_box_t *box) {
    uint16_t under = BLOCK_ID_AIR;
    if (!collision_cell_read(map, item_drop_cell_of(drop->position[0]), item_drop_cell_of(drop->position[1]),
                             item_drop_cell_of(box->min[2] - ITEM_DROP_TOUCH), &under)) {
        return ITEM_DROP_FRICTION_GROUND;
    }
    if (under == BLOCK_ID_ICE || under == BLOCK_ID_PACKED_ICE) {
        return ITEM_DROP_FRICTION_ICE;
    }
    return under == BLOCK_ID_BLUE_ICE ? ITEM_DROP_FRICTION_BLUE_ICE : ITEM_DROP_FRICTION_GROUND;
}

static bool item_drop_cell_is_open(const world_map_t *map, int32_t x, int32_t y, int32_t z) {
    uint16_t block = BLOCK_ID_AIR;
    return collision_cell_read(map, x, y, z, &block) && (block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u;
}

/* A block has come down on it. With its middle in an open cell it only sticks out of that, and moves in, which is the end of it, as its
 * box is less than a cell. With its middle in a solid one, it goes for the nearest face with nothing behind it, and up if none has, as
 * a world's top is always open: going that way only makes that face nearer, so it does not turn back. */
static void item_drop_push_out(const world_map_t *map, item_drop_t *drop) {
    const int32_t cell[COLLISION_AXES] = {item_drop_cell_of(drop->position[0]), item_drop_cell_of(drop->position[1]),
                                          item_drop_cell_of(drop->position[2])};
    drop->velocity[0] = 0.0f64;
    drop->velocity[1] = 0.0f64;
    drop->velocity[2] = 0.0f64;
    drop->on_ground = 0u;
    if (item_drop_cell_is_open(map, cell[0], cell[1], cell[2])) {
        const float64_t half = ITEM_DROP_SIZE / 2.0f64;
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            const float64_t low = (float64_t)cell[a] + half, high = (float64_t)cell[a] + 1.0f64 - half;
            drop->position[a] = drop->position[a] < low ? low : (drop->position[a] > high ? high : drop->position[a]);
        }
        return;
    }
    uint32_t best_axis = COLLISION_AXIS_Z;
    float64_t best_sign = 1.0f64, best_distance = INFINITY;
    /* Up first, so that it wins a tie. */
    static const uint8_t order[COLLISION_AXES] = {COLLISION_AXIS_Z, COLLISION_AXIS_X, COLLISION_AXIS_Y};
    for (uint32_t i = 0u; i < COLLISION_AXES; i++) {
        const uint32_t a = order[i];
        const float64_t inside = drop->position[a] - (float64_t)cell[a];
        for (int32_t sign = 1; sign >= -1; sign -= 2) {
            const float64_t distance = sign > 0 ? 1.0f64 - inside : inside;
            int32_t next[COLLISION_AXES] = {cell[0], cell[1], cell[2]};
            next[a] += sign;
            if (distance < best_distance && item_drop_cell_is_open(map, next[0], next[1], next[2])) {
                best_distance = distance;
                best_axis = a;
                best_sign = (float64_t)sign;
            }
        }
    }
    /* Straight through, as a sweep would stop it at the next block over if it is buried deep. */
    drop->position[best_axis] += best_sign * ITEM_DROP_PUSH_SPEED;
}

static bool item_drop_is_in_water(const world_map_t *map, const item_drop_t *drop, const collision_box_t *box) {
    uint16_t block = BLOCK_ID_AIR;
    return collision_cell_read(map, item_drop_cell_of(drop->position[0]), item_drop_cell_of(drop->position[1]),
                               item_drop_cell_of(box->min[2] + ITEM_DROP_WATER_DEPTH), &block) &&
           block == BLOCK_ID_WATER;
}

/* How its speed changes this tick, by whatever it is in. */
static void item_drop_accelerate(const world_map_t *map, item_drop_t *drop, const collision_box_t *box) {
    if (item_drop_is_in_water(map, drop, box)) {
        drop->velocity[COLLISION_AXIS_X] *= ITEM_DROP_WATER_DRAG;
        drop->velocity[COLLISION_AXIS_Y] *= ITEM_DROP_WATER_DRAG;
        if (drop->velocity[COLLISION_AXIS_Z] < ITEM_DROP_WATER_RISE_MAX) {
            drop->velocity[COLLISION_AXIS_Z] += ITEM_DROP_WATER_RISE;
        }
    } else {
        drop->velocity[COLLISION_AXIS_Z] -= ITEM_DROP_GRAVITY;
    }
}

static void item_drop_move(physics_t *physics, const world_map_t *map, item_drop_t *drop, const collision_box_t *box) {
    collision_move_t move;
    physics_move(physics, map, box, drop->velocity, &move);
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        drop->position[a] += move.moved[a];
    }
    drop->velocity[COLLISION_AXIS_X] = move.hit_x != 0u ? 0.0f64 : drop->velocity[COLLISION_AXIS_X];
    drop->velocity[COLLISION_AXIS_Y] = move.hit_y != 0u ? 0.0f64 : drop->velocity[COLLISION_AXIS_Y];
    if (move.hit_z_down != 0u || move.hit_z_up != 0u) {
        drop->velocity[COLLISION_AXIS_Z] = 0.0f64;
    }
    drop->on_ground = move.hit_z_down != 0u ? 1u : 0u;
    const collision_box_t moved = item_drop_box(drop->position);
    const float64_t across =
        ITEM_DROP_DRAG * (drop->on_ground != 0u ? item_drop_friction_under(map, drop, &moved) : 1.0f64);
    drop->velocity[COLLISION_AXIS_X] *= across;
    drop->velocity[COLLISION_AXIS_Y] *= across;
    drop->velocity[COLLISION_AXIS_Z] *= ITEM_DROP_DRAG;
}

/* The published shortcut: what lies still has nothing to work out on most ticks. It finds out the block under it has gone within
 * ITEM_DROP_REST_EVERY_TICKS. */
static bool item_drop_is_resting(const item_drop_store_t *store, const item_drop_t *drop) {
    const float64_t x = drop->velocity[COLLISION_AXIS_X], y = drop->velocity[COLLISION_AXIS_Y];
    return drop->on_ground != 0u && x * x + y * y <= ITEM_DROP_REST_SPEED_SQUARED &&
           (store->tick_count + drop->id) % ITEM_DROP_REST_EVERY_TICKS != 0u;
}

static void item_drop_tick_one(item_drop_store_t *store, physics_t *physics, const world_map_t *map, uint32_t index) {
    item_drop_t *const drop = &store->borrowed_items[index];
    const collision_box_t box = item_drop_box(drop->position);
    if (!item_drop_is_loaded(map, &box)) {
        return;
    }
    drop->pickup_delay = drop->pickup_delay != 0u ? (uint16_t)(drop->pickup_delay - 1u) : 0u;
    drop->age++;
    if (drop->age >= ITEM_DROP_DESPAWN_TICKS) {
        store->despawned_count += drop->count;
        item_drop_remove(store, index);
        return;
    }
    if (item_drop_is_resting(store, drop)) {
        return;
    }
    if (item_drop_is_destroyed(map, &box)) {
        store->destroyed_count += drop->count;
        item_drop_remove(store, index);
        return;
    }
    if (!physics_box_is_clear(physics, map, &box)) {
        item_drop_push_out(map, drop);
        return;
    }
    item_drop_accelerate(map, drop, &box);
    item_drop_move(physics, map, drop, &box);
}

/* ---- Merging ------------------------------------------------------------------------------------------------------- */

static uint32_t item_drop_bucket_of(int32_t x, int32_t y, int32_t z) {
    const uint32_t hash = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)z * 83492791u);
    return (hash ^ (hash >> 16u)) & (ITEM_DROP_GRID_BUCKETS - 1u);
}

static void item_drop_grid_build(item_drop_store_t *store) {
    memset(store->borrowed_buckets, 0xFF, (uint64_t)ITEM_DROP_GRID_BUCKETS * sizeof(uint32_t));
    for (uint32_t i = 0u; i < store->count; i++) {
        const item_drop_t *const drop = &store->borrowed_items[i];
        const uint32_t bucket =
            item_drop_bucket_of(item_drop_cell_of(drop->position[0]), item_drop_cell_of(drop->position[1]),
                                item_drop_cell_of(drop->position[2]));
        store->borrowed_chain[i] = store->borrowed_buckets[bucket];
        store->borrowed_buckets[bucket] = i;
    }
}

/* Each box grown sideways by the reach and not up: so the middles may be this far apart. Two that only touch do not reach. */
static bool item_drop_in_reach(const item_drop_t *a, const item_drop_t *b) {
    const float64_t across = ITEM_DROP_SIZE + ITEM_DROP_MERGE_REACH;
    return fabs(a->position[0] - b->position[0]) < across && fabs(a->position[1] - b->position[1]) < across &&
           fabs(a->position[2] - b->position[2]) < ITEM_DROP_SIZE;
}

static bool item_drop_can_merge(const item_drop_t *drop) {
    return drop->count != 0u && drop->count < ITEM_DROP_STACK_MAX;
}

/* The smaller goes into the larger, as much as fits. What is left of it stays where it is. */
static void item_drop_join(item_drop_store_t *store, item_drop_t *a, item_drop_t *b) {
    item_drop_t *const into = a->count >= b->count ? a : b;
    item_drop_t *const from = into == a ? b : a;
    const uint32_t room = ITEM_DROP_STACK_MAX - (uint32_t)into->count;
    const uint32_t moved = (uint32_t)from->count < room ? from->count : room;
    into->count = (uint16_t)(into->count + moved);
    from->count = (uint16_t)(from->count - moved);
    into->age = from->age < into->age ? from->age : into->age;
    into->pickup_delay = from->pickup_delay > into->pickup_delay ? from->pickup_delay : into->pickup_delay;
    store->merged_count += from->count == 0u ? 1u : 0u;
}

/* Those in one bucket with a higher index, so that each pair is looked at from one side. Two cells around may share a bucket, and then
 * a pair is looked at twice, which is harmless: whichever is left of it after the first look cannot merge again. */
static void item_drop_merge_bucket(item_drop_store_t *store, uint32_t index, uint32_t bucket) {
    item_drop_t *const drop = &store->borrowed_items[index];
    for (uint32_t other = store->borrowed_buckets[bucket]; other != ITEM_DROP_GRID_NONE && item_drop_can_merge(drop);
         other = store->borrowed_chain[other]) {
        item_drop_t *const near = &store->borrowed_items[other];
        if (other > index && near->item == drop->item && item_drop_can_merge(near) && item_drop_in_reach(drop, near)) {
            item_drop_join(store, drop, near);
        }
    }
}

static void item_drop_merge_all(item_drop_store_t *store) {
    item_drop_grid_build(store);
    for (uint32_t i = 0u; i < store->count; i++) {
        const item_drop_t *const drop = &store->borrowed_items[i];
        if (!item_drop_can_merge(drop)) {
            continue;
        }
        /* A reach is under a cell on every axis: the cells around hold whatever it can reach. */
        const int32_t x = item_drop_cell_of(drop->position[0]), y = item_drop_cell_of(drop->position[1]),
                      z = item_drop_cell_of(drop->position[2]);
        for (int32_t dz = -1; dz <= 1; dz++) {
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    item_drop_merge_bucket(store, i, item_drop_bucket_of(x + dx, y + dy, z + dz));
                }
            }
        }
    }
    for (uint32_t i = store->count; i-- > 0u;) {
        if (store->borrowed_items[i].count == 0u) {
            item_drop_remove(store, i);
        }
    }
}

/* ---- Everything ---------------------------------------------------------------------------------------------------- */

void item_drop_tick(item_drop_store_t *store, physics_t *physics, const world_map_t *map) {
    store->tick_count++;
    for (uint32_t i = store->count; i-- > 0u;) {
        item_drop_tick_one(store, physics, map, i);
    }
    if (store->tick_count % ITEM_DROP_MERGE_EVERY_TICKS == 0u) {
        item_drop_merge_all(store);
    }
}

uint32_t item_drop_pickup(item_drop_store_t *store, const collision_box_t *reach, item_drop_take_t *take,
                          void *borrowed_context) {
    uint32_t total = 0u;
    for (uint32_t i = store->count; i-- > 0u;) {
        item_drop_t *const drop = &store->borrowed_items[i];
        if (drop->pickup_delay != 0u) {
            continue;
        }
        const collision_box_t box = item_drop_box(drop->position);
        if (!collision_overlaps_any(&box, 1u, reach)) {
            continue;
        }
        const uint32_t taken = take(borrowed_context, drop->item, drop->count);
        ASSERT_ALWAYS(taken <= drop->count);
        drop->count = (uint16_t)(drop->count - taken);
        total += taken;
        if (drop->count == 0u) {
            item_drop_remove(store, i);
        }
    }
    store->picked_up_count += total;
    return total;
}

uint64_t item_drop_on_ground(const item_drop_store_t *store) {
    uint64_t total = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        total += store->borrowed_items[i].count;
    }
    return total;
}
