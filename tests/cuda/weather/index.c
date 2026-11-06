/* ctest render_feature_weather. See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/weather/index.h"
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
    WE_WIDTH = 320u,
    WE_HEIGHT = 180u,
    WE_PIXELS = WE_WIDTH * WE_HEIGHT,
    WE_RANDOM_RAYS = 10000u,
    WE_PROGRAM_TIMEOUT_MS = 120000u,
    WE_SIDE = POST_WEATHER_MAP_SIDE,
};

#define WE_PI 3.14159265358979f32
#define WE_TICKS 1234u

#include "cuda/sky/fixtures/rig.h"

/* ---- The arithmetic, on numbers worked out by hand ------------------------------------------------------------------------- */

static uint64_t we_random(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static float32_t we_unit(uint64_t *state) {
    return (float32_t)(we_random(state) >> 40) / 16777216.0f32;
}

/* An eye at the middle of the cell (0, 0, 0). */
static traverse_camera_t we_eye(int32_t z) {
    traverse_camera_t camera;
    memset(&camera, 0, sizeof(camera));
    camera.eye_cell[2] = z;
    camera.eye_frac[0] = 0.5f32;
    camera.eye_frac[1] = 0.5f32;
    camera.eye_frac[2] = 0.5f32;
    return camera;
}

static void we_normalize(float32_t v[3]) {
    const float32_t length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
}

TEST(a_clear_sky_changes_nothing) {
    /* No drops and no bolt: every colour comes back to the bit, whatever it is. */
    post_weather_t weather;
    memset(&weather, 0, sizeof(weather));
    weather.wind[0] = 12.0f32;
    weather.fog_mix = 0.0f32;
    const traverse_camera_t camera = we_eye(64);
    uint64_t state = 0x1234567887654321ull;
    uint32_t same = 0u;
    for (uint32_t i = 0u; i < WE_RANDOM_RAYS; i++) {
        float32_t colour[3] = {we_unit(&state) * 4.0f32, we_unit(&state), we_unit(&state) * 20.0f32};
        float32_t before[3];
        memcpy(before, colour, sizeof(before));
        float32_t direction[3] = {we_unit(&state) - 0.5f32, we_unit(&state) - 0.5f32, we_unit(&state) - 0.5f32};
        we_normalize(direction);
        post_weather_apply(&weather, &camera, nullptr, direction, we_unit(&state) * 50.0f32, colour);
        same += memcmp(colour, before, sizeof(before)) == 0 ? 1u : 0u;
    }
    CHECK_EQ_U64(same, WE_RANDOM_RAYS);
    /* And the sky, the light and the fog of a clear day are render_day_fill's to the bit, at any time of day. */
    render_weather_t clear;
    memset(&clear, 0, sizeof(clear));
    clear.active = 1u;
    clear.bolt_age = -1.0f32;
    clear.age_now = -1.0f32;
    const int32_t eye_cell[3] = {100, -200, 64};
    for (uint32_t step = 0u; step < 24u; step++) {
        post_settings_t day, weathered;
        shade_sun_t sun_day, sun_weathered;
        memset(&day, 0, sizeof(day));
        memset(&sun_day, 0, sizeof(sun_day));
        day.fog_start = 347.2f32;
        day.fog_end = 496.0f32;
        render_day_fill((float32_t)step / 24.0f32, &day, &sun_day);
        weathered = day;
        sun_weathered = sun_day;
        post_weather_t out;
        render_weather_apply(&clear, eye_cell, true, 496.0f32, &weathered, &sun_weathered, &out);
        CHECK_MEM_EQ(&weathered, &day, sizeof(day));
        CHECK_MEM_EQ(&sun_weathered, &sun_day, sizeof(sun_day));
        CHECK(out.density == 0.0f32 && out.bolt_alpha == 0.0f32 && out.fog_mix == 0.0f32);
    }
}

/* A cell of layer 0's rain that holds a drop at density, from round 0 up, and where in it. */
static void we_find_drop(float32_t density, uint32_t *out_round, int32_t *out_up, float32_t *out_across,
                         float32_t *out_cell_up) {
    float32_t phase = 0.0f32;
    for (int32_t up = 10; up < 100000; up++) {
        for (uint32_t round = 0u; round < post_weather_cells_round(0u); round++) {
            if (post_weather_cell(0u, false, round, up, density, out_across, out_cell_up, &phase)) {
                *out_round = round;
                *out_up = up;
                return;
            }
        }
    }
}

TEST(drops_by_hand) {
    const uint32_t cells = post_weather_cells_round(0u);
    CHECK_EQ_U64(cells, 50u); /* 2 pi 2 / 0.25 */
    post_weather_t weather;
    memset(&weather, 0, sizeof(weather));
    weather.density = 0.25f32; /* 10 mm an hour: 10 / 40 */
    weather.drop_rgb[0] = 1.0f32;
    weather.drop_rgb[1] = 1.0f32;
    weather.drop_rgb[2] = 1.0f32;
    uint32_t round = 0u;
    int32_t up = 0;
    float32_t across = 0.0f32, cell_up = 0.0f32;
    we_find_drop(weather.density, &round, &up, &across, &cell_up);
    /* A level ray at the eye's height, straight at the drop's middle on the 2-block cylinder, with the fall's clock where the drop
     * is at the ray's height: covered by POST_RAIN_ALPHA. A wall at 3 blocks keeps the outer layers out of it. */
    const traverse_camera_t camera = we_eye(0);
    const float32_t angle = (((float32_t)round + across) / (float32_t)cells - 0.5f32) * 2.0f32 * WE_PI;
    float32_t direction[3] = {cosf(angle), sinf(angle), 0.0f32};
    weather.time_s = (((float32_t)up + cell_up) * POST_WEATHER_CELL_HEIGHT - 0.5f32) / POST_RAIN_FALL;
    float32_t drops[4];
    post_weather_drops(&weather, &camera, nullptr, direction, 3.0f32, drops);
    CHECK_NEAR(drops[3], POST_RAIN_ALPHA, 1e-6f32);
    CHECK_NEAR(drops[0], POST_RAIN_SHADE * POST_RAIN_ALPHA, 1e-6f32);
    /* 0.1 blocks across, past the drop's 0.03: nothing. Toward the middle of the cell, so the ray stays in it. */
    const float32_t aside = (across < 0.5f32 ? 0.1f32 : -0.1f32) / POST_WEATHER_RADIUS_FIRST;
    direction[0] = cosf(angle + aside);
    direction[1] = sinf(angle + aside);
    post_weather_drops(&weather, &camera, nullptr, direction, 3.0f32, drops);
    CHECK(drops[3] == 0.0f32);
    /* Behind a wall nearer than the cylinder: nothing either. */
    direction[0] = cosf(angle);
    direction[1] = sinf(angle);
    post_weather_drops(&weather, &camera, nullptr, direction, 1.9f32, drops);
    CHECK(drops[3] == 0.0f32);

    /* Wind 6 blocks a second from the east: the streaks lean 6 / 9 = 0.67 sideways for each block down, so the drop's streak
     * through a place 1 block above the eye is 0.67 blocks east of where it is with no wind: upwind. */
    weather.wind[0] = -6.0f32;
    float32_t tilt[2];
    post_weather_tilt(&weather, POST_RAIN_FALL, tilt);
    CHECK_NEAR(tilt[0], -0.6666667f32, 1e-6f32);
    CHECK_NEAR(tilt[1], 0.0f32, 1e-9f32);
    CHECK_NEAR(atanf(-tilt[0]) * 180.0f32 / WE_PI, 33.69f32, 0.01f32);
    const float32_t place[3] = {2.0f32 * cosf(angle) + 0.6666667f32, 2.0f32 * sinf(angle), 1.0f32};
    float32_t at[3] = {place[0], place[1], place[2]};
    we_normalize(at);
    const float32_t reach = sqrtf(place[0] * place[0] + place[1] * place[1] + place[2] * place[2]);
    weather.time_s = (((float32_t)up + cell_up) * POST_WEATHER_CELL_HEIGHT - 1.5f32) / POST_RAIN_FALL;
    post_weather_drops(&weather, &camera, nullptr, at, reach + 0.5f32, drops);
    CHECK_NEAR(drops[3], POST_RAIN_ALPHA, 1e-6f32);
    /* The same place with no wind is off the cylinder, and its ray meets no drop there. */
    weather.wind[0] = 0.0f32;
    post_weather_drops(&weather, &camera, nullptr, at, reach + 0.5f32, drops);
    CHECK(drops[3] == 0.0f32);

    /* Snow: round flakes, drawn at POST_SNOW_ALPHA, white by day. */
    weather.snow_share = 1.0f32;
    weather.density = 0.85f32;
    uint64_t state = 99u;
    uint32_t snowed = 0u;
    for (uint32_t i = 0u; i < WE_RANDOM_RAYS; i++) {
        float32_t ray[3] = {we_unit(&state) - 0.5f32, we_unit(&state) - 0.5f32, 0.6f32 * (we_unit(&state) - 0.5f32)};
        we_normalize(ray);
        weather.time_s = we_unit(&state) * 100.0f32;
        post_weather_drops(&weather, &camera, nullptr, ray, 1e30f32, drops);
        snowed += drops[3] > 0.0f32 ? 1u : 0u;
        if (drops[3] > 0.0f32) {
            CHECK(drops[3] >= POST_SNOW_ALPHA - 1e-6f32);
        }
    }
    printf("  snow covers %" PRIu32 " of %" PRIu32 " random rays\n", snowed, WE_RANDOM_RAYS);
    CHECK(snowed > 100u);
}

TEST(rain_stops_under_a_roof) {
    /* The eye at 65 under a roof whose block is at 70, everywhere round: no ray up meets a drop before the roof. */
    int16_t *const roof = test_alloc(t, post_weather_map_bytes());
    int16_t *const open = test_alloc(t, post_weather_map_bytes());
    for (uint32_t i = 0u; i < WE_SIDE * WE_SIDE; i++) {
        roof[i] = 70;
        open[i] = 64;
    }
    post_weather_t weather;
    memset(&weather, 0, sizeof(weather));
    weather.density = 0.85f32;
    weather.wind[0] = 5.0f32;
    weather.map_origin[0] = -32;
    weather.map_origin[1] = -32;
    const traverse_camera_t camera = we_eye(65);
    uint64_t state = 7u;
    uint32_t under = 0u, outside = 0u;
    for (uint32_t i = 0u; i < 1000u; i++) {
        const float32_t elevation = 0.35f32 + 0.85f32 * we_unit(&state);
        const float32_t heading = 2.0f32 * WE_PI * we_unit(&state);
        const float32_t ray[3] = {cosf(elevation) * cosf(heading), cosf(elevation) * sinf(heading), sinf(elevation)};
        weather.time_s = 10.0f32 * we_unit(&state);
        float32_t drops[4];
        /* What the ray hits first is the roof's underside at 70. */
        post_weather_drops(&weather, &camera, roof, ray, (70.0f32 - 65.5f32) / ray[2], drops);
        under += drops[3] > 0.0f32 ? 1u : 0u;
        post_weather_drops(&weather, &camera, open, ray, 1e30f32, drops);
        outside += drops[3] > 0.0f32 ? 1u : 0u;
    }
    printf("  rays up meeting a drop: %" PRIu32 " under the roof, %" PRIu32 " in the open\n", under, outside);
    CHECK_EQ_U64(under, 0u);
    CHECK(outside > 20u);
    /* The plan's example: from 65 under a roof at 70, a drop at height 68 in the open column (64) is drawn, one at 67 under the
     * roof is not. Heights from the eye cell, as the kernels have them. */
    roof[(32u + 2u) * WE_SIDE + 32u] = 70;
    open[32u * WE_SIDE + 35u] = 64;
    const float32_t drop_open[3] = {3.5f32, 0.5f32, 68.0f32 - 65.0f32};
    const float32_t drop_roofed[3] = {0.5f32, 2.5f32, 67.0f32 - 65.0f32};
    CHECK(post_weather_open_sky(&weather, &camera, open, drop_open));
    CHECK(!post_weather_open_sky(&weather, &camera, roof, drop_roofed));
    /* A column not loaded is a roof, and so is anywhere off the map. */
    open[32u * WE_SIDE + 35u] = POST_WEATHER_MAP_COVERED;
    CHECK(!post_weather_open_sky(&weather, &camera, open, drop_open));
    const float32_t far_away[3] = {40.5f32, 0.5f32, 3.0f32};
    CHECK(!post_weather_open_sky(&weather, &camera, open, far_away));
}

TEST(drops_never_in_front_of_a_wall) {
    /* A wall at 1.5 blocks, nearer than the nearest cylinder: no drop at all with no wind. */
    post_weather_t weather;
    memset(&weather, 0, sizeof(weather));
    weather.density = 0.85f32;
    weather.snow_share = 0.5f32;
    const traverse_camera_t camera = we_eye(10);
    uint64_t state = 31u;
    uint32_t covered = 0u, beyond = 0u;
    for (uint32_t i = 0u; i < WE_RANDOM_RAYS; i++) {
        float32_t ray[3] = {we_unit(&state) - 0.5f32, we_unit(&state) - 0.5f32, we_unit(&state) - 0.5f32};
        we_normalize(ray);
        weather.time_s = we_unit(&state) * 1000.0f32;
        float32_t drops[4];
        post_weather_drops(&weather, &camera, nullptr, ray, 1.5f32, drops);
        covered += drops[3] > 0.0f32 ? 1u : 0u;
    }
    CHECK_EQ_U64(covered, 0u);
    /* In a gale the leaned cylinders come nearer looking along the wind: whatever a layer draws is where its ray meets it, before
     * the wall, worked out apart from the kernel's own lines. */
    weather.wind[0] = -20.0f32;
    weather.wind[1] = 5.0f32;
    weather.snow_share = 0.0f32;
    for (uint32_t i = 0u; i < WE_RANDOM_RAYS; i++) {
        float32_t ray[3] = {we_unit(&state) - 0.5f32, we_unit(&state) - 0.5f32, we_unit(&state) - 0.5f32};
        we_normalize(ray);
        weather.time_s = we_unit(&state) * 1000.0f32;
        for (uint32_t layer = 0u; layer < POST_WEATHER_LAYERS; layer++) {
            if (post_weather_layer(&weather, &camera, nullptr, ray, 1.5f32, layer, false, weather.density) > 0.0f32) {
                const float64_t tilt_x = -20.0f64 / 9.0f64, tilt_y = 5.0f64 / 9.0f64;
                const float64_t lean_x = (float64_t)ray[0] + (float64_t)ray[2] * tilt_x;
                const float64_t lean_y = (float64_t)ray[1] + (float64_t)ray[2] * tilt_y;
                const float64_t r = 2.0f64 * (float64_t)(1u << layer) / sqrt(lean_x * lean_x + lean_y * lean_y);
                beyond += r >= 1.5f64 ? 1u : 0u;
            }
        }
    }
    CHECK_EQ_U64(beyond, 0u);
}

TEST(the_bolt_by_hand) {
    /* A bolt straight down 10 blocks east of the eye, from 100 up to the ground. */
    post_weather_t weather;
    memset(&weather, 0, sizeof(weather));
    weather.bolt_alpha = 1.0f32;
    weather.bolt_width = POST_BOLT_WIDTH + POST_BOLT_WIDTH_PER_BLOCK * 10.0f32;
    for (uint32_t j = 0u; j <= POST_BOLT_SEGMENTS; j++) {
        weather.bolt[j][0] = 10.5f32;
        weather.bolt[j][1] = 0.5f32;
        weather.bolt[j][2] = 100.0f32 * (1.0f32 - (float32_t)j / (float32_t)POST_BOLT_SEGMENTS);
    }
    const traverse_camera_t camera = we_eye(0);
    const float32_t at[3] = {1.0f32, 0.0f32, 0.0f32};
    CHECK_NEAR(post_weather_bolt(&weather, &camera, at, 1e30f32), 1.0f32, 1e-5f32);
    /* Its middle seen from below and to the side too. */
    float32_t up[3] = {10.0f32, 0.0f32, 30.0f32};
    we_normalize(up);
    CHECK(post_weather_bolt(&weather, &camera, up, 1e30f32) > 0.9f32);
    /* A block off to the side: nothing. */
    float32_t aside[3] = {10.0f32, 1.0f32, 0.0f32};
    we_normalize(aside);
    CHECK(post_weather_bolt(&weather, &camera, aside, 1e30f32) == 0.0f32);
    /* Behind a hill at 5 blocks: nothing. */
    CHECK(post_weather_bolt(&weather, &camera, at, 5.0f32) == 0.0f32);
    /* And over a pixel: its colour. */
    float32_t colour[3] = {0.1f32, 0.2f32, 0.3f32};
    post_weather_apply(&weather, &camera, nullptr, at, 1e30f32, colour);
    CHECK_NEAR(colour[0], POST_BOLT_R, 1e-4f32);
    CHECK_NEAR(colour[2], POST_BOLT_B, 1e-4f32);
    /* Its flicker: full, half, full over 0.2 s, and the flash by distance: 1, 0.5 at 500 blocks, 0.1 at 1500. */
    CHECK(render_weather_flicker(0.0f32) == 1.0f32);
    CHECK(render_weather_flicker(0.1f32) == 0.5f32);
    CHECK(render_weather_flicker(0.19f32) == 1.0f32);
    CHECK(render_weather_flicker(0.2f32) == 0.0f32);
    CHECK(render_weather_flicker(-1.0f32) == 0.0f32);
    CHECK_NEAR(game_weather_flash(0.0f32), 1.0f32, 1e-6f32);
    CHECK_NEAR(game_weather_flash(500.0f32), 0.5f32, 1e-6f32);
    CHECK_NEAR(game_weather_flash(1500.0f32), 0.1f32, 1e-6f32);
}

TEST(fog_colour_by_hand) {
    /* Render distance 32: the fog ends a chunk short, at 31 x 16 = 496 blocks. */
    const float32_t fog_end = 496.0f32;
    CHECK_NEAR(render_weather_visibility(fog_end, 0.0f32, 0.0f32, 30.0f32), 124.0f32, 1e-3f32); /* a thunderstorm */
    CHECK_NEAR(render_weather_visibility(fog_end, 0.9f32, 0.0f32, 0.0f32), 116.56f32, 1e-3f32); /* 117, a foggy coast */
    CHECK_NEAR(render_weather_visibility(fog_end, 0.0f32, 0.9f32, 0.0f32), 116.56f32, 1e-3f32); /* dust the same */
    CHECK_NEAR(render_weather_visibility(fog_end, 0.0f32, 0.0f32, 0.0f32), 496.0f32, 1e-3f32);  /* clear */
    CHECK_NEAR(render_weather_visibility(fog_end, 1.0f32, 0.0f32, 400.0f32), 32.0f32, 1e-6f32); /* never under 32 */
    /* What the frame's fog becomes: starting at 0.3 of what is seen. */
    render_weather_t storm;
    memset(&storm, 0, sizeof(storm));
    storm.active = 1u;
    storm.rain_mm_h = 30.0f32;
    storm.age_now = -1.0f32;
    post_settings_t post;
    shade_sun_t sun;
    memset(&post, 0, sizeof(post));
    memset(&sun, 0, sizeof(sun));
    render_day_fill(0.5f32, &post, &sun);
    post_weather_t out;
    const int32_t eye[3] = {0, 0, 0};
    render_weather_apply(&storm, eye, true, fog_end, &post, &sun, &out);
    CHECK_NEAR(post.fog_end, 124.0f32, 1e-3f32);
    CHECK_NEAR(post.fog_start, 37.2f32, 1e-3f32);
    /* Toward grey by the cloud, times the daylight, which is all of it at noon under no cloud. */
    storm.cloud = 0.5f32;
    storm.rain_mm_h = 0.0f32;
    render_day_fill(0.5f32, &post, &sun);
    const float32_t sky_before = post.sky_zenith[2];
    const float32_t grey = 0.3f32 * post.sky_zenith[0] + 0.59f32 * post.sky_zenith[1] + 0.11f32 * post.sky_zenith[2];
    const float32_t direct_before = sun.direct[0];
    render_weather_apply(&storm, eye, true, fog_end, &post, &sun, &out);
    CHECK_NEAR(post.sky_zenith[2], sky_before + (grey - sky_before) * 0.4f32, 1e-5f32);
    CHECK_NEAR(sun.direct[0], direct_before * (1.0f32 - 0.375f32), 1e-6f32);
    CHECK_NEAR(out.fog_mix, 0.5f32, 1e-6f32);
    /* Noon's ambient green is 0.62, the whole day; cloud 0.5 cuts it by 0.15, so the grey is 0.85 of itself. */
    CHECK_NEAR(out.fog_rgb[0], 0.55f32 * 0.85f32, 1e-5f32);
    CHECK_NEAR(out.fog_rgb[2], 0.60f32 * 0.85f32, 1e-5f32);
    /* The colour a pixel's fog goes to, by fog_mix. */
    const float32_t sky[3] = {0.2f32, 0.4f32, 0.8f32};
    float32_t mixed[3];
    post_weather_fog_colour(&out, sky, mixed);
    CHECK_NEAR(mixed[0], 0.2f32 + (out.fog_rgb[0] - 0.2f32) * 0.5f32, 1e-6f32);
    CHECK_NEAR(mixed[2], 0.8f32 + (out.fog_rgb[2] - 0.8f32) * 0.5f32, 1e-6f32);
    /* Dust goes brown. */
    storm.cloud = 0.2f32;
    storm.dust = 0.9f32;
    render_weather_apply(&storm, eye, true, fog_end, &post, &sun, &out);
    CHECK_NEAR(out.fog_mix, 0.9f32, 1e-6f32);
    CHECK(out.fog_rgb[0] > out.fog_rgb[2]);
}

/* ---- Pictures, on the real card ------------------------------------------------------------------------------------------- */

/* A stone house with a doorway in its south wall and a flat roof, on a stone floor that goes on round it. */
static bool we_house_put(test_context_t *t, sk_rig_t *rig) {
    static const int32_t boxes[][7] = {
        /* low x, y, z, high x, y, z (not included), block */
        {-16, -16, -2, 48, 48, -1, BLOCK_ID_GRASS_BLOCK}, /* the ground, its top at z = -1 */
        {10, 10, -1, 21, 21, 5, BLOCK_ID_STONE_BRICKS},   /* the house, solid at first */
        {11, 11, -1, 20, 20, 4, BLOCK_ID_AIR},            /* hollowed, the roof at z = 4 */
        {14, 10, -1, 17, 11, 2, BLOCK_ID_AIR},            /* the doorway, 3 wide and 3 high */
        {11, 11, -2, 20, 20, -1, BLOCK_ID_OAK_PLANKS},    /* a wooden floor */
    };
    for (uint32_t b = 0u; b < sizeof(boxes) / sizeof(boxes[0]); b++) {
        for (int32_t z = boxes[b][2]; z < boxes[b][5]; z++) {
            for (int32_t y = boxes[b][1]; y < boxes[b][4]; y++) {
                for (int32_t x = boxes[b][0]; x < boxes[b][3]; x++) {
                    const world_edit_status_t status =
                        world_edit_block_set(&rig->world->edit, x, y, z, (uint16_t)boxes[b][6]);
                    if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                        return false;
                    }
                }
            }
        }
    }
    return sk_hand_world_sync(t, rig);
}

/* A slot of this kind as the sim makes it, at the eye's place, with the rain top of the hand world round it. */
static void we_slot_make(sk_rig_t *rig, game_weather_kind_t kind, const float64_t eye[3], float64_t strike_south,
                         render_weather_slot_t *out) {
    memset(out, 0, sizeof(*out));
    climate_now_t now;
    game_weather_forced_now(kind, &now);
    out->world_ticks = WE_TICKS;
    out->rain_mm_h = now.rain_mm_h;
    out->snow_share = now.snow_share;
    out->wind[0] = now.wind[0];
    out->wind[1] = now.wind[1];
    out->cloud = now.cloud;
    out->fog = now.fog;
    out->dust = now.dust;
    out->forced = 1u;
    out->sent = 1u;
    out->bolt_age = -1.0f32;
    out->map_origin[0] = (int32_t)floor(eye[0]) - (int32_t)WE_SIDE / 2;
    out->map_origin[1] = (int32_t)floor(eye[1]) - (int32_t)WE_SIDE / 2;
    out->map_revision = 1u + (uint32_t)kind;
    for (uint32_t row = 0u; row < WE_SIDE; row++) {
        for (uint32_t across = 0u; across < WE_SIDE; across++) {
            out->map[row * WE_SIDE + across] = game_weather_column_top(rig->world, out->map_origin[0] + (int32_t)across,
                                                                       out->map_origin[1] + (int32_t)row);
        }
    }
    if (strike_south > 0.0f64) {
        /* South of the eye by this much, where it looks. */
        const float64_t place[2] = {eye[0], eye[1] - strike_south};
        game_weather_strike_t bolt;
        game_weather_bolt_make(TERRAIN_SEED_DEFAULT, 7u, place, -1.0f64, (float64_t)COLUMN_Z_GAMEPLAY_MAX, eye, &bolt);
        out->bolt_age = 0.0f32;
        out->bolt_distance = bolt.distance;
        out->flash = game_weather_flash(bolt.distance);
        memcpy(out->bolt, bolt.bolt, sizeof(out->bolt));
        memcpy(out->bolt_origin, bolt.origin, sizeof(out->bolt_origin));
    }
}

static const uint8_t *we_draw(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera,
                              const render_weather_t *weather) {
    render_t *const render = rig->render;
    const gpu_status_t status =
        render_frame_draw_weather(&render->frame, &render->buffers, camera, weather, &render->owned_weather_map);
    if (!CHECK_EQ_U64(status, GPU_STATUS_OK)) {
        gpu_error_log(&render->frame.gpu.last_error);
        return nullptr;
    }
    const uint8_t *const picture = render_frame_take(&render->frame);
    CHECK(picture != nullptr);
    return picture;
}

/* The weather of a slot, as the thread that draws takes it. */
static bool we_take(test_context_t *t, sk_rig_t *rig, const render_weather_slot_t *slot, render_weather_t *out) {
    memset(out, 0, sizeof(*out));
    return CHECK_EQ_U64(render_weather_take(out, slot, 0, &rig->bridge->device_memory, &rig->render->owned_weather_map),
                        GPU_STATUS_OK);
}

static void we_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(WE_WIDTH, WE_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, WE_WIDTH * 4u, WE_WIDTH, WE_HEIGHT, file, capacity, &written));
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
    REQUIRE(info.width == WE_WIDTH && info.height == WE_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u;
    for (uint32_t y = 0u; y < WE_HEIGHT; y++) {
        for (uint32_t x = 0u; x < WE_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * WE_WIDTH + x;
            const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
            wrong += sk_bytes_near(sk_pixel(picture, WE_WIDTH, x, y), golden, 0u) ? 0u : 1u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels differ\n", path, wrong, (uint64_t)WE_PIXELS);
    CHECK_EQ_U64(wrong, 0u);
}

static bool we_rig_house(test_context_t *t, sk_rig_t *rig) {
    if (!sk_rig_begin(t, rig) || !sk_hand_world_open(t, rig, 3u, WE_WIDTH, WE_HEIGHT, 496.0f32)) {
        return false;
    }
    return we_house_put(t, rig);
}

/* From inside, three blocks back from the doorway, looking south out of it at the ground and the sky beyond. */
static void we_camera_doorway(render_camera_t *camera, float64_t eye[3]) {
    eye[0] = 15.5f64;
    eye[1] = 13.5f64;
    eye[2] = 0.62f64;
    render_camera_start(camera, eye[0], eye[1], eye[2], -WE_PI / 2.0f32, 0.12f32);
}

/* A golden picture: where it is kept, and of what. */
typedef struct we_kind_t {
    const char *path;         /* 0 */
    game_weather_kind_t kind; /* 8 */
    uint8_t strike;           /* 9 */
    uint8_t _pad0[6];         /* 10 */
} we_kind_t;
static_assert(sizeof(we_kind_t) == 16);

TEST(golden_pictures_of_each_weather) {
    sk_rig_t rig;
    REQUIRE(we_rig_house(t, &rig));
    render_camera_t camera;
    float64_t eye[3];
    we_camera_doorway(&camera, eye);
    static const we_kind_t kinds[] = {
        {"tests/cuda/weather/fixtures/clear.png", GAME_WEATHER_CLEAR, 0u, {0}},
        {"tests/cuda/weather/fixtures/rain.png", GAME_WEATHER_RAIN, 0u, {0}},
        {"tests/cuda/weather/fixtures/snow.png", GAME_WEATHER_SNOW, 0u, {0}},
        {"tests/cuda/weather/fixtures/thunder.png", GAME_WEATHER_THUNDER, 1u, {0}},
        {"tests/cuda/weather/fixtures/fog.png", GAME_WEATHER_FOG, 0u, {0}},
        {"tests/cuda/weather/fixtures/dust.png", GAME_WEATHER_DUST, 0u, {0}},
        {"tests/cuda/weather/fixtures/hurricane.png", GAME_WEATHER_HURRICANE, 0u, {0}},
    };
    const uint8_t *clear = nullptr;
    uint8_t *const kept = test_alloc(t, (uint64_t)WE_PIXELS * 4u);
    for (uint32_t k = 0u; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        render_weather_slot_t *const slot = test_alloc(t, sizeof(render_weather_slot_t));
        we_slot_make(&rig, kinds[k].kind, eye, kinds[k].strike != 0u ? 74.0f64 : 0.0f64, slot);
        render_weather_t weather;
        REQUIRE(we_take(t, &rig, slot, &weather));
        const uint8_t *const picture = we_draw(t, &rig, &camera, &weather);
        REQUIRE(picture != nullptr);
        we_golden_compare(t, kinds[k].path, picture);
        /* The same slot again is the same picture, to the byte. */
        uint8_t *const first = test_alloc(t, (uint64_t)WE_PIXELS * 4u);
        memcpy(first, picture, (uint64_t)WE_PIXELS * 4u);
        const uint8_t *const again = we_draw(t, &rig, &camera, &weather);
        REQUIRE(again != nullptr);
        CHECK_MEM_EQ(again, first, (uint64_t)WE_PIXELS * 4u);
        if (k == 0u) {
            memcpy(kept, first, (uint64_t)WE_PIXELS * 4u);
            clear = kept;
        } else {
            CHECK(memcmp(first, clear, (uint64_t)WE_PIXELS * 4u) != 0); /* every weather shows */
        }
    }
    /* With the setting off, any weather is the clear day of a frame with none: render_frame_draw's own picture. */
    const uint8_t *const none = sk_draw(t, &rig, &camera);
    REQUIRE(none != nullptr);
    uint8_t *const none_kept = test_alloc(t, (uint64_t)WE_PIXELS * 4u);
    memcpy(none_kept, none, (uint64_t)WE_PIXELS * 4u);
    render_frame_features_set(&rig.render->frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WEATHER);
    render_weather_slot_t *const storm = test_alloc(t, sizeof(render_weather_slot_t));
    we_slot_make(&rig, GAME_WEATHER_HURRICANE, eye, 0.0f64, storm);
    render_weather_t weather;
    REQUIRE(we_take(t, &rig, storm, &weather));
    const uint8_t *const off = we_draw(t, &rig, &camera, &weather);
    REQUIRE(off != nullptr);
    CHECK_MEM_EQ(off, none_kept, (uint64_t)WE_PIXELS * 4u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* Pixels of the top third that differ between two pictures. */
static uint32_t we_top_differences(const uint8_t *a, const uint8_t *b) {
    uint32_t count = 0u;
    for (uint32_t y = 0u; y < WE_HEIGHT / 3u; y++) {
        for (uint32_t x = 0u; x < WE_WIDTH; x++) {
            count += sk_pixel(a, WE_WIDTH, x, y) != sk_pixel(b, WE_WIDTH, x, y) ? 1u : 0u;
        }
    }
    return count;
}

TEST(rain_stops_under_a_roof_in_the_real_game) {
    sk_rig_t rig;
    REQUIRE(we_rig_house(t, &rig));
    /* Looking up, inside the house and then out on the ground south of it: the rain, against the same weather with no drops. */
    static const float64_t places[2][2] = {{15.5f64, 15.5f64}, {15.5f64, 0.5f64}};
    uint32_t differ[2] = {0u, 0u};
    uint8_t *const dry = test_alloc(t, (uint64_t)WE_PIXELS * 4u);
    for (uint32_t p = 0u; p < 2u; p++) {
        const float64_t eye[3] = {places[p][0], places[p][1], 0.62f64};
        render_camera_t camera;
        render_camera_start(&camera, eye[0], eye[1], eye[2], 0.3f32, 1.1f32);
        render_weather_slot_t *const slot = test_alloc(t, sizeof(render_weather_slot_t));
        we_slot_make(&rig, GAME_WEATHER_RAIN, eye, 0.0f64, slot);
        slot->rain_mm_h = 30.0f32; /* heavy, so the open sky is full of drops */
        render_weather_t weather;
        REQUIRE(we_take(t, &rig, slot, &weather));
        render_weather_t no_drops = weather;
        no_drops.density = 0.0f32;
        const uint8_t *picture = we_draw(t, &rig, &camera, &no_drops);
        REQUIRE(picture != nullptr);
        memcpy(dry, picture, (uint64_t)WE_PIXELS * 4u);
        picture = we_draw(t, &rig, &camera, &weather);
        REQUIRE(picture != nullptr);
        differ[p] = we_top_differences(picture, dry);
    }
    printf("  drop pixels in the top third: %" PRIu32 " under the roof, %" PRIu32 " outside\n", differ[0], differ[1]);
    CHECK_EQ_U64(differ[0], 0u);
    CHECK(differ[1] > 200u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* The mean of the bytes of the bottom half, which is ground. */
static float64_t we_ground_brightness(const uint8_t *picture) {
    uint64_t sum = 0u;
    for (uint32_t y = WE_HEIGHT / 2u; y < WE_HEIGHT; y++) {
        for (uint32_t x = 0u; x < WE_WIDTH; x++) {
            const uint32_t pixel = sk_pixel(picture, WE_WIDTH, x, y);
            sum += ((pixel >> 16) & 255u) + ((pixel >> 8) & 255u) + (pixel & 255u);
        }
    }
    return (float64_t)sum / (3.0f64 * (float64_t)(WE_WIDTH * (WE_HEIGHT / 2u)));
}

TEST(a_flash_lights_the_land) {
    sk_rig_t rig;
    REQUIRE(we_rig_house(t, &rig));
    /* Out on the ground, looking south over it, in a thunderstorm: the tick before a strike 300 blocks off, and the tick of it. */
    const float64_t eye[3] = {15.5f64, 4.5f64, 1.62f64};
    render_camera_t camera;
    render_camera_start(&camera, eye[0], eye[1], eye[2], -WE_PI / 2.0f32, -0.35f32);
    render_weather_slot_t *const before = test_alloc(t, sizeof(render_weather_slot_t));
    render_weather_slot_t *const flash = test_alloc(t, sizeof(render_weather_slot_t));
    we_slot_make(&rig, GAME_WEATHER_THUNDER, eye, 0.0f64, before);
    we_slot_make(&rig, GAME_WEATHER_THUNDER, eye, 300.0f64, flash);
    flash->world_ticks = before->world_ticks + 1u;
    CHECK_NEAR(flash->flash, game_weather_flash(flash->bolt_distance), 1e-6f32);
    render_weather_t weather;
    REQUIRE(we_take(t, &rig, before, &weather));
    const uint8_t *picture = we_draw(t, &rig, &camera, &weather);
    REQUIRE(picture != nullptr);
    const float64_t dark = we_ground_brightness(picture);
    /* The next tick's slot, eased into as the thread that draws does. */
    REQUIRE(CHECK_EQ_U64(
        render_weather_take(&weather, flash, 0, &rig.bridge->device_memory, &rig.render->owned_weather_map),
        GPU_STATUS_OK));
    picture = we_draw(t, &rig, &camera, &weather);
    REQUIRE(picture != nullptr);
    const float64_t lit = we_ground_brightness(picture);
    printf("  ground: %.2f before the strike, %.2f in its flash (f = %.3f at %.0f blocks)\n", (libc_double_t)dark,
           (libc_double_t)lit, (libc_double_t)flash->flash, (libc_double_t)flash->bolt_distance);
    CHECK(lit > dark * 1.2f64);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------------ */

typedef struct we_temp_dir_t {
    char path[64]; /* 0 */
} we_temp_dir_t;
static_assert(sizeof(we_temp_dir_t) == 64);

static bool we_temp_dir_make(we_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_weather_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void we_temp_dir_remove(const we_temp_dir_t *dir) {
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

static const char we_script[] = "0 move 0 60\n"
                                "6 screenshot view.png\n"
                                "7 end\n";

/* The program's picture of a weather, its bytes. */
static bool we_program_picture(test_context_t *t, const we_temp_dir_t *base, const char *script, const char *kind,
                               uint8_t *out, uint64_t capacity, uint64_t *out_length) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--no-sound",
                          (char *)"--test-world",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)base->path,
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          (char *)"--weather",
                          (char *)kind,
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, WE_PROGRAM_TIMEOUT_MS, &run);
    if (!CHECK(run.started != 0u) || !CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK)) {
        printf("%s\n", run.output);
        return false;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/view.png", base->path);
    return CHECK_EQ_U64(file_read_whole(path, out, capacity, out_length), FILE_OK);
}

TEST(the_program_takes_weather_and_refuses_it_with_no_world) {
    we_temp_dir_t base;
    REQUIRE(we_temp_dir_make(&base));
    char script[128];
    snprintf(script, sizeof(script), "%s/view.script", base.path);
    REQUIRE(file_write_atomic(script, we_script, sizeof(we_script) - 1u) == FILE_OK);
    const uint64_t capacity = 1u << 20;
    uint8_t *const clear = test_alloc(t, capacity);
    uint8_t *const rain = test_alloc(t, capacity);
    uint8_t *const again = test_alloc(t, capacity);
    uint64_t clear_length = 0u, rain_length = 0u, again_length = 0u;
    REQUIRE(we_program_picture(t, &base, script, "clear", clear, capacity, &clear_length));
    REQUIRE(we_program_picture(t, &base, script, "rain", rain, capacity, &rain_length));
    REQUIRE(we_program_picture(t, &base, script, "rain", again, capacity, &again_length));
    /* Rain is not the clear day, and the same script with the same weather is the same picture. */
    CHECK(clear_length != rain_length || memcmp(clear, rain, rain_length) != 0);
    CHECK_EQ_U64(again_length, rain_length);
    CHECK_MEM_EQ(again, rain, rain_length < again_length ? rain_length : again_length);
    test_process_t run;
    char *const unknown[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--test-world",
                             (char *)"--weather", (char *)"drizzle",    nullptr};
    test_process_run(t, unknown, WE_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "--weather takes clear, rain");
    char *const alone[] = {t->borrowed_args[0],
                           (char *)"--headless",
                           (char *)"--ticks",
                           (char *)"1",
                           (char *)"--weather",
                           (char *)"rain",
                           nullptr};
    test_process_run(t, alone, WE_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "--weather only means something with --test-world or --menu");
    char *const off[] = {t->borrowed_args[0], (char *)"--headless",   (char *)"--ticks",
                         (char *)"1",         (char *)"--no-weather", nullptr};
    test_process_run(t, off, WE_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    we_temp_dir_remove(&base);
}

TEST_MAIN()
