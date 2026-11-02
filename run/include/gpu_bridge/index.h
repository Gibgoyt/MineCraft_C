/* See run/src/gpu_bridge/README.md. The Owner. C only: nvcc never sees this, and takes the parts' headers from its .hu. */
#ifndef MC_RUN_GPU_BRIDGE_INDEX_H
#define MC_RUN_GPU_BRIDGE_INDEX_H

#include "arena/index.h"
#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "heap/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint64_t {
    /* Blocks live at once. Each pool is a few blocks, so this is far more than a game uses, and a record costs 32 bytes. */
    GPU_BRIDGE_BLOCK_COUNT_DEFAULT = 1024u,
    /* Past this a record table is not a mistake of size but of use: a block is not a small object. */
    GPU_BRIDGE_BLOCK_COUNT_MAX = 1u << 20,
};

typedef struct gpu_bridge_limits_t {
    device_memory_limits_t device; /* 0 */
    uint64_t block_count;          /* 24 */
} gpu_bridge_limits_t;
static_assert(sizeof(gpu_bridge_limits_t) == 32);
static_assert(alignof(gpu_bridge_limits_t) == 8);
static_assert(offsetof(gpu_bridge_limits_t, device) == 0);
static_assert(offsetof(gpu_bridge_limits_t, block_count) == 24);

/* Everything the program holds for the card. There is one, under app_t. */
typedef struct gpu_bridge_t {
    arena_t arena_host;            /* 0: over owned_host */
    heap_region_t owned_host;      /* 64: host memory for what the parts borrow */
    device_memory_t device_memory; /* 80 */
    uint8_t _pad0[40];             /* 344 */
} gpu_bridge_t;
static_assert(sizeof(gpu_bridge_t) == 384);
static_assert(alignof(gpu_bridge_t) == 64);
static_assert(offsetof(gpu_bridge_t, arena_host) == 0);
static_assert(offsetof(gpu_bridge_t, owned_host) == 64);
static_assert(offsetof(gpu_bridge_t, device_memory) == 80);
static_assert(offsetof(gpu_bridge_t, _pad0) == 344);

/* The limits a real game runs with: gpu.budget in config/machine.yaml. */
gpu_bridge_limits_t gpu_bridge_limits_default(void);

/* Starts CUDA on the card, and checks with a real kernel that the card runs what was compiled for it. On failure logs why,
 * leaves nothing behind, and returns what went wrong. Any thread CUDA starts inherits the caller's CPU mask, so call this
 * before the calling thread pins itself. A context does not survive fork: a process that has to fork forks first. */
gpu_status_t gpu_bridge_init(gpu_bridge_t *bridge, gpu_bridge_limits_t limits);

/* Frees whatever is left on the card, and everything init reserved. Safe on a zeroed gpu_bridge_t. */
void gpu_bridge_deinit(gpu_bridge_t *bridge);

/* The leak check for device memory, for before deinit, which frees what it finds and so hides it: logs each block still
 * live by name and returns false, or returns true if none is. */
bool gpu_bridge_exit_check(const gpu_bridge_t *bridge);

#endif
