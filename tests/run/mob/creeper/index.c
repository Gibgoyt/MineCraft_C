/* ctest run_mob_creeper. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "heap/index.h"
#include "item/stack/index.h"
#include "mob/index.h"
#include "mob/spawn/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/mobs/index.h"
#include "time/index.h"
#include "world/wall_torch/index.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Everything happens around here, in the middle of the loaded square, on a floor whose top is at z = 0. */
#define MC_X 8.5f64
#define MC_Y 8.5f64
/* A power that a test uses when it is not about the power. */
#define MC_POWER 3.0f64
#define MC_EXACT 1.0e-9f64

enum : int32_t {
    MC_FLOOR_MIN = -40,
    MC_FLOOR_MAX = 55,
};

typedef struct mc_rig_t {
    test_world_t world;          /* 0 */
    entity_t *entity;            /* 24 */
    mob_t *mob;                  /* 32 */
    player_movement_t *movement; /* 40 */
    player_survival_t *survival; /* 48 */
    mob_spawn_t *spawn;          /* 56: for mob_spawn_body */
    uint32_t *spawn_idle;        /* 64 */
    mob_creeper_scene_t scene;   /* 72 */
} mc_rig_t;
static_assert(sizeof(mc_rig_t) == 112);

static bool mc_begin(test_context_t *t, mc_rig_t *rig, uint64_t seed) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    rig->movement = test_alloc(t, sizeof(player_movement_t));
    rig->survival = test_alloc(t, sizeof(player_survival_t));
    rig->spawn = test_alloc(t, sizeof(mob_spawn_t));
    rig->spawn_idle = test_alloc(t, (uint64_t)ENTITY_STORE_CAPACITY * sizeof(uint32_t));
    rig->spawn->borrowed_idle = rig->spawn_idle;
    if (!CHECK(entity_init(rig->entity, rig->world.physics)) || !CHECK(mob_init(rig->mob, nullptr))) {
        return false;
    }
    entity_open(rig->entity, seed);
    mob_open(rig->mob, rig->world.world, seed);
    mob_creeper_seed_set(&rig->mob->creeper, seed);
    player_movement_start(rig->movement, MC_X + 40.0f64, MC_Y, 0.0f64);
    player_survival_start(rig->survival, MC_X + 40.0f64, MC_Y, 0.0f64);
    rig->scene = (mob_creeper_scene_t){
        .borrowed_entity = rig->entity,
        .borrowed_path = &rig->mob->path,
        .borrowed_edit = &rig->world.world->edit,
        .borrowed_movement = rig->movement,
        .borrowed_survival = rig->survival,
    };
    return true;
}

static void mc_end(mc_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

/* Each loaded column, from z0 to z1. */
static void mc_fill_loaded(test_context_t *t, mc_rig_t *rig, int32_t z0, int32_t z1, uint16_t block) {
    for (int32_t x = MC_FLOOR_MIN; x <= MC_FLOOR_MAX; x++) {
        for (int32_t y = MC_FLOOR_MIN; y <= MC_FLOOR_MAX; y++) {
            column_t *const column = world_map_column_at(rig->world.map, column_chunk_of(x), column_chunk_of(y));
            for (int32_t z = z0; column != nullptr && z <= z1; z++) {
                CHECK_EQ_U64(column_block_set(column, &rig->world.world->section_pool, column_local_of(x),
                                              column_local_of(y), z, block),
                             COLUMN_STATUS_OK);
            }
        }
    }
}

static void mc_floor(test_context_t *t, mc_rig_t *rig) {
    mc_fill_loaded(t, rig, -2, -1, BLOCK_ID_STONE);
}

static void mc_put(test_context_t *t, mc_rig_t *rig, int32_t x, int32_t y, int32_t z, uint16_t block) {
    test_world_fill(t, &rig->world, x, y, z, x, y, z, block);
}

/* The block there, and air if there is none to read. */
static uint16_t mc_block(const mc_rig_t *rig, int32_t x, int32_t y, int32_t z) {
    uint16_t block = BLOCK_ID_AIR;
    (void)world_map_voxel_get(rig->world.map, x, y, z, &block);
    return block;
}

/* How many cells from one corner to the other hold this block. */
static uint32_t mc_count(const mc_rig_t *rig, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                         uint16_t block) {
    uint32_t count = 0u;
    for (int32_t x = x0; x <= x1; x++) {
        for (int32_t y = y0; y <= y1; y++) {
            for (int32_t z = z0; z <= z1; z++) {
                count += mc_block(rig, x, y, z) == block ? 1u : 0u;
            }
        }
    }
    return count;
}

static entity_handle_t mc_body(mc_rig_t *rig, mob_id_t mob, float64_t x, float64_t y, float64_t z) {
    const float64_t position[COLLISION_AXES] = {x, y, z};
    return mob_spawn_body(rig->spawn, &rig->entity->store, mob, position, 0.0f32);
}

static uint32_t mc_index(test_context_t *t, const mc_rig_t *rig, entity_handle_t handle) {
    uint32_t index = 0u;
    CHECK(entity_store_index_of(&rig->entity->store, handle, &index));
    return index;
}

static bool mc_alive(const mc_rig_t *rig, entity_handle_t handle) {
    uint32_t index = 0u;
    return entity_store_index_of(&rig->entity->store, handle, &index);
}

static const mob_creeper_body_t *mc_mind(const mc_rig_t *rig, entity_handle_t handle) {
    return &rig->mob->creeper.borrowed_bodies[entity_handle_slot(handle)];
}

static void mc_player_at(mc_rig_t *rig, float64_t x, float64_t y, float64_t z) {
    memset(rig->movement->velocity, 0, sizeof(rig->movement->velocity));
    rig->movement->position[0] = x;
    rig->movement->position[1] = y;
    rig->movement->position[2] = z;
}

/* One tick, in the game's order, with the player standing as it is. */
static void mc_tick(mc_rig_t *rig) {
    mob_tick(rig->mob);
    mob_creeper_tick(&rig->mob->creeper, &rig->scene);
    entity_tick(rig->entity, rig->world.map);
}

static void mc_ticks(mc_rig_t *rig, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        mc_tick(rig);
    }
}

/* Ticks until this one is chasing, with a good limit on what 1 in 10 a tick can take. */
static bool mc_until_chasing(mc_rig_t *rig, entity_handle_t handle) {
    for (uint32_t i = 0u; i < 1000u; i++) {
        mc_tick(rig);
        if (mc_mind(rig, handle)->state == MOB_CREEPER_STATE_CHASING) {
            return true;
        }
    }
    return false;
}

static void mc_blast(mc_rig_t *rig, const float64_t center[COLLISION_AXES], float64_t power) {
    explosion_begin(&rig->entity->explosion, rig->world.map, &rig->entity->store, &rig->entity->item_drops, center,
                    power);
    (void)explosion_finish(&rig->entity->explosion, &rig->world.world->edit, &rig->entity->item_drops, power);
}

static uint64_t mc_gunpowder(const mc_rig_t *rig) {
    const uint16_t row = item_stack_drop_row(ITEM_ID_GUNPOWDER);
    uint64_t count = 0u;
    const item_drop_store_t *const drops = &rig->entity->item_drops;
    for (uint32_t i = 0u; i < drops->count; i++) {
        count += drops->borrowed_items[i].item == row ? drops->borrowed_items[i].count : 0u;
    }
    return count;
}

/* The damage a body takes at this impact from a blast of power 3, on paper. */
static float64_t mc_paper_damage(float64_t impact) {
    return floor((impact * impact + impact) * 0.5f64 * 7.0f64 * 6.0f64 + 1.0f64);
}

/* ---- Resistance ---------------------------------------------------------------------------------------------------- */

TEST(resistance_is_the_published_number) {
    CHECK_NEAR(explosion_resistance(BLOCK_ID_AIR), 0.0f64, MC_EXACT);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_DIRT), 0.5f64, 1.0e-6f64);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_STONE), 6.0f64, MC_EXACT);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_COBBLESTONE), 6.0f64, MC_EXACT);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_OAK_PLANKS), 3.0f64, MC_EXACT);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_OAK_LOG), 2.0f64, 1.0e-6f64);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_TERRACOTTA), 4.2f64, MC_EXACT);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_DEEPSLATE_IRON_ORE), 3.0f64, MC_EXACT);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_IRON_ORE), 3.0f64, 1.0e-6f64);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_OBSIDIAN), 1200.0f64, MC_EXACT);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_WATER), 100.0f64, 1.0e-6f64);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_LAVA), 100.0f64, 1.0e-6f64);
    CHECK(explosion_resistance(BLOCK_ID_BEDROCK) >= 3600000.0f64);
    CHECK_NEAR(explosion_resistance(BLOCK_ID_TORCH), 0.0f64, MC_EXACT);
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        CHECK(explosion_resistance((uint16_t)block) >= 0.0f64);
    }
}

/* ---- Sight --------------------------------------------------------------------------------------------------------- */

TEST(a_line_is_stopped_by_what_collides_and_only_that) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 1u));
    const world_map_t *const map = rig.world.map;
    const float64_t from[3] = {0.5f64, 0.5f64, 1.5f64}, to[3] = {6.5f64, 0.5f64, 1.5f64};
    CHECK(explosion_line_clear(map, from, to));
    CHECK(explosion_line_clear(map, from, from));

    mc_put(t, &rig, 3, 0, 1, BLOCK_ID_TALL_GRASS);
    CHECK(explosion_line_clear(map, from, to));
    mc_put(t, &rig, 3, 0, 1, BLOCK_ID_WATER);
    CHECK(explosion_line_clear(map, from, to));
    mc_put(t, &rig, 3, 0, 1, BLOCK_ID_GLASS);
    CHECK(!explosion_line_clear(map, from, to));
    CHECK(!explosion_line_clear(map, to, from));

    /* A slab fills the bottom half of its cell: over it and through it. */
    mc_put(t, &rig, 3, 0, 1, BLOCK_ID_OAK_SLAB);
    const float64_t over_from[3] = {0.5f64, 0.5f64, 1.75f64}, over_to[3] = {6.5f64, 0.5f64, 1.75f64};
    const float64_t under_from[3] = {0.5f64, 0.5f64, 1.25f64}, under_to[3] = {6.5f64, 0.5f64, 1.25f64};
    CHECK(explosion_line_clear(map, over_from, over_to));
    CHECK(!explosion_line_clear(map, under_from, under_to));

    /* Going along a face, or ending on one, does not go in. */
    mc_put(t, &rig, 3, 0, 1, BLOCK_ID_STONE);
    const float64_t along_from[3] = {0.5f64, 0.5f64, 2.0f64}, along_to[3] = {6.5f64, 0.5f64, 2.0f64};
    CHECK(explosion_line_clear(map, along_from, along_to));
    const float64_t end_on[3] = {3.0f64, 0.5f64, 1.5f64};
    CHECK(explosion_line_clear(map, from, end_on));

    /* From inside a block it is stopped from the start. */
    const float64_t inside[3] = {3.5f64, 0.5f64, 1.5f64};
    CHECK(!explosion_line_clear(map, inside, to));

    /* A column not loaded cannot be seen through. */
    const float64_t far[3] = {0.5f64, 300.5f64, 1.5f64};
    CHECK(!explosion_line_clear(map, from, far));

    /* Over the top of the world there is nothing. */
    const float64_t high_a[3] = {0.5f64, 0.5f64, (float64_t)COLUMN_Z_GAMEPLAY_MAX + 10.0f64};
    const float64_t high_b[3] = {6.5f64, 0.5f64, (float64_t)COLUMN_Z_GAMEPLAY_MAX + 10.0f64};
    CHECK(explosion_line_clear(map, high_a, high_b));

    /* Each way across a corner, with a negative start. */
    mc_put(t, &rig, -3, -3, 1, BLOCK_ID_STONE);
    const float64_t neg_a[3] = {-5.5f64, -5.5f64, 1.5f64}, neg_b[3] = {-0.5f64, -0.5f64, 1.5f64};
    CHECK(!explosion_line_clear(map, neg_a, neg_b));
    CHECK(!explosion_line_clear(map, neg_b, neg_a));
    const float64_t neg_c[3] = {-5.5f64, -0.5f64, 1.5f64};
    CHECK(explosion_line_clear(map, neg_c, neg_b));
    mc_end(&rig);
}

TEST(exposure_is_the_share_of_a_grid_with_a_clear_line) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 2u));
    const world_map_t *const map = rig.world.map;
    mc_floor(t, &rig);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    collision_box_t box;
    const float64_t position[3] = {MC_X + 3.0f64, MC_Y, 0.0f64};
    player_movement_box(position, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    CHECK_NEAR(explosion_exposure(map, center, &box), 1.0f64, MC_EXACT);

    /* A wall between them, wider and taller than the body from here: nothing gets through. */
    test_world_fill(t, &rig.world, 10, 0, 0, 10, 16, 3, BLOCK_ID_OBSIDIAN);
    CHECK_NEAR(explosion_exposure(map, center, &box), 0.0f64, MC_EXACT);

    /* Only the lowest cell of the wall: the grid for a player is 3 by 3 by 5, and its upper points see over. */
    test_world_fill(t, &rig.world, 10, 0, 1, 10, 16, 3, BLOCK_ID_AIR);
    const float64_t exposure = explosion_exposure(map, center, &box);
    CHECK(exposure > 0.0f64 && exposure < 1.0f64);
    /* In forty-fifths, since that is how many points there are. */
    CHECK_NEAR(exposure * 45.0f64, round(exposure * 45.0f64), 1.0e-9f64);
    mc_end(&rig);
}

/* ---- A blow, on paper ---------------------------------------------------------------------------------------------- */

TEST(a_blow_is_what_the_published_sum_says) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 3u));
    const world_map_t *const map = rig.world.map;
    mc_floor(t, &rig);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    /* A body 3 away along x, in the open, with its eye at its feet: halfway through the reach of 6. */
    const float64_t position[3] = {MC_X + 3.0f64, MC_Y, 0.5f64};
    collision_box_t box;
    player_movement_box(position, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    explosion_blow_t blow;
    REQUIRE(explosion_blow(map, center, MC_POWER, position, &box, 0.0f64, &blow));
    CHECK_NEAR(blow.impact, 0.5f64, MC_EXACT);
    /* (0.25 + 0.5) / 2 * 7 * 6 + 1 = 16.75, in whole half hearts. */
    CHECK_NEAR(blow.damage, 16.0f32, 1.0e-6f32);
    CHECK_NEAR(blow.push[0], 0.5f64, MC_EXACT);
    CHECK_NEAR(blow.push[1], 0.0f64, MC_EXACT);
    CHECK_NEAR(blow.push[2], 0.0f64, MC_EXACT);

    /* An eye of 4 makes the way from the middle a 3-4-5 triangle. The push turns, and its size stays the impact. */
    REQUIRE(explosion_blow(map, center, MC_POWER, position, &box, 4.0f64, &blow));
    CHECK_NEAR(blow.push[0], 0.5f64 * 3.0f64 / 5.0f64, MC_EXACT);
    CHECK_NEAR(blow.push[2], 0.5f64 * 4.0f64 / 5.0f64, MC_EXACT);

    /* Exactly at the edge of its reach it is still hit, at the least there is. */
    const float64_t edge[3] = {MC_X + 6.0f64, MC_Y, 0.5f64};
    player_movement_box(edge, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    REQUIRE(explosion_blow(map, center, MC_POWER, edge, &box, 0.0f64, &blow));
    CHECK_NEAR(blow.damage, 1.0f32, 1.0e-6f32);
    CHECK_NEAR(blow.impact, 0.0f64, MC_EXACT);
    /* And a hair past it not. */
    const float64_t past[3] = {MC_X + 6.001f64, MC_Y, 0.5f64};
    CHECK(!explosion_blow(map, center, MC_POWER, past, &box, 0.0f64, &blow));

    /* Eye exactly at the middle: no way is away. */
    const float64_t under[3] = {MC_X, MC_Y, -0.5f64};
    CHECK(!explosion_blow(map, center, MC_POWER, under, &box, 1.0f64, &blow));

    /* Behind a wall, even along the floor: 1, and no push. */
    player_movement_box(position, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    test_world_fill(t, &rig.world, 10, 0, 0, 10, 16, 5, BLOCK_ID_OBSIDIAN);
    REQUIRE(explosion_blow(map, center, MC_POWER, position, &box, 1.62f64, &blow));
    CHECK_NEAR(blow.damage, 1.0f32, 1.0e-6f32);
    CHECK_NEAR(blow.impact, 0.0f64, MC_EXACT);
    CHECK_NEAR(blow.push[0], 0.0f64, MC_EXACT);

    /* The same wall, with the blast and the body both on the floor: a line along z = 0 runs into it as well. */
    const float64_t floor_center[3] = {MC_X, MC_Y, 0.0f64}, floor_body[3] = {MC_X + 3.0f64, MC_Y, 0.0f64};
    player_movement_box(floor_body, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    CHECK_NEAR(explosion_exposure(map, floor_center, &box), 0.0f64, MC_EXACT);
    mc_end(&rig);
}

/* ---- What breaks --------------------------------------------------------------------------------------------------- */

TEST(a_blast_in_dirt_makes_a_crater_of_cells_within_its_reach) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 4u));
    test_world_fill(t, &rig.world, -4, -4, -8, 24, 24, 8, BLOCK_ID_DIRT);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    explosion_t *const explosion = &rig.entity->explosion;
    explosion_begin(explosion, rig.world.map, &rig.entity->store, &rig.entity->item_drops, center, MC_POWER);
    const uint32_t listed = explosion->count;
    /* A power of 3 in dirt: a ball a few blocks across, and not one block or the whole of it. */
    CHECK(listed > 20u);
    CHECK(listed < 300u);
    /* Every cell is on the list once, and none past where the strongest ray can get: 1.3 * 3 / (0.225 + 0.24) * 0.3 is 2.5 blocks. */
    for (uint32_t i = 0u; i < listed; i++) {
        const explosion_cell_t *const a = &explosion->borrowed_cells[i];
        CHECK_EQ_U64(a->block, BLOCK_ID_DIRT);
        const float64_t dx = (float64_t)a->x + 0.5f64 - center[0], dy = (float64_t)a->y + 0.5f64 - center[1],
                        dz = (float64_t)a->z + 0.5f64 - center[2];
        CHECK(sqrt(dx * dx + dy * dy + dz * dz) < 4.5f64);
        for (uint32_t j = i + 1u; j < listed; j++) {
            const explosion_cell_t *const b = &explosion->borrowed_cells[j];
            CHECK(a->x != b->x || a->y != b->y || a->z != b->z);
        }
    }
    /* Nothing has gone yet: that is the second half. */
    CHECK_EQ_U64(mc_block(&rig, 8, 8, 0), BLOCK_ID_DIRT);
    const uint32_t broken = explosion_finish(explosion, &rig.world.world->edit, &rig.entity->item_drops, MC_POWER);
    CHECK_EQ_U64(broken, listed);
    CHECK_EQ_U64(explosion->broken_count, listed);
    CHECK_EQ_U64(explosion->blast_count, 1u);
    CHECK_EQ_U64(mc_block(&rig, 8, 8, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_count(&rig, -4, -4, -8, 24, 24, 8, BLOCK_ID_AIR), listed);
    /* It went through the edit log, so light, the renderer and paths all hear of it. */
    CHECK_EQ_U64(world_edit_log_position(&rig.world.world->edit), listed);
    mc_end(&rig);
}

/* How many of a solid block of this a blast of power 3 at its middle breaks. */
static uint32_t mc_crater(test_context_t *t, uint16_t block, uint64_t seed) {
    mc_rig_t rig;
    if (!mc_begin(t, &rig, seed)) {
        return 0u;
    }
    test_world_fill(t, &rig.world, 0, 0, -8, 16, 16, 8, block);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    mc_blast(&rig, center, MC_POWER);
    const uint32_t broken = (uint32_t)rig.entity->explosion.broken_count;
    mc_end(&rig);
    return broken;
}

TEST(what_stands_harder_breaks_less_and_some_nothing_breaks) {
    const uint32_t dirt = mc_crater(t, BLOCK_ID_DIRT, 5u);
    const uint32_t planks = mc_crater(t, BLOCK_ID_OAK_PLANKS, 5u);
    const uint32_t stone = mc_crater(t, BLOCK_ID_STONE, 5u);
    CHECK(dirt > planks);
    CHECK(planks > stone);
    /* Even in stone a blast of 3 gets the block it is in: that takes 1.89 of at least 2.1. */
    CHECK(stone >= 1u);
    CHECK_EQ_U64(mc_crater(t, BLOCK_ID_OBSIDIAN, 5u), 0u);
    CHECK_EQ_U64(mc_crater(t, BLOCK_ID_BEDROCK, 5u), 0u);
    CHECK_EQ_U64(mc_crater(t, BLOCK_ID_WATER, 5u), 0u);
    CHECK_EQ_U64(mc_crater(t, BLOCK_ID_LAVA, 5u), 0u);
}

TEST(a_wall_shields_what_is_behind_it) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 6u));
    /* Obsidian all around a cell, with dirt just past it on one side. */
    test_world_fill(t, &rig.world, 7, 7, -1, 11, 11, 3, BLOCK_ID_OBSIDIAN);
    test_world_fill(t, &rig.world, 8, 8, 0, 10, 10, 2, BLOCK_ID_AIR);
    test_world_fill(t, &rig.world, 12, 8, 0, 13, 10, 2, BLOCK_ID_DIRT);
    /* And some inside, to show the blast has the strength. */
    mc_put(t, &rig, 10, 10, 0, BLOCK_ID_DIRT);
    const float64_t center[3] = {9.5f64, 9.5f64, 1.5f64};
    mc_blast(&rig, center, MC_POWER);
    CHECK_EQ_U64(mc_block(&rig, 10, 10, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_count(&rig, 12, 8, 0, 13, 10, 2, BLOCK_ID_DIRT), 18u);
    CHECK_EQ_U64(mc_count(&rig, 7, 7, -1, 11, 11, 3, BLOCK_ID_OBSIDIAN), 125u - 27u);
    mc_end(&rig);
}

TEST(a_ray_ends_at_a_column_not_loaded_and_goes_on_past_the_top) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 7u));
    /* In open air by the edge of what is loaded: nothing to break, and nothing to trip on. */
    const float64_t edge[3] = {63.5f64, 8.5f64, 0.5f64};
    REQUIRE(world_map_column_at(rig.world.map, column_chunk_of(63), column_chunk_of(8)) != nullptr);
    REQUIRE(world_map_column_at(rig.world.map, column_chunk_of(64), column_chunk_of(8)) == nullptr);
    mc_blast(&rig, edge, EXPLOSION_POWER_MAX);
    CHECK_EQ_U64(rig.entity->explosion.broken_count, 0u);

    /* At the top and the bottom of the world, with the strongest there is, and nothing goes out of bounds or aborts. */
    const float64_t top[3] = {8.5f64, 8.5f64, (float64_t)COLUMN_Z_GAMEPLAY_MAX + 0.5f64};
    test_world_fill(t, &rig.world, 8, 8, COLUMN_Z_GAMEPLAY_MAX - 1, 8, 8, COLUMN_Z_GAMEPLAY_MAX, BLOCK_ID_DIRT);
    mc_blast(&rig, top, EXPLOSION_POWER_MAX);
    CHECK_EQ_U64(mc_count(&rig, 8, 8, COLUMN_Z_GAMEPLAY_MAX - 1, 8, 8, COLUMN_Z_GAMEPLAY_MAX, BLOCK_ID_DIRT), 0u);
    const float64_t bottom[3] = {8.5f64, 8.5f64, (float64_t)COLUMN_Z_GAMEPLAY_MIN + 0.5f64};
    mc_blast(&rig, bottom, EXPLOSION_POWER_MAX);
    mc_end(&rig);
}

TEST(the_strongest_blast_stays_within_its_bounds) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 8u));
    const float64_t center[3] = {MC_X + 0.999f64, MC_Y + 0.999f64, 0.999f64};
    explosion_t *const explosion = &rig.entity->explosion;
    /* Through open air, as far as any ray can go, to a wall 13 off: only the strongest get there. */
    test_world_fill(t, &rig.world, 22, -4, -13, 22, 22, 13, BLOCK_ID_DIRT);
    explosion_begin(explosion, rig.world.map, &rig.entity->store, &rig.entity->item_drops, center, EXPLOSION_POWER_MAX);
    CHECK(explosion->count > 0u);
    CHECK(explosion->count < 27u * 27u / 4u);
    test_world_fill(t, &rig.world, 22, -4, -13, 22, 22, 13, BLOCK_ID_AIR);

    /* In what stands least: as many cells as there can be, near enough. */
    test_world_fill(t, &rig.world, -8, -8, -16, 32, 32, 16, BLOCK_ID_MOSS_BLOCK);
    explosion_begin(explosion, rig.world.map, &rig.entity->store, &rig.entity->item_drops, center, EXPLOSION_POWER_MAX);
    CHECK(explosion->count > 500u);
    CHECK(explosion->count < EXPLOSION_CELLS);
    int32_t widest = 0;
    for (uint32_t i = 0u; i < explosion->count; i++) {
        const int32_t dx = explosion->borrowed_cells[i].x - 9;
        widest = dx > widest ? dx : (-dx > widest ? -dx : widest);
    }
    CHECK(widest <= (int32_t)EXPLOSION_REACH);
    mc_end(&rig);
}

TEST(the_same_seed_gives_the_same_list_and_another_another) {
    explosion_cell_t *const lists[3] = {
        test_alloc(t, (uint64_t)EXPLOSION_CELLS * sizeof(explosion_cell_t)),
        test_alloc(t, (uint64_t)EXPLOSION_CELLS * sizeof(explosion_cell_t)),
        test_alloc(t, (uint64_t)EXPLOSION_CELLS * sizeof(explosion_cell_t)),
    };
    const uint64_t seeds[3] = {9u, 9u, 10u};
    uint32_t counts[3] = {0u, 0u, 0u};
    for (uint32_t run = 0u; run < 3u; run++) {
        mc_rig_t rig;
        REQUIRE(mc_begin(t, &rig, seeds[run]));
        test_world_fill(t, &rig.world, 0, 0, -8, 16, 16, 8, BLOCK_ID_DIRT);
        const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
        explosion_begin(&rig.entity->explosion, rig.world.map, &rig.entity->store, &rig.entity->item_drops, center,
                        MC_POWER);
        counts[run] = rig.entity->explosion.count;
        memcpy(lists[run], rig.entity->explosion.borrowed_cells, (uint64_t)counts[run] * sizeof(explosion_cell_t));
        mc_end(&rig);
    }
    REQUIRE(counts[0] == counts[1]);
    CHECK_MEM_EQ(lists[0], lists[1], (uint64_t)counts[0] * sizeof(explosion_cell_t));
    CHECK(counts[0] != counts[2] || memcmp(lists[0], lists[2], (uint64_t)counts[0] * sizeof(explosion_cell_t)) != 0);
}

/* ---- What goes with it --------------------------------------------------------------------------------------------- */

TEST(what_needs_the_ground_goes_with_it) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 11u));
    /* Dirt under a stack of sugar cane taller than the blast reaches: the top of it goes because the bottom did. */
    test_world_fill(t, &rig.world, 8, 8, 0, 8, 8, 1, BLOCK_ID_DIRT);
    test_world_fill(t, &rig.world, 8, 8, 2, 8, 8, 12, BLOCK_ID_SUGAR_CANE);
    mc_put(t, &rig, 9, 8, 1, BLOCK_ID_TORCH);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    mc_blast(&rig, center, MC_POWER);
    CHECK_EQ_U64(mc_count(&rig, 8, 8, 0, 8, 8, 12, BLOCK_ID_SUGAR_CANE), 0u);
    CHECK_EQ_U64(mc_block(&rig, 9, 8, 1), BLOCK_ID_AIR);
    mc_end(&rig);
}

TEST(what_hangs_on_a_broken_block_goes_with_it_though_the_blast_did_not_reach_it) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 13u));
    explosion_t *const explosion = &rig.entity->explosion;
    item_drop_store_t *const drops = &rig.entity->item_drops;
    mc_put(t, &rig, 8, 8, 0, BLOCK_ID_DIRT);
    mc_put(t, &rig, 9, 8, 0, WALL_TORCH_FACING(BLOCK_FACING_WEST));
    mc_put(t, &rig, 7, 8, 0, WALL_TORCH_FACING(BLOCK_FACING_EAST));
    mc_put(t, &rig, 8, 9, 0, WALL_TORCH_FACING(BLOCK_FACING_SOUTH));
    mc_put(t, &rig, 8, 7, 0, WALL_TORCH_FACING(BLOCK_FACING_NORTH));
    /* On another block, beside this one: it leans the wrong way to hang from the dirt. */
    mc_put(t, &rig, 11, 8, 0, BLOCK_ID_STONE);
    mc_put(t, &rig, 10, 8, 0, WALL_TORCH_FACING(BLOCK_FACING_EAST));
    /* The list a blast would have made with only the dirt on it, so that no ray can be what took them. A power of 1: all give what they give. */
    explosion->borrowed_cells[0] = (explosion_cell_t){.x = 8, .y = 8, .z = 0, .block = BLOCK_ID_DIRT, ._pad0 = {0}};
    explosion->count = 1u;
    CHECK_EQ_U64(explosion_finish(explosion, &rig.world.world->edit, drops, 1.0f64), 5u);
    CHECK_EQ_U64(mc_block(&rig, 8, 8, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_block(&rig, 9, 8, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_block(&rig, 7, 8, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_block(&rig, 8, 9, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_block(&rig, 8, 7, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_block(&rig, 10, 8, 0), WALL_TORCH_FACING(BLOCK_FACING_EAST));
    uint32_t torches = 0u;
    for (uint32_t i = 0u; i < drops->count; i++) {
        const item_drop_t *const stack = &drops->borrowed_items[i];
        torches += strcmp(block_drop_items[stack->item].name, "torch") == 0 ? stack->count : 0u;
    }
    CHECK_EQ_U64(torches, 4u);
    mc_end(&rig);
}

/* What lies in the world of this item. */
static uint32_t mc_drops_of(const item_drop_store_t *drops, uint16_t item) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < drops->count; i++) {
        count += block_drop_items[drops->borrowed_items[i].item].item == item ? drops->borrowed_items[i].count : 0u;
    }
    return count;
}

/* Ahmed's bench is two blocks that are one. Whichever half a blast lists, and if it lists both, both go and one bench comes out, at a power
 * where one block in three would give what it gives. */
TEST(a_blast_at_either_half_or_both_takes_the_bench_and_gives_one) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 15u));
    explosion_t *const explosion = &rig.entity->explosion;
    item_drop_store_t *const drops = &rig.entity->item_drops;
    enum : uint32_t { ROUNDS = 12u };
    uint32_t benches = 0u;
    for (uint32_t round = 0u; round < ROUNDS; round++) {
        mc_put(t, &rig, 8, 8, 0, BLOCK_ID_WOODWORKING_BENCH_WEST);
        mc_put(t, &rig, 9, 8, 0, BLOCK_ID_WOODWORKING_BENCH_EAST);
        const explosion_cell_t west = {.x = 8, .y = 8, .z = 0, .block = BLOCK_ID_WOODWORKING_BENCH_WEST, ._pad0 = {0}};
        const explosion_cell_t east = {.x = 9, .y = 8, .z = 0, .block = BLOCK_ID_WOODWORKING_BENCH_EAST, ._pad0 = {0}};
        const uint32_t way = round % 4u;
        explosion->borrowed_cells[0] = way == 1u || way == 3u ? east : west;
        explosion->borrowed_cells[1] = way == 3u ? west : east;
        explosion->count = way < 2u ? 1u : 2u;
        CHECK_EQ_U64(explosion_finish(explosion, &rig.world.world->edit, drops, MC_POWER), 2u);
        CHECK_EQ_U64(mc_block(&rig, 8, 8, 0), BLOCK_ID_AIR);
        CHECK_EQ_U64(mc_block(&rig, 9, 8, 0), BLOCK_ID_AIR);
        benches++;
        CHECK_EQ_U64(mc_drops_of(drops, ITEM_ID_WOODWORKING_BENCH), benches);
    }
    mc_end(&rig);
}

TEST(what_stood_on_or_hung_from_the_other_half_goes_in_a_blast_too) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 16u));
    explosion_t *const explosion = &rig.entity->explosion;
    item_drop_store_t *const drops = &rig.entity->item_drops;
    mc_put(t, &rig, 8, 8, 0, BLOCK_ID_WOODWORKING_BENCH_WEST);
    mc_put(t, &rig, 9, 8, 0, BLOCK_ID_WOODWORKING_BENCH_EAST);
    mc_put(t, &rig, 9, 8, 1, BLOCK_ID_TORCH);
    mc_put(t, &rig, 10, 8, 0, WALL_TORCH_FACING(BLOCK_FACING_WEST));
    explosion->borrowed_cells[0] =
        (explosion_cell_t){.x = 8, .y = 8, .z = 0, .block = BLOCK_ID_WOODWORKING_BENCH_WEST, ._pad0 = {0}};
    explosion->count = 1u;
    CHECK_EQ_U64(explosion_finish(explosion, &rig.world.world->edit, drops, 1.0f64), 4u);
    CHECK_EQ_U64(mc_block(&rig, 9, 8, 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_block(&rig, 10, 8, 0), BLOCK_ID_AIR);
    CHECK_EQ_U64(mc_drops_of(drops, ITEM_ID_WOODWORKING_BENCH), 1u);
    CHECK_EQ_U64(mc_drops_of(drops, ITEM_ID_TORCH), 2u);
    mc_end(&rig);
}

/* The real rays, from all sorts of places, whichever cells they reach and however many blocks they find. */
TEST(no_real_blast_leaves_half_a_bench_and_each_bench_gone_gives_one) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 17u));
    item_drop_store_t *const drops = &rig.entity->item_drops;
    uint32_t taken = 0u, stood = 0u;
    for (uint32_t round = 0u; round < 40u; round++) {
        mc_put(t, &rig, 8, 8, 1, BLOCK_ID_WOODWORKING_BENCH_WEST);
        mc_put(t, &rig, 9, 8, 1, BLOCK_ID_WOODWORKING_BENCH_EAST);
        drops->count = 0u;
        /* From beside the bench, a little further off each round, so that some reach one half and some do not. */
        const float64_t center[COLLISION_AXES] = {8.5f64 - (float64_t)(round % 8u) * 0.75f64, 8.5f64, 1.5f64};
        mc_blast(&rig, center, MC_POWER);
        const uint16_t west = mc_block(&rig, 8, 8, 1), east = mc_block(&rig, 9, 8, 1);
        CHECK((west == BLOCK_ID_AIR) == (east == BLOCK_ID_AIR));
        taken += west == BLOCK_ID_AIR ? 1u : 0u;
        stood += west == BLOCK_ID_AIR ? 0u : 1u;
        CHECK_EQ_U64(mc_drops_of(drops, ITEM_ID_WOODWORKING_BENCH), west == BLOCK_ID_AIR ? 1u : 0u);
        if (west != BLOCK_ID_AIR) {
            mc_put(t, &rig, 8, 8, 1, BLOCK_ID_AIR);
            mc_put(t, &rig, 9, 8, 1, BLOCK_ID_AIR);
        }
    }
    CHECK(taken > 0u);
    printf("  %u benches gone, %u stood\n", taken, stood);
    mc_end(&rig);
}

/* Which column is the last loaded is said by TEST_WORLD_RADIUS: a chunk's square up to 64. */
TEST(with_its_other_half_not_loaded_a_listed_half_stands) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 18u));
    explosion_t *const explosion = &rig.entity->explosion;
    item_drop_store_t *const drops = &rig.entity->item_drops;
    mc_put(t, &rig, 63, 8, 0, BLOCK_ID_WOODWORKING_BENCH_WEST);
    uint16_t block = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(rig.world.map, 64, 8, 0, &block) == WORLD_MAP_STATUS_NOT_LOADED);
    explosion->borrowed_cells[0] =
        (explosion_cell_t){.x = 63, .y = 8, .z = 0, .block = BLOCK_ID_WOODWORKING_BENCH_WEST, ._pad0 = {0}};
    explosion->count = 1u;
    CHECK_EQ_U64(explosion_finish(explosion, &rig.world.world->edit, drops, 1.0f64), 0u);
    CHECK_EQ_U64(mc_block(&rig, 63, 8, 0), BLOCK_ID_WOODWORKING_BENCH_WEST);
    CHECK_EQ_U64(mc_drops_of(drops, ITEM_ID_WOODWORKING_BENCH), 0u);
    mc_end(&rig);
}

/* At a real blast's power, where one block in three gives what it gives: a torch on a wall still gives its own, whichever order the list has it. */
TEST(a_torch_on_a_wall_gives_a_torch_at_any_power_on_the_list_or_not) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 14u));
    explosion_t *const explosion = &rig.entity->explosion;
    item_drop_store_t *const drops = &rig.entity->item_drops;
    enum : uint32_t { ROUNDS = 16u };
    uint32_t torches = 0u;
    for (uint32_t round = 0u; round < ROUNDS; round++) {
        mc_put(t, &rig, 8, 8, 0, BLOCK_ID_DIRT);
        mc_put(t, &rig, 9, 8, 0, WALL_TORCH_FACING(BLOCK_FACING_WEST));
        mc_put(t, &rig, 7, 8, 0, WALL_TORCH_FACING(BLOCK_FACING_EAST));
        mc_put(t, &rig, 8, 9, 0, WALL_TORCH_FACING(BLOCK_FACING_SOUTH));
        mc_put(t, &rig, 8, 7, 0, WALL_TORCH_FACING(BLOCK_FACING_NORTH));
        /* One taken by the blast itself, ahead of the dirt. The other three go because the dirt did. */
        explosion->borrowed_cells[0] =
            (explosion_cell_t){.x = 9, .y = 8, .z = 0, .block = WALL_TORCH_FACING(BLOCK_FACING_WEST), ._pad0 = {0}};
        explosion->borrowed_cells[1] = (explosion_cell_t){.x = 8, .y = 8, .z = 0, .block = BLOCK_ID_DIRT, ._pad0 = {0}};
        explosion->count = 2u;
        CHECK_EQ_U64(explosion_finish(explosion, &rig.world.world->edit, drops, MC_POWER), 5u);
        for (uint32_t i = 0u; i < drops->count; i++) {
            const item_drop_t *const stack = &drops->borrowed_items[i];
            torches += strcmp(block_drop_items[stack->item].name, "torch") == 0 ? stack->count : 0u;
        }
        drops->count = 0u;
    }
    CHECK_EQ_U64(torches, 4u * ROUNDS);
    mc_end(&rig);
}

TEST(one_block_in_power_gives_what_it_gives) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 12u));
    item_drop_store_t *const drops = &rig.entity->item_drops;
    for (uint32_t i = 0u; i < 200u; i++) {
        test_world_fill(t, &rig.world, 4, 4, -4, 12, 12, 4, BLOCK_ID_DIRT);
        const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
        mc_blast(&rig, center, MC_POWER);
        /* Taken back, or the next blast would have them. */
        drops->count = 0u;
    }
    const uint64_t broken = rig.entity->explosion.broken_count;
    /* Dirt gives one of itself. A third of the blocks, within what chance allows at this many. */
    CHECK(broken > 4000u);
    CHECK_NEAR((float64_t)drops->dropped_count / (float64_t)broken, 1.0f64 / 3.0f64, 0.03f64);
    mc_end(&rig);
}

TEST(a_drop_comes_of_what_broke_with_no_tool_asked_for) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 13u));
    mc_put(t, &rig, 8, 8, 0, BLOCK_ID_STONE);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    /* At 1 no ray can pay the 1.89 stone takes. */
    explosion_begin(&rig.entity->explosion, rig.world.map, &rig.entity->store, &rig.entity->item_drops, center, 1.0f64);
    CHECK_EQ_U64(rig.entity->explosion.count, 0u);
    explosion_begin(&rig.entity->explosion, rig.world.map, &rig.entity->store, &rig.entity->item_drops, center,
                    MC_POWER);
    REQUIRE(rig.entity->explosion.count == 1u);
    /* Stone wants a pickaxe of a hand. Here a power of 1 says every block gives, and there is no hand. */
    (void)explosion_finish(&rig.entity->explosion, &rig.world.world->edit, &rig.entity->item_drops, 1.0f64);
    REQUIRE(rig.entity->item_drops.count == 1u);
    CHECK_EQ_U64(rig.entity->item_drops.borrowed_items[0].item,
                 block_drops[block_defs[BLOCK_ID_STONE].first_drop].item);
    mc_end(&rig);
}

/* ---- The store and the items --------------------------------------------------------------------------------------- */

TEST(a_blast_hurts_and_pushes_bodies_by_the_sum) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 14u));
    mc_floor(t, &rig);
    const entity_handle_t near = mc_body(&rig, MOB_ID_ZOMBIE, MC_X + 3.0f64, MC_Y, 0.0f64);
    const entity_handle_t right_by = mc_body(&rig, MOB_ID_ZOMBIE, MC_X + 0.5f64, MC_Y, 0.0f64);
    const entity_handle_t far = mc_body(&rig, MOB_ID_ZOMBIE, MC_X + 20.0f64, MC_Y, 0.0f64);
    const entity_handle_t waiting = mc_body(&rig, MOB_ID_ZOMBIE, MC_X - 2.0f64, MC_Y, 0.0f64);
    entity_store_t *const store = &rig.entity->store;
    store->borrowed_flags[mc_index(t, &rig, waiting)] |= ENTITY_STORE_FLAG_WAITING;
    const float32_t full = mob_defs[MOB_ID_ZOMBIE].health;
    const float64_t center[3] = {MC_X, MC_Y, 0.0f64};
    mc_blast(&rig, center, MC_POWER);

    /* 3 of a reach of 6 in the open: impact 0.5, and so 16. */
    uint32_t i = mc_index(t, &rig, near);
    CHECK_NEAR(store->borrowed_health[i], full - (float32_t)mc_paper_damage(0.5f64), 1.0e-5f32);
    const float64_t eye = (float64_t)mob_defs[MOB_ID_ZOMBIE].height * EXPLOSION_EYE_SHARE;
    const float64_t length = sqrt(9.0f64 + eye * eye);
    CHECK_NEAR(store->borrowed_velocity[0][i], 0.5f64 * 3.0f64 / length, 1.0e-6f64);
    CHECK_NEAR(store->borrowed_velocity[2][i], 0.5f64 * eye / length, 1.0e-6f64);

    /* Right by it, more than it can take: it stops at 0 and stays for whoever owns it. */
    i = mc_index(t, &rig, right_by);
    CHECK_NEAR(store->borrowed_health[i], 0.0f32, 1.0e-6f32);

    i = mc_index(t, &rig, far);
    CHECK_NEAR(store->borrowed_health[i], full, 1.0e-6f32);
    CHECK_NEAR(store->borrowed_velocity[0][i], 0.0f64, MC_EXACT);
    i = mc_index(t, &rig, waiting);
    CHECK_NEAR(store->borrowed_health[i], full, 1.0e-6f32);
    CHECK_NEAR(store->borrowed_velocity[0][i], 0.0f64, MC_EXACT);
    mc_end(&rig);
}

/* Items, and what became of them: the store's sums must still add up. */
static void mc_check_sums(test_context_t *t, const item_drop_store_t *drops) {
    CHECK_EQ_U64(drops->dropped_count, drops->picked_up_count + drops->despawned_count + drops->destroyed_count +
                                           drops->lost_count + item_drop_on_ground(drops));
}

TEST(a_blast_ends_items_near_it_flings_those_further_and_spares_its_own) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 15u));
    /* What the floor does not give, this test can count: it gets one dirt from a block off to the side. */
    mc_fill_loaded(t, &rig, -2, -1, BLOCK_ID_OBSIDIAN);
    mc_put(t, &rig, 8, 7, 0, BLOCK_ID_DIRT);
    item_drop_store_t *const drops = &rig.entity->item_drops;
    const uint16_t row = item_stack_drop_row(ITEM_ID_GUNPOWDER);
    const float64_t places[3] = {MC_X + 1.0f64, MC_X + 5.5f64, MC_X + 20.0f64};
    for (uint32_t i = 0u; i < 3u; i++) {
        const float64_t place[3] = {places[i], MC_Y, 0.5f64};
        item_drop_spawn(drops, row, 3u - i, place);
        /* It scatters as it comes, and this wants it where it was put, at rest. */
        memcpy(drops->borrowed_items[i].position, place, sizeof(place));
        memset(drops->borrowed_items[i].velocity, 0, sizeof(drops->borrowed_items[i].velocity));
    }
    REQUIRE(drops->count == 3u);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    explosion_begin(&rig.entity->explosion, rig.world.map, &rig.entity->store, drops, center, MC_POWER);
    /* At 1 it is gone, at 5.5 it takes 2 and flies, and at 20 it is out of reach. */
    CHECK_EQ_U64(drops->destroyed_count, 3u);
    CHECK_EQ_U64(drops->count, 2u);
    mc_check_sums(t, drops);
    bool flew = false, stayed = false;
    for (uint32_t i = 0u; i < drops->count; i++) {
        const item_drop_t *const item = &drops->borrowed_items[i];
        flew = flew || (item->count == 2u && item->velocity[0] > 0.0f64);
        stayed = stayed || (item->count == 1u && item->velocity[0] == 0.0f64);
    }
    CHECK(flew);
    CHECK(stayed);
    /* What comes out now, with a power that always gives, is safe from the blast it came of. */
    CHECK_EQ_U64(explosion_finish(&rig.entity->explosion, &rig.world.world->edit, drops, 1.0f64), 1u);
    CHECK_EQ_U64(drops->destroyed_count, 3u);
    CHECK_EQ_U64(drops->count, 3u);
    mc_check_sums(t, drops);
    mc_end(&rig);
}

/* ---- The creeper --------------------------------------------------------------------------------------------------- */

TEST(the_table_says_what_a_creeper_is) {
    const mob_def_t *const def = &mob_defs[MOB_ID_CREEPER];
    CHECK_NEAR(def->health, 20.0f32, 1.0e-6f32);
    CHECK_EQ_U64(def->drop_count, 1u);
    const mob_drop_t *const drop = &mob_drops[def->first_drop];
    CHECK_EQ_U64(drop->item, ITEM_ID_GUNPOWDER);
    CHECK_EQ_U64(drop->count_min, 0u);
    CHECK_EQ_U64(drop->count_max, 2u);
}

TEST(it_does_not_notice_past_its_reach) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 20u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 17.0f64, MC_Y, 0.0f64);
    /* It is held where it is, as a stroll would bring it in reach. Long enough that 1 in 10 a tick would be sure. */
    for (uint32_t i = 0u; i < 300u; i++) {
        mob_creeper_tick(&rig.mob->creeper, &rig.scene);
        CHECK(mc_mind(&rig, handle)->state != MOB_CREEPER_STATE_CHASING);
    }
    mc_end(&rig);
}

TEST(it_notices_a_player_in_sight_at_about_one_in_ten_a_tick) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 21u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 15.0f64, MC_Y, 0.0f64);
    mob_creeper_body_t *const mind = &rig.mob->creeper.borrowed_bodies[entity_handle_slot(handle)];
    enum : uint32_t { MC_NOTICES = 400u };
    uint64_t total = 0u;
    /* The body does not move here, so the same place each time, and each try starts with it not having noticed. */
    for (uint32_t run = 0u; run < MC_NOTICES; run++) {
        *mind = (mob_creeper_body_t){.owner = handle};
        uint32_t ticks = 0u;
        do {
            mob_creeper_tick(&rig.mob->creeper, &rig.scene);
            ticks++;
        } while (mind->state != MOB_CREEPER_STATE_CHASING && ticks < 1000u);
        REQUIRE(ticks < 1000u);
        total += ticks;
    }
    CHECK_NEAR((float64_t)total / (float64_t)MC_NOTICES, 10.0f64, 2.0f64);
    mc_end(&rig);
}

TEST(it_does_not_notice_through_a_wall_and_gives_up_when_it_loses_sight) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 22u));
    mc_floor(t, &rig);
    /* A wall across the whole world, too high to jump. */
    test_world_fill(t, &rig.world, 12, MC_FLOOR_MIN, 0, 12, MC_FLOOR_MAX, 30, BLOCK_ID_OBSIDIAN);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, 16.5f64, MC_Y, 0.0f64);
    /* Held where it is, in reach. Long enough that 1 in 10 a tick would be sure. */
    for (uint32_t i = 0u; i < 300u; i++) {
        mob_creeper_tick(&rig.mob->creeper, &rig.scene);
        CHECK(mc_mind(&rig, handle)->state != MOB_CREEPER_STATE_CHASING);
    }
    /* On this side of the wall, and it notices. */
    mc_player_at(&rig, MC_X, MC_Y + 6.0f64, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    /* Behind the wall again: it holds on for the 60 ticks and then lets go. */
    mc_player_at(&rig, 13.5f64, MC_Y, 0.0f64);
    mc_ticks(&rig, MOB_CREEPER_UNSEEN_TICKS);
    CHECK_EQ_U64(mc_mind(&rig, handle)->state, MOB_CREEPER_STATE_CHASING);
    mc_ticks(&rig, 1u);
    CHECK(mc_mind(&rig, handle)->state != MOB_CREEPER_STATE_CHASING);
    mc_end(&rig);
}

TEST(it_walks_to_the_player_and_around_what_is_in_the_way) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 23u));
    mc_floor(t, &rig);
    /* A trench too wide to jump and with no floor, which it can see over and must walk around, by a bridge at y = 2 and 3. */
    test_world_fill(t, &rig.world, 12, MC_FLOOR_MIN, -2, 14, MC_FLOOR_MAX, -1, BLOCK_ID_AIR);
    test_world_fill(t, &rig.world, 12, 2, -2, 14, 3, -1, BLOCK_ID_STONE);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, 17.5f64, MC_Y, 0.0f64);
    entity_store_t *const store = &rig.entity->store;
    bool crossed_on_bridge = false;
    uint32_t tick = 0u;
    for (; tick < 1200u && mc_mind(&rig, handle)->fuse == 0u; tick++) {
        mc_tick(&rig);
        const uint32_t i = mc_index(t, &rig, handle);
        const float64_t x = store->borrowed_position[0][i], y = store->borrowed_position[1][i];
        /* Its feet never drop below the floor: it did not fall in. */
        CHECK(store->borrowed_position[2][i] > -1.0f64);
        crossed_on_bridge = crossed_on_bridge || (x > 12.0f64 && x < 15.0f64 && y > 2.0f64 && y < 4.0f64);
    }
    CHECK(mc_mind(&rig, handle)->fuse != 0u);
    CHECK(crossed_on_bridge);
    /* To within 3 of the player the straight way is 6 blocks, 48 ticks at 0.125, and by the bridge at least 10.9: 87 ticks on its own legs. */
    CHECK(tick > 80u);
    mc_end(&rig);
}

/* A creeper lit at 2 blocks from a player standing there. Returns ticks until it burst, 0 if it did not. */
static uint32_t mc_run_to_burst(mc_rig_t *rig, entity_handle_t handle, uint32_t limit) {
    for (uint32_t i = 1u; i <= limit; i++) {
        mc_tick(rig);
        if (!mc_alive(rig, handle)) {
            return i;
        }
    }
    return 0u;
}

TEST(the_fuse_takes_thirty_ticks_holds_it_still_and_swells_it) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 24u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_ticks(&rig, 2u);
    mc_player_at(&rig, MC_X + 2.0f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    /* The tick it noticed it also lit. From here it counts each tick. */
    const mob_creeper_body_t *const mind = mc_mind(&rig, handle);
    CHECK_EQ_U64(mind->lit, 1u);
    CHECK_EQ_U64(mind->fuse, 1u);
    entity_store_t *const store = &rig.entity->store;
    uint32_t index = mc_index(t, &rig, handle);
    CHECK_NEAR(store->borrowed_swell[index], 1.0f32 / 30.0f32, 1.0e-6f32);
    const float64_t held_x = store->borrowed_position[0][index];
    for (uint32_t fuse = 2u; fuse < MOB_CREEPER_FUSE_TICKS; fuse++) {
        mc_tick(&rig);
        REQUIRE(mc_alive(&rig, handle));
        index = mc_index(t, &rig, handle);
        CHECK_EQ_U64(mind->fuse, fuse);
        CHECK_NEAR(store->borrowed_swell[index], (float32_t)fuse / 30.0f32, 1.0e-6f32);
        CHECK_NEAR(store->borrowed_wish[0][index], 0.0f64, MC_EXACT);
        CHECK_NEAR(store->borrowed_wish[1][index], 0.0f64, MC_EXACT);
    }
    /* It has not gone anywhere, and it looks at the player: straight along its x. */
    CHECK_NEAR(store->borrowed_position[0][index], held_x, 1.0e-3f64);
    CHECK_NEAR(store->borrowed_head_yaw[index], 0.0f32, 0.01f32);
    CHECK(mc_alive(&rig, handle));
    mc_tick(&rig);
    CHECK(!mc_alive(&rig, handle));
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 1u);
    mc_end(&rig);
}

TEST(the_fuse_runs_back_when_the_player_gets_away_or_out_of_sight_or_dies) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 25u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 2.0f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    mc_ticks(&rig, 9u);
    const mob_creeper_body_t *const mind = mc_mind(&rig, handle);
    REQUIRE(mind->fuse == 10u);

    /* Between 3 and 7 it goes on. */
    uint32_t index = mc_index(t, &rig, handle);
    mc_player_at(&rig, rig.entity->store.borrowed_position[0][index] + 6.5f64, MC_Y, 0.0f64);
    mc_ticks(&rig, 2u);
    CHECK_EQ_U64(mind->fuse, 12u);

    /* Past 7 it runs back, a tick at a time, with the swell after it. */
    index = mc_index(t, &rig, handle);
    mc_player_at(&rig, rig.entity->store.borrowed_position[0][index] + 7.5f64, MC_Y, 0.0f64);
    mc_ticks(&rig, 2u);
    CHECK_EQ_U64(mind->fuse, 10u);
    CHECK_EQ_U64(mind->lit, 0u);
    index = mc_index(t, &rig, handle);
    CHECK_NEAR(rig.entity->store.borrowed_swell[index], 10.0f32 / 30.0f32, 1.0e-6f32);

    /* And between 3 and 7 now, it is not lit again: only nearer than 3 does that. */
    mc_ticks(&rig, 1u);
    const uint8_t fuse_before = mind->fuse;
    index = mc_index(t, &rig, handle);
    mc_player_at(&rig, rig.entity->store.borrowed_position[0][index] + 5.0f64, MC_Y, 0.0f64);
    mc_ticks(&rig, 1u);
    CHECK_EQ_U64(mind->lit, 0u);
    CHECK_EQ_U64(mind->fuse, (uint64_t)fuse_before - 1u);

    /* Near again, and lit from where it had got to. */
    index = mc_index(t, &rig, handle);
    mc_player_at(&rig, rig.entity->store.borrowed_position[0][index] + 2.0f64, MC_Y, 0.0f64);
    mc_ticks(&rig, 1u);
    CHECK_EQ_U64(mind->lit, 1u);

    /* A wall between them puts it out. */
    index = mc_index(t, &rig, handle);
    const int32_t wall = (int32_t)floor(rig.entity->store.borrowed_position[0][index]) + 1;
    test_world_fill(t, &rig.world, wall, MC_FLOOR_MIN, 0, wall, MC_FLOOR_MAX, 30, BLOCK_ID_OBSIDIAN);
    mc_ticks(&rig, 1u);
    CHECK_EQ_U64(mind->lit, 0u);
    test_world_fill(t, &rig.world, wall, MC_FLOOR_MIN, 0, wall, MC_FLOOR_MAX, 30, BLOCK_ID_AIR);
    mc_ticks(&rig, 1u);
    CHECK(mc_mind(&rig, handle)->state == MOB_CREEPER_STATE_CHASING);
    CHECK_EQ_U64(mind->lit, 1u);

    /* So does the player's dying: and no blast comes of it, however long it waits. */
    rig.survival->state = PLAYER_SURVIVAL_DEAD;
    mc_ticks(&rig, 200u);
    CHECK(mc_alive(&rig, handle));
    CHECK_EQ_U64(mind->fuse, 0u);
    CHECK(mind->state != MOB_CREEPER_STATE_CHASING);
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 0u);
    mc_end(&rig);
}

TEST(a_burst_breaks_the_ground_hurts_the_player_and_leaves_no_gunpowder) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 26u));
    /* Dirt, which a blast has its way with. */
    mc_fill_loaded(t, &rig, -3, -1, BLOCK_ID_DIRT);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 2.0f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    const uint32_t index = mc_index(t, &rig, handle);
    const float64_t held_x = rig.entity->store.borrowed_position[0][index];
    const float64_t held_y = rig.entity->store.borrowed_position[1][index];
    mc_ticks(&rig, MOB_CREEPER_FUSE_TICKS - 2u);
    REQUIRE(mc_alive(&rig, handle));
    /* The player steps back on the last tick, to 5: past where it lights and short of where it goes out. */
    mc_player_at(&rig, held_x + 5.0f64, held_y, 0.0f64);
    REQUIRE(mc_run_to_burst(&rig, handle, 1u) != 0u);
    /* All of it in the open: impact 1/6, so (1/36 + 1/6) / 2 * 42 + 1 = 5.08, which is 5. */
    CHECK_NEAR(rig.survival->health, (float32_t)PLAYER_SURVIVAL_HEALTH_MAX - 5.0f32, 1.0e-4f32);
    CHECK_EQ_U64(rig.survival->hurt_count, 1u);
    CHECK_NEAR(rig.movement->velocity[0], 1.0f64 / 6.0f64 * 5.0f64 / sqrt(25.0f64 + 1.62f64 * 1.62f64), 1.0e-3f64);
    CHECK(rig.movement->velocity[2] > 0.0f64);
    CHECK_EQ_U64(rig.survival->state, PLAYER_SURVIVAL_ALIVE);
    /* Where it stood is a hole. */
    CHECK_EQ_U64(mc_block(&rig, (int32_t)floor(held_x), (int32_t)floor(held_y), -1), BLOCK_ID_AIR);
    CHECK(rig.entity->explosion.broken_count > 5u);
    /* A burst leaves nothing of itself, whatever the dirt does. */
    CHECK_EQ_U64(mc_gunpowder(&rig), 0u);
    CHECK_EQ_U64(rig.mob->creeper.dropped_count, 0u);
    CHECK_EQ_U64(rig.mob->creeper.died_count, 0u);
    mc_end(&rig);
}

TEST(a_burst_at_arm_s_length_kills_a_bare_player) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 27u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 1.5f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    CHECK(mc_run_to_burst(&rig, handle, 40u) != 0u);
    /* Impact 3/4 in the open: 28 and more against 20. */
    CHECK_EQ_U64(rig.survival->state, PLAYER_SURVIVAL_DEAD);
    CHECK_EQ_U64(rig.survival->killed_by, PLAYER_SURVIVAL_CAUSE_ATTACK);
    mc_end(&rig);
}

TEST(armor_counts_against_a_burst) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 28u));
    mc_floor(t, &rig);
    player_survival_armor_set(rig.survival, 20.0f32, 0.0f32);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 2.0f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    const uint32_t index = mc_index(t, &rig, handle);
    const float64_t held_x = rig.entity->store.borrowed_position[0][index];
    REQUIRE(mc_run_to_burst(&rig, handle, 40u) != 0u);
    /* From a distance of 2: impact 2/3, and 24 on paper. */
    const float32_t raw = (float32_t)mc_paper_damage(1.0f64 - (rig.movement->position[0] - held_x) / 6.0f64);
    CHECK_NEAR(rig.survival->health,
               (float32_t)PLAYER_SURVIVAL_HEALTH_MAX - player_survival_after_armor(raw, 20.0f32, 0.0f32), 1.0e-4f32);
    mc_end(&rig);
}

TEST(a_killed_creeper_leaves_gunpowder_and_does_not_burst) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 29u));
    mc_floor(t, &rig);
    entity_store_t *const store = &rig.entity->store;
    uint32_t counts[3] = {0u, 0u, 0u};
    enum : uint32_t { MC_KILLS = 900u };
    for (uint32_t i = 0u; i < MC_KILLS; i++) {
        const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
        store->borrowed_health[mc_index(t, &rig, handle)] = 0.0f32;
        const uint64_t before = rig.mob->creeper.dropped_count;
        rig.entity->item_drops.count = 0u;
        mob_creeper_tick(&rig.mob->creeper, &rig.scene);
        CHECK(!mc_alive(&rig, handle));
        const uint64_t left = rig.mob->creeper.dropped_count - before;
        REQUIRE(left <= 2u);
        counts[left]++;
        CHECK_EQ_U64(mc_gunpowder(&rig), left);
    }
    CHECK_EQ_U64(rig.mob->creeper.died_count, MC_KILLS);
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 0u);
    CHECK_EQ_U64(rig.entity->explosion.blast_count, 0u);
    /* Evenly 0, 1 or 2, within what chance allows at this many. */
    CHECK(counts[0] > 220u && counts[0] < 380u);
    CHECK(counts[1] > 220u && counts[1] < 380u);
    CHECK(counts[2] > 220u && counts[2] < 380u);
    mc_end(&rig);
}

TEST(a_creeper_killed_by_another_s_blast_leaves_its_drops_and_others_are_only_hurt) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 30u));
    mc_floor(t, &rig);
    entity_store_t *const store = &rig.entity->store;
    const entity_handle_t burster = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    const entity_handle_t neighbour = mc_body(&rig, MOB_ID_CREEPER, MC_X - 0.8f64, MC_Y, 0.0f64);
    const entity_handle_t zombie = mc_body(&rig, MOB_ID_ZOMBIE, MC_X - 0.8f64, MC_Y + 1.0f64, 0.0f64);
    mc_player_at(&rig, MC_X + 1.5f64, MC_Y, 0.0f64);
    /* One tick from bursting, by hand, and the neighbour has only just met the player. */
    rig.mob->creeper.borrowed_bodies[entity_handle_slot(burster)] = (mob_creeper_body_t){
        .owner = burster,
        .state = MOB_CREEPER_STATE_CHASING,
        .fuse = MOB_CREEPER_FUSE_TICKS - 1u,
        .lit = 1u,
    };
    mob_creeper_tick(&rig.mob->creeper, &rig.scene);
    CHECK(!mc_alive(&rig, burster));
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 1u);
    /* The neighbour is dying, and goes on its next turn, leaving what it leaves. */
    REQUIRE(mc_alive(&rig, neighbour));
    CHECK_NEAR(store->borrowed_health[mc_index(t, &rig, neighbour)], 0.0f32, 1.0e-6f32);
    mob_creeper_tick(&rig.mob->creeper, &rig.scene);
    CHECK(!mc_alive(&rig, neighbour));
    CHECK_EQ_U64(rig.mob->creeper.died_count, 1u);
    /* A zombie is not this module's: it stays, however hurt, for its own stage. */
    CHECK(mc_alive(&rig, zombie));
    CHECK_NEAR(store->borrowed_health[mc_index(t, &rig, zombie)], 0.0f32, 1.0e-6f32);
    mc_end(&rig);
}

TEST(sixteen_burst_in_a_tick_and_the_rest_the_next) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 31u));
    mc_floor(t, &rig);
    enum : uint32_t { MC_CROWD = 20u };
    entity_handle_t handles[MC_CROWD];
    entity_store_t *const store = &rig.entity->store;
    mc_player_at(&rig, MC_X, MC_Y, 0.0f64);
    /* The player has to live through it, or those left would lose interest. */
    rig.survival->health = 100000.0f32;
    /* In a ring around the player, each one tick from bursting by hand, and able to take the others' blasts. */
    for (uint32_t i = 0u; i < MC_CROWD; i++) {
        const float64_t angle = 2.0f64 * 3.14159265358979f64 * (float64_t)i / (float64_t)MC_CROWD;
        handles[i] = mc_body(&rig, MOB_ID_CREEPER, MC_X + 2.0f64 * cos(angle), MC_Y + 2.0f64 * sin(angle), 0.0f64);
        rig.mob->creeper.borrowed_bodies[entity_handle_slot(handles[i])] = (mob_creeper_body_t){
            .owner = handles[i],
            .state = MOB_CREEPER_STATE_CHASING,
            .fuse = MOB_CREEPER_FUSE_TICKS - 1u,
            .lit = 1u,
        };
        store->borrowed_health[mc_index(t, &rig, handles[i])] = 2000.0f32;
    }
    mob_creeper_tick(&rig.mob->creeper, &rig.scene);
    CHECK_EQ_U64(rig.mob->creeper.burst_count, MOB_CREEPER_BURSTS_PER_TICK);
    CHECK_EQ_U64(store->count, MC_CROWD - MOB_CREEPER_BURSTS_PER_TICK);
    /* Those left have their fuses spent, and go first thing next tick. */
    for (uint32_t i = 0u; i < store->count; i++) {
        const uint32_t slot = entity_handle_slot(store->borrowed_handle[i]);
        CHECK_EQ_U64(rig.mob->creeper.borrowed_bodies[slot].fuse, MOB_CREEPER_FUSE_TICKS);
    }
    mob_creeper_tick(&rig.mob->creeper, &rig.scene);
    CHECK_EQ_U64(rig.mob->creeper.burst_count, MC_CROWD);
    CHECK_EQ_U64(store->count, 0u);
    mc_end(&rig);
}

TEST(with_nobody_about_it_strolls_and_stops_and_stays_on_the_ground) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 32u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    entity_store_t *const store = &rig.entity->store;
    /* No player at all is a thing a scene may say. */
    rig.scene.borrowed_movement = nullptr;
    rig.scene.borrowed_survival = nullptr;
    uint32_t strolling_ticks = 0u, idle_ticks = 0u;
    float64_t furthest = 0.0f64;
    for (uint32_t tick = 0u; tick < 3000u; tick++) {
        mc_tick(&rig);
        const uint32_t i = mc_index(t, &rig, handle);
        const uint8_t state = mc_mind(&rig, handle)->state;
        strolling_ticks += state == MOB_CREEPER_STATE_STROLLING ? 1u : 0u;
        idle_ticks += state == MOB_CREEPER_STATE_IDLE ? 1u : 0u;
        CHECK(state != MOB_CREEPER_STATE_CHASING);
        CHECK_EQ_U64(mc_mind(&rig, handle)->fuse, 0u);
        const float64_t dx = store->borrowed_position[0][i] - MC_X, dy = store->borrowed_position[1][i] - MC_Y;
        const float64_t distance = sqrt(dx * dx + dy * dy);
        furthest = distance > furthest ? distance : furthest;
        CHECK(store->borrowed_position[2][i] >= -1.0e-9f64);
    }
    CHECK(strolling_ticks > 100u);
    CHECK(idle_ticks > 500u);
    CHECK(furthest > 1.0f64);
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 0u);
    mc_end(&rig);
}

TEST(a_body_waiting_for_its_column_holds_its_fuse) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 33u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 2.0f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    mc_ticks(&rig, 3u);
    const uint8_t fuse = mc_mind(&rig, handle)->fuse;
    entity_store_t *const store = &rig.entity->store;
    for (uint32_t i = 0u; i < 100u; i++) {
        store->borrowed_flags[mc_index(t, &rig, handle)] |= ENTITY_STORE_FLAG_WAITING;
        mob_creeper_tick(&rig.mob->creeper, &rig.scene);
    }
    CHECK(mc_alive(&rig, handle));
    CHECK_EQ_U64(mc_mind(&rig, handle)->fuse, fuse);
    mc_end(&rig);
}

TEST(a_slot_used_again_starts_over) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 34u));
    mc_floor(t, &rig);
    entity_store_t *const store = &rig.entity->store;
    const entity_handle_t first = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 2.0f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, first));
    mc_ticks(&rig, 3u);
    REQUIRE(mc_mind(&rig, first)->fuse > 0u);
    const uint32_t slot = entity_handle_slot(first);
    REQUIRE(entity_store_remove(store, first));
    /* Round the ring of free slots, until this one is back. */
    entity_handle_t again = ENTITY_HANDLE_NONE;
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY && again == ENTITY_HANDLE_NONE; i++) {
        const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X + 30.0f64, MC_Y, 0.0f64);
        if (entity_handle_slot(handle) == slot) {
            again = handle;
        } else {
            REQUIRE(entity_store_remove(store, handle));
        }
    }
    REQUIRE(again != ENTITY_HANDLE_NONE);
    CHECK(again != first);
    mob_creeper_tick(&rig.mob->creeper, &rig.scene);
    CHECK_EQ_U64(mc_mind(&rig, again)->owner, again);
    CHECK_EQ_U64(mc_mind(&rig, again)->fuse, 0u);
    CHECK(mc_mind(&rig, again)->state != MOB_CREEPER_STATE_CHASING);
    mc_end(&rig);
}

/* ---- Same bytes, and the cost --------------------------------------------------------------------------------------- */

/* A night's worth of a pack hunting a player who stands there, on dirt, and a sum over everything it comes to. */
static uint64_t mc_run_sum(test_context_t *t, uint64_t seed) {
    mc_rig_t rig;
    if (!mc_begin(t, &rig, seed)) {
        return 0u;
    }
    mc_fill_loaded(t, &rig, -4, -1, BLOCK_ID_DIRT);
    for (uint32_t i = 0u; i < 8u; i++) {
        (void)mc_body(&rig, MOB_ID_CREEPER, MC_X + (float64_t)i * 3.0f64 - 10.0f64, MC_Y - 8.0f64, 0.0f64);
    }
    mc_player_at(&rig, MC_X, MC_Y, 0.0f64);
    mc_ticks(&rig, 800u);
    uint64_t sum = rig.mob->creeper.burst_count + rig.entity->explosion.broken_count * 1000u;
    sum = sum * 31u + rig.entity->item_drops.dropped_count;
    const entity_store_t *const store = &rig.entity->store;
    for (uint32_t i = 0u; i < store->count; i++) {
        for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
            uint64_t bits = 0u;
            memcpy(&bits, &store->borrowed_position[axis][i], sizeof(bits));
            sum = sum * 1099511628211ull + bits;
        }
    }
    for (int32_t x = -20; x <= 40; x++) {
        for (int32_t y = -20; y <= 40; y++) {
            for (int32_t z = -4; z <= 0; z++) {
                sum = sum * 1099511628211ull + mc_block(&rig, x, y, z);
            }
        }
    }
    mc_end(&rig);
    return sum;
}

TEST(the_same_calls_give_the_same_bytes_and_another_seed_others) {
    const uint64_t first = mc_run_sum(t, 40u);
    CHECK_EQ_U64(mc_run_sum(t, 40u), first);
    CHECK(mc_run_sum(t, 41u) != first);
}

TEST(no_heap_no_arena_and_within_the_budget) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 42u));
    mc_fill_loaded(t, &rig, -6, -1, BLOCK_ID_DIRT);
    /* The plan's 100 mobs, all creepers, all in sight. */
    for (uint32_t i = 0u; i < 100u; i++) {
        const float64_t x = MC_X + 4.0f64 * (float64_t)(i % 10u) - 18.0f64;
        const float64_t y = MC_Y + 4.0f64 * (float64_t)(i / 10u) - 18.0f64;
        (void)mc_body(&rig, MOB_ID_CREEPER, x, y, 0.0f64);
    }
    mc_player_at(&rig, MC_X, MC_Y, 0.0f64);
    heap_counters_t before, after;
    heap_counters_read(&before);
    const uint64_t used_entity = rig.entity->arena_world.used, used_mob = rig.mob->arena_world.used;
    const int64_t started = time_now_ns();
    mc_ticks(&rig, 200u);
    const int64_t took = time_now_ns() - started;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(rig.entity->arena_world.used, used_entity);
    CHECK_EQ_U64(rig.mob->arena_world.used, used_mob);
    /* Some of them will have burst, or this says nothing about a blast. */
    CHECK(rig.entity->explosion.blast_count > 0u);
    /* The phase's budget is 5 ms a tick for everything: a floor under it, at the debug flags and the sanitizers. */
    CHECK(took / 200 < 20000000);

    /* One blast by itself, of the strongest a creeper makes, in the ground. */
    test_world_fill(t, &rig.world, 0, 0, -6, 16, 16, 6, BLOCK_ID_DIRT);
    const float64_t center[3] = {MC_X, MC_Y, 0.5f64};
    const int64_t blast_started = time_now_ns();
    mc_blast(&rig, center, MOB_CREEPER_POWER);
    CHECK(time_now_ns() - blast_started < 20000000);
    mc_end(&rig);
}

TEST(a_world_closed_keeps_nothing_and_a_new_one_starts_clean) {
    mc_rig_t rig;
    REQUIRE(mc_begin(t, &rig, 43u));
    mc_floor(t, &rig);
    const entity_handle_t handle = mc_body(&rig, MOB_ID_CREEPER, MC_X, MC_Y, 0.0f64);
    mc_player_at(&rig, MC_X + 2.0f64, MC_Y, 0.0f64);
    REQUIRE(mc_until_chasing(&rig, handle));
    REQUIRE(mc_run_to_burst(&rig, handle, 40u) != 0u);
    mob_close(rig.mob);
    entity_close(rig.entity);
    CHECK_EQ_U64(rig.mob->arena_world.used, 0u);
    CHECK_EQ_U64(rig.entity->arena_world.used, 0u);
    CHECK(rig.mob->creeper.borrowed_bodies == nullptr);
    CHECK(rig.entity->explosion.borrowed_cells == nullptr);
    entity_open(rig.entity, 43u);
    mob_open(rig.mob, rig.world.world, 7u);
    CHECK_EQ_U64(rig.mob->creeper.burst_count, 0u);
    CHECK_EQ_U64(rig.entity->explosion.blast_count, 0u);
    CHECK(rig.mob->arena_world.used <= (uint64_t)MOB_PATH_BYTES + (uint64_t)MOB_ZOMBIE_BYTES +
                                           (uint64_t)MOB_SKELETON_BYTES + (uint64_t)MOB_CREEPER_BYTES +
                                           (uint64_t)MOB_COMBAT_BYTES + (uint64_t)MOB_PASSIVE_BYTES);
    mc_end(&rig);
}

TEST_MAIN()
