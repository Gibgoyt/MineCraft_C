/* See README.md. One colour per pixel of the hits: the texture of the face that was hit, at a mip chosen by distance, under
 * that face's brightness, the light stored in its voxel, and what its one sun ray saw. */
#include "shade/index.hu"

#include <math.h>

/* A texel's four bytes. */
static __device__ __forceinline__ const uint8_t *shade_texel(const shade_texture_t *texture, const uint8_t *pixels,
                                                             uint32_t level, float32_t u, float32_t v) {
    const uint32_t width = shade_level_dimension(texture->width, level);
    const uint32_t height = shade_level_dimension(texture->height, level);
    const uint32_t x = shade_texel_of(u, width), y = shade_texel_of(v, height);
    return pixels + texture->pixels_offset + texture->level_offset[level] + ((uint64_t)y * width + x) * 4u;
}

/* A texel in linear light, with what shows through a part-covered one in its place. */
static __device__ __forceinline__ void shade_texel_colour(const shade_texture_t *texture, const uint8_t *texel,
                                                          float32_t out[3]) {
    const uint32_t alpha = texel[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t seen = shade_srgb_to_linear((float32_t)texel[i] / 255.0F);
        if (alpha == SHADE_ALPHA_OPAQUE) {
            out[i] = seen;
        } else {
            /* Rays stop at a part-covered texel until Stage 09.3 lets them go on, so what is behind it is its mean colour. */
            const float32_t behind = shade_srgb_to_linear((float32_t)texture->mean_rgba[i] / 255.0F);
            const float32_t cover = (float32_t)alpha / 255.0F;
            out[i] = behind + (seen - behind) * cover;
        }
    }
}

/* How much of the sun reaches this point of a surface, from 0 to 1: the one sun ray. It starts from the surface's own voxel, so it
 * is as exact far from the eye as near it, and goes on through what is see-through as a primary ray does, with a part-covered
 * texel taking its share. from->t, how far off the surface is seen, chooses the mip of whatever is in the way, so that a far tree's
 * shadow has the same holes as its leaves. */
static __device__ __forceinline__ float32_t shade_sun_seen(const traverse_view_t *view, const traverse_camera_t *camera,
                                                           const shade_texture_t *textures, const uint16_t *faces,
                                                           const uint8_t *pixels, uint32_t material_count,
                                                           uint32_t features, const float32_t sun_direction[3],
                                                           const traverse_hit_t *from, uint32_t from_face,
                                                           const float32_t from_local[3], float32_t tangent) {
    traverse_trace_ray_t ray;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        ray.eye_cell[i] = camera->eye_cell[i] + (int32_t)from->offset[i];
        ray.eye_frac[i] = from_local[i];
        ray.direction[i] = traverse_guard(sun_direction[i]);
    }
    ray.eye_cell[2] -= TRAVERSE_Z_BASE;
    const float32_t bias = shade_sun_bias(from->t);
    ray.eye_frac[from_face >> 1] += (from_face & 1u) != 0u ? bias : -bias;
    ray.t_start = 0.0F;
    ray.resume = 1u;
    float32_t seen = 1.0F;
    for (uint32_t layer = 0u; layer < SHADE_SUN_LAYERS; layer += 1u) {
        traverse_hit_t hit;
        traverse_trace(view, &ray, &hit);
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        if (face == TRAVERSE_FACE_MISS) {
            return seen;
        }
        if ((features & SHADE_FEATURE_ALPHA) == 0u) {
            return 0.0F;
        }
        const uint32_t material = hit.value & TRAVERSE_VALUE_MATERIAL_MASK;
        const uint32_t lookup_face = face == TRAVERSE_FACE_INSIDE ? (uint32_t)TRAVERSE_FACE_POS_Z : face;
        const uint32_t texture_index =
            material < material_count ? faces[material * SHADE_FACES + lookup_face] : (uint32_t)SHADE_NO_TEXTURE;
        if (texture_index == SHADE_NO_TEXTURE) {
            return 0.0F;
        }
        float32_t local[3];
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            local[i] = ray.eye_frac[i] + ray.direction[i] * hit.t - (float32_t)hit.offset[i];
        }
        float32_t u = 0.0F, v = 0.0F;
        shade_face_uv(face, local, &u, &v);
        const shade_texture_t *const texture = &textures[texture_index];
        const uint32_t level = shade_mip_level(from->t, tangent, texture->width, texture->level_count);
        const uint32_t alpha = shade_texel(texture, pixels, level, u, v)[3];
        if (alpha == SHADE_ALPHA_OPAQUE) {
            return 0.0F;
        }
        seen *= 1.0F - (float32_t)alpha / 255.0F;
        ray.t_start = hit.t;
    }
    return 0.0F;
}

__global__ void shade_kernel(const traverse_view_t view, const traverse_camera_t camera,
                             const shade_texture_t *textures, const uint16_t *faces, const uint8_t *pixels,
                             uint32_t material_count, uint32_t features, const shade_sun_t sun,
                             const traverse_hit_t *hits, shade_pixel_t *out) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    const float32_t tangent = shade_pixel_tangent(&camera);
    traverse_hit_t hit = hits[index];
    shade_pixel_t result;
    /* Each pass is one surface: all but the last are holes, and the ray goes on past them. A ray never comes back to a surface,
     * so this ends by itself, and SHADE_ALPHA_LAYERS is only what a line of leaves may cost a pixel. */
    for (uint32_t layer = 0u;; layer += 1u) {
        const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
        result.t = hit.t;
        if (face == TRAVERSE_FACE_MISS) {
            result.t = TRAVERSE_COARSE_SKY;
            result.rgb[0] = 0.0F;
            result.rgb[1] = 0.0F;
            result.rgb[2] = 0.0F;
            break;
        }
        const uint32_t material = hit.value & TRAVERSE_VALUE_MATERIAL_MASK;
        const uint32_t lookup_face = face == TRAVERSE_FACE_INSIDE ? (uint32_t)TRAVERSE_FACE_POS_Z : face;
        const uint32_t texture_index =
            material < material_count ? faces[material * SHADE_FACES + lookup_face] : (uint32_t)SHADE_NO_TEXTURE;
        if (texture_index == SHADE_NO_TEXTURE) {
            result.rgb[0] = SHADE_MISSING_R;
            result.rgb[1] = SHADE_MISSING_G;
            result.rgb[2] = SHADE_MISSING_B;
            break;
        }
        /* From the voxel's own corner, so that this is as exact far from the origin as near it. */
        float32_t local[3];
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            local[i] = camera.eye_frac[i] + direction[i] * hit.t - (float32_t)hit.offset[i];
        }
        float32_t u = 0.0F, v = 0.0F;
        shade_face_uv(face, local, &u, &v);
        const shade_texture_t *const texture = &textures[texture_index];
        const uint32_t level = shade_mip_level(hit.t, tangent, texture->width, texture->level_count);
        const uint8_t *const texel = shade_texel(texture, pixels, level, u, v);
        if (texel[3] == 0u && (features & SHADE_FEATURE_ALPHA) != 0u && layer + 1u < SHADE_ALPHA_LAYERS) {
            traverse_trace_ray_t ray;
            ray.eye_cell[0] = camera.eye_cell[0];
            ray.eye_cell[1] = camera.eye_cell[1];
            ray.eye_cell[2] = camera.eye_cell[2] - TRAVERSE_Z_BASE;
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                ray.eye_frac[i] = camera.eye_frac[i];
                ray.direction[i] = traverse_guard(direction[i]);
            }
            ray.t_start = hit.t;
            ray.resume = 1u;
            traverse_trace(&view, &ray, &hit);
            continue;
        }
        float32_t colour[3];
        shade_texel_colour(texture, texel, colour);
        const uint32_t stored = (features & SHADE_FEATURE_LIGHT) != 0u
                                    ? (hit.value >> TRAVERSE_VALUE_LIGHT_SHIFT) & 0xFFu
                                    : (uint32_t)SHADE_LIGHT_OPEN_SKY;
        float32_t sun_seen = 1.0F;
        if ((features & SHADE_FEATURE_SHADOW) != 0u && shade_sun_towards(&sun, face, stored) > 0.0F &&
            sun.direct[0] + sun.direct[1] + sun.direct[2] > 0.0F) {
            sun_seen = shade_sun_seen(&view, &camera, textures, faces, pixels, material_count, features, sun.direction,
                                      &hit, face, local, tangent);
        }
        float32_t light[3];
        shade_surface_light(&sun, face, stored, sun_seen, light);
        result.rgb[0] = colour[0] * light[0];
        result.rgb[1] = colour[1] * light[1];
        result.rgb[2] = colour[2] * light[2];
        break;
    }
    out[index] = result;
}

static gpu_status_t shade_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

gpu_status_t shade_frame(gpu_error_t *error, const shade_scene_t *scene, const traverse_world_t *world,
                         const traverse_camera_t *camera, const device_block_t *hits, const device_block_t *pixels) {
    if (camera->width == 0u || camera->height == 0u) {
        return shade_refuse(error, "shade: the camera has no pixels");
    }
    if (hits->bytes < traverse_hits_bytes(camera->width, camera->height)) {
        return shade_refuse(error, "shade: the hits are too small for the camera");
    }
    if (pixels->bytes < shade_pixels_bytes(camera->width, camera->height)) {
        return shade_refuse(error, "shade: the pixel target is too small for the camera");
    }
    if (scene->borrowed_textures->bytes < (uint64_t)scene->texture_count * sizeof(shade_texture_t) ||
        scene->borrowed_faces->bytes < (uint64_t)scene->material_count * SHADE_FACES * sizeof(uint16_t)) {
        return shade_refuse(error, "shade: a table is smaller than the scene says");
    }
    traverse_view_t view;
    const gpu_status_t status = traverse_view_from(error, world, &view);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    shade_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        view, *camera, (const shade_texture_t *)(const void *)device_memory_pointer(scene->borrowed_textures),
        (const uint16_t *)(const void *)device_memory_pointer(scene->borrowed_faces),
        device_memory_pointer(scene->borrowed_pixels), scene->material_count, scene->features, scene->sun,
        (const traverse_hit_t *)(const void *)device_memory_pointer(hits),
        (shade_pixel_t *)(void *)device_memory_pointer(pixels));
    return device_memory_check(error, cudaGetLastError(), "shade: starting the kernel");
}
