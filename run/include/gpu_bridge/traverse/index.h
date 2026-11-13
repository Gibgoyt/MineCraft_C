/* See run/src/gpu_bridge/traverse/README.md and cuda/src/traverse/README.md, which implements this. C API, compiled by GCC
 * and by nvcc: both sides check these static_asserts. */
#ifndef MC_RUN_GPU_BRIDGE_TRAVERSE_INDEX_H
#define MC_RUN_GPU_BRIDGE_TRAVERSE_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "types/block_state/index.h"
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
    /* traverse_stops_t: the materials it has a bit for. One past them never stops a ray that asks. */
    TRAVERSE_STOPS_MATERIALS = 512u,
    TRAVERSE_STOPS_WORDS = TRAVERSE_STOPS_MATERIALS / 32u,
    /* traverse_hit_t.value: the voxel in the low 16 bits, and its light in the 8 above them. The voxel is the block id, its
     * material, and the block's state, as types/block_state/ lays them out: a shape and its pictures turn by the facing. */
    TRAVERSE_VALUE_VOXEL_MASK = 0xFFFFu,
    TRAVERSE_VALUE_MATERIAL_MASK = BLOCK_STATE_ID_MASK,
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
    TRAVERSE_FACE_MISS =
        7, /* nothing, inside the window: t is TRAVERSE_COARSE_SKY, or a ray's t_end for TRAVERSE_FACE_PAST */
} traverse_face_t;
/* Plan 009 phase 04: a ray that reached its t_end with nothing met says a miss whose t is that t_end, as three bits hold no ninth face. So
 * every reader that goes on past a miss goes on past this too, to the next level from there: traverse_hit_past tells the two apart. */
#define TRAVERSE_FACE_PAST TRAVERSE_FACE_MISS
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
/* Plan 009 phase 04: a ray's t_end when it has none. No t a ray steps through is ever this large. */
#define TRAVERSE_T_END_NONE F32(1e30)

/* What a material draws as: a row of the table of boxes in cuda/include/traverse/index.hu. CUBE is the whole voxel, and what a
 * ray takes for anything with no row. Boxes are inside their voxel, so the tree's masks and the coarse pass never need to know.
 * Each is as it faces north, and a voxel's facing turns it about the middle of the voxel: block_facing_box_turn. */
typedef enum traverse_shape_t : uint8_t {
    TRAVERSE_SHAPE_CUBE = 0,
    /* These first have a body: from inside one a ray says TRAVERSE_FACE_INSIDE. From CROSS on, it sees on past what it is in. */
    TRAVERSE_SHAPE_SLAB = 1,   /* the lower half */
    TRAVERSE_SHAPE_STAIRS = 2, /* the lower half, and the upper half towards +y, the way they face */
    TRAVERSE_SHAPE_CROSS = 3,  /* a plant: two thin plates through the middle, one along each of x and y */
    TRAVERSE_SHAPE_CROP = 4,   /* a crop: four thin plates, two along each */
    TRAVERSE_SHAPE_TORCH = 5,  /* a stick in the middle, standing on the floor */
    TRAVERSE_SHAPE_PLATE = 6,  /* a thin plate against the +y side, the way it faces: a ladder, a vine */
    TRAVERSE_SHAPE_FLAT = 7,   /* a thin plate on the floor: a lily pad */
    /* A torch on a wall, stepping out from it: its wall on the +y side of the voxel, the way it faces, so it leans out to -y. */
    TRAVERSE_SHAPE_TORCH_WALL = 8,
    TRAVERSE_SHAPE_LAYER = 9, /* an eighth of a block on the floor: the census's snow layer (plan 004 phase 08) */
    /* A door's plate, three sixteenths thick, against the +y side as it faces north; an open door is drawn by its facing turned a
     * quarter anticlockwise: block_state_drawn_facing (plan 005 phase 2). */
    TRAVERSE_SHAPE_DOOR = 10,
    TRAVERSE_SHAPE_COUNT = 11,
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
    /* 56: plan 009 phase 04, how far along each pixel's ray this world is used, by the bands: level k, of voxels of 2^k blocks, starts at
     * d_k = band_base 2^k blocks, never nearer than the near guarantee, so this world (level 0) ends at a pixel's d_1. band_base is
     * 1 / (2 E theta), theta = 2 tan(fov / 2) / the picture's rows. 0 for no bands: this world to its edge, as with no far field. */
    float32_t band_base;
    float32_t
        band_dither; /* 60: a pixel's edges all move by up to this share of themselves, by a hash of it: 0 for none */
    uint32_t
        band_seed; /* 64: hashed with the pixel: the frame with TAA on, 0 with it off, so that one picture comes out the same */
    /* 68: plan 009 phase 05: 1 for traverse_primary and traverse_rays without that phase's merged boxes, the loop a test holds every hit
     * of the real one to, bit for bit. 0 in the game. */
    uint8_t plain;
    uint8_t _pad0[3]; /* 69 */
} traverse_world_t;
static_assert(sizeof(traverse_world_t) == 72);
static_assert(alignof(traverse_world_t) == 8);
static_assert(offsetof(traverse_world_t, borrowed_table) == 0);
static_assert(offsetof(traverse_world_t, borrowed_pool) == 8);
static_assert(offsetof(traverse_world_t, window) == 16);
static_assert(offsetof(traverse_world_t, borrowed_shapes) == 40);
static_assert(offsetof(traverse_world_t, shape_count) == 48);
static_assert(offsetof(traverse_world_t, top_limit) == 52);
static_assert(offsetof(traverse_world_t, band_base) == 56);
static_assert(offsetof(traverse_world_t, band_dither) == 60);
static_assert(offsetof(traverse_world_t, band_seed) == 64);
static_assert(offsetof(traverse_world_t, plain) == 68);
static_assert(offsetof(traverse_world_t, _pad0) == 69);

/* A bit per material, material m at bit m % 32 of words[m / 32]: set for one that stops a ray asking only where the first thing is
 * that it cannot see through. A kernel takes it by value. */
typedef struct traverse_stops_t {
    uint32_t words[TRAVERSE_STOPS_WORDS]; /* 0 */
} traverse_stops_t;
static_assert(sizeof(traverse_stops_t) == 64);
static_assert(alignof(traverse_stops_t) == 4);
static_assert(offsetof(traverse_stops_t, words) == 0);

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
    float32_t
        t_end; /* 40: plan 009 phase 04: nor anything at or past this, where it says TRAVERSE_FACE_PAST; 0 for no end */
} traverse_ray_t;
static_assert(sizeof(traverse_ray_t) == 44);
static_assert(alignof(traverse_ray_t) == 4);
static_assert(offsetof(traverse_ray_t, eye_cell) == 0);
static_assert(offsetof(traverse_ray_t, eye_frac) == 12);
static_assert(offsetof(traverse_ray_t, direction) == 24);
static_assert(offsetof(traverse_ray_t, t_start) == 36);
static_assert(offsetof(traverse_ray_t, t_end) == 40);

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

/* Plan 009 phase 01: where a picture's steps go. A bench gives the frame a block of the card holding one of these and, after it,
 * TRAVERSE_COUNT_PLANES planes of a uint16_t per pixel; the counted kernels add every traversal's steps into it. Play never gives one,
 * and then the kernels it runs are the same code as before (the counted ones are second copies). A step is one pass of the loop in
 * cuda/include/traverse/index.hu, and its band is where along its own ray it began, in blocks: a primary or far ray's from the eye, a sun,
 * AO or reflection ray's from the surface it left. A "call" is one traversal: a sun ray through three leaves is four. */
enum : uint32_t {
    TRAVERSE_COUNT_KINDS = 8u,
    /* 0-16, 16-64, 64-128, 128-256, 256-512, 512-1024 and past 1024 blocks */
    TRAVERSE_COUNT_BANDS = 7u,
    TRAVERSE_COUNT_LEVELS = 8u,   /* the far field's levels, at least LOD_LEVELS_MAX */
    TRAVERSE_COUNT_BUCKETS = 64u, /* of a call's steps in a band: see traverse_count_bucket */
    /* Per pixel, after the block's traverse_counts_t: primary; the far field (far, its sun rays and the ask past level 0); sun rays;
     * the rest of shade (AO, alpha holes, reflections). Each saturates at 65535. */
    TRAVERSE_COUNT_PLANES = 4u,
    TRAVERSE_COUNT_NO_LEVEL = 0xFFFFFFFFu,
    TRAVERSE_COUNT_NO_PIXEL = 0xFFFFFFFFu,
};
typedef enum traverse_count_kind_t : uint32_t {
    TRAVERSE_COUNT_PRIMARY = 0, /* traverse_primary's ray of a pixel */
    TRAVERSE_COUNT_FAR = 1,     /* a far ray through one level of the far field, one call a level */
    TRAVERSE_COUNT_SUN = 2,     /* shade's sun (or moon) ray, each surface it goes on past a call */
    TRAVERSE_COUNT_AO = 3,      /* an AO ray, steady or fresh */
    TRAVERSE_COUNT_ALPHA = 4,   /* shade's own ray going on past an alpha-0 texel or a see-through surface */
    TRAVERSE_COUNT_PAST = 5,    /* the far field asking whether shade's ray gets past level 0 past a leaf or a pane */
    TRAVERSE_COUNT_FAR_SUN = 6, /* the far field's sun rays */
    TRAVERSE_COUNT_REFLECT = 7, /* water's reflection */
} traverse_count_kind_t;
static_assert(sizeof(traverse_count_kind_t) == 4);

typedef struct traverse_counts_t {
    uint64_t steps[TRAVERSE_COUNT_KINDS][TRAVERSE_COUNT_BANDS];        /* 0 */
    uint64_t rays[TRAVERSE_COUNT_KINDS][TRAVERSE_COUNT_BANDS];         /* 448: calls that took a step in the band */
    uint64_t calls[TRAVERSE_COUNT_KINDS];                              /* 896: every call, with a step or none */
    uint64_t level_steps[TRAVERSE_COUNT_KINDS][TRAVERSE_COUNT_LEVELS]; /* 960: far kinds only, by the level walked */
    uint64_t level_calls[TRAVERSE_COUNT_KINDS][TRAVERSE_COUNT_LEVELS]; /* 1472 */
    /* 1984: calls by their steps in each band, and in the last row their whole steps: traverse_count_bucket */
    uint32_t histogram[TRAVERSE_COUNT_KINDS][TRAVERSE_COUNT_BANDS + 1u][TRAVERSE_COUNT_BUCKETS];
    uint32_t pixels; /* 18368: set by whoever gives the block: each plane's length, 0 for no planes */
    uint32_t _pad0;  /* 18372 */
} traverse_counts_t;
static_assert(sizeof(traverse_counts_t) == 18376);
static_assert(alignof(traverse_counts_t) == 8);
static_assert(offsetof(traverse_counts_t, steps) == 0);
static_assert(offsetof(traverse_counts_t, rays) == 448);
static_assert(offsetof(traverse_counts_t, calls) == 896);
static_assert(offsetof(traverse_counts_t, level_steps) == 960);
static_assert(offsetof(traverse_counts_t, level_calls) == 1472);
static_assert(offsetof(traverse_counts_t, histogram) == 1984);
static_assert(offsetof(traverse_counts_t, pixels) == 18368);
static_assert(offsetof(traverse_counts_t, _pad0) == 18372);

/* The bytes of a counts block for a picture of this many pixels. */
static inline uint64_t traverse_counts_bytes(uint32_t pixels) {
    return sizeof(traverse_counts_t) + (uint64_t)TRAVERSE_COUNT_PLANES * pixels * sizeof(uint16_t);
}

/* A band's low edge in blocks, for band 0 to TRAVERSE_COUNT_BANDS - 1. */
static inline uint32_t traverse_count_band_low(uint32_t band) {
    static const uint32_t lows[TRAVERSE_COUNT_BANDS] = {0u, 16u, 64u, 128u, 256u, 512u, 1024u};
    return lows[band < TRAVERSE_COUNT_BANDS ? band : TRAVERSE_COUNT_BANDS - 1u];
}

/* Which bucket a count of steps goes in: 0 to 15 each their own, then four to each power of two, so a bucket is within a quarter of
 * what it holds; the last holds all from 49152 on. The same on the card, in cuda/include/traverse/index.hu. */
static inline uint32_t traverse_count_bucket(uint32_t steps) {
    if (steps < 16u) {
        return steps;
    }
    const uint32_t octave = 31u - (uint32_t)__builtin_clz(steps);
    const uint32_t bucket = 16u + (octave - 4u) * 4u + ((steps >> (octave - 2u)) & 3u);
    return bucket < TRAVERSE_COUNT_BUCKETS ? bucket : TRAVERSE_COUNT_BUCKETS - 1u;
}

/* The least count of steps a bucket holds. */
static inline uint32_t traverse_count_bucket_low(uint32_t bucket) {
    if (bucket < 16u) {
        return bucket;
    }
    const uint32_t octave = 4u + (bucket - 16u) / 4u;
    return (4u + (bucket - 16u) % 4u) << (octave - 2u);
}

static inline uint32_t traverse_hit_face(uint16_t info) {
    return (uint32_t)info & TRAVERSE_INFO_FACE_MASK;
}

/* Plan 009 phase 04: whether a hit is a ray that reached its t_end, at t, with nothing met before it. */
static inline bool traverse_hit_past(const traverse_hit_t *hit) {
    return ((uint32_t)hit->info & TRAVERSE_INFO_FACE_MASK) == (uint32_t)TRAVERSE_FACE_MISS &&
           hit->t < TRAVERSE_COARSE_SKY;
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

/* Plan 009 phase 01: the same two, with every ray's steps added into counts, a block holding a traverse_counts_t (and its planes, as
 * long as its pixels says: the giver keeps that within the block), as TRAVERSE_COUNT_PRIMARY, pixel by pixel or ray by ray. A second
 * copy of the kernel does it; with counts nullptr these are the calls above. The hits are the same bits either way. */
gpu_status_t traverse_primary_counted(gpu_error_t *error, const traverse_world_t *world,
                                      const traverse_camera_t *camera, const traverse_targets_t *targets,
                                      bool use_coarse, const device_block_t *counts);
gpu_status_t traverse_rays_counted(gpu_error_t *error, const traverse_world_t *world, const device_block_t *rays,
                                   const device_block_t *hits, uint32_t ray_count, const device_block_t *counts);

/* Waits until everything started has finished, and reports a failure of any of it. */
gpu_status_t traverse_wait(gpu_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
