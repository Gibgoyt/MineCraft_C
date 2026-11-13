/* ctest render_feature_resolution. See README.md. */
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
    RS_WIDTH = 320u,
    RS_HEIGHT = 180u,
    RS_PIXELS = RS_WIDTH * RS_HEIGHT,
    RS_SETTLE_FRAMES = 30u, /* the plan's: 30 at 100 %, 1 at 50 %, 30 at 100 % */
    RS_CONVERGED_FRAMES = 200u,
    RS_SCALE_FRAMES = 8u,
};

#define RS_FOG_END 100000.0f32
/* Every effect a game draws with but AO, whose noise is its own: what a size change does to it is its own test. */
#define RS_FEATURES ((SHADE_FEATURES_ALL & ~(uint32_t)(SHADE_FEATURE_AO | SHADE_FEATURE_LOD)))
#define RS_FEATURES_AO (RS_FEATURES | (uint32_t)SHADE_FEATURE_AO)
/* The light is each face's one brightness, each frame stands alone, and there is no sky: what the outline test compares is the outline. */
#define RS_PLAIN                                                                                                       \
    (SHADE_FEATURES_ALL & ~(uint32_t)(SHADE_FEATURE_SKY | SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW |                   \
                                      SHADE_FEATURE_LIGHT | SHADE_FEATURE_TAA | SHADE_FEATURE_AO | SHADE_FEATURE_LOD))

#include "cuda/sky/fixtures/rig.h"

/* ---- A room ------------------------------------------------------------------------------------------------------------------ */

static bool rs_box(test_context_t *t, sk_rig_t *rig, const int32_t low[3], const int32_t high[3], uint32_t state) {
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

/* render_feature_ao's room: a floor, a wall, a pillar, a slab over nothing and a low step, edges of every kind. */
static bool rs_room_put(test_context_t *t, sk_rig_t *rig) {
    static const int32_t boxes[][6] = {
        {0, 0, -2, 32, 32, -1}, {22, 0, -1, 23, 32, 7}, {6, 8, -1, 8, 10, 4},
        {12, 20, 1, 18, 26, 2}, {13, 3, -1, 16, 6, 0},
    };
    for (uint32_t b = 0u; b < sizeof(boxes) / sizeof(boxes[0]); b++) {
        if (!rs_box(t, rig, &boxes[b][0], &boxes[b][3], BLOCK_ID_STONE)) {
            return false;
        }
    }
    return sk_hand_world_sync(t, rig);
}

static bool rs_open(test_context_t *t, sk_rig_t *rig, uint32_t width, uint32_t height, uint32_t features, bool room) {
    if (!sk_rig_begin(t, rig) || !sk_hand_world_open(t, rig, 2u, width, height, RS_FOG_END) ||
        (room && !rs_room_put(t, rig))) {
        return false;
    }
    render_frame_features_set(&rig->render->frame, features);
    return true;
}

static void rs_close(test_context_t *t, sk_rig_t *rig) {
    sk_hand_world_close(rig);
    sk_rig_end(t, rig);
}

static void rs_camera(render_camera_t *camera) {
    render_camera_start(camera, 3.3f64, 18.7f64, 9.2f64, -0.25f32, -0.55f32);
}

/* frames pictures at percent, the last copied into out (RS_WIDTH by RS_HEIGHT, or nullptr). */
static bool rs_draw(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, uint32_t percent, uint32_t frames,
                    uint8_t *out) {
    render_frame_scale_set(&rig->render->frame, percent);
    const uint8_t *picture = nullptr;
    for (uint32_t i = 0u; i < frames; i++) {
        picture = sk_draw(t, rig, camera);
        if (picture == nullptr) {
            return false;
        }
    }
    if (out != nullptr) {
        memcpy(out, picture, post_xrgb_bytes(rig->render->frame.width, rig->render->frame.height));
    }
    return true;
}

/* The most any channel of any pixel differs between two pictures, and how many pixels differ at all. */
static uint32_t rs_most(const uint8_t *a, const uint8_t *b, uint64_t pixels, uint64_t *out_differ) {
    uint32_t most = 0u;
    uint64_t differ = 0u;
    for (uint64_t i = 0u; i < pixels; i++) {
        bool any = false;
        for (uint32_t c = 0u; c < 3u; c++) {
            const uint32_t gap = (uint32_t)abs((int32_t)a[i * 4u + c] - (int32_t)b[i * 4u + c]);
            most = gap > most ? gap : most;
            any = any || gap != 0u;
        }
        differ += any ? 1u : 0u;
    }
    if (out_differ != nullptr) {
        *out_differ = differ;
    }
    return most;
}

/* ---- On paper ---------------------------------------------------------------------------------------------------------------- */

TEST(a_rung_is_the_render_scale_s_size_and_the_pixel_under_is_the_nearest) {
    CHECK_EQ_U64(post_scaled_side(1920u, 100u), 1920u);
    CHECK_EQ_U64(post_scaled_side(1920u, 0u), 1920u);
    CHECK_EQ_U64(post_scaled_side(1920u, 85u), 1632u);
    CHECK_EQ_U64(post_scaled_side(1080u, 70u), 756u);
    CHECK_EQ_U64(post_scaled_side(1080u, 50u), 540u);
    CHECK_EQ_U64(post_scaled_side(1u, 25u), 1u);
    for (uint32_t x = 0u; x < 1920u; x++) {
        CHECK_EQ_U64(post_scaled_under(x, 1920u, 1920u), x);
        CHECK(post_scaled_under(x, 1920u, 960u) == x / 2u);
    }
    CHECK_EQ_U64(post_scaled_under(1919u, 1920u, 1344u), 1343u);
    /* The camera of a rung sees what the output's does: the same field of view and aspect, fewer pixels. */
    render_camera_t eye;
    render_camera_start(&eye, 0.5f64, 0.5f64, 300.5f64, 0.3f32, -0.2f32);
    traverse_camera_t whole, drawn;
    render_camera_to_traverse(&eye, 1920u, 1080u, &whole);
    render_camera_to_traverse_drawn(&eye, 1920u, 1080u, 1344u, 756u, &drawn);
    CHECK_EQ_U64(drawn.width, 1344u);
    CHECK_EQ_U64(drawn.height, 756u);
    CHECK_MEM_EQ(drawn.right, whole.right, sizeof(whole.right));
    CHECK_MEM_EQ(drawn.up, whole.up, sizeof(whole.up));
}

/* ---- On the card ------------------------------------------------------------------------------------------------------------- */

/* post_upscale alone, on blocks of a frame: a flat colour stays that colour to the bit, and a ramp across comes up as the ramp. */
TEST(the_upscale_of_a_flat_colour_is_that_colour) {
    sk_rig_t rig;
    REQUIRE(rs_open(t, &rig, RS_WIDTH, RS_HEIGHT, RS_FEATURES, false));
    device_memory_t *const memory = &rig.bridge->device_memory;
    const uint32_t inner_width = post_scaled_side(RS_WIDTH, 70u), inner_height = post_scaled_side(RS_HEIGHT, 70u);
    uint32_t *const inner = test_alloc(t, post_xrgb_bytes(inner_width, inner_height));
    uint32_t *const out = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    for (uint32_t i = 0u; i < inner_width * inner_height; i++) {
        inner[i] = 0x00A3417Fu;
    }
    REQUIRE(device_memory_write(memory, &rig.render->owned_hits, 0u, inner,
                                post_xrgb_bytes(inner_width, inner_height)) == GPU_STATUS_OK);
    gpu_error_t error = {0};
    REQUIRE(post_upscale(&error, &rig.render->owned_hits, inner_width, inner_height, &rig.render->owned_xrgb, RS_WIDTH,
                         RS_HEIGHT) == GPU_STATUS_OK);
    REQUIRE(device_memory_read(memory, &rig.render->owned_xrgb, 0u, out, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT)) ==
            GPU_STATUS_OK);
    uint64_t wrong = 0u;
    for (uint32_t i = 0u; i < RS_PIXELS; i++) {
        wrong += out[i] != 0x00A3417Fu ? 1u : 0u;
    }
    CHECK_EQ_U64(wrong, 0u);
    /* A ramp of red across: each output pixel is the ramp at its middle, within a byte's rounding. */
    for (uint32_t y = 0u; y < inner_height; y++) {
        for (uint32_t x = 0u; x < inner_width; x++) {
            inner[y * inner_width + x] = (x * 255u / (inner_width - 1u)) << 16;
        }
    }
    REQUIRE(device_memory_write(memory, &rig.render->owned_hits, 0u, inner,
                                post_xrgb_bytes(inner_width, inner_height)) == GPU_STATUS_OK);
    REQUIRE(post_upscale(&error, &rig.render->owned_hits, inner_width, inner_height, &rig.render->owned_xrgb, RS_WIDTH,
                         RS_HEIGHT) == GPU_STATUS_OK);
    REQUIRE(device_memory_read(memory, &rig.render->owned_xrgb, 0u, out, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT)) ==
            GPU_STATUS_OK);
    uint32_t most = 0u;
    for (uint32_t x = 0u; x < RS_WIDTH; x++) {
        float64_t place = ((float64_t)x + 0.5f64) * (float64_t)inner_width / (float64_t)RS_WIDTH - 0.5f64;
        place =
            place < 0.0f64 ? 0.0f64 : (place > (float64_t)(inner_width - 1u) ? (float64_t)(inner_width - 1u) : place);
        const float64_t low = floor(place);
        const uint32_t a = (uint32_t)low * 255u / (inner_width - 1u);
        const uint32_t b =
            ((uint32_t)low + 1u < inner_width ? (uint32_t)low + 1u : (uint32_t)low) * 255u / (inner_width - 1u);
        const float64_t want = (float64_t)a + ((float64_t)b - (float64_t)a) * (place - low);
        const uint32_t got = (out[(RS_HEIGHT / 2u) * RS_WIDTH + x] >> 16) & 0xFFu;
        const uint32_t gap = (uint32_t)fabs((float64_t)got - want + 0.0f64);
        most = gap > most ? gap : most;
    }
    CHECK(most <= 1u);
    /* What it refuses: a larger inner picture, nothing at all, and blocks too small. */
    CHECK_EQ_U64(post_upscale(&error, &rig.render->owned_hits, RS_WIDTH + 1u, RS_HEIGHT, &rig.render->owned_xrgb,
                              RS_WIDTH, RS_HEIGHT),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(
        post_upscale(&error, &rig.render->owned_hits, 0u, RS_HEIGHT, &rig.render->owned_xrgb, RS_WIDTH, RS_HEIGHT),
        GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(post_upscale(&error, &rig.render->owned_hits, inner_width, inner_height, &rig.render->owned_xrgb,
                              RS_WIDTH * 4u, RS_HEIGHT * 4u),
                 GPU_STATUS_INVALID_ARGUMENT);
    rs_close(t, &rig);
}

/* A picture of nothing but sky drawn at 70 % comes up the same as at 100 %, within a byte: the sky's colours change slowly across it. */
TEST(a_sky_drawn_at_seventy_comes_up_as_at_one_hundred) {
    sk_rig_t rig;
    REQUIRE(rs_open(t, &rig, RS_WIDTH, RS_HEIGHT, RS_FEATURES & ~(uint32_t)SHADE_FEATURE_TAA, false));
    render_camera_t eye;
    render_camera_start(&eye, 0.5f64, 0.5f64, 300.5f64, 1.5708f32,
                        0.1f32); /* north, a little up: away from the noon sun */
    uint8_t *const whole = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    uint8_t *const scaled = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    REQUIRE(rs_draw(t, &rig, &eye, 100u, 1u, whole));
    REQUIRE(rs_draw(t, &rig, &eye, 70u, 1u, scaled));
    uint64_t differ = 0u;
    const uint32_t most = rs_most(whole, scaled, RS_PIXELS, &differ);
    printf("  sky at 70 %% against 100 %%: %" PRIu64 " pixels differ, by at most %" PRIu32 "\n", differ, most);
    CHECK(most <= 1u);
    rs_close(t, &rig);
}

/* The history of TAA across a change of size: a still room, 30 frames at 100 %, 1 at 50 %, 30 at 100 %, against 61 at 100 %. No ghost of the
 * small picture, no smear. */
TEST(history_survives_a_size_change) {
    uint8_t *const stayed = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    uint8_t *const changed = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        sk_rig_t rig;
        REQUIRE(rs_open(t, &rig, RS_WIDTH, RS_HEIGHT, RS_FEATURES, true));
        render_camera_t eye;
        rs_camera(&eye);
        REQUIRE(rs_draw(t, &rig, &eye, 100u, RS_SETTLE_FRAMES, nullptr));
        REQUIRE(rs_draw(t, &rig, &eye, pass == 0u ? 100u : 50u, 1u, nullptr));
        REQUIRE(rs_draw(t, &rig, &eye, 100u, RS_SETTLE_FRAMES, pass == 0u ? stayed : changed));
        rs_close(t, &rig);
    }
    uint64_t differ = 0u;
    const uint32_t most = rs_most(stayed, changed, RS_PIXELS, &differ);
    printf("  30 at 100 %%, 1 at 50 %%, 30 at 100 %% against 61 at 100 %%: %" PRIu64
           " pixels differ, by at most %" PRIu32 "\n",
           differ, most);
    CHECK(most <= 2u);
}

/* AO's history across a change of size: a converged room, then one frame at 50 % and one at 100 %. Each finds its surface in the last frame's
 * AO at that frame's size, so almost no pixel starts again: indexed by this frame's width, most would land on another surface and go fresh. */
TEST(ao_history_survives_a_size_change) {
    sk_rig_t rig;
    REQUIRE(rs_open(t, &rig, RS_WIDTH, RS_HEIGHT, RS_FEATURES_AO, true));
    render_camera_t eye;
    rs_camera(&eye);
    REQUIRE(rs_draw(t, &rig, &eye, 100u, RS_CONVERGED_FRAMES, nullptr));
    shade_ao_pixel_t *const ao = test_alloc(t, (uint64_t)RS_PIXELS * sizeof(shade_ao_pixel_t));
    const uint32_t percents[2] = {50u, 100u};
    for (uint32_t step = 0u; step < 2u; step++) {
        REQUIRE(rs_draw(t, &rig, &eye, percents[step], 1u, nullptr));
        const uint32_t width = post_scaled_side(RS_WIDTH, percents[step]);
        const uint32_t height = post_scaled_side(RS_HEIGHT, percents[step]);
        REQUIRE(device_memory_read(&rig.bridge->device_memory, rig.render->frame.temporal.borrowed_ao_last, 0u, ao,
                                   (uint64_t)width * height * sizeof(shade_ao_pixel_t)) == GPU_STATUS_OK);
        uint64_t surfaces = 0u, kept = 0u;
        for (uint32_t i = 0u; i < width * height; i++) {
            if (ao[i].samples == 0u) {
                continue; /* sky */
            }
            surfaces++;
            kept += ao[i].samples > SHADE_AO_FRESH_RAYS + 1u ? 1u : 0u;
        }
        printf("  at %" PRIu32 " %%: %" PRIu64 " of %" PRIu64 " surface pixels kept their AO\n", percents[step], kept,
               surfaces);
        CHECK(surfaces > (uint64_t)width * height / 4u);
        CHECK(kept * 100u >= surfaces * 97u);
    }
    rs_close(t, &rig);
}

/* The outline is drawn after the upscale, at the output's size: at 50 % it is one output pixel wide as at 100 %, and every pixel it changes
 * is the outline's colour, not a blend of it. */
TEST(the_outline_is_one_pixel_at_any_rung) {
    sk_rig_t rig;
    REQUIRE(rs_open(t, &rig, RS_WIDTH, RS_HEIGHT, RS_PLAIN, true));
    REQUIRE(rs_box(t, &rig, (const int32_t[3]){12, 6, -1}, (const int32_t[3]){13, 7, 0}, BLOCK_ID_DIRT));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const render_entities_target_t target = {
        .box_min = {12.0f64, 6.0f64, -1.0f64}, .box_max = {13.0f64, 7.0f64, 0.0f64}, .outline = 1u};
    render_camera_t eye;
    render_camera_start(&eye, 9.5f64, 4.5f64, 1.5f64, 0.6f32, -0.55f32);
    uint8_t *const bare = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    uint8_t *const lined = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    uint64_t counts[2] = {0u, 0u};
    const uint32_t percents[2] = {100u, 50u};
    render_entities_t *const entities = rig.render->borrowed_entities;
    for (uint32_t step = 0u; step < 2u; step++) {
        render_entities_begin(entities);
        render_entities_publish(entities);
        REQUIRE(rs_draw(t, &rig, &eye, percents[step], 1u, bare));
        render_entities_begin(entities);
        render_entities_target_set(entities, &target);
        render_entities_publish(entities);
        REQUIRE(rs_draw(t, &rig, &eye, percents[step], 1u, lined));
        uint64_t wrong = 0u;
        const uint32_t *const a = (const uint32_t *)(const void *)lined, *const b =
                                                                             (const uint32_t *)(const void *)bare;
        for (uint32_t i = 0u; i < RS_PIXELS; i++) {
            if (a[i] != b[i]) {
                counts[step]++;
                wrong += a[i] == ENTITY_RAYS_OUTLINE_XRGB ? 0u : 1u;
            }
        }
        CHECK_EQ_U64(wrong, 0u);
    }
    printf("  outline pixels at 100 %%: %" PRIu64 ", at 50 %%: %" PRIu64 "\n", counts[0], counts[1]);
    CHECK(counts[0] > 40u);
    /* Drawn at the inner size and brought up it would be twice as wide, and blended. */
    CHECK(counts[1] * 10u >= counts[0] * 8u && counts[1] * 10u <= counts[0] * 12u);
    rs_close(t, &rig);
}

/* The old way of a render scale, nearest pixel on the CPU, for comparison: plan 008 phase 05 took it out of game/view. */
static void rs_nearest_up(const uint8_t *from, uint32_t from_width, uint32_t from_height, uint8_t *to,
                          uint32_t to_width, uint32_t to_height) {
    for (uint32_t y = 0u; y < to_height; y++) {
        const uint32_t row = (uint32_t)((uint64_t)y * from_height / to_height);
        for (uint32_t x = 0u; x < to_width; x++) {
            const uint32_t column = (uint32_t)((uint64_t)x * from_width / to_width);
            memcpy(to + ((uint64_t)y * to_width + x) * 4u, from + ((uint64_t)row * from_width + column) * 4u, 4u);
        }
    }
}

/* The static render scale at 70 %: the picture drawn is the very one a frame of the smaller size drew before (the same rays: the same field
 * of view and aspect), and what is shown is it brought up, which differs from the old nearest pixel only by the filtering. */
TEST(render_scale_seventy_matches_its_old_picture_in_size) {
    const uint32_t inner_width = post_scaled_side(RS_WIDTH, 70u), inner_height = post_scaled_side(RS_HEIGHT, 70u);
    const uint64_t inner_bytes = post_xrgb_bytes(inner_width, inner_height);
    uint8_t *const old = test_alloc(t, inner_bytes);
    uint8_t *const drawn = test_alloc(t, inner_bytes);
    uint8_t *const shown = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    uint8_t *const nearest = test_alloc(t, post_xrgb_bytes(RS_WIDTH, RS_HEIGHT));
    render_camera_t eye;
    rs_camera(&eye);
    {
        sk_rig_t rig;
        REQUIRE(rs_open(t, &rig, inner_width, inner_height, RS_FEATURES, true));
        REQUIRE(rs_draw(t, &rig, &eye, 100u, RS_SCALE_FRAMES, old));
        rs_close(t, &rig);
    }
    {
        sk_rig_t rig;
        REQUIRE(rs_open(t, &rig, RS_WIDTH, RS_HEIGHT, RS_FEATURES, true));
        REQUIRE(rs_draw(t, &rig, &eye, 70u, RS_SCALE_FRAMES, shown));
        /* The picture as drawn, before it came up: in the hits block, where post wrote it. */
        REQUIRE(device_memory_read(&rig.bridge->device_memory, &rig.render->owned_hits, 0u, drawn, inner_bytes) ==
                GPU_STATUS_OK);
        rs_close(t, &rig);
    }
    CHECK_EQ_U64((uint64_t)memcmp(drawn, old, inner_bytes), 0u);
    rs_nearest_up(old, inner_width, inner_height, nearest, RS_WIDTH, RS_HEIGHT);
    uint64_t differ = 0u, sum = 0u;
    for (uint64_t i = 0u; i < (uint64_t)RS_PIXELS * 4u; i++) {
        if ((i & 3u) != 3u) {
            sum += (uint64_t)abs((int32_t)shown[i] - (int32_t)nearest[i]);
        }
    }
    const uint32_t most = rs_most(shown, nearest, RS_PIXELS, &differ);
    const float64_t mean = (float64_t)sum / (float64_t)(RS_PIXELS * 3u);
    printf("  70 %% brought up against the old nearest pixel: mean %.3f, most %" PRIu32 ", %" PRIu64 " pixels differ\n",
           (libc_double_t)mean, most, differ);
    CHECK(mean < 3.0f64);
    /* And its middle pixel is the old one's, as nearest and bilinear agree inside a flat face. */
    CHECK(sk_bytes_near(sk_pixel(shown, RS_WIDTH, RS_WIDTH / 2u, RS_HEIGHT / 2u),
                        sk_pixel(nearest, RS_WIDTH, RS_WIDTH / 2u, RS_HEIGHT / 2u), 8u));
}

TEST_MAIN()
