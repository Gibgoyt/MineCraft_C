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
    uint64_t bytes = (uint64_t)slot_count * RENDER_POOL_BYTES_PER_COLUMN;
    bytes = bytes < RENDER_POOL_BYTES_MIN ? RENDER_POOL_BYTES_MIN : bytes;
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
