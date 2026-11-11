/* ctest run_mob_path. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "mob/index.h"
#include "tables/blocks/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A body of two blocks: 1.95 rounded up to half blocks. */
enum : uint32_t {
    MP_BODY = 4u,
    MP_MODEL_SIDE = 24u,
    MP_MODEL_CELLS = MP_MODEL_SIDE * MP_MODEL_SIDE,
    MP_MODEL_WORLDS = 20u,
    MP_MODEL_PAIRS = 20u,
    MP_MODEL_HEIGHT_MAX = 4u,
    MP_MODEL_WALL = 9u,
    MP_MODEL_UNREACHED = UINT32_MAX,
    MP_EDIT_STEPS = 4000u,
    MP_EDIT_AREA = 16u,
    MP_EDIT_PLACES = 8u,
    MP_WORKER_ROUNDS = 8u,
    MP_CROWD = 16u,
    MP_WALK_TICKS = 1200u,
};
/* A speed like a zombie's. */
#define MP_WALK_SPEED 0.2f64
#define MP_NEAR 0.5f64

typedef struct mp_rig_t {
    test_world_t world;          /* 0 */
    entity_t *entity;            /* 24 */
    mob_t *mob;                  /* 32 */
    mob_path_t *path;            /* 40 */
    mob_path_scratch_t *scratch; /* 48 */
    mob_path_found_t *found;     /* 56 */
    job_system_t *job;           /* 64: nullptr: no workers */
} mp_rig_t;
static_assert(sizeof(mp_rig_t) == 72);

static bool mp_begin_with(test_context_t *t, mp_rig_t *rig, job_system_t *job) {
    if (!test_world_begin(t, &rig->world)) {
        return false;
    }
    rig->job = job;
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    rig->scratch = test_world_aligned(t, sizeof(mob_path_scratch_t));
    rig->found = test_world_aligned(t, sizeof(mob_path_found_t));
    if (!CHECK(entity_init(rig->entity, rig->world.physics)) || !CHECK(mob_init(rig->mob, job))) {
        return false;
    }
    entity_open(rig->entity, 5u);
    mob_open(rig->mob, rig->world.world, 5u);
    rig->path = &rig->mob->path;
    return true;
}

static bool mp_begin(test_context_t *t, mp_rig_t *rig) {
    return mp_begin_with(t, rig, nullptr);
}

static void mp_end(mp_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    test_world_end(&rig->world);
}

static bool mp_job_begin(test_context_t *t, job_system_t **out) {
    *out = test_world_aligned(t, sizeof(job_system_t));
    thread_topology_t *const topology = test_world_aligned(t, sizeof(thread_topology_t));
    return CHECK(thread_topology_read(topology)) && CHECK(job_init(*out, topology));
}

/* The usual ground: stone under z = 0. */
static void mp_floor(test_context_t *t, mp_rig_t *rig) {
    test_world_fill(t, &rig->world, -16, -16, -1, 47, 31, -1, BLOCK_ID_STONE);
    test_world_fill(t, &rig->world, 48, 0, -1, 63, 15, -1, BLOCK_ID_STONE);
}

static void mp_fill(test_context_t *t, mp_rig_t *rig, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                    int32_t z1, uint16_t block) {
    test_world_fill(t, &rig->world, x0, y0, z0, x1, y1, z1, block);
}

/* Walls along y = -1 and y = 1, too high to jump, so that the only way is along y = 0. */
static void mp_corridor(test_context_t *t, mp_rig_t *rig, int32_t x0, int32_t x1) {
    mp_fill(t, rig, x0, 1, 0, x1, 1, 8, BLOCK_ID_STONE);
    mp_fill(t, rig, x0, -1, 0, x1, -1, 8, BLOCK_ID_STONE);
    mp_fill(t, rig, x0 - 1, -1, 0, x0 - 1, 1, 8, BLOCK_ID_STONE);
    mp_fill(t, rig, x1 + 1, -1, 0, x1 + 1, 1, 8, BLOCK_ID_STONE);
}

static mob_path_ask_t mp_ask(int32_t sx, int32_t sy, int32_t sz, int32_t gx, int32_t gy, int32_t gz) {
    return (mob_path_ask_t){
        .start = {sx, sy, sz},
        .goal = {gx, gy, gz},
        .nodes = (uint16_t)MOB_PATH_NODES_DEFAULT,
        .height = (uint8_t)MP_BODY,
        .drop = (uint8_t)MOB_PATH_DROP_DEFAULT,
    };
}

static const mob_path_found_t *mp_search(mp_rig_t *rig, const mob_path_ask_t *ask) {
    mob_path_search(rig->world.map, ask, rig->scratch, rig->found);
    return rig->found;
}

static const mob_path_point_t *mp_last(const mob_path_found_t *found) {
    return &found->points[found->point_count - 1u];
}

/* A handle no store made, whose slot is this number. */
static entity_handle_t mp_handle(uint32_t slot) {
    return (1u << ENTITY_STORE_SLOT_BITS) | slot;
}

/* ---- Each kind of move, by hand -------------------------------------------------------------------------------------------- */

TEST(a_straight_line_costs_what_it_says_point_by_point) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 5, 0, 0);
    const mob_path_found_t *const found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    REQUIRE(found->point_count == 6u);
    for (uint32_t i = 0u; i < 6u; i++) {
        CHECK_EQ_I64(found->points[i].x, (int64_t)i);
        CHECK_EQ_I64(found->points[i].y, 0);
        CHECK_EQ_I64(found->points[i].z, 0);
        CHECK_EQ_U64(found->points[i].cost, 100u * i);
        CHECK_EQ_U64(found->points[i].half, 0u);
    }
    mp_end(&rig);
}

TEST(diagonals_cost_141_and_mix_with_straight_steps) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t three = mp_ask(0, 0, 0, 3, 3, 0);
    CHECK_EQ_U64(mp_last(mp_search(&rig, &three))->cost, 3u * 141u);
    CHECK_EQ_U64(rig.found->point_count, 4u);
    const mob_path_ask_t mixed = mp_ask(0, 0, 0, 5, -2, 0);
    CHECK_EQ_U64(mp_last(mp_search(&rig, &mixed))->cost, 2u * 141u + 3u * 100u);
    /* Below zero and across chunks, all the same. */
    const mob_path_ask_t negative = mp_ask(-3, -3, 0, -12, -3, 0);
    CHECK_EQ_U64(mp_last(mp_search(&rig, &negative))->cost, 900u);
    mp_end(&rig);
}

TEST(a_wall_is_walked_round_without_cutting_its_corners) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_fill(t, &rig, 3, -2, 0, 3, 2, 1, BLOCK_ID_STONE);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 6, 0, 0);
    const mob_path_found_t *const found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    /* To (2, 3): two diagonals and a step. Along to (4, 3): two steps. Down to (6, 0): two diagonals and a step. */
    CHECK_EQ_U64(mp_last(found)->cost, 964u);
    for (uint32_t i = 0u; i < found->point_count; i++) {
        CHECK(found->points[i].x != 3 || found->points[i].y > 2 || found->points[i].y < -2);
    }
    mp_end(&rig);
}

TEST(a_block_is_jumped_and_two_are_not) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 0, BLOCK_ID_STONE);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 6, 0, 0);
    const mob_path_found_t *found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    REQUIRE(found->point_count == 7u);
    CHECK_EQ_I64(found->points[3].x, 3);
    CHECK_EQ_I64(found->points[3].z, 1);
    CHECK_EQ_U64(found->points[3].cost, 200u + 100u + 60u);
    CHECK_EQ_I64(found->points[4].z, 0);
    CHECK_EQ_U64(found->points[4].cost, 360u + 100u + 10u);
    CHECK_EQ_U64(found->points[6].cost, 670u);

    mp_fill(t, &rig, 3, 0, 1, 3, 0, 1, BLOCK_ID_STONE);
    found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_I64(mp_last(found)->x, 2);
    mp_end(&rig);
}

TEST(a_jump_wants_room_overhead_here_and_there) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 0, BLOCK_ID_STONE);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 6, 0, 0);
    /* Over where it jumps from. */
    mp_fill(t, &rig, 2, 0, 2, 2, 0, 2, BLOCK_ID_STONE);
    CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_PARTIAL);
    mp_fill(t, &rig, 2, 0, 2, 2, 0, 2, BLOCK_ID_AIR);
    /* Over where it lands. */
    mp_fill(t, &rig, 3, 0, 2, 3, 0, 2, BLOCK_ID_STONE);
    CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_PARTIAL);
    mp_fill(t, &rig, 3, 0, 2, 3, 0, 2, BLOCK_ID_AIR);
    /* A cell higher is no matter to a body of two blocks. */
    mp_fill(t, &rig, 3, 0, 3, 3, 0, 3, BLOCK_ID_STONE);
    mp_fill(t, &rig, 2, 0, 3, 2, 0, 3, BLOCK_ID_STONE);
    CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_COMPLETE);
    /* A body too tall for what is left. */
    mob_path_ask_t tall = ask;
    tall.height = 6u;
    CHECK_EQ_U64(mp_search(&rig, &tall)->status, MOB_PATH_STATUS_PARTIAL);
    mp_end(&rig);
}

TEST(a_fall_is_taken_up_to_what_it_was_asked) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_fill(t, &rig, -2, 0, 0, 2, 0, 3, BLOCK_ID_STONE);
    mob_path_ask_t ask = mp_ask(0, 0, 4, 3, 0, 0);
    const mob_path_found_t *found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_I64(mp_last(found)->z, 4);
    ask.start[0] = 2;
    ask.drop = 4u;
    found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    REQUIRE(found->point_count == 2u);
    CHECK_EQ_U64(found->points[1].cost, 100u + 8u * 5u);
    /* More than the most any ask may have is that most. A pillar with a ledge exactly that far down, and nothing else as near. */
    mp_fill(t, &rig, 30, 0, 0, 30, 0, 11, BLOCK_ID_STONE);
    mp_fill(t, &rig, 31, 0, 0, 31, 0, 3, BLOCK_ID_STONE);
    mob_path_ask_t far = mp_ask(30, 0, 12, 31, 0, 4);
    far.drop = 200u;
    found = mp_search(&rig, &far);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_U64(mp_last(found)->cost, 100u + 16u * 5u);
    mp_fill(t, &rig, 31, 0, 3, 31, 0, 3, BLOCK_ID_AIR);
    far.goal[2] = 3;
    found = mp_search(&rig, &far);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_NONE);
    mp_end(&rig);
}

TEST(a_slab_is_a_step_up_half_a_block_and_stairs_a_block) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 2, 0, 0, 2, 0, 0, BLOCK_ID_OAK_SLAB);
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 0, BLOCK_ID_STONE);
    const mob_path_ask_t up = mp_ask(0, 0, 0, 3, 0, 1);
    const mob_path_found_t *found = mp_search(&rig, &up);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    REQUIRE(found->point_count == 4u);
    CHECK_EQ_U64(found->points[2].half, 1u);
    CHECK_EQ_I64(found->points[2].z, 0);
    CHECK_EQ_U64(found->points[2].cost, 100u + 130u);
    CHECK_EQ_U64(found->points[3].cost, 230u + 130u);
    /* And back down it. */
    const mob_path_ask_t down = mp_ask(3, 0, 1, 1, 0, 0);
    found = mp_search(&rig, &down);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_U64(mp_last(found)->cost, 105u + 105u);

    mp_fill(t, &rig, 2, 0, 0, 2, 0, 0, BLOCK_ID_OAK_STAIRS);
    const mob_path_ask_t stairs = mp_ask(0, 0, 0, 2, 0, 1);
    found = mp_search(&rig, &stairs);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_U64(mp_last(found)->cost, 100u + 160u);
    CHECK_EQ_U64(mp_last(found)->half, 0u);
    mp_end(&rig);
}

TEST(nothing_goes_in_or_on_lava_or_cactus) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 6, 0, 0);
    /* A pit of lava one wide, and nothing in the plan jumps a gap. */
    mp_fill(t, &rig, 3, 0, -1, 3, 0, -1, BLOCK_ID_LAVA);
    CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_I64(mp_last(rig.found)->x, 2);
    mp_fill(t, &rig, 3, 0, -1, 3, 0, -1, BLOCK_ID_STONE);
    /* Lava at its feet. */
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 0, BLOCK_ID_LAVA);
    CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_PARTIAL);
    /* A cactus to walk into, and one to jump on. */
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 0, BLOCK_ID_CACTUS);
    CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_I64(mp_last(rig.found)->x, 2);
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 0, BLOCK_ID_STONE);
    CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_COMPLETE);
    mp_end(&rig);
}

TEST(water_is_swum_at_its_price_and_a_bank_climbed_out_of) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 3, 0, -2, 5, 0, -2, BLOCK_ID_STONE);
    mp_fill(t, &rig, 3, 0, -1, 5, 0, -1, BLOCK_ID_WATER);
    const mob_path_ask_t ask = mp_ask(2, 0, 0, 6, 0, 0);
    const mob_path_found_t *const found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    REQUIRE(found->point_count == 5u);
    CHECK_EQ_I64(found->points[1].z, -1);
    /* In: 100 + 150 + 2 halves down. Across: 250 twice. Out: 100 + 2 halves up. */
    CHECK_EQ_U64(found->points[1].cost, 260u);
    CHECK_EQ_U64(found->points[3].cost, 760u);
    CHECK_EQ_U64(found->points[4].cost, 920u);
    CHECK_EQ_I64(found->points[4].z, 0);
    mp_end(&rig);
}

TEST(a_ladder_is_climbed_to_the_top_and_off) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 3, BLOCK_ID_STONE);
    mp_fill(t, &rig, 2, 0, 0, 2, 0, 3, BLOCK_ID_LADDER);
    const mob_path_ask_t up = mp_ask(1, 0, 0, 3, 0, 4);
    const mob_path_found_t *found = mp_search(&rig, &up);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    REQUIRE(found->point_count == 5u);
    /* A jump on to its second rung is cheaper than a step and a climb. */
    CHECK_EQ_I64(found->points[1].z, 1);
    CHECK_EQ_U64(found->points[1].cost, 160u);
    CHECK_EQ_I64(found->points[3].x, 2);
    CHECK_EQ_I64(found->points[3].z, 3);
    /* Up two more: 200. Off the top, a block up: 160. */
    CHECK_EQ_U64(found->points[4].cost, 520u);
    /* And down: on to the top rung, 110, then off it and down three, 130, which beats climbing all the way. */
    const mob_path_ask_t down = mp_ask(3, 0, 4, 1, 0, 0);
    found = mp_search(&rig, &down);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_U64(mp_last(found)->cost, 240u);
    /* A fall it will not take is climbed. */
    mob_path_ask_t careful = down;
    careful.drop = 1u;
    found = mp_search(&rig, &careful);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    /* On the top rung, 110. Down two, 60. Off it and down one, 100 + 10. */
    CHECK_EQ_U64(mp_last(found)->cost, 280u);
    mp_end(&rig);
}

TEST(a_column_not_loaded_is_a_wall) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t ask = mp_ask(60, 0, 0, 70, 0, 0);
    const mob_path_found_t *found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_I64(mp_last(found)->x, 63);
    const mob_path_ask_t out = mp_ask(70, 0, 0, 60, 0, 0);
    found = mp_search(&rig, &out);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_START_BLOCKED);
    CHECK_EQ_U64(found->point_count, 0u);
    mp_end(&rig);
}

/* ---- The bounds ------------------------------------------------------------------------------------------------------------ */

TEST(a_budget_of_nodes_holds_and_gives_the_nearest_it_found) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mob_path_ask_t ask = mp_ask(0, 0, 0, 40, 0, 0);
    const mob_path_found_t *found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_U64(mp_last(found)->cost, 4000u);
    ask.nodes = 16u;
    found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK(found->nodes <= 16u);
    CHECK(mp_last(found)->x > 0);
    ask.nodes = 1u;
    found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_NONE);
    CHECK_EQ_U64(found->nodes, 1u);
    CHECK_EQ_U64(found->point_count, 0u);
    /* Asking for more than the most is asking for the most, and 0 for the usual. */
    ask.nodes = (uint16_t)(MOB_PATH_NODES_MAX * 2u);
    found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    ask.nodes = 0u;
    found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    mp_end(&rig);
}

TEST(a_search_goes_no_further_than_its_reach) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mob_path_ask_t ask = mp_ask(0, 0, 0, 60, 0, 0);
    ask.nodes = (uint16_t)MOB_PATH_NODES_MAX;
    const mob_path_found_t *const found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_I64(mp_last(found)->x, (int64_t)MOB_PATH_REACH_XY);
    CHECK_EQ_I64(mp_last(found)->y, 0);
    CHECK_EQ_U64(mp_last(found)->cost, MOB_PATH_REACH_XY * 100u);
    mp_end(&rig);
}

TEST(a_way_too_long_for_its_points_comes_back_cut_short) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    /* A box with walls across it, each open only at the end the last one was not. */
    mp_fill(t, &rig, -1, -9, 0, 21, -9, 8, BLOCK_ID_STONE);
    mp_fill(t, &rig, -1, 9, 0, 21, 9, 8, BLOCK_ID_STONE);
    mp_fill(t, &rig, -1, -9, 0, -1, 9, 8, BLOCK_ID_STONE);
    mp_fill(t, &rig, 21, -9, 0, 21, 9, 8, BLOCK_ID_STONE);
    for (int32_t i = 0; i < 10; i++) {
        const int32_t x = 1 + 2 * i;
        const int32_t y0 = (i & 1) == 0 ? -8 : -7;
        const int32_t y1 = (i & 1) == 0 ? 7 : 8;
        mp_fill(t, &rig, x, y0, 0, x, y1, 8, BLOCK_ID_STONE);
    }
    mob_path_ask_t ask = mp_ask(0, -8, 0, 20, 0, 0);
    ask.nodes = (uint16_t)MOB_PATH_NODES_MAX;
    const mob_path_found_t *const found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_U64(found->point_count, MOB_PATH_POINTS_MAX);
    CHECK_EQ_I64(found->points[0].x, 0);
    CHECK_EQ_I64(found->points[0].y, -8);
    CHECK_EQ_U64(found->points[0].cost, 0u);
    /* Each point of what is kept follows from the last. */
    for (uint32_t i = 1u; i < found->point_count; i++) {
        CHECK(found->points[i].cost > found->points[i - 1u].cost);
        CHECK(fabs((float64_t)(found->points[i].x - found->points[i - 1u].x)) <= 1.0f64);
        CHECK(fabs((float64_t)(found->points[i].y - found->points[i - 1u].y)) <= 1.0f64);
    }
    mp_end(&rig);
}

TEST(a_shut_room_is_searched_to_its_walls_and_no_more) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_fill(t, &rig, 0, 0, 0, 4, 0, 4, BLOCK_ID_STONE);
    mp_fill(t, &rig, 0, 4, 0, 4, 4, 4, BLOCK_ID_STONE);
    mp_fill(t, &rig, 0, 0, 0, 0, 4, 4, BLOCK_ID_STONE);
    mp_fill(t, &rig, 4, 0, 0, 4, 4, 4, BLOCK_ID_STONE);
    mob_path_ask_t ask = mp_ask(1, 1, 0, 30, 2, 0);
    ask.nodes = (uint16_t)MOB_PATH_NODES_MAX;
    const mob_path_found_t *found = mp_search(&rig, &ask);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_U64(found->nodes, 9u);
    CHECK_EQ_I64(mp_last(found)->x, 3);
    CHECK_EQ_I64(mp_last(found)->y, 2);
    /* Already as near as it can get. */
    const mob_path_ask_t there = mp_ask(3, 2, 0, 30, 2, 0);
    found = mp_search(&rig, &there);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_NONE);
    CHECK_EQ_U64(found->point_count, 0u);
    mp_end(&rig);
}

TEST(what_it_cannot_stand_on_is_a_start_that_is_blocked) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_fill(t, &rig, 2, 2, 0, 2, 2, 3, BLOCK_ID_STONE);
    const mob_path_ask_t inside = mp_ask(2, 2, 1, 5, 5, 0);
    CHECK_EQ_U64(mp_search(&rig, &inside)->status, MOB_PATH_STATUS_START_BLOCKED);
    const mob_path_ask_t high = mp_ask(0, 0, (int32_t)MOB_PATH_SETTLE_BLOCKS + 2, 5, 5, 0);
    CHECK_EQ_U64(mp_search(&rig, &high)->status, MOB_PATH_STATUS_START_BLOCKED);
    mp_end(&rig);
}

TEST(a_radius_stops_near_enough_and_places_in_the_air_come_down) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mob_path_ask_t near = mp_ask(0, 0, 0, 10, 0, 0);
    near.goal_radius = 2u;
    const mob_path_found_t *found = mp_search(&rig, &near);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_I64(mp_last(found)->x, 8);
    CHECK_EQ_U64(mp_last(found)->cost, 800u);
    const mob_path_ask_t air_goal = mp_ask(0, 0, 0, 5, 0, 4);
    found = mp_search(&rig, &air_goal);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_I64(mp_last(found)->z, 0);
    const mob_path_ask_t air_start = mp_ask(0, 0, 3, 5, 0, 0);
    found = mp_search(&rig, &air_start);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_I64(found->points[0].z, 0);
    CHECK_EQ_U64(mp_last(found)->cost, 500u);
    /* A radius that is more than the most is the most. */
    near.goal_radius = 200u;
    near.goal[0] = 16;
    found = mp_search(&rig, &near);
    CHECK_EQ_I64(mp_last(found)->x, 16 - (int64_t)MOB_PATH_RADIUS_MAX);
    mp_end(&rig);
}

/* ---- Against a separate search ---------------------------------------------------------------------------------------------- */

typedef struct mp_model_t {
    uint32_t heights[MP_MODEL_CELLS];
    uint32_t cost[MP_MODEL_CELLS];
    uint8_t done[MP_MODEL_CELLS];
} mp_model_t;
static_assert(sizeof(mp_model_t) == 2u * MP_MODEL_CELLS * sizeof(uint32_t) + MP_MODEL_CELLS);

static uint64_t mp_random(uint64_t *state) {
    *state ^= *state << 13u;
    *state ^= *state >> 7u;
    *state ^= *state << 17u;
    return *state;
}

/* What one move across open ground costs, by the table in the README, or UINT32_MAX if it cannot be made. Nothing here is shared with the
 * code it checks. */
static uint32_t mp_model_move(const mp_model_t *model, int32_t x, int32_t y, int32_t nx, int32_t ny) {
    const int32_t side = (int32_t)MP_MODEL_SIDE;
    if (nx < 0 || ny < 0 || nx >= side || ny >= side) {
        return UINT32_MAX;
    }
    const uint32_t here = model->heights[(uint32_t)(y * side + x)];
    const uint32_t there = model->heights[(uint32_t)(ny * side + nx)];
    if (nx != x && ny != y) {
        const uint32_t side_a = model->heights[(uint32_t)(y * side + nx)];
        const uint32_t side_b = model->heights[(uint32_t)(ny * side + x)];
        return there == here && side_a <= here && side_b <= here ? 141u : UINT32_MAX;
    }
    if (there > here) {
        return there - here == 1u ? 160u : UINT32_MAX;
    }
    return here - there <= MOB_PATH_DROP_DEFAULT ? 100u + (here - there) * 10u : UINT32_MAX;
}

static void mp_model_solve(mp_model_t *model, int32_t start_x, int32_t start_y) {
    const int32_t side = (int32_t)MP_MODEL_SIDE;
    for (uint32_t i = 0u; i < MP_MODEL_CELLS; i++) {
        model->cost[i] = MP_MODEL_UNREACHED;
        model->done[i] = 0u;
    }
    model->cost[(uint32_t)(start_y * side + start_x)] = 0u;
    for (;;) {
        uint32_t best = MP_MODEL_CELLS;
        for (uint32_t i = 0u; i < MP_MODEL_CELLS; i++) {
            if (model->done[i] == 0u && model->cost[i] != MP_MODEL_UNREACHED &&
                (best == MP_MODEL_CELLS || model->cost[i] < model->cost[best])) {
                best = i;
            }
        }
        if (best == MP_MODEL_CELLS) {
            return;
        }
        model->done[best] = 1u;
        const int32_t x = (int32_t)best % side, y = (int32_t)best / side;
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const uint32_t move = mp_model_move(model, x, y, x + dx, y + dy);
                if (move != UINT32_MAX) {
                    const uint32_t to = (uint32_t)((y + dy) * side + x + dx);
                    const uint32_t cost = model->cost[best] + move;
                    model->cost[to] = cost < model->cost[to] ? cost : model->cost[to];
                }
            }
        }
    }
}

TEST(the_cheapest_way_on_random_ground_is_what_a_separate_search_finds) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_model_t *const model = test_alloc(t, sizeof(mp_model_t));
    const int32_t side = (int32_t)MP_MODEL_SIDE;
    /* Too high to get on, all round. */
    mp_fill(t, &rig, -1, -1, 0, side, -1, (int32_t)MP_MODEL_WALL, BLOCK_ID_STONE);
    mp_fill(t, &rig, -1, side, 0, side, side, (int32_t)MP_MODEL_WALL, BLOCK_ID_STONE);
    mp_fill(t, &rig, -1, -1, 0, -1, side, (int32_t)MP_MODEL_WALL, BLOCK_ID_STONE);
    mp_fill(t, &rig, side, -1, 0, side, side, (int32_t)MP_MODEL_WALL, BLOCK_ID_STONE);
    uint64_t state = 0x1234567Du;
    uint64_t compared = 0u, unreachable = 0u;
    for (uint32_t w = 0u; w < MP_MODEL_WORLDS; w++) {
        mp_fill(t, &rig, 0, 0, 0, side - 1, side - 1, (int32_t)MP_MODEL_HEIGHT_MAX, BLOCK_ID_AIR);
        const uint64_t roughness = 1u + w % 3u;
        for (int32_t y = 0; y < side; y++) {
            for (int32_t x = 0; x < side; x++) {
                const uint32_t height =
                    (uint32_t)(mp_random(&state) % (roughness + 1u)) + (uint32_t)((x / 6 + y / 6) % 3);
                const uint32_t capped = height > MP_MODEL_HEIGHT_MAX ? MP_MODEL_HEIGHT_MAX : height;
                model->heights[y * side + x] = capped;
                if (capped != 0u) {
                    mp_fill(t, &rig, x, y, 0, x, y, (int32_t)capped - 1, BLOCK_ID_STONE);
                }
            }
        }
        for (uint32_t p = 0u; p < MP_MODEL_PAIRS; p++) {
            const int32_t sx = (int32_t)(mp_random(&state) % MP_MODEL_SIDE);
            const int32_t sy = (int32_t)(mp_random(&state) % MP_MODEL_SIDE);
            const int32_t gx = (int32_t)(mp_random(&state) % MP_MODEL_SIDE);
            const int32_t gy = (int32_t)(mp_random(&state) % MP_MODEL_SIDE);
            mob_path_ask_t ask = mp_ask(sx, sy, (int32_t)model->heights[sy * side + sx], gx, gy,
                                        (int32_t)model->heights[gy * side + gx]);
            ask.nodes = (uint16_t)MOB_PATH_NODES_MAX;
            const mob_path_found_t *const found = mp_search(&rig, &ask);
            mp_model_solve(model, sx, sy);
            const uint32_t expected = model->cost[gy * side + gx];
            if (expected == MP_MODEL_UNREACHED) {
                CHECK(found->status != MOB_PATH_STATUS_COMPLETE);
                unreachable++;
                continue;
            }
            /* A way too long for its points says nothing of the cost. */
            if (found->status == MOB_PATH_STATUS_PARTIAL && found->point_count == MOB_PATH_POINTS_MAX) {
                continue;
            }
            CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
            if (found->status == MOB_PATH_STATUS_COMPLETE) {
                CHECK_EQ_U64(mp_last(found)->cost, expected);
                compared++;
                /* And every step of it is one the model would make, for what the model says it costs. */
                for (uint32_t i = 1u; i < found->point_count; i++) {
                    const mob_path_point_t *const a = &found->points[i - 1u];
                    const mob_path_point_t *const b = &found->points[i];
                    const uint32_t move = mp_model_move(model, a->x, a->y, b->x, b->y);
                    CHECK_EQ_U64(move, (uint32_t)b->cost - a->cost);
                    CHECK_EQ_I64(b->z, (int64_t)model->heights[b->y * side + b->x]);
                }
            }
        }
    }
    CHECK(compared > MP_MODEL_WORLDS * MP_MODEL_PAIRS / 2u);
    printf("# compared %" PRIu64 " ways, and %" PRIu64 " with none\n", compared, unreachable);
    mp_end(&rig);
}

/* ---- The cache --------------------------------------------------------------------------------------------------------------- */

static const mob_path_found_t *mp_ask_tick(mp_rig_t *rig, entity_handle_t handle, const mob_path_ask_t *ask) {
    mob_path_request(rig->path, handle, ask);
    mob_tick(rig->mob);
    return mob_path_found(rig->path, handle);
}

static uint64_t mp_searches(const mp_rig_t *rig) {
    mob_path_stats_t stats;
    mob_path_stats_read(rig->path, &stats);
    return stats.searches;
}

TEST(the_same_ask_runs_no_second_search_and_gets_the_same_bytes) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 10, 4, 0);
    const mob_path_found_t *found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    REQUIRE(found != nullptr);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_U64(found->from_cache, 0u);
    CHECK_EQ_U64(mp_searches(&rig), 1u);
    mob_path_found_t first = *found;
    found = mp_ask_tick(&rig, mp_handle(2u), &ask);
    REQUIRE(found != nullptr);
    CHECK_EQ_U64(found->from_cache, 1u);
    CHECK_EQ_U64(mp_searches(&rig), 1u);
    CHECK_EQ_U64(found->point_count, first.point_count);
    CHECK_MEM_EQ(found->points, first.points, sizeof(first.points));
    CHECK(found->serial > first.serial);
    /* And is the same bytes as a search of its own. */
    mob_path_search(rig.world.map, &ask, rig.scratch, rig.found);
    CHECK_MEM_EQ(rig.found->points, first.points, sizeof(first.points));
    mp_end(&rig);
}

TEST(a_body_on_a_way_already_found_gets_the_rest_of_it) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t whole = mp_ask(0, 0, 0, 10, 0, 0);
    (void)mp_ask_tick(&rig, mp_handle(1u), &whole);
    const mob_path_ask_t on_it = mp_ask(6, 0, 0, 10, 0, 0);
    const mob_path_found_t *const found = mp_ask_tick(&rig, mp_handle(2u), &on_it);
    REQUIRE(found != nullptr);
    CHECK_EQ_U64(found->from_cache, 1u);
    CHECK_EQ_U64(mp_searches(&rig), 1u);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    REQUIRE(found->point_count == 5u);
    CHECK_EQ_I64(found->points[0].x, 6);
    CHECK_EQ_U64(found->points[0].cost, 0u);
    CHECK_EQ_U64(found->points[4].cost, 400u);
    mob_path_stats_t stats;
    mob_path_stats_read(rig.path, &stats);
    CHECK_EQ_U64(stats.suffix_hits, 1u);
    /* Another goal, or another body, is another ask. */
    mob_path_ask_t other = on_it;
    other.height = 6u;
    (void)mp_ask_tick(&rig, mp_handle(3u), &other);
    CHECK_EQ_U64(mp_searches(&rig), 2u);
    other = on_it;
    other.goal[0] = 11;
    (void)mp_ask_tick(&rig, mp_handle(4u), &other);
    CHECK_EQ_U64(mp_searches(&rig), 3u);
    mp_end(&rig);
}

TEST(a_partial_way_is_never_given_to_a_body_part_way_along_it) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 5, 0, 0, 5, 0, 8, BLOCK_ID_STONE);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 7, 0, 0);
    const mob_path_found_t *found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    REQUIRE(found != nullptr);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    const mob_path_ask_t along = mp_ask(2, 0, 0, 7, 0, 0);
    (void)mp_ask_tick(&rig, mp_handle(2u), &along);
    CHECK_EQ_U64(mp_searches(&rig), 2u);
    mp_end(&rig);
}

TEST(an_edit_spoils_only_what_read_that_cell) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 5, 0, 0, 5, 0, 8, BLOCK_ID_STONE);
    world_edit_t *const edit = &rig.world.world->edit;
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 7, 0, 0);
    const mob_path_found_t *found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    REQUIRE(found != nullptr);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);

    /* Far away: still known. */
    CHECK_EQ_U64(world_edit_block_set(edit, -14, 25, 0, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED);
    found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(found->from_cache, 1u);
    CHECK_EQ_U64(mp_searches(&rig), 1u);

    /* In the box, but nothing a body could tell: still known. */
    CHECK_EQ_U64(world_edit_block_set(edit, 5, 0, 3, BLOCK_ID_DIRT), WORLD_EDIT_STATUS_CHANGED);
    found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(found->from_cache, 1u);
    CHECK_EQ_U64(mp_searches(&rig), 1u);

    /* The wall broken: the way that was not there, is. */
    CHECK_EQ_U64(world_edit_block_set(edit, 5, 0, 0, BLOCK_ID_AIR), WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, 5, 0, 1, BLOCK_ID_AIR), WORLD_EDIT_STATUS_CHANGED);
    found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(found->from_cache, 0u);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    CHECK_EQ_U64(mp_searches(&rig), 2u);

    /* And a block put on the way. */
    CHECK_EQ_U64(world_edit_block_set(edit, 3, 0, 0, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED);
    found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(found->from_cache, 0u);
    CHECK_EQ_I64(found->points[3].z, 1);
    mob_path_stats_t stats;
    mob_path_stats_read(rig.path, &stats);
    CHECK(stats.dropped >= 2u);
    mp_end(&rig);
}

/* Plan 005 phase 2: a shut door across the corridor is a wall, which nothing goes through or stands on, and the same door open is a way
 * through. Opening it is an edit that spoils the way cached without it. */
TEST(a_shut_door_is_a_wall_and_an_open_one_a_way_through) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 7, 0, 0);
    for (uint32_t facing = 0u; facing < BLOCK_FACINGS; facing++) {
        mp_fill(t, &rig, 4, 0, 0, 4, 0, 0, block_state_make(BLOCK_ID_WOOD_DOOR, facing, false, false));
        mp_fill(t, &rig, 4, 0, 1, 4, 0, 1, block_state_make(BLOCK_ID_WOOD_DOOR, facing, true, false));
        CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_PARTIAL);
        CHECK_EQ_I64(mp_last(rig.found)->x, 3);
        mp_fill(t, &rig, 4, 0, 0, 4, 0, 0, block_state_make(BLOCK_ID_WOOD_DOOR, facing, false, true));
        mp_fill(t, &rig, 4, 0, 1, 4, 0, 1, block_state_make(BLOCK_ID_WOOD_DOOR, facing, true, true));
        CHECK_EQ_U64(mp_search(&rig, &ask)->status, MOB_PATH_STATUS_COMPLETE);
        CHECK_EQ_U64(mp_last(rig.found)->cost, 700u);
    }
    /* Through the cache: shut, the way stops short; opened by an edit, it is searched again and goes through. */
    world_edit_t *const edit = &rig.world.world->edit;
    CHECK_EQ_U64(world_edit_block_set(edit, 4, 0, 0, block_state_make(BLOCK_ID_WOOD_DOOR, 0u, false, false)),
                 WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, 4, 0, 1, block_state_make(BLOCK_ID_WOOD_DOOR, 0u, true, false)),
                 WORLD_EDIT_STATUS_CHANGED);
    const mob_path_found_t *found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    REQUIRE(found != nullptr);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_U64(world_edit_block_set(edit, 4, 0, 0, block_state_make(BLOCK_ID_WOOD_DOOR, 0u, false, true)),
                 WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_edit_block_set(edit, 4, 0, 1, block_state_make(BLOCK_ID_WOOD_DOOR, 0u, true, true)),
                 WORLD_EDIT_STATUS_CHANGED);
    found = mp_ask_tick(&rig, mp_handle(1u), &ask);
    REQUIRE(found != nullptr);
    CHECK_EQ_U64(found->from_cache, 0u);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    mp_end(&rig);
}

TEST(through_random_edits_what_the_cache_says_is_what_a_search_would_say) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    world_edit_t *const edit = &rig.world.world->edit;
    static const uint16_t blocks[] = {BLOCK_ID_AIR,    BLOCK_ID_AIR,      BLOCK_ID_AIR,       BLOCK_ID_STONE,
                                      BLOCK_ID_DIRT,   BLOCK_ID_OAK_SLAB, BLOCK_ID_WATER,     BLOCK_ID_LADDER,
                                      BLOCK_ID_CACTUS, BLOCK_ID_LAVA,     BLOCK_ID_OAK_STAIRS};
    enum : uint32_t { BLOCK_KINDS = sizeof(blocks) / sizeof(blocks[0]) };
    int32_t places[MP_EDIT_PLACES][3];
    uint64_t state = 0xBEEF1234u;
    /* A few places, so that the same asks come round again and again. */
    for (uint32_t i = 0u; i < MP_EDIT_PLACES; i++) {
        places[i][0] = (int32_t)(mp_random(&state) % MP_EDIT_AREA);
        places[i][1] = (int32_t)(mp_random(&state) % MP_EDIT_AREA);
        places[i][2] = 0;
    }
    mob_path_stats_t before;
    mob_path_stats_read(rig.path, &before);
    uint64_t checked_exact = 0u, checked_suffix = 0u, hits_needed = 0u;
    mob_path_found_t *const copy = test_alloc(t, sizeof(*copy));
    for (uint32_t step = 0u; step < MP_EDIT_STEPS; step++) {
        const uint32_t edits = (uint32_t)(mp_random(&state) % 4u) == 0u ? 1u : 0u;
        for (uint32_t e = 0u; e < edits; e++) {
            /* Sometimes far outside, sometimes through what all of them cross. */
            const int32_t span = (mp_random(&state) & 1u) != 0u ? (int32_t)MP_EDIT_AREA : 40;
            const int32_t x = (int32_t)(mp_random(&state) % (uint64_t)span) - 4;
            const int32_t y = (int32_t)(mp_random(&state) % (uint64_t)span) - 4;
            const int32_t z = (int32_t)(mp_random(&state) % 5u) - 1;
            (void)world_edit_block_set(edit, x, y, z, blocks[mp_random(&state) % BLOCK_KINDS]);
        }
        const int32_t *const from = places[mp_random(&state) % MP_EDIT_PLACES];
        const int32_t *const to = places[mp_random(&state) % MP_EDIT_PLACES];
        const mob_path_ask_t ask = mp_ask(from[0], from[1], from[2], to[0], to[1], to[2]);
        mob_path_stats_t was, now;
        mob_path_stats_read(rig.path, &was);
        const mob_path_found_t *const found = mp_ask_tick(&rig, mp_handle(1u), &ask);
        if (!CHECK(found != nullptr)) {
            break;
        }
        mob_path_stats_read(rig.path, &now);
        *copy = *found;
        const mob_path_found_t *const fresh = mp_search(&rig, &ask);
        if (now.exact_hits != was.exact_hits) {
            CHECK_EQ_U64(copy->status, fresh->status);
            CHECK_EQ_U64(copy->point_count, fresh->point_count);
            CHECK_EQ_U64(copy->nodes, fresh->nodes);
            CHECK_MEM_EQ(copy->points, fresh->points, sizeof(copy->points));
            checked_exact++;
        } else if (now.suffix_hits != was.suffix_hits) {
            /* Another cheapest way may be taken, ties being ties, but none is cheaper. */
            CHECK_EQ_U64(copy->status, MOB_PATH_STATUS_COMPLETE);
            if (fresh->status == MOB_PATH_STATUS_COMPLETE) {
                CHECK_EQ_U64(copy->points[copy->point_count - 1u].cost, fresh->points[fresh->point_count - 1u].cost);
            }
            checked_suffix++;
        } else {
            hits_needed++;
        }
    }
    mob_path_stats_t after;
    mob_path_stats_read(rig.path, &after);
    /* It did all of these, or this test has shown nothing. */
    CHECK(checked_exact > 500u);
    CHECK(checked_suffix > 10u);
    CHECK(hits_needed > 100u);
    CHECK(after.dropped - before.dropped > 100u);
    printf("# %" PRIu64 " exact, %" PRIu64 " by the rest of a way, %" PRIu64 " searched, %" PRIu64 " spoiled\n",
           checked_exact, checked_suffix, hits_needed, (after.dropped - before.dropped));
    mp_end(&rig);
}

TEST(the_longest_unused_answer_is_the_one_that_goes) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mob_path_ask_t ask = mp_ask(0, 0, 0, 1, 0, 0);
    /* Different budgets are different asks, here with the same answer. */
    for (uint32_t i = 0u; i < MOB_PATH_CACHE_ENTRIES; i++) {
        ask.nodes = (uint16_t)(100u + i);
        (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    }
    CHECK_EQ_U64(mp_searches(&rig), MOB_PATH_CACHE_ENTRIES);
    /* Used again, so that the second is now the oldest. */
    ask.nodes = 100u;
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(mp_searches(&rig), MOB_PATH_CACHE_ENTRIES);
    /* One more goes where the second was. */
    ask.nodes = 2000u;
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(mp_searches(&rig), MOB_PATH_CACHE_ENTRIES + 1u);
    ask.nodes = 100u;
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(mp_searches(&rig), MOB_PATH_CACHE_ENTRIES + 1u);
    ask.nodes = 102u;
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(mp_searches(&rig), MOB_PATH_CACHE_ENTRIES + 1u);
    ask.nodes = 101u;
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(mp_searches(&rig), MOB_PATH_CACHE_ENTRIES + 2u);
    mp_end(&rig);
}

TEST(too_many_edits_at_once_empty_it_all) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 3, 0, 0);
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    world_edit_t *const edit = &rig.world.world->edit;
    /* Exactly the limit is read, and none of these is near. */
    uint32_t made = 0u;
    for (int32_t y = 0; made < MOB_PATH_EDITS_MAX; y = (y + 1) % 40) {
        for (int32_t x = -40; x < -20 && made < MOB_PATH_EDITS_MAX; x++) {
            for (int32_t z = 8; z < 12 && made < MOB_PATH_EDITS_MAX; z++) {
                made += world_edit_block_set(edit, x, y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED ? 1u : 0u;
            }
        }
    }
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(mp_searches(&rig), 1u);
    /* One more than that is not. */
    for (int32_t z = 20; z <= 30; z++) {
        for (int32_t x = -32; x < -16; x++) {
            for (int32_t y = 32; y < 48; y++) {
                CHECK_EQ_U64(world_edit_block_set(edit, x, y, z, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    (void)mp_ask_tick(&rig, mp_handle(1u), &ask);
    CHECK_EQ_U64(mp_searches(&rig), 2u);
    mob_path_stats_t stats;
    mob_path_stats_read(rig.path, &stats);
    CHECK_EQ_U64(stats.emptied, 1u);
    mp_end(&rig);
}

/* Lets go of one column, as the map does when it falls out of range, and it comes back all air. */
static void mp_column_gone(test_context_t *t, mp_rig_t *rig, int32_t chunk_x, int32_t chunk_y) {
    uint32_t slot = 0u;
    if (CHECK(world_map_slot_of(rig->world.map, chunk_x, chunk_y, &slot))) {
        world_map_unload(rig->world.map, slot);
    }
}

TEST(a_column_going_spoils_ways_through_it_and_whatever_stopped_short) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t whole = mp_ask(0, 0, 0, 10, 0, 0);
    const mob_path_ask_t into = mp_ask(0, 0, 0, 20, 0, 0);
    mp_fill(t, &rig, 15, -16, 0, 15, 31, 8, BLOCK_ID_STONE); /* no way to x = 20 */
    const mob_path_found_t *found = mp_ask_tick(&rig, mp_handle(1u), &whole);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    found = mp_ask_tick(&rig, mp_handle(2u), &into);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_PARTIAL);
    CHECK_EQ_U64(mp_searches(&rig), 2u);

    /* A column nobody crossed: the whole way is still good, and the one that stopped short is asked again. */
    mp_column_gone(t, &rig, -3, 0);
    found = mp_ask_tick(&rig, mp_handle(1u), &whole);
    CHECK_EQ_U64(found->from_cache, 1u);
    CHECK_EQ_U64(mp_searches(&rig), 2u);
    found = mp_ask_tick(&rig, mp_handle(2u), &into);
    CHECK_EQ_U64(found->from_cache, 0u);
    CHECK_EQ_U64(mp_searches(&rig), 3u);

    /* The column it stands in. */
    mp_column_gone(t, &rig, 0, 0);
    found = mp_ask_tick(&rig, mp_handle(1u), &whole);
    CHECK_EQ_U64(found->from_cache, 0u);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_START_BLOCKED);
    /* What comes back in its place is all air, so nothing there is known either. */
    test_world_stream_all(t, &rig.world);
    found = mp_ask_tick(&rig, mp_handle(1u), &whole);
    CHECK_EQ_U64(found->from_cache, 0u);
    CHECK_EQ_U64(found->status, MOB_PATH_STATUS_START_BLOCKED);
    mp_end(&rig);
}

/* ---- Asking, and the round ---------------------------------------------------------------------------------------------------- */

TEST(many_bodies_asking_the_same_share_one_search) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 12, 7, 0);
    for (uint32_t i = 1u; i <= 10u; i++) {
        mob_path_request(rig.path, mp_handle(i), &ask);
    }
    mob_tick(rig.mob);
    mob_path_stats_t stats;
    mob_path_stats_read(rig.path, &stats);
    CHECK_EQ_U64(stats.searches, 1u);
    CHECK_EQ_U64(stats.shared, 9u);
    CHECK_EQ_U64(stats.exact_hits, 0u);
    const mob_path_found_t *const first = mob_path_found(rig.path, mp_handle(1u));
    REQUIRE(first != nullptr);
    for (uint32_t i = 1u; i <= 10u; i++) {
        const mob_path_found_t *const found = mob_path_found(rig.path, mp_handle(i));
        REQUIRE(found != nullptr);
        CHECK_EQ_U64(found->from_cache, 0u);
        CHECK_EQ_U64(found->serial, i);
        CHECK_MEM_EQ(found->points, first->points, sizeof(first->points));
    }
    mp_end(&rig);
}

TEST(a_new_ask_replaces_the_old_and_keeps_its_turn_and_an_old_handle_finds_nothing) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const mob_path_ask_t a = mp_ask(0, 0, 0, 5, 0, 0);
    const mob_path_ask_t b = mp_ask(0, 0, 0, 7, 0, 0);
    CHECK(mob_path_found(rig.path, mp_handle(1u)) == nullptr);
    mob_path_request(rig.path, mp_handle(1u), &a);
    CHECK(mob_path_found(rig.path, mp_handle(1u)) == nullptr);
    mob_path_request(rig.path, mp_handle(1u), &b);
    mob_tick(rig.mob);
    const mob_path_found_t *const found = mob_path_found(rig.path, mp_handle(1u));
    REQUIRE(found != nullptr);
    CHECK_EQ_I64(mp_last(found)->x, 7);
    CHECK_EQ_U64(mp_searches(&rig), 1u);
    /* The same slot under a later generation. */
    const entity_handle_t later = mp_handle(1u) + (1u << ENTITY_STORE_SLOT_BITS);
    CHECK(mob_path_found(rig.path, later) == nullptr);
    CHECK(mob_path_found(rig.path, ENTITY_HANDLE_NONE) == nullptr);
    mob_path_request(rig.path, later, &a);
    mob_tick(rig.mob);
    CHECK(mob_path_found(rig.path, mp_handle(1u)) == nullptr);
    CHECK(mob_path_found(rig.path, later) != nullptr);
    mp_end(&rig);
}

TEST(a_round_runs_no_more_than_its_budget_and_the_rest_keep_their_turn) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    /* Every one different. */
    const uint32_t bodies = MOB_PATH_ROUND_SEARCHES + 36u;
    for (uint32_t i = 0u; i < bodies; i++) {
        mob_path_ask_t ask = mp_ask(0, 0, 0, (int32_t)(i % 40u), (int32_t)(i / 40u), 0);
        ask.nodes = (uint16_t)(MOB_PATH_ROUND_NODES / MOB_PATH_ROUND_SEARCHES);
        mob_path_request(rig.path, mp_handle(i + 1u), &ask);
    }
    mob_tick(rig.mob);
    CHECK_EQ_U64(mp_searches(&rig), MOB_PATH_ROUND_SEARCHES);
    for (uint32_t i = 0u; i < bodies; i++) {
        const bool answered = mob_path_found(rig.path, mp_handle(i + 1u)) != nullptr;
        CHECK_EQ_U64(answered ? 1u : 0u, i < MOB_PATH_ROUND_SEARCHES ? 1u : 0u);
    }
    mob_tick(rig.mob);
    CHECK_EQ_U64(mp_searches(&rig), bodies);
    for (uint32_t i = 0u; i < bodies; i++) {
        CHECK(mob_path_found(rig.path, mp_handle(i + 1u)) != nullptr);
    }
    /* Nothing left: a tick does nothing. */
    mob_tick(rig.mob);
    CHECK_EQ_U64(mp_searches(&rig), bodies);

    /* A budget of nodes as well, and the first always goes, whatever it asks. */
    for (uint32_t i = 0u; i < 3u; i++) {
        mob_path_ask_t ask = mp_ask(1, 1, 0, 20, (int32_t)i, 0);
        ask.nodes = (uint16_t)MOB_PATH_NODES_MAX;
        mob_path_request(rig.path, mp_handle(i + 1u), &ask);
    }
    static_assert(MOB_PATH_ROUND_NODES < 2u * MOB_PATH_NODES_MAX); /* so one goes in each round */
    for (uint32_t i = 1u; i <= 3u; i++) {
        mob_tick(rig.mob);
        CHECK_EQ_U64(mp_searches(&rig), bodies + i);
    }
    mp_end(&rig);
}

/* A place that takes a search all of its budget to find it cannot be got to: this is what workers are there for. */
static void mp_hard_asks(mob_path_t *path, uint32_t round) {
    for (uint32_t i = 0u; i < MOB_PATH_ROUND_SEARCHES; i++) {
        mob_path_ask_t ask = mp_ask((int32_t)(i % 8u), (int32_t)(i / 8u), 0, 30, 20, 0);
        ask.nodes = (uint16_t)(MOB_PATH_ROUND_NODES / MOB_PATH_ROUND_SEARCHES - round);
        mob_path_request(path, mp_handle(i + 1u), &ask);
    }
}

TEST(the_workers_give_the_same_bytes_as_no_workers) {
    job_system_t *job = nullptr;
    REQUIRE(mp_job_begin(t, &job));
    mp_rig_t rig;
    REQUIRE(mp_begin_with(t, &rig, job));
    mp_floor(t, &rig);
    /* The goal is shut in, and each search spends its whole budget finding that out. */
    mp_fill(t, &rig, 29, 19, 0, 31, 21, 8, BLOCK_ID_STONE);
    mp_fill(t, &rig, 30, 20, 0, 30, 20, 1, BLOCK_ID_AIR);
    mob_t *const alone = test_world_aligned(t, sizeof(mob_t));
    REQUIRE(mob_init(alone, nullptr));
    mob_open(alone, rig.world.world, 5u);
    uint64_t compared = 0u;
    for (uint32_t round = 0u; round < MP_WORKER_ROUNDS; round++) {
        mp_hard_asks(&rig.mob->path, round);
        mp_hard_asks(&alone->path, round);
        mob_tick(rig.mob);
        mob_tick(alone);
        for (uint32_t i = 0u; i < MOB_PATH_ROUND_SEARCHES; i++) {
            const mob_path_found_t *const with = mob_path_found(&rig.mob->path, mp_handle(i + 1u));
            const mob_path_found_t *const without = mob_path_found(&alone->path, mp_handle(i + 1u));
            REQUIRE(with != nullptr && without != nullptr);
            CHECK_MEM_EQ(with, without, sizeof(*with));
            compared++;
        }
    }
    CHECK_EQ_U64(compared, MP_WORKER_ROUNDS * MOB_PATH_ROUND_SEARCHES);
    mob_path_stats_t with_stats, without_stats;
    mob_path_stats_read(&rig.mob->path, &with_stats);
    mob_path_stats_read(&alone->path, &without_stats);
    CHECK_EQ_U64(with_stats.searches, without_stats.searches);
    CHECK_EQ_U64(with_stats.nodes, without_stats.nodes);
    CHECK_EQ_U64(without_stats.worker_searches, 0u);
    /* They really did run there. */
    CHECK(with_stats.worker_searches > 0u);
    printf("# of %" PRIu64 " searches, %" PRIu64 " on workers\n", with_stats.searches, with_stats.worker_searches);
    mob_close(alone);
    mob_deinit(alone);
    mp_end(&rig);
    job_deinit(job);
}

typedef struct mp_gate_t {
    _Atomic uint32_t open;
    _Atomic uint32_t waiting;
} mp_gate_t;
static_assert(sizeof(mp_gate_t) == 8);

static void mp_gate_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    mp_gate_t *const gate = borrowed_argument;
    atomic_fetch_add_explicit(&gate->waiting, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&gate->open, memory_order_acquire) == 0u) {
        time_sleep_ns(100000);
    }
}

TEST(with_every_worker_busy_a_tick_runs_it_all_itself_and_does_not_wait) {
    job_system_t *job = nullptr;
    REQUIRE(mp_job_begin(t, &job));
    mp_rig_t rig;
    REQUIRE(mp_begin_with(t, &rig, job));
    mp_floor(t, &rig);
    mp_gate_t gate = {0u, 0u};
    job_group_t *const group = test_world_aligned(t, sizeof(job_group_t));
    job_group_init(group);
    for (uint32_t i = 0u; i < MC_THREADS_WORKERS; i++) {
        job_submit(job, group, mp_gate_job, &gate);
    }
    while (atomic_load_explicit(&gate.waiting, memory_order_relaxed) != MC_THREADS_WORKERS) {
        time_sleep_ns(100000);
    }
    /* If this waited for a job not started it would never come back, and the test's time limit is what would say so. */
    for (uint32_t i = 0u; i < 16u; i++) {
        mob_path_ask_t ask = mp_ask((int32_t)i, 0, 0, 20, 3, 0);
        ask.nodes = (uint16_t)(MOB_PATH_ROUND_NODES / 16u);
        mob_path_request(rig.path, mp_handle(i + 1u), &ask);
    }
    mob_tick(rig.mob);
    for (uint32_t i = 0u; i < 16u; i++) {
        const mob_path_found_t *const found = mob_path_found(rig.path, mp_handle(i + 1u));
        REQUIRE(found != nullptr);
        CHECK_EQ_U64(found->status, MOB_PATH_STATUS_COMPLETE);
    }
    mob_path_stats_t stats;
    mob_path_stats_read(rig.path, &stats);
    CHECK_EQ_U64(stats.searches, 16u);
    CHECK_EQ_U64(stats.worker_searches, 0u);
    /* And the jobs that were sent, starting now, find nothing to do. */
    atomic_store_explicit(&gate.open, 1u, memory_order_release);
    job_group_wait(job, group);
    job_group_deinit(group);
    mp_end(&rig);
    job_deinit(job);
}

/* ---- Real bodies through real physics ---------------------------------------------------------------------------------------- */

static entity_handle_t mp_spawn(mp_rig_t *rig, int32_t x, int32_t y, float64_t z) {
    const entity_store_spawn_t spawn = {
        .position = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, z},
        .half_width = 0.3f32,
        .height = 1.95f32,
        .health = 20.0f32,
        .kind = 1u,
    };
    return entity_store_spawn(&rig->entity->store, &spawn);
}

/* Walks to the place, asking again whenever the way runs out. Returns the tick it arrived on, or 0 if it never did. */
static uint32_t mp_walk(test_context_t *t, mp_rig_t *rig, entity_handle_t handle, const float64_t goal[3]) {
    entity_store_t *const store = &rig->entity->store;
    uint32_t index = 0u;
    if (!CHECK(entity_store_index_of(store, handle, &index))) {
        return 0u;
    }
    /* It starts in the air, so it lands first. */
    for (uint32_t i = 0u; i < 20u; i++) {
        entity_tick(rig->entity, rig->world.map);
    }
    mob_path_ask_t ask;
    mob_path_ask_for_body(store, index, goal, 0u, &ask);
    mob_path_request(rig->path, handle, &ask);
    for (uint32_t tick = 1u; tick <= MP_WALK_TICKS; tick++) {
        mob_tick(rig->mob);
        const mob_path_follow_t how = mob_path_follow(rig->path, store, handle, MP_WALK_SPEED);
        if (how == MOB_PATH_FOLLOW_ARRIVED) {
            return tick;
        }
        if (how == MOB_PATH_FOLLOW_ENDED || how == MOB_PATH_FOLLOW_STUCK || how == MOB_PATH_FOLLOW_NONE) {
            mob_path_ask_for_body(store, index, goal, 0u, &ask);
            mob_path_request(rig->path, handle, &ask);
        }
        entity_tick(rig->entity, rig->world.map);
    }
    return 0u;
}

static void mp_check_at(test_context_t *t, const mp_rig_t *rig, entity_handle_t handle, const float64_t goal[3]) {
    const entity_store_t *const store = &rig->entity->store;
    uint32_t index = 0u;
    if (CHECK(entity_store_index_of(store, handle, &index))) {
        CHECK_NEAR(store->borrowed_position[0][index], goal[0], MP_NEAR);
        CHECK_NEAR(store->borrowed_position[1][index], goal[1], MP_NEAR);
        CHECK_NEAR(store->borrowed_position[2][index], goal[2], 0.01f64);
    }
}

TEST(a_body_walks_round_a_wall) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_fill(t, &rig, 3, -2, 0, 3, 2, 1, BLOCK_ID_STONE);
    const float64_t goal[3] = {6.5f64, 0.5f64, 0.0f64};
    const entity_handle_t body = mp_spawn(&rig, 0, 0, 0.5f64);
    CHECK(mp_walk(t, &rig, body, goal) != 0u);
    mp_check_at(t, &rig, body, goal);
    mp_end(&rig);
}

TEST(a_body_jumps_a_block_and_comes_down_the_other_side) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 9);
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 0, BLOCK_ID_STONE);
    mp_fill(t, &rig, 6, 0, 0, 6, 0, 0, BLOCK_ID_STONE);
    mp_fill(t, &rig, 7, 0, 0, 7, 0, 1, BLOCK_ID_STONE);
    const float64_t goal[3] = {9.5f64, 0.5f64, 0.0f64};
    const entity_handle_t body = mp_spawn(&rig, 0, 0, 0.5f64);
    CHECK(mp_walk(t, &rig, body, goal) != 0u);
    mp_check_at(t, &rig, body, goal);
    mp_end(&rig);
}

TEST(a_body_walks_off_a_ledge_and_up_a_slab_and_stairs) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 12);
    mp_fill(t, &rig, 0, 0, 0, 1, 0, 2, BLOCK_ID_STONE);    /* a ledge to walk off, three high */
    mp_fill(t, &rig, 4, 0, 0, 4, 0, 0, BLOCK_ID_OAK_SLAB); /* half a step, and a block off it */
    mp_fill(t, &rig, 5, 0, 0, 5, 0, 0, BLOCK_ID_STONE);
    mp_fill(t, &rig, 6, 0, 0, 6, 0, 0, BLOCK_ID_OAK_STAIRS);
    const float64_t goal[3] = {10.5f64, 0.5f64, 0.0f64};
    const entity_handle_t body = mp_spawn(&rig, 0, 0, 3.5f64);
    CHECK(mp_walk(t, &rig, body, goal) != 0u);
    mp_check_at(t, &rig, body, goal);
    mp_end(&rig);
}

TEST(a_body_swims_a_pool_and_gets_out) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 9);
    mp_fill(t, &rig, 3, 0, -3, 5, 0, -3, BLOCK_ID_STONE);
    mp_fill(t, &rig, 3, 0, -2, 5, 0, -1, BLOCK_ID_WATER);
    const float64_t goal[3] = {8.5f64, 0.5f64, 0.0f64};
    const entity_handle_t body = mp_spawn(&rig, 0, 0, 0.5f64);
    CHECK(mp_walk(t, &rig, body, goal) != 0u);
    mp_check_at(t, &rig, body, goal);
    mp_end(&rig);
}

TEST(a_body_climbs_a_ladder_and_steps_off_the_top) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 9);
    mp_fill(t, &rig, 3, 0, 0, 6, 0, 3, BLOCK_ID_STONE);
    mp_fill(t, &rig, 2, 0, 0, 2, 0, 3, BLOCK_ID_LADDER);
    const float64_t goal[3] = {5.5f64, 0.5f64, 4.0f64};
    const entity_handle_t body = mp_spawn(&rig, 0, 0, 0.5f64);
    CHECK(mp_walk(t, &rig, body, goal) != 0u);
    mp_check_at(t, &rig, body, goal);
    mp_end(&rig);
}

TEST(a_body_on_a_way_a_block_is_put_on_gets_stuck_and_finds_another) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    const float64_t goal[3] = {12.5f64, 0.5f64, 0.0f64};
    const entity_handle_t body = mp_spawn(&rig, 0, 0, 0.5f64);
    entity_store_t *const store = &rig.entity->store;
    for (uint32_t i = 0u; i < 20u; i++) {
        entity_tick(rig.entity, rig.world.map);
    }
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(store, body, &index));
    mob_path_ask_t ask;
    mob_path_ask_for_body(store, index, goal, 0u, &ask);
    mob_path_request(rig.path, body, &ask);
    mob_tick(rig.mob);
    /* Behind its back, and too high to jump. */
    world_edit_t *const edit = &rig.world.world->edit;
    for (int32_t y = -3; y <= 3; y++) {
        for (int32_t z = 0; z <= 2; z++) {
            CHECK_EQ_U64(world_edit_block_set(edit, 6, y, z, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED);
        }
    }
    bool stuck = false;
    bool arrived = false;
    for (uint32_t tick = 0u; tick < MP_WALK_TICKS && !arrived; tick++) {
        mob_tick(rig.mob);
        const mob_path_follow_t how = mob_path_follow(rig.path, store, body, MP_WALK_SPEED);
        if (how == MOB_PATH_FOLLOW_STUCK) {
            stuck = true;
            mob_path_ask_for_body(store, index, goal, 0u, &ask);
            mob_path_request(rig.path, body, &ask);
        }
        arrived = how == MOB_PATH_FOLLOW_ARRIVED;
        entity_tick(rig.entity, rig.world.map);
    }
    CHECK(stuck);
    CHECK(arrived);
    mp_check_at(t, &rig, body, goal);
    mp_end(&rig);
}

TEST(a_crowd_walks_the_same_way_shoving_and_all_arrive) {
    job_system_t *job = nullptr;
    REQUIRE(mp_job_begin(t, &job));
    mp_rig_t rig;
    REQUIRE(mp_begin_with(t, &rig, job));
    mp_floor(t, &rig);
    mp_fill(t, &rig, 10, -8, 0, 10, 12, 1, BLOCK_ID_STONE);
    entity_handle_t bodies[MP_CROWD];
    for (uint32_t i = 0u; i < MP_CROWD; i++) {
        bodies[i] = mp_spawn(&rig, (int32_t)(i % 4u) * 2, (int32_t)(i / 4u) * 2 - 4, 0.5f64);
    }
    entity_store_t *const store = &rig.entity->store;
    for (uint32_t i = 0u; i < 20u; i++) {
        entity_tick(rig.entity, rig.world.map);
    }
    const float64_t goal[3] = {20.5f64, 0.5f64, 0.0f64};
    uint32_t arrived = 0u;
    uint8_t done[MP_CROWD] = {0};
    for (uint32_t tick = 0u; tick < MP_WALK_TICKS * 2u && arrived < MP_CROWD; tick++) {
        /* A radius, since they cannot all stand in one place. */
        for (uint32_t i = 0u; i < MP_CROWD; i++) {
            uint32_t index = 0u;
            REQUIRE(entity_store_index_of(store, bodies[i], &index));
            const mob_path_follow_t how =
                done[i] != 0u ? MOB_PATH_FOLLOW_ARRIVED : mob_path_follow(rig.path, store, bodies[i], MP_WALK_SPEED);
            if (how == MOB_PATH_FOLLOW_ARRIVED && done[i] == 0u) {
                done[i] = 1u;
                arrived++;
            } else if (how != MOB_PATH_FOLLOW_WALKING && how != MOB_PATH_FOLLOW_ARRIVED) {
                mob_path_ask_t ask;
                mob_path_ask_for_body(store, index, goal, 3u, &ask);
                mob_path_request(rig.path, bodies[i], &ask);
            }
        }
        mob_tick(rig.mob);
        entity_tick(rig.entity, rig.world.map);
    }
    CHECK_EQ_U64(arrived, MP_CROWD);
    mob_path_stats_t stats;
    mob_path_stats_read(rig.path, &stats);
    printf("# %" PRIu64 " asks: %" PRIu64 " searched, %" PRIu64 " whole, %" PRIu64 " by the rest of a way, %" PRIu64
           " on workers\n",
           stats.requests, stats.searches, stats.exact_hits, stats.suffix_hits, stats.worker_searches);
    CHECK(stats.searches <= stats.requests);
    mp_end(&rig);
    job_deinit(job);
}

TEST(following_says_what_has_nothing_to_walk) {
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mp_corridor(t, &rig, 0, 8);
    mp_fill(t, &rig, 3, 0, 0, 3, 0, 8, BLOCK_ID_STONE);
    entity_store_t *const store = &rig.entity->store;
    const entity_handle_t body = mp_spawn(&rig, 0, 0, 0.0f64);
    /* Nothing asked. */
    CHECK_EQ_U64(mob_path_follow(rig.path, store, body, MP_WALK_SPEED), MOB_PATH_FOLLOW_NONE);
    uint32_t index = 0u;
    REQUIRE(entity_store_index_of(store, body, &index));
    const float64_t goal[3] = {6.5f64, 0.5f64, 0.0f64};
    mob_path_ask_t ask;
    mob_path_ask_for_body(store, index, goal, 0u, &ask);
    CHECK_EQ_U64(ask.height, 4u);
    CHECK_EQ_I64(ask.start[2], 0);
    mob_path_request(rig.path, body, &ask);
    mob_tick(rig.mob);
    /* A wall in the way: it goes as near as it gets, and says it ended there. */
    mob_path_follow_t how = MOB_PATH_FOLLOW_WALKING;
    for (uint32_t tick = 0u; tick < MP_WALK_TICKS && how == MOB_PATH_FOLLOW_WALKING; tick++) {
        how = mob_path_follow(rig.path, store, body, MP_WALK_SPEED);
        entity_tick(rig.entity, rig.world.map);
    }
    CHECK_EQ_U64(how, MOB_PATH_FOLLOW_ENDED);
    /* Gone. */
    CHECK(entity_store_remove(store, body));
    CHECK_EQ_U64(mob_path_follow(rig.path, store, body, MP_WALK_SPEED), MOB_PATH_FOLLOW_NONE);
    mp_end(&rig);
}

/* ---- The Owner --------------------------------------------------------------------------------------------------------------- */

TEST(the_owner_gives_back_all_it_took_and_opens_again_with_nothing_known) {
    heap_counters_t before, after;
    mp_rig_t rig;
    REQUIRE(mp_begin(t, &rig));
    mp_floor(t, &rig);
    mob_t *const mob = test_world_aligned(t, sizeof(mob_t));
    /* Safe on nothing. */
    mob_deinit(mob);
    mob_close(mob);
    heap_counters_read(&before);
    REQUIRE(mob_init(mob, nullptr));
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions + 1u);
    const mob_path_ask_t ask = mp_ask(0, 0, 0, 5, 0, 0);
    for (uint32_t open = 0u; open < 3u; open++) {
        mob_open(mob, rig.world.world, 5u);
        CHECK(mob->arena_world.used <= (uint64_t)MOB_PATH_BYTES + (uint64_t)MOB_ZOMBIE_BYTES +
                                           (uint64_t)MOB_SKELETON_BYTES + (uint64_t)MOB_CREEPER_BYTES +
                                           (uint64_t)MOB_COMBAT_BYTES + (uint64_t)MOB_PASSIVE_BYTES);
        CHECK(mob_path_found(&mob->path, mp_handle(1u)) == nullptr);
        mob_path_request(&mob->path, mp_handle(1u), &ask);
        mob_tick(mob);
        const mob_path_found_t *const found = mob_path_found(&mob->path, mp_handle(1u));
        REQUIRE(found != nullptr);
        CHECK_EQ_U64(found->from_cache, 0u);
        mob_close(mob);
        CHECK_EQ_U64(mob->arena_world.used, 0u);
    }
    /* A close with none open does nothing. */
    mob_close(mob);
    mob_deinit(mob);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    mp_end(&rig);
}

TEST(no_tick_touches_the_heap_and_a_full_round_is_timed) {
    job_system_t *job = nullptr;
    REQUIRE(mp_job_begin(t, &job));
    mp_rig_t rig;
    REQUIRE(mp_begin_with(t, &rig, job));
    mp_floor(t, &rig);
    mp_fill(t, &rig, 29, 19, 0, 31, 21, 8, BLOCK_ID_STONE);
    mp_fill(t, &rig, 30, 20, 0, 30, 20, 1, BLOCK_ID_AIR);
    heap_counters_t before, after;
    heap_counters_read(&before);
    int64_t longest = 0;
    for (uint32_t round = 0u; round < MP_WORKER_ROUNDS; round++) {
        mp_hard_asks(rig.path, round);
        const int64_t from = time_now_ns();
        mob_tick(rig.mob);
        const int64_t took = time_now_ns() - from;
        longest = took > longest ? took : longest;
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    /* And with no worker to help, which is what a tick comes to when all of them are busy. */
    mob_t *const alone = test_world_aligned(t, sizeof(mob_t));
    REQUIRE(mob_init(alone, nullptr));
    mob_open(alone, rig.world.world, 5u);
    int64_t longest_alone = 0;
    for (uint32_t round = 0u; round < MP_WORKER_ROUNDS; round++) {
        mp_hard_asks(&alone->path, round);
        const int64_t from = time_now_ns();
        mob_tick(alone);
        const int64_t took = time_now_ns() - from;
        longest_alone = took > longest_alone ? took : longest_alone;
    }
    mob_close(alone);
    mob_deinit(alone);
    printf("# a whole round's budget, %u nodes: at most %.3f ms on workers, %.3f ms alone\n", MOB_PATH_ROUND_NODES,
           (libc_double_t)((float64_t)longest / 1.0e6f64), (libc_double_t)((float64_t)longest_alone / 1.0e6f64));
    mp_end(&rig);
    job_deinit(job);
}

TEST_MAIN()
