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

/* One channel of a history pixel: its three colours, and with a fourth its weight (plan 009 phase 07). */
static __device__ __forceinline__ float32_t post_taa_channel(const post_taa_pixel_t *pixel, uint32_t channel) {
    return channel < 3u ? pixel->rgb[channel] : pixel->weight;
}

/* The history at a place, in pixels from the top left corner, between the four texels around it by how near each is. Off the edge it takes
 * the edge's pixel. channels is 3 for the colours, or 4 with the weight. */
static __device__ __forceinline__ void post_history_bilinear(const post_taa_pixel_t *history, uint32_t width,
                                                             uint32_t height, float32_t x, float32_t y,
                                                             uint32_t channels, float32_t out[4]) {
    const float32_t centre_x = x - 0.5F, centre_y = y - 0.5F;
    const float32_t floor_x = floorf(centre_x), floor_y = floorf(centre_y);
    const float32_t fx = centre_x - floor_x, fy = centre_y - floor_y;
    const int32_t x0 = traverse_clamp((int32_t)floor_x, 0, (int32_t)width - 1);
    const int32_t x1 = traverse_clamp((int32_t)floor_x + 1, 0, (int32_t)width - 1);
    const int32_t y0 = traverse_clamp((int32_t)floor_y, 0, (int32_t)height - 1);
    const int32_t y1 = traverse_clamp((int32_t)floor_y + 1, 0, (int32_t)height - 1);
    const post_taa_pixel_t *const top = &history[(uint64_t)y0 * width];
    const post_taa_pixel_t *const bottom = &history[(uint64_t)y1 * width];
    for (uint32_t i = 0u; i < channels; i += 1u) {
        const float32_t upper =
            post_taa_channel(&top[x0], i) + (post_taa_channel(&top[x1], i) - post_taa_channel(&top[x0], i)) * fx;
        const float32_t lower = post_taa_channel(&bottom[x0], i) +
                                (post_taa_channel(&bottom[x1], i) - post_taa_channel(&bottom[x0], i)) * fx;
        out[i] = upper + (lower - upper) * fy;
    }
}

/* Catmull-Rom from five bilinear reads, its four corner texels left out (Jimenez, "Filmic SMAA", 2016): the middle two texels each way as one
 * read, at the place between them their weights make, and the four arms. */
static __device__ __forceinline__ void post_history_five_tap(const post_taa_pixel_t *history, uint32_t width,
                                                             uint32_t height, float32_t x, float32_t y,
                                                             uint32_t channels, float32_t out[4]) {
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
    for (uint32_t i = 0u; i < channels; i += 1u) {
        out[i] = 0.0F;
    }
    for (uint32_t k = 0u; k < 5u; k += 1u) {
        float32_t one[4];
        post_history_bilinear(history, width, height, places[k][0], places[k][1], channels, one);
        for (uint32_t i = 0u; i < channels; i += 1u) {
            out[i] += one[i] * weights[k];
        }
        sum += weights[k];
    }
    for (uint32_t i = 0u; i < channels; i += 1u) {
        out[i] /= sum;
    }
}

/* What the last frame's picture had at a place, in pixels from the top left corner: that pixel if the place is at its middle, as it
 * is for a camera that has not moved, and if not then Catmull-Rom, as anything softer would blur the picture a little more on every
 * frame. On a fast turn (plan 008 phase 04), POST_TAA_FAST_READ: motion hides the softness. Off the edge it takes the edge's pixel.
 * channels is 3 for the colours, or 4 with the weight. */
static __device__ __forceinline__ void post_history_at(const post_taa_pixel_t *history, uint32_t width, uint32_t height,
                                                       float32_t x, float32_t y, bool fast, uint32_t channels,
                                                       float32_t out[4]) {
    const float32_t centre_x = x - 0.5F, centre_y = y - 0.5F;
    const float32_t floor_x = floorf(centre_x), floor_y = floorf(centre_y);
    const float32_t fx = centre_x - floor_x, fy = centre_y - floor_y;
    if (fminf(fx, 1.0F - fx) <= POST_TAA_STILL && fminf(fy, 1.0F - fy) <= POST_TAA_STILL) {
        const post_taa_pixel_t *const one =
            &history[(uint64_t)traverse_clamp((int32_t)floorf(y), 0, (int32_t)height - 1) * width +
                     (uint32_t)traverse_clamp((int32_t)floorf(x), 0, (int32_t)width - 1)];
        for (uint32_t i = 0u; i < channels; i += 1u) {
            out[i] = post_taa_channel(one, i);
        }
        return;
    }
    if (fast && POST_TAA_FAST_READ == POST_TAA_READ_BILINEAR) {
        post_history_bilinear(history, width, height, x, y, channels, out);
        return;
    }
    if (fast && POST_TAA_FAST_READ == POST_TAA_READ_FIVE_TAP) {
        post_history_five_tap(history, width, height, x, y, channels, out);
        return;
    }
    float32_t across[4], down[4];
    post_catmull_rom(fx, across);
    post_catmull_rom(fy, down);
    for (uint32_t i = 0u; i < channels; i += 1u) {
        out[i] = 0.0F;
    }
    for (int32_t j = 0; j < 4; j += 1) {
        const int32_t row = traverse_clamp((int32_t)floor_y + j - 1, 0, (int32_t)height - 1);
        for (int32_t i = 0; i < 4; i += 1) {
            const int32_t column = traverse_clamp((int32_t)floor_x + i - 1, 0, (int32_t)width - 1);
            const post_taa_pixel_t *const one = &history[(uint64_t)row * width + (uint32_t)column];
            const float32_t weight = across[i] * down[j];
            for (uint32_t c = 0u; c < channels; c += 1u) {
                out[c] += post_taa_channel(one, c) * weight;
            }
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
        /* With TAA on the history is always at the output's size, which this camera is (plan 009 phase 07: a picture drawn smaller is
         * gathered there), and the last camera differs from it only in its size when the last frame was drawn smaller. */
        traverse_camera_t last_camera = temporal.previous;
        last_camera.width = camera.width;
        last_camera.height = camera.height;
        float32_t x = 0.0F, y = 0.0F;
        if (post_reproject(&last_camera, &camera, steady, pixel.t, &x, &y)) {
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
            float32_t last[4];
            post_history_at(history_last, camera.width, camera.height, x, y,
                            (temporal.flags & POST_TEMPORAL_FAST) != 0u, 3u, last);
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                const float32_t average = mean[i] / 9.0F;
                const float32_t deviation = sqrtf(fmaxf(squares[i] / 9.0F - average * average, 0.0F));
                const float32_t low = average - POST_TAA_CLIP * deviation, high = average + POST_TAA_CLIP * deviation;
                const float32_t kept = fminf(fmaxf(last[i], low), high);
                result[i] = kept + (pixel.rgb[i] - kept) * POST_TAA_BLEND;
            }
        }
    }
    history_next[index] = post_taa_pixel_t{{result[0], result[1], result[2]}, POST_TAA_WEIGHT_WHOLE};
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

/* Plan 009 phase 07: the temporal pass at the output's size for a picture drawn smaller, one thread per output pixel: see README.md, "The
 * temporal pass". camera is the drawn one, with its jitter; output is the same view at the output's size, with none. */
__global__ void post_taa_upscale_kernel(const traverse_camera_t camera, const traverse_camera_t output,
                                        const post_temporal_t temporal, const shade_pixel_t *pixels,
                                        const post_taa_pixel_t *history_last, post_taa_pixel_t *history_next,
                                        uint32_t *xrgb, const post_weather_t weather, bool weather_on,
                                        const int16_t *weather_map) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= output.width || pixel_y >= output.height) {
        return;
    }
    float32_t jitter[2];
    post_temporal_jitter(&temporal, jitter);
    /* This output pixel's middle on the drawn picture, and the drawn sample whose jittered middle is nearest it: sample i looks at
     * i + 0.5 + jitter. */
    const float32_t place_x = post_upscale_place(pixel_x, output.width, camera.width);
    const float32_t place_y = post_upscale_place(pixel_y, output.height, camera.height);
    const int32_t near_x = (int32_t)floorf(place_x - jitter[0]), near_y = (int32_t)floorf(place_y - jitter[1]);
    const float32_t across_x = (float32_t)output.width / (float32_t)camera.width;
    const float32_t across_y = (float32_t)output.height / (float32_t)camera.height;
    /* The 3 by 3 drawn samples around it: what this frame gives the pixel, by post_upscale_weight, and the neighbourhood its history is held
     * to, as at the output's size. */
    float32_t gathered[3] = {0.0F, 0.0F, 0.0F}, mean[3] = {0.0F, 0.0F, 0.0F}, squares[3] = {0.0F, 0.0F, 0.0F};
    float32_t weight = 0.0F;
    for (int32_t dy = -1; dy <= 1; dy += 1) {
        const int32_t row = traverse_clamp(near_y + dy, 0, (int32_t)camera.height - 1);
        for (int32_t dx = -1; dx <= 1; dx += 1) {
            const int32_t column = traverse_clamp(near_x + dx, 0, (int32_t)camera.width - 1);
            const shade_pixel_t *const sample = &pixels[(uint64_t)row * camera.width + (uint32_t)column];
            const float32_t w = post_upscale_weight((float32_t)column + 0.5F + jitter[0] - place_x,
                                                    (float32_t)row + 0.5F + jitter[1] - place_y, across_x, across_y);
            weight += w;
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                gathered[i] += sample->rgb[i] * w;
                mean[i] += sample->rgb[i];
                squares[i] += sample->rgb[i] * sample->rgb[i];
            }
        }
    }
    const shade_pixel_t nearest =
        pixels[(uint64_t)traverse_clamp(near_y, 0, (int32_t)camera.height - 1) * camera.width +
               (uint32_t)traverse_clamp(near_x, 0, (int32_t)camera.width - 1)];
    float32_t result[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        result[i] = weight > 0.0F ? gathered[i] / weight : nearest.rgb[i];
    }
    float32_t held = weight;
    float32_t steady[3];
    traverse_camera_direction(&output, (float32_t)pixel_x + 0.5F, (float32_t)pixel_y + 0.5F, steady);
    if (temporal.history_valid != 0u) {
        /* The history is at the output's size, and the last camera differs from output only in where it was and where it looked. */
        traverse_camera_t last_camera = temporal.previous;
        last_camera.width = output.width;
        last_camera.height = output.height;
        float32_t x = 0.0F, y = 0.0F;
        if (post_reproject(&last_camera, &output, steady, nearest.t, &x, &y)) {
            float32_t last[4];
            post_history_at(history_last, output.width, output.height, x, y,
                            (temporal.flags & POST_TEMPORAL_FAST) != 0u, 4u, last);
            const float32_t cap = post_upscale_cap(camera.width, camera.height, output.width, output.height,
                                                   (temporal.flags & POST_TEMPORAL_MOVED) != 0u);
            float32_t share = 1.0F;
            held = post_upscale_mix(last[3], weight, cap, &share);
            for (uint32_t i = 0u; i < 3u; i += 1u) {
                const float32_t average = mean[i] / 9.0F;
                const float32_t deviation = sqrtf(fmaxf(squares[i] / 9.0F - average * average, 0.0F));
                const float32_t low = average - POST_TAA_CLIP * deviation, high = average + POST_TAA_CLIP * deviation;
                const float32_t kept = fminf(fmaxf(last[i], low), high);
                result[i] = kept + (result[i] - kept) * share;
            }
        }
    }
    const uint64_t index = (uint64_t)pixel_y * output.width + pixel_x;
    history_next[index] = post_taa_pixel_t{{result[0], result[1], result[2]}, held};
    post_taa_unsqueeze(result);
    /* After the history, along this output pixel's ray, as at the output's size. */
    if (weather_on) {
        post_weather_apply(&weather, &output, weather_map, steady, nearest.t, result);
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

/* post_frame_temporal, and with output (not nullptr, and taa) post_frame_upscaled: the history and the target are then the output's size. */
static gpu_status_t post_frame_run(gpu_error_t *error, const post_settings_t *settings, const post_temporal_t *temporal,
                                   bool taa, bool shafts, const traverse_camera_t *camera,
                                   const traverse_camera_t *output, const device_block_t *pixels,
                                   const device_block_t *xrgb, const post_weather_t *weather,
                                   const device_block_t *weather_map) {
    const traverse_camera_t *const shown = output != nullptr ? output : camera;
    if (camera->width == 0u || camera->height == 0u) {
        return post_refuse(error, "post: the camera has no pixels");
    }
    if (pixels->bytes < (uint64_t)camera->width * camera->height * sizeof(shade_pixel_t)) {
        return post_refuse(error, "post: the pixels are too small for the camera");
    }
    if (xrgb->bytes < post_xrgb_bytes(shown->width, shown->height)) {
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
        const uint64_t bytes = post_history_bytes(shown->width, shown->height);
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
    const post_taa_pixel_t *const history_last =
        (const post_taa_pixel_t *)(const void *)device_memory_pointer(temporal->borrowed_history_last);
    post_taa_pixel_t *const history_next =
        (post_taa_pixel_t *)(void *)device_memory_pointer(temporal->borrowed_history_next);
    if (output != nullptr) {
        const dim3 out_grid(traverse_tiles_across(output->width), traverse_tiles_across(output->height));
        post_taa_upscale_kernel<<<out_grid, tile>>>(*camera, *output, *temporal, shaded, history_last, history_next,
                                                    picture, weather_given, weather_on, map);
        return device_memory_check(error, cudaGetLastError(), "post: starting the temporal upscale");
    }
    post_taa_kernel<<<grid, tile>>>(*camera, *temporal, shaded, history_last, history_next, picture, weather_given,
                                    weather_on, map);
    return device_memory_check(error, cudaGetLastError(), "post: starting the temporal pass");
}

gpu_status_t post_frame_temporal(gpu_error_t *error, const post_settings_t *settings, const post_temporal_t *temporal,
                                 bool taa, bool shafts, const traverse_camera_t *camera, const device_block_t *pixels,
                                 const device_block_t *xrgb, const post_weather_t *weather,
                                 const device_block_t *weather_map) {
    return post_frame_run(error, settings, temporal, taa, shafts, camera, nullptr, pixels, xrgb, weather, weather_map);
}

gpu_status_t post_frame_upscaled(gpu_error_t *error, const post_settings_t *settings, const post_temporal_t *temporal,
                                 bool shafts, const traverse_camera_t *camera, const traverse_camera_t *output,
                                 const device_block_t *pixels, const device_block_t *xrgb,
                                 const post_weather_t *weather, const device_block_t *weather_map) {
    if (output->width == 0u || output->height == 0u || camera->width > output->width ||
        camera->height > output->height) {
        return post_refuse(error, "post: an upscale to a smaller picture, or to none");
    }
    return post_frame_run(error, settings, temporal, true, shafts, camera, output, pixels, xrgb, weather, weather_map);
}

/* ---- Plan 008 phase 05: a picture drawn smaller, brought up to the output -------------------------------------------------------- */

/* One output pixel: its middle on the inner picture, and the four inner pixels around it by how near each is, channel by channel in bytes.
 * The weights are in 1/256ths, so a flat colour comes back as itself to the bit and the sum is integer arithmetic, the same on every run. */
__global__ void post_upscale_kernel(const uint32_t *inner, uint32_t inner_width, uint32_t inner_height, uint32_t *out,
                                    uint32_t out_width, uint32_t out_height) {
    const uint32_t pixel_x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t pixel_y = blockIdx.y * blockDim.y + threadIdx.y;
    if (pixel_x >= out_width || pixel_y >= out_height) {
        return;
    }
    /* Where the middle of this pixel is on the inner picture, in 256ths of its pixels, less half of one so that 0 is pixel 0's middle. */
    const int64_t place_x = (((int64_t)pixel_x * 2 + 1) * inner_width * 128) / out_width - 128;
    const int64_t place_y = (((int64_t)pixel_y * 2 + 1) * inner_height * 128) / out_height - 128;
    const int64_t floor_x = place_x >= 0 ? place_x / 256 : -1, floor_y = place_y >= 0 ? place_y / 256 : -1;
    const uint32_t fx = (uint32_t)(place_x - floor_x * 256), fy = (uint32_t)(place_y - floor_y * 256);
    const uint32_t x0 = (uint32_t)traverse_clamp((int32_t)floor_x, 0, (int32_t)inner_width - 1);
    const uint32_t x1 = (uint32_t)traverse_clamp((int32_t)floor_x + 1, 0, (int32_t)inner_width - 1);
    const uint32_t y0 = (uint32_t)traverse_clamp((int32_t)floor_y, 0, (int32_t)inner_height - 1);
    const uint32_t y1 = (uint32_t)traverse_clamp((int32_t)floor_y + 1, 0, (int32_t)inner_height - 1);
    const uint32_t taps[4] = {inner[(uint64_t)y0 * inner_width + x0], inner[(uint64_t)y0 * inner_width + x1],
                              inner[(uint64_t)y1 * inner_width + x0], inner[(uint64_t)y1 * inner_width + x1]};
    const uint32_t weights[4] = {(256u - fx) * (256u - fy), fx * (256u - fy), (256u - fx) * fy, fx * fy};
    uint32_t result = 0u;
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        uint32_t sum = 0u;
        for (uint32_t i = 0u; i < 4u; i += 1u) {
            sum += ((taps[i] >> shift) & 0xFFu) * weights[i];
        }
        result |= ((sum + 32768u) >> 16) << shift;
    }
    out[(uint64_t)pixel_y * out_width + pixel_x] = result | (taps[0] & 0xFF000000u);
}

gpu_status_t post_upscale(gpu_error_t *error, const device_block_t *inner, uint32_t inner_width, uint32_t inner_height,
                          const device_block_t *out, uint32_t out_width, uint32_t out_height) {
    if (inner_width == 0u || inner_height == 0u || out_width == 0u || out_height == 0u) {
        return post_refuse(error, "post: an upscale with no pixels");
    }
    if (inner_width > out_width || inner_height > out_height) {
        return post_refuse(error, "post: an upscale to a smaller picture");
    }
    if (!post_block_fits(inner, post_xrgb_bytes(inner_width, inner_height)) ||
        !post_block_fits(out, post_xrgb_bytes(out_width, out_height))) {
        return post_refuse(error, "post: an upscale's pictures are missing or too small");
    }
    const dim3 grid(traverse_tiles_across(out_width), traverse_tiles_across(out_height));
    const dim3 tile(TRAVERSE_TILE_PIXELS, TRAVERSE_TILE_PIXELS);
    post_upscale_kernel<<<grid, tile>>>((const uint32_t *)(const void *)device_memory_pointer(inner), inner_width,
                                        inner_height, (uint32_t *)(void *)device_memory_pointer(out), out_width,
                                        out_height);
    return device_memory_check(error, cudaGetLastError(), "post: starting the upscale");
}
