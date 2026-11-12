/* See README.md. The only file that calls cudaMalloc and cudaFree: plans/CODING_STANDARD.md section 4. */
#include "device_memory/index.hu"

#include <string.h>

static gpu_status_t device_memory_refuse(device_memory_t *memory, gpu_status_t status, const char *what) {
    memory->last_error = gpu_error_t{what, nullptr, nullptr, 0, status};
    return status;
}

static void device_memory_clear_error(device_memory_t *memory) {
    memory->last_error = gpu_error_t{nullptr, nullptr, nullptr, 0, GPU_STATUS_OK};
}

gpu_status_t device_memory_status_from_cuda(int32_t cuda_code) {
    switch ((cudaError_t)cuda_code) {
    case cudaSuccess:
        return GPU_STATUS_OK;
    case cudaErrorNoDevice:
    case cudaErrorInvalidDevice:
        return GPU_STATUS_NO_DEVICE;
    case cudaErrorInitializationError:
    case cudaErrorCudartUnloading:
    case cudaErrorStubLibrary:
    case cudaErrorInsufficientDriver:
    case cudaErrorCallRequiresNewerDriver:
    case cudaErrorDevicesUnavailable:
    case cudaErrorIncompatibleDriverContext:
    case cudaErrorDeviceNotLicensed:
    case cudaErrorSoftwareValidityNotEstablished:
    case cudaErrorStartupFailure:
    case cudaErrorDeviceUninitialized:
    case cudaErrorInsufficientLoaderVersion:
    case cudaErrorOperatingSystem:
    case cudaErrorNotPermitted:
    case cudaErrorSystemNotReady:
    case cudaErrorSystemDriverMismatch:
    case cudaErrorCompatNotSupportedOnDevice:
        return GPU_STATUS_DRIVER;
    case cudaErrorInvalidKernelImage:
    case cudaErrorNoKernelImageForDevice:
    case cudaErrorInvalidPtx:
    case cudaErrorUnsupportedPtxVersion:
    case cudaErrorJitCompilerNotFound:
    case cudaErrorJitCompilationDisabled:
    case cudaErrorInvalidDeviceFunction:
        return GPU_STATUS_NO_KERNEL_IMAGE;
    case cudaErrorMemoryAllocation:
    case cudaErrorLaunchOutOfResources:
        return GPU_STATUS_OUT_OF_MEMORY;
    case cudaErrorInvalidValue:
    case cudaErrorInvalidMemcpyDirection:
    case cudaErrorInvalidHostPointer:
    case cudaErrorMemoryValueTooLarge:
        return GPU_STATUS_INVALID_ARGUMENT;
    case cudaErrorInvalidDevicePointer:
        return GPU_STATUS_BAD_HANDLE;
    case cudaErrorIllegalAddress:
    case cudaErrorLaunchTimeout:
    case cudaErrorContextIsDestroyed:
    case cudaErrorAssert:
    case cudaErrorHardwareStackError:
    case cudaErrorIllegalInstruction:
    case cudaErrorMisalignedAddress:
    case cudaErrorInvalidAddressSpace:
    case cudaErrorInvalidPc:
    case cudaErrorLaunchFailure:
    case cudaErrorPriorLaunchFailure:
    case cudaErrorECCUncorrectable:
    case cudaErrorNvlinkUncorrectable:
    case cudaErrorContained:
        return GPU_STATUS_DEVICE_LOST;
    default:
        return GPU_STATUS_CUDA_OTHER;
    }
}

gpu_status_t device_memory_check(gpu_error_t *error, cudaError_t result, const char *what) {
    if (result == cudaSuccess) {
        return GPU_STATUS_OK;
    }
    const gpu_status_t status = device_memory_status_from_cuda((int32_t)result);
    *error = gpu_error_t{what, cudaGetErrorName(result), cudaGetErrorString(result), (int32_t)result, status};
    /* The runtime keeps this until asked, and the next launch's cudaGetLastError would report it as that launch's own: a
     * pool refused once would fail the next kernel (Stage 08.1 issue 2). Its value is the one just recorded. A lost device
     * stays lost: the runtime reports that from every call, whatever is cleared here. */
    (void)cudaGetLastError();
    return status;
}

gpu_status_t device_memory_init(device_memory_t *memory, device_memory_limits_t limits, device_memory_record_t *records,
                                uint64_t record_capacity) {
    memset(memory, 0, sizeof(*memory));
    memset(records, 0, record_capacity * sizeof(records[0]));
    memory->borrowed_records = records;
    memory->record_capacity = record_capacity;
    for (uint32_t i = 0; i < DEVICE_POOL_COUNT; i++) {
        memory->pools[i].limit_bytes = limits.pool_bytes[i];
    }

    gpu_error_t *const error = &memory->last_error;
    gpu_status_t status = device_memory_check(error, cudaGetDeviceCount(&memory->device_count), "counting devices");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    if (memory->device_count < 1) {
        return device_memory_refuse(memory, GPU_STATUS_NO_DEVICE, "counting devices");
    }
    status = device_memory_check(error, cudaSetDevice(0), "choosing the device");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    cudaDeviceProp properties;
    status = device_memory_check(error, cudaGetDeviceProperties(&properties, 0), "reading the device's properties");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    /* uint64_t is size_t here, so this is the call's own type. */
    status = device_memory_check(error, cudaMemGetInfo(&memory->free_bytes_at_init, &memory->total_bytes),
                                 "reading the device's memory");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    for (uint32_t i = 0; i + 1u < sizeof(memory->device_name) && properties.name[i] != '\0'; i++) {
        memory->device_name[i] = properties.name[i];
    }
    memory->open = 1;
    return GPU_STATUS_OK;
}

void device_memory_lost_set(device_memory_t *memory) {
    memory->lost = 1u;
}

gpu_status_t device_memory_deinit(device_memory_t *memory) {
    if (memory->open == 0) {
        return GPU_STATUS_OK;
    }
    device_memory_clear_error(memory);
    gpu_status_t first = GPU_STATUS_OK;
    for (uint64_t i = 0; i < memory->record_capacity; i++) {
        device_memory_record_t *const record = &memory->borrowed_records[i];
        if (record->address == 0u) {
            continue;
        }
        if (memory->lost == 0u) {
            gpu_error_t error = {};
            const gpu_status_t status =
                device_memory_check(&error, cudaFree((void *)record->address), "freeing a block");
            if (status == GPU_STATUS_DEVICE_LOST) {
                memory->lost = 1u; /* said once, and the rest are the driver's to take with the process */
            }
            if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
                first = status;
                memory->last_error = error;
            }
        }
        record->address = 0u;
    }
    memory->live_blocks = 0;
    for (uint32_t i = 0; i < DEVICE_POOL_COUNT; i++) {
        memory->pools[i].live_bytes = 0;
        memory->pools[i].live_blocks = 0;
    }
    memory->open = 0;
    return first;
}

static gpu_status_t device_memory_alloc_checked(device_memory_t *memory, device_pool_t pool, uint64_t bytes,
                                                const char *name, device_block_t *out) {
    if (memory->open == 0) {
        return device_memory_refuse(memory, GPU_STATUS_INVALID_ARGUMENT, "allocating from a closed device_memory_t");
    }
    if ((uint32_t)pool >= DEVICE_POOL_COUNT) {
        return device_memory_refuse(memory, GPU_STATUS_INVALID_ARGUMENT, "allocating from a pool that does not exist");
    }
    if (bytes == 0u || name == nullptr) {
        return device_memory_refuse(memory, GPU_STATUS_INVALID_ARGUMENT, "allocating no bytes, or with no name");
    }
    device_memory_pool_t *const entry = &memory->pools[(uint32_t)pool];
    if (bytes > entry->limit_bytes - entry->live_bytes) {
        return device_memory_refuse(memory, GPU_STATUS_OVER_BUDGET, name);
    }
    uint64_t index = memory->record_capacity;
    for (uint64_t i = 0; i < memory->record_capacity; i++) {
        if (memory->borrowed_records[i].address == 0u) {
            index = i;
            break;
        }
    }
    if (index == memory->record_capacity) {
        return device_memory_refuse(memory, GPU_STATUS_TABLE_FULL, name);
    }
    void *address = nullptr;
    const gpu_status_t status = device_memory_check(&memory->last_error, cudaMalloc(&address, bytes), name);
    if (status != GPU_STATUS_OK) {
        memory->lost = status == GPU_STATUS_DEVICE_LOST ? 1u : memory->lost;
        return status;
    }
    device_memory_record_t *const record = &memory->borrowed_records[index];
    record->address = (uintptr_t)address;
    record->bytes = bytes;
    record->borrowed_name = name;
    record->pool = (uint32_t)pool;
    record->generation++;
    entry->live_bytes += bytes;
    entry->live_blocks++;
    entry->peak_bytes = entry->live_bytes > entry->peak_bytes ? entry->live_bytes : entry->peak_bytes;
    memory->live_blocks++;
    *out = device_block_t{record->address, bytes, (uint32_t)index, record->generation};
    return GPU_STATUS_OK;
}

gpu_status_t device_memory_alloc(device_memory_t *memory, device_pool_t pool, uint64_t bytes, const char *name,
                                 device_block_t *out) {
    memset(out, 0, sizeof(*out));
    device_memory_clear_error(memory);
    memory->alloc_calls++;
    const gpu_status_t status = device_memory_alloc_checked(memory, pool, bytes, name, out);
    if (status != GPU_STATUS_OK) {
        memory->failed_allocs++;
    }
    return status;
}

/* The record this handle was given, if the block is still the one the handle means. */
static device_memory_record_t *device_memory_find(const device_memory_t *memory, const device_block_t *block) {
    if (memory->open == 0 || block->owned_address == 0u || block->record_index >= memory->record_capacity) {
        return nullptr;
    }
    device_memory_record_t *const record = &memory->borrowed_records[block->record_index];
    if (record->address != block->owned_address || record->generation != block->generation ||
        record->bytes != block->bytes) {
        return nullptr;
    }
    return record;
}

gpu_status_t device_memory_free(device_memory_t *memory, device_block_t *block) {
    device_memory_clear_error(memory);
    if (block->owned_address == 0u) {
        return GPU_STATUS_OK;
    }
    device_memory_record_t *const record = device_memory_find(memory, block);
    if (record == nullptr) {
        return device_memory_refuse(memory, GPU_STATUS_BAD_HANDLE, "freeing a block");
    }
    memory->free_calls++;
    /* With the card gone there is nothing to ask, and nothing a caller could do with the answer: the driver takes it with the process. */
    const gpu_status_t status =
        memory->lost != 0u
            ? GPU_STATUS_OK
            : device_memory_check(&memory->last_error, cudaFree((void *)record->address), record->borrowed_name);
    if (status == GPU_STATUS_DEVICE_LOST) {
        memory->lost = 1u;
    } else if (status != GPU_STATUS_OK) {
        return status; /* still recorded, so it still shows as live, and deinit tries again */
    }
    device_memory_pool_t *const entry = &memory->pools[record->pool];
    entry->live_bytes -= record->bytes;
    entry->live_blocks--;
    memory->live_blocks--;
    record->address = 0u;
    memset(block, 0, sizeof(*block));
    return GPU_STATUS_OK;
}

/* Written so that offset + bytes cannot overflow. */
static gpu_status_t device_memory_check_range(device_memory_t *memory, const device_block_t *block, uint64_t offset,
                                              uint64_t bytes, const char *what) {
    device_memory_clear_error(memory);
    const device_memory_record_t *const record = device_memory_find(memory, block);
    if (record == nullptr) {
        return device_memory_refuse(memory, GPU_STATUS_BAD_HANDLE, what);
    }
    if (offset > record->bytes || bytes > record->bytes - offset) {
        return device_memory_refuse(memory, GPU_STATUS_INVALID_ARGUMENT, what);
    }
    return GPU_STATUS_OK;
}

gpu_status_t device_memory_write(device_memory_t *memory, const device_block_t *block, uint64_t offset,
                                 const void *source, uint64_t bytes) {
    const gpu_status_t status = device_memory_check_range(memory, block, offset, bytes, "writing to a block");
    if (status != GPU_STATUS_OK || bytes == 0u) {
        return status;
    }
    return device_memory_check(&memory->last_error,
                               cudaMemcpy(device_memory_pointer(block) + offset, source, bytes, cudaMemcpyHostToDevice),
                               "writing to a block");
}

/* ---- Copies that do not wait (plan 008 phase 03) ----------------------------------------------------------------- */

enum : uint64_t {
    DEVICE_STAGING_ALIGNMENT = 16u,
};

gpu_status_t device_staging_copy(device_staging_t *staging, gpu_error_t *last_error, void *device_target,
                                 const void *source, uint64_t bytes, const char *what) {
    if (bytes == 0u) {
        return GPU_STATUS_OK;
    }
    const bool staged = staging != nullptr && staging->borrowed_areas[0] != nullptr;
    if (!staged || bytes > staging->bytes - staging->used) {
        if (staged) {
            staging->waited_count++;
        }
        /* From pageable memory on the legacy stream: it waits for everything queued before it, async copies included, so the order holds. */
        return device_memory_check(last_error, cudaMemcpy(device_target, source, bytes, cudaMemcpyHostToDevice), what);
    }
    const uint32_t area = staging->area;
    if (staging->used == 0u) {
        /* The frame that last read this area is two frames back, and already finished in the usual run: this costs nothing then. A mark
         * never recorded has passed. */
        const gpu_status_t status = device_memory_check(
            last_error, cudaEventSynchronize((cudaEvent_t)staging->owned_reuse[area]), "waiting to reuse staging");
        if (status != GPU_STATUS_OK) {
            return status;
        }
    }
    uint8_t *const at = staging->borrowed_areas[area] + staging->used;
    memcpy(at, source, bytes);
    staging->used += (bytes + DEVICE_STAGING_ALIGNMENT - 1u) / DEVICE_STAGING_ALIGNMENT * DEVICE_STAGING_ALIGNMENT;
    staging->used = staging->used > staging->bytes ? staging->bytes : staging->used;
    staging->sent_bytes += bytes;
    /* The legacy stream, as every kernel of a frame: behind them all, in order. */
    return device_memory_check(last_error, cudaMemcpyAsync(device_target, at, bytes, cudaMemcpyHostToDevice, 0), what);
}

gpu_status_t device_memory_write_async(device_memory_t *memory, device_staging_t *staging, const device_block_t *block,
                                       uint64_t offset, const void *source, uint64_t bytes) {
    const gpu_status_t status = device_memory_check_range(memory, block, offset, bytes, "writing to a block");
    if (status != GPU_STATUS_OK || bytes == 0u) {
        return status;
    }
    return device_staging_copy(staging, &memory->last_error, device_memory_pointer(block) + offset, source, bytes,
                               "writing to a block");
}

gpu_status_t device_staging_turn(device_staging_t *staging, gpu_error_t *error) {
    if (staging == nullptr || staging->borrowed_areas[0] == nullptr) {
        return GPU_STATUS_OK;
    }
    const gpu_status_t status =
        device_memory_check(error, cudaEventRecord((cudaEvent_t)staging->owned_reuse[staging->area], 0),
                            "marking where staging may be reused");
    staging->area ^= 1u;
    staging->used = 0u;
    return status;
}

gpu_status_t device_staging_init(device_staging_t *staging, uint8_t *areas, uint64_t bytes, gpu_error_t *error) {
    *staging = device_staging_t{};
    device_staging_t made = {};
    gpu_status_t status = GPU_STATUS_OK;
    for (uint32_t i = 0u; i < 2u && status == GPU_STATUS_OK; i++) {
        cudaEvent_t event = nullptr;
        status = device_memory_check(error, cudaEventCreateWithFlags(&event, cudaEventDisableTiming),
                                     "making a staging mark");
        made.owned_reuse[i] = (void *)event;
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error, cudaHostRegister(areas, 2u * bytes, cudaHostRegisterDefault),
                                     "pinning the staging for copies to the card");
    }
    if (status != GPU_STATUS_OK) {
        for (uint32_t i = 0u; i < 2u; i++) {
            if (made.owned_reuse[i] != nullptr) {
                (void)cudaEventDestroy((cudaEvent_t)made.owned_reuse[i]);
            }
        }
        return status;
    }
    made.borrowed_areas[0] = areas;
    made.borrowed_areas[1] = areas + bytes;
    made.bytes = bytes;
    *staging = made;
    return GPU_STATUS_OK;
}

gpu_status_t device_staging_deinit(device_staging_t *staging, gpu_error_t *error) {
    gpu_status_t first = GPU_STATUS_OK;
    if (staging->borrowed_areas[0] != nullptr) {
        first = device_memory_check(error, cudaHostUnregister(staging->borrowed_areas[0]), "unpinning the staging");
    }
    for (uint32_t i = 0u; i < 2u; i++) {
        if (staging->owned_reuse[i] != nullptr) {
            gpu_error_t said = {};
            const gpu_status_t status = device_memory_check(
                &said, cudaEventDestroy((cudaEvent_t)staging->owned_reuse[i]), "giving a staging mark back");
            if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
                first = status;
                *error = said;
            }
        }
    }
    *staging = device_staging_t{};
    return first;
}

gpu_status_t device_memory_read(device_memory_t *memory, const device_block_t *block, uint64_t offset,
                                void *destination, uint64_t bytes) {
    const gpu_status_t status = device_memory_check_range(memory, block, offset, bytes, "reading from a block");
    if (status != GPU_STATUS_OK || bytes == 0u) {
        return status;
    }
    return device_memory_check(
        &memory->last_error,
        cudaMemcpy(destination, device_memory_pointer(block) + offset, bytes, cudaMemcpyDeviceToHost),
        "reading from a block");
}

gpu_status_t device_memory_zero(device_memory_t *memory, const device_block_t *block, uint64_t offset, uint64_t bytes) {
    const gpu_status_t status = device_memory_check_range(memory, block, offset, bytes, "zeroing a block");
    if (status != GPU_STATUS_OK || bytes == 0u) {
        return status;
    }
    /* Memset alone may return before the card has done it, and a caller here was promised it had. */
    const gpu_status_t set = device_memory_check(
        &memory->last_error, cudaMemset(device_memory_pointer(block) + offset, 0, bytes), "zeroing a block");
    if (set != GPU_STATUS_OK) {
        return set;
    }
    return device_memory_check(&memory->last_error, cudaDeviceSynchronize(), "waiting for the zeroing");
}

/* ---- Losing the card on purpose ----------------------------------------------------------------------------------- */

__global__ void device_memory_fault_kernel(uint32_t *nowhere) {
    nowhere[threadIdx.x] = 1u;
}

gpu_status_t device_memory_fault_now(device_memory_t *memory) {
    device_memory_clear_error(memory);
    /* Not 0, which a compiler may take for undefined and do something else with. Nothing is mapped this low in any context. */
    uint32_t *const nowhere = (uint32_t *)(uintptr_t)0x1000u;
    device_memory_fault_kernel<<<1, 1>>>(nowhere);
    const gpu_status_t launched = device_memory_check(&memory->last_error, cudaGetLastError(), "faulting on purpose");
    const gpu_status_t status =
        launched != GPU_STATUS_OK
            ? launched
            : device_memory_check(&memory->last_error, cudaDeviceSynchronize(), "faulting on purpose");
    memory->lost = status == GPU_STATUS_DEVICE_LOST ? 1u : memory->lost;
    return status;
}

gpu_status_t device_memory_fault_queue(device_memory_t *memory) {
    device_memory_clear_error(memory);
    uint32_t *const nowhere = (uint32_t *)(uintptr_t)0x1000u;
    device_memory_fault_kernel<<<1, 1>>>(nowhere);
    return device_memory_check(&memory->last_error, cudaGetLastError(), "faulting on purpose");
}

/* One thread spins on the SM's clock. */
__global__ void device_memory_busy_kernel(uint64_t cycles) {
    const int64_t started = (int64_t)clock64();
    int64_t now = started;
    while ((uint64_t)(now - started) < cycles) {
        now = (int64_t)clock64();
    }
}

gpu_status_t device_memory_busy_queue(device_memory_t *memory, uint64_t cycles) {
    device_memory_clear_error(memory);
    device_memory_busy_kernel<<<1, 1>>>(cycles);
    return device_memory_check(&memory->last_error, cudaGetLastError(), "keeping the card busy");
}

/* ---- The self-check ---------------------------------------------------------------------------------------------- */

enum : uint32_t {
    DEVICE_MEMORY_SELF_CHECK_THREADS = 256u,
    DEVICE_MEMORY_SELF_CHECK_BLOCKS = DEVICE_MEMORY_SELF_CHECK_VALUES / DEVICE_MEMORY_SELF_CHECK_THREADS,
};
static_assert(DEVICE_MEMORY_SELF_CHECK_BLOCKS * DEVICE_MEMORY_SELF_CHECK_THREADS == DEVICE_MEMORY_SELF_CHECK_VALUES);
static_assert(DEVICE_MEMORY_SELF_CHECK_VALUES * sizeof(uint32_t) == DEVICE_MEMORY_SELF_CHECK_BYTES);

static __host__ __device__ uint32_t device_memory_mix(uint32_t value, uint32_t index) {
    return (value ^ 0x5BD1E995u) + index * 0x9E3779B1u;
}

__global__ void device_memory_self_check_kernel(uint32_t *values) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    values[index] = device_memory_mix(values[index], index);
}

static gpu_status_t device_memory_self_check_run(device_memory_t *memory, const device_block_t *block) {
    uint32_t values[DEVICE_MEMORY_SELF_CHECK_VALUES];
    for (uint32_t i = 0; i < DEVICE_MEMORY_SELF_CHECK_VALUES; i++) {
        values[i] = i * 0x01000193u;
    }
    gpu_status_t status = device_memory_write(memory, block, 0u, values, sizeof(values));
    if (status != GPU_STATUS_OK) {
        return status;
    }
    device_memory_self_check_kernel<<<DEVICE_MEMORY_SELF_CHECK_BLOCKS, DEVICE_MEMORY_SELF_CHECK_THREADS>>>(
        (uint32_t *)device_memory_pointer(block));
    status = device_memory_check(&memory->last_error, cudaGetLastError(), "starting the self-check kernel");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    status = device_memory_check(&memory->last_error, cudaDeviceSynchronize(), "running the self-check kernel");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    status = device_memory_read(memory, block, 0u, values, sizeof(values));
    if (status != GPU_STATUS_OK) {
        return status;
    }
    for (uint32_t i = 0; i < DEVICE_MEMORY_SELF_CHECK_VALUES; i++) {
        if (values[i] != device_memory_mix(i * 0x01000193u, i)) {
            return device_memory_refuse(memory, GPU_STATUS_SELF_CHECK, "comparing the self-check kernel's answer");
        }
    }
    return GPU_STATUS_OK;
}

gpu_status_t device_memory_self_check(device_memory_t *memory) {
    device_block_t block;
    const gpu_status_t allocated =
        device_memory_alloc(memory, DEVICE_POOL_FRAMES, DEVICE_MEMORY_SELF_CHECK_BYTES, "self-check", &block);
    if (allocated != GPU_STATUS_OK) {
        return allocated;
    }
    const gpu_status_t ran = device_memory_self_check_run(memory, &block);
    const gpu_error_t ran_error = memory->last_error; /* the free below would clear what the run said */
    const gpu_status_t freed = device_memory_free(memory, &block);
    if (ran != GPU_STATUS_OK) {
        memory->last_error = ran_error;
        return ran;
    }
    return freed;
}
