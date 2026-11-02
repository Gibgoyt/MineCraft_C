/* See run/src/gpu_bridge/shade/README.md and cuda/src/shade/README.md, which implements this. C API, compiled by GCC and by
 * nvcc: both sides check these static_asserts. The small functions below run on the card in the kernel and on the host in a
 * test, as the same lines. */
#ifndef MC_RUN_GPU_BRIDGE_SHADE_INDEX_H
#define MC_RUN_GPU_BRIDGE_SHADE_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "types/index.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __CUDACC__
#define SHADE_BOTH_SIDES __host__ __device__
#else
#define SHADE_BOTH_SIDES
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum : uint32_t {
    SHADE_FACES = 6u,
    SHADE_LEVELS_MAX = 13u,     /* TEXTURE_BLOB_MAX_LEVELS */
    SHADE_NO_TEXTURE = 0xFFFFu, /* in the face table: BLOCK_NO_TEXTURE, and a block with this draws SHADE_MISSING_* */
    SHADE_ALPHA_OPAQUE = 255u,
    /* How many surfaces a pixel looks at, holes and all. Past this it stops on the last one: see cuda/src/shade/README.md. */
    SHADE_ALPHA_LAYERS = 16u,
};

/* The switches of a frame, as shade_scene_t.features. A bit set means the effect is on. */
enum : uint32_t {
    SHADE_FEATURE_SHAPES = 1u << 0, /* boxes for what is not a cube, or every voxel whole */
    SHADE_FEATURE_ALPHA = 1u << 1,  /* rays go on through a texel of alpha 0, or stop on the texture's mean colour */
    SHADE_FEATURES_ALL = SHADE_FEATURE_SHAPES | SHADE_FEATURE_ALPHA,
};

/* A material with no texture, or none in the table: a colour nothing else in the game is, so that the bug shows. */
#define SHADE_MISSING_R F32(1.0)
#define SHADE_MISSING_G F32(0.0)
#define SHADE_MISSING_B F32(1.0)

/* The sun is straight up, for now: each kind of face has its one brightness. Stored light and shadow come with Stage 09.2. */
#define SHADE_BRIGHTNESS_TOP F32(1.0)
#define SHADE_BRIGHTNESS_BOTTOM F32(0.5)
#define SHADE_BRIGHTNESS_X F32(0.6)
#define SHADE_BRIGHTNESS_Y F32(0.8)
/* From inside a block: dark, so that it is plain where the eye is. */
#define SHADE_BRIGHTNESS_INSIDE F32(0.25)

/* What a kernel needs of one texture, from its entry in the blob. */
typedef struct shade_texture_t {
    uint64_t pixels_offset;                  /* 0: into the pixels block */
    uint32_t level_offset[SHADE_LEVELS_MAX]; /* 8: from pixels_offset to each level's first frame */
    uint16_t width;                          /* 60: of level 0 */
    uint16_t height;                         /* 62 */
    uint8_t level_count;                     /* 64 */
    uint8_t mean_rgba[4]; /* 65: what shows in a part-covered texel, until rays go on past those: Stage 09.3 */
    uint8_t _pad0[3];     /* 69 */
} shade_texture_t;
static_assert(sizeof(shade_texture_t) == 72);
static_assert(alignof(shade_texture_t) == 8);
static_assert(offsetof(shade_texture_t, pixels_offset) == 0);
static_assert(offsetof(shade_texture_t, level_offset) == 8);
static_assert(offsetof(shade_texture_t, width) == 60);
static_assert(offsetof(shade_texture_t, height) == 62);
static_assert(offsetof(shade_texture_t, level_count) == 64);
static_assert(offsetof(shade_texture_t, mean_rgba) == 65);
static_assert(offsetof(shade_texture_t, _pad0) == 69);

/* What shading reads, borrowed from whoever holds the blocks and not written while a kernel may run. */
typedef struct shade_scene_t {
    const device_block_t *borrowed_textures; /* 0: texture_count shade_texture_t */
    const device_block_t
        *borrowed_faces; /* 8: material_count * SHADE_FACES uint16_t: an index into textures, by face */
    const device_block_t *borrowed_pixels; /* 16: every texture's RGBA8 */
    uint32_t texture_count;                /* 24 */
    uint32_t material_count;               /* 28 */
    uint32_t features;                     /* 32: SHADE_FEATURE_* */
    uint8_t _pad0[4];                      /* 36 */
} shade_scene_t;
static_assert(sizeof(shade_scene_t) == 40);
static_assert(alignof(shade_scene_t) == 8);
static_assert(offsetof(shade_scene_t, borrowed_textures) == 0);
static_assert(offsetof(shade_scene_t, borrowed_faces) == 8);
static_assert(offsetof(shade_scene_t, borrowed_pixels) == 16);
static_assert(offsetof(shade_scene_t, texture_count) == 24);
static_assert(offsetof(shade_scene_t, material_count) == 28);
static_assert(offsetof(shade_scene_t, features) == 32);
static_assert(offsetof(shade_scene_t, _pad0) == 36);

/* One pixel, in linear light before the sky and the tone map: where post goes on from. */
typedef struct shade_pixel_t {
    float32_t rgb[3]; /* 0 */
    float32_t t;      /* 12: the hit's distance, or TRAVERSE_COARSE_SKY for sky */
} shade_pixel_t;
static_assert(sizeof(shade_pixel_t) == 16);
static_assert(alignof(shade_pixel_t) == 4);
static_assert(offsetof(shade_pixel_t, rgb) == 0);
static_assert(offsetof(shade_pixel_t, t) == 12);

static inline uint64_t shade_pixels_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * sizeof(shade_pixel_t);
}

/* A traverse_face_t's brightness. */
SHADE_BOTH_SIDES static inline float32_t shade_face_brightness(uint32_t face) {
    if (face == TRAVERSE_FACE_POS_Z) {
        return SHADE_BRIGHTNESS_TOP;
    }
    if (face == TRAVERSE_FACE_NEG_Z) {
        return SHADE_BRIGHTNESS_BOTTOM;
    }
    if (face == TRAVERSE_FACE_NEG_X || face == TRAVERSE_FACE_POS_X) {
        return SHADE_BRIGHTNESS_X;
    }
    if (face == TRAVERSE_FACE_NEG_Y || face == TRAVERSE_FACE_POS_Y) {
        return SHADE_BRIGHTNESS_Y;
    }
    return SHADE_BRIGHTNESS_INSIDE;
}

/* Where on a texture a point of a face is, with local from 0 to 1 inside the voxel. v is down the texture, since rows start at
 * the top. Looking straight at a side, up is z, and u runs to the viewer's right, so that no side comes out mirrored. Seen from
 * above, x is to the right and y is up the page, and the bottom is the top seen from beneath. From inside, as for the top. */
SHADE_BOTH_SIDES static inline void shade_face_uv(uint32_t face, const float32_t local[3], float32_t *out_u,
                                                  float32_t *out_v) {
    if (face == TRAVERSE_FACE_POS_X) {
        *out_u = local[1];
        *out_v = F32(1.0) - local[2];
    } else if (face == TRAVERSE_FACE_NEG_X) {
        *out_u = F32(1.0) - local[1];
        *out_v = F32(1.0) - local[2];
    } else if (face == TRAVERSE_FACE_POS_Y) {
        *out_u = F32(1.0) - local[0];
        *out_v = F32(1.0) - local[2];
    } else if (face == TRAVERSE_FACE_NEG_Y) {
        *out_u = local[0];
        *out_v = F32(1.0) - local[2];
    } else if (face == TRAVERSE_FACE_NEG_Z) {
        *out_u = local[0];
        *out_v = local[1];
    } else {
        *out_u = local[0];
        *out_v = F32(1.0) - local[1];
    }
}

/* A place along a texture's side, from 0 to 1 and a little either way from rounding, as a whole texel. */
SHADE_BOTH_SIDES static inline uint32_t shade_texel_of(float32_t coordinate, uint32_t texels) {
    const float32_t scaled = coordinate * (float32_t)texels;
    if (!(scaled > F32(0.0))) {
        return 0u;
    }
    const uint32_t whole = (uint32_t)scaled;
    return whole >= texels ? texels - 1u : whole;
}

/* Which mip level, by distance alone: the one whose texels are nearest to a pixel's size at that distance. pixel_tangent is how
 * many blocks wide a pixel is per block of distance. */
SHADE_BOTH_SIDES static inline uint32_t shade_mip_level(float32_t t, float32_t pixel_tangent, uint32_t texels,
                                                        uint32_t level_count) {
    const float32_t per_pixel = t * pixel_tangent * (float32_t)texels;
    if (!(per_pixel > F32(1.0))) {
        return 0u;
    }
    const uint32_t level = (uint32_t)(log2f(per_pixel) + F32(0.5));
    return level >= level_count ? level_count - 1u : level;
}

/* One level's size, as the blob has it: it halves, rounding down, and stops at 1. */
SHADE_BOTH_SIDES static inline uint32_t shade_level_dimension(uint32_t dimension, uint32_t level) {
    const uint32_t reduced = dimension >> level;
    return reduced == 0u ? 1u : reduced;
}

SHADE_BOTH_SIDES static inline float32_t shade_srgb_to_linear(float32_t encoded) {
    if (encoded <= F32(0.04045)) {
        return encoded / F32(12.92);
    }
    return powf((encoded + F32(0.055)) / F32(1.055), F32(2.4));
}

/* How long a camera's pixel is, per block of distance: see traverse_camera_t for what up carries. */
SHADE_BOTH_SIDES static inline float32_t shade_pixel_tangent(const traverse_camera_t *camera) {
    const float32_t squared =
        camera->up[0] * camera->up[0] + camera->up[1] * camera->up[1] + camera->up[2] * camera->up[2];
    return F32(2.0) * sqrtf(squared) / (float32_t)camera->height;
}

/* One colour per pixel of hits, into pixels, each row after row at the camera's size. A ray that lands on a texel of alpha 0 goes
 * on through world, which must be the one the hits came from. Starts a kernel and returns; it runs in order with any other.
 * Nothing here allocates. On failure error says why, and nothing was started. */
gpu_status_t shade_frame(gpu_error_t *error, const shade_scene_t *scene, const traverse_world_t *world,
                         const traverse_camera_t *camera, const device_block_t *hits, const device_block_t *pixels);

#ifdef __cplusplus
}
#endif

#endif
