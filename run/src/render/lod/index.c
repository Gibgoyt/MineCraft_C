/* See README.md. The coarse grids: each column of each level made on a worker from samples of the ground, and sent to the card in the
 * world's own format, in a table that scrolls with the eye. */
#include "render/lod/index.h"

#include "assert/index.h"
#include "gpu_bridge/shade/index.h"
#include "log/index.h"
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

/* How far from the eye a level's rays are sure to reach, in blocks. */
static uint32_t render_lod_reach_blocks(uint32_t shift, uint32_t radius) {
    return ((radius - RENDER_LOD_MARGIN_COLUMNS) * (uint32_t)COLUMN_SIZE) << shift;
}

render_lod_plan_t render_lod_plan(uint32_t full_detail_chunks, uint32_t render_distance_chunks) {
    render_lod_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    if (render_distance_chunks <= full_detail_chunks) {
        return plan;
    }
    const uint32_t full_blocks = full_detail_chunks * (uint32_t)COLUMN_SIZE;
    const uint32_t far_blocks = render_distance_chunks * (uint32_t)COLUMN_SIZE;
    for (uint32_t shift = LOD_SHIFT_MIN; shift < LOD_SHIFT_MIN + LOD_LEVELS_MAX; shift++) {
        if (render_lod_reach_blocks(shift, RENDER_LOD_RADIUS_COLUMNS) <= full_blocks) {
            continue; /* it would lie wholly under what is drawn block by block */
        }
        /* Only as wide as the distance asks, which is most of what the coarsest level would otherwise cost. */
        const uint32_t wanted = (far_blocks >> (4u + shift)) + 1u + RENDER_LOD_MARGIN_COLUMNS;
        const uint32_t radius = wanted < RENDER_LOD_RADIUS_COLUMNS ? wanted : RENDER_LOD_RADIUS_COLUMNS;
        plan.shifts[plan.level_count] = (uint8_t)shift;
        plan.radii[plan.level_count] = (uint8_t)radius;
        plan.level_count++;
        if (render_lod_reach_blocks(shift, radius) >= far_blocks) {
            break;
        }
    }
    return plan;
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

static bool render_lod_counts(uint16_t block) {
    if (block == BLOCK_ID_AIR) {
        return false;
    }
    switch ((block_shape_t)block_defs[block].shape) {
    case BLOCK_SHAPE_CUBE:
    case BLOCK_SHAPE_FLUID:
    case BLOCK_SHAPE_SLAB:
    case BLOCK_SHAPE_STAIRS:
        return true;
    case BLOCK_SHAPE_NONE:
    case BLOCK_SHAPE_CROSS:
    case BLOCK_SHAPE_CROP:
    case BLOCK_SHAPE_TORCH:
    case BLOCK_SHAPE_THIN:
    case BLOCK_SHAPE_TORCH_EAST:
    case BLOCK_SHAPE_TORCH_WEST:
    case BLOCK_SHAPE_TORCH_NORTH:
    case BLOCK_SHAPE_TORCH_SOUTH:
        break;
    }
    return false;
}

/* What feature_cold_paint does to water, for a level too coarse for features: a column's own biome, and only if its top is water. */
static void render_lod_sample_freeze(const terrain_t *terrain, int32_t x, int32_t y, uint16_t *column) {
    uint32_t top = RENDER_LOD_HEIGHT_BLOCKS;
    while (top > 0u && column[top - 1u] == BLOCK_ID_AIR) {
        top--;
    }
    if (top == 0u || column[top - 1u] != BLOCK_ID_WATER) {
        return;
    }
    const uint8_t biome = terrain_biome_column_at(terrain->borrowed_biome, x, y).biome;
    if (biome_features[biome][BIOME_FEATURE_ICE] > 0.0f32) {
        column[top - 1u] = BLOCK_ID_ICE;
    }
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
            if (render_lod_counts(state)) {
                count++;
                highest = state;
            }
        }
        const uint16_t kept = count * 2u >= size ? highest : (uint16_t)BLOCK_ID_AIR;
        out[voxel * stride] = kept;
        /* Ice is one block thick, so the half rule takes it: a frozen sea would read as open water until full detail drew it. The ice shows on the water it lay on. */
        if (kept == BLOCK_ID_AIR && holds_ice && voxel > 0u && out[(voxel - 1u) * stride] == BLOCK_ID_WATER) {
            out[(voxel - 1u) * stride] = BLOCK_ID_ICE;
        }
        if (kept != BLOCK_ID_AIR) {
            top = (int32_t)voxel;
        }
    }
    return top;
}

void render_lod_sample_terrain(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out) {
    const terrain_t *const terrain = borrowed_context;
    const terrain_density_box_t box = {
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = 1u,
        .count_y = 1u,
        .count_z = RENDER_LOD_HEIGHT_BLOCKS,
    };
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, &box, out);
    terrain_surface_fill_box(terrain->borrowed_surface, &box, out);
    if (shift <= RENDER_LOD_FEATURES_SHIFT_MAX) {
        terrain_feature_fill_box(terrain->borrowed_feature, &box, out);
    } else {
        render_lod_sample_freeze(terrain, x, y, out);
    }
}

static void render_lod_job_run(job_worker_t *worker, void *borrowed_job) {
    render_lod_job_t *const job = borrowed_job;
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
            lod->sample(lod->borrowed_sample_context, world_x, world_y, shift, samples);
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

void render_lod_all_changed(render_lod_t *lod) {
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

void render_lod_deinit(render_lod_t *lod) {
    if (lod->borrowed_job == nullptr) {
        return;
    }
    job_group_wait(lod->borrowed_job, &lod->group);
    job_group_deinit(&lod->group);
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        world_buffers_deinit(&lod->levels[i].buffers);
    }
    memset(lod, 0, sizeof(*lod));
}

/* Follows the eye. What has left the window goes back to the pool at once, as its slot belongs to what comes in on the other side. A level
 * keeps what it has and only ever asks for what is new, so an eye moving one column costs one row. */
static void render_lod_level_follow(render_lod_t *lod, uint32_t index, const float64_t position[3]) {
    render_lod_level_t *const level = &lod->levels[index];
    const int32_t center_x = render_lod_column_of(position[0], level->shift);
    const int32_t center_y = render_lod_column_of(position[1], level->shift);
    if (level->centered != 0u && center_x == level->center_x && center_y == level->center_y) {
        return;
    }
    level->centered = 1u;
    level->center_x = center_x;
    level->center_y = center_y;
    const world_buffers_window_t window = render_lod_window_at(level, center_x, center_y);
    world_buffers_set_window(&level->buffers, window);
    for (uint32_t slot = 0u; slot < level->column_count; slot++) {
        const uint8_t state = level->borrowed_state[slot];
        if (state == RENDER_LOD_COLUMN_WANTED) {
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
            level->borrowed_state[slot] == RENDER_LOD_COLUMN_WANTED) {
            level->borrowed_state[slot] = RENDER_LOD_COLUMN_BUILDING;
        }
    }
    level->request_cursor = 0u;
    level->shown_stale = 1u;
}

/* The widest square around the eye's column with every column built. It stops short of the table by the margin, so that the next column
 * the eye steps towards is there already, and what is shown need not shrink for it. */
static void render_lod_level_show(render_lod_level_t *level) {
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
                whole = world_buffers_slot_of(window, level->center_x + dx, level->center_y + dy, &slot) &&
                        render_lod_state_built(level->borrowed_state[slot]);
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

static gpu_status_t render_lod_jobs_collect(render_lod_t *lod) {
    uint64_t used = 0u;
    for (uint32_t i = 0u; i < RENDER_LOD_JOBS; i++) {
        render_lod_job_t *const job = &lod->borrowed_jobs[i];
        if (job->in_use == 0u || atomic_load_explicit(&job->done, memory_order_acquire) == 0u) {
            continue;
        }
        if (used >= RENDER_LOD_FRAME_BYTES) {
            lod->stats.budget_stops++;
            break;
        }
        const gpu_status_t status = render_lod_column_send(lod, job, &used);
        if (status != GPU_STATUS_OK) {
            return status;
        }
        lod->stats.build_ns += (uint64_t)job->build_ns;
        job->in_use = 0u;
        lod->jobs_in_use--;
    }
    return GPU_STATUS_OK;
}

/* The coarsest level first: it is what reaches the horizon, for the least work per block of it. */
static void render_lod_jobs_request(render_lod_t *lod) {
    uint32_t free_job = 0u;
    for (uint32_t index = lod->level_count; index-- > 0u;) {
        render_lod_level_t *const level = &lod->levels[index];
        while (level->request_cursor < level->column_count) {
            const int16_t *const offset = &level->borrowed_offsets[level->request_cursor * 2u];
            const int32_t chunk_x = level->center_x + offset[0], chunk_y = level->center_y + offset[1];
            uint32_t slot = 0u;
            ASSERT_ALWAYS(world_buffers_slot_of(&level->buffers.window, chunk_x, chunk_y, &slot));
            const uint8_t state = level->borrowed_state[slot];
            if (state != RENDER_LOD_COLUMN_WANTED && state != RENDER_LOD_COLUMN_STALE) {
                level->request_cursor++;
                continue;
            }
            while (free_job < RENDER_LOD_JOBS && lod->borrowed_jobs[free_job].in_use != 0u) {
                free_job++;
            }
            if (free_job == RENDER_LOD_JOBS) {
                return;
            }
            render_lod_job_t *const job = &lod->borrowed_jobs[free_job];
            job->chunk_x = chunk_x;
            job->chunk_y = chunk_y;
            job->level = (uint8_t)index;
            job->top_voxel = -1;
            job->stale = 0u;
            atomic_store_explicit(&job->done, 0u, memory_order_relaxed);
            if (!job_try_submit(lod->borrowed_job, &lod->group, render_lod_job_run, job)) {
                return; /* the shared queue is full: it is offered again next frame */
            }
            job->in_use = 1u;
            lod->jobs_in_use++;
            level->borrowed_state[slot] =
                state == RENDER_LOD_COLUMN_STALE ? RENDER_LOD_COLUMN_REBUILDING : RENDER_LOD_COLUMN_BUILDING;
            level->request_cursor++;
        }
    }
}

gpu_status_t render_lod_frame(render_lod_t *lod, const float64_t position[3]) {
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        render_lod_level_follow(lod, i, position);
    }
    gpu_status_t status = render_lod_jobs_collect(lod);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    render_lod_jobs_request(lod);
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        render_lod_level_show(&lod->levels[i]);
        status = world_buffers_flush(&lod->levels[i].buffers);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&lod->levels[i].buffers.last_error);
            return status;
        }
    }
    return GPU_STATUS_OK;
}

bool render_lod_idle(const render_lod_t *lod) {
    if (lod->jobs_in_use != 0u) {
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

void render_lod_scene(const render_lod_t *lod, lod_scene_t *out) {
    memset(out, 0, sizeof(*out));
    out->borrowed_hits = lod->borrowed_hits;
    out->level_count = lod->level_count;
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
