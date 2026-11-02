/* See README.md. One colour per pixel of the hits: the texture of the face that was hit, at a mip chosen by distance, under
 * that face's brightness. */
#include "shade/index.hu"

#include <math.h>

/* A texel in linear light, with what shows through a see-through one in its place. */
static __device__ __forceinline__ void shade_texel(const shade_texture_t *texture, const uint8_t *pixels,
                                                   uint32_t level, float32_t u, float32_t v, float32_t out[3]) {
    const uint32_t width = shade_level_dimension(texture->width, level);
    const uint32_t height = shade_level_dimension(texture->height, level);
    const uint32_t x = shade_texel_of(u, width), y = shade_texel_of(v, height);
    const uint8_t *const texel =
        pixels + texture->pixels_offset + texture->level_offset[level] + ((uint64_t)y * width + x) * 4u;
    const uint32_t alpha = texel[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t seen = shade_srgb_to_linear((float32_t)texel[i] / 255.0F);
        if (alpha == SHADE_ALPHA_OPAQUE) {
            out[i] = seen;
        } else {
            /* Rays stop at everything until Stage 09.3 lets them go on, so a leaf's holes are its mean colour. */
            const float32_t behind = shade_srgb_to_linear((float32_t)texture->mean_rgba[i] / 255.0F);
            const float32_t cover = (float32_t)alpha / 255.0F;
            out[i] = behind + (seen - behind) * cover;
        }
    }
}

__global__ void shade_kernel(const traverse_camera_t camera, const shade_texture_t *textures, const uint16_t *faces,
                             const uint8_t *pixels, uint32_t material_count, const traverse_hit_t *hits,
                             shade_pixel_t *out) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    const traverse_hit_t hit = hits[index];
    const uint32_t face = (uint32_t)hit.info & TRAVERSE_INFO_FACE_MASK;
    shade_pixel_t result;
    result.t = hit.t;
    if (face == TRAVERSE_FACE_MISS) {
        result.t = TRAVERSE_COARSE_SKY;
        result.rgb[0] = 0.0F;
        result.rgb[1] = 0.0F;
        result.rgb[2] = 0.0F;
        out[index] = result;
        return;
    }
    const uint32_t material = hit.value & TRAVERSE_VALUE_MATERIAL_MASK;
    const uint32_t lookup_face = face == TRAVERSE_FACE_INSIDE ? (uint32_t)TRAVERSE_FACE_POS_Z : face;
    const uint32_t texture_index =
        material < material_count ? faces[material * SHADE_FACES + lookup_face] : (uint32_t)SHADE_NO_TEXTURE;
    if (texture_index == SHADE_NO_TEXTURE) {
        result.rgb[0] = SHADE_MISSING_R;
        result.rgb[1] = SHADE_MISSING_G;
        result.rgb[2] = SHADE_MISSING_B;
        out[index] = result;
        return;
    }
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    /* From the voxel's own corner, so that this is as exact far from the origin as near it. */
    float32_t local[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        local[i] = camera.eye_frac[i] + direction[i] * hit.t - (float32_t)hit.offset[i];
    }
    float32_t u = 0.0F, v = 0.0F;
    shade_face_uv(face, local, &u, &v);
    const shade_texture_t *const texture = &textures[texture_index];
    const uint32_t level = shade_mip_level(hit.t, shade_pixel_tangent(&camera), texture->width, texture->level_count);
    float32_t texel[3];
    shade_texel(texture, pixels, level, u, v, texel);
    const float32_t brightness = shade_face_brightness(face);
    result.rgb[0] = texel[0] * brightness;
    result.rgb[1] = texel[1] * brightness;
    result.rgb[2] = texel[2] * brightness;
    out[index] = result;
}

static gpu_status_t shade_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

gpu_status_t shade_frame(gpu_error_t *error, const shade_scene_t *scene, const traverse_camera_t *camera,
                         const device_block_t *hits, const device_block_t *pixels) {
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
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    shade_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        *camera, (const shade_texture_t *)(const void *)device_memory_pointer(scene->borrowed_textures),
        (const uint16_t *)(const void *)device_memory_pointer(scene->borrowed_faces),
        device_memory_pointer(scene->borrowed_pixels), scene->material_count,
        (const traverse_hit_t *)(const void *)device_memory_pointer(hits),
        (shade_pixel_t *)(void *)device_memory_pointer(pixels));
    return device_memory_check(error, cudaGetLastError(), "shade: starting the kernel");
}
