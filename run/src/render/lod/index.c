/* See README.md. The coarse grids: each column of each level made on a worker from samples of the ground, and sent to the card in the
 * world's own format, in a table that scrolls with the eye. */
#include "render/lod/index.h"

#include "assert/index.h"
#include "gpu_bridge/shade/index.h"
#include "log/index.h"
#include "profiler/index.h"
#include "render/encode/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum render_lod_column_state_t : uint8_t {
    RENDER_LOD_COLUMN_WANTED = 0,
    RENDER_LOD_COLUMN_BUILDING = 1,
    RENDER_LOD_COLUMN_BUILT = 2,
    RENDER_LOD_COLUMN_NO_ROOM = 3,
    RENDER_LOD_COLUMN_STALE = 4,      /* built and shown, and wanted again: what is under it has changed */
    RENDER_LOD_COLUMN_REBUILDING = 5, /* built and shown, and being made again */
    /* wanted, but level 0 stands for all of it, and no ray of this level goes there: asked for after everything else */
    RENDER_LOD_COLUMN_COVERED = 6,
} render_lod_column_state_t;

enum : uint64_t {
    /* Every section of the finest level, each as large as a section can be. Pages cost nothing until touched. */
    RENDER_LOD_STAGING_BYTES = (uint64_t)RENDER_LOD_SECTIONS_MAX * WORLD_BUFFERS_SECTION_BYTES_MAX,
    /* Each section's byte count, in front of a column's sections. */
    RENDER_LOD_STAGING_HEAD_BYTES = (uint64_t)RENDER_LOD_SECTIONS_MAX * sizeof(uint64_t),
    RENDER_LOD_JOB_STAGING_BYTES = RENDER_LOD_STAGING_BYTES + RENDER_LOD_STAGING_HEAD_BYTES,
};
static_assert(RENDER_LOD_HEIGHT_BLOCKS == COLUMN_SECTIONS * SECTION_SIZE);
static_assert((int32_t)RENDER_LOD_HEIGHT_BLOCKS == COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN);

int32_t render_lod_column_of(float64_t blocks, uint32_t shift) {
    return (int32_t)floor(blocks) >> (4u + shift);
}

/* Whether a slot holds a column that can be drawn. */
static bool render_lod_state_built(uint8_t state) {
    return state == RENDER_LOD_COLUMN_BUILT || state == RENDER_LOD_COLUMN_STALE ||
           state == RENDER_LOD_COLUMN_REBUILDING;
}

static uint32_t render_lod_side(uint32_t radius) {
    return 2u * radius + 1u;
}

render_lod_bands_t render_lod_bands(float64_t pixel_error, uint32_t near_guarantee, float64_t half_fov_tangent,
                                    uint32_t height_pixels) {
    render_lod_bands_t bands;
    memset(&bands, 0, sizeof(bands));
    bands.level_count = RENDER_LOD_LEVEL_COUNT;
    /* One pixel's angle, and a level's error is half its voxel: s / 2 blocks seen from d covers s / (2 d theta) pixels. */
    const float64_t theta = 2.0f64 * half_fov_tangent / (float64_t)(height_pixels != 0u ? height_pixels : 1u);
    const float64_t error = pixel_error > 0.0f64 ? pixel_error : 1.0f64;
    for (uint32_t k = 1u; k <= bands.level_count + 1u; k++) {
        const float64_t from = (float64_t)(1u << k) / (2.0f64 * error * theta);
        bands.from[k] = from > (float64_t)near_guarantee ? from : (float64_t)near_guarantee;
    }
    return bands;
}

render_lod_plan_t render_lod_plan_by_bands(const render_lod_bands_t *bands, uint32_t render_distance_chunks) {
    render_lod_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    const float64_t far_blocks = (float64_t)render_distance_chunks * (float64_t)COLUMN_SIZE;
    for (uint32_t k = 1u; k <= bands->level_count && k < LOD_SHIFT_MIN + LOD_LEVELS_MAX; k++) {
        if (bands->from[k] >= far_blocks) {
            break; /* its band starts past what is drawn at all */
        }
        const float64_t edge = bands->from[k + 1u] < far_blocks ? bands->from[k + 1u] : far_blocks;
        const float64_t column_blocks = (float64_t)((uint32_t)COLUMN_SIZE << k);
        const uint32_t wanted = (uint32_t)ceil(edge / column_blocks) + RENDER_LOD_MARGIN_COLUMNS;
        plan.shifts[plan.level_count] = (uint8_t)k;
        plan.radii[plan.level_count] =
            (uint8_t)(wanted < RENDER_LOD_RADIUS_COLUMNS_MAX ? wanted : RENDER_LOD_RADIUS_COLUMNS_MAX);
        plan.level_count++;
    }
    return plan;
}

float32_t render_lod_band_base(float32_t pixel_error, float32_t half_fov_tangent, uint32_t height_pixels) {
    if (!(pixel_error > 0.0f32) || !(half_fov_tangent > 0.0f32)) {
        return 0.0f32;
    }
    /* s / (2 E theta) with theta = 2 tan / rows is s rows / (4 E tan): this is that for s = 1. */
    return (float32_t)height_pixels / (4.0f32 * pixel_error * half_fov_tangent);
}

uint32_t render_lod_block_radius(const render_lod_bands_t *bands, uint32_t render_distance_chunks) {
    const uint32_t reach = (uint32_t)ceil(bands->from[1] / (float64_t)COLUMN_SIZE) + RENDER_LOD_BLOCK_MARGIN_CHUNKS;
    uint32_t radius = reach < RENDER_LOD_BLOCK_RADIUS_MIN_CHUNKS ? RENDER_LOD_BLOCK_RADIUS_MIN_CHUNKS : reach;
    radius = radius > RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS ? RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS : radius;
    return radius > render_distance_chunks ? render_distance_chunks : radius;
}

uint64_t render_lod_table_bytes(const render_lod_plan_t *plan, uint32_t level) {
    const uint64_t side = render_lod_side(plan->radii[level]);
    return world_buffers_table_bytes((uint32_t)(side * side));
}

uint64_t render_lod_pool_bytes(const render_lod_plan_t *plan, uint32_t level) {
    const uint64_t side = render_lod_side(plan->radii[level]);
    const uint64_t bytes =
        side * side * RENDER_LOD_POOL_BYTES_PER_COLUMN + (uint64_t)WORLD_BUFFERS_CLASS_COUNT * WORLD_BUFFERS_PAGE_BYTES;
    return bytes / WORLD_BUFFERS_PAGE_BYTES * WORLD_BUFFERS_PAGE_BYTES;
}

uint64_t render_lod_pyramid_pool_bytes(const render_lod_plan_t *plan, uint32_t level, uint32_t block_radius_chunks) {
    const uint32_t shift = plan->shifts[level];
    if (shift > RENDER_LOD_OCTANT_LEVELS) {
        return render_lod_pool_bytes(plan, level);
    }
    const uint64_t side = render_lod_side(plan->radii[level]);
    /* The square of this level's columns over the blocks, as far as the level reaches. */
    const uint64_t over = 2u * (((uint64_t)block_radius_chunks + (1u << shift) - 1u) >> shift) + 1u;
    const uint64_t near_side = over < side ? over : side;
    const uint64_t near = near_side * near_side;
    const uint64_t bytes = near * (RENDER_LOD_OCTANT_POOL_BYTES_PER_COLUMN >> (shift - 1u)) +
                           (side * side - near) * RENDER_LOD_POOL_BYTES_PER_COLUMN +
                           (uint64_t)WORLD_BUFFERS_CLASS_COUNT * WORLD_BUFFERS_PAGE_BYTES;
    return bytes / WORLD_BUFFERS_PAGE_BYTES * WORLD_BUFFERS_PAGE_BYTES;
}

uint64_t render_lod_arena_bytes(const render_lod_plan_t *plan) {
    /* Each allocation can waste its alignment. */
    uint64_t bytes = (uint64_t)RENDER_LOD_JOBS *
                         (sizeof(render_lod_job_t) + RENDER_LOD_JOB_STAGING_BYTES + MC_CPU_CACHE_LINE_BYTES) +
                     2u * MC_CPU_CACHE_LINE_BYTES;
    for (uint32_t i = 0u; i < plan->level_count; i++) {
        const uint64_t side = render_lod_side(plan->radii[i]);
        const uint64_t slots = side * side;
        bytes += world_buffers_host_bytes((uint32_t)slots, render_lod_pool_bytes(plan, i)) + slots +
                 slots * 2u * sizeof(int16_t) + 3u * MC_CPU_CACHE_LINE_BYTES;
    }
    return bytes;
}

/* ---- One column on a worker ------------------------------------------------------------------------------------------ */

/* What feature_cold_paint does, for a level too coarse for features: plan 006 phase 04's rules from the same climate sample,
 * so a far mountain shows the snowline a near one does, and far water freezes where near water does. */
static void render_lod_sample_freeze(const terrain_t *terrain, const terrain_climate_sample_t *climate, int32_t x,
                                     int32_t y, uint16_t *column) {
    terrain_feature_cold_column(terrain->borrowed_feature, climate, x, y, COLUMN_Z_STORAGE_MIN,
                                RENDER_LOD_HEIGHT_BLOCKS, column);
}

int32_t render_lod_cells_from_column(const uint16_t *column, uint32_t shift, uint64_t stride, uint16_t *out) {
    const uint32_t size = 1u << shift;
    const uint32_t voxels = RENDER_LOD_HEIGHT_BLOCKS >> shift;
    int32_t top = -1;
    for (uint32_t voxel = 0u; voxel < voxels; voxel++) {
        const uint32_t floor_block = voxel << shift;
        uint32_t count = 0u;
        uint16_t highest = BLOCK_ID_AIR;
        bool holds_ice = false;
        for (uint32_t block = 0u; block < size; block++) {
            const uint16_t state = column[floor_block + block];
            holds_ice = holds_ice || state == BLOCK_ID_ICE;
            if (render_encode_block_counts(state)) {
                count++;
                highest = state;
            }
        }
        const uint16_t kept = count * 2u >= size ? highest : (uint16_t)BLOCK_ID_AIR;
        out[voxel * stride] = kept;
        /* Ice is one block thick, so the half rule takes it: a frozen sea would read as open water until level 0 drew it. The ice shows on the water it lay on. */
        if (kept == BLOCK_ID_AIR && holds_ice && voxel > 0u && out[(voxel - 1u) * stride] == BLOCK_ID_WATER) {
            out[(voxel - 1u) * stride] = BLOCK_ID_ICE;
        }
        if (kept != BLOCK_ID_AIR) {
            top = (int32_t)voxel;
        }
    }
    return top;
}

/* The column's climate sample in scratch, filled at its first sample: a far column is 16 voxels of 1 << shift blocks a
 * side, and the sample is centred on its middle. Plan 006 phase 03: a coarse column is wider than a sample, so a line whose
 * column, and the chunks round it that features read, the sample does not hold fills it again round itself; every sample
 * that holds a column gives it the same climate, so the bytes do not depend on the order of the lines. */
static const terrain_climate_sample_t *render_lod_sample_climate(const terrain_t *terrain, const climate_t *climate,
                                                                 int32_t x, int32_t y, uint32_t shift,
                                                                 uint8_t *scratch) {
    terrain_climate_sample_t *const sample =
        (terrain_climate_sample_t *)(void *)(scratch + RENDER_LOD_SAMPLE_FEATURE_BYTES);
    uint8_t *const filled = scratch + RENDER_LOD_SAMPLE_FEATURE_BYTES + RENDER_LOD_SAMPLE_CLIMATE_BYTES;
    const int64_t reach = 2 * (int64_t)TERRAIN_FEATURE_CHUNK;
    if (*filled != 0u && !terrain_climate_sample_holds(sample, (int64_t)x - reach, (int64_t)y - reach,
                                                       (int64_t)x + reach, (int64_t)y + reach)) {
        (void)climate_terrain_sample(climate, terrain->seed, (int64_t)x - 8, (int64_t)y - 8, sample);
    }
    if (*filled == 0u) {
        const int64_t side = (int64_t)COLUMN_SIZE << shift;
        const int64_t column_x = (int64_t)x >= 0 ? (int64_t)x / side : -((-(int64_t)x + side - 1) / side);
        const int64_t column_y = (int64_t)y >= 0 ? (int64_t)y / side : -((-(int64_t)y + side - 1) / side);
        (void)climate_terrain_sample(climate, terrain->seed, column_x * side + side / 2 - 8,
                                     column_y * side + side / 2 - 8, sample);
        *filled = 1u;
    }
    return sample;
}

static void render_lod_sample_with(const terrain_t *terrain, const climate_t *climate, int32_t x, int32_t y,
                                   uint32_t shift, uint16_t *out, uint8_t *scratch) {
    const terrain_climate_sample_t *const sample = render_lod_sample_climate(terrain, climate, x, y, shift, scratch);
    const terrain_density_box_t box = {
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = 1u,
        .count_y = 1u,
        .count_z = RENDER_LOD_HEIGHT_BLOCKS,
    };
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, &box, out);
    terrain_surface_fill_box(terrain->borrowed_surface, sample, &box, out);
    if (shift <= RENDER_LOD_FEATURES_SHIFT_MAX) {
        /* The chunks round a line are the chunks round its neighbours: walked once a column, not once a line. */
        terrain_feature_fill_box_cached(terrain->borrowed_feature, sample, &box, out,
                                        (terrain_feature_cache_t *)(void *)scratch);
    } else {
        render_lod_sample_freeze(terrain, sample, x, y, out);
    }
}

void render_lod_sample_terrain(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                               uint8_t *scratch) {
    render_lod_sample_with(borrowed_context, nullptr, x, y, shift, out, scratch);
}

void render_lod_sample_world(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                             uint8_t *scratch) {
    const render_lod_world_t *const world = borrowed_context;
    render_lod_sample_with(world->borrowed_terrain, world->borrowed_climate, x, y, shift, out, scratch);
}

static void render_lod_job_run(job_worker_t *worker, void *borrowed_job) {
    render_lod_job_t *const job = borrowed_job;
    if (job_dropped(worker)) {
        /* The eye moved or the far field closed after it was asked for: render_lod_jobs_collect asks again if still wanted. */
        job->dropped = 1u;
        atomic_store_explicit(&job->done, 1u, memory_order_release);
        return;
    }
    PROFILER_BEGIN(job_worker_lane(worker), "far_column");
    const int64_t started_ns = time_now_ns();
    const render_lod_t *const lod = job->borrowed_lod;
    const uint32_t shift = lod->levels[job->level].shift;
    const uint32_t size = 1u << shift;
    const uint32_t voxels = RENDER_LOD_HEIGHT_BLOCKS >> shift;
    const uint32_t sections = (voxels + SECTION_SIZE - 1u) / SECTION_SIZE;
    /* A worker's arena is reset after every job. */
    uint16_t *const samples =
        arena_alloc(&worker->arena_job, RENDER_LOD_HEIGHT_BLOCKS * sizeof(uint16_t), alignof(uint16_t));
    uint16_t *const cells = arena_alloc_zeroed(
        &worker->arena_job, (uint64_t)sections * SECTION_VOXELS * sizeof(uint16_t), alignof(uint16_t));
    /* Zero is an empty cache, for render_lod_sample_terrain, and whatever else a sample keeps from one line of a column to the next. */
    uint8_t *const sample_scratch =
        arena_alloc_zeroed(&worker->arena_job, RENDER_LOD_SAMPLE_SCRATCH_BYTES, MC_CPU_CACHE_LINE_BYTES);
    uint8_t *overlay_scratch = nullptr;
    if (lod->overlay != nullptr) {
        overlay_scratch = arena_alloc(&worker->arena_job, lod->overlay_scratch_bytes, MC_CPU_CACHE_LINE_BYTES);
        memset(overlay_scratch, 0, MC_CPU_CACHE_LINE_BYTES);
    }
    int32_t top = -1;
    for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
        for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
            /* The voxel's middle, so that what is sampled stands for all of it. */
            const int32_t world_x =
                (job->chunk_x * (int32_t)COLUMN_SIZE + (int32_t)x) * (int32_t)size + (int32_t)(size / 2u);
            const int32_t world_y =
                (job->chunk_y * (int32_t)COLUMN_SIZE + (int32_t)y) * (int32_t)size + (int32_t)(size / 2u);
            lod->sample(lod->borrowed_sample_context, world_x, world_y, shift, samples, sample_scratch);
            if (lod->overlay != nullptr) {
                lod->overlay(lod->borrowed_overlay_context, world_x, world_y, samples, overlay_scratch);
            }
            const int32_t highest = render_lod_cells_from_column(samples, shift, (uint64_t)COLUMN_SIZE * COLUMN_SIZE,
                                                                 cells + section_voxel_index(x, y, 0u));
            top = highest > top ? highest : top;
        }
    }
    uint64_t *const heads = (uint64_t *)(void *)job->borrowed_staging;
    uint8_t *cursor = job->borrowed_staging + RENDER_LOD_STAGING_HEAD_BYTES;
    for (uint32_t section = 0u; section < sections; section++) {
        const render_encode_source_t source = {
            .borrowed_blocks = cells + (uint64_t)section * SECTION_VOXELS,
            .borrowed_light = nullptr,
            .block_uniform = 0u,
            /* All that is ever seen of a coarse voxel is its outside, under an open sky. */
            .light_uniform = (uint8_t)SHADE_LIGHT_OPEN_SKY,
            ._pad0 = {0},
        };
        heads[section] = render_encode_section(&source, cursor);
        cursor += heads[section];
    }
    job->top_voxel = top;
    job->build_ns = time_now_ns() - started_ns;
    PROFILER_END(job_worker_lane(worker));
    /* Release gives the gpu thread what was just written, and pairs with the acquire in render_lod_jobs_collect. */
    atomic_store_explicit(&job->done, 1u, memory_order_release);
}

/* ---- The table ------------------------------------------------------------------------------------------------------- */

static int32_t render_lod_offset_compare(const void *a, const void *b) {
    const int16_t *const left = a;
    const int16_t *const right = b;
    const int32_t left_distance = (int32_t)left[0] * left[0] + (int32_t)left[1] * left[1];
    const int32_t right_distance = (int32_t)right[0] * right[0] + (int32_t)right[1] * right[1];
    if (left_distance != right_distance) {
        return left_distance < right_distance ? -1 : 1;
    }
    /* The same order each run, as qsort promises nothing about ties. */
    if (left[1] != right[1]) {
        return left[1] < right[1] ? -1 : 1;
    }
    return left[0] < right[0] ? -1 : (left[0] > right[0] ? 1 : 0);
}

static int32_t render_lod_floor_mod(int32_t value, uint32_t side) {
    const int32_t remainder = value % (int32_t)side;
    return remainder < 0 ? remainder + (int32_t)side : remainder;
}

static world_buffers_window_t render_lod_window_at(const render_lod_level_t *level, int32_t center_x,
                                                   int32_t center_y) {
    const uint32_t side = level->buffers.window.side;
    const int32_t min_x = center_x - (int32_t)level->radius, min_y = center_y - (int32_t)level->radius;
    return (world_buffers_window_t){
        .min_x = min_x,
        .min_y = min_y,
        .min_slot_x = (uint32_t)render_lod_floor_mod(min_x, side),
        .min_slot_y = (uint32_t)render_lod_floor_mod(min_y, side),
        .side = side,
        .slot_count = side * side,
    };
}

gpu_status_t render_lod_init(render_lod_t *lod, arena_t *arena, device_memory_t *memory, job_system_t *job,
                             const render_lod_plan_t *plan, const device_block_t *tables, const device_block_t *pools,
                             const device_block_t *hits, render_lod_sample_t sample, const void *sample_context) {
    memset(lod, 0, sizeof(*lod));
    job_group_init(&lod->group);
    lod->borrowed_job = job;
    lod->borrowed_hits = hits;
    lod->sample = sample;
    lod->borrowed_sample_context = sample_context;
    lod->pixel_error = RENDER_LOD_PIXEL_ERROR;
    lod->band_dither = RENDER_LOD_BAND_DITHER;
    lod->borrowed_memory = memory;
    lod->turn.side = RENDER_LOD_TURN_CACHE_SIDE;
    lod->borrowed_jobs = arena_alloc_array(arena, RENDER_LOD_JOBS, sizeof(render_lod_job_t), alignof(render_lod_job_t));
    for (uint32_t i = 0u; i < RENDER_LOD_JOBS; i++) {
        render_lod_job_t *const each = &lod->borrowed_jobs[i];
        memset(each, 0, sizeof(*each));
        each->borrowed_lod = lod;
        each->borrowed_staging = arena_alloc(arena, RENDER_LOD_JOB_STAGING_BYTES, MC_CPU_CACHE_LINE_BYTES);
    }
    for (uint32_t i = 0u; i < plan->level_count; i++) {
        render_lod_level_t *const level = &lod->levels[i];
        const uint32_t side = render_lod_side(plan->radii[i]);
        const uint32_t slots = side * side;
        const uint64_t host_bytes = world_buffers_host_bytes(slots, pools[i].bytes);
        uint8_t *const host = arena_alloc(arena, host_bytes, MC_CPU_CACHE_LINE_BYTES);
        /* Where it first looks is worked out by the first frame. */
        const world_buffers_window_t window = {
            .min_x = 0, .min_y = 0, .min_slot_x = 0u, .min_slot_y = 0u, .side = side, .slot_count = slots};
        const gpu_status_t status =
            world_buffers_init(&level->buffers, memory, &pools[i], &tables[i], window, host, host_bytes);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&level->buffers.last_error);
            return status;
        }
        level->shift = plan->shifts[i];
        level->radius = plan->radii[i];
        level->column_count = slots;
        level->top_voxel = -1;
        level->borrowed_state = arena_alloc_zeroed(arena, slots, 1u);
        level->borrowed_offsets = arena_alloc_array(arena, (uint64_t)slots * 2u, sizeof(int16_t), alignof(int16_t));
        uint32_t place = 0u;
        for (int32_t dy = -(int32_t)level->radius; dy <= (int32_t)level->radius; dy++) {
            for (int32_t dx = -(int32_t)level->radius; dx <= (int32_t)level->radius; dx++) {
                level->borrowed_offsets[place * 2u] = (int16_t)dx;
                level->borrowed_offsets[place * 2u + 1u] = (int16_t)dy;
                place++;
            }
        }
        qsort(level->borrowed_offsets, slots, 2u * sizeof(int16_t), render_lod_offset_compare);
        lod->level_count = i + 1u;
    }
    return GPU_STATUS_OK;
}

void render_lod_fine_set(render_lod_t *lod, const world_buffers_t *fine) {
    lod->borrowed_fine = fine;
}

void render_lod_overlay_set(render_lod_t *lod, render_lod_overlay_t overlay, void *borrowed_context,
                            uint64_t scratch_bytes) {
    ASSERT_ALWAYS(lod->jobs_in_use == 0u);
    lod->overlay = overlay;
    lod->borrowed_overlay_context = borrowed_context;
    lod->overlay_scratch_bytes = scratch_bytes;
}

/* One column of one level. What a worker has begun has perhaps read what changed already, so it goes round again when it is done. */
static void render_lod_level_column_changed(render_lod_t *lod, uint32_t index, int32_t column_x, int32_t column_y) {
    render_lod_level_t *const level = &lod->levels[index];
    uint32_t slot = 0u;
    if (level->centered == 0u || !world_buffers_slot_of(&level->buffers.window, column_x, column_y, &slot)) {
        return; /* what comes into the window later reads it as it then is */
    }
    for (uint32_t i = 0u; i < RENDER_LOD_JOBS; i++) {
        render_lod_job_t *const job = &lod->borrowed_jobs[i];
        if (job->in_use != 0u && job->level == index && job->chunk_x == column_x && job->chunk_y == column_y) {
            job->stale = 1u;
        }
    }
    const uint8_t state = level->borrowed_state[slot];
    if (state == RENDER_LOD_COLUMN_BUILT) {
        level->borrowed_state[slot] = RENDER_LOD_COLUMN_STALE;
        level->request_cursor = 0u;
    } else if (state == RENDER_LOD_COLUMN_NO_ROOM) {
        level->borrowed_state[slot] = RENDER_LOD_COLUMN_WANTED;
        level->request_cursor = 0u;
    }
}

void render_lod_column_changed(render_lod_t *lod, int32_t chunk_x, int32_t chunk_y) {
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const uint32_t shift = lod->levels[i].shift;
        render_lod_level_column_changed(lod, i, chunk_x >> shift, chunk_y >> shift);
    }
}

void render_lod_area_changed(render_lod_t *lod, int32_t min_chunk_x, int32_t min_chunk_y, int32_t max_chunk_x,
                             int32_t max_chunk_y) {
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const uint32_t shift = lod->levels[i].shift;
        for (int32_t y = min_chunk_y >> shift; y <= max_chunk_y >> shift; y++) {
            for (int32_t x = min_chunk_x >> shift; x <= max_chunk_x >> shift; x++) {
                render_lod_level_column_changed(lod, i, x, y);
            }
        }
    }
}

void render_lod_all_changed(render_lod_t *lod) {
    job_priority_drop(lod->borrowed_job, JOB_PRIORITY_FAR);
    lod->covered_pass = 0u;
    for (uint32_t i = 0u; i < RENDER_LOD_JOBS; i++) {
        lod->borrowed_jobs[i].stale = lod->borrowed_jobs[i].in_use;
    }
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        render_lod_level_t *const level = &lod->levels[i];
        for (uint32_t slot = 0u; slot < level->column_count; slot++) {
            if (level->borrowed_state[slot] == RENDER_LOD_COLUMN_BUILT) {
                level->borrowed_state[slot] = RENDER_LOD_COLUMN_STALE;
            } else if (level->borrowed_state[slot] == RENDER_LOD_COLUMN_NO_ROOM) {
                level->borrowed_state[slot] = RENDER_LOD_COLUMN_WANTED;
            }
        }
        level->request_cursor = 0u;
    }
}

void render_lod_quiesce(render_lod_t *lod) {
    if (lod->borrowed_job == nullptr) {
        return;
    }
    /* Nothing a job finishes is kept below, so what has not begun returns at once: the wait is then only for what the
     * far field's one capped worker holds, not for every column queued behind it. */
    job_priority_drop(lod->borrowed_job, JOB_PRIORITY_FAR);
    job_group_wait(lod->borrowed_job, &lod->group);
    for (uint32_t i = 0u; i < RENDER_LOD_JOBS; i++) {
        render_lod_job_t *const job = &lod->borrowed_jobs[i];
        if (job->in_use == 0u) {
            continue;
        }
        render_lod_level_t *const level = &lod->levels[job->level];
        uint32_t slot = 0u;
        if (world_buffers_slot_of(&level->buffers.window, job->chunk_x, job->chunk_y, &slot)) {
            const uint8_t state = level->borrowed_state[slot];
            level->borrowed_state[slot] = state == RENDER_LOD_COLUMN_REBUILDING ? RENDER_LOD_COLUMN_STALE
                                          : state == RENDER_LOD_COLUMN_BUILDING ? RENDER_LOD_COLUMN_WANTED
                                                                                : state;
        }
        job->in_use = 0u;
        lod->jobs_in_use--;
    }
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        lod->levels[i].request_cursor = 0u;
    }
}

uint32_t render_lod_level_matching(const render_lod_t *lod, uint32_t shift, uint32_t radius) {
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        if (lod->levels[i].shift == shift && lod->levels[i].radius == radius) {
            return i;
        }
    }
    return UINT32_MAX;
}

gpu_status_t render_lod_level_adopt(render_lod_t *lod, uint32_t index, const render_lod_t *from, uint32_t from_index) {
    render_lod_level_t *const level = &lod->levels[index];
    const render_lod_level_t *const old = &from->levels[from_index];
    ASSERT_ALWAYS(from->jobs_in_use == 0u && level->shift == old->shift && level->radius == old->radius);
    const gpu_status_t status = world_buffers_adopt(&level->buffers, &old->buffers);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&level->buffers.last_error);
        return status;
    }
    memcpy(level->borrowed_state, old->borrowed_state, level->column_count);
    level->center_x = old->center_x;
    level->center_y = old->center_y;
    level->shown_min_x = old->shown_min_x;
    level->shown_min_y = old->shown_min_y;
    level->shown_side = old->shown_side;
    level->generation = old->generation;
    level->top_voxel = old->top_voxel;
    level->centered = old->centered;
    level->request_cursor = 0u;
    level->shown_stale = 1u;
    return GPU_STATUS_OK;
}

void render_lod_deinit(render_lod_t *lod) {
    if (lod->borrowed_job == nullptr) {
        return;
    }
    /* What has not begun returns at once, so that closing waits only for what the capped workers hold. */
    job_priority_drop(lod->borrowed_job, JOB_PRIORITY_FAR);
    job_group_wait(lod->borrowed_job, &lod->group);
    job_group_deinit(&lod->group);
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        world_buffers_deinit(&lod->levels[i].buffers);
    }
    if (lod->borrowed_memory != nullptr &&
        device_memory_free(lod->borrowed_memory, &lod->owned_turn_block) != GPU_STATUS_OK) {
        gpu_error_log(&lod->borrowed_memory->last_error);
    }
    memset(lod, 0, sizeof(*lod));
}

/* Follows the eye. What has left the window goes back to the pool at once, as its slot belongs to what comes in on the other side. A level
 * keeps what it has and only ever asks for what is new, so an eye moving one column costs one row. */
static bool render_lod_level_follow(render_lod_t *lod, uint32_t index, const float64_t position[3]) {
    render_lod_level_t *const level = &lod->levels[index];
    const int32_t center_x = render_lod_column_of(position[0], level->shift);
    const int32_t center_y = render_lod_column_of(position[1], level->shift);
    if (level->centered != 0u && center_x == level->center_x && center_y == level->center_y) {
        return false;
    }
    level->centered = 1u;
    level->center_x = center_x;
    level->center_y = center_y;
    const world_buffers_window_t window = render_lod_window_at(level, center_x, center_y);
    world_buffers_set_window(&level->buffers, window);
    for (uint32_t slot = 0u; slot < level->column_count; slot++) {
        const uint8_t state = level->borrowed_state[slot];
        if (state == RENDER_LOD_COLUMN_WANTED || state == RENDER_LOD_COLUMN_COVERED) {
            continue;
        }
        const world_buffers_column_t *const column = &level->buffers.borrowed_shadow[slot];
        uint32_t still = 0u;
        const bool kept = render_lod_state_built(state) &&
                          world_buffers_slot_of(&window, column->chunk_x, column->chunk_y, &still) && still == slot;
        if (!kept) {
            world_buffers_column_clear(&level->buffers, slot);
            level->borrowed_state[slot] = RENDER_LOD_COLUMN_WANTED;
        }
    }
    /* What is on a worker for a place still in the window is still on its way, whatever its slot held before. */
    for (uint32_t i = 0u; i < RENDER_LOD_JOBS; i++) {
        const render_lod_job_t *const job = &lod->borrowed_jobs[i];
        uint32_t slot = 0u;
        if (job->in_use != 0u && job->level == index &&
            world_buffers_slot_of(&window, job->chunk_x, job->chunk_y, &slot) &&
            (level->borrowed_state[slot] == RENDER_LOD_COLUMN_WANTED ||
             level->borrowed_state[slot] == RENDER_LOD_COLUMN_COVERED)) {
            level->borrowed_state[slot] = RENDER_LOD_COLUMN_BUILDING;
        }
    }
    level->request_cursor = 0u;
    level->shown_stale = 1u;
    lod->covered_pass = 0u;
    return true;
}

/* Whether level 0 holds every chunk a level's column stands on, and the whole column is nearer than any ray of this level starts (plan 009
 * phase 04): its band's start, by the last frame's bands, less the dither. So nothing of it would be seen yet. */
static bool render_lod_covered(const render_lod_t *lod, const render_lod_level_t *level, int32_t column_x,
                               int32_t column_y) {
    const world_buffers_t *const fine = lod->borrowed_fine;
    if (fine == nullptr || fine->borrowed_shadow == nullptr || !(lod->band_base > 0.0f32)) {
        return false;
    }
    const float32_t starts = lod->band_base * (float32_t)(1u << level->shift) * (1.0f32 - lod->band_dither);
    const int32_t away_x = abs(column_x - level->center_x), away_y = abs(column_y - level->center_y);
    /* The column's far corner, with the eye anywhere in its own column. */
    const float32_t far_corner = (float32_t)((away_x > away_y ? away_x : away_y) + 1) *
                                 (float32_t)((uint32_t)COLUMN_SIZE << level->shift) * 1.4143f32;
    if (far_corner >= starts) {
        return false;
    }
    const int32_t chunks = 1 << level->shift;
    for (int32_t dy = 0; dy < chunks; dy++) {
        for (int32_t dx = 0; dx < chunks; dx++) {
            const int32_t chunk_x = column_x * chunks + dx, chunk_y = column_y * chunks + dy;
            uint32_t slot = 0u;
            if (!world_buffers_slot_of(&fine->window, chunk_x, chunk_y, &slot)) {
                return false;
            }
            const world_buffers_column_t *const column = &fine->borrowed_shadow[slot];
            if (column->ready == 0u || column->chunk_x != chunk_x || column->chunk_y != chunk_y) {
                return false;
            }
        }
    }
    return true;
}

/* The widest square around the eye's column with every column built. It stops short of the table by the margin, so that the next column
 * the eye steps towards is there already, and what is shown need not shrink for it. */
static void render_lod_level_show(const render_lod_t *lod, render_lod_level_t *level) {
    if (level->shown_stale == 0u) {
        return;
    }
    level->shown_stale = 0u;
    const world_buffers_window_t *const window = &level->buffers.window;
    const int32_t most = (int32_t)level->radius - (int32_t)RENDER_LOD_MARGIN_COLUMNS;
    int32_t reached = -1;
    for (int32_t ring = 0; ring <= most; ring++) {
        bool whole = true;
        for (int32_t dy = -ring; dy <= ring && whole; dy++) {
            const int32_t step = dy == -ring || dy == ring || ring == 0 ? 1 : 2 * ring;
            for (int32_t dx = -ring; dx <= ring && whole; dx += step) {
                uint32_t slot = 0u;
                /* A column put off as covered counts while it is still covered: a ray of this level does not go there. */
                whole = world_buffers_slot_of(window, level->center_x + dx, level->center_y + dy, &slot) &&
                        (render_lod_state_built(level->borrowed_state[slot]) ||
                         (level->borrowed_state[slot] == RENDER_LOD_COLUMN_COVERED &&
                          render_lod_covered(lod, level, level->center_x + dx, level->center_y + dy)));
            }
        }
        if (!whole) {
            break;
        }
        reached = ring;
    }
    level->shown_side = reached < 0 ? 0u : 2u * (uint32_t)reached + 1u;
    level->shown_min_x = level->center_x - reached;
    level->shown_min_y = level->center_y - reached;
}

/* A finished column into the table and onto the card. */
static gpu_status_t render_lod_column_send(render_lod_t *lod, render_lod_job_t *job, uint64_t *budget_used) {
    render_lod_level_t *const level = &lod->levels[job->level];
    uint32_t slot = 0u;
    if (!world_buffers_slot_of(&level->buffers.window, job->chunk_x, job->chunk_y, &slot) ||
        (level->borrowed_state[slot] != RENDER_LOD_COLUMN_BUILDING &&
         level->borrowed_state[slot] != RENDER_LOD_COLUMN_REBUILDING)) {
        lod->stats.columns_dropped++;
        return GPU_STATUS_OK;
    }
    const uint32_t voxels = RENDER_LOD_HEIGHT_BLOCKS >> level->shift;
    const uint32_t sections = (voxels + SECTION_SIZE - 1u) / SECTION_SIZE;
    level->generation++;
    /* No block at all reads as a top below everything: traverse_column_top makes that 0. */
    const int16_t top_z = (int16_t)(job->top_voxel + COLUMN_Z_STORAGE_MIN);
    world_buffers_column_set(&level->buffers, slot, job->chunk_x, job->chunk_y, level->generation, top_z);
    const uint64_t *const heads = (const uint64_t *)(const void *)job->borrowed_staging;
    const uint8_t *cursor = job->borrowed_staging + RENDER_LOD_STAGING_HEAD_BYTES;
    for (uint32_t section = 0u; section < sections; section++) {
        const uint64_t bytes = heads[section];
        if (bytes == 0u) {
            continue;
        }
        const gpu_status_t written = world_buffers_section_write(&level->buffers, slot, section, cursor, bytes);
        if (written == GPU_STATUS_OVER_BUDGET) {
            world_buffers_column_clear(&level->buffers, slot);
            level->borrowed_state[slot] = RENDER_LOD_COLUMN_NO_ROOM;
            lod->stats.columns_no_room++;
            if (lod->no_room_said == 0u) {
                LOG_WARN("render: a far field level's pool is full, so it stops short of where it should reach");
                lod->no_room_said = 1u;
            }
            return GPU_STATUS_OK;
        }
        if (written != GPU_STATUS_OK) {
            gpu_error_log(&level->buffers.last_error);
            return written;
        }
        cursor += bytes;
        *budget_used += bytes;
        lod->stats.bytes_sent += bytes;
    }
    level->top_voxel = job->top_voxel > level->top_voxel ? job->top_voxel : level->top_voxel;
    level->borrowed_state[slot] = job->stale != 0u ? RENDER_LOD_COLUMN_STALE : RENDER_LOD_COLUMN_BUILT;
    if (job->stale != 0u) {
        level->request_cursor = 0u;
    }
    level->shown_stale = 1u;
    lod->stats.columns_built++;
    return GPU_STATUS_OK;
}

/* A job dropped before it began: its column is wanted again, if it is still in the window, and asked for in its turn. */
static void render_lod_column_unbuilt(render_lod_t *lod, const render_lod_job_t *job) {
    render_lod_level_t *const level = &lod->levels[job->level];
    uint32_t slot = 0u;
    if (world_buffers_slot_of(&level->buffers.window, job->chunk_x, job->chunk_y, &slot)) {
        if (level->borrowed_state[slot] == RENDER_LOD_COLUMN_BUILDING) {
            level->borrowed_state[slot] = RENDER_LOD_COLUMN_WANTED;
        } else if (level->borrowed_state[slot] == RENDER_LOD_COLUMN_REBUILDING) {
            level->borrowed_state[slot] = RENDER_LOD_COLUMN_STALE;
        }
        level->request_cursor = 0u;
    }
    lod->stats.columns_dropped++;
}

/* ---- The pyramid's levels made from octants (plan 009 phase 03) --------------------------------------------------- */

/* Which level has this shift, or UINT32_MAX. */
static uint32_t render_lod_level_of_shift(const render_lod_t *lod, uint32_t shift) {
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        if (lod->levels[i].shift == shift) {
            return i;
        }
    }
    return UINT32_MAX;
}

/* Where the level of this shift is made from: the blocks for shift 1, the level below for the rest, or nullptr if it is not one made from
 * octants, or the level below is not here. */
static const world_buffers_t *render_lod_octant_source(const render_lod_t *lod, uint32_t shift) {
    if (lod->borrowed_octants == nullptr || shift < 1u || shift > RENDER_LOD_OCTANT_LEVELS) {
        return nullptr;
    }
    if (shift == 1u) {
        return lod->borrowed_fine;
    }
    const uint32_t below = render_lod_level_of_shift(lod, shift - 1u);
    return below != UINT32_MAX ? &lod->levels[below].buffers : nullptr;
}

/* Whether a column of this level can be made from octants now, with the slots of the four under it. */
static bool render_lod_octant_ready(const render_lod_t *lod, const render_lod_level_t *level, int32_t column_x,
                                    int32_t column_y, uint32_t slots[4]) {
    const world_buffers_t *const source = render_lod_octant_source(lod, level->shift);
    return source != nullptr &&
           render_octant_column_ready(lod->borrowed_octants, level->shift - 1u, source, column_x, column_y, slots);
}

/* A column of the level of this shift is to be made again, now, whatever its state: the cursor goes back for it. */
static void render_lod_shift_column_changed(render_lod_t *lod, uint32_t shift, int32_t column_x, int32_t column_y) {
    const uint32_t index = render_lod_level_of_shift(lod, shift);
    if (index == UINT32_MAX) {
        return;
    }
    render_lod_level_column_changed(lod, index, column_x, column_y);
    lod->levels[index].request_cursor = 0u;
}

/* Whether a level's column at this slot is one the level above may be made from: made from octants, at the generation it has now. */
static bool render_lod_octant_current(const render_lod_t *lod, const render_lod_level_t *level, uint32_t slot) {
    if (lod->borrowed_octants == nullptr || level->shift >= RENDER_LOD_OCTANT_LEVELS ||
        level->shift >= RENDER_OCTANT_LEVELS_MAX) {
        return false;
    }
    const render_octant_column_t *const own = &lod->borrowed_octants->tables[level->shift].borrowed_columns[slot];
    const world_buffers_column_t *const there = &level->buffers.borrowed_shadow[slot];
    return own->valid != 0u && there->ready != 0u && own->chunk_x == there->chunk_x && own->chunk_y == there->chunk_y &&
           own->generation == there->generation;
}

uint64_t render_lod_octant_arena_bytes(void) {
    const uint64_t voxels = (uint64_t)RENDER_LOD_SECTIONS_MAX * SECTION_VOXELS;
    return voxels * (sizeof(uint16_t) + sizeof(uint8_t)) + RENDER_LOD_JOB_STAGING_BYTES +
           RENDER_LOD_SECTIONS_MAX * sizeof(render_octant_section_t) + 4u * MC_CPU_CACHE_LINE_BYTES;
}

void render_lod_octants_set(render_lod_t *lod, arena_t *arena, render_octant_t *octants) {
    ASSERT_ALWAYS(lod->jobs_in_use == 0u);
    const uint64_t voxels = (uint64_t)RENDER_LOD_SECTIONS_MAX * SECTION_VOXELS;
    lod->borrowed_octants = octants;
    lod->borrowed_octant_cells = arena_alloc_array(arena, voxels, sizeof(uint16_t), MC_CPU_CACHE_LINE_BYTES);
    lod->borrowed_octant_light = arena_alloc(arena, voxels, MC_CPU_CACHE_LINE_BYTES);
    lod->borrowed_octant_staging = arena_alloc(arena, RENDER_LOD_JOB_STAGING_BYTES, MC_CPU_CACHE_LINE_BYTES);
    lod->borrowed_octant_sections = arena_alloc_array(arena, RENDER_LOD_SECTIONS_MAX, sizeof(render_octant_section_t),
                                                      alignof(render_octant_section_t));
    /* Every column is wanted from the start, so what changed before is said already; and levels above 0 begin again. */
    int32_t x = 0, y = 0;
    while (render_octant_changed_next(octants, &x, &y)) {
    }
    (void)render_octant_changed_lost_take(octants);
    render_octant_levels_forget(octants);
}

void render_lod_jobs_hold(render_lod_t *lod, bool held) {
    lod->jobs_held = held ? 1u : 0u;
}

/* The level 0 columns that changed: the level 1 column over each is made again. */
static void render_lod_octant_changes_take(render_lod_t *lod) {
    render_octant_t *const octants = lod->borrowed_octants;
    int32_t chunk_x = 0, chunk_y = 0;
    while (render_octant_changed_next(octants, &chunk_x, &chunk_y)) {
        render_lod_shift_column_changed(lod, 1u, chunk_x >> 1, chunk_y >> 1);
    }
    const uint32_t index = render_lod_level_of_shift(lod, 1u);
    if (render_octant_changed_lost_take(octants) && index != UINT32_MAX) {
        render_lod_level_t *const level = &lod->levels[index];
        for (uint32_t slot = 0u; slot < level->column_count; slot++) {
            const world_buffers_column_t *const column = &level->buffers.borrowed_shadow[slot];
            if (render_lod_state_built(level->borrowed_state[slot]) && column->ready != 0u) {
                render_lod_level_column_changed(lod, index, column->chunk_x, column->chunk_y);
            }
        }
        level->request_cursor = 0u;
    }
}

/* What the watched place is in a column just assembled at this shift, if it is in it. */
static void render_lod_octant_watch(render_lod_t *lod, uint32_t shift, int32_t column_x, int32_t column_y) {
    render_octant_t *const octants = lod->borrowed_octants;
    if (atomic_load_explicit(&octants->watch.armed, memory_order_acquire) == 0u) {
        return;
    }
    const int32_t vx = octants->watch.x >> shift, vy = octants->watch.y >> shift;
    const int32_t vz = (octants->watch.z - COLUMN_Z_STORAGE_MIN) >> shift;
    if ((vx >> 4) != column_x || (vy >> 4) != column_y || vz < 0 ||
        vz >= (int32_t)(RENDER_LOD_HEIGHT_BLOCKS >> shift)) {
        return;
    }
    const uint32_t section = (uint32_t)vz >> 4;
    const render_octant_section_t *const holds = &lod->borrowed_octant_sections[section];
    const uint16_t material =
        holds->assembled != 0u ? lod->borrowed_octant_cells[(uint64_t)section * SECTION_VOXELS +
                                                            section_voxel_index((uint32_t)vx & 15u, (uint32_t)vy & 15u,
                                                                                (uint32_t)vz & 15u)]
                               : holds->block_uniform;
    render_octant_watch_note(octants, shift, material, time_now_ns());
}

/* One column made from the four under it, encoded and sent as a worker's would be, and its own octants kept for the level above. */
static gpu_status_t render_lod_octant_build(render_lod_t *lod, uint32_t index, int32_t column_x, int32_t column_y,
                                            uint32_t slot, const uint32_t slots[4], uint64_t *budget_used) {
    render_lod_level_t *const level = &lod->levels[index];
    render_octant_t *const octants = lod->borrowed_octants;
    const int64_t started_ns = time_now_ns();
    const uint32_t shift = level->shift;
    const uint32_t sections = (RENDER_LOD_HEIGHT_BLOCKS >> shift) / SECTION_SIZE;
    render_octant_section_t *const holds = lod->borrowed_octant_sections;
    uint16_t *const cells = lod->borrowed_octant_cells;
    uint8_t *const light = lod->borrowed_octant_light;
    render_lod_job_t job;
    memset(&job, 0, sizeof(job));
    job.borrowed_lod = lod;
    job.borrowed_staging = lod->borrowed_octant_staging;
    job.level = (uint8_t)index;
    job.top_voxel = render_octant_column_assemble(octants, shift - 1u, slots, cells, light, holds);
    job.chunk_x = column_x;
    job.chunk_y = column_y;
    uint64_t *const heads = (uint64_t *)(void *)job.borrowed_staging;
    uint8_t *cursor = job.borrowed_staging + RENDER_LOD_STAGING_HEAD_BYTES;
    uint64_t bytes = 0u;
    for (uint32_t section = 0u; section < sections; section++) {
        const render_encode_source_t source =
            holds[section].assembled != 0u
                ? (render_encode_source_t){cells + (uint64_t)section * SECTION_VOXELS,
                                           light + (uint64_t)section * SECTION_VOXELS,
                                           0u,
                                           0u,
                                           {0}}
                : (render_encode_source_t){
                      nullptr, nullptr, holds[section].block_uniform, holds[section].light_uniform, {0}};
        heads[section] = render_encode_section(&source, cursor);
        cursor += heads[section];
        bytes += heads[section];
    }
    const uint8_t state = level->borrowed_state[slot];
    level->borrowed_state[slot] =
        state == RENDER_LOD_COLUMN_STALE ? RENDER_LOD_COLUMN_REBUILDING : RENDER_LOD_COLUMN_BUILDING;
    const gpu_status_t status = render_lod_column_send(lod, &job, budget_used);
    if (status != GPU_STATUS_OK || level->borrowed_state[slot] != RENDER_LOD_COLUMN_BUILT) {
        return status;
    }
    octants->stats.columns_assembled[shift - 1u]++;
    octants->stats.bytes_sent += bytes;
    render_lod_octant_watch(lod, shift, job.chunk_x, job.chunk_y);
    if (shift < RENDER_LOD_OCTANT_LEVELS && shift < RENDER_OCTANT_LEVELS_MAX) {
        /* This level is what the next is made from: its own octants, at the generation it was just sent with. */
        render_octant_level_column_set(octants, shift, slot, job.chunk_x, job.chunk_y,
                                       level->buffers.borrowed_shadow[slot].generation);
        render_encode_octant_t octant;
        for (uint32_t section = 0u; section < sections; section++) {
            const render_encode_source_t source =
                holds[section].assembled != 0u
                    ? (render_encode_source_t){cells + (uint64_t)section * SECTION_VOXELS,
                                               light + (uint64_t)section * SECTION_VOXELS,
                                               0u,
                                               0u,
                                               {0}}
                    : (render_encode_source_t){
                          nullptr, nullptr, holds[section].block_uniform, holds[section].light_uniform, {0}};
            if (source.borrowed_blocks == nullptr && source.block_uniform == 0u) {
                continue; /* air, as a column starts */
            }
            (void)render_encode_section_octant(&source, &octant);
            render_octant_section_set(octants, shift, slot, section, &octant);
        }
        render_lod_shift_column_changed(lod, shift + 1u, job.chunk_x >> 1, job.chunk_y >> 1);
    }
    octants->stats.assemble_ns += (uint64_t)(time_now_ns() - started_ns);
    return GPU_STATUS_OK;
}

/* Each level made from octants, finest first so that one frame takes an edit up through all of them: every column it wants, nearest first,
 * that can be made from octants now, up to the frame's count and bytes. */
static gpu_status_t render_lod_octants_request(render_lod_t *lod, uint64_t *budget_used) {
    lod->octant_pending = 0u;
    uint32_t built = 0u;
    for (uint32_t index = 0u; index < lod->level_count; index++) {
        render_lod_level_t *const level = &lod->levels[index];
        if (level->centered == 0u || render_lod_octant_source(lod, level->shift) == nullptr) {
            continue;
        }
        for (uint32_t place = 0u; place < level->column_count; place++) {
            const int16_t *const offset = &level->borrowed_offsets[place * 2u];
            const int32_t column_x = level->center_x + offset[0], column_y = level->center_y + offset[1];
            uint32_t slot = 0u;
            ASSERT_ALWAYS(world_buffers_slot_of(&level->buffers.window, column_x, column_y, &slot));
            const uint8_t state = level->borrowed_state[slot];
            if (state != RENDER_LOD_COLUMN_WANTED && state != RENDER_LOD_COLUMN_COVERED &&
                state != RENDER_LOD_COLUMN_STALE) {
                continue;
            }
            uint32_t slots[4];
            if (!render_lod_octant_ready(lod, level, column_x, column_y, slots)) {
                continue;
            }
            if (built >= RENDER_LOD_OCTANT_COLUMNS_PER_FRAME || *budget_used >= RENDER_LOD_FRAME_BYTES) {
                lod->octant_pending++;
                continue;
            }
            const gpu_status_t status =
                render_lod_octant_build(lod, index, column_x, column_y, slot, slots, budget_used);
            if (status != GPU_STATUS_OK) {
                return status;
            }
            built++;
        }
    }
    return GPU_STATUS_OK;
}

/* For the pyramid, a column a worker made from terrain: if the level above was made from its octants, that is no longer so, and if it could
 * be made from octants now, it is made again from them. */
static void render_lod_terrain_sent(render_lod_t *lod, const render_lod_job_t *job, bool was_current) {
    render_lod_level_t *const level = &lod->levels[job->level];
    uint32_t slot = 0u;
    if (!world_buffers_slot_of(&level->buffers.window, job->chunk_x, job->chunk_y, &slot) ||
        level->borrowed_state[slot] != RENDER_LOD_COLUMN_BUILT) {
        return;
    }
    if (was_current) {
        render_lod_shift_column_changed(lod, level->shift + 1u, job->chunk_x >> 1, job->chunk_y >> 1);
    }
    uint32_t slots[4];
    if (render_lod_octant_ready(lod, level, job->chunk_x, job->chunk_y, slots)) {
        level->borrowed_state[slot] = RENDER_LOD_COLUMN_STALE;
    }
}

static gpu_status_t render_lod_jobs_collect(render_lod_t *lod, uint64_t *budget_used) {
    uint64_t used = 0u;
    for (uint32_t i = 0u; i < RENDER_LOD_JOBS; i++) {
        render_lod_job_t *const job = &lod->borrowed_jobs[i];
        if (job->in_use == 0u || atomic_load_explicit(&job->done, memory_order_acquire) == 0u) {
            continue;
        }
        if (job->dropped != 0u) {
            render_lod_column_unbuilt(lod, job);
            job->in_use = 0u;
            lod->jobs_in_use--;
            continue;
        }
        if (used >= RENDER_LOD_FRAME_BYTES) {
            lod->stats.budget_stops++;
            break;
        }
        bool was_current = false;
        if (lod->borrowed_octants != nullptr) {
            const render_lod_level_t *const level = &lod->levels[job->level];
            uint32_t slot = 0u;
            was_current = world_buffers_slot_of(&level->buffers.window, job->chunk_x, job->chunk_y, &slot) &&
                          render_lod_octant_current(lod, level, slot);
        }
        const gpu_status_t status = render_lod_column_send(lod, job, &used);
        if (status != GPU_STATUS_OK) {
            return status;
        }
        if (lod->borrowed_octants != nullptr) {
            render_lod_terrain_sent(lod, job, was_current);
        }
        lod->stats.build_ns += (uint64_t)job->build_ns;
        job->in_use = 0u;
        lod->jobs_in_use--;
    }
    *budget_used = used;
    return GPU_STATUS_OK;
}

/* Moves a level's cursor past what needs nothing, and returns the slot of the next column it wants, or false if it wants none. On the first
 * pass a covered column is put off, as nothing of it is seen: on the second it is asked for like any other. */
static bool render_lod_level_next(render_lod_t *lod, render_lod_level_t *level, uint32_t *out_slot) {
    while (level->request_cursor < level->column_count) {
        const int16_t *const offset = &level->borrowed_offsets[level->request_cursor * 2u];
        const int32_t column_x = level->center_x + offset[0], column_y = level->center_y + offset[1];
        uint32_t slot = 0u;
        ASSERT_ALWAYS(world_buffers_slot_of(&level->buffers.window, column_x, column_y, &slot));
        const uint8_t state = level->borrowed_state[slot];
        uint32_t slots[4];
        if ((state == RENDER_LOD_COLUMN_STALE || state == RENDER_LOD_COLUMN_WANTED ||
             state == RENDER_LOD_COLUMN_COVERED) &&
            lod->borrowed_octants != nullptr && render_lod_octant_ready(lod, level, column_x, column_y, slots)) {
            /* The pyramid makes this one from octants, on its own pass: no worker for it. */
            level->request_cursor++;
            continue;
        }
        if (state == RENDER_LOD_COLUMN_STALE || (lod->covered_pass != 0u && state == RENDER_LOD_COLUMN_COVERED)) {
            *out_slot = slot;
            return true;
        }
        if (state == RENDER_LOD_COLUMN_WANTED || state == RENDER_LOD_COLUMN_COVERED) {
            if (lod->covered_pass != 0u || !render_lod_covered(lod, level, column_x, column_y)) {
                *out_slot = slot;
                return true;
            }
            level->borrowed_state[slot] = RENDER_LOD_COLUMN_COVERED;
        }
        level->request_cursor++;
    }
    return false;
}

/* Whether any level has a column put off for being covered. */
static bool render_lod_any_covered(const render_lod_t *lod) {
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const render_lod_level_t *const level = &lod->levels[i];
        for (uint32_t slot = 0u; slot < level->column_count; slot++) {
            if (level->borrowed_state[slot] == RENDER_LOD_COLUMN_COVERED) {
                return true;
            }
        }
    }
    return false;
}

/* The coarsest level first, the nearest column first in it: the coarsest is what reaches the horizon, for the least work per block, and it
 * stands round the eye too, so a new world or a new render distance shows the whole of it in well under a second, coarse, and finer levels
 * sharpen what is already there. A covered column waits until all the rest are asked for. */
static void render_lod_jobs_request(render_lod_t *lod) {
    if (lod->jobs_held != 0u) {
        return;
    }
    uint32_t free_job = 0u;
    for (;;) {
        uint32_t best = UINT32_MAX, best_slot = 0u;
        for (uint32_t index = lod->level_count; index-- > 0u;) {
            render_lod_level_t *const level = &lod->levels[index];
            uint32_t slot = 0u;
            if (!render_lod_level_next(lod, level, &slot)) {
                continue;
            }
            best = index;
            best_slot = slot;
            break; /* levels go from the coarsest */
        }
        if (best == UINT32_MAX && lod->covered_pass == 0u && render_lod_any_covered(lod)) {
            /* All that shows is asked for: now what is covered, which a move or a change of level 0 may uncover. */
            lod->covered_pass = 1u;
            for (uint32_t i = 0u; i < lod->level_count; i++) {
                lod->levels[i].request_cursor = 0u;
            }
            continue;
        }
        if (best == UINT32_MAX) {
            return;
        }
        while (free_job < RENDER_LOD_JOBS && lod->borrowed_jobs[free_job].in_use != 0u) {
            free_job++;
        }
        if (free_job == RENDER_LOD_JOBS) {
            return;
        }
        render_lod_level_t *const level = &lod->levels[best];
        const int16_t *const offset = &level->borrowed_offsets[level->request_cursor * 2u];
        render_lod_job_t *const job = &lod->borrowed_jobs[free_job];
        job->chunk_x = level->center_x + offset[0];
        job->chunk_y = level->center_y + offset[1];
        job->level = (uint8_t)best;
        job->top_voxel = -1;
        job->stale = 0u;
        job->dropped = 0u;
        atomic_store_explicit(&job->done, 0u, memory_order_relaxed);
        if (!job_try_submit_at(lod->borrowed_job, &lod->group, JOB_PRIORITY_FAR, render_lod_job_run, job)) {
            return; /* the shared queue is full: it is offered again next frame */
        }
        job->in_use = 1u;
        lod->jobs_in_use++;
        const uint8_t state = level->borrowed_state[best_slot];
        level->borrowed_state[best_slot] =
            state == RENDER_LOD_COLUMN_STALE ? RENDER_LOD_COLUMN_REBUILDING : RENDER_LOD_COLUMN_BUILDING;
        level->request_cursor++;
    }
}

/* Plan 008 phase 06: the revision moves on at once when any level's square shown has moved, and when anything has been put in a level's table
 * since it last moved on, but then at most once every RENDER_LOD_TURN_REVISE_FRAMES frames: at Ahmed's spot at render distance 64 the pyramid
 * sends about 3 columns a frame for a long while after it has filled (levels made from the blocks taking over from terrain, 8 a frame), which
 * would end the turn cache's generation every frame. So a far column sent shows in what a turn reads from the cache that many frames late at
 * most, and at once in what is traced. */
static void render_lod_revise(render_lod_t *lod) {
    uint64_t shown = 0xCBF29CE484222325ull, contents = 0xCBF29CE484222325ull;
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const render_lod_level_t *const level = &lod->levels[i];
        const uint32_t words[3] = {(uint32_t)level->shown_min_x, (uint32_t)level->shown_min_y, level->shown_side};
        for (uint32_t w = 0u; w < 3u; w++) {
            shown = (shown ^ words[w]) * 0x100000001B3ull;
        }
        contents = (contents ^ level->generation) * 0x100000001B3ull;
    }
    lod->revision_wait = lod->revision_wait < UINT32_MAX ? lod->revision_wait + 1u : UINT32_MAX;
    if (shown != lod->shown_key ||
        (contents != lod->revision_key && lod->revision_wait >= (uint32_t)RENDER_LOD_TURN_REVISE_FRAMES)) {
        lod->shown_key = shown;
        lod->revision_key = contents;
        lod->revision++;
        lod->revision_wait = 0u;
    }
}

/* Plan 008 phase 06: the turn cache's block, the first frame it is wanted, so that a far field replanned never holds two. If the voxels' pool
 * has no room, the far field goes on without one, and says so once. */
static void render_lod_turn_take(render_lod_t *lod) {
    if (lod->turn.side == 0u || lod->owned_turn_block.owned_address != 0u || lod->turn_given_up != 0u ||
        lod->borrowed_memory == nullptr) {
        return;
    }
    const uint64_t bytes = lod_turn_cache_bytes(lod->turn.side);
    if (device_memory_alloc(lod->borrowed_memory, DEVICE_POOL_VOXELS, bytes, "far turn cache",
                            &lod->owned_turn_block) != GPU_STATUS_OK) {
        LOG_WARN("render: no room for the far field's turn cache (%" PRIu64 " MiB), so turning traces every far ray",
                 bytes >> 20);
        lod->turn_given_up = 1u;
        return;
    }
    lod->turn.borrowed_block = &lod->owned_turn_block;
    lod->turn.cleared = 0u;
    LOG_INFO("render: the far field's turn cache, %" PRIu32 " texels a face, %" PRIu64 " MiB", lod->turn.side,
             bytes >> 20);
}

gpu_status_t render_lod_turn_tally(render_lod_t *lod, lod_turn_tally_t *out) {
    memset(out, 0, sizeof(*out));
    if (lod->turn.borrowed_block == nullptr || lod->turn.cleared == 0u) {
        return GPU_STATUS_OK;
    }
    const uint64_t at = lod_turn_cache_bytes(lod->turn.side) - sizeof(lod_turn_tally_t);
    const gpu_status_t status =
        device_memory_read(lod->borrowed_memory, lod->turn.borrowed_block, at, out, sizeof(*out));
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&lod->borrowed_memory->last_error);
    }
    return status;
}

gpu_status_t render_lod_frame(render_lod_t *lod, const float64_t position[3]) {
    bool moved = false;
    /* The levels' copies go through the frame's staging, levels made since included. */
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        lod->levels[i].buffers.borrowed_staging = lod->borrowed_staging;
    }
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        moved = render_lod_level_follow(lod, i, position) || moved;
    }
    /* What is queued was asked for nearest the old place. Dropped, it is asked for again below, nearest the new one, and
     * whatever has left the window is never made. What workers have begun goes on. */
    if (moved) {
        job_priority_drop(lod->borrowed_job, JOB_PRIORITY_FAR);
    }
    if (lod->borrowed_octants != nullptr) {
        render_lod_octant_changes_take(lod);
    }
    uint64_t used = 0u;
    gpu_status_t status = render_lod_jobs_collect(lod, &used);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    if (lod->borrowed_octants != nullptr) {
        status = render_lod_octants_request(lod, &used);
        if (status != GPU_STATUS_OK) {
            return status;
        }
    }
    render_lod_jobs_request(lod);
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        render_lod_level_show(lod, &lod->levels[i]);
        status = world_buffers_flush(&lod->levels[i].buffers);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&lod->levels[i].buffers.last_error);
            return status;
        }
    }
    render_lod_revise(lod);
    render_lod_turn_take(lod);
    return GPU_STATUS_OK;
}

bool render_lod_idle(const render_lod_t *lod) {
    if (lod->jobs_in_use != 0u || lod->octant_pending != 0u) {
        return false;
    }
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const render_lod_level_t *const level = &lod->levels[i];
        if (level->centered == 0u || level->request_cursor < level->column_count) {
            return false;
        }
    }
    return true;
}

void render_lod_scene(render_lod_t *lod, lod_scene_t *out) {
    memset(out, 0, sizeof(*out));
    out->borrowed_hits = lod->borrowed_hits;
    out->level_count = lod->level_count;
    out->borrowed_turn = lod->turn.borrowed_block != nullptr ? &lod->turn : nullptr;
    out->revision = lod->revision;
    int32_t top = 0;
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const render_lod_level_t *const level = &lod->levels[i];
        out->levels[i] = (lod_level_t){
            .borrowed_table = level->buffers.borrowed_table,
            .borrowed_pool = level->buffers.borrowed_pool,
            .window = level->buffers.window,
            .shown_min_x = level->shown_min_x,
            .shown_min_y = level->shown_min_y,
            .shown_side = level->shown_side,
            .shift = level->shift,
        };
        const int32_t above = (level->top_voxel + 1) * (1 << level->shift);
        top = above > top ? above : top;
    }
    out->top_storage = top;
}
