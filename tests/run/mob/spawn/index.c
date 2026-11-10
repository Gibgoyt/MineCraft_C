/* ctest run_mob_spawn. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "heap/index.h"
#include "mob/spawn/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "time/index.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MS_NOON 1.0f64
#define MS_MIDNIGHT (-1.0f64)
#define MS_EXACT 1.0e-6f32

/* Where the player stands in these tests, on a floor whose top is at z = 0. Every chunk near it is loaded. */
#define MS_PLAYER_X 8.5f64
#define MS_PLAYER_Y 8.5f64

enum : uint32_t {
    MS_NIGHT_TICKS = 4000u,
    MS_DESPAWN_BODIES = 200u,
    MS_SPEED_TICKS = 2000u,
    MS_PACKS = 300u,
    MS_PACK_TICKS_MAX = 20000u,
};
enum : int32_t {
    /* A place in the middle of a chunk that is loaded, MS_PLACE_X - 8.5 = 24 from the player. */
    MS_PLACE_X = 32,
    MS_PLACE_Y = 8,
    /* The floor the fixture's world is given: its corners are all loaded, or the columns are skipped. */
    MS_FLOOR_MIN = -48,
    MS_FLOOR_MAX = 63,
};

typedef struct ms_rig_t {
    test_world_t world;    /* 0 */
    entity_t *entity;      /* 24 */
    entity_store_t *store; /* 32 */
    mob_spawn_t *spawn;    /* 40 */
    arena_t *arena;        /* 48: what a game's arena_world is to the spawn */
} ms_rig_t;
static_assert(sizeof(ms_rig_t) == 56);

static bool ms_begin(test_context_t *t, ms_rig_t *rig, uint64_t seed) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics))) {
        return false;
    }
    entity_open(rig->entity, seed);
    rig->store = &rig->entity->store;
    rig->spawn = test_alloc(t, sizeof(mob_spawn_t));
    rig->arena = test_world_aligned(t, sizeof(arena_t));
    arena_init(rig->arena, "ms_spawn", test_alloc(t, MOB_SPAWN_BYTES), MOB_SPAWN_BYTES);
    mob_spawn_start(rig->spawn, rig->arena, seed);
    return true;
}

static void ms_end(ms_rig_t *rig) {
    arena_deinit(rig->arena);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

/* Each loaded column of a floor one block thick under z = 0, and nothing else. */
static void ms_fill_loaded(test_context_t *t, ms_rig_t *rig, int32_t z0, int32_t z1, uint16_t block) {
    for (int32_t x = MS_FLOOR_MIN; x <= MS_FLOOR_MAX; x++) {
        for (int32_t y = MS_FLOOR_MIN; y <= MS_FLOOR_MAX; y++) {
            column_t *const column = world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y));
            for (int32_t z = z0; column != nullptr && z <= z1; z++) {
                CHECK_EQ_U64(column_block_set(column, &rig->world.world->section_pool, column_local_of(x),
                                              column_local_of(y), z, block),
                             COLUMN_STATUS_OK);
            }
        }
    }
}

/* One cell. */
static void ms_put(test_context_t *t, ms_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    test_world_fill(t, &rig->world, x, y, z, x, y, z, block);
}

/* The light of one cell, by hand: what the rules read is all that this test wants of it. */
static void ms_light(test_context_t *t, ms_rig_t *rig, int32_t x, int32_t y, int32_t z, uint32_t sky, uint32_t block) {
    column_t *const column = world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y));
    REQUIRE(column != nullptr);
    CHECK_EQ_U64(column_light_set(column, &rig->world.world->section_pool, column_local_of(x), column_local_of(y), z,
                                  section_light_pack(sky, block)),
                 COLUMN_STATUS_OK);
}

/* Real light, from nothing, for every column there is. */
static void ms_light_all(test_context_t *t, ms_rig_t *rig) {
    const world_map_t *const map = rig->world.map;
    for (uint32_t i = 0u; i < map->active_count; i++) {
        CHECK_EQ_U64(world_light_column_added(&rig->world.world->light, map->borrowed_active[i]),
                     WORLD_LIGHT_STATUS_OK);
    }
    CHECK_EQ_U64(world_light_rebuild(&rig->world.world->light), WORLD_LIGHT_STATUS_OK);
}

/* The weights the three had before plan 005 phase 3 turned them off: 100 each. The rules are tested on these, as the files now bring
 * nothing for them to work on. */
static const uint16_t MS_WEIGHTS[MOB_COUNT] = {
    [MOB_ID_CREEPER] = 100u, [MOB_ID_SKELETON] = 100u, [MOB_ID_ZOMBIE] = 100u};

static mob_spawn_view_t ms_view(float64_t sun_height) {
    return (mob_spawn_view_t){
        .position = {MS_PLAYER_X, MS_PLAYER_Y, 0.0f64}, .sun_height = sun_height, .weights = MS_WEIGHTS};
}

/* A dark place on stone, with a clear box over it, at the given spot. */
static void ms_dark_place(test_context_t *t, ms_rig_t *rig, int32_t x, int32_t y) {
    ms_put(t, rig, x, y, -1, BLOCK_ID_STONE);
    ms_light(t, rig, x, y, 0, 0u, 0u);
}

static mob_spawn_status_t ms_check_at(ms_rig_t *rig, mob_id_t mob, int32_t x, int32_t y, int32_t z,
                                      const mob_spawn_view_t *view, uint32_t roll) {
    const float64_t position[COLLISION_AXES] = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)z};
    return mob_spawn_check(rig->world.physics, rig->world.map, mob, position, view, roll);
}

static uint32_t ms_mobs_in(const entity_store_t *store) {
    uint32_t mobs = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        mobs += store->borrowed_kind[i] != MOB_ID_NONE && store->borrowed_kind[i] < MOB_COUNT ? 1u : 0u;
    }
    return mobs;
}

/* ---- The tables ---------------------------------------------------------------------------------------------------- */

TEST(the_tables_say_what_the_files_say) {
    CHECK_EQ_U64(MOB_COUNT, 10u);
    CHECK_STR_EQ(mob_defs[MOB_ID_NONE].name, "none");
    CHECK(mob_defs[MOB_ID_NONE].texture == nullptr);
    /* By name: the six animals of plan 005 phase 4 among the three turned off. */
    CHECK_EQ_U64(MOB_ID_CATTLE, 1u);
    CHECK_EQ_U64(MOB_ID_CHICKEN, 2u);
    CHECK_EQ_U64(MOB_ID_CREEPER, 3u);
    CHECK_EQ_U64(MOB_ID_GOAT, 4u);
    CHECK_EQ_U64(MOB_ID_MOUNTAIN_GOAT, 5u);
    CHECK_EQ_U64(MOB_ID_PIG, 6u);
    CHECK_EQ_U64(MOB_ID_SHEEP, 7u);
    CHECK_EQ_U64(MOB_ID_SKELETON, 8u);
    CHECK_EQ_U64(MOB_ID_ZOMBIE, 9u);

    const mob_def_t *const zombie = &mob_defs[MOB_ID_ZOMBIE];
    CHECK_STR_EQ(zombie->name, "zombie");
    CHECK_STR_EQ(zombie->texture, "mobs/zombie");
    CHECK(zombie->health == 20.0f32);
    CHECK(zombie->speed == 0.115f32);
    CHECK(zombie->half_width == 0.3f32);
    CHECK(zombie->height == 1.95f32);
    /* Turned off, plan 005 phase 3: all three, with the rest of each file kept. */
    CHECK_EQ_U64(zombie->weight, 0u);
    CHECK_EQ_U64(mob_defs[MOB_ID_SKELETON].weight, 0u);
    CHECK_EQ_U64(mob_defs[MOB_ID_CREEPER].weight, 0u);
    CHECK_EQ_U64(zombie->pack_min, 4u);
    CHECK_EQ_U64(zombie->pack_max, 4u);
    CHECK_EQ_U64(zombie->max_light, 7u);
    CHECK_EQ_U64(zombie->max_block_light, 0u);
    CHECK_EQ_U64(zombie->min_distance, 24u);
    CHECK_EQ_U64(zombie->despawn_near, 32u);
    CHECK_EQ_U64(zombie->despawn_far, 128u);
    CHECK_EQ_U64(zombie->despawn_idle_ticks, 600u);
    CHECK_EQ_U64(zombie->despawn_one_in, 800u);

    CHECK(mob_defs[MOB_ID_SKELETON].height == 1.99f32);
    CHECK(mob_defs[MOB_ID_CREEPER].height == 1.7f32);
    CHECK(mob_defs[MOB_ID_CREEPER].speed == 0.125f32);
}

TEST(a_mobs_rows_are_its_own) {
    CHECK_EQ_U64(mob_defs[MOB_ID_ZOMBIE].drop_count, 4u);
    const mob_drop_t *const flesh = &mob_drops[mob_defs[MOB_ID_ZOMBIE].first_drop];
    CHECK_EQ_U64(flesh->item, ITEM_ID_ROTTEN_FLESH);
    CHECK_EQ_U64(flesh->count_min, 0u);
    CHECK_EQ_U64(flesh->count_max, 2u);
    CHECK(flesh->chance == 1.0f32);
    CHECK_EQ_U64(flesh[1].item, ITEM_ID_IRON_INGOT);
    CHECK(flesh[1].chance == 0.0083f32);

    CHECK_EQ_U64(mob_defs[MOB_ID_CREEPER].drop_count, 1u);
    CHECK_EQ_U64(mob_drops[mob_defs[MOB_ID_CREEPER].first_drop].item, ITEM_ID_GUNPOWDER);
    CHECK_EQ_U64(mob_defs[MOB_ID_SKELETON].drop_count, 2u);
    CHECK_EQ_U64(mob_drops[mob_defs[MOB_ID_SKELETON].first_drop].item, ITEM_ID_BONE);
    CHECK_EQ_U64(mob_drops[mob_defs[MOB_ID_SKELETON].first_drop + 1u].item, ITEM_ID_ARROW);

    static const mob_id_t HOSTILE[] = {MOB_ID_CREEPER, MOB_ID_SKELETON, MOB_ID_ZOMBIE};
    for (uint32_t i = 0u; i < sizeof(HOSTILE) / sizeof(HOSTILE[0]); i++) {
        const mob_def_t *const def = &mob_defs[HOSTILE[i]];
        CHECK_EQ_U64(def->never_on_count, 1u);
        CHECK_EQ_U64(mob_never_on[def->first_never_on], BLOCK_ID_BEDROCK);
        CHECK_EQ_U64(def->part_count, 6u);
        CHECK_EQ_U64(def->mind, MOB_MIND_HOSTILE);
        CHECK_EQ_U64(def->first_sex, 0u);
    }
    /* Every body fits a rig, with its sex's boxes. */
    for (uint32_t mob = 1u; mob < MOB_COUNT; mob++) {
        uint32_t most = mob_defs[mob].part_count;
        for (uint32_t s = 0u; mob_defs[mob].first_sex != 0u && s < 2u; s++) {
            const uint32_t both = mob_defs[mob].part_count + mob_sexes[mob_defs[mob].first_sex + s].part_count;
            most = both > most ? both : most;
        }
        CHECK(most <= ENTITY_ANIMATION_PARTS_MAX);
    }
    CHECK_EQ_U64(ENTITY_ANIMATION_PARTS_MAX, 16u);
}

TEST(a_rig_is_what_its_mobs_file_says) {
    static const entity_animation_rig_id_t RIGS[] = {ENTITY_ANIMATION_RIG_ZOMBIE, ENTITY_ANIMATION_RIG_SKELETON,
                                                     ENTITY_ANIMATION_RIG_CREEPER};
    static const mob_id_t MOBS[] = {MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER};
    for (uint32_t i = 0u; i < 3u; i++) {
        const entity_animation_rig_t *const rig = entity_animation_rig(RIGS[i]);
        const mob_def_t *const def = &mob_defs[MOBS[i]];
        CHECK(rig->borrowed_parts == &mob_parts[def->first_part]);
        CHECK_EQ_U64(rig->part_count, def->part_count);
        CHECK_EQ_U64(rig->texture_width, def->texture_width);
        CHECK_EQ_U64(rig->texture_height, def->texture_height);
    }
    /* One of each kind of row, by hand. */
    const mob_part_t *const zombie = entity_animation_rig(ENTITY_ANIMATION_RIG_ZOMBIE)->borrowed_parts;
    CHECK_EQ_U64(zombie[0].flags, ENTITY_ANIMATION_PART_HEAD);
    CHECK_EQ_U64(zombie[2].flags, ENTITY_ANIMATION_PART_ARM);
    CHECK_EQ_U64(zombie[4].flags, 0u);
    CHECK(zombie[2].pivot[1] == 5.0f32);
    CHECK(zombie[3].offset[1] == -1.0f32);
    CHECK_NEAR(zombie[2].rest_pitch, 1.57079632f32, MS_EXACT);
    CHECK(zombie[4].walk_swing == -1.4f32);
    CHECK(zombie[4].pivot[1] == 1.9f32);
    CHECK_EQ_U64(zombie[2].texture_u, 40u);
    CHECK_EQ_U64(zombie[2].texture_v, 16u);
    const mob_part_t *const creeper = entity_animation_rig(ENTITY_ANIMATION_RIG_CREEPER)->borrowed_parts;
    CHECK(creeper[0].pivot[2] == 18.0f32);
    CHECK(creeper[5].size[2] == 6.0f32);
}

/* ---- The sky ------------------------------------------------------------------------------------------------------- */

/* The sun's height on the equator at the equinox at a time of day: cos(2 pi (f - 0.5)), what the curve read before plan 004 phase 02. */
static float64_t ms_equator_sun(float64_t day) {
    return cos(2.0f64 * 3.14159265358979f64 * (day - 0.5f64));
}

/* The curve as it was before plan 004 phase 02, by the time of day, to hold the new one to on the equator at the equinox. */
static uint32_t ms_old_darken(float64_t day) {
    const float64_t height = cos(2.0f64 * 3.14159265358979f64 * (day - 0.5f64)) * 2.0f64 + 0.5f64;
    const float64_t lit = height < 0.0f64 ? 0.0f64 : (height > 1.0f64 ? 1.0f64 : height);
    return (uint32_t)((1.0f64 - lit) * (float64_t)MOB_SPAWN_DARKEN_MAX);
}

TEST(the_sky_darkens_from_noon_to_midnight) {
    CHECK_EQ_U64(mob_spawn_sky_darken(MS_NOON), 0u);
    CHECK_EQ_U64(mob_spawn_sky_darken(MS_MIDNIGHT), MOB_SPAWN_DARKEN_MAX);
    /* The curve at the horizon, by hand: 2 * 0 + 0.5 lit, half of 11, down to 5. */
    CHECK_EQ_U64(mob_spawn_sky_darken(0.0f64), 5u);
    /* 08:24 on the equator, the sun 36 degrees up: full day. */
    CHECK_EQ_U64(mob_spawn_sky_darken(0.5878f64), 0u);
    /* Phase 02's worked examples: 6.5 degrees down is darken 7, 13.5 degrees down is 10, 14.5 or more is all 11. */
    CHECK_EQ_U64(mob_spawn_sky_darken(-0.1132f64), 7u);
    CHECK_EQ_U64(mob_spawn_sky_darken(-0.2334f64), 10u);
    CHECK_EQ_U64(mob_spawn_sky_darken(-0.25f64), MOB_SPAWN_DARKEN_MAX);
    /* Day and night each hold for a while: both ends are clamped. */
    CHECK_EQ_U64(mob_spawn_sky_darken(0.25f64), 0u);
    CHECK_EQ_U64(mob_spawn_sky_darken(-0.5f64), MOB_SPAWN_DARKEN_MAX);
    /* It gets no lighter as the sun goes down. */
    uint32_t last = 0u;
    for (uint32_t i = 0u; i <= 2000u; i++) {
        const uint32_t darken = mob_spawn_sky_darken(1.0f64 - (float64_t)i / 1000.0f64);
        CHECK(darken >= last);
        last = darken;
    }
    CHECK_EQ_U64(last, MOB_SPAWN_DARKEN_MAX);
}

TEST(on_the_equator_at_the_equinox_the_sky_darkens_as_it_did_at_every_time_of_day) {
    uint64_t differ = 0u;
    for (uint32_t i = 0u; i <= 100000u; i++) {
        const float64_t day = (float64_t)i / 100000.0f64;
        differ += mob_spawn_sky_darken(ms_equator_sun(day)) != ms_old_darken(day) ? 1u : 0u;
    }
    CHECK_EQ_U64(differ, 0u);
}

/* ---- Each rule at its edge --------------------------------------------------------------------------------------- */

TEST(distance_counts_in_three_dimensions_up_to_its_edges) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 1u));
    ms_dark_place(t, &rig, MS_PLACE_X, MS_PLACE_Y);
    const float64_t place[COLLISION_AXES] = {(float64_t)MS_PLACE_X + 0.5f64, (float64_t)MS_PLACE_Y + 0.5f64, 0.0f64};
    mob_spawn_view_t view = ms_view(MS_NOON);
    /* Exactly min_distance along x. */
    view.position[0] = place[0] - 24.0f64;
    view.position[1] = place[1];
    CHECK_EQ_U64(mob_spawn_check(rig.world.physics, rig.world.map, MOB_ID_ZOMBIE, place, &view, 7u), MOB_SPAWN_OK);
    view.position[0] = place[0] - 23.999f64;
    CHECK_EQ_U64(mob_spawn_check(rig.world.physics, rig.world.map, MOB_ID_ZOMBIE, place, &view, 7u),
                 MOB_SPAWN_TOO_NEAR);
    /* 3, 4, 12 by 2 make 26: near along x, not near over all. */
    view.position[0] = place[0] - 6.0f64;
    view.position[1] = place[1] - 8.0f64;
    view.position[2] = place[2] - 24.0f64;
    CHECK_EQ_U64(mob_spawn_check(rig.world.physics, rig.world.map, MOB_ID_ZOMBIE, place, &view, 7u), MOB_SPAWN_OK);
    /* Exactly far, and just past it, up above. */
    view.position[0] = place[0];
    view.position[1] = place[1];
    view.position[2] = place[2] + 128.0f64;
    CHECK_EQ_U64(mob_spawn_check(rig.world.physics, rig.world.map, MOB_ID_ZOMBIE, place, &view, 7u), MOB_SPAWN_OK);
    view.position[2] = place[2] + 128.001f64;
    CHECK_EQ_U64(mob_spawn_check(rig.world.physics, rig.world.map, MOB_ID_ZOMBIE, place, &view, 7u), MOB_SPAWN_TOO_FAR);
    ms_end(&rig);
}

TEST(a_column_not_loaded_will_not_do) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 1u));
    /* The fixture's edge: a chunk at distance 3 is there, and (4, 0) is not. */
    const mob_spawn_view_t view = {.position = {64.5f64, 8.5f64, 0.0f64}, .sun_height = MS_NOON};
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, 100, 8, 0, &view, 7u), MOB_SPAWN_NOT_LOADED);
    /* The last cell before it, whose body is all on this side. */
    ms_dark_place(t, &rig, 63, 8);
    const mob_spawn_view_t back = ms_view(MS_NOON);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, 63, 8, 0, &back, 7u), MOB_SPAWN_OK);
    /* Above and below all of gameplay there is nothing to stand on. */
    mob_spawn_view_t low = ms_view(MS_NOON);
    low.position[2] = (float64_t)COLUMN_Z_GAMEPLAY_MIN;
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, COLUMN_Z_GAMEPLAY_MIN, &low, 7u),
                 MOB_SPAWN_BAD_SURFACE);
    ms_end(&rig);
}

TEST(only_a_whole_opaque_block_is_a_floor) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 1u));
    const mob_spawn_view_t view = ms_view(MS_NOON);
    ms_dark_place(t, &rig, MS_PLACE_X, MS_PLACE_Y);
    static const uint16_t GOOD[] = {BLOCK_ID_STONE, BLOCK_ID_DIRT, BLOCK_ID_GRASS_BLOCK, BLOCK_ID_SAND,
                                    BLOCK_ID_OAK_PLANKS};
    for (uint32_t i = 0u; i < sizeof(GOOD) / sizeof(GOOD[0]); i++) {
        ms_put(t, &rig, MS_PLACE_X, MS_PLACE_Y, -1, GOOD[i]);
        CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &view, 7u), MOB_SPAWN_OK);
    }
    static const uint16_t BAD[] = {BLOCK_ID_AIR,      BLOCK_ID_GLASS, BLOCK_ID_OAK_LEAVES,
                                   BLOCK_ID_OAK_SLAB, BLOCK_ID_WATER, BLOCK_ID_LAVA,
                                   BLOCK_ID_BEDROCK,  BLOCK_ID_TORCH, BLOCK_ID_TORCH_WALL_EAST};
    for (uint32_t i = 0u; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        ms_put(t, &rig, MS_PLACE_X, MS_PLACE_Y, -1, BAD[i]);
        CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &view, 7u), MOB_SPAWN_BAD_SURFACE);
    }
    ms_end(&rig);
}

TEST(light_by_its_two_channels_and_the_time_of_day) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 1u));
    ms_dark_place(t, &rig, MS_PLACE_X, MS_PLACE_Y);
    const mob_spawn_view_t noon = ms_view(MS_NOON), midnight = ms_view(MS_MIDNIGHT);
    /* The dark passes on any roll. */
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &noon, 0u), MOB_SPAWN_OK);
    /* Any block light at all is too much, on any roll and at any hour. */
    ms_light(t, &rig, MS_PLACE_X, MS_PLACE_Y, 0, 0u, 1u);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &noon, 15u), MOB_SPAWN_TOO_BRIGHT);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &midnight, 15u), MOB_SPAWN_TOO_BRIGHT);
    /* Sky 7 by day passes only on the roll of 7; sky 8 by day on none of the rolls a tick can draw. */
    ms_light(t, &rig, MS_PLACE_X, MS_PLACE_Y, 0, 7u, 0u);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &noon, 7u), MOB_SPAWN_OK);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &noon, 6u), MOB_SPAWN_TOO_BRIGHT);
    ms_light(t, &rig, MS_PLACE_X, MS_PLACE_Y, 0, 8u, 0u);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &noon, 7u), MOB_SPAWN_TOO_BRIGHT);
    /* Open sky at midnight: 15 - 11 = 4. */
    ms_light(t, &rig, MS_PLACE_X, MS_PLACE_Y, 0, 15u, 0u);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &midnight, 4u), MOB_SPAWN_OK);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &midnight, 3u), MOB_SPAWN_TOO_BRIGHT);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &noon, 7u), MOB_SPAWN_TOO_BRIGHT);
    /* A sky darker than the darkening does not underflow. */
    ms_light(t, &rig, MS_PLACE_X, MS_PLACE_Y, 0, 3u, 0u);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, MS_PLACE_X, MS_PLACE_Y, 0, &midnight, 0u), MOB_SPAWN_OK);
    ms_end(&rig);
}

TEST(the_body_must_fit_and_be_dry) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 1u));
    const mob_spawn_view_t view = ms_view(MS_NOON);
    const int32_t x = MS_PLACE_X, y = MS_PLACE_Y;
    ms_dark_place(t, &rig, x, y);
    /* A zombie is 1.95 and a creeper 1.7: both fit under 2, and neither under 1. */
    ms_put(t, &rig, x, y, 2, BLOCK_ID_STONE);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, x, y, 0, &view, 7u), MOB_SPAWN_OK);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_SKELETON, x, y, 0, &view, 7u), MOB_SPAWN_OK);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_CREEPER, x, y, 0, &view, 7u), MOB_SPAWN_OK);
    ms_put(t, &rig, x, y, 2, BLOCK_ID_AIR);
    ms_put(t, &rig, x, y, 1, BLOCK_ID_STONE);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, x, y, 0, &view, 7u), MOB_SPAWN_NO_ROOM);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_CREEPER, x, y, 0, &view, 7u), MOB_SPAWN_NO_ROOM);
    ms_put(t, &rig, x, y, 1, BLOCK_ID_AIR);
    /* A wall beside it touches nothing: a body is narrower than its cell. */
    ms_put(t, &rig, x + 1, y, 0, BLOCK_ID_STONE);
    ms_put(t, &rig, x + 1, y, 1, BLOCK_ID_STONE);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, x, y, 0, &view, 7u), MOB_SPAWN_OK);
    /* A solid at the feet. */
    ms_put(t, &rig, x, y, 0, BLOCK_ID_STONE);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, x, y, 0, &view, 7u), MOB_SPAWN_NO_ROOM);
    ms_put(t, &rig, x, y, 0, BLOCK_ID_AIR);
    /* Water and lava at the feet and at the head. */
    static const uint16_t WET[] = {BLOCK_ID_WATER, BLOCK_ID_LAVA};
    for (uint32_t i = 0u; i < 2u; i++) {
        for (int32_t z = 0; z < 2; z++) {
            ms_put(t, &rig, x, y, z, WET[i]);
            CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, x, y, 0, &view, 7u), MOB_SPAWN_IN_FLUID);
            ms_put(t, &rig, x, y, z, BLOCK_ID_AIR);
        }
    }
    /* Above its head does not matter. */
    ms_put(t, &rig, x, y, 2, BLOCK_ID_WATER);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, x, y, 0, &view, 7u), MOB_SPAWN_OK);
    ms_put(t, &rig, x, y, 2, BLOCK_ID_AIR);
    /* A flower is nothing to a body. */
    ms_put(t, &rig, x, y, 0, BLOCK_ID_DANDELION);
    CHECK_EQ_U64(ms_check_at(&rig, MOB_ID_ZOMBIE, x, y, 0, &view, 7u), MOB_SPAWN_OK);
    ms_end(&rig);
}

TEST(a_body_is_its_mobs) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 1u));
    const float64_t place[COLLISION_AXES] = {1.5f64, 2.5f64, 3.0f64};
    const entity_handle_t handle = mob_spawn_body(rig.spawn, rig.store, MOB_ID_SKELETON, place, 0.5f32);
    REQUIRE(handle != ENTITY_HANDLE_NONE);
    uint32_t i = 0u;
    REQUIRE(entity_store_index_of(rig.store, handle, &i));
    CHECK_EQ_U64(rig.store->borrowed_kind[i], MOB_ID_SKELETON);
    CHECK(rig.store->borrowed_half_width[i] == 0.3f32);
    CHECK(rig.store->borrowed_height[i] == 1.99f32);
    CHECK(rig.store->borrowed_health[i] == 20.0f32);
    CHECK(rig.store->borrowed_yaw[i] == 0.5f32);
    CHECK(rig.store->borrowed_position[0][i] == 1.5f64);
    CHECK(rig.store->borrowed_position[1][i] == 2.5f64);
    CHECK(rig.store->borrowed_position[2][i] == 3.0f64);
    CHECK_EQ_U64(rig.spawn->spawned_count, 1u);
    ms_end(&rig);
}

/* ---- The tick ------------------------------------------------------------------------------------------------------ */

static void ms_ticks(ms_rig_t *rig, const mob_spawn_view_t *view, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        mob_spawn_tick(rig->spawn, rig->store, rig->world.physics, rig->world.map, view);
    }
}

/* Every body in the store, by the rules that hold for all time and any roll. */
static void ms_check_all_placed(test_context_t *t, const ms_rig_t *rig, const mob_spawn_view_t *view) {
    const entity_store_t *const store = rig->store;
    for (uint32_t i = 0u; i < store->count; i++) {
        const uint16_t kind = store->borrowed_kind[i];
        REQUIRE(kind != MOB_ID_NONE && kind < MOB_COUNT);
        const float64_t x = store->borrowed_position[0][i] - view->position[0];
        const float64_t y = store->borrowed_position[1][i] - view->position[1];
        const float64_t z = store->borrowed_position[2][i] - view->position[2];
        const float64_t reach = sqrt(x * x + y * y + z * z);
        CHECK(reach >= (float64_t)mob_defs[kind].min_distance);
        CHECK(reach <= (float64_t)mob_defs[kind].despawn_far);
        CHECK_EQ_I64((int64_t)store->borrowed_position[2][i], 0);
        CHECK(store->borrowed_height[i] == mob_defs[kind].height);
        CHECK(store->borrowed_health[i] == mob_defs[kind].health);
    }
}

TEST(an_open_field_stays_empty_by_day_and_fills_at_night) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 11u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    const mob_spawn_view_t noon = ms_view(MS_NOON), midnight = ms_view(MS_MIDNIGHT);
    ms_ticks(&rig, &noon, MS_NIGHT_TICKS);
    CHECK_EQ_U64(rig.store->count, 0u);
    CHECK_EQ_U64(rig.spawn->spawned_count, 0u);

    ms_ticks(&rig, &midnight, MS_NIGHT_TICKS);
    const uint32_t cap = mob_spawn_cap(rig.world.map->ready_count);
    REQUIRE(cap > 0u);
    CHECK_EQ_U64(rig.store->count, cap);
    CHECK(rig.spawn->spawned_count >= cap);
    ms_check_all_placed(t, &rig, &midnight);
    ms_end(&rig);
}

/* One tick at a time, each pack taken away again, so the cap never comes into it. A tick of more than one pack is rare, and is told
 * by holding more than a pack can or two kinds: those are counted, and not looked at for what a pack is. */
TEST(packs_are_of_one_kind_close_together_and_the_kinds_come_by_weight) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 16u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    const mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT);
    uint32_t seen[MOB_COUNT] = {0};
    uint32_t whole = 0u, packs = 0u, mixed = 0u, scattered = 0u;
    /* Four steps of at most 5 along an axis, each past a place that may not have passed. */
    const float64_t spread = 3.0f64 * (float64_t)MOB_SPAWN_PACK_STEP;
    for (uint32_t tick = 0u; tick < MS_PACK_TICKS_MAX && packs < MS_PACKS; tick++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &midnight);
        const uint32_t count = rig.store->count;
        if (count == 0u) {
            continue;
        }
        bool one_kind = true;
        for (uint32_t i = 1u; i < count; i++) {
            one_kind = one_kind && rig.store->borrowed_kind[i] == rig.store->borrowed_kind[0];
        }
        mixed += one_kind ? 0u : 1u;
        if (one_kind && count <= mob_defs[MOB_ID_ZOMBIE].pack_max) {
            packs++;
            whole += count == mob_defs[MOB_ID_ZOMBIE].pack_max ? 1u : 0u;
            seen[rig.store->borrowed_kind[0]]++;
            bool close = true;
            for (uint32_t i = 1u; i < count; i++) {
                close =
                    close && fabs(rig.store->borrowed_position[0][i] - rig.store->borrowed_position[0][0]) <= spread;
                close =
                    close && fabs(rig.store->borrowed_position[1][i] - rig.store->borrowed_position[1][0]) <= spread;
                close = close && rig.store->borrowed_position[2][i] == rig.store->borrowed_position[2][0];
            }
            scattered += close ? 0u : 1u;
        }
        while (rig.store->count != 0u) {
            REQUIRE(entity_store_remove(rig.store, rig.store->borrowed_handle[0]));
        }
    }
    REQUIRE(packs == MS_PACKS);
    CHECK(whole > 0u);
    /* If a pack were of several kinds or far apart these would be most ticks. Two small packs of one kind in a tick is what is left. */
    CHECK(mixed * 5u < packs);
    CHECK(scattered * 5u < packs);
    CHECK_EQ_U64(seen[MOB_ID_NONE], 0u);
    /* Equal weights: 100 each, give or take 8 at one sigma. */
    for (uint32_t mob = 1u; mob < MOB_COUNT; mob++) {
        if (MS_WEIGHTS[mob] != 0u) {
            CHECK(seen[mob] >= 60u && seen[mob] <= 140u);
        } else {
            CHECK_EQ_U64(seen[mob], 0u); /* an animal, which has no weight here and comes by its own rules */
        }
    }
    ms_end(&rig);
}

TEST(a_roofed_cave_fills_by_day_except_by_a_torch) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 12u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_fill_loaded(t, &rig, 3, 3, BLOCK_ID_STONE);
    /* A lava pool would do; a torch is what a player brings. Its light reaches 14 cells on the floor. */
    const int32_t torch_x = 40, torch_y = 8;
    ms_put(t, &rig, torch_x, torch_y, 0, BLOCK_ID_TORCH);
    ms_light_all(t, &rig);
    uint8_t packed = 0u;
    REQUIRE(world_map_light_get(rig.world.map, torch_x + 1, torch_y, 0, &packed) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(section_light_block(packed), block_defs[BLOCK_ID_TORCH].light - 1u);
    /* Sky comes in under the roof at the edge of what is loaded? Only columns told of count, so here nothing does. */
    CHECK_EQ_U64(section_light_sky(packed), 0u);

    const mob_spawn_view_t noon = ms_view(MS_NOON);
    ms_ticks(&rig, &noon, MS_NIGHT_TICKS);
    CHECK_EQ_U64(rig.store->count, mob_spawn_cap(rig.world.map->ready_count));
    ms_check_all_placed(t, &rig, &noon);
    /* Out of its reach by steps, as light goes round nothing here. */
    const int32_t reach = (int32_t)block_defs[BLOCK_ID_TORCH].light;
    for (uint32_t i = 0u; i < rig.store->count; i++) {
        const int32_t dx = abs((int32_t)floor(rig.store->borrowed_position[0][i]) - torch_x);
        const int32_t dy = abs((int32_t)floor(rig.store->borrowed_position[1][i]) - torch_y);
        CHECK(dx + dy >= reach);
    }
    ms_end(&rig);
}

TEST(a_water_covered_world_and_a_world_of_glass_bring_nothing) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 13u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_GLASS);
    ms_light_all(t, &rig);
    const mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT);
    ms_ticks(&rig, &midnight, 500u);
    CHECK_EQ_U64(rig.spawn->spawned_count, 0u);
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_fill_loaded(t, &rig, 0, 2, BLOCK_ID_WATER);
    ms_light_all(t, &rig);
    ms_ticks(&rig, &midnight, 500u);
    CHECK_EQ_U64(rig.spawn->spawned_count, 0u);
    ms_end(&rig);
}

TEST(the_cap_goes_by_columns_and_stops_a_pack_part_way) {
    CHECK_EQ_U64(mob_spawn_cap(0u), 0u);
    CHECK_EQ_U64(mob_spawn_cap(1u), 0u);
    CHECK_EQ_U64(mob_spawn_cap(5u), 1u);
    CHECK_EQ_U64(mob_spawn_cap(MOB_SPAWN_CAP_COLUMNS / 2u), MOB_SPAWN_CAP / 2u - 1u);
    CHECK_EQ_U64(mob_spawn_cap(MOB_SPAWN_CAP_COLUMNS), MOB_SPAWN_CAP);
    CHECK_EQ_U64(mob_spawn_cap(100000u), MOB_SPAWN_CAP);

    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 14u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    const mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT);
    const uint32_t cap = mob_spawn_cap(rig.world.map->ready_count);
    /* Packs are 4, so reaching it exactly on every tick means a pack was cut short somewhere unless cap is a multiple. */
    for (uint32_t i = 0u; i < MS_NIGHT_TICKS; i++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &midnight);
        CHECK(ms_mobs_in(rig.store) <= cap);
    }
    CHECK_EQ_U64(ms_mobs_in(rig.store), cap);
    ms_end(&rig);
}

TEST(a_full_store_refuses_and_nothing_breaks) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 15u));
    /* Full of what is not a mob, so that the cap does not stop first, and those are left alone. */
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        const entity_store_spawn_t body = {.position = {8.5f64, 8.5f64, 40.0f64},
                                           .half_width = 0.3f32,
                                           .height = 1.0f32,
                                           .health = 1.0f32,
                                           .kind = 0u};
        REQUIRE(entity_store_spawn(rig.store, &body) != ENTITY_HANDLE_NONE);
    }
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    const mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT);
    ms_ticks(&rig, &midnight, 500u);
    CHECK_EQ_U64(rig.store->count, ENTITY_STORE_CAPACITY);
    CHECK_EQ_U64(rig.spawn->spawned_count, 0u);
    CHECK(rig.spawn->refused_count > 0u);
    CHECK_EQ_U64(rig.spawn->despawned_count, 0u);
    ms_end(&rig);
}

/* All of a world that a run of ticks leaves in the store and the counts, for comparing. */
static void ms_run_copy(test_context_t *t, uint64_t seed, float64_t *out_positions, uint64_t *out_spawned,
                        uint32_t *out_count) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, seed));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    const mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT);
    ms_ticks(&rig, &midnight, 300u);
    *out_count = rig.store->count;
    *out_spawned = rig.spawn->spawned_count;
    for (uint32_t i = 0u; i < rig.store->count; i++) {
        out_positions[i * 3u + 0u] = rig.store->borrowed_position[0][i];
        out_positions[i * 3u + 1u] = rig.store->borrowed_position[1][i];
        out_positions[i * 3u + 2u] = (float64_t)rig.store->borrowed_kind[i];
    }
    ms_end(&rig);
}

TEST(the_same_calls_give_the_same_bytes_and_another_seed_others) {
    const uint64_t bytes = (uint64_t)MOB_SPAWN_CAP * 3u * sizeof(float64_t);
    float64_t *const a = test_alloc(t, bytes), *const b = test_alloc(t, bytes), *const c = test_alloc(t, bytes);
    uint64_t spawned_a = 0u, spawned_b = 0u, spawned_c = 0u;
    uint32_t count_a = 0u, count_b = 0u, count_c = 0u;
    ms_run_copy(t, 21u, a, &spawned_a, &count_a);
    ms_run_copy(t, 21u, b, &spawned_b, &count_b);
    ms_run_copy(t, 22u, c, &spawned_c, &count_c);
    REQUIRE(count_a > 0u);
    CHECK_EQ_U64(count_a, count_b);
    CHECK_EQ_U64(spawned_a, spawned_b);
    CHECK_MEM_EQ(a, b, bytes);
    CHECK(count_c > 0u);
    CHECK(memcmp(a, c, bytes) != 0);
}

/* ---- Turned off (plan 005 phase 3) --------------------------------------------------------------------------------- */

/* How many zombies, skeletons and creepers the store holds. */
static uint32_t ms_hostile_in(const entity_store_t *store) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        const uint16_t kind = store->borrowed_kind[i];
        count += kind == MOB_ID_ZOMBIE || kind == MOB_ID_SKELETON || kind == MOB_ID_CREEPER ? 1u : 0u;
    }
    return count;
}

/* The weights in config/mobs/, as the game passes them: a whole night, then a whole day, in an open field and a roofed cave that the
 * same ticks with the old weights fill to the cap, brings no zombie, skeleton or creeper. Goats, which stand on stone, may come by
 * day: plan 005 phase 4's animals have weights. */
TEST(a_long_night_in_the_open_brings_no_zombie_skeleton_or_creeper) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 11u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT), noon = ms_view(MS_NOON);
    midnight.weights = nullptr;
    noon.weights = nullptr;
    /* Six times what fills the field with the old weights (an_open_field_stays_empty_by_day_and_fills_at_night). */
    ms_ticks(&rig, &midnight, 6u * MS_NIGHT_TICKS);
    CHECK_EQ_U64(rig.store->count, 0u); /* no animal comes by night either */
    ms_ticks(&rig, &noon, MS_NIGHT_TICKS);
    CHECK_EQ_U64(ms_hostile_in(rig.store), 0u);
    CHECK_EQ_U64(rig.spawn->refused_count, 0u);
    /* The same field with the old weights fills, so it is the weights and not the field. */
    const mob_spawn_view_t old = ms_view(MS_MIDNIGHT);
    ms_ticks(&rig, &old, MS_NIGHT_TICKS);
    CHECK_EQ_U64(ms_hostile_in(rig.store), mob_spawn_cap(rig.world.map->ready_count));
    ms_end(&rig);
}

/* What a save made while they came keeps goes as the world opens: every mob whose weight is 0, and nothing else. */
TEST(retiring_takes_every_turned_off_mob_and_leaves_the_rest) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 32u));
    static const mob_id_t MOBS[] = {MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER, MOB_ID_ZOMBIE, MOB_ID_ZOMBIE};
    const entity_store_spawn_t other = {
        .position = {8.5f64, 8.5f64, 40.0f64}, .half_width = 0.3f32, .height = 1.0f32, .health = 1.0f32, .kind = 0u};
    const entity_handle_t first = entity_store_spawn(rig.store, &other);
    REQUIRE(first != ENTITY_HANDLE_NONE);
    for (uint32_t i = 0u; i < sizeof(MOBS) / sizeof(MOBS[0]); i++) {
        const float64_t place[COLLISION_AXES] = {10.5f64 + (float64_t)i, 8.5f64, 0.0f64};
        REQUIRE(mob_spawn_body(rig.spawn, rig.store, MOBS[i], place, 0.0f32) != ENTITY_HANDLE_NONE);
    }
    const entity_handle_t last = entity_store_spawn(rig.store, &other);
    REQUIRE(last != ENTITY_HANDLE_NONE);
    const uint64_t despawned = rig.spawn->despawned_count;
    uint32_t removed[MOB_COUNT] = {0};
    CHECK_EQ_U64(mob_spawn_retire(rig.store, removed), 5u);
    CHECK_EQ_U64(removed[MOB_ID_NONE], 0u);
    CHECK_EQ_U64(removed[MOB_ID_ZOMBIE], 3u);
    CHECK_EQ_U64(removed[MOB_ID_SKELETON], 1u);
    CHECK_EQ_U64(removed[MOB_ID_CREEPER], 1u);
    CHECK_EQ_U64(rig.store->count, 2u);
    CHECK_EQ_U64(ms_mobs_in(rig.store), 0u);
    uint32_t index = 0u;
    CHECK(entity_store_index_of(rig.store, first, &index));
    CHECK(entity_store_index_of(rig.store, last, &index));
    CHECK_EQ_U64(rig.spawn->despawned_count, despawned);
    /* Again: nothing left to take. */
    CHECK_EQ_U64(mob_spawn_retire(rig.store, removed), 0u);
    CHECK_EQ_U64(removed[MOB_ID_ZOMBIE], 3u);
    ms_end(&rig);
}

/* ---- Despawning ---------------------------------------------------------------------------------------------------- */

static entity_handle_t ms_body_at(ms_rig_t *rig, mob_id_t mob, float64_t x, float64_t y, float64_t z) {
    const float64_t place[COLLISION_AXES] = {x, y, z};
    return mob_spawn_body(rig->spawn, rig->store, mob, place, 0.0f32);
}

/* An empty world has no floor, so in these only the despawn rules do anything. */
TEST(past_far_a_mob_goes_at_once_and_the_rest_keep_their_handles) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 31u));
    const mob_spawn_view_t noon = ms_view(MS_NOON);
    const entity_handle_t near = ms_body_at(&rig, MOB_ID_ZOMBIE, MS_PLAYER_X, MS_PLAYER_Y, 20.0f64);
    const entity_handle_t edge = ms_body_at(&rig, MOB_ID_ZOMBIE, MS_PLAYER_X + 128.0f64, MS_PLAYER_Y, 0.0f64);
    const entity_handle_t past = ms_body_at(&rig, MOB_ID_CREEPER, MS_PLAYER_X + 128.5f64, MS_PLAYER_Y, 0.0f64);
    const entity_handle_t up = ms_body_at(&rig, MOB_ID_SKELETON, MS_PLAYER_X, MS_PLAYER_Y, 129.0f64);
    const entity_handle_t kept = ms_body_at(&rig, MOB_ID_CREEPER, MS_PLAYER_X + 40.0f64, MS_PLAYER_Y, 0.0f64);
    mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &noon);
    uint32_t i = 0u;
    CHECK(entity_store_index_of(rig.store, near, &i));
    CHECK(entity_store_index_of(rig.store, edge, &i));
    CHECK(!entity_store_index_of(rig.store, past, &i));
    CHECK(!entity_store_index_of(rig.store, up, &i));
    CHECK(entity_store_index_of(rig.store, kept, &i));
    CHECK_EQ_U64(rig.store->borrowed_kind[i], MOB_ID_CREEPER);
    CHECK_EQ_U64(rig.spawn->despawned_count, 2u);
    CHECK_EQ_U64(rig.store->count, 3u);
    ms_end(&rig);
}

TEST(within_near_a_mob_stays_and_its_idle_count_starts_over) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 32u));
    const mob_def_t *const def = &mob_defs[MOB_ID_ZOMBIE];
    mob_spawn_view_t view = ms_view(MS_NOON);
    const entity_handle_t handle = ms_body_at(&rig, MOB_ID_ZOMBIE, MS_PLAYER_X + 32.0f64, MS_PLAYER_Y, 0.0f64);
    const uint32_t slot = entity_handle_slot(handle);
    /* Exactly near counts as near, far beyond what would ever have sent it. */
    for (uint32_t i = 0u; i < def->despawn_idle_ticks + def->despawn_one_in * 30u; i++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &view);
    }
    uint32_t index = 0u;
    CHECK(entity_store_index_of(rig.store, handle, &index));
    CHECK_EQ_U64(rig.spawn->borrowed_idle[slot], 0u);
    /* Just past it the count goes up, by one a tick; back again and it starts over. */
    view.position[0] -= 0.5f64;
    for (uint32_t i = 0u; i < 10u; i++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &view);
    }
    CHECK_EQ_U64(rig.spawn->borrowed_idle[slot], 10u);
    view.position[0] += 0.5f64;
    mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &view);
    CHECK_EQ_U64(rig.spawn->borrowed_idle[slot], 0u);
    ms_end(&rig);
}

TEST(between_they_wait_their_time_then_go_about_one_in_so_many) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 33u));
    const mob_def_t *const def = &mob_defs[MOB_ID_ZOMBIE];
    const mob_spawn_view_t noon = ms_view(MS_NOON);
    entity_handle_t *const handles = test_alloc(t, MS_DESPAWN_BODIES * sizeof(entity_handle_t));
    for (uint32_t i = 0u; i < MS_DESPAWN_BODIES; i++) {
        handles[i] =
            ms_body_at(&rig, MOB_ID_ZOMBIE, MS_PLAYER_X + 40.0f64 + (float64_t)i * 0.25f64, MS_PLAYER_Y, 0.0f64);
    }
    /* idle_ticks of counting, in which nobody may go. */
    for (uint32_t i = 0u; i < def->despawn_idle_ticks; i++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &noon);
    }
    CHECK_EQ_U64(rig.store->count, MS_DESPAWN_BODIES);
    CHECK_EQ_U64(rig.spawn->despawned_count, 0u);
    /* Then each has one in 800 per tick: over 800 ticks, about 1 - 1/e of them go. 200 bodies give 126 +- 7 at one sigma. */
    for (uint32_t i = 0u; i < def->despawn_one_in; i++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &noon);
    }
    const uint64_t gone = rig.spawn->despawned_count;
    CHECK_EQ_U64(gone + rig.store->count, MS_DESPAWN_BODIES);
    CHECK(gone >= 91u && gone <= 161u);
    /* And each handle still answers for what it was. */
    uint32_t found = 0u;
    for (uint32_t i = 0u; i < MS_DESPAWN_BODIES; i++) {
        uint32_t index = 0u;
        if (entity_store_index_of(rig.store, handles[i], &index)) {
            found++;
            const float64_t expect = MS_PLAYER_X + 40.0f64 + (float64_t)i * 0.25f64;
            CHECK(rig.store->borrowed_position[0][index] == expect);
        }
    }
    CHECK_EQ_U64(found, rig.store->count);
    ms_end(&rig);
}

TEST(a_body_waiting_for_its_column_neither_counts_nor_rolls) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 34u));
    const mob_spawn_view_t noon = ms_view(MS_NOON);
    const float64_t x = 100.5f64; /* over a column not loaded, and within far */
    const entity_handle_t handle = ms_body_at(&rig, MOB_ID_ZOMBIE, x, MS_PLAYER_Y, 0.0f64);
    entity_store_tick(rig.store, rig.world.physics, rig.world.map);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(rig.store, handle, &index));
    REQUIRE((rig.store->borrowed_flags[index] & ENTITY_STORE_FLAG_WAITING) != 0u);
    for (uint32_t i = 0u; i < 20000u; i++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &noon);
    }
    CHECK(entity_store_index_of(rig.store, handle, &index));
    CHECK_EQ_U64(rig.spawn->borrowed_idle[entity_handle_slot(handle)], 0u);
    /* Too far still takes it, since nothing is saved. */
    mob_spawn_view_t away = noon;
    away.position[0] = x - 129.0f64;
    mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &away);
    CHECK(!entity_store_index_of(rig.store, handle, &index));
    ms_end(&rig);
}

TEST(other_kinds_are_not_touched_or_counted) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 35u));
    const mob_spawn_view_t noon = ms_view(MS_NOON);
    const uint16_t kinds[] = {(uint16_t)MOB_ID_NONE, (uint16_t)MOB_COUNT, 999u};
    entity_handle_t handles[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        const entity_store_spawn_t body = {.position = {MS_PLAYER_X + 500.0f64, MS_PLAYER_Y, 0.0f64},
                                           .half_width = 0.3f32,
                                           .height = 1.0f32,
                                           .health = 1.0f32,
                                           .kind = kinds[i]};
        handles[i] = entity_store_spawn(rig.store, &body);
    }
    ms_ticks(&rig, &noon, 2000u);
    for (uint32_t i = 0u; i < 3u; i++) {
        uint32_t index = 0u;
        CHECK(entity_store_index_of(rig.store, handles[i], &index));
    }
    CHECK_EQ_U64(rig.spawn->despawned_count, 0u);
    CHECK_EQ_U64(ms_mobs_in(rig.store), 0u);
    ms_end(&rig);
}

TEST(a_slot_used_again_starts_over) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 36u));
    const mob_spawn_view_t noon = ms_view(MS_NOON);
    const entity_handle_t first = ms_body_at(&rig, MOB_ID_ZOMBIE, MS_PLAYER_X + 40.0f64, MS_PLAYER_Y, 0.0f64);
    ms_ticks(&rig, &noon, 50u);
    const uint32_t slot = entity_handle_slot(first);
    CHECK_EQ_U64(rig.spawn->borrowed_idle[slot], 50u);
    /* Round the ring of free slots until this one comes back. */
    REQUIRE(entity_store_remove(rig.store, first));
    entity_handle_t again = ENTITY_HANDLE_NONE;
    for (uint32_t i = 0u; i <= ENTITY_STORE_CAPACITY; i++) {
        again = ms_body_at(&rig, MOB_ID_ZOMBIE, MS_PLAYER_X + 40.0f64, MS_PLAYER_Y, 0.0f64);
        if (entity_handle_slot(again) == slot) {
            break;
        }
        REQUIRE(entity_store_remove(rig.store, again));
    }
    REQUIRE(entity_handle_slot(again) == slot);
    CHECK(again != first);
    CHECK_EQ_U64(rig.spawn->borrowed_idle[slot], 0u);
    ms_end(&rig);
}

/* ---- The Owner and the cost ---------------------------------------------------------------------------------------- */

TEST(a_tick_takes_no_heap_and_no_arena_and_little_time) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 41u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    const mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT);
    heap_counters_t before, after;
    heap_counters_read(&before);
    const uint64_t used = rig.arena->used, allocs = rig.arena->alloc_count;
    const int64_t started = time_now_ns();
    ms_ticks(&rig, &midnight, MS_SPEED_TICKS);
    const int64_t took = time_now_ns() - started;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(rig.arena->used, used);
    CHECK_EQ_U64(rig.arena->alloc_count, allocs);
    /* The phase's budget is 5 ms a tick for everything, at the debug flags and with the sanitizers too: this is a floor under it. */
    CHECK(took / (int64_t)MS_SPEED_TICKS < 1000000);
    ms_end(&rig);
}

TEST(starting_again_is_empty_and_does_what_it_did) {
    ms_rig_t rig;
    REQUIRE(ms_begin(t, &rig, 42u));
    ms_fill_loaded(t, &rig, -1, -1, BLOCK_ID_STONE);
    ms_light_all(t, &rig);
    const mob_spawn_view_t midnight = ms_view(MS_MIDNIGHT);
    ms_ticks(&rig, &midnight, 200u);
    const uint64_t spawned = rig.spawn->spawned_count;
    REQUIRE(spawned > 0u);

    entity_open(rig.entity, 42u);
    arena_reset(rig.arena);
    mob_spawn_start(rig.spawn, rig.arena, 42u);
    CHECK_EQ_U64(rig.store->count, 0u);
    CHECK_EQ_U64(rig.spawn->spawned_count, 0u);
    CHECK_EQ_U64(rig.spawn->despawned_count, 0u);
    CHECK(rig.arena->used <= MOB_SPAWN_BYTES);
    ms_ticks(&rig, &midnight, 200u);
    CHECK_EQ_U64(rig.spawn->spawned_count, spawned);
    ms_end(&rig);
}

TEST_MAIN()
