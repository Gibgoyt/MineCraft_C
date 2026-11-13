/* ctest render_feature_ao. See README.md. */
#include "harness/index.h"

#include "game/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    AO_WIDTH = 320u,
    AO_HEIGHT = 180u,
    AO_PIXELS = AO_WIDTH * AO_HEIGHT,
    AO_STILL_FRAMES = 20u,
    AO_AWAY_FRAMES = 10u,
    /* Long enough for every pixel's AO to be the mean of all it may be. */
    AO_CONVERGED_FRAMES = 200u,
};

/* The light is then each face's one brightness and nothing else, so that what a picture shows of AO is AO. */
#define AO_FEATURES                                                                                                    \
    ((SHADE_FEATURES_ALL & ~(uint32_t)(SHADE_FEATURE_SKY | SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW)) |                \
     (uint32_t)SHADE_FEATURE_AO)
#define AO_FEATURES_GUESS (AO_FEATURES | (uint32_t)SHADE_FEATURE_AO_FIRST_GUESS)
#define AO_NEAR 0.001f32
#define AO_TURN_RADIANS 0.5235988f32 /* 30 degrees */
#define AO_HALF_TURN_RADIANS 3.1415927f32

#include "cuda/sky/fixtures/rig.h"

/* ---- On paper, on the host, the same lines as the card ----------------------------------------------------------------------- */

TEST(corner_ao_in_an_open_field_is_one) {
    for (uint32_t j = 0u; j <= 4u; j++) {
        for (uint32_t i = 0u; i <= 4u; i++) {
            CHECK_NEAR(shade_ao_corner(0u, (float32_t)i / 4.0f32, (float32_t)j / 4.0f32), 1.0f32, AO_NEAR);
        }
    }
}

/* A wall along the face's high v edge: its side cell and both diagonals there are solid, so each corner on that edge is 1 - 2/3. With the
 * floor at 0.25 that is 0.5 at the wall, 1 at the far edge and 0.75 between. The plan's paper had 0.8, counting the side cell alone and a
 * floor of 0.4; ray AO says 0.53 at the wall and 0.69 between (the phase 04 file). */
TEST(corner_ao_beside_one_wall_is_half_at_that_edge) {
    const uint32_t wall = SHADE_AO_CELL_V_HIGH | SHADE_AO_CELL_LOW_HIGH | SHADE_AO_CELL_HIGH_HIGH;
    for (uint32_t i = 0u; i <= 4u; i++) {
        const float32_t u = (float32_t)i / 4.0f32;
        CHECK_NEAR(shade_ao_corner(wall, u, 1.0f32), 0.5f32, AO_NEAR);
        CHECK_NEAR(shade_ao_corner(wall, u, 0.0f32), 1.0f32, AO_NEAR);
        CHECK_NEAR(shade_ao_corner(wall, u, 0.5f32), 0.75f32, AO_NEAR);
    }
    CHECK_NEAR(SHADE_AO_CORNER_FLOOR, 0.25f32, AO_NEAR);
}

TEST(corner_ao_in_a_two_wall_corner_is_the_floor) {
    const uint32_t walls = SHADE_AO_CELL_U_LOW | SHADE_AO_CELL_LOW_LOW | SHADE_AO_CELL_LOW_HIGH | SHADE_AO_CELL_V_LOW |
                           SHADE_AO_CELL_HIGH_LOW;
    CHECK_NEAR(shade_ao_corner(walls, 0.0f32, 0.0f32), SHADE_AO_CORNER_FLOOR, AO_NEAR);
    /* Along either wall, away from the corner, as beside one wall; the far corner open. */
    CHECK_NEAR(shade_ao_corner(walls, 1.0f32, 0.0f32), 0.5f32, AO_NEAR);
    CHECK_NEAR(shade_ao_corner(walls, 0.0f32, 1.0f32), 0.5f32, AO_NEAR);
    CHECK_NEAR(shade_ao_corner(walls, 1.0f32, 1.0f32), 1.0f32, AO_NEAR);
    /* A single block at the diagonal alone darkens its corner to 2/3 of the way. */
    CHECK_NEAR(shade_ao_corner(SHADE_AO_CELL_HIGH_HIGH, 1.0f32, 1.0f32), 0.25f32 + 0.75f32 * (2.0f32 / 3.0f32),
               AO_NEAR);
}

TEST(a_cache_entry_round_trips) {
    const shade_ao_cache_entry_t entry = {SHADE_AO_CACHE_AGE_MASK - 3u, 0xBEEFu, 40000u, 31u, 5u, {0u, 0u}};
    shade_ao_cache_entry_t back;
    shade_ao_cache_unpack(shade_ao_cache_pack(&entry), &back);
    CHECK_MEM_EQ(&back, &entry, sizeof(entry));
    /* And from a pixel's mean: to the nearest 1/65535, and its samples as they were. */
    const int32_t cell[3] = {-1234567, 7654321, 300};
    const uint64_t key = shade_ao_cache_key(cell, 4u, 9u, 77u, 3u);
    float32_t visible = 0.0f32;
    uint32_t samples = 0u;
    CHECK(shade_ao_cache_hit(shade_ao_cache_word(key, 4u, 1000u, 0.625f32, 6u), key, 4u, 1000u, &visible, &samples));
    CHECK_NEAR(visible, 0.625f32, 1.0f32 / 65535.0f32);
    CHECK_EQ_U64(samples, 6u);
    /* Past the cap, a pixel takes SHADE_AO_CACHE_SAMPLES_CAP, so that its own rays still move it. */
    CHECK(shade_ao_cache_hit(shade_ao_cache_word(key, 4u, 1000u, 0.625f32, 32u), key, 4u, 1000u, &visible, &samples));
    CHECK_EQ_U64(samples, SHADE_AO_CACHE_SAMPLES_CAP);
    /* Another face of the same key is a miss, and an entry never written is one. */
    CHECK(!shade_ao_cache_hit(shade_ao_cache_word(key, 4u, 1000u, 0.625f32, 6u), key, 5u, 1000u, &visible, &samples));
    CHECK(!shade_ao_cache_hit(0u, key, 4u, 1000u, &visible, &samples));
}

TEST(a_tag_mismatch_is_a_miss) {
    const int32_t first[3] = {10, 20, 300};
    const uint64_t key = shade_ao_cache_key(first, 4u, 0u, 1u, 0u);
    /* Another place with the same slot and another tag. */
    uint64_t other = 0u;
    bool found = false;
    for (int32_t x = 0; x < 4000000 && !found; x++) {
        const int32_t cell[3] = {x, 99, 301};
        other = shade_ao_cache_key(cell, 4u, 0u, 1u, 0u);
        found = shade_ao_cache_slot(other) == shade_ao_cache_slot(key) &&
                shade_ao_cache_tag(other) != shade_ao_cache_tag(key);
    }
    REQUIRE(found);
    float32_t visible = 0.0f32;
    uint32_t samples = 0u;
    const uint64_t word = shade_ao_cache_word(key, 4u, 50u, 0.5f32, 9u);
    CHECK(shade_ao_cache_hit(word, key, 4u, 50u, &visible, &samples));
    CHECK(!shade_ao_cache_hit(word, other, 4u, 50u, &visible, &samples));
}

TEST(an_old_entry_is_a_miss) {
    const int32_t cell[3] = {1, 2, 3};
    const uint64_t key = shade_ao_cache_key(cell, 0u, 15u, 0u, 0u);
    float32_t visible = 0.0f32;
    uint32_t samples = 0u;
    const uint32_t written = SHADE_AO_CACHE_AGE_MASK - 100u; /* across the wrap of the frame count, too */
    const uint64_t word = shade_ao_cache_word(key, 0u, written, 0.3f32, 4u);
    CHECK_EQ_U64(SHADE_AO_CACHE_AGE, 240u);
    CHECK(shade_ao_cache_hit(word, key, 0u, (written + 239u) & SHADE_AO_CACHE_AGE_MASK, &visible, &samples));
    CHECK(!shade_ao_cache_hit(word, key, 0u, (written + 240u) & SHADE_AO_CACHE_AGE_MASK, &visible, &samples));
    CHECK(!shade_ao_cache_hit(word, key, 0u, (written + 241u) & SHADE_AO_CACHE_AGE_MASK, &visible, &samples));
}

TEST(an_edit_breaks_the_key) {
    const int32_t cell[3] = {-5, 6, 260};
    const uint64_t key = shade_ao_cache_key(cell, 4u, 5u, 9u, 0u);
    CHECK(shade_ao_cache_key(cell, 4u, 5u, 9u, 1u) != key);      /* an edit in front of it */
    CHECK(shade_ao_cache_key(cell, 4u, 5u, 10u, 0u) != key);     /* a new load of the column */
    CHECK(shade_ao_cache_key(cell, 4u, 6u, 9u, 0u) != key);      /* the next square */
    CHECK(shade_ao_cache_key(cell, 5u, 5u, 9u, 0u) != key);      /* the next face */
    CHECK_EQ_U64(shade_ao_cache_key(cell, 4u, 5u, 9u, 0u), key); /* and the same is the same */
    /* The sections' revisions combine in order: the same ones in another order, or one changed, are another. */
    CHECK(shade_ao_revision_add(shade_ao_revision_add(0u, 7u), 9u) !=
          shade_ao_revision_add(shade_ao_revision_add(0u, 9u), 7u));
    CHECK(shade_ao_revision_add(shade_ao_revision_add(0u, 7u), 9u) !=
          shade_ao_revision_add(shade_ao_revision_add(0u, 7u), 10u));
}

/* The card writes by atomicMax: the newest wins over any number of samples, and of one frame the one of most samples, so that what is
 * there after a frame is the same whatever order its pixels came in. */
TEST(the_newest_entry_wins_and_then_the_most_samples) {
    const int32_t cell[3] = {7, 7, 300};
    const uint64_t key = shade_ao_cache_key(cell, 4u, 0u, 1u, 0u);
    CHECK(shade_ao_cache_word(key, 4u, 101u, 0.9f32, 1u) > shade_ao_cache_word(key, 4u, 100u, 0.1f32, 32u));
    CHECK(shade_ao_cache_word(key, 4u, 100u, 0.1f32, 9u) > shade_ao_cache_word(key, 4u, 100u, 0.9f32, 8u));
}

TEST(the_turn_at_the_centre_is_in_pixels) {
    render_camera_t camera;
    render_camera_start(&camera, 0.5f64, 0.5f64, 300.5f64, 0.0f32, 0.0f32);
    traverse_camera_t before, after;
    render_camera_to_traverse(&camera, 1920u, 1080u, &before);
    CHECK_NEAR((float32_t)post_turn_pixels(&before, &before), 0.0f32, 1e-6f32);
    /* One pixel at the centre of 1080 rows is atan(2 tan(fov / 2) / 1080). */
    const float64_t up =
        sqrt((float64_t)(before.up[0] * before.up[0] + before.up[1] * before.up[1] + before.up[2] * before.up[2]));
    const float64_t pixel = atan(2.0f64 * up / 1080.0f64);
    render_camera_start(&camera, 0.5f64, 0.5f64, 300.5f64, (float32_t)(3.0f64 * pixel), 0.0f32);
    render_camera_to_traverse(&camera, 1920u, 1080u, &after);
    CHECK_NEAR((float32_t)post_turn_pixels(&before, &after), 3.0f32, 0.01f32);
    CHECK_NEAR(POST_TAA_FAST_PIXELS, 2.0f32, 1e-6f32);
}

/* ---- On the card ------------------------------------------------------------------------------------------------------------- */

static bool ao_box(test_context_t *t, sk_rig_t *rig, const int32_t low[3], const int32_t high[3], uint32_t state) {
    for (int32_t z = low[2]; z < high[2]; z++) {
        for (int32_t y = low[1]; y < high[1]; y++) {
            for (int32_t x = low[0]; x < high[0]; x++) {
                const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, (uint16_t)state);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* plan 008 phase 04's room: a floor, a wall along its east side, a pillar, a low step and a slab over nothing; corners of all kinds and
 * a lot of open floor, as render_feature_post's AO tests have. */
static bool ao_room_put(test_context_t *t, sk_rig_t *rig) {
    static const int32_t boxes[][6] = {
        {0, 0, -2, 32, 32, -1}, /* the floor */
        {22, 0, -1, 23, 32, 7}, /* the wall */
        {6, 8, -1, 8, 10, 4},   /* the pillar */
        {12, 20, 1, 18, 26, 2}, /* the slab */
        {13, 3, -1, 16, 6, 0},  /* a low step */
    };
    for (uint32_t b = 0u; b < sizeof(boxes) / sizeof(boxes[0]); b++) {
        if (!ao_box(t, rig, &boxes[b][0], &boxes[b][3], BLOCK_ID_STONE)) {
            return false;
        }
    }
    return sk_hand_world_sync(t, rig);
}

static void ao_camera(render_camera_t *camera, float32_t turn) {
    render_camera_start(camera, 3.3f64, 18.7f64, 9.2f64, -0.25f32 + turn, -0.55f32);
}

static bool ao_draw(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, uint32_t frames,
                    const uint8_t **out_picture) {
    const uint8_t *picture = nullptr;
    for (uint32_t i = 0u; i < frames; i++) {
        picture = sk_draw(t, rig, camera);
        if (picture == nullptr) {
            return false;
        }
    }
    if (out_picture != nullptr) {
        *out_picture = picture;
    }
    return true;
}

static bool ao_open(test_context_t *t, sk_rig_t *rig, uint32_t features) {
    if (!sk_rig_begin(t, rig) || !sk_hand_world_open(t, rig, 2u, AO_WIDTH, AO_HEIGHT, 100000.0f32) ||
        !ao_room_put(t, rig)) {
        return false;
    }
    render_frame_features_set(&rig->render->frame, features);
    return CHECK(rig->render->frame.temporal.borrowed_ao_cache != nullptr);
}

static void ao_close(test_context_t *t, sk_rig_t *rig) {
    sk_hand_world_close(rig);
    sk_rig_end(t, rig);
}

static bool ao_read(test_context_t *t, sk_rig_t *rig, shade_ao_pixel_t *out) {
    return CHECK_EQ_U64(device_memory_read(&rig->bridge->device_memory, rig->render->frame.temporal.borrowed_ao_last,
                                           0u, out, (uint64_t)AO_PIXELS * sizeof(shade_ao_pixel_t)),
                        GPU_STATUS_OK);
}

/* Still for a while, turned 30 degrees left for a while, then back: the strip on the right was off the picture while turned, so screen
 * space history has forgotten it, and it comes back fresh. */
static bool ao_strip_run(test_context_t *t, uint32_t features, shade_ao_pixel_t *out) {
    sk_rig_t rig;
    if (!ao_open(t, &rig, features)) {
        return false;
    }
    render_camera_t camera;
    ao_camera(&camera, 0.0f32);
    bool ok = ao_draw(t, &rig, &camera, AO_STILL_FRAMES, nullptr);
    ao_camera(&camera, AO_TURN_RADIANS);
    ok = ok && ao_draw(t, &rig, &camera, AO_AWAY_FRAMES, nullptr);
    ao_camera(&camera, 0.0f32);
    ok = ok && ao_draw(t, &rig, &camera, 1u, nullptr) && ao_read(t, &rig, out);
    ao_close(t, &rig);
    return ok;
}

TEST(a_strip_that_comes_back_hits_the_cache) {
    shade_ao_pixel_t *const before = test_alloc(t, (uint64_t)AO_PIXELS * sizeof(shade_ao_pixel_t));
    shade_ao_pixel_t *const after = test_alloc(t, (uint64_t)AO_PIXELS * sizeof(shade_ao_pixel_t));
    /* Without the first guess a fresh pixel is one with exactly SHADE_AO_FRESH_RAYS samples: any other has been on the picture longer. */
    REQUIRE(ao_strip_run(t, AO_FEATURES, before));
    REQUIRE(ao_strip_run(t, AO_FEATURES_GUESS, after));
    uint64_t fresh = 0u, hits = 0u, corner = 0u, same_rest = 0u, rest = 0u;
    for (uint32_t i = 0u; i < AO_PIXELS; i++) {
        if (before[i].samples == SHADE_AO_FRESH_RAYS) {
            fresh++;
            /* The same pixels are fresh with it: what reprojection finds does not depend on how a fresh pixel was filled. */
            if (after[i].samples > 1u && after[i].samples <= SHADE_AO_CACHE_SAMPLES_CAP) {
                hits++;
            } else if (after[i].samples == 1u) {
                corner++;
            }
        } else if (before[i].samples != 0u) {
            rest++;
            same_rest += after[i].samples == before[i].samples ? 1u : 0u;
        }
    }
    printf("  the strip back: %" PRIu64 " fresh pixels, %" PRIu64 " from the cache, %" PRIu64
           " from the corner estimate; %" PRIu64 " of %" PRIu64 " others with the same samples\n",
           fresh, hits, corner, same_rest, rest);
    CHECK(fresh > AO_PIXELS / 20u);
    CHECK(hits * 10u >= fresh * 9u);
    CHECK_EQ_U64(hits + corner, fresh);
}

/* Looking at a floor, then away; a block is put on the floor, at a chunk's border so that its neighbours are in another column; then back.
 * The floor around it must not take its AO from before the block. */
TEST(a_block_placed_is_not_shown_with_old_ao) {
    sk_rig_t rig;
    REQUIRE(ao_open(t, &rig, AO_FEATURES_GUESS));
    render_camera_t camera;
    ao_camera(&camera, 0.0f32);
    REQUIRE(ao_draw(t, &rig, &camera, AO_STILL_FRAMES, nullptr));
    ao_camera(&camera, AO_HALF_TURN_RADIANS);
    REQUIRE(ao_draw(t, &rig, &camera, AO_AWAY_FRAMES, nullptr));
    const int32_t block[3] = {10, 16, -1};
    const int32_t block_high[3] = {11, 17, 0};
    REQUIRE(ao_box(t, &rig, block, block_high, BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &rig));
    ao_camera(&camera, 0.0f32);
    REQUIRE(ao_draw(t, &rig, &camera, 1u, nullptr));
    shade_ao_pixel_t *const ao = test_alloc(t, (uint64_t)AO_PIXELS * sizeof(shade_ao_pixel_t));
    traverse_hit_t *const hits = test_alloc(t, traverse_hits_bytes(AO_WIDTH, AO_HEIGHT));
    REQUIRE(ao_read(t, &rig, ao));
    REQUIRE(device_memory_read(&rig.bridge->device_memory, rig.render->frame.targets.borrowed_hits, 0u, hits,
                               traverse_hits_bytes(AO_WIDTH, AO_HEIGHT)) == GPU_STATUS_OK);
    traverse_camera_t view;
    render_camera_to_traverse(&camera, AO_WIDTH, AO_HEIGHT, &view);
    uint64_t beside = 0u, beside_cached = 0u, beside_dark = 0u, others = 0u, others_cached = 0u;
    float64_t beside_sum = 0.0f64;
    for (uint32_t i = 0u; i < AO_PIXELS; i++) {
        const uint32_t face = (uint32_t)hits[i].info & TRAVERSE_INFO_FACE_MASK;
        const int32_t x = view.eye_cell[0] + hits[i].offset[0], y = view.eye_cell[1] + hits[i].offset[1];
        const int32_t z = view.eye_cell[2] + hits[i].offset[2];
        if (face != TRAVERSE_FACE_POS_Z || z != -2 || ao[i].samples == 0u) {
            continue;
        }
        const int32_t dx = abs(x - block[0]), dy = abs(y - block[1]);
        if (dx <= 1 && dy <= 1 && (dx | dy) != 0) {
            beside++;
            beside_cached += ao[i].samples > 1u ? 1u : 0u;
            beside_dark += ao[i].visible < 0.99f32 ? 1u : 0u;
            beside_sum += (float64_t)ao[i].visible;
        } else {
            others++;
            others_cached += ao[i].samples > 1u ? 1u : 0u;
        }
    }
    /* A key holds the revisions of the sections within 4 blocks of a face's open cell, so the floor of the block's own column and the near
     * edge of the next misses too, and the floor further away still finds what it had. */
    const float64_t beside_mean = beside != 0u ? beside_sum / (float64_t)beside : 1.0f64;
    printf("  beside the block: %" PRIu64 " floor pixels, %" PRIu64 " from the cache, %" PRIu64
           " darkened, mean %.3f; the rest of the floor %" PRIu64 " of %" PRIu64 " from the cache\n",
           beside, beside_cached, beside_dark, (libc_double_t)beside_mean, others_cached, others);
    CHECK(beside > 20u);
    CHECK_EQ_U64(beside_cached, 0u);
    /* The corner estimate darkens the two corners by the block of a side neighbour to 0.75 and one of a diagonal one, and leaves the far ones
     * at 1: on paper, a mean of 0.875 over a side neighbour and 0.9375 over a diagonal one, 0.906 over the eight. Open floor, what the cache
     * held from before, is 1. */
    CHECK(beside_dark * 4u > beside * 3u);
    CHECK(beside_mean < 0.94f64);
    CHECK(others_cached > 1000u && others_cached < others);
    ao_close(t, &rig);
}

/* The first guess is for a camera that moves: with it still, every frame is the same to the bit with the switch on and off. */
TEST(a_still_camera_converges_to_todays_ao) {
    const uint64_t bytes = post_xrgb_bytes(AO_WIDTH, AO_HEIGHT);
    uint8_t *const off = test_alloc(t, bytes);
    const uint32_t settings[2] = {AO_FEATURES | (uint32_t)SHADE_FEATURE_TAA,
                                  AO_FEATURES_GUESS | (uint32_t)SHADE_FEATURE_TAA};
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        sk_rig_t rig;
        REQUIRE(ao_open(t, &rig, settings[pass]));
        render_camera_t camera;
        ao_camera(&camera, 0.0f32);
        const uint8_t *picture = nullptr;
        REQUIRE(ao_draw(t, &rig, &camera, AO_CONVERGED_FRAMES, &picture));
        if (pass == 0u) {
            memcpy(off, picture, bytes);
        } else {
            CHECK_EQ_U64((uint64_t)memcmp(off, picture, bytes), 0u);
            CHECK(sk_bytes_near(sk_pixel(off, AO_WIDTH, AO_WIDTH / 2u, AO_HEIGHT / 2u),
                                sk_pixel(picture, AO_WIDTH, AO_WIDTH / 2u, AO_HEIGHT / 2u), 0u));
        }
        ao_close(t, &rig);
    }
}

/* Turning, the first guess and the cache make the same picture from the same frames: nothing a frame reads was written in a way that hangs
 * on the order the card ran it in. */
TEST(the_same_turning_frames_give_the_same_picture_to_the_byte) {
    const uint64_t bytes = post_xrgb_bytes(AO_WIDTH, AO_HEIGHT);
    uint8_t *const first = test_alloc(t, bytes);
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        sk_rig_t rig;
        REQUIRE(ao_open(t, &rig, AO_FEATURES_GUESS | (uint32_t)SHADE_FEATURE_TAA));
        render_camera_t camera;
        const uint8_t *picture = nullptr;
        for (uint32_t i = 0u; i < 40u; i++) {
            /* Back and forth by 6 degrees a frame. */
            const float32_t swing = (float32_t)((i / 10u) % 2u == 0u ? i % 10u : 10u - i % 10u) * 0.1047f32;
            ao_camera(&camera, swing);
            REQUIRE(ao_draw(t, &rig, &camera, 1u, &picture));
        }
        if (pass == 0u) {
            memcpy(first, picture, bytes);
        } else {
            CHECK_EQ_U64((uint64_t)memcmp(first, picture, bytes), 0u);
        }
        ao_close(t, &rig);
    }
}

TEST_MAIN()
