/* ctest run_render_frame. See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/watch/index.h"
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
    FR_WIDTH = 320u,
    FR_HEIGHT = 180u,
    FR_GOLDEN_TOLERANCE = 2u,
    FR_REFERENCE_BLOCKS_MAX = 8u,
    FR_PROGRAM_TIMEOUT_MS = 120000u,
};

#define FR_PI 3.14159265358979f32

/* ---- A rig: everything a real run has, on memory of its own ----------------------------------------------------------- */

typedef struct fr_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
} fr_rig_t;
static_assert(sizeof(fr_rig_t) == 64);

static void *fr_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool fr_rig_begin(test_context_t *t, fr_rig_t *rig) {
    rig->bridge = fr_aligned(t, sizeof(gpu_bridge_t));
    rig->job = fr_aligned(t, sizeof(job_system_t));
    rig->world = fr_aligned(t, sizeof(world_t));
    rig->terrain = fr_aligned(t, sizeof(terrain_t));
    rig->render = fr_aligned(t, sizeof(render_t));
    rig->texture = fr_aligned(t, sizeof(texture_t));
    rig->game = fr_aligned(t, sizeof(game_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    return true;
}

static void fr_rig_end(test_context_t *t, fr_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static const uint8_t *fr_draw(test_context_t *t, fr_rig_t *rig, const render_camera_t *camera) {
    render_t *const render = rig->render;
    const gpu_status_t status = render_frame_draw(&render->frame, &render->buffers, camera);
    if (!CHECK_EQ_U64(status, GPU_STATUS_OK)) {
        gpu_error_log(&render->frame.gpu.last_error);
        return nullptr;
    }
    const uint8_t *const picture = render_frame_take(&render->frame);
    CHECK(picture != nullptr);
    return picture;
}

static uint32_t fr_pixel(const uint8_t *picture, uint32_t width, uint32_t x, uint32_t y) {
    uint32_t value = 0u;
    memcpy(&value, picture + ((uint64_t)y * width + x) * 4u, 4u);
    return value;
}

static bool fr_bytes_near(uint32_t a, uint32_t b, uint32_t tolerance) {
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        const int32_t gap = (int32_t)((a >> shift) & 255u) - (int32_t)((b >> shift) & 255u);
        if ((uint32_t)abs(gap) > tolerance) {
            return false;
        }
    }
    return true;
}

/* ---- By hand ----------------------------------------------------------------------------------------------------------- */

TEST(a_face_says_where_on_its_texture_a_point_is) {
    /* A point at x = 0.25, y = 0.5, z = 0.75 of a voxel, seen on each face. */
    const float32_t local[3] = {0.25f32, 0.5f32, 0.75f32};
    float32_t u = 0.0f32, v = 0.0f32;
    /* The top: x runs right, and y up the page, while rows run down it. */
    shade_face_uv(TRAVERSE_FACE_POS_Z, local, &u, &v);
    CHECK_NEAR(u, 0.25f32, 1e-6f32);
    CHECK_NEAR(v, 0.5f32, 1e-6f32);
    /* The bottom: the top seen from beneath, so y runs down the page. */
    shade_face_uv(TRAVERSE_FACE_NEG_Z, local, &u, &v);
    CHECK_NEAR(u, 0.25f32, 1e-6f32);
    CHECK_NEAR(v, 0.5f32, 1e-6f32);
    /* Looking west at the east side, north is on the right, and z = 0.75 is a quarter of the way down. */
    shade_face_uv(TRAVERSE_FACE_POS_X, local, &u, &v);
    CHECK_NEAR(u, 0.5f32, 1e-6f32);
    CHECK_NEAR(v, 0.25f32, 1e-6f32);
    const float32_t off_centre[3] = {0.25f32, 0.125f32, 0.75f32};
    shade_face_uv(TRAVERSE_FACE_POS_X, off_centre, &u, &v);
    CHECK_NEAR(u, 0.125f32, 1e-6f32);
    /* Looking east at the west side, north is on the left. */
    shade_face_uv(TRAVERSE_FACE_NEG_X, off_centre, &u, &v);
    CHECK_NEAR(u, 0.875f32, 1e-6f32);
    CHECK_NEAR(v, 0.25f32, 1e-6f32);
    /* Looking south at the north side, west is on the right. */
    shade_face_uv(TRAVERSE_FACE_POS_Y, local, &u, &v);
    CHECK_NEAR(u, 0.75f32, 1e-6f32);
    CHECK_NEAR(v, 0.25f32, 1e-6f32);
    /* Looking north at the south side, east is on the right. */
    shade_face_uv(TRAVERSE_FACE_NEG_Y, local, &u, &v);
    CHECK_NEAR(u, 0.25f32, 1e-6f32);
    CHECK_NEAR(v, 0.25f32, 1e-6f32);
}

TEST(a_place_on_a_texture_is_always_a_texel_it_has) {
    CHECK_EQ_U64(shade_texel_of(0.0f32, 16u), 0u);
    CHECK_EQ_U64(shade_texel_of(0.0624f32, 16u), 0u);
    CHECK_EQ_U64(shade_texel_of(0.0626f32, 16u), 1u);
    CHECK_EQ_U64(shade_texel_of(0.5f32, 16u), 8u);
    CHECK_EQ_U64(shade_texel_of(0.9999f32, 16u), 15u);
    /* Rounding can put a hit a little outside its voxel, and it must stay on the texture. */
    CHECK_EQ_U64(shade_texel_of(1.0f32, 16u), 15u);
    CHECK_EQ_U64(shade_texel_of(1.0001f32, 16u), 15u);
    CHECK_EQ_U64(shade_texel_of(-0.0001f32, 16u), 0u);
    CHECK_EQ_U64(shade_texel_of(NAN, 16u), 0u);
    CHECK_EQ_U64(shade_texel_of(0.5f32, 1u), 0u);
}

TEST(the_mip_is_the_level_whose_texels_are_nearest_a_pixel) {
    /* A pixel 1/100 of a block wide per block of distance, on a 16-texel texture of 5 levels. */
    const float32_t tangent = 0.01f32;
    CHECK_EQ_U64(shade_mip_level(0.0f32, tangent, 16u, 5u), 0u);
    CHECK_EQ_U64(shade_mip_level(6.0f32, tangent, 16u, 5u), 0u);      /* 0.96 texels a pixel */
    CHECK_EQ_U64(shade_mip_level(8.0f32, tangent, 16u, 5u), 0u);      /* 1.28: log2 is 0.36, nearer 0 */
    CHECK_EQ_U64(shade_mip_level(10.0f32, tangent, 16u, 5u), 1u);     /* 1.6: log2 is 0.68, nearer 1 */
    CHECK_EQ_U64(shade_mip_level(25.0f32, tangent, 16u, 5u), 2u);     /* 4 */
    CHECK_EQ_U64(shade_mip_level(100000.0f32, tangent, 16u, 5u), 4u); /* and no further than there are levels */
    CHECK_EQ_U64(shade_mip_level(TRAVERSE_COARSE_SKY, tangent, 16u, 5u), 4u);
    CHECK_EQ_U64(shade_level_dimension(16u, 2u), 4u);
    CHECK_EQ_U64(shade_level_dimension(165u, 1u), 82u);
    CHECK_EQ_U64(shade_level_dimension(16u, 9u), 1u);
}

TEST(the_curves_are_the_published_ones) {
    /* The tables stand for these functions: the numbers are the card's powf, so they need only agree with this CPU's about as far as two
     * good powfs do. Relative, as the low end is small. */
    for (uint32_t byte = 0u; byte < 256u; byte++) {
        const float32_t direct = shade_srgb_to_linear((float32_t)byte / 255.0f32);
        CHECK_NEAR(shade_srgb_byte(byte), direct, direct * 1e-6f32 + 1e-9f32);
    }
    for (uint32_t level = 0u; level < SHADE_LIGHT_LEVEL_MAX; level++) {
        const float32_t direct = powf(0.8f32, (float32_t)(SHADE_LIGHT_LEVEL_MAX - level));
        CHECK_NEAR(shade_sky_level(level), direct, direct * 1e-6f32);
    }
    CHECK_NEAR(shade_srgb_to_linear(0.0f32), 0.0f32, 1e-6f32);
    CHECK_NEAR(shade_srgb_to_linear(1.0f32), 1.0f32, 1e-5f32);
    CHECK_NEAR(shade_srgb_to_linear(0.04045f32), 0.0031308f32, 1e-5f32);
    CHECK_NEAR(shade_srgb_to_linear(0.5f32), 0.21404f32, 1e-4f32);
    CHECK_EQ_U64(post_tone_map(0.0f32), 0u);
    CHECK_EQ_U64(post_tone_map(-1.0f32), 0u);
    CHECK_EQ_U64(post_tone_map(NAN), 0u);
    /* 0.5 in sRGB is 1.055 * 0.5^(1/2.4) - 0.055 = 0.7354, and 187.5 + 0.5 rounds to 188. */
    CHECK_EQ_U64(post_tone_map(0.5f32), 188u);
    /* Past the shoulder 1.0 rolls down: 0.8 + 0.2 * (1 - e^-1) = 0.92642, which is 0.96692 in sRGB, or 246.56 + 0.5. */
    CHECK_EQ_U64(post_tone_map(1.0f32), 247u);
    CHECK_EQ_U64(post_tone_map(1000000.0f32), 255u);
    CHECK_EQ_U64(post_xrgb(0x12u, 0x34u, 0x56u), 0x123456u);
    /* Below the shoulder a texture under full light keeps its bytes. */
    for (uint32_t byte = 0u; byte <= 231u; byte++) {
        CHECK_EQ_U64(post_tone_map(shade_srgb_to_linear((float32_t)byte / 255.0f32)), byte);
    }
    /* And going brighter never comes out darker. */
    uint32_t last = 0u;
    for (uint32_t i = 0u; i < 4000u; i++) {
        const uint32_t now = post_tone_map((float32_t)i * 0.001f32);
        CHECK(now >= last);
        last = now;
    }
}

TEST(sky_and_fog_go_where_they_should) {
    const post_settings_t settings = {.sky_zenith = {0.1f32, 0.2f32, 0.3f32},
                                      .sky_horizon = {0.5f32, 0.6f32, 0.7f32},
                                      .fog_start = 100.0f32,
                                      .fog_end = 200.0f32};
    float32_t sky[3];
    post_sky(&settings, (const float32_t[3]){0.0f32, 0.0f32, 1.0f32}, sky);
    CHECK_NEAR(sky[0], 0.1f32, 1e-6f32);
    CHECK_NEAR(sky[2], 0.3f32, 1e-6f32);
    post_sky(&settings, (const float32_t[3]){1.0f32, 0.0f32, 0.0f32}, sky);
    CHECK_NEAR(sky[0], 0.5f32, 1e-6f32);
    post_sky(&settings, (const float32_t[3]){0.0f32, 0.6f32, -0.8f32}, sky);
    CHECK_NEAR(sky[1], 0.6f32, 1e-6f32);
    post_sky(&settings, (const float32_t[3]){0.0f32, 0.0f32, 0.5f32}, sky);
    CHECK_NEAR(sky[0], 0.3f32, 1e-6f32);
    CHECK_NEAR(post_fog(&settings, 50.0f32), 0.0f32, 1e-6f32);
    CHECK_NEAR(post_fog(&settings, 100.0f32), 0.0f32, 1e-6f32);
    CHECK_NEAR(post_fog(&settings, 150.0f32), 0.25f32, 1e-6f32);
    CHECK_NEAR(post_fog(&settings, 200.0f32), 1.0f32, 1e-6f32);
    CHECK_NEAR(post_fog(&settings, TRAVERSE_COARSE_SKY), 1.0f32, 1e-6f32);
}

TEST(each_key_and_the_mouse_move_the_camera_the_way_they_say) {
    render_camera_t camera;
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.0f32, 0.0f32);
    const float32_t second = 1.0f32;
    const float64_t speed = (float64_t)RENDER_CAMERA_SPEED_DEFAULT;
    /* A yaw of 0 looks east, along +x. */
    render_fly_input_t input = {.forward = 1};
    render_camera_fly(&camera, &input, second);
    CHECK_NEAR(camera.position[0], speed, 1e-4f64);
    CHECK_NEAR(camera.position[1], 0.0f64, 1e-4f64);
    /* Looking east, the right hand is to the south, along -y. */
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.0f32, 0.0f32);
    input = (render_fly_input_t){.right = 1};
    render_camera_fly(&camera, &input, second);
    CHECK_NEAR(camera.position[1], -speed, 1e-4f64);
    /* A quarter turn left looks north. */
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, FR_PI / 2.0f32, 0.0f32);
    input = (render_fly_input_t){.forward = 1};
    render_camera_fly(&camera, &input, second);
    CHECK_NEAR(camera.position[0], 0.0f64, 1e-4f64);
    CHECK_NEAR(camera.position[1], speed, 1e-4f64);
    /* Up is z, whichever way it looks, and looking down does not change how level walking goes. */
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 1.0f32, -1.0f32);
    input = (render_fly_input_t){.up = 1};
    render_camera_fly(&camera, &input, second);
    CHECK_NEAR(camera.position[2], speed, 1e-4f64);
    input = (render_fly_input_t){.forward = 1};
    render_camera_fly(&camera, &input, second);
    CHECK_NEAR(camera.position[2], speed, 1e-4f64);
    /* Ctrl is five times. */
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.0f32, 0.0f32);
    input = (render_fly_input_t){.forward = 1, .fast = 1};
    render_camera_fly(&camera, &input, second);
    CHECK_NEAR(camera.position[0], speed * (float64_t)RENDER_CAMERA_FAST_FACTOR, 1e-3f64);
    /* The mouse to the right turns right, which is less yaw, and the mouse towards the player looks down. */
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.0f32, 0.0f32);
    input = (render_fly_input_t){.mouse_dx = 100, .mouse_dy = 40};
    render_camera_fly(&camera, &input, 0.0f32);
    CHECK_NEAR(camera.yaw, -100.0f32 * RENDER_CAMERA_RADIANS_PER_COUNT, 1e-5f32);
    CHECK_NEAR(camera.pitch, -40.0f32 * RENDER_CAMERA_RADIANS_PER_COUNT, 1e-5f32);
    /* It never quite looks straight up or down. */
    input = (render_fly_input_t){.mouse_dy = 1000000};
    render_camera_fly(&camera, &input, 0.0f32);
    CHECK_NEAR(camera.pitch, -RENDER_CAMERA_PITCH_LIMIT, 1e-6f32);
    input = (render_fly_input_t){.mouse_dy = -2000000};
    render_camera_fly(&camera, &input, 0.0f32);
    CHECK_NEAR(camera.pitch, RENDER_CAMERA_PITCH_LIMIT, 1e-6f32);
    /* The wheel goes up a step at a time, and stops where it should. */
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.0f32, 0.0f32);
    input = (render_fly_input_t){.wheel = 1};
    render_camera_fly(&camera, &input, 0.0f32);
    CHECK_NEAR(camera.speed, RENDER_CAMERA_SPEED_DEFAULT * RENDER_CAMERA_SPEED_STEP, 1e-4f32);
    input = (render_fly_input_t){.wheel = 100000};
    render_camera_fly(&camera, &input, 0.0f32);
    CHECK(camera.speed >= RENDER_CAMERA_SPEED_MAX && camera.speed < RENDER_CAMERA_SPEED_MAX * RENDER_CAMERA_SPEED_STEP);
    input = (render_fly_input_t){.wheel = -100000};
    render_camera_fly(&camera, &input, 0.0f32);
    CHECK(camera.speed <= RENDER_CAMERA_SPEED_MIN && camera.speed > RENDER_CAMERA_SPEED_MIN / RENDER_CAMERA_SPEED_STEP);
}

TEST(a_camera_becomes_a_cell_a_fraction_and_three_axes) {
    render_camera_t camera;
    render_camera_start(&camera, -0.25f64, 16000000.5f64, 3.0f64, 0.0f32, 0.0f32);
    traverse_camera_t view;
    render_camera_to_traverse(&camera, 200u, 100u, &view);
    CHECK_EQ_I64(view.eye_cell[0], -1);
    CHECK_NEAR(view.eye_frac[0], 0.75f32, 1e-6f32);
    CHECK_EQ_I64(view.eye_cell[1], 16000000);
    CHECK_NEAR(view.eye_frac[1], 0.5f32, 1e-6f32); /* exact, out here, as a position in float32_t could not be */
    CHECK_EQ_I64(view.eye_cell[2], 3);
    CHECK_NEAR(view.eye_frac[2], 0.0f32, 1e-6f32);
    CHECK_NEAR(view.forward[0], 1.0f32, 1e-6f32);
    /* Looking east, right is south and up is up, and each is as long as the field of view makes it, twice as wide as tall. */
    CHECK_NEAR(view.right[1], -2.0f32 * camera.half_fov_tangent, 1e-6f32);
    CHECK_NEAR(view.up[2], camera.half_fov_tangent, 1e-6f32);
    CHECK_EQ_U64(view.width, 200u);
    CHECK_EQ_U64(view.height, 100u);
}

TEST(the_exchange_hands_over_the_newest_and_keeps_what_is_being_read) {
    render_exchange_t *const exchange = fr_aligned(t, sizeof(render_exchange_t));
    render_exchange_start(exchange);
    uint32_t slot = 99u;
    CHECK(!render_exchange_take(exchange, &slot));
    CHECK(!render_exchange_waiting(exchange));
    const uint32_t first = exchange->back;
    render_exchange_publish(exchange);
    CHECK(render_exchange_waiting(exchange));
    const uint32_t second = exchange->back;
    CHECK(second != first);
    render_exchange_publish(exchange); /* never read, so the first is the writer's again */
    const uint32_t third = exchange->back;
    CHECK(third != second);
    CHECK(render_exchange_take(exchange, &slot));
    CHECK_EQ_U64(slot, second); /* the newest */
    CHECK(!render_exchange_waiting(exchange));
    CHECK(!render_exchange_take(exchange, &slot));
    /* The writer goes round and round, and never lands on what the reader holds. */
    for (uint32_t i = 0u; i < 16u; i++) {
        CHECK(exchange->back != slot);
        render_exchange_publish(exchange);
    }
    CHECK(exchange->back != slot);
    /* However the two sides take turns, the writer, the reader and the middle hold one slot each. */
    uint32_t turns = 0x2d6bu;
    for (uint32_t i = 0u; i < 64u; i++) {
        if ((turns & 1u) != 0u) {
            render_exchange_publish(exchange);
        } else {
            (void)render_exchange_take(exchange, &slot);
        }
        turns = turns >> 1 | turns << 15;
        const uint32_t middle =
            atomic_load_explicit(&exchange->exchange, memory_order_relaxed) & 3u; /* the low bits name a slot */
        CHECK_EQ_U64(1u << exchange->back | 1u << exchange->front | 1u << middle, 7u);
    }
}

/* ---- A world of a few blocks, against an exact reference ----------------------------------------------------------------- */

typedef struct fr_reference_block_t {
    int32_t cell[3];  /* 0 */
    uint16_t state;   /* 12 */
    uint8_t _pad0[2]; /* 14 */
} fr_reference_block_t;
static_assert(sizeof(fr_reference_block_t) == 16);

/* Every column of a table empty but there, and nothing else: so that all a picture holds is what a test put in it. */
static bool fr_hand_world_open(test_context_t *t, fr_rig_t *rig, uint32_t radius, uint32_t width, uint32_t height) {
    world_t *const world = rig->world;
    render_t *const render = rig->render;
    if (!CHECK(world_open(world, radius, 0, 0))) {
        return false;
    }
    const render_upload_budget_t budget = {.bytes = RENDER_UPLOAD_BUDGET_BYTES_DEFAULT,
                                           .sections = RENDER_UPLOAD_BUDGET_SECTIONS_DEFAULT};
    if (!CHECK_EQ_U64(render_open(render, &world->snapshot, RENDER_POOL_BYTES_MIN, budget), GPU_STATUS_OK) ||
        !CHECK_EQ_U64(render_open_frame(render, rig->texture, width, height, 100000.0f32), GPU_STATUS_OK)) {
        return false;
    }
    /* The reference knows each kind of face's one brightness: tests/cuda/sky/ has the sun and stored light. */
    render_frame_features_set(&render->frame, SHADE_FEATURE_SHAPES | SHADE_FEATURE_ALPHA);
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        if (!CHECK(world_map_stream_commit(&world->map, &request) != nullptr)) {
            return false;
        }
        world_edit_column_filled(&world->edit, request.slot);
    }
    return true;
}

static void fr_hand_world_close(fr_rig_t *rig) {
    render_close(rig->render);
    world_close(rig->world);
}

/* Publishes, and pumps until the card has it all. */
static bool fr_hand_world_sync(test_context_t *t, fr_rig_t *rig) {
    world_t *const world = rig->world;
    const world_edit_snapshot_t *const snapshot = &world->snapshot;
    for (uint32_t pass = 0u; pass < 1000000u; pass++) {
        (void)render_upload_resend_drain(&rig->render->upload, &world->edit);
        (void)world_edit_snapshot_publish(&world->snapshot);
        if (!CHECK_EQ_U64(render_upload_frame(&rig->render->upload), GPU_STATUS_OK)) {
            return false;
        }
        if (snapshot->published_count == snapshot->acquired_count + snapshot->overwritten_count &&
            world->edit.queue_count[WORLD_EDIT_CONSUMER_RENDER] == 0u && render_upload_idle(&rig->render->upload)) {
            return true;
        }
    }
    return CHECK(false);
}

/* The nearest of the boxes along a ray, by the slab test, in float64_t. Which face was entered comes from which slab was
 * entered last. Returns false for no hit, or a hit from inside. */
static bool fr_reference_trace(const fr_reference_block_t *blocks, uint32_t count, const float64_t eye[3],
                               const float64_t direction[3], uint32_t *out_block, uint32_t *out_face,
                               float64_t *out_t) {
    float64_t best = 1e30f64;
    bool found = false;
    for (uint32_t b = 0u; b < count; b++) {
        float64_t t_near = -1e30f64, t_far = 1e30f64;
        uint32_t face = 0u;
        bool possible = true;
        for (uint32_t i = 0u; i < 3u && possible; i++) {
            const float64_t low = (float64_t)blocks[b].cell[i] - eye[i];
            if (fabs(direction[i]) < 1e-12f64) {
                possible = low <= 0.0f64 && low + 1.0f64 >= 0.0f64;
                continue;
            }
            const float64_t a = low / direction[i], c = (low + 1.0f64) / direction[i];
            const float64_t entered = a < c ? a : c, left = a < c ? c : a;
            if (entered > t_near) {
                t_near = entered;
                face = i * 2u + (direction[i] > 0.0f64 ? 0u : 1u);
            }
            t_far = left < t_far ? left : t_far;
        }
        if (possible && t_near < t_far && t_near > 0.0f64 && t_near < best) {
            best = t_near;
            found = true;
            *out_block = b;
            *out_face = face;
        }
    }
    *out_t = best;
    return found;
}

/* A texture's coordinates, in a table written apart from the kernel's, from the point of view of someone looking at the face:
 * up the texture is +z on a side, and each side's right hand is worked out from which way they look. */
static void fr_reference_uv(uint32_t face, const float64_t local[3], float64_t *out_u, float64_t *out_v) {
    switch (face) {
    case 0u: /* west side, looking east: right is south */
        *out_u = 1.0f64 - local[1];
        *out_v = 1.0f64 - local[2];
        break;
    case 1u: /* east side, looking west: right is north */
        *out_u = local[1];
        *out_v = 1.0f64 - local[2];
        break;
    case 2u: /* south side, looking north: right is east */
        *out_u = local[0];
        *out_v = 1.0f64 - local[2];
        break;
    case 3u: /* north side, looking south: right is west */
        *out_u = 1.0f64 - local[0];
        *out_v = 1.0f64 - local[2];
        break;
    case 4u: /* the bottom */
        *out_u = local[0];
        *out_v = local[1];
        break;
    default: /* the top */
        *out_u = local[0];
        *out_v = 1.0f64 - local[1];
        break;
    }
}

/* Near an edge of a texel, or of a box, rounding may decide the answer either way, so that pixel says nothing. */
static bool fr_near_whole(float64_t value, float64_t margin) {
    return fabs(value - round(value)) < margin;
}

typedef struct fr_compare_t {
    uint64_t compared;               /* 0 */
    uint64_t hits;                   /* 8 */
    uint64_t wrong;                  /* 16 */
    uint64_t face_seen[SHADE_FACES]; /* 24 */
} fr_compare_t;
static_assert(sizeof(fr_compare_t) == 72);

/* Every pixel of a picture of a few blocks against the reference. */
static void fr_compare_picture(test_context_t *t, const fr_rig_t *rig, const fr_reference_block_t *blocks,
                               uint32_t count, const render_camera_t *camera, const uint8_t *picture, uint32_t width,
                               uint32_t height, fr_compare_t *out) {
    traverse_camera_t view;
    render_camera_to_traverse(camera, width, height, &view);
    const post_settings_t *const post = &rig->render->frame.post;
    for (uint32_t py = 0u; py < height; py++) {
        for (uint32_t px = 0u; px < width; px++) {
            const float64_t a = 2.0f64 * ((float64_t)px + 0.5f64) / (float64_t)width - 1.0f64;
            const float64_t b = 1.0f64 - 2.0f64 * ((float64_t)py + 0.5f64) / (float64_t)height;
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
            uint32_t block = 0u, face = 0u;
            float64_t distance = 0.0f64;
            uint32_t expected = 0u;
            const bool hit = fr_reference_trace(blocks, count, camera->position, direction, &block, &face, &distance);
            /* A ray this close to a box's edge may go either way. */
            bool usable = true;
            if (hit) {
                float64_t local[3];
                for (uint32_t i = 0u; i < 3u; i++) {
                    local[i] = camera->position[i] + direction[i] * distance - (float64_t)blocks[block].cell[i];
                }
                float64_t u = 0.0f64, v = 0.0f64;
                fr_reference_uv(face, local, &u, &v);
                const uint16_t texture_index = block_defs[blocks[block].state].faces[face];
                uint32_t id = 0u;
                REQUIRE(texture_find(rig->texture, block_texture_names[texture_index], &id));
                texture_mip_t mip;
                REQUIRE(texture_mip_get(rig->texture, id, 0u, 0u, &mip));
                const float32_t tangent = shade_pixel_tangent(&view);
                const uint32_t top_width = mip.width;
                const uint32_t level = shade_mip_level((float32_t)distance, tangent, top_width, 16u);
                texture_info_t info;
                REQUIRE(texture_info_get(rig->texture, id, &info));
                const uint32_t real_level =
                    level >= info.borrowed_entry->level_count ? info.borrowed_entry->level_count - 1u : level;
                REQUIRE(texture_mip_get(rig->texture, id, real_level, 0u, &mip));
                usable = !fr_near_whole(u * (float64_t)mip.width, 0.02f64) &&
                         !fr_near_whole(v * (float64_t)mip.height, 0.02f64);
                /* Also not across an edge in the choice of level. */
                const float32_t per_pixel = (float32_t)distance * tangent * (float32_t)top_width;
                usable =
                    usable && (per_pixel <= 1.0f32 || !fr_near_whole((float64_t)log2f(per_pixel) + 0.5f64, 0.005f64));
                const uint32_t x = shade_texel_of((float32_t)u, mip.width),
                               y = shade_texel_of((float32_t)v, mip.height);
                const uint8_t *const texel = mip.borrowed_pixels + ((uint64_t)y * mip.width + x) * 4u;
                const float64_t brightness[SHADE_FACES] = {0.6f64, 0.6f64, 0.8f64, 0.8f64, 0.5f64, 1.0f64};
                uint32_t channel[3];
                for (uint32_t i = 0u; i < 3u; i++) {
                    channel[i] = post_tone_map((
                        float32_t)((float64_t)shade_srgb_to_linear((float32_t)texel[i] / 255.0f32) * brightness[face]));
                }
                expected = post_xrgb(channel[0], channel[1], channel[2]);
                if (usable) {
                    out->hits++;
                    out->face_seen[face]++;
                }
            } else {
                const float32_t unit[3] = {(float32_t)direction[0], (float32_t)direction[1], (float32_t)direction[2]};
                float32_t sky[3];
                post_sky(post, unit, sky);
                expected = post_xrgb(post_tone_map(sky[0]), post_tone_map(sky[1]), post_tone_map(sky[2]));
            }
            /* A silhouette: this ray or one a hair from it may go either way. Decided by looking a little to each side. */
            for (uint32_t i = 0u; i < 2u && usable; i++) {
                float64_t nudged[3];
                for (uint32_t k = 0u; k < 3u; k++) {
                    const float64_t sideways = (i == 0u ? 1.0f64 : -1.0f64) * 0.01f64 / (float64_t)height;
                    nudged[k] = direction[k] + sideways * ((float64_t)view.right[k] + (float64_t)view.up[k]);
                }
                uint32_t other_block = 0u, other_face = 0u;
                float64_t other_distance = 0.0f64;
                const bool other = fr_reference_trace(blocks, count, camera->position, nudged, &other_block,
                                                      &other_face, &other_distance);
                if (other != hit || (hit && (other_block != block || other_face != face))) {
                    usable = false;
                }
            }
            if (!usable) {
                continue;
            }
            out->compared++;
            if (!fr_bytes_near(fr_pixel(picture, width, px, py), expected, 1u)) {
                if (out->wrong < 8u) {
                    printf("  pixel %" PRIu32 ", %" PRIu32 ": card %06" PRIX32 ", reference %06" PRIX32
                           ", hit %d face %" PRIu32 "\n",
                           px, py, fr_pixel(picture, width, px, py), expected, hit ? 1 : 0, face);
                }
                out->wrong++;
            }
        }
    }
}

TEST(a_world_of_a_few_blocks_matches_an_exact_trace_from_all_sides) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    REQUIRE(fr_hand_world_open(t, &rig, 2u, FR_WIDTH, FR_HEIGHT));
    /* Grass has three textures, and a furnace a front on its north side alone. */
    const fr_reference_block_t blocks[] = {
        {.cell = {0, 0, 0}, .state = BLOCK_ID_GRASS_BLOCK},
        {.cell = {3, 1, 0}, .state = BLOCK_ID_FURNACE},
        {.cell = {-2, 2, 1}, .state = BLOCK_ID_STONE},
        {.cell = {1, -3, 2}, .state = BLOCK_ID_SAND},
    };
    const uint32_t count = sizeof(blocks) / sizeof(blocks[0]);
    for (uint32_t b = 0u; b < count; b++) {
        CHECK_EQ_U64(world_edit_block_set(&rig.world->edit, blocks[b].cell[0], blocks[b].cell[1], blocks[b].cell[2],
                                          blocks[b].state),
                     WORLD_EDIT_STATUS_CHANGED);
    }
    REQUIRE(fr_hand_world_sync(t, &rig));
    /* From each side, above and below, near enough to fill the picture and far enough for a mip level or two. */
    fr_compare_t compare = {0};
    const float64_t distances[] = {3.5f64, 9.0f64, 40.0f64};
    for (uint32_t d = 0u; d < sizeof(distances) / sizeof(distances[0]); d++) {
        for (uint32_t turn = 0u; turn < 8u; turn++) {
            for (uint32_t level = 0u; level < 3u; level++) {
                const float32_t yaw = (float32_t)turn * FR_PI / 4.0f32 + 0.11f32;
                const float32_t pitch = -0.9f32 + (float32_t)level * 0.9f32;
                const float64_t reach = distances[d];
                render_camera_t camera;
                render_camera_start(&camera, 1.5f64 - reach * (float64_t)cosf(pitch) * (float64_t)cosf(yaw),
                                    0.5f64 - reach * (float64_t)cosf(pitch) * (float64_t)sinf(yaw),
                                    0.5f64 - reach * (float64_t)sinf(pitch), yaw, pitch);
                const uint8_t *const picture = fr_draw(t, &rig, &camera);
                REQUIRE(picture != nullptr);
                fr_compare_picture(t, &rig, blocks, count, &camera, picture, FR_WIDTH, FR_HEIGHT, &compare);
            }
        }
    }
    printf("  %" PRIu64 " pixels compared, %" PRIu64 " of them hits, %" PRIu64 " wrong\n", compare.compared,
           compare.hits, compare.wrong);
    CHECK_EQ_U64(compare.wrong, 0u);
    /* A test that looks at almost nothing passes too. */
    CHECK(compare.compared > (uint64_t)FR_WIDTH * FR_HEIGHT * 50u);
    for (uint32_t face = 0u; face < SHADE_FACES; face++) {
        CHECK(compare.face_seen[face] > 1000u);
    }
    fr_hand_world_close(&rig);
    fr_rig_end(t, &rig);
}

/* Knowing a kernel can be wrong: the same comparison, with the picture's rows mirrored, has to fail by a long way. */
TEST(the_comparison_can_fail) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    REQUIRE(fr_hand_world_open(t, &rig, 2u, FR_WIDTH, FR_HEIGHT));
    const fr_reference_block_t block = {.cell = {0, 0, 0}, .state = BLOCK_ID_FURNACE};
    CHECK_EQ_U64(world_edit_block_set(&rig.world->edit, 0, 0, 0, block.state), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(fr_hand_world_sync(t, &rig));
    render_camera_t camera;
    render_camera_start(&camera, 0.5f64, 3.0f64, 0.8f64, -FR_PI / 2.0f32, -0.1f32); /* looking south at its front */
    const uint8_t *const picture = fr_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    fr_compare_t good = {0};
    fr_compare_picture(t, &rig, &block, 1u, &camera, picture, FR_WIDTH, FR_HEIGHT, &good);
    CHECK_EQ_U64(good.wrong, 0u);
    CHECK(good.hits > 1000u);
    /* A furnace's front is not its own mirror image. */
    uint8_t *const mirrored = test_alloc(t, (uint64_t)FR_WIDTH * FR_HEIGHT * 4u);
    for (uint32_t y = 0u; y < FR_HEIGHT; y++) {
        for (uint32_t x = 0u; x < FR_WIDTH; x++) {
            memcpy(mirrored + ((uint64_t)y * FR_WIDTH + x) * 4u,
                   picture + ((uint64_t)y * FR_WIDTH + (FR_WIDTH - 1u - x)) * 4u, 4u);
        }
    }
    fr_compare_t bad = {0};
    fr_compare_picture(t, &rig, &block, 1u, &camera, mirrored, FR_WIDTH, FR_HEIGHT, &bad);
    CHECK(bad.wrong > good.hits / 4u);
    fr_hand_world_close(&rig);
    fr_rig_end(t, &rig);
}

TEST(a_whole_section_of_one_block_costs_a_slab_of_128_bytes_and_still_draws) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    REQUIRE(fr_hand_world_open(t, &rig, 2u, FR_WIDTH, FR_HEIGHT));
    /* All of a section: z = 0 is storage 256, the start of one. */
    for (int32_t z = 0; z < 16; z++) {
        for (int32_t y = 0; y < 16; y++) {
            for (int32_t x = 0; x < 16; x++) {
                REQUIRE(world_edit_block_set(&rig.world->edit, x, y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    REQUIRE(fr_hand_world_sync(t, &rig));
    CHECK_EQ_U64(rig.render->buffers.section_count, 1u);
    CHECK_EQ_U64(rig.render->buffers.slab_bytes, 128u);
    render_camera_t camera;
    render_camera_start(&camera, 8.5f64, 8.5f64, 40.5f64, 0.0f32, -1.4f32);
    const uint8_t *const picture = fr_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    /* The ray through the middle goes down to the top of the section, and stone's top under full light is its own bytes. */
    traverse_hit_t hit;
    REQUIRE(device_memory_read(&rig.bridge->device_memory, &rig.render->owned_hits,
                               ((uint64_t)(FR_HEIGHT / 2u) * FR_WIDTH + FR_WIDTH / 2u) * sizeof(traverse_hit_t), &hit,
                               sizeof(hit)) == GPU_STATUS_OK);
    CHECK_EQ_U64(traverse_hit_face(hit.info), TRAVERSE_FACE_POS_Z);
    CHECK_EQ_U64(hit.value & TRAVERSE_VALUE_MATERIAL_MASK, BLOCK_ID_STONE);
    CHECK_EQ_U64(hit.value >> TRAVERSE_VALUE_LIGHT_SHIFT, 0xF0u); /* what a column starts with, under an open sky */
    fr_hand_world_close(&rig);
    fr_rig_end(t, &rig);
}

TEST(a_material_with_no_texture_draws_the_colour_that_shows_the_bug) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    REQUIRE(fr_hand_world_open(t, &rig, 2u, FR_WIDTH, FR_HEIGHT));
    CHECK_EQ_U64(world_edit_block_set(&rig.world->edit, 8, 8, 0, 60000u), WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(fr_hand_world_sync(t, &rig));
    render_camera_t camera;
    render_camera_start(&camera, 8.5f64, 8.5f64, 2.5f64, 0.0f32, -1.553f32);
    const uint8_t *const picture = fr_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    CHECK_EQ_U64(
        fr_pixel(picture, FR_WIDTH, FR_WIDTH / 2u, FR_HEIGHT / 2u),
        post_xrgb(post_tone_map(SHADE_MISSING_R), post_tone_map(SHADE_MISSING_G), post_tone_map(SHADE_MISSING_B)));
    fr_hand_world_close(&rig);
    fr_rig_end(t, &rig);
}

TEST(what_makes_no_sense_is_refused_and_starts_nothing) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    REQUIRE(fr_hand_world_open(t, &rig, 1u, 64u, 64u));
    render_t *const render = rig.render;
    render_frame_t *const frame = &render->frame;
    traverse_camera_t too_large;
    render_camera_t camera;
    render_camera_start(&camera, 0.0f64, 0.0f64, 0.0f64, 0.0f32, 0.0f32);
    render_camera_to_traverse(&camera, 65u, 64u, &too_large);
    const traverse_world_t world = {.borrowed_table = render->buffers.borrowed_table,
                                    .borrowed_pool = render->buffers.borrowed_pool,
                                    .window = render->buffers.window};
    gpu_error_t error = {0};
    CHECK_EQ_U64(shade_frame(&error, &frame->scene, &frame->post, &world, &too_large, &render->owned_hits,
                             &render->owned_shaded),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "hits are too small");
    CHECK_EQ_U64(post_frame(&error, &frame->post, &too_large, &render->owned_shaded, &render->owned_xrgb),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "pixels are too small");
    traverse_camera_t no_pixels = too_large;
    no_pixels.width = 0u;
    CHECK_EQ_U64(shade_frame(&error, &frame->scene, &frame->post, &world, &no_pixels, &render->owned_hits,
                             &render->owned_shaded),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_EQ_U64(post_frame(&error, &frame->post, &no_pixels, &render->owned_shaded, &render->owned_xrgb),
                 GPU_STATUS_INVALID_ARGUMENT);
    post_settings_t backwards = frame->post;
    backwards.fog_start = backwards.fog_end;
    traverse_camera_t fits;
    render_camera_to_traverse(&camera, 64u, 64u, &fits);
    CHECK_EQ_U64(post_frame(&error, &backwards, &fits, &render->owned_shaded, &render->owned_xrgb),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "fog");
    shade_scene_t short_table = frame->scene;
    short_table.material_count = 1u << 24;
    CHECK_EQ_U64(
        shade_frame(&error, &short_table, &frame->post, &world, &fits, &render->owned_hits, &render->owned_shaded),
        GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "table");
    const frame_args_t args = {.borrowed_world = &world,
                               .borrowed_scene = &frame->scene,
                               .borrowed_post = &frame->post,
                               .borrowed_camera = &too_large,
                               .borrowed_targets = &frame->targets,
                               .borrowed_host_xrgb = frame->borrowed_pictures[0]};
    frame_timings_t timings;
    CHECK_EQ_U64(frame_gpu_run(&frame->gpu, &args, &timings), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(frame->gpu.last_error.borrowed_what, "too large");
    frame_gpu_t never_started = {0};
    CHECK_EQ_U64(frame_gpu_run(&never_started, &args, &timings), GPU_STATUS_INVALID_ARGUMENT);
    fr_hand_world_close(&rig);
    fr_rig_end(t, &rig);
}

/* ---- The real path, on real terrain -------------------------------------------------------------------------------------- */

static uint16_t fr_world_block(const world_t *world, const world_buffers_window_t *window, int32_t x, int32_t y,
                               int32_t z) {
    const int64_t column_x = (int64_t)(x >> 4) - window->min_x, column_y = (int64_t)(y >> 4) - window->min_y;
    if (column_x < 0 || column_y < 0 || column_x >= (int64_t)window->side || column_y >= (int64_t)window->side) {
        return 0u;
    }
    uint16_t state = 0u;
    return world_map_block_get(&world->map, x, y, z, &state) == WORLD_MAP_STATUS_OK ? state : (uint16_t)0u;
}

/* An exact trace through the map, voxel by voxel, in float64_t. Out of the window it stops. */
static bool fr_map_trace(const world_t *world, const world_buffers_window_t *window, const int32_t eye_cell[3],
                         const float32_t eye_frac[3], const float64_t direction[3], int32_t out_cell[3]) {
    int32_t cell[3] = {eye_cell[0], eye_cell[1], eye_cell[2]};
    float64_t next[3], step_t[3];
    int32_t step[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        const float64_t frac = (float64_t)eye_frac[i];
        step[i] = direction[i] > 0.0f64 ? 1 : -1;
        const float64_t reach = direction[i] > 0.0f64 ? 1.0f64 - frac : frac;
        const float64_t size = fabs(direction[i]);
        step_t[i] = size > 1e-12f64 ? 1.0f64 / size : 1e30f64;
        next[i] = size > 1e-12f64 ? reach / size : 1e30f64;
    }
    const int32_t x_low = window->min_x * 16, y_low = window->min_y * 16;
    const int32_t wide = (int32_t)window->side * 16;
    for (uint32_t i = 0u; i < 4096u; i++) {
        if (cell[0] < x_low || cell[1] < y_low || cell[0] >= x_low + wide || cell[1] >= y_low + wide ||
            cell[2] < COLUMN_Z_STORAGE_MIN || cell[2] >= COLUMN_Z_STORAGE_END) {
            return false;
        }
        if (fr_world_block(world, window, cell[0], cell[1], cell[2]) != 0u) {
            out_cell[0] = cell[0];
            out_cell[1] = cell[1];
            out_cell[2] = cell[2];
            return true;
        }
        const uint32_t axis = next[0] < next[1] ? (next[0] < next[2] ? 0u : 2u) : (next[1] < next[2] ? 1u : 2u);
        cell[axis] += step[axis];
        next[axis] += step_t[axis];
    }
    return false;
}

typedef struct fr_terrain_tally_t {
    uint64_t rays;              /* 0 */
    uint64_t hits;              /* 8 */
    uint64_t voxel_not_block;   /* 16: the card says a block is here, and the map says otherwise */
    uint64_t material_wrong;    /* 24 */
    uint64_t light_wrong;       /* 32 */
    uint64_t reference_differs; /* 40 */
    uint64_t not_adjacent;      /* 48: of those, the two disagree by more than a neighbouring voxel */
    uint64_t pixel_wrong;       /* 56 */
    uint64_t pixels;            /* 64 */
} fr_terrain_tally_t;
static_assert(sizeof(fr_terrain_tally_t) == 72);

/* One pixel from its hit, on the host, with the same lines the kernels ran. */
static uint32_t fr_expected_pixel(const fr_rig_t *rig, const traverse_camera_t *view, const traverse_hit_t *hit,
                                  uint32_t px, uint32_t py) {
    const float32_t a = 2.0f32 * ((float32_t)px + 0.5f32) / (float32_t)view->width - 1.0f32;
    const float32_t b = 1.0f32 - 2.0f32 * ((float32_t)py + 0.5f32) / (float32_t)view->height;
    float32_t direction[3];
    float32_t squared = 0.0f32;
    for (uint32_t i = 0u; i < 3u; i++) {
        direction[i] = view->forward[i] + a * view->right[i] + b * view->up[i];
        squared += direction[i] * direction[i];
    }
    const float32_t scale = 1.0f32 / sqrtf(squared);
    for (uint32_t i = 0u; i < 3u; i++) {
        direction[i] *= scale;
    }
    const post_settings_t *const post = &rig->render->frame.post;
    float32_t sky[3];
    post_sky(post, direction, sky);
    const uint32_t face = traverse_hit_face(hit->info);
    float32_t colour[3] = {0.0f32, 0.0f32, 0.0f32};
    float32_t distance = TRAVERSE_COARSE_SKY;
    if (face != TRAVERSE_FACE_MISS) {
        distance = hit->t;
        const uint32_t material = hit->value & TRAVERSE_VALUE_MATERIAL_MASK;
        const uint32_t lookup = face == TRAVERSE_FACE_INSIDE ? (uint32_t)TRAVERSE_FACE_POS_Z : face;
        uint32_t id = 0u;
        texture_info_t info;
        if (!texture_find(rig->texture, block_texture_names[block_defs[material].faces[lookup]], &id) ||
            !texture_info_get(rig->texture, id, &info)) {
            return 0xFFFFFFFFu;
        }
        const texture_blob_entry_t *const entry = info.borrowed_entry;
        const uint32_t level = shade_mip_level(hit->t, shade_pixel_tangent(view), entry->width, entry->level_count);
        texture_mip_t mip;
        if (!texture_mip_get(rig->texture, id, level, 0u, &mip)) {
            return 0xFFFFFFFFu;
        }
        float32_t local[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            local[i] = view->eye_frac[i] + direction[i] * hit->t - (float32_t)hit->offset[i];
        }
        float32_t u = 0.0f32, v = 0.0f32;
        shade_face_uv(face, local, &u, &v);
        const uint32_t x = shade_texel_of(u, mip.width), y = shade_texel_of(v, mip.height);
        const uint8_t *const texel = mip.borrowed_pixels + ((uint64_t)y * mip.width + x) * 4u;
        const float32_t cover = (float32_t)texel[3] / 255.0f32;
        for (uint32_t i = 0u; i < 3u; i++) {
            const float32_t seen = shade_srgb_to_linear((float32_t)texel[i] / 255.0f32);
            const float32_t behind = shade_srgb_to_linear((float32_t)entry->mean_rgba[i] / 255.0f32);
            colour[i] = (texel[3] == 255u ? seen : behind + (seen - behind) * cover) * shade_face_brightness(face);
        }
    }
    const float32_t fog = post_fog(post, distance);
    return post_xrgb(post_tone_map(colour[0] + (sky[0] - colour[0]) * fog),
                     post_tone_map(colour[1] + (sky[1] - colour[1]) * fog),
                     post_tone_map(colour[2] + (sky[2] - colour[2]) * fog));
}

/* What the card drew from the real path, against the world in memory. */
static void fr_check_terrain_picture(test_context_t *t, const fr_rig_t *rig, const render_camera_t *camera,
                                     const uint8_t *picture, uint32_t width, uint32_t height,
                                     fr_terrain_tally_t *tally) {
    render_t *const render = rig->render;
    const world_t *const world = rig->world;
    traverse_camera_t view;
    render_camera_to_traverse(camera, width, height, &view);
    const uint64_t bytes = traverse_hits_bytes(width, height);
    traverse_hit_t *const hits = test_alloc(t, bytes);
    REQUIRE(device_memory_read(&rig->bridge->device_memory, &render->owned_hits, 0u, hits, bytes) == GPU_STATUS_OK);
    const world_buffers_window_t *const window = &render->buffers.window;
    for (uint32_t py = 0u; py < height; py++) {
        for (uint32_t px = 0u; px < width; px++) {
            const traverse_hit_t *const hit = &hits[(uint64_t)py * width + px];
            const uint32_t face = traverse_hit_face(hit->info);
            tally->pixels++;
            if (!fr_bytes_near(fr_pixel(picture, width, px, py), fr_expected_pixel(rig, &view, hit, px, py), 1u)) {
                tally->pixel_wrong++;
            }
            int32_t card_cell[3] = {0, 0, 0};
            if (face != TRAVERSE_FACE_MISS) {
                tally->hits++;
                for (uint32_t i = 0u; i < 3u; i++) {
                    card_cell[i] = view.eye_cell[i] + (int32_t)hit->offset[i];
                }
                const uint16_t state = fr_world_block(world, window, card_cell[0], card_cell[1], card_cell[2]);
                tally->voxel_not_block += state == 0u ? 1u : 0u;
                tally->material_wrong += state != (hit->value & TRAVERSE_VALUE_MATERIAL_MASK) ? 1u : 0u;
                uint8_t light = 0u;
                if (world_map_light_get(&world->map, card_cell[0], card_cell[1], card_cell[2], &light) ==
                    WORLD_MAP_STATUS_OK) {
                    tally->light_wrong += light != (hit->value >> TRAVERSE_VALUE_LIGHT_SHIFT) ? 1u : 0u;
                }
            }
            /* Every 3rd pixel each way, since this one walks voxel by voxel through the map. */
            if (px % 3u != 0u || py % 3u != 0u) {
                continue;
            }
            tally->rays++;
            const float64_t a = 2.0f64 * ((float64_t)px + 0.5f64) / (float64_t)width - 1.0f64;
            const float64_t b = 1.0f64 - 2.0f64 * ((float64_t)py + 0.5f64) / (float64_t)height;
            float64_t direction[3];
            float64_t squared = 0.0f64;
            for (uint32_t i = 0u; i < 3u; i++) {
                direction[i] = (float64_t)view.forward[i] + a * (float64_t)view.right[i] + b * (float64_t)view.up[i];
                squared += direction[i] * direction[i];
            }
            const float64_t length = sqrt(squared);
            for (uint32_t i = 0u; i < 3u; i++) {
                direction[i] /= length;
            }
            int32_t reference_cell[3] = {0, 0, 0};
            const bool reference_hit =
                fr_map_trace(world, window, view.eye_cell, view.eye_frac, direction, reference_cell);
            const bool card_hit = face != TRAVERSE_FACE_MISS;
            if (reference_hit != card_hit) {
                tally->reference_differs++;
                tally->not_adjacent++;
            } else if (card_hit && (card_cell[0] != reference_cell[0] || card_cell[1] != reference_cell[1] ||
                                    card_cell[2] != reference_cell[2])) {
                tally->reference_differs++;
                if (abs(card_cell[0] - reference_cell[0]) > 1 || abs(card_cell[1] - reference_cell[1]) > 1 ||
                    abs(card_cell[2] - reference_cell[2]) > 1) {
                    tally->not_adjacent++;
                }
            }
        }
    }
}

static const game_config_t fr_small_game = {.render_distance = 6u,
                                            .width = FR_WIDTH,
                                            .height = FR_HEIGHT,
                                            .features_off =
                                                SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                            .show_date = 0u};

static void fr_check_tally(test_context_t *t, const fr_terrain_tally_t *tally) {
    printf("  %" PRIu64 " pixels, %" PRIu64 " hits, picture wrong on %" PRIu64 "; %" PRIu64
           " rays against the map, differing on %" PRIu64 ", %" PRIu64 " of those by more than a neighbour\n",
           tally->pixels, tally->hits, tally->pixel_wrong, tally->rays, tally->reference_differs, tally->not_adjacent);
    CHECK(tally->hits > tally->pixels / 4u);
    CHECK_EQ_U64(tally->voxel_not_block, 0u);
    CHECK_EQ_U64(tally->material_wrong, 0u);
    CHECK_EQ_U64(tally->light_wrong, 0u);
    /* The host's floats and the card's can land on either side of a texel's edge, and a place's rays on either side of a corner
     * of a voxel: Stage 08.3 issue 4. These rates are far above what was measured, and far below any real fault. */
    CHECK(tally->pixel_wrong * 200u <= tally->pixels);
    CHECK(tally->reference_differs * 500u <= tally->rays);
    CHECK(tally->not_adjacent * 2000u <= tally->rays);
}

/* What the game streams into its map is the real world, every stage of it, and not something else that looks like ground. The
 * goldens could not say so: they would look the same on any world that had been made again. */
TEST(the_columns_the_game_streams_are_what_terrain_generates) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_small_game));
    REQUIRE(game_settle(game));
    const int32_t storage_voxels = (COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN) * (int32_t)(COLUMN_SIZE * COLUMN_SIZE);
    uint16_t *const states = test_alloc(t, (uint64_t)storage_voxels * sizeof(uint16_t));
    uint8_t *const opened = test_alloc(t, (uint64_t)storage_voxels);
    const int32_t reach = (int32_t)fr_small_game.render_distance;
    uint64_t columns = 0u, wrong = 0u, stored_outside = 0u;
    uint8_t *const seen = test_alloc(t, BLOCK_COUNT);
    memset(seen, 0, BLOCK_COUNT);
    /* Round where the eye started, which is where the game looks for land and not always the world's middle. */
    const int32_t eye_chunk_x = column_chunk_of((int32_t)floor(rig.game->camera.position[0]));
    const int32_t eye_chunk_y = column_chunk_of((int32_t)floor(rig.game->camera.position[1]));
    for (int32_t chunk_y = eye_chunk_y - reach; chunk_y <= eye_chunk_y + reach; chunk_y++) {
        for (int32_t chunk_x = eye_chunk_x - reach; chunk_x <= eye_chunk_x + reach; chunk_x++) {
            const column_t *const column = world_map_column_at(&rig.world->map, chunk_x, chunk_y);
            if (column == nullptr) {
                continue;
            }
            columns++;
            const terrain_density_box_t box = {
                .origin_x = chunk_x * (int32_t)COLUMN_SIZE,
                .origin_y = chunk_y * (int32_t)COLUMN_SIZE,
                .origin_z = COLUMN_Z_STORAGE_MIN,
                .count_x = COLUMN_SIZE,
                .count_y = COLUMN_SIZE,
                .count_z = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
            };
            terrain_generate_box_fallback(rig.terrain, &box, opened, states);
            for (int32_t z = COLUMN_Z_STORAGE_MIN; z < COLUMN_Z_STORAGE_END; z++) {
                const uint64_t layer = (uint64_t)(z - COLUMN_Z_STORAGE_MIN) * (COLUMN_SIZE * COLUMN_SIZE);
                for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
                    for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
                        const uint16_t want = states[layer + y * COLUMN_SIZE + x];
                        uint16_t block = BLOCK_ID_AIR;
                        if (column_block_get(column, x, y, z, &block)) {
                            wrong += block != want ? 1u : 0u;
                            seen[want < BLOCK_COUNT ? want : 0u] = 1u;
                        } else {
                            /* Past gameplay's range, where nothing can read it, nothing should have been made. */
                            stored_outside += want != BLOCK_ID_AIR ? 1u : 0u;
                        }
                    }
                }
            }
        }
    }
    printf("  %" PRIu64 " columns compared\n", columns);
    CHECK(columns >= 9u);
    CHECK_EQ_U64(wrong, 0u);
    CHECK_EQ_U64(stored_outside, 0u);
    /* The stages' own blocks, which the density alone never makes. */
    CHECK(seen[BLOCK_ID_BEDROCK] != 0u);
    CHECK(seen[BLOCK_ID_DEEPSLATE] != 0u);
    CHECK(seen[BLOCK_ID_COAL_ORE] != 0u || seen[BLOCK_ID_IRON_ORE] != 0u);
    CHECK(seen[BLOCK_ID_LAVA] != 0u);
    game_close(game);
    fr_rig_end(t, &rig);
}

TEST(real_terrain_through_the_real_path_is_what_the_map_holds) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_small_game));
    REQUIRE(game_settle(game));
    CHECK_EQ_U64(rig.render->buffers.pool_full_count, 0u);
    CHECK(rig.render->buffers.section_count > 100u);
    /* This compares voxels with the map, so every voxel is drawn whole. Shapes and holes have their test: tests/cuda/shapes/. */
    render_frame_features_set(&rig.render->frame, 0u);
    /* What the pool allows a column: the real world must fit it, or a whole render distance would not. */
    CHECK(rig.render->buffers.slab_bytes < (uint64_t)rig.world->map.ready_count * RENDER_POOL_BYTES_PER_COLUMN);
    fr_terrain_tally_t tally = {0};
    const uint8_t *picture = fr_draw(t, &rig, &game->camera);
    REQUIRE(picture != nullptr);
    fr_check_terrain_picture(t, &rig, &game->camera, picture, FR_WIDTH, FR_HEIGHT, &tally);
    /* Looking around from the same place. */
    for (uint32_t i = 0u; i < 4u; i++) {
        const render_fly_input_t turn = {.mouse_dx = 300, .mouse_dy = i % 2u == 0u ? 60 : -60};
        game_fly(game, &turn, 0.0f32);
        picture = fr_draw(t, &rig, &game->camera);
        REQUIRE(picture != nullptr);
        fr_check_terrain_picture(t, &rig, &game->camera, picture, FR_WIDTH, FR_HEIGHT, &tally);
    }
    fr_check_tally(t, &tally);
    game_close(game);
    fr_rig_end(t, &rig);
}

/* The map follows the eye, so columns wrap in the table, and what left a slot must not be seen in it again. */
TEST(flying_far_scrolls_the_table_and_the_picture_stays_right) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_small_game));
    render_frame_features_set(&rig.render->frame, 0u); /* as above: this compares voxels */
    fr_terrain_tally_t tally = {0};
    uint64_t columns_before = 0u;
    for (uint32_t i = 0u; i < 6u; i++) {
        REQUIRE(game_settle(game));
        CHECK(i == 0u || game->columns_made > columns_before);
        columns_before = game->columns_made;
        const uint8_t *const picture = fr_draw(t, &rig, &game->camera);
        REQUIRE(picture != nullptr);
        fr_check_terrain_picture(t, &rig, &game->camera, picture, FR_WIDTH, FR_HEIGHT, &tally);
        /* About 6 chunks a go, and past a whole table in the last of these: every slot gets a new column. */
        const render_fly_input_t fly = {.right = (int8_t)(i % 2u == 0u ? 1 : 0), .forward = 1};
        game_fly(game, &fly, i == 4u ? 40.0f32 : 5.0f32);
    }
    fr_check_tally(t, &tally);
    CHECK(rig.world->map.center_x != 0 || rig.world->map.center_y != 0);
    game_close(game);
    fr_rig_end(t, &rig);
}

/* ---- Golden pictures ----------------------------------------------------------------------------------------------------- */

typedef struct fr_golden_view_t {
    const char *path;    /* 0 */
    float64_t offset[3]; /* 8: from where the eye starts */
    float32_t yaw;       /* 32 */
    float32_t pitch;     /* 36 */
} fr_golden_view_t;
static_assert(sizeof(fr_golden_view_t) == 40);

static const fr_golden_view_t fr_golden_views[] = {
    {"tests/run/render/frame/fixtures/start.png", {0.0f64, 0.0f64, 0.0f64}, 0.6f32, -0.3f32},
    {"tests/run/render/frame/fixtures/north_low.png", {4.0f64, 2.0f64, -18.0f64}, 1.6f32, -0.25f32},
    {"tests/run/render/frame/fixtures/down.png", {-3.0f64, 5.0f64, 30.0f64}, 3.9f32, -1.2f32},
};

static void fr_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(FR_WIDTH, FR_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, FR_WIDTH * 4u, FR_WIDTH, FR_HEIGHT, file, capacity, &written));
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
    REQUIRE(info.width == FR_WIDTH && info.height == FR_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u, exact = 0u;
    for (uint64_t i = 0u; i < (uint64_t)FR_WIDTH * FR_HEIGHT; i++) {
        uint32_t ours = 0u;
        memcpy(&ours, picture + i * 4u, 4u);
        const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
        wrong += fr_bytes_near(ours & 0xFFFFFFu, golden, FR_GOLDEN_TOLERANCE) ? 0u : 1u;
        exact += (ours & 0xFFFFFFu) == golden ? 1u : 0u;
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", path, exact,
           (uint64_t)FR_WIDTH * FR_HEIGHT, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

TEST(golden_pictures_match) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_small_game));
    const float64_t start[3] = {game->camera.position[0], game->camera.position[1], game->camera.position[2]};
    for (uint32_t v = 0u; v < sizeof(fr_golden_views) / sizeof(fr_golden_views[0]); v++) {
        const fr_golden_view_t *const view = &fr_golden_views[v];
        for (uint32_t i = 0u; i < 3u; i++) {
            game->camera.position[i] = start[i] + view->offset[i];
        }
        game->camera.yaw = view->yaw;
        game->camera.pitch = view->pitch;
        const render_fly_input_t still = {0};
        game_fly(game, &still, 0.0f32);
        REQUIRE(game_settle(game));
        const uint8_t *const picture = fr_draw(t, &rig, &game->camera);
        REQUIRE(picture != nullptr);
        CHECK_EQ_U64(rig.render->buffers.pool_full_count, 0u);
        fr_golden_compare(t, view->path, picture);
    }
    game_close(game);
    fr_rig_end(t, &rig);
}

/* ---- The built program --------------------------------------------------------------------------------------------------- */

typedef struct fr_temp_dir_t {
    char path[64]; /* 0 */
} fr_temp_dir_t;
static_assert(sizeof(fr_temp_dir_t) == 64);

static bool fr_temp_dir_make(fr_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_frame_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void fr_temp_dir_remove(const fr_temp_dir_t *dir) {
    DIR *const handle = opendir(dir->path);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir->path, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir->path);
}

static const char fr_flight_script[] = "0 screenshot still.png\n"
                                       "1 key w down\n"
                                       "1 move 40 10\n"
                                       "10 screenshot moved.png\n"
                                       "11 key w up\n"
                                       "12 end\n";

static void fr_program_flight(test_context_t *t, const fr_temp_dir_t *dir, const char *script, test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)dir->path,
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          nullptr};
    test_process_run(t, argv, FR_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *fr_file_read(test_context_t *t, const fr_temp_dir_t *dir, const char *name, uint64_t *out_length) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = display_png_bytes_needed(160u, 90u);
    uint8_t *const bytes = test_alloc(t, capacity);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, out_length), FILE_OK)) {
        *out_length = 0u;
    }
    return bytes;
}

TEST(the_program_flies_a_script_and_gives_the_same_bytes_each_time) {
    fr_temp_dir_t first, second;
    REQUIRE(fr_temp_dir_make(&first));
    REQUIRE(fr_temp_dir_make(&second));
    char script[128];
    snprintf(script, sizeof(script), "%s/flight.script", first.path);
    REQUIRE(file_write_atomic(script, fr_flight_script, sizeof(fr_flight_script) - 1u) == FILE_OK);
    test_process_t run;
    fr_program_flight(t, &first, script, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "frames of 160x90");
    CHECK_CONTAINS(run.output, "13 frames, 2 screenshots");
    fr_program_flight(t, &second, script, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    uint64_t still_a = 0u, still_b = 0u, moved_a = 0u, moved_b = 0u;
    const uint8_t *const still_first = fr_file_read(t, &first, "still.png", &still_a);
    const uint8_t *const still_second = fr_file_read(t, &second, "still.png", &still_b);
    const uint8_t *const moved_first = fr_file_read(t, &first, "moved.png", &moved_a);
    const uint8_t *const moved_second = fr_file_read(t, &second, "moved.png", &moved_b);
    REQUIRE(still_a == still_b && moved_a == moved_b && still_a == moved_a && still_a != 0u);
    CHECK_MEM_EQ(still_first, still_second, still_a);
    CHECK_MEM_EQ(moved_first, moved_second, moved_a);
    /* And flying changed the picture. */
    CHECK(memcmp(still_first, moved_first, still_a) != 0);
    fr_temp_dir_remove(&first);
    fr_temp_dir_remove(&second);
}

TEST(the_program_refuses_what_makes_no_sense) {
    test_process_t run;
    char *const distance_alone[] = {t->borrowed_args[0],
                                    (char *)"--headless",
                                    (char *)"--ticks",
                                    (char *)"1",
                                    (char *)"--render-distance",
                                    (char *)"4",
                                    nullptr};
    test_process_run(t, distance_alone, FR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "only means something with --test-world");
    char *const seed_alone[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--seed", (char *)"7", nullptr};
    test_process_run(t, seed_alone, FR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "--seed only means something with --test-world");
    char *const seed_wrong[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--test-world",
                                (char *)"--seed",    (char *)"7up",        nullptr};
    test_process_run(t, seed_wrong, FR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "--seed wants a whole number");
    char *const too_far[] = {t->borrowed_args[0],         (char *)"--headless", (char *)"--test-world",
                             (char *)"--render-distance", (char *)"513",        nullptr};
    test_process_run(t, too_far, FR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "goes up to 512");
    char *const with_report[] = {t->borrowed_args[0], (char *)"--test-world", (char *)"--report", nullptr};
    test_process_run(t, with_report, FR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "does not go with");
    char *const with_pattern[] = {t->borrowed_args[0], (char *)"--test-world", (char *)"--test-pattern", nullptr};
    test_process_run(t, with_pattern, FR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    /* No script and no tick count: the headless backend says so, and nothing was left behind. */
    char *const nothing[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--test-world", nullptr};
    test_process_run(t, nothing, FR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_HEADLESS);
    CHECK_CONTAINS(run.output, "0 bytes live");
}

/* ---- The real threads ---------------------------------------------------------------------------------------------------- */

TEST(the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_small_game));
    const uint64_t bytes = post_xrgb_bytes(FR_WIDTH, FR_HEIGHT);
    uint8_t *const screen = fr_aligned(t, bytes);
    uint8_t *const last = fr_aligned(t, bytes);
    display_buffer_t target = {
        .owned_map = screen, .map_bytes = bytes, .pitch = FR_WIDTH * 4u, .width = FR_WIDTH, .height = FR_HEIGHT};
    REQUIRE(game_loop_start(game, nullptr));

    /* Flying, so that the table scrolls under the threads, by a clock as a real frame goes, and looking down, at the ground. */
    const render_fly_input_t look_down = {.mouse_dy = 200};
    REQUIRE(game_loop_frame(game, &look_down, 0.0f32, &target));
    const render_fly_input_t fly = {.forward = 1, .fast = 1};
    const render_fly_input_t still = {0};
    for (uint32_t i = 0u; i < 300u; i++) {
        REQUIRE(game_loop_frame(game, &fly, 1.0f32 / 120.0f32, &target));
        time_sleep_ns(1000000);
    }
    /* Then until two seconds' worth of frames in a row come out the same: everything has arrived. */
    heap_counters_t before = {0};
    uint32_t same = 0u;
    int64_t started_ns = time_now_ns();
    uint64_t pictures = game->pictures_shown;
    for (;;) {
        REQUIRE(game_loop_frame(game, &still, 1.0f32 / 120.0f32, &target));
        /* Only a new picture says anything, and only once the sim has no more columns to come: on a loaded machine, frames can be the same for
         * seconds because nothing has been drawn. The sim's word is read only to wait, as its last word comes round again. */
        const bool quiet = __atomic_load_n(&game->stream_quiet, __ATOMIC_RELAXED) != 0u;
        if (!quiet) {
            same = 0u;
        } else if (game->pictures_shown != pictures) {
            same = memcmp(screen, last, bytes) == 0 ? same + 1u : 0u;
        }
        pictures = game->pictures_shown;
        memcpy(last, screen, bytes);
        if (same == 200u) {
            heap_counters_read(&before);
        }
        if (same >= 400u) {
            break;
        }
        REQUIRE(time_now_ns() - started_ns < 120ll * TIME_NS_PER_SECOND);
        time_sleep_ns(1000000);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    REQUIRE(game_loop_stop(game));

    render_t *const render = rig.render;
    CHECK(render->frame.stats.frames > 10u);
    CHECK(game->columns_made > 100u);
    CHECK_EQ_U64(render->buffers.pool_full_count, 0u);
    /* One thread now, from the same place, with the world as it stands. */
    REQUIRE(game_settle(game));
    const uint8_t *const picture = fr_draw(t, &rig, &game->camera);
    REQUIRE(picture != nullptr);
    CHECK_MEM_EQ(picture, screen, bytes);
    /* A picture of something, and not of an empty sky. */
    const uint64_t hit_bytes = traverse_hits_bytes(FR_WIDTH, FR_HEIGHT);
    traverse_hit_t *const hits = test_alloc(t, hit_bytes);
    REQUIRE(device_memory_read(&rig.bridge->device_memory, &render->owned_hits, 0u, hits, hit_bytes) == GPU_STATUS_OK);
    uint64_t hit_count = 0u;
    for (uint64_t i = 0u; i < (uint64_t)FR_WIDTH * FR_HEIGHT; i++) {
        hit_count += traverse_hit_face(hits[i].info) != TRAVERSE_FACE_MISS ? 1u : 0u;
    }
    CHECK(hit_count > (uint64_t)FR_WIDTH * FR_HEIGHT / 8u);
    game_close(game);
    fr_rig_end(t, &rig);
}

TEST(a_frame_takes_nothing_from_the_heap_and_the_stats_add_up) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_small_game));
    REQUIRE(game_settle(game));
    heap_counters_t before, after;
    heap_counters_read(&before);
    enum : uint32_t { FR_FRAMES = 200u };
    for (uint32_t i = 0u; i < FR_FRAMES; i++) {
        const render_fly_input_t turn = {.mouse_dx = 5};
        game_fly(game, &turn, 0.05f32);
        REQUIRE(game_settle(game));
        REQUIRE(fr_draw(t, &rig, &game->camera) != nullptr);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    render_frame_t *const frame = &rig.render->frame;
    CHECK_EQ_U64(frame->stats.frames, FR_FRAMES);
    CHECK_EQ_U64(frame->stats.readback_bytes, FR_FRAMES * post_xrgb_bytes(FR_WIDTH, FR_HEIGHT));
    float32_t p50 = 0.0f32, p99 = 0.0f32;
    render_frame_percentiles(frame, &p50, &p99);
    CHECK(p50 > 0.0f32 && p99 >= p50 && p99 <= frame->stats.max_total_ms);
    float64_t pass_sum = 0.0f64;
    for (uint32_t i = 0u; i < FRAME_PASS_COUNT; i++) {
        CHECK(frame->stats.sum_ms[i] > 0.0f64);
        pass_sum += frame->stats.sum_ms[i];
    }
    CHECK_NEAR(pass_sum, frame->stats.sum_total_ms, 0.01f64);
    game_close(game);
    fr_rig_end(t, &rig);
}

TEST(a_world_can_be_opened_again_after_one_is_closed_and_nothing_is_left) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    for (uint32_t i = 0u; i < 3u; i++) {
        REQUIRE(game_open(game, &fr_small_game));
        REQUIRE(game_settle(game));
        REQUIRE(fr_draw(t, &rig, &game->camera) != nullptr);
        game_close(game);
        CHECK(gpu_bridge_exit_check(rig.bridge));
    }
    const game_config_t no_distance = {.render_distance = 0u, .width = FR_WIDTH, .height = FR_HEIGHT, .show_date = 0u};
    CHECK(!game_open(game, &no_distance));
    fr_rig_end(t, &rig);
}

/* ---- Plan 008 phase 01: what a picture's time goes on ---------------------------------------------------------------- */

/* Every effect of play, AO and TAA included, which the tests above leave off. */
static const game_config_t fr_play_game = {.render_distance = 6u,
                                           .width = FR_WIDTH,
                                           .height = FR_HEIGHT,
                                           .features_off = SHADE_FEATURE_DAY_CYCLE,
                                           .show_date = 0u};

/* A frame as the game draws it, with the watch's marks, from camera. */
static const frame_marks_t *fr_draw_marked(test_context_t *t, fr_rig_t *rig, const render_camera_t *camera) {
    game_watch_t *const watch = game_watch_of(rig->game);
    game_watch_marks_arm(watch, &rig->render->frame.gpu, time_now_ns());
    if (fr_draw(t, rig, camera) == nullptr) {
        return nullptr;
    }
    return watch->borrowed_marks;
}

/* Looking out over the ground the game opened on, from a little above it, so that nearly every pixel has a surface. */
static void fr_marked_camera(const game_t *game, render_camera_t *out) {
    *out = game->camera;
    out->pitch = -0.45f32;
}

TEST(shade_spans_add_up_to_the_shade_pass) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_play_game));
    REQUIRE(game_settle(game));
    REQUIRE(game_watch_of(game)->borrowed_marks != nullptr);
    render_camera_t camera;
    fr_marked_camera(game, &camera);
    float32_t most_gap = 0.0f32;
    for (uint32_t i = 0u; i < 30u; i++) {
        camera.yaw += 0.02f32;
        const frame_marks_t *const marks = fr_draw_marked(t, &rig, &camera);
        REQUIRE(marks != nullptr);
        const frame_timings_t *const timings = &rig.render->frame.last_timings;
        float32_t sum = 0.0f32;
        for (uint32_t span = 0u; span < SHADE_SPAN_COUNT; span++) {
            CHECK(marks->shade_ms[span] >= 0.0f32);
            sum += marks->shade_ms[span];
        }
        const float32_t gap = fabsf(sum - timings->kernel_ms[FRAME_KERNEL_SHADE]);
        most_gap = gap > most_gap ? gap : most_gap;
        CHECK(gap <= 0.01f32);
        /* Every effect is on, so each kernel ran and took some time. */
        CHECK(marks->shade_ms[SHADE_SPAN_SURFACES] > 0.0f32);
        CHECK(marks->shade_ms[SHADE_SPAN_AO] > 0.0f32);
    }
    printf("  the five spans are within %.4f ms of the shade pass on every frame\n", (libc_double_t)most_gap);
    game_close(game);
    fr_rig_end(t, &rig);
}

/* Ten frames turning, from a world just opened, with the watch's marks or with none; the last picture into out. */
static bool fr_marked_run(test_context_t *t, fr_rig_t *rig, bool marked, uint8_t *out) {
    game_t *const game = rig->game;
    if (!CHECK(game_open(game, &fr_play_game)) || !CHECK(game_settle(game))) {
        return false;
    }
    render_camera_t camera;
    fr_marked_camera(game, &camera);
    const uint8_t *picture = nullptr;
    for (uint32_t i = 0u; i < 10u; i++) {
        camera.yaw += 0.03f32;
        if (marked) {
            game_watch_marks_arm(game_watch_of(game), &rig->render->frame.gpu, time_now_ns());
        } else {
            rig->render->frame.gpu.borrowed_marks = nullptr;
        }
        picture = fr_draw(t, rig, &camera);
        if (picture == nullptr) {
            game_close(game);
            return false;
        }
    }
    memcpy(out, picture, post_xrgb_bytes(FR_WIDTH, FR_HEIGHT));
    game_close(game);
    return true;
}

/* The marks are clock events, a clock kernel that writes only its own word, and four bytes copied home: no picture changes with them. */
TEST(the_marks_change_no_picture) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    const uint64_t bytes = post_xrgb_bytes(FR_WIDTH, FR_HEIGHT);
    uint8_t *const with = fr_aligned(t, bytes);
    uint8_t *const without = fr_aligned(t, bytes);
    REQUIRE(fr_marked_run(t, &rig, true, with));
    REQUIRE(fr_marked_run(t, &rig, false, without));
    CHECK_MEM_EQ(with, without, bytes);
    fr_rig_end(t, &rig);
}

/* The first frame has no AO history, so every pixel with a surface is fresh: what the tests below count against. */
static uint32_t fr_surfaces_first(test_context_t *t, fr_rig_t *rig, const render_camera_t *camera) {
    const frame_marks_t *const marks = fr_draw_marked(t, rig, camera);
    return marks != nullptr ? marks->fresh_count : 0u;
}

/* Measured, plan 008 phase 01: a still camera with TAA is not quite free of fresh pixels. The jitter moves where each pixel looks by under a
 * pixel, and at an edge that can be a surface neither it nor its neighbours had on the last picture: 72 to 114 of 27067 here, 0.3 to 0.4 %.
 * The phase file's first guess was none. */
TEST(a_still_camera_has_almost_no_fresh_pixels_after_the_first_frame) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_play_game));
    REQUIRE(game_settle(game));
    render_camera_t camera;
    fr_marked_camera(game, &camera);
    const uint32_t surfaces = fr_surfaces_first(t, &rig, &camera);
    REQUIRE(surfaces > FR_WIDTH * FR_HEIGHT / 4u);
    uint32_t most = 0u;
    for (uint32_t frame = 2u; frame <= 10u; frame++) {
        const frame_marks_t *const marks = fr_draw_marked(t, &rig, &camera);
        REQUIRE(marks != nullptr);
        most = marks->fresh_count > most ? marks->fresh_count : most;
    }
    printf("  the first frame has %" PRIu32 " fresh pixels of %u; frames 2 to 10 at most %" PRIu32 "\n", surfaces,
           FR_WIDTH * FR_HEIGHT, most);
    CHECK((float64_t)most <= 0.01f64 * (float64_t)surfaces);
    game_close(game);
    fr_rig_end(t, &rig);
}

TEST(the_fresh_count_is_the_strip_on_a_turn) {
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &fr_play_game));
    REQUIRE(game_settle(game));
    render_camera_t camera;
    fr_marked_camera(game, &camera);
    const uint32_t surfaces = fr_surfaces_first(t, &rig, &camera);
    REQUIRE(surfaces > FR_WIDTH * FR_HEIGHT / 4u);
    for (uint32_t frame = 1u; frame < 8u; frame++) {
        REQUIRE(fr_draw_marked(t, &rig, &camera) != nullptr);
    }
    /* The strip a 12 degree turn brings in, by the master's section 1 C3: the share of the width, in tangents, between the edge and
     * where the edge was. Measured here at this camera's own field of view and shape. */
    const float64_t half_tangent = (float64_t)camera.half_fov_tangent * (float64_t)FR_WIDTH / (float64_t)FR_HEIGHT;
    const float64_t half_width = atan(half_tangent);
    const float64_t turn = 12.0f64 * (float64_t)FR_PI / 180.0f64;
    const float64_t strip = (half_tangent - tan(half_width - turn)) / (2.0f64 * half_tangent);
    camera.yaw -= (float32_t)turn;
    const frame_marks_t *const marks = fr_draw_marked(t, &rig, &camera);
    REQUIRE(marks != nullptr);
    /* Sky has no surface to be fresh, so the share is of the pixels that have one. */
    const float64_t share = (float64_t)marks->fresh_count / (float64_t)surfaces;
    printf("  a 12 degree turn: %" PRIu32 " fresh pixels, %.1f %% of the %" PRIu32
           " with a surface; the strip is %.1f %%\n",
           marks->fresh_count, (libc_double_t)(share * 100.0f64), surfaces, (libc_double_t)(strip * 100.0f64));
    /* Neighbours on the last picture lower it, never raise it. */
    CHECK(share <= strip + 0.01f64);
    CHECK(share >= strip * 0.5f64);
    game_close(game);
    fr_rig_end(t, &rig);
}

/* Lines and commas in a frame log, and that its seq and times go up. */
static bool fr_log_check(test_context_t *t, const char *text, uint64_t length, uint64_t *out_lines) {
    uint64_t lines = 0u, commas = 0u, header_commas = 0u;
    float64_t last_time = -1.0f64;
    int64_t last_seq = -1;
    const char *line = text;
    for (uint64_t i = 0u; i < length; i++) {
        if (text[i] == ',') {
            commas++;
        }
        if (text[i] != '\n') {
            continue;
        }
        if (lines == 0u) {
            header_commas = commas;
            CHECK(strncmp(line, "seq,taken_s,interval_ms", 23u) == 0);
        } else {
            CHECK_EQ_U64(commas, header_commas);
            char *end = nullptr;
            const int64_t seq = strtoll(line, &end, 10);
            const float64_t taken = strtod(end + 1, nullptr);
            CHECK(seq > last_seq);
            CHECK(taken >= last_time);
            last_seq = seq;
            last_time = taken;
        }
        commas = 0u;
        lines++;
        line = text + i + 1u;
    }
    *out_lines = lines;
    return header_commas >= 30u;
}

TEST(the_frame_log_has_one_line_per_picture) {
    fr_temp_dir_t dir;
    REQUIRE(fr_temp_dir_make(&dir));
    char path[FILE_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/frames.csv", dir.path);
    fr_rig_t rig;
    REQUIRE(fr_rig_begin(t, &rig));
    game_t *const game = rig.game;
    game_frame_log_set(game, path);
    REQUIRE(game_open(game, &fr_play_game));
    const uint64_t bytes = post_xrgb_bytes(FR_WIDTH, FR_HEIGHT);
    display_buffer_t target = {.owned_map = fr_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = FR_WIDTH * 4u,
                               .width = FR_WIDTH,
                               .height = FR_HEIGHT};
    REQUIRE(game_loop_start(game, nullptr));
    const render_fly_input_t turn = {.mouse_dx = 20};
    const int64_t started_ns = time_now_ns();
    int64_t next_ns = started_ns;
    while (time_now_ns() - started_ns < 2ll * TIME_NS_PER_SECOND) {
        REQUIRE(game_loop_frame(game, &turn, 1.0f32 / 120.0f32, &target));
        next_ns += TIME_NS_PER_SECOND / 120;
        time_sleep_until_ns(next_ns);
    }
    REQUIRE(game_loop_stop(game));
    const uint64_t taken = game->pictures_shown;
    /* F3's last second, read as main would, while the rings are still there. */
    game_watch_recent_t recent;
    CHECK(game_watch_recent_read(game_watch_of(game), time_now_ns(), &recent) || taken == 0u);
    game_close(game);
    fr_rig_end(t, &rig);
    uint64_t length = 0u;
    const uint64_t capacity = 4ull << 20;
    uint8_t *const text = test_alloc(t, capacity);
    REQUIRE(file_read_whole(path, text, capacity, &length) == FILE_OK);
    uint64_t lines = 0u;
    CHECK(fr_log_check(t, (const char *)text, length, &lines));
    printf("  %" PRIu64 " pictures taken in 2 s, %" PRIu64 " lines in the log\n", taken, lines);
    CHECK(taken > 10u);
    CHECK_EQ_U64(lines, taken + 1u);
    fr_temp_dir_remove(&dir);
}

TEST_MAIN()
