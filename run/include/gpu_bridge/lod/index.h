/* See run/src/gpu_bridge/lod/README.md and cuda/src/lod/README.md, which implements this. C API, compiled by GCC and by nvcc: both
 * sides check these static_asserts. A level of the far field is a world of its own in the format of gpu_bridge/world_buffers, whose
 * voxel is 1 << shift blocks each way. */
#ifndef MC_RUN_GPU_BRIDGE_LOD_INDEX_H
#define MC_RUN_GPU_BRIDGE_LOD_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

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
    /* How many times one level lets a ray go on past something that lies in a column full detail holds. A ray that met nothing in
     * full detail meets very little of the coarse ground there, and past this it calls the level empty. */
    LOD_SKIPS_MAX = 32u,
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
} lod_scene_t;
static_assert(sizeof(lod_scene_t) == 352);
static_assert(alignof(lod_scene_t) == 8);
static_assert(offsetof(lod_scene_t, levels) == 0);
static_assert(offsetof(lod_scene_t, borrowed_hits) == 336);
static_assert(offsetof(lod_scene_t, level_count) == 344);
static_assert(offsetof(lod_scene_t, top_storage) == 348);

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

/* What a frame's far rays need of shading: which faces of which block show which texture's mean colour, and the sun. */
typedef struct lod_light_t {
    const device_block_t *borrowed_textures; /* 0: shade_texture_t */
    const device_block_t *borrowed_faces;    /* 8: material_count * SHADE_FACES uint16_t */
    float32_t sun_direction[3];              /* 16: unit, towards it */
    uint32_t material_count;                 /* 28 */
    uint32_t shadows;                        /* 32: 0 for no sun rays: every hit has all of the sun */
    uint8_t _pad0[4];                        /* 36 */
} lod_light_t;
static_assert(sizeof(lod_light_t) == 40);
static_assert(alignof(lod_light_t) == 8);
static_assert(offsetof(lod_light_t, borrowed_textures) == 0);
static_assert(offsetof(lod_light_t, borrowed_faces) == 8);
static_assert(offsetof(lod_light_t, sun_direction) == 16);
static_assert(offsetof(lod_light_t, material_count) == 28);
static_assert(offsetof(lod_light_t, shadows) == 32);
static_assert(offsetof(lod_light_t, _pad0) == 36);

static inline uint64_t lod_hits_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * sizeof(lod_hit_t);
}

/* One lod_hit_t per pixel of this camera, from rays that go on past what full detail shows: those that hit nothing in hits, and those
 * that hit something a ray can see through. world says what full detail holds, and whatever a level has in a column that is there is
 * not looked at, since that column draws itself. Starts a kernel, and returns; it runs in order with any other. Nothing here
 * allocates. On failure error says why, and nothing was started. */
gpu_status_t lod_frame(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                       const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits);

/* The same trace over ray_count rays of traverse_ray_t in rays, into as many lod_hit_t in out, for a test and for anything that is not a
 * frame. world may be nullptr, for no full detail at all. t_start in a ray is where it starts looking. With light nullptr no sun ray is
 * sent. Starts a kernel, as above. */
gpu_status_t lod_rays(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                      const traverse_world_t *world, const device_block_t *rays, const device_block_t *out,
                      uint32_t ray_count);

#ifdef __cplusplus
}
#endif

#endif
