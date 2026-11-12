/* bench_lod: what one far-field column costs on a worker, at each level, on Ahmed's seed. See README.md. */
#include "arena/index.h"
#include "heap/index.h"
#include "render/encode/index.h"
#include "render/lod/index.h"
#include "render/octant/index.h"
#include "world/section/index.h"
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

/* ---- Plan 009 phase 03: the pyramid's level 1 made from the blocks ---------------------------------------------------- */

enum : uint32_t {
    BENCH_LOD_LEVEL1_SECTIONS = (RENDER_LOD_HEIGHT_BLOCKS >> 1) / SECTION_SIZE,
    BENCH_LOD_COLUMN_VOXELS = COLUMN_SIZE * COLUMN_SIZE * RENDER_LOD_HEIGHT_BLOCKS,
    /* What a section's voxels carry as light here: open sky. Terrain has none of its own, and the cost does not follow it. */
    BENCH_LOD_LIGHT = 0xF0u,
};

/* What bench_lod_octants needs, in static storage. */
typedef struct bench_lod_octant_space_t {
    alignas(MC_CPU_CACHE_LINE_BYTES)
        uint8_t scratch[RENDER_LOD_SAMPLE_SCRATCH_BYTES];        /* a sample's, a whole number of lines */
    alignas(8) uint8_t encoded[WORLD_BUFFERS_SECTION_BYTES_MAX]; /* one section's bytes */
    render_encode_octant_t octant;                               /* one section's octant */
    uint16_t blocks[4][BENCH_LOD_COLUMN_VOXELS];                 /* the four chunks under one level 1 column */
    uint16_t cells[BENCH_LOD_LEVEL1_SECTIONS * SECTION_VOXELS];  /* level 1, assembled or sampled */
    uint16_t samples[RENDER_LOD_HEIGHT_BLOCKS];                  /* one sampled line */
    render_octant_section_t sections[BENCH_LOD_LEVEL1_SECTIONS];
    uint8_t opened[BENCH_LOD_COLUMN_VOXELS];                   /* terrain's working bytes */
    uint8_t light[BENCH_LOD_LEVEL1_SECTIONS * SECTION_VOXELS]; /* level 1's light */
    uint8_t _pad0[48];                                         /* to a whole line: 12944 is 16 past one */
} bench_lod_octant_space_t;
static_assert(sizeof(bench_lod_octant_space_t) % MC_CPU_CACHE_LINE_BYTES == 0u);

/* A level 1 column's bytes, from cells and light, or from a section's one value. */
static uint64_t bench_lod_encode_level1(bench_lod_octant_space_t *space, bool assembled) {
    uint64_t bytes = 0u;
    for (uint32_t s = 0u; s < BENCH_LOD_LEVEL1_SECTIONS; s++) {
        const bool cells = !assembled || space->sections[s].assembled != 0u;
        const render_encode_source_t source =
            cells ? (render_encode_source_t){space->cells + (uint64_t)s * SECTION_VOXELS,
                                             assembled ? space->light + (uint64_t)s * SECTION_VOXELS : nullptr,
                                             0u,
                                             (uint8_t)BENCH_LOD_LIGHT,
                                             {0}}
                  : (render_encode_source_t){
                        nullptr, nullptr, space->sections[s].block_uniform, space->sections[s].light_uniform, {0}};
        bytes += render_encode_section(&source, space->encoded);
    }
    return bytes;
}

/* At each place, the four chunks under one level 1 column generated as the game generates them, then: each of their 384 sections reduced to
 * its octant (what the upload's worker adds to a section's encode), the column put together from the store and encoded (what the gpu thread
 * does for an edit), its sections reduced again for level 2, and its bytes against the same column sampled from terrain and against the
 * four chunks' own. */
static void bench_lod_octants(const terrain_t *terrain, render_octant_t *store) {
    static bench_lod_octant_space_t space;
    printf("\nbench_lod: the pyramid's level 1 from the blocks (plan 009 phase 03), one thread, at each place\n");
    printf("%-14s %12s %12s %12s %12s %12s %12s %10s\n", "place", "octant us", "assemble us", "level 2 us",
           "blocks KiB", "octant KiB", "terrain KiB", "blocks live");
    float64_t sums[6] = {0.0f64, 0.0f64, 0.0f64, 0.0f64, 0.0f64, 0.0f64};
    for (uint32_t place = 0u; place < BENCH_LOD_PLACES; place++) {
        const int32_t column_x = BENCH_LOD_PLACES_XY[place][0] >> 5, column_y = BENCH_LOD_PLACES_XY[place][1] >> 5;
        uint64_t level0_bytes = 0u;
        for (uint32_t i = 0u; i < 4u; i++) {
            const int32_t chunk_x = column_x * 2 + (int32_t)(i & 1u), chunk_y = column_y * 2 + (int32_t)(i >> 1);
            const terrain_density_box_t box = {.origin_x = chunk_x * (int32_t)COLUMN_SIZE,
                                               .origin_y = chunk_y * (int32_t)COLUMN_SIZE,
                                               .origin_z = COLUMN_Z_STORAGE_MIN,
                                               .count_x = COLUMN_SIZE,
                                               .count_y = COLUMN_SIZE,
                                               .count_z = RENDER_LOD_HEIGHT_BLOCKS};
            terrain_generate_box_fallback(terrain, &box, space.opened, space.blocks[i]);
            render_octant_column_set(store, i, chunk_x, chunk_y, place + 1u);
        }
        int64_t octant_ns = 0;
        for (uint32_t i = 0u; i < 4u; i++) {
            for (uint32_t s = 0u; s < COLUMN_SECTIONS; s++) {
                const render_encode_source_t source = {
                    space.blocks[i] + (uint64_t)s * SECTION_VOXELS, nullptr, 0u, (uint8_t)BENCH_LOD_LIGHT, {0}};
                level0_bytes += render_encode_section(&source, space.encoded);
                const int64_t started = time_now_ns();
                (void)render_encode_section_octant(&source, &space.octant);
                octant_ns += time_now_ns() - started;
                render_octant_section_set(store, 0u, i, s, &space.octant);
            }
        }
        const uint32_t slots[4] = {0u, 1u, 2u, 3u};
        int64_t started = time_now_ns();
        (void)render_octant_column_assemble(store, 0u, slots, space.cells, space.light, space.sections);
        const uint64_t octant_bytes = bench_lod_encode_level1(&space, true);
        const int64_t assemble_ns = time_now_ns() - started;
        started = time_now_ns();
        for (uint32_t s = 0u; s < BENCH_LOD_LEVEL1_SECTIONS; s++) {
            const render_encode_source_t source =
                space.sections[s].assembled != 0u
                    ? (render_encode_source_t){space.cells + (uint64_t)s * SECTION_VOXELS,
                                               space.light + (uint64_t)s * SECTION_VOXELS,
                                               0u,
                                               0u,
                                               {0}}
                    : (render_encode_source_t){
                          nullptr, nullptr, space.sections[s].block_uniform, space.sections[s].light_uniform, {0}};
            (void)render_encode_section_octant(&source, &space.octant);
        }
        const int64_t level2_ns = time_now_ns() - started;
        const uint32_t live = store->blocks_live;
        /* The same column as the far field samples it, through render_lod_cells_from_column. */
        memset(space.cells, 0, sizeof(space.cells));
        memset(space.scratch, 0, sizeof(space.scratch));
        for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
            for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
                const int32_t world_x = (column_x * (int32_t)COLUMN_SIZE + (int32_t)x) * 2 + 1;
                const int32_t world_y = (column_y * (int32_t)COLUMN_SIZE + (int32_t)y) * 2 + 1;
                render_lod_sample_terrain(terrain, world_x, world_y, 1u, space.samples, space.scratch);
                (void)render_lod_cells_from_column(space.samples, 1u, (uint64_t)COLUMN_SIZE * COLUMN_SIZE,
                                                   space.cells + section_voxel_index(x, y, 0u));
            }
        }
        const uint64_t terrain_bytes = bench_lod_encode_level1(&space, false);
        const float64_t row[6] = {(float64_t)octant_ns / 1.0e3f64 / (float64_t)(4u * COLUMN_SECTIONS),
                                  (float64_t)assemble_ns / 1.0e3f64,
                                  (float64_t)level2_ns / 1.0e3f64,
                                  (float64_t)level0_bytes / 1024.0f64,
                                  (float64_t)octant_bytes / 1024.0f64,
                                  (float64_t)terrain_bytes / 1024.0f64};
        char name[24];
        (void)snprintf(name, sizeof(name), "%" PRId32 ", %" PRId32, BENCH_LOD_PLACES_XY[place][0],
                       BENCH_LOD_PLACES_XY[place][1]);
        printf("%-14s %12.2f %12.1f %12.1f %12.1f %12.1f %12.1f %10" PRIu32 "\n", name, (libc_double_t)row[0],
               (libc_double_t)row[1], (libc_double_t)row[2], (libc_double_t)row[3], (libc_double_t)row[4],
               (libc_double_t)row[5], live);
        for (uint32_t i = 0u; i < 6u; i++) {
            sums[i] += row[i];
        }
    }
    printf(
        "%-14s %12.2f %12.1f %12.1f %12.1f %12.1f %12.1f\n", "mean",
        (libc_double_t)(sums[0] / (float64_t)BENCH_LOD_PLACES), (libc_double_t)(sums[1] / (float64_t)BENCH_LOD_PLACES),
        (libc_double_t)(sums[2] / (float64_t)BENCH_LOD_PLACES), (libc_double_t)(sums[3] / (float64_t)BENCH_LOD_PLACES),
        (libc_double_t)(sums[4] / (float64_t)BENCH_LOD_PLACES), (libc_double_t)(sums[5] / (float64_t)BENCH_LOD_PLACES));
    printf("level 1 from octants is %.1f%% of the blocks' bytes under it (the master's 1/8 is 12.5%%)\n",
           (libc_double_t)(100.0f64 * sums[4] / sums[3]));
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
    heap_region_t region;
    if (!heap_reserve(render_octant_arena_bytes(4u) + MC_CPU_CACHE_LINE_BYTES, &region)) {
        fprintf(stderr, "bench_lod: no memory for the octant store\n");
        terrain_deinit(&bench_terrain);
        return 2;
    }
    arena_t arena;
    arena_init(&arena, "bench_octant", region.owned_base, region.bytes);
    static render_octant_t bench_store;
    render_octant_init(&bench_store, &arena, 4u);
    bench_lod_octants(&bench_terrain, &bench_store);
    render_octant_deinit(&bench_store);
    arena_deinit(&arena);
    heap_release(&region);
    terrain_deinit(&bench_terrain);
    return status;
}
