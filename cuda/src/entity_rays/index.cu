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
    const float32_t surface_t = shaded[index].t;
    const float32_t box_t = entity_hits != nullptr ? entity_hits[index].t : TRAVERSE_COARSE_SKY;
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
    const entity_rays_mark_t *const mark = &scene->mark;
    if (mark->outline == 0u || mark->pixel_min[0] >= mark->pixel_end[0] || mark->pixel_min[1] >= mark->pixel_end[1]) {
        return GPU_STATUS_OK;
    }
    if (mark->pixel_end[0] > camera->width || mark->pixel_end[1] > camera->height) {
        return entity_rays_refuse(error, "entity_rays: the outline's rectangle is off the picture");
    }
    if (shaded->bytes < shade_pixels_bytes(camera->width, camera->height) ||
        xrgb->bytes < (uint64_t)camera->width * camera->height * sizeof(uint32_t)) {
        return entity_rays_refuse(error, "entity_rays: the pixels or the picture are too small for the camera");
    }
    traverse_view_t view;
    const gpu_status_t status = traverse_view_from(error, world, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const uint32_t tile_x = mark->pixel_min[0] / TRAVERSE_TILE_PIXELS;
    const uint32_t tile_y = mark->pixel_min[1] / TRAVERSE_TILE_PIXELS;
    const dim3 grid(traverse_tiles_across(mark->pixel_end[0]) - tile_x,
                    traverse_tiles_across(mark->pixel_end[1]) - tile_y);
    entity_rays_overlay_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        view, *camera, *mark, jitter[0], jitter[1], water_material, tile_x, tile_y,
        scene->box_count != 0u ? (const entity_rays_hit_t *)(const void *)device_memory_pointer(scene->borrowed_hits)
                               : nullptr,
        (const shade_pixel_t *)(const void *)device_memory_pointer(shaded),
        (uint32_t *)(void *)device_memory_pointer(xrgb));
    return device_memory_check(error, cudaGetLastError(), "entity_rays: starting the outline");
}
