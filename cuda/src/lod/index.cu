/* See README.md. Rays through the coarse grids, from the finest level out: each level is a world of the same format as the blocks',
 * traced by the same loop in that level's voxels. */
#include "lod/index.hu"

#include "gpu_bridge/shade/index.h"

#include <math.h>

/* What a kernel needs of each level. */
typedef struct lod_view_level_t {
    traverse_view_t view; /* 0: its window is the square rays are let into, and not the table's */
    uint32_t shift;       /* 64 */
    uint8_t _pad0[4];     /* 68 */
} lod_view_level_t;
static_assert(sizeof(lod_view_level_t) == 72);

typedef struct lod_view_t {
    lod_view_level_t levels[LOD_LEVELS_MAX]; /* 0 */
    uint32_t level_count;                    /* 432 */
    int32_t top_storage;                     /* 436 */
} lod_view_t;
static_assert(sizeof(lod_view_t) == 440);

/* Where a ray comes from in blocks, a storage height on z. */
typedef struct lod_origin_t {
    int32_t cell[3];   /* 0 */
    float32_t frac[3]; /* 12 */
} lod_origin_t;
static_assert(sizeof(lod_origin_t) == 24);

/* What the loop below found. */
typedef struct lod_found_t {
    float32_t t;        /* 0: in blocks */
    uint32_t material;  /* 4 */
    uint32_t face;      /* 8 */
    uint32_t level;     /* 12 */
    int32_t voxel[3];   /* 16: in that level's voxels, on z a storage height */
    float32_t local[3]; /* 28: where in the voxel, from 0 to 1 */
} lod_found_t;
static_assert(sizeof(lod_found_t) == 40);

/* Past this much of a block a hit at a plane counts as inside what it entered. */
#define LOD_CELL_NUDGE 1e-3F
/* How far back of a level's edge the next level is asked from, in blocks. Its grid can have a plane on that very edge, and the voxel a ray
 * then enters is one to look at, whichever way the numbers round: well over a float32_t's rounding at the furthest t there is. */
#define LOD_HANDOVER_BACK 0.01F
/* How far off a surface a sun ray starts, in voxels of the level it hit. */
#define LOD_SUN_BIAS 1e-3F

/* A ray in a level's voxels. A voxel is the same size each way, so a ray's direction and its unit length stay as they are. */
static __device__ __forceinline__ void lod_ray_in_level(const lod_origin_t *origin, const float32_t direction[3],
                                                        uint32_t shift, float32_t t_blocks, traverse_trace_ray_t *out) {
    const float32_t scale = 1.0F / (float32_t)(1u << shift);
    const int32_t mask = (1 << shift) - 1;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out->eye_cell[i] = origin->cell[i] >> shift;
        out->eye_frac[i] = ((float32_t)(origin->cell[i] & mask) + origin->frac[i]) * scale;
        out->direction[i] = direction[i];
    }
    out->t_start = t_blocks * scale;
    /* A coarser voxel can hold what a finer one said was air. A ray takes only what it enters from here on, and never starts inside. */
    out->resume = 1u;
}

/* Where a ray leaves a level's square, in blocks, whichever way z goes. */
static __device__ __forceinline__ float32_t lod_level_exit(const lod_view_level_t *level, const lod_origin_t *origin,
                                                           const float32_t direction[3]) {
    float32_t t = 1e30F;
    for (uint32_t i = 0u; i < 2u; i += 1u) {
        const int32_t low = level->view.window_min_block[i] * (1 << level->shift);
        const int32_t across = (int32_t)level->view.window_blocks * (1 << level->shift);
        const int32_t plane = direction[i] > 0.0F ? low + across : low;
        t = fminf(t, ((float32_t)(plane - origin->cell[i]) - origin->frac[i]) / direction[i]);
    }
    return t;
}

/* Whether full detail holds the column this place along a ray is in, and if so where the ray leaves that column. */
static __device__ __forceinline__ bool lod_full_detail_at(const traverse_view_t *fine, const lod_origin_t *origin,
                                                          const float32_t direction[3], float32_t t,
                                                          float32_t *out_exit) {
    int32_t cell[2];
    for (uint32_t i = 0u; i < 2u; i += 1u) {
        cell[i] = origin->cell[i] + (int32_t)floorf(origin->frac[i] + direction[i] * (t + LOD_CELL_NUDGE));
        if ((uint32_t)(cell[i] - fine->window_min_block[i]) >= fine->window_blocks) {
            return false;
        }
    }
    const int32_t column_x = cell[0] >> 4, column_y = cell[1] >> 4;
    const world_buffers_column_t *const column = &fine->borrowed_table[traverse_slot(fine, column_x, column_y)];
    if (column->ready == 0u || column->chunk_x != column_x || column->chunk_y != column_y) {
        return false;
    }
    float32_t exit = 1e30F;
    for (uint32_t i = 0u; i < 2u; i += 1u) {
        const int32_t low = cell[i] & ~15;
        const int32_t plane = direction[i] > 0.0F ? low + 16 : low;
        exit = fminf(exit, ((float32_t)(plane - origin->cell[i]) - origin->frac[i]) / direction[i]);
    }
    *out_exit = exit;
    return true;
}

/* A ray from t_blocks on, through the levels from first_level out. With fine, what lies in a column full detail holds is not looked at.
 * The levels nest, and a ray goes from each to the next where it leaves that one's square. */
static __device__ __forceinline__ bool lod_trace(const lod_view_t *view, const traverse_view_t *fine,
                                                 const lod_origin_t *origin, const float32_t direction[3],
                                                 uint32_t first_level, float32_t t_blocks, lod_found_t *out) {
    /* Past this a rising ray is above all there is, which is most of a picture's sky. */
    const float32_t t_up = direction[2] > 0.0F
                               ? ((float32_t)(view->top_storage - origin->cell[2]) - origin->frac[2]) / direction[2]
                               : 1e30F;
    float32_t t_from = t_blocks;
    for (uint32_t index = first_level; index < view->level_count; index += 1u) {
        if (t_from >= t_up) {
            return false;
        }
        const lod_view_level_t *const level = &view->levels[index];
        if (level->view.window_blocks == 0u) {
            continue;
        }
        const float32_t t_exit = lod_level_exit(level, origin, direction);
        const float32_t size = (float32_t)(1u << level->shift);
        for (uint32_t skip = 0u; skip < LOD_SKIPS_MAX && t_from < t_exit; skip += 1u) {
            traverse_trace_ray_t ray;
            lod_ray_in_level(origin, direction, level->shift, t_from, &ray);
            traverse_hit_t hit;
            traverse_trace(&level->view, &ray, &hit);
            const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
            if (face == TRAVERSE_FACE_MISS) {
                break;
            }
            const float32_t t_hit = hit.t * size;
            float32_t t_past = 0.0F;
            if (fine != nullptr && lod_full_detail_at(fine, origin, direction, t_hit, &t_past)) {
                t_from = fmaxf(t_past, t_hit + LOD_CELL_NUDGE);
                continue;
            }
            out->t = t_hit;
            out->material = hit.value & TRAVERSE_VALUE_MATERIAL_MASK;
            out->face = face;
            out->level = index;
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                out->voxel[i] = ray.eye_cell[i] + (int32_t)hit.offset[i];
                const float32_t local = ray.eye_frac[i] + direction[i] * hit.t - (float32_t)hit.offset[i];
                out->local[i] = local < 0.0F ? 0.0F : (local > 1.0F ? 1.0F : local);
            }
            return true;
        }
        t_from = fmaxf(t_from, t_exit - LOD_HANDOVER_BACK);
    }
    return false;
}

/* Whether the sun reaches a surface: one ray, through the coarse grids alone, from the level the surface is in and out. A level holds all
 * of the ground, under full detail too, so that a far hill's shadow is cast by what is really there. */
static __device__ __forceinline__ bool lod_sun_reaches(const lod_view_t *view, const lod_found_t *found,
                                                       const float32_t sun[3]) {
    const uint32_t shift = view->levels[found->level].shift;
    const float32_t size = (float32_t)(1u << shift);
    const uint32_t axis = found->face >> 1;
    lod_origin_t origin;
    float32_t guarded[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        float32_t local = found->local[i];
        if (i == axis) {
            local = (found->face & 1u) != 0u ? 1.0F + LOD_SUN_BIAS : -LOD_SUN_BIAS;
        }
        const float32_t blocks = local * size;
        const float32_t whole = floorf(blocks);
        origin.cell[i] = found->voxel[i] * (1 << shift) + (int32_t)whole;
        origin.frac[i] = blocks - whole;
        guarded[i] = traverse_guard(sun[i]);
    }
    lod_found_t blocked;
    return !lod_trace(view, nullptr, &origin, guarded, found->level, 0.0F, &blocked);
}

/* Whether a ray can go on past what it first hit: nothing hit at all, or a face whose texture has anything to see through. */
static __device__ __forceinline__ bool lod_ray_goes_on(const traverse_hit_t *hit, const shade_texture_t *textures,
                                                       const uint16_t *faces, uint32_t material_count) {
    const uint32_t info_face = (uint32_t)hit->info & TRAVERSE_INFO_FACE_MASK;
    if (info_face == TRAVERSE_FACE_MISS) {
        return true;
    }
    const uint32_t material = hit->value & TRAVERSE_VALUE_MATERIAL_MASK;
    if (material >= material_count) {
        return false;
    }
    const uint32_t face = info_face < SHADE_FACES ? info_face : (uint32_t)TRAVERSE_FACE_POS_Z;
    const uint32_t texture = faces[material * SHADE_FACES + face];
    return texture != SHADE_NO_TEXTURE && textures[texture].mean_rgba[3] != SHADE_ALPHA_OPAQUE;
}

static __device__ __forceinline__ void lod_hit_write(const lod_view_t *view, const traverse_view_t *fine,
                                                     const lod_origin_t *origin, const float32_t direction[3],
                                                     float32_t t_start, bool shadows, const float32_t sun[3],
                                                     lod_hit_t *out) {
    lod_hit_t result = {TRAVERSE_COARSE_SKY, (uint16_t)LOD_NO_MATERIAL, (uint8_t)TRAVERSE_FACE_MISS,
                        (uint8_t)LOD_SUN_SEEN};
    lod_found_t found;
    if (lod_trace(view, fine, origin, direction, 0u, t_start, &found)) {
        result.t = found.t;
        result.material = (uint16_t)found.material;
        result.face = (uint8_t)found.face;
        if (shadows) {
            /* A face turned from the sun gets none of it whatever is in the way, so no ray is sent for it. */
            const uint32_t axis = found.face >> 1;
            const bool facing = (found.face & 1u) != 0u ? sun[axis] > 0.0F : sun[axis] < 0.0F;
            result.sun =
                facing && !lod_sun_reaches(view, &found, sun) ? (uint8_t)LOD_SUN_HIDDEN : (uint8_t)LOD_SUN_SEEN;
        }
    }
    *out = result;
}

/* One block of threads is one tile, as in primary rays. */
__global__ void lod_frame_kernel(const lod_view_t view, const traverse_view_t fine, const traverse_camera_t camera,
                                 const shade_texture_t *textures, const uint16_t *faces, uint32_t material_count,
                                 bool shadows, float32_t sun_x, float32_t sun_y, float32_t sun_z,
                                 const traverse_hit_t *hits, lod_hit_t *out) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    if (!lod_ray_goes_on(&hits[index], textures, faces, material_count)) {
        out[index] = lod_hit_t{TRAVERSE_COARSE_SKY, (uint16_t)LOD_NO_MATERIAL, (uint8_t)TRAVERSE_FACE_MISS,
                               (uint8_t)LOD_SUN_SEEN};
        return;
    }
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    lod_origin_t origin;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        origin.cell[i] = camera.eye_cell[i];
        origin.frac[i] = camera.eye_frac[i];
        direction[i] = traverse_guard(direction[i]);
    }
    origin.cell[2] -= TRAVERSE_Z_BASE;
    const float32_t sun[3] = {sun_x, sun_y, sun_z};
    lod_hit_write(&view, &fine, &origin, direction, 0.0F, shadows, sun, &out[index]);
}

__global__ void lod_rays_kernel(const lod_view_t view, const traverse_view_t fine, bool has_fine, bool shadows,
                                float32_t sun_x, float32_t sun_y, float32_t sun_z, const traverse_ray_t *rays,
                                lod_hit_t *out, uint32_t ray_count) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= ray_count) {
        return;
    }
    const traverse_ray_t given = rays[index];
    const float32_t scale =
        1.0F / sqrtf(given.direction[0] * given.direction[0] + given.direction[1] * given.direction[1] +
                     given.direction[2] * given.direction[2]);
    float32_t direction[3];
    lod_origin_t origin;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        origin.cell[i] = given.eye_cell[i];
        origin.frac[i] = given.eye_frac[i];
        direction[i] = traverse_guard(given.direction[i] * scale);
    }
    origin.cell[2] -= TRAVERSE_Z_BASE;
    const float32_t sun[3] = {sun_x, sun_y, sun_z};
    lod_hit_write(&view, has_fine ? &fine : nullptr, &origin, direction, given.t_start, shadows, sun, &out[index]);
}

static gpu_status_t lod_view_from(gpu_error_t *error, const lod_scene_t *scene, lod_view_t *out) {
    if (scene->level_count > LOD_LEVELS_MAX) {
        return traverse_refuse(error, "lod: more levels than there is room for");
    }
    *out = lod_view_t{};
    out->level_count = scene->level_count;
    out->top_storage = scene->top_storage;
    for (uint32_t i = 0u; i < scene->level_count; i += 1u) {
        const lod_level_t *const level = &scene->levels[i];
        const traverse_world_t world = {level->borrowed_table, level->borrowed_pool, level->window, nullptr, 0u,
                                        {0u, 0u, 0u, 0u}};
        lod_view_level_t *const into = &out->levels[i];
        const gpu_status_t status = traverse_view_from(error, &world, &into->view);
        if (status != GPU_STATUS_OK) {
            return status;
        }
        into->shift = level->shift;
        if (level->shown_side > level->window.side) {
            return traverse_refuse(error, "lod: a level shows more than its table holds");
        }
        if (level->shown_side == 0u) {
            into->view.window_blocks = 0u;
            continue;
        }
        uint32_t slot = 0u;
        if (!world_buffers_slot_of(&level->window, level->shown_min_x, level->shown_min_y, &slot)) {
            return traverse_refuse(error, "lod: a level shows a square outside its table");
        }
        /* The same table, seen through a smaller window: traverse_slot only asks where the window's corner is. */
        into->view.window_min_column[0] = level->shown_min_x;
        into->view.window_min_column[1] = level->shown_min_y;
        into->view.window_min_block[0] = level->shown_min_x * (int32_t)TRAVERSE_COLUMN_BLOCKS;
        into->view.window_min_block[1] = level->shown_min_y * (int32_t)TRAVERSE_COLUMN_BLOCKS;
        into->view.origin_slot[0] = slot % level->window.side;
        into->view.origin_slot[1] = slot / level->window.side;
        into->view.window_blocks = level->shown_side * TRAVERSE_COLUMN_BLOCKS;
    }
    return GPU_STATUS_OK;
}

gpu_status_t lod_frame(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                       const traverse_world_t *world, const traverse_camera_t *camera, const device_block_t *hits) {
    if (camera->width == 0u || camera->height == 0u) {
        return traverse_refuse(error, "lod: the camera has no pixels");
    }
    if (hits->bytes < traverse_hits_bytes(camera->width, camera->height)) {
        return traverse_refuse(error, "lod: the hits are too small for the camera");
    }
    if (scene->borrowed_hits == nullptr ||
        scene->borrowed_hits->bytes < lod_hits_bytes(camera->width, camera->height)) {
        return traverse_refuse(error, "lod: its hits are missing or too small for the camera");
    }
    lod_view_t view;
    traverse_view_t fine;
    gpu_status_t status = lod_view_from(error, scene, &view);
    if (status == GPU_STATUS_OK) {
        status = traverse_view_from(error, world, &fine);
    }
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    lod_frame_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        view, fine, *camera, (const shade_texture_t *)(const void *)device_memory_pointer(light->borrowed_textures),
        (const uint16_t *)(const void *)device_memory_pointer(light->borrowed_faces), light->material_count,
        light->shadows != 0u, light->sun_direction[0], light->sun_direction[1], light->sun_direction[2],
        (const traverse_hit_t *)(const void *)device_memory_pointer(hits),
        (lod_hit_t *)(void *)device_memory_pointer(scene->borrowed_hits));
    return device_memory_check(error, cudaGetLastError(), "lod: starting the rays of a frame");
}

gpu_status_t lod_rays(gpu_error_t *error, const lod_scene_t *scene, const lod_light_t *light,
                      const traverse_world_t *world, const device_block_t *rays, const device_block_t *out,
                      uint32_t ray_count) {
    if (ray_count == 0u) {
        return GPU_STATUS_OK;
    }
    if (rays->bytes < (uint64_t)ray_count * sizeof(traverse_ray_t) ||
        out->bytes < (uint64_t)ray_count * sizeof(lod_hit_t)) {
        return traverse_refuse(error, "lod: a ray or hit block is too small for the ray count");
    }
    lod_view_t view;
    gpu_status_t status = lod_view_from(error, scene, &view);
    traverse_view_t fine = {};
    if (status == GPU_STATUS_OK && world != nullptr) {
        status = traverse_view_from(error, world, &fine);
    }
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const bool shadows = light != nullptr && light->shadows != 0u;
    const uint32_t per_block = 64u;
    lod_rays_kernel<<<(ray_count + per_block - 1u) / per_block, per_block>>>(
        view, fine, world != nullptr, shadows, shadows ? light->sun_direction[0] : 0.0F,
        shadows ? light->sun_direction[1] : 0.0F, shadows ? light->sun_direction[2] : 0.0F,
        (const traverse_ray_t *)(const void *)device_memory_pointer(rays),
        (lod_hit_t *)(void *)device_memory_pointer(out), ray_count);
    return device_memory_check(error, cudaGetLastError(), "lod: starting rays");
}
