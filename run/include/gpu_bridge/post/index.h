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
    /* The eyes are under water, for this frame. All below is only read then: see post_under_water. */
    float32_t water_tint[3];   /* 80: what each channel of the picture is multiplied by */
    float32_t water_colour[3]; /* 92: what the distance goes to, in linear light, by the time of day */
    float32_t water_fog_end;   /* 104: in blocks, where nothing but water_colour is left. Above 0 */
    uint32_t
        under_water; /* 108: 1 if the eyes are under water and the setting is on, and shade counts a ray's way from where the water stops */
} post_settings_t;
static_assert(sizeof(post_settings_t) == 112);
static_assert(alignof(post_settings_t) == 4);
static_assert(offsetof(post_settings_t, sky_zenith) == 0);
static_assert(offsetof(post_settings_t, sky_horizon) == 12);
static_assert(offsetof(post_settings_t, fog_start) == 24);
static_assert(offsetof(post_settings_t, fog_end) == 28);
static_assert(offsetof(post_settings_t, sun_direction) == 32);
static_assert(offsetof(post_settings_t, sun_glow) == 44);
static_assert(offsetof(post_settings_t, sun_disc) == 56);
static_assert(offsetof(post_settings_t, moon_disc) == 68);
static_assert(offsetof(post_settings_t, water_tint) == 80);
static_assert(offsetof(post_settings_t, water_colour) == 92);
static_assert(offsetof(post_settings_t, water_fog_end) == 104);
static_assert(offsetof(post_settings_t, under_water) == 108);

/* What the temporal pass keeps of a pixel from frame to frame: see post_taa_squeeze for what the numbers mean. */
typedef struct post_taa_pixel_t {
    float32_t rgb[3]; /* 0 */
} post_taa_pixel_t;
static_assert(sizeof(post_taa_pixel_t) == 12);
static_assert(alignof(post_taa_pixel_t) == 4);
static_assert(offsetof(post_taa_pixel_t, rgb) == 0);

/* One texel of the god rays, at half the picture's size each way. */
typedef struct post_shaft_t {
    float32_t amount; /* 0: how much of the light this ray gathered on its way, by the angle to the sun */
    float32_t t; /* 4: the distance of what the ray ended on, so that a far texel does not bleed onto a near pixel */
} post_shaft_t;
static_assert(sizeof(post_shaft_t) == 8);
static_assert(alignof(post_shaft_t) == 4);
static_assert(offsetof(post_shaft_t, amount) == 0);
static_assert(offsetof(post_shaft_t, t) == 4);

/* What goes from one frame to the next. render_frame_t holds one, swaps the _last and _next blocks after each frame, and the blocks
 * are its parent's. A block may be nullptr only while the effect that needs it is off. */
typedef struct post_temporal_t {
    const device_block_t *borrowed_ao_last;      /* 0: shade_ao_bytes: what the last frame left */
    const device_block_t *borrowed_ao_next;      /* 8: what this frame writes */
    const device_block_t *borrowed_history_last; /* 16: post_history_bytes */
    const device_block_t *borrowed_history_next; /* 24 */
    const device_block_t *borrowed_shafts;       /* 32: post_shafts_bytes */
    traverse_camera_t previous;                  /* 40: the last frame's, with no jitter on it */
    float32_t jitter[2];                         /* 108: in pixels, how far this frame looks off each pixel's centre */
    float32_t shaft_colour[3];                   /* 116: the light a god ray scatters, under an amount of 1 */
    uint32_t frame;                              /* 128: counts every frame drawn, so that nothing random repeats */
    uint8_t ao_valid;                            /* 132: the last frame wrote borrowed_ao_last */
    uint8_t history_valid;                       /* 133: the last frame wrote borrowed_history_last */
    uint8_t sun_apart;                           /* 134: see SHADE_FEATURE_SUN_APART */
    uint8_t _pad0[1];                            /* 135 */
} post_temporal_t;
static_assert(sizeof(post_temporal_t) == 136);
static_assert(alignof(post_temporal_t) == 8);
static_assert(offsetof(post_temporal_t, borrowed_ao_last) == 0);
static_assert(offsetof(post_temporal_t, borrowed_ao_next) == 8);
static_assert(offsetof(post_temporal_t, borrowed_history_last) == 16);
static_assert(offsetof(post_temporal_t, borrowed_history_next) == 24);
static_assert(offsetof(post_temporal_t, borrowed_shafts) == 32);
static_assert(offsetof(post_temporal_t, previous) == 40);
static_assert(offsetof(post_temporal_t, jitter) == 108);
static_assert(offsetof(post_temporal_t, shaft_colour) == 116);
static_assert(offsetof(post_temporal_t, frame) == 128);
static_assert(offsetof(post_temporal_t, ao_valid) == 132);
static_assert(offsetof(post_temporal_t, history_valid) == 133);
static_assert(offsetof(post_temporal_t, sun_apart) == 134);
static_assert(offsetof(post_temporal_t, _pad0) == 135);

enum : uint32_t {
    /* How many frames the jitter takes to come back to where it was. */
    POST_JITTER_FRAMES = 8u,
};

/* What a frame adds to what the temporal pass has of a pixel: a tenth, so that about 20 frames make up what is seen. */
#define POST_TAA_BLEND F32(0.1)
/* How many standard deviations of the pixels around it a pixel's history may be from their mean. */
#define POST_TAA_CLIP F32(1.25)
/* A place in the last frame this near a pixel's centre is that pixel, with no filter to blur it: a still camera. In pixels. */
#define POST_TAA_STILL F32(0.002)

static inline uint64_t post_xrgb_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * POST_BYTES_PER_PIXEL;
}

static inline uint64_t post_history_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * sizeof(post_taa_pixel_t);
}

/* God rays are worked out for every second pixel each way. */
POST_BOTH_SIDES static inline uint32_t post_shafts_across(uint32_t pixels) {
    return (pixels + 1u) / 2u;
}

static inline uint64_t post_shafts_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)post_shafts_across(width) * post_shafts_across(height) * sizeof(post_shaft_t);
}

/* Where inside its pixel a frame looks, from -0.5 to 0.5 each way: the first eight points of the Halton sequence in 2 and 3, which
 * cover a pixel about as evenly as eight points can. */
POST_BOTH_SIDES static inline void post_jitter(uint32_t frame, float32_t out[2]) {
    const uint32_t bases[2] = {2u, 3u};
    for (uint32_t axis = 0u; axis < 2u; axis += 1u) {
        uint32_t left = frame % POST_JITTER_FRAMES + 1u;
        float32_t place = F32(1.0), sum = F32(0.0);
        while (left > 0u) {
            place /= (float32_t)bases[axis];
            sum += place * (float32_t)(left % bases[axis]);
            left /= bases[axis];
        }
        out[axis] = sum - F32(0.5);
    }
}

/* Moves a camera's view by a part of a pixel, x to the right and y down the picture: each pixel's ray then goes where the ray of
 * (x + 0.5 + jitter[0], y + 0.5 + jitter[1]) of the camera it was goes. No kernel needs to know, as all of them read forward. */
static inline void post_camera_jitter(traverse_camera_t *camera, const float32_t jitter[2]) {
    const float32_t across = F32(2.0) * jitter[0] / (float32_t)camera->width;
    const float32_t down = F32(2.0) * jitter[1] / (float32_t)camera->height;
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        camera->forward[i] += across * camera->right[i] - down * camera->up[i];
    }
}

/* Where a thing at t along a unit direction from this eye was on the picture of an earlier camera with no jitter, in pixels from
 * the top left corner, so that the middle of pixel 0 is 0.5. False if it was behind that eye or off that picture. A thing at
 * TRAVERSE_COARSE_SKY is as far as the sky, which does not move as the eye does. */
POST_BOTH_SIDES static inline bool post_reproject(const traverse_camera_t *previous, const traverse_camera_t *camera,
                                                  const float32_t direction[3], float32_t t, float32_t *out_x,
                                                  float32_t *out_y) {
    float32_t place[3];
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        /* The whole cells first, as the difference of two large numbers is exact in an integer and is not in a float. */
        place[i] = t >= TRAVERSE_COARSE_SKY ? direction[i]
                                            : (float32_t)(camera->eye_cell[i] - previous->eye_cell[i]) +
                                                  (camera->eye_frac[i] - previous->eye_frac[i]) + direction[i] * t;
    }
    float32_t along = F32(0.0), across = F32(0.0), up = F32(0.0), across_squared = F32(0.0), up_squared = F32(0.0);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        along += place[i] * previous->forward[i];
        across += place[i] * previous->right[i];
        up += place[i] * previous->up[i];
        across_squared += previous->right[i] * previous->right[i];
        up_squared += previous->up[i] * previous->up[i];
    }
    if (!(along > F32(0.0))) {
        return false;
    }
    const float32_t u = across / (across_squared * along), v = up / (up_squared * along);
    const float32_t x = (u + F32(1.0)) * F32(0.5) * (float32_t)previous->width;
    const float32_t y = (F32(1.0) - v) * F32(0.5) * (float32_t)previous->height;
    *out_x = x;
    *out_y = y;
    return x >= F32(0.0) && y >= F32(0.0) && x < (float32_t)previous->width && y < (float32_t)previous->height;
}

/* Colours as the temporal pass mixes them: squeezed so that a sun's disc of 20 weighs no more against the sky beside it than any
 * bright thing does, or its edge would stay aliased however long the history. post_taa_unsqueeze undoes it. */
POST_BOTH_SIDES static inline void post_taa_squeeze(float32_t in_out[3]) {
    const float32_t most = fmaxf(fmaxf(in_out[0], in_out[1]), fmaxf(in_out[2], F32(0.0)));
    const float32_t scale = F32(1.0) / (F32(1.0) + most);
    in_out[0] *= scale;
    in_out[1] *= scale;
    in_out[2] *= scale;
}

POST_BOTH_SIDES static inline void post_taa_unsqueeze(float32_t in_out[3]) {
    const float32_t most = fmaxf(fmaxf(in_out[0], in_out[1]), fmaxf(in_out[2], F32(0.0)));
    const float32_t scale = F32(1.0) / (F32(1.0) - fminf(most, F32(0.999)));
    in_out[0] *= scale;
    in_out[1] *= scale;
    in_out[2] *= scale;
}

/* The four Catmull-Rom weights of a place f between the second sample and the third. They come to 1, and the outer two are below
 * 0, which is what keeps a moving picture sharp. */
POST_BOTH_SIDES static inline void post_catmull_rom(float32_t f, float32_t out[4]) {
    const float32_t f2 = f * f, f3 = f2 * f;
    out[0] = F32(0.5) * (F32(2.0) * f2 - f3 - f);
    out[1] = F32(0.5) * (F32(3.0) * f3 - F32(5.0) * f2 + F32(2.0));
    out[2] = F32(0.5) * (F32(4.0) * f2 - F32(3.0) * f3 + f);
    out[3] = F32(0.5) * (f3 - f2);
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

/* Where fog starts, as a share of where it is whole. */
#define POST_FOG_START_SHARE F32(0.7)

/* How far a way counts for the fog, of length t that goes up or down by height. Fog is there to hide the edge of what is loaded, and that edge is out to
 * the side: straight down or up from the eye the world goes on, whatever the distance. So what is up or down counts only as far as where fog starts, and
 * a way going up or down by less than that counts whole, to the byte. From high above, the ground below stays in sight, in clear air under the eye and
 * melting into the sky out towards the edge. */
POST_BOTH_SIDES static inline float32_t post_fog_way(float32_t fog_start, float32_t t, float32_t height) {
    const float32_t up_or_down = fabsf(height);
    if (up_or_down <= fog_start) {
        return t;
    }
    /* Rounding can make the side a touch below nothing, straight down. */
    const float32_t side_squared = fmaxf(t * t - up_or_down * up_or_down, F32(0.0));
    return sqrtf(side_squared + fog_start * fog_start);
}

/* How much of a hit at distance t, along a ray whose unit direction has this z, is fog, from 0 to 1. Sky is past any fog and has no way to speak of. */
POST_BOTH_SIDES static inline float32_t post_fog_along(const post_settings_t *settings, float32_t direction_z,
                                                       float32_t t) {
    if (t <= settings->fog_start || t >= TRAVERSE_COARSE_SKY) {
        return post_fog(settings, t);
    }
    return post_fog(settings, post_fog_way(settings->fog_start, t, direction_z * t));
}

/* How much of the picture is water even right at the eye: stored light in the sea runs out with depth, and the bed at arm's length would be black. */
#define POST_WATER_FOG_NEAR F32(0.12)
/* Where only water is left, in blocks. */
#define POST_WATER_FOG_END F32(28.0)
/* What the picture is multiplied by under water. */
#define POST_WATER_TINT_R F32(0.62)
#define POST_WATER_TINT_G F32(0.86)
#define POST_WATER_TINT_B F32(1.0)
/* What the distance goes to at noon, in linear light. */
#define POST_WATER_COLOUR_R F32(0.018)
#define POST_WATER_COLOUR_G F32(0.13)
#define POST_WATER_COLOUR_B F32(0.27)
/* The share of that colour at midnight: dark, and still not a void. */
#define POST_WATER_COLOUR_NIGHT F32(0.22)

/* How much of what is at distance t is water, seen from under water: from POST_WATER_FOG_NEAR at the eye, quickly at first, to all of it at water_fog_end. */
POST_BOTH_SIDES static inline float32_t post_water_fog(const post_settings_t *settings, float32_t t) {
    const float32_t x =
        t >= settings->water_fog_end ? F32(1.0) : (t > F32(0.0) ? t / settings->water_fog_end : F32(0.0));
    return POST_WATER_FOG_NEAR + (F32(1.0) - POST_WATER_FOG_NEAR) * x * (F32(2.0) - x);
}

/* The picture as eyes under water see it: tinted, then going to water with distance. Goes over the sky and its fog, as it is nearer than either. */
POST_BOTH_SIDES static inline void post_under_water(const post_settings_t *settings, float32_t t, float32_t in_out[3]) {
    const float32_t fog = post_water_fog(settings, t);
    for (uint32_t i = 0u; i < 3u; i += 1u) {
        const float32_t tinted = in_out[i] * settings->water_tint[i];
        in_out[i] = tinted + (settings->water_colour[i] - tinted) * fog;
    }
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

/* post_frame, with what goes from frame to frame: god rays from temporal's block on top of the fog, if shafts, and the temporal pass
 * if taa, which then writes the next history and leaves pixels holding its own form of what was seen. On failure error says why,
 * and nothing was started. */
gpu_status_t post_frame_temporal(gpu_error_t *error, const post_settings_t *settings, const post_temporal_t *temporal,
                                 bool taa, bool shafts, const traverse_camera_t *camera, const device_block_t *pixels,
                                 const device_block_t *xrgb);

#ifdef __cplusplus
}
#endif

#endif
