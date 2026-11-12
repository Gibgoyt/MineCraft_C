/* ctest run_terrain_landform. See README.md. */
#include "harness/index.h"

#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16);

enum : uint32_t {
    LANDFORM_SEEDS = 8u,
    LANDFORM_WORKERS = 16u,
};
static const uint64_t LANDFORM_WORLD = 0x1234567890ABCDEFull;
/* Plan 003's eight seeds, which plan 007's targets are measured on. */
static const uint64_t LANDFORM_SEED_LIST[LANDFORM_SEEDS] = {
    2198175988018515243ull, 12636146071196537873ull, 5569068473609503046ull, 1ull, 42ull, 20261104ull,
    9999999999999ull,       18446744073709551557ull,
};

/* The typical gradient of the plateau input, per block: master 3.4's worked numbers. */
static const float64_t LANDFORM_RAMP = 4.0e-4f64;

/* The README's coast gate, 0.6 (1 - smoothstep(b; -0.7, 0.2)). */
static float64_t landform_gate(float64_t broad) {
    const float64_t t = fmin(1.0f64, fmax(0.0f64, (broad + 0.7f64) / 0.9f64));
    return 0.6f64 * (1.0f64 - t * t * (3.0f64 - 2.0f64 * t));
}

/* The uplift that gives the plateau input L at a broad continentalness: the README's L = U - 0.3 + 0.25 (b - 0.1) - gate,
 * turned round. */
static float64_t landform_uplift_for(float64_t input, float64_t broad) {
    return input + 0.3f64 - 0.25f64 * (broad - 0.1f64) + landform_gate(broad);
}

/* The plateau in blocks on a ramp of L, at a broad continentalness: what terrain_landform_shape_from gives, with the uplift
 * chosen so that L is exactly the ramp. */
static float64_t landform_ramp_height(const terrain_landform_t *landform, float64_t x, float64_t broad) {
    const float64_t uplift = landform_uplift_for(LANDFORM_RAMP * x, broad);
    return 650.0f64 * terrain_landform_shape_from(landform, uplift, broad).plateau;
}

/* ---- The plateau's shape, on a synthetic ramp ---------------------------------------------------------------------- */

TEST(the_plateau_spline_is_the_master_s_table) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, LANDFORM_WORLD));
    const terrain_landform_t *const landform = terrain.borrowed_landform;
    /* Master 3.4's knots and their heights at 650: at the ramp's x for each L. b = 0.1 has no stretch and no b term. */
    static const float64_t inputs[] = {0.0f64, 0.10f64, 0.16f64, 0.19f64, 0.21f64, 0.40f64, 0.48f64, 0.53f64, 0.56f64};
    static const float64_t blocks[] = {0.0f64,   26.0f64,  91.0f64,  260.0f64, 293.0f64,
                                       312.0f64, 358.0f64, 618.0f64, 650.0f64};
    for (uint32_t i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        const float64_t x = inputs[i] / LANDFORM_RAMP;
        const float64_t height = landform_ramp_height(landform, x, 0.1f64);
        printf("L %.2f at x %6.1f: %7.2f blocks (table %.0f)\n", (libc_double_t)inputs[i], (libc_double_t)x,
               (libc_double_t)height, (libc_double_t)blocks[i]);
        CHECK(fabs(height - blocks[i]) <= 1.0f64);
    }
    CHECK(landform_ramp_height(landform, -2500.0f64, 0.1f64) == 0.0f64);
    /* Tier 1's face, L 0.16 to 0.19, is x 400 to 475. Walk the ramp at a quarter block: never past 650, never down, and the
     * steepest point is on a face, 1:2 to 1:3.5. */
    float64_t last = landform_ramp_height(landform, -100.0f64, 0.1f64);
    float64_t steepest = 0.0f64;
    float64_t steepest_x = 0.0f64;
    uint32_t dips = 0u;
    for (float64_t x = -99.75f64; x <= 2500.0f64; x += 0.25f64) {
        const float64_t height = landform_ramp_height(landform, x, 0.1f64);
        dips += height < last ? 1u : 0u;
        CHECK(height <= 650.0f64);
        const float64_t gradient = (height - last) / 0.25f64;
        if (gradient > steepest) {
            steepest = gradient;
            steepest_x = x;
        }
        last = height;
    }
    printf("steepest %.3f at x %.1f\n", (libc_double_t)steepest, (libc_double_t)steepest_x);
    CHECK_EQ_U64(dips, 0u);
    CHECK(steepest >= 2.0f64 && steepest <= 3.5f64);
    CHECK((steepest_x >= 400.0f64 && steepest_x <= 475.0f64) || (steepest_x >= 1200.0f64 && steepest_x <= 1325.0f64));
    CHECK(fabs(landform_ramp_height(landform, 400.0f64, 0.1f64) - 91.0f64) <= 1.0f64);
    CHECK(fabs(landform_ramp_height(landform, 475.0f64, 0.1f64) - 260.0f64) <= 1.0f64);
    /* The face goes with the slope: 1 on a face of slope 2 or more, small on a level. */
    const terrain_landform_shape_t on_face =
        terrain_landform_shape_from(landform, landform_uplift_for(0.175f64, 0.1f64), 0.1f64);
    const terrain_landform_shape_t on_top =
        terrain_landform_shape_from(landform, landform_uplift_for(0.75f64, 0.1f64), 0.1f64);
    CHECK(on_face.face == 1.0f64);
    CHECK(on_top.face == 0.0f64);
    terrain_deinit(&terrain);
}

/* Where x along the ramp first reaches a height, at a quarter block. */
static float64_t landform_ramp_where(const terrain_landform_t *landform, float64_t height, float64_t broad) {
    for (float64_t x = -2500.0f64; x <= 2500.0f64; x += 0.25f64) {
        if (landform_ramp_height(landform, x, broad) >= height) {
            return x;
        }
    }
    return 2500.0f64;
}

TEST(the_back_edge_is_five_times_wider) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, LANDFORM_WORLD));
    const terrain_landform_t *const landform = terrain.borrowed_landform;
    /* Tier 2's edge, from tier 1's top at 312 to 650: its 10% to 90% width. Seaward, b is low; inland, b is high. */
    const float64_t low = 312.0f64 + 0.1f64 * (650.0f64 - 312.0f64);
    const float64_t high = 312.0f64 + 0.9f64 * (650.0f64 - 312.0f64);
    const float64_t seaward = landform_ramp_where(landform, high, 0.0f64) - landform_ramp_where(landform, low, 0.0f64);
    const float64_t inland = landform_ramp_where(landform, high, 0.6f64) - landform_ramp_where(landform, low, 0.6f64);
    printf("tier 2's edge, 10%% to 90%%: %.2f blocks seaward, %.2f inland: %.2f times\n", (libc_double_t)seaward,
           (libc_double_t)inland, (libc_double_t)(inland / seaward));
    CHECK(inland >= 4.0f64 * seaward && inland <= 6.0f64 * seaward);
    /* The stretch follows b by a smoothstep, and only ever lifts. */
    CHECK(terrain_landform_shape_from(landform, 0.0f64, 0.2f64).stretch == 1.0f64);
    CHECK(fabs(terrain_landform_shape_from(landform, 0.0f64, 0.35f64).stretch - 0.6f64) < 1.0e-12f64);
    CHECK(fabs(terrain_landform_shape_from(landform, 0.0f64, 0.5f64).stretch - 0.2f64) < 1.0e-12f64);
    /* The master's example: b = 0.35, L = 0.48 gives L' = 0.50. */
    const terrain_landform_shape_t example =
        terrain_landform_shape_from(landform, landform_uplift_for(0.48f64, 0.35f64), 0.35f64);
    CHECK(fabs(example.input - 0.50f64) < 1.0e-12f64);
    /* The stretch only lifts, and by at most 0.8 * 0.375 in L however far below tier 2 a column is. */
    const terrain_landform_shape_t deep =
        terrain_landform_shape_from(landform, landform_uplift_for(-0.6f64, 0.6f64), 0.6f64);
    CHECK(fabs(deep.input - (-0.6f64 + 0.3f64)) < 1.0e-12f64);
    /* The coast's gate: out at sea the input is 0.6 lower than the same uplift well inland of the coast would give. */
    const terrain_landform_shape_t at_sea = terrain_landform_shape_from(landform, 0.8f64, -0.7f64);
    const terrain_landform_shape_t behind = terrain_landform_shape_from(landform, 0.8f64, 0.2f64);
    CHECK(fabs((behind.input - at_sea.input) - (0.6f64 + 0.25f64 * 0.9f64)) < 1.0e-12f64);
    CHECK(at_sea.plateau == 0.0f64);
    terrain_deinit(&terrain);
}

/* ---- The lattice ------------------------------------------------------------------------------------------------- */

static float32_t landform_expected_spread(float32_t raw, float64_t limit) {
    float64_t unit = (float64_t)raw / limit;
    unit = fmin(1.0f64, fmax(-1.0f64, unit));
    return (float32_t)(unit * (1.5f64 - 0.5f64 * unit * unit));
}

/* A node worked out from the README: the warp at the node's point, then the uplift where it moves to. */
static float32_t landform_expected_node(const terrain_landform_t *landform, int32_t i, int32_t j) {
    const float64_t x = (float64_t)i * 32.0f64, y = (float64_t)j * 32.0f64, z = 0.0f64;
    float32_t wx = 0.0f32, wy = 0.0f32, u = 0.0f32;
    terrain_noise_sample_points(landform->borrowed_edge_x, &x, &y, &z, 1u, &wx);
    terrain_noise_sample_points(landform->borrowed_edge_y, &x, &y, &z, 1u, &wy);
    const float64_t at_x = x + 700.0f64 * (float64_t)landform_expected_spread(wx, 0.39f64);
    const float64_t at_y = y + 700.0f64 * (float64_t)landform_expected_spread(wy, 0.39f64);
    terrain_noise_sample_points(landform->borrowed_uplift, &at_x, &at_y, &z, 1u, &u);
    return landform_expected_spread(u, 0.35f64);
}

TEST(a_column_blends_its_four_nodes_x_first_as_the_readme_says) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, LANDFORM_WORLD));
    const terrain_landform_t *const landform = terrain.borrowed_landform;
    uint64_t same = 0u;
    uint64_t on_lines = 0u;
    for (uint32_t p = 0u; p < 400u; p++) {
        /* Whole and half blocks, either side of 0, some on a node line. */
        /* Every fifth on a node line in x, every seventh in y. */
        const float64_t x = p % 5u == 0u ? (float64_t)((int32_t)(p * 13u % 5000u) - 2500) * 32.0f64
                                         : (float64_t)((int32_t)(p * 7919u % 200000u) - 100000) * 0.5f64 + 0.25f64;
        const float64_t y = p % 7u == 0u ? (float64_t)((int32_t)(p * 17u % 5000u) - 2500) * 32.0f64
                                         : (float64_t)((int32_t)(p * 104729u % 160000u) - 80000) + 0.25f64;
        const int32_t i = (int32_t)floor(x / 32.0f64), j = (int32_t)floor(y / 32.0f64);
        const float64_t tx = (x - 32.0f64 * (float64_t)i) / 32.0f64, ty = (y - 32.0f64 * (float64_t)j) / 32.0f64;
        const float64_t v00 = (float64_t)landform_expected_node(landform, i, j);
        const float64_t v10 = (float64_t)landform_expected_node(landform, i + 1, j);
        const float64_t v01 = (float64_t)landform_expected_node(landform, i, j + 1);
        const float64_t v11 = (float64_t)landform_expected_node(landform, i + 1, j + 1);
        const float64_t south = tx > 0.0f64 ? v00 + tx * (v10 - v00) : v00;
        const float64_t north = tx > 0.0f64 ? v01 + tx * (v11 - v01) : v01;
        const float64_t expected = ty > 0.0f64 ? south + ty * (north - south) : south;
        float64_t actual = 0.0f64;
        terrain_landform_uplift_points(landform, &x, &y, 1u, &actual);
        same += memcmp(&actual, &expected, sizeof(actual)) == 0 ? 1u : 0u;
        on_lines += (tx == 0.0f64 || ty == 0.0f64) ? 1u : 0u;
    }
    printf("400 columns blended by hand, %" PRIu64 " on a node line: %" PRIu64 " the same\n", on_lines, same);
    CHECK_EQ_U64(same, 400u);
    CHECK(on_lines > 100u);
    terrain_deinit(&terrain);
}

/* 1000 lattice columns, each alone, in a chunk's box, in a box 300 blocks across and through the height estimate: the same
 * plateau, lift and height to the bit, whatever came with them. */
TEST(landform_values_do_not_depend_on_the_box) {
    enum : uint32_t { POINTS = 1000u, WIDE = 76u, WIDE_ROWS = 3u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, LANDFORM_WORLD));
    const terrain_density_t *const density = terrain.borrowed_density;
    float32_t *const heights = test_alloc(t, WIDE * WIDE_ROWS * sizeof(float32_t));
    float32_t *const softness = test_alloc(t, WIDE * WIDE_ROWS * sizeof(float32_t));
    float32_t *const lift = test_alloc(t, WIDE * WIDE_ROWS * sizeof(float32_t));
    float64_t batch_x[TERRAIN_LANDFORM_BATCH_COLUMNS];
    float64_t batch_y[TERRAIN_LANDFORM_BATCH_COLUMNS];
    float64_t batch_uplift[TERRAIN_LANDFORM_BATCH_COLUMNS];
    uint64_t same = 0u;
    uint64_t lifted = 0u;
    for (uint32_t p = 0u; p < POINTS; p++) {
        const int32_t x = ((int32_t)(p * 2654435761u % 40000u) - 20000) * 4;
        const int32_t y = ((int32_t)(p * 40503u % 40000u) - 20000) * 4;
        bool all = true;
        /* Alone. */
        const float64_t fx = (float64_t)x, fy = (float64_t)y;
        float32_t h1 = 0.0f32, s1 = 0.0f32, l1 = 0.0f32;
        terrain_density_profile_points_lift(density, &fx, &fy, 1u, &h1, &s1, &l1);
        lifted += l1 > 0.0f32 ? 1u : 0u;
        /* In a chunk's box: the 5 by 5 lattice columns of the 16-block box round it. */
        const terrain_fields_area_t chunk = {.origin_x = (x >> 4) * 16,
                                             .origin_y = (y >> 4) * 16,
                                             .step_x = 4,
                                             .step_y = 4,
                                             .count_x = 5u,
                                             .count_y = 5u};
        terrain_density_profile_area_lift(density, &chunk, heights, softness, lift);
        const uint32_t at_chunk = (uint32_t)((x - chunk.origin_x) / 4) + 5u * (uint32_t)((y - chunk.origin_y) / 4);
        all = all && memcmp(&heights[at_chunk], &h1, 4u) == 0 && memcmp(&lift[at_chunk], &l1, 4u) == 0;
        /* In a box 300 blocks across, the column somewhere in it. */
        const int32_t back = (int32_t)(p % WIDE) * 4;
        const terrain_fields_area_t wide = {
            .origin_x = x - back, .origin_y = y - 4, .step_x = 4, .step_y = 4, .count_x = WIDE, .count_y = WIDE_ROWS};
        terrain_density_profile_area_lift(density, &wide, heights, softness, lift);
        const uint32_t at_wide = (uint32_t)(back / 4) + WIDE;
        all = all && memcmp(&heights[at_wide], &h1, 4u) == 0 && memcmp(&lift[at_wide], &l1, 4u) == 0;
        /* Through the estimate, which the climate reads. */
        const terrain_density_profile_t estimate = terrain_height_estimate_profile(&terrain, x, y);
        float32_t area_height = 0.0f32;
        terrain_height_estimate_area(&terrain, x, y, 4, 1u, 1u, &area_height);
        all = all && memcmp(&estimate.height, &h1, 4u) == 0 && memcmp(&estimate.lift, &l1, 4u) == 0 &&
              memcmp(&area_height, &h1, 4u) == 0;
        CHECK(terrain_biome_height_estimate(&terrain, x, y) == h1 - l1);
        /* The uplift itself, alone and among 127 others far away. */
        for (uint32_t b = 0u; b < TERRAIN_LANDFORM_BATCH_COLUMNS; b++) {
            batch_x[b] = (float64_t)(x + (int32_t)(b * 977u) - 60000);
            batch_y[b] = (float64_t)(y - (int32_t)(b * 1291u));
        }
        batch_x[p % TERRAIN_LANDFORM_BATCH_COLUMNS] = fx;
        batch_y[p % TERRAIN_LANDFORM_BATCH_COLUMNS] = fy;
        terrain_landform_uplift_points(terrain.borrowed_landform, batch_x, batch_y, TERRAIN_LANDFORM_BATCH_COLUMNS,
                                       batch_uplift);
        float64_t alone = 0.0f64;
        terrain_landform_uplift_points(terrain.borrowed_landform, &fx, &fy, 1u, &alone);
        all = all && memcmp(&alone, &batch_uplift[p % TERRAIN_LANDFORM_BATCH_COLUMNS], sizeof(alone)) == 0;
        same += all ? 1u : 0u;
    }
    printf("%" PRIu32 " lattice columns, %" PRIu64 " on a plateau: %" PRIu64 " the same every way\n", (uint32_t)POINTS,
           lifted, same);
    CHECK_EQ_U64(same, POINTS);
    CHECK(lifted > POINTS / 20u);
    terrain_deinit(&terrain);
}

/* Each field's limit is meant to be about twice what it measures. Prints what it finds. */
TEST(raw_noise_measures_what_the_limits_were_chosen_for) {
    enum : uint32_t { LOTS = 1u << 20 };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, LANDFORM_WORLD));
    const terrain_landform_t *const landform = terrain.borrowed_landform;
    float32_t *const raw = test_alloc(t, LOTS * sizeof(float32_t));
    float64_t *const xs = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const zs = test_alloc(t, LOTS * sizeof(float64_t));
    const terrain_noise_field_t *const fields[TERRAIN_LANDFORM_NOISE_FIELDS] = {
        landform->borrowed_uplift, landform->borrowed_edge_x, landform->borrowed_edge_y};
    static const float64_t limits[TERRAIN_LANDFORM_NOISE_FIELDS] = {0.35f64, 0.39f64, 0.39f64};
    static const char *const names[TERRAIN_LANDFORM_NOISE_FIELDS] = {"uplift", "edge_x", "edge_y"};
    /* Over 1.5 million blocks a side, so even the uplift's 24 km cell is seen thousands of times. */
    for (uint32_t i = 0u; i < LOTS; i++) {
        xs[i] = (float64_t)((int32_t)(i % 1024u) * 1531 - 780000);
        ys[i] = (float64_t)((int32_t)(i / 1024u) * 1543 - 790000);
        zs[i] = 0.0f64;
    }
    for (uint32_t f = 0u; f < TERRAIN_LANDFORM_NOISE_FIELDS; f++) {
        CHECK_STR_EQ(fields[f]->borrowed_name + strlen("landform/"), names[f]);
        terrain_noise_sample_points(fields[f], xs, ys, zs, LOTS, raw);
        float64_t squares = 0.0f64;
        uint64_t past = 0u;
        for (uint32_t i = 0u; i < LOTS; i++) {
            squares += (float64_t)raw[i] * (float64_t)raw[i];
            past += fabs((float64_t)raw[i]) >= limits[f] ? 1u : 0u;
        }
        const float64_t deviation = sqrt(squares / (float64_t)LOTS);
        const float64_t share = (float64_t)past / (float64_t)LOTS;
        printf("%-7s raw standard deviation %.4f; limit is %.2f of them; %.2f%% past it\n", names[f],
               (libc_double_t)deviation, (libc_double_t)(limits[f] / deviation), (libc_double_t)(share * 100.0f64));
        CHECK(limits[f] > 1.5f64 * deviation && limits[f] < 2.5f64 * deviation);
        CHECK(share < 0.08f64);
        for (uint32_t o = 0u; o < fields[f]->octave_count; o++) {
            CHECK(fields[f]->octaves[o].frequency_z == 0.0f64);
        }
    }
    terrain_deinit(&terrain);
}

/* What a chunk's box pays for its lattice, on one thread: printed for the phase file, as step 3 asks. */
TEST(the_lattice_costs_little_per_chunk_box) {
    enum : uint32_t { BOXES = 4096u, LATTICE = 25u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, LANDFORM_WORLD));
    float64_t xs[LATTICE];
    float64_t ys[LATTICE];
    float64_t uplift[LATTICE];
    float32_t heights[LATTICE];
    float32_t softness[LATTICE];
    float64_t check = 0.0f64;
    int64_t landform_ns = INT64_MAX;
    int64_t profile_ns = INT64_MAX;
    for (uint32_t round = 0u; round < 3u; round++) {
        int64_t started = time_now_ns();
        for (uint32_t box = 0u; box < BOXES; box++) {
            const int32_t x0 = ((int32_t)(box % 64u) - 32) * 16 + (int32_t)round * 4096;
            const int32_t y0 = ((int32_t)(box / 64u) - 32) * 16;
            for (uint32_t i = 0u; i < LATTICE; i++) {
                xs[i] = (float64_t)(x0 + (int32_t)(i % 5u) * 4);
                ys[i] = (float64_t)(y0 + (int32_t)(i / 5u) * 4);
            }
            terrain_landform_uplift_points(terrain.borrowed_landform, xs, ys, LATTICE, uplift);
            check += uplift[12];
        }
        const int64_t landform_done = time_now_ns();
        landform_ns = landform_done - started < landform_ns ? landform_done - started : landform_ns;
        started = time_now_ns();
        for (uint32_t box = 0u; box < BOXES; box++) {
            const int32_t x0 = ((int32_t)(box % 64u) - 32) * 16 + (int32_t)round * 4096;
            const int32_t y0 = ((int32_t)(box / 64u) - 32) * 16;
            for (uint32_t i = 0u; i < LATTICE; i++) {
                xs[i] = (float64_t)(x0 + (int32_t)(i % 5u) * 4);
                ys[i] = (float64_t)(y0 + (int32_t)(i / 5u) * 4);
            }
            terrain_density_profile_points(terrain.borrowed_density, xs, ys, LATTICE, heights, softness);
            check += (float64_t)heights[12];
        }
        const int64_t profile_done = time_now_ns();
        profile_ns = profile_done - started < profile_ns ? profile_done - started : profile_ns;
    }
    const float64_t landform_us = (float64_t)landform_ns / 1000.0f64 / (float64_t)BOXES;
    const float64_t profile_us = (float64_t)profile_ns / 1000.0f64 / (float64_t)BOXES;
    printf(
        "a chunk box's 25 lattice columns, best of 3 over %u boxes, one thread: the landform %.2f us (%.3f a lattice "
        "column), the whole profile with it %.2f us (%.3f a lattice column), the landform %.1f%% of it [check %.1f]\n",
        (uint32_t)BOXES, (libc_double_t)landform_us, (libc_double_t)(landform_us / (float64_t)LATTICE),
        (libc_double_t)profile_us, (libc_double_t)(profile_us / (float64_t)LATTICE),
        (libc_double_t)(100.0f64 * landform_us / profile_us), (libc_double_t)check);
    CHECK(landform_us < profile_us);
    terrain_deinit(&terrain);
}

/* ---- Real columns of real seeds ---------------------------------------------------------------------------------- */

enum : uint32_t {
    LIMITS_SIDE = 4096u, /* 64 km at 16 blocks */
    LIMITS_STEP = 16u,
};

typedef struct landform_limits_run_t {
    const terrain_density_t *borrowed_density; /* 0 */
    float32_t *borrowed_rows;            /* 8: LIMITS_SIDE heights per worker, then as many of softness and lift */
    _Atomic uint32_t next_row;           /* 16 */
    _Atomic uint32_t next_worker;        /* 20 */
    float32_t lowest[LANDFORM_WORKERS];  /* 24 */
    float32_t highest[LANDFORM_WORKERS]; /* 88 */
    uint64_t lifted[LANDFORM_WORKERS];   /* 152: columns with any plateau */
    uint64_t past[LANDFORM_WORKERS];     /* 280: lift above height less the lowest base, which cannot be */
} landform_limits_run_t;
static_assert(sizeof(landform_limits_run_t) == 408);

static void landform_limits_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    landform_limits_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    float32_t *const heights = run->borrowed_rows + (uint64_t)me * LIMITS_SIDE * 3u;
    float32_t *const softness = heights + LIMITS_SIDE;
    float32_t *const lift = softness + LIMITS_SIDE;
    float32_t lowest = 1.0e9f32, highest = -1.0e9f32;
    uint64_t lifted = 0u, past = 0u;
    const int32_t half = (int32_t)(LIMITS_SIDE * LIMITS_STEP / 2u);
    for (;;) {
        const uint32_t row = atomic_fetch_add_explicit(&run->next_row, 1u, memory_order_relaxed);
        if (row >= LIMITS_SIDE) {
            break;
        }
        const terrain_fields_area_t area = {.origin_x = -half,
                                            .origin_y = -half + (int32_t)(row * LIMITS_STEP),
                                            .step_x = (int32_t)LIMITS_STEP,
                                            .step_y = 1,
                                            .count_x = LIMITS_SIDE,
                                            .count_y = 1u};
        terrain_density_profile_area_lift(run->borrowed_density, &area, heights, softness, lift);
        for (uint32_t i = 0u; i < LIMITS_SIDE; i++) {
            lowest = heights[i] < lowest ? heights[i] : lowest;
            highest = heights[i] > highest ? heights[i] : highest;
            lifted += lift[i] > 0.0f32 ? 1u : 0u;
            /* A column stands on its lift less at most the sea bed's depth and the sea bed's hills, give or take the
             * rounding of each to a float32_t. */
            past += lift[i] < 0.0f32 || lift[i] > 650.0f32 || heights[i] < lift[i] - 116.001f32 ? 1u : 0u;
        }
    }
    run->lowest[me] = lowest;
    run->highest[me] = highest;
    run->lifted[me] = lifted;
    run->past[me] = past;
}

TEST(the_height_limits_add_up_on_eight_seeds_real_columns) {
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));
    float32_t *const rows = test_alloc(t, (uint64_t)LANDFORM_WORKERS * LIMITS_SIDE * 3u * sizeof(float32_t));
    float32_t lowest = 1.0e9f32, highest = -1.0e9f32;
    for (uint32_t s = 0u; s < LANDFORM_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, LANDFORM_SEED_LIST[s]));
        landform_limits_run_t run = {.borrowed_density = terrain.borrowed_density, .borrowed_rows = rows};
        job_group_t group;
        job_group_init(&group);
        for (uint32_t w = 0u; w < LANDFORM_WORKERS; w++) {
            job_submit(&system, &group, landform_limits_job, &run);
        }
        job_group_wait(&system, &group);
        job_group_deinit(&group);
        float32_t seed_low = 1.0e9f32, seed_high = -1.0e9f32;
        uint64_t lifted = 0u, past = 0u;
        for (uint32_t w = 0u; w < LANDFORM_WORKERS; w++) {
            seed_low = run.lowest[w] < seed_low ? run.lowest[w] : seed_low;
            seed_high = run.highest[w] > seed_high ? run.highest[w] : seed_high;
            lifted += run.lifted[w];
            past += run.past[w];
        }
        printf("seed %" PRIu64 ": height %.1f to %.1f over 64 km at 16; %.1f%% of columns on a plateau\n",
               LANDFORM_SEED_LIST[s], (libc_double_t)seed_low, (libc_double_t)seed_high,
               (libc_double_t)(100.0f64 * (float64_t)lifted / ((float64_t)LIMITS_SIDE * (float64_t)LIMITS_SIDE)));
        CHECK_EQ_U64(past, 0u);
        lowest = seed_low < lowest ? seed_low : lowest;
        highest = seed_high > highest ? seed_high : highest;
        terrain_deinit(&terrain);
    }
    job_deinit(&system);
    CHECK(lowest >= (float32_t)TERRAIN_DENSITY_HEIGHT_MIN);
    CHECK(highest <= (float32_t)TERRAIN_DENSITY_HEIGHT_MAX);
    CHECK(highest > 900.0f32);
}

/* A plateau's top is low ground to a biome: the climate cools it, but its biome is a plain's or a forest's, not a peak's. */
TEST(plateau_tops_choose_lowland_biomes) {
    enum : uint32_t { SIDE = 512u, STEP = 128u, COLUMNS = SIDE * SIDE };
    terrain_hydrology_column_t *const water = test_alloc(t, (uint64_t)SIDE * sizeof(terrain_hydrology_column_t));
    terrain_biome_column_t *const biomes = test_alloc(t, (uint64_t)SIDE * sizeof(terrain_biome_column_t));
    uint64_t tops = 0u;
    uint64_t lowland = 0u;
    uint64_t peaks_by_height = 0u;
    for (uint32_t s = 0u; s < 3u; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, LANDFORM_SEED_LIST[s]));
        const int32_t half = (int32_t)(SIDE * STEP / 2u);
        for (uint32_t row = 0u; row < SIDE; row++) {
            const terrain_fields_area_t area = {.origin_x = -half,
                                                .origin_y = -half + (int32_t)(row * STEP),
                                                .step_x = (int32_t)STEP,
                                                .step_y = 1,
                                                .count_x = SIDE,
                                                .count_y = 1u};
            terrain_hydrology_columns_area(terrain.borrowed_hydrology, &area, water);
            bool any = false;
            for (uint32_t i = 0u; i < SIDE && !any; i++) {
                any = water[i].lift >= 550.0f32;
            }
            if (!any) {
                continue;
            }
            terrain_biome_columns_area(terrain.borrowed_biome, &area, biomes);
            for (uint32_t i = 0u; i < SIDE; i++) {
                if (water[i].lift < 550.0f32 || water[i].kind != TERRAIN_HYDROLOGY_KIND_LAND ||
                    terrain_biome_height(water[i].ground, water[i].lift) >= 200.0f32) {
                    continue;
                }
                const int32_t x = area.origin_x + (int32_t)(i * STEP), y = area.origin_y;
                const float32_t east = terrain_height_estimate(&terrain, x + 8, y);
                const float32_t west = terrain_height_estimate(&terrain, x - 8, y);
                const float32_t north = terrain_height_estimate(&terrain, x, y + 8);
                const float32_t south = terrain_height_estimate(&terrain, x, y - 8);
                const float32_t slope = fmaxf(fabsf(east - west), fabsf(north - south)) / 16.0f32;
                if (slope >= 0.1f32) {
                    continue;
                }
                tops++;
                const uint8_t b = biomes[i].biome;
                lowland += (b != BIOME_ID_STONY_PEAKS && b != BIOME_ID_JAGGED_PEAKS && b != BIOME_ID_FROZEN_PEAKS &&
                            b != BIOME_ID_SNOWY_SLOPES)
                               ? 1u
                               : 0u;
                /* What it would have been by its real height: stony_peaks starts at 500. */
                peaks_by_height += water[i].ground >= 500.0f32 ? 1u : 0u;
            }
        }
        terrain_deinit(&terrain);
    }
    (void)COLUMNS;
    printf(
        "%" PRIu64 " flat plateau tops at 550 or more on 3 seeds, %" PRIu64 " standing at 500 or more: %.2f%% chose a "
        "lowland biome\n",
        tops, peaks_by_height, (libc_double_t)(tops > 0u ? 100.0f64 * (float64_t)lowland / (float64_t)tops : 0.0f64));
    CHECK(tops >= 100u);
    CHECK((float64_t)lowland >= 0.95f64 * (float64_t)tops);
}

TEST_MAIN()
