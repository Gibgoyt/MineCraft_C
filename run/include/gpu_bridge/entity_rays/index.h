/* See run/src/gpu_bridge/entity_rays/README.md and cuda/src/entity_rays/README.md, which implements this. C API, compiled by GCC
 * and by nvcc: both sides check these static_asserts. The small functions below run on the card in the kernel and on the host in a
 * test, as the same lines. */
#ifndef MC_RUN_GPU_BRIDGE_ENTITY_RAYS_INDEX_H
#define MC_RUN_GPU_BRIDGE_ENTITY_RAYS_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "types/index.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __CUDACC__
#define ENTITY_RAYS_BOTH_SIDES __host__ __device__
#else
#define ENTITY_RAYS_BOTH_SIDES
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum : uint32_t {
    /* Boxes one frame draws: every item stack there can be, and as many again for what Stage 15 adds. */
    ENTITY_RAYS_BOXES_MAX = 8192u,
    /* A box in one tile's list is a link. A pile at the feet covers few tiles each, so this takes far more than a game makes. */
    ENTITY_RAYS_LINKS_MAX = 1u << 20,
    ENTITY_RAYS_NO_BOX = 0xFFFFu,
    /* In a box: draw by its texture and not by a block's faces. */
    ENTITY_RAYS_NO_MATERIAL = 0xFFFFu,
    /* In a box: nothing to draw it with, so it draws SHADE_MISSING_*. */
    ENTITY_RAYS_NO_TEXTURE = 0xFFFFu,
    /* Rows of a frame's own table of textures, past the block table the scene has. Row 0 is the crack. */
    ENTITY_RAYS_TEXTURES_MAX = 256u,
    ENTITY_RAYS_TEXTURE_CRACK = 0u,
    ENTITY_RAYS_CRACK_FRAMES = 10u,
    ENTITY_RAYS_EDGES = 12u,
    /* An outline's line is a pixel wide for each this many rows, and never less than one. */
    ENTITY_RAYS_OUTLINE_ROWS_PER_PIXEL = 720u,
    /* How many voxels of water a line looks through. Reach is 4.5 blocks, so this is all it can have in front of it. */
    ENTITY_RAYS_OUTLINE_WATER_LAYERS = 12u,
    /* Cells a placing preview can show at once (plan 005 phase 10): a bench's two halves and a door's, and room for a smith table's four.
     * A plan of more is cut to these, and run/src/game/ says at compile time if the plan can hold more. */
    ENTITY_RAYS_PREVIEW_CELLS = 4u,
    /* How much of a preview's colour goes over the picture, of 256: about half, so the world shows through it. */
    ENTITY_RAYS_PREVIEW_ALPHA = 128u,
};

/* A body part's skin has this many texels to a block: the size of what config/mobs/ measures a part in. */
#define ENTITY_RAYS_SKIN_TEXELS_PER_BLOCK F32(16.0)
/* What the outline's lines are: black. */
#define ENTITY_RAYS_OUTLINE_XRGB 0x00000000u
/* What a preview of placing is: blue where the block would go down, and red where placing would fail. */
#define ENTITY_RAYS_PREVIEW_OK_XRGB 0x003C8CFFu
#define ENTITY_RAYS_PREVIEW_BLOCKED_XRGB 0x00FF3030u
/* Nothing a ray meets nearer than this along itself is a line: it would fill the picture. */
#define ENTITY_RAYS_OUTLINE_NEAR F32(0.05)
/* A line is hidden by what is at least this much nearer: anything less is the surface it lies on, or one flush with it. */
#define ENTITY_RAYS_OUTLINE_DEPTH_BLOCKS F32(0.05)
#define ENTITY_RAYS_OUTLINE_DEPTH_PIXELS F32(8.0)

/* One box, for one frame. Tilted by pitch about its own left axis (more points its forward up), then turned by yaw about z, both through
 * its middle. Plain floats from the frame's eye, which the host works out in float64_t, so that a box is as exact far from the origin as
 * near it. */
typedef struct entity_rays_box_t {
    float32_t center[3]; /* 0: from the eye */
    float32_t half[3];   /* 12: half its size, each way along its own axes */
    float32_t cos_yaw;   /* 24 */
    float32_t sin_yaw;   /* 28 */
    float32_t cos_pitch; /* 32 */
    float32_t sin_pitch; /* 36 */
    uint16_t material;   /* 40: a row of the scene's face table, or ENTITY_RAYS_NO_MATERIAL */
    uint16_t texture;    /* 42: with no material, a row of the frame's own table, or ENTITY_RAYS_NO_TEXTURE */
    uint16_t skin_u;     /* 44: with a skin, the corner of its unwrap in the texture, in texels */
    uint16_t skin_v;     /* 46 */
    uint8_t stored;      /* 48: the light in the air around it, as a voxel's stored light */
    uint8_t skin_size
        [3]; /* 49: forward, left and up, in texels: a body part's unwrap. All 0: the whole texture on every face */
} entity_rays_box_t;
static_assert(sizeof(entity_rays_box_t) == 52);
static_assert(alignof(entity_rays_box_t) == 4);
static_assert(offsetof(entity_rays_box_t, center) == 0);
static_assert(offsetof(entity_rays_box_t, half) == 12);
static_assert(offsetof(entity_rays_box_t, cos_yaw) == 24);
static_assert(offsetof(entity_rays_box_t, sin_yaw) == 28);
static_assert(offsetof(entity_rays_box_t, cos_pitch) == 32);
static_assert(offsetof(entity_rays_box_t, sin_pitch) == 36);
static_assert(offsetof(entity_rays_box_t, material) == 40);
static_assert(offsetof(entity_rays_box_t, texture) == 42);
static_assert(offsetof(entity_rays_box_t, skin_u) == 44);
static_assert(offsetof(entity_rays_box_t, skin_v) == 46);
static_assert(offsetof(entity_rays_box_t, stored) == 48);
static_assert(offsetof(entity_rays_box_t, skin_size) == 49);

/* The nearest box along a pixel's ray. */
typedef struct entity_rays_hit_t {
    float32_t t;   /* 0: blocks from the eye, or TRAVERSE_COARSE_SKY for none */
    uint16_t box;  /* 4: or ENTITY_RAYS_NO_BOX */
    uint16_t face; /* 6: a traverse_face_t, on the box's own axes */
} entity_rays_hit_t;
static_assert(sizeof(entity_rays_hit_t) == 8);
static_assert(alignof(entity_rays_hit_t) == 4);
static_assert(offsetof(entity_rays_hit_t, t) == 0);
static_assert(offsetof(entity_rays_hit_t, box) == 4);
static_assert(offsetof(entity_rays_hit_t, face) == 6);

/* What the player is aiming at and mining, for one frame. */
typedef struct entity_rays_mark_t {
    float32_t box_min[3];  /* 0: the outline's box, from the eye */
    float32_t box_max[3];  /* 12 */
    int32_t crack_cell[3]; /* 24: the block being mined, from the eye's cell */
    uint32_t crack_frame;  /* 36: 0 for no crack, or 1 and on for the texture's frames from its first */
    uint32_t outline;      /* 40: whether there is a box */
    uint16_t pixel_min[2]; /* 44: what the outline's box can touch, in pixels */
    uint16_t pixel_end[2]; /* 48 */
} entity_rays_mark_t;
static_assert(sizeof(entity_rays_mark_t) == 52);
static_assert(alignof(entity_rays_mark_t) == 4);
static_assert(offsetof(entity_rays_mark_t, box_min) == 0);
static_assert(offsetof(entity_rays_mark_t, box_max) == 12);
static_assert(offsetof(entity_rays_mark_t, crack_cell) == 24);
static_assert(offsetof(entity_rays_mark_t, crack_frame) == 36);
static_assert(offsetof(entity_rays_mark_t, outline) == 40);
static_assert(offsetof(entity_rays_mark_t, pixel_min) == 44);
static_assert(offsetof(entity_rays_mark_t, pixel_end) == 48);

/* Where a held block would go down, and as what, for one frame (plan 005 phase 10). Each cell is drawn in the shape the card draws that
 * block in, turned by its facing: cuda/include/traverse/'s table, through the world's shapes. */
typedef struct entity_rays_preview_t {
    int32_t cells[ENTITY_RAYS_PREVIEW_CELLS][3]; /* 0: each from the eye's cell */
    uint16_t voxels[ENTITY_RAYS_PREVIEW_CELLS];  /* 48: each block with its state: run/include/types/block_state/ */
    uint32_t count;                              /* 56: cells to draw, 0 for none */
    uint32_t xrgb;         /* 60: ENTITY_RAYS_PREVIEW_OK_XRGB or ENTITY_RAYS_PREVIEW_BLOCKED_XRGB */
    uint16_t pixel_min[2]; /* 64: what the cells can touch, in pixels */
    uint16_t pixel_end[2]; /* 68 */
} entity_rays_preview_t;
static_assert(sizeof(entity_rays_preview_t) == 72);
static_assert(alignof(entity_rays_preview_t) == 4);
static_assert(offsetof(entity_rays_preview_t, cells) == 0);
static_assert(offsetof(entity_rays_preview_t, voxels) == 48);
static_assert(offsetof(entity_rays_preview_t, count) == 56);
static_assert(offsetof(entity_rays_preview_t, xrgb) == 60);
static_assert(offsetof(entity_rays_preview_t, pixel_min) == 64);
static_assert(offsetof(entity_rays_preview_t, pixel_end) == 68);

/* What one frame draws of entities. The lists block holds, in this order, the boxes, each tile's first link, the links and the
 * frame's own textures: see the functions below. Borrowed, and not written while a kernel may run. */
typedef struct entity_rays_scene_t {
    const device_block_t *borrowed_lists; /* 0: entity_rays_textures_offset, then the textures */
    const device_block_t *borrowed_hits;  /* 8: entity_rays_hits_bytes */
    uint32_t box_count;                   /* 16 */
    uint32_t link_count;                  /* 20 */
    entity_rays_mark_t mark;              /* 24 */
    uint8_t _pad0[4];                     /* 76 */
    entity_rays_preview_t preview;        /* 80 */
} entity_rays_scene_t;
static_assert(sizeof(entity_rays_scene_t) == 152);
static_assert(alignof(entity_rays_scene_t) == 8);
static_assert(offsetof(entity_rays_scene_t, borrowed_lists) == 0);
static_assert(offsetof(entity_rays_scene_t, borrowed_hits) == 8);
static_assert(offsetof(entity_rays_scene_t, box_count) == 16);
static_assert(offsetof(entity_rays_scene_t, link_count) == 20);
static_assert(offsetof(entity_rays_scene_t, mark) == 24);
static_assert(offsetof(entity_rays_scene_t, _pad0) == 76);
static_assert(offsetof(entity_rays_scene_t, preview) == 80);

static inline uint64_t entity_rays_hits_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * sizeof(entity_rays_hit_t);
}

/* Where each tile's first link is: one per tile, row by row, and one more for where the last tile's end. */
static inline uint64_t entity_rays_starts_offset(void) {
    return (uint64_t)ENTITY_RAYS_BOXES_MAX * sizeof(entity_rays_box_t);
}

static inline uint64_t entity_rays_links_offset(uint32_t width, uint32_t height) {
    return entity_rays_starts_offset() +
           ((uint64_t)traverse_tiles_across(width) * traverse_tiles_across(height) + 1u) * sizeof(uint32_t);
}

/* On 8 bytes, as a texture's row wants. */
static inline uint64_t entity_rays_textures_offset(uint32_t width, uint32_t height) {
    return (entity_rays_links_offset(width, height) + (uint64_t)ENTITY_RAYS_LINKS_MAX * sizeof(uint16_t) + 7u) / 8u *
           8u;
}

/* A point from the world's axes to a box's own. */
ENTITY_RAYS_BOTH_SIDES static inline void entity_rays_to_box(const entity_rays_box_t *box, const float32_t point[3],
                                                             float32_t out[3]) {
    const float32_t level = box->cos_yaw * point[0] + box->sin_yaw * point[1];
    out[0] = box->cos_pitch * level + box->sin_pitch * point[2];
    out[1] = box->cos_yaw * point[1] - box->sin_yaw * point[0];
    out[2] = box->cos_pitch * point[2] - box->sin_pitch * level;
}

/* The way a face of a box looks, on the world's axes. */
ENTITY_RAYS_BOTH_SIDES static inline void entity_rays_normal(const entity_rays_box_t *box, uint32_t face,
                                                             float32_t out[3]) {
    const float32_t sign = (face & 1u) != 0u ? F32(1.0) : -F32(1.0);
    const uint32_t axis = face >> 1;
    /* Along the ground, its own forward or up comes to this much, and the rest of it is up. */
    const float32_t level = axis == 0u ? box->cos_pitch : -box->sin_pitch;
    out[0] = axis == 1u ? -sign * box->sin_yaw : sign * level * box->cos_yaw;
    out[1] = axis == 1u ? sign * box->cos_yaw : sign * level * box->sin_yaw;
    out[2] = axis == 0u ? sign * box->sin_pitch : (axis == 2u ? sign * box->cos_pitch : F32(0.0));
}

/* Where on a skin a place on a face of a body part is, as parts of the texture's width and height. local is from 0 to 1 along each of the
 * box's own axes. A skin is unwrapped as a player's: from its corner, a row with the top and the bottom, each as many texels down as the
 * part is deep, and under it a row round the sides: its right, its front, its left and its back. Each is as seen from outside, with a
 * side's top up, and the top's front edge down. */
ENTITY_RAYS_BOTH_SIDES static inline void entity_rays_skin_uv(const entity_rays_box_t *box, uint32_t face,
                                                              const float32_t local[3], uint32_t width, uint32_t height,
                                                              float32_t *out_u, float32_t *out_v) {
    const float32_t deep = (float32_t)box->skin_size[0], wide = (float32_t)box->skin_size[1],
                    tall = (float32_t)box->skin_size[2];
    float32_t from_u = deep, from_v = deep, across = wide, down = tall;
    float32_t part_u = local[1], part_v = F32(1.0) - local[2];
    if (face == TRAVERSE_FACE_POS_Z) {
        from_v = F32(0.0);
        down = deep;
        part_v = local[0];
    } else if (face == TRAVERSE_FACE_NEG_Z) {
        from_u = deep + wide;
        from_v = F32(0.0);
        down = deep;
        part_v = F32(1.0) - local[0];
    } else if (face == TRAVERSE_FACE_NEG_Y) {
        from_u = F32(0.0);
        across = deep;
        part_u = local[0];
    } else if (face == TRAVERSE_FACE_POS_Y) {
        from_u = deep + wide;
        across = deep;
        part_u = F32(1.0) - local[0];
    } else if (face == TRAVERSE_FACE_NEG_X) {
        from_u = deep + wide + deep;
        part_u = F32(1.0) - local[1];
    }
    /* Never quite to its far edge, which is the first texel of whatever lies next on the skin. */
    const float32_t inside = F32(0.998);
    *out_u = ((float32_t)box->skin_u + from_u + (part_u * inside + F32(0.001)) * across) / (float32_t)width;
    *out_v = ((float32_t)box->skin_v + from_v + (part_v * inside + F32(0.001)) * down) / (float32_t)height;
}

/* A ray from the eye, along a unit direction, against a box: the slabs of its own axes. False if it misses, if the box is behind, or
 * if the eye is inside it: a box has no inside to see. */
ENTITY_RAYS_BOTH_SIDES static inline bool
entity_rays_box_hit(const entity_rays_box_t *box, const float32_t direction[3], float32_t *out_t, uint32_t *out_face) {
    const float32_t eye_world[3] = {-box->center[0], -box->center[1], -box->center[2]};
    float32_t eye[3], along[3];
    entity_rays_to_box(box, eye_world, eye);
    entity_rays_to_box(box, direction, along);
    float32_t enter = -TRAVERSE_COARSE_SKY, leave = TRAVERSE_COARSE_SKY;
    uint32_t face = 0u;
    for (uint32_t axis = 0u; axis < 3u; axis += 1u) {
        /* Dead along a side, the slab is all or nothing, which is what a very long step says, without a zero over a zero. */
        const float32_t step = fabsf(along[axis]) < F32(1e-12) ? F32(1e-12) : along[axis];
        const float32_t low = (-box->half[axis] - eye[axis]) / step, high = (box->half[axis] - eye[axis]) / step;
        const float32_t near_side = low < high ? low : high, far_side = low < high ? high : low;
        if (near_side > enter) {
            enter = near_side;
            face = axis * 2u + (step > F32(0.0) ? 0u : 1u);
        }
        leave = far_side < leave ? far_side : leave;
    }
    if (enter > leave || enter <= F32(0.0)) {
        return false;
    }
    *out_t = enter;
    *out_face = face;
    return true;
}

/* The ends of one edge of a box, as player_camera_edge numbers them. */
ENTITY_RAYS_BOTH_SIDES static inline void entity_rays_edge(const float32_t box_min[3], const float32_t box_max[3],
                                                           uint32_t edge, float32_t out_from[3], float32_t out_to[3]) {
    const uint32_t axis = edge / 4u, sides = edge % 4u;
    const uint32_t first = (axis + 1u) % 3u, second = (axis + 2u) % 3u;
    out_from[axis] = box_min[axis];
    out_to[axis] = box_max[axis];
    out_from[first] = out_to[first] = (sides & 1u) != 0u ? box_max[first] : box_min[first];
    out_from[second] = out_to[second] = (sides & 2u) != 0u ? box_max[second] : box_min[second];
}

/* How many pixels a ray from the eye, along a unit direction, passes from a line, and out_t how far along itself it is when it does.
 * pixel_tangent is how many blocks wide a pixel is per block of distance. The largest number there is if the line is behind the eye. */
ENTITY_RAYS_BOTH_SIDES static inline float32_t entity_rays_line_pixels(const float32_t from[3], const float32_t to[3],
                                                                       const float32_t direction[3],
                                                                       float32_t pixel_tangent, float32_t *out_t) {
    float32_t along[3];
    float32_t ray_on_line = F32(0.0), line_squared = F32(0.0), ray_on_from = F32(0.0), line_on_from = F32(0.0);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        along[i] = to[i] - from[i];
        ray_on_line += direction[i] * along[i];
        line_squared += along[i] * along[i];
        ray_on_from += direction[i] * from[i];
        line_on_from += along[i] * from[i];
    }
    const float32_t under = line_squared - ray_on_line * ray_on_line;
    float32_t s = under > F32(1e-9) ? (ray_on_line * ray_on_from - line_on_from) / under : F32(0.0);
    s = s < F32(0.0) ? F32(0.0) : (s > F32(1.0) ? F32(1.0) : s);
    const float32_t t = ray_on_from + s * ray_on_line;
    *out_t = t;
    if (t <= F32(0.0)) {
        return TRAVERSE_COARSE_SKY;
    }
    float32_t squared = F32(0.0);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t gap = from[i] + s * along[i] - t * direction[i];
        squared += gap * gap;
    }
    return sqrtf(squared) / (t * pixel_tangent);
}

/* How wide an outline's line is, in pixels, for a picture this tall. */
ENTITY_RAYS_BOTH_SIDES static inline uint32_t entity_rays_outline_pixels(uint32_t height) {
    const uint32_t wide = (height + ENTITY_RAYS_OUTLINE_ROWS_PER_PIXEL / 2u) / ENTITY_RAYS_OUTLINE_ROWS_PER_PIXEL;
    return wide == 0u ? 1u : wide;
}

/* The boxes against this camera's rays: one entity_rays_hit_t per pixel, row by row, into the scene's hits. A kernel in tiles, so
 * that a tile with nothing in its list costs next to nothing. camera may be jittered, by as much as its lists were made to allow.
 * With no boxes this starts nothing. Starts a kernel and returns; it runs in order with any other. Nothing here allocates. On
 * failure error says why, and nothing was started. */
gpu_status_t entity_rays_frame(gpu_error_t *error, const entity_rays_scene_t *scene, const traverse_camera_t *camera);

/* How much of a preview's colour a face of it takes, of 256, so that its shape shows: the top all of it, the sides less, the bottom least. */
ENTITY_RAYS_BOTH_SIDES static inline uint32_t entity_rays_preview_face_light(uint32_t face) {
    return face == TRAVERSE_FACE_POS_Z   ? 256u
           : face == TRAVERSE_FACE_NEG_Z ? 140u
           : face <= TRAVERSE_FACE_POS_X ? 205u
                                         : 174u;
}

/* One pixel of a picture under a preview's colour on a face: ENTITY_RAYS_PREVIEW_ALPHA of the colour, lit by the face, over the rest. */
ENTITY_RAYS_BOTH_SIDES static inline uint32_t entity_rays_preview_blend(uint32_t under, uint32_t xrgb, uint32_t face) {
    const uint32_t light = entity_rays_preview_face_light(face);
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        const uint32_t below = (under >> shift) & 0xFFu;
        const uint32_t colour = (((xrgb >> shift) & 0xFFu) * light) >> 8;
        out |= ((below * (256u - ENTITY_RAYS_PREVIEW_ALPHA) + colour * ENTITY_RAYS_PREVIEW_ALPHA) >> 8) << shift;
    }
    return out;
}

/* The outline, onto a finished picture: every pixel of the mark's box's edges that nothing stands in front of, by shaded's distance
 * at that pixel and, past that, through water with a ray from world. jitter is what the camera has on it, as the picture has none
 * by the time it is finished. With no box this starts nothing. Before the outline, so that its lines are on top, the preview: each of
 * its cells' boxes, by the world's shapes, half see-through, where nothing stands in front of it by the same rules. With neither this
 * starts nothing. As above. */
gpu_status_t entity_rays_overlay(gpu_error_t *error, const entity_rays_scene_t *scene, const traverse_world_t *world,
                                 const traverse_camera_t *camera, const float32_t jitter[2], uint32_t water_material,
                                 const device_block_t *shaded, const device_block_t *xrgb);

#ifdef __cplusplus
}
#endif

#endif
