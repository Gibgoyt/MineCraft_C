/* See run/src/gpu_bridge/lod/README.md and cuda/src/lod/README.md, which implements this. C API, compiled by GCC and by nvcc: both
 * sides check these static_asserts. A level of the far field is a world of its own in the format of gpu_bridge/world_buffers, whose
 * voxel is 1 << shift blocks each way. */
#ifndef MC_RUN_GPU_BRIDGE_LOD_INDEX_H
#define MC_RUN_GPU_BRIDGE_LOD_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "tuning/render/lod/index.h"
#include "types/index.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __CUDACC__
#define LOD_BOTH_SIDES __host__ __device__
#else
#define LOD_BOTH_SIDES
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum : uint32_t {
    /* Voxels of 2, 4, 8, 16, 32 and 64 blocks: the last reaches past a render distance of 512. */
    LOD_LEVELS_MAX = 6u,
    /* The finest level's: one block per 8 cubic metres, as the plan says. */
    LOD_SHIFT_MIN = 1u,
    /* lod_hit_t.material when a ray found nothing. */
    LOD_NO_MATERIAL = 0xFFFFu,
    /* lod_hit_t.sun: all of the sun, and none of it. */
    LOD_SUN_SEEN = 255u,
    LOD_SUN_HIDDEN = 0u,
};

/* One level as kernels read it. The table holds side columns each way, of 16 voxels each, which scroll as the world's do. Rays are
 * only let into the square of shown_side columns at shown_min: every column there has been built, so a level never shows a hole. */
typedef struct lod_level_t {
    const device_block_t *borrowed_table; /* 0: window.slot_count world_buffers_column_t */
    const device_block_t *borrowed_pool;  /* 8 */
    world_buffers_window_t window;        /* 16 */
    int32_t shown_min_x;                  /* 40: in this level's columns */
    int32_t shown_min_y;                  /* 44 */
    uint32_t shown_side;                  /* 48: 0 for none yet */
    uint32_t shift;                       /* 52: a voxel is 1 << shift blocks each way */
} lod_level_t;
static_assert(sizeof(lod_level_t) == 56);
static_assert(alignof(lod_level_t) == 8);
static_assert(offsetof(lod_level_t, borrowed_table) == 0);
static_assert(offsetof(lod_level_t, borrowed_pool) == 8);
static_assert(offsetof(lod_level_t, window) == 16);
static_assert(offsetof(lod_level_t, shown_min_x) == 40);
static_assert(offsetof(lod_level_t, shown_min_y) == 44);
static_assert(offsetof(lod_level_t, shown_side) == 48);
static_assert(offsetof(lod_level_t, shift) == 52);

/* All of the far field, finest first. Borrowed blocks, not written while a kernel may run but on its stream, as the world's are. */
typedef struct lod_scene_t {
    lod_level_t levels[LOD_LEVELS_MAX];  /* 0 */
    const device_block_t *borrowed_hits; /* 336: lod_hits_bytes */
    uint32_t level_count;                /* 344 */
    int32_t top_storage;                 /* 348: no level holds a block at this storage height or above */
    /* 352: plan 008 phase 06, or nullptr for none: lod_frame reads and writes it, and keeps its state (lod_turn_cache_t, below) */
    struct lod_turn_cache_t *borrowed_turn;
    uint32_t revision; /* 360: changes whenever what any level shows does */
    uint8_t _pad0[4];  /* 364 */
} lod_scene_t;
static_assert(sizeof(lod_scene_t) == 368);
static_assert(alignof(lod_scene_t) == 8);
static_assert(offsetof(lod_scene_t, levels) == 0);
static_assert(offsetof(lod_scene_t, borrowed_hits) == 336);
static_assert(offsetof(lod_scene_t, level_count) == 344);
static_assert(offsetof(lod_scene_t, top_storage) == 348);
static_assert(offsetof(lod_scene_t, borrowed_turn) == 352);
static_assert(offsetof(lod_scene_t, revision) == 360);
static_assert(offsetof(lod_scene_t, _pad0) == 364);

/* What a ray found in the far field, one per pixel. */
typedef struct lod_hit_t {
    float32_t t;       /* 0: in blocks from the eye, or TRAVERSE_COARSE_SKY for nothing */
    uint16_t material; /* 4: the block the voxel is drawn as, or LOD_NO_MATERIAL */
    uint8_t face;      /* 6: a traverse_face_t */
    uint8_t sun;       /* 7: LOD_SUN_SEEN or LOD_SUN_HIDDEN */
} lod_hit_t;
static_assert(sizeof(lod_hit_t) == 8);
static_assert(alignof(lod_hit_t) == 4);
static_assert(offsetof(lod_hit_t, t) == 0);
static_assert(offsetof(lod_hit_t, material) == 4);
static_assert(offsetof(lod_hit_t, face) == 6);
static_assert(offsetof(lod_hit_t, sun) == 7);

/* ---- Plan 008 phase 06: the turn cache ---------------------------------------------------------------------------------------- */

enum : uint32_t {
    /* A cube round the eye: +x, -x, +y, -y, +z, -z. */
    LOD_TURN_FACES = 6u,
};

/* How far the eye may move from where a generation began and still read it, in blocks: a surface the far field shows is at least d_1 away
 * (193 blocks at 50 % of 1080 rows and E 2), where 0.25 blocks moves it by under half a pixel. */
#define LOD_TURN_EYE_BLOCKS F32(0.25)
/* The cosine of how far the sun may move under one generation (0.25 degrees), as a texel holds whether the sun reached its surface. */
#define LOD_TURN_SUN_COSINE F32(0.99999)
/* A frame is a turn, and reads texels, when the camera's middle moved by more than this many pixels since the last frame: more than TAA's
 * jitter, which moves it by under one. A frame that is not a turn traces as before, so a still camera's picture is the same to the bit. */
#define LOD_TURN_PIXELS F32(2.0)

/* One direction's answer: what the far ray from the generation's eye found, where it started, and under which generation. 16 bytes, read
 * and written whole. */
typedef struct lod_turn_texel_t {
    lod_hit_t hit;       /* 0 */
    float32_t from;      /* 8: blocks from the eye where the ray started looking */
    uint32_t generation; /* 12: lod_turn_cache_t's when written; 0 is never one */
} lod_turn_texel_t;
static_assert(sizeof(lod_turn_texel_t) == 16);
static_assert(alignof(lod_turn_texel_t) == 4);
static_assert(offsetof(lod_turn_texel_t, hit) == 0);
static_assert(offsetof(lod_turn_texel_t, from) == 8);
static_assert(offsetof(lod_turn_texel_t, generation) == 12);

/* Far rays since the block was cleared, at its end after the texels: traced, and read from a texel in place of a trace. */
typedef struct lod_turn_tally_t {
    uint64_t traced; /* 0 */
    uint64_t read;   /* 8 */
} lod_turn_tally_t;
static_assert(sizeof(lod_turn_tally_t) == 16);
static_assert(alignof(lod_turn_tally_t) == 8);
static_assert(offsetof(lod_turn_tally_t, traced) == 0);
static_assert(offsetof(lod_turn_tally_t, read) == 8);

/* The cache's state between frames, on the host: lod_frame keeps it. The block is LOD_TURN_FACES side^2 lod_turn_texel_t, then a
 * lod_turn_tally_t. A generation ends when the eye has moved LOD_TURN_EYE_BLOCKS from where it began, the far field's revision changes, or
 * the sun moves: every texel of an older one is invalid at once, with no pass to clear them. */
typedef struct lod_turn_cache_t {
    const device_block_t *borrowed_block; /* 0: lod_turn_cache_bytes(side) */
    int32_t eye_cell[3];                  /* 8: where this generation began */
    float32_t eye_frac[3];                /* 20 */
    float32_t forward[3];                 /* 32: the last frame's camera, to tell a turn */
    float32_t sun[3];                     /* 44: this generation's */
    uint32_t side;                        /* 56: texels along a face's edge */
    uint32_t generation;                  /* 60: 0 before the first frame */
    uint32_t revision;                    /* 64: the far field's, under this generation */
    uint8_t shadows;                      /* 68: this generation's lod_light_t.shadows */
    uint8_t turning;                      /* 69: the last frame was a turn, and read texels */
    uint8_t cleared;                      /* 70: the block has been zeroed on the card */
    uint8_t _pad0;                        /* 71 */
} lod_turn_cache_t;
static_assert(sizeof(lod_turn_cache_t) == 72);
static_assert(alignof(lod_turn_cache_t) == 8);
static_assert(offsetof(lod_turn_cache_t, borrowed_block) == 0);
static_assert(offsetof(lod_turn_cache_t, eye_cell) == 8);
static_assert(offsetof(lod_turn_cache_t, eye_frac) == 20);
static_assert(offsetof(lod_turn_cache_t, forward) == 32);
static_assert(offsetof(lod_turn_cache_t, sun) == 44);
static_assert(offsetof(lod_turn_cache_t, side) == 56);
static_assert(offsetof(lod_turn_cache_t, generation) == 60);
static_assert(offsetof(lod_turn_cache_t, revision) == 64);
static_assert(offsetof(lod_turn_cache_t, shadows) == 68);
static_assert(offsetof(lod_turn_cache_t, turning) == 69);
static_assert(offsetof(lod_turn_cache_t, cleared) == 70);
static_assert(offsetof(lod_turn_cache_t, _pad0) == 71);

static inline uint64_t lod_turn_cache_bytes(uint32_t side) {
    return (uint64_t)LOD_TURN_FACES * side * side * sizeof(lod_turn_texel_t) + sizeof(lod_turn_tally_t);
}

/* Which texel of a cube of side texels a direction falls in. It need not be unit length, and only the axis it leans on most must be non-zero:
 * the face is that axis (the lower one on a tie) and its sign, then the other two over it, from -1 to 1, are a column and a row.
 * face * side^2 + row * side + column. */
LOD_BOTH_SIDES static inline uint32_t lod_turn_texel_of(const float32_t direction[3], uint32_t side) {
    const float32_t ax = direction[0] < F32(0.0) ? -direction[0] : direction[0];
    const float32_t ay = direction[1] < F32(0.0) ? -direction[1] : direction[1];
    const float32_t az = direction[2] < F32(0.0) ? -direction[2] : direction[2];
    const uint32_t axis = ax >= ay && ax >= az ? 0u : (ay >= az ? 1u : 2u);
    const float32_t major = axis == 0u ? ax : (axis == 1u ? ay : az);
    const uint32_t first = axis == 0u ? 1u : 0u, second = axis == 2u ? 1u : 2u;
    const uint32_t face = axis * 2u + (direction[axis] < F32(0.0) ? 1u : 0u);
    const float32_t half = F32(0.5) * (float32_t)side;
    const float32_t last = (float32_t)(side - 1u);
    float32_t across = direction[first] / major * half + half;
    float32_t down = direction[second] / major * half + half;
    across = across < F32(0.0) ? F32(0.0) : (across > last ? last : across);
    down = down < F32(0.0) ? F32(0.0) : (down > last ? last : down);
    return (face * side + (uint32_t)down) * side + (uint32_t)across;
}

/* The unit direction through the middle of a texel, as lod_turn_texel_of numbers them. */
LOD_BOTH_SIDES static inline void lod_turn_texel_direction(uint32_t texel, uint32_t side, float32_t out[3]) {
    const uint32_t face = texel / (side * side), within = texel % (side * side);
    const uint32_t axis = face >> 1;
    const uint32_t first = axis == 0u ? 1u : 0u, second = axis == 2u ? 1u : 2u;
    float32_t d[3];
    d[axis] = (face & 1u) != 0u ? -F32(1.0) : F32(1.0);
    d[first] = F32(2.0) * ((float32_t)(within % side) + F32(0.5)) / (float32_t)side - F32(1.0);
    d[second] = F32(2.0) * ((float32_t)(within / side) + F32(0.5)) / (float32_t)side - F32(1.0);
    const float32_t length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (uint32_t i = 0u; i < 3u; i++) {
        out[i] = d[i] / length;
    }
}

/* What a frame's far rays need of shading: which faces of which block show which texture's mean colour, and the sun. And what shade
 * itself reads of the surfaces a pixel's ray meets, so that a far ray is sent only where shade will look at it. */
typedef struct lod_light_t {
    /* 0: shade_texture_t */
    const device_block_t *borrowed_textures;
    /* 8: material_count * SHADE_FACES uint16_t, and with stops_after_faces a traverse_stops_t at lod_stops_offset after them */
    const device_block_t *borrowed_faces;
    /* 16: shade_scene_t's, every texture's RGBA8, for the texel shade will meet. nullptr goes by each texture's mean alpha instead,
     * which sends a far ray for every surface shade could see past. */
    const device_block_t *borrowed_pixels;
    float32_t sun_direction[3]; /* 24: unit, towards it */
    uint32_t material_count;    /* 36 */
    uint32_t shadows;           /* 40: 0 for no sun rays: every hit has all of the sun */
    /* 44: the block shade walks through as one body, as lod_frame then does, or SHADE_NO_MATERIAL. 0, air, is never met, so it is
     * none as well, and a voxel of water is then one more thing to see through. */
    uint16_t water_material;
    /* 46: 1 with SHADE_FEATURE_WATER_TINT: past SHADE_WATER_SEEN_BLOCKS of water, shade ends a ray */
    uint8_t water_tint;
    /* 47: 1 when the face table holds, after its faces, the blocks no ray of shade goes on through on any face, as none has anything
     * to see through, and water. With them one trace tells whether anything opaque, or water, stands behind a see-through
     * surface. 0 follows every such ray as shade does, which is slower and comes to the same. */
    uint8_t stops_after_faces;
} lod_light_t;
static_assert(sizeof(lod_light_t) == 48);
static_assert(alignof(lod_light_t) == 8);
static_assert(offsetof(lod_light_t, borrowed_textures) == 0);
static_assert(offsetof(lod_light_t, borrowed_faces) == 8);
static_assert(offsetof(lod_light_t, borrowed_pixels) == 16);
static_assert(offsetof(lod_light_t, sun_direction) == 24);
static_assert(offsetof(lod_light_t, material_count) == 36);
static_assert(offsetof(lod_light_t, shadows) == 40);
static_assert(offsetof(lod_light_t, water_material) == 44);
static_assert(offsetof(lod_light_t, water_tint) == 46);
static_assert(offsetof(lod_light_t, stops_after_faces) == 47);

/* Where in a face table of face_bytes its traverse_stops_t starts, in bytes: the next 64 after the faces. */
static inline uint64_t lod_stops_offset(uint64_t face_bytes) {
    return (face_bytes + 63u) & ~(uint64_t)63u;
}

static inline uint64_t lod_hits_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * sizeof(lod_hit_t);
}

/* One lod_hit_t per pixel of this camera, from rays that go on past level 0, world: those that hit nothing in hits, and those that hit
 * something a ray can see through and, going on through world past all it can see through as shade's own ray does, get past it at last.
 * Any other pixel gets nothing, as shade would never look. Plan 009 phase 04: a pixel's ray leaves level 0 at its d_1, by the camera's bands,
 * or at level 0's edge if that is nearer, and each level takes over from the last at the next level's d or its own edge: so nothing nearer
 * is looked at, and nothing is passed by. With no bands in the camera, level 0 goes to its edge and each level to its own. Starts a
 * kernel, and returns; it runs in order with any other. Nothing here allocates. On failure error says why, and nothing was started.
 * Plan 008 phase 06: with scene->borrowed_turn, every far ray traced writes its direction's texel, and on a frame that is a turn a pixel
 * whose texel is of this generation and started within 2 band_dither + 2 % of where its own ray starts takes it in place of tracing. The
 * first frame zeroes the block on the card. */
gpu_status_t lod_frame(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                       const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits);

/* Plan 009 phase 01: lod_frame with every traversal's steps added into counts (see traverse_counts_t): far rays and the far field's sun
 * rays by level, and the ask past level 0. A second copy of each kernel does it; with counts nullptr this is lod_frame. */
gpu_status_t lod_frame_counted(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                               const traverse_world_t *world, const traverse_camera_t *camera,
                               const device_block_t *hits, const device_block_t *counts);

/* The same trace over ray_count rays of traverse_ray_t in rays, into as many lod_hit_t in out, for a test and for anything that is not a
 * frame, with no bands: each level to its own edge. t_start in a ray is where it starts looking. With light nullptr no sun ray is sent.
 * Starts a kernel, as above. */
gpu_status_t lod_rays(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                      const device_block_t *rays, const device_block_t *out, uint32_t ray_count);

#ifdef __cplusplus
}
#endif

#endif
