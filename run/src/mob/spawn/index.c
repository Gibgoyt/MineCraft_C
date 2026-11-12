/* See README.md. The Arena tier by its depth: it takes one array from the arena it is given, and never frees. */
#include "mob/spawn/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <math.h>
#include <string.h>

#define MOB_SPAWN_PI 3.14159265358979f64

/* A body's sex in the store is a sex of the tables. */
static_assert((uint32_t)ENTITY_STORE_SEX_NONE == MOB_SEX_NONE && (uint32_t)ENTITY_STORE_SEX_MALE == MOB_SEX_MALE &&
              (uint32_t)ENTITY_STORE_SEX_FEMALE == MOB_SEX_FEMALE);

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
    /* With an on list, only what is on it: grass for an animal of the fields. */
    for (uint32_t i = 0u; i < def->on_count; i++) {
        if (mob_on[def->first_on + i] == block) {
            return true;
        }
    }
    return def->on_count == 0u;
}

static float64_t mob_spawn_distance_squared(const float64_t a[COLLISION_AXES], const float64_t b[COLLISION_AXES]) {
    const float64_t x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

bool mob_spawn_has_sexes(mob_id_t mob) {
    return (uint32_t)mob < MOB_COUNT && mob_defs[mob].first_sex != 0u;
}

/* Of a body of this sex: 1 for none. */
static float64_t mob_spawn_scale(mob_id_t mob, uint8_t sex) {
    if (!mob_spawn_has_sexes(mob) || sex == ENTITY_STORE_SEX_NONE) {
        return 1.0f64;
    }
    return (float64_t)mob_sexes[mob_defs[mob].first_sex + (uint32_t)sex - (uint32_t)ENTITY_STORE_SEX_MALE].scale;
}

/* The larger of its sexes, for a place either must fit. */
static float64_t mob_spawn_scale_most(mob_id_t mob) {
    const float64_t male = mob_spawn_scale(mob, ENTITY_STORE_SEX_MALE);
    const float64_t female = mob_spawn_scale(mob, ENTITY_STORE_SEX_FEMALE);
    return male > female ? male : female;
}

static void mob_spawn_box_of(const mob_def_t *def, const float64_t position[COLLISION_AXES], float64_t scale,
                             collision_box_t *out) {
    const float64_t half = (float64_t)def->half_width * scale;
    *out = (collision_box_t){
        .min = {position[0] - half, position[1] - half, position[2]},
        .max = {position[0] + half, position[1] + half, position[2] + (float64_t)def->height * scale},
    };
}

bool mob_spawn_biome_allows(mob_id_t mob, biome_id_t biome) {
    const mob_def_t *const def = &mob_defs[mob];
    for (uint32_t i = 0u; i < def->biome_count; i++) {
        if (mob_biomes[def->first_biome + i] == (uint8_t)biome) {
            return true;
        }
    }
    return def->biome_count == 0u;
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
    if (light < def->min_light) {
        return MOB_SPAWN_TOO_DARK;
    }
    return light <= light_roll ? MOB_SPAWN_OK : MOB_SPAWN_TOO_BRIGHT;
}

/* How near the player it may come: on the first day, nearer, so that the first herds are seen. */
static float64_t mob_spawn_nearest(const mob_def_t *def, const mob_spawn_view_t *view) {
    const uint32_t near =
        view->first_day != 0u && def->mind == MOB_MIND_PASSIVE && def->min_distance > MOB_SPAWN_FIRST_DAY_NEAR
            ? MOB_SPAWN_FIRST_DAY_NEAR
            : def->min_distance;
    return (float64_t)near;
}

mob_spawn_status_t mob_spawn_check(physics_t *physics, const world_map_t *map, mob_id_t mob,
                                   const float64_t position[COLLISION_AXES], const mob_spawn_view_t *view,
                                   uint32_t light_roll) {
    ASSERT_ALWAYS(mob != MOB_ID_NONE && (uint32_t)mob < MOB_COUNT);
    const mob_def_t *const def = &mob_defs[mob];
    const float64_t reach = mob_spawn_distance_squared(position, view->position);
    const float64_t nearest = mob_spawn_nearest(def, view);
    if (reach < nearest * nearest) {
        return MOB_SPAWN_TOO_NEAR;
    }
    /* A passive mob, whose far is 0, is never taken away, so no distance is too far for it. */
    if (def->despawn_far != 0u && reach > (float64_t)def->despawn_far * (float64_t)def->despawn_far) {
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
    const int32_t height = z - (int32_t)MC_WORLD_Z_SEA;
    if (height < (int32_t)def->min_height || height > (int32_t)def->max_height) {
        return MOB_SPAWN_BAD_HEIGHT;
    }
    if (def->biome_count != 0u && view->biomes != nullptr &&
        !mob_spawn_biome_allows(mob, terrain_biome_column_at(view->biomes, view->climate, x, y).biome)) {
        return MOB_SPAWN_BAD_BIOME;
    }
    const mob_spawn_status_t light = mob_spawn_check_light(def, packed, view->sun_height, light_roll);
    if (light != MOB_SPAWN_OK) {
        return light;
    }
    collision_box_t box;
    mob_spawn_box_of(def, position, mob_spawn_scale_most(mob), &box);
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

entity_handle_t mob_spawn_body_as(mob_spawn_t *spawn, entity_store_t *store, mob_id_t mob,
                                  const float64_t position[COLLISION_AXES], float32_t yaw, uint8_t sex) {
    ASSERT_ALWAYS(mob != MOB_ID_NONE && (uint32_t)mob < MOB_COUNT);
    ASSERT_ALWAYS(mob_spawn_has_sexes(mob) ? (sex == ENTITY_STORE_SEX_MALE || sex == ENTITY_STORE_SEX_FEMALE)
                                           : sex == ENTITY_STORE_SEX_NONE);
    const mob_def_t *const def = &mob_defs[mob];
    const float64_t scale = mob_spawn_scale(mob, sex);
    const entity_store_spawn_t body = {
        .position = {position[0], position[1], position[2]},
        .half_width = (float32_t)((float64_t)def->half_width * scale),
        .height = (float32_t)((float64_t)def->height * scale),
        .yaw = yaw,
        .health = def->health,
        .kind = (uint16_t)mob,
        .sex = sex,
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

/* Half and half, and a draw only for a mob that has both. */
static uint8_t mob_spawn_roll_sex(mob_spawn_t *spawn, mob_id_t mob) {
    if (!mob_spawn_has_sexes(mob)) {
        return ENTITY_STORE_SEX_NONE;
    }
    return terrain_rng_next_below(&spawn->random, 2u) == 0u ? ENTITY_STORE_SEX_MALE : ENTITY_STORE_SEX_FEMALE;
}

entity_handle_t mob_spawn_body(mob_spawn_t *spawn, entity_store_t *store, mob_id_t mob,
                               const float64_t position[COLLISION_AXES], float32_t yaw) {
    ASSERT_ALWAYS(mob != MOB_ID_NONE && (uint32_t)mob < MOB_COUNT);
    return mob_spawn_body_as(spawn, store, mob, position, yaw, mob_spawn_roll_sex(spawn, mob));
}

uint32_t mob_spawn_cap(uint64_t columns_loaded) {
    const uint64_t cap = (uint64_t)MOB_SPAWN_CAP * columns_loaded / (uint64_t)MOB_SPAWN_CAP_COLUMNS;
    return cap > (uint64_t)MOB_SPAWN_CAP ? MOB_SPAWN_CAP : (uint32_t)cap;
}

uint32_t mob_spawn_animal_cap(uint64_t columns_loaded) {
    const uint64_t cap = (uint64_t)MOB_SPAWN_ANIMAL_CAP * columns_loaded / (uint64_t)MOB_SPAWN_CAP_COLUMNS;
    return cap > (uint64_t)MOB_SPAWN_ANIMAL_CAP ? MOB_SPAWN_ANIMAL_CAP : (uint32_t)cap;
}

static bool mob_spawn_kind_is_mob(uint16_t kind) {
    return kind != (uint16_t)MOB_ID_NONE && (uint32_t)kind < MOB_COUNT;
}

/* What is about after despawning: every hostile mob, and the passive ones in loaded columns, each against its own cap; and every
 * animal the store keeps, against MOB_SPAWN_ANIMAL_STORE_MAX, with the furthest left behind. */
typedef struct mob_spawn_counts_t {
    float64_t furthest; /* 0: along x or y from the player, of the furthest animal waiting for its column */
    entity_handle_t furthest_handle; /* 8: ENTITY_HANDLE_NONE if none waits */
    uint32_t hostile;                /* 12 */
    uint32_t passive;                /* 16 */
    uint32_t stored;                 /* 20: animals in the store, loaded or not */
} mob_spawn_counts_t;
static_assert(sizeof(mob_spawn_counts_t) == 24);

/* Takes away what the rules say, from the end since a removal moves the last body into the gap. A passive mob is never taken away.
 * Returns how many of each are left to count. */
static mob_spawn_counts_t mob_spawn_despawn(mob_spawn_t *spawn, entity_store_t *store, const mob_spawn_view_t *view) {
    mob_spawn_counts_t counts = {.furthest = 0.0f64, .furthest_handle = ENTITY_HANDLE_NONE};
    for (uint32_t i = store->count; i-- > 0u;) {
        const uint16_t kind = store->borrowed_kind[i];
        if (!mob_spawn_kind_is_mob(kind)) {
            continue;
        }
        const mob_def_t *const def = &mob_defs[kind];
        if (def->mind == MOB_MIND_PASSIVE) {
            counts.stored++;
            if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) == 0u) {
                counts.passive++;
                continue;
            }
            const float64_t across = fmax(fabs(store->borrowed_position[0][i] - view->position[0]),
                                          fabs(store->borrowed_position[1][i] - view->position[1]));
            if (counts.furthest_handle == ENTITY_HANDLE_NONE || across > counts.furthest) {
                counts.furthest = across;
                counts.furthest_handle = store->borrowed_handle[i];
            }
            continue;
        }
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
            counts.hostile++;
        }
    }
    return counts;
}

/* How often a mob is picked among those of a mind: 0 for one of the other mind. */
static uint32_t mob_spawn_weight(const mob_spawn_view_t *view, uint32_t mob, uint8_t mind) {
    if (mob_defs[mob].mind != mind) {
        return 0u;
    }
    return view->weights != nullptr ? (uint32_t)view->weights[mob] : (uint32_t)mob_defs[mob].weight;
}

/* What the weights of a mind add up to: 0 if none of it ever comes. */
static uint32_t mob_spawn_weight_total(const mob_spawn_view_t *view, uint8_t mind) {
    uint32_t total = 0u;
    for (uint32_t i = 1u; i < MOB_COUNT; i++) {
        total += mob_spawn_weight(view, i, mind);
    }
    return total;
}

/* Which mob of a mind comes, by weight, from weights adding up to total, which is above 0. */
static mob_id_t mob_spawn_pick(mob_spawn_t *spawn, const mob_spawn_view_t *view, uint8_t mind, uint32_t total) {
    uint32_t roll = terrain_rng_next_below(&spawn->random, total);
    for (uint32_t i = 1u; i < MOB_COUNT; i++) {
        const uint32_t weight = mob_spawn_weight(view, i, mind);
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
 * lasts. A hostile pack's light is a roll, so that the darker the likelier; a herd's is its max_light, as daylight is no rarer than
 * dusk to it. A herd of a mob with sexes is male first and female second, so that any herd of two or more has both, and then either. */
static uint32_t mob_spawn_pack(mob_spawn_t *spawn, entity_store_t *store, physics_t *physics, const world_map_t *map,
                               const mob_spawn_view_t *view, mob_id_t mob, int32_t start_x, int32_t start_y, int32_t z,
                               uint32_t room) {
    const mob_def_t *const def = &mob_defs[mob];
    const bool herd = def->mind == MOB_MIND_PASSIVE;
    const uint32_t size =
        def->pack_min + terrain_rng_next_below(&spawn->random, (uint32_t)(def->pack_max - def->pack_min) + 1u);
    int32_t x = start_x, y = start_y;
    uint32_t made = 0u;
    for (uint32_t i = 0u; i < size && made < room; i++) {
        x += mob_spawn_step(spawn);
        y += mob_spawn_step(spawn);
        const float64_t position[COLLISION_AXES] = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)z};
        const uint32_t roll =
            herd ? (uint32_t)def->max_light : terrain_rng_next_below(&spawn->random, (uint32_t)def->max_light + 1u);
        if (mob_spawn_check(physics, map, mob, position, view, roll) != MOB_SPAWN_OK) {
            continue;
        }
        const float32_t yaw = terrain_rng_next_f32(&spawn->random) * (float32_t)(2.0f64 * MOB_SPAWN_PI);
        uint8_t sex = ENTITY_STORE_SEX_NONE;
        if (mob_spawn_has_sexes(mob)) {
            sex = made == 0u   ? ENTITY_STORE_SEX_MALE
                  : made == 1u ? ENTITY_STORE_SEX_FEMALE
                               : mob_spawn_roll_sex(spawn, mob);
        }
        if (mob_spawn_body_as(spawn, store, mob, position, yaw, sex) == ENTITY_HANDLE_NONE) {
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

/* The top of what a column holds: the last layer of its highest section that is not all air, where the ground under open sky is
 * found by looking down. False for a column of air. */
static bool mob_spawn_column_top(const column_t *column, int32_t *out_z) {
    for (uint32_t i = COLUMN_SECTIONS; i-- > 0u;) {
        const section_t *const section = &column->sections[i];
        if (section->bits != SECTION_BITS_UNIFORM || section->block_uniform != BLOCK_ID_AIR) {
            /* The layer over it: the top layer itself may be ground, and a floor is looked for under where it starts. */
            *out_z = column_z_clamp(COLUMN_Z_STORAGE_MIN + (int32_t)((i + 1u) * SECTION_SIZE));
            return true;
        }
    }
    return false;
}

/* The hostile mobs' tries: anywhere in reach, at any height, as caves count. */
static void mob_spawn_hostile(mob_spawn_t *spawn, entity_store_t *store, physics_t *physics, const world_map_t *map,
                              const mob_spawn_view_t *view, uint32_t alive) {
    const uint32_t total = mob_spawn_weight_total(view, MOB_MIND_HOSTILE);
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
        const mob_id_t mob = mob_spawn_pick(spawn, view, MOB_MIND_HOSTILE, total);
        alive += mob_spawn_pack(spawn, store, physics, map, view, mob, x, y, floor_z, cap - alive);
    }
}

/* The animals' tries: now and then, or every tick on the first day, a herd on the ground under the sky of a column in reach, under
 * its own cap. */
static void mob_spawn_passive(mob_spawn_t *spawn, entity_store_t *store, physics_t *physics, const world_map_t *map,
                              const mob_spawn_view_t *view, mob_spawn_counts_t *counts) {
    const uint32_t total = mob_spawn_weight_total(view, MOB_MIND_PASSIVE);
    if (total == 0u) {
        return; /* no animal comes by itself, and no number is drawn */
    }
    uint32_t alive = counts->passive;
    const uint32_t cap_view = mob_spawn_animal_cap(map->ready_count);
    if (alive >= cap_view ||
        (view->first_day == 0u && terrain_rng_next_below(&spawn->random, MOB_SPAWN_ANIMAL_ONE_IN) != 0u)) {
        return;
    }
    /* The store keeps at most MOB_SPAWN_ANIMAL_STORE_MAX animals. At that many, a herd that would come here forgets the one left
     * furthest behind, past MOB_SPAWN_ANIMAL_FORGET, which counts as gone; with none that far it does not come. */
    if (counts->stored >= MOB_SPAWN_ANIMAL_STORE_MAX) {
        if (counts->furthest_handle == ENTITY_HANDLE_NONE || counts->furthest <= (float64_t)MOB_SPAWN_ANIMAL_FORGET) {
            return;
        }
        ASSERT_ALWAYS(entity_store_remove(store, counts->furthest_handle));
        spawn->despawned_count++;
        counts->stored--;
    }
    /* As many more as the store has room for, beside those already about. */
    const uint32_t room = alive + (MOB_SPAWN_ANIMAL_STORE_MAX - counts->stored);
    const uint32_t cap = cap_view < room ? cap_view : room;
    const uint32_t reach = view->first_day != 0u ? MOB_SPAWN_FIRST_DAY_REACH : MOB_SPAWN_REACH;
    const int32_t player_x = (int32_t)floor(view->position[0]), player_y = (int32_t)floor(view->position[1]);
    const uint32_t across = 2u * reach + 1u;
    for (uint32_t i = 0u; i < MOB_SPAWN_ANIMAL_TRIES && alive < cap; i++) {
        const int32_t x = player_x - (int32_t)reach + (int32_t)terrain_rng_next_below(&spawn->random, across);
        const int32_t y = player_y - (int32_t)reach + (int32_t)terrain_rng_next_below(&spawn->random, across);
        const column_t *const column = world_map_column_at(map, column_chunk_of(x), column_chunk_of(y));
        int32_t top = 0, floor_z = 0;
        if (column == nullptr || !mob_spawn_column_top(column, &top) ||
            !mob_spawn_find_floor(map, x, y, top, &floor_z)) {
            continue;
        }
        const mob_id_t mob = mob_spawn_pick(spawn, view, MOB_MIND_PASSIVE, total);
        alive += mob_spawn_pack(spawn, store, physics, map, view, mob, x, y, floor_z, cap - alive);
    }
}

void mob_spawn_tick(mob_spawn_t *spawn, entity_store_t *store, physics_t *physics, const world_map_t *map,
                    const mob_spawn_view_t *view) {
    mob_spawn_counts_t counts = mob_spawn_despawn(spawn, store, view);
    mob_spawn_hostile(spawn, store, physics, map, view, counts.hostile);
    mob_spawn_passive(spawn, store, physics, map, view, &counts);
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
