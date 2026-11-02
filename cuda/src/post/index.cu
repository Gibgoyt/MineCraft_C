/* See README.md. From shade's pixels to what goes on the screen: sky, fog and the tone map to XRGB8888. */
#include "post/index.hu"

#include "gpu_bridge/shade/index.h"

#include <math.h>

__global__ void post_kernel(const post_settings_t settings, const traverse_camera_t camera, const shade_pixel_t *pixels,
                            uint32_t *xrgb) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    const shade_pixel_t pixel = pixels[index];
    float32_t direction[3];
    traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, direction);
    float32_t sky[3];
    post_sky(&settings, direction, sky);
    /* TRAVERSE_COARSE_SKY is past any fog_end, so sky needs no case of its own. */
    const float32_t fog = post_fog(&settings, pixel.t);
    const float32_t red = pixel.rgb[0] + (sky[0] - pixel.rgb[0]) * fog;
    const float32_t green = pixel.rgb[1] + (sky[1] - pixel.rgb[1]) * fog;
    const float32_t blue = pixel.rgb[2] + (sky[2] - pixel.rgb[2]) * fog;
    xrgb[index] = post_xrgb(post_tone_map(red), post_tone_map(green), post_tone_map(blue));
}

static gpu_status_t post_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

gpu_status_t post_frame(gpu_error_t *error, const post_settings_t *settings, const traverse_camera_t *camera,
                        const device_block_t *pixels, const device_block_t *xrgb) {
    if (camera->width == 0u || camera->height == 0u) {
        return post_refuse(error, "post: the camera has no pixels");
    }
    if (pixels->bytes < (uint64_t)camera->width * camera->height * sizeof(shade_pixel_t)) {
        return post_refuse(error, "post: the pixels are too small for the camera");
    }
    if (xrgb->bytes < post_xrgb_bytes(camera->width, camera->height)) {
        return post_refuse(error, "post: the target is too small for the camera");
    }
    if (!(settings->fog_end > settings->fog_start)) {
        return post_refuse(error, "post: fog that ends before it starts");
    }
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    post_kernel<<<grid, dim3(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS)>>>(
        *settings, *camera, (const shade_pixel_t *)(const void *)device_memory_pointer(pixels),
        (uint32_t *)(void *)device_memory_pointer(xrgb));
    return device_memory_check(error, cudaGetLastError(), "post: starting the kernel");
}
