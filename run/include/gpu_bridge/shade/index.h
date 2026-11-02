/* See run/src/gpu_bridge/shade/README.md and cuda/src/shade/README.md, which implements this. C API, compiled by GCC and by
 * nvcc: both sides check these static_asserts. The small functions below run on the card in the kernel and on the host in a
 * test, as the same lines. */
#ifndef MC_RUN_GPU_BRIDGE_SHADE_INDEX_H
#define MC_RUN_GPU_BRIDGE_SHADE_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/post/index.h"
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
    /* How many surfaces a sun ray looks at, holes and all, before it calls the sun hidden: see cuda/src/shade/README.md. */
    SHADE_SUN_LAYERS = 8u,
    /* How many surfaces a reflection looks at, holes and all, before it stops on the last. */
    SHADE_REFLECTION_LAYERS = 8u,
    /* A material no voxel has: shade_scene_t's water and glass, in a scene with none. */
    SHADE_NO_MATERIAL = 0xFFFFu,
    /* A voxel's stored light byte with nothing stored: the sky open above it, and no block light. Sky is the high half. */
    SHADE_LIGHT_OPEN_SKY = 0xF0u,
    SHADE_LIGHT_LEVEL_MAX = 15u,
};

/* The switches of a frame, as shade_scene_t.features. A bit set means the effect is on. */
enum : uint32_t {
    SHADE_FEATURE_SHAPES = 1u << 0, /* boxes for what is not a cube, or every voxel whole */
    SHADE_FEATURE_ALPHA = 1u << 1,  /* rays go on through a texel of alpha 0, or stop on the texture's mean colour */
    SHADE_FEATURE_SKY =
        1u << 2, /* a sun that moves and lights things, and a sky to go with it, or the sun straight up */
    SHADE_FEATURE_DAY_CYCLE = 1u << 3, /* the time of day goes on, or stays where it was set */
    SHADE_FEATURE_FOG = 1u << 4,       /* the far world melts into the sky, or stops at a wall */
    SHADE_FEATURE_SHADOW = 1u << 5,    /* one ray per surface facing the sun, or nothing ever stands in its way */
    SHADE_FEATURE_LIGHT = 1u << 6,     /* the light stored per voxel counts, or every voxel is under an open sky */
    /* Rays go on through part-covered texels and through water, or stop there on the texture's mean colour. The next two need it. */
    SHADE_FEATURE_TRANSPARENCY = 1u << 7,
    SHADE_FEATURE_WATER_REFLECTION = 1u << 8, /* one ray off the surface of water, or none */
    SHADE_FEATURE_WATER_TINT = 1u << 9,       /* water takes light by how far a ray goes in it, or is clear */
    SHADE_FEATURES_ALL = SHADE_FEATURE_SHAPES | SHADE_FEATURE_ALPHA | SHADE_FEATURE_SKY | SHADE_FEATURE_DAY_CYCLE |
                         SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW | SHADE_FEATURE_LIGHT | SHADE_FEATURE_TRANSPARENCY |
                         SHADE_FEATURE_WATER_REFLECTION | SHADE_FEATURE_WATER_TINT,
};

/* A material with no texture, or none in the table: a colour nothing else in the game is, so that the bug shows. */
#define SHADE_MISSING_R F32(1.0)
#define SHADE_MISSING_G F32(0.0)
#define SHADE_MISSING_B F32(1.0)

/* Each kind of face has its one brightness under the sky: what is left of the sky's light on a face that looks sideways or down. */
#define SHADE_BRIGHTNESS_TOP F32(1.0)
#define SHADE_BRIGHTNESS_BOTTOM F32(0.5)
#define SHADE_BRIGHTNESS_X F32(0.6)
#define SHADE_BRIGHTNESS_Y F32(0.8)
/* From inside a block: dark, so that it is plain where the eye is. */
#define SHADE_BRIGHTNESS_INSIDE F32(0.25)

/* How much of the light water reflects, looking straight at it: Schlick's form, with water's own number. */
#define SHADE_WATER_F0 F32(0.02)
/* What water takes of the light per block a ray goes through it, by channel: red goes first, so that deep water is blue. */
#define SHADE_WATER_ABSORB_R F32(0.30)
#define SHADE_WATER_ABSORB_G F32(0.10)
#define SHADE_WATER_ABSORB_B F32(0.07)
/* What water gives back to the eye in place of what it took, under full light: the colour of very deep water. */
#define SHADE_WATER_DEEP_R F32(0.012)
#define SHADE_WATER_DEEP_G F32(0.06)
#define SHADE_WATER_DEEP_B F32(0.10)
/* What a sun ray loses per block of water. One number, as what it carries is one: see cuda/src/shade/README.md. */
#define SHADE_WATER_ABSORB_SUN F32(0.10)
/* Past this many blocks of water nothing behind it can be told from none, so a ray with tint on looks no further. */
#define SHADE_WATER_SEEN_BLOCKS F32(48.0)

/* What a kernel needs of one texture, from its entry in the blob. */
typedef struct shade_texture_t {
    uint64_t pixels_offset;                  /* 0: into the pixels block */
    uint32_t level_offset[SHADE_LEVELS_MAX]; /* 8: from pixels_offset to each level's first frame */
    uint16_t width;                          /* 60: of level 0 */
    uint16_t height;                         /* 62 */
    uint8_t level_count;                     /* 64 */
    uint8_t mean_rgba[4];                    /* 65: what shows in a part-covered texel where a ray stops on it */
    uint8_t _pad0[3];                        /* 69 */
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

/* The light of this moment of the day, in linear light, worked out once a frame by render_day_fill. With all but ambient at
 * zero and ambient at one, each kind of face has its one brightness and nothing else, which is the setting's off. */
typedef struct shade_sun_t {
    float32_t direction[3]; /* 0: unit, towards the sun, or the moon when the sun is down */
    float32_t direct[3];    /* 12: its light on a surface square to it */
    float32_t ambient[3];   /* 24: the sky's light on a surface under all of it */
    float32_t block[3];     /* 36: block light at its brightest */
} shade_sun_t;
static_assert(sizeof(shade_sun_t) == 48);
static_assert(alignof(shade_sun_t) == 4);
static_assert(offsetof(shade_sun_t, direction) == 0);
static_assert(offsetof(shade_sun_t, direct) == 12);
static_assert(offsetof(shade_sun_t, ambient) == 24);
static_assert(offsetof(shade_sun_t, block) == 36);

/* What shading reads, borrowed from whoever holds the blocks and not written while a kernel may run. */
typedef struct shade_scene_t {
    const device_block_t *borrowed_textures; /* 0: texture_count shade_texture_t */
    const device_block_t
        *borrowed_faces; /* 8: material_count * SHADE_FACES uint16_t: an index into textures, by face */
    const device_block_t *borrowed_pixels; /* 16: every texture's RGBA8 */
    uint32_t texture_count;                /* 24 */
    uint32_t material_count;               /* 28 */
    uint32_t features;                     /* 32: SHADE_FEATURE_* */
    uint16_t water_material;               /* 36: or SHADE_NO_MATERIAL */
    uint16_t glass_material;               /* 38: or SHADE_NO_MATERIAL */
    shade_sun_t sun;                       /* 40 */
} shade_scene_t;
static_assert(sizeof(shade_scene_t) == 88);
static_assert(alignof(shade_scene_t) == 8);
static_assert(offsetof(shade_scene_t, borrowed_textures) == 0);
static_assert(offsetof(shade_scene_t, borrowed_faces) == 8);
static_assert(offsetof(shade_scene_t, borrowed_pixels) == 16);
static_assert(offsetof(shade_scene_t, texture_count) == 24);
static_assert(offsetof(shade_scene_t, material_count) == 28);
static_assert(offsetof(shade_scene_t, features) == 32);
static_assert(offsetof(shade_scene_t, water_material) == 36);
static_assert(offsetof(shade_scene_t, glass_material) == 38);
static_assert(offsetof(shade_scene_t, sun) == 40);

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

/* How square a face is to a unit direction: 0 if it looks away. From inside a block, no face looks at anything. */
SHADE_BOTH_SIDES static inline float32_t shade_face_towards(uint32_t face, const float32_t direction[3]) {
    if (face >= SHADE_FACES) {
        return F32(0.0);
    }
    const float32_t along = direction[face >> 1];
    const float32_t towards = (face & 1u) != 0u ? along : -along;
    return towards > F32(0.0) ? towards : F32(0.0);
}

/* A stored level of 0 to 15 as light: each step down is 0.8 of the one above, so a level of 15 is exactly 1. Sky light of 0 is
 * still a little, as a cave is not pitch black; block light of 0 is none, or a world with no torch would pay for one. */
SHADE_BOTH_SIDES static inline float32_t shade_sky_level(uint32_t level) {
    if (level >= SHADE_LIGHT_LEVEL_MAX) {
        return F32(1.0);
    }
    return powf(F32(0.8), (float32_t)(SHADE_LIGHT_LEVEL_MAX - level));
}

SHADE_BOTH_SIDES static inline float32_t shade_block_level(uint32_t level) {
    if (level == 0u) {
        return F32(0.0);
    }
    return level >= SHADE_LIGHT_LEVEL_MAX ? F32(1.0) : powf(F32(0.8), (float32_t)(SHADE_LIGHT_LEVEL_MAX - level));
}

/* How much of the sun a voxel with this stored light may get before asking a ray: none, where no sky light reaches. */
SHADE_BOTH_SIDES static inline float32_t shade_sun_towards(const shade_sun_t *sun, uint32_t face, uint32_t stored) {
    return (stored >> 4) == 0u ? F32(0.0) : shade_face_towards(face, sun->direction);
}

/* What falls on a surface, to multiply its colour by: the sky and block light stored in its voxel, under the face's brightness,
 * and the sun on top, as much of it as sun_seen says reached here. */
SHADE_BOTH_SIDES static inline void shade_surface_light(const shade_sun_t *sun, uint32_t face, uint32_t stored,
                                                        float32_t sun_seen, float32_t out[3]) {
    const float32_t brightness = shade_face_brightness(face);
    const float32_t sky = shade_sky_level(stored >> 4), block = shade_block_level(stored & 15u);
    const float32_t towards = shade_sun_towards(sun, face, stored);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out[i] = brightness * (sun->ambient[i] * sky + sun->block[i] * block) + sun->direct[i] * towards * sun_seen;
    }
}

/* How far off a surface a sun ray starts, for the distance the surface is seen from: more than that distance's rounding, so that
 * a surface does not shadow itself, and far less than anything a block is made of. */
SHADE_BOTH_SIDES static inline float32_t shade_sun_bias(float32_t t) {
    return F32(2.5e-4) + t * F32(1e-6);
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

/* How much of the light a surface of water reflects, for how square a ray meets it: the cosine, from 0 along it to 1 straight on. */
SHADE_BOTH_SIDES static inline float32_t shade_water_fresnel(float32_t cosine) {
    const float32_t off = cosine >= F32(1.0) ? F32(0.0) : (cosine <= F32(0.0) ? F32(1.0) : F32(1.0) - cosine);
    const float32_t squared = off * off;
    return SHADE_WATER_F0 + (F32(1.0) - SHADE_WATER_F0) * squared * squared * off;
}

/* What a length of water does to a ray: out_through is what gets past it, and out_given what it gives back under full light. */
SHADE_BOTH_SIDES static inline void shade_water_tint(float32_t blocks, float32_t out_through[3],
                                                     float32_t out_given[3]) {
    out_through[0] = expf(-SHADE_WATER_ABSORB_R * blocks);
    out_through[1] = expf(-SHADE_WATER_ABSORB_G * blocks);
    out_through[2] = expf(-SHADE_WATER_ABSORB_B * blocks);
    out_given[0] = SHADE_WATER_DEEP_R * (F32(1.0) - out_through[0]);
    out_given[1] = SHADE_WATER_DEEP_G * (F32(1.0) - out_through[1]);
    out_given[2] = SHADE_WATER_DEEP_B * (F32(1.0) - out_through[2]);
}

/* Post fogs a whole pixel by its first surface. What is seen behind that, at t, needs only the rest of its own fog on top, so that
 * both together come to post_fog at t. */
SHADE_BOTH_SIDES static inline float32_t shade_fog_behind(const post_settings_t *settings, float32_t first,
                                                          float32_t t) {
    const float32_t near_part = post_fog(settings, first);
    return near_part >= F32(1.0) ? F32(0.0) : (post_fog(settings, t) - near_part) / (F32(1.0) - near_part);
}

/* Whether a surface is only the far side of the see-through voxel a ray just left: the same block, and next to it across the face
 * the ray came in by. A wall of glass has no frames inside it, and a stack of ice no faces. */
SHADE_BOTH_SIDES static inline bool shade_own_kind_behind(uint32_t last_material, const int16_t last_offset[3],
                                                          uint32_t material, const int16_t offset[3], uint32_t face) {
    if (material != last_material || face >= SHADE_FACES) {
        return false;
    }
    const uint32_t axis = face >> 1;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const int32_t towards = i != axis ? 0 : ((face & 1u) != 0u ? 1 : -1);
        if ((int32_t)offset[i] + towards != (int32_t)last_offset[i]) {
            return false;
        }
    }
    return true;
}

/* One colour per pixel of hits, into pixels, each row after row at the camera's size. A ray that lands on a texel of alpha 0 goes
 * on through world, which must be the one the hits came from, as one does through glass and water. post is the same sky and fog
 * post_frame is about to use: what shows behind glass, and in water, is shaded here. Starts a kernel and returns; it runs in order
 * with any other. Nothing here allocates. On failure error says why, and nothing was started. */
gpu_status_t shade_frame(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                         const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits,
                         const device_block_t *pixels);

#ifdef __cplusplus
}
#endif

#endif
