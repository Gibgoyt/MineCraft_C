/* ctest run_gpu_bridge. See README.md. */
#include "harness/index.h"

#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "machine/index.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint64_t {
    GPU_TEST_POOL_BYTES = 16ull << 20,
    GPU_TEST_BLOCK_BYTES = 1ull << 20,
    GPU_TEST_RECORDS = 8u,
    /* More than any card has, and it is refused before it takes anything. */
    GPU_TEST_IMPOSSIBLE_BYTES = 1ull << 50,
};
enum : uint32_t { GPU_TEST_PROCESS_MS = 60000u };

/* How main knows this run is the one a test started, and which. */
static const char gpu_test_child_variable[] = "MC_GPU_BRIDGE_TEST_CHILD";

static device_memory_limits_t gpu_test_limits(uint64_t bytes) {
    return (device_memory_limits_t){.pool_bytes = {bytes, bytes, bytes}};
}

static bool gpu_test_open(test_context_t *t, device_memory_t *memory, uint64_t limit, uint64_t record_count) {
    device_memory_record_t *const records = test_alloc(t, record_count * sizeof(device_memory_record_t));
    const gpu_status_t status = device_memory_init(memory, gpu_test_limits(limit), records, record_count);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
    }
    return CHECK_EQ_U64(status, GPU_STATUS_OK);
}

/* The arena gives no alignment above what its callers ask for, and a gpu_bridge_t wants its 64. */
static gpu_bridge_t *gpu_test_bridge(test_context_t *t) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, sizeof(gpu_bridge_t) + 64u);
    return (gpu_bridge_t *)((raw + 63u) & ~(uintptr_t)63u);
}

/* ---- The bridge ---------------------------------------------------------------------------------------------------- */

TEST(the_default_limits_are_the_yaml_budget) {
    const gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    CHECK_EQ_U64(limits.device.pool_bytes[DEVICE_POOL_VOXELS], (uint64_t)MC_GPU_BUDGET_VOXELS_MIB << 20);
    CHECK_EQ_U64(limits.device.pool_bytes[DEVICE_POOL_TEXTURES], (uint64_t)MC_GPU_BUDGET_TEXTURES_MIB << 20);
    CHECK_EQ_U64(limits.device.pool_bytes[DEVICE_POOL_FRAMES], (uint64_t)MC_GPU_BUDGET_FRAMES_MIB << 20);
    CHECK(limits.block_count > 0u);
}

TEST(the_bridge_starts_runs_its_self_check_and_stops) {
    heap_counters_t before;
    heap_counters_read(&before);
    gpu_bridge_t *const bridge = gpu_test_bridge(t);
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);

    const device_memory_t *const memory = &bridge->device_memory;
    CHECK(memory->open != 0);
    CHECK(memory->device_count >= 1);
    CHECK(memory->device_name[0] != '\0');
    CHECK(memory->total_bytes > 0u);
    CHECK(memory->free_bytes_at_init <= memory->total_bytes);
    /* The self-check took one block and gave it back. */
    CHECK_EQ_U64(memory->alloc_calls, 1u);
    CHECK_EQ_U64(memory->free_calls, 1u);
    CHECK_EQ_U64(memory->live_blocks, 0u);
    CHECK_EQ_U64(memory->pools[DEVICE_POOL_FRAMES].peak_bytes, DEVICE_MEMORY_SELF_CHECK_BYTES);
    CHECK(gpu_bridge_exit_check(bridge));

    heap_counters_t during;
    heap_counters_read(&during);
    CHECK_EQ_U64(during.reserve_calls, before.reserve_calls + 1u);

    gpu_bridge_deinit(bridge);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    CHECK_EQ_U64(bridge->device_memory.open, 0u);
}

TEST(the_bridge_refuses_a_block_limit_it_cannot_use_before_touching_anything) {
    heap_counters_t before;
    heap_counters_read(&before);
    gpu_bridge_t *const bridge = gpu_test_bridge(t);
    gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    limits.block_count = 0u;
    CHECK_EQ_U64(gpu_bridge_init(bridge, limits), GPU_STATUS_INVALID_ARGUMENT);
    limits.block_count = UINT64_MAX;
    CHECK_EQ_U64(gpu_bridge_init(bridge, limits), GPU_STATUS_INVALID_ARGUMENT);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
}

TEST(the_self_check_fails_cleanly_when_its_pool_has_no_room) {
    gpu_bridge_t *const bridge = gpu_test_bridge(t);
    gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    limits.device.pool_bytes[DEVICE_POOL_FRAMES] = DEVICE_MEMORY_SELF_CHECK_BYTES - 1u;
    heap_counters_t before;
    heap_counters_read(&before);
    CHECK_EQ_U64(gpu_bridge_init(bridge, limits), GPU_STATUS_OVER_BUDGET);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
    /* And it left a zeroed bridge, which deinit accepts. */
    gpu_bridge_deinit(bridge);
}

TEST(deinit_is_safe_on_a_zeroed_bridge_and_twice) {
    gpu_bridge_t *const bridge = gpu_test_bridge(t);
    gpu_bridge_deinit(bridge);
    CHECK(gpu_bridge_exit_check(bridge));
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    gpu_bridge_deinit(bridge);
    gpu_bridge_deinit(bridge);
}

/* ---- Blocks -------------------------------------------------------------------------------------------------------- */

TEST(what_is_written_is_what_is_read_back) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    device_block_t block;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, GPU_TEST_BLOCK_BYTES, "round trip", &block) ==
            GPU_STATUS_OK);
    CHECK(block.owned_address != 0u);
    CHECK_EQ_U64(block.bytes, GPU_TEST_BLOCK_BYTES);

    uint8_t *const sent = test_alloc(t, GPU_TEST_BLOCK_BYTES);
    uint8_t *const received = test_alloc(t, GPU_TEST_BLOCK_BYTES);
    uint64_t rng = 0x2545F4914F6CDD1Dull;
    for (uint64_t i = 0; i < GPU_TEST_BLOCK_BYTES; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        sent[i] = (uint8_t)rng;
    }
    CHECK_EQ_U64(device_memory_write(&memory, &block, 0u, sent, GPU_TEST_BLOCK_BYTES), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_read(&memory, &block, 0u, received, GPU_TEST_BLOCK_BYTES), GPU_STATUS_OK);
    CHECK_MEM_EQ(received, sent, GPU_TEST_BLOCK_BYTES);

    /* A range in the middle leaves the rest alone. */
    const uint8_t patch[16] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u};
    CHECK_EQ_U64(device_memory_write(&memory, &block, 4096u, patch, sizeof(patch)), GPU_STATUS_OK);
    memcpy(sent + 4096u, patch, sizeof(patch));
    CHECK_EQ_U64(device_memory_read(&memory, &block, 0u, received, GPU_TEST_BLOCK_BYTES), GPU_STATUS_OK);
    CHECK_MEM_EQ(received, sent, GPU_TEST_BLOCK_BYTES);

    /* So does zeroing one. */
    CHECK_EQ_U64(device_memory_zero(&memory, &block, 8192u, 1024u), GPU_STATUS_OK);
    memset(sent + 8192u, 0, 1024u);
    CHECK_EQ_U64(device_memory_read(&memory, &block, 0u, received, GPU_TEST_BLOCK_BYTES), GPU_STATUS_OK);
    CHECK_MEM_EQ(received, sent, GPU_TEST_BLOCK_BYTES);

    /* Reading the last byte and copying nothing at the very end are both in range. */
    CHECK_EQ_U64(device_memory_read(&memory, &block, GPU_TEST_BLOCK_BYTES - 1u, received, 1u), GPU_STATUS_OK);
    CHECK_EQ_U64(received[0], sent[GPU_TEST_BLOCK_BYTES - 1u]);
    CHECK_EQ_U64(device_memory_read(&memory, &block, GPU_TEST_BLOCK_BYTES, received, 0u), GPU_STATUS_OK);

    CHECK_EQ_U64(device_memory_free(&memory, &block), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(a_range_outside_the_block_is_refused_and_copies_nothing) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    device_block_t block;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "range", &block) == GPU_STATUS_OK);
    REQUIRE(device_memory_zero(&memory, &block, 0u, 4096u) == GPU_STATUS_OK);

    uint8_t bytes[64];
    memset(bytes, 0xAB, sizeof(bytes));
    const uint64_t bad_offsets[] = {4096u - 63u, 4097u, UINT64_MAX, UINT64_MAX - 63u};
    for (uint32_t i = 0; i < sizeof(bad_offsets) / sizeof(bad_offsets[0]); i++) {
        CHECK_EQ_U64(device_memory_write(&memory, &block, bad_offsets[i], bytes, sizeof(bytes)),
                     GPU_STATUS_INVALID_ARGUMENT);
        CHECK_EQ_U64(memory.last_error.status, GPU_STATUS_INVALID_ARGUMENT);
        CHECK_EQ_U64(device_memory_read(&memory, &block, bad_offsets[i], bytes, sizeof(bytes)),
                     GPU_STATUS_INVALID_ARGUMENT);
        CHECK_EQ_U64(device_memory_zero(&memory, &block, bad_offsets[i], sizeof(bytes)), GPU_STATUS_INVALID_ARGUMENT);
    }
    /* An error goes away with the next call that works. */
    uint8_t tail[64];
    CHECK_EQ_U64(device_memory_read(&memory, &block, 4096u - 64u, tail, sizeof(tail)), GPU_STATUS_OK);
    CHECK_EQ_U64(memory.last_error.status, GPU_STATUS_OK);
    const uint8_t zeros[64] = {0};
    CHECK_MEM_EQ(tail, zeros, sizeof(zeros));
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(blocks_are_aligned_for_anything_we_would_put_in_them) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    const uint64_t sizes[] = {1u, 3u, 100u, 4096u, GPU_TEST_BLOCK_BYTES};
    for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        device_block_t block;
        REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_FRAMES, sizes[i], "alignment", &block) == GPU_STATUS_OK);
        CHECK_EQ_U64(block.owned_address % 64u, 0u);
    }
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(a_kernel_changes_a_real_block) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    const gpu_status_t first = device_memory_self_check(&memory);
    if (first != GPU_STATUS_OK) {
        gpu_error_log(&memory.last_error);
    }
    CHECK_EQ_U64(first, GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_self_check(&memory), GPU_STATUS_OK);
    CHECK_EQ_U64(memory.live_blocks, 0u);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_FRAMES].peak_bytes, DEVICE_MEMORY_SELF_CHECK_BYTES);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

/* ---- Budgets and counts ---------------------------------------------------------------------------------------------- */

TEST(a_pool_holds_its_budget_and_no_more_and_gets_room_back) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_BLOCK_BYTES * 2u, GPU_TEST_RECORDS));
    device_block_t first;
    device_block_t second;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, GPU_TEST_BLOCK_BYTES, "first", &first) == GPU_STATUS_OK);
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, GPU_TEST_BLOCK_BYTES, "second", &second) == GPU_STATUS_OK);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_VOXELS].live_bytes, GPU_TEST_BLOCK_BYTES * 2u);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_VOXELS].live_blocks, 2u);

    device_block_t third;
    third.owned_address = 1u; /* it must come back empty, whatever was in it */
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 1u, "third", &third), GPU_STATUS_OVER_BUDGET);
    CHECK_EQ_U64(third.owned_address, 0u);
    CHECK_EQ_U64(memory.failed_allocs, 1u);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_VOXELS].live_blocks, 2u);
    CHECK_EQ_U64(memory.last_error.status, GPU_STATUS_OVER_BUDGET);
    CHECK_EQ_I64(memory.last_error.cuda_code, 0); /* CUDA was not asked */
    CHECK(memory.last_error.borrowed_cuda_name == nullptr);
    CHECK_STR_EQ(memory.last_error.borrowed_what, "third");

    /* Another pool's budget is its own. */
    device_block_t elsewhere;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_TEXTURES, GPU_TEST_BLOCK_BYTES, "elsewhere", &elsewhere) ==
            GPU_STATUS_OK);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_TEXTURES].live_bytes, GPU_TEST_BLOCK_BYTES);

    CHECK_EQ_U64(device_memory_free(&memory, &first), GPU_STATUS_OK);
    CHECK_EQ_U64(first.owned_address, 0u);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_VOXELS].live_bytes, GPU_TEST_BLOCK_BYTES);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_VOXELS].peak_bytes, GPU_TEST_BLOCK_BYTES * 2u);
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, GPU_TEST_BLOCK_BYTES, "third", &third),
                 GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(a_budget_the_size_of_the_address_space_does_not_overflow) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, UINT64_MAX, GPU_TEST_RECORDS));
    device_block_t block;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "small", &block) == GPU_STATUS_OK);
    device_block_t too_much;
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, UINT64_MAX, "all of it", &too_much),
                 GPU_STATUS_OVER_BUDGET);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(a_full_table_refuses_and_a_free_makes_room) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, 2u));
    device_block_t blocks[3];
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "a", &blocks[0]) == GPU_STATUS_OK);
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "b", &blocks[1]) == GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "c", &blocks[2]), GPU_STATUS_TABLE_FULL);
    CHECK_EQ_U64(memory.live_blocks, 2u);
    CHECK_EQ_U64(device_memory_free(&memory, &blocks[0]), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "c", &blocks[2]), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(a_bad_allocation_is_refused_by_name) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    device_block_t block;
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 0u, "nothing", &block), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 16u, nullptr, &block), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(device_memory_alloc(&memory, (device_pool_t)DEVICE_POOL_COUNT, 16u, "no such pool", &block),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(memory.failed_allocs, 3u);
    CHECK_EQ_U64(memory.live_blocks, 0u);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
    /* Closed now: it is refused and CUDA is not asked. */
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 16u, "closed", &block), GPU_STATUS_INVALID_ARGUMENT);
    /* And deinit a second time does nothing. */
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(no_call_here_reaches_the_host_heap) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    heap_counters_t before;
    heap_counters_read(&before);
    for (uint32_t i = 0; i < 64u; i++) {
        device_block_t block;
        REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_FRAMES, 4096u, "churn", &block) == GPU_STATUS_OK);
        REQUIRE(device_memory_free(&memory, &block) == GPU_STATUS_OK);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(memory.alloc_calls, 64u);
    CHECK_EQ_U64(memory.free_calls, 64u);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

/* ---- Handles --------------------------------------------------------------------------------------------------------- */

TEST(an_empty_handle_does_nothing_and_a_stale_one_is_refused) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    device_block_t empty = {0};
    CHECK_EQ_U64(device_memory_free(&memory, &empty), GPU_STATUS_OK);
    CHECK_EQ_U64(memory.free_calls, 0u);

    device_block_t block;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "stale", &block) == GPU_STATUS_OK);
    device_block_t stale = block;
    REQUIRE(device_memory_free(&memory, &block) == GPU_STATUS_OK);
    CHECK_EQ_U64(block.owned_address, 0u);
    CHECK_EQ_U64(device_memory_free(&memory, &stale), GPU_STATUS_BAD_HANDLE);
    CHECK(stale.owned_address != 0u);
    uint8_t byte = 0;
    CHECK_EQ_U64(device_memory_read(&memory, &stale, 0u, &byte, 1u), GPU_STATUS_BAD_HANDLE);
    CHECK_EQ_U64(device_memory_write(&memory, &stale, 0u, &byte, 1u), GPU_STATUS_BAD_HANDLE);
    CHECK_EQ_U64(device_memory_zero(&memory, &stale, 0u, 1u), GPU_STATUS_BAD_HANDLE);
    CHECK_EQ_U64(memory.free_calls, 1u);

    /* The next block takes the same record, and cudaMalloc may well give it the same address. The old handle still must not
     * reach it. */
    device_block_t next;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "next", &next) == GPU_STATUS_OK);
    CHECK_EQ_U64(next.record_index, stale.record_index);
    CHECK_EQ_U64(device_memory_free(&memory, &stale), GPU_STATUS_BAD_HANDLE);
    CHECK_EQ_U64(memory.live_blocks, 1u);
    CHECK_EQ_U64(device_memory_free(&memory, &next), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

TEST(a_forged_handle_never_gets_as_far_as_cuda) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    device_block_t block;
    REQUIRE(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "real", &block) == GPU_STATUS_OK);

    device_block_t forged = block;
    forged.owned_address += 4096u;
    CHECK_EQ_U64(device_memory_free(&memory, &forged), GPU_STATUS_BAD_HANDLE);
    forged = block;
    forged.record_index = UINT32_MAX;
    CHECK_EQ_U64(device_memory_free(&memory, &forged), GPU_STATUS_BAD_HANDLE);
    forged = block;
    forged.bytes = 1ull << 40; /* and it must not widen what a range check allows */
    uint8_t byte = 0;
    CHECK_EQ_U64(device_memory_read(&memory, &forged, 1ull << 30, &byte, 1u), GPU_STATUS_BAD_HANDLE);
    CHECK_EQ_I64(memory.last_error.cuda_code, 0);
    CHECK_EQ_U64(memory.live_blocks, 1u);
    CHECK_EQ_U64(device_memory_free(&memory, &block), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

/* ---- CUDA's own errors ----------------------------------------------------------------------------------------------- */

TEST(the_card_saying_no_is_reported_in_cuda_s_words) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, UINT64_MAX, GPU_TEST_RECORDS));
    device_block_t block;
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, GPU_TEST_IMPOSSIBLE_BYTES, "a pebibyte", &block),
                 GPU_STATUS_OUT_OF_MEMORY);
    CHECK_EQ_U64(block.owned_address, 0u);
    CHECK_EQ_U64(memory.pools[DEVICE_POOL_VOXELS].live_bytes, 0u);
    CHECK_EQ_U64(memory.live_blocks, 0u);
    CHECK_EQ_U64(memory.failed_allocs, 1u);
    CHECK_EQ_I64(memory.last_error.cuda_code, 2); /* cudaErrorMemoryAllocation, in driver_types.h */
    REQUIRE(memory.last_error.borrowed_cuda_name != nullptr);
    CHECK_STR_EQ(memory.last_error.borrowed_cuda_name, "cudaErrorMemoryAllocation");
    REQUIRE(memory.last_error.borrowed_cuda_text != nullptr);
    CHECK(memory.last_error.borrowed_cuda_text[0] != '\0');
    CHECK_STR_EQ(memory.last_error.borrowed_what, "a pebibyte");
    /* And that record was not used up, so the next one works. */
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, 4096u, "after", &block), GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);

    /* A failure that was reported must not be reported again by the next launch, which asks the runtime for its last error. */
    REQUIRE(gpu_test_open(t, &memory, UINT64_MAX, GPU_TEST_RECORDS));
    CHECK_EQ_U64(device_memory_alloc(&memory, DEVICE_POOL_VOXELS, GPU_TEST_IMPOSSIBLE_BYTES, "a pebibyte", &block),
                 GPU_STATUS_OUT_OF_MEMORY);
    const gpu_status_t after_failure = device_memory_self_check(&memory);
    if (after_failure != GPU_STATUS_OK) {
        gpu_error_log(&memory.last_error);
    }
    CHECK_EQ_U64(after_failure, GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

typedef struct gpu_test_mapping_t {
    int32_t code;
    gpu_status_t status;
} gpu_test_mapping_t;
static_assert(sizeof(gpu_test_mapping_t) == 8);

/* The numbers are from /opt/cuda/include/driver_types.h, on purpose: C may not include it, and these are what a card sends. */
TEST(every_cuda_error_that_means_something_to_us_has_its_status) {
    const gpu_test_mapping_t table[] = {
        {0, GPU_STATUS_OK},
        {1, GPU_STATUS_INVALID_ARGUMENT},  /* InvalidValue */
        {2, GPU_STATUS_OUT_OF_MEMORY},     /* MemoryAllocation */
        {3, GPU_STATUS_DRIVER},            /* InitializationError */
        {17, GPU_STATUS_BAD_HANDLE},       /* InvalidDevicePointer */
        {35, GPU_STATUS_DRIVER},           /* InsufficientDriver */
        {46, GPU_STATUS_DRIVER},           /* DevicesUnavailable */
        {53, GPU_STATUS_DEVICE_LOST},      /* PriorLaunchFailure */
        {100, GPU_STATUS_NO_DEVICE},       /* NoDevice */
        {101, GPU_STATUS_NO_DEVICE},       /* InvalidDevice */
        {200, GPU_STATUS_NO_KERNEL_IMAGE}, /* InvalidKernelImage */
        {209, GPU_STATUS_NO_KERNEL_IMAGE}, /* NoKernelImageForDevice */
        {214, GPU_STATUS_DEVICE_LOST},     /* ECCUncorrectable */
        {222, GPU_STATUS_NO_KERNEL_IMAGE}, /* UnsupportedPtxVersion */
        {700, GPU_STATUS_DEVICE_LOST},     /* IllegalAddress */
        {701, GPU_STATUS_OUT_OF_MEMORY},   /* LaunchOutOfResources */
        {702, GPU_STATUS_DEVICE_LOST},     /* LaunchTimeout */
        {709, GPU_STATUS_DEVICE_LOST},     /* ContextIsDestroyed */
        {714, GPU_STATUS_DEVICE_LOST},     /* HardwareStackError */
        {719, GPU_STATUS_DEVICE_LOST},     /* LaunchFailure */
        {803, GPU_STATUS_DRIVER},          /* SystemDriverMismatch */
        {999, GPU_STATUS_CUDA_OTHER},      /* Unknown */
        {123456, GPU_STATUS_CUDA_OTHER},   /* not an error at all */
    };
    for (uint32_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        CHECK_EQ_U64(device_memory_status_from_cuda(table[i].code), table[i].status);
    }
}

TEST(every_status_has_a_name_of_its_own) {
    for (uint32_t i = 0; i < GPU_STATUS_COUNT; i++) {
        const char *const name = gpu_status_name((gpu_status_t)i);
        REQUIRE(name != nullptr);
        CHECK(strcmp(name, "not a gpu_status_t") != 0);
        for (uint32_t j = 0; j < i; j++) {
            CHECK(strcmp(name, gpu_status_name((gpu_status_t)j)) != 0);
        }
    }
    CHECK_STR_EQ(gpu_status_name((gpu_status_t)GPU_STATUS_COUNT), "not a gpu_status_t");
}

/* ---- Another thread, as gpu will be -------------------------------------------------------------------------------- */

typedef struct gpu_test_thread_t {
    device_memory_t *borrowed_memory;
    gpu_status_t status;
    uint8_t _pad0[4];
} gpu_test_thread_t;
static_assert(sizeof(gpu_test_thread_t) == 16);

static void *gpu_test_thread_main(void *argument) {
    gpu_test_thread_t *const thread = argument;
    thread->status = device_memory_self_check(thread->borrowed_memory);
    return nullptr;
}

TEST(a_second_thread_uses_what_main_opened) {
    device_memory_t memory;
    REQUIRE(gpu_test_open(t, &memory, GPU_TEST_POOL_BYTES, GPU_TEST_RECORDS));
    gpu_test_thread_t thread = {.borrowed_memory = &memory, .status = GPU_STATUS_CUDA_OTHER};
    pthread_t handle;
    REQUIRE(pthread_create(&handle, nullptr, gpu_test_thread_main, &thread) == 0);
    REQUIRE(pthread_join(handle, nullptr) == 0);
    CHECK_EQ_U64(thread.status, GPU_STATUS_OK);
    CHECK_EQ_U64(device_memory_deinit(&memory), GPU_STATUS_OK);
}

/* ---- In a process of its own ------------------------------------------------------------------------------------------ */

/* Runs this same program again in a child mode, with CUDA_VISIBLE_DEVICES as given, or left alone if nullptr. */
static void gpu_test_run_child(test_context_t *t, const char *mode, const char *visible, test_process_t *out) {
    REQUIRE(setenv(gpu_test_child_variable, mode, 1) == 0);
    if (visible != nullptr) {
        REQUIRE(setenv("CUDA_VISIBLE_DEVICES", visible, 1) == 0);
    }
    char *const argv[] = {(char *)"/proc/self/exe", nullptr};
    test_process_run(t, argv, GPU_TEST_PROCESS_MS, out);
    unsetenv(gpu_test_child_variable);
    if (visible != nullptr) {
        unsetenv("CUDA_VISIBLE_DEVICES");
    }
}

static int32_t gpu_test_child_no_device(void) {
    gpu_bridge_t bridge;
    const gpu_status_t status = gpu_bridge_init(&bridge, gpu_bridge_limits_default());
    if (status != GPU_STATUS_NO_DEVICE) {
        return 1;
    }
    return heap_exit_check() ? 0 : 2;
}

static int32_t gpu_test_child_leak(void) {
    gpu_bridge_t bridge;
    if (gpu_bridge_init(&bridge, gpu_bridge_limits_default()) != GPU_STATUS_OK) {
        return 1;
    }
    device_block_t kept;
    device_block_t freed;
    if (device_memory_alloc(&bridge.device_memory, DEVICE_POOL_VOXELS, 4096u, "kept by mistake", &kept) !=
            GPU_STATUS_OK ||
        device_memory_alloc(&bridge.device_memory, DEVICE_POOL_VOXELS, 4096u, "freed properly", &freed) !=
            GPU_STATUS_OK ||
        device_memory_free(&bridge.device_memory, &freed) != GPU_STATUS_OK) {
        return 2;
    }
    const bool clean = gpu_bridge_exit_check(&bridge);
    /* Deinit frees it anyway, so that a leak costs a message and not the card. */
    gpu_bridge_deinit(&bridge);
    if (clean) {
        return 3;
    }
    return heap_exit_check() ? 0 : 4;
}

TEST(no_device_is_a_clean_failure_with_a_clear_message) {
    test_process_t run = {0};
    gpu_test_run_child(t, "no_device", "", &run);
    REQUIRE(run.started != 0);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "no CUDA device");
    CHECK_CONTAINS(run.output, "cudaErrorNoDevice");
}

TEST(a_block_left_live_is_named_and_still_given_back) {
    test_process_t run = {0};
    gpu_test_run_child(t, "leak", nullptr, &run);
    REQUIRE(run.started != 0);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "leak: block 'kept by mistake' of 4096 bytes");
    CHECK(strstr(run.output, "freed properly") == nullptr);
}

/* ---- The real program ------------------------------------------------------------------------------------------------- */

static void gpu_test_run_program(test_context_t *t, const char *visible, test_process_t *out) {
    REQUIRE(t->arg_count >= 1);
    if (visible != nullptr) {
        REQUIRE(setenv("CUDA_VISIBLE_DEVICES", visible, 1) == 0);
    }
    char *const argv[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks", (char *)"1", nullptr};
    test_process_run(t, argv, GPU_TEST_PROCESS_MS, out);
    if (visible != nullptr) {
        unsetenv("CUDA_VISIBLE_DEVICES");
    }
}

TEST(the_program_starts_the_card_and_stops_it) {
    test_process_t run = {0};
    gpu_test_run_program(t, nullptr, &run);
    REQUIRE(run.started != 0);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "gpu: ");
    CHECK_CONTAINS(run.output, "MiB of memory");
    CHECK_CONTAINS(run.output, " 0 bytes live");
}

TEST(the_program_says_why_and_exits_when_there_is_no_card) {
    test_process_t run = {0};
    gpu_test_run_program(t, "", &run);
    REQUIRE(run.started != 0);
    CHECK_EQ_I64(run.exit_code, 5); /* MAIN_EXIT_RUNTIME */
    CHECK_CONTAINS(run.output, "no CUDA device");
}

int32_t main(int32_t argc, char **argv) {
    const char *const child = getenv(gpu_test_child_variable);
    if (child != nullptr && strcmp(child, "no_device") == 0) {
        return gpu_test_child_no_device();
    }
    if (child != nullptr && strcmp(child, "leak") == 0) {
        return gpu_test_child_leak();
    }
    return test_main(argc, argv, nullptr);
}
