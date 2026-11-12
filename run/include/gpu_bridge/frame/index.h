/* See run/src/gpu_bridge/frame/README.md and cuda/src/frame/README.md, which implements this. C API, compiled by GCC and by
 * nvcc: both sides check these static_asserts. */
#ifndef MC_RUN_GPU_BRIDGE_FRAME_INDEX_H
#define MC_RUN_GPU_BRIDGE_FRAME_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/entity_rays/index.h"
#include "gpu_bridge/lod/index.h"
#include "gpu_bridge/post/index.h"
#include "gpu_bridge/shade/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A frame's passes, in order. */
typedef enum frame_pass_t : uint32_t {
    FRAME_PASS_COARSE = 0,
    FRAME_PASS_PRIMARY = 1,
    FRAME_PASS_SHADE = 2,
    FRAME_PASS_POST = 3,
    FRAME_PASS_READBACK = 4,
    FRAME_PASS_COUNT = 5,
} frame_pass_t;
static_assert(sizeof(frame_pass_t) == 4);

/* Each kernel call of a frame, and the copy home, in the order they run. A pass is one or more of these in a row. */
typedef enum frame_kernel_t : uint32_t {
    FRAME_KERNEL_COARSE = 0,  /* FRAME_PASS_COARSE */
    FRAME_KERNEL_PRIMARY = 1, /* FRAME_PASS_PRIMARY, from here */
    FRAME_KERNEL_ENTITY_RAYS = 2,
    FRAME_KERNEL_LOD = 3,
    FRAME_KERNEL_SHADE = 4,    /* FRAME_PASS_SHADE */
    FRAME_KERNEL_GOD_RAYS = 5, /* FRAME_PASS_POST, from here */
    FRAME_KERNEL_POST = 6,
    FRAME_KERNEL_OVERLAY = 7,
    FRAME_KERNEL_READBACK = 8, /* FRAME_PASS_READBACK */
    FRAME_KERNEL_COUNT = 9,
} frame_kernel_t;
static_assert(sizeof(frame_kernel_t) == 4);

/* Where a frame's passes write, for the size being rendered or larger. Borrowed from whoever holds the blocks. */
typedef struct frame_targets_t {
    const device_block_t *borrowed_coarse; /* 0: traverse_coarse_bytes */
    const device_block_t *borrowed_hits;   /* 8: traverse_hits_bytes */
    const device_block_t *borrowed_pixels; /* 16: shade_pixels_bytes */
    const device_block_t *borrowed_xrgb;   /* 24: post_xrgb_bytes */
} frame_targets_t;
static_assert(sizeof(frame_targets_t) == 32);
static_assert(alignof(frame_targets_t) == 8);
static_assert(offsetof(frame_targets_t, borrowed_coarse) == 0);
static_assert(offsetof(frame_targets_t, borrowed_hits) == 8);
static_assert(offsetof(frame_targets_t, borrowed_pixels) == 16);
static_assert(offsetof(frame_targets_t, borrowed_xrgb) == 24);

/* What the card's own clock says each pass took, in the stream's order. */
typedef struct frame_timings_t {
    float32_t pass_ms[FRAME_PASS_COUNT];     /* 0 */
    float32_t total_ms;                      /* 20 */
    float32_t kernel_ms[FRAME_KERNEL_COUNT]; /* 24: 0 for one this frame did not run */
    uint32_t kernels_ran;                    /* 60: a bit for each frame_kernel_t this frame ran */
} frame_timings_t;
static_assert(sizeof(frame_timings_t) == 64);
static_assert(alignof(frame_timings_t) == 4);
static_assert(offsetof(frame_timings_t, pass_ms) == 0);
static_assert(offsetof(frame_timings_t, total_ms) == 20);
static_assert(offsetof(frame_timings_t, kernel_ms) == 24);
static_assert(offsetof(frame_timings_t, kernels_ran) == 60);

/* One frame: everything it reads and writes. */
typedef struct frame_args_t {
    const traverse_world_t *borrowed_world;       /* 0 */
    const shade_scene_t *borrowed_scene;          /* 8 */
    const post_settings_t *borrowed_post;         /* 16 */
    const traverse_camera_t *borrowed_camera;     /* 24 */
    const frame_targets_t *borrowed_targets;      /* 32 */
    uint8_t *borrowed_host_xrgb;                  /* 40: post_xrgb_bytes, 4-aligned: the finished frame comes here */
    const entity_rays_scene_t *borrowed_entities; /* 48: or nullptr for none */
    const lod_scene_t *borrowed_lod;              /* 56: the far field, or nullptr for none */
    const lod_light_t *borrowed_lod_light;        /* 64: with it */
    const post_weather_t *borrowed_weather;       /* 72: rain, snow, lightning and weather fog, or nullptr for none */
    const device_block_t *borrowed_weather_map; /* 80: its rain top, post_weather_map_bytes, or nullptr for open sky */
} frame_args_t;
static_assert(sizeof(frame_args_t) == 88);
static_assert(alignof(frame_args_t) == 8);
static_assert(offsetof(frame_args_t, borrowed_world) == 0);
static_assert(offsetof(frame_args_t, borrowed_scene) == 8);
static_assert(offsetof(frame_args_t, borrowed_post) == 16);
static_assert(offsetof(frame_args_t, borrowed_camera) == 24);
static_assert(offsetof(frame_args_t, borrowed_targets) == 32);
static_assert(offsetof(frame_args_t, borrowed_host_xrgb) == 40);
static_assert(offsetof(frame_args_t, borrowed_entities) == 48);
static_assert(offsetof(frame_args_t, borrowed_lod) == 56);
static_assert(offsetof(frame_args_t, borrowed_lod_light) == 64);
static_assert(offsetof(frame_args_t, borrowed_weather) == 72);
static_assert(offsetof(frame_args_t, borrowed_weather_map) == 80);

/* Plan 008 phase 01: what a frame says past its passes, when it is given somewhere to say it. The events between shade's kernels, the card's
 * clock speed once in a while, and the fresh AO list's length. One thread at a time, as frame_gpu_t. Zeroed means not started, so
 * _deinit is safe either way. */
enum : uint32_t {
    FRAME_CLOCK_CYCLES =
        200000u, /* the clock kernel spins this many of the SM's cycles: 0.1 ms at 2 GHz, 0.33 ms at 600 MHz */
};
typedef struct frame_marks_t {
    void *owned_shade_events[SHADE_SPAN_COUNT - 1u]; /* 0: between shade's kernels: see shade_marks_t */
    void *owned_clock_events[2];                     /* 32: either side of the clock kernel */
    int64_t queued_ns;     /* 48: CLOCK_MONOTONIC, as time_now_ns: the last frame was all queued, and its wait began */
    uint64_t clock_cycles; /* 56: the clock kernel's last reading, in the SM's cycles */
    float32_t
        shade_ms[SHADE_SPAN_COUNT]; /* 64: the last frame's shade kernels, which add up to its FRAME_KERNEL_SHADE */
    uint32_t fresh_count;           /* 84: the last frame's fresh AO pixels; 0 with AO off */
    float32_t card_mhz;             /* 88: from the last clock reading; 0 before one */
    float32_t clock_ms;             /* 92: what that reading took, on the card's timer */
    uint8_t clock_due;              /* 96: set by the caller: the next frame reads the clock first, and clears it */
    uint8_t clock_read;             /* 97: the last frame read the clock */
    uint8_t open;                   /* 98 */
    uint8_t _pad0[5];               /* 99 */
} frame_marks_t;
static_assert(sizeof(frame_marks_t) == 104);
static_assert(alignof(frame_marks_t) == 8);
static_assert(offsetof(frame_marks_t, owned_shade_events) == 0);
static_assert(offsetof(frame_marks_t, owned_clock_events) == 32);
static_assert(offsetof(frame_marks_t, queued_ns) == 48);
static_assert(offsetof(frame_marks_t, clock_cycles) == 56);
static_assert(offsetof(frame_marks_t, shade_ms) == 64);
static_assert(offsetof(frame_marks_t, fresh_count) == 84);
static_assert(offsetof(frame_marks_t, card_mhz) == 88);
static_assert(offsetof(frame_marks_t, clock_ms) == 92);
static_assert(offsetof(frame_marks_t, clock_due) == 96);
static_assert(offsetof(frame_marks_t, clock_read) == 97);
static_assert(offsetof(frame_marks_t, open) == 98);
static_assert(offsetof(frame_marks_t, _pad0) == 99);

gpu_status_t frame_marks_init(frame_marks_t *marks, gpu_error_t *error);
/* Gives every event back whatever happens, and returns the first failure, which error then holds. */
gpu_status_t frame_marks_deinit(frame_marks_t *marks, gpu_error_t *error);

/* The card's clock, as CUDA events. One thread at a time. Zeroed means not started, so _deinit is safe either way. Started means its
 * first event is there. */
typedef struct frame_gpu_t {
    void *owned_events[FRAME_KERNEL_COUNT + 1u]; /* 0: one before each kernel, and one after the last */
    gpu_error_t last_error;                      /* 80: the last failure */
    /* 112: where each frame says more of itself, or nullptr for nowhere. Set after frame_gpu_init, which empties it. */
    frame_marks_t *borrowed_marks;
} frame_gpu_t;
static_assert(sizeof(frame_gpu_t) == 120);
static_assert(alignof(frame_gpu_t) == 8);
static_assert(offsetof(frame_gpu_t, owned_events) == 0);
static_assert(offsetof(frame_gpu_t, last_error) == 80);
static_assert(offsetof(frame_gpu_t, borrowed_marks) == 112);

gpu_status_t frame_gpu_init(frame_gpu_t *frame);
/* Gives every event back whatever happens, and returns the first failure, which last_error then holds. */
gpu_status_t frame_gpu_deinit(frame_gpu_t *frame);

/* Pins host memory the card will copy into, which makes those copies direct and so about twice as fast. Takes nothing: the
 * memory stays whoever's it was, and must outlive the pin. Each pin is given back with frame_host_unpin. */
gpu_status_t frame_host_pin(frame_gpu_t *frame, void *host, uint64_t bytes);
gpu_status_t frame_host_unpin(frame_gpu_t *frame, void *host);

/* Runs one whole frame and waits for it: coarse pass, primary rays and boxes, shading, post with the outline, and the copy to host_xrgb. Anything but
 * GPU_STATUS_OK is in last_error. Nothing here allocates. */
gpu_status_t frame_gpu_run(frame_gpu_t *frame, const frame_args_t *args, frame_timings_t *out_timings);

/* The card's clock speed now, from a one-warp kernel that spins FRAME_CLOCK_CYCLES of clock64(), timed by events, and
 * waits for it. Into marks' card_mhz, clock_ms and clock_cycles. Anything but GPU_STATUS_OK is in error. */
gpu_status_t frame_gpu_clock_mhz(frame_marks_t *marks, gpu_error_t *error);

/* The card's peak clock speed as CUDA states it (cudaDevAttrClockRate), in MHz: what frame_gpu_clock_mhz is held under. */
gpu_status_t frame_gpu_clock_rated(float32_t *out_mhz, gpu_error_t *error);

/* A kernel's name, for a profile. Lasts for good. */
const char *frame_kernel_name(frame_kernel_t kernel);

#ifdef __cplusplus
}
#endif

#endif
