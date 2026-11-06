/* ctest render_feature_sky. See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "climate/sun/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "log/index.h"
#include "main/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    SK_WIDTH = 320u,
    SK_HEIGHT = 180u,
    SK_GOLDEN_TOLERANCE = 2u,
    SK_PROGRAM_TIMEOUT_MS = 120000u,
    SK_BOXES_MAX = 340u,
    SK_COST_FRAMES = 30u,
    SK_COST_WIDTH = 1920u,
    SK_COST_HEIGHT = 1080u,
    SK_COST_RENDER_DISTANCE = 10u,
    /* Always on here: the test's blocks have shapes and holes, and tests/cuda/shapes/ holds those. */
    SK_FEATURES_BASE = SHADE_FEATURE_SHAPES | SHADE_FEATURE_ALPHA,
    /* What draws with nothing of this stage's on. */
    SK_FEATURES_NONE = SK_FEATURES_BASE,
    /* All of what this stage's reference knows: a ray here stops at what is part-covered. Going on past it is tests/cuda/water/'s. */
    SK_FEATURES_ALL = SHADE_FEATURES_ALL & ~(uint32_t)(SHADE_FEATURE_TRANSPARENCY | SHADE_FEATURE_WATER_REFLECTION |
                                                       SHADE_FEATURE_WATER_TINT),
};

#define SK_NEAR_NOTHING 1e-5f32
#define SK_MORNING 0.31f32
#define SK_NOON 0.5f32
#define SK_EVENING 0.71f32
#define SK_DUSK 0.748f32
#define SK_NIGHT 0.03f32

#include "cuda/sky/fixtures/rig.h"

/* A world with nothing in it, drawn as this test's reference knows how to draw. */
static bool sk_open(test_context_t *t, sk_rig_t *rig, uint32_t radius, uint32_t width, uint32_t height,
                    float32_t fog_end) {
    if (!sk_hand_world_open(t, rig, radius, width, height, fog_end)) {
        return false;
    }
    render_frame_features_set(&rig->render->frame, SK_FEATURES_ALL);
    return true;
}

/* ---- The day, and the light, on numbers worked out by hand ---------------------------------------------------------------- */

TEST(the_sun_rises_in_the_east_stands_high_at_noon_and_sets_in_the_west) {
    post_settings_t post;
    shade_sun_t sun;
    memset(&post, 0, sizeof(post));
    render_day_fill(0.25f32, &post, &sun);
    CHECK_NEAR(post.sun_direction[0], 1.0f32, SK_NEAR_NOTHING);
    CHECK_NEAR(post.sun_direction[2], 0.0f32, SK_NEAR_NOTHING);
    render_day_fill(0.5f32, &post, &sun);
    /* Noon: straight up but for the tilt, 0.35 radians, towards the south. */
    CHECK_NEAR(post.sun_direction[0], 0.0f32, SK_NEAR_NOTHING);
    CHECK_NEAR(post.sun_direction[1], -0.34290f32, 1e-4f32);
    CHECK_NEAR(post.sun_direction[2], 0.93937f32, 1e-4f32);
    CHECK_NEAR(sun.direction[2], 0.93937f32, 1e-4f32);
    render_day_fill(0.75f32, &post, &sun);
    CHECK_NEAR(post.sun_direction[0], -1.0f32, SK_NEAR_NOTHING);
    CHECK_NEAR(post.sun_direction[2], 0.0f32, SK_NEAR_NOTHING);
    /* Midnight: the sun is under the world, and what lights it is the moon, opposite. */
    render_day_fill(0.0f32, &post, &sun);
    CHECK(post.sun_direction[2] < -0.9f32);
    CHECK(sun.direction[2] > 0.9f32);
    CHECK_NEAR(sun.direction[2], -post.sun_direction[2], SK_NEAR_NOTHING);
}

TEST(a_day_goes_round_with_no_jump_and_wraps) {
    post_settings_t post, last;
    shade_sun_t sun, last_sun;
    render_day_fill(0.0f32, &last, &last_sun);
    float32_t widest = 0.0f32;
    uint32_t steps_up = 0u;
    /* What a surface sees is direction times direct: it must not jump as the light swaps from the sun to the moon. */
    for (uint32_t i = 1u; i <= 4000u; i++) {
        render_day_fill((float32_t)i / 4000.0f32, &post, &sun);
        float32_t length = 0.0f32;
        for (uint32_t k = 0u; k < 3u; k++) {
            length += sun.direction[k] * sun.direction[k];
            const float32_t gaps[] = {
                post.sky_zenith[k] - last.sky_zenith[k],
                post.sky_horizon[k] - last.sky_horizon[k],
                post.sun_glow[k] - last.sun_glow[k],
                sun.ambient[k] - last_sun.ambient[k],
                sun.direct[k] * sun.direction[2] - last_sun.direct[k] * last_sun.direction[2],
            };
            for (uint32_t g = 0u; g < sizeof(gaps) / sizeof(gaps[0]); g++) {
                widest = fabsf(gaps[g]) > widest ? fabsf(gaps[g]) : widest;
            }
        }
        CHECK_NEAR(length, 1.0f32, 1e-4f32);
        CHECK(sun.direction[2] >= -1e-4f32);
        steps_up += post.sun_direction[2] > 0.0f32 ? 1u : 0u;
        last = post;
        last_sun = sun;
    }
    printf("  the widest step of any colour in a 4000th of a day: %.4f\n", (libc_double_t)widest);
    CHECK(widest < 0.02f32);
    CHECK(steps_up > 1950u && steps_up < 2050u);
    /* 1.25 is 0.25. */
    post_settings_t again;
    shade_sun_t again_sun;
    render_day_fill(0.25f32, &post, &sun);
    render_day_fill(1.25f32, &again, &again_sun);
    CHECK_NEAR(again.sun_direction[0], post.sun_direction[0], SK_NEAR_NOTHING);
    render_day_fill(-0.75f32, &again, &again_sun);
    CHECK_NEAR(again.sun_direction[0], post.sun_direction[0], SK_NEAR_NOTHING);
    /* Noon is what a texture was painted for: its top comes out as itself. Night is well under dusk, and dusk under noon. */
    render_day_fill(0.5f32, &post, &sun);
    float32_t light[3];
    shade_surface_light(&sun, TRAVERSE_FACE_POS_Z, SHADE_LIGHT_OPEN_SKY, 1.0f32, light);
    for (uint32_t k = 0u; k < 3u; k++) {
        CHECK_NEAR(light[k], 1.0f32, 0.02f32);
    }
    float32_t noon_ambient = sun.ambient[0];
    render_day_fill(0.75f32, &post, &sun);
    float32_t dusk_ambient = sun.ambient[0];
    /* And right at the horizon nothing direct falls, from either. */
    CHECK_NEAR(sun.direct[0], 0.0f32, 1e-3f32);
    render_day_fill(0.0f32, &post, &sun);
    CHECK(sun.ambient[0] * 4.0f32 < dusk_ambient);
    CHECK(dusk_ambient < noon_ambient);
}

TEST(stored_light_and_the_sun_add_up_as_on_paper) {
    /* Each step down is 0.8 of the one above. */
    CHECK_NEAR(shade_sky_level(15u), 1.0f32, 0.0f32);
    CHECK_NEAR(shade_sky_level(14u), 0.8f32, 1e-5f32);
    CHECK_NEAR(shade_sky_level(13u), 0.64f32, 1e-5f32);
    CHECK_NEAR(shade_sky_level(0u), 0.035184f32, 1e-5f32);
    CHECK_NEAR(shade_block_level(15u), 1.0f32, 0.0f32);
    CHECK_NEAR(shade_block_level(14u), 0.8f32, 1e-5f32);
    CHECK_NEAR(shade_block_level(0u), 0.0f32, 0.0f32);

    const float32_t up[3] = {0.0f32, 0.0f32, 1.0f32};
    CHECK_NEAR(shade_face_towards(TRAVERSE_FACE_POS_Z, up), 1.0f32, 0.0f32);
    CHECK_NEAR(shade_face_towards(TRAVERSE_FACE_NEG_Z, up), 0.0f32, 0.0f32);
    CHECK_NEAR(shade_face_towards(TRAVERSE_FACE_POS_X, up), 0.0f32, 0.0f32);
    CHECK_NEAR(shade_face_towards(TRAVERSE_FACE_INSIDE, up), 0.0f32, 0.0f32);
    const float32_t slanted[3] = {-0.6f32, 0.0f32, 0.8f32};
    CHECK_NEAR(shade_face_towards(TRAVERSE_FACE_NEG_X, slanted), 0.6f32, 1e-6f32);
    CHECK_NEAR(shade_face_towards(TRAVERSE_FACE_POS_X, slanted), 0.0f32, 0.0f32);
    CHECK_NEAR(shade_face_towards(TRAVERSE_FACE_POS_Z, slanted), 0.8f32, 1e-6f32);

    const shade_sun_t sun = {.direction = {-0.6f32, 0.0f32, 0.8f32},
                             .direct = {0.5f32, 0.25f32, 0.125f32},
                             .ambient = {0.25f32, 0.5f32, 1.0f32},
                             .block = {1.0f32, 0.5f32, 0.25f32}};
    float32_t light[3];
    /* A top under open sky, in the sun: 1 x ambient + 0.8 x direct. */
    shade_surface_light(&sun, TRAVERSE_FACE_POS_Z, 0xF0u, 1.0f32, light);
    CHECK_NEAR(light[0], 0.25f32 + 0.4f32, 1e-6f32);
    CHECK_NEAR(light[1], 0.5f32 + 0.2f32, 1e-6f32);
    CHECK_NEAR(light[2], 1.0f32 + 0.1f32, 1e-6f32);
    /* The same in shadow: only the sky. */
    shade_surface_light(&sun, TRAVERSE_FACE_POS_Z, 0xF0u, 0.0f32, light);
    CHECK_NEAR(light[0], 0.25f32, 1e-6f32);
    /* A -x side, with sky 14 and block 13, half in the sun: 0.6 x (0.8 ambient + 0.64 block) + 0.6 x direct x 0.5. */
    shade_surface_light(&sun, TRAVERSE_FACE_NEG_X, 0xEDu, 0.5f32, light);
    CHECK_NEAR(light[0], 0.6f32 * (0.8f32 * 0.25f32 + 0.64f32) + 0.6f32 * 0.5f32 * 0.5f32, 1e-5f32);
    CHECK_NEAR(light[2], 0.6f32 * (0.8f32 * 1.0f32 + 0.64f32 * 0.25f32) + 0.6f32 * 0.125f32 * 0.5f32, 1e-5f32);
    /* No sky light where it is stored: no sun, whatever a ray would say. */
    shade_surface_light(&sun, TRAVERSE_FACE_POS_Z, 0x00u, 1.0f32, light);
    CHECK_NEAR(light[0], 0.035184f32 * 0.25f32, 1e-5f32);
    /* The setting's off gives a plain face's brightness, to the bit: the old pictures rest on this. */
    const shade_sun_t plain = {.direction = {0.0f32, 0.0f32, 1.0f32}, .ambient = {1.0f32, 1.0f32, 1.0f32}};
    for (uint32_t face = 0u; face < SHADE_FACES; face++) {
        shade_surface_light(&plain, face, SHADE_LIGHT_OPEN_SKY, 1.0f32, light);
        CHECK_NEAR(light[0], shade_face_brightness(face), 0.0f32);
    }
}

TEST(the_sky_has_its_glow_and_discs_where_the_sun_is) {
    post_settings_t post;
    shade_sun_t sun;
    memset(&post, 0, sizeof(post));
    render_day_fill(0.25f32, &post, &sun);
    /* At sunrise, looking at the horizon: towards the sun is the zenith's mix with the most glow, away from it none at all. */
    const float32_t east[3] = {1.0f32, 0.0f32, 0.0f32}, west[3] = {-1.0f32, 0.0f32, 0.0f32};
    float32_t towards[3], away[3];
    post_sky(&post, east, towards);
    post_sky(&post, west, away);
    CHECK_NEAR(away[0], post.sky_horizon[0], 0.0f32);
    CHECK_NEAR(towards[0], post.sky_horizon[0] + post.sun_glow[0], 1e-3f32);
    CHECK(towards[0] > away[0] + 0.3f32);
    /* The fog's colour has no disc in it: only what hit nothing is given one. */
    render_day_fill(0.3f32, &post, &sun);
    float32_t sun_way[3], moon_way[3];
    for (uint32_t k = 0u; k < 3u; k++) {
        sun_way[k] = post.sun_direction[k];
        moon_way[k] = -post.sun_direction[k];
    }
    float32_t seen[3] = {0.25f32, 0.25f32, 0.25f32};
    post_sky_discs(&post, sun_way, seen);
    CHECK_NEAR(seen[0], 0.25f32 + post.sun_disc[0], 1e-3f32);
    /* By day the moon is under the horizon, and hangs in nothing there. */
    float32_t no_moon[3] = {0.25f32, 0.25f32, 0.25f32};
    post_sky_discs(&post, moon_way, no_moon);
    CHECK_NEAR(no_moon[0], 0.25f32, 0.0f32);
    /* By night it is up, and the sun is what is under the horizon. */
    render_day_fill(0.1f32, &post, &sun);
    for (uint32_t k = 0u; k < 3u; k++) {
        sun_way[k] = post.sun_direction[k];
        moon_way[k] = -post.sun_direction[k];
    }
    float32_t moon_seen[3] = {0.25f32, 0.25f32, 0.25f32};
    post_sky_discs(&post, moon_way, moon_seen);
    CHECK_NEAR(moon_seen[0], 0.25f32 + post.moon_disc[0], 1e-3f32);
    float32_t no_sun[3] = {0.25f32, 0.25f32, 0.25f32};
    post_sky_discs(&post, sun_way, no_sun);
    CHECK_NEAR(no_sun[0], 0.25f32, 0.0f32);
    const float32_t up[3] = {0.0f32, 0.0f32, 1.0f32};
    float32_t neither[3] = {0.5f32, 0.5f32, 0.5f32};
    post_sky_discs(&post, up, neither);
    CHECK_NEAR(neither[0], 0.5f32, 0.0f32);
}

/* ---- An exact reference, written apart from the kernels ----------------------------------------------------------------- */

typedef struct sk_box_t {
    float64_t low[3];  /* 0: world coordinates */
    float64_t high[3]; /* 24 */
    int32_t cell[3];   /* 48 */
    uint32_t state;    /* 60 */
    uint32_t light;    /* 64: what is stored in its voxel */
    uint8_t _pad0[4];  /* 68 */
} sk_box_t;
static_assert(sizeof(sk_box_t) == 72);

typedef struct sk_scene_t {
    sk_box_t boxes[SK_BOXES_MAX]; /* 0 */
    uint32_t box_count;           /* 24480 */
    uint8_t _pad0[4];             /* 24484 */
} sk_scene_t;
static_assert(sizeof(sk_scene_t) == 24488);

typedef struct sk_block_t {
    int32_t cell[3]; /* 0 */
    uint32_t state;  /* 12 */
    uint32_t light;  /* 16 */
} sk_block_t;
static_assert(sizeof(sk_block_t) == 20);

/* Adds a block to the picture's list and to the world, with what it is made of written apart from the card's table. */
static bool sk_block_put(test_context_t *t, sk_rig_t *rig, sk_scene_t *scene, const sk_block_t *block) {
    world_edit_t *const edit = &rig->world->edit;
    if (!CHECK_EQ_U64(
            world_edit_block_set(edit, block->cell[0], block->cell[1], block->cell[2], (uint16_t)block->state),
            WORLD_EDIT_STATUS_CHANGED)) {
        return false;
    }
    if (block->light != SHADE_LIGHT_OPEN_SKY &&
        !CHECK_EQ_U64(world_edit_light_set(edit, block->cell[0], block->cell[1], block->cell[2], (uint8_t)block->light),
                      WORLD_EDIT_STATUS_CHANGED)) {
        return false;
    }
    const float64_t none = 0.0f64, half = 0.5f64, whole = 1.0f64;
    const float64_t thin_low = 15.0f64 / 32.0f64, thin_high = 17.0f64 / 32.0f64;
    float64_t lows[2][3] = {{none, none, none}, {none, none, none}};
    float64_t highs[2][3] = {{whole, whole, whole}, {whole, whole, whole}};
    uint32_t count = 1u;
    switch ((block_shape_t)block_defs[block->state].shape) {
    case BLOCK_SHAPE_SLAB:
        highs[0][2] = half;
        break;
    case BLOCK_SHAPE_STAIRS:
        highs[0][2] = half;
        lows[1][1] = half;
        lows[1][2] = half;
        count = 2u;
        break;
    case BLOCK_SHAPE_CROSS:
        lows[0][1] = thin_low;
        highs[0][1] = thin_high;
        lows[1][0] = thin_low;
        highs[1][0] = thin_high;
        count = 2u;
        break;
    default:
        break;
    }
    for (uint32_t b = 0u; b < count; b++) {
        if (!CHECK(scene->box_count < SK_BOXES_MAX)) {
            return false;
        }
        sk_box_t *const out = &scene->boxes[scene->box_count];
        scene->box_count++;
        memset(out, 0, sizeof(*out));
        out->state = block->state;
        out->light = block->light;
        for (uint32_t k = 0u; k < 3u; k++) {
            out->cell[k] = block->cell[k];
            out->low[k] = (float64_t)block->cell[k] + lows[b][k];
            out->high[k] = (float64_t)block->cell[k] + highs[b][k];
        }
    }
    return true;
}

/* A texture's coordinates, written apart from the kernel's. */
static void sk_reference_uv(uint32_t face, const float64_t local[3], float64_t *out_u, float64_t *out_v) {
    switch (face) {
    case 0u:
        *out_u = 1.0f64 - local[1];
        *out_v = 1.0f64 - local[2];
        break;
    case 1u:
        *out_u = local[1];
        *out_v = 1.0f64 - local[2];
        break;
    case 2u:
        *out_u = local[0];
        *out_v = 1.0f64 - local[2];
        break;
    case 3u:
        *out_u = 1.0f64 - local[0];
        *out_v = 1.0f64 - local[2];
        break;
    case 4u:
        *out_u = local[0];
        *out_v = local[1];
        break;
    default:
        *out_u = local[0];
        *out_v = 1.0f64 - local[1];
        break;
    }
}

static bool sk_near_whole(float64_t value, float64_t margin) {
    return fabs(value - round(value)) < margin;
}

/* The nearest box entered after a distance, by the slab test. */
static bool sk_first_box(const sk_scene_t *scene, const float64_t eye[3], const float64_t direction[3], float64_t after,
                         uint32_t *out_box, uint32_t *out_face, float64_t *out_t) {
    float64_t best = 1e30f64;
    bool found = false;
    for (uint32_t b = 0u; b < scene->box_count; b++) {
        const sk_box_t *const box = &scene->boxes[b];
        float64_t t_in = -1e30f64, t_out = 1e30f64;
        uint32_t face = 0u;
        bool possible = true;
        for (uint32_t i = 0u; i < 3u && possible; i++) {
            const float64_t low = box->low[i] - eye[i], high = box->high[i] - eye[i];
            if (fabs(direction[i]) < 1e-12f64) {
                possible = low <= 0.0f64 && high >= 0.0f64;
                continue;
            }
            const float64_t a = low / direction[i], c = high / direction[i];
            const float64_t entered = a < c ? a : c, left = a < c ? c : a;
            if (entered > t_in) {
                t_in = entered;
                face = i * 2u + (direction[i] > 0.0f64 ? 0u : 1u);
            }
            t_out = left < t_out ? left : t_out;
        }
        if (possible && t_in < t_out && t_in > after && t_in < best) {
            best = t_in;
            *out_box = b;
            *out_face = face;
            found = true;
        }
    }
    *out_t = best;
    return found;
}

/* What one ray must show. */
typedef struct sk_seen_t {
    uint64_t
        signature; /* 0: every surface on its way and on its way to the sun, to tell one ray's story from a neighbour's */
    float32_t sun_seen; /* 8 */
    uint32_t expected;  /* 12: the pixel */
    uint32_t face;      /* 16 */
    uint32_t stored;    /* 20 */
    uint8_t hit;        /* 24 */
    uint8_t usable;     /* 25: nothing on its way was near enough an edge for rounding to choose */
    uint8_t asked_sun;  /* 26: a sun ray went out */
    uint8_t sun_met;    /* 27: and it went through something on its way */
    uint8_t _pad0[4];   /* 28 */
} sk_seen_t;
static_assert(sizeof(sk_seen_t) == 32);

/* The texel a box shows at a point: which mip comes from distance_seen, as a kernel does for a surface and for what shades it. */
static const uint8_t *sk_texel(test_context_t *t, const sk_rig_t *rig, const traverse_camera_t *view,
                               const sk_box_t *box, uint32_t face, const float64_t point[3], float64_t distance_seen,
                               const texture_blob_entry_t **out_entry, sk_seen_t *out) {
    float64_t local[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        local[i] = point[i] - (float64_t)box->cell[i];
    }
    float64_t u = 0.0f64, v = 0.0f64;
    sk_reference_uv(face, local, &u, &v);
    uint32_t id = 0u;
    texture_info_t info;
    if (!CHECK(texture_find(rig->texture, block_texture_names[block_defs[box->state].faces[face]], &id)) ||
        !CHECK(texture_info_get(rig->texture, id, &info))) {
        return nullptr;
    }
    const texture_blob_entry_t *const entry = info.borrowed_entry;
    const float32_t tangent = shade_pixel_tangent(view);
    const uint32_t level = shade_mip_level((float32_t)distance_seen, tangent, entry->width, entry->level_count);
    texture_mip_t mip;
    if (!CHECK(texture_mip_get(rig->texture, id, level, 0u, &mip))) {
        return nullptr;
    }
    const float32_t per_pixel = (float32_t)distance_seen * tangent * (float32_t)entry->width;
    if (sk_near_whole(u * (float64_t)mip.width, 0.02f64) || sk_near_whole(v * (float64_t)mip.height, 0.02f64) ||
        (per_pixel > 1.0f32 && sk_near_whole((float64_t)log2f(per_pixel) + 0.5f64, 0.005f64))) {
        out->usable = 0u;
    }
    *out_entry = entry;
    const uint32_t x = shade_texel_of((float32_t)u, mip.width), y = shade_texel_of((float32_t)v, mip.height);
    return mip.borrowed_pixels + ((uint64_t)y * mip.width + x) * 4u;
}

/* The one sun ray, from a point of a surface, through what is see-through. */
static float32_t sk_sun_seen(test_context_t *t, const sk_rig_t *rig, const sk_scene_t *scene,
                             const traverse_camera_t *view, const shade_sun_t *sun, uint32_t features,
                             const float64_t point[3], uint32_t face, float64_t distance_seen, sk_seen_t *out) {
    float64_t origin[3], direction[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        origin[i] = point[i];
        direction[i] = (float64_t)sun->direction[i];
    }
    const float64_t bias = (float64_t)shade_sun_bias((float32_t)distance_seen);
    origin[face >> 1] += (face & 1u) != 0u ? bias : -bias;
    float32_t seen = 1.0f32;
    float64_t after = 0.0f64;
    for (uint32_t layer = 0u; layer < SHADE_SUN_LAYERS; layer++) {
        uint32_t box = 0u, hit_face = 0u;
        float64_t along = 0.0f64;
        if (!sk_first_box(scene, origin, direction, after, &box, &hit_face, &along)) {
            return seen;
        }
        out->signature = out->signature * 1000003u + box * 8u + hit_face + 1u;
        out->sun_met = 1u;
        if ((features & SHADE_FEATURE_ALPHA) == 0u) {
            return 0.0f32;
        }
        float64_t on_it[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            on_it[i] = origin[i] + direction[i] * along;
        }
        const texture_blob_entry_t *entry = nullptr;
        const uint8_t *const texel =
            sk_texel(t, rig, view, &scene->boxes[box], hit_face, on_it, distance_seen, &entry, out);
        if (texel == nullptr || texel[3] == SHADE_ALPHA_OPAQUE) {
            return 0.0f32;
        }
        seen *= 1.0f32 - (float32_t)texel[3] / 255.0f32;
        after = along;
    }
    return 0.0f32;
}

/* Goes through the boxes in float64_t from surface to surface, then asks for the sun. The sum of the light is shade_surface_light,
 * as the card's is: a test above holds that to numbers on paper. */
static void sk_trace(test_context_t *t, const sk_rig_t *rig, const sk_scene_t *scene, const traverse_camera_t *view,
                     const float64_t eye[3], const float64_t direction[3], sk_seen_t *out) {
    const render_frame_t *const frame = &rig->render->frame;
    const post_settings_t *const post = &frame->post;
    const shade_sun_t *const sun = &frame->scene.sun;
    const uint32_t features = frame->scene.features;
    memset(out, 0, sizeof(*out));
    out->usable = 1u;
    const float32_t unit[3] = {(float32_t)direction[0], (float32_t)direction[1], (float32_t)direction[2]};
    float32_t sky[3];
    post_sky(post, unit, sky);
    float64_t after = 0.0f64;
    for (uint32_t layer = 0u; layer < SHADE_ALPHA_LAYERS; layer++) {
        uint32_t index = 0u, face = 0u;
        float64_t distance = 0.0f64;
        if (!sk_first_box(scene, eye, direction, after, &index, &face, &distance)) {
            post_sky_discs(post, unit, sky);
            out->expected = post_xrgb(post_tone_map(sky[0]), post_tone_map(sky[1]), post_tone_map(sky[2]));
            return;
        }
        const sk_box_t *const box = &scene->boxes[index];
        out->signature = out->signature * 1000003u + index * 8u + face + 1u;
        float64_t point[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            point[i] = eye[i] + direction[i] * distance;
        }
        const texture_blob_entry_t *entry = nullptr;
        const uint8_t *const texel = sk_texel(t, rig, view, box, face, point, distance, &entry, out);
        REQUIRE(texel != nullptr);
        if (texel[3] == 0u && (features & SHADE_FEATURE_ALPHA) != 0u && layer + 1u < SHADE_ALPHA_LAYERS) {
            after = distance;
            continue;
        }
        out->hit = 1u;
        out->face = face;
        const uint32_t stored = (features & SHADE_FEATURE_LIGHT) != 0u ? box->light : (uint32_t)SHADE_LIGHT_OPEN_SKY;
        out->stored = stored;
        out->sun_seen = 1.0f32;
        if ((features & SHADE_FEATURE_SHADOW) != 0u && shade_sun_towards(sun, face, stored) > 0.0f32 &&
            sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0f32) {
            out->asked_sun = 1u;
            out->sun_seen = sk_sun_seen(t, rig, scene, view, sun, features, point, face, distance, out);
        }
        float32_t light[3];
        shade_surface_light(sun, face, stored, out->sun_seen, light);
        const float32_t cover = (float32_t)texel[3] / 255.0f32;
        const float32_t fog = post_fog_along(post, unit[2], (float32_t)distance);
        uint32_t channel[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            const float32_t shown = shade_srgb_to_linear((float32_t)texel[i] / 255.0f32);
            const float32_t behind = shade_srgb_to_linear((float32_t)entry->mean_rgba[i] / 255.0f32);
            const float32_t colour = (texel[3] == 255u ? shown : behind + (shown - behind) * cover) * light[i];
            channel[i] = post_tone_map(colour + (sky[i] - colour) * fog);
        }
        out->expected = post_xrgb(channel[0], channel[1], channel[2]);
        return;
    }
}

typedef struct sk_tally_t {
    uint64_t compared;              /* 0 */
    uint64_t hits;                  /* 8 */
    uint64_t wrong;                 /* 16 */
    uint64_t sun_rays;              /* 24 */
    uint64_t in_the_sun;            /* 32 */
    uint64_t in_shadow;             /* 40 */
    uint64_t part_shadow;           /* 48: through something part-covered */
    uint64_t shadow_holes;          /* 56: met something on the way, and reached the sun all the same, by its holes */
    uint64_t light_levels[16 * 16]; /* 64: what was seen, by stored byte */
} sk_tally_t;
static_assert(sizeof(sk_tally_t) == 64u + 2048u);

static void sk_compare_picture(test_context_t *t, const sk_rig_t *rig, const sk_scene_t *scene,
                               const render_camera_t *camera, const uint8_t *picture, sk_tally_t *out) {
    traverse_camera_t view;
    render_camera_to_traverse(camera, SK_WIDTH, SK_HEIGHT, &view);
    for (uint32_t py = 0u; py < SK_HEIGHT; py++) {
        for (uint32_t px = 0u; px < SK_WIDTH; px++) {
            const float64_t a = 2.0f64 * ((float64_t)px + 0.5f64) / (float64_t)SK_WIDTH - 1.0f64;
            const float64_t b = 1.0f64 - 2.0f64 * ((float64_t)py + 0.5f64) / (float64_t)SK_HEIGHT;
            float64_t direction[3];
            float64_t length = 0.0f64;
            for (uint32_t i = 0u; i < 3u; i++) {
                direction[i] = (float64_t)view.forward[i] + a * (float64_t)view.right[i] + b * (float64_t)view.up[i];
                length += direction[i] * direction[i];
            }
            length = sqrt(length);
            for (uint32_t i = 0u; i < 3u; i++) {
                direction[i] /= length;
            }
            sk_seen_t seen;
            sk_trace(t, rig, scene, &view, camera->position, direction, &seen);
            bool usable = seen.usable != 0u;
            /* A silhouette, or the edge of a shadow: this ray or one a hair from it may go either way. */
            for (uint32_t i = 0u; i < 2u && usable; i++) {
                float64_t nudged[3];
                for (uint32_t k = 0u; k < 3u; k++) {
                    const float64_t sideways = (i == 0u ? 1.0f64 : -1.0f64) * 0.01f64 / (float64_t)SK_HEIGHT;
                    nudged[k] = direction[k] + sideways * ((float64_t)view.right[k] + (float64_t)view.up[k]);
                }
                sk_seen_t other;
                sk_trace(t, rig, scene, &view, camera->position, nudged, &other);
                usable = other.signature == seen.signature;
            }
            if (!usable) {
                continue;
            }
            out->compared++;
            if (seen.hit != 0u) {
                out->hits++;
                out->light_levels[seen.stored & 255u]++;
            }
            if (seen.asked_sun != 0u) {
                out->sun_rays++;
                out->in_the_sun += seen.sun_seen >= 1.0f32 ? 1u : 0u;
                out->in_shadow += seen.sun_seen <= 0.0f32 ? 1u : 0u;
                out->part_shadow += seen.sun_seen > 0.0f32 && seen.sun_seen < 1.0f32 ? 1u : 0u;
                out->shadow_holes += seen.sun_seen >= 1.0f32 && seen.sun_met != 0u ? 1u : 0u;
            }
            if (!sk_bytes_near(sk_pixel(picture, SK_WIDTH, px, py), seen.expected, 1u)) {
                if (out->wrong < 8u) {
                    printf("  pixel %" PRIu32 ", %" PRIu32 ": card %06" PRIX32 ", reference %06" PRIX32
                           ", face %" PRIu32 ", sun seen %.3f, stored %02" PRIX32 "\n",
                           px, py, sk_pixel(picture, SK_WIDTH, px, py), seen.expected, seen.face,
                           (libc_double_t)seen.sun_seen, seen.stored);
                }
                out->wrong++;
            }
        }
    }
}

static void sk_report(const char *what, const sk_tally_t *tally) {
    printf("  %s: %" PRIu64 " pixels compared, %" PRIu64 " hits, %" PRIu64 " sun rays, %" PRIu64 " in the sun, %" PRIu64
           " in shadow, %" PRIu64 " part, %" PRIu64 " wrong\n",
           what, tally->compared, tally->hits, tally->sun_rays, tally->in_the_sun, tally->in_shadow, tally->part_shadow,
           tally->wrong);
}

/* ---- The scene ---------------------------------------------------------------------------------------------------------- */

/* A floor, a tower, an arch with a roof, a slab and stairs, a plant, a tree's crown on a trunk, panes of glass, and water hanging in
 * the air, which is the one thing with texels neither hole nor opaque, to shade the floor by only as much as it covers. */
static const sk_block_t sk_scene_blocks[] = {
    {{8, 8, 0}, BLOCK_ID_STONE, 0xF0u},        {{8, 8, 1}, BLOCK_ID_STONE, 0xF0u},
    {{8, 8, 2}, BLOCK_ID_STONE, 0xF0u},        {{8, 8, 3}, BLOCK_ID_STONE, 0xF0u},
    {{3, 3, 0}, BLOCK_ID_COBBLESTONE, 0xF0u},  {{3, 3, 1}, BLOCK_ID_COBBLESTONE, 0xF0u},
    {{3, 3, 2}, BLOCK_ID_OAK_PLANKS, 0xF0u},   {{4, 3, 2}, BLOCK_ID_OAK_PLANKS, 0xF0u},
    {{5, 3, 2}, BLOCK_ID_OAK_PLANKS, 0xF0u},   {{3, 4, 2}, BLOCK_ID_OAK_PLANKS, 0xF0u},
    {{4, 4, 2}, BLOCK_ID_OAK_PLANKS, 0xF0u},   {{5, 4, 2}, BLOCK_ID_OAK_PLANKS, 0xF0u},
    {{11, 3, 0}, BLOCK_ID_STONE_SLAB, 0xF0u},  {{12, 6, 0}, BLOCK_ID_OAK_STAIRS, 0xF0u},
    {{6, 11, 0}, BLOCK_ID_TALL_GRASS, 0xF0u},  {{10, 11, 0}, BLOCK_ID_OAK_LOG, 0xF0u},
    {{10, 11, 1}, BLOCK_ID_OAK_LOG, 0xF0u},    {{10, 11, 2}, BLOCK_ID_OAK_LEAVES, 0xF0u},
    {{9, 11, 2}, BLOCK_ID_OAK_LEAVES, 0xF0u},  {{11, 11, 2}, BLOCK_ID_OAK_LEAVES, 0xF0u},
    {{10, 10, 2}, BLOCK_ID_OAK_LEAVES, 0xF0u}, {{10, 12, 2}, BLOCK_ID_OAK_LEAVES, 0xF0u},
    {{10, 11, 3}, BLOCK_ID_OAK_LEAVES, 0xF0u}, {{13, 13, 1}, BLOCK_ID_GLASS, 0xF0u},
    {{13, 12, 1}, BLOCK_ID_GLASS, 0xF0u},      {{13, 13, 0}, BLOCK_ID_STONE, 0xF0u},
    {{2, 12, 2}, BLOCK_ID_WATER, 0xF0u},       {{2, 13, 2}, BLOCK_ID_WATER, 0xF0u},
};
enum : uint32_t { SK_SCENE_BLOCKS = sizeof(sk_scene_blocks) / sizeof(sk_scene_blocks[0]) };

/* 16 x 16 stone blocks at z = -1, as blocks and not one box: a cell's light is its own. */
static bool sk_floor_put(test_context_t *t, sk_rig_t *rig, sk_scene_t *scene, bool varied_light) {
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            const uint32_t light = varied_light ? (uint32_t)x << 4 | (uint32_t)y : (uint32_t)SHADE_LIGHT_OPEN_SKY;
            const sk_block_t block = {{x, y, -1}, BLOCK_ID_STONE, light};
            if (!sk_block_put(t, rig, scene, &block)) {
                return false;
            }
        }
    }
    return true;
}

static bool sk_scene_put(test_context_t *t, sk_rig_t *rig, sk_scene_t *scene, bool varied_light) {
    memset(scene, 0, sizeof(*scene));
    if (!sk_floor_put(t, rig, scene, varied_light)) {
        return false;
    }
    for (uint32_t b = 0u; b < SK_SCENE_BLOCKS; b++) {
        if (!sk_block_put(t, rig, scene, &sk_scene_blocks[b])) {
            return false;
        }
    }
    return sk_hand_world_sync(t, rig);
}

static void sk_camera_around(render_camera_t *camera, float32_t yaw, float32_t pitch, float64_t reach) {
    render_camera_start(camera, 8.0f64 - reach * (float64_t)cosf(pitch) * (float64_t)cosf(yaw),
                        8.0f64 - reach * (float64_t)cosf(pitch) * (float64_t)sinf(yaw),
                        0.5f64 - reach * (float64_t)sinf(pitch), yaw, pitch);
}

/* Pictures from around the scene, at a time of day, each against the trace. */
static void sk_compare_around(test_context_t *t, sk_rig_t *rig, const sk_scene_t *scene, float32_t day, uint32_t turns,
                              sk_tally_t *tally) {
    render_frame_time_set(&rig->render->frame, day);
    const float64_t reaches[] = {9.0f64, 22.0f64};
    for (uint32_t d = 0u; d < 2u; d++) {
        for (uint32_t turn = 0u; turn < turns; turn++) {
            const float32_t yaw = (float32_t)turn * 6.2831853f32 / (float32_t)turns + 0.11f32;
            render_camera_t camera;
            sk_camera_around(&camera, yaw, -0.55f32, reaches[d]);
            const uint8_t *const picture = sk_draw(t, rig, &camera);
            REQUIRE(picture != nullptr);
            sk_compare_picture(t, rig, scene, &camera, picture, tally);
        }
    }
}

/* ---- On the card ------------------------------------------------------------------------------------------------------- */

TEST(everything_starts_on_and_noon_is_the_first_time) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 1u, SK_WIDTH, SK_HEIGHT, 1000.0f32));
    const render_frame_t *const frame = &rig.render->frame;
    CHECK_EQ_U64(frame->scene.features, SHADE_FEATURES_ALL);
    CHECK_NEAR(frame->day_fraction, 0.5f32, 0.0f32);
    CHECK_NEAR(frame->post.fog_end, 1000.0f32, 0.0f32);
    CHECK_NEAR(frame->post.fog_start, 700.0f32, 1e-2f32);
    /* What a setting does to what the kernels read. */
    render_frame_features_set(&rig.render->frame, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_FOG);
    CHECK(frame->post.fog_start > 1e20f32);
    render_frame_features_set(&rig.render->frame, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SKY);
    CHECK_NEAR(frame->scene.sun.ambient[0], 1.0f32, 0.0f32);
    CHECK_NEAR(frame->scene.sun.direct[0], 0.0f32, 0.0f32);
    CHECK_NEAR(frame->post.fog_end, 1000.0f32, 0.0f32);
    render_frame_features_set(&rig.render->frame, 0xFFFFFFFFu);
    CHECK_EQ_U64(frame->scene.features, SHADE_FEATURES_EVERY);
    render_frame_time_set(&rig.render->frame, 1.25f32);
    CHECK_NEAR(frame->day_fraction, 0.25f32, 0.0f32);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* Nothing but sky, all over it and all through a day: each pixel against the arithmetic, on a ray worked out apart. */
TEST(the_sky_all_round_and_all_day_is_what_the_arithmetic_says) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_open(t, &rig, 1u, SK_WIDTH, SK_HEIGHT, 1000.0f32));
    REQUIRE(sk_hand_world_sync(t, &rig));
    sk_scene_t *const scene = test_alloc(t, sizeof(sk_scene_t));
    memset(scene, 0, sizeof(*scene));
    sk_tally_t *const tally = test_alloc(t, sizeof(sk_tally_t));
    memset(tally, 0, sizeof(*tally));
    const float32_t days[] = {SK_NIGHT, 0.23f32, 0.25f32, SK_MORNING, SK_NOON, SK_EVENING, SK_DUSK, 0.77f32};
    for (uint32_t d = 0u; d < sizeof(days) / sizeof(days[0]); d++) {
        render_frame_time_set(&rig.render->frame, days[d]);
        for (uint32_t turn = 0u; turn < 4u; turn++) {
            render_camera_t camera;
            render_camera_start(&camera, 0.5f64, 0.5f64, 200.5f64, (float32_t)turn * 1.5708f32 + 0.3f32,
                                turn % 2u == 0u ? 0.5f32 : -0.3f32);
            const uint8_t *const picture = sk_draw(t, &rig, &camera);
            REQUIRE(picture != nullptr);
            sk_compare_picture(t, &rig, scene, &camera, picture, tally);
        }
    }
    sk_report("sky", tally);
    CHECK_EQ_U64(tally->wrong, 0u);
    CHECK_EQ_U64(tally->compared, (uint64_t)SK_WIDTH * SK_HEIGHT * 8u * 4u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* A camera looking straight at a direction, with no roll to speak of. */
static void sk_camera_at(render_camera_t *camera, const float32_t direction[3]) {
    const float32_t yaw = atan2f(direction[1], direction[0]);
    const float32_t pitch = asinf(direction[2]);
    render_camera_start(camera, 0.5f64, 0.5f64, 0.5f64, yaw, pitch);
}

/* Looked at straight: white-hot by day, pale at night, and what is beside it is plain sky. A wall past the fog hides it. */
TEST(the_sun_and_moon_are_where_they_should_be_and_fog_hides_them) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_open(t, &rig, 4u, SK_WIDTH, SK_HEIGHT, 40.0f32));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    const uint32_t middle_x = SK_WIDTH / 2u, middle_y = SK_HEIGHT / 2u;
    render_frame_time_set(frame, SK_MORNING);
    float32_t sun_direction[3];
    memcpy(sun_direction, frame->post.sun_direction, sizeof(sun_direction));
    render_camera_t camera;
    sk_camera_at(&camera, sun_direction);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint32_t sun_seen = sk_pixel(picture, SK_WIDTH, middle_x, middle_y);
    printf("  the sun straight on: %06" PRIX32 ", beside it: %06" PRIX32 "\n", sun_seen,
           sk_pixel(picture, SK_WIDTH, 10u, 10u));
    CHECK(sun_seen == 0xFFFFFFu);
    CHECK(sk_pixel(picture, SK_WIDTH, 10u, 10u) != 0xFFFFFFu);

    /* The moon, opposite. */
    render_frame_time_set(frame, SK_NIGHT);
    float32_t moon_direction[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        moon_direction[i] = -frame->post.sun_direction[i];
    }
    sk_camera_at(&camera, moon_direction);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint32_t moon_seen = sk_pixel(picture, SK_WIDTH, middle_x, middle_y);
    const uint32_t night_beside = sk_pixel(picture, SK_WIDTH, 10u, 10u);
    printf("  the moon straight on: %06" PRIX32 ", beside it: %06" PRIX32 "\n", moon_seen, night_beside);
    CHECK(((moon_seen >> 16) & 255u) > 200u);
    CHECK(((night_beside >> 16) & 255u) < 60u);

    /* A wall in front of the sun, past where the fog is whole. */
    render_frame_time_set(frame, SK_MORNING);
    sk_camera_at(&camera, sun_direction);
    int32_t centre[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        centre[i] = (int32_t)floorf(0.5f32 + sun_direction[i] * 50.0f32);
    }
    for (int32_t a = -3; a <= 3; a++) {
        for (int32_t b = -3; b <= 3; b++) {
            REQUIRE(world_edit_block_set(&rig.world->edit, centre[0], centre[1] + a, centre[2] + b, BLOCK_ID_STONE) ==
                    WORLD_EDIT_STATUS_CHANGED);
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    float32_t sky[3];
    post_sky(&frame->post, sun_direction, sky);
    const uint32_t foggy = post_xrgb(post_tone_map(sky[0]), post_tone_map(sky[1]), post_tone_map(sky[2]));
    const uint32_t through = sk_pixel(picture, SK_WIDTH, middle_x, middle_y);
    printf("  in front of the sun, past the fog: %06" PRIX32 ", the sky with no disc: %06" PRIX32 "\n", through, foggy);
    CHECK(sk_bytes_near(through, foggy, 2u));
    /* And with fog off, the wall itself, and it is plainly not the sky. */
    render_frame_features_set(frame, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_FOG);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    CHECK(!sk_bytes_near(sk_pixel(picture, SK_WIDTH, middle_x, middle_y), foggy, 8u));
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(fog_is_exact_at_every_distance_and_off_is_off) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    /* From nothing at 21 blocks to all sky at 30. */
    REQUIRE(sk_open(t, &rig, 4u, SK_WIDTH, SK_HEIGHT, 30.0f32));
    sk_scene_t *const scene = test_alloc(t, sizeof(sk_scene_t));
    memset(scene, 0, sizeof(*scene));
    REQUIRE(sk_floor_put(t, &rig, scene, false));
    /* Pillars out along the way the eye looks, some in clear air, some in fog and some past it. */
    for (int32_t i = 0; i < 12; i++) {
        const sk_block_t pillar = {{16 + i * 2, 6 + i % 5, 0}, BLOCK_ID_STONE, SHADE_LIGHT_OPEN_SKY};
        const sk_block_t top = {{16 + i * 2, 6 + i % 5, 1}, BLOCK_ID_OAK_PLANKS, SHADE_LIGHT_OPEN_SKY};
        REQUIRE(sk_block_put(t, &rig, scene, &pillar));
        REQUIRE(sk_block_put(t, &rig, scene, &top));
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t camera;
    render_camera_start(&camera, 2.5f64, 8.5f64, 3.5f64, 0.0f32, -0.05f32);
    const float32_t days[] = {SK_NOON, SK_DUSK, SK_NIGHT};
    uint64_t foggy_hits = 0u;
    for (uint32_t d = 0u; d < 3u; d++) {
        sk_tally_t *const tally = test_alloc(t, sizeof(sk_tally_t));
        memset(tally, 0, sizeof(*tally));
        render_frame_features_set(&rig.render->frame, SK_FEATURES_ALL);
        render_frame_time_set(&rig.render->frame, days[d]);
        const uint8_t *const picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        sk_compare_picture(t, &rig, scene, &camera, picture, tally);
        sk_report("fog on", tally);
        CHECK_EQ_U64(tally->wrong, 0u);
        foggy_hits += tally->hits;
        sk_tally_t *const off = test_alloc(t, sizeof(sk_tally_t));
        memset(off, 0, sizeof(*off));
        render_frame_features_set(&rig.render->frame, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_FOG);
        const uint8_t *const clear = sk_draw(t, &rig, &camera);
        REQUIRE(clear != nullptr);
        sk_compare_picture(t, &rig, scene, &camera, clear, off);
        sk_report("fog off", off);
        CHECK_EQ_U64(off->wrong, 0u);
    }
    CHECK(foggy_hits > 10000u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(every_pixel_and_every_shadow_matches_an_exact_trace_all_day) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_open(t, &rig, 2u, SK_WIDTH, SK_HEIGHT, 100000.0f32));
    sk_scene_t *const scene = test_alloc(t, sizeof(sk_scene_t));
    REQUIRE(sk_scene_put(t, &rig, scene, false));
    const float32_t days[] = {SK_MORNING, SK_NOON, SK_EVENING, SK_DUSK, SK_NIGHT};
    const char *const names[] = {"morning", "noon", "evening", "dusk", "night"};
    sk_tally_t *const all = test_alloc(t, sizeof(sk_tally_t));
    memset(all, 0, sizeof(*all));
    for (uint32_t d = 0u; d < 5u; d++) {
        sk_tally_t *const tally = test_alloc(t, sizeof(sk_tally_t));
        memset(tally, 0, sizeof(*tally));
        sk_compare_around(t, &rig, scene, days[d], 4u, tally);
        sk_report(names[d], tally);
        CHECK_EQ_U64(tally->wrong, 0u);
        CHECK(tally->compared > (uint64_t)SK_WIDTH * SK_HEIGHT * 3u);
        CHECK(tally->sun_rays > 1000u);
        CHECK(tally->in_shadow > 100u);
        CHECK(tally->in_the_sun > 1000u);
        all->part_shadow += tally->part_shadow;
        all->shadow_holes += tally->shadow_holes;
    }
    /* Leaves let the sun through their holes and water lets some through all over, or the see-through path was not tested. */
    printf("  through something part-covered: %" PRIu64 " pixels, through a hole: %" PRIu64 "\n", all->part_shadow,
           all->shadow_holes);
    CHECK(all->shadow_holes > 100u);
    CHECK(all->part_shadow > 100u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* By hand, seen from straight above: where a tower's shadow must fall at noon, and that the sun's being there is all that differs. */
TEST(a_tower_shades_the_ground_where_the_sun_says_and_nowhere_else) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_open(t, &rig, 2u, 64u, 64u, 100000.0f32));
    world_edit_t *const edit = &rig.world->edit;
    for (int32_t y = -16; y < 32; y++) {
        for (int32_t x = -16; x < 32; x++) {
            REQUIRE(world_edit_block_set(edit, x, y, -1, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
        }
    }
    /* A tower 10 tall: at noon the sun's y is -0.3429 for a z of 0.93937, so its shadow goes north by 3.65 blocks. */
    for (int32_t z = 0; z < 10; z++) {
        REQUIRE(world_edit_block_set(edit, 8, 8, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, 0.5f32);
    /* Each spot is seen through the whole of a small picture, so an average of many texels says what the light is. */
    typedef struct spot_t {
        float32_t x;     /* 0 */
        float32_t y;     /* 4 */
        uint32_t shaded; /* 8 */
    } spot_t;
    static_assert(sizeof(spot_t) == 12);
    const spot_t spots[] = {
        {8.5f32, 10.5f32, 1u},  /* behind the tower, in its shadow */
        {8.5f32, 12.2f32, 1u},  /* near the tip: 9 + 3.65 = 12.65 */
        {8.5f32, 13.3f32, 0u},  /* past the tip */
        {8.5f32, 6.5f32, 0u},   /* on the sun's side */
        {6.5f32, 10.5f32, 0u},  /* off to one side */
        {10.5f32, 10.5f32, 0u}, /* off to the other */
    };
    const uint32_t lit_features = SK_FEATURES_ALL;
    uint64_t bright[2] = {0u, 0u};
    for (uint32_t s = 0u; s < sizeof(spots) / sizeof(spots[0]); s++) {
        render_camera_t camera;
        render_camera_start(&camera, (float64_t)spots[s].x, (float64_t)spots[s].y, 0.7f64, 0.0f32,
                            -RENDER_CAMERA_PITCH_LIMIT);
        camera.half_fov_tangent = 0.2f32;
        render_frame_features_set(frame, lit_features);
        const uint8_t *picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        uint64_t with_ray = 0u;
        for (uint32_t i = 0u; i < 64u * 64u; i++) {
            with_ray += sk_pixel(picture, 64u, i % 64u, i / 64u) >> 16;
        }
        render_frame_features_set(frame, lit_features & ~(uint32_t)SHADE_FEATURE_SHADOW);
        picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        uint64_t no_ray = 0u;
        for (uint32_t i = 0u; i < 64u * 64u; i++) {
            no_ray += sk_pixel(picture, 64u, i % 64u, i / 64u) >> 16;
        }
        printf("  (%.1f, %.1f): red with the ray %" PRIu64 ", without %" PRIu64 "\n", (libc_double_t)spots[s].x,
               (libc_double_t)spots[s].y, with_ray, no_ray);
        if (spots[s].shaded != 0u) {
            CHECK(with_ray * 10u < no_ray * 9u);
        } else {
            CHECK_EQ_U64(with_ray, no_ray);
        }
        bright[spots[s].shaded] += with_ray;
    }
    CHECK(bright[1] < bright[0]);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(stored_light_reaches_the_picture_at_every_level) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_open(t, &rig, 2u, SK_WIDTH, SK_HEIGHT, 100000.0f32));
    sk_scene_t *const scene = test_alloc(t, sizeof(sk_scene_t));
    REQUIRE(sk_scene_put(t, &rig, scene, true));
    /* The card has it as written: each cell of the floor, read back from the hits of a look straight down. */
    render_camera_t camera;
    render_camera_start(&camera, 8.0f64, 8.0f64, 16.0f64, 1.5708f32, -RENDER_CAMERA_PITCH_LIMIT);
    REQUIRE(sk_draw(t, &rig, &camera) != nullptr);
    const uint64_t hit_bytes = traverse_hits_bytes(SK_WIDTH, SK_HEIGHT);
    traverse_hit_t *const hits = test_alloc(t, hit_bytes);
    REQUIRE(device_memory_read(&rig.bridge->device_memory, &rig.render->owned_hits, 0u, hits, hit_bytes) ==
            GPU_STATUS_OK);
    traverse_camera_t view;
    render_camera_to_traverse(&camera, SK_WIDTH, SK_HEIGHT, &view);
    uint64_t floor_hits = 0u, wrong_light = 0u;
    for (uint64_t i = 0u; i < (uint64_t)SK_WIDTH * SK_HEIGHT; i++) {
        const int32_t cell_z = view.eye_cell[2] + hits[i].offset[2];
        if (traverse_hit_face(hits[i].info) == TRAVERSE_FACE_POS_Z && cell_z == -1) {
            const int32_t x = view.eye_cell[0] + hits[i].offset[0], y = view.eye_cell[1] + hits[i].offset[1];
            const uint32_t stored = (hits[i].value >> TRAVERSE_VALUE_LIGHT_SHIFT) & 255u;
            wrong_light += stored == ((uint32_t)x << 4 | (uint32_t)y) ? 0u : 1u;
            floor_hits++;
        }
    }
    printf("  the floor from above: %" PRIu64 " hits, %" PRIu64 " with the wrong light\n", floor_hits, wrong_light);
    CHECK(floor_hits > 10000u);
    CHECK_EQ_U64(wrong_light, 0u);

    /* At noon, at dusk and at night, every level against the trace: block light shows most at night. */
    const float32_t days[] = {SK_NOON, SK_DUSK, SK_NIGHT};
    for (uint32_t d = 0u; d < 3u; d++) {
        sk_tally_t *const tally = test_alloc(t, sizeof(sk_tally_t));
        memset(tally, 0, sizeof(*tally));
        sk_compare_around(t, &rig, scene, days[d], 3u, tally);
        sk_report("stored light", tally);
        CHECK_EQ_U64(tally->wrong, 0u);
        uint32_t levels_seen = 0u;
        for (uint32_t i = 0u; i < 256u; i++) {
            levels_seen += tally->light_levels[i] != 0u ? 1u : 0u;
        }
        CHECK(levels_seen > 180u);
    }
    /* Off, every voxel is under open sky: what was stored is not looked at. */
    render_frame_features_set(&rig.render->frame, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_LIGHT);
    sk_tally_t *const tally = test_alloc(t, sizeof(sk_tally_t));
    memset(tally, 0, sizeof(*tally));
    sk_compare_around(t, &rig, scene, SK_NOON, 3u, tally);
    sk_report("stored light off", tally);
    CHECK_EQ_U64(tally->wrong, 0u);
    CHECK(tally->light_levels[SHADE_LIGHT_OPEN_SKY] == tally->hits);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* A whole section of one block and one light is a form of its own on the card: its light is read from a different place. */
TEST(a_section_all_of_one_light_shows_that_light) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_open(t, &rig, 1u, SK_WIDTH, SK_HEIGHT, 100000.0f32));
    world_edit_t *const edit = &rig.world->edit;
    const uint8_t packed = 0x35u;
    for (int32_t z = 0; z < 16; z++) {
        for (int32_t y = 0; y < 16; y++) {
            for (int32_t x = 0; x < 16; x++) {
                REQUIRE(world_edit_block_set(edit, x, y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
                REQUIRE(world_edit_light_set(edit, x, y, z, packed) == WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t camera;
    render_camera_start(&camera, 8.0f64, 8.0f64, 30.0f64, 1.5708f32, -RENDER_CAMERA_PITCH_LIMIT);
    camera.half_fov_tangent = 0.1f32;
    REQUIRE(sk_draw(t, &rig, &camera) != nullptr);
    const uint64_t hit_bytes = traverse_hits_bytes(SK_WIDTH, SK_HEIGHT);
    traverse_hit_t *const hits = test_alloc(t, hit_bytes);
    REQUIRE(device_memory_read(&rig.bridge->device_memory, &rig.render->owned_hits, 0u, hits, hit_bytes) ==
            GPU_STATUS_OK);
    CHECK_EQ_U64((hits[(uint64_t)(SK_HEIGHT / 2u) * SK_WIDTH + SK_WIDTH / 2u].value >> TRAVERSE_VALUE_LIGHT_SHIFT) &
                     255u,
                 packed);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(each_setting_off_is_exactly_what_it_says) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_open(t, &rig, 2u, SK_WIDTH, SK_HEIGHT, 30.0f32));
    sk_scene_t *const scene = test_alloc(t, sizeof(sk_scene_t));
    REQUIRE(sk_scene_put(t, &rig, scene, true));
    const uint32_t settings[] = {
        SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SKY,   SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_DAY_CYCLE,
        SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_FOG,   SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SHADOW,
        SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_LIGHT, SK_FEATURES_NONE,
    };
    const char *const names[] = {"no sky", "no day cycle", "no fog", "no shadows", "no stored light", "none of these"};
    for (uint32_t s = 0u; s < 6u; s++) {
        render_frame_features_set(&rig.render->frame, settings[s]);
        sk_tally_t *const tally = test_alloc(t, sizeof(sk_tally_t));
        memset(tally, 0, sizeof(*tally));
        sk_compare_around(t, &rig, scene, SK_MORNING, 2u, tally);
        sk_report(names[s], tally);
        CHECK_EQ_U64(tally->wrong, 0u);
        CHECK(((settings[s] & SHADE_FEATURE_SKY) != 0u && (settings[s] & SHADE_FEATURE_SHADOW) != 0u) ==
              (tally->sun_rays > 0u));
    }
    /* A shadow only ever takes light away. */
    render_frame_time_set(&rig.render->frame, SK_MORNING);
    render_camera_t camera;
    sk_camera_around(&camera, 2.0f32, -0.6f32, 14.0f64);
    uint64_t bytes = post_xrgb_bytes(SK_WIDTH, SK_HEIGHT);
    uint8_t *const with_rays = test_alloc(t, bytes);
    render_frame_features_set(&rig.render->frame, SK_FEATURES_ALL);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(with_rays, picture, bytes);
    render_frame_features_set(&rig.render->frame, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SHADOW);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    uint64_t brighter = 0u, darker = 0u;
    for (uint32_t i = 0u; i < SK_WIDTH * SK_HEIGHT; i++) {
        const uint32_t on = sk_pixel(with_rays, SK_WIDTH, i % SK_WIDTH, i / SK_WIDTH);
        const uint32_t off = sk_pixel(picture, SK_WIDTH, i % SK_WIDTH, i / SK_WIDTH);
        for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
            brighter += ((on >> shift) & 255u) > ((off >> shift) & 255u) ? 1u : 0u;
            darker += ((on >> shift) & 255u) < ((off >> shift) & 255u) ? 1u : 0u;
        }
    }
    printf("  shadows against none: %" PRIu64 " channels darker, %" PRIu64 " brighter\n", darker, brighter);
    CHECK(darker > 1000u);
    CHECK_EQ_U64(brighter, 0u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* The real threads: main keeps the time, and the gpu thread draws by it. The clock here is what the test says has passed. */
TEST(the_clock_runs_through_the_real_threads_and_the_setting_stops_it) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const uint32_t width = 160u, height = 90u;
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        const game_config_t config = {.render_distance = 2u,
                                      .width = width,
                                      .height = height,
                                      .features_off = pass == 0u ? 0u : (uint16_t)SHADE_FEATURE_DAY_CYCLE,
                                      .show_date = 0u};
        REQUIRE(game_open(game, &config));
        CHECK_NEAR(game->day_fraction, GAME_DAY_START, 0.0f64);
        game_day_set(game, 0.9f64);
        const uint64_t bytes = post_xrgb_bytes(width, height);
        uint8_t *const screen = sk_aligned(t, bytes);
        display_buffer_t target = {
            .owned_map = screen, .map_bytes = bytes, .pitch = width * 4u, .width = width, .height = height};
        REQUIRE(game_loop_start(game, nullptr));
        const render_fly_input_t still = {0};
        /* Two minutes in one step is a tenth of a day: 0.9 goes round to 0.0, and 3 more steps to 0.3. */
        for (uint32_t i = 0u; i < 4u; i++) {
            REQUIRE(game_loop_frame(game, &still, 120.0f32, &target));
        }
        const int64_t started_ns = time_now_ns();
        /* The gpu thread picks it up on its next frame. */
        while (time_now_ns() - started_ns < TIME_NS_PER_SECOND / 2) {
            REQUIRE(game_loop_frame(game, &still, 0.0f32, &target));
            time_sleep_ns(2000000);
        }
        CHECK(game_loop_stop(game));
        const float64_t wanted = pass == 0u ? 0.3f64 : 0.9f64;
        /* Since plan 004 phase 02 the game hands the frame the sun of the clock at the eye's latitude, not a time of day: the frame
         * holds the sun of main's last clock. */
        climate_sun_t sun;
        climate_sun_of_clock(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed),
                             game->camera.position[1], &sun);
        printf("  %s: main says %.4f, a sun %.4f up; the frame was drawn with one %.4f up\n", pass == 0u ? "on" : "off",
               (libc_double_t)game->day_fraction, (libc_double_t)sun.direction[2],
               (libc_double_t)rig.render->frame.sun_given[2]);
        CHECK_NEAR(game->day_fraction, wanted, 1e-4f64);
        CHECK(rig.render->frame.sun_from_outside != 0u);
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            CHECK_NEAR((float64_t)rig.render->frame.sun_given[axis], sun.direction[axis], 1e-4f64);
        }
        game_close(game);
    }
    sk_rig_end(t, &rig);
}

/* ---- Golden pictures -------------------------------------------------------------------------------------------------------- */

static void sk_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(SK_WIDTH, SK_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, SK_WIDTH * 4u, SK_WIDTH, SK_HEIGHT, file, capacity, &written));
        REQUIRE(file_write_atomic(path, file, written) == FILE_OK);
        printf("  wrote %s\n", path);
        return;
    }
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        printf("  cannot read %s: run with MC_GOLDEN_WRITE=1, then look at what it wrote\n", path);
        return;
    }
    png_info_t info;
    png_error_t error;
    REQUIRE(png_probe(file, length, &info, &error) == PNG_OK);
    REQUIRE(info.width == SK_WIDTH && info.height == SK_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u, exact = 0u;
    for (uint32_t y = 0u; y < SK_HEIGHT; y++) {
        for (uint32_t x = 0u; x < SK_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * SK_WIDTH + x;
            const uint32_t ours = sk_pixel(picture, SK_WIDTH, x, y);
            const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
            wrong += sk_bytes_near(ours, golden, SK_GOLDEN_TOLERANCE) ? 0u : 1u;
            exact += ours == golden ? 1u : 0u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", path, exact,
           (uint64_t)SK_WIDTH * SK_HEIGHT, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

TEST(golden_pictures_match) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    /* The fog is a little short of the far side of the floor, so that the fog's own picture shows some. */
    REQUIRE(sk_open(t, &rig, 2u, SK_WIDTH, SK_HEIGHT, 30.0f32));
    sk_scene_t *const scene = test_alloc(t, sizeof(sk_scene_t));
    REQUIRE(sk_scene_put(t, &rig, scene, false));
    typedef struct golden_t {
        const char *path;  /* 0 */
        float32_t day;     /* 8 */
        uint32_t features; /* 12 */
    } golden_t;
    static_assert(sizeof(golden_t) == 16);
    const golden_t goldens[] = {
        {"tests/cuda/sky/fixtures/noon.png", SK_NOON, SK_FEATURES_ALL},
        {"tests/cuda/sky/fixtures/morning_shadows.png", SK_MORNING, SK_FEATURES_ALL},
        {"tests/cuda/sky/fixtures/dusk.png", SK_DUSK, SK_FEATURES_ALL},
        {"tests/cuda/sky/fixtures/night.png", SK_NIGHT, SK_FEATURES_ALL},
        {"tests/cuda/sky/fixtures/no_fog.png", SK_NOON, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_FOG},
        {"tests/cuda/sky/fixtures/no_shadows.png", SK_MORNING, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SHADOW},
        {"tests/cuda/sky/fixtures/no_sky.png", SK_MORNING, SK_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SKY},
    };
    render_camera_t camera;
    sk_camera_around(&camera, 2.1f32, -0.5f32, 18.0f64);
    for (uint32_t g = 0u; g < sizeof(goldens) / sizeof(goldens[0]); g++) {
        render_frame_features_set(&rig.render->frame, goldens[g].features);
        render_frame_time_set(&rig.render->frame, goldens[g].day);
        const uint8_t *const picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        sk_golden_compare(t, goldens[g].path, picture);
    }
    sk_hand_world_close(&rig);

    /* The floor's cells, each with its own light: sky across, block up the picture. At night the block light is what shows. */
    REQUIRE(sk_open(t, &rig, 2u, SK_WIDTH, SK_HEIGHT, 100000.0f32));
    REQUIRE(sk_scene_put(t, &rig, scene, true));
    render_camera_start(&camera, 8.0f64, 8.0f64, 10.0f64, 1.5708f32, -RENDER_CAMERA_PITCH_LIMIT);
    render_frame_time_set(&rig.render->frame, SK_NOON);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    sk_golden_compare(t, "tests/cuda/sky/fixtures/light_noon.png", picture);
    render_frame_time_set(&rig.render->frame, SK_NIGHT);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    sk_golden_compare(t, "tests/cuda/sky/fixtures/light_night.png", picture);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- What it costs, on the card's own clock -------------------------------------------------------------------------------- */

typedef struct sk_cost_t {
    float64_t shade_ms; /* 0 */
    float64_t post_ms;  /* 8 */
    float64_t total_ms; /* 16 */
} sk_cost_t;
static_assert(sizeof(sk_cost_t) == 24);

static void sk_cost_measure(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, uint32_t features,
                            sk_cost_t *out) {
    render_frame_t *const frame = &rig->render->frame;
    render_frame_features_set(frame, features);
    REQUIRE(sk_draw(t, rig, camera) != nullptr); /* a first frame pays for what the others find ready */
    const render_frame_stats_t before = frame->stats;
    for (uint32_t i = 0u; i < SK_COST_FRAMES; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t *const after = &frame->stats;
    const float64_t frames = (float64_t)SK_COST_FRAMES;
    out->shade_ms = (after->sum_ms[FRAME_PASS_SHADE] - before.sum_ms[FRAME_PASS_SHADE]) / frames;
    out->post_ms = (after->sum_ms[FRAME_PASS_POST] - before.sum_ms[FRAME_PASS_POST]) / frames;
    out->total_ms = (after->sum_total_ms - before.sum_total_ms) / frames;
}

static void sk_cost_row(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, const char *where) {
    const uint32_t settings[] = {SK_FEATURES_NONE,
                                 SK_FEATURES_BASE | SHADE_FEATURE_SKY,
                                 SK_FEATURES_BASE | SHADE_FEATURE_FOG,
                                 SK_FEATURES_BASE | SHADE_FEATURE_LIGHT,
                                 SK_FEATURES_BASE | SHADE_FEATURE_SKY | SHADE_FEATURE_SHADOW,
                                 SK_FEATURES_ALL};
    const char *const names[] = {"none", "sky", "fog", "light", "sky+shadow", "all"};
    for (uint32_t s = 0u; s < 6u; s++) {
        sk_cost_t cost = {0};
        sk_cost_measure(t, rig, camera, settings[s], &cost);
        printf("  cost, %s, %" PRIu32 " x %" PRIu32 ", %-10s: shade %7.3f ms, post %7.3f ms, frame %7.3f ms\n", where,
               rig->render->frame.width, rig->render->frame.height, names[s], (libc_double_t)cost.shade_ms,
               (libc_double_t)cost.post_ms, (libc_double_t)cost.total_ms);
        CHECK(cost.total_ms > 0.0f64);
    }
}

/* Real terrain, then the worst case on purpose: every surface in view asks for the sun, through a whole forest's crowns. Nothing
 * here is a limit: the numbers go in the stage file. The switches must do something on real terrain, or none of it was looked at. */
TEST(the_cost_is_measured_on_real_terrain_and_under_a_forest) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = SK_COST_RENDER_DISTANCE,
                                  .width = SK_COST_WIDTH,
                                  .height = SK_COST_HEIGHT,
                                  .features_off = 0u,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    render_t *const render = rig.render;
    render_camera_t camera = game->camera;
    camera.pitch = -0.35f32;
    render_frame_time_set(&render->frame, (float32_t)GAME_DAY_START);
    sk_cost_row(t, &rig, &camera, "terrain");
    /* What each setting changes, here. */
    const uint64_t bytes = (uint64_t)SK_COST_WIDTH * SK_COST_HEIGHT * 4u;
    uint8_t *const all = test_alloc(t, bytes);
    render_frame_features_set(&render->frame, SK_FEATURES_ALL);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(all, picture, bytes);
    const uint32_t bits[] = {SHADE_FEATURE_SKY, SHADE_FEATURE_FOG, SHADE_FEATURE_SHADOW, SHADE_FEATURE_LIGHT};
    const char *const names[] = {"sky", "fog", "shadow", "stored light"};
    for (uint32_t b = 0u; b < 4u; b++) {
        render_frame_features_set(&render->frame, SK_FEATURES_ALL & ~bits[b]);
        picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        uint64_t changed = 0u;
        for (uint64_t i = 0u; i < (uint64_t)SK_COST_WIDTH * SK_COST_HEIGHT; i++) {
            changed += memcmp(all + i * 4u, picture + i * 4u, 3u) != 0 ? 1u : 0u;
        }
        printf("  terrain, %s off against on: %" PRIu64 " pixels of %" PRIu64 " change\n", names[b], changed,
               (uint64_t)SK_COST_WIDTH * SK_COST_HEIGHT);
        /* Nothing writes stored light on real terrain until Stage 11.1: a floor with it is above, so this one is only reported. */
        if (bits[b] != SHADE_FEATURE_LIGHT) {
            CHECK(changed > 1000u);
        }
    }
    /* The time of day is worked out on the host, once a frame. */
    const int64_t started_ns = time_now_ns();
    for (uint32_t i = 0u; i < 100000u; i++) {
        render_frame_time_set(&render->frame, (float32_t)i / 100000.0f32);
    }
    printf("  cost, the day cycle on the host: %.3f us a frame\n",
           (libc_double_t)((float64_t)(time_now_ns() - started_ns) / 100000.0f64 / 1000.0f64));
    game_close(game);

    REQUIRE(sk_open(t, &rig, 3u, SK_COST_WIDTH, SK_COST_HEIGHT, 100000.0f32));
    for (int32_t y = -32; y < 32; y++) {
        for (int32_t x = -32; x < 32; x++) {
            REQUIRE(world_edit_block_set(&rig.world->edit, x, y, -1, BLOCK_ID_GRASS_BLOCK) ==
                    WORLD_EDIT_STATUS_CHANGED);
            /* Light of no special level, everywhere in view: the sum costs more than at 15 and 0, as on real terrain today. */
            const uint32_t sky = 7u + ((uint32_t)(x & 7)), block = 1u + ((uint32_t)(y & 7));
            REQUIRE(world_edit_light_set(&rig.world->edit, x, y, -1, (uint8_t)(sky << 4 | block)) ==
                    WORLD_EDIT_STATUS_CHANGED);
            for (int32_t z = 5; z < 9; z++) {
                REQUIRE(world_edit_block_set(&rig.world->edit, x, y, z, BLOCK_ID_OAK_LEAVES) ==
                        WORLD_EDIT_STATUS_CHANGED);
                REQUIRE(world_edit_light_set(&rig.world->edit, x, y, z, (uint8_t)(sky << 4 | block)) ==
                        WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_start(&camera, -30.5f64, 0.5f64, 2.6f64, 0.0f32, -0.05f32);
    render_frame_time_set(&render->frame, 0.4f32);
    sk_cost_row(t, &rig, &camera, "under leaves");
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

#include "cuda/sky/fixtures/program.h"
#include "cuda/sky/fixtures/from_above.h"

TEST_MAIN()
