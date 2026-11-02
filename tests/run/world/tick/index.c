/* ctest run_world_sim. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    SIM_ARENA_BYTES = 64ull << 20,
    SIM_POOL_BYTES = 32ull << 20,
};
enum : uint32_t {
    SIM_NODES = 1u << 18,
    SIM_PAGES = 1u << 12,
    SIM_LOG = 1u << 16,
    /* The map wants a circle of this radius round chunk (0, 0): every column of the 3 by 3 the tests build on, x and y from -16 to
     * 31, and some more. x = 47 is the last loaded along a row through the middle. */
    SIM_RADIUS = 2u,
    SIM_SETTLE_LIMIT = 20000u,
    /* Enough picks that every voxel of a section gets some, in a few ticks. */
    SIM_RANDOM_FAST = 4096u,
};
enum : int32_t {
    SIM_MIN = -16,
    SIM_END = 32,
    SIM_MID = 8, /* in the middle of chunk (0, 0): a diamond of reach 7 stays in it, along each row */
    SIM_FLOOR_Z = 0,
};

typedef struct sim_t {
    arena_t arena;
    arena_t arena_pool;
    section_pool_t pool;
    world_map_t map;
    world_edit_t edit;
    world_light_t light;
    world_tick_t tick;
    uint8_t _pad0[8];
} sim_t;
static_assert(sizeof(sim_t) == 1408);

/* Everything but the columns. */
static sim_t *sim_start(test_context_t *t, uint32_t nodes, uint32_t pages, uint64_t seed) {
    uint8_t *const raw = test_alloc(t, sizeof(sim_t) + 64u);
    sim_t *const sim = (sim_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    arena_init(&sim->arena, "sim", test_alloc(t, SIM_ARENA_BYTES), SIM_ARENA_BYTES);
    arena_init(&sim->arena_pool, "sim_pool", test_alloc(t, SIM_POOL_BYTES), SIM_POOL_BYTES);
    section_pool_init(&sim->pool, &sim->arena_pool);
    world_map_init(&sim->map, &sim->arena, &sim->pool, SIM_RADIUS, 0, 0);
    world_edit_init(&sim->edit, &sim->arena, &sim->map, SIM_LOG);
    world_light_init(&sim->light, &sim->arena, &sim->edit, WORLD_LIGHT_WAVE_CAPACITY_DEFAULT);
    world_tick_init(&sim->tick, &sim->arena, &sim->edit, nodes, pages);
    world_tick_seed_set(&sim->tick, seed);
    return sim;
}

/* Every column air, and random ticks off: a test that wants them says so. */
static sim_t *sim_begin_sized(test_context_t *t, uint32_t nodes, uint32_t pages, uint64_t seed) {
    sim_t *const sim = sim_start(t, nodes, pages, seed);
    sim->tick.random_per_section = 0u;
    world_map_request_t request;
    while (world_map_stream_take(&sim->map, &request, 1u) == 1u) {
        ASSERT_ALWAYS(world_map_stream_commit(&sim->map, &request) != nullptr);
    }
    for (uint32_t i = 0u; i < sim->map.active_count; i++) {
        CHECK_EQ_U64(world_light_column_added(&sim->light, sim->map.borrowed_active[i]), WORLD_LIGHT_STATUS_OK);
    }
    return sim;
}

static sim_t *sim_begin(test_context_t *t) {
    return sim_begin_sized(t, SIM_NODES, SIM_PAGES, 1u);
}

static void sim_end(sim_t *sim) {
    world_tick_deinit(&sim->tick);
    world_light_deinit(&sim->light);
    world_edit_deinit(&sim->edit);
    world_map_deinit(&sim->map);
    section_pool_deinit(&sim->pool);
    arena_deinit(&sim->arena_pool);
    arena_deinit(&sim->arena);
}

static void sim_set(test_context_t *t, sim_t *sim, int32_t x, int32_t y, int32_t z, uint16_t state) {
    const world_edit_status_t status = world_edit_block_set(&sim->edit, x, y, z, state);
    CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
}

static uint16_t sim_at(test_context_t *t, const sim_t *sim, int32_t x, int32_t y, int32_t z) {
    uint16_t state = BLOCK_NO_ID;
    CHECK_EQ_U64(world_map_block_get(&sim->map, x, y, z, &state), WORLD_MAP_STATUS_OK);
    return state;
}

/* A floor of one block at z = 0, and everything to do with the first floor's log gone by. */
static void sim_floor(test_context_t *t, sim_t *sim, uint16_t state) {
    for (int32_t y = SIM_MIN; y < SIM_END; y++) {
        for (int32_t x = SIM_MIN; x < SIM_END; x++) {
            sim_set(t, sim, x, y, SIM_FLOOR_Z, state);
        }
    }
}

static void sim_run(sim_t *sim, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        world_tick_step(&sim->tick);
        (void)world_light_update(&sim->light);
    }
}

/* With no light passes: for when the pool is too full to give them what they want. */
static void sim_run_only_ticks(sim_t *sim, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        world_tick_step(&sim->tick);
    }
}

/* Until nothing is waiting. Fails the test if that never happens. */
static uint32_t sim_settle(test_context_t *t, sim_t *sim) {
    uint32_t ticks = 0u;
    world_tick_step(&sim->tick); /* so that what is only in the log so far is waiting too */
    (void)world_light_update(&sim->light);
    while (world_tick_pending(&sim->tick) != 0u && ticks < SIM_SETTLE_LIMIT) {
        world_tick_step(&sim->tick);
        (void)world_light_update(&sim->light);
        ticks++;
    }
    CHECK(ticks < SIM_SETTLE_LIMIT);
    return ticks;
}

static uint64_t sim_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

static int32_t sim_abs(int32_t value) {
    return value < 0 ? -value : value;
}

/* Every voxel from z_min to z_max, in the whole map, into a digest. */
static uint64_t sim_digest(test_context_t *t, const sim_t *sim, int32_t z_min, int32_t z_max) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (int32_t z = z_min; z <= z_max; z++) {
        for (int32_t y = SIM_MIN; y < SIM_END; y++) {
            for (int32_t x = SIM_MIN; x < SIM_END; x++) {
                hash = (hash ^ sim_at(t, sim, x, y, z)) * 0x00000100000001B3ull;
                hash = (hash ^ world_tick_fluid_level(&sim->tick, x, y, z)) * 0x00000100000001B3ull;
            }
        }
    }
    return hash;
}

/* A wall round the floor's edge, with no way off it: what runs off a floor falls to the bottom of the world, a way down changes
 * which way water goes, and a neighbour not loaded keeps water trying. */
static void sim_ring(test_context_t *t, sim_t *sim, int32_t z_top) {
    for (int32_t z = 1; z <= z_top; z++) {
        for (int32_t along = SIM_MIN; along < SIM_END; along++) {
            sim_set(t, sim, along, SIM_MIN, z, BLOCK_ID_STONE);
            sim_set(t, sim, along, SIM_END - 1, z, BLOCK_ID_STONE);
            sim_set(t, sim, SIM_MIN, along, z, BLOCK_ID_STONE);
            sim_set(t, sim, SIM_END - 1, along, z, BLOCK_ID_STONE);
        }
    }
}

/* ---- Water: worked out by hand --------------------------------------------------------------------------------------- */

TEST(water_on_a_floor_makes_a_diamond_of_levels_one_block_every_five_ticks) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_run(sim, 1u);
    CHECK_EQ_U64(world_tick_pending(&sim->tick), 0u); /* stone does nothing */

    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_WATER);
    /* Tick 1 of these reads the log, so the source's own tick is due 5 later. */
    sim_run(sim, 5u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_AIR);
    sim_run(sim, 1u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 2, SIM_MID, 1), BLOCK_ID_AIR);
    sim_run(sim, 4u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 2, SIM_MID, 1), BLOCK_ID_AIR);
    sim_run(sim, 1u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 2, SIM_MID, 1), BLOCK_ID_WATER);

    (void)sim_settle(t, sim);
    for (int32_t y = SIM_MIN; y < SIM_END; y++) {
        for (int32_t x = SIM_MIN; x < SIM_END; x++) {
            const int32_t distance = sim_abs(x - SIM_MID) + sim_abs(y - SIM_MID);
            if (distance <= 7) {
                CHECK_EQ_U64(sim_at(t, sim, x, y, 1), BLOCK_ID_WATER);
                CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, x, y, 1), (uint64_t)distance);
            } else {
                CHECK_EQ_U64(sim_at(t, sim, x, y, 1), BLOCK_ID_AIR);
            }
        }
    }
    CHECK_EQ_U64(sim->tick.node_full_count, 0u);
    CHECK_EQ_U64(sim->tick.page_full_count, 0u);
    sim_end(sim);
}

TEST(water_with_no_source_dries_up_and_gives_back_its_pages) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    CHECK(sim->tick.page_live != 0u);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_AIR);
    (void)sim_settle(t, sim);
    for (int32_t y = SIM_MIN; y < SIM_END; y++) {
        for (int32_t x = SIM_MIN; x < SIM_END; x++) {
            CHECK_EQ_U64(sim_at(t, sim, x, y, 1), BLOCK_ID_AIR);
        }
    }
    CHECK_EQ_U64(sim->tick.page_live, 0u);
    sim_end(sim);
}

TEST(water_falls_off_a_ledge_and_starts_again_at_the_bottom) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    /* A pillar, with the source on top. */
    for (int32_t z = 1; z <= 9; z++) {
        sim_set(t, sim, SIM_MID, SIM_MID, z, BLOCK_ID_STONE);
    }
    sim_set(t, sim, SIM_MID, SIM_MID, 10, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    /* One step out on the top, level 1. It then has nothing under it, so it goes down and not on. */
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 1, SIM_MID, 10), 1u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 2, SIM_MID, 10), BLOCK_ID_AIR);
    for (int32_t z = 1; z <= 9; z++) {
        CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, z), BLOCK_ID_WATER);
        CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 1, SIM_MID, z), WORLD_TICK_LEVEL_FALLING);
    }
    /* On the floor, level 1 next to where it lands, on to 7, and no further. */
    for (int32_t step = 1; step <= 7; step++) {
        CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1 + step, SIM_MID, 1), BLOCK_ID_WATER);
        CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 1 + step, SIM_MID, 1), (uint64_t)step);
    }
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 9, SIM_MID, 1), BLOCK_ID_AIR);
    /* Under the top of the fall nothing else was there to stop it, and the pillar is untouched. */
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 5), BLOCK_ID_STONE);
    sim_end(sim);
}

TEST(water_goes_only_the_way_that_is_nearest_to_a_hole) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    for (int32_t y = SIM_MIN; y < SIM_END; y++) {
        for (int32_t x = SIM_MIN; x < SIM_END; x++) {
            sim_set(t, sim, x, y, SIM_FLOOR_Z - 1, BLOCK_ID_STONE);
        }
    }
    /* A hole in the floor, two along from the source. */
    sim_set(t, sim, SIM_MID + 2, SIM_MID, SIM_FLOOR_Z, BLOCK_ID_AIR);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 1, SIM_MID, 1), 1u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 2, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 2, SIM_MID, 1), 2u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 2, SIM_MID, 0), BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 2, SIM_MID, 0), WORLD_TICK_LEVEL_FALLING);
    /* And not the other ways, nor on past the hole. */
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID - 1, SIM_MID, 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID + 1, 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID - 1, 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 3, SIM_MID, 1), BLOCK_ID_AIR);
    sim_end(sim);
}

TEST(two_sources_make_a_third_only_with_something_under_it) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID - 1, SIM_MID, 1, BLOCK_ID_WATER);
    sim_set(t, sim, SIM_MID + 1, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID, SIM_MID, 1), WORLD_TICK_LEVEL_SOURCE);
    /* It stays when they go, and feeds what is around it. */
    sim_set(t, sim, SIM_MID - 1, SIM_MID, 1, BLOCK_ID_AIR);
    sim_set(t, sim, SIM_MID + 1, SIM_MID, 1, BLOCK_ID_AIR);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 1, SIM_MID, 1), 1u);
    sim_end(sim);

    /* Over a pit, the same two make none. */
    sim_t *const pit = sim_begin(t);
    sim_floor(t, pit, BLOCK_ID_STONE);
    sim_set(t, pit, SIM_MID, SIM_MID, 0, BLOCK_ID_AIR);
    sim_set(t, pit, SIM_MID, SIM_MID, -1, BLOCK_ID_STONE);
    sim_set(t, pit, SIM_MID - 1, SIM_MID, 1, BLOCK_ID_WATER);
    sim_set(t, pit, SIM_MID + 1, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, pit);
    CHECK_EQ_U64(sim_at(t, pit, SIM_MID, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK(world_tick_fluid_level(&pit->tick, SIM_MID, SIM_MID, 1) != WORLD_TICK_LEVEL_SOURCE);
    sim_end(pit);
}

TEST(a_ladder_holds_water_back_and_a_plant_washes_away) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID + 1, SIM_MID, 1, BLOCK_ID_TALL_GRASS);
    sim_set(t, sim, SIM_MID - 1, SIM_MID, 1, BLOCK_ID_LADDER);
    sim_set(t, sim, SIM_MID, SIM_MID + 1, 1, BLOCK_ID_KELP);
    sim_set(t, sim, SIM_MID, SIM_MID - 1, 1, BLOCK_ID_SEAGRASS);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID - 1, SIM_MID, 1), BLOCK_ID_LADDER);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID + 1, 1), BLOCK_ID_KELP);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID - 1, 1), BLOCK_ID_SEAGRASS);
    sim_end(sim);
}

TEST(water_terrain_left_alone_stays_as_it_is_until_something_touches_it) {
    sim_t *const sim = sim_begin(t);
    /* As terrain writes it: into the column, past the log, with nothing under it. */
    column_t *const column = world_map_column_at(&sim->map, 0, 0);
    REQUIRE(column != nullptr);
    CHECK_EQ_U64(column_block_set(column, &sim->pool, 3u, 3u, 40, BLOCK_ID_WATER), COLUMN_STATUS_OK);
    CHECK_EQ_U64(column_block_set(column, &sim->pool, 8u, 8u, 40, BLOCK_ID_SAND), COLUMN_STATUS_OK);
    sim_run(sim, 200u);
    CHECK_EQ_U64(world_tick_pending(&sim->tick), 0u);
    CHECK_EQ_U64(sim_at(t, sim, 3, 3, 39), BLOCK_ID_AIR);
    CHECK_EQ_U64(sim_at(t, sim, 8, 8, 40), BLOCK_ID_SAND);
    /* A block next to it changes, and so it wakes. */
    sim_set(t, sim, 4, 3, 40, BLOCK_ID_STONE);
    sim_set(t, sim, 8, 8, 41, BLOCK_ID_STONE);
    sim_run(sim, 10u);
    CHECK_EQ_U64(sim_at(t, sim, 3, 3, 39), BLOCK_ID_WATER);
    CHECK_EQ_U64(sim_at(t, sim, 8, 8, 40), BLOCK_ID_AIR);
    sim_end(sim);
}

/* ---- Water: against a separate flood fill ---------------------------------------------------------------------------- */

typedef struct sim_flood_t {
    int16_t x;
    int16_t y;
} sim_flood_t;
static_assert(sizeof(sim_flood_t) == 4);

/* On a flat floor with no holes, every level is the steps from the nearest source, going round pillars. Worked out here by a
 * fill that shares nothing with the code. Two sources with one between them are kept from the world this checks, since they
 * would make a third. */
TEST(on_a_flat_floor_with_pillars_levels_match_a_separate_flood_fill) {
    enum : uint32_t {
        ACROSS = (uint32_t)(SIM_END - SIM_MIN),
        CELLS = ACROSS * ACROSS,
    };
    uint64_t rng = 0x5A5A1234DEADBEEFull;
    int8_t *const distance = test_alloc(t, CELLS);
    sim_flood_t *const queue = test_alloc(t, CELLS * sizeof(sim_flood_t));
    for (uint32_t round = 0u; round < 4u; round++) {
        sim_t *const sim = sim_begin(t);
        sim_floor(t, sim, BLOCK_ID_STONE);
        sim_ring(t, sim, 1);
        memset(distance, -1, CELLS);
        uint32_t head = 0u, tail = 0u;
        for (uint32_t i = 0u; i < 40u + round * 60u; i++) {
            const int32_t x = SIM_MIN + (int32_t)(sim_random(&rng) % ACROSS);
            const int32_t y = SIM_MIN + (int32_t)(sim_random(&rng) % ACROSS);
            sim_set(t, sim, x, y, 1, BLOCK_ID_STONE);
        }
        /* Sources 16 apart along each axis, so that no two ever share a neighbour, and 7 from the end of the floor at most. */
        for (int32_t y = SIM_MIN + 8; y < SIM_END; y += 16) {
            for (int32_t x = SIM_MIN + 8; x < SIM_END; x += 16) {
                sim_set(t, sim, x, y, 1, BLOCK_ID_WATER);
                const uint32_t cell = (uint32_t)(y - SIM_MIN) * ACROSS + (uint32_t)(x - SIM_MIN);
                distance[cell] = 0;
                queue[tail++] = (sim_flood_t){(int16_t)x, (int16_t)y};
            }
        }
        while (head < tail) {
            const sim_flood_t at = queue[head++];
            const int8_t here = distance[(uint32_t)(at.y - SIM_MIN) * ACROSS + (uint32_t)(at.x - SIM_MIN)];
            if (here == 7) {
                continue;
            }
            static const int8_t way_x[4] = {1, -1, 0, 0};
            static const int8_t way_y[4] = {0, 0, 1, -1};
            for (uint32_t way = 0u; way < 4u; way++) {
                const int32_t x = at.x + way_x[way], y = at.y + way_y[way];
                if (x < SIM_MIN || y < SIM_MIN || x >= SIM_END || y >= SIM_END) {
                    continue;
                }
                const uint32_t cell = (uint32_t)(y - SIM_MIN) * ACROSS + (uint32_t)(x - SIM_MIN);
                if (distance[cell] >= 0 || sim_at(t, sim, x, y, 1) == BLOCK_ID_STONE) {
                    continue;
                }
                distance[cell] = (int8_t)(here + 1);
                queue[tail++] = (sim_flood_t){(int16_t)x, (int16_t)y};
            }
        }
        (void)sim_settle(t, sim);
        uint32_t wrong = 0u;
        for (int32_t y = SIM_MIN; y < SIM_END; y++) {
            for (int32_t x = SIM_MIN; x < SIM_END; x++) {
                const int8_t want = distance[(uint32_t)(y - SIM_MIN) * ACROSS + (uint32_t)(x - SIM_MIN)];
                const uint16_t state = sim_at(t, sim, x, y, 1);
                if (want < 0) {
                    wrong += state == BLOCK_ID_WATER ? 1u : 0u;
                } else {
                    wrong += state != BLOCK_ID_WATER ? 1u : 0u;
                    wrong += world_tick_fluid_level(&sim->tick, x, y, 1) != (uint8_t)want ? 1u : 0u;
                }
            }
        }
        CHECK_EQ_U64(wrong, 0u);
        sim_end(sim);
    }
}

/* What every settled world must hold, whatever its shape: water has something feeding it, and nothing waits above a gap. */
static uint32_t sim_water_inconsistent(test_context_t *t, const sim_t *sim, int32_t z_top) {
    uint32_t wrong = 0u;
    for (int32_t z = 1; z <= z_top; z++) {
        for (int32_t y = SIM_MIN + 1; y < SIM_END - 1; y++) {
            for (int32_t x = SIM_MIN + 1; x < SIM_END - 1; x++) {
                if (sim_at(t, sim, x, y, z) != BLOCK_ID_WATER) {
                    continue;
                }
                const uint8_t level = world_tick_fluid_level(&sim->tick, x, y, z);
                if (sim_at(t, sim, x, y, z - 1) == BLOCK_ID_AIR) {
                    wrong++; /* it should have gone down */
                }
                if (level == WORLD_TICK_LEVEL_SOURCE) {
                    continue;
                }
                if (level == WORLD_TICK_LEVEL_FALLING) {
                    wrong += sim_at(t, sim, x, y, z + 1) != BLOCK_ID_WATER ? 1u : 0u;
                    continue;
                }
                /* The nearest neighbour feeding it is exactly one less. */
                static const int8_t way_x[4] = {1, -1, 0, 0};
                static const int8_t way_y[4] = {0, 0, 1, -1};
                uint32_t nearest = 100u;
                for (uint32_t way = 0u; way < 4u; way++) {
                    if (sim_at(t, sim, x + way_x[way], y + way_y[way], z) == BLOCK_ID_WATER) {
                        uint32_t other = world_tick_fluid_level(&sim->tick, x + way_x[way], y + way_y[way], z);
                        other = other == WORLD_TICK_LEVEL_FALLING ? 0u : other;
                        nearest = other < nearest ? other : nearest;
                    }
                }
                wrong += nearest + 1u != level ? 1u : 0u;
                wrong += sim_at(t, sim, x, y, z + 1) == BLOCK_ID_WATER ? 1u : 0u;
            }
        }
    }
    return wrong;
}

TEST(on_rough_ground_water_settles_consistent_and_drains_to_nothing_when_its_sources_go) {
    uint64_t rng = 0x0123456789ABCDEFull;
    for (uint32_t round = 0u; round < 3u; round++) {
        sim_t *const sim = sim_begin(t);
        sim_floor(t, sim, BLOCK_ID_STONE);
        /* Ground in steps of up to 6, in squares, so that there are ledges to run off and pits to fill. */
        for (int32_t y = SIM_MIN; y < SIM_END; y += 3) {
            for (int32_t x = SIM_MIN; x < SIM_END; x += 3) {
                const int32_t height = (int32_t)(sim_random(&rng) % 7u);
                for (int32_t dy = 0; dy < 3 && y + dy < SIM_END; dy++) {
                    for (int32_t dx = 0; dx < 3 && x + dx < SIM_END; dx++) {
                        for (int32_t z = 1; z <= height; z++) {
                            sim_set(t, sim, x + dx, y + dy, z, BLOCK_ID_STONE);
                        }
                    }
                }
            }
        }
        sim_ring(t, sim, 13);
        for (uint32_t i = 0u; i < 6u; i++) {
            sim_set(t, sim, SIM_MIN + 4 + (int32_t)(sim_random(&rng) % 40u),
                    SIM_MIN + 4 + (int32_t)(sim_random(&rng) % 40u), 12, BLOCK_ID_WATER);
        }
        (void)sim_settle(t, sim);
        CHECK_EQ_U64(sim_water_inconsistent(t, sim, 12), 0u);

        /* Take away every source, including any it made. New ones cannot form with none left to make them. */
        for (uint32_t pass = 0u; pass < 8u; pass++) {
            for (int32_t z = 1; z <= 12; z++) {
                for (int32_t y = SIM_MIN; y < SIM_END; y++) {
                    for (int32_t x = SIM_MIN; x < SIM_END; x++) {
                        if (sim_at(t, sim, x, y, z) == BLOCK_ID_WATER &&
                            world_tick_fluid_level(&sim->tick, x, y, z) == WORLD_TICK_LEVEL_SOURCE) {
                            sim_set(t, sim, x, y, z, BLOCK_ID_AIR);
                        }
                    }
                }
            }
            (void)sim_settle(t, sim);
        }
        uint32_t left = 0u;
        for (int32_t z = 1; z <= 12; z++) {
            for (int32_t y = SIM_MIN; y < SIM_END; y++) {
                for (int32_t x = SIM_MIN; x < SIM_END; x++) {
                    left += sim_at(t, sim, x, y, z) == BLOCK_ID_WATER ? 1u : 0u;
                }
            }
        }
        CHECK_EQ_U64(left, 0u);
        CHECK_EQ_U64(sim->tick.page_live, 0u);
        sim_end(sim);
    }
}

/* ---- Lava ------------------------------------------------------------------------------------------------------------ */

TEST(lava_is_slow_goes_three_blocks_and_never_makes_a_source) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_LAVA);
    sim_run(sim, 30u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_AIR);
    sim_run(sim, 1u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_LAVA);
    (void)sim_settle(t, sim);
    for (int32_t y = SIM_MID - 5; y <= SIM_MID + 5; y++) {
        for (int32_t x = SIM_MID - 5; x <= SIM_MID + 5; x++) {
            const int32_t distance = sim_abs(x - SIM_MID) + sim_abs(y - SIM_MID);
            if (distance <= 3) {
                CHECK_EQ_U64(sim_at(t, sim, x, y, 1), BLOCK_ID_LAVA);
                CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, x, y, 1), (uint64_t)(2 * distance));
            } else {
                CHECK_EQ_U64(sim_at(t, sim, x, y, 1), BLOCK_ID_AIR);
            }
        }
    }
    sim_end(sim);

    /* Where water would make a third source, lava makes none. */
    sim_t *const pair = sim_begin(t);
    sim_floor(t, pair, BLOCK_ID_STONE);
    sim_set(t, pair, SIM_MID - 1, SIM_MID, 1, BLOCK_ID_LAVA);
    sim_set(t, pair, SIM_MID + 1, SIM_MID, 1, BLOCK_ID_LAVA);
    (void)sim_settle(t, pair);
    CHECK_EQ_U64(world_tick_fluid_level(&pair->tick, SIM_MID, SIM_MID, 1), 2u);
    sim_end(pair);
}

/* A wall round a voxel, on the sides given as bits: 1 east, 2 west, 4 north, 8 south. */
static void sim_wall(test_context_t *t, sim_t *sim, int32_t x, int32_t y, int32_t z, uint32_t sides) {
    if ((sides & 1u) != 0u) {
        sim_set(t, sim, x + 1, y, z, BLOCK_ID_STONE);
    }
    if ((sides & 2u) != 0u) {
        sim_set(t, sim, x - 1, y, z, BLOCK_ID_STONE);
    }
    if ((sides & 4u) != 0u) {
        sim_set(t, sim, x, y + 1, z, BLOCK_ID_STONE);
    }
    if ((sides & 8u) != 0u) {
        sim_set(t, sim, x, y - 1, z, BLOCK_ID_STONE);
    }
}

TEST(water_hardens_lava_into_what_it_should) {
    /* A source beside water: obsidian. */
    sim_t *sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_wall(t, sim, SIM_MID, SIM_MID, 1, 2u | 4u | 8u);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_LAVA);
    sim_set(t, sim, SIM_MID + 1, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_OBSIDIAN);
    sim_end(sim);

    /* Flowing lava beside water: cobblestone. */
    sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_wall(t, sim, SIM_MID, SIM_MID, 1, 2u | 4u | 8u);
    sim_wall(t, sim, SIM_MID + 1, SIM_MID, 1, 1u | 4u | 8u);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_LAVA);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_LAVA);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 1, SIM_MID, 1), 2u);
    sim_set(t, sim, SIM_MID + 2, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_COBBLESTONE);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_LAVA); /* the source is behind that now */
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, SIM_MID + 1, SIM_MID, 1), WORLD_TICK_LEVEL_SOURCE);
    sim_end(sim);

    /* Water falling on a source: obsidian. */
    sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_wall(t, sim, SIM_MID, SIM_MID, 1, 15u);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_LAVA);
    sim_wall(t, sim, SIM_MID, SIM_MID, 2, 15u);
    sim_set(t, sim, SIM_MID, SIM_MID, 3, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_OBSIDIAN);
    sim_end(sim);

    /* Lava falling on water: stone where the water was. */
    sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_DIRT);
    sim_wall(t, sim, SIM_MID, SIM_MID, 1, 15u);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_WATER);
    sim_wall(t, sim, SIM_MID, SIM_MID, 2, 15u);
    sim_set(t, sim, SIM_MID, SIM_MID, 3, BLOCK_ID_LAVA);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_STONE);
    sim_end(sim);
}

/* ---- Falling --------------------------------------------------------------------------------------------------------- */

TEST(sand_and_gravel_fall_two_ticks_a_block_and_stack_on_the_floor) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID, SIM_MID, 10, BLOCK_ID_SAND);
    sim_run(sim, 2u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 10), BLOCK_ID_SAND);
    sim_run(sim, 1u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 10), BLOCK_ID_AIR);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 9), BLOCK_ID_SAND);
    sim_run(sim, 2u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 8), BLOCK_ID_SAND);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_SAND);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 2), BLOCK_ID_AIR);

    /* A stack keeps its order. */
    sim_set(t, sim, SIM_MID + 1, SIM_MID, 8, BLOCK_ID_GRAVEL);
    sim_set(t, sim, SIM_MID + 1, SIM_MID, 9, BLOCK_ID_SAND);
    sim_set(t, sim, SIM_MID + 1, SIM_MID, 10, BLOCK_ID_RED_SAND);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 1), BLOCK_ID_GRAVEL);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 2), BLOCK_ID_SAND);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 3), BLOCK_ID_RED_SAND);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 1, SIM_MID, 4), BLOCK_ID_AIR);
    sim_end(sim);
}

TEST(sand_on_something_that_goes_away_falls_and_so_does_what_is_on_it) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID, SIM_MID, 5, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID, SIM_MID, 6, BLOCK_ID_SAND);
    sim_set(t, sim, SIM_MID, SIM_MID, 7, BLOCK_ID_SAND);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 6), BLOCK_ID_SAND);
    sim_set(t, sim, SIM_MID, SIM_MID, 5, BLOCK_ID_AIR);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_SAND);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 2), BLOCK_ID_SAND);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 3), BLOCK_ID_AIR);
    sim_end(sim);
}

TEST(sand_falls_through_water_and_flowers_and_stops_at_the_bottom_of_the_world) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_wall(t, sim, SIM_MID, SIM_MID, 1, 15u);
    sim_wall(t, sim, SIM_MID, SIM_MID, 2, 15u);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_WATER);
    sim_set(t, sim, SIM_MID, SIM_MID, 2, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    sim_set(t, sim, SIM_MID, SIM_MID, 3, BLOCK_ID_SAND);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_SAND);
    /* A block of water has nothing to feed it, and has gone from over the sand. */
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 3), BLOCK_ID_AIR);

    sim_set(t, sim, SIM_MID + 2, SIM_MID, 1, BLOCK_ID_DANDELION);
    sim_set(t, sim, SIM_MID + 2, SIM_MID, 5, BLOCK_ID_GRAVEL);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID + 2, SIM_MID, 1), BLOCK_ID_GRAVEL);

    sim_set(t, sim, 0, 0, COLUMN_Z_GAMEPLAY_MIN, BLOCK_ID_SAND);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, 0, 0, COLUMN_Z_GAMEPLAY_MIN), BLOCK_ID_SAND);
    sim_end(sim);
}

/* ---- Leaves ---------------------------------------------------------------------------------------------------------- */

TEST(leaves_six_steps_from_a_log_stay_and_seven_go) {
    sim_t *const sim = sim_begin(t);
    /* A line of leaves off a log, in the air. */
    sim_set(t, sim, 0, 0, 20, BLOCK_ID_OAK_LOG);
    for (int32_t step = 1; step <= 9; step++) {
        sim_set(t, sim, step, 0, 20, BLOCK_ID_OAK_LEAVES);
    }
    (void)sim_settle(t, sim);
    sim->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(sim, 30u);
    for (int32_t step = 1; step <= 6; step++) {
        CHECK_EQ_U64(sim_at(t, sim, step, 0, 20), BLOCK_ID_OAK_LEAVES);
    }
    for (int32_t step = 7; step <= 9; step++) {
        CHECK_EQ_U64(sim_at(t, sim, step, 0, 20), BLOCK_ID_AIR);
    }
    /* Any log holds any leaves, and when the only one goes, they all do. */
    sim_set(t, sim, 3, 1, 20, BLOCK_ID_BIRCH_LEAVES);
    sim_set(t, sim, 0, 0, 20, BLOCK_ID_JUNGLE_LOG);
    sim_run(sim, 30u);
    CHECK_EQ_U64(sim_at(t, sim, 3, 1, 20), BLOCK_ID_BIRCH_LEAVES);
    sim_set(t, sim, 0, 0, 20, BLOCK_ID_AIR);
    sim_run(sim, 30u);
    for (int32_t step = 1; step <= 6; step++) {
        CHECK_EQ_U64(sim_at(t, sim, step, 0, 20), BLOCK_ID_AIR);
    }
    CHECK_EQ_U64(sim_at(t, sim, 3, 1, 20), BLOCK_ID_AIR);
    sim_end(sim);
}

TEST(a_tree_on_the_edge_of_what_is_loaded_keeps_its_leaves) {
    sim_t *const sim = sim_begin(t);
    /* x = 47 is the last of what is loaded, and the world goes on, not loaded, at x = 48. */
    sim_set(t, sim, 47, 8, 20, BLOCK_ID_SPRUCE_LEAVES);
    sim_set(t, sim, 8, 8, 20, BLOCK_ID_SPRUCE_LEAVES); /* with air around it, and no excuse */
    sim->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(sim, 30u);
    CHECK_EQ_U64(sim_at(t, sim, 47, 8, 20), BLOCK_ID_SPRUCE_LEAVES);
    CHECK_EQ_U64(sim_at(t, sim, 8, 8, 20), BLOCK_ID_AIR);
    sim_end(sim);
}

TEST(every_block_named_as_a_log_counts_as_one) {
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        const char *const name = block_defs[block].name;
        const uint64_t length = strlen(name);
        if (length < 4u || strcmp(name + length - 4u, "_log") != 0) {
            continue;
        }
        sim_t *const sim = sim_begin(t);
        sim_set(t, sim, 0, 0, 20, (uint16_t)block);
        sim_set(t, sim, 1, 0, 20, BLOCK_ID_OAK_LEAVES);
        sim->tick.random_per_section = SIM_RANDOM_FAST;
        sim_run(sim, 10u);
        CHECK_EQ_U64(sim_at(t, sim, 1, 0, 20), BLOCK_ID_OAK_LEAVES);
        sim_end(sim);
    }
}

/* ---- Crops ----------------------------------------------------------------------------------------------------------- */

TEST(wheat_in_the_light_grows_through_every_stage_and_stops) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, 2, 2, 1, BLOCK_ID_FARMLAND);
    sim_set(t, sim, 2, 2, 2, BLOCK_ID_WHEAT_STAGE_1);
    (void)sim_settle(t, sim);
    (void)world_light_update(&sim->light);
    sim->tick.random_per_section = SIM_RANDOM_FAST;
    uint32_t stages_seen = 1u;
    uint16_t last = BLOCK_ID_WHEAT_STAGE_1;
    for (uint32_t i = 0u; i < 200u; i++) {
        sim_run(sim, 1u);
        const uint16_t now = sim_at(t, sim, 2, 2, 2);
        /* A stage at a time, even with many picks a tick, so long as only one tick's worth is seen. */
        CHECK(now >= last && now <= BLOCK_ID_WHEAT_STAGE_8);
        stages_seen += now != last ? 1u : 0u;
        last = now;
    }
    CHECK_EQ_U64(last, BLOCK_ID_WHEAT_STAGE_8);
    CHECK(stages_seen >= 2u);
    sim_end(sim);
}

TEST(wheat_in_the_dark_does_not_grow) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, 2, 2, 1, BLOCK_ID_FARMLAND);
    sim_set(t, sim, 2, 2, 2, BLOCK_ID_WHEAT_STAGE_1);
    /* A closed box of stone round it: no sky, and no light gets in. */
    for (int32_t z = 1; z <= 3; z++) {
        for (int32_t y = 1; y <= 3; y++) {
            for (int32_t x = 1; x <= 3; x++) {
                if (x != 2 || y != 2 || z == 3) {
                    sim_set(t, sim, x, y, z, BLOCK_ID_STONE);
                }
            }
        }
    }
    (void)sim_settle(t, sim);
    (void)world_light_update(&sim->light);
    sim->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(sim, 50u);
    CHECK_EQ_U64(sim_at(t, sim, 2, 2, 2), BLOCK_ID_WHEAT_STAGE_1);
    sim_end(sim);
}

/* Odds: out of many crops and one pick per voxel tick, how many went up. Worked out from the numbers in the README: 1 in 7 near water
 * and 1 in 13 without. What this checks is the ratio and that each is near its own number. */
static uint32_t sim_wheat_growth(test_context_t *t, bool wet) {
    enum : uint32_t { ROWS = 9u };
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    uint32_t grown = 0u;
    for (int32_t y = 0; y < (int32_t)ROWS; y++) {
        for (int32_t x = 0; x < (int32_t)ROWS; x++) {
            sim_set(t, sim, x, y, 1, BLOCK_ID_FARMLAND);
            sim_set(t, sim, x, y, 2, BLOCK_ID_WHEAT_STAGE_1);
        }
    }
    if (wet) {
        /* Water in a stone trough over the farmland row, within reach of all of it. */
        sim_set(t, sim, 4, 4, 1, BLOCK_ID_WATER);
        sim_set(t, sim, 4, 4, 2, BLOCK_ID_STONE);
    }
    (void)sim_settle(t, sim);
    (void)world_light_update(&sim->light);
    sim->tick.random_per_section = 1u;
    sim_run(sim, 20000u);
    for (int32_t y = 0; y < (int32_t)ROWS; y++) {
        for (int32_t x = 0; x < (int32_t)ROWS; x++) {
            const uint16_t now = sim_at(t, sim, x, y, 2);
            if (x != 4 || y != 4) {
                grown += now >= BLOCK_ID_WHEAT_STAGE_1 && now <= BLOCK_ID_WHEAT_STAGE_8
                             ? (uint32_t)now - BLOCK_ID_WHEAT_STAGE_1
                             : 0u;
            }
        }
    }
    sim_end(sim);
    return grown;
}

TEST(wheat_grows_faster_with_water_at_the_rate_the_readme_says) {
    /* A section's one pick a tick hits a given voxel 20000 / 4096 = 4.88 times. Seen on 80 crops, wet: 390 picks / 7 = 56 stages.
     * Dry: 390 / 13 = 30. Within half either way is a loose bound; the runs are fixed by the seed, so this does not flake. */
    const uint32_t wet = sim_wheat_growth(t, true);
    const uint32_t dry = sim_wheat_growth(t, false);
    printf("wheat stages gained in 20000 ticks over 80 crops: %" PRIu32 " wet, %" PRIu32 " dry\n", wet, dry);
    CHECK(wet >= 28u && wet <= 84u);
    CHECK(dry >= 15u && dry <= 45u);
    CHECK(wet > dry);
}

TEST(wheat_goes_with_its_farmland_and_farmland_goes_under_a_block) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, 2, 2, 1, BLOCK_ID_FARMLAND);
    sim_set(t, sim, 2, 2, 2, BLOCK_ID_WHEAT_STAGE_4);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, 2, 2, 2), BLOCK_ID_WHEAT_STAGE_4);
    sim_set(t, sim, 2, 2, 1, BLOCK_ID_DIRT);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, 2, 2, 2), BLOCK_ID_AIR);

    sim_set(t, sim, 5, 5, 1, BLOCK_ID_FARMLAND);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, 5, 5, 1), BLOCK_ID_FARMLAND);
    sim_set(t, sim, 5, 5, 2, BLOCK_ID_STONE);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, 5, 5, 1), BLOCK_ID_DIRT);
    sim_end(sim);
}

TEST(farmland_with_no_water_and_no_crop_goes_to_dirt_and_with_either_stays) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, 1, 1, 1, BLOCK_ID_FARMLAND);   /* dry, bare */
    sim_set(t, sim, 12, 12, 1, BLOCK_ID_FARMLAND); /* bare, with water near */
    sim_set(t, sim, 14, 12, 1, BLOCK_ID_WATER);
    sim_wall(t, sim, 14, 12, 1, 15u);
    sim_set(t, sim, 1, 6, 1, BLOCK_ID_FARMLAND); /* dry, with a crop */
    sim_set(t, sim, 1, 6, 2, BLOCK_ID_WHEAT_STAGE_8);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, 12, 12, 1), BLOCK_ID_FARMLAND);
    sim->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(sim, 60u);
    CHECK_EQ_U64(sim_at(t, sim, 1, 1, 1), BLOCK_ID_DIRT);
    CHECK_EQ_U64(sim_at(t, sim, 12, 12, 1), BLOCK_ID_FARMLAND);
    CHECK_EQ_U64(sim_at(t, sim, 1, 6, 1), BLOCK_ID_FARMLAND);
    sim_end(sim);
}

/* ---- Grass ----------------------------------------------------------------------------------------------------------- */

TEST(grass_spreads_over_lit_dirt_by_its_reach_and_no_further) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_DIRT);
    sim_set(t, sim, SIM_MID, SIM_MID, 0, BLOCK_ID_GRASS_BLOCK);
    (void)sim_settle(t, sim);
    sim->tick.random_per_section = SIM_RANDOM_FAST;
    /* Each tick it goes out by one at most, whatever the picks. */
    uint32_t last_grass = 1u;
    uint32_t reached = 0u;
    for (uint32_t i = 0u; i < 40u; i++) {
        sim_run(sim, 1u);
        uint32_t grass = 0u;
        for (int32_t y = SIM_MIN; y < SIM_END; y++) {
            for (int32_t x = SIM_MIN; x < SIM_END; x++) {
                grass += sim_at(t, sim, x, y, 0) == BLOCK_ID_GRASS_BLOCK ? 1u : 0u;
            }
        }
        CHECK(grass >= last_grass);
        last_grass = grass;
    }
    reached = last_grass;
    printf("grass after 40 ticks of fast random ticks: %" PRIu32 " blocks\n", reached);
    CHECK(reached > 100u);
    sim_end(sim);

    /* Only 1 sideways, and 3 down or 1 up: so these never get it, even given every chance. */
    sim_t *const far = sim_begin(t);
    sim_set(t, far, 0, 0, 20, BLOCK_ID_GRASS_BLOCK);
    sim_set(t, far, 2, 0, 20, BLOCK_ID_DIRT); /* 2 along */
    sim_set(t, far, 0, 0, 16, BLOCK_ID_DIRT); /* 4 down */
    sim_set(t, far, 1, 0, 22, BLOCK_ID_DIRT); /* 2 up */
    (void)sim_settle(t, far);
    far->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(far, 30u);
    CHECK_EQ_U64(sim_at(t, far, 2, 0, 20), BLOCK_ID_DIRT);
    CHECK_EQ_U64(sim_at(t, far, 0, 0, 16), BLOCK_ID_DIRT);
    CHECK_EQ_U64(sim_at(t, far, 1, 0, 22), BLOCK_ID_DIRT);
    sim_end(far);

    /* And the far ends of what it can reach. */
    sim_t *const near = sim_begin(t);
    sim_set(t, near, 0, 0, 20, BLOCK_ID_GRASS_BLOCK);
    sim_set(t, near, 1, 0, 17, BLOCK_ID_DIRT); /* 3 down, and 1 along */
    sim_set(t, near, 0, 1, 21, BLOCK_ID_DIRT); /* 1 up, and 1 along */
    (void)sim_settle(t, near);
    near->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(near, 30u);
    CHECK_EQ_U64(sim_at(t, near, 1, 0, 17), BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(sim_at(t, near, 0, 1, 21), BLOCK_ID_GRASS_BLOCK);
    sim_end(near);
}

TEST(grass_under_stone_or_water_turns_to_dirt_and_dark_grass_does_not_spread) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, 1, 1, 1, BLOCK_ID_GRASS_BLOCK);
    sim_set(t, sim, 1, 1, 2, BLOCK_ID_STONE);
    sim_set(t, sim, 12, 12, 1, BLOCK_ID_GRASS_BLOCK);
    sim_wall(t, sim, 12, 12, 2, 15u);
    sim_set(t, sim, 12, 12, 2, BLOCK_ID_WATER);
    sim_set(t, sim, 5, 12, 1, BLOCK_ID_GRASS_BLOCK); /* left alone, in the open */
    (void)sim_settle(t, sim);
    sim->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(sim, 30u);
    CHECK_EQ_U64(sim_at(t, sim, 1, 1, 1), BLOCK_ID_DIRT);
    CHECK_EQ_U64(sim_at(t, sim, 12, 12, 1), BLOCK_ID_DIRT);
    CHECK_EQ_U64(sim_at(t, sim, 5, 12, 1), BLOCK_ID_GRASS_BLOCK);
    sim_end(sim);

    /* In a dark room, with air over both, dirt beside grass stays dirt. */
    sim_t *const dark = sim_begin(t);
    for (int32_t y = SIM_MIN; y < SIM_END; y++) {
        for (int32_t x = SIM_MIN; x < SIM_END; x++) {
            sim_set(t, dark, x, y, 3, BLOCK_ID_STONE);
            sim_set(t, dark, x, y, 0, BLOCK_ID_DIRT);
        }
    }
    sim_set(t, dark, 8, 8, 0, BLOCK_ID_GRASS_BLOCK);
    (void)sim_settle(t, dark);
    (void)world_light_update(&dark->light);
    dark->tick.random_per_section = SIM_RANDOM_FAST;
    sim_run(dark, 30u);
    CHECK_EQ_U64(sim_at(t, dark, 9, 8, 0), BLOCK_ID_DIRT);
    CHECK_EQ_U64(sim_at(t, dark, 8, 8, 0), BLOCK_ID_GRASS_BLOCK);
    sim_end(dark);
}

/* ---- Columns --------------------------------------------------------------------------------------------------------- */

static uint32_t sim_slot(const sim_t *sim, int32_t chunk_x, int32_t chunk_y) {
    uint32_t slot = UINT32_MAX;
    (void)world_map_slot_of(&sim->map, chunk_x, chunk_y, &slot);
    return slot;
}

/* The column at that chunk ends, as streaming ends one. */
static void sim_unload(sim_t *sim, int32_t chunk_x, int32_t chunk_y) {
    const uint32_t slot = sim_slot(sim, chunk_x, chunk_y);
    (void)world_light_column_leaving(&sim->light, slot);
    world_tick_column_leaving(&sim->tick, slot);
    world_map_unload(&sim->map, slot);
}

/* And comes back, all air with a stone floor. */
static void sim_reload(test_context_t *t, sim_t *sim, int32_t chunk_x, int32_t chunk_y) {
    world_map_request_t request;
    REQUIRE(world_map_stream_take(&sim->map, &request, 1u) == 1u);
    CHECK_EQ_U64((uint64_t)(int64_t)request.chunk_x, (uint64_t)(int64_t)chunk_x);
    CHECK_EQ_U64((uint64_t)(int64_t)request.chunk_y, (uint64_t)(int64_t)chunk_y);
    REQUIRE(world_map_stream_commit(&sim->map, &request) != nullptr);
    world_edit_column_filled(&sim->edit, request.slot);
    CHECK_EQ_U64(world_light_column_added(&sim->light, request.slot), WORLD_LIGHT_STATUS_OK);
    const int32_t base_x = chunk_x * (int32_t)COLUMN_SIZE, base_y = chunk_y * (int32_t)COLUMN_SIZE;
    for (int32_t y = 0; y < (int32_t)COLUMN_SIZE; y++) {
        for (int32_t x = 0; x < (int32_t)COLUMN_SIZE; x++) {
            sim_set(t, sim, base_x + x, base_y + y, SIM_FLOOR_Z, BLOCK_ID_STONE);
        }
    }
}

TEST(water_crosses_into_a_loaded_column_as_if_there_were_no_edge) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, 14, SIM_MID, 1, BLOCK_ID_WATER);
    (void)sim_settle(t, sim);
    for (int32_t step = 1; step <= 7; step++) {
        CHECK_EQ_U64(sim_at(t, sim, 14 + step, SIM_MID, 1), BLOCK_ID_WATER);
        CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, 14 + step, SIM_MID, 1), (uint64_t)step);
    }
    CHECK_EQ_U64(sim_at(t, sim, 14 + 8, SIM_MID, 1), BLOCK_ID_AIR);
    sim_end(sim);
}

TEST(water_at_a_column_not_loaded_waits_and_carries_on_when_it_comes) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_unload(sim, 1, 0);
    sim_set(t, sim, 14, SIM_MID, 1, BLOCK_ID_WATER);
    sim_run(sim, 200u);
    /* At the edge, level 1, and still trying. It has not dried up for lack of what is over there. */
    CHECK_EQ_U64(sim_at(t, sim, 15, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, 15, SIM_MID, 1), 1u);
    CHECK(world_tick_pending(&sim->tick) != 0u);

    sim_reload(t, sim, 1, 0);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, 16, SIM_MID, 1), BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, 16, SIM_MID, 1), 2u);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, 21, SIM_MID, 1), 7u);
    CHECK_EQ_U64(world_tick_pending(&sim->tick), 0u);
    sim_end(sim);
}

TEST(water_that_leaves_with_its_column_leaves_nothing_behind) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    sim_set(t, sim, 24, SIM_MID, 1, BLOCK_ID_WATER); /* all in chunk (1, 0) */
    sim_run(sim, 14u);
    CHECK(sim->tick.page_live != 0u);
    CHECK(world_tick_pending(&sim->tick) != 0u);

    const uint64_t fired_before = sim->tick.fired_count;
    sim_unload(sim, 1, 0);
    CHECK_EQ_U64(sim->tick.page_live, 0u);
    /* What was waiting there comes due, and is dropped: the column it comes back to has no water. */
    sim_reload(t, sim, 1, 0);
    (void)sim_settle(t, sim);
    CHECK(sim->tick.fired_count > fired_before);
    CHECK_EQ_U64(world_tick_pending(&sim->tick), 0u);
    for (int32_t y = 0; y < (int32_t)COLUMN_SIZE; y++) {
        for (int32_t x = 16; x < 32; x++) {
            CHECK_EQ_U64(sim_at(t, sim, x, y, 1), BLOCK_ID_AIR);
        }
    }
    /* A voxel there holds no old level: new water is a source. */
    sim_set(t, sim, 25, SIM_MID, 1, BLOCK_ID_WATER);
    sim_run(sim, 1u);
    CHECK_EQ_U64(world_tick_fluid_level(&sim->tick, 25, SIM_MID, 1), WORLD_TICK_LEVEL_SOURCE);
    sim_end(sim);
}

/* ---- Running out ----------------------------------------------------------------------------------------------------- */

TEST(with_too_few_nodes_wakes_are_dropped_and_counted_and_it_keeps_going) {
    sim_t *const sim = sim_begin_sized(t, 4u, SIM_PAGES, 1u);
    sim_floor(t, sim, BLOCK_ID_STONE);
    for (int32_t x = 0; x < 12; x++) {
        sim_set(t, sim, x, x, 12, BLOCK_ID_SAND);
    }
    sim_run(sim, 50u);
    CHECK(sim->tick.node_full_count != 0u);
    CHECK(sim->tick.node_live <= 4u);
    /* Some of the sand has moved, and none was lost or made. */
    uint32_t sand = 0u;
    for (int32_t z = 1; z <= 12; z++) {
        for (int32_t x = 0; x < 12; x++) {
            sand += sim_at(t, sim, x, x, z) == BLOCK_ID_SAND ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(sand, 12u);
    sim_end(sim);
}

TEST(with_too_few_pages_water_holds_back_and_goes_on_when_a_page_is_free) {
    sim_t *const sim = sim_begin_sized(t, SIM_NODES, 1u, 1u);
    sim_floor(t, sim, BLOCK_ID_STONE);
    /* The one page goes to this, at section z = 0 to 15 of chunk (0, 0). */
    sim_set(t, sim, 14, SIM_MID, 1, BLOCK_ID_WATER);
    sim_run(sim, 7u);
    CHECK_EQ_U64(sim->tick.page_live, 1u);
    CHECK_EQ_U64(sim_at(t, sim, 15, SIM_MID, 1), BLOCK_ID_WATER);
    /* Past the column's edge is another page, so it stops there. */
    sim_run(sim, 200u);
    CHECK(sim->tick.page_full_count != 0u);
    CHECK_EQ_U64(sim_at(t, sim, 16, SIM_MID, 1), BLOCK_ID_AIR);
    CHECK_EQ_U64(sim->tick.page_live, 1u);

    /* Move the source over the line: chunk (0, 0)'s page empties, and (1, 0) gets it. */
    sim_set(t, sim, 14, SIM_MID, 1, BLOCK_ID_AIR);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim->tick.page_live, 0u);
    sim_set(t, sim, 16, SIM_MID, 1, BLOCK_ID_WATER);
    sim_run(sim, 7u);
    CHECK_EQ_U64(sim_at(t, sim, 17, SIM_MID, 1), BLOCK_ID_WATER);
    sim_end(sim);
}

TEST(a_full_section_pool_loses_no_block_and_it_goes_on_when_there_is_room) {
    sim_t *const sim = sim_begin(t);
    /* Sand at the bottom of one section, over a section of nothing but air, which would need its first array. */
    sim_set(t, sim, SIM_MID, SIM_MID, -17, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_STONE);
    sim_set(t, sim, SIM_MID, SIM_MID, 0, BLOCK_ID_SAND);
    sim_set(t, sim, SIM_MID, SIM_MID, 1, BLOCK_ID_AIR);
    (void)world_light_update(&sim->light);
    /* The pool has nothing left to give: nothing in its lists, and nothing more from its arena. */
    const uint64_t capacity = sim->arena_pool.capacity;
    sim->arena_pool.capacity = sim->arena_pool.used;
    uint8_t *lists[SECTION_CLASS_COUNT];
    for (uint32_t i = 0u; i < SECTION_CLASS_COUNT; i++) {
        lists[i] = sim->pool.borrowed_free[i];
        sim->pool.borrowed_free[i] = nullptr;
    }
    world_tick_step(&sim->tick);
    sim_run_only_ticks(sim, 30u);
    CHECK(sim->tick.no_memory_count != 0u);
    CHECK(sim->pool.failed_count != 0u);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 0), BLOCK_ID_SAND);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, -1), BLOCK_ID_AIR);
    CHECK(world_tick_pending(&sim->tick) != 0u);

    sim->arena_pool.capacity = capacity;
    for (uint32_t i = 0u; i < SECTION_CLASS_COUNT; i++) {
        sim->pool.borrowed_free[i] = lists[i];
    }
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, -16), BLOCK_ID_SAND);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 0), BLOCK_ID_AIR);
    sim_end(sim);
}

TEST(one_tick_does_no_more_than_its_limit_and_the_rest_are_done_next_tick) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    /* More sand over the map than a tick is allowed to move: 48 x 48 x 8 is 18432. */
    for (int32_t z = 40; z < 48; z++) {
        for (int32_t y = SIM_MIN; y < SIM_END; y++) {
            for (int32_t x = SIM_MIN; x < SIM_END; x++) {
                sim_set(t, sim, x, y, z, BLOCK_ID_SAND);
            }
        }
    }
    sim_run(sim, 1u);
    uint64_t biggest = 0u;
    for (uint32_t i = 0u; i < 400u; i++) {
        const uint64_t log_before = sim->edit.log_next;
        world_tick_step(&sim->tick);
        const uint64_t wrote = sim->edit.log_next - log_before;
        biggest = wrote > biggest ? wrote : biggest;
        (void)world_light_update(&sim->light);
    }
    printf("the most blocks written by one tick of a big fall: %" PRIu64 "\n", biggest);
    /* A fired fall writes 2, so the limit is passed by at most that. */
    CHECK(biggest <= (uint64_t)WORLD_TICK_WRITES_PER_TICK + 2u);
    CHECK_EQ_U64(sim->tick.lost_count, 0u);
    (void)sim_settle(t, sim);
    uint64_t sand = 0u;
    for (int32_t z = 1; z <= 8; z++) {
        for (int32_t y = SIM_MIN; y < SIM_END; y++) {
            for (int32_t x = SIM_MIN; x < SIM_END; x++) {
                sand += sim_at(t, sim, x, y, z) == BLOCK_ID_SAND ? 1u : 0u;
            }
        }
    }
    CHECK_EQ_U64(sand, 18432u);
    sim_end(sim);
}

TEST(a_log_that_lapped_this_reader_is_counted_and_it_carries_on) {
    sim_t *const sim = sim_begin(t);
    sim_floor(t, sim, BLOCK_ID_STONE);
    world_tick_step(&sim->tick);
    for (uint32_t i = 0u; i < SIM_LOG + 100u; i++) {
        sim_set(t, sim, SIM_MID, SIM_MID, 5, (i & 1u) != 0u ? BLOCK_ID_AIR : BLOCK_ID_STONE);
    }
    world_tick_step(&sim->tick);
    CHECK(sim->tick.lost_count != 0u);
    sim_set(t, sim, SIM_MID, SIM_MID, 8, BLOCK_ID_SAND);
    (void)sim_settle(t, sim);
    CHECK_EQ_U64(sim_at(t, sim, SIM_MID, SIM_MID, 1), BLOCK_ID_SAND);
    sim_end(sim);
}

/* ---- The same world every time --------------------------------------------------------------------------------------- */

static uint64_t sim_busy_world(test_context_t *t, uint64_t seed, uint32_t *out_grass) {
    sim_t *const sim = sim_begin_sized(t, SIM_NODES, SIM_PAGES, seed);
    sim_floor(t, sim, BLOCK_ID_DIRT);
    uint64_t rng = 0xABCDEF0123456789ull;
    for (uint32_t i = 0u; i < 60u; i++) {
        const int32_t x = SIM_MIN + (int32_t)(sim_random(&rng) % 48u);
        const int32_t y = SIM_MIN + (int32_t)(sim_random(&rng) % 48u);
        static const uint16_t kinds[6] = {BLOCK_ID_GRASS_BLOCK, BLOCK_ID_SAND,       BLOCK_ID_WATER,
                                          BLOCK_ID_LAVA,        BLOCK_ID_OAK_LEAVES, BLOCK_ID_FARMLAND};
        const uint16_t kind = kinds[sim_random(&rng) % 6u];
        sim_set(t, sim, x, y, kind == BLOCK_ID_GRASS_BLOCK ? 0 : 1 + (int32_t)(sim_random(&rng) % 8u), kind);
    }
    sim->tick.random_per_section = 64u;
    sim_run(sim, 600u);
    const uint64_t digest = sim_digest(t, sim, 0, 12);
    uint32_t grass = 0u;
    for (int32_t y = SIM_MIN; y < SIM_END; y++) {
        for (int32_t x = SIM_MIN; x < SIM_END; x++) {
            grass += sim_at(t, sim, x, y, 0) == BLOCK_ID_GRASS_BLOCK ? 1u : 0u;
        }
    }
    *out_grass = grass;
    sim_end(sim);
    return digest;
}

TEST(the_same_seed_and_edits_make_the_same_world_and_another_seed_does_not) {
    uint32_t grass_a = 0u, grass_b = 0u, grass_c = 0u;
    const uint64_t a = sim_busy_world(t, 77u, &grass_a);
    const uint64_t b = sim_busy_world(t, 77u, &grass_b);
    const uint64_t c = sim_busy_world(t, 78u, &grass_c);
    CHECK_EQ_U64(a, b);
    CHECK(a != c);
    CHECK(grass_a > 0u);
}

/* ---- Real terrain ---------------------------------------------------------------------------------------------------- */

typedef struct sim_terrain_t {
    uint16_t *borrowed_states;
    uint8_t *borrowed_opened;
} sim_terrain_t;
static_assert(sizeof(sim_terrain_t) == 16);

/* Every column of the map, as game does it: filled past the log, then told to light. */
static void sim_fill_from_terrain(test_context_t *t, sim_t *sim, const terrain_t *terrain, const sim_terrain_t *scratch,
                                  int32_t origin_x, int32_t origin_y) {
    world_map_request_t request;
    while (world_map_stream_take(&sim->map, &request, 1u) == 1u) {
        column_t *const column = world_map_stream_commit(&sim->map, &request);
        REQUIRE(column != nullptr);
        const terrain_density_box_t box = {
            .origin_x = origin_x + request.chunk_x * (int32_t)COLUMN_SIZE,
            .origin_y = origin_y + request.chunk_y * (int32_t)COLUMN_SIZE,
            .origin_z = COLUMN_Z_STORAGE_MIN,
            .count_x = COLUMN_SIZE,
            .count_y = COLUMN_SIZE,
            .count_z = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
        };
        terrain_generate_box(terrain, &box, scratch->borrowed_opened, scratch->borrowed_states);
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            REQUIRE(section_blocks_import(&column->sections[section], &sim->pool,
                                          scratch->borrowed_states + (uint64_t)section * SECTION_VOXELS));
        }
        world_edit_column_filled(&sim->edit, request.slot);
        CHECK_EQ_U64(world_light_column_added(&sim->light, request.slot), WORLD_LIGHT_STATUS_OK);
    }
}

static void sim_block_counts(const sim_t *sim, uint64_t *counts) {
    memset(counts, 0, BLOCK_COUNT * sizeof(uint64_t));
    for (uint32_t i = 0u; i < sim->map.active_count; i++) {
        const uint32_t slot = sim->map.borrowed_active[i];
        const column_t *const column = &sim->map.borrowed_columns[slot];
        for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
            for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel++) {
                counts[section_block_get(&column->sections[section], voxel)]++;
            }
        }
    }
}

TEST(real_terrain_runs_on_its_own_without_flooding_or_stripping_its_trees) {
    terrain_t *const terrain = test_alloc(t, sizeof(terrain_t) + 64u);
    terrain_t *const aligned = (terrain_t *)(((uintptr_t)terrain + 63u) & ~(uintptr_t)63u);
    REQUIRE(terrain_init(aligned, 0x1234567890ABCDEFull));
    const uint64_t voxels = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS;
    const sim_terrain_t scratch = {
        .borrowed_states = test_alloc(t, voxels * sizeof(uint16_t)),
        .borrowed_opened = test_alloc(t, voxels),
    };
    uint64_t *const before = test_alloc(t, BLOCK_COUNT * sizeof(uint64_t));
    uint64_t *const after = test_alloc(t, BLOCK_COUNT * sizeof(uint64_t));

    /* Places far enough apart to differ, among them some with forest. */
    static const int32_t places[3][2] = {{0, 0}, {4096, -2048}, {-8192, 16384}};
    uint64_t leaves_before = 0u, leaves_after = 0u;
    for (uint32_t place = 0u; place < 3u; place++) {
        sim_t *const sim = sim_start(t, SIM_NODES, SIM_PAGES, 5u);
        sim_fill_from_terrain(t, sim, aligned, &scratch, places[place][0], places[place][1]);
        (void)world_light_update(&sim->light);
        sim_block_counts(sim, before);

        const int64_t started = time_now_ns();
        for (uint32_t i = 0u; i < 400u; i++) {
            world_tick_step(&sim->tick);
            (void)world_light_update(&sim->light);
        }
        const int64_t spent = time_now_ns() - started;
        sim_block_counts(sim, after);
        /* Nothing woke, and so no fluid moved. */
        CHECK_EQ_U64(world_tick_pending(&sim->tick), 0u);
        CHECK_EQ_U64(after[BLOCK_ID_WATER], before[BLOCK_ID_WATER]);
        CHECK_EQ_U64(after[BLOCK_ID_LAVA], before[BLOCK_ID_LAVA]);
        CHECK_EQ_U64(after[BLOCK_ID_SAND], before[BLOCK_ID_SAND]);
        CHECK_EQ_U64(after[BLOCK_ID_GRAVEL], before[BLOCK_ID_GRAVEL]);
        for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
            if ((block_defs[block].flags & BLOCK_FLAG_DECAYS) != 0u) {
                leaves_before += before[block];
                leaves_after += after[block];
            }
        }
        printf("place %" PRIu32 ": %" PRIu64 " random-ticked voxels, %.3f ms a tick over 21 columns\n", place,
               sim->tick.random_count, (libc_double_t)(time_ns_to_seconds(spent) * 1000.0f64 / 400.0f64));
        sim_end(sim);
    }
    printf("leaves in 400 ticks: %" PRIu64 " to %" PRIu64 "\n", leaves_before, leaves_after);
    /* 400 ticks is 0.3 picks a voxel: if generated leaves were not held up, a good share of them would go. */
    CHECK(leaves_before == 0u || leaves_after * 100u >= leaves_before * 99u);
    terrain_deinit(aligned);
}

/* ---- world_open ------------------------------------------------------------------------------------------------------ */

TEST(world_open_starts_it_within_what_it_said_it_needs_and_a_step_touches_no_heap) {
    world_limits_t limits = world_limits_default();
    limits.column_count = 25u;
    limits.section_bytes = 64ull << 20;
    uint8_t *const raw = test_alloc(t, sizeof(world_t) + 64u);
    world_t *const world = (world_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
    REQUIRE(world_init(world, limits));
    REQUIRE(world_open(world, 2u, 0, 0));
    CHECK(world->arena_columns.used <= world_open_bytes(2u));
    CHECK_EQ_U64(world->tick.node_capacity, WORLD_TICK_NODE_CAPACITY_DEFAULT);
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        REQUIRE(world_map_stream_commit(&world->map, &request) != nullptr);
    }
    REQUIRE(world_edit_block_set(&world->edit, 3, 3, 60, BLOCK_ID_WATER) == WORLD_EDIT_STATUS_CHANGED);
    heap_counters_t heap_before;
    heap_counters_read(&heap_before);
    for (uint32_t i = 0u; i < 120u; i++) {
        world_tick_step(&world->tick);
    }
    heap_counters_t heap_after;
    heap_counters_read(&heap_after);
    CHECK_EQ_U64(heap_after.reserve_calls, heap_before.reserve_calls);
    CHECK_EQ_U64(world_edit_block_set(&world->edit, 3, 3, 60, BLOCK_ID_WATER), WORLD_EDIT_STATUS_UNCHANGED);
    uint16_t state = BLOCK_ID_AIR;
    CHECK_EQ_U64(world_map_block_get(&world->map, 3, 3, 40, &state), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(state, BLOCK_ID_WATER);
    world_close(world);
    world_deinit(world);
}

TEST_MAIN()
