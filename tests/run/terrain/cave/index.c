/* ctest run_terrain_cave. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "simd/reference/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert(MC_THREADS_WORKERS >= 16); /* the plan's gate says 16 */
static_assert(MC_THREADS_WORKERS <= 64); /* the worker mask below */

enum : uint32_t {
    CAVE_LAYERS =
        751u, /* -250 to 500, both included: all terrain makes, so the pinned digests say a taller world moved nothing */
    CAVE_WORKERS_MANY = 16u,
    CAVE_CHUNK = 16u,
    CAVE_CHUNK_VOXELS = CAVE_CHUNK * CAVE_CHUNK * CAVE_LAYERS,
    CAVE_MAP_SIDE = 6u,
    CAVE_CHUNKS = CAVE_MAP_SIDE * CAVE_MAP_SIDE,
    CAVE_GUARD = 4u,
    CAVE_SEEDS = 4u,
};
static const int32_t CAVE_MAP_MIN = -3;
static const int64_t CAVE_WAIT_NS = 60 * TIME_NS_PER_SECOND;
static const uint64_t CAVE_WORLD = 0x1234567890ABCDEFull;
static const uint64_t CAVE_SEED_LIST[CAVE_SEEDS] = {0x1234567890ABCDEFull, 1ull, 0x4D494E4543524146ull, 42ull};

/* Pinned: see README.md. */
/* Plan 003 phase 8 stage 3 moved the coast and the rivers. */
static const uint64_t CAVE_DIGEST_PIN = 0x3BFEBDD9E240B6F3ull;

static uint64_t cave_digest(uint64_t hash, const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static terrain_density_box_t cave_tall_box(int32_t x, int32_t y, uint32_t across) {
    return (terrain_density_box_t){
        .origin_x = x,
        .origin_y = y,
        .origin_z = MC_WORLD_Z_MIN,
        .count_x = across,
        .count_y = across,
        .count_z = CAVE_LAYERS,
    };
}

static uint64_t cave_voxel(const terrain_density_box_t *box, uint32_t ix, uint32_t iy, uint32_t iz) {
    return (uint64_t)ix + (uint64_t)box->count_x * ((uint64_t)iy + (uint64_t)box->count_y * iz);
}

/* ---- The README's numbers ------------------------------------------------------------------------------------------ */

TEST(the_limits_are_the_ones_the_readme_gives) {
    CHECK_EQ_I64(TERRAIN_CAVE_FLOOR_TOP, -242);
    CHECK_EQ_I64(TERRAIN_CAVE_SEALED_TOP, 2);
    CHECK_EQ_I64(TERRAIN_CAVE_ROOF_CHEESE, 8);
    CHECK_EQ_I64(TERRAIN_CAVE_ROOF_NOODLE, 6);
    CHECK_EQ_I64(TERRAIN_CAVE_ROOF_SEA, 6);
    CHECK_EQ_U64(TERRAIN_CAVE_CELL, 4u);
    CHECK_EQ_U64(TERRAIN_CAVE_NOISE_FIELDS, 9u);
}

/* Each field's limit is meant to be about twice what it measures. Prints what it finds. */
TEST(raw_noise_measures_what_the_limits_were_chosen_for) {
    enum : uint32_t { LOTS = 1u << 20, FIELDS = 6u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    const terrain_cave_t *const cave = terrain.borrowed_cave;
    float32_t *const raw = test_alloc(t, LOTS * sizeof(float32_t));
    float64_t *const xs = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, LOTS * sizeof(float64_t));
    float64_t *const zs = test_alloc(t, LOTS * sizeof(float64_t));
    const terrain_noise_field_t *const fields[FIELDS] = {cave->borrowed_cheese,          cave->borrowed_cheese_region,
                                                         cave->borrowed_spaghetti_gate,  cave->borrowed_spaghetti_a,
                                                         cave->borrowed_spaghetti_width, cave->borrowed_noodle_gate};
    static const float64_t limits[FIELDS] = {0.40f64, 0.54f64, 0.54f64, 0.54f64, 0.54f64, 0.54f64};
    static const char *const names[FIELDS] = {"cheese", "region", "sp-gate", "sp-a", "sp-width", "no-gate"};
    for (uint32_t i = 0u; i < LOTS; i++) {
        xs[i] = (float64_t)((int32_t)(i % 1024u) * 83 - 42000);
        ys[i] = (float64_t)((int32_t)(i / 1024u) * 89 - 45000);
        zs[i] = (float64_t)((int32_t)(i % 751u) * 31 - 11000);
    }
    for (uint32_t f = 0u; f < FIELDS; f++) {
        terrain_noise_sample_points(fields[f], xs, ys, zs, LOTS, raw);
        float64_t squares = 0.0f64;
        uint64_t past = 0u;
        for (uint32_t i = 0u; i < LOTS; i++) {
            squares += (float64_t)raw[i] * (float64_t)raw[i];
            past += fabs((float64_t)raw[i]) >= limits[f] ? 1u : 0u;
        }
        const float64_t deviation = sqrt(squares / (float64_t)LOTS);
        printf("%-8s raw standard deviation %.4f; limit is %.2f of them; %.2f%% past it\n", names[f],
               (libc_double_t)deviation, (libc_double_t)(limits[f] / deviation),
               (libc_double_t)((float64_t)past / (float64_t)LOTS * 100.0f64));
        CHECK(limits[f] > 1.5f64 * deviation && limits[f] < 2.5f64 * deviation);
    }
    terrain_deinit(&terrain);
}

/* ---- Shares and shapes --------------------------------------------------------------------------------------------- */

/* How much of the rock each kind takes, over seeds and places, deep and shallow. A world of mostly hole, or with one kind
 * missing, fails here. The numbers are printed, as the README quotes them. */
TEST(all_three_kinds_take_a_believable_share_of_the_rock) {
    enum : uint32_t { ACROSS = 32u, PLACES_SIDE = 6u, BANDS = 3u };
    const terrain_density_box_t template_box = cave_tall_box(0, 0, ACROSS);
    const uint64_t voxels = terrain_density_box_voxels(&template_box);
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const mask = test_alloc(t, voxels);
    uint64_t rock[BANDS] = {0u, 0u, 0u};
    uint64_t cut[BANDS][3] = {{0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}};
    uint64_t any[BANDS] = {0u, 0u, 0u};
    for (uint32_t s = 0u; s < CAVE_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, CAVE_SEED_LIST[s]));
        for (uint32_t p = 0u; p < PLACES_SIDE * PLACES_SIDE; p++) {
            const terrain_density_box_t box = cave_tall_box((int32_t)(p % PLACES_SIDE) * 1021 - 3000,
                                                            (int32_t)(p / PLACES_SIDE) * 1021 - 3000, ACROSS);
            terrain_density_fill_box(terrain.borrowed_density, &box, blocks);
            terrain_cave_mask_box(terrain.borrowed_cave, &box, mask);
            for (uint32_t iz = 0u; iz < CAVE_LAYERS; iz++) {
                const int32_t z = MC_WORLD_Z_MIN + (int32_t)iz;
                const uint32_t band = z < -125 ? 0u : (z < -30 ? 1u : 2u);
                for (uint32_t i = 0u; i < ACROSS * ACROSS; i++) {
                    const uint64_t at = (uint64_t)iz * ACROSS * ACROSS + i;
                    if (blocks[at] != BLOCK_ID_STONE) {
                        continue;
                    }
                    rock[band]++;
                    any[band] += mask[at] != 0u ? 1u : 0u;
                    for (uint32_t k = 0u; k < 3u; k++) {
                        cut[band][k] += (mask[at] & (1u << k)) != 0u ? 1u : 0u;
                    }
                }
            }
        }
        terrain_deinit(&terrain);
    }
    static const char *const band_names[BANDS] = {"deep", "middle", "shallow"};
    for (uint32_t b = 0u; b < BANDS; b++) {
        const float64_t all = (float64_t)rock[b];
        printf("%-8s rock %" PRIu64 ": cheese %.2f%%, spaghetti %.2f%%, noodle %.2f%%, any %.2f%%\n", band_names[b],
               rock[b], (libc_double_t)((float64_t)cut[b][0] / all * 100.0f64),
               (libc_double_t)((float64_t)cut[b][1] / all * 100.0f64),
               (libc_double_t)((float64_t)cut[b][2] / all * 100.0f64),
               (libc_double_t)((float64_t)any[b] / all * 100.0f64));
        REQUIRE(rock[b] > 0u);
        CHECK(cut[b][0] > 0u);
        CHECK(cut[b][1] > 0u);
        CHECK(cut[b][2] > 0u);
    }
    const float64_t deep = (float64_t)rock[0];
    CHECK((float64_t)cut[0][0] / deep > 0.015f64 && (float64_t)cut[0][0] / deep < 0.12f64);
    CHECK((float64_t)cut[0][1] / deep > 0.004f64 && (float64_t)cut[0][1] / deep < 0.05f64);
    CHECK((float64_t)cut[0][2] / deep > 0.001f64 && (float64_t)cut[0][2] / deep < 0.02f64);
    CHECK((float64_t)any[0] / deep < 0.15f64);
    /* The shallow rock keeps its roof, so it has less of each. */
    CHECK((float64_t)any[2] / (float64_t)rock[2] < (float64_t)any[0] / deep);
}

/* The size of the thinnest way across: from a voxel of a kind, how far before leaving it, along each axis, and the least of the
 * three. */
static uint32_t cave_thinnest_at(const uint8_t *mask, const terrain_density_box_t *box, uint32_t ix, uint32_t iy,
                                 uint32_t iz, uint8_t kind) {
    uint32_t least = UINT32_MAX;
    const uint32_t counts[3] = {box->count_x, box->count_y, box->count_z};
    const uint32_t at[3] = {ix, iy, iz};
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        uint32_t chord = 1u;
        uint32_t place[3] = {at[0], at[1], at[2]};
        while (place[axis] > 0u) {
            place[axis]--;
            if ((mask[cave_voxel(box, place[0], place[1], place[2])] & kind) == 0u) {
                break;
            }
            chord++;
        }
        place[axis] = at[axis];
        while (place[axis] + 1u < counts[axis]) {
            place[axis]++;
            if ((mask[cave_voxel(box, place[0], place[1], place[2])] & kind) == 0u) {
                break;
            }
            chord++;
        }
        least = chord < least ? chord : least;
    }
    return least;
}

TEST(noodles_are_thinner_than_spaghetti_and_spaghetti_than_cheese) {
    enum : uint32_t { ACROSS = 96u, TALL = 96u, PLACES = 8u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    uint8_t *const mask = test_alloc(t, (uint64_t)ACROSS * ACROSS * TALL);
    uint64_t totals[3] = {0u, 0u, 0u};
    uint64_t voxels[3] = {0u, 0u, 0u};
    for (uint32_t p = 0u; p < PLACES; p++) {
        const terrain_density_box_t box = {
            .origin_x = (int32_t)p * 503 - 2000,
            .origin_y = (int32_t)p * 311 + 700,
            .origin_z = -200,
            .count_x = ACROSS,
            .count_y = ACROSS,
            .count_z = TALL,
        };
        terrain_cave_mask_box(terrain.borrowed_cave, &box, mask);
        for (uint32_t iz = 0u; iz < TALL; iz++) {
            for (uint32_t iy = 0u; iy < ACROSS; iy++) {
                for (uint32_t ix = 0u; ix < ACROSS; ix++) {
                    const uint8_t bits = mask[cave_voxel(&box, ix, iy, iz)];
                    for (uint32_t k = 0u; k < 3u; k++) {
                        if ((bits & (1u << k)) != 0u) {
                            totals[k] += cave_thinnest_at(mask, &box, ix, iy, iz, (uint8_t)(1u << k));
                            voxels[k]++;
                        }
                    }
                }
            }
        }
    }
    REQUIRE(voxels[0] > 0u && voxels[1] > 0u && voxels[2] > 0u);
    const float64_t cheese = (float64_t)totals[0] / (float64_t)voxels[0];
    const float64_t spaghetti = (float64_t)totals[1] / (float64_t)voxels[1];
    const float64_t noodle = (float64_t)totals[2] / (float64_t)voxels[2];
    printf("mean thinnest way across, in blocks: cheese %.2f, spaghetti %.2f, noodle %.2f\n", (libc_double_t)cheese,
           (libc_double_t)spaghetti, (libc_double_t)noodle);
    CHECK(noodle < spaghetti);
    CHECK(spaghetti < cheese);
    CHECK(noodle >= 1.0f64 && noodle < 3.5f64);
    CHECK(spaghetti >= 2.5f64 && spaghetti < 8.0f64);
    CHECK(cheese >= 5.0f64);
    terrain_deinit(&terrain);
}

/* Tubes are one long thing, not beads: most of a kind's voxels belong to pieces that go a long way. This is what blending a
 * tube's two fields, rather than the tube itself, is there to give. */
TEST(tubes_are_long_and_join_up) {
    enum : uint32_t { ACROSS = 128u, TALL = 96u, VOXELS = ACROSS * ACROSS * TALL, KINDS = 2u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    const terrain_density_box_t box = {
        .origin_x = 1800,
        .origin_y = -900,
        .origin_z = -210,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    uint8_t *const mask = test_alloc(t, VOXELS);
    uint8_t *const seen = test_alloc(t, VOXELS);
    uint32_t *const stack = test_alloc(t, (uint64_t)VOXELS * sizeof(uint32_t));
    terrain_cave_mask_box(terrain.borrowed_cave, &box, mask);
    static const uint8_t kinds[KINDS] = {TERRAIN_CAVE_SPAGHETTI, TERRAIN_CAVE_NOODLE};
    static const char *const names[KINDS] = {"spaghetti", "noodle"};
    for (uint32_t k = 0u; k < KINDS; k++) {
        memset(seen, 0, VOXELS);
        uint64_t total = 0u;
        uint64_t in_long = 0u;
        uint32_t longest = 0u;
        for (uint32_t start = 0u; start < VOXELS; start++) {
            if ((mask[start] & kinds[k]) == 0u || seen[start] != 0u) {
                continue;
            }
            /* One piece, by a flood fill from an explicit stack. */
            uint32_t held = 0u;
            uint64_t size = 0u;
            uint32_t low[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
            uint32_t high[3] = {0u, 0u, 0u};
            stack[held] = start;
            held++;
            seen[start] = 1u;
            while (held > 0u) {
                held--;
                const uint32_t at = stack[held];
                const uint32_t place[3] = {at % ACROSS, at / ACROSS % ACROSS, at / (ACROSS * ACROSS)};
                size++;
                for (uint32_t axis = 0u; axis < 3u; axis++) {
                    low[axis] = place[axis] < low[axis] ? place[axis] : low[axis];
                    high[axis] = place[axis] > high[axis] ? place[axis] : high[axis];
                }
                static const uint32_t steps[3] = {1u, ACROSS, ACROSS * ACROSS};
                const uint32_t limits[3] = {ACROSS, ACROSS, TALL};
                for (uint32_t axis = 0u; axis < 3u; axis++) {
                    if (place[axis] > 0u) {
                        const uint32_t next = at - steps[axis];
                        if ((mask[next] & kinds[k]) != 0u && seen[next] == 0u) {
                            seen[next] = 1u;
                            stack[held] = next;
                            held++;
                        }
                    }
                    if (place[axis] + 1u < limits[axis]) {
                        const uint32_t next = at + steps[axis];
                        if ((mask[next] & kinds[k]) != 0u && seen[next] == 0u) {
                            seen[next] = 1u;
                            stack[held] = next;
                            held++;
                        }
                    }
                }
            }
            uint32_t extent = 0u;
            for (uint32_t axis = 0u; axis < 3u; axis++) {
                const uint32_t across = high[axis] - low[axis] + 1u;
                extent = across > extent ? across : extent;
            }
            total += size;
            in_long += extent >= 24u ? size : 0u;
            longest = extent > longest ? extent : longest;
        }
        printf("%-9s %" PRIu64 " voxels, %.1f%% in pieces at least 24 blocks long, longest %" PRIu32 "\n", names[k],
               total, (libc_double_t)((float64_t)in_long / (float64_t)(total == 0u ? 1u : total) * 100.0f64), longest);
        REQUIRE(total > 0u);
        CHECK(longest >= 32u);
        CHECK((float64_t)in_long / (float64_t)total > 0.5f64);
    }
    terrain_deinit(&terrain);
}

/* ---- What a cave may not do ---------------------------------------------------------------------------------------- */

/* Whole columns far apart: a floor over the bedrock, always, on every seed, and bedrock never cut. */
TEST(the_floor_and_the_bedrock_are_never_cut) {
    enum : uint32_t { ACROSS = 16u, SIDE = 8u };
    const terrain_density_box_t shape = cave_tall_box(0, 0, ACROSS);
    uint64_t voxels = terrain_density_box_voxels(&shape);
    uint8_t *const mask = test_alloc(t, voxels);
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    int32_t lowest = INT32_MAX;
    int32_t highest = INT32_MIN;
    for (uint32_t s = 0u; s < CAVE_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, CAVE_SEED_LIST[s]));
        for (uint32_t p = 0u; p < SIDE * SIDE; p++) {
            const terrain_density_box_t box =
                cave_tall_box((int32_t)(p % SIDE) * 1531 - 6000, (int32_t)(p / SIDE) * 1531 - 6000, ACROSS);
            terrain_cave_mask_box(terrain.borrowed_cave, &box, mask);
            terrain_density_fill_box(terrain.borrowed_density, &box, blocks);
            (void)terrain_cave_carve_box(terrain.borrowed_cave, &box, blocks);
            for (uint64_t i = 0u; i < voxels; i++) {
                const int32_t z = MC_WORLD_Z_MIN + (int32_t)(i / ((uint64_t)ACROSS * ACROSS));
                if (mask[i] != 0u) {
                    lowest = z < lowest ? z : lowest;
                    highest = z > highest ? z : highest;
                }
                if (z == MC_WORLD_Z_MIN) {
                    CHECK_EQ_U64(blocks[i], BLOCK_ID_BEDROCK);
                } else if (z <= TERRAIN_CAVE_FLOOR_TOP) {
                    CHECK_EQ_U64(blocks[i], BLOCK_ID_STONE);
                    CHECK_EQ_U64(mask[i], 0u);
                }
            }
        }
        terrain_deinit(&terrain);
    }
    printf("caves from z = %" PRId32 " to z = %" PRId32 "\n", lowest, highest);
    CHECK(lowest > TERRAIN_CAVE_FLOOR_TOP);
    /* It goes close to the floor, too: the rule is doing the holding-up, not a lack of caves down there. */
    CHECK(lowest < TERRAIN_CAVE_FLOOR_TOP + 24);
    CHECK(highest < TERRAIN_DENSITY_NEVER_SOLID_FROM + 16);
}

/* Air in a cave next to water would be a cave the sea should already have flooded. The ground under the sea and the shore are
 * where this could go wrong, so it looks at those first. */
TEST(no_cut_voxel_ever_touches_the_sea) {
    enum : uint32_t {
        SEARCH_SIDE = 192u,
        SEARCH_COLUMNS = SEARCH_SIDE * SEARCH_SIDE,
        ACROSS = 32u,
        TALL = 300u,
        VOXELS = ACROSS * ACROSS * TALL,
        PATCHES = 80u,
    };
    float32_t *const heights = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    float32_t *const softness = test_alloc(t, SEARCH_COLUMNS * sizeof(float32_t));
    uint16_t *const before = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const after = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint64_t cut = 0u;
    uint64_t below_sea = 0u;
    uint64_t touching = 0u;
    uint32_t patches = 0u;
    uint64_t water = 0u;
    for (uint32_t s = 0u; s < CAVE_SEEDS; s++) {
        terrain_t terrain;
        REQUIRE(terrain_init(&terrain, CAVE_SEED_LIST[s]));
        const terrain_climate_area_t search = {
            .origin_x = -24000,
            .origin_y = -24000,
            .step_x = 251,
            .step_y = 251,
            .count_x = SEARCH_SIDE,
            .count_y = SEARCH_SIDE,
        };
        terrain_density_profile_area(terrain.borrowed_density, &search, heights, softness);
        uint32_t taken = 0u;
        for (uint32_t i = 0u; i < SEARCH_COLUMNS && taken < PATCHES / CAVE_SEEDS; i += 3u) {
            /* The sea bed, the shore, and low land by the shore. */
            if (heights[i] > 50.0f32 || heights[i] < -80.0f32 || (i % 13u) != 0u) {
                continue;
            }
            taken++;
            const terrain_density_box_t box = {
                .origin_x = search.origin_x + (int32_t)(i % SEARCH_SIDE) * search.step_x,
                .origin_y = search.origin_y + (int32_t)(i / SEARCH_SIDE) * search.step_y,
                .origin_z = -249,
                .count_x = ACROSS,
                .count_y = ACROSS,
                .count_z = TALL,
            };
            terrain_density_fill_box(terrain.borrowed_density, &box, before);
            memcpy(after, before, VOXELS * sizeof(uint16_t));
            cut += terrain_cave_carve_box(terrain.borrowed_cave, &box, after);
            for (uint32_t iz = 0u; iz < TALL; iz++) {
                for (uint32_t iy = 0u; iy < ACROSS; iy++) {
                    for (uint32_t ix = 0u; ix < ACROSS; ix++) {
                        const uint64_t at = cave_voxel(&box, ix, iy, iz);
                        water += after[at] == BLOCK_ID_WATER ? 1u : 0u;
                        CHECK(before[at] == after[at] || (before[at] == BLOCK_ID_STONE && after[at] == BLOCK_ID_AIR));
                        if (before[at] == after[at]) {
                            continue;
                        }
                        below_sea += box.origin_z + (int32_t)iz <= MC_WORLD_Z_SEA ? 1u : 0u;
                        bool wet = false;
                        wet = wet || (ix > 0u && after[cave_voxel(&box, ix - 1u, iy, iz)] == BLOCK_ID_WATER);
                        wet = wet || (ix + 1u < ACROSS && after[cave_voxel(&box, ix + 1u, iy, iz)] == BLOCK_ID_WATER);
                        wet = wet || (iy > 0u && after[cave_voxel(&box, ix, iy - 1u, iz)] == BLOCK_ID_WATER);
                        wet = wet || (iy + 1u < ACROSS && after[cave_voxel(&box, ix, iy + 1u, iz)] == BLOCK_ID_WATER);
                        wet = wet || (iz > 0u && after[cave_voxel(&box, ix, iy, iz - 1u)] == BLOCK_ID_WATER);
                        wet = wet || (iz + 1u < TALL && after[cave_voxel(&box, ix, iy, iz + 1u)] == BLOCK_ID_WATER);
                        touching += wet ? 1u : 0u;
                    }
                }
            }
        }
        patches += taken;
        terrain_deinit(&terrain);
    }
    printf("%" PRIu32 " patches by the sea, %" PRIu64 " water voxels: %" PRIu64 " voxels cut, %" PRIu64
           " of them below the sea's surface, %" PRIu64 " touching water\n",
           patches, water, cut, below_sea, touching);
    REQUIRE(patches == PATCHES);
    CHECK(water > 0u);
    CHECK(below_sea > 0u); /* the rule costs caves, it does not cost all of them */
    CHECK_EQ_U64(touching, 0u);
}

/* Cheese and noodles stay under a roof, so only spaghetti ever opens to the sky. */
TEST(only_spaghetti_opens_to_the_sky) {
    enum : uint32_t { ACROSS = 32u, SIDE = 16u, CHECK_ABOVE = 5u };
    const terrain_density_box_t shape = cave_tall_box(0, 0, ACROSS);
    const uint64_t voxels = terrain_density_box_voxels(&shape);
    uint8_t *const mask = test_alloc(t, voxels);
    uint16_t *const blocks = test_alloc(t, voxels * sizeof(uint16_t));
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    uint64_t roofed[2] = {0u, 0u};
    uint64_t open_roof[2] = {0u, 0u};
    uint64_t mouths = 0u;
    for (uint32_t p = 0u; p < SIDE * SIDE; p++) {
        const terrain_density_box_t box =
            cave_tall_box((int32_t)(p % SIDE) * 733 - 6000, (int32_t)(p / SIDE) * 733 - 6000, ACROSS);
        terrain_cave_mask_box(terrain.borrowed_cave, &box, mask);
        terrain_density_fill_box(terrain.borrowed_density, &box, blocks);
        for (uint32_t iy = 0u; iy < ACROSS; iy++) {
            for (uint32_t ix = 0u; ix < ACROSS; ix++) {
                for (uint32_t iz = 0u; iz + CHECK_ABOVE + 1u < CAVE_LAYERS; iz++) {
                    const uint64_t at = cave_voxel(&box, ix, iy, iz);
                    if (mask[at] == 0u || blocks[at] != BLOCK_ID_STONE) {
                        continue;
                    }
                    bool rock_above = true;
                    for (uint32_t up = 1u; up <= CHECK_ABOVE; up++) {
                        rock_above = rock_above && blocks[cave_voxel(&box, ix, iy, iz + up)] == BLOCK_ID_STONE;
                    }
                    if ((mask[at] & TERRAIN_CAVE_CHEESE) != 0u) {
                        roofed[0] += rock_above ? 1u : 0u;
                        open_roof[0] += rock_above ? 0u : 1u;
                    }
                    if ((mask[at] & TERRAIN_CAVE_NOODLE) != 0u) {
                        roofed[1] += rock_above ? 1u : 0u;
                        open_roof[1] += rock_above ? 0u : 1u;
                    }
                    if ((mask[at] & TERRAIN_CAVE_SPAGHETTI) != 0u &&
                        blocks[cave_voxel(&box, ix, iy, iz + 1u)] != BLOCK_ID_STONE && (int32_t)iz > 250) {
                        mouths++;
                    }
                }
            }
        }
    }
    printf("cheese with rock over it %" PRIu64 ", without %" PRIu64 "; noodle %" PRIu64 ", without %" PRIu64
           "; columns where spaghetti meets the sky above the sea %" PRIu64 "\n",
           roofed[0], open_roof[0], roofed[1], open_roof[1], mouths);
    CHECK(roofed[0] > 0u);
    CHECK(roofed[1] > 0u);
    CHECK_EQ_U64(open_roof[0], 0u);
    /* A tube's gate is interpolated between lattice columns, so on the wall of a needle, where a column 4 blocks away is
     * 40 higher, a noodle can come within a block or two of what the rule promises. A voxel in hundreds of thousands, and
     * only ever a block short of the 5 asked for here: plan 003 phase 8's issues have the one seen. */
    CHECK(open_roof[1] * 100000u <= roofed[1]);
    CHECK(mouths > 0u);
    /* Rare, so that the ground is not a colander: at most about one column in 200. */
    CHECK(mouths < (uint64_t)SIDE * SIDE * ACROSS * ACROSS / 100u);
    terrain_deinit(&terrain);
}

TEST(carving_changes_rock_to_air_and_nothing_else) {
    enum : uint32_t { ACROSS = 48u, TALL = 96u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    const terrain_density_box_t box = {
        .origin_x = 1800,
        .origin_y = -900,
        .origin_z = -160,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    uint8_t *const mask = test_alloc(t, VOXELS);
    uint16_t *const blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const original = test_alloc(t, VOXELS * sizeof(uint16_t));
    static const uint16_t kinds[] = {
        BLOCK_ID_STONE, BLOCK_ID_AIR,  BLOCK_ID_DEEPSLATE, BLOCK_ID_WATER, BLOCK_ID_DIRT,
        BLOCK_ID_LAVA,  BLOCK_ID_TUFF, BLOCK_ID_BEDROCK,   BLOCK_ID_STONE, BLOCK_ID_COAL_ORE,
    };
    enum : uint32_t { KINDS = sizeof(kinds) / sizeof(kinds[0]) };
    for (uint32_t i = 0u; i < VOXELS; i++) {
        blocks[i] = kinds[(i * 2654435761u) >> 28 > KINDS - 1u ? i % KINDS : (i * 2654435761u) >> 28];
    }
    memcpy(original, blocks, VOXELS * sizeof(uint16_t));
    terrain_cave_mask_box(terrain.borrowed_cave, &box, mask);
    const uint64_t changed = terrain_cave_carve_box(terrain.borrowed_cave, &box, blocks);
    uint64_t counted = 0u;
    uint64_t cut = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        const uint16_t was = original[i];
        const bool safe =
            was == BLOCK_ID_AIR || was == BLOCK_ID_WATER || was == BLOCK_ID_LAVA || was == BLOCK_ID_BEDROCK;
        cut += mask[i] != 0u ? 1u : 0u;
        const uint16_t expected = (mask[i] != 0u && !safe) ? (uint16_t)BLOCK_ID_AIR : was;
        CHECK_EQ_U64(blocks[i], expected);
        counted += blocks[i] != was ? 1u : 0u;
    }
    printf("%" PRIu64 " voxels in the mask, %" PRIu64 " changed\n", cut, changed);
    CHECK_EQ_U64(changed, counted);
    CHECK(cut > 0u);
    CHECK(changed > 0u && changed < cut);
    terrain_deinit(&terrain);
}

TEST(out_of_range_places_are_safe) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    const terrain_cave_t *const cave = terrain.borrowed_cave;
    static const int32_t places[] = {0, -1, 1, 15, 16, -16, 1000003, -1000003, 8388607, -8388608};
    for (uint32_t a = 0u; a < sizeof(places) / sizeof(places[0]); a++) {
        for (uint32_t b = 0u; b < sizeof(places) / sizeof(places[0]); b++) {
            const int32_t x = places[a];
            const int32_t y = places[b];
            CHECK_EQ_U64(terrain_cave_at(cave, x, y, MC_WORLD_Z_MIN - 1), 0u);
            CHECK_EQ_U64(terrain_cave_at(cave, x, y, MC_WORLD_Z_MIN), 0u);
            CHECK_EQ_U64(terrain_cave_at(cave, x, y, TERRAIN_CAVE_FLOOR_TOP), 0u);
            CHECK_EQ_U64(terrain_cave_at(cave, x, y, MC_WORLD_Z_MAX), 0u);
            CHECK_EQ_U64(terrain_cave_at(cave, x, y, INT32_MAX), 0u);
            CHECK_EQ_U64(terrain_cave_at(cave, x, y, INT32_MIN), 0u);
        }
    }
    /* The very ends of the coordinates, with a box that goes past them. Nothing wraps. */
    const terrain_density_box_t corner = {
        .origin_x = INT32_MAX - 1,
        .origin_y = INT32_MIN,
        .origin_z = -100,
        .count_x = 2u,
        .count_y = 2u,
        .count_z = 4u,
    };
    uint8_t mask[16];
    terrain_cave_mask_box(cave, &corner, mask);
    for (uint32_t i = 0u; i < 16u; i++) {
        CHECK(mask[i] <= (TERRAIN_CAVE_CHEESE | TERRAIN_CAVE_SPAGHETTI | TERRAIN_CAVE_NOODLE));
    }
    terrain_deinit(&terrain);
}

/* ---- Each voxel worked out by hand --------------------------------------------------------------------------------- */

/* A field on the scalar kernel: the sum of its octaves at a point. */
static float32_t cave_scalar_field(const terrain_noise_field_t *field, float64_t x, float64_t y, float64_t z) {
    float32_t sum = 0.0f32;
    for (uint32_t o = 0u; o < field->octave_count; o++) {
        const terrain_noise_octave_t *const octave = &field->octaves[o];
        const float32_t cell_x = (float32_t)(x * octave->frequency_x + octave->offset_x);
        const float32_t cell_y = (float32_t)(y * octave->frequency_y + octave->offset_y);
        const float32_t cell_z = (float32_t)(z * octave->frequency_z + octave->offset_z);
        float32_t value = 0.0f32;
        simd_reference_noise3(&value, &cell_x, &cell_y, &cell_z, 1u, octave->seed);
        sum += value * octave->amplitude;
    }
    return sum;
}

static float32_t cave_expected_spread(float32_t raw, float32_t limit) {
    const float32_t unit = fminf(1.0f32, fmaxf(-1.0f32, raw / limit));
    return unit * (1.5f32 - 0.5f32 * unit * unit);
}

static float32_t cave_expected_clamp(float32_t value) {
    return fminf(1.0f32, fmaxf(0.0f32, value));
}

/* A lattice point, from the README's sums, with every number typed out again. */
typedef struct cave_expected_t {
    float32_t cheese;          /* 0 */
    float32_t spaghetti_limit; /* 4 */
    float32_t spaghetti_a;     /* 8 */
    float32_t spaghetti_b;     /* 12 */
    float32_t noodle_limit;    /* 16 */
    float32_t noodle_a;        /* 20 */
    float32_t noodle_b;        /* 24 */
} cave_expected_t;
static_assert(sizeof(cave_expected_t) == 28);

static cave_expected_t cave_expected_corner(const terrain_cave_t *cave, int32_t x, int32_t y, int32_t z) {
    const terrain_climate_area_t one = {
        .origin_x = x, .origin_y = y, .step_x = 1, .step_y = 1, .count_x = 1u, .count_y = 1u};
    float32_t height = 0.0f32;
    float32_t softness = 0.0f32;
    terrain_density_profile_area(cave->borrowed_density, &one, &height, &softness);

    const float32_t fz = (float32_t)z;
    const float32_t cover = height - fz;
    const float32_t certain = cover - (softness + 8.0f32);
    const float32_t floor_gate = cave_expected_clamp((fz + 242.0f32) / 24.0f32);
    const float32_t cheese_gate = floor_gate * cave_expected_clamp((certain - 8.0f32) / 12.0f32);
    const float32_t noodle_gate = floor_gate * cave_expected_clamp((certain - 6.0f32) / 12.0f32);
    float32_t spaghetti_gate = floor_gate;
    if (cover < -(softness + 8.0f32 + 4.0f32)) {
        spaghetti_gate = 0.0f32;
    } else if (z <= 2) {
        spaghetti_gate = floor_gate * cave_expected_clamp((certain - 6.0f32) / 12.0f32);
    }
    const float32_t surface = cave_expected_clamp(1.0f32 - cover / 32.0f32);
    const float64_t fx = (float64_t)x;
    const float64_t fy = (float64_t)y;
    const float64_t fw = (float64_t)z;

    cave_expected_t out = {.cheese = -1.0f32};
    if (cheese_gate > 0.0f32) {
        const float32_t wanted =
            cave_expected_spread(cave_scalar_field(cave->borrowed_cheese, fx, fy, fw), 0.40f32) +
            0.35f32 * cave_expected_spread(cave_scalar_field(cave->borrowed_cheese_region, fx, fy, fw), 0.54f32) -
            0.90f32;
        out.cheese = cheese_gate * wanted - (1.0f32 - cheese_gate);
    }
    if (spaghetti_gate > 0.0f32) {
        const float32_t needed = -0.15f32 + 0.65f32 * surface;
        const float32_t open = cave_expected_clamp(
            (cave_expected_spread(cave_scalar_field(cave->borrowed_spaghetti_gate, fx, fy, fw), 0.54f32) - needed) /
            0.2f32);
        const float32_t share =
            0.5f32 +
            0.5f32 * cave_expected_spread(cave_scalar_field(cave->borrowed_spaghetti_width, fx, fy, fw), 0.54f32);
        out.spaghetti_limit = (0.04f32 + (0.085f32 - 0.04f32) * share) * open * spaghetti_gate;
    }
    if (noodle_gate > 0.0f32) {
        const float32_t open = cave_expected_clamp(
            (cave_expected_spread(cave_scalar_field(cave->borrowed_noodle_gate, fx, fy, fw), 0.54f32) - 0.1f32) /
            0.2f32);
        out.noodle_limit = 0.055f32 * open * noodle_gate;
    }
    out.spaghetti_a = cave_scalar_field(cave->borrowed_spaghetti_a, fx, fy, fw);
    out.spaghetti_b = cave_scalar_field(cave->borrowed_spaghetti_b, fx, fy, fw);
    out.noodle_a = cave_scalar_field(cave->borrowed_noodle_a, fx, fy, fw);
    out.noodle_b = cave_scalar_field(cave->borrowed_noodle_b, fx, fy, fw);
    return out;
}

static float32_t cave_lerp(float32_t a, float32_t b, float32_t t) {
    return a + t * (b - a);
}

/* A field's value at a voxel from its 8 corners: z, then x, then y. Offset is where in cave_expected_t the field is. */
static float32_t cave_expected_blend(const cave_expected_t corners[2][2][2], uint32_t field, float32_t tx, float32_t ty,
                                     float32_t tz) {
    float32_t columns[2][2];
    for (uint32_t b = 0u; b < 2u; b++) {
        for (uint32_t a = 0u; a < 2u; a++) {
            const float32_t *const low = (const float32_t *)&corners[b][a][0];
            const float32_t *const high = (const float32_t *)&corners[b][a][1];
            columns[b][a] = cave_lerp(low[field], high[field], tz);
        }
    }
    return cave_lerp(cave_lerp(columns[0][0], columns[0][1], tx), cave_lerp(columns[1][0], columns[1][1], tx), ty);
}

/* How far a test keeps from the edge of a cave: the scalar kernel and the batch one are close, not the same to the last bit, and
 * a voxel that close to an edge says nothing either way. */
static const float32_t CAVE_HAND_MARGIN = 2.0e-4f32;

TEST(each_voxel_is_worked_out_as_the_readme_says) {
    enum : uint32_t { PLACES = 40u, ACROSS = 8u, TALL = 8u };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    const terrain_cave_t *const cave = terrain.borrowed_cave;
    uint64_t compared = 0u;
    uint64_t skipped = 0u;
    uint64_t seen[3] = {0u, 0u, 0u};
    uint8_t *const everywhere = test_alloc(t, 256u * 256u * 96u);
    /* Find where each kind really is, so that the sums below are tried on caves, and not only on solid rock. */
    const terrain_density_box_t search = {
        .origin_x = 1800, .origin_y = -900, .origin_z = -200, .count_x = 256u, .count_y = 256u, .count_z = 96u};
    terrain_cave_mask_box(cave, &search, everywhere);
    uint32_t next_place = 0u;
    for (uint32_t i = 0u; i < 256u * 256u * 96u && next_place < PLACES; i += 61u) {
        const uint32_t kind = next_place % 4u;
        const bool wanted = kind == 3u ? true : (everywhere[i] & (1u << kind)) != 0u;
        if (!wanted) {
            continue;
        }
        next_place++;
        i += 40009u; /* so the next place is somewhere else */
        const int32_t base_x = search.origin_x + (int32_t)(i % 256u);
        const int32_t base_y = search.origin_y + (int32_t)(i / 256u % 256u);
        const int32_t base_z = search.origin_z + (int32_t)(i / (256u * 256u));
        for (uint32_t oz = 0u; oz < TALL; oz++) {
            for (uint32_t oy = 0u; oy < ACROSS; oy++) {
                for (uint32_t ox = 0u; ox < ACROSS; ox++) {
                    const int32_t x = base_x + (int32_t)ox - 4;
                    const int32_t y = base_y + (int32_t)oy - 4;
                    const int32_t z = base_z + (int32_t)oz - 4;
                    const int32_t x0 = (x >> 2) * 4;
                    const int32_t y0 = (y >> 2) * 4;
                    const int32_t z0 = ((z + 250) >> 2) * 4 - 250;
                    const float32_t tx = (float32_t)(x - x0) * 0.25f32;
                    const float32_t ty = (float32_t)(y - y0) * 0.25f32;
                    const float32_t tz = (float32_t)(z - z0) * 0.25f32;
                    cave_expected_t corners[2][2][2];
                    for (uint32_t c = 0u; c < 2u; c++) {
                        for (uint32_t b = 0u; b < 2u; b++) {
                            for (uint32_t a = 0u; a < 2u; a++) {
                                corners[b][a][c] = cave_expected_corner(cave, x0 + (int32_t)a * 4, y0 + (int32_t)b * 4,
                                                                        z0 + (int32_t)c * 4);
                            }
                        }
                    }
                    const float32_t cheese = cave_expected_blend(corners, 0u, tx, ty, tz);
                    const float32_t spaghetti_limit = cave_expected_blend(corners, 1u, tx, ty, tz);
                    const float32_t spaghetti_a = cave_expected_blend(corners, 2u, tx, ty, tz);
                    const float32_t spaghetti_b = cave_expected_blend(corners, 3u, tx, ty, tz);
                    const float32_t noodle_limit = cave_expected_blend(corners, 4u, tx, ty, tz);
                    const float32_t noodle_a = cave_expected_blend(corners, 5u, tx, ty, tz);
                    const float32_t noodle_b = cave_expected_blend(corners, 6u, tx, ty, tz);
                    /* How far inside or outside a tube, the same way the code asks it, but rooted, to keep the margin in the
                     * same units as the fields. */
                    const float32_t spaghetti_edge =
                        spaghetti_limit - sqrtf(spaghetti_a * spaghetti_a + spaghetti_b * spaghetti_b);
                    const float32_t noodle_edge = noodle_limit - sqrtf(noodle_a * noodle_a + noodle_b * noodle_b);
                    uint8_t expected = 0u;
                    expected |= cheese > 0.0f32 ? TERRAIN_CAVE_CHEESE : 0u;
                    expected |= (spaghetti_limit > 0.0f32 && spaghetti_edge > 0.0f32) ? TERRAIN_CAVE_SPAGHETTI : 0u;
                    expected |= (noodle_limit > 0.0f32 && noodle_edge > 0.0f32) ? TERRAIN_CAVE_NOODLE : 0u;
                    const uint8_t actual = terrain_cave_at(cave, x, y, z);
                    /* Compare each kind apart, away from its own edge. */
                    const float32_t edges[3] = {cheese, spaghetti_edge, noodle_edge};
                    for (uint32_t k = 0u; k < 3u; k++) {
                        if (fabsf(edges[k]) < CAVE_HAND_MARGIN) {
                            skipped++;
                            continue;
                        }
                        CHECK_EQ_U64(actual & (1u << k), expected & (1u << k));
                        compared++;
                        seen[k] += (expected & (1u << k)) != 0u ? 1u : 0u;
                    }
                }
            }
        }
    }
    printf("%" PRIu32 " places, %" PRIu64 " voxel-and-kinds worked out by hand, %" PRIu64
           " too close to an edge to say: cheese %" PRIu64 ", spaghetti %" PRIu64 ", noodle %" PRIu64
           " inside a cave\n",
           next_place, compared, skipped, seen[0], seen[1], seen[2]);
    REQUIRE(next_place == PLACES);
    CHECK(seen[0] > 0u);
    CHECK(seen[1] > 0u);
    CHECK(seen[2] > 0u);
    CHECK(skipped < compared / 50u);
    terrain_deinit(&terrain);
}

/* ---- Any split of the work, and any worker ------------------------------------------------------------------------- */

/* Every way of asking must give the same bytes: a box, rows backwards, slices of every size across the tiles' and cells' edges,
 * one voxel at a time, the mask and the cutting alike. */
TEST(any_split_of_the_work_gives_the_same_bytes) {
    enum : uint32_t { ACROSS = 70u, TALL = 90u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    const terrain_cave_t *const cave = terrain.borrowed_cave;
    /* Starting off every grid at once, and where there is plenty cut. */
    const terrain_density_box_t whole = {
        .origin_x = 1763,
        .origin_y = -899,
        .origin_z = -181,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    uint8_t *const mask = test_alloc(t, VOXELS);
    uint8_t *const parts = test_alloc(t, VOXELS);
    terrain_cave_mask_box(cave, &whole, mask);
    uint64_t cut = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        cut += mask[i] != 0u ? 1u : 0u;
    }
    REQUIRE(cut > 0u);

    const uint32_t lengths[] = {1u, 3u, 4u, 5u, 7u, 8u, 16u, 33u};
    for (uint32_t which = 0u; which < sizeof(lengths) / sizeof(lengths[0]); which++) {
        memset(parts, 0xEEu, VOXELS);
        for (uint32_t z_end = TALL; z_end > 0u;) {
            const uint32_t length = z_end < lengths[which] ? z_end : lengths[which];
            const uint32_t iz = z_end - length;
            terrain_density_box_t slice = whole;
            slice.origin_z = whole.origin_z + (int32_t)iz;
            slice.count_z = length;
            terrain_cave_mask_box(cave, &slice, parts + (uint64_t)iz * ACROSS * ACROSS);
            z_end = iz;
        }
        CHECK_MEM_EQ(mask, parts, VOXELS);

        memset(parts, 0xEEu, VOXELS);
        for (uint32_t iz = 0u; iz < TALL; iz++) {
            for (uint32_t iy = 0u; iy < ACROSS; iy += lengths[which]) {
                const uint32_t length = ACROSS - iy < lengths[which] ? ACROSS - iy : lengths[which];
                const terrain_density_box_t slice = {
                    .origin_x = whole.origin_x,
                    .origin_y = whole.origin_y + (int32_t)iy,
                    .origin_z = whole.origin_z + (int32_t)iz,
                    .count_x = ACROSS,
                    .count_y = length,
                    .count_z = 1u,
                };
                terrain_cave_mask_box(cave, &slice, parts + ((uint64_t)iz * ACROSS + iy) * ACROSS);
            }
        }
        CHECK_MEM_EQ(mask, parts, VOXELS);
    }

    /* One voxel at a time, every so often: the whole lattice worked out for it, against what a tile made. */
    for (uint32_t i = 0u; i < VOXELS; i += 397u) {
        const int32_t x = whole.origin_x + (int32_t)(i % ACROSS);
        const int32_t y = whole.origin_y + (int32_t)(i / ACROSS % ACROSS);
        const int32_t z = whole.origin_z + (int32_t)(i / (ACROSS * ACROSS));
        CHECK_EQ_U64(terrain_cave_at(cave, x, y, z), mask[i]);
    }

    /* Cutting in parts cuts the same. */
    uint16_t *const whole_blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const part_blocks = test_alloc(t, VOXELS * sizeof(uint16_t));
    for (uint32_t i = 0u; i < VOXELS; i++) {
        whole_blocks[i] = BLOCK_ID_STONE;
        part_blocks[i] = BLOCK_ID_STONE;
    }
    const uint64_t whole_changed = terrain_cave_carve_box(cave, &whole, whole_blocks);
    uint64_t part_changed = 0u;
    for (uint32_t iz = 0u; iz < TALL; iz += 9u) {
        terrain_density_box_t slice = whole;
        slice.origin_z = whole.origin_z + (int32_t)iz;
        slice.count_z = TALL - iz < 9u ? TALL - iz : 9u;
        part_changed += terrain_cave_carve_box(cave, &slice, part_blocks + (uint64_t)iz * ACROSS * ACROSS);
    }
    CHECK_EQ_U64(whole_changed, cut);
    CHECK_EQ_U64(part_changed, cut);
    CHECK_MEM_EQ(whole_blocks, part_blocks, VOXELS * sizeof(uint16_t));

    /* Nothing asked for: nothing written, and no crash. */
    const terrain_density_box_t nothing = {.origin_x = 5, .origin_y = 5, .origin_z = 5, .count_x = 0u, .count_y = 3u};
    terrain_cave_mask_box(cave, &nothing, parts);
    CHECK_EQ_U64(terrain_cave_carve_box(cave, &nothing, part_blocks), 0u);
    terrain_deinit(&terrain);
}

/* One run over all the chunks: as tests/run/terrain/density/ does it, so that every worker of the run really makes some. */
typedef struct cave_run_t {
    _Atomic uint32_t arrived;            /* 0: jobs at the barrier; each takes its number from this */
    uint8_t _pad0[4];                    /* 4 */
    _Atomic uint64_t worker_mask;        /* 8: who ran a job */
    _Atomic uint32_t barrier_failed;     /* 16 */
    uint32_t barrier;                    /* 20: jobs that must all be running before any does its work */
    const terrain_cave_t *borrowed_cave; /* 24 */
    uint8_t *borrowed_mask;              /* 32: CAVE_CHUNK_VOXELS per chunk, in map order */
    const uint32_t *borrowed_order;      /* 40: the order chunks are taken in */
} cave_run_t;
static_assert(sizeof(cave_run_t) == 48);

static void cave_job(job_worker_t *worker, void *borrowed_argument) {
    cave_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t ticket = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    const int64_t deadline = time_now_ns() + CAVE_WAIT_NS;
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        if (time_now_ns() > deadline) {
            atomic_store_explicit(&run->barrier_failed, 1u, memory_order_relaxed);
            break;
        }
        (void)sched_yield();
    }
    for (uint32_t i = ticket; i < CAVE_CHUNKS; i += run->barrier) {
        const uint32_t chunk = run->borrowed_order[i];
        const terrain_density_box_t box =
            cave_tall_box((CAVE_MAP_MIN + (int32_t)(chunk % CAVE_MAP_SIDE)) * (int32_t)CAVE_CHUNK,
                          (CAVE_MAP_MIN + (int32_t)(chunk / CAVE_MAP_SIDE)) * (int32_t)CAVE_CHUNK, CAVE_CHUNK);
        terrain_cave_mask_box(run->borrowed_cave, &box, run->borrowed_mask + (uint64_t)chunk * CAVE_CHUNK_VOXELS);
    }
}

static uint64_t cave_run_all(job_system_t *system, const terrain_cave_t *cave, uint32_t workers, const uint32_t *order,
                             uint8_t *mask) {
    cave_run_t run = {
        .barrier = workers,
        .borrowed_cave = cave,
        .borrowed_mask = mask,
        .borrowed_order = order,
    };
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, cave_job, &run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    if (atomic_load_explicit(&run.barrier_failed, memory_order_relaxed) != 0u) {
        return 0u;
    }
    return atomic_load_explicit(&run.worker_mask, memory_order_relaxed);
}

static uint32_t cave_count_bits(uint64_t bits) {
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

/* The gate: the same bytes from 1 worker and 16, whichever order the chunks are taken in. And a pin, against any change of a
 * shape, a limit or the lattice, and against the presets disagreeing. */
TEST(one_and_sixteen_workers_give_the_same_bytes) {
    const uint64_t bytes = (uint64_t)CAVE_CHUNKS * CAVE_CHUNK_VOXELS;
    uint8_t *const one = test_alloc(t, bytes + 2u * CAVE_GUARD);
    uint8_t *const many = test_alloc(t, bytes);
    uint8_t *const shuffled = test_alloc(t, bytes);
    uint32_t in_order[CAVE_CHUNKS];
    uint32_t out_of_order[CAVE_CHUNKS];
    for (uint32_t i = 0u; i < CAVE_CHUNKS; i++) {
        in_order[i] = i;
        out_of_order[i] = CAVE_CHUNKS - 1u - i;
    }

    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    thread_topology_t topology;
    job_system_t system;
    REQUIRE(thread_topology_read(&topology));
    REQUIRE(job_init(&system, &topology));

    uint8_t *const inside = one + CAVE_GUARD;
    for (uint32_t i = 0u; i < CAVE_GUARD; i++) {
        one[i] = 0xBEu;
        inside[bytes + i] = 0xBEu;
    }

    const int64_t started = time_now_ns();
    const uint64_t one_mask = cave_run_all(&system, terrain.borrowed_cave, 1u, in_order, inside);
    const int64_t one_done = time_now_ns();
    const uint64_t many_mask = cave_run_all(&system, terrain.borrowed_cave, CAVE_WORKERS_MANY, in_order, many);
    const int64_t many_done = time_now_ns();
    const uint64_t shuffled_mask =
        cave_run_all(&system, terrain.borrowed_cave, CAVE_WORKERS_MANY, out_of_order, shuffled);
    job_deinit(&system);

    printf("%" PRIu32 " chunks of %" PRIu32 " voxels: %.1f ms on 1 worker, %.1f ms on 16\n", (uint32_t)CAVE_CHUNKS,
           (uint32_t)CAVE_CHUNK_VOXELS, (libc_double_t)((float64_t)(one_done - started) / 1.0e6f64),
           (libc_double_t)((float64_t)(many_done - one_done) / 1.0e6f64));
    CHECK_EQ_U64(cave_count_bits(one_mask), 1u);
    CHECK_EQ_U64(cave_count_bits(many_mask), CAVE_WORKERS_MANY);
    CHECK_EQ_U64(cave_count_bits(shuffled_mask), CAVE_WORKERS_MANY);
    CHECK_MEM_EQ(inside, many, bytes);
    CHECK_MEM_EQ(inside, shuffled, bytes);
    for (uint32_t i = 0u; i < CAVE_GUARD; i++) {
        CHECK_EQ_U64(one[i], 0xBEu);
        CHECK_EQ_U64(inside[bytes + i], 0xBEu);
    }

    const uint64_t digest = cave_digest(0xCBF29CE484222325ull, inside, bytes);
    printf("digest 0x%016" PRIX64 "\n", digest);
    CHECK_EQ_U64(digest, CAVE_DIGEST_PIN);
    terrain_deinit(&terrain);
}

TEST(sampling_stays_inside_its_output_and_allocates_nothing) {
    enum : uint32_t { ACROSS = 37u, TALL = 41u, VOXELS = ACROSS * ACROSS * TALL };
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    const terrain_cave_t *const cave = terrain.borrowed_cave;
    uint8_t *const mask = test_alloc(t, VOXELS + 2u * CAVE_GUARD);
    uint16_t *const blocks = test_alloc(t, (VOXELS + 2u * CAVE_GUARD) * sizeof(uint16_t));
    for (uint32_t i = 0u; i < VOXELS + 2u * CAVE_GUARD; i++) {
        mask[i] = 0xBEu;
        blocks[i] = BLOCK_ID_STONE;
    }
    const terrain_density_box_t box = {
        .origin_x = 1800,
        .origin_y = -900,
        .origin_z = -150,
        .count_x = ACROSS,
        .count_y = ACROSS,
        .count_z = TALL,
    };
    heap_counters_t before;
    heap_counters_read(&before);
    const uint64_t arena_before = terrain.arena_tables.alloc_count;
    terrain_cave_mask_box(cave, &box, mask + CAVE_GUARD);
    (void)terrain_cave_carve_box(cave, &box, blocks + CAVE_GUARD);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(terrain.arena_tables.alloc_count, arena_before);
    for (uint32_t i = 0u; i < CAVE_GUARD; i++) {
        CHECK_EQ_U64(mask[i], 0xBEu);
        CHECK_EQ_U64(mask[VOXELS + CAVE_GUARD + i], 0xBEu);
        CHECK_EQ_U64(blocks[i], BLOCK_ID_STONE);
        CHECK_EQ_U64(blocks[VOXELS + CAVE_GUARD + i], BLOCK_ID_STONE);
    }
    terrain_deinit(&terrain);
}

/* ---- One world per seed ------------------------------------------------------------------------------------------- */

TEST(a_seed_gives_its_own_caves_and_the_same_ones_every_time) {
    enum : uint32_t { ACROSS = 48u, VOXELS = ACROSS * ACROSS * CAVE_LAYERS };
    const terrain_density_box_t box = cave_tall_box(0, 0, ACROSS);
    uint8_t *const first = test_alloc(t, VOXELS);
    uint8_t *const again = test_alloc(t, VOXELS);
    uint8_t *const other = test_alloc(t, VOXELS);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    terrain_cave_mask_box(terrain.borrowed_cave, &box, first);
    terrain_deinit(&terrain);
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    terrain_cave_mask_box(terrain.borrowed_cave, &box, again);
    terrain_deinit(&terrain);
    REQUIRE(terrain_init(&terrain, CAVE_WORLD + 1u));
    terrain_cave_mask_box(terrain.borrowed_cave, &box, other);
    terrain_deinit(&terrain);
    CHECK_MEM_EQ(first, again, VOXELS);
    CHECK(memcmp(first, other, VOXELS) != 0);
}

/* The chain of stages cuts with the mask it already asked for, as asking twice is most of a chunk's walk. It has to be the same. */
TEST(cutting_with_a_mask_is_what_carving_gives) {
    enum : uint32_t { ACROSS = 64u, TALL = 96u, VOXELS = ACROSS * ACROSS * TALL };
    uint16_t *const carved = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const cut = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const before = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint8_t *const mask = test_alloc(t, VOXELS);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    uint64_t total = 0u;
    /* From the bedrock to the sea and the ground. No real mask reaches the bedrock, so the next test holds that by hand. */
    const int32_t levels[3] = {MC_WORLD_Z_MIN, -60, -48};
    for (uint32_t l = 0u; l < 3u; l++) {
        const terrain_density_box_t box = {
            .origin_x = -32,
            .origin_y = 16,
            .origin_z = levels[l],
            .count_x = ACROSS,
            .count_y = ACROSS,
            .count_z = TALL,
        };
        terrain_hydrology_fill_box(terrain.borrowed_hydrology, &box, before);
        memcpy(carved, before, VOXELS * sizeof(uint16_t));
        memcpy(cut, before, VOXELS * sizeof(uint16_t));
        const uint64_t changed = terrain_cave_carve_box(terrain.borrowed_cave, &box, carved);
        terrain_cave_mask_box(terrain.borrowed_cave, &box, mask);
        terrain_cave_cut_with_mask(&box, mask, cut);
        CHECK_MEM_EQ(carved, cut, VOXELS * sizeof(uint16_t));
        uint64_t marked = 0u;
        uint64_t wrong = 0u;
        for (uint32_t i = 0u; i < VOXELS; i++) {
            marked += mask[i];
            wrong += (mask[i] != 0u) != (before[i] != cut[i]) ? 1u : 0u;
        }
        CHECK_EQ_U64(marked, changed);
        CHECK_EQ_U64(wrong, 0u);
        total += changed;
    }
    CHECK(total > 0u);
    terrain_deinit(&terrain);
}

/* By hand, for what real masks never ask: caves stop above the bedrock, so only this says a mask there still cuts nothing. */
TEST(a_mask_cuts_only_rock_and_says_what_it_cut) {
    enum : uint32_t { KINDS = 6u, BOTH = 2u * KINDS };
    const uint16_t kinds[KINDS] = {BLOCK_ID_AIR,     BLOCK_ID_WATER, BLOCK_ID_LAVA,
                                   BLOCK_ID_BEDROCK, BLOCK_ID_STONE, BLOCK_ID_DIRT};
    const bool is_cut[KINDS] = {false, false, false, false, true, true};
    uint16_t blocks[BOTH];
    uint8_t mask[BOTH];
    for (uint32_t i = 0u; i < KINDS; i++) {
        blocks[i] = kinds[i];
        mask[i] = 0u;
        blocks[KINDS + i] = kinds[i];
        mask[KINDS + i] = 2u; /* any kind of cave: what comes back is only 0 or 1 */
    }
    const terrain_density_box_t box = {
        .origin_x = 0, .origin_y = 0, .origin_z = 0, .count_x = BOTH, .count_y = 1u, .count_z = 1u};
    terrain_cave_cut_with_mask(&box, mask, blocks);
    for (uint32_t i = 0u; i < KINDS; i++) {
        CHECK_EQ_U64(blocks[i], kinds[i]);
        CHECK_EQ_U64(mask[i], 0u);
        CHECK_EQ_U64(blocks[KINDS + i], is_cut[i] ? (uint16_t)BLOCK_ID_AIR : kinds[i]);
        CHECK_EQ_U64(mask[KINDS + i], is_cut[i] ? 1u : 0u);
    }
}

TEST(a_full_bank_is_refused) {
    enum : uint64_t { BYTES = 1024ull * 1024ull };
    uint8_t *const memory = test_alloc(t, BYTES);
    arena_t arena;
    arena_init(&arena, "test", memory, BYTES);
    terrain_noise_bank_t bank;
    terrain_noise_bank_init(&bank, &arena, CAVE_WORLD, TERRAIN_CAVE_NOISE_FIELDS - 1u);
    terrain_cave_t *cave = (terrain_cave_t *)(uintptr_t)1u;
    CHECK(!terrain_cave_init(&cave, &arena, &bank, nullptr));
    CHECK(cave == nullptr);
    terrain_noise_bank_deinit(&bank);
    arena_deinit(&arena);
}

TEST(terrain_starts_the_caves_and_stops_them) {
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, CAVE_WORLD));
    REQUIRE(terrain.borrowed_cave != nullptr);
    const terrain_cave_t *const cave = terrain.borrowed_cave;
    CHECK(cave->borrowed_density == terrain.borrowed_density);
    const terrain_noise_field_t *const fields[TERRAIN_CAVE_NOISE_FIELDS] = {
        cave->borrowed_cheese,      cave->borrowed_cheese_region,  cave->borrowed_spaghetti_a,
        cave->borrowed_spaghetti_b, cave->borrowed_spaghetti_gate, cave->borrowed_spaghetti_width,
        cave->borrowed_noodle_a,    cave->borrowed_noodle_b,       cave->borrowed_noodle_gate};
    for (uint32_t f = 0u; f < TERRAIN_CAVE_NOISE_FIELDS; f++) {
        REQUIRE(fields[f] != nullptr);
        for (uint32_t o = 0u; o < fields[f]->octave_count; o++) {
            /* The lattice has to be able to hold what it samples: at least two points to a noise cell. */
            CHECK(fields[f]->octaves[o].frequency_x * (float64_t)TERRAIN_CAVE_CELL <= 0.5f64);
            CHECK(fields[f]->octaves[o].frequency_y * (float64_t)TERRAIN_CAVE_CELL <= 0.5f64);
            CHECK(fields[f]->octaves[o].frequency_z * (float64_t)TERRAIN_CAVE_CELL <= 0.5f64);
            CHECK(fields[f]->octaves[o].frequency_z != 0.0f64);
        }
    }
    terrain_deinit(&terrain);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK(terrain.borrowed_cave == nullptr);
}

TEST_MAIN()
