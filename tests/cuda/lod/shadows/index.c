/* ctest render_feature_lod_shadows. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    LS_WIDTH = 320u,
    LS_HEIGHT = 180u,
    LS_PIXELS = LS_WIDTH * LS_HEIGHT,
    LS_TOWER_PATCHES = 7u,
};

#define LS_FOG_FAR 20000.0f32
/* A pixel darker in luma than with no shadow rays at all is in shadow: lit, the two are the same bits, and a low sun on a top is faint. */
#define LS_DARKER 0.5f64
/* Every pixel stands alone and nothing moves: no day going on, no history, no fog, no weather. */
#define LS_STILL                                                                                                       \
    (SHADE_FEATURES_ALL &                                                                                              \
     ~(uint32_t)(SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA | SHADE_FEATURE_FOG |                  \
                 SHADE_FEATURE_WEATHER | SHADE_FEATURE_GOD_RAYS | SHADE_FEATURE_ENTITIES))

#include "cuda/sky/fixtures/rig.h"

/* ---- Ground the far field samples, and the same in level 0 ------------------------------------------------------------------ */

typedef enum ls_ground_t : uint32_t {
    LS_GROUND_PLAIN = 0, /* grass at z = 10 */
    LS_GROUND_TOWER = 1, /* and a tower of one block, its top at z = 50, at (-150, 2) */
    LS_GROUND_HILL = 2,  /* and a ridge from x = 560 to 700 across all y, its top at z = 110 */
} ls_ground_t;
static_assert(sizeof(ls_ground_t) == 4);

typedef struct ls_source_t {
    uint32_t ground;  /* 0 */
    uint8_t _pad0[4]; /* 4 */
} ls_source_t;
static_assert(sizeof(ls_source_t) == 8);

enum : int32_t {
    LS_GROUND_TOP = 10,
    LS_TOWER_X = -150,
    LS_TOWER_Y = 2,
    LS_TOWER_TOP = 50,
    LS_HILL_FROM = 560,
    LS_HILL_TO = 700,
    LS_HILL_TOP = 110,
};

/* The highest block at a place, as a world z. */
static int32_t ls_top(uint32_t ground, int32_t x, int32_t y) {
    if (ground == LS_GROUND_TOWER && x == LS_TOWER_X && y == LS_TOWER_Y) {
        return LS_TOWER_TOP;
    }
    if (ground == LS_GROUND_HILL && x >= LS_HILL_FROM && x < LS_HILL_TO) {
        return LS_HILL_TOP;
    }
    return LS_GROUND_TOP;
}

static void ls_sample(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                      uint8_t *scratch) {
    (void)shift;
    (void)scratch;
    const uint32_t ground = ((const ls_source_t *)borrowed_context)->ground;
    const int32_t top = ls_top(ground, x, y) + (int32_t)WORLD_BUFFERS_Z_ORIGIN;
    const int32_t low = LS_GROUND_TOP + (int32_t)WORLD_BUFFERS_Z_ORIGIN - 8;
    for (int32_t z = 0; z < (int32_t)RENDER_LOD_HEIGHT_BLOCKS; z++) {
        out[z] = z == top ? (uint16_t)BLOCK_ID_GRASS_BLOCK : (z < top && z > low ? (uint16_t)BLOCK_ID_STONE : 0u);
    }
}

/* A box of one block in level 0, from (x0, y0, z0) up to but not taking (x1, y1, z1), where the hand world holds it. */
static bool ls_box(test_context_t *t, sk_rig_t *rig, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                   int32_t z1, uint16_t block) {
    for (int32_t z = z0; z < z1; z++) {
        for (int32_t y = y0; y < y1; y++) {
            for (int32_t x = x0; x < x1; x++) {
                const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, block);
                if (status == WORLD_EDIT_STATUS_NOT_LOADED) {
                    continue;
                }
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* The far field from source, by plan, about position, every column on the card. */
static bool ls_far_open(test_context_t *t, sk_rig_t *rig, const render_lod_plan_t *plan, const ls_source_t *source,
                        const float64_t position[3]) {
    if (!CHECK_EQ_U64(render_open_lod(rig->render, plan, 0u, ls_sample, source), GPU_STATUS_OK)) {
        return false;
    }
    render_lod_t *const lod = rig->render->borrowed_lod;
    for (uint64_t pass = 0u; pass < 100000000ull; pass++) {
        if (!CHECK_EQ_U64(render_lod_frame(lod, position), GPU_STATUS_OK)) {
            return false;
        }
        if (render_lod_idle(lod)) {
            return CHECK_EQ_U64(render_lod_frame(lod, position), GPU_STATUS_OK);
        }
        time_sleep_ns(20000);
    }
    return CHECK(false);
}

static void ls_far_close(sk_rig_t *rig) {
    render_close_lod(rig->render);
    sk_hand_world_close(rig);
}

/* ---- Pictures ---------------------------------------------------------------------------------------------------------------- */

typedef struct ls_picture_t {
    uint8_t *xrgb;         /* 0 */
    shade_pixel_t *shaded; /* 8 */
    lod_hit_t *far;        /* 16: LOD_NO_MATERIAL where no far ray went, and all of it with the far field off */
} ls_picture_t;
static_assert(sizeof(ls_picture_t) == 24);

static bool ls_draw(test_context_t *t, sk_rig_t *rig, const render_camera_t *eye, uint32_t features,
                    ls_picture_t *out) {
    render_frame_features_set(&rig->render->frame, features);
    const uint8_t *const picture = sk_draw(t, rig, eye);
    if (picture == nullptr) {
        return false;
    }
    device_memory_t *const memory = &rig->bridge->device_memory;
    out->xrgb = test_alloc(t, post_xrgb_bytes(LS_WIDTH, LS_HEIGHT));
    memcpy(out->xrgb, picture, post_xrgb_bytes(LS_WIDTH, LS_HEIGHT));
    const uint64_t shaded_bytes = shade_pixels_bytes(LS_WIDTH, LS_HEIGHT);
    out->shaded = test_alloc(t, shaded_bytes);
    CHECK_EQ_U64(device_memory_read(memory, &rig->render->owned_shaded, 0u, out->shaded, shaded_bytes), GPU_STATUS_OK);
    const uint64_t far_bytes = lod_hits_bytes(LS_WIDTH, LS_HEIGHT);
    out->far = test_alloc(t, far_bytes);
    for (uint64_t i = 0u; i < (uint64_t)LS_PIXELS; i++) {
        out->far[i].material = (uint16_t)LOD_NO_MATERIAL;
    }
    if ((features & SHADE_FEATURE_LOD) != 0u && rig->render->borrowed_lod != nullptr) {
        CHECK_EQ_U64(device_memory_read(memory, &rig->render->owned_lod_hits, 0u, out->far, far_bytes), GPU_STATUS_OK);
    }
    return true;
}

static float64_t ls_luma(const uint8_t *xrgb, uint64_t i) {
    return 0.299f64 * (float64_t)xrgb[i * 4u + 2u] + 0.587f64 * (float64_t)xrgb[i * 4u + 1u] +
           0.114f64 * (float64_t)xrgb[i * 4u + 0u];
}

/* Whether a pixel is in shadow: darker than the same pixel with no shadow rays at all. */
static bool ls_shadowed(const ls_picture_t *with, const ls_picture_t *without, uint64_t i) {
    return ls_luma(with->xrgb, i) + LS_DARKER < ls_luma(without->xrgb, i);
}

/* A pixel's unit direction, as the card works it out. */
static void ls_direction(const render_camera_t *eye, uint32_t x, uint32_t y, float64_t out[3]) {
    traverse_camera_t view;
    render_camera_to_traverse(eye, LS_WIDTH, LS_HEIGHT, &view);
    const float64_t u = 2.0f64 * ((float64_t)x + 0.5f64) / (float64_t)LS_WIDTH - 1.0f64;
    const float64_t v = 1.0f64 - 2.0f64 * ((float64_t)y + 0.5f64) / (float64_t)LS_HEIGHT;
    float64_t length = 0.0f64;
    for (uint32_t a = 0u; a < 3u; a++) {
        out[a] = (float64_t)view.forward[a] + u * (float64_t)view.right[a] + v * (float64_t)view.up[a];
        length += out[a] * out[a];
    }
    for (uint32_t a = 0u; a < 3u; a++) {
        out[a] /= sqrt(length);
    }
}

/* Towards a light so many radians up, from the east (+x) or the west. */
static void ls_sun(float64_t up, bool from_east, float32_t out[3]) {
    out[0] = (float32_t)((from_east ? 1.0f64 : -1.0f64) * cos(up));
    out[1] = 0.0f32;
    out[2] = (float32_t)sin(up);
}

/* An eye looking straight down at (x, y) from height blocks over the ground, seeing about reach blocks each way across the narrow side. */
static void ls_eye_down(render_camera_t *eye, float64_t x, float64_t y, float64_t height, float64_t reach) {
    render_camera_start(eye, x, y, (float64_t)LS_GROUND_TOP + 1.0f64 + height, 0.0f32, -1.5607963f32);
    eye->half_fov_tangent = (float32_t)(reach / height);
}

/* ---- Near shadows stay exact -------------------------------------------------------------------------------------------------- */

/* A pillar one block across and 20 high, its shadow on the ground under a sun 30 degrees up: every sun ray that meets it does so within 40
 * blocks, all in level 0, so every pixel level 0 shows is the same bits as with no far field at all (the far field's pixel error is set so
 * small that level 0 goes to its edge, and only the shadow rays differ). */
TEST(a_near_pillars_shadow_is_unchanged) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 4u, LS_WIDTH, LS_HEIGHT, LS_FOG_FAR));
    REQUIRE(ls_box(t, &rig, -96, -96, LS_GROUND_TOP, 96, 96, LS_GROUND_TOP + 1, BLOCK_ID_GRASS_BLOCK));
    REQUIRE(ls_box(t, &rig, 0, 0, LS_GROUND_TOP + 1, 1, 1, LS_GROUND_TOP + 21, BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const ls_source_t source = {LS_GROUND_PLAIN, {0}};
    const render_lod_plan_t plan = {.shifts = {1u, 2u, 3u}, .radii = {4u, 4u, 4u}, .level_count = 3u};
    render_camera_t eye;
    render_camera_start(&eye, -17.5f64, -14.5f64, 18.5f64, 1.5707963f32, -0.6f32);
    REQUIRE(ls_far_open(t, &rig, &plan, &source, eye.position));
    rig.render->borrowed_lod->pixel_error = 1e-4f32;
    float32_t sun[3];
    ls_sun(0.5235988f64, true, sun);
    render_frame_sun_set(&rig.render->frame, sun);
    ls_picture_t cone, reference, plain;
    REQUIRE(ls_draw(t, &rig, &eye, LS_STILL, &cone));
    REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)SHADE_FEATURE_LOD, &reference));
    REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)(SHADE_FEATURE_LOD | SHADE_FEATURE_SHADOW), &plain));
    uint64_t compared = 0u, differ = 0u, shadowed = 0u;
    for (uint64_t i = 0u; i < (uint64_t)LS_PIXELS; i++) {
        if (!((float64_t)reference.shaded[i].t < 60.0f64)) {
            continue; /* past level 0's edge, 64 blocks off */
        }
        compared++;
        differ += memcmp(&cone.shaded[i], &reference.shaded[i], sizeof(shade_pixel_t)) != 0 ? 1u : 0u;
        shadowed += ls_shadowed(&reference, &plain, i) ? 1u : 0u;
    }
    printf("  %" PRIu64 " pixels of level 0, %" PRIu64 " of them in the pillar's shadow; %" PRIu64 " differ\n",
           compared, shadowed, differ);
    CHECK(compared > (uint64_t)LS_PIXELS / 2u);
    CHECK(shadowed > 200u);
    CHECK_EQ_U64(differ, 0u);
    rig.render->borrowed_lod->pixel_error = RENDER_LOD_PIXEL_ERROR;
    ls_far_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- A thin far shadow, by the cone ------------------------------------------------------------------------------------------- */

/* A tower one block across, its top 40 over the ground, and a sun from the west so low that its shadow runs 330 blocks east. Patches of ground
 * along the shadow, seen from straight above, each with the far field (shadow rays by the cone: level 0 to 108 blocks, level 1 to 216, level 2
 * to 432) and without (level 0 all the way, the reference). Within 108 the shadow is the reference's to the pixel. Past it the far field's own
 * voxels decide: level 1 samples each 2 x 2 column at a corner the tower is not on, so the tower is not in it and its shadow vanishes from 108
 * to 216; level 2 samples the 4 x 4 column it stands in at the tower, so from 216 on its shadow is that voxel's, up to 4 blocks wide. The test
 * holds both, so that a change to either is seen. */
TEST(a_far_towers_shadow_follows_the_cone) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 12u, LS_WIDTH, LS_HEIGHT, LS_FOG_FAR));
    REQUIRE(ls_box(t, &rig, -192, -16, LS_GROUND_TOP, 192, 16, LS_GROUND_TOP + 1, BLOCK_ID_GRASS_BLOCK));
    REQUIRE(ls_box(t, &rig, LS_TOWER_X, LS_TOWER_Y, LS_GROUND_TOP + 1, LS_TOWER_X + 1, LS_TOWER_Y + 1, LS_TOWER_TOP + 1,
                   BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const ls_source_t source = {LS_GROUND_TOWER, {0}};
    /* Each level shows its radius less RENDER_LOD_MARGIN_COLUMNS: level 1 to 192 blocks west, level 2 to 256, both past the tower. */
    const render_lod_plan_t plan = {.shifts = {1u, 2u, 3u}, .radii = {8u, 6u, 5u}, .level_count = 3u};
    const float64_t centre[3] = {0.5f64, 0.5f64, 30.5f64};
    REQUIRE(ls_far_open(t, &rig, &plan, &source, centre));
    rig.render->borrowed_lod->pixel_error = 1e-4f32;
    const float64_t up = atan(40.0f64 / 330.0f64);
    float32_t sun[3];
    ls_sun(up, false, sun);
    render_frame_sun_set(&rig.render->frame, sun);
    static const float64_t along[LS_TOWER_PATCHES] = {20.0f64,  60.0f64,  100.0f64, 150.0f64,
                                                      200.0f64, 250.0f64, 300.0f64};
    for (uint32_t p = 0u; p < LS_TOWER_PATCHES; p++) {
        render_camera_t eye;
        ls_eye_down(&eye, (float64_t)LS_TOWER_X + 0.5f64 + along[p], (float64_t)LS_TOWER_Y + 0.5f64, 20.0f64, 3.0f64);
        ls_picture_t cone, reference, plain;
        REQUIRE(ls_draw(t, &rig, &eye, LS_STILL, &cone));
        REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)SHADE_FEATURE_LOD, &reference));
        REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)(SHADE_FEATURE_LOD | SHADE_FEATURE_SHADOW), &plain));
        uint64_t cone_dark = 0u, reference_dark = 0u, same = 0u, outside = 0u;
        for (uint32_t y = 0u; y < LS_HEIGHT; y++) {
            for (uint32_t x = 0u; x < LS_WIDTH; x++) {
                const uint64_t i = (uint64_t)y * LS_WIDTH + x;
                const bool c = ls_shadowed(&cone, &plain, i), r = ls_shadowed(&reference, &plain, i);
                cone_dark += c ? 1u : 0u;
                reference_dark += r ? 1u : 0u;
                same += c == r ? 1u : 0u;
                if (c) {
                    float64_t direction[3];
                    ls_direction(&eye, x, y, direction);
                    const float64_t world_y = eye.position[1] + direction[1] * (float64_t)cone.shaded[i].t;
                    /* The 4 x 4 voxel the tower is in, or the tower itself. */
                    outside += world_y < -0.01f64 || world_y > 4.01f64 ? 1u : 0u;
                }
            }
        }
        const float64_t blocks = along[p] / cos(up);
        printf("  %5.0f blocks along the ray: reference %5" PRIu64 " pixels of shadow, cone %5" PRIu64 " (%" PRIu64
               " outside the voxel), %" PRIu64 " pixels agree\n",
               (libc_double_t)blocks, reference_dark, cone_dark, outside, same);
        CHECK(reference_dark > 0u);
        if (blocks < 108.0f64) {
            CHECK_EQ_U64(same, (uint64_t)LS_PIXELS);
        } else if (blocks < 216.0f64) {
            CHECK_EQ_U64(cone_dark, 0u);
        } else {
            CHECK(cone_dark >= reference_dark);
            CHECK_EQ_U64(outside, 0u);
        }
    }
    rig.render->borrowed_lod->pixel_error = RENDER_LOD_PIXEL_ERROR;
    ls_far_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- Far hills shadow near ground --------------------------------------------------------------------------------------------- */

/* A ridge 100 high, 560 to 700 blocks east, in the far field alone, and a low sun behind it. With a sun at 1 in 10 every shadow ray from the
 * field under the eye meets the ridge (at x = 560 it is 56 to 57 blocks up, under the ridge's 100 by more than the cone's 5 blocks of
 * widening and a level 3 voxel's half), so the field is in shadow: today, with level 0 alone, it is lit. With a sun at 3 in 10 the rays pass 68
 * blocks over the ridge, and the field is lit either way. */
TEST(near_ground_gets_a_far_hills_shadow) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 4u, LS_WIDTH, LS_HEIGHT, LS_FOG_FAR));
    REQUIRE(ls_box(t, &rig, -32, -32, LS_GROUND_TOP, 32, 32, LS_GROUND_TOP + 1, BLOCK_ID_GRASS_BLOCK));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const ls_source_t source = {LS_GROUND_HILL, {0}};
    const render_lod_plan_t plan = {.shifts = {1u, 2u, 3u}, .radii = {4u, 4u, 8u}, .level_count = 3u};
    render_camera_t eye;
    ls_eye_down(&eye, 0.5f64, 0.5f64, 20.0f64, 6.0f64);
    REQUIRE(ls_far_open(t, &rig, &plan, &source, eye.position));
    static const float64_t slopes[2] = {0.1f64, 0.3f64};
    for (uint32_t s = 0u; s < 2u; s++) {
        float32_t sun[3];
        ls_sun(atan(slopes[s]), true, sun);
        render_frame_sun_set(&rig.render->frame, sun);
        ls_picture_t cone, reference, plain;
        REQUIRE(ls_draw(t, &rig, &eye, LS_STILL, &cone));
        REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)SHADE_FEATURE_LOD, &reference));
        REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)(SHADE_FEATURE_LOD | SHADE_FEATURE_SHADOW), &plain));
        uint64_t ground = 0u, cone_dark = 0u, reference_dark = 0u;
        for (uint64_t i = 0u; i < (uint64_t)LS_PIXELS; i++) {
            if (!((float64_t)reference.shaded[i].t < 60.0f64)) {
                continue;
            }
            ground++;
            cone_dark += ls_shadowed(&cone, &plain, i) ? 1u : 0u;
            reference_dark += ls_shadowed(&reference, &plain, i) ? 1u : 0u;
        }
        printf("  sun at %.1f in 10: %" PRIu64 " pixels of the field, %" PRIu64
               " in shadow with the far field, %" PRIu64 " with level 0 alone\n",
               (libc_double_t)(slopes[s] * 10.0f64), ground, cone_dark, reference_dark);
        CHECK_EQ_U64(ground, (uint64_t)LS_PIXELS);
        CHECK_EQ_U64(reference_dark, 0u);
        /* A few of the darkest texels move by under half a step of luma in shadow. */
        if (s == 0u) {
            CHECK(cone_dark * 1000u >= ground * 999u);
        } else {
            CHECK_EQ_U64(cone_dark, 0u);
        }
    }
    ls_far_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- No seam where the levels take over ---------------------------------------------------------------------------------------- */

/* A plain in level 0 and in the far field, the eye low over it, d_1 at 32 blocks and d_2 at 64 (E 4 at 180 rows), and a sun 20 degrees up. Nothing
 * stands on the plain, so no pixel may be in shadow on either side of a band edge: near pixels' shadow rays go through level 0 and on through
 * the levels, far pixels' through the levels alone, and a coarse voxel's rounding must shadow neither. Every pixel is the same bits as with
 * shadows off, and no far hit says the sun is hidden. */
TEST(far_and_near_sun_rays_agree_at_the_band_edge) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 6u, LS_WIDTH, LS_HEIGHT, LS_FOG_FAR));
    REQUIRE(ls_box(t, &rig, -128, -128, LS_GROUND_TOP, 128, 128, LS_GROUND_TOP + 1, BLOCK_ID_GRASS_BLOCK));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const ls_source_t source = {LS_GROUND_PLAIN, {0}};
    const render_lod_plan_t plan = {.shifts = {1u, 2u, 3u}, .radii = {6u, 4u, 3u}, .level_count = 3u};
    render_camera_t eye;
    render_camera_start(&eye, 0.5f64, 0.5f64, 16.5f64, 0.0f32, -0.14f32);
    REQUIRE(ls_far_open(t, &rig, &plan, &source, eye.position));
    rig.render->borrowed_lod->pixel_error = 4.0f32;
    const float32_t sun[3] = {(float32_t)(cos(0.349066f64) * cos(0.8f64)), (float32_t)(cos(0.349066f64) * sin(0.8f64)),
                              (float32_t)sin(0.349066f64)};
    render_frame_sun_set(&rig.render->frame, sun);
    ls_picture_t lit, unshadowed;
    REQUIRE(ls_draw(t, &rig, &eye, LS_STILL, &lit));
    REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)SHADE_FEATURE_SHADOW, &unshadowed));
    const float64_t d1 = (float64_t)rig.render->borrowed_lod->band_base * 2.0f64;
    uint64_t near = 0u, far = 0u, differ = 0u, hidden = 0u;
    for (uint64_t i = 0u; i < (uint64_t)LS_PIXELS; i++) {
        const float64_t t_hit = (float64_t)lit.shaded[i].t;
        if (!(t_hit < (float64_t)TRAVERSE_COARSE_SKY)) {
            continue;
        }
        if (lit.far[i].material != LOD_NO_MATERIAL) {
            far++;
            hidden += lit.far[i].sun == LOD_SUN_HIDDEN ? 1u : 0u;
        } else {
            near++;
        }
        differ += memcmp(&lit.shaded[i], &unshadowed.shaded[i], sizeof(shade_pixel_t)) != 0 ? 1u : 0u;
    }
    printf("  d_1 %.1f blocks: %" PRIu64 " pixels of level 0, %" PRIu64 " of the far field (%" PRIu64
           " with the sun hidden); %" PRIu64 " differ from no shadows\n",
           (libc_double_t)d1, near, far, hidden, differ);
    CHECK(near > 1000u);
    CHECK(far > 1000u);
    CHECK_EQ_U64(hidden, 0u);
    CHECK_EQ_U64(differ, 0u);
    rig.render->borrowed_lod->pixel_error = RENDER_LOD_PIXEL_ERROR;
    ls_far_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- The dim-light cut -------------------------------------------------------------------------------------------------------- */

/* Whether the cut holds for the sun at this height (the sine of its angle up), from the east, as render_sky_fill lights it. */
static bool ls_cut_at(float64_t height, shade_sun_t *out) {
    float32_t direction[3];
    ls_sun(asin(height), true, direction);
    post_settings_t post;
    memset(&post, 0, sizeof(post));
    memset(out, 0, sizeof(*out));
    render_sky_fill(direction, (float32_t)height, &post, out);
    return shade_shadows_skipped(out->direct);
}

/* Where the cut starts: the highest sun it holds for, found by halves, and the lowest it does not. Then a pillar's ground drawn with the sun
 * either side of that edge: one picture sends shadow rays and one does not, and no channel of any pixel moves by more than 2. It also holds
 * the plan's own reading of where the cut fires: never on moonlight, not at 7 degrees, and under full cloud a little higher than in a clear
 * sky. */
TEST(the_dim_light_cut_moves_no_pixel_by_more_than_2) {
    shade_sun_t sun;
    float64_t low = 0.0f64, high = 0.12f64;
    CHECK(ls_cut_at(low, &sun));
    CHECK(!ls_cut_at(high, &sun));
    for (uint32_t i = 0u; i < 48u; i++) {
        const float64_t middle = 0.5f64 * (low + high);
        if (ls_cut_at(middle, &sun)) {
            low = middle;
        } else {
            high = middle;
        }
    }
    printf("  the cut holds up to %.4f degrees over the horizon, %.3g of direct light\n",
           (libc_double_t)(asin(low) * 57.29578f64),
           (libc_double_t)(SHADE_SHADOW_SKIP_BELOW / SHADE_TONE_MAP_STEEPEST));
    CHECK(asin(high) * 57.29578f64 < 0.5f64);
    /* The moon at midnight, and the sun at 7 degrees: shadow rays both. */
    post_settings_t post;
    memset(&post, 0, sizeof(post));
    render_day_fill(0.0f32, &post, &sun);
    CHECK(!shade_shadows_skipped(sun.direct));
    CHECK(!ls_cut_at(sin(7.0f64 / 57.29578f64), &sun));
    /* Under full cloud the direct light is a quarter of a clear sky's, so a sun half as high again over the edge is cut there, and not in the
     * clear. */
    const float64_t raised = high * 1.5f64;
    CHECK(!ls_cut_at(raised, &sun));
    render_weather_t weather;
    memset(&weather, 0, sizeof(weather));
    weather.cloud = 1.0f32;
    weather.bolt_age = -1.0f32;
    weather.age_now = -1.0f32;
    const int32_t eye_cell[3] = {0, 0, 0};
    post_weather_t kernel_weather;
    float32_t direction[3];
    ls_sun(asin(raised), true, direction);
    memset(&post, 0, sizeof(post));
    render_sky_fill(direction, (float32_t)raised, &post, &sun);
    render_weather_apply(&weather, eye_cell, false, 100.0f32, &post, &sun, &kernel_weather);
    CHECK(shade_shadows_skipped(sun.direct));

    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, LS_WIDTH, LS_HEIGHT, LS_FOG_FAR));
    REQUIRE(ls_box(t, &rig, -32, -32, LS_GROUND_TOP, 32, 32, LS_GROUND_TOP + 1, BLOCK_ID_GRASS_BLOCK));
    REQUIRE(ls_box(t, &rig, 0, 0, LS_GROUND_TOP + 1, 1, 1, LS_GROUND_TOP + 21, BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_t eye;
    render_camera_start(&eye, -10.5f64, -12.5f64, 18.5f64, 1.5707963f32, -0.6f32);
    uint8_t *pictures[2];
    for (uint32_t side = 0u; side < 2u; side++) {
        ls_sun(asin(side == 0u ? low : high), true, direction);
        render_frame_sun_set(&rig.render->frame, direction);
        CHECK(shade_shadows_skipped(rig.render->frame.scene.sun.direct) == (side == 0u));
        ls_picture_t picture;
        REQUIRE(ls_draw(t, &rig, &eye, LS_STILL & ~(uint32_t)SHADE_FEATURE_LOD, &picture));
        pictures[side] = picture.xrgb;
    }
    uint32_t most = 0u;
    uint64_t moved = 0u;
    for (uint64_t i = 0u; i < (uint64_t)LS_PIXELS * 4u; i++) {
        if (i % 4u == 3u) {
            continue;
        }
        const uint32_t gap = (uint32_t)abs((int32_t)pictures[0][i] - (int32_t)pictures[1][i]);
        most = gap > most ? gap : most;
        moved += gap != 0u ? 1u : 0u;
    }
    printf("  either side of the edge: %" PRIu64 " channels moved, by %" PRIu32 " at most\n", moved, most);
    CHECK(most <= 2u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- Far leaves -------------------------------------------------------------------------------------------------------------- */

/* render_feature_shapes' line of leaves, seen from 100 blocks: past alpha_far_from_blocks (48) a pixel goes through at most alpha_layers_far (4)
 * surfaces. Four leaves with a hole on the line and a stone behind: from 20 blocks the stone shows, the fifth surface; from 100 the fourth leaf
 * stops the ray, on its mean colour. With one leaf fewer the stone is the fourth surface, and shows from 100 too. */
TEST(far_leaves_stop_after_four_holes) {
    static_assert(SHADE_ALPHA_LAYERS_FAR == 4u);
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 8u, LS_WIDTH, LS_HEIGHT, LS_FOG_FAR));
    uint32_t id = 0u;
    REQUIRE(texture_find(rig.texture, "blocks/oak_leaves", &id));
    texture_mip_t mip;
    REQUIRE(texture_mip_get(rig.texture, id, 0u, 0u, &mip));
    texture_info_t info;
    REQUIRE(texture_info_get(rig.texture, id, &info));
    int32_t hole_x = -1, hole_y = -1;
    for (uint32_t y = 0u; y < mip.height && hole_x < 0; y++) {
        for (uint32_t x = 0u; x < mip.width && hole_x < 0; x++) {
            if (mip.borrowed_pixels[((uint64_t)y * mip.width + x) * 4u + 3u] == 0u) {
                hole_x = (int32_t)x;
                hole_y = (int32_t)y;
            }
        }
    }
    REQUIRE(hole_x >= 0);
    /* The side towards -x, looked at from the -x side: u = 1 - y, v = 1 - z. */
    const float64_t eye_y = 8.0f64 + 1.0f64 - ((float64_t)hole_x + 0.5f64) / (float64_t)mip.width;
    const float64_t eye_z = 8.0f64 + 1.0f64 - ((float64_t)hole_y + 0.5f64) / (float64_t)mip.height;
    for (int32_t i = 0; i < 4; i++) {
        REQUIRE(world_edit_block_set(&rig.world->edit, i, 8, 8, BLOCK_ID_OAK_LEAVES) == WORLD_EDIT_STATUS_CHANGED);
    }
    REQUIRE(world_edit_block_set(&rig.world->edit, 4, 8, 8, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_features_set(&rig.render->frame, SHADE_FEATURE_SHAPES | SHADE_FEATURE_ALPHA);
    const uint32_t x = LS_WIDTH / 2u, y = LS_HEIGHT / 2u;
    render_camera_t near_eye, far_eye;
    render_camera_start(&near_eye, -20.5f64, eye_y, eye_z, 0.0f32, 0.0f32);
    render_camera_start(&far_eye, -100.5f64, eye_y, eye_z, 0.0f32, 0.0f32);
    near_eye.half_fov_tangent = 0.02f32;
    far_eye.half_fov_tangent = 0.02f32;
    const uint8_t *picture = sk_draw(t, &rig, &near_eye);
    REQUIRE(picture != nullptr);
    const uint32_t near_four = sk_pixel(picture, LS_WIDTH, x, y);
    picture = sk_draw(t, &rig, &far_eye);
    REQUIRE(picture != nullptr);
    const uint32_t far_four = sk_pixel(picture, LS_WIDTH, x, y);
    REQUIRE(world_edit_block_set(&rig.world->edit, 0, 8, 8, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(sk_hand_world_sync(t, &rig));
    picture = sk_draw(t, &rig, &far_eye);
    REQUIRE(picture != nullptr);
    const uint32_t far_three = sk_pixel(picture, LS_WIDTH, x, y);
    /* A side under the plain light of no sky (render_feature_shapes' 0.6), of the leaves' mean colour. */
    uint32_t mean_channel[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        mean_channel[i] =
            post_tone_map(shade_srgb_to_linear((float32_t)info.borrowed_entry->mean_rgba[i] / 255.0f32) * 0.6f32);
    }
    const uint32_t mean = post_xrgb(mean_channel[0], mean_channel[1], mean_channel[2]);
    printf("  four leaves: %06" PRIX32 " from 20 blocks, %06" PRIX32 " from 100 (the leaves' mean %06" PRIX32
           "); three: %06" PRIX32 " from 100\n",
           near_four, far_four, mean, far_three);
    CHECK(sk_bytes_near(far_four, mean, 1u));
    CHECK(sk_bytes_near(far_three, near_four, 1u));
    CHECK(!sk_bytes_near(near_four, mean, 4u));
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST_MAIN()
