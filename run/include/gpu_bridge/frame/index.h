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

/* Plan 008 phase 01: what a frame says past its passes, when it is given somewhere to say it. Shade's kernels each on the card's clock,
 * the card's clock speed once in a while, and the fresh AO list's length. The events these are read from are the frame's own (frame_slot_t),
 * as two frames may be on their way at once; the two here are for frame_gpu_clock_mhz alone. One thread at a time, as frame_gpu_t. Zeroed
 * means not started, so _deinit is safe either way. */
enum : uint32_t {
    FRAME_CLOCK_CYCLES =
        200000u, /* the clock kernel spins this many of the SM's cycles: 0.1 ms at 2 GHz, 0.33 ms at 600 MHz */
};
typedef struct frame_marks_t {
    void *owned_clock_events[2]; /* 0: either side of frame_gpu_clock_mhz's clock kernel */
    int64_t
        queued_ns; /* 16: CLOCK_MONOTONIC, as time_now_ns: the last finished frame was all queued, and its wait began */
    uint64_t clock_cycles; /* 24: the clock kernel's last reading, in the SM's cycles */
    float32_t shade_ms
        [SHADE_SPAN_COUNT]; /* 32: the last finished frame's shade kernels, which add up to its FRAME_KERNEL_SHADE */
    uint32_t fresh_count;   /* 52: the last finished frame's fresh AO pixels; 0 with AO off */
    float32_t card_mhz;     /* 56: from the last clock reading; 0 before one */
    float32_t clock_ms;     /* 60: what that reading took, on the card's timer */
    uint8_t clock_due;      /* 64: set by the caller: the next frame queued reads the clock first, and clears it */
    uint8_t clock_read;     /* 65: the last finished frame read the clock */
    uint8_t open;           /* 66 */
    uint8_t _pad0[5];       /* 67 */
    /* 72: plan 009 phase 01: set by a bench, never by play: a counts block (see traverse_counts_t) for a picture of the size being drawn,
     * traverse_counts_bytes of its pixels at least. Each frame queued with the marks then runs the counted copies of the ray kernels, which
     * add every ray's steps into it, and the picture is the same bits. nullptr counts nothing. */
    const device_block_t *borrowed_counts;
} frame_marks_t;
static_assert(sizeof(frame_marks_t) == 80);
static_assert(alignof(frame_marks_t) == 8);
static_assert(offsetof(frame_marks_t, owned_clock_events) == 0);
static_assert(offsetof(frame_marks_t, queued_ns) == 16);
static_assert(offsetof(frame_marks_t, clock_cycles) == 24);
static_assert(offsetof(frame_marks_t, shade_ms) == 32);
static_assert(offsetof(frame_marks_t, fresh_count) == 52);
static_assert(offsetof(frame_marks_t, card_mhz) == 56);
static_assert(offsetof(frame_marks_t, clock_ms) == 60);
static_assert(offsetof(frame_marks_t, clock_due) == 64);
static_assert(offsetof(frame_marks_t, clock_read) == 65);
static_assert(offsetof(frame_marks_t, open) == 66);
static_assert(offsetof(frame_marks_t, _pad0) == 67);
static_assert(offsetof(frame_marks_t, borrowed_counts) == 72);

gpu_status_t frame_marks_init(frame_marks_t *marks, gpu_error_t *error);
/* Gives every event back whatever happens, and returns the first failure, which error then holds. */
gpu_status_t frame_marks_deinit(frame_marks_t *marks, gpu_error_t *error);

/* Plan 008 phase 03: frames on their way. A frame is queued (its kernels, behind its copies, on the frame's stream), its picture sent home
 * (on the copy stream, once its post is done), and finished (waited for, and read). Up to FRAME_FLIGHT_SLOTS of them at once, each in a
 * slot of its own, in turn. */
enum : uint32_t {
    FRAME_FLIGHT_SLOTS = 2u,
    FRAME_FLIGHT_ALIGNMENT = 4096u, /* frame_flight_t is pinned, so it has its pages to itself */
};

/* Where a slot is. */
typedef enum frame_slot_state_t : uint8_t {
    FRAME_SLOT_FREE = 0,   /* finished, or never used */
    FRAME_SLOT_QUEUED = 1, /* its kernels are queued; its picture not yet sent home */
    FRAME_SLOT_HOME = 2,   /* its copy home is queued too: frame_gpu_finish may wait for it */
} frame_slot_state_t;

/* One frame on its way. owned_events[FRAME_KERNEL_READBACK] is recorded on the frame's stream once post and the outline are done: the
 * picture is whole, and the copy stream waits for it. owned_events[FRAME_KERNEL_COUNT] is recorded on the copy stream after the copy home,
 * so the readback's time is still from one to the other. owned_end is the last thing on the frame's stream: the small copies home are done,
 * and whatever staging the frame read is free again. */
typedef struct frame_slot_t {
    void *owned_events[FRAME_KERNEL_COUNT + 1u];     /* 0: one before each kernel, and one after the copy home */
    void *owned_shade_events[SHADE_SPAN_COUNT - 1u]; /* 80: between shade's kernels: see shade_marks_t */
    void *owned_clock_events[2];          /* 112: either side of the clock kernel, when this frame reads it */
    void *owned_end;                      /* 128 */
    const uint32_t *borrowed_fresh_count; /* 136: on the card: the fresh AO list's length, or nullptr */
    const device_block_t *borrowed_xrgb;  /* 144: the device picture it writes, which goes home */
    int64_t queued_ns;                    /* 152: CLOCK_MONOTONIC: this frame was all queued */
    uint64_t
        clock_seen; /* 160: the clock kernel's cycles, copied here on the frame's stream (frame_flight_t is pinned) */
    uint32_t fresh_seen; /* 168: the fresh list's length, the same */
    uint32_t ran;        /* 172: a bit for each frame_kernel_t this frame ran */
    uint32_t width;      /* 176: what it drew */
    uint32_t height;     /* 180 */
    uint8_t state;       /* 184: frame_slot_state_t */
    uint8_t marked;      /* 185: queued with marks */
    uint8_t clock;       /* 186: it reads the clock */
    uint8_t _pad0[5];    /* 187 */
} frame_slot_t;
static_assert(sizeof(frame_slot_t) == 192);
static_assert(alignof(frame_slot_t) == 8);
static_assert(offsetof(frame_slot_t, owned_events) == 0);
static_assert(offsetof(frame_slot_t, owned_shade_events) == 80);
static_assert(offsetof(frame_slot_t, owned_clock_events) == 112);
static_assert(offsetof(frame_slot_t, owned_end) == 128);
static_assert(offsetof(frame_slot_t, borrowed_fresh_count) == 136);
static_assert(offsetof(frame_slot_t, borrowed_xrgb) == 144);
static_assert(offsetof(frame_slot_t, queued_ns) == 152);
static_assert(offsetof(frame_slot_t, clock_seen) == 160);
static_assert(offsetof(frame_slot_t, fresh_seen) == 168);
static_assert(offsetof(frame_slot_t, ran) == 172);
static_assert(offsetof(frame_slot_t, width) == 176);
static_assert(offsetof(frame_slot_t, height) == 180);
static_assert(offsetof(frame_slot_t, state) == 184);
static_assert(offsetof(frame_slot_t, marked) == 185);
static_assert(offsetof(frame_slot_t, clock) == 186);
static_assert(offsetof(frame_slot_t, _pad0) == 187);

/* The frames on their way, in host memory the caller gives frame_gpu_init: FRAME_FLIGHT_ALIGNMENT-aligned, frame_flight_bytes of it. It is
 * pinned there, so the card can copy a slot's few words home without the thread waiting; the events in it are frame_gpu_t's. */
typedef struct frame_flight_t {
    frame_slot_t slots[FRAME_FLIGHT_SLOTS]; /* 0 */
    uint32_t next;                          /* 384: the slot the next frame is queued in */
    uint8_t _pad0[4];                       /* 388 */
} frame_flight_t;
static_assert(sizeof(frame_flight_t) == 392);
static_assert(alignof(frame_flight_t) == 8);
static_assert(offsetof(frame_flight_t, slots) == 0);
static_assert(offsetof(frame_flight_t, next) == 384);
static_assert(offsetof(frame_flight_t, _pad0) == 388);

/* What frame_gpu_init takes for the flight: whole pages. */
uint64_t frame_flight_bytes(void);

/* The card's clock, as CUDA events, and the frames on their way. One thread at a time. Zeroed means not started, so _deinit is safe
 * either way. Started means it has its flight. */
typedef struct frame_gpu_t {
    frame_flight_t *borrowed_flight; /* 0: the caller's memory, pinned by init; the events in it are owned here */
    void *owned_copy_stream;  /* 8: cudaStreamNonBlocking: only the pictures' copies home, behind a frame's post */
    device_staging_t staging; /* 16: the per-frame copies' areas, one for each slot, turned as each frame is queued */
    gpu_error_t last_error;   /* 80: the last failure */
    /* 112: where each frame says more of itself, or nullptr for nowhere. Set after frame_gpu_init, which empties it. */
    frame_marks_t *borrowed_marks;
} frame_gpu_t;
static_assert(sizeof(frame_gpu_t) == 120);
static_assert(alignof(frame_gpu_t) == 8);
static_assert(offsetof(frame_gpu_t, borrowed_flight) == 0);
static_assert(offsetof(frame_gpu_t, owned_copy_stream) == 8);
static_assert(offsetof(frame_gpu_t, staging) == 16);
static_assert(offsetof(frame_gpu_t, last_error) == 80);
static_assert(offsetof(frame_gpu_t, borrowed_marks) == 112);

/* Makes the events and the copy stream, and pins flight (frame_flight_bytes, FRAME_FLIGHT_ALIGNMENT-aligned) and staging (two areas of
 * staging_bytes each, one after the other, on whole pages; or nullptr and 0 for copies that wait). Both are the caller's and must outlive
 * it. On failure last_error says why, and deinit gives back what was made. */
gpu_status_t frame_gpu_init(frame_gpu_t *frame, void *flight, uint8_t *staging, uint64_t staging_bytes);
/* Waits for any frame still on its way, then gives every event, the stream and the pins back whatever happens, and returns the first
 * failure, which last_error then holds. */
gpu_status_t frame_gpu_deinit(frame_gpu_t *frame);

/* Pins host memory the card will copy into, which makes those copies direct and so about twice as fast. Takes nothing: the
 * memory stays whoever's it was, and must outlive the pin. Each pin is given back with frame_host_unpin. */
gpu_status_t frame_host_pin(frame_gpu_t *frame, void *host, uint64_t bytes);
gpu_status_t frame_host_unpin(frame_gpu_t *frame, void *host);

/* Plan 008 phase 03. Queues one frame's kernels on the frame's stream (coarse pass, primary rays and boxes, shading, post with the
 * outline), behind every copy queued for it, and returns at once with the slot it is in. args->borrowed_host_xrgb is not read: the
 * picture goes home with frame_gpu_home. The next slot must be free: at most FRAME_FLIGHT_SLOTS frames on their way. Anything but
 * GPU_STATUS_OK is in last_error. */
gpu_status_t frame_gpu_queue(frame_gpu_t *frame, const frame_args_t *args, uint32_t *out_slot);

/* Queues the copy of a queued frame's picture to host_xrgb (post_xrgb_bytes, pinned), on the copy stream, once the frame's post is done:
 * so it overlaps the next frame's first passes. host_xrgb must not be read until frame_gpu_finish of this slot. */
gpu_status_t frame_gpu_home(frame_gpu_t *frame, uint32_t slot, uint8_t *host_xrgb);

/* Waits for a frame sent home: its picture is then in host_xrgb. Reads each kernel's time, and the marks if it was queued with them. A
 * kernel that faulted, in this frame or one before it, is reported here, with what CUDA says, as "frame: waiting for the passes". The slot is
 * free again either way. */
gpu_status_t frame_gpu_finish(frame_gpu_t *frame, uint32_t slot, frame_timings_t *out_timings);

/* Whether a frame sent home is done, picture and all, without waiting: frame_gpu_finish of it would not wait. False for a slot not sent
 * home. A failure reads as done, so the finish reports it. */
bool frame_gpu_done(frame_gpu_t *frame, uint32_t slot);

/* Runs one whole frame and waits for it: frame_gpu_queue, frame_gpu_home to args->borrowed_host_xrgb, and frame_gpu_finish. As before
 * plan 008 phase 03, for --serial-frames and every caller that wants its picture now. Anything but GPU_STATUS_OK is in last_error. Nothing
 * here allocates. */
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
