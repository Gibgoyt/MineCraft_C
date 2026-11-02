/* ctest run_terrain_seed. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "simd/reference/index.h"
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
    SEED_WORKERS_MANY = 16u,
    SEED_TILE_SIDE = 16u,
    SEED_TILE_COLUMNS = SEED_TILE_SIDE * SEED_TILE_SIDE,
    /* The shape of a coarse 3D density pass: every 4 blocks across the whole height. */
    SEED_SOLID_STEP = 4u,
    SEED_SOLID_ACROSS = SEED_TILE_SIDE / SEED_SOLID_STEP,
    SEED_SOLID_UP = 188u,
    SEED_SOLID_POINTS = SEED_SOLID_ACROSS * SEED_SOLID_ACROSS * SEED_SOLID_UP,
    /* Tiles on each side of the map, half of them at negative coordinates. */
    SEED_MAP_SIDE = 8u,
    SEED_TILES = SEED_MAP_SIDE * SEED_MAP_SIDE,
    /* Floats watched on each side of an output. */
    SEED_GUARD = 4u,
    SEED_STAT_POINTS = 1u << 18,
};
static const int32_t SEED_SOLID_Z_MIN = -250;
static const int32_t SEED_MAP_MIN = -4;

/* A million blocks out, where the README says terrain is still smooth. */
static const int32_t SEED_FAR_BLOCK = 1000000;

/* Pinned: see README.md. */
static const uint64_t SEED_DIGEST_PIN = 0x1ACDF595C17D8FC4ull;

static const int64_t SEED_WAIT_NS = 60 * TIME_NS_PER_SECOND;

static const uint64_t SEED_WORLD = 0x1234567890ABCDEFull;

static const terrain_noise_shape_t SEED_SHAPE_FLAT = {
    .frequency_x = 1.0f64 / 256.0f64,
    .frequency_y = 1.0f64 / 256.0f64,
    .frequency_z = 0.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 6u,
};
static const terrain_noise_shape_t SEED_SHAPE_SOLID = {
    .frequency_x = 1.0f64 / 64.0f64,
    .frequency_y = 1.0f64 / 64.0f64,
    .frequency_z = 1.0f64 / 32.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 4u,
};

/* What one job makes for one chunk-sized tile: every kind of thing later stages will build columns from. */
typedef struct seed_tile_t {
    uint64_t draws[SEED_TILE_COLUMNS];
    float32_t flat[SEED_TILE_COLUMNS];
    float32_t solid[SEED_SOLID_POINTS];
} seed_tile_t;
static_assert(sizeof(seed_tile_t) == 15104);

/* One run over all the tiles, dealt out evenly between its jobs, so that every worker of the run really makes some. */
typedef struct seed_run_t {
    _Atomic uint32_t arrived;                    /* 0: jobs at the barrier; each takes its number from this */
    uint8_t _pad0[4];                            /* 4 */
    _Atomic uint64_t worker_mask;                /* 8: who ran a job */
    _Atomic uint32_t barrier_failed;             /* 16 */
    uint32_t barrier;                            /* 20: jobs that must all be running before any does its work */
    const terrain_noise_field_t *borrowed_flat;  /* 24 */
    const terrain_noise_field_t *borrowed_solid; /* 32 */
    seed_tile_t *borrowed_tiles;                 /* 40: in map order, wherever the work was done */
    const uint32_t *borrowed_order;              /* 48: the order tiles are taken in */
    uint64_t seed;                               /* 56 */
} seed_run_t;
static_assert(sizeof(seed_run_t) == 64);

static uint32_t seed_count_bits(uint64_t bits) {
    uint32_t count = 0u;
    while (bits != 0u) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

static uint32_t seed_bits_of(float32_t value) {
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* FNV-1a over bytes, which is enough to notice a change. */
static uint64_t seed_digest(const void *memory, uint64_t bytes) {
    const uint8_t *const data = memory;
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x00000100000001B3ull;
    }
    return hash;
}

static terrain_noise_grid_t seed_flat_grid(int32_t block_x, int32_t block_y, uint32_t across) {
    return (terrain_noise_grid_t){
        .origin_x = block_x,
        .origin_y = block_y,
        .origin_z = 0,
        .step_x = 1,
        .step_y = 1,
        .step_z = 1,
        .count_x = across,
        .count_y = across,
        .count_z = 1u,
    };
}

static terrain_noise_grid_t seed_solid_grid(int32_t block_x, int32_t block_y) {
    return (terrain_noise_grid_t){
        .origin_x = block_x,
        .origin_y = block_y,
        .origin_z = SEED_SOLID_Z_MIN,
        .step_x = (int32_t)SEED_SOLID_STEP,
        .step_y = (int32_t)SEED_SOLID_STEP,
        .step_z = (int32_t)SEED_SOLID_STEP,
        .count_x = SEED_SOLID_ACROSS,
        .count_y = SEED_SOLID_ACROSS,
        .count_z = SEED_SOLID_UP,
    };
}

static int32_t seed_tile_block_x(uint32_t tile) {
    return (SEED_MAP_MIN + (int32_t)(tile % SEED_MAP_SIDE)) * (int32_t)SEED_TILE_SIDE;
}

static int32_t seed_tile_block_y(uint32_t tile) {
    return (SEED_MAP_MIN + (int32_t)(tile / SEED_MAP_SIDE)) * (int32_t)SEED_TILE_SIDE;
}

/* Makes one tile, from the fields, the seed and the tile's position and nothing else. */
static void seed_tile_fill(const seed_run_t *run, uint32_t tile, seed_tile_t *out) {
    const int32_t block_x = seed_tile_block_x(tile);
    const int32_t block_y = seed_tile_block_y(tile);
    const terrain_noise_grid_t flat = seed_flat_grid(block_x, block_y, SEED_TILE_SIDE);
    const terrain_noise_grid_t solid = seed_solid_grid(block_x, block_y);
    terrain_noise_sample_grid(run->borrowed_flat, &flat, out->flat);
    terrain_noise_sample_grid(run->borrowed_solid, &solid, out->solid);
    const uint64_t purpose = terrain_rng_derive(run->seed, terrain_rng_salt("test/draws"));
    for (uint32_t i = 0u; i < SEED_TILE_COLUMNS; i++) {
        const int32_t x = block_x + (int32_t)(i % SEED_TILE_SIDE);
        const int32_t y = block_y + (int32_t)(i / SEED_TILE_SIDE);
        terrain_rng_t rng = terrain_rng_start(terrain_rng_hash2(purpose, x, y));
        const uint64_t first = terrain_rng_next_u64(&rng);
        const uint64_t below = terrain_rng_next_below(&rng, 1000u);
        out->draws[i] = first ^ below ^ terrain_rng_hash3(purpose, x, y, SEED_SOLID_Z_MIN);
    }
}

/* Waits for the other jobs of the run, then makes its share of the tiles. Waiting is what makes this a run of that many
 * workers: no job can finish and let its worker pick up another of the run's jobs while any of them has not started. */
static void seed_job(job_worker_t *worker, void *borrowed_argument) {
    seed_run_t *const run = borrowed_argument;
    atomic_fetch_or_explicit(&run->worker_mask, 1ull << worker->index, memory_order_relaxed);
    const uint32_t ticket = atomic_fetch_add_explicit(&run->arrived, 1u, memory_order_relaxed);
    const int64_t deadline = time_now_ns() + SEED_WAIT_NS;
    while (atomic_load_explicit(&run->arrived, memory_order_relaxed) < run->barrier) {
        if (time_now_ns() > deadline) {
            atomic_store_explicit(&run->barrier_failed, 1u, memory_order_relaxed);
            break;
        }
        (void)sched_yield();
    }
    for (uint32_t i = ticket; i < SEED_TILES; i += run->barrier) {
        const uint32_t tile = run->borrowed_order[i];
        seed_tile_fill(run, tile, &run->borrowed_tiles[tile]);
    }
}

/* Runs all the tiles with this many jobs. Returns the mask of the workers that ran them, or 0 if the barrier never filled. */
static uint64_t seed_run_all(job_system_t *system, const terrain_noise_field_t *flat,
                             const terrain_noise_field_t *solid, uint64_t seed, uint32_t workers, const uint32_t *order,
                             seed_tile_t *tiles) {
    seed_run_t run = {
        .barrier = workers,
        .borrowed_flat = flat,
        .borrowed_solid = solid,
        .borrowed_tiles = tiles,
        .borrowed_order = order,
        .seed = seed,
    };
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < workers; i++) {
        job_submit(system, &group, seed_job, &run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    if (atomic_load_explicit(&run.barrier_failed, memory_order_relaxed) != 0u) {
        return 0u;
    }
    return atomic_load_explicit(&run.worker_mask, memory_order_relaxed);
}

TEST(stream_is_splitmix64) {
    /* The published vector for SplitMix64 from seed 1234567. */
    static const uint64_t expected[] = {6457827717110365317ull, 3203168211198807973ull, 9817491932198370423ull,
                                        4593380528125082431ull, 16408922859458223821ull};
    terrain_rng_t rng = terrain_rng_start(1234567u);
    for (uint32_t i = 0u; i < 5u; i++) {
        CHECK_EQ_U64(terrain_rng_next_u64(&rng), expected[i]);
    }
}

TEST(hashes_match_values_worked_out_separately) {
    /* From a separate Python program: see README.md. */
    CHECK_EQ_U64(terrain_rng_hash2(0u, 0, 0), 0x238275BC38FCBE91ull);
    CHECK_EQ_U64(terrain_rng_hash2(42u, -1, 7), 0xD8CCBC631E81BA60ull);
    CHECK_EQ_U64(terrain_rng_hash3(42u, -1, 7, -250), 0x94C96EFC7E99E2F5ull);
    CHECK_EQ_U64(terrain_rng_hash3(UINT64_MAX, INT32_MIN, INT32_MAX, 0), 0x89194880FBCD8D5Cull);
    CHECK_EQ_U64(terrain_rng_derive(42u, 7u), 0x16062D6C1339E500ull);
    CHECK_EQ_U64(terrain_rng_salt("climate/erosion"), 0x29F8826B4C41FB9Bull);
    CHECK_EQ_U64(terrain_rng_salt(""), 0xF52A15E9A9B5E89Bull);
}

TEST(hashes_tell_positions_and_seeds_apart) {
    CHECK(terrain_rng_hash2(1u, 3, 5) != terrain_rng_hash2(1u, 5, 3));
    CHECK(terrain_rng_hash2(1u, 0, 1) != terrain_rng_hash2(1u, 1, 0));
    CHECK(terrain_rng_hash2(1u, -1, 0) != terrain_rng_hash2(1u, 1, 0));
    CHECK(terrain_rng_hash2(1u, 3, 5) != terrain_rng_hash2(2u, 3, 5));
    CHECK(terrain_rng_hash3(1u, 3, 5, 0) != terrain_rng_hash2(1u, 3, 5));
    /* A seed xored with a coordinate must not be the same world moved: see rng_hash_begin. */
    CHECK(terrain_rng_hash2(5u, 1, 0) != terrain_rng_hash2(4u, 0, 0));
    CHECK(terrain_rng_derive(1u, 2u) != terrain_rng_derive(2u, 1u));
    CHECK(terrain_rng_salt("climate/erosion") != terrain_rng_salt("climate/ridges"));
}

/* Every coordinate bit, flipped: about half of the 64 output bits should change, however the position is near the origin. */
TEST(hash_avalanches) {
    uint64_t changed = 0u;
    uint64_t flips = 0u;
    for (int32_t x = -32; x < 32; x++) {
        for (int32_t y = -32; y < 32; y++) {
            const uint64_t base = terrain_rng_hash3(SEED_WORLD, x, y, 0);
            for (uint32_t bit = 0u; bit < 32u; bit++) {
                const int32_t flipped_x = (int32_t)((uint32_t)x ^ (1u << bit));
                const int32_t flipped_y = (int32_t)((uint32_t)y ^ (1u << bit));
                changed += seed_count_bits(base ^ terrain_rng_hash3(SEED_WORLD, flipped_x, y, 0));
                changed += seed_count_bits(base ^ terrain_rng_hash3(SEED_WORLD, x, flipped_y, 0));
                flips += 2u;
            }
        }
    }
    const float64_t mean = (float64_t)changed / (float64_t)flips;
    CHECK_NEAR(mean, 32.0f64, 0.2f64);
}

/* Neighbouring columns, hashed down to a few buckets, as a feature placer does. */
TEST(hash_of_a_grid_is_uniform) {
    enum : uint32_t { BUCKETS = 16u, SIDE = 512u };
    uint32_t counts[BUCKETS] = {0u};
    for (int32_t x = 0; x < (int32_t)SIDE; x++) {
        for (int32_t y = 0; y < (int32_t)SIDE; y++) {
            counts[terrain_rng_hash2(SEED_WORLD, x, y) >> 60]++;
        }
    }
    const float64_t expected = (float64_t)(SIDE * SIDE) / (float64_t)BUCKETS;
    float64_t chi_square = 0.0f64;
    for (uint32_t i = 0u; i < BUCKETS; i++) {
        const float64_t gap = (float64_t)counts[i] - expected;
        chi_square += gap * gap / expected;
    }
    /* 15 degrees of freedom: chance puts this past 45 about once in 10,000 seeds, and this one seed is fixed. */
    CHECK(chi_square < 45.0f64);
}

TEST(draws_stay_in_range) {
    terrain_rng_t rng = terrain_rng_start(SEED_WORLD);
    uint32_t seen[7] = {0u};
    for (uint32_t i = 0u; i < 70000u; i++) {
        const uint32_t below = terrain_rng_next_below(&rng, 7u);
        CHECK(below < 7u);
        seen[below]++;
        CHECK_EQ_U64(terrain_rng_next_below(&rng, 1u), 0u);
        const float32_t f32 = terrain_rng_next_f32(&rng);
        CHECK(f32 >= 0.0f32 && f32 < 1.0f32);
        const float64_t f64 = terrain_rng_next_f64(&rng);
        CHECK(f64 >= 0.0f64 && f64 < 1.0f64);
    }
    for (uint32_t i = 0u; i < 7u; i++) {
        CHECK(seen[i] > 9500u && seen[i] < 10500u);
    }
    /* A bound past half the range, where most of the rejection happens. */
    const uint32_t big = 0xC0000000u;
    uint32_t top_half = 0u;
    for (uint32_t i = 0u; i < 40000u; i++) {
        const uint32_t below = terrain_rng_next_below(&rng, big);
        CHECK(below < big);
        top_half += below >= big / 2u ? 1u : 0u;
    }
    CHECK(top_half > 19000u && top_half < 21000u);
}

/* Works a field out from its definition in the README, on the scalar kernel, with what its octaves must hold derived by hand
 * as well. So this holds the bank, the wrapper and the assembly to one another. */
TEST(field_is_the_sum_of_its_octaves) {
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SEED_WORLD));
    const terrain_noise_field_t *field = nullptr;
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/solid", &SEED_SHAPE_SOLID, &field));

    const uint64_t field_seed = terrain_rng_derive(SEED_WORLD, terrain_rng_salt("test/solid"));
    const float32_t amplitudes[] = {8.0f32 / 15.0f32, 4.0f32 / 15.0f32, 2.0f32 / 15.0f32, 1.0f32 / 15.0f32};
    CHECK_EQ_U64(field->octave_count, 4u);
    float64_t scale = 1.0f64;
    for (uint32_t o = 0u; o < 4u; o++) {
        const terrain_noise_octave_t *const octave = &field->octaves[o];
        terrain_rng_t rng = terrain_rng_start(terrain_rng_derive(field_seed, o));
        CHECK_EQ_U64(octave->seed, terrain_rng_next_u32(&rng));
        CHECK(octave->offset_x == terrain_rng_next_f64(&rng) * 256.0f64);
        CHECK(octave->offset_y == terrain_rng_next_f64(&rng) * 256.0f64);
        CHECK(octave->offset_z == terrain_rng_next_f64(&rng) * 256.0f64);
        CHECK(octave->frequency_x == scale / 64.0f64);
        CHECK(octave->frequency_y == scale / 64.0f64);
        CHECK(octave->frequency_z == scale / 32.0f64);
        CHECK_EQ_U64(seed_bits_of(octave->amplitude), seed_bits_of(amplitudes[o]));
        scale *= 2.0f64;
    }

    const terrain_noise_grid_t grid = seed_solid_grid(-48, 112);
    float32_t *const got = test_alloc(t, SEED_SOLID_POINTS * sizeof(float32_t));
    terrain_noise_sample_grid(field, &grid, got);
    for (uint32_t i = 0u; i < SEED_SOLID_POINTS; i++) {
        const int32_t ix = (int32_t)(i % SEED_SOLID_ACROSS);
        const int32_t iy = (int32_t)(i / SEED_SOLID_ACROSS % SEED_SOLID_ACROSS);
        const int32_t iz = (int32_t)(i / SEED_SOLID_ACROSS / SEED_SOLID_ACROSS);
        const float64_t x = (float64_t)(-48 + ix * 4);
        const float64_t y = (float64_t)(112 + iy * 4);
        const float64_t z = (float64_t)(SEED_SOLID_Z_MIN + iz * 4);
        float32_t sum = 0.0f32;
        for (uint32_t o = 0u; o < 4u; o++) {
            const terrain_noise_octave_t *const octave = &field->octaves[o];
            const float32_t cell_x = (float32_t)(x * octave->frequency_x + octave->offset_x);
            const float32_t cell_y = (float32_t)(y * octave->frequency_y + octave->offset_y);
            const float32_t cell_z = (float32_t)(z * octave->frequency_z + octave->offset_z);
            float32_t value = 0.0f32;
            simd_reference_noise3(&value, &cell_x, &cell_y, &cell_z, 1u, octave->seed);
            sum += value * octave->amplitude;
        }
        if (!CHECK_EQ_U64(seed_bits_of(got[i]), seed_bits_of(sum))) {
            break;
        }
    }
    terrain_deinit(&terrain);
}

/* Every way of cutting a grid must give the same bits, including cuts that land on either side of a batch's end. */
TEST(any_split_of_the_work_gives_the_same_bits) {
    enum : uint32_t { ACROSS = 128u, POINTS = ACROSS * ACROSS };
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SEED_WORLD));
    const terrain_noise_field_t *field = nullptr;
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &field));

    float32_t *const whole = test_alloc(t, POINTS * sizeof(float32_t));
    float32_t *const part = test_alloc(t, POINTS * sizeof(float32_t));
    const terrain_noise_grid_t grid = seed_flat_grid(-64, -64, ACROSS);
    CHECK_EQ_U64(terrain_noise_grid_points(&grid), POINTS);
    terrain_noise_sample_grid(field, &grid, whole);

    /* Row by row, last row first. */
    for (uint32_t row = ACROSS; row-- > 0u;) {
        terrain_noise_grid_t one_row = grid;
        one_row.origin_y = grid.origin_y + (int32_t)row;
        one_row.count_y = 1u;
        terrain_noise_sample_grid(field, &one_row, part + row * ACROSS);
    }
    CHECK_MEM_EQ(whole, part, POINTS * sizeof(float32_t));

    /* As lists of points in blocks, cut into every length near the batch sizes. */
    float64_t *const xs = test_alloc(t, POINTS * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, POINTS * sizeof(float64_t));
    float64_t *const zs = test_alloc(t, POINTS * sizeof(float64_t));
    for (uint32_t i = 0u; i < POINTS; i++) {
        xs[i] = (float64_t)(grid.origin_x + (int32_t)(i % ACROSS));
        ys[i] = (float64_t)(grid.origin_y + (int32_t)(i / ACROSS));
        zs[i] = 0.0f64;
    }
    const uint32_t lengths[] = {1u, 7u, 8u, 9u, 255u, 256u, 257u, 511u, 512u, 1000u};
    for (uint32_t which = 0u; which < sizeof(lengths) / sizeof(lengths[0]); which++) {
        memset(part, 0, POINTS * sizeof(float32_t));
        for (uint32_t done = 0u; done < POINTS;) {
            const uint32_t length = POINTS - done < lengths[which] ? POINTS - done : lengths[which];
            terrain_noise_sample_points(field, xs + done, ys + done, zs + done, length, part + done);
            done += length;
        }
        CHECK_MEM_EQ(whole, part, POINTS * sizeof(float32_t));
    }
    terrain_deinit(&terrain);
}

TEST(sampling_stays_inside_its_output_and_allocates_nothing) {
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SEED_WORLD));
    const terrain_noise_field_t *field = nullptr;
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &field));

    heap_counters_t before;
    heap_counters_read(&before);
    const uint64_t arena_before = terrain.arena_tables.alloc_count;
    const uint32_t lengths[] = {0u, 1u, 7u, 8u, 9u, 255u, 256u, 257u, 513u};
    for (uint32_t which = 0u; which < sizeof(lengths) / sizeof(lengths[0]); which++) {
        const uint32_t length = lengths[which];
        float32_t out[513u + 2u * SEED_GUARD];
        float64_t coordinates[513u];
        for (uint32_t i = 0u; i < length; i++) {
            coordinates[i] = (float64_t)i;
        }
        const float32_t sentinel = 12345.0f32;
        for (uint32_t i = 0u; i < length + 2u * SEED_GUARD; i++) {
            out[i] = sentinel;
        }
        terrain_noise_sample_points(field, coordinates, coordinates, coordinates, length, out + SEED_GUARD);
        for (uint32_t i = 0u; i < SEED_GUARD; i++) {
            CHECK(out[i] == sentinel);
            CHECK(out[SEED_GUARD + length + i] == sentinel);
        }
        for (uint32_t i = 0u; i < length; i++) {
            CHECK(out[SEED_GUARD + i] != sentinel);
        }
    }
    /* An empty grid, on any axis, writes nothing. */
    float32_t untouched = 1.0f32;
    terrain_noise_grid_t empty = seed_flat_grid(0, 0, 16u);
    empty.count_z = 0u;
    CHECK_EQ_U64(terrain_noise_grid_points(&empty), 0u);
    terrain_noise_sample_grid(field, &empty, &untouched);
    CHECK(untouched == 1.0f32);

    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(terrain.arena_tables.alloc_count, arena_before);
    terrain_deinit(&terrain);
}

TEST(a_field_looks_like_noise) {
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SEED_WORLD));
    const terrain_noise_field_t *flat = nullptr;
    const terrain_noise_field_t *solid = nullptr;
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &flat));
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/solid", &SEED_SHAPE_SOLID, &solid));

    /* A frequency of 0 on z: every height gives the same bits. */
    enum : uint32_t { SMALL = 64u };
    float32_t *const low = test_alloc(t, SMALL * SMALL * sizeof(float32_t));
    float32_t *const high = test_alloc(t, SMALL * SMALL * sizeof(float32_t));
    terrain_noise_grid_t plane = seed_flat_grid(-32, -32, SMALL);
    plane.origin_z = -250;
    terrain_noise_sample_grid(flat, &plane, low);
    plane.origin_z = 500;
    terrain_noise_sample_grid(flat, &plane, high);
    CHECK_MEM_EQ(low, high, SMALL * SMALL * sizeof(float32_t));

    /* The solid one does change with height. */
    plane.origin_z = -250;
    terrain_noise_sample_grid(solid, &plane, low);
    plane.origin_z = 500;
    terrain_noise_sample_grid(solid, &plane, high);
    CHECK(memcmp(low, high, SMALL * SMALL * sizeof(float32_t)) != 0);

    /* Roughly -1 to 1, centered, and with real variation. */
    const terrain_noise_grid_t big = {
        .origin_x = -256,
        .origin_y = -256,
        .origin_z = -128,
        .step_x = 1,
        .step_y = 1,
        .step_z = 3,
        .count_x = 512u,
        .count_y = 512u,
        .count_z = 1u,
    };
    CHECK_EQ_U64(terrain_noise_grid_points(&big), SEED_STAT_POINTS);
    float32_t *const values = test_alloc(t, SEED_STAT_POINTS * sizeof(float32_t));
    terrain_noise_sample_grid(solid, &big, values);
    float64_t total = 0.0f64;
    float64_t squares = 0.0f64;
    float32_t lowest = values[0];
    float32_t highest = values[0];
    for (uint32_t i = 0u; i < SEED_STAT_POINTS; i++) {
        total += (float64_t)values[i];
        squares += (float64_t)values[i] * (float64_t)values[i];
        lowest = values[i] < lowest ? values[i] : lowest;
        highest = values[i] > highest ? values[i] : highest;
    }
    const float64_t mean = total / (float64_t)SEED_STAT_POINTS;
    const float64_t root_mean_square = sqrt(squares / (float64_t)SEED_STAT_POINTS);
    printf("values from %.4f to %.4f, mean %.4f, rms %.4f\n", (libc_double_t)lowest, (libc_double_t)highest,
           (libc_double_t)mean, (libc_double_t)root_mean_square);
    CHECK(lowest > -1.1f32 && highest < 1.1f32);
    CHECK(lowest < -0.3f32 && highest > 0.3f32);
    CHECK_NEAR(mean, 0.0f64, 0.05f64);
    CHECK(root_mean_square > 0.08f64);

    /* The offset's job: the origin, a whole-number point of every octave, is not forced to 0. */
    const terrain_noise_grid_t origin = seed_flat_grid(0, 0, 1u);
    float32_t at_origin = 0.0f32;
    terrain_noise_sample_grid(solid, &origin, &at_origin);
    CHECK(at_origin != 0.0f32);
    terrain_deinit(&terrain);
}

/* The README's claim about how far out a field is good, at both ends of it. */
TEST(far_from_the_origin_it_stays_smooth_then_stays_finite) {
    enum : uint32_t { ACROSS = 64u };
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SEED_WORLD));
    const terrain_noise_field_t *solid = nullptr;
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/solid", &SEED_SHAPE_SOLID, &solid));

    float32_t values[ACROSS];
    terrain_noise_grid_t line = seed_flat_grid(SEED_FAR_BLOCK, -SEED_FAR_BLOCK, 1u);
    line.count_x = ACROSS;
    terrain_noise_sample_grid(solid, &line, values);
    /* The steepest gradient noise gets is under 3 per cell, and each octave adds amplitude * frequency of that. */
    const float32_t steepest = 3.0f32 * (4.0f32 * (8.0f32 / 15.0f32) / 64.0f32);
    bool all_equal = true;
    for (uint32_t i = 1u; i < ACROSS; i++) {
        CHECK(fabsf(values[i] - values[i - 1u]) <= steepest);
        all_equal = all_equal && values[i] == values[0];
    }
    CHECK(!all_equal);

    const int32_t extremes[] = {INT32_MIN, INT32_MIN + 1, INT32_MAX - 1, INT32_MAX};
    for (uint32_t i = 0u; i < 4u; i++) {
        const terrain_noise_grid_t edge = seed_flat_grid(extremes[i], extremes[3u - i], 1u);
        float32_t first = 0.0f32;
        float32_t second = 0.0f32;
        terrain_noise_sample_grid(solid, &edge, &first);
        terrain_noise_sample_grid(solid, &edge, &second);
        CHECK(isfinite(first));
        CHECK_EQ_U64(seed_bits_of(first), seed_bits_of(second));
    }
    terrain_deinit(&terrain);
}

/* The stage's gate. */
TEST(one_and_sixteen_workers_give_the_same_bytes) {
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SEED_WORLD));
    const terrain_noise_field_t *flat = nullptr;
    const terrain_noise_field_t *solid = nullptr;
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &flat));
    CHECK(terrain_noise_bank_add(&terrain.noise_bank, "test/solid", &SEED_SHAPE_SOLID, &solid));

    uint32_t forward[SEED_TILES];
    uint32_t backward[SEED_TILES];
    for (uint32_t i = 0u; i < SEED_TILES; i++) {
        forward[i] = i;
        backward[i] = SEED_TILES - 1u - i;
    }
    const uint64_t bytes = SEED_TILES * sizeof(seed_tile_t);
    seed_tile_t *const by_one = test_alloc(t, bytes);
    seed_tile_t *const by_many = test_alloc(t, bytes);
    seed_tile_t *const by_many_backward = test_alloc(t, bytes);

    thread_topology_t topology;
    job_system_t system;
    CHECK(thread_topology_read(&topology));
    CHECK(job_init(&system, &topology));
    const uint64_t one_mask = seed_run_all(&system, flat, solid, terrain.seed, 1u, forward, by_one);
    const uint64_t many_mask = seed_run_all(&system, flat, solid, terrain.seed, SEED_WORKERS_MANY, forward, by_many);
    const uint64_t backward_mask =
        seed_run_all(&system, flat, solid, terrain.seed, SEED_WORKERS_MANY, backward, by_many_backward);
    job_deinit(&system);

    CHECK_EQ_U64(seed_count_bits(one_mask), 1u);
    CHECK_EQ_U64(seed_count_bits(many_mask), SEED_WORKERS_MANY);
    CHECK_EQ_U64(seed_count_bits(backward_mask), SEED_WORKERS_MANY);
    CHECK_MEM_EQ(by_one, by_many, bytes);
    CHECK_MEM_EQ(by_one, by_many_backward, bytes);

    /* It must be real work to be a real comparison: a bug that left the tiles empty would pass all of the above. */
    CHECK(by_one[0].flat[0] != by_one[0].flat[1]);
    CHECK(by_one[0].solid[0] != by_one[0].solid[1]);
    CHECK(by_one[0].draws[0] != by_one[0].draws[1]);
    CHECK(memcmp(&by_one[0], &by_one[1], sizeof(seed_tile_t)) != 0);

    const uint64_t digest = seed_digest(by_one, bytes);
    printf("digest of all the tiles: 0x%016" PRIX64 "\n", digest);
    CHECK_EQ_U64(digest, SEED_DIGEST_PIN);
    terrain_deinit(&terrain);
}

TEST(another_seed_is_another_world) {
    terrain_t first;
    terrain_t second;
    CHECK(terrain_init(&first, SEED_WORLD));
    CHECK(terrain_init(&second, SEED_WORLD + 1u));
    const terrain_noise_field_t *a = nullptr;
    const terrain_noise_field_t *b = nullptr;
    const terrain_noise_field_t *other_name = nullptr;
    CHECK(terrain_noise_bank_add(&first.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &a));
    CHECK(terrain_noise_bank_add(&second.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &b));
    CHECK(terrain_noise_bank_add(&first.noise_bank, "test/other", &SEED_SHAPE_FLAT, &other_name));
    float32_t from_a[SEED_TILE_COLUMNS];
    float32_t from_b[SEED_TILE_COLUMNS];
    float32_t from_other_name[SEED_TILE_COLUMNS];
    const terrain_noise_grid_t grid = seed_flat_grid(0, 0, SEED_TILE_SIDE);
    terrain_noise_sample_grid(a, &grid, from_a);
    terrain_noise_sample_grid(b, &grid, from_b);
    terrain_noise_sample_grid(other_name, &grid, from_other_name);
    CHECK(memcmp(from_a, from_b, sizeof(from_a)) != 0);
    CHECK(memcmp(from_a, from_other_name, sizeof(from_a)) != 0);
    terrain_deinit(&second);
    terrain_deinit(&first);
}

/* A later stage's field going in first must not move anything in a field that was there. */
TEST(a_field_does_not_depend_on_what_was_added_before_it) {
    terrain_t first;
    terrain_t second;
    CHECK(terrain_init(&first, SEED_WORLD));
    CHECK(terrain_init(&second, SEED_WORLD));
    const terrain_noise_field_t *alone = nullptr;
    const terrain_noise_field_t *crowded = nullptr;
    const terrain_noise_field_t *ignored = nullptr;
    CHECK(terrain_noise_bank_add(&first.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &alone));
    CHECK(terrain_noise_bank_add(&second.noise_bank, "test/before", &SEED_SHAPE_SOLID, &ignored));
    CHECK(terrain_noise_bank_add(&second.noise_bank, "test/flat", &SEED_SHAPE_FLAT, &crowded));
    CHECK_MEM_EQ(alone->octaves, crowded->octaves, sizeof(alone->octaves));
    CHECK_EQ_U64(second.noise_bank.count,
                 TERRAIN_CLIMATE_NOISE_FIELDS + TERRAIN_DENSITY_NOISE_FIELDS + TERRAIN_CAVE_NOISE_FIELDS + 2u);
    terrain_deinit(&second);
    terrain_deinit(&first);
}

TEST(a_bank_refuses_what_is_wrong) {
    terrain_t terrain;
    CHECK(terrain_init(&terrain, SEED_WORLD));
    terrain_noise_bank_t *const bank = &terrain.noise_bank;
    const terrain_noise_field_t *field = &(terrain_noise_field_t){.octave_count = 99u};

    terrain_noise_shape_t bad[9];
    for (uint32_t i = 0u; i < 9u; i++) {
        bad[i] = SEED_SHAPE_SOLID;
    }
    bad[0].octaves = 0u;
    bad[1].octaves = TERRAIN_NOISE_OCTAVES_MAX + 1u;
    bad[2].lacunarity = 0.5f32;
    bad[3].lacunarity = nanf("");
    bad[4].persistence = 0.0f32;
    bad[5].persistence = 1.5f32;
    bad[6].frequency_x = -1.0f64;
    bad[7].frequency_z = 1.0f64; /* with three doublings, past a cell per block */
    bad[8].frequency_x = 0.0f64;
    bad[8].frequency_y = 0.0f64;
    bad[8].frequency_z = 0.0f64;
    for (uint32_t i = 0u; i < 9u; i++) {
        CHECK(!terrain_noise_bank_add(bank, "test/bad", &bad[i], &field));
        CHECK(field == nullptr);
    }
    /* What terrain_init added for itself is already there. */
    const uint32_t count_at_start = bank->count;
    CHECK_EQ_U64(count_at_start,
                 TERRAIN_CLIMATE_NOISE_FIELDS + TERRAIN_DENSITY_NOISE_FIELDS + TERRAIN_CAVE_NOISE_FIELDS);

    CHECK(terrain_noise_bank_add(bank, "test/same", &SEED_SHAPE_SOLID, &field));
    CHECK(field != nullptr);
    CHECK_STR_EQ(field->borrowed_name, "test/same");
    CHECK(!terrain_noise_bank_add(bank, "test/same", &SEED_SHAPE_FLAT, &field));
    CHECK(field == nullptr);
    CHECK_EQ_U64(bank->count, count_at_start + 1u);

    /* Up to exactly full, and no further. Names kept in the test arena, since a bank borrows them. */
    for (uint32_t i = count_at_start + 1u; i < TERRAIN_NOISE_FIELDS_MAX; i++) {
        char *const name = test_alloc(t, 16u);
        (void)snprintf(name, 16u, "test/%" PRIu32, i);
        CHECK(terrain_noise_bank_add(bank, name, &SEED_SHAPE_SOLID, &field));
    }
    CHECK_EQ_U64(bank->count, TERRAIN_NOISE_FIELDS_MAX);
    CHECK(!terrain_noise_bank_add(bank, "test/one_too_many", &SEED_SHAPE_SOLID, &field));
    CHECK(field == nullptr);
    terrain_deinit(&terrain);
}

TEST(init_and_deinit_are_a_pair) {
    heap_counters_t before;
    heap_counters_read(&before);
    terrain_t terrain;
    CHECK(terrain_init(&terrain, 7u));
    CHECK_EQ_U64(terrain.seed, 7u);
    CHECK_EQ_U64(terrain.noise_bank.seed, 7u);
    CHECK_EQ_U64(terrain.noise_bank.capacity, TERRAIN_NOISE_FIELDS_MAX);
    CHECK(terrain.arena_tables.used >= TERRAIN_NOISE_FIELDS_MAX * sizeof(terrain_noise_field_t));
    heap_counters_t during;
    heap_counters_read(&during);
    CHECK_EQ_U64(during.live_bytes, before.live_bytes + TERRAIN_TABLES_BYTES);
    terrain_deinit(&terrain);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);

    /* Safe again, and on a handle that never started. */
    terrain_deinit(&terrain);
    terrain_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    terrain_deinit(&zeroed);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
}

TEST_MAIN()
