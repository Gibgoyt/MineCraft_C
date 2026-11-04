/* ctest run_game_spawn. See README.md. */
#include "harness/index.h"

#include "game/spawn/index.h"
#include "machine/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "terrain/rng/index.h"
#include "time/index.h"
#include "types/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    SPAWN_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    /* Other seeds looked at, over and above the ones that are written down. */
    SPAWN_SWEEP_SEEDS = 64u,
    /* Columns made whole for each seed, to hold the fields to the blocks. */
    SPAWN_SHORE_COLUMNS = 200u,
};
enum : int32_t {
    /* How far the ground 4 blocks from a start may differ from the start's own, which stands on grass or the like. */
    SPAWN_NEAR_DIFFER_MAX = 7,
};
/* Plan 003 phase 8's seeds. Ahmed's is the first: it starts on a frozen sea 376 blocks from land if ice counts. */
static const uint64_t SPAWN_SEEDS[] = {
    2198175988018515243ull, 12636146071196537873ull, 0x4D494E4543524146ull, 1ull, 42ull, 20261104ull,
    9999999999999ull,       18446744073709551557ull,
};
enum : uint32_t { SPAWN_SEED_COUNT = sizeof(SPAWN_SEEDS) / sizeof(SPAWN_SEEDS[0]) };

/* What the guardian's watchdog waits, as run/src/game/session/README.md says. */
static const int64_t SPAWN_WATCHDOG_NS = 8 * TIME_NS_PER_SECOND;

/* A column of air, with these from the bottom of its storage up to a top at z = 0. */
typedef struct spawn_stack_t {
    uint16_t blocks[4]; /* 0 */
} spawn_stack_t;
static_assert(sizeof(spawn_stack_t) == 8);

static bool spawn_stack_is_land(uint16_t *states, const spawn_stack_t *stack) {
    memset(states, 0, (uint64_t)SPAWN_LAYERS * sizeof(uint16_t));
    const int32_t ground_z = 0;
    const int32_t top = ground_z - COLUMN_Z_STORAGE_MIN;
    states[top] = stack->blocks[0];
    states[top - 1] = stack->blocks[1];
    states[top - 2] = stack->blocks[2];
    states[top - 3] = stack->blocks[3];
    return game_spawn_top_is_dry_land(states, ground_z);
}

TEST(what_is_land_and_what_is_not) {
    uint16_t *const states = test_alloc(t, (uint64_t)SPAWN_LAYERS * sizeof(uint16_t));
    typedef struct spawn_case_t {
        spawn_stack_t stack; /* 0: from the top down */
        uint8_t land;        /* 8 */
        uint8_t _pad0[1];    /* 9 */
    } spawn_case_t;
    static_assert(sizeof(spawn_case_t) == 10);
    const spawn_case_t cases[] = {
        /* The bug: ice on the sea, as far down as the sea goes. */
        {.stack = {{BLOCK_ID_ICE, BLOCK_ID_WATER, BLOCK_ID_WATER, BLOCK_ID_WATER}}, .land = 0u},
        /* Frozen right through, and on rock. */
        {.stack = {{BLOCK_ID_ICE, BLOCK_ID_ICE, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 0u},
        {.stack = {{BLOCK_ID_ICE, BLOCK_ID_STONE, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 0u},
        /* An iceberg, and a cold peak. */
        {.stack = {{BLOCK_ID_PACKED_ICE, BLOCK_ID_PACKED_ICE, BLOCK_ID_WATER, BLOCK_ID_WATER}}, .land = 0u},
        {.stack = {{BLOCK_ID_BLUE_ICE, BLOCK_ID_STONE, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 0u},
        /* Snow is as dry as what it lies on. */
        {.stack = {{BLOCK_ID_SNOW_BLOCK, BLOCK_ID_ICE, BLOCK_ID_WATER, BLOCK_ID_WATER}}, .land = 0u},
        {.stack = {{BLOCK_ID_SNOW_BLOCK, BLOCK_ID_SNOW_BLOCK, BLOCK_ID_WATER, BLOCK_ID_WATER}}, .land = 0u},
        {.stack = {{BLOCK_ID_SNOW_BLOCK, BLOCK_ID_PACKED_ICE, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 0u},
        {.stack = {{BLOCK_ID_SNOW_BLOCK, BLOCK_ID_GRASS_BLOCK, BLOCK_ID_DIRT, BLOCK_ID_STONE}}, .land = 1u},
        {.stack = {{BLOCK_ID_SNOW_BLOCK, BLOCK_ID_STONE, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 1u},
        /* Water and lava, on top and right under it. */
        {.stack = {{BLOCK_ID_WATER, BLOCK_ID_SAND, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 0u},
        {.stack = {{BLOCK_ID_LAVA, BLOCK_ID_STONE, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 0u},
        {.stack = {{BLOCK_ID_STONE, BLOCK_ID_WATER, BLOCK_ID_WATER, BLOCK_ID_SAND}}, .land = 0u},
        {.stack = {{BLOCK_ID_STONE, BLOCK_ID_LAVA, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 0u},
        /* What grows. */
        {.stack = {{BLOCK_ID_OAK_LEAVES, BLOCK_ID_OAK_LEAVES, BLOCK_ID_OAK_LOG, BLOCK_ID_OAK_LOG}}, .land = 0u},
        {.stack = {{BLOCK_ID_TALL_GRASS, BLOCK_ID_GRASS_BLOCK, BLOCK_ID_DIRT, BLOCK_ID_STONE}}, .land = 0u},
        {.stack = {{BLOCK_ID_LILY_PAD, BLOCK_ID_WATER, BLOCK_ID_WATER, BLOCK_ID_SAND}}, .land = 0u},
        {.stack = {{BLOCK_ID_CACTUS, BLOCK_ID_CACTUS, BLOCK_ID_SAND, BLOCK_ID_SAND}}, .land = 0u},
        /* Ground. */
        {.stack = {{BLOCK_ID_GRASS_BLOCK, BLOCK_ID_DIRT, BLOCK_ID_DIRT, BLOCK_ID_STONE}}, .land = 1u},
        {.stack = {{BLOCK_ID_SAND, BLOCK_ID_SAND, BLOCK_ID_SANDSTONE, BLOCK_ID_STONE}}, .land = 1u},
        {.stack = {{BLOCK_ID_STONE, BLOCK_ID_STONE, BLOCK_ID_STONE, BLOCK_ID_STONE}}, .land = 1u},
        /* Over a cave, with the roof in between. */
        {.stack = {{BLOCK_ID_DIRT, BLOCK_ID_AIR, BLOCK_ID_AIR, BLOCK_ID_STONE}}, .land = 1u},
    };
    for (uint32_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK_EQ_U64(spawn_stack_is_land(states, &cases[i].stack), cases[i].land);
    }
}

TEST(the_bottom_of_a_column_is_read_safely) {
    uint16_t *const states = test_alloc(t, (uint64_t)SPAWN_LAYERS * sizeof(uint16_t));
    states[0] = BLOCK_ID_BEDROCK;
    CHECK(game_spawn_top_is_dry_land(states, COLUMN_Z_STORAGE_MIN));
    states[0] = BLOCK_ID_SNOW_BLOCK;
    CHECK(game_spawn_top_is_dry_land(states, COLUMN_Z_STORAGE_MIN));
}

/* A probe that keeps count, and takes the one place it is told to, if any. */
typedef struct spawn_probe_t {
    uint64_t asked;        /* 0 */
    uint64_t went_inwards; /* 8 */
    uint64_t out_of_reach; /* 16 */
    int32_t land_x;        /* 24 */
    int32_t land_y;        /* 28 */
    int32_t middle_x;      /* 32 */
    int32_t middle_y;      /* 36 */
    int32_t last_distance; /* 40 */
    uint8_t has_land;      /* 44 */
    uint8_t _pad0[3];      /* 45 */
} spawn_probe_t;
static_assert(sizeof(spawn_probe_t) == 48);

static bool spawn_probe_ask(void *borrowed_context, int32_t x, int32_t y) {
    spawn_probe_t *const probe = borrowed_context;
    probe->asked++;
    const int32_t dx = x - probe->middle_x, dy = y - probe->middle_y;
    const int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    const int32_t distance = ax > ay ? ax : ay;
    probe->went_inwards += distance < probe->last_distance ? 1u : 0u;
    probe->last_distance = distance;
    probe->out_of_reach += distance > GAME_SPAWN_REACH ? 1u : 0u;
    return probe->has_land != 0u && x == probe->land_x && y == probe->land_y;
}

static void spawn_alive_count(void *context) {
    uint64_t *const count = context;
    (*count)++;
}

/* A game that has only someone to tell. */
static game_t *spawn_game_for_alive(test_context_t *t, uint64_t *count) {
    uint8_t *const memory = test_alloc(t, sizeof(game_t) + (uint64_t)alignof(game_t));
    game_t *const game = (game_t *)(memory + ((uint64_t)alignof(game_t) - (uintptr_t)memory % alignof(game_t)));
    game_open_alive_set(game, spawn_alive_count, count);
    return game;
}

TEST(no_land_anywhere_ends_at_the_middle_having_looked_everywhere) {
    uint64_t told = 0u;
    const game_t *const game = spawn_game_for_alive(t, &told);
    spawn_probe_t probe = {.middle_x = GAME_WORLD_MIDDLE, .middle_y = GAME_WORLD_MIDDLE};
    int32_t x = 1, y = 1;
    CHECK(!game_spawn_walk(game, spawn_probe_ask, &probe, GAME_WORLD_MIDDLE, GAME_WORLD_MIDDLE, &x, &y));
    CHECK_EQ_I64(x, GAME_WORLD_MIDDLE);
    CHECK_EQ_I64(y, GAME_WORLD_MIDDLE);
    CHECK_EQ_U64(probe.asked, GAME_SPAWN_PLACES);
    CHECK_EQ_U64(told, GAME_SPAWN_SQUARES);
    CHECK_EQ_U64(probe.went_inwards, 0u);
    CHECK_EQ_U64(probe.out_of_reach, 0u);
    CHECK_EQ_I64(probe.last_distance, GAME_SPAWN_REACH);
}

TEST(a_walk_needs_no_game) {
    spawn_probe_t probe = {.middle_x = -5, .middle_y = 7};
    int32_t x = 1, y = 1;
    CHECK(!game_spawn_walk(nullptr, spawn_probe_ask, &probe, -5, 7, &x, &y));
    CHECK_EQ_I64(x, -5);
    CHECK_EQ_I64(y, 7);
    CHECK_EQ_U64(probe.asked, GAME_SPAWN_PLACES);
}

TEST(land_is_found_in_each_band_and_past_where_the_old_search_stopped) {
    /* Offsets from the middle: the middle itself, on each band's grid, the corners of the last. The old search stopped at 1024. */
    const int32_t places[][2] = {
        {0, 0},      {16, 0},       {0, -16},    {1024, 1024}, {-1024, 1024},  {1088, 0},        {0, -1088},
        {8192, -64}, {-8192, 8192}, {8448, 256}, {-256, 8448}, {32768, 32768}, {-32768, -32768}, {32768, -256},
    };
    for (uint32_t i = 0u; i < sizeof(places) / sizeof(places[0]); i++) {
        spawn_probe_t probe = {
            .middle_x = GAME_WORLD_MIDDLE,
            .middle_y = GAME_WORLD_MIDDLE,
            .land_x = GAME_WORLD_MIDDLE + places[i][0],
            .land_y = GAME_WORLD_MIDDLE + places[i][1],
            .has_land = 1u,
        };
        int32_t x = 0, y = 0;
        CHECK(game_spawn_walk(nullptr, spawn_probe_ask, &probe, GAME_WORLD_MIDDLE, GAME_WORLD_MIDDLE, &x, &y));
        CHECK_EQ_I64(x, probe.land_x);
        CHECK_EQ_I64(y, probe.land_y);
        CHECK(probe.asked <= GAME_SPAWN_PLACES);
    }
    /* Just past the reach is not looked at. */
    spawn_probe_t beyond = {
        .middle_x = 0,
        .middle_y = 0,
        .land_x = GAME_SPAWN_REACH + 256,
        .land_y = 0,
        .has_land = 1u,
    };
    int32_t x = 1, y = 1;
    CHECK(!game_spawn_walk(nullptr, spawn_probe_ask, &beyond, 0, 0, &x, &y));
    CHECK_EQ_I64(x, 0);
    CHECK_EQ_I64(y, 0);
}

/* Land at all of these: which is taken says what order the walk goes in. */
static bool spawn_probe_ring_of_two(void *borrowed_context, int32_t x, int32_t y) {
    (void)borrowed_context;
    return (x == 32 && y == 32) || (x == 16 && y == 0);
}

TEST(the_nearest_square_is_taken) {
    int32_t x = 0, y = 0;
    CHECK(game_spawn_walk(nullptr, spawn_probe_ring_of_two, nullptr, 0, 0, &x, &y));
    CHECK_EQ_I64(x, 16);
    CHECK_EQ_I64(y, 0);
}

static game_column_job_t *spawn_job_make(test_context_t *t) {
    uint8_t *const memory = test_alloc(t, sizeof(game_column_job_t) + (uint64_t)alignof(game_column_job_t));
    game_column_job_t *const job = (game_column_job_t *)(memory + ((uint64_t)alignof(game_column_job_t) -
                                                                   (uintptr_t)memory % alignof(game_column_job_t)));
    job->borrowed_states = test_alloc(t, (uint64_t)SPAWN_LAYERS * sizeof(uint16_t));
    job->borrowed_opened = test_alloc(t, SPAWN_LAYERS);
    return job;
}

/* What is on top of this place, from every stage, with nothing of the search's in between. */
static uint16_t spawn_real_top(const terrain_t *terrain, const game_column_job_t *job, int32_t x, int32_t y,
                               int32_t *out_z) {
    const terrain_density_box_t box = {
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = 1u,
        .count_y = 1u,
        .count_z = SPAWN_LAYERS,
    };
    terrain_generate_box(terrain, &box, job->borrowed_opened, job->borrowed_states);
    int32_t storage = (int32_t)SPAWN_LAYERS - 1;
    while (storage > 0 && job->borrowed_states[storage] == BLOCK_ID_AIR) {
        storage--;
    }
    *out_z = storage + COLUMN_Z_STORAGE_MIN;
    return job->borrowed_states[storage];
}

/* A body starts on real land, on this seed, and twice in the same place. Returns how long the first search took. */
static int64_t spawn_check_seed(test_context_t *t, const game_column_job_t *job, uint64_t seed, int32_t *out_x,
                                int32_t *out_y) {
    terrain_t terrain;
    CHECK(terrain_init(&terrain, seed));
    const int64_t started = time_now_ns();
    int32_t x = 0, y = 0;
    const bool found = game_spawn_find(nullptr, &terrain, job, GAME_WORLD_MIDDLE, GAME_WORLD_MIDDLE, &x, &y);
    const int64_t took = time_now_ns() - started;
    CHECK(found);
    int32_t again_x = 0, again_y = 0;
    CHECK(game_spawn_find(nullptr, &terrain, job, GAME_WORLD_MIDDLE, GAME_WORLD_MIDDLE, &again_x, &again_y));
    CHECK_EQ_I64(again_x, x);
    CHECK_EQ_I64(again_y, y);
    int32_t ground_z = 0;
    const uint16_t top = spawn_real_top(&terrain, job, x, y, &ground_z);
    CHECK_EQ_I64(game_ground_z(&terrain, job, x, y), ground_z);
    CHECK(game_spawn_top_is_dry_land(job->borrowed_states, ground_z));
    CHECK(top != BLOCK_ID_ICE && top != BLOCK_ID_PACKED_ICE && top != BLOCK_ID_BLUE_ICE && top != BLOCK_ID_WATER);
    CHECK(ground_z >= MC_WORLD_Z_SEA);
    /* Low and gentle, by the real blocks as well as the fields: no fall or wall in reach of the first few steps, what grows left out. */
    CHECK(game_spawn_is_gentle(&terrain, x, y));
    CHECK(ground_z <= 150);
    static const int32_t AXES[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (uint32_t axis = 0u; axis < 4u; axis++) {
        const int32_t z_near = game_spawn_bare_ground_z(&terrain, job, x + AXES[axis][0] * 4, y + AXES[axis][1] * 4);
        const int32_t near_differs = z_near < ground_z ? ground_z - z_near : z_near - ground_z;
        CHECK(near_differs <= SPAWN_NEAR_DIFFER_MAX);
    }
    printf("seed %" PRIu64 ": starts at %d, %d, %d on %s, found in %.1f ms\n", seed, x, y, ground_z + 1,
           block_defs[top].name, (libc_double_t)((float64_t)took / 1.0e6f64));
    terrain_deinit(&terrain);
    *out_x = x;
    *out_y = y;
    return took;
}

TEST(every_seed_written_down_starts_on_land) {
    const game_column_job_t *const job = spawn_job_make(t);
    for (uint32_t i = 0u; i < SPAWN_SEED_COUNT; i++) {
        int32_t x = 0, y = 0;
        CHECK(spawn_check_seed(t, job, SPAWN_SEEDS[i], &x, &y) < SPAWN_WATCHDOG_NS);
    }
}

TEST(many_seeds_start_on_land) {
    const game_column_job_t *const job = spawn_job_make(t);
    uint64_t seed = 0xD1CE5EEDu;
    int64_t slowest = 0;
    int64_t furthest = 0;
    for (uint32_t i = 0u; i < SPAWN_SWEEP_SEEDS; i++) {
        seed = terrain_rng_derive(seed, i);
        int32_t x = 0, y = 0;
        const int64_t took = spawn_check_seed(t, job, seed, &x, &y);
        slowest = took > slowest ? took : slowest;
        const int64_t ax = x < 0 ? -(int64_t)x : (int64_t)x, ay = y < 0 ? -(int64_t)y : (int64_t)y;
        const int64_t distance = ax > ay ? ax : ay;
        furthest = distance > furthest ? distance : furthest;
    }
    printf("%u seeds: slowest search %.1f ms, furthest start %" PRId64 " blocks\n", SPAWN_SWEEP_SEEDS,
           (libc_double_t)((float64_t)slowest / 1.0e6f64), furthest);
    CHECK(slowest < SPAWN_WATCHDOG_NS);
}

/* The cheap question gives up on a place only where every stage agrees it is not land. Random places, to the block. */
TEST(the_fields_never_rule_out_real_land) {
    const game_column_job_t *const job = spawn_job_make(t);
    uint64_t made = 0u, ruled_out = 0u, lost = 0u, land = 0u;
    int64_t making_ns = 0;
    for (uint32_t i = 0u; i < SPAWN_SEED_COUNT; i++) {
        terrain_t terrain;
        CHECK(terrain_init(&terrain, SPAWN_SEEDS[i]));
        uint64_t stream = terrain_rng_derive(SPAWN_SEEDS[i], terrain_rng_salt("test/spawn"));
        for (uint32_t c = 0u; c < SPAWN_SHORE_COLUMNS; c++) {
            stream = terrain_rng_derive(stream, c);
            /* Within the middle's 2 km, to the block. */
            const int32_t x = GAME_WORLD_MIDDLE + (int32_t)(stream % 4096u) - 2048;
            const int32_t y = GAME_WORLD_MIDDLE + (int32_t)((stream >> 16u) % 4096u) - 2048;
            const terrain_hydrology_column_t column = terrain_hydrology_column_at(terrain.borrowed_hydrology, x, y);
            int32_t ground_z = 0;
            const int64_t started = time_now_ns();
            (void)spawn_real_top(&terrain, job, x, y, &ground_z);
            making_ns += time_now_ns() - started;
            made++;
            const bool dry = game_spawn_top_is_dry_land(job->borrowed_states, ground_z);
            land += dry ? 1u : 0u;
            if (!game_spawn_may_be_dry(&column)) {
                ruled_out++;
                lost += dry ? 1u : 0u;
            }
        }
        terrain_deinit(&terrain);
    }
    printf("%" PRIu64 " columns made, each %.2f ms: %" PRIu64 " dry land, %" PRIu64 " ruled out by the fields, %" PRIu64
           " of those wrongly\n",
           made, (libc_double_t)((float64_t)making_ns / (float64_t)made / 1.0e6f64), land, ruled_out, lost);
    CHECK_EQ_U64(lost, 0u);
    /* It does save something, and something is left to find. */
    CHECK(ruled_out > made / 8u);
    CHECK(land > made / 8u);
}

/* The longest a game can be kept waiting: a whole walk with nothing but the sea's fields to read. */
static bool spawn_probe_fields_only(void *borrowed_context, int32_t x, int32_t y) {
    const terrain_t *const terrain = borrowed_context;
    const terrain_hydrology_column_t column = terrain_hydrology_column_at(terrain->borrowed_hydrology, x, y);
    (void)game_spawn_may_be_dry(&column);
    return false;
}

TEST(a_whole_walk_of_fields_is_well_inside_the_watchdog) {
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SPAWN_SEEDS[0]));
    const int64_t started = time_now_ns();
    int32_t x = 0, y = 0;
    CHECK(!game_spawn_walk(nullptr, spawn_probe_fields_only, &terrain, 0, 0, &x, &y));
    const int64_t took = time_now_ns() - started;
    printf("%u places by their fields alone: %.1f ms\n", GAME_SPAWN_PLACES,
           (libc_double_t)((float64_t)took / 1.0e6f64));
    CHECK(took < SPAWN_WATCHDOG_NS);
    terrain_deinit(&terrain);
}

TEST_MAIN()
