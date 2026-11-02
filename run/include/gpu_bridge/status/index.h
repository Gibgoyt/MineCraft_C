/* See run/src/gpu_bridge/status/README.md. Compiled by GCC and by nvcc, so it holds to what both accept, and its
 * static_asserts are checked on both sides. */
#ifndef MC_RUN_GPU_BRIDGE_STATUS_INDEX_H
#define MC_RUN_GPU_BRIDGE_STATUS_INDEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What became of a call across the boundary. Every CUDA result is turned into one of these: plans/CODING_STANDARD.md
 * section 8. C never sees a cudaError_t. */
typedef enum gpu_status_t : uint32_t {
    GPU_STATUS_OK = 0,
    GPU_STATUS_NO_DEVICE = 1,        /* no card CUDA can see */
    GPU_STATUS_DRIVER = 2,           /* a card, and a driver that will not run us */
    GPU_STATUS_NO_KERNEL_IMAGE = 3,  /* the card cannot run what the build compiled: gpu.architecture is wrong for it */
    GPU_STATUS_OUT_OF_MEMORY = 4,    /* the machine or the card itself said no */
    GPU_STATUS_OVER_BUDGET = 5,      /* gpu.budget in config/machine.yaml said no, and CUDA was not asked */
    GPU_STATUS_TABLE_FULL = 6,       /* more live blocks than the limits allow */
    GPU_STATUS_INVALID_ARGUMENT = 7, /* a caller's bug: a size, a range, a pool, or a device_memory_t not open */
    GPU_STATUS_BAD_HANDLE = 8,       /* a block already freed, or never allocated: a caller's bug */
    GPU_STATUS_DEVICE_LOST = 9,      /* a kernel faulted or the context died. CUDA's errors here are sticky: every later
                                        call fails too, so the only way on is to stop */
    GPU_STATUS_SELF_CHECK = 10,      /* everything said yes, and the kernel's answer was wrong */
    GPU_STATUS_CUDA_OTHER = 11,      /* a CUDA error with no meaning for us; gpu_error_t has its code */
} gpu_status_t;
static_assert(sizeof(gpu_status_t) == 4);

enum : uint32_t { GPU_STATUS_COUNT = 12u };

/* The last failure, with what the runtime said about it. The text is CUDA's or ours, static, and outlives everything. */
typedef struct gpu_error_t {
    const char *borrowed_what;      /* 0: what we were doing */
    const char *borrowed_cuda_name; /* 8: cudaErrorNoDevice and so on, or nullptr if CUDA was not asked */
    const char *borrowed_cuda_text; /* 16: CUDA's sentence, or nullptr */
    int32_t cuda_code;              /* 24: 0 if CUDA was not asked */
    gpu_status_t status;            /* 28 */
} gpu_error_t;
static_assert(sizeof(gpu_error_t) == 32);
static_assert(alignof(gpu_error_t) == 8);
static_assert(offsetof(gpu_error_t, borrowed_what) == 0);
static_assert(offsetof(gpu_error_t, borrowed_cuda_name) == 8);
static_assert(offsetof(gpu_error_t, borrowed_cuda_text) == 16);
static_assert(offsetof(gpu_error_t, cuda_code) == 24);
static_assert(offsetof(gpu_error_t, status) == 28);

/* A short sentence fragment for messages. Any value is safe to pass: one that is not a status says so. */
const char *gpu_status_name(gpu_status_t status);

/* Writes one error line for this failure. CUDA code does not log: it returns a status, and the C side reports it. */
void gpu_error_log(const gpu_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
