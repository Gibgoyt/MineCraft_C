/* See README.md. The Arena tier by its depth: it takes one array from the arena it is given, and never frees. */
#include "mob/spawn/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <math.h>
#include <string.h>

#define MOB_SPAWN_PI 3.14159265358979f64

/* A body's cells: its box stands this far inside a cell it only touches. */
#define MOB_SPAWN_TOUCH 1.0e-6f64

/* The opacity of a block that lets nothing through. */
enum : uint8_t { MOB_SPAWN_OPAQUE = 15u };
/* Layers from the bottom of the world a try always looks among. */
enum : uint32_t { MOB_SPAWN_TALL_LEAST = 751u };

void mob_spawn_start(mob_spawn_t *spawn, arena_t *arena, uint64_t seed) {
    memset(spawn, 0, sizeof(*spawn));
    spawn->borrowed_idle = arena_alloc_zeroed(arena, (uint64_t)ENTITY_STORE_CAPACITY * sizeof(uint32_t), 64u);
    spawn->random = terrain_rng_start(seed);
}

uint32_t mob_spawn_sky_darken(float64_t sun_height) {
    /* The published curve, with the sun's real height in place of the clock's (plan 004 phase 02): full day holds while the sun is
     * well up, and full night while it is 14.5 degrees or more down, so nights follow the latitude and the season. */
    const float64_t height = sun_height * 2.0f64 + 0.5f64;
    const float64_t lit = height < 0.0f64 ? 0.0f64 : (height > 1.0f64 ? 1.0f64 : height);
    return (uint32_t)((1.0f64 - lit) * (float64_t)MOB_SPAWN_DARKEN_MAX);
}

static bool mob_spawn_block_is_floor(const mob_def_t *def, uint16_t block) {
    const block_def_t *const below = &block_defs[block];
    if (below->shape != BLOCK_SHAPE_CUBE || (below->flags & BLOCK_FLAG_SOLID) == 0u ||
        below->opacity != MOB_SPAWN_OPAQUE) {
        return false;
    }
    for (uint32_t i = 0u; i < def->never_on_count; i++) {
        if (mob_never_on[def->first_never_on + i] == block) {
            return false;
        }
    }
    return true;
}

static float64_t mob_spawn_distance_squared(const float64_t a[COLLISION_AXES], const float64_t b[COLLISION_AXES]) {
    const float64_t x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

static void mob_spawn_box_of(const mob_def_t *def, const float64_t position[COLLISION_AXES], collision_box_t *out) {
    const float64_t half = (float64_t)def->half_width;
    *out = (collision_box_t){
        .min = {position[0] - half, position[1] - half, position[2]},
        .max = {position[0] + half, position[1] + half, position[2] + (float64_t)def->height},
    };
}

/* Whether water or lava is anywhere in a box known to be in loaded columns. */
static bool mob_spawn_box_is_wet(const world_map_t *map, const collision_box_t *box) {
    const int32_t x0 = (int32_t)floor(box->min[0]), x1 = (int32_t)floor(box->max[0] - MOB_SPAWN_TOUCH);
    const int32_t y0 = (int32_t)floor(box->min[1]), y1 = (int32_t)floor(box->max[1] - MOB_SPAWN_TOUCH);
    const int32_t z0 = (int32_t)floor(box->min[2]), z1 = (int32_t)floor(box->max[2] - MOB_SPAWN_TOUCH);
    for (int32_t x = x0; x <= x1; x++) {
        for (int32_t y = y0; y <= y1; y++) {
            for (int32_t z = z0; z <= z1; z++) {
                uint16_t block = BLOCK_ID_AIR;
                if (world_map_block_get(map, x, y, z, &block) == WORLD_MAP_STATUS_OK &&
                    block_defs[block].shape == BLOCK_SHAPE_FLUID) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* What the place's light says: the sky's share goes down with the sun, and block light does not. */
static mob_spawn_status_t mob_spawn_check_light(const mob_def_t *def, uint8_t packed, float64_t sun_height,
                                                uint32_t light_roll) {
    const uint32_t sky = (uint32_t)section_light_sky(packed);
    const uint32_t block = (uint32_t)section_light_block(packed);
    if (block > def->max_block_light) {
        return MOB_SPAWN_TOO_BRIGHT;
    }
    const uint32_t darken = mob_spawn_sky_darken(sun_height);
    const uint32_t from_sky = sky > darken ? sky - darken : 0u;
    const uint32_t light = from_sky > block ? from_sky : block;
    return light <= light_roll ? MOB_SPAWN_OK : MOB_SPAWN_TOO_BRIGHT;
}

mob_spawn_status_t mob_spawn_check(physics_t *physics, const world_map_t *map, mob_id_t mob,
                                   const float64_t position[COLLISION_AXES], const mob_spawn_view_t *view,
                                   uint32_t light_roll) {
    ASSERT_ALWAYS(mob != MOB_ID_NONE && (uint32_t)mob < MOB_COUNT);
    const mob_def_t *const def = &mob_defs[mob];
    const float64_t reach = mob_spawn_distance_squared(position, view->position);
    if (reach < (float64_t)def->min_distance * (float64_t)def->min_distance) {
        return MOB_SPAWN_TOO_NEAR;
    }
    if (reach > (float64_t)def->despawn_far * (float64_t)def->despawn_far) {
        return MOB_SPAWN_TOO_FAR;
    }
    const int32_t x = (int32_t)floor(position[0]), y = (int32_t)floor(position[1]), z = (int32_t)floor(position[2]);
    uint8_t packed = 0u;
    uint16_t below = BLOCK_ID_AIR;
    const world_map_status_t light_status = world_map_light_get(map, x, y, z, &packed);
    const world_map_status_t below_status = world_map_block_get(map, x, y, z - 1, &below);
    if (light_status == WORLD_MAP_STATUS_NOT_LOADED || below_status == WORLD_MAP_STATUS_NOT_LOADED) {
        return MOB_SPAWN_NOT_LOADED;
    }
    /* The bottom and top of the world: there is nothing to stand on that gameplay can reach. */
    if (light_status != WORLD_MAP_STATUS_OK || below_status != WORLD_MAP_STATUS_OK ||
        !mob_spawn_block_is_floor(def, below)) {
        return MOB_SPAWN_BAD_SURFACE;
    }
    const mob_spawn_status_t light = mob_spawn_check_light(def, packed, view->sun_height, light_roll);
    if (light != MOB_SPAWN_OK) {
        return light;
    }
    collision_box_t box;
    mob_spawn_box_of(def, position, &box);
    /* physics_box_is_clear calls a column not loaded unclear, and the body must know which of the two it was. */
    for (int32_t cx = (int32_t)floor(box.min[0]); cx <= (int32_t)floor(box.max[0] - MOB_SPAWN_TOUCH); cx++) {
        for (int32_t cy = (int32_t)floor(box.min[1]); cy <= (int32_t)floor(box.max[1] - MOB_SPAWN_TOUCH); cy++) {
            if (world_map_column_at(map, column_chunk_of(cx), column_chunk_of(cy)) == nullptr) {
                return MOB_SPAWN_NOT_LOADED;
            }
        }
    }
    if (!physics_box_is_clear(physics, map, &box)) {
        return MOB_SPAWN_NO_ROOM;
    }
    return mob_spawn_box_is_wet(map, &box) ? MOB_SPAWN_IN_FLUID : MOB_SPAWN_OK;
}

entity_handle_t mob_spawn_body(mob_spawn_t *spawn, entity_store_t *store, mob_id_t mob,
                               const float64_t position[COLLISION_AXES], float32_t yaw) {
    ASSERT_ALWAYS(mob != MOB_ID_NONE && (uint32_t)mob < MOB_COUNT);
    const mob_def_t *const def = &mob_defs[mob];
    const entity_store_spawn_t body = {
        .position = {position[0], position[1], position[2]},
        .half_width = def->half_width,
        .height = def->height,
        .yaw = yaw,
        .health = def->health,
        .kind = (uint16_t)mob,
    };
    const entity_handle_t handle = entity_store_spawn(store, &body);
    if (handle == ENTITY_HANDLE_NONE) {
        spawn->refused_count++;
        return handle;
    }
    spawn->borrowed_idle[entity_handle_slot(handle)] = 0u;
    spawn->spawned_count++;
    return handle;
}

uint32_t mob_spawn_cap(uint64_t columns_loaded) {
    const uint64_t cap = (uint64_t)MOB_SPAWN_CAP * columns_loaded / (uint64_t)MOB_SPAWN_CAP_COLUMNS;
    return cap > (uint64_t)MOB_SPAWN_CAP ? MOB_SPAWN_CAP : (uint32_t)cap;
}

static bool mob_spawn_kind_is_mob(uint16_t kind) {
    return kind != (uint16_t)MOB_ID_NONE && (uint32_t)kind < MOB_COUNT;
}

/* Takes away what the rules say, from the end since a removal moves the last body into the gap. Returns how many mobs are left. */
static uint32_t mob_spawn_despawn(mob_spawn_t *spawn, entity_store_t *store, const mob_spawn_view_t *view) {
    uint32_t alive = 0u;
    for (uint32_t i = store->count; i-- > 0u;) {
        const uint16_t kind = store->borrowed_kind[i];
        if (!mob_spawn_kind_is_mob(kind)) {
            continue;
        }
        const mob_def_t *const def = &mob_defs[kind];
        const entity_handle_t handle = store->borrowed_handle[i];
        const uint32_t slot = entity_handle_slot(handle);
        const float64_t position[COLLISION_AXES] = {store->borrowed_position[0][i], store->borrowed_position[1][i],
                                                    store->borrowed_position[2][i]};
        const float64_t reach = mob_spawn_distance_squared(position, view->position);
        bool gone = reach > (float64_t)def->despawn_far * (float64_t)def->despawn_far;
        if (reach <= (float64_t)def->despawn_near * (float64_t)def->despawn_near) {
            spawn->borrowed_idle[slot] = 0u;
        } else if (!gone && (store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) == 0u) {
            /* A body whose column is not loaded is not there to come or go: it only goes when too far. */
            if (spawn->borrowed_idle[slot] < def->despawn_idle_ticks) {
                spawn->borrowed_idle[slot]++;
            } else {
                gone = terrain_rng_next_below(&spawn->random, def->despawn_one_in) == 0u;
            }
        }
        if (gone) {
            ASSERT_ALWAYS(entity_store_remove(store, handle));
            spawn->despawned_count++;
        } else {
            alive++;
        }
    }
    return alive;
}

static uint32_t mob_spawn_weight(const mob_spawn_view_t *view, uint32_t mob) {
    return view->weights != nullptr ? (uint32_t)view->weights[mob] : (uint32_t)mob_defs[mob].weight;
}

/* What the weights add up to: 0 if no mob ever comes. */
static uint32_t mob_spawn_weight_total(const mob_spawn_view_t *view) {
    uint32_t total = 0u;
    for (uint32_t i = 1u; i < MOB_COUNT; i++) {
        total += mob_spawn_weight(view, i);
    }
    return total;
}

/* Which mob comes, by weight, from weights adding up to total, which is above 0. */
static mob_id_t mob_spawn_pick(mob_spawn_t *spawn, const mob_spawn_view_t *view, uint32_t total) {
    uint32_t roll = terrain_rng_next_below(&spawn->random, total);
    for (uint32_t i = 1u; i < MOB_COUNT; i++) {
        const uint32_t weight = mob_spawn_weight(view, i);
        if (roll < weight) {
            return (mob_id_t)i;
        }
        roll -= weight;
    }
    return MOB_ID_NONE; /* the rolls add up, so this is never reached */
}

/* The first cell at or under z that has a whole block under it and is itself not solid, within MOB_SPAWN_SCAN_CELLS. Starting inside
 * something solid finds nothing, as digging out of it would be no way to arrive. */
static bool mob_spawn_find_floor(const world_map_t *map, int32_t x, int32_t y, int32_t z, int32_t *out_z) {
    for (uint32_t i = 0u; i < MOB_SPAWN_SCAN_CELLS; i++) {
        const int32_t at = z - (int32_t)i;
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(map, x, y, at - 1, &block) != WORLD_MAP_STATUS_OK) {
            return false;
        }
        if ((block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u) {
            *out_z = at;
            return true;
        }
    }
    return false;
}

static int32_t mob_spawn_step(mob_spawn_t *spawn) {
    const uint32_t range = MOB_SPAWN_PACK_STEP + 1u;
    return (int32_t)terrain_rng_next_below(&spawn->random, range) -
           (int32_t)terrain_rng_next_below(&spawn->random, range);
}

/* One pack, near one place: each next one a step from the last, at the height of the first, as far as the rules allow and the cap
 * lasts. */
static uint32_t mob_spawn_pack(mob_spawn_t *spawn, entity_store_t *store, physics_t *physics, const world_map_t *map,
                               const mob_spawn_view_t *view, mob_id_t mob, int32_t start_x, int32_t start_y, int32_t z,
                               uint32_t room) {
    const mob_def_t *const def = &mob_defs[mob];
    const uint32_t size =
        def->pack_min + terrain_rng_next_below(&spawn->random, (uint32_t)(def->pack_max - def->pack_min) + 1u);
    int32_t x = start_x, y = start_y;
    uint32_t made = 0u;
    for (uint32_t i = 0u; i < size && made < room; i++) {
        x += mob_spawn_step(spawn);
        y += mob_spawn_step(spawn);
        const float64_t position[COLLISION_AXES] = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)z};
        const uint32_t roll = terrain_rng_next_below(&spawn->random, (uint32_t)def->max_light + 1u);
        if (mob_spawn_check(physics, map, mob, position, view, roll) != MOB_SPAWN_OK) {
            continue;
        }
        const float32_t yaw = terrain_rng_next_f32(&spawn->random) * (float32_t)(2.0f64 * MOB_SPAWN_PI);
        if (mob_spawn_body(spawn, store, mob, position, yaw) == ENTITY_HANDLE_NONE) {
            return made;
        }
        made++;
    }
    return made;
}

/* How many layers from the bottom of the world a try looks among in this column. Empty sky is not worth a try: a world made taller must not make the ground it has spawn less, and
 * a column that reaches above MOB_SPAWN_TALL_LEAST layers is looked at all the way up. */
static uint32_t mob_spawn_tall(const column_t *column) {
    if (column != nullptr) {
        const uint32_t always = column_section_of(COLUMN_Z_GAMEPLAY_MIN + (int32_t)MOB_SPAWN_TALL_LEAST - 1);
        for (uint32_t i = COLUMN_SECTIONS - 1u; i > always; i--) {
            const section_t *const section = &column->sections[i];
            if (section->bits != SECTION_BITS_UNIFORM || section->block_uniform != BLOCK_ID_AIR) {
                const int32_t top = COLUMN_Z_STORAGE_MIN + (int32_t)((i + 1u) * SECTION_SIZE) - 1;
                return (uint32_t)(column_z_clamp(top) - COLUMN_Z_GAMEPLAY_MIN) + 1u;
            }
        }
    }
    return MOB_SPAWN_TALL_LEAST;
}

void mob_spawn_tick(mob_spawn_t *spawn, entity_store_t *store, physics_t *physics, const world_map_t *map,
                    const mob_spawn_view_t *view) {
    uint32_t alive = mob_spawn_despawn(spawn, store, view);
    const uint32_t total = mob_spawn_weight_total(view);
    if (total == 0u) {
        return; /* no mob comes by itself (plan 005 phase 3 turned them all off), so no place is worth a look */
    }
    const uint32_t cap = mob_spawn_cap(map->ready_count);
    const int32_t player_x = (int32_t)floor(view->position[0]), player_y = (int32_t)floor(view->position[1]);
    const uint32_t across = 2u * MOB_SPAWN_REACH + 1u;
    for (uint32_t i = 0u; i < MOB_SPAWN_TRIES_PER_TICK && alive < cap; i++) {
        const int32_t x = player_x - (int32_t)MOB_SPAWN_REACH + (int32_t)terrain_rng_next_below(&spawn->random, across);
        const int32_t y = player_y - (int32_t)MOB_SPAWN_REACH + (int32_t)terrain_rng_next_below(&spawn->random, across);
        const column_t *const column = world_map_column_at(map, column_chunk_of(x), column_chunk_of(y));
        const int32_t z =
            COLUMN_Z_GAMEPLAY_MIN + (int32_t)terrain_rng_next_below(&spawn->random, mob_spawn_tall(column));
        if (column == nullptr) {
            continue;
        }
        int32_t floor_z = 0;
        uint16_t here = BLOCK_ID_AIR;
        if (world_map_block_get(map, x, y, z, &here) != WORLD_MAP_STATUS_OK ||
            (block_defs[here].flags & BLOCK_FLAG_SOLID) != 0u || !mob_spawn_find_floor(map, x, y, z, &floor_z)) {
            continue;
        }
        const mob_id_t mob = mob_spawn_pick(spawn, view, total);
        alive += mob_spawn_pack(spawn, store, physics, map, view, mob, x, y, floor_z, cap - alive);
    }
}

uint32_t mob_spawn_retire(entity_store_t *store, uint32_t out_removed[MOB_COUNT]) {
    uint32_t removed = 0u;
    for (uint32_t i = store->count; i-- > 0u;) {
        const uint16_t kind = store->borrowed_kind[i];
        if (!mob_spawn_kind_is_mob(kind) || mob_defs[kind].weight != 0u) {
            continue;
        }
        ASSERT_ALWAYS(entity_store_remove(store, store->borrowed_handle[i]));
        out_removed[kind]++;
        removed++;
    }
    return removed;
}
