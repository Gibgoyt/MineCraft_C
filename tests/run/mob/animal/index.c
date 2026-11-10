/* ctest run_mob_animal. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/animation/index.h"
#include "entity/index.h"
#include "heap/index.h"
#include "mob/index.h"
#include "mob/spawn/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MA_NOON 1.0f64
#define MA_MIDNIGHT (-1.0f64)
#define MA_EXACT 1.0e-6f32

/* Where the player stands, on a floor whose top is at z = 0. */
#define MA_PLAYER_X 8.5f64
#define MA_PLAYER_Y 8.5f64

enum : uint32_t {
    MA_SEX_BODIES = 4000u,
    MA_FILL_TICKS = 2000u,
    MA_RARE_TICKS = 8000u,
    MA_FLEE_LOOK = 60u,
    MA_DEATHS = 400u,
    MA_HERD_TICKS = 6000u,
    MA_RADIUS = 9u,    /* chunks: about 250 columns, so the animals' cap is 8 */
    MA_COLUMNS = 512u, /* room for them all */
};
enum : int32_t {
    /* Every block of every chunk within MA_RADIUS: a floor goes where a column is loaded. */
    MA_FLOOR_MIN = -(int32_t)MA_RADIUS * 16,
    MA_FLOOR_MAX = ((int32_t)MA_RADIUS + 1) * 16 - 1,
};

typedef struct ma_rig_t {
    test_world_t world;       /* 0 */
    entity_t *entity;         /* 24 */
    mob_t *mob;               /* 32 */
    entity_store_t *store;    /* 40 */
    mob_spawn_t *spawn;       /* 48 */
    arena_t *arena;           /* 56: what a game's arena_world is to the spawn */
    item_drop_store_t *drops; /* 64 */
} ma_rig_t;
static_assert(sizeof(ma_rig_t) == 72);

/* test_world_begin's world, wider: the animals' cap is 10 for 289 columns, and the fixture's 37 or so would hold one animal, which is
 * no herd. */
static bool ma_world_begin(test_context_t *t, test_world_t *w) {
    w->world = test_world_aligned(t, sizeof(world_t));
    w->physics = test_world_aligned(t, sizeof(physics_t));
    if (!CHECK(world_init(w->world, (world_limits_t){512ull << 20, MA_COLUMNS})) ||
        !CHECK(world_open(w->world, MA_RADIUS, 0, 0)) || !CHECK(physics_init(w->physics))) {
        return false;
    }
    w->map = &w->world->map;
    test_world_stream_all(t, w);
    return true;
}

static bool ma_begin(test_context_t *t, ma_rig_t *rig, uint64_t seed) {
    if (!ma_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics)) || !CHECK(mob_init(rig->mob, nullptr))) {
        return false;
    }
    entity_open(rig->entity, seed);
    mob_open(rig->mob, rig->world.world, seed);
    rig->store = &rig->entity->store;
    rig->drops = &rig->entity->item_drops;
    rig->spawn = test_alloc(t, sizeof(mob_spawn_t));
    rig->arena = test_world_aligned(t, sizeof(arena_t));
    arena_init(rig->arena, "ma_spawn", test_alloc(t, MOB_SPAWN_BYTES), MOB_SPAWN_BYTES);
    mob_spawn_start(rig->spawn, rig->arena, seed);
    return true;
}

static void ma_end(ma_rig_t *rig) {
    arena_deinit(rig->arena);
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

/* A floor of this block under z = 0 in every loaded column, with real light over it. */
static void ma_floor(test_context_t *t, ma_rig_t *rig, uint16_t block) {
    for (int32_t x = MA_FLOOR_MIN; x <= MA_FLOOR_MAX; x++) {
        for (int32_t y = MA_FLOOR_MIN; y <= MA_FLOOR_MAX; y++) {
            column_t *const column = world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y));
            if (column != nullptr) {
                CHECK_EQ_U64(column_block_set(column, &rig->world.world->section_pool, column_local_of(x),
                                              column_local_of(y), -1, block),
                             COLUMN_STATUS_OK);
            }
        }
    }
    const world_map_t *const map = rig->world.map;
    for (uint32_t i = 0u; i < map->active_count; i++) {
        CHECK_EQ_U64(world_light_column_added(&rig->world.world->light, map->borrowed_active[i]),
                     WORLD_LIGHT_STATUS_OK);
    }
    CHECK_EQ_U64(world_light_rebuild(&rig->world.world->light), WORLD_LIGHT_STATUS_OK);
}

/* The pig stands in for the animals of stage 3, and its file has weight 0, so these tests give it one. */
static const uint16_t MA_PIGS[MOB_COUNT] = {[MOB_ID_PIG] = 100u};
static const uint16_t MA_PIGS_AND_ZOMBIES[MOB_COUNT] = {[MOB_ID_PIG] = 100u, [MOB_ID_ZOMBIE] = 100u};

static mob_spawn_view_t ma_view(float64_t sun_height, bool first_day) {
    return (mob_spawn_view_t){.position = {MA_PLAYER_X, MA_PLAYER_Y, 0.0f64},
                              .sun_height = sun_height,
                              .weights = MA_PIGS,
                              .first_day = (uint8_t)(first_day ? 1u : 0u)};
}

static mob_spawn_status_t ma_check(ma_rig_t *rig, mob_id_t mob, float64_t x, float64_t y, const mob_spawn_view_t *view,
                                   uint32_t roll) {
    const float64_t position[COLLISION_AXES] = {x, y, 0.0f64};
    return mob_spawn_check(rig->world.physics, rig->world.map, mob, position, view, roll);
}

static void ma_ticks(ma_rig_t *rig, const mob_spawn_view_t *view, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        mob_spawn_tick(rig->spawn, rig->store, rig->world.physics, rig->world.map, view);
    }
}

static uint32_t ma_count_kind(const entity_store_t *store, mob_id_t mob) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        count += store->borrowed_kind[i] == (uint16_t)mob ? 1u : 0u;
    }
    return count;
}

static uint32_t ma_index(test_context_t *t, const ma_rig_t *rig, entity_handle_t handle) {
    uint32_t index = 0u;
    CHECK(entity_store_index_of(rig->store, handle, &index));
    return index;
}

static entity_handle_t ma_pig(ma_rig_t *rig, float64_t x, float64_t y, uint8_t sex) {
    const float64_t place[COLLISION_AXES] = {x, y, 0.0f64};
    return mob_spawn_body_as(rig->spawn, rig->store, MOB_ID_PIG, place, 0.0f32, sex);
}

/* ---- The tables ---------------------------------------------------------------------------------------------------- */

TEST(the_pig_stand_in_is_a_passive_animal_with_a_male_and_a_female) {
    const mob_def_t *const pig = &mob_defs[MOB_ID_PIG];
    CHECK_EQ_U64(pig->mind, MOB_MIND_PASSIVE);
    CHECK_EQ_U64(pig->weight, 0u); /* a stand-in comes by itself only once stage 3 makes it a pig */
    CHECK_EQ_U64(pig->despawn_far, 0u);
    CHECK_EQ_U64(pig->min_light, 9u);
    CHECK_EQ_U64(pig->max_light, 15u);
    CHECK_EQ_U64(pig->on_count, 1u);
    CHECK_EQ_U64(mob_on[pig->first_on], BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(pig->biome_count, 7u);
    CHECK_EQ_U64(mob_biomes[pig->first_biome], BIOME_ID_PLAINS);
    CHECK_EQ_I64(pig->min_height, -1250);
    CHECK_EQ_I64(pig->max_height, 1250);
    REQUIRE(pig->first_sex != 0u);
    const mob_sex_t *const boar = &mob_sexes[pig->first_sex];
    const mob_sex_t *const sow = &mob_sexes[pig->first_sex + 1u];
    CHECK_STR_EQ(boar->name, "boar");
    CHECK_STR_EQ(sow->name, "sow");
    CHECK_EQ_U64(boar->sex, MOB_SEX_MALE);
    CHECK_EQ_U64(sow->sex, MOB_SEX_FEMALE);
    CHECK(boar->scale == 1.2f32);
    CHECK(sow->scale == 1.0f32);
    /* The boar's tusks come right after the pig's own six boxes, and turn with its head. */
    CHECK_EQ_U64(boar->part_count, 2u);
    CHECK_EQ_U64(boar->first_part, pig->first_part + pig->part_count);
    CHECK_EQ_U64(mob_parts[boar->first_part].flags, MOB_PART_MOVES_HEAD);
    CHECK_EQ_U64(sow->part_count, 0u);
    CHECK_EQ_U64(boar->drop_count, 1u);
    CHECK_EQ_U64(mob_drops[boar->first_drop].item, ITEM_ID_LEATHER);
    CHECK_EQ_U64(boar->first_drop, pig->first_drop + pig->drop_count);
    CHECK_EQ_U64(boar->yield_count, 0u);
    CHECK_EQ_U64(sow->yield_count, 0u);
    /* Row 0 of each new array belongs to none. */
    CHECK_STR_EQ(mob_sexes[0].name, "none");
    CHECK_EQ_U64(mob_yields[0].every, 0u);
    CHECK_EQ_U64(MOB_YIELDS_MAX, 4u);
}

TEST(every_sex_of_every_mob_still_fits_what_physics_takes) {
    for (uint32_t mob = 1u; mob < MOB_COUNT; mob++) {
        const mob_def_t *const def = &mob_defs[mob];
        for (uint32_t s = 0u; def->first_sex != 0u && s < 2u; s++) {
            const float64_t scale = (float64_t)mob_sexes[def->first_sex + s].scale;
            CHECK((float64_t)def->half_width * scale * 2.0f64 <= PHYSICS_BOX_SIZE_MAX);
            CHECK((float64_t)def->height * scale <= PHYSICS_BOX_SIZE_MAX);
        }
    }
}

/* ---- A body -------------------------------------------------------------------------------------------------------- */

TEST(an_animal_is_male_or_female_half_and_half_and_a_male_is_bigger) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 3u));
    const float64_t place[COLLISION_AXES] = {1.5f64, 2.5f64, 0.0f64};
    uint32_t males = 0u;
    for (uint32_t i = 0u; i < MA_SEX_BODIES; i++) {
        const entity_handle_t handle = mob_spawn_body(rig.spawn, rig.store, MOB_ID_PIG, place, 0.0f32);
        REQUIRE(handle != ENTITY_HANDLE_NONE);
        const uint32_t index = ma_index(t, &rig, handle);
        const uint8_t sex = rig.store->borrowed_sex[index];
        REQUIRE(sex == ENTITY_STORE_SEX_MALE || sex == ENTITY_STORE_SEX_FEMALE);
        const float32_t scale = sex == ENTITY_STORE_SEX_MALE ? 1.2f32 : 1.0f32;
        CHECK_NEAR(rig.store->borrowed_half_width[index], 0.45f32 * scale, MA_EXACT);
        CHECK_NEAR(rig.store->borrowed_height[index], 0.9f32 * scale, MA_EXACT);
        males += sex == ENTITY_STORE_SEX_MALE ? 1u : 0u;
        REQUIRE(entity_store_remove(rig.store, handle));
    }
    /* Half of 4000, within five standard deviations of about 32 each way. */
    CHECK(males > 1840u && males < 2160u);
    /* Anything without a male and a female is neither, and draws no number for it. */
    const terrain_rng_t before = rig.spawn->random;
    const entity_handle_t zombie = mob_spawn_body(rig.spawn, rig.store, MOB_ID_ZOMBIE, place, 0.0f32);
    CHECK_EQ_U64(rig.store->borrowed_sex[ma_index(t, &rig, zombie)], ENTITY_STORE_SEX_NONE);
    CHECK_EQ_U64(rig.spawn->random.state, before.state);
    CHECK(mob_spawn_has_sexes(MOB_ID_PIG));
    CHECK(!mob_spawn_has_sexes(MOB_ID_ZOMBIE));
    ma_end(&rig);
}

TEST(a_bodys_sex_stays_with_it_when_others_go) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 4u));
    const entity_handle_t first = ma_pig(&rig, 1.5f64, 1.5f64, ENTITY_STORE_SEX_FEMALE);
    const entity_handle_t second = ma_pig(&rig, 3.5f64, 1.5f64, ENTITY_STORE_SEX_MALE);
    const entity_handle_t third = ma_pig(&rig, 5.5f64, 1.5f64, ENTITY_STORE_SEX_FEMALE);
    /* The last moves into the first's place, and takes its sex with it. */
    REQUIRE(entity_store_remove(rig.store, first));
    CHECK_EQ_U64(rig.store->borrowed_sex[ma_index(t, &rig, second)], ENTITY_STORE_SEX_MALE);
    CHECK_EQ_U64(rig.store->borrowed_sex[ma_index(t, &rig, third)], ENTITY_STORE_SEX_FEMALE);
    CHECK_EQ_U64(ma_index(t, &rig, third), 0u);
    ma_end(&rig);
}

TEST(a_males_boxes_are_its_kinds_and_its_own_and_all_bigger) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 5u));
    const entity_handle_t boar = ma_pig(&rig, 1.5f64, 1.5f64, ENTITY_STORE_SEX_MALE);
    const entity_handle_t sow = ma_pig(&rig, 5.5f64, 1.5f64, ENTITY_STORE_SEX_FEMALE);
    entity_animation_pose_t poses[ENTITY_ANIMATION_PARTS_MAX];
    const entity_animation_part_t *parts[ENTITY_ANIMATION_PARTS_MAX];
    CHECK_EQ_U64(entity_animation_body_pose(rig.store, ma_index(t, &rig, boar), poses, parts), 8u);
    const mob_def_t *const pig = &mob_defs[MOB_ID_PIG];
    CHECK(parts[0] == &mob_parts[pig->first_part]);
    CHECK(parts[6] == &mob_parts[mob_sexes[pig->first_sex].first_part]);
    /* The head, 8 pixels across, is 1.2 times half a block across, and its middle 1.2 times as high. */
    CHECK_NEAR(poses[0].half[0], 0.25f32 * 1.2f32, MA_EXACT);
    CHECK_NEAR((float32_t)poses[0].center[2], 12.0f32 / 16.0f32 * 1.2f32, MA_EXACT);
    CHECK_EQ_U64(entity_animation_body_pose(rig.store, ma_index(t, &rig, sow), poses, parts), 6u);
    CHECK_NEAR(poses[0].half[0], 0.25f32, MA_EXACT);
    CHECK_NEAR((float32_t)poses[0].center[2], 12.0f32 / 16.0f32, MA_EXACT);
    /* A zombie is as it was. */
    const float64_t place[COLLISION_AXES] = {9.5f64, 1.5f64, 0.0f64};
    const entity_handle_t zombie = mob_spawn_body(rig.spawn, rig.store, MOB_ID_ZOMBIE, place, 0.0f32);
    CHECK_EQ_U64(entity_animation_body_pose(rig.store, ma_index(t, &rig, zombie), poses, parts), 6u);
    CHECK(entity_animation_scale_of(MOB_ID_ZOMBIE, ENTITY_STORE_SEX_NONE) == 1.0f32);
    CHECK(entity_animation_scale_of(MOB_ID_PIG, ENTITY_STORE_SEX_MALE) == 1.2f32);
    CHECK_EQ_U64(entity_animation_sex_rig_of(MOB_ID_PIG, ENTITY_STORE_SEX_NONE).part_count, 0u);
    ma_end(&rig);
}

/* ---- The rules ----------------------------------------------------------------------------------------------------- */

TEST(an_animal_comes_onto_grass_by_day_and_not_by_night) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 6u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    const mob_spawn_view_t noon = ma_view(MA_NOON, false), midnight = ma_view(MA_MIDNIGHT, false);
    const float64_t x = MA_PLAYER_X + 30.0f64, y = MA_PLAYER_Y;
    /* A passive mob's roll is its max_light, so open sky at noon passes every time. */
    CHECK_EQ_U64(ma_check(&rig, MOB_ID_PIG, x, y, &noon, 15u), MOB_SPAWN_OK);
    CHECK_EQ_U64(ma_check(&rig, MOB_ID_PIG, x, y, &midnight, 15u), MOB_SPAWN_TOO_DARK);
    /* Not nearer than its min_distance, but on the first day as near as MOB_SPAWN_FIRST_DAY_NEAR. */
    CHECK_EQ_U64(ma_check(&rig, MOB_ID_PIG, MA_PLAYER_X + 14.0f64, y, &noon, 15u), MOB_SPAWN_TOO_NEAR);
    const mob_spawn_view_t first = ma_view(MA_NOON, true);
    CHECK_EQ_U64(ma_check(&rig, MOB_ID_PIG, MA_PLAYER_X + 14.0f64, y, &first, 15u), MOB_SPAWN_OK);
    CHECK_EQ_U64(ma_check(&rig, MOB_ID_PIG, MA_PLAYER_X + 10.0f64, y, &first, 15u), MOB_SPAWN_TOO_NEAR);
    /* No distance is too far for what never goes. */
    CHECK_EQ_U64(ma_check(&rig, MOB_ID_PIG, MA_PLAYER_X + 130.0f64, MA_PLAYER_Y, &noon, 15u), MOB_SPAWN_OK);
    /* Only on its on list: grass, and not stone. */
    test_world_fill(t, &rig.world, (int32_t)x, (int32_t)y, -1, (int32_t)x, (int32_t)y, -1, BLOCK_ID_STONE);
    CHECK_EQ_U64(ma_check(&rig, MOB_ID_PIG, x, y, &noon, 15u), MOB_SPAWN_BAD_SURFACE);
    /* A zombie, with no on list, still takes stone. */
    CHECK(mob_spawn_biome_allows(MOB_ID_ZOMBIE, BIOME_ID_DESERT));
    ma_end(&rig);
}

TEST(an_animal_comes_only_in_its_biomes) {
    CHECK(mob_spawn_biome_allows(MOB_ID_PIG, BIOME_ID_PLAINS));
    CHECK(mob_spawn_biome_allows(MOB_ID_PIG, BIOME_ID_MEADOW));
    CHECK(mob_spawn_biome_allows(MOB_ID_PIG, BIOME_ID_SAVANNA));
    CHECK(mob_spawn_biome_allows(MOB_ID_PIG, BIOME_ID_FOREST));
    CHECK(!mob_spawn_biome_allows(MOB_ID_PIG, BIOME_ID_DESERT));
    CHECK(!mob_spawn_biome_allows(MOB_ID_PIG, BIOME_ID_JAGGED_PEAKS));
    CHECK(!mob_spawn_biome_allows(MOB_ID_PIG, BIOME_ID_OCEAN));
    for (uint32_t biome = 0u; biome < BIOME_COUNT; biome++) {
        CHECK(mob_spawn_biome_allows(MOB_ID_CREEPER, (biome_id_t)biome));
    }
}

TEST(herds_fill_their_own_cap_by_day_with_males_first_and_never_go) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 7u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    const mob_spawn_view_t noon = ma_view(MA_NOON, true);
    const uint32_t cap = mob_spawn_animal_cap(rig.world.map->ready_count);
    REQUIRE(cap > 1u);
    CHECK(cap <= MOB_SPAWN_ANIMAL_CAP);
    uint32_t males = 0u, females = 0u, ticks_with_some = 0u;
    for (uint32_t tick = 0u; tick < MA_FILL_TICKS; tick++) {
        const uint32_t before = rig.store->count;
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &noon);
        if (rig.store->count != before) {
            ticks_with_some++;
            /* The first of a tick is the first of a herd, which is a male. */
            CHECK_EQ_U64(rig.store->borrowed_sex[before], ENTITY_STORE_SEX_MALE);
        }
        CHECK(rig.store->count <= cap);
    }
    CHECK_EQ_U64(rig.store->count, cap);
    CHECK_EQ_U64(ma_count_kind(rig.store, MOB_ID_PIG), cap);
    for (uint32_t i = 0u; i < rig.store->count; i++) {
        males += rig.store->borrowed_sex[i] == ENTITY_STORE_SEX_MALE ? 1u : 0u;
        females += rig.store->borrowed_sex[i] == ENTITY_STORE_SEX_FEMALE ? 1u : 0u;
        CHECK_EQ_U64(rig.store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING, 0u);
    }
    CHECK(ticks_with_some > 0u);
    CHECK(males > 0u);
    CHECK(females > 0u);
    CHECK_EQ_U64(males + females, cap);
    /* Far from the player, a long while: an animal is kept, unlike a zombie. */
    mob_spawn_view_t away = noon;
    away.position[0] += 5000.0f64;
    ma_ticks(&rig, &away, MA_FILL_TICKS);
    CHECK_EQ_U64(rig.store->count, cap);
    CHECK_EQ_U64(rig.spawn->despawned_count, 0u);
    ma_end(&rig);
}

TEST(the_two_caps_are_apart) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 8u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    /* Herds by day first, then night: the zombies fill their own cap and the animals do not stop them. */
    mob_spawn_view_t noon = ma_view(MA_NOON, true);
    noon.weights = MA_PIGS_AND_ZOMBIES;
    ma_ticks(&rig, &noon, MA_FILL_TICKS);
    const uint32_t animals = mob_spawn_animal_cap(rig.world.map->ready_count);
    CHECK_EQ_U64(ma_count_kind(rig.store, MOB_ID_PIG), animals);
    CHECK_EQ_U64(ma_count_kind(rig.store, MOB_ID_ZOMBIE), 0u);
    mob_spawn_view_t midnight = noon;
    midnight.sun_height = MA_MIDNIGHT;
    ma_ticks(&rig, &midnight, MA_FILL_TICKS);
    CHECK_EQ_U64(ma_count_kind(rig.store, MOB_ID_PIG), animals);
    CHECK_EQ_U64(ma_count_kind(rig.store, MOB_ID_ZOMBIE), mob_spawn_cap(rig.world.map->ready_count));
    ma_end(&rig);
}

TEST(after_the_first_day_herds_come_rarely_and_not_near) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 9u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    const uint32_t cap = mob_spawn_animal_cap(rig.world.map->ready_count);
    /* On the first day the cap fills in a few ticks. */
    const mob_spawn_view_t first = ma_view(MA_NOON, true);
    uint32_t first_ticks = 0u;
    while (rig.store->count < cap && first_ticks < MA_RARE_TICKS) {
        ma_ticks(&rig, &first, 1u);
        first_ticks++;
    }
    CHECK(first_ticks < 50u);
    while (rig.store->count != 0u) {
        REQUIRE(entity_store_remove(rig.store, rig.store->borrowed_handle[0]));
    }
    /* After it, a look comes about once in 400 ticks: over 8000, about 20 ticks bring a herd, each taken away again so the cap never
     * comes into it, and none comes nearer than its min_distance. */
    const mob_spawn_view_t noon = ma_view(MA_NOON, false);
    uint32_t herd_ticks = 0u;
    for (uint32_t tick = 0u; tick < MA_RARE_TICKS; tick++) {
        ma_ticks(&rig, &noon, 1u);
        herd_ticks += rig.store->count != 0u ? 1u : 0u;
        for (uint32_t i = 0u; i < rig.store->count; i++) {
            const float64_t x = rig.store->borrowed_position[0][i] - MA_PLAYER_X;
            const float64_t y = rig.store->borrowed_position[1][i] - MA_PLAYER_Y;
            CHECK(sqrt(x * x + y * y) >= (float64_t)mob_defs[MOB_ID_PIG].min_distance);
        }
        while (rig.store->count != 0u) {
            REQUIRE(entity_store_remove(rig.store, rig.store->borrowed_handle[0]));
        }
    }
    printf("  the cap of %u filled in %u ticks on the first day; after it, %u of %u ticks brought a herd\n", cap,
           first_ticks, herd_ticks, MA_RARE_TICKS);
    CHECK(herd_ticks >= 5u && herd_ticks <= 45u);
    ma_end(&rig);
}

TEST(with_no_animal_weighed_nothing_is_drawn) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 10u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    /* The files' own weights: the pig's is 0, as are the three hostile mobs'. */
    mob_spawn_view_t noon = ma_view(MA_NOON, true);
    noon.weights = nullptr;
    const terrain_rng_t before = rig.spawn->random;
    ma_ticks(&rig, &noon, MA_FILL_TICKS);
    CHECK_EQ_U64(rig.store->count, 0u);
    CHECK_EQ_U64(rig.spawn->random.state, before.state);
    ma_end(&rig);
}

/* ---- The mind ------------------------------------------------------------------------------------------------------ */

static mob_zombie_view_t ma_player(float64_t x, float64_t y) {
    return (mob_zombie_view_t){
        .position = {x, y, 0.0f64}, .eye = 1.62f64, .half_width = 0.3f64, .height = 1.8f64, .alive = 1u};
}

/* A tick as the game runs one, returning the blow the player takes. */
static float32_t ma_step(ma_rig_t *rig, const mob_zombie_view_t *view) {
    mob_tick(rig->mob);
    const float32_t blow = mob_minds_tick(rig->mob, rig->entity, view);
    entity_tick(rig->entity, rig->world.map);
    mob_combat_tick(&rig->mob->combat, rig->store);
    return blow;
}

TEST(a_hit_animal_runs_from_the_player_for_a_while_male_or_female_and_never_strikes) {
    static const uint8_t SEXES[] = {ENTITY_STORE_SEX_MALE, ENTITY_STORE_SEX_FEMALE};
    for (uint32_t s = 0u; s < 2u; s++) {
        ma_rig_t rig;
        REQUIRE(ma_begin(t, &rig, 11u + s));
        ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
        const entity_handle_t pig = ma_pig(&rig, 10.5f64, 8.5f64, SEXES[s]);
        const mob_zombie_view_t player = ma_player(8.5f64, 8.5f64);
        for (uint32_t i = 0u; i < 20u; i++) {
            CHECK(ma_step(&rig, &player) == 0.0f32); /* it lands */
        }
        const float64_t start = rig.store->borrowed_position[0][ma_index(t, &rig, pig)];
        const mob_combat_outcome_t hit = mob_combat_hurt(&rig.mob->combat, rig.store, ma_index(t, &rig, pig), 1.0f32);
        CHECK_EQ_U64(hit.landed, 1u);
        for (uint32_t i = 0u; i < MA_FLEE_LOOK; i++) {
            CHECK(ma_step(&rig, &player) == 0.0f32);
        }
        const uint32_t index = ma_index(t, &rig, pig);
        const mob_passive_body_t *const body = mob_passive_body_of(&rig.mob->passive, rig.store, index);
        CHECK(body->flee > 0u);
        CHECK_EQ_U64(rig.mob->passive.borrowed_tally->fled_count, 1u);
        /* Away along x, faster than it walks: 60 ticks at 0.19 a tick is over 11 blocks, less the start. */
        CHECK(rig.store->borrowed_position[0][index] - start > 6.0f64);
        for (uint32_t i = 0u; i < MOB_PASSIVE_FLEE_TICKS; i++) {
            CHECK(ma_step(&rig, &player) == 0.0f32);
        }
        CHECK_EQ_U64(mob_passive_body_of(&rig.mob->passive, rig.store, ma_index(t, &rig, pig))->flee, 0u);
        CHECK_EQ_U64(rig.mob->zombie.struck_count, 0u);
        ma_end(&rig);
    }
}

TEST(a_dead_animal_leaves_its_kinds_drops_and_its_sexs) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 13u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    const mob_zombie_view_t player = ma_player(-30.5f64, -30.5f64);
    uint64_t boar_items = 0u, sow_items = 0u;
    for (uint32_t i = 0u; i < MA_DEATHS; i++) {
        const uint8_t sex = (i & 1u) == 0u ? ENTITY_STORE_SEX_MALE : ENTITY_STORE_SEX_FEMALE;
        const entity_handle_t pig = ma_pig(&rig, 20.5f64, 8.5f64, sex);
        rig.store->borrowed_health[ma_index(t, &rig, pig)] = 0.0f32;
        const uint64_t before = rig.mob->passive.borrowed_tally->dropped_count;
        (void)ma_step(&rig, &player);
        uint32_t index = 0u;
        CHECK(!entity_store_index_of(rig.store, pig, &index));
        const uint64_t items = rig.mob->passive.borrowed_tally->dropped_count - before;
        if (sex == ENTITY_STORE_SEX_MALE) {
            /* A pig's 0 to 1, and a boar's 1 more. */
            CHECK(items >= 1u && items <= 2u);
            boar_items += items;
        } else {
            CHECK(items <= 1u);
            sow_items += items;
        }
    }
    CHECK_EQ_U64(rig.mob->passive.borrowed_tally->died_count, MA_DEATHS);
    /* On average 1.5 against 0.5. */
    CHECK(boar_items > sow_items + MA_DEATHS / 4u);
    ma_end(&rig);
}

TEST(a_stray_walks_back_to_its_herd) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 14u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    const entity_handle_t stray = ma_pig(&rig, 18.5f64, 8.5f64, ENTITY_STORE_SEX_MALE);
    (void)ma_pig(&rig, 30.5f64, 8.5f64, ENTITY_STORE_SEX_FEMALE);
    (void)ma_pig(&rig, 31.5f64, 9.5f64, ENTITY_STORE_SEX_FEMALE);
    (void)ma_pig(&rig, 30.5f64, 10.5f64, ENTITY_STORE_SEX_MALE);
    const mob_zombie_view_t player = ma_player(-30.5f64, -30.5f64);
    float64_t nearest = 1.0e9f64;
    for (uint32_t i = 0u; i < MA_HERD_TICKS; i++) {
        (void)ma_step(&rig, &player);
        float64_t middle[2] = {0.0f64, 0.0f64};
        const uint32_t me = ma_index(t, &rig, stray);
        for (uint32_t b = 0u; b < rig.store->count; b++) {
            if (b != me) {
                middle[0] += rig.store->borrowed_position[0][b] / 3.0f64;
                middle[1] += rig.store->borrowed_position[1][b] / 3.0f64;
            }
        }
        const float64_t gap =
            hypot(rig.store->borrowed_position[0][me] - middle[0], rig.store->borrowed_position[1][me] - middle[1]);
        nearest = gap < nearest ? gap : nearest;
    }
    /* It started 12 or more from them, past MOB_PASSIVE_HERD_FAR, and goes back to within it. */
    CHECK(nearest <= (float64_t)MOB_PASSIVE_HERD_FAR);
    CHECK_EQ_U64(rig.store->count, 4u);
    ma_end(&rig);
}

TEST(a_tick_of_animals_takes_no_heap) {
    ma_rig_t rig;
    REQUIRE(ma_begin(t, &rig, 15u));
    ma_floor(t, &rig, BLOCK_ID_GRASS_BLOCK);
    const mob_spawn_view_t noon = ma_view(MA_NOON, true);
    ma_ticks(&rig, &noon, 50u);
    const mob_zombie_view_t player = ma_player(8.5f64, 8.5f64);
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 200u; i++) {
        mob_spawn_tick(rig.spawn, rig.store, rig.world.physics, rig.world.map, &noon);
        (void)ma_step(&rig, &player);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    ma_end(&rig);
}

TEST_MAIN()
