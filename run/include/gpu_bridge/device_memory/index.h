/* See cuda/src/device_memory/README.md, which implements this, and run/src/gpu_bridge/device_memory/README.md. C API,
 * compiled by GCC and by nvcc: both sides check these static_asserts. */
#ifndef MC_RUN_GPU_BRIDGE_DEVICE_MEMORY_INDEX_H
#define MC_RUN_GPU_BRIDGE_DEVICE_MEMORY_INDEX_H

#include "gpu_bridge/status/index.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* gpu.budget in config/machine.yaml has one line for each of these. */
typedef enum device_pool_t : uint32_t {
    DEVICE_POOL_VOXELS = 0,
    DEVICE_POOL_TEXTURES = 1,
    DEVICE_POOL_FRAMES = 2,
} device_pool_t;
static_assert(sizeof(device_pool_t) == 4);

enum : uint32_t {
    DEVICE_POOL_COUNT = 3u,
    /* What device_memory_self_check takes from DEVICE_POOL_FRAMES, while it runs. */
    DEVICE_MEMORY_SELF_CHECK_VALUES = 1024u,
    DEVICE_MEMORY_SELF_CHECK_BYTES = DEVICE_MEMORY_SELF_CHECK_VALUES * 4u,
};

/* The most each pool may hold at once. Device memory has no untouched-pages-are-free rule: what is allocated is taken. */
typedef struct device_memory_limits_t {
    uint64_t pool_bytes[DEVICE_POOL_COUNT]; /* 0 */
} device_memory_limits_t;
static_assert(sizeof(device_memory_limits_t) == 24);
static_assert(alignof(device_memory_limits_t) == 8);
static_assert(offsetof(device_memory_limits_t, pool_bytes) == 0);

/* One block on the card. The struct field holding this is its one owner, and passes it to device_memory_free. C never
 * reads through the address. A CUDA area gets a pointer from device_memory_pointer in its .hu. Empty when all zero. */
typedef struct device_block_t {
    uintptr_t owned_address; /* 0 */
    uint64_t bytes;          /* 8: what was asked for */
    uint32_t record_index;   /* 16 */
    uint32_t generation;     /* 20: tells this block from a later one in the same record, even at the same address */
} device_block_t;
static_assert(sizeof(device_block_t) == 24);
static_assert(alignof(device_block_t) == 8);
static_assert(offsetof(device_block_t, owned_address) == 0);
static_assert(offsetof(device_block_t, bytes) == 8);
static_assert(offsetof(device_block_t, record_index) == 16);
static_assert(offsetof(device_block_t, generation) == 20);

/* What device_memory knows of one live block. A free record has address 0. */
typedef struct device_memory_record_t {
    uintptr_t address;         /* 0 */
    uint64_t bytes;            /* 8 */
    const char *borrowed_name; /* 16: for messages; must outlive the block */
    uint32_t pool;             /* 24: a device_pool_t */
    uint32_t generation;       /* 28: counts the blocks this record has held */
} device_memory_record_t;
static_assert(sizeof(device_memory_record_t) == 32);
static_assert(alignof(device_memory_record_t) == 8);
static_assert(offsetof(device_memory_record_t, address) == 0);
static_assert(offsetof(device_memory_record_t, bytes) == 8);
static_assert(offsetof(device_memory_record_t, borrowed_name) == 16);
static_assert(offsetof(device_memory_record_t, pool) == 24);
static_assert(offsetof(device_memory_record_t, generation) == 28);

typedef struct device_memory_pool_t {
    uint64_t limit_bytes; /* 0 */
    uint64_t live_bytes;  /* 8 */
    uint64_t peak_bytes;  /* 16 */
    uint64_t live_blocks; /* 24 */
} device_memory_pool_t;
static_assert(sizeof(device_memory_pool_t) == 32);
static_assert(alignof(device_memory_pool_t) == 8);
static_assert(offsetof(device_memory_pool_t, limit_bytes) == 0);
static_assert(offsetof(device_memory_pool_t, live_bytes) == 8);
static_assert(offsetof(device_memory_pool_t, peak_bytes) == 16);
static_assert(offsetof(device_memory_pool_t, live_blocks) == 24);

/* The device allocator. One thread at a time, with no atomics: the gpu thread once it runs, and main before and after. The
 * fields may be read directly; only these functions write them. */
typedef struct device_memory_t {
    device_memory_record_t *borrowed_records;      /* 0: from the bridge's arena */
    uint64_t record_capacity;                      /* 8 */
    uint64_t total_bytes;                          /* 16: on the card */
    uint64_t free_bytes_at_init;                   /* 24: the desktop shares the card, so this moves */
    uint64_t alloc_calls;                          /* 32: including those that failed */
    uint64_t free_calls;                           /* 40: not counting a free of an empty block, which does nothing */
    uint64_t failed_allocs;                        /* 48 */
    uint64_t live_blocks;                          /* 56 */
    device_memory_pool_t pools[DEVICE_POOL_COUNT]; /* 64 */
    gpu_error_t last_error;                        /* 160: the last call's failure; status OK if it did not fail */
    char device_name[64];                          /* 192: what the driver calls the card */
    int32_t device_count;                          /* 256: int32_t because the runtime asks for one by pointer */
    uint8_t open;                                  /* 260 */
    uint8_t lost;                                  /* 261: the card has gone, and what it held went with it */
    uint8_t _pad0[2];                              /* 262 */
} device_memory_t;
static_assert(sizeof(device_memory_t) == 264);
static_assert(alignof(device_memory_t) == 8);
static_assert(offsetof(device_memory_t, borrowed_records) == 0);
static_assert(offsetof(device_memory_t, record_capacity) == 8);
static_assert(offsetof(device_memory_t, total_bytes) == 16);
static_assert(offsetof(device_memory_t, free_bytes_at_init) == 24);
static_assert(offsetof(device_memory_t, alloc_calls) == 32);
static_assert(offsetof(device_memory_t, free_calls) == 40);
static_assert(offsetof(device_memory_t, failed_allocs) == 48);
static_assert(offsetof(device_memory_t, live_blocks) == 56);
static_assert(offsetof(device_memory_t, pools) == 64);
static_assert(offsetof(device_memory_t, last_error) == 160);
static_assert(offsetof(device_memory_t, device_name) == 192);
static_assert(offsetof(device_memory_t, device_count) == 256);
static_assert(offsetof(device_memory_t, open) == 260);
static_assert(offsetof(device_memory_t, lost) == 261);
static_assert(offsetof(device_memory_t, _pad0) == 262);

/* Finds the card and starts CUDA, which takes a good fraction of a second the first time in a process. records is borrowed
 * and must outlive this, and its capacity is the most blocks that can be live at once. Takes no device memory. On failure
 * last_error says why and nothing needs undoing, though deinit is safe. */
gpu_status_t device_memory_init(device_memory_t *memory, device_memory_limits_t limits, device_memory_record_t *records,
                                uint64_t record_capacity);

/* Frees every block still live. Whoever kept one has leaked it: the records name them, for anyone wanting to say so first.
 * Safe on a zeroed or failed device_memory_t. Returns the first failure, also left in last_error. */
gpu_status_t device_memory_deinit(device_memory_t *memory);

/* Allocates bytes from a pool's budget, aligned for any kind of variable. Contents are not defined. name is for messages,
 * and must outlive the block. On failure out is empty, nothing is taken and last_error says why. Not for a hot path: it
 * walks the records, and it counts. */
gpu_status_t device_memory_alloc(device_memory_t *memory, device_pool_t pool, uint64_t bytes, const char *name,
                                 device_block_t *out);

/* Frees a block and empties the handle. An empty handle does nothing, so _deinit can call this on a partial _init. A handle
 * that is not live is refused without asking CUDA, and left as it was. */
gpu_status_t device_memory_free(device_memory_t *memory, device_block_t *block);

/* Copies between here and the card, and waits until it is done. Ranges are checked against the block, and a range that does
 * not fit is refused with nothing copied. For a few bytes or a start-up load: the upload queue has its own path. */
gpu_status_t device_memory_write(device_memory_t *memory, const device_block_t *block, uint64_t offset,
                                 const void *source, uint64_t bytes);
gpu_status_t device_memory_read(device_memory_t *memory, const device_block_t *block, uint64_t offset,
                                void *destination, uint64_t bytes);

/* Sets a range to zero on the card, and waits until it is done. */
gpu_status_t device_memory_zero(device_memory_t *memory, const device_block_t *block, uint64_t offset, uint64_t bytes);

/* Allocates DEVICE_MEMORY_SELF_CHECK_BYTES, sends it a pattern, has a real kernel change every value, reads it back and
 * compares, and frees it. So a card that cannot run what the build compiled for it fails here, and not in the first frame. */
gpu_status_t device_memory_self_check(device_memory_t *memory);

/* The table from the runtime's error numbers to our statuses. 0 is OK. Public so that a test can read it, as a card will
 * make only some of these on request. */
gpu_status_t device_memory_status_from_cuda(int32_t cuda_code);

/* The card has gone: GPU_STATUS_DEVICE_LOST came back from somewhere. Every block counts as given back from now on, as nothing can
 * free what the driver will take with this process, and asking again would only say the same thing for each of them. */
void device_memory_lost_set(device_memory_t *memory);

/* Makes this process's context on the card fault for real, by a kernel that writes where no memory is, and waits for what the runtime
 * says of it: GPU_STATUS_DEVICE_LOST, in last_error, and in every call from then on. It takes only this process's context with it: the
 * kernel's log gets a line for it, and the desktop on the same card goes on. For --pretend-gpu-lost and tests. There is no way back but
 * to end the process. */
gpu_status_t device_memory_fault_now(device_memory_t *memory);

#ifdef __cplusplus
}
#endif

#endif
