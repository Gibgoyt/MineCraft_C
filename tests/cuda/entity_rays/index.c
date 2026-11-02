/* ctest render_feature_entities. See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
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
    EN_WIDTH = 320u,
    EN_HEIGHT = 180u,
    EN_PIXELS = EN_WIDTH * EN_HEIGHT,
    EN_GOLDEN_TOLERANCE = 2u,
    EN_GOLDEN_FRAMES = 40u,
    EN_PROGRAM_TIMEOUT_MS = 120000u,
    EN_COST_FRAMES = 30u,
    EN_COST_WIDTH = 1920u,
    EN_COST_HEIGHT = 1080u,
    EN_COST_RENDER_DISTANCE = 10u,
    EN_RANDOM_BOXES = 200u,
};

#define EN_FOG_END 400.0f32
#define EN_NEAR 2e-3f32
/* The light is then each face's one brightness and nothing else, and each frame stands alone. */
#define EN_PLAIN                                                                                                       \
    (SHADE_FEATURES_ALL &                                                                                              \
     ~(uint32_t)(SHADE_FEATURE_SKY | SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW | SHADE_FEATURE_LIGHT))
#define EN_PLAIN_NO_BOXES (EN_PLAIN & ~(uint32_t)SHADE_FEATURE_ENTITIES)
#define EN_PLAIN_NO_MARK (EN_PLAIN & ~(uint32_t)SHADE_FEATURE_OUTLINE)

#include "cuda/sky/fixtures/rig.h"

/* Numbers from 0 to 1, the same each run, with nothing to do with the code under test: a test's own, by xorshift. */
typedef struct en_rng_t {
    uint64_t state; /* 0 */
} en_rng_t;
static_assert(sizeof(en_rng_t) == 8);

static void en_rng_seed(en_rng_t *rng, uint64_t seed) {
    rng->state = seed * 0x9E3779B97F4A7C15ull + 1u;
}

static float64_t en_rng_next(en_rng_t *rng) {
    uint64_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng->state = x;
    return (float64_t)(x >> 11) / 9007199254740992.0f64;
}

/* ---- By hand ------------------------------------------------------------------------------------------------------------- */

static entity_rays_box_t en_box(float32_t x, float32_t y, float32_t z, float32_t half, float32_t yaw) {
    return (entity_rays_box_t){.center = {x, y, z},
                               .half = {half, half, half},
                               .cos_yaw = cosf(yaw),
                               .sin_yaw = sinf(yaw),
                               .material = BLOCK_ID_DIRT,
                               .texture = ENTITY_RAYS_NO_TEXTURE,
                               .stored = SHADE_LIGHT_OPEN_SKY};
}

TEST(a_ray_against_a_box_is_as_on_paper) {
    const float32_t east[3] = {1.0f32, 0.0f32, 0.0f32};
    const float32_t west[3] = {-1.0f32, 0.0f32, 0.0f32};
    const float32_t up[3] = {0.0f32, 0.0f32, 1.0f32};
    float32_t hit = 0.0f32;
    uint32_t face = 0u;
    const entity_rays_box_t ahead = en_box(3.0f32, 0.0f32, 0.0f32, 0.5f32, 0.0f32);
    CHECK(entity_rays_box_hit(&ahead, east, &hit, &face));
    CHECK_NEAR(hit, 2.5f32, 1e-5f32);
    CHECK_EQ_U64(face, TRAVERSE_FACE_NEG_X);
    /* Behind, beside, and from inside: none. */
    CHECK(!entity_rays_box_hit(&ahead, west, &hit, &face));
    CHECK(!entity_rays_box_hit(&ahead, up, &hit, &face));
    const entity_rays_box_t around = en_box(0.1f32, 0.0f32, 0.0f32, 0.5f32, 0.0f32);
    CHECK(!entity_rays_box_hit(&around, east, &hit, &face));
    /* The other way, the other face. */
    const entity_rays_box_t behind = en_box(-3.0f32, 0.0f32, 0.0f32, 0.5f32, 0.0f32);
    CHECK(entity_rays_box_hit(&behind, west, &hit, &face));
    CHECK_EQ_U64(face, TRAVERSE_FACE_POS_X);
    const entity_rays_box_t above = en_box(0.0f32, 0.0f32, 4.0f32, 1.0f32, 0.0f32);
    CHECK(entity_rays_box_hit(&above, up, &hit, &face));
    CHECK_NEAR(hit, 3.0f32, 1e-5f32);
    CHECK_EQ_U64(face, TRAVERSE_FACE_NEG_Z);

    /* A quarter turned: a corner comes at the ray, half a diagonal nearer. */
    const entity_rays_box_t turned = en_box(3.0f32, 0.0f32, 0.0f32, 0.5f32, 0.785398163f32);
    CHECK(entity_rays_box_hit(&turned, east, &hit, &face));
    CHECK_NEAR(hit, 3.0f32 - 0.707106781f32, 1e-4f32);
    /* Its faces look along the turned axes. */
    float32_t normal[3];
    const entity_rays_box_t square = en_box(0.0f32, 0.0f32, 0.0f32, 1.0f32, 1.5707963f32);
    entity_rays_normal(&square, TRAVERSE_FACE_POS_X, normal);
    CHECK_NEAR(normal[0], 0.0f32, 1e-5f32);
    CHECK_NEAR(normal[1], 1.0f32, 1e-5f32);
    entity_rays_normal(&square, TRAVERSE_FACE_POS_Y, normal);
    CHECK_NEAR(normal[0], -1.0f32, 1e-5f32);
    CHECK_NEAR(normal[1], 0.0f32, 1e-5f32);
    entity_rays_normal(&square, TRAVERSE_FACE_NEG_Z, normal);
    CHECK_NEAR(normal[2], -1.0f32, 1e-5f32);
    /* A box's own axes take a point the same way. */
    const float32_t north[3] = {0.0f32, 2.0f32, 0.0f32};
    float32_t own[3];
    entity_rays_to_box(&square, north, own);
    CHECK_NEAR(own[0], 2.0f32, 1e-5f32);
    CHECK_NEAR(own[1], 0.0f32, 1e-5f32);
}

TEST(a_line_a_crack_and_a_layout_are_as_on_paper) {
    /* 10 blocks ahead, a line off to the side by one, at a tangent of 0.01: a hundredth of a block a pixel per block. */
    const float32_t east[3] = {1.0f32, 0.0f32, 0.0f32};
    const float32_t from[3] = {10.0f32, 1.0f32, -5.0f32}, to[3] = {10.0f32, 1.0f32, 5.0f32};
    float32_t along = 0.0f32;
    CHECK_NEAR(entity_rays_line_pixels(from, to, east, 0.01f32, &along), 10.0f32, 1e-3f32);
    CHECK_NEAR(along, 10.0f32, 1e-4f32);
    /* Past its end, it is the end that counts. */
    const float32_t short_from[3] = {10.0f32, 1.0f32, 3.0f32}, short_to[3] = {10.0f32, 1.0f32, 5.0f32};
    CHECK_NEAR(entity_rays_line_pixels(short_from, short_to, east, 0.01f32, &along), sqrtf(10.0f32) * 10.0f32, 1e-1f32);
    /* Along the ray itself: no fault, and the near end. */
    const float32_t along_from[3] = {4.0f32, 0.0f32, 0.0f32}, along_to[3] = {6.0f32, 0.0f32, 0.0f32};
    CHECK_NEAR(entity_rays_line_pixels(along_from, along_to, east, 0.01f32, &along), 0.0f32, 1e-5f32);
    /* All behind the eye: never. */
    const float32_t behind_from[3] = {-6.0f32, 1.0f32, 0.0f32}, behind_to[3] = {-4.0f32, 1.0f32, 0.0f32};
    CHECK(entity_rays_line_pixels(behind_from, behind_to, east, 0.01f32, &along) >= TRAVERSE_COARSE_SKY);

    CHECK_EQ_U64(entity_rays_outline_pixels(180u), 1u);
    CHECK_EQ_U64(entity_rays_outline_pixels(720u), 1u);
    CHECK_EQ_U64(entity_rays_outline_pixels(1080u), 2u);
    CHECK_EQ_U64(entity_rays_outline_pixels(2160u), 3u);

    /* Every edge once, in 12: each of a unit box's has one axis changing, and the four of each axis differ. */
    const float32_t low[3] = {0.0f32, 0.0f32, 0.0f32}, high[3] = {1.0f32, 1.0f32, 1.0f32};
    for (uint32_t edge = 0u; edge < ENTITY_RAYS_EDGES; edge++) {
        float32_t a[3], b[3];
        entity_rays_edge(low, high, edge, a, b);
        uint32_t changing = 0u;
        for (uint32_t i = 0u; i < 3u; i++) {
            changing += a[i] != b[i] ? 1u : 0u;
        }
        CHECK_EQ_U64(changing, 1u);
        CHECK_EQ_U64((uint32_t)a[edge / 4u], 0u);
        CHECK_EQ_U64((uint32_t)b[edge / 4u], 1u);
    }

    CHECK_EQ_U64(render_entities_crack_frame(0.0f32), 0u);
    CHECK_EQ_U64(render_entities_crack_frame(-1.0f32), 0u);
    CHECK_EQ_U64(render_entities_crack_frame(1e-6f32), 1u);
    CHECK_EQ_U64(render_entities_crack_frame(0.1f32), 2u);
    CHECK_EQ_U64(render_entities_crack_frame(0.55f32), 6u);
    CHECK_EQ_U64(render_entities_crack_frame(0.999f32), 10u);
    CHECK_EQ_U64(render_entities_crack_frame(1.0f32), 10u);

    /* In one block, in order, and with nothing on top of anything. */
    const uint64_t starts = entity_rays_starts_offset(), links = entity_rays_links_offset(320u, 180u);
    CHECK_EQ_U64(starts, 8192u * 40u);
    CHECK_EQ_U64(links, starts + (40u * 23u + 1u) * 4u);
    CHECK(entity_rays_textures_offset(320u, 180u) >= links + (1u << 20) * 2u);
    CHECK_EQ_U64(entity_rays_textures_offset(320u, 180u) % 8u, 0u);
    CHECK_EQ_U64(entity_rays_hits_bytes(320u, 180u), EN_PIXELS * 8ull);
}

TEST(a_box_is_lit_as_a_block_would_be_on_the_same_face) {
    post_settings_t post;
    shade_sun_t sun;
    memset(&post, 0, sizeof(post));
    render_day_fill(0.4f32, &post, &sun);
    const entity_rays_box_t box = en_box(0.0f32, 0.0f32, 0.0f32, 1.0f32, 0.0f32);
    const uint32_t stored[] = {0xF0u, 0x30u, 0x0Du, 0x00u};
    for (uint32_t s = 0u; s < 4u; s++) {
        for (uint32_t face = 0u; face < SHADE_FACES; face++) {
            float32_t normal[3], ours[3], block[3];
            entity_rays_normal(&box, face, normal);
            shade_box_light(&sun, face, normal, stored[s], 0.5f32, ours);
            shade_surface_light(&sun, face, stored[s], 0.5f32, block);
            for (uint32_t i = 0u; i < 3u; i++) {
                CHECK_NEAR(ours[i], block[i], 1e-5f32);
            }
        }
    }
}

TEST(which_pixels_a_box_can_touch_are_as_on_paper) {
    /* Looking straight along an axis, at a square turned an eighth of a turn: it reaches sqrt(2) each way, its near corners that much
     * nearer. Along each axis, so that each of what a turned box reaches is what counts in one of them. */
    const float32_t half[3] = {1.0f32, 1.0f32, 1.0f32};
    const float32_t diagonal = 1.41421356f32;
    const float32_t eighth = 0.70710678f32;
    const float32_t yaws[2] = {0.0f32, 1.57079633f32};
    for (uint32_t look = 0u; look < 2u; look++) {
        render_camera_t eye;
        render_camera_start(&eye, 0.0f64, 0.0f64, 0.0f64, yaws[look], 0.0f32);
        traverse_camera_t view;
        render_camera_to_traverse(&eye, 320u, 180u, &view);
        const float32_t center[3] = {look == 0u ? 10.0f32 : 0.0f32, look == 0u ? 0.0f32 : 10.0f32, 0.0f32};
        uint32_t pixels[4];
        REQUIRE(render_entities_pixels(&view, center, half, eighth, eighth, 0.0f32, pixels));
        const float32_t across = sqrtf(view.right[0] * view.right[0] + view.right[1] * view.right[1]);
        const float32_t u = diagonal / ((10.0f32 - diagonal) * across);
        CHECK_NEAR((float32_t)pixels[2], (u + 1.0f32) * 160.0f32, 1.01f32);
        CHECK_NEAR((float32_t)pixels[0], (1.0f32 - u) * 160.0f32, 1.01f32);
        const float32_t v = 1.0f32 / ((10.0f32 - diagonal) * RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT);
        CHECK_NEAR((float32_t)pixels[3], (v + 1.0f32) * 90.0f32, 1.01f32);
        /* A margin widens it by that much, and what is off to one side is none. */
        uint32_t wider[4];
        REQUIRE(render_entities_pixels(&view, center, half, eighth, eighth, 3.0f32, wider));
        CHECK_EQ_U64(wider[2], pixels[2] + 3u);
        CHECK_EQ_U64(wider[0], pixels[0] - 3u);
        const float32_t behind[3] = {-center[0], -center[1], 0.0f32};
        CHECK(!render_entities_pixels(&view, behind, half, 1.0f32, 0.0f32, 0.0f32, pixels));
        const float32_t aside[3] = {center[0] + center[1] * 3.0f32, center[1] - center[0] * 3.0f32, 0.0f32};
        CHECK(!render_entities_pixels(&view, aside, half, 1.0f32, 0.0f32, 0.0f32, pixels));
        /* Across the plane of the eye and off to the side, it is cut there, and is not the whole picture. */
        const float32_t beside[3] = {look == 0u ? 0.0f32 : -6.0f32, look == 0u ? 6.0f32 : 0.0f32, 0.0f32};
        const float32_t long_half[3] = {look == 0u ? 3.0f32 : 1.0f32, look == 0u ? 1.0f32 : 3.0f32, 1.0f32};
        if (render_entities_pixels(&view, beside, long_half, 1.0f32, 0.0f32, 0.0f32, pixels)) {
            CHECK(pixels[2] - pixels[0] < 320u);
        }
    }
}

/* A ray against a march in tiny steps, in float64_t: another way altogether of saying whether a place is in a box. */
TEST(random_rays_agree_with_a_march_in_tiny_steps) {
    en_rng_t rng;
    en_rng_seed(&rng, 12345u);
    uint64_t hits = 0u;
    for (uint32_t i = 0u; i < 3000u; i++) {
        const float64_t yaw = en_rng_next(&rng) * 6.283185307f64;
        const entity_rays_box_t box = {
            .center = {(float32_t)(en_rng_next(&rng) * 8.0f64 - 4.0f64),
                       (float32_t)(en_rng_next(&rng) * 8.0f64 - 4.0f64),
                       (float32_t)(en_rng_next(&rng) * 8.0f64 - 4.0f64)},
            .half = {(float32_t)(en_rng_next(&rng) * 1.0f64 + 0.1f64), (float32_t)(en_rng_next(&rng) * 1.0f64 + 0.1f64),
                     (float32_t)(en_rng_next(&rng) * 1.0f64 + 0.1f64)},
            .cos_yaw = (float32_t)cos(yaw),
            .sin_yaw = (float32_t)sin(yaw),
        };
        /* Near the box's way half the time, or few would meet it. */
        float64_t way[3];
        float64_t length = 0.0f64;
        for (uint32_t a = 0u; a < 3u; a++) {
            way[a] = (float64_t)box.center[a] * 0.8f64 + (en_rng_next(&rng) - 0.5f64) * 2.0f64;
            length += way[a] * way[a];
        }
        length = sqrt(length);
        const float32_t direction[3] = {(float32_t)(way[0] / length), (float32_t)(way[1] / length),
                                        (float32_t)(way[2] / length)};
        float32_t met = 0.0f32;
        uint32_t face = 0u;
        const bool said = entity_rays_box_hit(&box, direction, &met, &face);
        /* Inside at the start, it should say none whatever a march says. */
        float32_t own[3], eye[3] = {-box.center[0], -box.center[1], -box.center[2]};
        entity_rays_to_box(&box, eye, own);
        if (fabsf(own[0]) < box.half[0] && fabsf(own[1]) < box.half[1] && fabsf(own[2]) < box.half[2]) {
            CHECK(!said);
            continue;
        }
        float64_t marched = -1.0f64;
        for (uint32_t step = 0u; step < 8000u && marched < 0.0f64; step++) {
            const float64_t at = (float64_t)step * 0.002f64;
            const float32_t point[3] = {(float32_t)((float64_t)direction[0] * at) - box.center[0],
                                        (float32_t)((float64_t)direction[1] * at) - box.center[1],
                                        (float32_t)((float64_t)direction[2] * at) - box.center[2]};
            entity_rays_to_box(&box, point, own);
            if (fabsf(own[0]) <= box.half[0] && fabsf(own[1]) <= box.half[1] && fabsf(own[2]) <= box.half[2]) {
                marched = at;
            }
        }
        if (marched >= 0.0f64) {
            CHECK(said);
            CHECK_NEAR((float64_t)met, marched, 0.004f64);
            /* It came in by the face it says: that way is out of the box, against the ray. */
            float32_t normal[3];
            const entity_rays_box_t flat = box;
            entity_rays_normal(&flat, face, normal);
            CHECK(normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2] < 1e-4f32);
            hits++;
        } else if (said) {
            /* A clip of a corner, shorter than a step of the march. Past that, the march would have found it. */
            float32_t middle[3] = {direction[0] * (met + 1e-4f32) - box.center[0],
                                   direction[1] * (met + 1e-4f32) - box.center[1],
                                   direction[2] * (met + 1e-4f32) - box.center[2]};
            entity_rays_to_box(&box, middle, own);
            CHECK(fabsf(own[0]) <= box.half[0] + 2e-3f32 && fabsf(own[1]) <= box.half[1] + 2e-3f32 &&
                  fabsf(own[2]) <= box.half[2] + 2e-3f32);
            CHECK(met < 16.0f32);
        }
    }
    printf("  %" PRIu64 " rays that met a box\n", hits);
    CHECK(hits > 300u);
}

/* ---- A world, and what goes in it ------------------------------------------------------------------------------------------- */

static bool en_put(test_context_t *t, sk_rig_t *rig, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                   int32_t z1, uint32_t state) {
    for (int32_t z = z0; z < z1; z++) {
        for (int32_t y = y0; y < y1; y++) {
            for (int32_t x = x0; x < x1; x++) {
                const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, (uint16_t)state);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* A stone floor at z = -1, and a wall across at x = 20. */
static bool en_room(test_context_t *t, sk_rig_t *rig) {
    return en_put(t, rig, -2, -16, -2, 24, 32, -1, BLOCK_ID_STONE) &&
           en_put(t, rig, 20, -16, -1, 21, 32, 10, BLOCK_ID_STONE);
}

static bool en_begin(test_context_t *t, sk_rig_t *rig, uint32_t features) {
    if (!sk_rig_begin(t, rig) || !sk_hand_world_open(t, rig, 3u, EN_WIDTH, EN_HEIGHT, EN_FOG_END) || !en_room(t, rig) ||
        !sk_hand_world_sync(t, rig)) {
        return false;
    }
    render_frame_features_set(&rig->render->frame, features);
    return true;
}

static void en_end(test_context_t *t, sk_rig_t *rig) {
    sk_hand_world_close(rig);
    sk_rig_end(t, rig);
}

static render_entities_item_t en_item(float64_t x, float64_t y, float64_t z, float32_t half, uint16_t block,
                                      float32_t yaw) {
    return (render_entities_item_t){.center = {x, y, z},
                                    .half = {half, half, half},
                                    .yaw = yaw,
                                    .material = block,
                                    .texture = ENTITY_RAYS_NO_TEXTURE,
                                    .stored = SHADE_LIGHT_OPEN_SKY};
}

/* These boxes, and nothing else, with target if it is not nullptr. */
static void en_send(sk_rig_t *rig, const render_entities_item_t *items, uint32_t count,
                    const render_entities_target_t *target) {
    render_entities_t *const entities = rig->render->borrowed_entities;
    render_entities_begin(entities);
    for (uint32_t i = 0u; i < count; i++) {
        render_entities_add(entities, &items[i]);
    }
    if (target != nullptr) {
        render_entities_target_set(entities, target);
    }
    render_entities_publish(entities);
}

static uint8_t *en_picture(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera) {
    const uint64_t bytes = post_xrgb_bytes(EN_WIDTH, EN_HEIGHT);
    uint8_t *const copy = test_alloc(t, bytes);
    const uint8_t *const picture = sk_draw(t, rig, camera);
    if (picture != nullptr) {
        memcpy(copy, picture, bytes);
    }
    return copy;
}

static shade_pixel_t *en_shaded(test_context_t *t, sk_rig_t *rig) {
    const uint64_t bytes = shade_pixels_bytes(EN_WIDTH, EN_HEIGHT);
    shade_pixel_t *const shaded = test_alloc(t, bytes);
    CHECK_EQ_U64(device_memory_read(&rig->bridge->device_memory, &rig->render->owned_shaded, 0u, shaded, bytes),
                 GPU_STATUS_OK);
    return shaded;
}

static entity_rays_hit_t *en_hits(test_context_t *t, sk_rig_t *rig) {
    const uint64_t bytes = entity_rays_hits_bytes(EN_WIDTH, EN_HEIGHT);
    entity_rays_hit_t *const hits = test_alloc(t, bytes);
    CHECK_EQ_U64(device_memory_read(&rig->bridge->device_memory, &rig->render->owned_entity_hits, 0u, hits, bytes),
                 GPU_STATUS_OK);
    return hits;
}

/* A pixel's ray, in float64_t and then as the kernels have it. */
static void en_direction(const traverse_camera_t *camera, float32_t pixel_x, float32_t pixel_y, float32_t out[3]) {
    const float64_t u = 2.0f64 * (float64_t)pixel_x / (float64_t)camera->width - 1.0f64;
    const float64_t v = 1.0f64 - 2.0f64 * (float64_t)pixel_y / (float64_t)camera->height;
    float64_t way[3], length = 0.0f64;
    for (uint32_t i = 0u; i < 3u; i++) {
        way[i] = (float64_t)camera->forward[i] + u * (float64_t)camera->right[i] + v * (float64_t)camera->up[i];
        length += way[i] * way[i];
    }
    length = sqrt(length);
    for (uint32_t i = 0u; i < 3u; i++) {
        out[i] = (float32_t)(way[i] / length);
    }
}

/* What a texture's texel is, as a kernel reads it, from the blob. */
static const uint8_t *en_texel(const sk_rig_t *rig, const char *name, uint32_t frame, float32_t u, float32_t v,
                               float32_t t, float32_t tangent) {
    uint32_t id = 0u;
    texture_info_t info;
    if (!texture_find(rig->texture, name, &id) || !texture_info_get(rig->texture, id, &info)) {
        return nullptr;
    }
    const texture_blob_entry_t *const entry = info.borrowed_entry;
    const uint32_t level = shade_mip_level(t, tangent, entry->width, entry->level_count);
    const uint32_t width = shade_level_dimension(entry->width, level),
                   height = shade_level_dimension(entry->height, level);
    const uint8_t *pixels = nullptr;
    uint64_t bytes = 0u;
    texture_pixels_get(rig->texture, &pixels, &bytes);
    return pixels + entry->pixels_offset + entry->level_offset[level] +
           (uint64_t)frame * (uint64_t)width * height * 4u +
           ((uint64_t)shade_texel_of(v, height) * width + shade_texel_of(u, width)) * 4u;
}

static void en_texel_linear(const uint8_t *texel, float32_t out[3]) {
    for (uint32_t i = 0u; i < 3u; i++) {
        out[i] = shade_srgb_to_linear((float32_t)texel[i] / 255.0f32);
    }
}

/* ---- The lists -------------------------------------------------------------------------------------------------------------- */

/* Every box a pixel's ray can meet, this frame's jitter or not, is in that pixel's tile's list. */
static void en_lists_check(test_context_t *t, sk_rig_t *rig, const traverse_camera_t *camera, uint64_t *out_needed,
                           const entity_rays_scene_t *scene) {
    const render_entities_t *const entities = rig->render->borrowed_entities;
    const uint32_t across = traverse_tiles_across(EN_WIDTH), down = traverse_tiles_across(EN_HEIGHT);
    const uint32_t *const starts = entities->borrowed_starts;
    const uint16_t *const links = entities->borrowed_links;
    CHECK_EQ_U64(starts[(uint64_t)across * down], scene->link_count);
    uint8_t *const needed = test_alloc(t, (uint64_t)across * down * scene->box_count);
    memset(needed, 0, (uint64_t)across * down * scene->box_count);
    static const float32_t jitters[5][2] = {
        {0.0f32, 0.0f32}, {0.5f32, 0.5f32}, {-0.5f32, 0.5f32}, {0.5f32, -0.5f32}, {-0.5f32, -0.5f32}};
    for (uint32_t y = 0u; y < EN_HEIGHT; y++) {
        for (uint32_t x = 0u; x < EN_WIDTH; x++) {
            const uint64_t tile = (uint64_t)(y / 8u) * across + x / 8u;
            for (uint32_t j = 0u; j < 5u; j++) {
                float32_t direction[3];
                en_direction(camera, (float32_t)x + 0.5f32 + jitters[j][0], (float32_t)y + 0.5f32 + jitters[j][1],
                             direction);
                for (uint32_t b = 0u; b < scene->box_count; b++) {
                    float32_t hit = 0.0f32;
                    uint32_t face = 0u;
                    if (entity_rays_box_hit(&entities->borrowed_boxes[b], direction, &hit, &face)) {
                        needed[tile * scene->box_count + b] = 1u;
                    }
                }
            }
        }
    }
    uint64_t wanted = 0u;
    for (uint64_t tile = 0u; tile < (uint64_t)across * down; tile++) {
        CHECK(starts[tile] <= starts[tile + 1u]);
        for (uint32_t b = 0u; b < scene->box_count; b++) {
            if (needed[tile * scene->box_count + b] == 0u) {
                continue;
            }
            wanted++;
            bool found = false;
            for (uint32_t link = starts[tile]; link < starts[tile + 1u]; link++) {
                found = found || links[link] == b;
            }
            CHECK(found);
        }
    }
    *out_needed = wanted;
}

TEST(each_tile_lists_every_box_it_can_see_and_little_else) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN));
    render_entities_t *const entities = rig.render->borrowed_entities;
    en_rng_t rng;
    en_rng_seed(&rng, 7u);
    render_entities_item_t *const items = test_alloc(t, EN_RANDOM_BOXES * sizeof(render_entities_item_t));
    /* All around the eye, some near, turned any way. */
    for (uint32_t i = 0u; i < EN_RANDOM_BOXES; i++) {
        items[i] =
            en_item(10.5f64 + (en_rng_next(&rng) - 0.5f64) * 18.0f64, 6.5f64 + (en_rng_next(&rng) - 0.5f64) * 18.0f64,
                    4.5f64 + (en_rng_next(&rng) - 0.5f64) * 12.0f64, (float32_t)(0.05f64 + en_rng_next(&rng) * 0.5f64),
                    BLOCK_ID_DIRT, (float32_t)(en_rng_next(&rng) * 6.28f64));
    }
    render_camera_t eye;
    render_camera_start(&eye, 10.5f64, 6.5f64, 4.5f64, 0.3f32, -0.2f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    en_send(&rig, items, EN_RANDOM_BOXES, nullptr);
    entity_rays_scene_t scene;
    REQUIRE(render_entities_frame(entities, &view, EN_PLAIN, 0.0f32, &scene) == GPU_STATUS_OK);
    CHECK(scene.box_count > 20u);
    CHECK(scene.box_count < EN_RANDOM_BOXES);
    CHECK_EQ_U64(scene.box_count + entities->stats.out_of_sight, EN_RANDOM_BOXES);
    uint64_t needed = 0u;
    en_lists_check(t, &rig, &view, &needed, &scene);
    printf("  %" PRIu32 " boxes on the picture, %" PRIu32 " links for %" PRIu64 " that any ray needs\n",
           scene.box_count, scene.link_count, needed);
    CHECK(needed > 0u);
    /* A rectangle around a box in 8 by 8 tiles: never tight, but not much more than it needs. */
    CHECK((uint64_t)scene.link_count < needed * 4u + scene.box_count * 12u);

    /* Looking along the other axis, as a turned box's reach along x is then across the picture. */
    render_camera_t north;
    render_camera_start(&north, 10.5f64, 6.5f64, 4.5f64, 1.65f32, -0.2f32);
    render_camera_to_traverse(&north, EN_WIDTH, EN_HEIGHT, &view);
    en_send(&rig, items, EN_RANDOM_BOXES, nullptr);
    REQUIRE(render_entities_frame(entities, &view, EN_PLAIN, 0.0f32, &scene) == GPU_STATUS_OK);
    en_lists_check(t, &rig, &view, &needed, &scene);
    CHECK(needed > 0u);

    /* What is behind goes, with nothing sent, and past the fog the same. */
    const render_entities_item_t behind = en_item(10.5f64 - 5.0f64, 6.5f64, 4.5f64, 0.25f32, BLOCK_ID_DIRT, 0.0f32);
    const render_entities_item_t far_one = en_item(310.5f64, 6.5f64, 4.5f64, 0.25f32, BLOCK_ID_DIRT, 0.0f32);
    render_camera_t east;
    render_camera_start(&east, 10.5f64, 6.5f64, 4.5f64, 0.0f32, 0.0f32);
    render_camera_to_traverse(&east, EN_WIDTH, EN_HEIGHT, &view);
    en_send(&rig, &behind, 1u, nullptr);
    REQUIRE(render_entities_frame(entities, &view, EN_PLAIN, 0.0f32, &scene) == GPU_STATUS_OK);
    CHECK_EQ_U64(scene.box_count, 0u);
    en_send(&rig, &far_one, 1u, nullptr);
    REQUIRE(render_entities_frame(entities, &view, EN_PLAIN, 0.0f32, &scene) == GPU_STATUS_OK);
    CHECK_EQ_U64(scene.box_count, 1u);
    REQUIRE(render_entities_frame(entities, &view, EN_PLAIN, 200.0f32, &scene) == GPU_STATUS_OK);
    CHECK_EQ_U64(scene.box_count, 0u);
    /* The setting off: nothing. */
    REQUIRE(render_entities_frame(entities, &view, EN_PLAIN_NO_BOXES, 0.0f32, &scene) == GPU_STATUS_OK);
    CHECK_EQ_U64(scene.box_count, 0u);
    en_end(t, &rig);
}

TEST(a_full_snapshot_and_full_lists_leave_out_what_is_far) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN));
    render_entities_t *const entities = rig.render->borrowed_entities;
    render_camera_t eye;
    render_camera_start(&eye, 0.5f64, 0.5f64, 4.5f64, 0.0f32, 0.0f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    /* One more than fits: a wide far box first, then near ones wide enough to fill all the links. */
    render_entities_begin(entities);
    for (uint32_t i = 0u; i < ENTITY_RAYS_BOXES_MAX + 1u; i++) {
        const bool far_one = i < 4000u;
        const render_entities_item_t item =
            en_item(far_one ? 40.5f64 : 2.5f64, 0.5f64, 4.5f64, far_one ? 18.0f32 : 1.5f32, BLOCK_ID_DIRT, 0.0f32);
        render_entities_add(entities, &item);
    }
    CHECK_EQ_U64(entities->stats.out_of_snapshots, 1u);
    CHECK_EQ_U64(entities->slots[entities->exchange.back].count, ENTITY_RAYS_BOXES_MAX);
    render_entities_publish(entities);
    entity_rays_scene_t scene;
    REQUIRE(render_entities_frame(entities, &view, EN_PLAIN, 0.0f32, &scene) == GPU_STATUS_OK);
    printf("  %" PRIu32 " boxes kept, %" PRIu32 " links, %" PRIu64 " left out\n", scene.box_count, scene.link_count,
           entities->stats.out_of_links);
    CHECK(entities->stats.out_of_links > 0u);
    CHECK_EQ_U64(scene.box_count + entities->stats.out_of_links, ENTITY_RAYS_BOXES_MAX);
    CHECK(scene.link_count <= ENTITY_RAYS_LINKS_MAX);
    /* Near ones first: all of them are kept before any far one. */
    uint32_t near_kept = 0u;
    for (uint32_t b = 0u; b < scene.box_count; b++) {
        near_kept += entities->borrowed_boxes[b].center[0] < 10.0f32 ? 1u : 0u;
    }
    /* All that the links could hold were near ones, and a far one could only have the links too few for another near one. */
    CHECK(scene.box_count - near_kept <= 1u);
    CHECK(near_kept * 920u + 920u > ENTITY_RAYS_LINKS_MAX);
    /* And the frame still draws. */
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    en_end(t, &rig);
}

/* ---- On the card ------------------------------------------------------------------------------------------------------------- */

static void en_hits_compare(test_context_t *t, sk_rig_t *rig, const traverse_camera_t *camera, uint64_t *out_hits) {
    const render_entities_t *const entities = rig->render->borrowed_entities;
    const entity_rays_hit_t *const hits = en_hits(t, rig);
    uint64_t wrong = 0u, seen = 0u;
    /* What the scene said, kept by the last draw in borrowed_boxes. */
    uint32_t boxes = 0u;
    while (boxes < ENTITY_RAYS_BOXES_MAX && entities->borrowed_boxes[boxes].half[0] != 0.0f32) {
        boxes++;
    }
    for (uint32_t y = 0u; y < EN_HEIGHT; y++) {
        for (uint32_t x = 0u; x < EN_WIDTH; x++) {
            float32_t direction[3];
            en_direction(camera, (float32_t)x + 0.5f32, (float32_t)y + 0.5f32, direction);
            float32_t nearest = TRAVERSE_COARSE_SKY;
            uint32_t nearest_face = TRAVERSE_FACE_MISS, nearest_box = ENTITY_RAYS_NO_BOX;
            for (uint32_t b = 0u; b < boxes; b++) {
                float32_t hit = 0.0f32;
                uint32_t face = 0u;
                if (entity_rays_box_hit(&entities->borrowed_boxes[b], direction, &hit, &face) && hit < nearest) {
                    nearest = hit;
                    nearest_face = face;
                    nearest_box = b;
                }
            }
            const entity_rays_hit_t *const card = &hits[(uint64_t)y * EN_WIDTH + x];
            /* A ray past a corner can go either way between the two sides' rounding: then both must say so nearly the same. */
            if (card->box != ENTITY_RAYS_NO_BOX) {
                seen++;
            }
            const bool same =
                card->box == nearest_box && card->face == nearest_face && fabsf(card->t - nearest) <= EN_NEAR * nearest;
            wrong += same ? 0u : 1u;
        }
    }
    printf("  %" PRIu64 " pixels on a box, %" PRIu64 " not as the host says\n", seen, wrong);
    /* A float32_t direction worked out two ways can differ at a box's very edge, so a few may differ, and they are few. */
    CHECK(wrong * 200u < seen + 200u);
    *out_hits = seen;
}

TEST(the_card_finds_the_box_the_host_finds_at_every_pixel) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN));
    render_entities_t *const entities = rig.render->borrowed_entities;
    en_rng_t rng;
    en_rng_seed(&rng, 99u);
    render_entities_item_t *const items = test_alloc(t, EN_RANDOM_BOXES * sizeof(render_entities_item_t));
    for (uint32_t i = 0u; i < EN_RANDOM_BOXES; i++) {
        items[i] =
            en_item(12.0f64 + en_rng_next(&rng) * 7.0f64, 6.5f64 + (en_rng_next(&rng) - 0.5f64) * 8.0f64,
                    4.5f64 + (en_rng_next(&rng) - 0.5f64) * 6.0f64, (float32_t)(0.05f64 + en_rng_next(&rng) * 0.2f64),
                    BLOCK_ID_DIRT, (float32_t)(en_rng_next(&rng) * 6.28f64));
    }
    render_camera_t eye;
    render_camera_start(&eye, 4.5f64, 6.5f64, 4.5f64, 0.0f32, 0.0f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    memset(entities->borrowed_boxes, 0, (uint64_t)ENTITY_RAYS_BOXES_MAX * sizeof(entity_rays_box_t));
    en_send(&rig, items, EN_RANDOM_BOXES, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    uint64_t seen = 0u;
    en_hits_compare(t, &rig, &view, &seen);
    CHECK(seen > 2000u);

    /* A jittered frame looks off each pixel's middle, and its lists were made with no jitter: the same still holds. */
    render_frame_features_set(&rig.render->frame, EN_PLAIN | (uint32_t)SHADE_FEATURE_TAA);
    for (uint32_t frame = 0u; frame < 4u; frame++) {
        REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
        traverse_camera_t jittered = view;
        post_camera_jitter(&jittered, rig.render->frame.temporal.jitter);
        en_hits_compare(t, &rig, &jittered, &seen);
        CHECK(seen > 2000u);
    }
    en_end(t, &rig);
}

/* ---- What a box looks like --------------------------------------------------------------------------------------------------- */

/* What a box's pixel holds with this one light: its texture over the whole of each face, by that face's one brightness. */
static uint64_t en_box_pixels_check(test_context_t *t, const sk_rig_t *rig, const traverse_camera_t *camera,
                                    const entity_rays_box_t *box, const char *const names[SHADE_FACES],
                                    const shade_pixel_t *shaded) {
    uint64_t checked = 0u, wrong = 0u;
    const float32_t tangent = shade_pixel_tangent(camera);
    for (uint32_t y = 0u; y < EN_HEIGHT; y++) {
        for (uint32_t x = 0u; x < EN_WIDTH; x++) {
            float32_t direction[3], hit = 0.0f32;
            uint32_t face = 0u;
            en_direction(camera, (float32_t)x + 0.5f32, (float32_t)y + 0.5f32, direction);
            if (!entity_rays_box_hit(box, direction, &hit, &face)) {
                continue;
            }
            const float32_t point[3] = {direction[0] * hit - box->center[0], direction[1] * hit - box->center[1],
                                        direction[2] * hit - box->center[2]};
            float32_t own[3], local[3], u = 0.0f32, v = 0.0f32;
            entity_rays_to_box(box, point, own);
            float32_t longest = 0.0f32;
            for (uint32_t i = 0u; i < 3u; i++) {
                local[i] = (own[i] + box->half[i]) / (2.0f32 * box->half[i]);
                longest = box->half[i] * 2.0f32 > longest ? box->half[i] * 2.0f32 : longest;
            }
            /* Right on a texel's edge, rounding on the two sides chooses differently, and that is no fault of either. */
            shade_face_uv(face, local, &u, &v);
            const uint8_t *const texel = en_texel(rig, names[face], 0u, u, v, hit / longest, tangent);
            if (!CHECK(texel != nullptr)) {
                return checked;
            }
            float32_t colour[3];
            en_texel_linear(texel, colour);
            const float32_t brightness = shade_face_brightness(face);
            const shade_pixel_t *const pixel = &shaded[(uint64_t)y * EN_WIDTH + x];
            checked++;
            for (uint32_t i = 0u; i < 3u; i++) {
                if (fabsf(pixel->rgb[i] - colour[i] * brightness) > 0.01f32) {
                    wrong++;
                    break;
                }
            }
            wrong += fabsf(pixel->t - hit) > hit * EN_NEAR ? 1u : 0u;
        }
    }
    printf("  %" PRIu64 " pixels of a box against its texture, %" PRIu64 " off\n", checked, wrong);
    CHECK(wrong * 100u <= checked);
    return checked;
}

TEST(a_box_shows_its_texture_and_hides_what_is_behind_it_and_nothing_else) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN));
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 6.5f64, 4.5f64, 0.0f32, -0.1f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    en_send(&rig, nullptr, 0u, nullptr);
    uint8_t *const bare = en_picture(t, &rig, &eye);
    const shade_pixel_t *const bare_shaded = en_shaded(t, &rig);

    /* A grass block, an item the eye looks a little down on, and turned. */
    const render_entities_item_t item = en_item(13.0f64, 6.5f64, 4.2f64, 0.5f32, BLOCK_ID_GRASS_BLOCK, 0.6f32);
    en_send(&rig, &item, 1u, nullptr);
    uint8_t *const with_box = en_picture(t, &rig, &eye);
    const shade_pixel_t *const shaded = en_shaded(t, &rig);
    const entity_rays_box_t box = rig.render->borrowed_entities->borrowed_boxes[0];
    /* By the table's names, for each of its faces. */
    const char *names[SHADE_FACES];
    for (uint32_t f = 0u; f < SHADE_FACES; f++) {
        names[f] = block_texture_names[block_defs[BLOCK_ID_GRASS_BLOCK].faces[f]];
    }
    CHECK(en_box_pixels_check(t, &rig, &view, &box, names, shaded) > 500u);

    /* Every pixel not on it is as if it were not there, to the byte. */
    uint64_t changed = 0u, off_box = 0u;
    for (uint32_t y = 0u; y < EN_HEIGHT; y++) {
        for (uint32_t x = 0u; x < EN_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * EN_WIDTH + x;
            float32_t direction[3], hit = 0.0f32;
            uint32_t face = 0u;
            en_direction(&view, (float32_t)x + 0.5f32, (float32_t)y + 0.5f32, direction);
            const bool on_box = entity_rays_box_hit(&box, direction, &hit, &face);
            const bool same = memcmp(bare + i * 4u, with_box + i * 4u, 4u) == 0;
            changed += same ? 0u : 1u;
            off_box += !same && !on_box ? 1u : 0u;
            if (!on_box) {
                CHECK_NEAR(shaded[i].t, bare_shaded[i].t, 1e-3f32 + bare_shaded[i].t * 1e-5f32);
            }
        }
    }
    printf("  %" PRIu64 " pixels changed, %" PRIu64 " of them not on the box\n", changed, off_box);
    CHECK(changed > 500u);
    /* Only a pixel at a box's very rim, where rounding differs by side. */
    CHECK(off_box < 16u);

    /* Behind a wall, or off the picture: exactly as bare. */
    const render_entities_item_t walled = en_item(21.5f64, 6.5f64, 4.2f64, 0.5f32, BLOCK_ID_GRASS_BLOCK, 0.0f32);
    en_send(&rig, &walled, 1u, nullptr);
    uint8_t *const behind_wall = en_picture(t, &rig, &eye);
    CHECK_MEM_EQ(behind_wall, bare, EN_PIXELS * 4ull);
    /* The setting off: as bare. */
    en_send(&rig, &item, 1u, nullptr);
    render_frame_features_set(&rig.render->frame, EN_PLAIN_NO_BOXES);
    uint8_t *const off = en_picture(t, &rig, &eye);
    CHECK_MEM_EQ(off, bare, EN_PIXELS * 4ull);
    render_frame_features_set(&rig.render->frame, EN_PLAIN);
    /* The sky behind a box: it hides that too, on a pixel that had none. */
    render_camera_t up = eye;
    up.pitch = 0.9f32;
    const render_entities_item_t high = en_item(11.5f64, 6.5f64, 12.5f64, 0.7f32, BLOCK_ID_DIRT, 0.0f32);
    en_send(&rig, nullptr, 0u, nullptr);
    REQUIRE(sk_draw(t, &rig, &up) != nullptr);
    const shade_pixel_t *const sky = en_shaded(t, &rig);
    en_send(&rig, &high, 1u, nullptr);
    REQUIRE(sk_draw(t, &rig, &up) != nullptr);
    const shade_pixel_t *const on_sky = en_shaded(t, &rig);
    uint64_t took_sky = 0u;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        took_sky += sky[i].t >= TRAVERSE_COARSE_SKY && on_sky[i].t < TRAVERSE_COARSE_SKY ? 1u : 0u;
    }
    CHECK(took_sky > 100u);
    en_end(t, &rig);
}

TEST(an_item_with_no_block_has_its_own_texture_and_one_with_none_says_so) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN));
    render_entities_t *const entities = rig.render->borrowed_entities;
    uint32_t coal = BLOCK_DROP_ITEM_COUNT;
    uint32_t blocks = 0u, no_blocks = 0u, missing = 0u;
    for (uint32_t i = 0u; i < BLOCK_DROP_ITEM_COUNT; i++) {
        if (strcmp(block_drop_items[i].name, "coal") == 0) {
            coal = i;
        }
        if (block_drop_items[i].block != BLOCK_NO_ID) {
            blocks++;
            CHECK_EQ_U64(entities->borrowed_item_textures[i], ENTITY_RAYS_NO_TEXTURE);
        } else {
            no_blocks++;
            missing += entities->borrowed_item_textures[i] == ENTITY_RAYS_NO_TEXTURE ? 1u : 0u;
        }
    }
    printf("  %" PRIu32 " items are blocks, %" PRIu32 " have their own texture, %" PRIu32 " have none\n", blocks,
           no_blocks - missing, missing);
    CHECK_EQ_U64(missing, 0u);
    REQUIRE(coal != BLOCK_DROP_ITEM_COUNT);
    /* Its picture, from what goes in the snapshot. */
    render_entities_begin(entities);
    const float64_t place[3] = {13.0f64, 6.5f64, 4.2f64};
    render_entities_add_item_drop(entities, (uint16_t)coal, place, 0.5f32, 0u, SHADE_LIGHT_OPEN_SKY);
    const render_entities_item_t *const row = &entities->slots[entities->exchange.back].borrowed_items[0];
    CHECK_EQ_U64(row->material, ENTITY_RAYS_NO_MATERIAL);
    CHECK_NEAR(row->half[0], 0.25f32, 1e-6f32);
    render_entities_publish(entities);
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 6.5f64, 4.5f64, 0.0f32, -0.1f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    const shade_pixel_t *const shaded = en_shaded(t, &rig);
    const entity_rays_hit_t *const hits = en_hits(t, &rig);
    uint64_t on_box = 0u, magenta = 0u;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (hits[i].box != ENTITY_RAYS_NO_BOX) {
            on_box++;
            magenta += shaded[i].rgb[0] > 0.45f32 && shaded[i].rgb[1] == 0.0f32 && shaded[i].rgb[2] > 0.45f32 ? 1u : 0u;
        }
    }
    CHECK(on_box > 100u);
    CHECK_EQ_U64(magenta, 0u);
    /* One with no texture and no block: the colour nothing else is. And it goes round with age. */
    render_entities_item_t none = en_item(13.0f64, 6.5f64, 4.2f64, 0.25f32, BLOCK_ID_DIRT, 0.0f32);
    none.material = ENTITY_RAYS_NO_MATERIAL;
    none.texture = ENTITY_RAYS_NO_TEXTURE;
    en_send(&rig, &none, 1u, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    const shade_pixel_t *const shaded_none = en_shaded(t, &rig);
    const entity_rays_hit_t *const hits_none = en_hits(t, &rig);
    on_box = 0u;
    magenta = 0u;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (hits_none[i].box != ENTITY_RAYS_NO_BOX) {
            on_box++;
            magenta += shaded_none[i].rgb[1] == 0.0f32 && shaded_none[i].rgb[0] > 0.4f32 ? 1u : 0u;
        }
    }
    CHECK(on_box > 100u);
    CHECK_EQ_U64(magenta, on_box);
    render_entities_begin(entities);
    render_entities_add_item_drop(entities, 0u, place, 0.5f32, 20u, SHADE_LIGHT_OPEN_SKY);
    const render_entities_item_t *const old = &entities->slots[entities->exchange.back].borrowed_items[0];
    CHECK_NEAR(old->yaw, 1.0f32, 1e-5f32);
    CHECK_EQ_U64(old->material, block_drop_items[0].block);
    en_end(t, &rig);
}

/* A box the world's own surfaces go in front of and behind as they should, in light, shade and fog. */
TEST(a_box_stands_in_the_sun_in_shade_and_in_fog) {
    sk_rig_t rig;
    const uint32_t lit = SHADE_FEATURES_ALL;
    REQUIRE(en_begin(t, &rig, lit));
    /* A low roof at z = 6, over x from 12 to 15 and y from 0 to 16, and a clear place beside it: noon's sun is high. */
    REQUIRE(en_put(t, &rig, 11, -4, 6, 16, 16, 7, BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_time_set(&rig.render->frame, 0.5f32);
    const shade_sun_t sun = rig.render->frame.scene.sun;
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 6.5f64, 5.0f64, 0.0f32, -0.5f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    const render_entities_item_t under = en_item(13.5f64, 6.5f64, 2.5f64, 0.5f32, BLOCK_ID_STONE, 0.0f32);
    en_send(&rig, &under, 1u, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    const shade_pixel_t *const shaded = en_shaded(t, &rig);
    const entity_rays_hit_t *const hits = en_hits(t, &rig);
    float64_t under_sum = 0.0f64;
    uint64_t under_count = 0u;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (hits[i].box != ENTITY_RAYS_NO_BOX && hits[i].face == TRAVERSE_FACE_POS_Z) {
            under_sum += (float64_t)shaded[i].rgb[1];
            under_count++;
        }
    }
    REQUIRE(under_count > 50u);
    /* The same box, the same height, clear of the roof, by the roof's side. */
    const render_entities_item_t clear = en_item(13.5f64, 20.5f64, 2.5f64, 0.5f32, BLOCK_ID_STONE, 0.0f32);
    render_camera_t eye_clear = eye;
    eye_clear.position[1] += 14.0f64;
    en_send(&rig, &clear, 1u, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye_clear) != nullptr);
    const shade_pixel_t *const shaded_clear = en_shaded(t, &rig);
    const entity_rays_hit_t *const hits_clear = en_hits(t, &rig);
    float64_t clear_sum = 0.0f64;
    uint64_t clear_count = 0u;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (hits_clear[i].box != ENTITY_RAYS_NO_BOX && hits_clear[i].face == TRAVERSE_FACE_POS_Z) {
            clear_sum += (float64_t)shaded_clear[i].rgb[1];
            clear_count++;
        }
    }
    REQUIRE(clear_count > 50u);
    /* By exactly the sun on that top: the texture's the same, so the ratio is the light's. */
    float32_t lit_light[3], dark_light[3];
    const float32_t up[3] = {0.0f32, 0.0f32, 1.0f32};
    shade_box_light(&sun, TRAVERSE_FACE_POS_Z, up, SHADE_LIGHT_OPEN_SKY, 1.0f32, lit_light);
    shade_box_light(&sun, TRAVERSE_FACE_POS_Z, up, SHADE_LIGHT_OPEN_SKY, 0.0f32, dark_light);
    const float64_t ratio = (under_sum / (float64_t)under_count) / (clear_sum / (float64_t)clear_count);
    printf("  a box's top under a roof against in the open: %.4f, by the light %.4f\n", (libc_double_t)ratio,
           (libc_double_t)(dark_light[1] / lit_light[1]));
    CHECK_NEAR(ratio, (float64_t)(dark_light[1] / lit_light[1]), 0.02f64);

    /* The light stored around it counts: in the dark with no block light it has the least of what a cave has. */
    render_entities_item_t dark = under;
    dark.stored = 0x00u;
    en_send(&rig, &dark, 1u, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    const shade_pixel_t *const shaded_dark = en_shaded(t, &rig);
    float64_t dark_sum = 0.0f64;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (hits[i].box != ENTITY_RAYS_NO_BOX && hits[i].face == TRAVERSE_FACE_POS_Z) {
            dark_sum += (float64_t)shaded_dark[i].rgb[1];
        }
    }
    const float64_t stored_ratio = dark_sum / under_sum;
    printf("  the same with no light around it: %.4f, by the level %.4f\n", (libc_double_t)stored_ratio,
           (libc_double_t)shade_sky_level(0u));
    CHECK_NEAR(stored_ratio, (float64_t)shade_sky_level(0u), 0.02f64);

    /* Fog goes by the box and not by what is behind it: the picture's distance is the box's. */
    render_frame_features_set(&rig.render->frame, lit);
    en_send(&rig, &clear, 1u, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye_clear) != nullptr);
    const shade_pixel_t *const shaded_fog = en_shaded(t, &rig);
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (hits_clear[i].box != ENTITY_RAYS_NO_BOX) {
            CHECK(shaded_fog[i].t <= hits_clear[i].t * 1.001f32);
        }
    }
    en_end(t, &rig);
}

TEST(a_box_shows_through_glass_and_water_and_water_stops_at_it) {
    sk_rig_t rig;
    const uint32_t see = EN_PLAIN;
    REQUIRE(en_begin(t, &rig, see));
    /* A pane of glass in front of a place on the floor, and a pool with a deep part and a shallow part. */
    REQUIRE(en_put(t, &rig, 11, 2, -1, 12, 10, 9, BLOCK_ID_GLASS));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 6.5f64, 4.5f64, 0.0f32, 0.0f32);
    const render_entities_item_t beyond = en_item(14.5f64, 6.5f64, 4.5f64, 0.5f32, BLOCK_ID_DIRT, 0.0f32);
    en_send(&rig, nullptr, 0u, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    const shade_pixel_t *const bare = en_shaded(t, &rig);
    en_send(&rig, &beyond, 1u, nullptr);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    const shade_pixel_t *const through = en_shaded(t, &rig);
    const entity_rays_hit_t *const hits = en_hits(t, &rig);
    uint64_t on_box = 0u, shows = 0u, by_glass = 0u;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (hits[i].box != ENTITY_RAYS_NO_BOX) {
            on_box++;
            /* This pack's glass has holes, and texels that hide what is behind: each pixel is one or the other. */
            const bool hole = through[i].t >= hits[i].t - 1e-3f32;
            shows += hole && through[i].rgb[0] != bare[i].rgb[0] ? 1u : 0u;
            by_glass += !hole && memcmp(through[i].rgb, bare[i].rgb, sizeof(through[i].rgb)) == 0 ? 1u : 0u;
        }
    }
    printf("  behind glass: %" PRIu64 " pixels on a box, %" PRIu64 " show it, %" PRIu64 " with the glass nearer\n",
           on_box, shows, by_glass);
    CHECK(on_box > 300u);
    CHECK(shows > 100u);
    CHECK(by_glass > 100u);
    CHECK(shows + by_glass + 8u >= on_box);
    en_end(t, &rig);

    /* Water: a box the same way into it looks the same whether the bottom is near or far, as tint stops at it. */
    sk_rig_t pool;
    REQUIRE(en_begin(t, &pool, see | 0u));
    REQUIRE(en_put(t, &pool, -2, -16, -1, 19, 32, 10, BLOCK_ID_AIR));
    /* Shallow: a floor 4 under the top. Deep: the other side, 40 under. */
    REQUIRE(en_put(t, &pool, 2, 0, 0, 16, 8, 4, BLOCK_ID_WATER));
    REQUIRE(en_put(t, &pool, 2, 12, -40, 16, 20, 4, BLOCK_ID_WATER));
    REQUIRE(en_put(t, &pool, 2, 0, -1, 16, 8, 0, BLOCK_ID_STONE));
    REQUIRE(en_put(t, &pool, 2, 12, -41, 16, 20, -40, BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &pool));
    render_frame_features_set(&pool.render->frame, see & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION);
    float32_t reds[2] = {0.0f32, 0.0f32};
    for (uint32_t which = 0u; which < 2u; which++) {
        const float64_t y = which == 0u ? 4.5f64 : 16.5f64;
        render_camera_t above;
        render_camera_start(&above, 8.5f64, y, 9.5f64, 0.0f32, -1.5f32);
        const render_entities_item_t in_water = en_item(8.5f64, y, 2.5f64, 0.5f32, BLOCK_ID_STONE, 0.0f32);
        en_send(&pool, &in_water, 1u, nullptr);
        REQUIRE(sk_draw(t, &pool, &above) != nullptr);
        const shade_pixel_t *const seen = en_shaded(t, &pool);
        const entity_rays_hit_t *const met = en_hits(t, &pool);
        uint64_t count = 0u;
        float64_t sum = 0.0f64;
        for (uint64_t i = 0u; i < EN_PIXELS; i++) {
            if (met[i].box != ENTITY_RAYS_NO_BOX) {
                sum += (float64_t)seen[i].rgb[0];
                count++;
            }
        }
        REQUIRE(count > 50u);
        reds[which] = (float32_t)(sum / (float64_t)count);
    }
    printf("  in water, red of a box over a near bottom %.4f, over a far one %.4f\n", (libc_double_t)reds[0],
           (libc_double_t)reds[1]);
    CHECK(reds[0] > 0.0f32);
    /* Water here covers most of what is under it, so that what a box adds is small, and this has to be close to mean anything. */
    CHECK_NEAR(reds[0], reds[1], 0.0005f32);
    en_end(t, &pool);
}

/* ---- The outline ------------------------------------------------------------------------------------------------------------- */

static render_entities_target_t en_target_of(int32_t x, int32_t y, int32_t z) {
    return (render_entities_target_t){.box_min = {(float64_t)x, (float64_t)y, (float64_t)z},
                                      .box_max = {(float64_t)x + 1.0f64, (float64_t)y + 1.0f64, (float64_t)z + 1.0f64},
                                      .outline = 1u};
}

/* How many pixels of a picture differ, and whether every one of those is what an outline's are. */
static uint64_t en_outline_pixels(test_context_t *t, const uint8_t *with, const uint8_t *without) {
    uint64_t changed = 0u, wrong = 0u;
    const uint32_t *const a = (const uint32_t *)(const void *)with, *const b = (const uint32_t *)(const void *)without;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        if (a[i] != b[i]) {
            changed++;
            wrong += a[i] == ENTITY_RAYS_OUTLINE_XRGB ? 0u : 1u;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    return changed;
}

/* The middle of an edge of a box on the camera, and whether it is black there, or within a pixel. */
static bool en_edge_drawn(const uint8_t *picture, const traverse_camera_t *camera,
                          const render_entities_target_t *target, uint32_t edge, bool *out_on_picture) {
    const float32_t low[3] = {
        (float32_t)(target->box_min[0] - (float64_t)camera->eye_cell[0] - (float64_t)camera->eye_frac[0]),
        (float32_t)(target->box_min[1] - (float64_t)camera->eye_cell[1] - (float64_t)camera->eye_frac[1]),
        (float32_t)(target->box_min[2] - (float64_t)camera->eye_cell[2] - (float64_t)camera->eye_frac[2])};
    const float32_t high[3] = {low[0] + (float32_t)(target->box_max[0] - target->box_min[0]),
                               low[1] + (float32_t)(target->box_max[1] - target->box_min[1]),
                               low[2] + (float32_t)(target->box_max[2] - target->box_min[2])};
    float32_t from[3], to[3], middle[3];
    entity_rays_edge(low, high, edge, from, to);
    float32_t ahead = 0.0f32, side = 0.0f32, rise = 0.0f32, right_squared = 0.0f32, up_squared = 0.0f32;
    for (uint32_t i = 0u; i < 3u; i++) {
        middle[i] = (from[i] + to[i]) * 0.5f32;
        ahead += middle[i] * camera->forward[i];
        side += middle[i] * camera->right[i];
        rise += middle[i] * camera->up[i];
        right_squared += camera->right[i] * camera->right[i];
        up_squared += camera->up[i] * camera->up[i];
    }
    *out_on_picture = false;
    if (ahead <= 0.1f32) {
        return false;
    }
    const float32_t x = (side / (ahead * right_squared) + 1.0f32) * 0.5f32 * (float32_t)camera->width;
    const float32_t y = (1.0f32 - rise / (ahead * up_squared)) * 0.5f32 * (float32_t)camera->height;
    if (x < 2.0f32 || y < 2.0f32 || x >= (float32_t)camera->width - 2.0f32 || y >= (float32_t)camera->height - 2.0f32) {
        return false;
    }
    *out_on_picture = true;
    for (int32_t dy = -1; dy <= 1; dy++) {
        for (int32_t dx = -1; dx <= 1; dx++) {
            const uint64_t i = (uint64_t)((int32_t)y + dy) * camera->width + (uint64_t)((int32_t)x + dx);
            if (sk_pixel(picture, camera->width, (uint32_t)((int32_t)x + dx), (uint32_t)((int32_t)y + dy)) ==
                (ENTITY_RAYS_OUTLINE_XRGB & 0xFFFFFFu)) {
                (void)i;
                return true;
            }
        }
    }
    return false;
}

TEST(the_outline_is_on_the_edges_that_can_be_seen_and_nowhere_else) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN));
    /* A block standing on the floor, seen from above and to one side. */
    REQUIRE(en_put(t, &rig, 12, 6, -1, 13, 7, 0, BLOCK_ID_DIRT));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const render_entities_target_t target = en_target_of(12, 6, -1);
    render_camera_t eye;
    render_camera_start(&eye, 9.5f64, 4.5f64, 1.5f64, 0.6f32, -0.55f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    en_send(&rig, nullptr, 0u, nullptr);
    uint8_t *const bare = en_picture(t, &rig, &eye);
    en_send(&rig, nullptr, 0u, &target);
    uint8_t *const lined = en_picture(t, &rig, &eye);
    const uint64_t changed = en_outline_pixels(t, lined, bare);
    printf("  %" PRIu64 " pixels of outline\n", changed);
    CHECK(changed > 40u);
    CHECK(changed < 2000u);
    /* From up here: the seven edges a cube shows, and the bottom's far ones and its hidden corner's not. */
    uint32_t drawn = 0u, asked = 0u;
    for (uint32_t edge = 0u; edge < ENTITY_RAYS_EDGES; edge++) {
        bool on_picture = false;
        if (en_edge_drawn(lined, &view, &target, edge, &on_picture)) {
            drawn++;
        }
        asked += on_picture ? 1u : 0u;
    }
    printf("  of %" PRIu32 " edges on the picture, %" PRIu32 " drawn\n", asked, drawn);
    CHECK_EQ_U64(asked, 12u);
    CHECK_EQ_U64(drawn, 9u); /* top 4, the two near sides down to the floor's plane: 3, and the two near bottoms */

    /* The setting off: nothing. */
    render_frame_features_set(&rig.render->frame, EN_PLAIN_NO_MARK);
    uint8_t *const off = en_picture(t, &rig, &eye);
    CHECK_MEM_EQ(off, bare, EN_PIXELS * 4ull);
    render_frame_features_set(&rig.render->frame, EN_PLAIN);

    /* A wall in front of it: none of it, however it is aimed at. */
    REQUIRE(en_put(t, &rig, 11, 3, -1, 12, 9, 6, BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t behind;
    render_camera_start(&behind, 9.5f64, 6.5f64, 0.5f64, 0.0f32, -0.2f32);
    en_send(&rig, nullptr, 0u, nullptr);
    uint8_t *const wall_bare = en_picture(t, &rig, &behind);
    en_send(&rig, nullptr, 0u, &target);
    uint8_t *const wall_lined = en_picture(t, &rig, &behind);
    CHECK_EQ_U64(en_outline_pixels(t, wall_lined, wall_bare), 0u);
    /* A box in front of it hides its line where it stands. */
    REQUIRE(en_put(t, &rig, 11, 3, -1, 12, 9, 6, BLOCK_ID_AIR));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t flat;
    render_camera_start(&flat, 9.5f64, 6.5f64, 0.5f64, 0.0f32, -0.2f32);
    en_send(&rig, nullptr, 0u, nullptr);
    uint8_t *const open_bare = en_picture(t, &rig, &flat);
    en_send(&rig, nullptr, 0u, &target);
    const uint8_t *const open_lined = en_picture(t, &rig, &flat);
    const uint64_t open_lines = en_outline_pixels(t, open_lined, open_bare);
    CHECK(open_lines > 40u);
    const render_entities_item_t blocking = en_item(10.8f64, 6.5f64, 0.0f64, 0.45f32, BLOCK_ID_STONE, 0.0f32);
    en_send(&rig, &blocking, 1u, nullptr);
    uint8_t *const box_bare_mark = en_picture(t, &rig, &flat);
    const render_entities_target_t none = {0};
    en_send(&rig, &blocking, 1u, &none);
    uint8_t *const box_bare = en_picture(t, &rig, &flat);
    en_send(&rig, &blocking, 1u, &target);
    uint8_t *const box_lined = en_picture(t, &rig, &flat);
    (void)box_bare_mark;
    const uint64_t box_lines = en_outline_pixels(t, box_lined, box_bare);
    printf("  a line with nothing in front %" PRIu64 ", with a box in front %" PRIu64 "\n", open_lines, box_lines);
    CHECK(box_lines < open_lines);
    en_end(t, &rig);
}

/* A player aims through water, so that what it aims at keeps its lines, from above and from under it. Glass is aimed at, so it hides. */
TEST(the_outline_shows_through_water_and_not_through_what_can_be_aimed_at) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION));
    REQUIRE(en_put(t, &rig, 2, 0, -1, 16, 12, 6, BLOCK_ID_WATER));
    REQUIRE(en_put(t, &rig, 8, 5, -1, 9, 6, 0, BLOCK_ID_DIRT));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const render_entities_target_t target = en_target_of(8, 5, -1);
    const float64_t eyes[2][3] = {{6.5f64, 3.5f64, 8.5f64}, {6.5f64, 3.5f64, 3.5f64}};
    const char *const where[2] = {"from above the water", "under it"};
    for (uint32_t e = 0u; e < 2u; e++) {
        render_camera_t eye;
        render_camera_start(&eye, eyes[e][0], eyes[e][1], eyes[e][2], 0.8f32, e == 0u ? -1.2f32 : -0.8f32);
        en_send(&rig, nullptr, 0u, nullptr);
        uint8_t *const bare = en_picture(t, &rig, &eye);
        en_send(&rig, nullptr, 0u, &target);
        uint8_t *const lined = en_picture(t, &rig, &eye);
        const uint64_t lines = en_outline_pixels(t, lined, bare);
        printf("  %s: %" PRIu64 " pixels of outline\n", where[e], lines);
        CHECK(lines > 30u);
    }
    /* Glass between: hidden. */
    REQUIRE(en_put(t, &rig, 7, 3, 3, 11, 8, 4, BLOCK_ID_GLASS));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 5.5f64, 5.5f64, 0.0f32, -1.5f32);
    en_send(&rig, nullptr, 0u, nullptr);
    uint8_t *const bare = en_picture(t, &rig, &eye);
    en_send(&rig, nullptr, 0u, &target);
    uint8_t *const lined = en_picture(t, &rig, &eye);
    CHECK_EQ_U64(en_outline_pixels(t, lined, bare), 0u);
    en_end(t, &rig);
}

TEST(the_outline_leaves_nothing_in_the_history_and_takes_nothing_from_it) {
    sk_rig_t rig;
    const uint32_t temporal = EN_PLAIN | (uint32_t)SHADE_FEATURE_TAA | (uint32_t)SHADE_FEATURE_AO;
    REQUIRE(en_begin(t, &rig, temporal));
    REQUIRE(en_put(t, &rig, 12, 6, -1, 13, 7, 0, BLOCK_ID_DIRT));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const render_entities_target_t target = en_target_of(12, 6, -1);
    render_camera_t eye;
    render_camera_start(&eye, 9.5f64, 4.5f64, 1.5f64, 0.6f32, -0.55f32);
    /* Eight frames of outline and four without, against twelve without: the same to the byte, as the lines never reach the history. */
    en_send(&rig, nullptr, 0u, &target);
    const uint8_t *picture = nullptr;
    for (uint32_t i = 0u; i < 8u; i++) {
        picture = sk_draw(t, &rig, &eye);
        REQUIRE(picture != nullptr);
    }
    const uint32_t *const shown = (const uint32_t *)(const void *)picture;
    uint64_t black = 0u;
    for (uint64_t i = 0u; i < EN_PIXELS; i++) {
        black += shown[i] == ENTITY_RAYS_OUTLINE_XRGB ? 1u : 0u;
    }
    CHECK(black > 40u);
    en_send(&rig, nullptr, 0u, nullptr);
    for (uint32_t i = 0u; i < 4u; i++) {
        picture = sk_draw(t, &rig, &eye);
        REQUIRE(picture != nullptr);
    }
    uint8_t *const after = test_alloc(t, EN_PIXELS * 4ull);
    memcpy(after, picture, EN_PIXELS * 4ull);
    en_end(t, &rig);

    sk_rig_t again;
    REQUIRE(en_begin(t, &again, temporal));
    REQUIRE(en_put(t, &again, 12, 6, -1, 13, 7, 0, BLOCK_ID_DIRT));
    REQUIRE(sk_hand_world_sync(t, &again));
    for (uint32_t i = 0u; i < 12u; i++) {
        picture = sk_draw(t, &again, &eye);
        REQUIRE(picture != nullptr);
    }
    CHECK_MEM_EQ(after, picture, EN_PIXELS * 4ull);
    en_end(t, &again);
}

/* ---- The crack ----------------------------------------------------------------------------------------------------------------- */

TEST(the_crack_is_on_the_block_being_mined_by_its_frame_and_nowhere_else) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN_NO_MARK));
    REQUIRE(en_put(t, &rig, 12, 6, -1, 13, 7, 0, BLOCK_ID_DIRT));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t eye;
    render_camera_start(&eye, 9.5f64, 4.5f64, 1.5f64, 0.6f32, -0.55f32);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    render_entities_target_t target = {.crack_cell = {12, 6, -1}, .crack_frame = 5u};
    /* The setting off: no crack. */
    en_send(&rig, nullptr, 0u, &target);
    uint8_t *const bare = en_picture(t, &rig, &eye);
    const shade_pixel_t *const shaded_bare = en_shaded(t, &rig);
    uint64_t hit_bytes = traverse_hits_bytes(EN_WIDTH, EN_HEIGHT);
    traverse_hit_t *const hits = test_alloc(t, hit_bytes);
    REQUIRE(device_memory_read(&rig.bridge->device_memory, &rig.render->owned_hits, 0u, hits, hit_bytes) ==
            GPU_STATUS_OK);
    render_frame_features_set(&rig.render->frame, EN_PLAIN);
    const float32_t tangent = shade_pixel_tangent(&view);
    uint64_t previous_dark = 0u;
    for (uint32_t frame = 1u; frame <= ENTITY_RAYS_CRACK_FRAMES; frame++) {
        target.crack_frame = frame;
        en_send(&rig, nullptr, 0u, &target);
        uint8_t *const cracked = en_picture(t, &rig, &eye);
        const shade_pixel_t *const shaded = en_shaded(t, &rig);
        uint64_t on_block = 0u, off_block_changed = 0u, wrong = 0u, covered = 0u;
        for (uint64_t i = 0u; i < EN_PIXELS; i++) {
            const traverse_hit_t *const hit = &hits[i];
            const uint32_t face = hit->info & TRAVERSE_INFO_FACE_MASK;
            const bool on = face < SHADE_FACES && (int32_t)hit->offset[0] + view.eye_cell[0] == 12 &&
                            (int32_t)hit->offset[1] + view.eye_cell[1] == 6 &&
                            (int32_t)hit->offset[2] + view.eye_cell[2] == -1;
            if (!on) {
                off_block_changed += memcmp(cracked + i * 4u, bare + i * 4u, 4u) != 0 ? 1u : 0u;
                continue;
            }
            on_block++;
            float32_t direction[3];
            en_direction(&view, (float32_t)(i % EN_WIDTH) + 0.5f32, (float32_t)(i / EN_WIDTH) + 0.5f32, direction);
            float32_t local[3], u = 0.0f32, v = 0.0f32;
            for (uint32_t a = 0u; a < 3u; a++) {
                local[a] = view.eye_frac[a] + direction[a] * hit->t - (float32_t)hit->offset[a];
            }
            shade_face_uv(face, local, &u, &v);
            const uint8_t *const texel = en_texel(&rig, "blocks/crack", frame - 1u, u, v, hit->t, tangent);
            REQUIRE(texel != nullptr);
            float32_t seen[3];
            en_texel_linear(texel, seen);
            const float32_t cover = (float32_t)texel[3] / 255.0f32;
            covered += texel[3] != 0u ? 1u : 0u;
            const float32_t brightness = shade_face_brightness(face);
            for (uint32_t c = 0u; c < 3u; c++) {
                const float32_t expected =
                    shaded_bare[i].rgb[c] + (seen[c] * brightness - shaded_bare[i].rgb[c]) * cover;
                if (fabsf(shaded[i].rgb[c] - expected) > 0.02f32) {
                    wrong++;
                    break;
                }
            }
        }
        printf("  frame %2" PRIu32 ": %" PRIu64 " pixels on the block, %" PRIu64 " under the crack, %" PRIu64
               " off, %" PRIu64 " changed elsewhere\n",
               frame, on_block, covered, wrong, off_block_changed);
        CHECK(on_block > 500u);
        CHECK(wrong * 100u <= on_block);
        CHECK_EQ_U64(off_block_changed, 0u);
        /* It spreads as mining goes on. */
        CHECK(covered + on_block / 20u >= previous_dark);
        previous_dark = covered;
    }
    CHECK(previous_dark > 50u);
    en_end(t, &rig);
}

/* ---- What a frame costs the machine ---------------------------------------------------------------------------------------------- */

TEST(a_frame_with_boxes_takes_nothing_from_the_heap_and_refuses_what_is_too_small) {
    sk_rig_t rig;
    REQUIRE(en_begin(t, &rig, EN_PLAIN));
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 6.5f64, 4.5f64, 0.0f32, -0.1f32);
    const render_entities_item_t item = en_item(13.0f64, 6.5f64, 4.2f64, 0.5f32, BLOCK_ID_DIRT, 0.0f32);
    const render_entities_target_t target = en_target_of(13, 6, 3);
    en_send(&rig, &item, 1u, &target);
    heap_counters_t before, after;
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 8u; i++) {
        en_send(&rig, &item, 1u, &target);
        REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));

    gpu_error_t error;
    memset(&error, 0, sizeof(error));
    traverse_camera_t view;
    render_camera_to_traverse(&eye, EN_WIDTH, EN_HEIGHT, &view);
    entity_rays_scene_t scene = {.box_count = 1u, .link_count = 1u};
    CHECK_EQ_U64(entity_rays_frame(&error, &scene, &view), GPU_STATUS_INVALID_ARGUMENT);
    scene.borrowed_lists = &rig.render->owned_entity_lists;
    scene.borrowed_hits = &rig.render->owned_entity_hits;
    scene.box_count = ENTITY_RAYS_BOXES_MAX + 1u;
    CHECK_EQ_U64(entity_rays_frame(&error, &scene, &view), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "entity_rays");
    /* With none it starts nothing, whatever it was given. */
    entity_rays_scene_t empty = {0};
    CHECK_EQ_U64(entity_rays_frame(&error, &empty, &view), GPU_STATUS_OK);
    empty.mark.outline = 1u;
    empty.mark.pixel_end[0] = (uint16_t)(EN_WIDTH + 1u);
    empty.mark.pixel_end[1] = 1u;
    const float32_t jitter[2] = {0.0f32, 0.0f32};
    const traverse_world_t world = {.borrowed_table = rig.render->buffers.borrowed_table,
                                    .borrowed_pool = rig.render->buffers.borrowed_pool,
                                    .window = rig.render->buffers.window};
    CHECK_EQ_U64(entity_rays_overlay(&error, &empty, &world, &view, jitter, BLOCK_ID_WATER, &rig.render->owned_shaded,
                                     &rig.render->owned_xrgb),
                 GPU_STATUS_INVALID_ARGUMENT);
    empty.mark.outline = 0u;
    CHECK_EQ_U64(entity_rays_overlay(&error, &empty, &world, &view, jitter, BLOCK_ID_WATER, &rig.render->owned_shaded,
                                     &rig.render->owned_xrgb),
                 GPU_STATUS_OK);
    en_end(t, &rig);
}

#include "cuda/entity_rays/fixtures/rest.h"

TEST_MAIN()
