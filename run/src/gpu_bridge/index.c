/* See README.md. The Owner: it holds the host region, and starts and stops the card. */
#include "gpu_bridge/index.h"

#include "log/index.h"
#include "machine/index.h"

#include <inttypes.h>
#include <string.h>

gpu_bridge_limits_t gpu_bridge_limits_default(void) {
    return (gpu_bridge_limits_t){
        .device = {.pool_bytes = {[DEVICE_POOL_VOXELS] = (uint64_t)MC_GPU_BUDGET_VOXELS_MIB << 20,
                                  [DEVICE_POOL_TEXTURES] = (uint64_t)MC_GPU_BUDGET_TEXTURES_MIB << 20,
                                  [DEVICE_POOL_FRAMES] = (uint64_t)MC_GPU_BUDGET_FRAMES_MIB << 20}},
        .block_count = GPU_BRIDGE_BLOCK_COUNT_DEFAULT,
    };
}

gpu_status_t gpu_bridge_init(gpu_bridge_t *bridge, gpu_bridge_limits_t limits) {
    memset(bridge, 0, sizeof(*bridge));
    if (limits.block_count == 0u || limits.block_count > GPU_BRIDGE_BLOCK_COUNT_MAX) {
        LOG_ERROR("gpu: a limit of %" PRIu64 " live blocks is outside 1 to %" PRIu64, limits.block_count,
                  (uint64_t)GPU_BRIDGE_BLOCK_COUNT_MAX);
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t record_bytes = limits.block_count * sizeof(device_memory_record_t);
    if (!heap_reserve(record_bytes, &bridge->owned_host)) {
        return GPU_STATUS_OUT_OF_MEMORY; /* it has said why */
    }
    arena_init(&bridge->arena_host, "gpu_bridge_host", bridge->owned_host.owned_base, bridge->owned_host.bytes);
    device_memory_record_t *const records = arena_alloc_array(
        &bridge->arena_host, limits.block_count, sizeof(device_memory_record_t), alignof(device_memory_record_t));

    device_memory_t *const memory = &bridge->device_memory;
    gpu_status_t status = device_memory_init(memory, limits.device, records, limits.block_count);
    if (status == GPU_STATUS_OK) {
        status = device_memory_self_check(memory);
    }
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        goto cleanup;
    }

    uint64_t budget_bytes = 0;
    for (uint32_t i = 0; i < DEVICE_POOL_COUNT; i++) {
        budget_bytes += memory->pools[i].limit_bytes;
    }
    LOG_INFO("gpu: %s, %" PRIu64 " MiB of memory, %" PRIu64 " MiB free, and a budget of %" PRIu64 " MiB",
             memory->device_name, memory->total_bytes >> 20, memory->free_bytes_at_init >> 20, budget_bytes >> 20);
    if (budget_bytes > memory->free_bytes_at_init) {
        /* Not fatal: a budget is a ceiling, and a small world never reaches it. The failure, if it comes, names itself. */
        LOG_WARN("gpu: the budget is more than is free now, so a large world may run out of device memory");
    }
    return GPU_STATUS_OK;

cleanup:
    gpu_bridge_deinit(bridge);
    return status;
}

void gpu_bridge_deinit(gpu_bridge_t *bridge) {
    if (device_memory_deinit(&bridge->device_memory) != GPU_STATUS_OK) {
        gpu_error_log(&bridge->device_memory.last_error);
    }
    arena_deinit(&bridge->arena_host);
    heap_release(&bridge->owned_host);
    memset(bridge, 0, sizeof(*bridge));
}

bool gpu_bridge_exit_check(const gpu_bridge_t *bridge) {
    const device_memory_t *const memory = &bridge->device_memory;
    /* With the card gone they are the driver's, and no bug: a run that stopped in the middle of things need not have given back what it could not. */
    if (memory->open == 0 || memory->lost != 0u || memory->live_blocks == 0u) {
        return true;
    }
    for (uint64_t i = 0; i < memory->record_capacity; i++) {
        const device_memory_record_t *const record = &memory->borrowed_records[i];
        if (record->address != 0u) {
            LOG_ERROR("gpu: leak: block '%s' of %" PRIu64 " bytes in pool %" PRIu32 " is still live",
                      record->borrowed_name, record->bytes, record->pool);
        }
    }
    return false;
}
