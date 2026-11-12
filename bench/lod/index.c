/* bench_lod: what one far-field column costs on a worker, at each level, on Ahmed's seed. See README.md. */
#include "render/lod/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    BENCH_LOD_PLACES = 6u,
    BENCH_LOD_ROUNDS = 2u,
    BENCH_LOD_SHIFT_FIRST = 1u,
    BENCH_LOD_SHIFT_LAST = 6u,
    /* Plan 005 phase 14's target for one column's worker time. */
    BENCH_LOD_TARGET_MS = 50u,
};
enum : uint64_t {
    /* Ahmed's seed, from plan 005 phase 14. */
    BENCH_LOD_SEED = 11107447384583428913ull,
};

/* Columns in level units are worked out per shift from these places in blocks: the spawn, and ground of other kinds round it. */
static const int32_t BENCH_LOD_PLACES_XY[BENCH_LOD_PLACES][2] = {
    {-8, -8}, {1500, 300}, {-2600, 1900}, {4000, -4000}, {-6000, -500}, {9000, 7000},
};

/* What the far field sampled before plan 005 phase 14: every stage it takes, over all 1536 layers, each line walking the chunks of what
 * grows again. Kept here to be timed against. */
static void bench_lod_sample_whole(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                                   uint8_t *scratch) {
    (void)scratch;
    const terrain_t *const terrain = borrowed_context;
    const terrain_density_box_t box = {.origin_x = x,
                                       .origin_y = y,
                                       .origin_z = COLUMN_Z_STORAGE_MIN,
                                       .count_x = 1u,
                                       .count_y = 1u,
                                       .count_z = RENDER_LOD_HEIGHT_BLOCKS};
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, &box, out);
    terrain_surface_fill_box(terrain->borrowed_surface, &box, out);
    if (shift <= RENDER_LOD_FEATURES_SHIFT_MAX) {
        terrain_feature_fill_box(terrain->borrowed_feature, &box, out);
        return;
    }
    /* render_lod_sample_freeze, as it was. */
    uint32_t top = RENDER_LOD_HEIGHT_BLOCKS;
    while (top > 0u && out[top - 1u] == BLOCK_ID_AIR) {
        top--;
    }
    if (top > 0u && out[top - 1u] == BLOCK_ID_WATER) {
        terrain_climate_sample_t climate; /* plan 006 phase 02: the fallback, as nothing reads it yet */
        terrain_climate_fallback(terrain->seed, x - 8, y - 8, &climate);
        if (biome_features[terrain_biome_column_at(terrain->borrowed_biome, &climate, x, y).biome][BIOME_FEATURE_ICE] >
            0.0f32) {
            out[top - 1u] = BLOCK_ID_ICE;
        }
    }
}

/* One column of one level, as render_lod_job_run samples it, through sample. Returns nanoseconds. */
static int64_t bench_lod_column(const terrain_t *terrain, render_lod_sample_t sample, int32_t chunk_x, int32_t chunk_y,
                                uint32_t shift, uint16_t *samples, uint16_t *cells, uint8_t *scratch,
                                uint64_t *checksum) {
    const uint32_t size = 1u << shift;
    const int64_t started = time_now_ns();
    memset(scratch, 0, RENDER_LOD_SAMPLE_SCRATCH_BYTES);
    for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
        for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
            const int32_t world_x =
                (chunk_x * (int32_t)COLUMN_SIZE + (int32_t)x) * (int32_t)size + (int32_t)(size / 2u);
            const int32_t world_y =
                (chunk_y * (int32_t)COLUMN_SIZE + (int32_t)y) * (int32_t)size + (int32_t)(size / 2u);
            sample(terrain, world_x, world_y, shift, samples, scratch);
            const int32_t top = render_lod_cells_from_column(samples, shift, 1u, cells);
            *checksum = *checksum * 31u + (uint64_t)(top + 1) * 65537u + cells[top < 0 ? 0 : top];
        }
    }
    return time_now_ns() - started;
}

/* One way at one level: the mean and the worst of its best rounds, in milliseconds. */
static void bench_lod_way(const terrain_t *terrain, render_lod_sample_t sample, uint32_t shift, uint16_t *samples,
                          uint16_t *cells, uint8_t *scratch, float64_t *out_mean, float64_t *out_worst,
                          uint64_t *out_checksum, uint64_t *out_loads) {
    float64_t sum_ms = 0.0f64, worst_ms = 0.0f64;
    uint64_t checksum = 0u;
    for (uint32_t place = 0u; place < BENCH_LOD_PLACES; place++) {
        const int32_t chunk_x = BENCH_LOD_PLACES_XY[place][0] >> (4 + (int32_t)shift);
        const int32_t chunk_y = BENCH_LOD_PLACES_XY[place][1] >> (4 + (int32_t)shift);
        int64_t best = INT64_MAX;
        for (uint32_t round = 0u; round < BENCH_LOD_ROUNDS; round++) {
            uint64_t each = 0u;
            const int64_t took =
                bench_lod_column(terrain, sample, chunk_x, chunk_y, shift, samples, cells, scratch, &each);
            best = took < best ? took : best;
            *out_loads += round == 0u ? ((const terrain_feature_cache_t *)(const void *)scratch)->loads : 0u;
            checksum = round == 0u ? checksum * 1000003u + each : checksum;
        }
        const float64_t ms = (float64_t)best / 1.0e6f64;
        sum_ms += ms;
        worst_ms = ms > worst_ms ? ms : worst_ms;
    }
    *out_mean = sum_ms / (float64_t)BENCH_LOD_PLACES;
    *out_worst = worst_ms;
    *out_checksum = checksum;
}

int32_t main(void) {
    static terrain_t bench_terrain;
    static uint16_t bench_samples[RENDER_LOD_HEIGHT_BLOCKS];
    static uint16_t bench_cells[RENDER_LOD_HEIGHT_BLOCKS];
    alignas(MC_CPU_CACHE_LINE_BYTES) static uint8_t bench_scratch[RENDER_LOD_SAMPLE_SCRATCH_BYTES];
    if (!terrain_init(&bench_terrain, BENCH_LOD_SEED)) {
        fprintf(stderr, "bench_lod: could not start\n");
        return 2;
    }
    printf("bench_lod: one far-field column, 16 x 16 lines, on one thread, best of %" PRIu32 " rounds, %" PRIu32
           " places, target %" PRIu32 " ms\n",
           BENCH_LOD_ROUNDS, BENCH_LOD_PLACES, BENCH_LOD_TARGET_MS);
    printf("%-22s %14s %14s %14s %14s %-9s %s\n", "level", "before mean ms", "before worst", "now mean ms", "now worst",
           "same tops", "chunks loaded");
    int32_t status = 0;
    for (uint32_t shift = BENCH_LOD_SHIFT_FIRST; shift <= BENCH_LOD_SHIFT_LAST; shift++) {
        float64_t whole_mean = 0.0f64, whole_worst = 0.0f64, band_mean = 0.0f64, band_worst = 0.0f64;
        uint64_t whole_sum = 0u, band_sum = 0u, whole_loads = 0u, loads = 0u;
        bench_lod_way(&bench_terrain, bench_lod_sample_whole, shift, bench_samples, bench_cells, bench_scratch,
                      &whole_mean, &whole_worst, &whole_sum, &whole_loads);
        bench_lod_way(&bench_terrain, render_lod_sample_terrain, shift, bench_samples, bench_cells, bench_scratch,
                      &band_mean, &band_worst, &band_sum, &loads);
        char name[32];
        (void)snprintf(name, sizeof(name), "shift %" PRIu32 " (voxels of %" PRIu32 ")", shift, 1u << shift);
        printf("%-22s %14.2f %14.2f %14.2f %14.2f %-9s %" PRIu64 "\n", name, (libc_double_t)whole_mean,
               (libc_double_t)whole_worst, (libc_double_t)band_mean, (libc_double_t)band_worst,
               whole_sum == band_sum ? "yes" : "NO", loads / BENCH_LOD_PLACES);
        if (band_worst > (float64_t)BENCH_LOD_TARGET_MS || whole_sum != band_sum) {
            status = 1;
        }
    }
    terrain_deinit(&bench_terrain);
    return status;
}
