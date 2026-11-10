/* ctest run_physics_raycast. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define RT_EXACT 1.0e-9f64
#define RT_REACH 16.0f64

static raycast_result_t rt_cast(test_world_t *w, float64_t x, float64_t y, float64_t z, float64_t dx, float64_t dy,
                                float64_t dz, float64_t reach, raycast_hit_t *out) {
    const float64_t origin[3] = {x, y, z};
    const float64_t direction[3] = {dx, dy, dz};
    return raycast_cast(w->map, origin, direction, reach, out);
}

static void rt_put(test_context_t *t, test_world_t *w, int32_t x, int32_t y, int32_t z, uint16_t block) {
    test_world_fill(t, w, x, y, z, x, y, z, block);
}

static void rt_check_cell(test_context_t *t, const raycast_hit_t *hit, int32_t x, int32_t y, int32_t z) {
    CHECK_EQ_I64(hit->cell[0], x);
    CHECK_EQ_I64(hit->cell[1], y);
    CHECK_EQ_I64(hit->cell[2], z);
}

/* ---- Casts worked out by hand ------------------------------------------------------------------------------------- */

typedef struct rt_face_case_t {
    float64_t origin[3];    /* 0 */
    float64_t direction[3]; /* 24 */
    int32_t front[3];       /* 48 */
    uint8_t axis;           /* 60 */
    int8_t sign;            /* 61 */
    uint8_t _pad0[2];       /* 62 */
} rt_face_case_t;
static_assert(sizeof(rt_face_case_t) == 64);

TEST(each_of_the_six_faces_is_hit_at_its_distance_and_says_which) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 10, 10, 10, BLOCK_ID_STONE);
    static const rt_face_case_t cases[] = {
        {{7.5f64, 10.5f64, 10.5f64}, {1.0f64, 0.0f64, 0.0f64}, {9, 10, 10}, 0u, -1, {0}},
        {{13.5f64, 10.5f64, 10.5f64}, {-1.0f64, 0.0f64, 0.0f64}, {11, 10, 10}, 0u, 1, {0}},
        {{10.5f64, 7.5f64, 10.5f64}, {0.0f64, 1.0f64, 0.0f64}, {10, 9, 10}, 1u, -1, {0}},
        {{10.5f64, 13.5f64, 10.5f64}, {0.0f64, -1.0f64, 0.0f64}, {10, 11, 10}, 1u, 1, {0}},
        {{10.5f64, 10.5f64, 7.5f64}, {0.0f64, 0.0f64, 1.0f64}, {10, 10, 9}, 2u, -1, {0}},
        {{10.5f64, 10.5f64, 13.5f64}, {0.0f64, 0.0f64, -1.0f64}, {10, 10, 11}, 2u, 1, {0}},
    };
    for (uint32_t i = 0u; i < 6u; i++) {
        raycast_hit_t hit;
        CHECK_EQ_U64(raycast_cast(w.map, cases[i].origin, cases[i].direction, RT_REACH, &hit), RAYCAST_RESULT_HIT);
        CHECK_EQ_U64(hit.result, RAYCAST_RESULT_HIT);
        rt_check_cell(t, &hit, 10, 10, 10);
        CHECK_EQ_U64(hit.block, BLOCK_ID_STONE);
        CHECK_NEAR(hit.distance, 2.5f64, RT_EXACT);
        CHECK_EQ_U64(hit.axis, cases[i].axis);
        CHECK_EQ_I64(hit.normal_sign, cases[i].sign);
        int32_t front[3];
        raycast_hit_in_front(&hit, front);
        CHECK_MEM_EQ(front, cases[i].front, sizeof(front));
    }
    /* Any length of direction gives the same distance. */
    raycast_hit_t hit;
    CHECK_EQ_U64(rt_cast(&w, 7.5f64, 10.5f64, 10.5f64, 250.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    CHECK_NEAR(hit.distance, 2.5f64, RT_EXACT);
    test_world_end(&w);
}

TEST(a_diagonal_is_measured_along_itself_and_goes_through_each_cell_in_order) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* Two stones the same way along; the nearer one is what it stops at. */
    rt_put(t, &w, 3, 3, 3, BLOCK_ID_STONE);
    rt_put(t, &w, 6, 6, 6, BLOCK_ID_STONE);
    raycast_hit_t hit;
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 1.0f64, 1.0f64, 1.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 3, 3, 3);
    CHECK_NEAR(hit.distance, 2.5f64 * sqrt(3.0f64), RT_EXACT);
    /* All three walls meet there, and the first axis to be equal is the one reported. */
    CHECK_EQ_U64(hit.axis, 0u);
    /* Just past the stone's corner it goes by, to what is behind. */
    CHECK_EQ_U64(rt_cast(&w, 2.5f64, 4.01f64, 5.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    test_world_end(&w);
}

TEST(negative_coordinates_and_chunk_borders_are_the_same_as_any_other) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* Either side of the origin's chunk border, and of the world's z of 0. */
    rt_put(t, &w, -1, -1, -1, BLOCK_ID_DIRT);
    rt_put(t, &w, -17, -1, 0, BLOCK_ID_STONE);
    raycast_hit_t hit;
    CHECK_EQ_U64(rt_cast(&w, 2.5f64, -0.5f64, -0.5f64, -1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, -1, -1, -1);
    CHECK_NEAR(hit.distance, 2.5f64, RT_EXACT);
    CHECK_EQ_I64(hit.normal_sign, 1);
    CHECK_EQ_U64(rt_cast(&w, -10.5f64, -0.5f64, 0.5f64, -1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, -17, -1, 0);
    CHECK_NEAR(hit.distance, 5.5f64, RT_EXACT);
    test_world_end(&w);
}

TEST(reach_counts_a_face_exactly_on_it_and_nothing_past_it) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 5, 0, 0, BLOCK_ID_STONE);
    raycast_hit_t hit;
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, 4.5f64, &hit), RAYCAST_RESULT_HIT);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, 4.4999f64, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(hit.result, RAYCAST_RESULT_MISS);
    test_world_end(&w);
}

TEST(starting_inside_is_a_hit_with_no_face_and_touching_is_not_a_hit) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 4, 4, 4, BLOCK_ID_STONE);
    raycast_hit_t hit;
    CHECK_EQ_U64(rt_cast(&w, 4.5f64, 4.5f64, 4.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    CHECK_EQ_U64(hit.axis, RAYCAST_AXIS_NONE);
    CHECK_EQ_I64(hit.normal_sign, 0);
    CHECK_NEAR(hit.distance, 0.0f64, RT_EXACT);
    int32_t front[3];
    raycast_hit_in_front(&hit, front);
    CHECK_EQ_I64(front[0], 4);
    /* On a face going in: the face, at 0. */
    CHECK_EQ_U64(rt_cast(&w, 4.0f64, 4.5f64, 4.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    CHECK_EQ_U64(hit.axis, 0u);
    CHECK_NEAR(hit.distance, 0.0f64, RT_EXACT);
    /* On a face going away, both ways: it never goes in. */
    CHECK_EQ_U64(rt_cast(&w, 4.0f64, 4.5f64, 4.5f64, -1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 5.0f64, 4.5f64, 4.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    /* Along the top of it, and along its high side. */
    CHECK_EQ_U64(rt_cast(&w, 2.5f64, 4.5f64, 5.0f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 2.5f64, 5.0f64, 4.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    test_world_end(&w);
}

TEST(it_goes_over_a_slab_and_into_stairs_by_what_they_really_fill) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 3, 0, 0, BLOCK_ID_OAK_SLAB);
    rt_put(t, &w, 6, 0, 0, BLOCK_ID_STONE);
    raycast_hit_t hit;
    /* Into the slab's side. */
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.25f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 3, 0, 0);
    CHECK_NEAR(hit.bounds.max[2], 0.5f64, RT_EXACT);
    /* Over it, to what is behind. */
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.75f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 6, 0, 0);
    /* Down onto its top, half way down the cell. */
    CHECK_EQ_U64(rt_cast(&w, 3.5f64, 0.5f64, 2.5f64, 0.0f64, 0.0f64, -1.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 3, 0, 0);
    CHECK_NEAR(hit.distance, 2.0f64, RT_EXACT);
    CHECK_EQ_U64(hit.axis, 2u);
    /* Stairs: the lower half, and the upper half on the north side; a ray over the south side's step goes on. */
    rt_put(t, &w, 10, 0, 5, BLOCK_ID_COBBLESTONE_STAIRS);
    rt_put(t, &w, 13, 0, 5, BLOCK_ID_STONE);
    CHECK_EQ_U64(rt_cast(&w, 8.5f64, 0.25f64, 5.75f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 13, 0, 5);
    CHECK_EQ_U64(rt_cast(&w, 8.5f64, 0.75f64, 5.75f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 10, 0, 5);
    CHECK_EQ_U64(rt_cast(&w, 10.5f64, -2.5f64, 5.75f64, 0.0f64, 1.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    CHECK_NEAR(hit.distance, 3.0f64, RT_EXACT); /* to the upper half, half a block into the cell */
    /* Its bounds are around both boxes. */
    CHECK_NEAR(hit.bounds.min[1], 0.0f64, RT_EXACT);
    CHECK_NEAR(hit.bounds.max[2], 6.0f64, RT_EXACT);
    test_world_end(&w);
}

/* Plan 005 phase 12: a ladder lies on the side it faces, and a hit says the block and its state. */
TEST(a_turned_ladder_is_aimed_at_where_it_is_drawn) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    const uint16_t east = block_state_make(BLOCK_ID_LADDER, BLOCK_FACING_EAST, false, false);
    rt_put(t, &w, 12, 5, 3, east);
    raycast_hit_t hit;
    /* From the west, along x: the plate is at the far, east side of its cell. */
    REQUIRE(rt_cast(&w, 9.5f64, 5.5f64, 3.5f64, 1.0f64, 0.0f64, 0.0f64, 8.0f64, &hit) == RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 12, 5, 3);
    CHECK_NEAR(hit.distance, 2.5f64 + 30.0f64 / 32.0f64, 1e-9f64);
    CHECK_EQ_U64(hit.block, BLOCK_ID_LADDER);
    CHECK_EQ_U64(hit.voxel, east);
    /* Along y through its cell's west side, there is nothing to aim at. */
    CHECK_EQ_U64(rt_cast(&w, 12.25f64, 2.5f64, 3.5f64, 0.0f64, 1.0f64, 0.0f64, 6.0f64, &hit), RAYCAST_RESULT_MISS);
    /* Facing north, as an id alone, it is on the north side, as every ladder was. */
    rt_put(t, &w, 12, 5, 3, BLOCK_ID_LADDER);
    REQUIRE(rt_cast(&w, 12.5f64, 2.5f64, 3.5f64, 0.0f64, 1.0f64, 0.0f64, 6.0f64, &hit) == RAYCAST_RESULT_HIT);
    CHECK_NEAR(hit.distance, 2.5f64 + 30.0f64 / 32.0f64, 1e-9f64);
    CHECK_EQ_U64(hit.voxel, BLOCK_ID_LADDER);
    test_world_end(&w);
}

TEST(air_and_fluids_are_looked_through_and_what_is_not_solid_is_still_aimed_at) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    test_world_fill(t, &w, 2, 0, 0, 4, 0, 0, BLOCK_ID_WATER);
    rt_put(t, &w, 5, 0, 0, BLOCK_ID_STONE);
    raycast_hit_t hit;
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 5, 0, 0);
    /* A plant, down its middle and past the edge of its box. */
    rt_put(t, &w, 0, 5, 0, BLOCK_ID_DANDELION);
    rt_put(t, &w, 0, 7, 0, BLOCK_ID_STONE);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 3.5f64, 0.5f64, 0.0f64, 1.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 0, 5, 0);
    CHECK_EQ_U64(hit.block, BLOCK_ID_DANDELION);
    CHECK_NEAR(hit.distance, 1.5f64 + 0.125f64, RT_EXACT);
    CHECK_EQ_U64(rt_cast(&w, 0.05f64, 3.5f64, 0.5f64, 0.0f64, 1.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 0, 7, 0);
    /* A torch stands in the middle and is short. */
    rt_put(t, &w, 8, 5, 0, BLOCK_ID_TORCH);
    rt_put(t, &w, 8, 7, 0, BLOCK_ID_STONE);
    CHECK_EQ_U64(rt_cast(&w, 8.5f64, 3.5f64, 0.25f64, 0.0f64, 1.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 8, 5, 0);
    CHECK_EQ_U64(rt_cast(&w, 8.5f64, 3.5f64, 0.9f64, 0.0f64, 1.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, 8, 7, 0);
    /* A ladder is on the +y side of its cell. */
    rt_put(t, &w, 12, 5, 0, BLOCK_ID_LADDER);
    CHECK_EQ_U64(rt_cast(&w, 12.5f64, 3.5f64, 0.5f64, 0.0f64, 1.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    CHECK_NEAR(hit.distance, 1.5f64 + 30.0f64 / 32.0f64, RT_EXACT);
    /* A lily pad, on top of water, from above. */
    rt_put(t, &w, 16, 5, 0, BLOCK_ID_LILY_PAD);
    rt_put(t, &w, 16, 5, -1, BLOCK_ID_WATER);
    rt_put(t, &w, 16, 5, -2, BLOCK_ID_STONE);
    CHECK_EQ_U64(rt_cast(&w, 16.5f64, 5.5f64, 2.5f64, 0.0f64, 0.0f64, -1.0f64, RT_REACH, &hit), RAYCAST_RESULT_HIT);
    CHECK_EQ_U64(hit.block, BLOCK_ID_LILY_PAD);
    CHECK_NEAR(hit.distance, 2.5f64 - 2.0f64 / 32.0f64, RT_EXACT);
    test_world_end(&w);
}

TEST(what_is_not_loaded_stops_a_ray_and_the_worlds_ends_do_not) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    raycast_hit_t hit;
    /* Chunks reach 64 along an axis from the fixture's middle. */
    CHECK_EQ_U64(rt_cast(&w, 60.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit),
                 RAYCAST_RESULT_NOT_LOADED);
    CHECK_EQ_U64(hit.result, RAYCAST_RESULT_NOT_LOADED);
    CHECK_EQ_I64(hit.cell[0], 64);
    CHECK_NEAR(hit.distance, 3.5f64, RT_EXACT);
    /* Not that far: nothing there at all. */
    CHECK_EQ_U64(rt_cast(&w, 60.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, 3.0f64, &hit), RAYCAST_RESULT_MISS);
    /* Off the bottom of the world, and off the top: no block to aim at, and no column asked. */
    const float64_t floor_z = (float64_t)COLUMN_Z_GAMEPLAY_MIN;
    const float64_t top_z = (float64_t)COLUMN_Z_GAMEPLAY_MAX + 1.0f64;
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, floor_z + 0.5f64, 0.0f64, 0.0f64, -1.0f64, RT_REACH, &hit),
                 RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, top_z - 0.5f64, 0.0f64, 0.0f64, 1.0f64, RT_REACH, &hit),
                 RAYCAST_RESULT_MISS);
    /* Over a column that is not loaded, above the world, no column is read. */
    CHECK_EQ_U64(rt_cast(&w, 60.5f64, 0.5f64, top_z + 5.0f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit),
                 RAYCAST_RESULT_MISS);
    /* Coming back down from above the top, at a stone on it. */
    rt_put(t, &w, 2, 2, COLUMN_Z_GAMEPLAY_MAX, BLOCK_ID_STONE);
    CHECK_EQ_U64(rt_cast(&w, 2.5f64, 2.5f64, top_z + 3.0f64, 0.0f64, 0.0f64, -1.0f64, RT_REACH, &hit),
                 RAYCAST_RESULT_HIT);
    CHECK_NEAR(hit.distance, 3.0f64, RT_EXACT);
    test_world_end(&w);
}

TEST(a_broken_ray_is_a_miss_and_nothing_worse) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    raycast_hit_t hit;
    const float64_t nan = (float64_t)NAN, inf = (float64_t)INFINITY;
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 0.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, nan, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, inf, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, nan, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, inf, 0.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 1.0e30f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, 0.0f64, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, nan, &hit), RAYCAST_RESULT_MISS);
    CHECK_EQ_U64(rt_cast(&w, 0.5f64, 0.5f64, 0.5f64, 1.0f64, 0.0f64, 0.0f64, RAYCAST_DISTANCE_MAX * 2.0f64, &hit),
                 RAYCAST_RESULT_MISS);
    test_world_end(&w);
}

TEST(far_from_the_origin_a_hit_is_as_exact) {
    /* 2^25 blocks out: more than the world will reach, where float32_t could not tell a block's sides apart. */
    test_world_t w = {0};
    w.world = test_world_aligned(t, sizeof(world_t));
    REQUIRE(world_init(w.world, (world_limits_t){256ull << 20, 64u}));
    const int32_t chunk = 1 << 21;
    REQUIRE(world_open(w.world, 2u, chunk, -chunk));
    w.map = &w.world->map;
    test_world_stream_all(t, &w);
    const int32_t x = chunk * 16 + 5, y = -chunk * 16 + 5;
    rt_put(t, &w, x, y, 3, BLOCK_ID_STONE);
    raycast_hit_t hit;
    CHECK_EQ_U64(
        rt_cast(&w, (float64_t)x - 2.25f64, (float64_t)y + 0.5f64, 3.5f64, 1.0f64, 0.0f64, 0.0f64, RT_REACH, &hit),
        RAYCAST_RESULT_HIT);
    rt_check_cell(t, &hit, x, y, 3);
    CHECK_NEAR(hit.distance, 2.25f64, RT_EXACT);
    CHECK_NEAR(hit.bounds.min[0], (float64_t)x, RT_EXACT);
    world_close(w.world);
    world_deinit(w.world);
}

/* ---- Every block ---------------------------------------------------------------------------------------------------- */

TEST(every_block_can_be_aimed_at_as_its_entry_says) {
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        collision_box_t boxes[RAYCAST_BOXES_PER_BLOCK];
        const uint32_t count = raycast_block_boxes((uint16_t)block, 7, -3, 11, boxes);
        const uint8_t shape = block_defs[block].shape;
        if (shape == BLOCK_SHAPE_NONE || shape == BLOCK_SHAPE_FLUID) {
            CHECK_EQ_U64(count, 0u);
            continue;
        }
        /* Everything else can be pointed at, so that it can be mined. */
        CHECK(count >= 1u);
        if ((block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u) {
            collision_box_t solid[COLLISION_BOXES_PER_BLOCK];
            const uint32_t solid_count = collision_block_boxes((uint16_t)block, 7, -3, 11, solid);
            CHECK_EQ_U64(count, solid_count);
            CHECK_MEM_EQ(boxes, solid, (uint64_t)count * sizeof(collision_box_t));
        }
        for (uint32_t i = 0u; i < count; i++) {
            const float64_t cell[3] = {7.0f64, -3.0f64, 11.0f64};
            for (uint32_t a = 0u; a < 3u; a++) {
                CHECK(boxes[i].min[a] >= cell[a] && boxes[i].max[a] <= cell[a] + 1.0f64);
                CHECK(boxes[i].min[a] < boxes[i].max[a]);
            }
        }
    }
    /* A number no block has is solid, as it is to collision. */
    collision_box_t boxes[RAYCAST_BOXES_PER_BLOCK];
    CHECK_EQ_U64(raycast_block_boxes((uint16_t)BLOCK_COUNT, 0, 0, 0, boxes), 1u);
}

/* ---- Random casts against brute force --------------------------------------------------------------------------- */

enum : uint32_t {
    RT_RANDOM_SIDE = 20u,
    RT_RANDOM_CASTS = 3000u,
};
#define RT_MARCH_STEP 0.002f64

static uint64_t rt_random(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x;
    return x;
}

static float64_t rt_random_unit(uint64_t *rng) {
    return (float64_t)(rt_random(rng) >> 11) * 0x1.0p-53f64;
}

/* Whether a point is in a box of what its cell holds. Reads the world one cell at a time, with no walk of its own. */
static bool rt_point_in_block(const world_map_t *map, const float64_t point[3], int32_t cell[3]) {
    for (uint32_t a = 0u; a < 3u; a++) {
        cell[a] = (int32_t)floor(point[a]);
    }
    uint16_t block = BLOCK_ID_AIR;
    if (!collision_cell_read(map, cell[0], cell[1], cell[2], &block)) {
        return false;
    }
    collision_box_t boxes[RAYCAST_BOXES_PER_BLOCK];
    const uint32_t count = raycast_block_boxes(block, cell[0], cell[1], cell[2], boxes);
    for (uint32_t i = 0u; i < count; i++) {
        if (point[0] >= boxes[i].min[0] && point[0] < boxes[i].max[0] && point[1] >= boxes[i].min[1] &&
            point[1] < boxes[i].max[1] && point[2] >= boxes[i].min[2] && point[2] < boxes[i].max[2]) {
            return true;
        }
    }
    return false;
}

TEST(random_casts_find_what_a_march_in_tiny_steps_finds) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    static const uint16_t blocks[] = {BLOCK_ID_STONE,     BLOCK_ID_OAK_SLAB, BLOCK_ID_COBBLESTONE_STAIRS,
                                      BLOCK_ID_DANDELION, BLOCK_ID_TORCH,    BLOCK_ID_LADDER,
                                      BLOCK_ID_LILY_PAD,  BLOCK_ID_WATER,    BLOCK_ID_WHEAT_STAGE_1};
    uint64_t rng = 0x123456789ABCDEFull;
    const int32_t side = (int32_t)RT_RANDOM_SIDE;
    for (int32_t x = -side / 2; x < side / 2; x++) {
        for (int32_t y = -side / 2; y < side / 2; y++) {
            for (int32_t z = 0; z < side; z++) {
                if (rt_random(&rng) % 5u == 0u) {
                    rt_put(t, &w, x, y, z, blocks[rt_random(&rng) % (sizeof(blocks) / sizeof(blocks[0]))]);
                }
            }
        }
    }
    uint32_t hits = 0u, same_cell = 0u;
    for (uint32_t i = 0u; i < RT_RANDOM_CASTS; i++) {
        const float64_t origin[3] = {(rt_random_unit(&rng) - 0.5f64) * (float64_t)side,
                                     (rt_random_unit(&rng) - 0.5f64) * (float64_t)side,
                                     rt_random_unit(&rng) * (float64_t)side};
        float64_t direction[3] = {rt_random_unit(&rng) - 0.5f64, rt_random_unit(&rng) - 0.5f64,
                                  rt_random_unit(&rng) - 0.5f64};
        const float64_t length =
            sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        direction[0] /= length;
        direction[1] /= length;
        direction[2] /= length;
        const float64_t reach = 1.0f64 + rt_random_unit(&rng) * 7.0f64;
        raycast_hit_t hit;
        const raycast_result_t result = raycast_cast(w.map, origin, direction, reach, &hit);
        /* The march. */
        float64_t march_t = -1.0f64;
        int32_t march_cell[3] = {0, 0, 0};
        for (float64_t along = 0.0f64; along <= reach; along += RT_MARCH_STEP) {
            const float64_t point[3] = {origin[0] + direction[0] * along, origin[1] + direction[1] * along,
                                        origin[2] + direction[2] * along};
            if (rt_point_in_block(w.map, point, march_cell)) {
                march_t = along;
                break;
            }
        }
        CHECK(result != RAYCAST_RESULT_NOT_LOADED);
        if (result != RAYCAST_RESULT_HIT) {
            /* The march can find nothing the walk missed. */
            CHECK(march_t < 0.0f64);
            continue;
        }
        hits++;
        /* Just past where it says it came in, it is in that block's box. */
        const float64_t past = hit.distance + 1.0e-7f64;
        const float64_t inside[3] = {origin[0] + direction[0] * past, origin[1] + direction[1] * past,
                                     origin[2] + direction[2] * past};
        int32_t inside_cell[3];
        CHECK(rt_point_in_block(w.map, inside, inside_cell));
        CHECK_MEM_EQ(inside_cell, hit.cell, sizeof(inside_cell));
        /* And nothing came before it. The march may only have stepped over a corner of it, finding something later or nothing. */
        if (march_t >= 0.0f64) {
            CHECK(march_t >= hit.distance - RT_EXACT);
            same_cell += memcmp(march_cell, hit.cell, sizeof(march_cell)) == 0 ? 1u : 0u;
        }
    }
    /* A real test of it: plenty of hits, nearly all of them the same cell. */
    CHECK(hits > RT_RANDOM_CASTS / 4u);
    CHECK(same_cell * 100u >= hits * 99u);
    test_world_end(&w);
}

TEST(a_cast_touches_no_heap_and_no_arena) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 3, 0, 0, BLOCK_ID_STONE);
    const uint64_t peak = w.physics->arena_boxes.peak;
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 1000u; i++) {
        player_target_t target;
        const float64_t eye[3] = {0.5f64, 0.5f64, 0.5f64};
        player_camera_target(w.map, eye, 0.0f32, 0.0f32, &target);
        CHECK_EQ_U64(target.hit.result, RAYCAST_RESULT_HIT);
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    CHECK_EQ_U64(w.physics->arena_boxes.peak, peak);
    test_world_end(&w);
}

/* ---- Mouse look ---------------------------------------------------------------------------------------------------- */

TEST(the_mouse_turns_as_flying_does_and_stops_at_the_top_and_bottom) {
    render_camera_t camera, flying;
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.5f32, 0.25f32);
    flying = camera;
    render_fly_input_t input;
    memset(&input, 0, sizeof(input));
    input.mouse_dx = 40;
    input.mouse_dy = -25;
    player_camera_look(&camera, input.mouse_dx, input.mouse_dy);
    render_camera_fly(&flying, &input, 0.0f32);
    CHECK_NEAR(camera.yaw, flying.yaw, 1.0e-6f32);
    CHECK_NEAR(camera.pitch, flying.pitch, 1.0e-6f32);
    /* Moving right turns right: a smaller yaw. Away from the player looks up. */
    CHECK(camera.yaw < 0.5f32);
    CHECK(camera.pitch > 0.25f32);
    /* Where it stands has not changed. */
    CHECK_NEAR(camera.position[0], 0.0f64, 0.0f64);
    /* Limits, both ways, and they hold. */
    player_camera_look(&camera, 0, -100000);
    CHECK_NEAR(camera.pitch, RENDER_CAMERA_PITCH_LIMIT, 1.0e-6f32);
    player_camera_look(&camera, 0, -5);
    CHECK_NEAR(camera.pitch, RENDER_CAMERA_PITCH_LIMIT, 1.0e-6f32);
    player_camera_look(&camera, 0, 100000);
    CHECK_NEAR(camera.pitch, -RENDER_CAMERA_PITCH_LIMIT, 1.0e-6f32);
}

TEST(any_amount_of_turning_keeps_the_yaw_small_and_looks_the_same_way) {
    render_camera_t camera;
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.0f32, 0.0f32);
    /* Ten turns at a time, the same way, a long way round. */
    for (uint32_t i = 0u; i < 100000u; i++) {
        player_camera_look(&camera, -25132, 0);
        if (!CHECK(fabsf(camera.yaw) <= 3.1416f32)) {
            break;
        }
    }
    /* A whole turn and back again gets within rounding of where it was. */
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 3.0f32, 0.0f32);
    player_camera_look(&camera, -200, 0);
    CHECK(camera.yaw < 0.0f32);
    CHECK_NEAR(cosf(camera.yaw), cosf(3.5f32), 1.0e-4f32);
    CHECK_NEAR(sinf(camera.yaw), sinf(3.5f32), 1.0e-4f32);
}

TEST(the_aim_is_the_line_down_the_middle_of_the_picture) {
    for (float32_t yaw = -3.0f32; yaw < 3.1f32; yaw += 0.37f32) {
        for (float32_t pitch = -1.5f32; pitch < 1.51f32; pitch += 0.25f32) {
            render_camera_t camera;
            render_camera_start(&camera, 5.25f64, -7.5f64, 80.125f64, yaw, pitch);
            traverse_camera_t view;
            render_camera_to_traverse(&camera, 640u, 360u, &view);
            float64_t direction[3];
            player_camera_direction(yaw, pitch, direction);
            CHECK_NEAR(direction[0], (float64_t)view.forward[0], 1.0e-12f64);
            CHECK_NEAR(direction[1], (float64_t)view.forward[1], 1.0e-12f64);
            CHECK_NEAR(direction[2], (float64_t)view.forward[2], 1.0e-12f64);
            CHECK_NEAR(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2], 1.0f64,
                       1.0e-6f64);
            /* A point along it lands in the middle of the picture. */
            const float64_t point[3] = {5.25f64 + direction[0] * 3.0f64, -7.5f64 + direction[1] * 3.0f64,
                                        80.125f64 + direction[2] * 3.0f64};
            float64_t pixel[2];
            CHECK(player_camera_project(&view, point, pixel));
            CHECK_NEAR(pixel[0], 320.0f64, 1.0e-2f64);
            CHECK_NEAR(pixel[1], 180.0f64, 1.0e-2f64);
        }
    }
}

TEST(a_point_on_a_pixels_ray_projects_back_onto_that_pixel) {
    render_camera_t camera;
    render_camera_start(&camera, 1000000.25f64, -2000000.75f64, 64.5f64, 1.1f32, -0.4f32);
    traverse_camera_t view;
    render_camera_to_traverse(&camera, 800u, 450u, &view);
    uint64_t rng = 0xFEEDFACEull;
    for (uint32_t i = 0u; i < 2000u; i++) {
        /* traverse_camera_t's own formula for a pixel's ray, written out by hand. */
        const float64_t px = (float64_t)(rt_random(&rng) % 800u), py = (float64_t)(rt_random(&rng) % 450u);
        const float64_t u = 2.0f64 * (px + 0.5f64) / 800.0f64 - 1.0f64;
        const float64_t v = 1.0f64 - 2.0f64 * (py + 0.5f64) / 450.0f64;
        const float64_t s = 0.5f64 + rt_random_unit(&rng) * 20.0f64;
        float64_t point[3];
        for (uint32_t a = 0u; a < 3u; a++) {
            const float64_t ray = (float64_t)view.forward[a] + u * (float64_t)view.right[a] + v * (float64_t)view.up[a];
            point[a] = (float64_t)view.eye_cell[a] + (float64_t)view.eye_frac[a] + ray * s;
        }
        float64_t pixel[2];
        CHECK(player_camera_project(&view, point, pixel));
        CHECK_NEAR(pixel[0], px + 0.5f64, 1.0e-2f64);
        CHECK_NEAR(pixel[1], py + 0.5f64, 1.0e-2f64);
    }
    /* Behind the eye there is no pixel. */
    float64_t pixel[2] = {-1.0f64, -1.0f64};
    float64_t behind[3];
    for (uint32_t a = 0u; a < 3u; a++) {
        behind[a] = (float64_t)view.eye_cell[a] + (float64_t)view.eye_frac[a] - (float64_t)view.forward[a];
    }
    CHECK(!player_camera_project(&view, behind, pixel));
    CHECK_NEAR(pixel[0], -1.0f64, 0.0f64);
}

TEST(a_box_has_twelve_edges_each_a_side_long_and_no_two_alike) {
    const collision_box_t box = {.min = {1.0f64, 2.0f64, 3.0f64}, .max = {2.0f64, 4.0f64, 7.0f64}};
    float64_t from[PLAYER_CAMERA_EDGES][3], to[PLAYER_CAMERA_EDGES][3];
    uint32_t along[3] = {0u, 0u, 0u};
    for (uint32_t e = 0u; e < PLAYER_CAMERA_EDGES; e++) {
        player_camera_edge(&box, e, from[e], to[e]);
        uint32_t differ = 0u;
        for (uint32_t a = 0u; a < 3u; a++) {
            if (from[e][a] != to[e][a]) {
                differ++;
                along[a]++;
                CHECK_NEAR(from[e][a], box.min[a], 0.0f64);
                CHECK_NEAR(to[e][a], box.max[a], 0.0f64);
            }
        }
        CHECK_EQ_U64(differ, 1u);
        for (uint32_t other = 0u; other < e; other++) {
            CHECK(memcmp(from[e], from[other], sizeof(from[e])) != 0 || memcmp(to[e], to[other], sizeof(to[e])) != 0);
        }
    }
    CHECK_EQ_U64(along[0], 4u);
    CHECK_EQ_U64(along[1], 4u);
    CHECK_EQ_U64(along[2], 4u);
}

/* ---- What it aims at, and what can be seen of it --------------------------------------------------------------------- */

static float32_t rt_yaw_to(const float64_t eye[3], float64_t x, float64_t y) {
    return (float32_t)atan2(y - eye[1], x - eye[0]);
}

TEST(it_aims_at_the_block_in_the_middle_of_the_view_within_reach) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 3, 0, 1, BLOCK_ID_STONE);
    const float64_t eye[3] = {0.5f64, 0.5f64, 1.5f64};
    player_target_t target;
    player_camera_target(w.map, eye, 0.0f32, 0.0f32, &target);
    CHECK_EQ_U64(target.hit.result, RAYCAST_RESULT_HIT);
    rt_check_cell(t, &target.hit, 3, 0, 1);
    CHECK_NEAR(target.hit.distance, 2.5f64, RT_EXACT);
    /* The other way nothing is there, and nothing is seen. */
    player_camera_target(w.map, eye, 3.141592f32, 0.0f32, &target);
    CHECK_EQ_U64(target.hit.result, RAYCAST_RESULT_MISS);
    for (uint32_t e = 0u; e < PLAYER_CAMERA_EDGES; e++) {
        CHECK_EQ_U64(target.edge_visible[e], 0u);
    }
    /* Reach is 4.5 from the eye: a block 3.25 or 4.4 off counts, 5 off does not. */
    rt_put(t, &w, 0, -5, 1, BLOCK_ID_STONE);
    const float64_t near_eye[3] = {0.5f64, -0.75f64, 1.5f64};
    player_camera_target(w.map, near_eye, -1.570796327f32, 0.0f32, &target);
    CHECK_EQ_U64(target.hit.result, RAYCAST_RESULT_HIT);
    CHECK_NEAR(target.hit.distance, 3.25f64, 1.0e-5f64);
    const float64_t far_eye[3] = {0.5f64, 0.4f64, 1.5f64};
    player_camera_target(w.map, far_eye, -1.570796327f32, 0.0f32, &target);
    CHECK_EQ_U64(target.hit.result, RAYCAST_RESULT_HIT);
    CHECK_NEAR(target.hit.distance, 4.4f64, 1.0e-5f64);
    const float64_t past_eye[3] = {0.5f64, 1.0f64, 1.5f64};
    player_camera_target(w.map, past_eye, -1.570796327f32, 0.0f32, &target);
    CHECK_EQ_U64(target.hit.result, RAYCAST_RESULT_MISS);
    test_world_end(&w);
}

TEST(a_lone_block_seen_square_on_shows_its_front_and_hides_the_rest) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 3, 0, 1, BLOCK_ID_STONE);
    const float64_t eye[3] = {0.5f64, 0.5f64, 1.5f64};
    player_target_t target;
    player_camera_target(w.map, eye, 0.0f32, 0.0f32, &target);
    REQUIRE(target.hit.result == RAYCAST_RESULT_HIT);
    for (uint32_t e = 0u; e < PLAYER_CAMERA_EDGES; e++) {
        float64_t from[3], to[3];
        player_camera_edge(&target.hit.bounds, e, from, to);
        /* Only the four around the near face: the eye is level with the block and straight in front. */
        const bool front = from[0] == 3.0f64 && to[0] == 3.0f64;
        CHECK_EQ_U64(target.edge_visible[e], front ? 0xFFu : 0x00u);
    }
    test_world_end(&w);
}

TEST(from_above_it_shows_the_top_too) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 3, 0, 1, BLOCK_ID_STONE);
    const float64_t eye[3] = {1.0f64, 0.5f64, 3.5f64};
    /* At the middle of the top. */
    const float32_t pitch = (float32_t)atan2(-1.5f64, 2.5f64);
    player_target_t target;
    player_camera_target(w.map, eye, 0.0f32, pitch, &target);
    REQUIRE(target.hit.result == RAYCAST_RESULT_HIT);
    CHECK_EQ_U64(target.hit.axis, 2u);
    CHECK_EQ_I64(target.hit.normal_sign, 1);
    for (uint32_t e = 0u; e < PLAYER_CAMERA_EDGES; e++) {
        float64_t from[3], to[3];
        player_camera_edge(&target.hit.bounds, e, from, to);
        const bool top = from[2] == 2.0f64 && to[2] == 2.0f64;
        const bool front = from[0] == 3.0f64 && to[0] == 3.0f64;
        CHECK_EQ_U64(target.edge_visible[e], top || front ? 0xFFu : 0x00u);
    }
    test_world_end(&w);
}

TEST(a_block_in_front_hides_what_it_is_in_front_of_piece_by_piece) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 3, 0, 1, BLOCK_ID_STONE);
    const float64_t eye[3] = {0.5f64, 1.5f64, 1.5f64};
    const float32_t yaw = rt_yaw_to(eye, 3.0f64, 0.5f64);
    player_target_t open;
    player_camera_target(w.map, eye, yaw, 0.0f32, &open);
    REQUIRE(open.hit.result == RAYCAST_RESULT_HIT);
    rt_check_cell(t, &open.hit, 3, 0, 1);
    /* The edge up the near face's +y side: it can be seen, until there is something there. */
    const uint32_t up_near_side = 2u * 4u + 2u; /* along z: x at its min, y at its max */
    float64_t from[3], to[3];
    player_camera_edge(&open.hit.bounds, up_near_side, from, to);
    CHECK_NEAR(from[0], 3.0f64, 0.0f64);
    CHECK_NEAR(from[1], 1.0f64, 0.0f64);
    CHECK_EQ_U64(open.edge_visible[up_near_side], 0xFFu);
    /* The sight lines to the near top edge's pieces start at y = 1.5 and end from 0 to 1. */
    const uint32_t along_near_top = 4u + 1u; /* along y: z at its max, x at its min */
    player_camera_edge(&open.hit.bounds, along_near_top, from, to);
    CHECK_NEAR(from[0], 3.0f64, 0.0f64);
    CHECK_NEAR(from[2], 2.0f64, 0.0f64);
    CHECK_EQ_U64(open.edge_visible[along_near_top], 0xFFu);
    /* This stands beside the line to the middle, and across the others. */
    rt_put(t, &w, 2, 1, 1, BLOCK_ID_STONE);
    player_target_t blocked;
    player_camera_target(w.map, eye, yaw, 0.0f32, &blocked);
    REQUIRE(blocked.hit.result == RAYCAST_RESULT_HIT);
    rt_check_cell(t, &blocked.hit, 3, 0, 1);
    CHECK_EQ_U64(blocked.edge_visible[up_near_side], 0x00u);
    /* Its far end, y low, still shows, and its near end does not. */
    const uint8_t pieces = blocked.edge_visible[along_near_top];
    CHECK((pieces & 0x01u) != 0u);
    CHECK((pieces & 0x80u) == 0u);
    test_world_end(&w);
}

TEST(a_neighbour_flush_with_it_leaves_their_shared_edge_in_sight) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    /* A floor, standing above it and looking down: the edges between floor blocks must show. */
    test_world_fill(t, &w, -4, -4, 0, 8, 8, 0, BLOCK_ID_STONE);
    const float64_t eye[3] = {0.5f64, 0.5f64, 2.62f64};
    const float32_t pitch = (float32_t)atan2(-1.62f64, 2.0f64);
    player_target_t target;
    player_camera_target(w.map, eye, 0.0f32, pitch, &target);
    REQUIRE(target.hit.result == RAYCAST_RESULT_HIT);
    rt_check_cell(t, &target.hit, 2, 0, 0);
    for (uint32_t e = 0u; e < PLAYER_CAMERA_EDGES; e++) {
        float64_t from[3], to[3];
        player_camera_edge(&target.hit.bounds, e, from, to);
        const bool top = from[2] == 1.0f64 && to[2] == 1.0f64;
        CHECK_EQ_U64(target.edge_visible[e], top ? 0xFFu : 0x00u);
    }
    test_world_end(&w);
}

/* ---- The outline's pixels -------------------------------------------------------------------------------------------- */

enum : uint32_t {
    RT_PICTURE_WIDTH = 320u,
    RT_PICTURE_HEIGHT = 180u,
    RT_PICTURE_SPARE_COLUMNS = 16u, /* past each row, and whole rows after: nothing may write there */
    RT_PICTURE_SPARE_ROWS = 4u,
    RT_BACKGROUND = 0x00A0B0C0u,
};

typedef struct rt_picture_t {
    display_buffer_t buffer; /* 0 */
    uint32_t *pixels;        /* then the buffer's size */
    uint64_t words;          /* and 8 more */
} rt_picture_t;
static_assert(sizeof(rt_picture_t) == sizeof(display_buffer_t) + 16u);

static void rt_picture_clear(rt_picture_t *picture) {
    for (uint64_t i = 0u; i < picture->words; i++) {
        picture->pixels[i] = RT_BACKGROUND;
    }
}

static void rt_picture_begin(test_context_t *t, rt_picture_t *picture, uint32_t width, uint32_t height) {
    const uint32_t stride = width + RT_PICTURE_SPARE_COLUMNS;
    picture->words = (uint64_t)stride * (height + RT_PICTURE_SPARE_ROWS);
    picture->pixels = test_alloc(t, picture->words * sizeof(uint32_t));
    picture->buffer = (display_buffer_t){.owned_map = (uint8_t *)picture->pixels,
                                         .map_bytes = picture->words * sizeof(uint32_t),
                                         .pitch = stride * 4u,
                                         .width = width,
                                         .height = height};
    rt_picture_clear(picture);
}

static uint32_t rt_picture_at(const rt_picture_t *picture, int64_t x, int64_t y) {
    return picture->pixels[(uint64_t)y * (picture->buffer.pitch / 4u) + (uint64_t)x];
}

/* Whether anything was drawn within a pixel or so of here. */
static bool rt_picture_drawn_near(const rt_picture_t *picture, float64_t x, float64_t y, int64_t radius) {
    const int64_t cx = (int64_t)floor(x), cy = (int64_t)floor(y);
    for (int64_t py = cy - radius; py <= cy + radius; py++) {
        for (int64_t px = cx - radius; px <= cx + radius; px++) {
            if (px >= 0 && py >= 0 && px < (int64_t)picture->buffer.width && py < (int64_t)picture->buffer.height &&
                rt_picture_at(picture, px, py) == PLAYER_CAMERA_OUTLINE_XRGB) {
                return true;
            }
        }
    }
    return false;
}

static uint64_t rt_picture_count_drawn(const rt_picture_t *picture) {
    uint64_t count = 0u;
    for (uint32_t y = 0u; y < picture->buffer.height; y++) {
        for (uint32_t x = 0u; x < picture->buffer.width; x++) {
            count += rt_picture_at(picture, x, y) == PLAYER_CAMERA_OUTLINE_XRGB ? 1u : 0u;
        }
    }
    return count;
}

/* Everything past the picture's own pixels is as it was. */
static bool rt_picture_spare_untouched(const rt_picture_t *picture) {
    const uint64_t stride = picture->buffer.pitch / 4u;
    for (uint64_t i = 0u; i < picture->words; i++) {
        const uint64_t row = i / stride, column = i % stride;
        if ((row >= picture->buffer.height || column >= picture->buffer.width) && picture->pixels[i] != RT_BACKGROUND) {
            return false;
        }
    }
    return true;
}

TEST(the_outline_is_drawn_on_the_edges_that_can_be_seen_and_nowhere_else) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    rt_put(t, &w, 3, 0, 1, BLOCK_ID_STONE);
    render_camera_t camera;
    const float64_t eye[3] = {1.0f64, 0.5f64, 3.5f64};
    const float32_t pitch = (float32_t)atan2(-1.5f64, 2.5f64);
    render_camera_start(&camera, eye[0], eye[1], eye[2], 0.0f32, pitch);
    player_target_t target;
    player_camera_target(w.map, eye, camera.yaw, camera.pitch, &target);
    REQUIRE(target.hit.result == RAYCAST_RESULT_HIT);
    rt_picture_t picture;
    rt_picture_begin(t, &picture, RT_PICTURE_WIDTH, RT_PICTURE_HEIGHT);
    player_camera_outline_draw(&camera, &target, &picture.buffer);
    traverse_camera_t view;
    render_camera_to_traverse(&camera, RT_PICTURE_WIDTH, RT_PICTURE_HEIGHT, &view);
    uint32_t seen = 0u;
    for (uint32_t e = 0u; e < PLAYER_CAMERA_EDGES; e++) {
        float64_t from[3], to[3];
        player_camera_edge(&target.hit.bounds, e, from, to);
        for (uint32_t step = 1u; step < 8u; step += 2u) {
            const float64_t along = (float64_t)step / 8.0f64;
            const float64_t point[3] = {from[0] + (to[0] - from[0]) * along, from[1] + (to[1] - from[1]) * along,
                                        from[2] + (to[2] - from[2]) * along};
            float64_t pixel[2];
            REQUIRE(player_camera_project(&view, point, pixel));
            if (target.edge_visible[e] == 0xFFu) {
                CHECK(rt_picture_drawn_near(&picture, pixel[0], pixel[1], 1));
                seen++;
            }
        }
    }
    CHECK_EQ_U64(seen, 7u * 4u); /* the top's four edges and the front's other three */
    /* The middle of the top is not an edge, and neither is the rest of the picture. */
    float64_t pixel[2];
    REQUIRE(player_camera_project(&view, (const float64_t[3]){3.5f64, 0.5f64, 2.0f64}, pixel));
    CHECK(!rt_picture_drawn_near(&picture, pixel[0], pixel[1], 2));
    CHECK(rt_picture_at(&picture, 0, 0) == RT_BACKGROUND);
    CHECK(rt_picture_at(&picture, RT_PICTURE_WIDTH - 1, RT_PICTURE_HEIGHT - 1) == RT_BACKGROUND);
    CHECK(rt_picture_count_drawn(&picture) > 50u);
    CHECK(rt_picture_spare_untouched(&picture));
    test_world_end(&w);
}

TEST(no_target_draws_nothing_and_nothing_seen_draws_nothing) {
    rt_picture_t picture;
    rt_picture_begin(t, &picture, RT_PICTURE_WIDTH, RT_PICTURE_HEIGHT);
    render_camera_t camera;
    render_camera_start(&camera, 0.5f64, 0.5f64, 1.5f64, 0.0f32, 0.0f32);
    player_target_t target;
    memset(&target, 0, sizeof(target));
    player_camera_outline_draw(&camera, &target, &picture.buffer);
    CHECK_EQ_U64(rt_picture_count_drawn(&picture), 0u);
    /* A real hit with every piece hidden. */
    target.hit.result = RAYCAST_RESULT_HIT;
    target.hit.bounds = (collision_box_t){.min = {3.0f64, 0.0f64, 1.0f64}, .max = {4.0f64, 1.0f64, 2.0f64}};
    player_camera_outline_draw(&camera, &target, &picture.buffer);
    CHECK_EQ_U64(rt_picture_count_drawn(&picture), 0u);
    /* One piece of one edge is a short line. */
    target.edge_visible[4] = 0x10u;
    player_camera_outline_draw(&camera, &target, &picture.buffer);
    const uint64_t one_piece = rt_picture_count_drawn(&picture);
    CHECK(one_piece > 0u);
    rt_picture_clear(&picture);
    target.edge_visible[4] = 0xFFu;
    player_camera_outline_draw(&camera, &target, &picture.buffer);
    CHECK(rt_picture_count_drawn(&picture) > one_piece * 4u);
}

TEST(lines_are_wider_on_a_larger_picture) {
    render_camera_t camera;
    render_camera_start(&camera, 0.5f64, 0.5f64, 1.5f64, 0.0f32, 0.0f32);
    player_target_t target;
    memset(&target, 0, sizeof(target));
    target.hit.result = RAYCAST_RESULT_HIT;
    target.hit.bounds = (collision_box_t){.min = {3.0f64, 0.0f64, 1.0f64}, .max = {4.0f64, 1.0f64, 2.0f64}};
    memset(target.edge_visible, 0xFF, sizeof(target.edge_visible));
    rt_picture_t small, large;
    rt_picture_begin(t, &small, 640u, 360u);
    rt_picture_begin(t, &large, 1920u, 1080u);
    player_camera_outline_draw(&camera, &target, &small.buffer);
    player_camera_outline_draw(&camera, &target, &large.buffer);
    /* Three times as long and twice as wide, with some loss to where lines cross. */
    CHECK(rt_picture_count_drawn(&large) > rt_picture_count_drawn(&small) * 5u);
    CHECK(rt_picture_spare_untouched(&small));
    CHECK(rt_picture_spare_untouched(&large));
}

TEST(from_any_place_and_any_look_it_stays_inside_the_buffer) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    uint64_t rng = 0xC0FFEEull;
    const int32_t side = 12;
    for (int32_t x = -side; x < side; x++) {
        for (int32_t y = -side; y < side; y++) {
            for (int32_t z = 0; z < side; z++) {
                if (rt_random(&rng) % 6u == 0u) {
                    rt_put(t, &w, x, y, z, rt_random(&rng) % 2u == 0u ? BLOCK_ID_STONE : BLOCK_ID_OAK_SLAB);
                }
            }
        }
    }
    rt_picture_t picture;
    rt_picture_begin(t, &picture, 200u, 100u);
    uint32_t drawn = 0u;
    for (uint32_t i = 0u; i < 1000u; i++) {
        /* Eyes right up against and inside things, so that lines go past the screen and behind the eye. */
        const float64_t eye[3] = {(rt_random_unit(&rng) - 0.5f64) * 20.0f64, (rt_random_unit(&rng) - 0.5f64) * 20.0f64,
                                  rt_random_unit(&rng) * 12.0f64};
        render_camera_t camera;
        render_camera_start(&camera, eye[0], eye[1], eye[2], (float32_t)((rt_random_unit(&rng) - 0.5f64) * 6.28f64),
                            (float32_t)((rt_random_unit(&rng) - 0.5f64) * 3.1f64));
        player_target_t target;
        player_camera_target(w.map, eye, camera.yaw, camera.pitch, &target);
        /* Half the time all of its edges, whatever can be seen, so that the lines that go furthest out are tried. */
        if (rt_random(&rng) % 2u == 0u) {
            memset(target.edge_visible, 0xFF, sizeof(target.edge_visible));
        }
        rt_picture_clear(&picture);
        player_camera_outline_draw(&camera, &target, &picture.buffer);
        drawn += rt_picture_count_drawn(&picture) > 0u ? 1u : 0u;
        CHECK(rt_picture_spare_untouched(&picture));
    }
    CHECK(drawn > 100u);
    test_world_end(&w);
}

/* ---- Through the player ---------------------------------------------------------------------------------------------- */

TEST(a_tick_aims_with_the_look_main_sent_and_main_can_read_what_it_found) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    test_world_fill(t, &w, -16, -16, -1, 31, 31, -1, BLOCK_ID_STONE);
    rt_put(t, &w, 3, 0, 1, BLOCK_ID_DIRT);
    player_t *const player = test_world_aligned(t, sizeof(player_t));
    player_init(player, w.physics);
    player_spawn(player, 0.5f64, 0.5f64, 0.0f64);
    CHECK_EQ_U64(player->target.hit.result, RAYCAST_RESULT_MISS);
    float64_t out[3];
    player_eye_at(player, 0, out);
    CHECK_EQ_U64(player_target_view(player)->hit.result, RAYCAST_RESULT_MISS);
    /* Level, at eye height 1.62: straight into the dirt. */
    int64_t now = PLAYER_TICK_NS;
    player_look_send(player, 0.0f32, 0.0f32);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->target.hit.result, RAYCAST_RESULT_HIT);
    rt_check_cell(t, &player->target.hit, 3, 0, 1);
    CHECK_EQ_U64(player->target.hit.block, BLOCK_ID_DIRT);
    CHECK_NEAR(player->target.hit.distance, 2.5f64, 1.0e-6f64);
    /* The picture of it reaches main with its tick. */
    player_eye_at(player, now, out);
    CHECK_EQ_U64(player_target_view(player)->hit.result, RAYCAST_RESULT_HIT);
    CHECK_EQ_I64(player_target_view(player)->hit.cell[0], 3);
    /* Look straight down: it is the floor under its feet. */
    now += PLAYER_TICK_NS;
    player_look_send(player, 0.0f32, -RENDER_CAMERA_PITCH_LIMIT);
    /* Not until a tick has run: main sees the old one. */
    player_eye_at(player, now, out);
    CHECK_EQ_I64(player_target_view(player)->hit.cell[0], 3);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->target.hit.result, RAYCAST_RESULT_HIT);
    CHECK_EQ_U64(player->target.hit.block, BLOCK_ID_STONE);
    CHECK_EQ_I64(player->target.hit.cell[2], -1);
    CHECK_EQ_U64(player->target.hit.axis, 2u);
    player_eye_at(player, now, out);
    CHECK_EQ_I64(player_target_view(player)->hit.cell[2], -1);
    /* Up at the sky: nothing. */
    now += PLAYER_TICK_NS;
    player_look_send(player, 0.0f32, RENDER_CAMERA_PITCH_LIMIT);
    player_tick(player, w.map, now);
    CHECK_EQ_U64(player->target.hit.result, RAYCAST_RESULT_MISS);
    /* With no one at the controls it keeps the look it had. */
    player_look_send(player, 0.0f32, 0.0f32);
    now += PLAYER_INTENT_STALE_NS * 4;
    player_tick(player, w.map, now);
    rt_check_cell(t, &player->target.hit, 3, 0, 1);
    /* Walking towards it brings it closer. */
    const player_intent_t forward = {
        .yaw = 0.0f32, .forward = 1, .right = 0, .jump = 0u, .sneak = 0u, .sprint = 0u, ._pad0 = {0}};
    const float64_t before = player->target.hit.distance;
    for (uint32_t i = 0u; i < 5u; i++) {
        now += PLAYER_TICK_NS;
        player_send(player, &forward, now);
        player_tick(player, w.map, now);
    }
    CHECK(player->target.hit.distance < before - 0.3f64);
    player_deinit(player);
    test_world_end(&w);
}

TEST(a_tick_with_its_aim_touches_no_heap) {
    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    test_world_fill(t, &w, -16, -16, -1, 31, 31, -1, BLOCK_ID_STONE);
    player_t *const player = test_world_aligned(t, sizeof(player_t));
    player_init(player, w.physics);
    player_spawn(player, 0.5f64, 0.5f64, 0.0f64);
    player_look_send(player, 0.0f32, -1.0f32);
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 200u; i++) {
        player_tick(player, w.map, (int64_t)(i + 1u) * PLAYER_TICK_NS);
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    CHECK_EQ_U64(player->target.hit.result, RAYCAST_RESULT_HIT);
    player_deinit(player);
    test_world_end(&w);
}

/* ---- The game, on the real card ---------------------------------------------------------------------------------------- */

typedef struct rt_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64 */
    player_t *player;            /* 72 */
} rt_rig_t;
static_assert(sizeof(rt_rig_t) == 80);

static bool rt_rig_begin(test_context_t *t, rt_rig_t *rig) {
    rig->bridge = test_world_aligned(t, sizeof(gpu_bridge_t));
    rig->job = test_world_aligned(t, sizeof(job_system_t));
    rig->world = test_world_aligned(t, sizeof(world_t));
    rig->terrain = test_world_aligned(t, sizeof(terrain_t));
    rig->render = test_world_aligned(t, sizeof(render_t));
    rig->texture = test_world_aligned(t, sizeof(texture_t));
    rig->game = test_world_aligned(t, sizeof(game_t));
    rig->physics = test_world_aligned(t, sizeof(physics_t));
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    return true;
}

static void rt_rig_end(test_context_t *t, rt_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static const game_config_t rt_small_game = {.render_distance = 5u,
                                            .width = RT_PICTURE_WIDTH,
                                            .height = RT_PICTURE_HEIGHT,
                                            .features_off = SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                            .show_date = 0u};

/* Compares a shown frame with the same frame drawn with no outline: what differs must be the outline's colour, and there must be some. */
static uint64_t rt_outline_pixels(test_context_t *t, const uint8_t *shown, const uint8_t *picture) {
    uint64_t differing = 0u;
    const uint32_t *const after = (const uint32_t *)(const void *)shown;
    const uint32_t *const under = (const uint32_t *)(const void *)picture;
    for (uint64_t i = 0u; i < (uint64_t)RT_PICTURE_WIDTH * RT_PICTURE_HEIGHT; i++) {
        if (after[i] != under[i]) {
            CHECK_EQ_U64(after[i], PLAYER_CAMERA_OUTLINE_XRGB);
            differing++;
        }
    }
    return differing;
}

TEST(on_one_thread_a_script_looks_down_and_the_block_underfoot_gets_its_outline) {
    rt_rig_t rig;
    REQUIRE(rt_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &rt_small_game));
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    const uint64_t bytes = post_xrgb_bytes(RT_PICTURE_WIDTH, RT_PICTURE_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = RT_PICTURE_WIDTH * 4u,
                               .width = RT_PICTURE_WIDTH,
                               .height = RT_PICTURE_HEIGHT};
    display_script_state_t state;
    memset(&state, 0, sizeof(state));
    const display_headless_tick_t tick = {.events = nullptr, .state = &state, .event_count = 0u, .tick = 0u};
    /* Land first, with no look. */
    for (uint32_t i = 0u; i < 100u; i++) {
        REQUIRE(scene.tick(game, &tick));
    }
    CHECK_EQ_U64(rig.player->movement.on_ground, 1u);
    /* A big move of the mouse towards the player: straight down. */
    state.tick_dy = 100000;
    REQUIRE(scene.tick(game, &tick));
    state.tick_dy = 0;
    CHECK_NEAR(game->camera.pitch, -RENDER_CAMERA_PITCH_LIMIT, 1.0e-6f32);
    const player_target_t *const aim = player_target_view(rig.player);
    CHECK_EQ_U64(aim->hit.result, RAYCAST_RESULT_HIT);
    /* What it aims at is what the body stands on. */
    CHECK_EQ_I64(aim->hit.cell[0], (int64_t)floor(rig.player->movement.position[0]));
    CHECK_EQ_I64(aim->hit.cell[1], (int64_t)floor(rig.player->movement.position[1]));
    CHECK_EQ_I64(aim->hit.cell[2], (int64_t)floor(rig.player->movement.position[2] - 0.5f64));
    CHECK_NEAR(aim->hit.distance, PLAYER_MOVEMENT_EYE_STANDING, 0.05f64);
    REQUIRE(scene.draw(game, &tick, &target));
    uint8_t *const first = test_alloc(t, bytes);
    memcpy(first, target.owned_map, bytes);
    /* The kernels draw it into the picture, so the same frame with that off is what is under it. */
    render_frame_t *const frame = &rig.render->frame;
    const uint32_t features = frame->scene.features;
    render_frame_features_set(frame, features & ~(uint32_t)SHADE_FEATURE_OUTLINE);
    REQUIRE(scene.draw(game, &tick, &target));
    render_frame_features_set(frame, features);
    const uint64_t outlined = rt_outline_pixels(t, first, target.owned_map);
    CHECK(outlined > 50u);
    /* And the same frame again is the same bytes: one script, one picture. */
    REQUIRE(scene.draw(game, &tick, &target));
    CHECK_MEM_EQ(first, target.owned_map, bytes);
    game_close(game);
    rt_rig_end(t, &rig);
}

TEST(on_real_threads_the_outline_is_in_the_picture_on_the_camera_it_was_drawn_from) {
    rt_rig_t rig;
    REQUIRE(rt_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &rt_small_game));
    const uint64_t bytes = post_xrgb_bytes(RT_PICTURE_WIDTH, RT_PICTURE_HEIGHT);
    rt_picture_t shown;
    shown.buffer = (display_buffer_t){.owned_map = test_world_aligned(t, bytes),
                                      .map_bytes = bytes,
                                      .pitch = RT_PICTURE_WIDTH * 4u,
                                      .width = RT_PICTURE_WIDTH,
                                      .height = RT_PICTURE_HEIGHT};
    shown.pixels = (uint32_t *)(void *)shown.buffer.owned_map;
    shown.words = bytes / 4u;
    REQUIRE(game_loop_start(game, nullptr));
    render_fly_input_t none;
    memset(&none, 0, sizeof(none));
    /* Down, then let it land and everything arrive. */
    render_fly_input_t look = none;
    look.mouse_dy = 100000;
    game_walk_send(game, &look, false, false, time_now_ns());
    /* Part of the way back up: looking along a slope a turn moves the whole picture, where straight down it only spins it, and
     * along the edges' own line at that. */
    look.mouse_dy = -260;
    game_walk_send(game, &look, false, false, time_now_ns());
    const int64_t give_up = time_now_ns() + 40000000000;
    uint64_t checked = 0u, behind = 0u, with_lines = 0u, edges_asked = 0u, edges_found = 0u;
    /* The mouse goes on turning, so that each picture has been drawn from an older look than main's. */
    render_fly_input_t turn = none;
    turn.mouse_dx = 16;
    while (time_now_ns() < give_up && checked < 40u) {
        game_walk_send(game, &turn, false, false, time_now_ns());
        CHECK(game_loop_frame(game, &turn, 0.0f32, &shown.buffer));
        time_sleep_ns(2000000);
        const player_target_t *const aim = player_target_view(rig.player);
        if (rig.player->movement.on_ground == 0u || game->borrowed_last_picture == nullptr ||
            aim->hit.result != RAYCAST_RESULT_HIT) {
            continue;
        }
        behind += game->shown_camera.yaw != game->camera.yaw ? 1u : 0u;
        /* The camera the picture was drawn from: the lines are where it says, and not where main is looking now. */
        traverse_camera_t view;
        render_camera_to_traverse(&game->shown_camera, RT_PICTURE_WIDTH, RT_PICTURE_HEIGHT, &view);
        CHECK(game->shown_camera.pitch < -0.5f32);
        /* The card put the lines in the picture, so what is shown is that picture, byte for byte. */
        CHECK_MEM_EQ(shown.buffer.owned_map, game->borrowed_last_picture, bytes);
        /* What it aims at is a tick old, and at this speed may have left the picture, so not every frame has lines. */
        uint64_t found_here = 0u;
        for (uint32_t e = 0u; e < PLAYER_CAMERA_EDGES; e++) {
            if (aim->edge_visible[e] != 0xFFu) {
                continue;
            }
            float64_t from[3], to[3], pixel[2];
            player_camera_edge(&aim->hit.bounds, e, from, to);
            const float64_t middle[3] = {(from[0] + to[0]) / 2.0f64, (from[1] + to[1]) / 2.0f64,
                                         (from[2] + to[2]) / 2.0f64};
            if (player_camera_project(&view, middle, pixel) && pixel[0] >= 2.0f64 && pixel[1] >= 2.0f64 &&
                pixel[0] < (float64_t)RT_PICTURE_WIDTH - 2.0f64 && pixel[1] < (float64_t)RT_PICTURE_HEIGHT - 2.0f64) {
                /* This is main's newest aim, and the picture has the aim the gpu thread last had: often the same, and not always. */
                edges_asked++;
                found_here += rt_picture_drawn_near(&shown, pixel[0], pixel[1], 1) ? 1u : 0u;
            }
        }
        edges_found += found_here;
        with_lines += found_here != 0u ? 1u : 0u;
        checked++;
    }
    CHECK_EQ_U64(checked, 40u);
    /* Or this has shown nothing about which camera it uses. */
    CHECK(behind > checked / 2u);
    CHECK(with_lines > checked / 4u);
    CHECK(edges_found * 2u > edges_asked);
    CHECK(game_loop_stop(game));
    game_close(game);
    rt_rig_end(t, &rig);
}

TEST(flying_aims_at_nothing_and_shows_the_picture_as_it_is) {
    rt_rig_t rig;
    REQUIRE(rt_rig_begin(t, &rig));
    game_t *const game = rig.game;
    game_player_set(game, nullptr);
    REQUIRE(game_open(game, &rt_small_game));
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    const uint64_t bytes = post_xrgb_bytes(RT_PICTURE_WIDTH, RT_PICTURE_HEIGHT);
    display_buffer_t target = {.owned_map = test_world_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = RT_PICTURE_WIDTH * 4u,
                               .width = RT_PICTURE_WIDTH,
                               .height = RT_PICTURE_HEIGHT};
    display_script_state_t state;
    memset(&state, 0, sizeof(state));
    const display_headless_tick_t tick = {.events = nullptr, .state = &state, .event_count = 0u, .tick = 0u};
    REQUIRE(scene.tick(game, &tick));
    REQUIRE(scene.draw(game, &tick, &target));
    render_frame_t *const frame = &rig.render->frame;
    CHECK_MEM_EQ(target.owned_map, frame->borrowed_pictures[frame->pictures.front], bytes);
    game_close(game);
    rt_rig_end(t, &rig);
}

TEST_MAIN()
