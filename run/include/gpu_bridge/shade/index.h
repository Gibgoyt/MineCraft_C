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
#include "tuning/render/shade/index.h"
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
    /* How many surfaces a pixel looks at, holes and all, past which it stops on the last one; and how many a sun ray looks at before it
     * calls the sun hidden: compile/config/render/shade/index.yaml, and cuda/src/shade/README.md. */
    SHADE_ALPHA_LAYERS = MC_TUNE_RENDER_SHADE_ALPHA_LAYERS,
    SHADE_SUN_LAYERS = MC_TUNE_RENDER_SHADE_SUN_LAYERS,
    /* Plan 009 phase 06: past SHADE_ALPHA_FAR_FROM blocks, where a leaf's texel is under a pixel, a pixel looks at this many surfaces at most:
     * see shade_alpha_layers_at. */
    SHADE_ALPHA_LAYERS_FAR = MC_TUNE_RENDER_SHADE_ALPHA_LAYERS_FAR,
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
    SHADE_FEATURE_LOD = 1u << 15, /* the world goes on past level 0 in coarse grids, or stops there */
    /* To render_frame_features_set only, and past that post_temporal_t.sun_apart, as it is no effect that shows: a last surface's sun ray has a
     * kernel of its own, even with AO off: the same picture, for less time, and the hits block then ends up holding those surfaces. No setting
     * turns this off. What wants the frame's own hits afterwards, a test that holds them to something, leaves it out. */
    SHADE_FEATURE_SUN_APART = 1u << 16,
    /* From under water the picture is blue and the distance goes in fog, or it is what the rays alone make of it, often black. */
    SHADE_FEATURE_UNDER_WATER = 1u << 17,
    /* Rain, snow, lightning, cloud and weather fog where the eye is (plan 004 phase 08), or the sky of a clear day always. Only what
     * the sim sends counts: with no weather sent, on or off is the same picture. */
    SHADE_FEATURE_WEATHER = 1u << 18,
    /* Plan 008 phase 04 (Q2 A): while the camera moves, a pixel newly seen takes its first AO from the world-space AO cache, or a corner
     * estimate from the blocks around its face, and sends no rays; or it sends SHADE_AO_FRESH_RAYS at once. As SHADE_FEATURE_SUN_APART: to
     * render_frame_features_set only, and past that a bit of post_temporal_t.flags. The game asks for it unless its features_off has it, so
     * bench_world_shake --features-off 80000 turns it off; nothing on the Settings screen does, and a still camera's picture is the same
     * either way. */
    SHADE_FEATURE_AO_FIRST_GUESS = 1u << 19,
    /* Plan 008 phase 06: the far field keeps each direction's far ray in a cube round the eye, and a frame that turns reads it in place of
     * tracing again; or every far ray is traced. As SHADE_FEATURE_AO_FIRST_GUESS: asked for by the game unless its features_off has it
     * (bench_world_shake --features-off 100000), on no Settings screen, and a still camera's picture is the same either way. */
    SHADE_FEATURE_LOD_TURN_CACHE = 1u << 20,
    /* What a picture is drawn with on its own, from nothing: no frame of these reads what the last one left. What render_frame_init
     * starts with, and so what a test that wants one exact picture gets. */
    SHADE_FEATURES_ALL = SHADE_FEATURE_SHAPES | SHADE_FEATURE_ALPHA | SHADE_FEATURE_SKY | SHADE_FEATURE_DAY_CYCLE |
                         SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW | SHADE_FEATURE_LIGHT | SHADE_FEATURE_TRANSPARENCY |
                         SHADE_FEATURE_WATER_REFLECTION | SHADE_FEATURE_WATER_TINT | SHADE_FEATURE_ENTITIES |
                         SHADE_FEATURE_OUTLINE | SHADE_FEATURE_LOD | SHADE_FEATURE_UNDER_WATER | SHADE_FEATURE_WEATHER,
    /* What a world is played with unless told otherwise. God rays are optional, so they are asked for. */
    SHADE_FEATURES_PLAY = SHADE_FEATURES_ALL | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
    SHADE_FEATURES_EVERY = SHADE_FEATURES_PLAY | SHADE_FEATURE_GOD_RAYS,
};

enum : uint32_t {
    /* A pixel with history sends its one AO ray on one frame of this many, in turn with the three next to it. A pixel with nothing from
     * the last frame on its surface, itself or around it, sends SHADE_AO_FRESH_RAYS, as it has nothing else. A pixel's AO is the mean of
     * at most SHADE_AO_SAMPLES_MAX samples. All three: compile/config/render/shade/index.yaml. */
    SHADE_AO_TURNS = MC_TUNE_RENDER_SHADE_AO_TURNS,
    SHADE_AO_FRESH_RAYS = MC_TUNE_RENDER_SHADE_AO_FRESH_RAYS,
    SHADE_AO_SAMPLES_MAX = MC_TUNE_RENDER_SHADE_AO_SAMPLES_MAX,
    /* How many surfaces an AO ray looks at, holes and all, before it stops looking. */
    SHADE_AO_LAYERS = 4u,
    /* An AO ray's cap on steps: a ray of SHADE_AO_REACH crosses no more than 3 * sqrt(3) + 3 planes between cells, and each step
     * crosses at least one, so this always gets past its reach. So a capped ray has met nothing. */
    SHADE_AO_STEPS = 12u,
    /* How many places a god ray asks about the sun: compile/config/render/shade/index.yaml. */
    SHADE_SHAFT_STEPS = MC_TUNE_RENDER_SHADE_SHAFT_STEPS,
};
/* shade_ao_turn's table has four places, one per warp of a 2 x 2 group. */
static_assert(SHADE_AO_TURNS >= 1u && SHADE_AO_TURNS <= 4u);
/* Both are counted in shade_ao_pixel_t.samples, a uint16_t. */
static_assert(SHADE_AO_SAMPLES_MAX <= UINT16_MAX && SHADE_AO_FRESH_RAYS <= UINT16_MAX);

/* How far an AO ray looks, in blocks: compile/config/render/shade/index.yaml. */
#define SHADE_AO_REACH MC_TUNE_RENDER_SHADE_AO_REACH
/* What a corner between two walls comes to in the corner estimate: compile/config/render/shade/index.yaml. */
#define SHADE_AO_CORNER_FLOOR MC_TUNE_RENDER_SHADE_AO_CORNER_FLOOR

/* Plan 008 phase 04: the world-space AO cache. One uint64_t an entry, 2^21 of them (16 MiB), in a block of its own. An entry, from its high bits
 * to its low: the frame it was written, modulo 2^21; how many samples its mean is of; 16 bits of its key; its face; and that mean, 0 to 1 in
 * 1/65535. Written by atomicMax, so the newest wins, then the one of most samples: the same entries whatever order the card writes them in. */
enum : uint32_t {
    SHADE_AO_CACHE_LOG2 = 21u,
    SHADE_AO_CACHE_ENTRIES = 1u << SHADE_AO_CACHE_LOG2,
    SHADE_AO_CACHE_AGE_BITS = 21u,
    SHADE_AO_CACHE_AGE_MASK = (1u << SHADE_AO_CACHE_AGE_BITS) - 1u,
    SHADE_AO_CACHE_VISIBLE_SHIFT = 0u,
    SHADE_AO_CACHE_FACE_SHIFT = 16u,
    SHADE_AO_CACHE_TAG_SHIFT = 19u,
    SHADE_AO_CACHE_SAMPLES_SHIFT = 35u,
    SHADE_AO_CACHE_AGE_SHIFT = 43u,
    /* A face is cut 4 by 4 for it: a quarter of a block each way. */
    SHADE_AO_CACHE_PLACES = 4u,
    /* An entry older than this many frames is a miss, and a pixel takes at most this many samples from one: the YAML. */
    SHADE_AO_CACHE_AGE = MC_TUNE_RENDER_SHADE_AO_CACHE_AGE_FRAMES,
    SHADE_AO_CACHE_SAMPLES_CAP = MC_TUNE_RENDER_SHADE_AO_CACHE_SAMPLES_CAP,
    SHADE_AO_CACHE_STILL_EVERY = MC_TUNE_RENDER_SHADE_AO_CACHE_STILL_EVERY,
};
static_assert((uint32_t)SHADE_AO_TURNS * (uint32_t)SHADE_AO_CACHE_STILL_EVERY < (uint32_t)SHADE_AO_CACHE_AGE);
static_assert(SHADE_AO_CACHE_AGE_SHIFT + SHADE_AO_CACHE_AGE_BITS == 64u);
static_assert(SHADE_AO_CACHE_AGE < SHADE_AO_CACHE_AGE_MASK / 2u);
static_assert(SHADE_AO_CACHE_SAMPLES_CAP <= 255u);

/* The corner estimate's cells, as bits: of the eight around the open cell in front of a face, in its plane, which are solid. u and v are
 * the face's two axes after its own, in turn: (axis + 1) % 3 and (axis + 2) % 3. */
enum : uint32_t {
    SHADE_AO_CELL_U_LOW = 1u << 0,
    SHADE_AO_CELL_U_HIGH = 1u << 1,
    SHADE_AO_CELL_V_LOW = 1u << 2,
    SHADE_AO_CELL_V_HIGH = 1u << 3,
    SHADE_AO_CELL_LOW_LOW = 1u << 4, /* u low, v low */
    SHADE_AO_CELL_HIGH_LOW = 1u << 5,
    SHADE_AO_CELL_LOW_HIGH = 1u << 6,
    SHADE_AO_CELL_HIGH_HIGH = 1u << 7,
};
#ifdef __cplusplus
/* The step cap must get past the reach: SHADE_AO_STEPS's comment. C cannot compare floats in a static_assert, so nvcc checks it. */
static_assert((float32_t)SHADE_AO_STEPS >= SHADE_AO_REACH * F32(1.7320508) + F32(3.0));
#endif
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
/* Past this many blocks of water nothing behind it can be told from none, so a ray with tint on looks no further:
 * compile/config/render/shade/index.yaml. */
#define SHADE_WATER_SEEN_BLOCKS MC_TUNE_RENDER_SHADE_WATER_SEEN_BLOCKS
/* Plan 009 phase 06: past this many blocks a pixel's ray goes through at most SHADE_ALPHA_LAYERS_FAR surfaces. */
#define SHADE_ALPHA_FAR_FROM MC_TUNE_RENDER_SHADE_ALPHA_FAR_FROM_BLOCKS
/* Plan 009 phase 06: the light's angular diameter in degrees, and how much coarser than its cone a shadow ray may go (1 is physical). */
#define SHADE_LIGHT_ANGULAR_DIAMETER MC_TUNE_RENDER_SHADE_LIGHT_ANGULAR_DIAMETER
#define SHADE_SHADOW_CONE_SCALE MC_TUNE_RENDER_SHADE_SHADOW_CONE_SCALE
/* Plan 009 phase 06: the dim-light cut. No shadow ray when one could move no pixel by more than this share of a byte's range after the tone
 * map: 2 / 255. */
#define SHADE_SHADOW_SKIP_BELOW MC_TUNE_RENDER_SHADE_SHADOW_SKIP_BELOW
/* The steepest the tone map ever is, in bytes' range per unit of linear light: sRGB's straight foot (post_tone_map). */
#define SHADE_TONE_MAP_STEEPEST F32(12.92)

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

/* Plan 008 phase 04: a face's AO with no rays, as a block game darkens its corners. Each corner of the face takes its two side cells s1 and s2
 * and the diagonal c between them: 0 if both sides are solid, else 1 - (s1 + s2 + c) / 3; then SHADE_AO_CORNER_FLOOR up to 1. The four are
 * blended by where on the face, u and v from 0 to 1. solid is SHADE_AO_CELL_* bits. */
SHADE_BOTH_SIDES static inline float32_t shade_ao_corner(uint32_t solid, float32_t u, float32_t v) {
    const float32_t across = fminf(fmaxf(u, F32(0.0)), F32(1.0)), down = fminf(fmaxf(v, F32(0.0)), F32(1.0));
    float32_t corner[4];
    for (uint32_t i = 0u; i < 4u; i += 1u) {
        const uint32_t high_u = i & 1u, high_v = i >> 1;
        const uint32_t s1 = (solid >> (high_u != 0u ? 1u : 0u)) & 1u;
        const uint32_t s2 = (solid >> (high_v != 0u ? 3u : 2u)) & 1u;
        const uint32_t c = (solid >> (4u + i)) & 1u;
        const float32_t open = s1 != 0u && s2 != 0u ? F32(0.0) : F32(1.0) - (float32_t)(s1 + s2 + c) / F32(3.0);
        corner[i] = SHADE_AO_CORNER_FLOOR + (F32(1.0) - SHADE_AO_CORNER_FLOOR) * open;
    }
    const float32_t low = corner[0] + (corner[1] - corner[0]) * across;
    const float32_t high = corner[2] + (corner[3] - corner[2]) * across;
    return low + (high - low) * down;
}

/* A fixed 64-bit mix: splitmix64's last steps, shade_ao_hash widened. */
SHADE_BOTH_SIDES static inline uint64_t shade_ao_mix(uint64_t value) {
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

/* The AO cache's key for a place on a face: the world cell of the block, the face, which of the 4 x 4 squares of it, the load generation of the
 * column in front of it and the revision of the sections around it (world_buffers_column_t.revisions, as shade_ao_revision combines them), so
 * that an edit near it gives every key there a new one. */
SHADE_BOTH_SIDES static inline uint64_t shade_ao_cache_key(const int32_t cell[3], uint32_t face, uint32_t place,
                                                           uint32_t generation, uint32_t revision) {
    const uint64_t across = (uint64_t)(uint32_t)cell[0] | ((uint64_t)(uint32_t)cell[1] << 32);
    const uint64_t up = (uint64_t)(uint32_t)cell[2] | ((uint64_t)(face & 7u) << 32) | ((uint64_t)(place & 15u) << 35);
    return shade_ao_mix(shade_ao_mix(shade_ao_mix(across) ^ up) ^ ((uint64_t)revision << 32 | (uint64_t)generation));
}

/* One more section's revision into a key's: in a fixed order, so that the same sections give the same number. */
SHADE_BOTH_SIDES static inline uint32_t shade_ao_revision_add(uint32_t revision, uint32_t section) {
    return revision * 0x9E3779B1u + section + 1u;
}

/* Which square of the 4 x 4 a place on a face is in, from where on it, 0 to 1 each way. */
SHADE_BOTH_SIDES static inline uint32_t shade_ao_cache_place(float32_t u, float32_t v) {
    const int32_t last = (int32_t)SHADE_AO_CACHE_PLACES - 1;
    const int32_t x = (int32_t)(u * (float32_t)SHADE_AO_CACHE_PLACES),
                  y = (int32_t)(v * (float32_t)SHADE_AO_CACHE_PLACES);
    const uint32_t cx = (uint32_t)(x < 0 ? 0 : (x > last ? last : x)),
                   cy = (uint32_t)(y < 0 ? 0 : (y > last ? last : y));
    return cy * SHADE_AO_CACHE_PLACES + cx;
}

SHADE_BOTH_SIDES static inline uint32_t shade_ao_cache_slot(uint64_t key) {
    return (uint32_t)(key & (uint64_t)(SHADE_AO_CACHE_ENTRIES - 1u));
}

SHADE_BOTH_SIDES static inline uint32_t shade_ao_cache_tag(uint64_t key) {
    return (uint32_t)(key >> 48);
}

/* One entry, unpacked. */
typedef struct shade_ao_cache_entry_t {
    uint32_t age;     /* 0: the frame it was written, modulo 2^21 */
    uint16_t tag;     /* 4 */
    uint16_t visible; /* 6: 0 to 65535 */
    uint8_t samples;  /* 8: 0 for an entry never written */
    uint8_t face;     /* 9 */
    uint8_t _pad0[2]; /* 10 */
} shade_ao_cache_entry_t;
static_assert(sizeof(shade_ao_cache_entry_t) == 12);
static_assert(alignof(shade_ao_cache_entry_t) == 4);
static_assert(offsetof(shade_ao_cache_entry_t, age) == 0);
static_assert(offsetof(shade_ao_cache_entry_t, tag) == 4);
static_assert(offsetof(shade_ao_cache_entry_t, visible) == 6);
static_assert(offsetof(shade_ao_cache_entry_t, samples) == 8);
static_assert(offsetof(shade_ao_cache_entry_t, face) == 9);
static_assert(offsetof(shade_ao_cache_entry_t, _pad0) == 10);

SHADE_BOTH_SIDES static inline uint64_t shade_ao_cache_pack(const shade_ao_cache_entry_t *entry) {
    return ((uint64_t)(entry->age & SHADE_AO_CACHE_AGE_MASK) << SHADE_AO_CACHE_AGE_SHIFT) |
           ((uint64_t)entry->samples << SHADE_AO_CACHE_SAMPLES_SHIFT) |
           ((uint64_t)entry->tag << SHADE_AO_CACHE_TAG_SHIFT) |
           ((uint64_t)(entry->face & 7u) << SHADE_AO_CACHE_FACE_SHIFT) |
           ((uint64_t)entry->visible << SHADE_AO_CACHE_VISIBLE_SHIFT);
}

SHADE_BOTH_SIDES static inline void shade_ao_cache_unpack(uint64_t word, shade_ao_cache_entry_t *out) {
    out->age = (uint32_t)(word >> SHADE_AO_CACHE_AGE_SHIFT) & SHADE_AO_CACHE_AGE_MASK;
    out->samples = (uint8_t)(word >> SHADE_AO_CACHE_SAMPLES_SHIFT);
    out->tag = (uint16_t)(word >> SHADE_AO_CACHE_TAG_SHIFT);
    out->face = (uint8_t)((word >> SHADE_AO_CACHE_FACE_SHIFT) & 7u);
    out->visible = (uint16_t)(word >> SHADE_AO_CACHE_VISIBLE_SHIFT);
    out->_pad0[0] = 0u;
    out->_pad0[1] = 0u;
}

/* What a pixel of this many samples, of this mean, writes for a key on this frame. */
SHADE_BOTH_SIDES static inline uint64_t shade_ao_cache_word(uint64_t key, uint32_t face, uint32_t frame,
                                                            float32_t visible, uint32_t samples) {
    const float32_t clamped = fminf(fmaxf(visible, F32(0.0)), F32(1.0));
    const shade_ao_cache_entry_t entry = {frame,
                                          (uint16_t)shade_ao_cache_tag(key),
                                          (uint16_t)(clamped * F32(65535.0) + F32(0.5)),
                                          (uint8_t)(samples > 255u ? 255u : samples),
                                          (uint8_t)face,
                                          {0u, 0u}};
    return shade_ao_cache_pack(&entry);
}

/* Whether an entry answers for a key and face on this frame: written, the same tag and face, and younger than SHADE_AO_CACHE_AGE. If so, its
 * mean, and its samples up to SHADE_AO_CACHE_SAMPLES_CAP. */
SHADE_BOTH_SIDES static inline bool shade_ao_cache_hit(uint64_t word, uint64_t key, uint32_t face, uint32_t frame,
                                                       float32_t *out_visible, uint32_t *out_samples) {
    shade_ao_cache_entry_t entry;
    shade_ao_cache_unpack(word, &entry);
    const uint32_t age = (frame - entry.age) & SHADE_AO_CACHE_AGE_MASK;
    if (entry.samples == 0u || entry.tag != shade_ao_cache_tag(key) || entry.face != face ||
        age >= SHADE_AO_CACHE_AGE) {
        return false;
    }
    *out_visible = (float32_t)entry.visible / F32(65535.0);
    *out_samples = entry.samples < SHADE_AO_CACHE_SAMPLES_CAP ? entry.samples : (uint32_t)SHADE_AO_CACHE_SAMPLES_CAP;
    return true;
}

/* Whether a frame's rays write the cache: every frame while the camera moves, and with it still one turn in SHADE_AO_CACHE_STILL_EVERY, so
 * that a still camera pays little for it (measured: writing on every turn made AO's history pass 16 % dearer) and what it sees is there, at
 * most SHADE_AO_TURNS * SHADE_AO_CACHE_STILL_EVERY frames old, when it turns away and back. */
SHADE_BOTH_SIDES static inline bool shade_ao_cache_writes(uint32_t flags, uint32_t frame) {
    return (flags & POST_TEMPORAL_MOVED) != 0u || (frame / SHADE_AO_TURNS) % SHADE_AO_CACHE_STILL_EVERY == 0u;
}

static inline uint64_t shade_ao_cache_bytes(void) {
    return (uint64_t)SHADE_AO_CACHE_ENTRIES * sizeof(uint64_t);
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
/* Plan 009 phase 06: how many surfaces a pixel's ray may look at, holes and all, when the one it is at is t blocks off. Past
 * SHADE_ALPHA_FAR_FROM, where a leaf's texel is under a pixel, which hole a ray finds is noise that TAA averages, and fewer cost less. */
SHADE_BOTH_SIDES static inline uint32_t shade_alpha_layers_at(float32_t t) {
    return t > SHADE_ALPHA_FAR_FROM ? (uint32_t)SHADE_ALPHA_LAYERS_FAR : (uint32_t)SHADE_ALPHA_LAYERS;
}

/* Plan 009 phase 06: the light's cone. An occluder L blocks off blurs a real shadow's edge over L tan(diameter) blocks, so a level whose surface
 * is off by up to half its voxel shows nothing a real light would not blur away once L reaches that half over the blur: L_k = 2^k times this.
 * 54 at the defaults, so level 1 from 108 blocks, level 2 from 216, level 3 from 432. */
SHADE_BOTH_SIDES static inline float32_t shade_shadow_cone_base(void) {
    const float32_t radians = SHADE_LIGHT_ANGULAR_DIAMETER * F32(0.017453292519943295);
    return F32(0.5) / (tanf(radians) * SHADE_SHADOW_CONE_SCALE);
}

/* Plan 009 phase 06, the dim-light cut: whether a light this bright could move no pixel by SHADE_SHADOW_SKIP_BELOW, and so sends no shadow
 * ray: the light then reaches every surface facing it, as with shadows off. A shadow takes at most direct from a pixel, a white texel square to
 * the light; through the tone map's steepest part that is SHADE_TONE_MAP_STEEPEST times as much. Asked of the sun a frame is drawn with, after
 * render_sky_fill and the weather (render_weather_apply takes direct down under cloud), so both decide it. */
SHADE_BOTH_SIDES static inline bool shade_shadows_skipped(const float32_t direct[3]) {
    const float32_t most = fmaxf(direct[0], fmaxf(direct[1], direct[2]));
    return most * SHADE_TONE_MAP_STEEPEST <= SHADE_SHADOW_SKIP_BELOW;
}

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

/* Plan 009 phase 07: the same for a picture drawn smaller than it is shown, rows being the shown picture's (0 for the camera's own): a texture
 * drawn at 75 % takes the mip it would at 100 %, so that the frames gathered at the output's size come to the 100 % picture. The same bits as
 * shade_pixel_tangent when rows is the camera's. */
SHADE_BOTH_SIDES static inline float32_t shade_pixel_tangent_rows(const traverse_camera_t *camera, uint32_t rows) {
    const float32_t squared =
        camera->up[0] * camera->up[0] + camera->up[1] * camera->up[1] + camera->up[2] * camera->up[2];
    return F32(2.0) * sqrtf(squared) / (float32_t)(rows != 0u ? rows : camera->height);
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
 * both together come to post_fog_along at t. direction_z is the z of the eye's own ray, and a way that a mirror bent is taken as if it had gone on along that. */
SHADE_BOTH_SIDES static inline float32_t shade_fog_behind(const post_settings_t *settings, float32_t direction_z,
                                                          float32_t first, float32_t t) {
    const float32_t near_part = post_fog_along(settings, direction_z, first);
    return near_part >= F32(1.0) ? F32(0.0)
                                 : (post_fog_along(settings, direction_z, t) - near_part) / (F32(1.0) - near_part);
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

/* Plan 008 phase 01: shade's own kernels each between two clock marks, and where the fresh list's length is. */
typedef enum shade_span_t : uint32_t {
    SHADE_SPAN_WATER = 0,    /* what is seen through water and glass */
    SHADE_SPAN_SURFACES = 1, /* every pixel's surface, sky and fog */
    SHADE_SPAN_SUN = 2,      /* the rays towards the sun */
    SHADE_SPAN_AO = 3,       /* AO's history pass: one ray on one frame in four, and the fresh list made */
    SHADE_SPAN_FRESH = 4,    /* fresh AO: SHADE_AO_FRESH_RAYS rays for every pixel on that list */
    SHADE_SPAN_COUNT = 5,
} shade_span_t;
static_assert(sizeof(shade_span_t) == 4);

typedef struct shade_marks_t {
    /* 0: CUDA events, recorded at the end of each span but the last, whose end is the caller's next mark: whatever does not run is a span
     * of nothing. Borrowed from whoever made them. */
    void *borrowed_events[SHADE_SPAN_COUNT - 1u];
    /* 32: written by shade_frame_marked: where on the card this frame's fresh list keeps its length, or nullptr with AO off. It stays
     * there until the next frame that writes the same AO block. */
    const uint32_t *fresh_count;
    /* 40: plan 009 phase 01: a counts block (see traverse_counts_t), or nullptr. With one, shade runs the counted copies of its kernels
     * and every sun, AO, alpha and reflection ray's steps go into it, by pixel; the picture is the same bits. */
    const device_block_t *borrowed_counts;
} shade_marks_t;
static_assert(sizeof(shade_marks_t) == 48);
static_assert(alignof(shade_marks_t) == 8);
static_assert(offsetof(shade_marks_t, borrowed_events) == 0);
static_assert(offsetof(shade_marks_t, fresh_count) == 32);
static_assert(offsetof(shade_marks_t, borrowed_counts) == 40);

/* shade_frame_entities with marks, or without if marks is nullptr. Plan 009 phase 06: with lod, this frame's far field (what lod_frame was
 * given), and SHADE_FEATURE_LOD on, a shadow ray goes through level 0 only as far as the light's cone lets level 1 take over, and on through
 * the levels from there (shade_shadow_cone_base); with nullptr it goes through level 0 to its edge, as before. lod is read during the call.
 * Plan 009 phase 07: mip_rows is the rows of the picture as it is shown, by which a texture's mip is chosen (shade_pixel_tangent_rows): the
 * output's when the camera is drawn smaller and gathered at the output's size, or 0 for the camera's own. */
gpu_status_t shade_frame_marked(gpu_error_t *error, const shade_scene_t *scene, const post_settings_t *post,
                                const traverse_world_t *world, const traverse_camera_t *camera,
                                const device_block_t *hits, const device_block_t *pixels,
                                const entity_rays_scene_t *entities, const lod_scene_t *lod, uint32_t mip_rows,
                                shade_marks_t *marks);

/* A span's name, for a profile and the frame log. Lasts for good. */
const char *shade_span_name(shade_span_t span);

/* God rays: for every second pixel each way of what shade_frame just wrote to pixels, how much sun the air along its ray shows, into
 * scene's borrowed_temporal's borrowed_shafts. Here and not in post, as a sun ray needs the textures and the loop above. Starts a
 * kernel and returns. On failure error says why, and nothing was started. */
gpu_status_t shade_shafts_frame(gpu_error_t *error, const shade_scene_t *scene, const traverse_world_t *world,
                                const traverse_camera_t *camera, const device_block_t *pixels);

#ifdef __cplusplus
}
#endif

#endif
