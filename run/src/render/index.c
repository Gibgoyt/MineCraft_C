/* See README.md. The Owner: it holds the host region and the blocks on the card, and lends both to its parts. */
#include "render/index.h"

#include "assert/index.h"
#include "log/index.h"

#include <inttypes.h>
#include <string.h>

render_limits_t render_limits_default(void) {
    return (render_limits_t){
        .pool_bytes = RENDER_POOL_BYTES_DEFAULT,
        .column_count = RENDER_COLUMN_COUNT_DEFAULT,
        .section_count = WORLD_EDIT_SNAPSHOT_SECTIONS_DEFAULT,
        ._pad0 = {0},
    };
}

/* Each arena allocation can waste its alignment. */
static uint64_t render_host_bytes(uint64_t column_count, uint64_t pool_bytes, uint32_t section_count) {
    return world_buffers_host_bytes((uint32_t)column_count, pool_bytes) + MC_CPU_CACHE_LINE_BYTES +
           render_upload_arena_bytes(section_count);
}

bool render_init(render_t *render, device_memory_t *memory, job_system_t *job, render_limits_t limits) {
    memset(render, 0, sizeof(*render));
    if (limits.pool_bytes == 0u || limits.pool_bytes % WORLD_BUFFERS_PAGE_BYTES != 0u) {
        LOG_ERROR("render: a pool of %" PRIu64 " bytes is not a whole number of %" PRIu32 "-byte pages",
                  limits.pool_bytes, (uint32_t)WORLD_BUFFERS_PAGE_BYTES);
        return false;
    }
    if (limits.column_count == 0u || limits.column_count > UINT32_MAX || limits.section_count == 0u) {
        LOG_ERROR("render: limits of %" PRIu64 " columns and %" PRIu32 " sections a view are not usable",
                  limits.column_count, limits.section_count);
        return false;
    }
    if (!heap_reserve(render_host_bytes(limits.column_count, limits.pool_bytes, limits.section_count),
                      &render->owned_host)) {
        return false; /* it has said why */
    }
    arena_init(&render->arena_host, "render_host", render->owned_host.owned_base, render->owned_host.bytes);
    render->borrowed_memory = memory;
    render->borrowed_job = job;
    render->limits = limits;
    return true;
}

void render_deinit(render_t *render) {
    render_close(render);
    arena_deinit(&render->arena_host);
    heap_release(&render->owned_host);
    memset(render, 0, sizeof(*render));
}

uint64_t render_pool_bytes_for(const render_t *render, uint32_t slot_count) {
    /* On top of the floor, and not up to it: each size of slab holds a page of its own however little of it is in use, so a
     * small table with the floor alone ran out: Stage 08.4 issue 3. */
    const uint64_t near = slot_count < RENDER_POOL_NEAR_COLUMNS ? slot_count : RENDER_POOL_NEAR_COLUMNS;
    uint64_t bytes = near * RENDER_POOL_BYTES_PER_NEAR_COLUMN +
                     ((uint64_t)slot_count - near) * RENDER_POOL_BYTES_PER_COLUMN + RENDER_POOL_BYTES_MIN;
    bytes = bytes > render->limits.pool_bytes ? render->limits.pool_bytes : bytes;
    return bytes / WORLD_BUFFERS_PAGE_BYTES * WORLD_BUFFERS_PAGE_BYTES;
}

/* Takes the blocks and starts world_buffers on them, saying why if it cannot. What it took, render_open gives back. */
static gpu_status_t render_open_buffers(render_t *render, const world_map_t *map, uint64_t pool_bytes) {
    device_memory_t *const memory = render->borrowed_memory;
    gpu_status_t status = device_memory_alloc(memory, DEVICE_POOL_VOXELS, world_buffers_table_bytes(map->slot_count),
                                              "column table", &render->owned_table);
    if (status == GPU_STATUS_OK) {
        status = device_memory_alloc(memory, DEVICE_POOL_VOXELS, pool_bytes, "section pool", &render->owned_pool);
    }
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        return status;
    }
    const world_buffers_window_t window = {
        .min_x = map->min_x,
        .min_y = map->min_y,
        .min_slot_x = map->min_slot_x,
        .min_slot_y = map->min_slot_y,
        .side = map->side,
        .slot_count = map->slot_count,
    };
    const uint64_t host_bytes = world_buffers_host_bytes(map->slot_count, pool_bytes);
    uint8_t *const host = arena_alloc(&render->arena_host, host_bytes, MC_CPU_CACHE_LINE_BYTES);
    status = world_buffers_init(&render->buffers, memory, &render->owned_pool, &render->owned_table, window, host,
                                host_bytes);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&render->buffers.last_error);
    }
    return status;
}

gpu_status_t render_open(render_t *render, world_edit_snapshot_t *snapshot, uint64_t pool_bytes,
                         render_upload_budget_t budget) {
    ASSERT_ALWAYS(render->open == 0u);
    const world_map_t *const map = snapshot->borrowed_edit->borrowed_map;
    if (map->slot_count > render->limits.column_count || pool_bytes > render->limits.pool_bytes ||
        snapshot->section_capacity > render->limits.section_count) {
        LOG_ERROR("render: a table of %" PRIu32 " slots, a pool of %" PRIu64 " bytes and %" PRIu32
                  " sections a view are past the limits given to render_init",
                  map->slot_count, pool_bytes, snapshot->section_capacity);
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    const gpu_status_t status = render_open_buffers(render, map, pool_bytes);
    if (status != GPU_STATUS_OK) {
        world_buffers_deinit(&render->buffers);
        arena_reset(&render->arena_host);
        /* Each is a block this holds or an empty handle, so neither can refuse, and the failure that matters is reported. */
        (void)device_memory_free(render->borrowed_memory, &render->owned_pool);
        (void)device_memory_free(render->borrowed_memory, &render->owned_table);
        return status;
    }
    render_upload_init(&render->upload, &render->arena_host, snapshot, render->borrowed_job, &render->buffers,
                       snapshot->section_capacity, budget);
    render->open = 1u;
    return GPU_STATUS_OK;
}

void render_close(render_t *render) {
    render_close_frame(render);
    if (render->open == 0u) {
        return;
    }
    render_upload_deinit(&render->upload);
    world_buffers_deinit(&render->buffers);
    arena_reset(&render->arena_host);
    device_memory_t *const memory = render->borrowed_memory;
    if (device_memory_free(memory, &render->owned_pool) != GPU_STATUS_OK ||
        device_memory_free(memory, &render->owned_table) != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
    }
    render->open = 0u;
}

typedef struct render_block_wanted_t {
    device_block_t *borrowed_block; /* 0 */
    const char *borrowed_name;      /* 8 */
    uint64_t bytes;                 /* 16 */
    device_pool_t pool;             /* 24 */
    uint8_t _pad0[4];               /* 28 */
} render_block_wanted_t;
static_assert(sizeof(render_block_wanted_t) == 32);

/* Takes every block a frame needs, saying why if it cannot. What it took, render_close_frame gives back. */
static gpu_status_t render_frame_blocks_take(render_t *render, const texture_t *texture, uint32_t width,
                                             uint32_t height) {
    device_memory_t *const memory = render->borrowed_memory;
    const uint8_t *pixels = nullptr;
    uint64_t pixel_bytes = 0u;
    texture_pixels_get(texture, &pixels, &pixel_bytes);
    const render_block_wanted_t wanted[] = {
        {&render->owned_textures, "texture table", render_frame_texture_table_bytes(), DEVICE_POOL_TEXTURES, {0}},
        {&render->owned_faces, "face table", render_frame_face_table_bytes(), DEVICE_POOL_TEXTURES, {0}},
        {&render->owned_shapes, "shape table", render_frame_shape_table_bytes(), DEVICE_POOL_TEXTURES, {0}},
        {&render->owned_tex_pixels, "texture pixels", pixel_bytes, DEVICE_POOL_TEXTURES, {0}},
        {&render->owned_coarse, "coarse depth", traverse_coarse_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_hits, "hits", traverse_hits_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_shaded, "shaded pixels", shade_pixels_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_xrgb, "finished picture", post_xrgb_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_ao[0], "AO history", shade_ao_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_ao[1], "AO history", shade_ao_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_history[0], "picture history", post_history_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_history[1], "picture history", post_history_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_shafts, "god rays", post_shafts_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_entity_lists,
         "entity lists",
         render_entities_lists_bytes(width, height),
         DEVICE_POOL_FRAMES,
         {0}},
        {&render->owned_entity_hits, "entity hits", entity_rays_hits_bytes(width, height), DEVICE_POOL_FRAMES, {0}},
        {&render->owned_weather_map, "rain top", post_weather_map_bytes(), DEVICE_POOL_FRAMES, {0}},
    };
    for (uint32_t i = 0u; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        const gpu_status_t status = device_memory_alloc(memory, wanted[i].pool, wanted[i].bytes,
                                                        wanted[i].borrowed_name, wanted[i].borrowed_block);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&memory->last_error);
            return status;
        }
    }
    return GPU_STATUS_OK;
}

static void render_frame_blocks_give_back(render_t *render) {
    device_memory_t *const memory = render->borrowed_memory;
    device_block_t *const blocks[] = {
        &render->owned_weather_map, &render->owned_entity_hits, &render->owned_entity_lists, &render->owned_shafts,
        &render->owned_history[1],  &render->owned_history[0],  &render->owned_ao[1],        &render->owned_ao[0],
        &render->owned_xrgb,        &render->owned_shaded,      &render->owned_hits,         &render->owned_coarse,
        &render->owned_tex_pixels,  &render->owned_shapes,      &render->owned_faces,        &render->owned_textures};
    for (uint32_t i = 0u; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        if (device_memory_free(memory, blocks[i]) != GPU_STATUS_OK) {
            gpu_error_log(&memory->last_error);
        }
    }
}

gpu_status_t render_open_frame(render_t *render, const texture_t *texture, uint32_t width, uint32_t height,
                               float32_t fog_end) {
    ASSERT_ALWAYS(render->frame_open == 0u);
    gpu_status_t status = render_frame_blocks_take(render, texture, width, height);
    if (status != GPU_STATUS_OK) {
        render_frame_blocks_give_back(render);
        return status;
    }
    if (!heap_reserve(render_frame_arena_bytes(width, height) + render_entities_arena_bytes(width, height),
                      &render->owned_frame_host)) {
        render_frame_blocks_give_back(render);
        return GPU_STATUS_OUT_OF_MEMORY; /* it has said why */
    }
    arena_init(&render->arena_frame, "render_frame", render->owned_frame_host.owned_base,
               render->owned_frame_host.bytes);
    const render_frame_blocks_t blocks = {
        .borrowed_textures = &render->owned_textures,
        .borrowed_faces = &render->owned_faces,
        .borrowed_pixels = &render->owned_tex_pixels,
        .borrowed_coarse = &render->owned_coarse,
        .borrowed_hits = &render->owned_hits,
        .borrowed_shaded = &render->owned_shaded,
        .borrowed_xrgb = &render->owned_xrgb,
        .borrowed_shapes = &render->owned_shapes,
        .borrowed_ao = {&render->owned_ao[0], &render->owned_ao[1]},
        .borrowed_history = {&render->owned_history[0], &render->owned_history[1]},
        .borrowed_shafts = &render->owned_shafts,
    };
    status = render_frame_init(&render->frame, &render->arena_frame, render->borrowed_memory, texture, &blocks, width,
                               height, fog_end);
    if (status != GPU_STATUS_OK) {
        arena_deinit(&render->arena_frame);
        heap_release(&render->owned_frame_host);
        render_frame_blocks_give_back(render);
        return status;
    }
    render->borrowed_entities =
        arena_alloc(&render->arena_frame, sizeof(render_entities_t), alignof(render_entities_t));
    status = render_entities_init(render->borrowed_entities, &render->arena_frame, render->borrowed_memory, texture,
                                  &render->owned_entity_lists, &render->owned_entity_hits, width, height);
    if (status != GPU_STATUS_OK) {
        render_entities_deinit(render->borrowed_entities);
        render_frame_deinit(&render->frame);
        arena_deinit(&render->arena_frame);
        heap_release(&render->owned_frame_host);
        render_frame_blocks_give_back(render);
        render->borrowed_entities = nullptr;
        return status;
    }
    render->frame.borrowed_entities = render->borrowed_entities;
    render->frame_open = 1u;
    return GPU_STATUS_OK;
}

static void render_lod_blocks_give_back(render_t *render) {
    device_memory_t *const memory = render->borrowed_memory;
    if (device_memory_free(memory, &render->owned_lod_hits) != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
    }
    for (uint32_t i = LOD_LEVELS_MAX; i-- > 0u;) {
        if (device_memory_free(memory, &render->owned_lod_pools[i]) != GPU_STATUS_OK ||
            device_memory_free(memory, &render->owned_lod_tables[i]) != GPU_STATUS_OK) {
            gpu_error_log(&memory->last_error);
        }
    }
}

gpu_status_t render_open_lod(render_t *render, const render_lod_plan_t *plan, render_lod_sample_t sample,
                             const void *sample_context) {
    ASSERT_ALWAYS(render->frame_open != 0u && render->borrowed_lod == nullptr);
    device_memory_t *const memory = render->borrowed_memory;
    gpu_status_t status =
        device_memory_alloc(memory, DEVICE_POOL_FRAMES, lod_hits_bytes(render->frame.width, render->frame.height),
                            "far hits", &render->owned_lod_hits);
    for (uint32_t i = 0u; i < plan->level_count && status == GPU_STATUS_OK; i++) {
        status = device_memory_alloc(memory, DEVICE_POOL_VOXELS, render_lod_table_bytes(plan, i), "far column table",
                                     &render->owned_lod_tables[i]);
        if (status == GPU_STATUS_OK) {
            status = device_memory_alloc(memory, DEVICE_POOL_VOXELS, render_lod_pool_bytes(plan, i), "far section pool",
                                         &render->owned_lod_pools[i]);
        }
    }
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        render_lod_blocks_give_back(render);
        return status;
    }
    const uint64_t arena_bytes = render_lod_arena_bytes(plan) + sizeof(render_lod_t) + MC_CPU_CACHE_LINE_BYTES;
    if (!heap_reserve(arena_bytes, &render->owned_lod_host)) {
        render_lod_blocks_give_back(render);
        return GPU_STATUS_OUT_OF_MEMORY; /* it has said why */
    }
    arena_init(&render->arena_lod, "render_lod", render->owned_lod_host.owned_base, render->owned_lod_host.bytes);
    render_lod_t *const lod = arena_alloc(&render->arena_lod, sizeof(render_lod_t), alignof(render_lod_t));
    status = render_lod_init(lod, &render->arena_lod, memory, render->borrowed_job, plan, render->owned_lod_tables,
                             render->owned_lod_pools, &render->owned_lod_hits, sample, sample_context);
    if (status != GPU_STATUS_OK) {
        render_lod_deinit(lod);
        arena_deinit(&render->arena_lod);
        heap_release(&render->owned_lod_host);
        render_lod_blocks_give_back(render);
        return status;
    }
    render->borrowed_lod = lod;
    render->frame.borrowed_lod = lod;
    return GPU_STATUS_OK;
}

/* What render_replan_lod takes back from an old far field, once the new one no longer needs to read it. */
static void render_lod_old_give_back(render_lod_t *old, arena_t *old_arena, heap_region_t *old_host) {
    render_lod_deinit(old);
    arena_deinit(old_arena);
    heap_release(old_host);
}

gpu_status_t render_replan_lod(render_t *render, const render_lod_plan_t *plan, render_lod_sample_t sample,
                               const void *sample_context) {
    render_lod_t *const old = render->borrowed_lod;
    uint32_t from[LOD_LEVELS_MAX];
    uint32_t kept = 0u;
    for (uint32_t j = 0u; j < LOD_LEVELS_MAX; j++) {
        from[j] = old != nullptr && j < plan->level_count
                      ? render_lod_level_matching(old, plan->shifts[j], plan->radii[j])
                      : UINT32_MAX;
        kept += from[j] != UINT32_MAX ? 1u : 0u;
    }
    if (kept == 0u) {
        render_close_lod(render);
        return plan->level_count != 0u ? render_open_lod(render, plan, sample, sample_context) : GPU_STATUS_OK;
    }
    render_lod_quiesce(old);
    device_memory_t *const memory = render->borrowed_memory;
    /* What is kept moves to its new level's place. The rest of the old goes back first, so that the card never holds both. */
    device_block_t tables[LOD_LEVELS_MAX];
    device_block_t pools[LOD_LEVELS_MAX];
    memset(tables, 0, sizeof(tables));
    memset(pools, 0, sizeof(pools));
    for (uint32_t j = 0u; j < plan->level_count; j++) {
        if (from[j] != UINT32_MAX) {
            tables[j] = render->owned_lod_tables[from[j]];
            pools[j] = render->owned_lod_pools[from[j]];
            memset(&render->owned_lod_tables[from[j]], 0, sizeof(device_block_t));
            memset(&render->owned_lod_pools[from[j]], 0, sizeof(device_block_t));
        }
    }
    gpu_status_t status = GPU_STATUS_OK;
    for (uint32_t i = 0u; i < LOD_LEVELS_MAX; i++) {
        if (device_memory_free(memory, &render->owned_lod_pools[i]) != GPU_STATUS_OK ||
            device_memory_free(memory, &render->owned_lod_tables[i]) != GPU_STATUS_OK) {
            gpu_error_log(&memory->last_error);
        }
        render->owned_lod_tables[i] = tables[i];
        render->owned_lod_pools[i] = pools[i];
    }
    for (uint32_t j = 0u; j < plan->level_count && status == GPU_STATUS_OK; j++) {
        if (from[j] != UINT32_MAX) {
            continue;
        }
        status = device_memory_alloc(memory, DEVICE_POOL_VOXELS, render_lod_table_bytes(plan, j), "far column table",
                                     &render->owned_lod_tables[j]);
        if (status == GPU_STATUS_OK) {
            status = device_memory_alloc(memory, DEVICE_POOL_VOXELS, render_lod_pool_bytes(plan, j), "far section pool",
                                         &render->owned_lod_pools[j]);
        }
    }
    arena_t old_arena = render->arena_lod;
    heap_region_t old_host = render->owned_lod_host;
    render->borrowed_lod = nullptr;
    render->frame.borrowed_lod = nullptr;
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        render_lod_old_give_back(old, &old_arena, &old_host);
        render_lod_blocks_give_back(render);
        return status;
    }
    const uint64_t arena_bytes = render_lod_arena_bytes(plan) + sizeof(render_lod_t) + MC_CPU_CACHE_LINE_BYTES;
    if (!heap_reserve(arena_bytes, &render->owned_lod_host)) {
        render_lod_old_give_back(old, &old_arena, &old_host);
        render_lod_blocks_give_back(render);
        return GPU_STATUS_OUT_OF_MEMORY; /* it has said why */
    }
    arena_init(&render->arena_lod, "render_lod", render->owned_lod_host.owned_base, render->owned_lod_host.bytes);
    render_lod_t *const lod = arena_alloc(&render->arena_lod, sizeof(render_lod_t), alignof(render_lod_t));
    status = render_lod_init(lod, &render->arena_lod, memory, render->borrowed_job, plan, render->owned_lod_tables,
                             render->owned_lod_pools, &render->owned_lod_hits, sample, sample_context);
    for (uint32_t j = 0u; j < plan->level_count && status == GPU_STATUS_OK; j++) {
        if (from[j] != UINT32_MAX) {
            status = render_lod_level_adopt(lod, j, old, from[j]);
        }
    }
    render_lod_old_give_back(old, &old_arena, &old_host);
    if (status != GPU_STATUS_OK) {
        render_lod_deinit(lod);
        arena_deinit(&render->arena_lod);
        heap_release(&render->owned_lod_host);
        render_lod_blocks_give_back(render);
        return status;
    }
    render->borrowed_lod = lod;
    render->frame.borrowed_lod = lod;
    return GPU_STATUS_OK;
}

void render_close_lod(render_t *render) {
    if (render->borrowed_lod == nullptr) {
        return;
    }
    render_lod_deinit(render->borrowed_lod);
    arena_deinit(&render->arena_lod);
    heap_release(&render->owned_lod_host);
    render_lod_blocks_give_back(render);
    render->frame.borrowed_lod = nullptr;
    render->borrowed_lod = nullptr;
}

void render_close_frame(render_t *render) {
    if (render->frame_open == 0u) {
        return;
    }
    render_close_lod(render);
    render_entities_deinit(render->borrowed_entities);
    render_frame_deinit(&render->frame);
    arena_deinit(&render->arena_frame);
    heap_release(&render->owned_frame_host);
    render_frame_blocks_give_back(render);
    render->borrowed_entities = nullptr;
    render->frame_open = 0u;
}
