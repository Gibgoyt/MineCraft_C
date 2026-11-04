/* See run/src/gpu_bridge/traverse/README.md and cuda/src/traverse/README.md, which implements this. C API, compiled by GCC
 * and by nvcc: both sides check these static_asserts. */
#ifndef MC_RUN_GPU_BRIDGE_TRAVERSE_INDEX_H
#define MC_RUN_GPU_BRIDGE_TRAVERSE_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum : uint32_t {
    TRAVERSE_COLUMN_BLOCKS = 16u,
    TRAVERSE_COLUMN_SECTIONS = 96u,
    TRAVERSE_HEIGHT_BLOCKS = 1536u, /* a world z of TRAVERSE_Z_BASE is storage height 0 */
    /* One block of threads, one coarse value. Stage 01.3 measured 8x8 best or tied for best of those it tried. */
    TRAVERSE_TILE_PIXELS = 8u,
    /* traverse_hit_t.value: the voxel's material in the low 16 bits, and its light in the 8 above them. */
    TRAVERSE_VALUE_MATERIAL_MASK = 0xFFFFu,
    TRAVERSE_VALUE_LIGHT_SHIFT = 16u,
};
enum : int32_t { TRAVERSE_Z_BASE = -256 };
static_assert((uint32_t)TRAVERSE_COLUMN_SECTIONS == (uint32_t)WORLD_BUFFERS_COLUMN_SECTIONS);
static_assert((int32_t)TRAVERSE_Z_BASE == -(int32_t)WORLD_BUFFERS_Z_ORIGIN);
static_assert((uint32_t)TRAVERSE_HEIGHT_BLOCKS == WORLD_BUFFERS_COLUMN_SECTIONS * 16u);

/* traverse_hit_t.info: bits 0-2 are one of these, bits 3-14 the steps taken (saturating), and bit 15 TRAVERSE_INFO_CAPPED. A
 * face is named by the way it looks: FACE_NEG_X is the side of the voxel towards -x, so its normal is -x. */
typedef enum traverse_face_t : uint16_t {
    TRAVERSE_FACE_NEG_X = 0,
    TRAVERSE_FACE_POS_X = 1,
    TRAVERSE_FACE_NEG_Y = 2,
    TRAVERSE_FACE_POS_Y = 3,
    TRAVERSE_FACE_NEG_Z = 4,
    TRAVERSE_FACE_POS_Z = 5,
    TRAVERSE_FACE_INSIDE = 6, /* the ray started inside this voxel: t is where it started, and no face was crossed */
    TRAVERSE_FACE_MISS = 7,   /* nothing, inside the window */
} traverse_face_t;
static_assert(sizeof(traverse_face_t) == 2);

enum : uint32_t {
    TRAVERSE_INFO_FACE_MASK = 7u,
    TRAVERSE_INFO_STEPS_SHIFT = 3u,
    TRAVERSE_INFO_STEPS_MAX = 4095u,
    TRAVERSE_INFO_CAPPED =
        1u << 15, /* ran out of steps and reports a miss. Never seen, and a test says so: Stage 01.3 issue 3 */
};

/* What the coarse pass writes for a tile no ray of which can hit anything. */
#define TRAVERSE_COARSE_SKY F32(1e30)

/* What a material draws as: a row of the table of boxes in cuda/include/traverse/index.hu. CUBE is the whole voxel, and what a
 * ray takes for anything with no row. Boxes are inside their voxel, so the tree's masks and the coarse pass never need to know. */
typedef enum traverse_shape_t : uint8_t {
    TRAVERSE_SHAPE_CUBE = 0,
    /* These first have a body: from inside one a ray says TRAVERSE_FACE_INSIDE. From CROSS on, it sees on past what it is in. */
    TRAVERSE_SHAPE_SLAB = 1,   /* the lower half */
    TRAVERSE_SHAPE_STAIRS = 2, /* the lower half, and the upper half towards +y */
    TRAVERSE_SHAPE_CROSS = 3,  /* a plant: two thin plates through the middle, one along each of x and y */
    TRAVERSE_SHAPE_CROP = 4,   /* a crop: four thin plates, two along each */
    TRAVERSE_SHAPE_TORCH = 5,  /* a stick in the middle, standing on the floor */
    TRAVERSE_SHAPE_PLATE = 6,  /* a thin plate against the +y side: a ladder, a vine */
    TRAVERSE_SHAPE_FLAT = 7,   /* a thin plate on the floor: a lily pad */
    TRAVERSE_SHAPE_COUNT = 8,
} traverse_shape_t;
static_assert(sizeof(traverse_shape_t) == 1);

/* The world as kernels see it, in the form gpu_bridge/world_buffers/index.h defines and run/src/render/encode/ writes: one
 * format, whatever Stage 08.3 and Stage 08.2 each started with (Stage 08.4 issue 1). The blocks are borrowed from whoever holds
 * them, and read only: they must not be freed or written while a kernel using them may still run. A column x, y in the window
 * lives where the window says. A slot may still hold the column that wrapped out of it, with other coordinates: that one reads
 * as empty, so what wrapped cannot be seen twice, as can nothing past the window, where rays are clipped. With no shapes, every
 * voxel is a whole cube: which is the setting's off, and what a world of bare materials means. */
typedef struct traverse_world_t {
    const device_block_t *borrowed_table;  /* 0: window.slot_count world_buffers_column_t */
    const device_block_t *borrowed_pool;   /* 8: where the sections are */
    world_buffers_window_t window;         /* 16 */
    const device_block_t *borrowed_shapes; /* 40: shape_count traverse_shape_t, by material, or nullptr */
    uint32_t shape_count;                  /* 48 */
    /* 52: as a storage height, no block in any column of the window is at or above this, so a ray rising past it has met all it will
     * meet, without asking each column it crosses. 0 for not known. It may be high, and never low. */
    uint32_t top_limit;
} traverse_world_t;
static_assert(sizeof(traverse_world_t) == 56);
static_assert(alignof(traverse_world_t) == 8);
static_assert(offsetof(traverse_world_t, borrowed_table) == 0);
static_assert(offsetof(traverse_world_t, borrowed_pool) == 8);
static_assert(offsetof(traverse_world_t, window) == 16);
static_assert(offsetof(traverse_world_t, borrowed_shapes) == 40);
static_assert(offsetof(traverse_world_t, shape_count) == 48);
static_assert(offsetof(traverse_world_t, top_limit) == 52);

/* The eye is a whole block and a fraction, so that a ray is computed relative to the eye and is as exact at x = 10000000 as at
 * the origin. width and height are the size being rendered, which may be below the display's: Stage 01.3's fallback. For
 * pixel (px, py) the ray is forward + u * right + v * up, normalized, with u = 2 (px + 0.5) / width - 1 and
 * v = 1 - 2 (py + 0.5) / height. So right and up carry the field of view in their length. */
typedef struct traverse_camera_t {
    int32_t eye_cell[3];   /* 0: world x, y, z */
    float32_t eye_frac[3]; /* 12: from the cell's low corner; 0 to 1 is the sensible range */
    float32_t forward[3];  /* 24 */
    float32_t right[3];    /* 36 */
    float32_t up[3];       /* 48 */
    uint32_t width;        /* 60 */
    uint32_t height;       /* 64 */
} traverse_camera_t;
static_assert(sizeof(traverse_camera_t) == 68);
static_assert(alignof(traverse_camera_t) == 4);
static_assert(offsetof(traverse_camera_t, eye_cell) == 0);
static_assert(offsetof(traverse_camera_t, eye_frac) == 12);
static_assert(offsetof(traverse_camera_t, forward) == 24);
static_assert(offsetof(traverse_camera_t, right) == 36);
static_assert(offsetof(traverse_camera_t, up) == 48);
static_assert(offsetof(traverse_camera_t, width) == 60);
static_assert(offsetof(traverse_camera_t, height) == 64);

/* A ray given by hand: for a test, and for anything that is not a frame. direction need not be unit length; t is measured in
 * blocks along it once normalized. */
typedef struct traverse_ray_t {
    int32_t eye_cell[3];    /* 0 */
    float32_t eye_frac[3];  /* 12 */
    float32_t direction[3]; /* 24 */
    float32_t t_start;      /* 36: nothing before this is looked at */
} traverse_ray_t;
static_assert(sizeof(traverse_ray_t) == 40);
static_assert(alignof(traverse_ray_t) == 4);
static_assert(offsetof(traverse_ray_t, eye_cell) == 0);
static_assert(offsetof(traverse_ray_t, eye_frac) == 12);
static_assert(offsetof(traverse_ray_t, direction) == 24);
static_assert(offsetof(traverse_ray_t, t_start) == 36);

/* What a ray found, one per pixel. 16 bytes, so a 1080p frame's hits are 33 MiB and shading touches few cache lines. */
typedef struct traverse_hit_t {
    float32_t t;       /* 0: blocks from the eye to where the ray enters the voxel, or the voxel's box */
    uint32_t value;    /* 4: the voxel's material and light: TRAVERSE_VALUE_* */
    int16_t offset[3]; /* 8: the voxel, from eye_cell. int16_t is enough: the widest window is 1025 columns */
    uint16_t info;     /* 14: see traverse_face_t */
} traverse_hit_t;
static_assert(sizeof(traverse_hit_t) == 16);
static_assert(alignof(traverse_hit_t) == 4);
static_assert(offsetof(traverse_hit_t, t) == 0);
static_assert(offsetof(traverse_hit_t, value) == 4);
static_assert(offsetof(traverse_hit_t, offset) == 8);
static_assert(offsetof(traverse_hit_t, info) == 14);

/* Where a frame's passes write, borrowed from whoever holds the blocks: DEVICE_POOL_FRAMES is the pool for them. Sizes come
 * from traverse_coarse_bytes and traverse_hits_bytes, for the largest size to be rendered into them. */
typedef struct traverse_targets_t {
    const device_block_t *borrowed_coarse; /* 0: one float32_t per tile, row by row */
    const device_block_t *borrowed_hits;   /* 8: one traverse_hit_t per pixel, row by row */
} traverse_targets_t;
static_assert(sizeof(traverse_targets_t) == 16);
static_assert(alignof(traverse_targets_t) == 8);
static_assert(offsetof(traverse_targets_t, borrowed_coarse) == 0);
static_assert(offsetof(traverse_targets_t, borrowed_hits) == 8);

static inline uint32_t traverse_hit_face(uint16_t info) {
    return (uint32_t)info & TRAVERSE_INFO_FACE_MASK;
}

static inline uint32_t traverse_hit_steps(uint16_t info) {
    return ((uint32_t)info >> TRAVERSE_INFO_STEPS_SHIFT) & TRAVERSE_INFO_STEPS_MAX;
}

static inline uint32_t traverse_tiles_across(uint32_t pixels) {
    return (pixels + TRAVERSE_TILE_PIXELS - 1u) / TRAVERSE_TILE_PIXELS;
}

static inline uint64_t traverse_coarse_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)traverse_tiles_across(width) * traverse_tiles_across(height) * sizeof(float32_t);
}

static inline uint64_t traverse_hits_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * sizeof(traverse_hit_t);
}

/* The coarse pass: for each tile, a distance no ray of that tile can hit anything before, or TRAVERSE_COARSE_SKY. Conservative,
 * so a tile near something says 0 and costs nothing but its own work. Starts a kernel and returns; it runs in order with any
 * other, and traverse_wait waits for all of them. Nothing here allocates. On failure error says why, and nothing was started. */
gpu_status_t traverse_coarse(gpu_error_t *error, const traverse_world_t *world, const traverse_camera_t *camera,
                             const traverse_targets_t *targets);

/* Primary rays, one per pixel, into targets' hits: hierarchical DDA through the tree. With use_coarse each ray starts where
 * traverse_coarse said for its tile, so the coarse pass of the same camera must have been started first. Starts a kernel, as
 * above. */
gpu_status_t traverse_primary(gpu_error_t *error, const traverse_world_t *world, const traverse_camera_t *camera,
                              const traverse_targets_t *targets, bool use_coarse);

/* The same traversal over ray_count rays in rays, into hits, in order. Starts a kernel, as above. */
gpu_status_t traverse_rays(gpu_error_t *error, const traverse_world_t *world, const device_block_t *rays,
                           const device_block_t *hits, uint32_t ray_count);

/* Waits until everything started has finished, and reports a failure of any of it. */
gpu_status_t traverse_wait(gpu_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
