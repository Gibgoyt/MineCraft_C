/* ctest run_game_cover. See README.md. */
#include "harness/index.h"

#include "run/climate/fixtures/ground.h"
#include "run/physics/fixtures/test_world.h"

#include "arena/index.h"
#include "climate/index.h"
#include "climate/weather/index.h"
#include "game/cover/index.h"
#include "heap/index.h"
#include "save/world/index.h"
#include "tables/blocks/index.h"
#include "job/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A week the same everywhere, or the same but at sea west of x = 0. */
typedef struct cv_week_t {
    game_cover_week_t week; /* 0 */
    uint8_t sea_west;       /* 12 */
    uint8_t _pad0[3];       /* 13 */
} cv_week_t;
static_assert(sizeof(cv_week_t) == 16);

static void cv_week_fixed(void *context, int32_t x, int32_t y, game_cover_week_t *out) {
    (void)y;
    const cv_week_t *const fixed = context;
    *out = fixed->week;
    out->sea = fixed->sea_west != 0u && x < 0 ? 1u : 0u;
}

static cv_week_t cv_week(float32_t snow_mm, float32_t ice_cm) {
    cv_week_t week;
    memset(&week, 0, sizeof(week));
    week.week.snow_mm = snow_mm;
    week.week.ice_cm = ice_cm;
    week.week.known = 1u;
    return week;
}

/* Every block from one corner to the other, both included, as edits: so the edit log and each column's top know of them. */
static void cv_fill(test_context_t *t, test_world_t *w, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                    int32_t z1, uint16_t block) {
    for (int32_t z = z0; z <= z1; z++) {
        for (int32_t y = y0; y <= y1; y++) {
            for (int32_t x = x0; x <= x1; x++) {
                const world_edit_status_t status = world_edit_block_set(&w->world->edit, x, y, z, block);
                CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
        }
    }
}

static uint16_t cv_block(test_world_t *w, int32_t x, int32_t y, int32_t z) {
    uint16_t block = BLOCK_ID_AIR;
    (void)world_map_block_get(w->map, x, y, z, &block);
    return block;
}

/* The cover rule on every loaded chunk of the test world. */
static game_cover_counts_t cv_cover(test_world_t *w, cv_week_t *week) {
    game_cover_counts_t counts;
    memset(&counts, 0, sizeof(counts));
    const int32_t reach = (int32_t)TEST_WORLD_RADIUS;
    for (int32_t chunk_y = -reach; chunk_y <= reach; chunk_y++) {
        for (int32_t chunk_x = -reach; chunk_x <= reach; chunk_x++) {
            game_cover_column(w->world, cv_week_fixed, week, chunk_x, chunk_y, &counts);
        }
    }
    return counts;
}

/* Grass with its top at z = 0 over the middle, and a lake of sources in it from (2, 2) to (9, 9), its water at z = -2 to 0. */
static bool cv_begin_lake(test_context_t *t, test_world_t *w) {
    if (!test_world_begin(t, w)) {
        return false;
    }
    cv_fill(t, w, -16, -16, -3, 31, 31, -1, BLOCK_ID_DIRT);
    cv_fill(t, w, -16, -16, 0, 31, 31, 0, BLOCK_ID_GRASS_BLOCK);
    cv_fill(t, w, 2, 2, -2, 9, 9, 0, BLOCK_ID_WATER);
    return true;
}

static uint64_t cv_log_now(test_world_t *w) {
    return world_edit_log_position(&w->world->edit);
}

TEST(a_cold_week_freezes_still_water) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    cv_week_t week = cv_week(0.0f32, 6.0f32);
    const uint64_t before = cv_log_now(&w);
    const game_cover_counts_t counts = cv_cover(&w, &week);
    /* Every top of the lake is ice, and nothing else changed: 64 edits, the water under the ice still water. */
    uint32_t frozen = 0u;
    for (int32_t y = 2; y <= 9; y++) {
        for (int32_t x = 2; x <= 9; x++) {
            frozen += cv_block(&w, x, y, 0) == BLOCK_ID_ICE && cv_block(&w, x, y, -1) == BLOCK_ID_WATER ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(frozen, 64u);
    CHECK_EQ_U64(counts.ice_on, 64u);
    CHECK_EQ_U64(cv_log_now(&w) - before, 64u);
    CHECK_EQ_U64(counts.snow_on + counts.snow_off + counts.ice_off, 0u);
    CHECK_EQ_U64(cv_block(&w, 1, 1, 0), BLOCK_ID_GRASS_BLOCK);
    CHECK(counts.columns >= 9u); /* every loaded chunk of the test world, the lake's among them */
    test_world_end(&w);
}

TEST(a_warm_week_melts_it) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    cv_week_t cold = cv_week(0.0f32, 6.0f32);
    (void)cv_cover(&w, &cold);
    /* Between the lines, 3 cm: nothing changes, so the ice that is there stays. */
    cv_week_t between = cv_week(0.0f32, 3.0f32);
    uint64_t before = cv_log_now(&w);
    (void)cv_cover(&w, &between);
    CHECK_EQ_U64(cv_log_now(&w) - before, 0u);
    CHECK_EQ_U64(cv_block(&w, 5, 5, 0), BLOCK_ID_ICE);
    /* 1 cm: every one of them a water source again. */
    cv_week_t warm = cv_week(0.0f32, 1.0f32);
    before = cv_log_now(&w);
    const game_cover_counts_t counts = cv_cover(&w, &warm);
    CHECK_EQ_U64(counts.ice_off, 64u);
    CHECK_EQ_U64(cv_log_now(&w) - before, 64u);
    uint32_t water = 0u;
    for (int32_t y = 2; y <= 9; y++) {
        for (int32_t x = 2; x <= 9; x++) {
            water += cv_block(&w, x, y, 0) == BLOCK_ID_WATER &&
                             world_tick_fluid_level(&w.world->tick, x, y, 0) == WORLD_TICK_LEVEL_SOURCE
                         ? 1u
                         : 0u;
        }
    }
    CHECK_EQ_U64(water, 64u);
    /* Ice a player laid on dry ground, with no water under it, is not melted: it was never a lake's. */
    cv_fill(t, &w, 20, 20, 1, 20, 20, 1, BLOCK_ID_ICE);
    (void)cv_cover(&w, &warm);
    CHECK_EQ_U64(cv_block(&w, 20, 20, 1), BLOCK_ID_ICE);
    test_world_end(&w);
}

TEST(ice_is_idempotent) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    cv_week_t snowy = cv_week(40.0f32, 8.0f32);
    (void)cv_cover(&w, &snowy);
    const uint64_t before = cv_log_now(&w);
    const game_cover_counts_t again = cv_cover(&w, &snowy);
    CHECK_EQ_U64(cv_log_now(&w) - before, 0u);
    CHECK_EQ_U64(again.ice_on + again.ice_off + again.snow_on + again.snow_off, 0u);
    test_world_end(&w);
}

TEST(ice_survives_a_save_and_load) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    cv_week_t cold = cv_week(0.0f32, 6.0f32);
    (void)cv_cover(&w, &cold);
    char directory[64];
    snprintf(directory, sizeof(directory), "/tmp/mc_game_cover_XXXXXX");
    REQUIRE(mkdtemp(directory) != nullptr);
    save_world_t *const save = test_world_aligned(t, sizeof(save_world_t));
    uint8_t *const memory = test_world_aligned(t, SAVE_WORLD_ARENA_BYTES);
    arena_t arena;
    arena_init(&arena, "cover_save", memory, SAVE_WORLD_ARENA_BYTES);
    memset(save, 0, sizeof(*save));
    REQUIRE(save_world_open(save, &arena, directory, 1u) == SAVE_WORLD_OK);
    save_world_levels_from(save, &w.world->tick);
    /* The edits are the save's like any other: an autosave's flush writes them as deltas. */
    CHECK_EQ_U64(save_world_flush(save, &w.world->edit), SAVE_WORLD_OK);
    save_world_close(save);
    arena_reset(&arena);
    memset(save, 0, sizeof(*save));
    REQUIRE(save_world_open(save, &arena, directory, 1u) == SAVE_WORLD_OK);
    /* The lake's chunk as it would generate, all air here, with what is saved over it: the ice is there. */
    uint16_t *const states = test_alloc(t, (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    const column_mask_t sections = save_world_column_overlay(save, 0, 0, states, scratch);
    const uint32_t section = column_section_of(0);
    CHECK(column_mask_has(sections, section));
    uint32_t ice = 0u;
    for (uint32_t y = 2u; y <= 9u; y++) {
        for (uint32_t x = 2u; x <= 9u; x++) {
            const uint64_t at =
                (uint64_t)section * SECTION_VOXELS + section_voxel_index(x, y, column_section_local_of(0));
            ice += states[at] == BLOCK_ID_ICE ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(ice, 64u);
    save_world_close(save);
    arena_deinit(&arena);
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
    test_world_end(&w);
}

TEST(flowing_water_does_not_freeze) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    /* The lake's top row at y = 9 flows: a level of 3 in each of its voxels at z = 0. */
    uint8_t levels[WORLD_TICK_PAGE_BYTES];
    memset(levels, 0, sizeof(levels));
    const uint32_t section = column_section_of(0);
    for (uint32_t x = 2u; x <= 9u; x++) {
        const uint32_t voxel = section_voxel_index(x, 9u, column_section_local_of(0));
        levels[voxel / 2u] = (uint8_t)(levels[voxel / 2u] | (3u << ((voxel & 1u) * 4u)));
    }
    world_tick_section_levels_restore(&w.world->tick, 0, 0, section, levels);
    CHECK_EQ_U64(world_tick_fluid_level(&w.world->tick, 5, 9, 0), 3u);
    cv_week_t cold = cv_week(0.0f32, 20.0f32);
    const game_cover_counts_t counts = cv_cover(&w, &cold);
    CHECK_EQ_U64(counts.ice_on, 56u);
    for (int32_t x = 2; x <= 9; x++) {
        CHECK_EQ_U64(cv_block(&w, x, 9, 0), BLOCK_ID_WATER);
        CHECK_EQ_U64(cv_block(&w, x, 8, 0), BLOCK_ID_ICE);
    }
    test_world_end(&w);
}

TEST(shallow_ice_melts_too) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    /* A shore a block deep round the lake, two blocks wide, and a pond a block deep on its own: water at z = 0 on dirt. */
    cv_fill(t, &w, 0, 0, 0, 11, 11, 0, BLOCK_ID_WATER);
    cv_fill(t, &w, 0, 0, -1, 11, 1, -1, BLOCK_ID_DIRT);
    cv_fill(t, &w, 0, 10, -1, 11, 11, -1, BLOCK_ID_DIRT);
    cv_fill(t, &w, 0, 2, -1, 1, 9, -1, BLOCK_ID_DIRT);
    cv_fill(t, &w, 10, 2, -1, 11, 9, -1, BLOCK_ID_DIRT);
    cv_fill(t, &w, 20, 2, 0, 22, 4, 0, BLOCK_ID_WATER);
    cv_week_t cold = cv_week(0.0f32, 6.0f32);
    game_cover_counts_t counts = cv_cover(&w, &cold);
    CHECK_EQ_U64(counts.ice_on, 144u + 9u);
    /* A warm week thaws all of it in one pass, the shallows as well as the deep, and a second pass changes nothing. */
    cv_week_t warm = cv_week(0.0f32, 1.0f32);
    counts = cv_cover(&w, &warm);
    CHECK_EQ_U64(counts.ice_off, 144u + 9u);
    uint32_t ice = 0u;
    for (int32_t y = 0; y <= 11; y++) {
        for (int32_t x = 0; x <= 22; x++) {
            ice += cv_block(&w, x, y, 0) == BLOCK_ID_ICE ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(ice, 0u);
    CHECK_EQ_U64(world_tick_fluid_level(&w.world->tick, 0, 0, 0), WORLD_TICK_LEVEL_SOURCE);
    const uint64_t before = cv_log_now(&w);
    (void)cv_cover(&w, &warm);
    CHECK_EQ_U64(cv_log_now(&w) - before, 0u);
    test_world_end(&w);
}

TEST(the_sea_freezes_only_in_polar_cold) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    cv_fill(t, &w, -16, -4, -3, 15, 4, -3, BLOCK_ID_SAND);
    cv_fill(t, &w, -16, -4, -2, 15, 4, 0, BLOCK_ID_WATER);
    /* 10 cm: the lake east of x = 0 freezes, the sea west of it does not (Risks R5). 31 cm: both. */
    cv_week_t week = cv_week(0.0f32, 10.0f32);
    week.sea_west = 1u;
    (void)cv_cover(&w, &week);
    CHECK_EQ_U64(cv_block(&w, 4, 0, 0), BLOCK_ID_ICE);
    CHECK_EQ_U64(cv_block(&w, -4, 0, 0), BLOCK_ID_WATER);
    week.week.ice_cm = 31.0f32;
    (void)cv_cover(&w, &week);
    CHECK_EQ_U64(cv_block(&w, -4, 0, 0), BLOCK_ID_ICE);
    test_world_end(&w);
}

TEST(a_column_streamed_in_matches_the_week) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    cv_week_t cold = cv_week(30.0f32, 6.0f32);
    (void)cv_cover(&w, &cold);
    /* The lake's chunk goes and comes back as it generates, all water and grass again, as a column first walked into does. */
    uint32_t slot = 0u;
    REQUIRE(world_map_slot_of(w.map, 0, 0, &slot));
    world_map_unload(w.map, slot);
    test_world_stream_all(t, &w);
    REQUIRE(world_map_column_at(w.map, 0, 0) != nullptr);
    cv_fill(t, &w, 0, 0, -3, 15, 15, -1, BLOCK_ID_DIRT);
    cv_fill(t, &w, 0, 0, 0, 15, 15, 0, BLOCK_ID_GRASS_BLOCK);
    cv_fill(t, &w, 2, 2, -2, 9, 9, 0, BLOCK_ID_WATER);
    CHECK_EQ_U64(cv_block(&w, 5, 5, 0), BLOCK_ID_WATER);
    game_cover_counts_t counts;
    memset(&counts, 0, sizeof(counts));
    game_cover_column(w.world, cv_week_fixed, &cold, 0, 0, &counts);
    /* Frozen and white like its neighbours. */
    CHECK_EQ_U64(cv_block(&w, 5, 5, 0), BLOCK_ID_ICE);
    CHECK_EQ_U64(cv_block(&w, 12, 12, 1), BLOCK_ID_SNOW_LAYER);
    CHECK_EQ_U64(cv_block(&w, 20, 20, 1), BLOCK_ID_SNOW_LAYER);
    /* A chunk not loaded is left alone: the rule never loads one. */
    memset(&counts, 0, sizeof(counts));
    game_cover_column(w.world, cv_week_fixed, &cold, 40, 40, &counts);
    CHECK_EQ_U64(counts.columns, 0u);
    test_world_end(&w);
}

TEST(snow_lies_over_20_mm_and_goes_at_10) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    cv_week_t snowy = cv_week(25.0f32, 0.0f32);
    const game_cover_counts_t on = cv_cover(&w, &snowy);
    CHECK_EQ_U64(cv_block(&w, 0, 0, 1), BLOCK_ID_SNOW_LAYER);
    CHECK_EQ_U64(cv_block(&w, 5, 5, 1), BLOCK_ID_AIR); /* not on water */
    /* 48 by 48 of grass, less the 64 of water. */
    CHECK_EQ_U64(on.snow_on, 48u * 48u - 64u);
    cv_week_t melt = cv_week(10.0f32, 0.0f32);
    const game_cover_counts_t off = cv_cover(&w, &melt);
    CHECK_EQ_U64(off.snow_off, on.snow_on);
    CHECK_EQ_U64(cv_block(&w, 0, 0, 1), BLOCK_ID_AIR);
    test_world_end(&w);
}

TEST(snow_never_on_leaves_or_ice) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    /* A tree's crown, a frozen lake, a lava pool, a slab, a torch and a flower. */
    cv_fill(t, &w, 20, 20, 1, 20, 20, 4, BLOCK_ID_OAK_LOG);
    cv_fill(t, &w, 18, 18, 5, 22, 22, 5, BLOCK_ID_OAK_LEAVES);
    cv_fill(t, &w, -10, -10, 0, -8, -8, 0, BLOCK_ID_LAVA);
    cv_fill(t, &w, 12, -10, 1, 12, -10, 1, BLOCK_ID_OAK_SLAB);
    cv_fill(t, &w, 13, -10, 1, 13, -10, 1, BLOCK_ID_TORCH);
    cv_fill(t, &w, 14, -10, 1, 14, -10, 1, BLOCK_ID_DANDELION);
    cv_fill(t, &w, -14, 10, 1, -14, 10, 1, BLOCK_ID_SNOW_BLOCK);
    cv_fill(t, &w, -13, 10, 1, -13, 10, 1, BLOCK_ID_PACKED_ICE);
    cv_week_t winter = cv_week(60.0f32, 12.0f32);
    (void)cv_cover(&w, &winter);
    for (int32_t y = 18; y <= 22; y++) {
        for (int32_t x = 18; x <= 22; x++) {
            CHECK_EQ_U64(cv_block(&w, x, y, 6), BLOCK_ID_AIR);
        }
    }
    CHECK_EQ_U64(cv_block(&w, 5, 5, 0), BLOCK_ID_ICE); /* frozen this week */
    CHECK_EQ_U64(cv_block(&w, 5, 5, 1), BLOCK_ID_AIR); /* and no layer on its ice */
    CHECK_EQ_U64(cv_block(&w, -9, -9, 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(cv_block(&w, 12, -10, 2), BLOCK_ID_AIR);
    CHECK_EQ_U64(cv_block(&w, 13, -10, 2), BLOCK_ID_AIR);
    CHECK_EQ_U64(cv_block(&w, 14, -10, 2), BLOCK_ID_AIR);
    CHECK_EQ_U64(cv_block(&w, -14, 10, 2), BLOCK_ID_AIR);
    CHECK_EQ_U64(cv_block(&w, -13, 10, 2), BLOCK_ID_AIR);
    CHECK_EQ_U64(cv_block(&w, 16, 16, 1), BLOCK_ID_SNOW_LAYER); /* the open grass beside them */
    test_world_end(&w);
}

TEST(a_roof_keeps_new_snow_off) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    /* A roofed room: walls round (20..26, 20..26), a roof at z = 4. */
    cv_fill(t, &w, 20, 20, 1, 26, 26, 4, BLOCK_ID_COBBLESTONE);
    cv_fill(t, &w, 21, 21, 1, 25, 25, 3, BLOCK_ID_AIR);
    cv_week_t snowy = cv_week(40.0f32, 0.0f32);
    (void)cv_cover(&w, &snowy);
    for (int32_t y = 21; y <= 25; y++) {
        for (int32_t x = 21; x <= 25; x++) {
            CHECK_EQ_U64(cv_block(&w, x, y, 1), BLOCK_ID_AIR);
            CHECK_EQ_U64(cv_block(&w, x, y, 5), BLOCK_ID_SNOW_LAYER); /* on the roof */
        }
    }
    /* Snow that was there before a roof went over it goes when the week thaws: the whole column is looked at. */
    cv_fill(t, &w, 0, 0, 3, 0, 0, 3, BLOCK_ID_COBBLESTONE);
    CHECK_EQ_U64(cv_block(&w, 0, 0, 1), BLOCK_ID_SNOW_LAYER);
    cv_week_t thaw = cv_week(0.0f32, 0.0f32);
    (void)cv_cover(&w, &thaw);
    CHECK_EQ_U64(cv_block(&w, 0, 0, 1), BLOCK_ID_AIR);
    test_world_end(&w);
}

TEST(placing_a_block_replaces_the_layer) {
    /* The layer is thin, walked through, given way to by a block placed on it, has no item and gives a snowball. */
    const block_def_t *const layer = &block_defs[BLOCK_ID_SNOW_LAYER];
    CHECK_EQ_U64(layer->shape, BLOCK_SHAPE_THIN);
    CHECK((layer->flags & BLOCK_FLAG_SOLID) == 0u);
    CHECK((layer->flags & BLOCK_FLAG_REPLACEABLE) != 0u);
    CHECK_EQ_U64(layer->opacity, 0u);
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    cv_week_t snowy = cv_week(40.0f32, 0.0f32);
    (void)cv_cover(&w, &snowy);
    REQUIRE(cv_block(&w, 0, 0, 1) == BLOCK_ID_SNOW_LAYER);
    /* What a place does on a replaceable block: the new block in its cell. Then the next census's snow lies on the stone. */
    CHECK(world_edit_block_set(&w.world->edit, 0, 0, 1, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(cv_block(&w, 0, 0, 1), BLOCK_ID_STONE);
    (void)cv_cover(&w, &snowy);
    CHECK_EQ_U64(cv_block(&w, 0, 0, 2), BLOCK_ID_SNOW_LAYER);
    test_world_end(&w);
}

TEST(the_snow_edge_does_not_flicker) {
    test_world_t w;
    REQUIRE(cv_begin_lake(t, &w));
    static const float32_t weeks[4] = {25.0f32, 15.0f32, 25.0f32, 15.0f32};
    uint64_t changes = 0u;
    for (uint32_t i = 0u; i < 4u; i++) {
        cv_week_t week = cv_week(weeks[i], 0.0f32);
        const uint64_t before = cv_log_now(&w);
        (void)cv_cover(&w, &week);
        changes += i > 0u ? cv_log_now(&w) - before : 0u;
        CHECK_EQ_U64(cv_block(&w, 0, 0, 1), BLOCK_ID_SNOW_LAYER);
    }
    CHECK_EQ_U64(changes, 0u);
    /* A week nobody knows of changes nothing either. */
    cv_week_t unknown = cv_week(0.0f32, 0.0f32);
    unknown.week.known = 0u;
    const uint64_t before = cv_log_now(&w);
    (void)cv_cover(&w, &unknown);
    CHECK_EQ_U64(cv_log_now(&w) - before, 0u);
    test_world_end(&w);
}

TEST(cover_time_at_render_distance_32) {
    /* 65 by 65 columns, as render distance 32 loads, of ground at z = 0 under grass, with a pond in each: a first snow and ice
     * over all of them. Flat ground, laid section by section, as generating 4225 real columns would take this test minutes. */
    world_t *const world = test_world_aligned(t, sizeof(world_t));
    REQUIRE(world_init(world, (world_limits_t){256ull << 20, 8649u}));
    REQUIRE(world_open(world, 46u, 0, 0)); /* a circle round the 65 by 65 square */
    world_map_request_t request;
    uint32_t columns = 0u;
    const uint32_t ground = column_section_of(-1), top = column_section_of(0);
    uint16_t *const states = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        const uint32_t local_x = i & 15u, local_y = (i >> 4) & 15u, local_z = i >> 8;
        const int32_t z = COLUMN_Z_STORAGE_MIN + (int32_t)(top * SECTION_SIZE + local_z);
        const bool pond = local_x >= 4u && local_x < 8u && local_y >= 4u && local_y < 8u;
        states[i] = z > 0 ? BLOCK_ID_AIR : (z == 0 ? (pond ? BLOCK_ID_WATER : BLOCK_ID_GRASS_BLOCK) : BLOCK_ID_DIRT);
    }
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        column_t *const column = world_map_stream_commit(&world->map, &request);
        REQUIRE(column != nullptr);
        for (uint32_t s = 0u; s < ground; s++) {
            section_block_fill(&column->sections[s], &world->section_pool, BLOCK_ID_STONE);
        }
        if (ground != top) {
            section_block_fill(&column->sections[ground], &world->section_pool, BLOCK_ID_DIRT);
        }
        REQUIRE(section_blocks_import(&column->sections[top], &world->section_pool, states));
        world_edit_column_filled(&world->edit, request.slot);
        columns++;
    }
    CHECK(columns >= 4225u);
    cv_week_t first = cv_week(30.0f32, 6.0f32);
    game_cover_counts_t counts;
    memset(&counts, 0, sizeof(counts));
    const int64_t started = time_now_ns();
    for (int32_t chunk_y = -32; chunk_y <= 32; chunk_y++) {
        for (int32_t chunk_x = -32; chunk_x <= 32; chunk_x++) {
            game_cover_column(world, cv_week_fixed, &first, chunk_x, chunk_y, &counts);
        }
    }
    const float64_t seconds = (float64_t)(time_now_ns() - started) * 1e-9f64;
    printf("  cover of %" PRIu64 " columns at a first snow: %.3f s, %" PRIu64 " snow layers and %" PRIu64
           " blocks of ice\n",
           counts.columns, (libc_double_t)seconds, counts.snow_on, counts.ice_on);
    CHECK_EQ_U64(counts.columns, 4225u);
    CHECK_EQ_U64(counts.snow_on, 4225u * 240u);
    CHECK_EQ_U64(counts.ice_on, 4225u * 16u);
    CHECK(seconds < 10.0f64);
    world_close(world);
    world_deinit(world);
}

/* ---- Through the years, with the climate's own weeks ------------------------------------------------------------------- */

static const float64_t CV_Y45 = 19148.936170212766f64; /* 45 degrees north: latitude rises evenly, 23.5 at 10 km */
static const float64_t CV_Y75 = 31914.893617021276f64;
enum : uint32_t {
    CV_WEEKS = 157u, /* three years of censuses from the census of day 364 */
};

typedef struct cv_climate_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
    ground_t *ground;            /* 24 */
    arena_t *arena;              /* 32 */
} cv_climate_t;
static_assert(sizeof(cv_climate_t) == 40);

static bool cv_climate_start(test_context_t *t, cv_climate_t *rig, ground_kind_t kind, int32_t coast, int32_t height) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = test_world_aligned(t, sizeof(job_system_t));
    rig->climate = test_world_aligned(t, sizeof(climate_t));
    rig->ground = test_alloc(t, sizeof(ground_t));
    memset(rig->ground, 0, sizeof(*rig->ground));
    rig->ground->kind = kind;
    rig->ground->coast = coast;
    rig->ground->radius = height;
    rig->arena = test_world_aligned(t, sizeof(arena_t));
    arena_init(rig->arena, "test_cover_year", test_world_aligned(t, 32ull << 20), 32ull << 20);
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, MC_THREADS_WORKERS)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return climate_open_ground(rig->climate, 0x1234567890ABCDEFull, ground_fn, rig->ground);
}

static void cv_climate_stop(cv_climate_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
    arena_deinit(rig->arena);
}

/* A cell's week by week state, from spin-up at the census of day 364, as the census works it out: the days of each week from
 * climate_weather_day, then the weekly update. Into the snow on the ground and the ice at each census, and its day. */
static bool cv_climate_weeks(cv_climate_t *rig, int32_t x, int32_t y, float32_t *out_snow, float32_t *out_ice,
                             uint16_t *out_day) {
    climate_prepare_area(rig->climate, rig->job, x - 7000, y - 7000, x + 7000, y + 7000, nullptr, nullptr);
    climate_week_t state;
    const uint64_t start = 52u;
    if (!climate_week_spinup(rig->climate, x >> 6, y >> 6, start, &state, nullptr)) {
        return false;
    }
    for (uint32_t k = 0u; k < CV_WEEKS; k++) {
        const uint64_t week = start + 1u + k;
        climate_day_t days[CLIMATE_WEEK_DAYS];
        for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
            arena_reset(rig->arena);
            if (!climate_weather_day(rig->climate, x, y, (int64_t)(7u * (week - 1u) + i), &state, rig->arena,
                                     &days[i])) {
                return false;
            }
        }
        climate_week_update(&state, days, &state);
        out_snow[k] = state.snow_mm;
        out_ice[k] = state.ice_cm;
        out_day[k] = (uint16_t)((7u * week) % 365u);
    }
    return true;
}

/* The cover rule at each census with that week's snow and ice, on open grass and a pond: whether a layer lay, and ice. */
static void cv_through_the_weeks(test_context_t *t, const float32_t *snow, const float32_t *ice, uint8_t *out_layer,
                                 uint8_t *out_frozen) {
    test_world_t w;
    if (!CHECK(cv_begin_lake(t, &w))) {
        return;
    }
    for (uint32_t k = 0u; k < CV_WEEKS; k++) {
        cv_week_t week = cv_week(snow[k], ice[k]);
        (void)cv_cover(&w, &week);
        out_layer[k] = cv_block(&w, 0, 0, 1) == BLOCK_ID_SNOW_LAYER ? 1u : 0u;
        out_frozen[k] = cv_block(&w, 5, 5, 0) == BLOCK_ID_ICE ? 1u : 0u;
    }
    test_world_end(&w);
}

TEST(snow_builds_and_melts_at_45_degrees_in_loaded_chunks) {
    /* Land 600 high north of a coast 1200 blocks south of 45 N, as phase 07's own test of the snow there. */
    const int32_t y = (int32_t)CV_Y45, x = 32;
    cv_climate_t rig;
    REQUIRE(cv_climate_start(t, &rig, GROUND_HIGH_COAST_Y, y - 1200, 600));
    float32_t snow[CV_WEEKS], ice[CV_WEEKS];
    uint16_t day[CV_WEEKS];
    REQUIRE(cv_climate_weeks(&rig, x, y, snow, ice, day));
    cv_climate_stop(&rig);
    uint8_t layer[CV_WEEKS], frozen[CV_WEEKS];
    cv_through_the_weeks(t, snow, ice, layer, frozen);
    /* Every year: a layer at some census between days 1 and 60, and none left at day 200 or after it in summer. */
    uint32_t winters = 0u, summers_bare = 0u, summers = 0u;
    for (uint32_t year = 0u; year < 3u; year++) {
        bool white = false, bare = true;
        float32_t most = 0.0f32;
        for (uint32_t k = year * 52u; k < (year + 1u) * 52u + 1u && k < CV_WEEKS; k++) {
            if (day[k] >= 1u && day[k] <= 60u) {
                white = white || layer[k] != 0u;
                most = snow[k] > most ? snow[k] : most;
            }
            if (day[k] >= 200u && day[k] <= 280u) {
                bare = bare && layer[k] == 0u;
                summers += 1u;
            }
        }
        printf("  45 N, year %u: most snow between days 1 and 60 %.1f mm, a layer then %s, bare from day 200 %s\n",
               year + 1u, (libc_double_t)most, white ? "yes" : "no", bare ? "yes" : "no");
        winters += white ? 1u : 0u;
        summers_bare += bare ? 1u : 0u;
    }
    /* Phase 07's Issues found 6: one winter in three at this place stays near 20 mm, so at least two of three must lay snow. */
    CHECK(winters >= 2u);
    CHECK_EQ_U64(summers_bare, 3u);
    CHECK(summers > 0u);
}

TEST(snow_stays_at_75_degrees) {
    const int32_t y = (int32_t)CV_Y75, x = 32;
    cv_climate_t rig;
    REQUIRE(cv_climate_start(t, &rig, GROUND_COAST_Y, y - 64, 0));
    float32_t snow[CV_WEEKS], ice[CV_WEEKS];
    uint16_t day[CV_WEEKS];
    REQUIRE(cv_climate_weeks(&rig, x, y, snow, ice, day));
    cv_climate_stop(&rig);
    uint8_t layer[CV_WEEKS], frozen[CV_WEEKS];
    cv_through_the_weeks(t, snow, ice, layer, frozen);
    uint32_t white = 0u, censuses = 0u;
    float32_t least = INFINITY;
    for (uint32_t k = 52u; k < 104u; k++) {
        white += layer[k] != 0u ? 1u : 0u;
        censuses++;
        least = snow[k] < least ? snow[k] : least;
    }
    printf("  75 N, year 2: a layer at %u of %u censuses, the least snow %.1f mm\n", white, censuses,
           (libc_double_t)least);
    CHECK_EQ_U64(white, censuses);
}

TEST_MAIN()
