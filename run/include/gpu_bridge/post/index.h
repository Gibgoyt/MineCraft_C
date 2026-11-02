/* See run/src/gpu_bridge/post/README.md and cuda/src/post/README.md, which implements this. C API, compiled by GCC and by nvcc:
 * both sides check these static_asserts. The small functions below run on the card in the kernel and on the host in a test, as
 * the same lines. */
#ifndef MC_RUN_GPU_BRIDGE_POST_INDEX_H
#define MC_RUN_GPU_BRIDGE_POST_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"
#include "gpu_bridge/traverse/index.h"
#include "types/index.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __CUDACC__
#define POST_BOTH_SIDES __host__ __device__
#else
#define POST_BOTH_SIDES
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum : uint32_t { POST_BYTES_PER_PIXEL = 4u };

/* Where the tone map stops being a straight line, so that a texture under full light keeps the colours it was drawn with. */
#define POST_SHOULDER F32(0.8)

/* How near a direction must be to the sun or the moon to look at its disc, as a cosine: about 2.8 and 2 degrees either way. */
#define POST_SUN_DISC_COSINE F32(0.9988)
#define POST_MOON_DISC_COSINE F32(0.9994)
/* How tightly the sun's glow gathers around it. */
#define POST_GLOW_POWER F32(8.0)

/* The sky and the fog, in linear light, for this moment of the day: render_day_fill. Fog runs from nothing at fog_start to all sky
 * at fog_end, so the edge of what is loaded melts into the sky and does not show as a wall. */
typedef struct post_settings_t {
    float32_t sky_zenith[3];    /* 0 */
    float32_t sky_horizon[3];   /* 12 */
    float32_t fog_start;        /* 24: in blocks */
    float32_t fog_end;          /* 28: above fog_start */
    float32_t sun_direction[3]; /* 32: unit, towards the sun. The moon is always opposite it */
    float32_t
        sun_glow[3]; /* 44: added around the sun, and so to the fog: this is where a red evening's fog comes from */
    float32_t sun_disc[3];  /* 56 */
    float32_t moon_disc[3]; /* 68 */
} post_settings_t;
static_assert(sizeof(post_settings_t) == 80);
static_assert(alignof(post_settings_t) == 4);
static_assert(offsetof(post_settings_t, sky_zenith) == 0);
static_assert(offsetof(post_settings_t, sky_horizon) == 12);
static_assert(offsetof(post_settings_t, fog_start) == 24);
static_assert(offsetof(post_settings_t, fog_end) == 28);
static_assert(offsetof(post_settings_t, sun_direction) == 32);
static_assert(offsetof(post_settings_t, sun_glow) == 44);
static_assert(offsetof(post_settings_t, sun_disc) == 56);
static_assert(offsetof(post_settings_t, moon_disc) == 68);

static inline uint64_t post_xrgb_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * POST_BYTES_PER_PIXEL;
}

/* The sky one unit direction looks at, by how far up it looks, with the sun's glow around where it is. Below the horizon it stays
 * the horizon's colour: nothing should be seen there unless the world is not loaded yet. No disc: this is also the fog's colour,
 * and a sun does not show through a hill. */
POST_BOTH_SIDES static inline void post_sky(const post_settings_t *settings, const float32_t direction[3],
                                            float32_t out[3]) {
    const float32_t up = direction[2] > F32(0.0) ? direction[2] : F32(0.0);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        out[i] = settings->sky_horizon[i] + (settings->sky_zenith[i] - settings->sky_horizon[i]) * up;
    }
    const float32_t cosine = direction[0] * settings->sun_direction[0] + direction[1] * settings->sun_direction[1] +
                             direction[2] * settings->sun_direction[2];
    if (cosine > F32(0.0)) {
        const float32_t glow = powf(cosine, POST_GLOW_POWER);
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            out[i] += settings->sun_glow[i] * glow;
        }
    }
}

/* What a ray that hit nothing sees on top of post_sky: the sun or the moon, if it looks straight at one above the horizon. Below
 * it there is only the world, not yet loaded: the moon by day must not hang in that. */
POST_BOTH_SIDES static inline void post_sky_discs(const post_settings_t *settings, const float32_t direction[3],
                                                  float32_t in_out[3]) {
    if (direction[2] <= F32(0.0)) {
        return;
    }
    const float32_t cosine = direction[0] * settings->sun_direction[0] + direction[1] * settings->sun_direction[1] +
                             direction[2] * settings->sun_direction[2];
    if (cosine > POST_SUN_DISC_COSINE) {
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            in_out[i] += settings->sun_disc[i];
        }
    } else if (-cosine > POST_MOON_DISC_COSINE) {
        for (uint32_t i = 0u; i < 3u; i += 1u) {
            in_out[i] += settings->moon_disc[i];
        }
    }
}

/* How much of a hit at distance t is fog, from 0 to 1. */
POST_BOTH_SIDES static inline float32_t post_fog(const post_settings_t *settings, float32_t t) {
    if (t <= settings->fog_start) {
        return F32(0.0);
    }
    if (t >= settings->fog_end) {
        return F32(1.0);
    }
    const float32_t x = (t - settings->fog_start) / (settings->fog_end - settings->fog_start);
    return x * x;
}

/* Linear light, of any size above 0, to a byte: a straight line up to POST_SHOULDER, a smooth roll towards 1 past it, then the
 * sRGB curve. No dither: one script gives one picture, byte for byte. */
POST_BOTH_SIDES static inline uint32_t post_tone_map(float32_t linear) {
    float32_t mapped = linear;
    if (!(mapped > F32(0.0))) {
        mapped = F32(0.0);
    } else if (mapped > POST_SHOULDER) {
        const float32_t room = F32(1.0) - POST_SHOULDER;
        mapped = POST_SHOULDER + room * (F32(1.0) - expf((POST_SHOULDER - mapped) / room));
    }
    const float32_t encoded =
        mapped <= F32(0.0031308) ? mapped * F32(12.92) : F32(1.055) * powf(mapped, F32(1.0) / F32(2.4)) - F32(0.055);
    const uint32_t byte = (uint32_t)(encoded * F32(255.0) + F32(0.5));
    return byte > 255u ? 255u : byte;
}

POST_BOTH_SIDES static inline uint32_t post_xrgb(uint32_t red, uint32_t green, uint32_t blue) {
    return red << 16 | green << 8 | blue;
}

/* From shade's pixels to XRGB8888, row after row at the camera's size: sky where nothing was hit, fog, the sun and moon, the tone map. Starts a
 * kernel and returns; it runs in order with any other. Nothing here allocates. On failure error says why, and nothing was
 * started. */
gpu_status_t post_frame(gpu_error_t *error, const post_settings_t *settings, const traverse_camera_t *camera,
                        const device_block_t *pixels, const device_block_t *xrgb);

#ifdef __cplusplus
}
#endif

#endif
