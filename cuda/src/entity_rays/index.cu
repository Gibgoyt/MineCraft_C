/* See README.md. Mobs' and items' boxes against a frame's rays, from each tile's list, and the outline of what is aimed at. */
#include "entity_rays/index.hu"

#include "gpu_bridge/shade/index.h"

#include <math.h>

/* One pixel's nearest box. */
__global__ void entity_rays_kernel(const traverse_camera_t camera, const uint32_t *starts, const uint16_t *links,
                                   const entity_rays_box_t *boxes, entity_rays_hit_t *out) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    const uint64_t tile = (uint64_t)blockIdx.y * gridDim.x + blockIdx.x;
    entity_rays_hit_t nearest = {TRAVERSE_COARSE_SKY, (uint16_t)ENTITY_RAYS_NO_BOX, (uint16_t)TRAVERSE_FACE_MISS};
    const uint32_t first = starts[tile], end = starts[tile + 1u];
    if (first != end) {
        float32_t direction[3];
        traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
        for (uint32_t link = first; link < end; link += 1u) {
            float32_t t = 0.0F;
            uint32_t face = 0u;
            if (entity_rays_box_hit(&boxes[links[link]], direction, &t, &face) && t < nearest.t) {
                nearest.t = t;
                nearest.box = links[link];
                nearest.face = (uint16_t)face;
            }
        }
    }
    out[(uint64_t)pixel_y * camera.width + pixel_x] = nearest;
}

/* Whether all that stands in front of a line at this pixel, up to reach, is water, which a player aims through. */
static __device__ __forceinline__ bool entity_rays_only_water(const traverse_view_t *view,
                                                              const traverse_camera_t *camera,
                                                              const float32_t direction[3], uint32_t water_material,
                                                              float32_t reach) {
    traverse_trace_ray_t ray;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_cell[i] = camera->eye_cell[i];
        ray.eye_frac[i] = camera->eye_frac[i];
        ray.direction[i] = traverse_guard(direction[i]);
    }
    ray.eye_cell[2] -= TRAVERSE_Z_BASE;
    ray.t_start = 0.0F;
    ray.resume = 0u;
    ray.t_end = TRAVERSE_T_END_NONE;
    for (uint32_t layer = 0u; layer < ENTITY_RAYS_OUTLINE_WATER_LAYERS; layer += 1u) {
        traverse_hit_t hit;
        traverse_trace(view, &ray, &hit);
        if (((uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK) == TRAVERSE_FACE_MISS || hit.t >= reach) {
            return true;
        }
        if ((hit.value & TRAVERSE_VALUE_MATERIAL_MASK) != water_material) {
            return false;
        }
        ray.t_start = hit.t;
        ray.resume = 1u;
    }
    return false;
}

/* One pixel of the rectangle the outline's box can touch. */
__global__ void entity_rays_overlay_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                           const entity_rays_mark_t mark, float32_t jitter_x, float32_t jitter_y,
                                           uint32_t water_material, uint32_t tile_x, uint32_t tile_y,
                                           uint32_t inner_width, uint32_t inner_height,
                                           const entity_rays_hit_t *entity_hits, const shade_pixel_t *shaded,
                                           uint32_t *xrgb) {
    const uint32_t pixel_x = (tile_x + blockIdx.x) * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = (tile_y + blockIdx.y) * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    /* A finished picture is about the pixel's middle, whatever this frame's rays looked at. */
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F - jitter_x, (float32_t)pixel_y + 0.5F - jitter_y,
                              direction);
    const float32_t tangent = shade_pixel_tangent(&camera);
    const float32_t half_wide = (float32_t)entity_rays_outline_pixels(camera.height) * 0.5F;
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    /* The depths were drawn at the inner size (plan 008 phase 05), the outline at the output's: the inner pixel under this one. */
    const uint64_t under = (uint64_t)post_scaled_under(pixel_y, camera.height, inner_height) * inner_width +
                           post_scaled_under(pixel_x, camera.width, inner_width);
    const float32_t surface_t = shaded[under].t;
    const float32_t box_t = entity_hits != nullptr ? entity_hits[under].t : TRAVERSE_COARSE_SKY;
    for (uint32_t edge = 0u; edge < ENTITY_RAYS_EDGES; edge += 1u) {
        float32_t from[3], to[3], t = 0.0F;
        entity_rays_edge(mark.box_min, mark.box_max, edge, from, to);
        if (entity_rays_line_pixels(from, to, direction, tangent, &t) > half_wide || t < ENTITY_RAYS_OUTLINE_NEAR) {
            continue;
        }
        const float32_t reach = t - ENTITY_RAYS_OUTLINE_DEPTH_BLOCKS - ENTITY_RAYS_OUTLINE_DEPTH_PIXELS * tangent * t;
        if (box_t < reach) {
            continue;
        }
        if (surface_t >= reach || (water_material != SHADE_NO_MATERIAL &&
                                   entity_rays_only_water(&view, &camera, direction, water_material, reach))) {
            xrgb[index] = ENTITY_RAYS_OUTLINE_XRGB;
            return;
        }
    }
}

/* The nearest of a preview's boxes along a ray from the eye: each cell's shape, from the world's shapes as traverse_voxel_hit has it, turned
 * by its facing. False if the ray meets none of them further than ENTITY_RAYS_OUTLINE_NEAR ahead. */
static __device__ __forceinline__ bool entity_rays_preview_hit(const traverse_view_t *view,
                                                               const traverse_camera_t *camera,
                                                               const entity_rays_preview_t *preview,
                                                               const float32_t direction[3], float32_t *out_t,
                                                               uint32_t *out_face) {
    float32_t inverse[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        inverse[i] = 1.0F / traverse_guard(direction[i]);
    }
    float32_t best = TRAVERSE_COARSE_SKY;
    uint32_t best_face = 0u;
    for (uint32_t c = 0u; c < preview->count; c += 1u) {
        const uint32_t voxel = preview->voxels[c];
        const uint32_t material = voxel & TRAVERSE_VALUE_MATERIAL_MASK;
        uint32_t shape = TRAVERSE_SHAPE_CUBE;
        if (view->borrowed_shapes != nullptr && material < view->shape_count) {
            const uint32_t given = view->borrowed_shapes[material];
            shape = given < TRAVERSE_SHAPE_COUNT ? given : (uint32_t)TRAVERSE_SHAPE_CUBE;
        }
        const uint32_t facing = block_state_facing((uint16_t)voxel);
        const traverse_shape_row_t row = traverse_shape_rows[shape];
        for (uint32_t b = 0u; b < row.count; b += 1u) {
            traverse_box_t box = traverse_boxes[row.first + b];
            block_facing_box_turn(facing, 32u, box.low, box.high);
            float32_t t_in = -TRAVERSE_COARSE_SKY, t_out = TRAVERSE_COARSE_SKY;
            uint32_t axis = 0u;
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                const float32_t low =
                    (float32_t)preview->cells[c][i] + (float32_t)box.low[i] * TRAVERSE_BOX_UNIT - camera->eye_frac[i];
                const float32_t high =
                    (float32_t)preview->cells[c][i] + (float32_t)box.high[i] * TRAVERSE_BOX_UNIT - camera->eye_frac[i];
                const float32_t t_low = low * inverse[i], t_high = high * inverse[i];
                const float32_t t_near = t_low < t_high ? t_low : t_high, t_far = t_low < t_high ? t_high : t_low;
                if (t_near > t_in) {
                    t_in = t_near;
                    axis = i;
                }
                t_out = t_far < t_out ? t_far : t_out;
            }
            if (t_in < t_out && t_in > ENTITY_RAYS_OUTLINE_NEAR && t_in < best) {
                best = t_in;
                best_face = axis * 2u + (direction[axis] > 0.0F ? 0u : 1u);
            }
        }
    }
    *out_t = best;
    *out_face = best_face;
    return best < TRAVERSE_COARSE_SKY;
}

/* One pixel of the rectangle a preview's cells can touch: its colour over the picture where it is the nearest thing there, by the outline's
 * rules of what hides what. */
__global__ void entity_rays_preview_kernel(const traverse_view_t view, const traverse_camera_t camera,
                                           const entity_rays_preview_t preview, float32_t jitter_x, float32_t jitter_y,
                                           uint32_t water_material, uint32_t tile_x, uint32_t tile_y,
                                           uint32_t inner_width, uint32_t inner_height,
                                           const entity_rays_hit_t *entity_hits, const shade_pixel_t *shaded,
                                           uint32_t *xrgb) {
    const uint32_t pixel_x = (tile_x + blockIdx.x) * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = (tile_y + blockIdx.y) * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F - jitter_x, (float32_t)pixel_y + 0.5F - jitter_y,
                              direction);
    float32_t t = 0.0F;
    uint32_t face = 0u;
    if (!entity_rays_preview_hit(&view, &camera, &preview, direction, &t, &face)) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    const uint64_t under = (uint64_t)post_scaled_under(pixel_y, camera.height, inner_height) * inner_width +
                           post_scaled_under(pixel_x, camera.width, inner_width);
    const float32_t tangent = shade_pixel_tangent(&camera);
    const float32_t reach = t - ENTITY_RAYS_OUTLINE_DEPTH_BLOCKS - ENTITY_RAYS_OUTLINE_DEPTH_PIXELS * tangent * t;
    const float32_t box_t = entity_hits != nullptr ? entity_hits[under].t : TRAVERSE_COARSE_SKY;
    if (box_t < reach) {
        return;
    }
    if (shaded[under].t >= reach || (water_material != SHADE_NO_MATERIAL &&
                                     entity_rays_only_water(&view, &camera, direction, water_material, reach))) {
        xrgb[index] = entity_rays_preview_blend(xrgb[index], preview.xrgb, face);
    }
}

static gpu_status_t entity_rays_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

gpu_status_t entity_rays_frame(gpu_error_t *error, const entity_rays_scene_t *scene, const traverse_camera_t *camera) {
    if (scene->box_count == 0u) {
        return GPU_STATUS_OK;
    }
    if (camera->width == 0u || camera->height == 0u) {
        return entity_rays_refuse(error, "entity_rays: the camera has no pixels");
    }
    if (scene->box_count > ENTITY_RAYS_BOXES_MAX || scene->link_count > ENTITY_RAYS_LINKS_MAX) {
        return entity_rays_refuse(error, "entity_rays: more boxes or links than a frame can hold");
    }
    if (scene->borrowed_lists == nullptr || scene->borrowed_hits == nullptr ||
        scene->borrowed_lists->bytes < entity_rays_textures_offset(camera->width, camera->height) ||
        scene->borrowed_hits->bytes < entity_rays_hits_bytes(camera->width, camera->height)) {
        return entity_rays_refuse(error, "entity_rays: the lists or the hits are missing or too small for the camera");
    }
    const uint8_t *const lists = device_memory_pointer(scene->borrowed_lists);
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    entity_rays_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        *camera, (const uint32_t *)(const void *)(lists + entity_rays_starts_offset()),
        (const uint16_t *)(const void *)(lists + entity_rays_links_offset(camera->width, camera->height)),
        (const entity_rays_box_t *)(const void *)lists,
        (entity_rays_hit_t *)(void *)device_memory_pointer(scene->borrowed_hits));
    return device_memory_check(error, cudaGetLastError(), "entity_rays: starting the kernel");
}

gpu_status_t entity_rays_overlay(gpu_error_t *error, const entity_rays_scene_t *scene, const traverse_world_t *world,
                                 const traverse_camera_t *camera, const float32_t jitter[2], uint32_t water_material,
                                 const device_block_t *shaded, const device_block_t *xrgb) {
    return entity_rays_overlay_scaled(error, scene, world, camera, camera->width, camera->height, jitter,
                                      water_material, shaded, xrgb);
}

gpu_status_t entity_rays_overlay_scaled(gpu_error_t *error, const entity_rays_scene_t *scene,
                                        const traverse_world_t *world, const traverse_camera_t *camera,
                                        uint32_t inner_width, uint32_t inner_height, const float32_t jitter[2],
                                        uint32_t water_material, const device_block_t *shaded,
                                        const device_block_t *xrgb) {
    const entity_rays_mark_t *const mark = &scene->mark;
    const entity_rays_preview_t *const preview = &scene->preview;
    const bool lines =
        mark->outline != 0u && mark->pixel_min[0] < mark->pixel_end[0] && mark->pixel_min[1] < mark->pixel_end[1];
    const bool ghost = preview->count != 0u && preview->pixel_min[0] < preview->pixel_end[0] &&
                       preview->pixel_min[1] < preview->pixel_end[1];
    if (!lines && !ghost) {
        return GPU_STATUS_OK;
    }
    if (lines && (mark->pixel_end[0] > camera->width || mark->pixel_end[1] > camera->height)) {
        return entity_rays_refuse(error, "entity_rays: the outline's rectangle is off the picture");
    }
    if (ghost && (preview->pixel_end[0] > camera->width || preview->pixel_end[1] > camera->height ||
                  preview->count > ENTITY_RAYS_PREVIEW_CELLS)) {
        return entity_rays_refuse(error,
                                  "entity_rays: the preview's rectangle is off the picture, or it has too many cells");
    }
    if (inner_width == 0u || inner_height == 0u || inner_width > camera->width || inner_height > camera->height) {
        return entity_rays_refuse(error, "entity_rays: the depths' size is not inside the picture's");
    }
    if (shaded->bytes < shade_pixels_bytes(inner_width, inner_height) ||
        xrgb->bytes < (uint64_t)camera->width * camera->height * sizeof(uint32_t)) {
        return entity_rays_refuse(error, "entity_rays: the pixels or the picture are too small for the camera");
    }
    traverse_view_t view;
    const gpu_status_t status = traverse_view_from(error, world, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const entity_rays_hit_t *const entity_hits =
        scene->box_count != 0u ? (const entity_rays_hit_t *)(const void *)device_memory_pointer(scene->borrowed_hits)
                               : nullptr;
    const shade_pixel_t *const pixels = (const shade_pixel_t *)(const void *)device_memory_pointer(shaded);
    uint32_t *const picture = (uint32_t *)(void *)device_memory_pointer(xrgb);
    const dim3 tile(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS);
    /* The preview first, so that the outline's lines are drawn over it. */
    if (ghost) {
        const uint32_t tile_x = preview->pixel_min[0] / TRAVERSE_TILE_PIXELS;
        const uint32_t tile_y = preview->pixel_min[1] / TRAVERSE_TILE_PIXELS;
        const dim3 grid(traverse_tiles_across(preview->pixel_end[0]) - tile_x,
                        traverse_tiles_across(preview->pixel_end[1]) - tile_y);
        entity_rays_preview_kernel<<<grid, tile>>>(view, *camera, *preview, jitter[0], jitter[1], water_material,
                                                   tile_x, tile_y, inner_width, inner_height, entity_hits, pixels,
                                                   picture);
        const gpu_status_t started =
            device_memory_check(error, cudaGetLastError(), "entity_rays: starting the preview");
        if (started != GPU_STATUS_OK || !lines) {
            return started;
        }
    }
    const uint32_t tile_x = mark->pixel_min[0] / TRAVERSE_TILE_PIXELS;
    const uint32_t tile_y = mark->pixel_min[1] / TRAVERSE_TILE_PIXELS;
    const dim3 grid(traverse_tiles_across(mark->pixel_end[0]) - tile_x,
                    traverse_tiles_across(mark->pixel_end[1]) - tile_y);
    entity_rays_overlay_kernel<<<grid, tile>>>(view, *camera, *mark, jitter[0], jitter[1], water_material, tile_x,
                                               tile_y, inner_width, inner_height, entity_hits, pixels, picture);
    return device_memory_check(error, cudaGetLastError(), "entity_rays: starting the outline");
}
