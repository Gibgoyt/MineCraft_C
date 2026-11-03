/* See run/src/gpu_bridge/shade/README.md and cuda/src/shade/README.md, which implements this. C API, compiled by GCC and by
 * nvcc: both sides check these static_asserts. The small functions below run on the card in the kernel and on the host in a
 * test, as the same lines. */
#ifndef MC_RUN_GPU_BRIDGE_SHADE_INDEX_H
#define MC_RUN_GPU_BRIDGE_SHADE_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/entity_rays/index.h"
#include "gpu_bridge/lod/index.h"
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
    SHADE_FEATURE_AO = 1u << 10,       /* short rays darken what stored light falls on near other blocks, or none */
    SHADE_FEATURE_TAA = 1u << 11,      /* each frame looks a little off centre and adds to the last, or stands alone */
    SHADE_FEATURE_GOD_RAYS = 1u << 12, /* the air shows the sun's light where it reaches, or is clear */
    SHADE_FEATURE_ENTITIES = 1u << 13, /* mobs and items are drawn as boxes, or not at all */
    SHADE_FEATURE_OUTLINE =
        1u << 14,                 /* what is aimed at has an outline, and what is being mined its crack, or neither */
    SHADE_FEATURE_LOD = 1u << 15, /* the world goes on past full detail in coarse grids, or stops there */
    /* To render_frame_features_set only, and past that post_temporal_t.sun_apart, as it is no effect that shows: a last surface's sun ray has a
     * kernel of its own, even with AO off: the same picture, for less time, and the hits block then ends up holding those surfaces. No setting
     * turns this off. What wants the frame's own hits afterwards, a test that holds them to something, leaves it out. */
    SHADE_FEATURE_SUN_APART = 1u << 16,
    /* What a picture is drawn with on its own, from nothing: no frame of these reads what the last one left. What render_frame_init
     * starts with, and so what a test that wants one exact picture gets. */
    SHADE_FEATURES_ALL = SHADE_FEATURE_SHAPES | SHADE_FEATURE_ALPHA | SHADE_FEATURE_SKY | SHADE_FEATURE_DAY_CYCLE |
                         SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW | SHADE_FEATURE_LIGHT | SHADE_FEATURE_TRANSPARENCY |
                         SHADE_FEATURE_WATER_REFLECTION | SHADE_FEATURE_WATER_TINT | SHADE_FEATURE_ENTITIES |
                         SHADE_FEATURE_OUTLINE | SHADE_FEATURE_LOD,
    /* What a world is played with unless told otherwise. God rays are optional, so they are asked for. */
    SHADE_FEATURES_PLAY = SHADE_FEATURES_ALL | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
    SHADE_FEATURES_EVERY = SHADE_FEATURES_PLAY | SHADE_FEATURE_GOD_RAYS,
};

enum : uint32_t {
    /* A pixel with history sends its one AO ray on one frame of this many, in turn with the three next to it. */
    SHADE_AO_TURNS = 4u,
    /* A pixel with nothing from the last frame on its surface, itself or around it, sends this many, as it has nothing else. */
    SHADE_AO_FRESH_RAYS = 4u,
    /* How many samples a pixel's AO is the mean of, at most. Past this the oldest count for less. */
    SHADE_AO_SAMPLES_MAX = 32u,
    /* How many surfaces an AO ray looks at, holes and all, before it stops looking. */
    SHADE_AO_LAYERS = 4u,
    /* An AO ray's cap on steps: a ray of SHADE_AO_REACH crosses no more than 3 * sqrt(3) + 3 planes between cells, and each step
     * crosses at least one, so this always gets past its reach. So a capped ray has met nothing. */
    SHADE_AO_STEPS = 12u,
    /* How many places a god ray asks about the sun. */
    SHADE_SHAFT_STEPS = 6u,
};

/* How far an AO ray looks, in blocks. */
#define SHADE_AO_REACH F32(3.0)
/* How far along a god ray the air is looked at, in blocks, and how much of the light it stops per block. */
#define SHADE_SHAFT_REACH F32(64.0)
#define SHADE_SHAFT_DENSITY F32(0.015)
/* Henyey-Greenstein's number: how much of what the air scatters goes on forward, so that the rays show looking towards the sun. */
#define SHADE_SHAFT_FORWARD F32(0.6)
/* What the air shows of the sun's light, so that they can be seen at all with so little of it scattered. */
#define SHADE_SHAFT_STRENGTH F32(2.0)

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

#ifdef __CUDA_ARCH__
#define SHADE_TABLE static __constant__ const
#else
#define SHADE_TABLE static const
#endif

/* powf costs more than most of a surface's other arithmetic, and a surface asks it three times for its colour and twice for its light,
 * of a value that can only be one of these. The numbers are what this card's powf gives, printed as they are, so that a picture stays what
 * it was to the bit: the phase 17 stage 3 file has what that saved. run_gpu_bridge's shade test holds them to the functions they stand for. */
SHADE_TABLE float32_t SHADE_SRGB_BYTE[256] = {
    F32(0x0p+0),         F32(0x1.3e4568p-12), F32(0x1.3e4568p-11), F32(0x1.dd681cp-11), F32(0x1.3e4568p-10),
    F32(0x1.8dd6c2p-10), F32(0x1.dd681cp-10), F32(0x1.167cbap-9),  F32(0x1.3e4568p-9),  F32(0x1.660e16p-9),
    F32(0x1.8dd6c2p-9),  F32(0x1.b6a31ap-9),  F32(0x1.e1e31ap-9),  F32(0x1.07c38cp-8),  F32(0x1.1fcc2cp-8),
    F32(0x1.390ffap-8),  F32(0x1.53936ep-8),  F32(0x1.6f5adep-8),  F32(0x1.8c6a92p-8),  F32(0x1.aac6c2p-8),
    F32(0x1.ca7382p-8),  F32(0x1.eb74ep-8),   F32(0x1.06e76ap-7),  F32(0x1.18c2a4p-7),  F32(0x1.2b4e06p-7),
    F32(0x1.3e8b7cp-7),  F32(0x1.527cd6p-7),  F32(0x1.6723eep-7),  F32(0x1.7c8292p-7),  F32(0x1.929a86p-7),
    F32(0x1.a96d8ep-7),  F32(0x1.c0fd62p-7),  F32(0x1.d94bbep-7),  F32(0x1.f25a44p-7),  F32(0x1.06155p-6),
    F32(0x1.135f3ep-6),  F32(0x1.210bb6p-6),  F32(0x1.2f1b8ap-6),  F32(0x1.3d8f84p-6),  F32(0x1.4c6866p-6),
    F32(0x1.5ba6fap-6),  F32(0x1.6b4c02p-6),  F32(0x1.7b584p-6),   F32(0x1.8bcc72p-6),  F32(0x1.9ca954p-6),
    F32(0x1.adefaap-6),  F32(0x1.bfa02p-6),   F32(0x1.d1bb72p-6),  F32(0x1.e44258p-6),  F32(0x1.f73582p-6),
    F32(0x1.054ad2p-5),  F32(0x1.0f31b8p-5),  F32(0x1.194fccp-5),  F32(0x1.23a55ep-5),  F32(0x1.2e32c6p-5),
    F32(0x1.38f85cp-5),  F32(0x1.43f678p-5),  F32(0x1.4f2d64p-5),  F32(0x1.5a9d76p-5),  F32(0x1.664702p-5),
    F32(0x1.722a56p-5),  F32(0x1.7e47c6p-5),  F32(0x1.8a9fa2p-5),  F32(0x1.973238p-5),  F32(0x1.a3ffdep-5),
    F32(0x1.b108d4p-5),  F32(0x1.be4d6ep-5),  F32(0x1.cbcdfcp-5),  F32(0x1.d98ac8p-5),  F32(0x1.e7841ep-5),
    F32(0x1.f5ba52p-5),  F32(0x1.0216cep-4),  F32(0x1.096f2ap-4),  F32(0x1.10e66p-4),   F32(0x1.187c94p-4),
    F32(0x1.2031ecp-4),  F32(0x1.28068ap-4),  F32(0x1.2ffa94p-4),  F32(0x1.380e2cp-4),  F32(0x1.404176p-4),
    F32(0x1.489496p-4),  F32(0x1.5107aep-4),  F32(0x1.599aep-4),   F32(0x1.624e5p-4),   F32(0x1.6b2224p-4),
    F32(0x1.741676p-4),  F32(0x1.7d2b6ap-4),  F32(0x1.866124p-4),  F32(0x1.8fb7c4p-4),  F32(0x1.992f6cp-4),
    F32(0x1.a2c83ep-4),  F32(0x1.ac825ap-4),  F32(0x1.b65ddep-4),  F32(0x1.c05aeep-4),  F32(0x1.ca79aap-4),
    F32(0x1.d4ba32p-4),  F32(0x1.df1ca4p-4),  F32(0x1.e9a122p-4),  F32(0x1.f447dp-4),   F32(0x1.ff10c2p-4),
    F32(0x1.04fe0ep-3),  F32(0x1.0a85p-3),    F32(0x1.101d46p-3),  F32(0x1.15c6fp-3),   F32(0x1.1b820ap-3),
    F32(0x1.214ea8p-3),  F32(0x1.272cd6p-3),  F32(0x1.2d1ca2p-3),  F32(0x1.331e2p-3),   F32(0x1.39315ap-3),
    F32(0x1.3f566p-3),   F32(0x1.458d46p-3),  F32(0x1.4bd612p-3),  F32(0x1.5230d8p-3),  F32(0x1.589dap-3),
    F32(0x1.5f1c82p-3),  F32(0x1.65ad8ap-3),  F32(0x1.6c50c2p-3),  F32(0x1.73063cp-3),  F32(0x1.79ce06p-3),
    F32(0x1.80a82cp-3),  F32(0x1.8794bep-3),  F32(0x1.8e93c8p-3),  F32(0x1.95a55cp-3),  F32(0x1.9cc984p-3),
    F32(0x1.a4004ep-3),  F32(0x1.ab49ccp-3),  F32(0x1.b2a606p-3),  F32(0x1.ba1516p-3),  F32(0x1.c196f8p-3),
    F32(0x1.c92bcp-3),   F32(0x1.d0d37ep-3),  F32(0x1.d88e4p-3),   F32(0x1.e05c18p-3),  F32(0x1.e83d08p-3),
    F32(0x1.f0312p-3),   F32(0x1.f8387p-3),   F32(0x1.002984p-2),  F32(0x1.044078p-2),  F32(0x1.08611ap-2),
    F32(0x1.0c8b74p-2),  F32(0x1.10bf8ap-2),  F32(0x1.14fd64p-2),  F32(0x1.194506p-2),  F32(0x1.1d967ap-2),
    F32(0x1.21f1c2p-2),  F32(0x1.2656e8p-2),  F32(0x1.2ac5fp-2),   F32(0x1.2f3ee2p-2),  F32(0x1.33c1c4p-2),
    F32(0x1.384e9cp-2),  F32(0x1.3ce56ep-2),  F32(0x1.418644p-2),  F32(0x1.463124p-2),  F32(0x1.4ae61p-2),
    F32(0x1.4fa512p-2),  F32(0x1.546e3p-2),   F32(0x1.59416ep-2),  F32(0x1.5e1ed4p-2),  F32(0x1.630666p-2),
    F32(0x1.67f83p-2),   F32(0x1.6cf43p-2),   F32(0x1.71fa6ep-2),  F32(0x1.770af2p-2),  F32(0x1.7c25cp-2),
    F32(0x1.814ae2p-2),  F32(0x1.867a5ap-2),  F32(0x1.8bb42ep-2),  F32(0x1.90f866p-2),  F32(0x1.964706p-2),
    F32(0x1.9ba016p-2),  F32(0x1.a10398p-2),  F32(0x1.a67198p-2),  F32(0x1.abea14p-2),  F32(0x1.b16d1ap-2),
    F32(0x1.b6faa8p-2),  F32(0x1.bc92cap-2),  F32(0x1.c23582p-2),  F32(0x1.c7e2d6p-2),  F32(0x1.cd9acep-2),
    F32(0x1.d35d6cp-2),  F32(0x1.d92abap-2),  F32(0x1.df02bap-2),  F32(0x1.e4e574p-2),  F32(0x1.ead2ecp-2),
    F32(0x1.f0cb28p-2),  F32(0x1.f6ce2ep-2),  F32(0x1.fcdc04p-2),  F32(0x1.017a5ap-1),  F32(0x1.048c1cp-1),
    F32(0x1.07a34ep-1),  F32(0x1.0abff2p-1),  F32(0x1.0de20cp-1),  F32(0x1.11099ep-1),  F32(0x1.1436acp-1),
    F32(0x1.176936p-1),  F32(0x1.1aa14p-1),   F32(0x1.1ddecep-1),  F32(0x1.2121e2p-1),  F32(0x1.246a7cp-1),
    F32(0x1.27b8a2p-1),  F32(0x1.2b0c56p-1),  F32(0x1.2e659ap-1),  F32(0x1.31c472p-1),  F32(0x1.3528dep-1),
    F32(0x1.3892e2p-1),  F32(0x1.3c0282p-1),  F32(0x1.3f77bep-1),  F32(0x1.42f29cp-1),  F32(0x1.46731cp-1),
    F32(0x1.49f94p-1),   F32(0x1.4d850cp-1),  F32(0x1.511684p-1),  F32(0x1.54ada6p-1),  F32(0x1.584a7ap-1),
    F32(0x1.5becfep-1),  F32(0x1.5f953cp-1),  F32(0x1.63432ep-1),  F32(0x1.66f6d8p-1),  F32(0x1.6ab04p-1),
    F32(0x1.6e6f66p-1),  F32(0x1.72344cp-1),  F32(0x1.75fef8p-1),  F32(0x1.79cf68p-1),  F32(0x1.7da5a4p-1),
    F32(0x1.8181a8p-1),  F32(0x1.85637cp-1),  F32(0x1.894b2p-1),   F32(0x1.8d3896p-1),  F32(0x1.912be2p-1),
    F32(0x1.952504p-1),  F32(0x1.992402p-1),  F32(0x1.9d28dcp-1),  F32(0x1.a13396p-1),  F32(0x1.a5443p-1),
    F32(0x1.a95aaep-1),  F32(0x1.ad7714p-1),  F32(0x1.b19962p-1),  F32(0x1.b5c19ap-1),  F32(0x1.b9efcp-1),
    F32(0x1.be23d8p-1),  F32(0x1.c25ddcp-1),  F32(0x1.c69ddep-1),  F32(0x1.cae3d2p-1),  F32(0x1.cf2fc6p-1),
    F32(0x1.d381acp-1),  F32(0x1.d7d99ap-1),  F32(0x1.dc377ep-1),  F32(0x1.e09b7p-1),   F32(0x1.e5055ep-1),
    F32(0x1.e9755cp-1),  F32(0x1.edeb5cp-1),  F32(0x1.f2677p-1),   F32(0x1.f6e98cp-1),  F32(0x1.fb71cp-1),
    F32(0x1p+0),
};
/* 0.8 to the power of 15 - level: see shade_sky_level. */
SHADE_TABLE float32_t SHADE_LEVEL_LIGHT[16] = {
    F32(0x1.203afep-5), F32(0x1.6849bep-5), F32(0x1.c25c2cp-5), F32(0x1.19799cp-4),
    F32(0x1.5fd802p-4), F32(0x1.b7ce02p-4), F32(0x1.12e0cp-3),  F32(0x1.5798fp-3),
    F32(0x1.ad7f2cp-3), F32(0x1.0c6f7cp-2), F32(0x1.4f8b5ap-2), F32(0x1.a36e3p-2),
    F32(0x1.0624dep-1), F32(0x1.47ae16p-1), F32(0x1.99999ap-1), F32(0x1p+0),
};

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
    /* 88: AO's history, for a scene with SHADE_FEATURE_AO, and god rays' target for one with SHADE_FEATURE_GOD_RAYS. */
    const post_temporal_t *borrowed_temporal;
    /* 96: what lod_frame wrote for this frame: what a ray that leaves the world goes on to meet, or nullptr for none. */
    const device_block_t *borrowed_lod_hits;
} shade_scene_t;
static_assert(sizeof(shade_scene_t) == 104);
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
static_assert(offsetof(shade_scene_t, borrowed_temporal) == 88);
static_assert(offsetof(shade_scene_t, borrowed_lod_hits) == 96);

/* One pixel, in linear light before the sky and the tone map: where post goes on from. */
typedef struct shade_pixel_t {
    float32_t rgb[3]; /* 0 */
    float32_t t;      /* 12: the hit's distance, or TRAVERSE_COARSE_SKY for sky */
} shade_pixel_t;
static_assert(sizeof(shade_pixel_t) == 16);
static_assert(alignof(shade_pixel_t) == 4);
static_assert(offsetof(shade_pixel_t, rgb) == 0);
static_assert(offsetof(shade_pixel_t, t) == 12);

/* What AO keeps of a pixel from frame to frame, about the surface its ray ended on. */
typedef struct shade_ao_pixel_t {
    float32_t visible; /* 0: the mean of its samples: 1 with nothing near, down towards 0 in a corner */
    float32_t plane;  /* 4: where that surface is along the axis it looks along, in blocks from that frame's eye cell */
    uint16_t samples; /* 8: how many it is the mean of, 0 if no surface */
    uint16_t face;    /* 10: a traverse_face_t */
} shade_ao_pixel_t;
static_assert(sizeof(shade_ao_pixel_t) == 12);
static_assert(alignof(shade_ao_pixel_t) == 4);
static_assert(offsetof(shade_ao_pixel_t, visible) == 0);
static_assert(offsetof(shade_ao_pixel_t, plane) == 4);
static_assert(offsetof(shade_ao_pixel_t, samples) == 8);
static_assert(offsetof(shade_ao_pixel_t, face) == 10);

static inline uint64_t shade_pixels_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * sizeof(shade_pixel_t);
}

/* A pixel's shade_ao_pixel_t, then behind them all a uint32_t per pixel and one more: a list of the pixels with nothing to go on, and how long it
 * is, for one frame, so that their rays run together: see cuda/src/shade/README.md. */
static inline uint64_t shade_ao_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * (sizeof(shade_ao_pixel_t) + sizeof(uint32_t)) + sizeof(uint32_t);
}

/* Whether this is a pixel's frame to send its AO ray. By groups of 8 by 4 pixels and not pixel by pixel, because that is a warp of a
 * kernel of TRAVERSE_TILE_PIXELS: a warp with one ray in it takes as long as that ray takes for all 32 threads, so one ray in four
 * spread evenly over the pixels costs nearly as much as a ray each. So of each two by two groups, one a frame. */
SHADE_BOTH_SIDES static inline bool shade_ao_turn(uint32_t frame, uint32_t pixel_x, uint32_t pixel_y) {
    const uint32_t place = ((pixel_x >> 3) & 1u) | (((pixel_y >> 2) & 1u) << 1);
    const uint32_t order = frame % SHADE_AO_TURNS;
    return place == (order == 0u ? 0u : (order == 1u ? 3u : (order == 2u ? 1u : 2u)));
}

/* A pixel's own fixed number, so that no two near each other walk the same way through their samples. */
SHADE_BOTH_SIDES static inline uint32_t shade_ao_hash(uint32_t pixel_x, uint32_t pixel_y) {
    uint32_t value = pixel_x * 73856093u ^ pixel_y * 19349663u;
    value ^= value >> 16;
    value *= 2654435769u;
    value ^= value >> 13;
    value *= 2246822519u;
    return value ^ (value >> 16);
}

/* A pixel's sample number n, as two numbers from 0 to 1: Roberts' R2 sequence from where its hash starts it, in 32 bits so that it
 * is as good on frame 1000000 as on the first. Any stretch of it covers the square evenly. */
SHADE_BOTH_SIDES static inline void shade_ao_sample(uint32_t hash, uint32_t n, float32_t *out_u, float32_t *out_v) {
    const uint32_t u = hash + n * 3242174889u;
    const uint32_t v = (hash * 2654435769u) + n * 2447445414u;
    *out_u = (float32_t)(u >> 8) / F32(16777216.0);
    *out_v = (float32_t)(v >> 8) / F32(16777216.0);
}

/* A unit direction off a face, cosine weighted about its normal, for two numbers from 0 to 1. Then the mean of what the rays see is
 * how much of the sky's light reaches the face, which falls on it by the same cosine. */
SHADE_BOTH_SIDES static inline void shade_ao_direction(uint32_t face, float32_t u, float32_t v, float32_t out[3]) {
    const float32_t radius = sqrtf(u);
    const float32_t angle = F32(6.28318530718) * v;
    const uint32_t axis = face >> 1;
    out[axis] = sqrtf(fmaxf(F32(1.0) - u, F32(0.0)));
    out[axis] = (face & 1u) != 0u ? out[axis] : -out[axis];
    out[(axis + 1u) % 3u] = radius * cosf(angle);
    out[(axis + 2u) % 3u] = radius * sinf(angle);
}

/* How much a surface t along an AO ray takes from what it sees: all, touching, and nothing by SHADE_AO_REACH, so that nothing pops
 * as it comes into reach. */
SHADE_BOTH_SIDES static inline float32_t shade_ao_near(float32_t t) {
    const float32_t x = t / SHADE_AO_REACH;
    return x >= F32(1.0) ? F32(0.0) : F32(1.0) - x * x;
}

/* Whether what a pixel has from the last frame is about this surface: the same way up, on the same plane. cell_moved is how far the
 * eye's cell went along that axis, from then to now. Rounding grows with distance. */
SHADE_BOTH_SIDES static inline bool shade_ao_same_surface(const shade_ao_pixel_t *last, uint32_t face, float32_t plane,
                                                          int32_t cell_moved, float32_t t) {
    if (last->samples == 0u || last->face != face) {
        return false;
    }
    return fabsf((float32_t)cell_moved + plane - last->plane) <= F32(0.01) + t * F32(4e-5);
}

/* How much of what the air scatters comes to the eye, by the cosine of the angle from the way it looks to the sun: Henyey-Greenstein. */
SHADE_BOTH_SIDES static inline float32_t shade_shaft_phase(float32_t cosine) {
    const float32_t g = SHADE_SHAFT_FORWARD;
    const float32_t under = F32(1.0) + g * g - F32(2.0) * g * cosine;
    return (F32(1.0) - g * g) / (F32(12.5663706144) * under * sqrtf(under));
}

/* Of the light from the eye to from, and from it to to, what the air between them adds, with what the air before them lets through
 * taken off. Added up along a ray they come to exactly 1 - e^(-density * length) however it is cut. */
SHADE_BOTH_SIDES static inline float32_t shade_shaft_part(float32_t from, float32_t to) {
    return expf(-SHADE_SHAFT_DENSITY * from) - expf(-SHADE_SHAFT_DENSITY * to);
}

/* A number from 0 to 1 that looks like noise and differs a lot between neighbours: Jimenez's, so that where a god ray asks about the
 * sun changes smoothly enough over the picture to be filtered away. */
SHADE_BOTH_SIDES static inline float32_t shade_shaft_noise(uint32_t x, uint32_t y, uint32_t frame) {
    const float32_t a = F32(0.06711056) * (float32_t)x + F32(0.00583715) * (float32_t)y +
                        F32(0.7548776662) * (float32_t)(frame % POST_JITTER_FRAMES);
    const float32_t b = F32(52.9829189) * (a - floorf(a));
    return b - floorf(b);
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
    return SHADE_LEVEL_LIGHT[level >= SHADE_LIGHT_LEVEL_MAX ? SHADE_LIGHT_LEVEL_MAX : level];
}

SHADE_BOTH_SIDES static inline float32_t shade_block_level(uint32_t level) {
    return level == 0u ? F32(0.0) : shade_sky_level(level);
}

/* How much of the sun a voxel with this stored light may get before asking a ray: none, where no sky light reaches. */
SHADE_BOTH_SIDES static inline float32_t shade_sun_towards(const shade_sun_t *sun, uint32_t face, uint32_t stored) {
    return (stored >> 4) == 0u ? F32(0.0) : shade_face_towards(face, sun->direction);
}

/* What falls on a surface, to multiply its colour by: the sky and block light stored in its voxel, under the face's brightness,
 * and the sun on top, as much of it as sun_seen says reached here. With AO, visible is how much of the way to it is open: what is
 * near takes from the stored light, which comes from all around, and not from the sun, whose own ray knows what is in its way. */
SHADE_BOTH_SIDES static inline void shade_surface_light_near(const shade_sun_t *sun, uint32_t face, uint32_t stored,
                                                             float32_t sun_seen, float32_t visible, float32_t out[3]) {
    const float32_t brightness = shade_face_brightness(face);
    const float32_t sky = shade_sky_level(stored >> 4), block = shade_block_level(stored & 15u);
    const float32_t towards = shade_sun_towards(sun, face, stored);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out[i] = brightness * (sun->ambient[i] * sky + sun->block[i] * block) * visible +
                 sun->direct[i] * towards * sun_seen;
    }
}

/* Of that, the part AO takes from: what comes out for a visible of 1, with no sun. */
SHADE_BOTH_SIDES static inline void shade_surface_light_stored(const shade_sun_t *sun, uint32_t face, uint32_t stored,
                                                               float32_t out[3]) {
    const float32_t brightness = shade_face_brightness(face);
    const float32_t sky = shade_sky_level(stored >> 4), block = shade_block_level(stored & 15u);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out[i] = brightness * (sun->ambient[i] * sky + sun->block[i] * block);
    }
}

/* The same with nothing near. */
SHADE_BOTH_SIDES static inline void shade_surface_light(const shade_sun_t *sun, uint32_t face, uint32_t stored,
                                                        float32_t sun_seen, float32_t out[3]) {
    shade_surface_light_near(sun, face, stored, sun_seen, F32(1.0), out);
}

/* What falls on a face of a box, which may be turned: as on a block's face under the same light and sun, with nothing near, but for
 * how it looks towards the sun. normal is on the world's axes, and face on the box's. */
SHADE_BOTH_SIDES static inline void shade_box_light(const shade_sun_t *sun, uint32_t face, const float32_t normal[3],
                                                    uint32_t stored, float32_t sun_seen, float32_t out[3]) {
    const float32_t brightness = shade_face_brightness(face);
    const float32_t sky = shade_sky_level(stored >> 4), block = shade_block_level(stored & 15u);
    const float32_t along =
        normal[0] * sun->direction[0] + normal[1] * sun->direction[1] + normal[2] * sun->direction[2];
    const float32_t towards = (stored >> 4) == 0u || along <= F32(0.0) ? F32(0.0) : along;
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

/* The same for a texel's byte, which is all a kernel ever has: see SHADE_SRGB_BYTE. */
SHADE_BOTH_SIDES static inline float32_t shade_srgb_byte(uint32_t byte) {
    return SHADE_SRGB_BYTE[byte];
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

/* What a far surface looks like: a coarse voxel has no texels to speak of, so it is its face's mean colour under that face's light. Water
 * that is deep and clear is all a far ray can tell of it, with no ray to its bottom, so it is what near water that deep comes to: the sky
 * off its surface with reflect, for how square the ray meets it, then the texture's mean as far as that covers, over what deep water
 * gives back. Water has no sides: a coarse voxel's side is only where a level's rounding of the surface shows, so it is lit and mirrors as
 * a top does, and asks no sun of its own, as lod_frame sends no ray for a side turned from the sun. And from in_water, a ray that came in
 * through near water, a far voxel of water is more of the same: there is no surface to it, only what deep water gives back. */
SHADE_BOTH_SIDES static inline void shade_far_colour(const shade_sun_t *sun, const uint8_t mean_rgba[4],
                                                     bool deep_water, bool reflect, bool in_water, const lod_hit_t *hit,
                                                     const float32_t direction[3], const float32_t sky[3],
                                                     float32_t out[3]) {
    const bool water = deep_water || reflect;
    const uint32_t face = water ? (uint32_t)TRAVERSE_FACE_POS_Z : hit->face;
    const bool sunlit = hit->sun == LOD_SUN_SEEN || (water && hit->face != TRAVERSE_FACE_POS_Z);
    float32_t light[3];
    shade_surface_light(sun, face, SHADE_LIGHT_OPEN_SKY, sunlit ? F32(1.0) : F32(0.0), light);
    const float32_t deep[3] = {SHADE_WATER_DEEP_R, SHADE_WATER_DEEP_G, SHADE_WATER_DEEP_B};
    const float32_t cosine = direction[face >> 1] < F32(0.0) ? -direction[face >> 1] : direction[face >> 1];
    const float32_t share = reflect && !in_water ? shade_water_fresnel(cosine) : F32(0.0);
    const float32_t cover = in_water ? F32(0.0) : (float32_t)mean_rgba[3] / F32(255.0);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t mean = shade_srgb_byte(mean_rgba[i]);
        const float32_t own = deep_water ? mean * cover + deep[i] * (F32(1.0) - cover) : mean;
        out[i] = own * light[i] * (F32(1.0) - share) + sky[i] * share;
    }
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
 * post_frame is about to use: what shows behind glass, and in water, is shaded here. Starts its kernels and returns; they run in order
 * with any other. Nothing here allocates. On failure error says why, and nothing was started. */
gpu_status_t shade_frame(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                         const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits,
                         const device_block_t *pixels);

/* The same, with entities: a box a ray meets before whatever it would have stopped on is what it stops on, and the block being mined
 * has its crack. scene is what render_entities_frame said, or nullptr for neither, and entity_rays_frame has been started for it. */
gpu_status_t shade_frame_entities(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                                  const traverse_world_t *world, const traverse_camera_t *camera,
                                  const device_block_t *hits, const device_block_t *pixels,
                                  const entity_rays_scene_t *entities);

/* God rays: for every second pixel each way of what shade_frame just wrote to pixels, how much sun the air along its ray shows, into
 * scene's borrowed_temporal's borrowed_shafts. Here and not in post, as a sun ray needs the textures and the loop above. Starts a
 * kernel and returns. On failure error says why, and nothing was started. */
gpu_status_t shade_shafts_frame(gpu_error_t *error, const shade_scene_t *scene, const traverse_world_t *world,
                                const traverse_camera_t *camera, const device_block_t *pixels);

#ifdef __cplusplus
}
#endif

#endif
