/* See run/src/render/frame/README.md. */
#ifndef MC_RUN_RENDER_FRAME_INDEX_H
#define MC_RUN_RENDER_FRAME_INDEX_H

#include "arena/index.h"
#include "display/flip/index.h"
#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/frame/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "machine/index.h"
#include "texture/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* One being read, one newest and waiting, one being written, so neither side ever waits for the other. */
    RENDER_EXCHANGE_SLOTS = 3u,
    /* Frames whose time is kept for the percentiles. Past this the oldest go. */
    RENDER_FRAME_SAMPLES = 1u << 16,
};

/* A triple buffer's numbers, from one writing thread to one reading thread, with no locks and no syscalls: the same idea as
 * world_edit_snapshot_t. Whoever uses it holds the three slots. Each side has its own cache line. */
typedef struct render_exchange_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) uint32_t back; /* 0: the writer's */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 4u];
    _Atomic uint32_t exchange; /* 64: a slot's number, and RENDER_EXCHANGE_FRESH if it is waiting unread */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 4u];
    uint32_t front; /* 128: the reader's */
    uint8_t _pad2[MC_CPU_CACHE_LINE_BYTES - 4u];
} render_exchange_t;
static_assert(sizeof(render_exchange_t) == 3u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_exchange_t, exchange) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_exchange_t, front) == 2u * MC_CPU_CACHE_LINE_BYTES);

/* Call before either thread starts. */
void render_exchange_start(render_exchange_t *exchange);

/* The writer: the slot just filled becomes the newest, and this is the next to fill. A slot the reader never saw is reused. */
uint32_t render_exchange_publish(render_exchange_t *exchange);

/* The reader: the newest slot not yet read, or false if there is none. Its slot stays whole until this next returns true. */
bool render_exchange_take(render_exchange_t *exchange, uint32_t *out_slot);

/* Whether the slot last published has not been taken yet. Either thread. */
bool render_exchange_waiting(const render_exchange_t *exchange);

/* Where the eye is and how it looks. x is east, y north and z up, a yaw of 0 looks east and a positive one turns towards north,
 * and a positive pitch looks up. In float64_t, so that it stays exact as far out as the world goes. */
typedef struct render_camera_t {
    float64_t position[3];      /* 0 */
    float32_t yaw;              /* 24: radians */
    float32_t pitch;            /* 28: radians */
    float32_t half_fov_tangent; /* 32: of half the vertical field of view */
    float32_t speed;            /* 36: blocks per second */
} render_camera_t;
static_assert(sizeof(render_camera_t) == 40);

/* What moves a free-fly camera for one step, wherever it came from: a script, or real keys and a real mouse. */
typedef struct render_fly_input_t {
    int64_t mouse_dx; /* 0: counts, right is positive */
    int64_t mouse_dy; /* 8: counts, towards the player is positive */
    int64_t wheel;    /* 16: steps, away from the player is positive: each makes the camera faster */
    int8_t forward;   /* 24: 1 forward, -1 back */
    int8_t right;     /* 25 */
    int8_t up;        /* 26 */
    uint8_t fast;     /* 27: go at RENDER_CAMERA_FAST_FACTOR times the speed */
    uint8_t _pad0[4]; /* 28 */
} render_fly_input_t;
static_assert(sizeof(render_fly_input_t) == 32);

#define RENDER_CAMERA_FAST_FACTOR F32(5.0)
#define RENDER_CAMERA_SPEED_DEFAULT F32(20.0)
#define RENDER_CAMERA_SPEED_STEP F32(1.25)
#define RENDER_CAMERA_SPEED_MAX F32(2000.0)
#define RENDER_CAMERA_SPEED_MIN F32(0.5)
#define RENDER_CAMERA_RADIANS_PER_COUNT F32(0.0025)
/* Looking straight up or down leaves no sideways. */
#define RENDER_CAMERA_PITCH_LIMIT F32(1.553)
/* The tangent of half of a vertical field of view of 70 degrees. */
#define RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT F32(0.7002)

void render_camera_start(render_camera_t *camera, float64_t x, float64_t y, float64_t z, float32_t yaw,
                         float32_t pitch);

/* One step of flying: looks first, then moves along where it now looks, level, and up and down along z. */
void render_camera_fly(render_camera_t *camera, const render_fly_input_t *input, float32_t seconds);

/* What the kernels take, for a picture of this size. */
void render_camera_to_traverse(const render_camera_t *camera, uint32_t width, uint32_t height, traverse_camera_t *out);

/* What the frames took, on the card's clock, since render_frame_init. */
typedef struct render_frame_stats_t {
    uint64_t frames;                    /* 0 */
    uint64_t readback_bytes;            /* 8 */
    float64_t sum_ms[FRAME_PASS_COUNT]; /* 16 */
    float64_t sum_total_ms;             /* 56 */
    float32_t max_ms[FRAME_PASS_COUNT]; /* 64 */
    float32_t max_total_ms;             /* 84 */
} render_frame_stats_t;
static_assert(sizeof(render_frame_stats_t) == 88);

/* The device blocks a frame needs, which the Owner holds. */
typedef struct render_frame_blocks_t {
    const device_block_t *borrowed_textures; /* 0: render_frame_texture_table_bytes */
    const device_block_t *borrowed_faces;    /* 8: render_frame_face_table_bytes */
    const device_block_t *borrowed_pixels;   /* 16: all of texture_pixels_get */
    const device_block_t *borrowed_coarse;   /* 24 */
    const device_block_t *borrowed_hits;     /* 32 */
    const device_block_t *borrowed_shaded;   /* 40 */
    const device_block_t *borrowed_xrgb;     /* 48 */
} render_frame_blocks_t;
static_assert(sizeof(render_frame_blocks_t) == 56);

/* One picture's way from the card to the screen. The gpu thread draws, and the main thread takes and presents. */
typedef struct render_frame_t {
    render_exchange_t pictures;                        /* 0 */
    frame_gpu_t gpu;                                   /* 192 */
    frame_targets_t targets;                           /* 280 */
    shade_scene_t scene;                               /* 312 */
    post_settings_t post;                              /* 344 */
    render_frame_stats_t stats;                        /* 376 */
    device_memory_t *borrowed_memory;                  /* 464 */
    uint8_t *borrowed_pictures[RENDER_EXCHANGE_SLOTS]; /* 472: width * height * 4 each */
    float32_t *borrowed_samples;                       /* 496: RENDER_FRAME_SAMPLES of total_ms */
    uint64_t picture_bytes;                            /* 504 */
    uint32_t width;                                    /* 512 */
    uint32_t height;                                   /* 516 */
    uint8_t pinned;                                    /* 520: how many of the pictures are pinned */
    uint8_t _pad0[55];                                 /* 521 */
} render_frame_t;
static_assert(sizeof(render_frame_t) == 576);
static_assert(alignof(render_frame_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_frame_t, gpu) == 192);
static_assert(offsetof(render_frame_t, borrowed_memory) == 464);
static_assert(offsetof(render_frame_t, pinned) == 520);

/* Sizes, for the Owner to take blocks and memory. */
uint64_t render_frame_texture_table_bytes(void);
uint64_t render_frame_face_table_bytes(void);
uint64_t render_frame_arena_bytes(uint32_t width, uint32_t height);

/* Builds the tables from the block table and the texture blob and sends them and every texture's pixels to the card, takes the
 * pictures from the arena, and pins them. fog_end is in blocks. All of it must outlive this. On failure logs why, leaves
 * nothing pinned, and returns what went wrong. */
gpu_status_t render_frame_init(render_frame_t *frame, arena_t *arena, device_memory_t *memory, const texture_t *texture,
                               const render_frame_blocks_t *blocks, uint32_t width, uint32_t height, float32_t fog_end);

/* Unpins, and forgets the rest. Safe on zero bytes. No kernel may be running. */
void render_frame_deinit(render_frame_t *frame);

/* The gpu thread. Draws one whole frame of what buffers hold now, from this camera, and makes it the newest picture. Waits for
 * the card. Allocates nothing. Anything but GPU_STATUS_OK means nothing more should be asked of the card, and gpu.last_error
 * says why. */
gpu_status_t render_frame_draw(render_frame_t *frame, const world_buffers_t *buffers, const render_camera_t *camera);

/* The main thread. The newest picture not taken yet, or nullptr if there is none. It stays whole until this next returns one. */
const uint8_t *render_frame_take(render_frame_t *frame);

/* The main thread. Copies a picture to a buffer of the same size, which may be scanout memory, with a fence after it. */
void render_frame_present(const render_frame_t *frame, const uint8_t *picture, display_buffer_t *target);

/* What the frames took, as a log line for each pass, with percentiles. Reorders the samples, so for the end of a run. */
void render_frame_report(render_frame_t *frame);

/* The frame's picture reduced to what a person would call its time: the p50 and p99 of total_ms, for tests and logs. Reorders
 * the samples. */
void render_frame_percentiles(render_frame_t *frame, float32_t *out_p50, float32_t *out_p99);

#endif
