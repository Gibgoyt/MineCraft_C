/* See README.md. From shade's pixels to what goes on the screen: sky, fog, god rays, the temporal pass and the tone map to XRGB8888. */
#include "post/index.hu"

#include "gpu_bridge/shade/index.h"

#include <math.h>

/* A place in the god rays, for a pixel: between the four texels around it, each by how near it is and whether it is at the same
 * distance, so that a far texel does not bleed onto a near pixel. Where none is, all four count. */
static __device__ __forceinline__ float32_t post_shaft_at(const post_shaft_t *shafts, const traverse_camera_t *camera,
                                                          uint32_t pixel_x, uint32_t pixel_y, float32_t t) {
    const uint32_t across = post_shafts_across(camera->width), down = post_shafts_across(camera->height);
    const uint32_t x0 = pixel_x / 2u, y0 = pixel_y / 2u;
    const uint32_t x1 = x0 + 1u < across ? x0 + 1u : x0, y1 = y0 + 1u < down ? y0 + 1u : y0;
    const float32_t fx = (pixel_x & 1u) != 0u ? 0.5F : 0.0F, fy = (pixel_y & 1u) != 0u ? 0.5F : 0.0F;
    const post_shaft_t taps[4] = {shafts[(uint64_t)y0 * across + x0], shafts[(uint64_t)y0 * across + x1],
                                  shafts[(uint64_t)y1 * across + x0], shafts[(uint64_t)y1 * across + x1]};
    const float32_t weights[4] = {(1.0F - fx) * (1.0F - fy), fx * (1.0F - fy), (1.0F - fx) * fy, fx * fy};
    float32_t sum = 0.0F, all = 0.0F, count = 0.0F;
    for (uint32_t i = 0u; i < 4u; i += 1u) {
        all += taps[i].amount * weights[i];
        if (fabsf(taps[i].t - t) <= t * 0.2F) {
            sum += taps[i].amount * weights[i];
            count += weights[i];
        }
    }
    return count > 0.0F ? sum / count : all;
}

/* Fog, sky and god rays. With keep, this leaves what was seen in pixels, in the temporal pass's form, for post_taa_kernel to go on
 * from. Without it, this is the end, and writes the picture. */
__global__ void post_kernel(const post_settings_t settings, const traverse_camera_t camera, shade_pixel_t *pixels,
                            uint32_t *xrgb, const post_shaft_t *shafts, float32_t shaft_r, float32_t shaft_g,
                            float32_t shaft_b, bool keep, const post_weather_t weather, bool weather_on,
                            const int16_t *weather_map) {
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
    /* Weather fog goes toward its own colour: steady from frame to frame, so before the history, as fog always was. */
    if (weather_on && weather.fog_mix > 0.0F) {
        float32_t toward[3];
        post_weather_fog_colour(&weather, sky, toward);
        sky[0] = toward[0];
        sky[1] = toward[1];
        sky[2] = toward[2];
    }
    /* TRAVERSE_COARSE_SKY is past any fog_end, so sky needs no case of its own but its discs. */
    const float32_t fog = post_fog_along(&settings, direction[2], pixel.t);
    float32_t seen[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        seen[i] = pixel.rgb[i] + (sky[i] - pixel.rgb[i]) * fog;
    }
    if (pixel.t >= TRAVERSE_COARSE_SKY) {
        post_sky_discs(&settings, direction, seen);
    }
    if (shafts != nullptr) {
        const float32_t amount = post_shaft_at(shafts, &camera, pixel_x, pixel_y, pixel.t);
        seen[0] += amount * shaft_r;
        seen[1] += amount * shaft_g;
        seen[2] += amount * shaft_b;
    }
    if (settings.under_water != 0u) {
        post_under_water(&settings, pixel.t, seen);
    }
    if (keep) {
        post_taa_squeeze(seen);
        pixels[index].rgb[0] = seen[0];
        pixels[index].rgb[1] = seen[1];
        pixels[index].rgb[2] = seen[2];
        return;
    }
    if (weather_on) {
        post_weather_apply(&weather, &camera, weather_map, direction, pixel.t, seen);
    }
    xrgb[index] = post_xrgb(post_tone_map(seen[0]), post_tone_map(seen[1]), post_tone_map(seen[2]));
}

/* The history at a place, in pixels from the top left corner, between the four texels around it by how near each is. Off the edge it takes
 * the edge's pixel. */
static __device__ __forceinline__ void post_history_bilinear(const post_taa_pixel_t *history, uint32_t width,
                                                             uint32_t height, float32_t x, float32_t y,
                                                             float32_t out[3]) {
    const float32_t centre_x = x - 0.5F, centre_y = y - 0.5F;
    const float32_t floor_x = floorf(centre_x), floor_y = floorf(centre_y);
    const float32_t fx = centre_x - floor_x, fy = centre_y - floor_y;
    const int32_t x0 = traverse_clamp((int32_t)floor_x, 0, (int32_t)width - 1);
    const int32_t x1 = traverse_clamp((int32_t)floor_x + 1, 0, (int32_t)width - 1);
    const int32_t y0 = traverse_clamp((int32_t)floor_y, 0, (int32_t)height - 1);
    const int32_t y1 = traverse_clamp((int32_t)floor_y + 1, 0, (int32_t)height - 1);
    const post_taa_pixel_t *const top = &history[(uint64_t)y0 * width];
    const post_taa_pixel_t *const bottom = &history[(uint64_t)y1 * width];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t upper = top[x0].rgb[i] + (top[x1].rgb[i] - top[x0].rgb[i]) * fx;
        const float32_t lower = bottom[x0].rgb[i] + (bottom[x1].rgb[i] - bottom[x0].rgb[i]) * fx;
        out[i] = upper + (lower - upper) * fy;
    }
}

/* Catmull-Rom from five bilinear reads, its four corner texels left out (Jimenez, "Filmic SMAA", 2016): the middle two texels each way as one
 * read, at the place between them their weights make, and the four arms. */
static __device__ __forceinline__ void post_history_five_tap(const post_taa_pixel_t *history, uint32_t width,
                                                             uint32_t height, float32_t x, float32_t y,
                                                             float32_t out[3]) {
    const float32_t first_x = floorf(x - 0.5F) + 0.5F, first_y = floorf(y - 0.5F) + 0.5F;
    float32_t across[4], down[4];
    post_catmull_rom(x - first_x, across);
    post_catmull_rom(y - first_y, down);
    const float32_t middle_x = across[1] + across[2], middle_y = down[1] + down[2];
    const float32_t at_x = first_x + across[2] / middle_x, at_y = first_y + down[2] / middle_y;
    const float32_t places[5][2] = {
        {at_x, first_y - 1.0F}, {first_x - 1.0F, at_y}, {at_x, at_y}, {first_x + 2.0F, at_y}, {at_x, first_y + 2.0F}};
    const float32_t weights[5] = {middle_x * down[0], across[0] * middle_y, middle_x * middle_y, across[3] * middle_y,
                                  middle_x * down[3]};
    float32_t sum = 0.0F;
    out[0] = 0.0F;
    out[1] = 0.0F;
    out[2] = 0.0F;
    for (uint32_t k = 0u; k < 5u; k += 1u) {
        float32_t one[3];
        post_history_bilinear(history, width, height, places[k][0], places[k][1], one);
        out[0] += one[0] * weights[k];
        out[1] += one[1] * weights[k];
        out[2] += one[2] * weights[k];
        sum += weights[k];
    }
    out[0] /= sum;
    out[1] /= sum;
    out[2] /= sum;
}

/* What the last frame's picture had at a place, in pixels from the top left corner: that pixel if the place is at its middle, as it
 * is for a camera that has not moved, and if not then Catmull-Rom, as anything softer would blur the picture a little more on every
 * frame. On a fast turn (plan 008 phase 04), POST_TAA_FAST_READ: motion hides the softness. Off the edge it takes the edge's pixel. */
static __device__ __forceinline__ void post_history_at(const post_taa_pixel_t *history, uint32_t width, uint32_t height,
                                                       float32_t x, float32_t y, bool fast, float32_t out[3]) {
    const float32_t centre_x = x - 0.5F, centre_y = y - 0.5F;
    const float32_t floor_x = floorf(centre_x), floor_y = floorf(centre_y);
    const float32_t fx = centre_x - floor_x, fy = centre_y - floor_y;
    if (fminf(fx, 1.0F - fx) <= POST_TAA_STILL && fminf(fy, 1.0F - fy) <= POST_TAA_STILL) {
        const post_taa_pixel_t *const one =
            &history[(uint64_t)traverse_clamp((int32_t)floorf(y), 0, (int32_t)height - 1) * width +
                     (uint32_t)traverse_clamp((int32_t)floorf(x), 0, (int32_t)width - 1)];
        out[0] = one->rgb[0];
        out[1] = one->rgb[1];
        out[2] = one->rgb[2];
        return;
    }
    if (fast && POST_TAA_FAST_READ == POST_TAA_READ_BILINEAR) {
        post_history_bilinear(history, width, height, x, y, out);
        return;
    }
    if (fast && POST_TAA_FAST_READ == POST_TAA_READ_FIVE_TAP) {
        post_history_five_tap(history, width, height, x, y, out);
        return;
    }
    float32_t across[4], down[4];
    post_catmull_rom(fx, across);
    post_catmull_rom(fy, down);
    out[0] = 0.0F;
    out[1] = 0.0F;
    out[2] = 0.0F;
    for (int32_t j = 0; j < 4; j += 1) {
        const int32_t row = traverse_clamp((int32_t)floor_y + j - 1, 0, (int32_t)height - 1);
        for (int32_t i = 0; i < 4; i += 1) {
            const int32_t column = traverse_clamp((int32_t)floor_x + i - 1, 0, (int32_t)width - 1);
            const post_taa_pixel_t *const one = &history[(uint64_t)row * width + (uint32_t)column];
            const float32_t weight = across[i] * down[j];
            out[0] += one->rgb[0] * weight;
            out[1] += one->rgb[1] * weight;
            out[2] += one->rgb[2] * weight;
        }
    }
}

/* The temporal pass: see README.md. */
__global__ void post_taa_kernel(const traverse_camera_t camera, const post_temporal_t temporal,
                                const shade_pixel_t *pixels, const post_taa_pixel_t *history_last,
                                post_taa_pixel_t *history_next, uint32_t *xrgb, const post_weather_t weather,
                                bool weather_on, const int16_t *weather_map) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= camera.width || pixel_y >= camera.height) {
        return;
    }
    const uint64_t index = (uint64_t)pixel_y * camera.width + pixel_x;
    const shade_pixel_t pixel = pixels[index];
    float32_t result[3] = {pixel.rgb[0], pixel.rgb[1], pixel.rgb[2]};
    float32_t jitter[2];
    post_temporal_jitter(&temporal, jitter);
    if (temporal.history_valid != 0u) {
        float32_t steady[3];
        traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F - jitter[0], (float32_t)pixel_y + 0.5F - jitter[1],
                                  steady);
        float32_t x = 0.0F, y = 0.0F;
        if (post_reproject(&temporal.previous, &camera, steady, pixel.t, &x, &y)) {
            /* What this frame's pixels around this one have, in the form they are mixed in. */
            float32_t mean[3] = {0.0F, 0.0F, 0.0F}, squares[3] = {0.0F, 0.0F, 0.0F};
            for (int32_t dy = -1; dy <= 1; dy += 1) {
                const int32_t row = traverse_clamp((int32_t)pixel_y + dy, 0, (int32_t)camera.height - 1);
                for (int32_t dx = -1; dx <= 1; dx += 1) {
                    const int32_t column = traverse_clamp((int32_t)pixel_x + dx, 0, (int32_t)camera.width - 1);
                    const shade_pixel_t *const beside = &pixels[(uint64_t)row * camera.width + (uint32_t)column];
                    for (uint32_t i = 0u; i < 3u; i += 1u) {
                        mean[i] += beside->rgb[i];
                        squares[i] += beside->rgb[i] * beside->rgb[i];
                    }
                }
            }
            float32_t last[3];
            post_history_at(history_last, camera.width, camera.height, x, y,
                            (temporal.flags & POST_TEMPORAL_FAST) != 0u, last);
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                const float32_t average = mean[i] / 9.0F;
                const float32_t deviation = sqrtf(fmaxf(squares[i] / 9.0F - average * average, 0.0F));
                const float32_t low = average - POST_TAA_CLIP * deviation, high = average + POST_TAA_CLIP * deviation;
                const float32_t kept = fminf(fmaxf(last[i], low), high);
                result[i] = kept + (pixel.rgb[i] - kept) * POST_TAA_BLEND;
            }
        }
    }
    history_next[index] = post_taa_pixel_t{{result[0], result[1], result[2]}};
    post_taa_unsqueeze(result);
    /* After the history: a drop moves several pixels a frame, and mixed into it would smear into haze and leave ghosts. Along the
     * pixel's ray with no jitter, so the drops do not shake while the history settles under them. */
    if (weather_on) {
        float32_t steady[3];
        traverse_camera_direction(&camera, (float32_t)pixel_x + 0.5F - jitter[0], (float32_t)pixel_y + 0.5F - jitter[1],
                                  steady);
        post_weather_apply(&weather, &camera, weather_map, steady, pixel.t, result);
    }
    xrgb[index] = post_xrgb(post_tone_map(result[0]), post_tone_map(result[1]), post_tone_map(result[2]));
}

static gpu_status_t post_refuse(gpu_error_t *error, const char *what) {
    *error = gpu_error_t{what, nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
    return GPU_STATUS_INVALID_ARGUMENT;
}

/* A block that is there, and as large as this asks. */
static bool post_block_fits(const device_block_t *block, uint64_t bytes) {
    return block != nullptr && block->bytes >= bytes;
}

gpu_status_t post_frame(gpu_error_t *error, const post_settings_t *settings, const traverse_camera_t *camera,
                        const device_block_t *pixels, const device_block_t *xrgb) {
    return post_frame_temporal(error, settings, nullptr, false, false, camera, pixels, xrgb, nullptr, nullptr);
}

gpu_status_t post_frame_temporal(gpu_error_t *error, const post_settings_t *settings, const post_temporal_t *temporal,
                                 bool taa, bool shafts, const traverse_camera_t *camera, const device_block_t *pixels,
                                 const device_block_t *xrgb, const post_weather_t *weather,
                                 const device_block_t *weather_map) {
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
    if (settings->under_water != 0u && !(settings->water_fog_end > 0.0F)) {
        return post_refuse(error, "post: under water, with no distance for its fog");
    }
    if ((taa || shafts) && temporal == nullptr) {
        return post_refuse(error, "post: an effect that needs what goes from frame to frame, with none");
    }
    if (shafts && !post_block_fits(temporal->borrowed_shafts, post_shafts_bytes(camera->width, camera->height))) {
        return post_refuse(error, "post: god rays are on, and their target is missing or too small for the camera");
    }
    if (taa) {
        const uint64_t bytes = post_history_bytes(camera->width, camera->height);
        if (!post_block_fits(temporal->borrowed_history_last, bytes) ||
            !post_block_fits(temporal->borrowed_history_next, bytes)) {
            return post_refuse(error, "post: TAA is on, and its history is missing or too small for the camera");
        }
    }
    if (weather != nullptr && weather_map != nullptr && !post_block_fits(weather_map, post_weather_map_bytes())) {
        return post_refuse(error, "post: the rain top map is smaller than its side asks");
    }
    /* No weather is a weather of zeros, which the kernels never look at. */
    const post_weather_t none = {};
    const bool weather_on = weather != nullptr;
    const post_weather_t weather_given = weather_on ? *weather : none;
    const int16_t *const map = weather_on && weather_map != nullptr
                                   ? (const int16_t *)(const void *)device_memory_pointer(weather_map)
                                   : nullptr;
    const dim3 grid(traverse_tiles_across(camera->width), traverse_tiles_across(camera->height));
    const dim3 tile(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS);
    shade_pixel_t *const shaded = (shade_pixel_t *)(void *)device_memory_pointer(pixels);
    uint32_t *const picture = (uint32_t *)(void *)device_memory_pointer(xrgb);
    const post_shaft_t *const shaft_texels =
        shafts ? (const post_shaft_t *)(const void *)device_memory_pointer(temporal->borrowed_shafts) : nullptr;
    post_kernel<<<grid, tile>>>(*settings, *camera, shaded, picture, shaft_texels,
                                shafts ? temporal->shaft_colour[0] : 0.0F, shafts ? temporal->shaft_colour[1] : 0.0F,
                                shafts ? temporal->shaft_colour[2] : 0.0F, taa, weather_given, weather_on, map);
    gpu_status_t status = device_memory_check(error, cudaGetLastError(), "post: starting the kernel");
    if (status != GPU_STATUS_OK || !taa) {
        return status;
    }
    post_taa_kernel<<<grid, tile>>>(
        *camera, *temporal, shaded,
        (const post_taa_pixel_t *)(const void *)device_memory_pointer(temporal->borrowed_history_last),
        (post_taa_pixel_t *)(void *)device_memory_pointer(temporal->borrowed_history_next), picture, weather_given,
        weather_on, map);
    return device_memory_check(error, cudaGetLastError(), "post: starting the temporal pass");
}
