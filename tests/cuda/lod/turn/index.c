/* ctest render_feature_lod_turn. See README.md. */
#include "harness/index.h"

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
    LT_WIDTH = 320u,
    LT_HEIGHT = 180u,
    LT_PIXELS = LT_WIDTH * LT_HEIGHT,
    LT_HAND_RADIUS = 4u, /* chunks of level 0 about the eye: past d_1, 64 blocks at E 2 and 180 rows */
    LT_STILL_FRAMES = 60u,
    LT_TURN_FRAMES = 10u, /* each way */
    /* A texel about a pixel of 180 rows of 70 degrees (0.0078 radians), as 1024 is about a pixel of 1080. */
    LT_SIDE = 256u,
};

#define LT_TURN_RADIANS 1.0471975512f32 /* 60 degrees */

#include "cuda/sky/fixtures/rig.h"

/* Rolling hills, the same in level 0 and in the far field's samples: a world z. */
static int32_t lt_top(int32_t x, int32_t y) {
    return 12 + (int32_t)floor(10.0f64 * sin((float64_t)x / 41.0f64) * cos((float64_t)y / 57.0f64) +
                               6.0f64 * sin((float64_t)(x + y) / 23.0f64) + 0.5f64);
}

static void lt_sample(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                      uint8_t *scratch) {
    (void)borrowed_context;
    (void)shift;
    (void)scratch;
    const int32_t top = lt_top(x, y) + (int32_t)WORLD_BUFFERS_Z_ORIGIN;
    for (int32_t z = 0; z < (int32_t)RENDER_LOD_HEIGHT_BLOCKS; z++) {
        out[z] = z == top ? (uint16_t)BLOCK_ID_GRASS_BLOCK : (z < top && z > top - 8 ? (uint16_t)BLOCK_ID_STONE : 0u);
    }
}

static bool lt_ground_put(test_context_t *t, sk_rig_t *rig, int32_t radius_blocks) {
    for (int32_t y = -radius_blocks; y < radius_blocks; y++) {
        for (int32_t x = -radius_blocks; x < radius_blocks; x++) {
            const int32_t top = lt_top(x, y);
            for (int32_t z = top - 7; z <= top; z++) {
                const world_edit_status_t status = world_edit_block_set(
                    &rig->world->edit, x, y, z, z == top ? (uint16_t)BLOCK_ID_GRASS_BLOCK : (uint16_t)BLOCK_ID_STONE);
                if (status == WORLD_EDIT_STATUS_NOT_LOADED) {
                    continue;
                }
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return sk_hand_world_sync(t, rig);
}

static bool lt_settle(test_context_t *t, sk_rig_t *rig, const float64_t position[3]) {
    render_lod_t *const lod = rig->render->borrowed_lod;
    for (uint64_t pass = 0u; pass < 100000000ull; pass++) {
        if (!CHECK_EQ_U64(render_lod_frame(lod, position), GPU_STATUS_OK)) {
            return false;
        }
        if (render_lod_idle(lod)) {
            /* A revision put off for columns sent lands within RENDER_LOD_TURN_REVISE_FRAMES frames: before the test, not in it. */
            for (uint32_t frame = 0u; frame <= (uint32_t)RENDER_LOD_TURN_REVISE_FRAMES; frame++) {
                if (!CHECK_EQ_U64(render_lod_frame(lod, position), GPU_STATUS_OK)) {
                    return false;
                }
            }
            return true;
        }
        time_sleep_ns(20000);
    }
    return CHECK(false);
}

/* The hand world and its far field, settled about the eye, with a turn cache of side texels a face (0 for none). */
static bool lt_open(test_context_t *t, sk_rig_t *rig, uint32_t side, const render_camera_t *eye) {
    if (!sk_hand_world_open(t, rig, LT_HAND_RADIUS, LT_WIDTH, LT_HEIGHT, 20000.0f32) || !sk_hand_world_sync(t, rig) ||
        !lt_ground_put(t, rig, (int32_t)(LT_HAND_RADIUS + 2u) * 16)) {
        return false;
    }
    const render_lod_plan_t plan = {.shifts = {1u, 2u, 3u}, .radii = {6u, 4u, 3u}, .level_count = 3u};
    if (!CHECK_EQ_U64(render_open_lod(rig->render, &plan, 0u, lt_sample, nullptr), GPU_STATUS_OK)) {
        return false;
    }
    rig->render->borrowed_lod->turn.side = side;
    render_frame_time_set(&rig->render->frame, 0.38f32);
    return lt_settle(t, rig, eye->position);
}

static void lt_close(sk_rig_t *rig) {
    render_close_lod(rig->render);
    sk_hand_world_close(rig);
}

static uint64_t lt_hash(const uint8_t *bytes, uint64_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < count; i++) {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }
    return hash;
}

static uint64_t lt_far_count(test_context_t *t, sk_rig_t *rig) {
    const uint64_t bytes = lod_hits_bytes(LT_WIDTH, LT_HEIGHT);
    lod_hit_t *const hits = test_alloc(t, bytes);
    CHECK_EQ_U64(device_memory_read(&rig->bridge->device_memory, &rig->render->owned_lod_hits, 0u, hits, bytes),
                 GPU_STATUS_OK);
    uint64_t count = 0u;
    for (uint64_t i = 0u; i < (uint64_t)LT_PIXELS; i++) {
        count += hits[i].material != LOD_NO_MATERIAL ? 1u : 0u;
    }
    return count;
}

static lod_turn_tally_t lt_tally(test_context_t *t, sk_rig_t *rig) {
    lod_turn_tally_t tally;
    CHECK_EQ_U64(render_lod_turn_tally(rig->render->borrowed_lod, &tally), GPU_STATUS_OK);
    return tally;
}

/* One frame, as the game draws it: the far field follows the eye, then the picture. */
static const uint8_t *lt_draw(test_context_t *t, sk_rig_t *rig, const render_camera_t *eye) {
    if (!CHECK_EQ_U64(render_lod_frame(rig->render->borrowed_lod, eye->position), GPU_STATUS_OK)) {
        return nullptr;
    }
    return sk_draw(t, rig, eye);
}

static void lt_eye(render_camera_t *eye) {
    render_camera_start(eye, 0.5f64, 0.5f64, (float64_t)lt_top(0, 0) + 6.5f64, 0.3f32, -0.2f32);
}

static const uint32_t LT_STILL = SHADE_FEATURES_ALL & ~(uint32_t)(SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_FOG |
                                                                  SHADE_FEATURE_WEATHER | SHADE_FEATURE_GOD_RAYS);

/* 60 frames at a fixed eye with TAA, so jittered, with the cache on at the game's side and with it off: every picture the same bytes, as a
 * frame that is not a turn traces every far ray; and the cache was written all the same. */
TEST(a_still_eye_draws_the_same_far_field) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    render_camera_t eye;
    lt_eye(&eye);
    uint64_t hashes[2][LT_STILL_FRAMES];
    lod_turn_tally_t tally = {0u, 0u};
    uint64_t far = 0u;
    for (uint32_t run = 0u; run < 2u; run++) {
        REQUIRE(lt_open(t, &rig, RENDER_LOD_TURN_CACHE_SIDE, &eye));
        const uint32_t features = LT_STILL | SHADE_FEATURE_TAA | (run == 0u ? SHADE_FEATURE_LOD_TURN_CACHE : 0u);
        render_frame_features_set(&rig.render->frame, features);
        for (uint32_t frame = 0u; frame < LT_STILL_FRAMES; frame++) {
            const uint8_t *const picture = lt_draw(t, &rig, &eye);
            REQUIRE(picture != nullptr);
            hashes[run][frame] = lt_hash(picture, post_xrgb_bytes(LT_WIDTH, LT_HEIGHT));
        }
        if (run == 0u) {
            tally = lt_tally(t, &rig);
            far = lt_far_count(t, &rig);
        }
        lt_close(&rig);
    }
    uint32_t differ = 0u;
    for (uint32_t frame = 0u; frame < LT_STILL_FRAMES; frame++) {
        differ += hashes[0][frame] != hashes[1][frame] ? 1u : 0u;
    }
    printf("  %" PRIu64 " far pixels with ground a frame; cache: %" PRIu64 " traced, %" PRIu64 " read; %" PRIu32
           " of %" PRIu32 " pictures differ\n",
           far, tally.traced, tally.read, differ, (uint32_t)LT_STILL_FRAMES);
    CHECK(far > 0u);
    CHECK(tally.traced > (uint64_t)LT_PIXELS / 10u * LT_STILL_FRAMES);
    CHECK_EQ_U64(differ, 0u);
    CHECK_EQ_U64(tally.read, 0u);
    sk_rig_end(t, &rig);
}

/* The device memory the game's cache takes, beside the far field's levels: the line it has in render_lod's budget. */
TEST(the_cache_takes_what_its_side_says_from_the_voxels) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    render_camera_t eye;
    lt_eye(&eye);
    REQUIRE(lt_open(t, &rig, 0u, &eye));
    const device_memory_pool_t *const voxels = &rig.bridge->device_memory.pools[DEVICE_POOL_VOXELS];
    const uint64_t before = voxels->live_bytes;
    render_lod_t *const lod = rig.render->borrowed_lod;
    lod->turn.side = RENDER_LOD_TURN_CACHE_SIDE;
    REQUIRE(lt_draw(t, &rig, &eye) != nullptr);
    const uint64_t taken = voxels->live_bytes - before;
    printf("  side %" PRIu32 ": %.1f MiB, beside %.1f MiB of the voxels' pool before it, of %.0f\n",
           (uint32_t)RENDER_LOD_TURN_CACHE_SIDE, (libc_double_t)((float64_t)taken / 1048576.0f64),
           (libc_double_t)((float64_t)before / 1048576.0f64),
           (libc_double_t)((float64_t)voxels->limit_bytes / 1048576.0f64));
    CHECK_EQ_U64(taken, lod_turn_cache_bytes(RENDER_LOD_TURN_CACHE_SIDE));
    CHECK(lod->turn.borrowed_block != nullptr);
    render_close_lod(rig.render);
    /* Closed, the far field gives it back with the rest. */
    CHECK(voxels->live_bytes + lod_turn_cache_bytes(RENDER_LOD_TURN_CACHE_SIDE) <= before + taken);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* Turn 60 degrees right and back over 20 frames at a fixed eye: on the way back, the far rays traced are under 5 % of the way out's. Each
 * picture of the way back with the cache is held to the same camera's traced again with it off: the far field's answers a texel apart. */
TEST(a_turn_back_traces_nothing_far) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    render_camera_t eye;
    lt_eye(&eye);
    REQUIRE(lt_open(t, &rig, LT_SIDE, &eye));
    const uint32_t on = LT_STILL | SHADE_FEATURE_LOD_TURN_CACHE;
    render_frame_features_set(&rig.render->frame, on);
    const float32_t start = eye.yaw;
    REQUIRE(lt_draw(t, &rig, &eye) != nullptr);
    const lod_turn_tally_t begun = lt_tally(t, &rig);
    for (uint32_t frame = 1u; frame <= LT_TURN_FRAMES; frame++) {
        eye.yaw = start - LT_TURN_RADIANS * (float32_t)frame / (float32_t)LT_TURN_FRAMES;
        REQUIRE(lt_draw(t, &rig, &eye) != nullptr);
    }
    const lod_turn_tally_t out = lt_tally(t, &rig);
    uint64_t wrong = 0u, looked = 0u;
    uint8_t *const cached = test_alloc(t, post_xrgb_bytes(LT_WIDTH, LT_HEIGHT));
    for (uint32_t frame = LT_TURN_FRAMES; frame-- > 0u;) {
        eye.yaw = start - LT_TURN_RADIANS * (float32_t)frame / (float32_t)LT_TURN_FRAMES;
        const lod_turn_tally_t before = lt_tally(t, &rig);
        render_frame_features_set(&rig.render->frame, on);
        const uint8_t *picture = lt_draw(t, &rig, &eye);
        REQUIRE(picture != nullptr);
        memcpy(cached, picture, post_xrgb_bytes(LT_WIDTH, LT_HEIGHT));
        const lod_turn_tally_t after = lt_tally(t, &rig);
        CHECK(after.read > before.read);
        render_frame_features_set(&rig.render->frame, LT_STILL);
        picture = lt_draw(t, &rig, &eye);
        REQUIRE(picture != nullptr);
        for (uint64_t i = 0u; i < (uint64_t)LT_PIXELS; i++) {
            looked++;
            wrong += sk_bytes_near(sk_pixel(cached, LT_WIDTH, (uint32_t)(i % LT_WIDTH), (uint32_t)(i / LT_WIDTH)),
                                   sk_pixel(picture, LT_WIDTH, (uint32_t)(i % LT_WIDTH), (uint32_t)(i / LT_WIDTH)), 24u)
                         ? 0u
                         : 1u;
        }
    }
    const lod_turn_tally_t back = lt_tally(t, &rig);
    const uint64_t out_traced = out.traced - begun.traced, back_traced = back.traced - out.traced;
    const uint64_t back_read = back.read - out.read;
    printf("  way out: %" PRIu64 " far rays traced, %" PRIu64 " read; way back: %" PRIu64 " traced, %" PRIu64
           " read; %.2f %% of the way back's pixels off by more than 24 from the same camera traced\n",
           out_traced, out.read - begun.read, back_traced, back_read,
           (libc_double_t)(100.0f64 * (float64_t)wrong / (float64_t)looked));
    CHECK(out_traced > 0u);
    CHECK(back_traced * 20u < out_traced);
    CHECK(wrong * 50u < looked);
    lt_close(&rig);
    sk_rig_end(t, &rig);
}

/* After a turn has filled the cache: a turn back from an eye 0.3 blocks off reads no texel, nor does one after the far field's revision moved
 * on; and one from the same eye reads. */
TEST(moving_a_quarter_block_or_a_far_change_retraces) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    render_camera_t eye;
    lt_eye(&eye);
    REQUIRE(lt_open(t, &rig, LT_SIDE, &eye));
    render_frame_features_set(&rig.render->frame, LT_STILL | SHADE_FEATURE_LOD_TURN_CACHE);
    const float32_t start = eye.yaw;
    const float32_t step = 0.05f32;
    for (uint32_t kind = 0u; kind < 3u; kind++) {
        /* Fill: a little turn both ways from the start, from the same eye. */
        for (uint32_t frame = 0u; frame < 4u; frame++) {
            eye.yaw = start + step * (float32_t)((frame & 1u) != 0u ? 1 : -1);
            REQUIRE(lt_draw(t, &rig, &eye) != nullptr);
        }
        const float64_t home = eye.position[0];
        if (kind == 1u) {
            eye.position[0] = home + 0.3f64;
        } else if (kind == 2u) {
            rig.render->borrowed_lod->revision++;
        }
        const lod_turn_tally_t before = lt_tally(t, &rig);
        eye.yaw = start;
        REQUIRE(sk_draw(t, &rig, &eye) != nullptr); /* no render_lod_frame, which would work the revision out again */
        const lod_turn_tally_t after = lt_tally(t, &rig);
        static const char *const names[3] = {"the same eye", "0.3 blocks off", "a far change"};
        printf("  %s: %" PRIu64 " traced, %" PRIu64 " read\n", names[kind], after.traced - before.traced,
               after.read - before.read);
        CHECK(after.traced + after.read > before.traced + before.read);
        if (kind == 0u) {
            CHECK(after.read > before.read);
        } else {
            CHECK_EQ_U64(after.read, before.read);
        }
        eye.position[0] = home;
    }
    lt_close(&rig);
    sk_rig_end(t, &rig);
}

TEST_MAIN()
