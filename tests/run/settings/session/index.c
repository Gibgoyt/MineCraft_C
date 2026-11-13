/* ctest run_settings_session. See README.md. */
#include "harness/index.h"

/* The fake kernel for input, as tests/run/ui/menu/ has it. */
#include "run/input/fixtures/fake_evdev.h"
#include "run/input/fixtures/replay.h"

#include "entity/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "game/view/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "settings/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    GAME_WIDTH = 320u,
    GAME_HEIGHT = 200u,
    /* What is asked of real threads here takes seconds at -O0, on a card with room, and what a far field gives back waits for its workers. */
    THREADS_WAIT_NS_SECONDS = 60u,
    PATH_BYTES = 256u,
};

static void *aligned_alloc_64(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* ---- The real game ------------------------------------------------------------------------------------------------------ */

typedef struct game_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    uint8_t *screen;             /* 64 */
    display_buffer_t buffer;     /* 72 */
} game_rig_t;
static_assert(sizeof(game_rig_t) == 128);

static bool game_rig_begin(test_context_t *t, game_rig_t *rig) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = aligned_alloc_64(t, sizeof(gpu_bridge_t));
    rig->job = aligned_alloc_64(t, sizeof(job_system_t));
    rig->world = aligned_alloc_64(t, sizeof(world_t));
    rig->terrain = aligned_alloc_64(t, sizeof(terrain_t));
    rig->render = aligned_alloc_64(t, sizeof(render_t));
    rig->texture = aligned_alloc_64(t, sizeof(texture_t));
    rig->game = aligned_alloc_64(t, sizeof(game_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    const uint64_t bytes = (uint64_t)GAME_WIDTH * GAME_HEIGHT * 4u;
    rig->screen = test_alloc(t, bytes);
    rig->buffer = (display_buffer_t){.owned_map = rig->screen,
                                     .map_bytes = bytes,
                                     .pitch = GAME_WIDTH * 4u,
                                     .width = GAME_WIDTH,
                                     .height = GAME_HEIGHT};
    return CHECK(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK) &&
           CHECK(thread_topology_read(rig->topology)) && CHECK(job_init(rig->job, rig->topology)) &&
           CHECK(texture_init(rig->texture)) && CHECK(world_init(rig->world, world_limits_default())) &&
           CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) &&
           CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default())) &&
           (game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology), true);
}

static void game_rig_end(test_context_t *t, game_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

/* Settled, drawn and shown with no threads, into the rig's screen. */
static bool game_rig_draw(game_rig_t *rig) {
    display_headless_scene_t scene;
    game_headless_scene(rig->game, &scene);
    const display_headless_tick_t tick = {0};
    return scene.draw(scene.borrowed_context, &tick, &rig->buffer);
}

static const game_config_t small_game = {.render_distance = 6u,
                                         .width = GAME_WIDTH,
                                         .height = GAME_HEIGHT,
                                         .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                         .show_date = 0u,
                                         .block_radius_given = 6u,
                                         .table_chunks = 6u};

/* Plan 009 phase 04: the far field the game plans for this picture at a render distance, by the bands. */
static render_lod_plan_t session_plan(uint32_t render_distance) {
    const render_lod_bands_t bands = render_lod_bands((float64_t)RENDER_LOD_PIXEL_ERROR, RENDER_LOD_NEAR_GUARANTEE,
                                                      (float64_t)RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT, GAME_HEIGHT);
    return render_lod_plan_by_bands(&bands, render_distance);
}

/* Columns in a disc of this radius, by counting. */
static uint32_t disc_columns(int32_t radius) {
    uint32_t count = 0u;
    for (int32_t y = -radius; y <= radius; y++) {
        for (int32_t x = -radius; x <= radius; x++) {
            count += x * x + y * y <= radius * radius ? 1u : 0u;
        }
    }
    return count;
}

TEST(render_distance_changes_in_an_open_world_and_what_stays_in_reach_stays) {
    game_rig_t *const rig = aligned_alloc_64(t, sizeof(game_rig_t));
    REQUIRE(game_rig_begin(t, rig));
    game_t *const game = rig->game;
    world_map_t *const map = &rig->world->map;
    render_t *const render = rig->render;
    REQUIRE(game_open(game, &small_game));
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(map->view_radius, 6u);
    CHECK_EQ_U64(map->ready_count, disc_columns(6));
    CHECK(render->borrowed_lod == nullptr);
    const uint32_t table = map->table_radius;

    /* What a player would have done, near the eye, with a stone in the air. */
    const int32_t x = (int32_t)floor(game->camera.position[0]);
    const int32_t y = (int32_t)floor(game->camera.position[1]);
    const int32_t z = (int32_t)floor(game->camera.position[2]);
    REQUIRE(world_edit_block_set(&rig->world->edit, x, y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    uint16_t block = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(map, x, y, z, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_STONE);

    const float32_t start_yaw = game->camera.yaw;
    const uint64_t columns_made = game->columns_made;
    const uint64_t pictures_before = render->frame.stats.frames;

    /* In: columns leave, and the far ones are gone from the card as well. */
    game_config_t config = small_game;
    config.render_distance = 2u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(map->view_radius, 2u);
    CHECK_NEAR(render->frame.fog_end, 16.0f32, 0.001f32);
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(map->ready_count, disc_columns(4)); /* what is wanted, and what is kept past it */
    CHECK_EQ_U64(game->columns_made, columns_made);  /* all of them were there */
    CHECK_EQ_U64(map->table_radius, table);          /* the world is the one it was */
    CHECK_EQ_U64(game->open, 1u);
    CHECK(render->frame.stats.frames > pictures_before);
    block = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(map, x, y, z, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_STONE);
    CHECK(game->camera.yaw == start_yaw);

    /* Out again: only what had gone is made again. */
    config.render_distance = 6u;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(map->ready_count, disc_columns(6));
    CHECK_EQ_U64(game->columns_made, columns_made + (uint64_t)disc_columns(6) - disc_columns(4));
    block = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(map, x, y, z, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_STONE);

    /* Past level 0: a far field, and the map has not heard of it. */
    const uint64_t columns_before_far = game->columns_made;
    config.render_distance = 64u;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(render->borrowed_lod != nullptr);
    const render_lod_plan_t plan_64 = session_plan(64u);
    CHECK_EQ_U64(render->borrowed_lod->level_count, plan_64.level_count);
    CHECK_NEAR(render->frame.fog_end, 63.0f32 * 16.0f32, 0.001f32);
    /* Settling one is minutes at -O0, so its columns are the threads' test's and far.script's. */
    CHECK_EQ_U64(map->view_radius, 6u);
    CHECK_EQ_U64(game->columns_made, columns_before_far);

    /* Another plan, the same way. */
    config.render_distance = 96u;
    REQUIRE(game_reconfigure(game, &config));
    const render_lod_plan_t plan_96 = session_plan(96u);
    CHECK_EQ_U64(render->borrowed_lod->level_count, plan_96.level_count);
    CHECK_NEAR(render->frame.fog_end, 95.0f32 * 16.0f32, 0.001f32);

    /* And all the way in, with the far field gone. */
    config.render_distance = 3u;
    heap_counters_t held;
    heap_counters_read(&held);
    REQUIRE(game_reconfigure(game, &config));
    CHECK(render->borrowed_lod == nullptr);
    CHECK_EQ_U64(map->view_radius, 3u);
    heap_counters_t after_far;
    heap_counters_read(&after_far);
    CHECK(after_far.live_bytes < held.live_bytes);
    REQUIRE(game_rig_draw(rig));

    /* A config that comes to the same is nothing at all. */
    const uint64_t frames = render->frame.stats.frames;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(game->view_stale, 0u);
    CHECK_EQ_U64(render->frame.stats.frames, frames);
    game_close(game);
    game_rig_end(t, rig);
}

TEST(a_world_opened_with_room_for_level_0_holds_it_at_any_render_distance) {
    game_rig_t *const rig = aligned_alloc_64(t, sizeof(game_rig_t));
    REQUIRE(game_rig_begin(t, rig));
    game_t *const game = rig->game;
    world_map_t *const map = &rig->world->map;
    game_config_t config = small_game;
    config.render_distance = 2u;
    config.block_radius_given = 6u;
    config.table_chunks = 6u;
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(map->table_radius, 6u + WORLD_MAP_KEEP_EXTRA);
    CHECK_EQ_U64(map->view_radius, 2u);
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(map->ready_count, disc_columns(2));
    config.render_distance = 6u;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(map->ready_count, disc_columns(6));

    /* Level 0 in, all by itself, and the far field is what would take up from there. */
    config.render_distance = 40u;
    config.block_radius_given = 3u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(map->view_radius, 3u);
    REQUIRE(game->borrowed_render->borrowed_lod != nullptr);
    config.features_off = small_game.features_off | SHADE_FEATURE_LOD;
    REQUIRE(game_reconfigure(game, &config));
    CHECK(game->borrowed_render->borrowed_lod == nullptr);
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(map->ready_count, disc_columns(5));
    /* And past what the table holds it is held to that, until a new world: nothing has failed, and nothing is on the screen that should not be. */
    config.block_radius_given = 48u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(map->view_radius, 6u);
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(map->ready_count, disc_columns(6));
    game_close(game);
    game_rig_end(t, rig);
}

/* How many pixels differ by more than a little, between two pictures of the rig's size. */
static uint64_t pixels_different(const uint8_t *a, const uint8_t *b, uint32_t pixels) {
    uint64_t count = 0u;
    for (uint32_t i = 0u; i < pixels * 4u; i += 4u) {
        for (uint32_t c = 0u; c < 3u; c++) {
            const int32_t gap = (int32_t)a[i + c] - (int32_t)b[i + c];
            if (gap > 8 || gap < -8) {
                count++;
                break;
            }
        }
    }
    return count;
}

TEST(the_field_of_view_scale_effects_and_mouse_all_take_at_once_in_an_open_world) {
    game_rig_t *const rig = aligned_alloc_64(t, sizeof(game_rig_t));
    REQUIRE(game_rig_begin(t, rig));
    game_t *const game = rig->game;
    render_t *const render = rig->render;
    const uint32_t pixels = GAME_WIDTH * GAME_HEIGHT;
    REQUIRE(game_open(game, &small_game));
    CHECK(game->camera.half_fov_tangent == RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT);
    REQUIRE(game_rig_draw(rig));
    uint8_t *const first = test_alloc(t, (uint64_t)pixels * 4u);
    memcpy(first, rig->screen, (uint64_t)pixels * 4u);
    /* The same is the same. */
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(pixels_different(rig->screen, first, pixels), 0u);

    /* Field of view: a word, and no threads or memory touched. */
    game_config_t config = small_game;
    config.fov_degrees = 100u;
    heap_counters_t before;
    heap_counters_read(&before);
    REQUIRE(game_reconfigure(game, &config));
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_NEAR(game->camera.half_fov_tangent, 1.19175f32, 0.0001f32);
    /* What a pause shows comes to it with nothing else changing: this is all a pause on one thread draws. */
    CHECK_EQ_U64(game->view_stale, 1u);
    REQUIRE(game_view_refresh(game));
    CHECK_EQ_U64(game->view_stale, 0u);
    REQUIRE(game->borrowed_last_picture != nullptr);
    CHECK(pixels_different(game->borrowed_last_picture, first, pixels) > pixels / 10u);
    REQUIRE(game_rig_draw(rig));
    CHECK(pixels_different(rig->screen, first, pixels) > pixels / 10u);
    config.fov_degrees = 0u;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(pixels_different(rig->screen, first, pixels), 0u);

    /* An effect, on and off. Off, it is in what is drawn from the next frame, and back on it is as it was. */
    config.features_off = small_game.features_off | SHADE_FEATURE_SHADOW;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(render->frame.scene.features & SHADE_FEATURE_SHADOW, 0u);
    REQUIRE(game_rig_draw(rig));
    CHECK(pixels_different(rig->screen, first, pixels) > 0u);
    config.features_off = small_game.features_off;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(pixels_different(rig->screen, first, pixels), 0u);

    /* Half the size each way: plan 008 phase 05 draws it so on the same frame and brings it up on the card, the display's size. */
    config.render_scale_percent = 50u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(render->frame.width, GAME_WIDTH);
    CHECK_EQ_U64(render->frame.height, GAME_HEIGHT);
    CHECK_EQ_U64(render->frame.scale_percent, 50u);
    CHECK(game_picture_held(game) != nullptr); /* what a pause would still show, at its size */
    REQUIRE(game_rig_draw(rig));
    CHECK(pixels_different(rig->screen, first, pixels) < pixels / 2u);
    CHECK(pixels_different(rig->screen, first, pixels) > 0u);
    /* Back: what the world looks like has not changed, and what it was. */
    config.render_scale_percent = 100u;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(game_rig_draw(rig));
    CHECK_EQ_U64(pixels_different(rig->screen, first, pixels), 0u);

    /* The mouse: the same counts at twice the turn. */
    render_fly_input_t input = {0};
    input.mouse_dx = 40;
    const float32_t yaw = game->camera.yaw;
    game_fly(game, &input, 0.0f32);
    const float32_t usual = yaw - game->camera.yaw;
    config.sensitivity_percent = 200u;
    REQUIRE(game_reconfigure(game, &config));
    const float32_t yaw_2 = game->camera.yaw;
    game_fly(game, &input, 0.0f32);
    CHECK_NEAR(yaw_2 - game->camera.yaw, usual * 2.0f32, 0.0001f32);

    /* Uncapped is a word for the gpu thread. */
    config.uncapped = 1u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(atomic_load_explicit(&game->uncapped, memory_order_relaxed), 1u);
    game_close(game);
    /* Nothing of the size's copy is left. */
    CHECK_EQ_U64(game->owned_view.bytes, 0u);

    /* A world opened at a scale, as one from the settings file is, has its pictures and its room from the first, and goes to any other. */
    config = small_game;
    config.render_scale_percent = 50u;
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(render->frame.width, GAME_WIDTH);
    CHECK_EQ_U64(render->frame.scale_percent, 50u);
    REQUIRE(game_rig_draw(rig));
    CHECK(pixels_different(rig->screen, first, pixels) < pixels / 2u);
    config.render_scale_percent = 75u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(render->frame.scale_percent, 75u);
    REQUIRE(game_rig_draw(rig));
    CHECK(pixels_different(rig->screen, first, pixels) < pixels / 2u);
    game_close(game);
    game_rig_end(t, rig);
}

/* A frame by the real clock, as a real loop does, until what is wanted has come or the time is up. */
static bool threads_run_until(test_context_t *t, game_rig_t *rig, uint32_t ready_wanted, bool far_wanted) {
    game_t *const game = rig->game;
    const int64_t deadline_ns = time_now_ns() + (int64_t)THREADS_WAIT_NS_SECONDS * 1000000000ll;
    const render_fly_input_t still = {0};
    while (time_now_ns() < deadline_ns) {
        if (!CHECK(game_loop_frame(game, &still, 0.001f32, &rig->buffer))) {
            return false;
        }
        /* Reads the map from main, only to wait: the sim has the last word, and a stale count only goes round again. */
        const bool ready = __atomic_load_n(&rig->world->map.ready_count, __ATOMIC_RELAXED) == ready_wanted;
        const bool far_there =
            !far_wanted || (rig->render->borrowed_lod != nullptr &&
                            __atomic_load_n(&rig->render->borrowed_lod->stats.columns_built, __ATOMIC_RELAXED) >= 4u);
        if (ready && far_there) {
            return true;
        }
        time_sleep_ns(1000000);
    }
    printf("waited for %" PRIu32 " columns, and %" PRIu32 " came\n", ready_wanted, rig->world->map.ready_count);
    return false;
}

TEST(with_the_real_threads_render_distance_changes_between_two_frames_and_they_go_on) {
    game_rig_t *const rig = aligned_alloc_64(t, sizeof(game_rig_t));
    REQUIRE(game_rig_begin(t, rig));
    game_t *const game = rig->game;
    render_t *const render = rig->render;
    REQUIRE(game_open(game, &small_game));
    REQUIRE(game_loop_start(game, nullptr));
    REQUIRE(threads_run_until(t, rig, disc_columns(6), false));
    const uint64_t shown = game->pictures_shown;

    game_config_t config = small_game;
    config.render_distance = 2u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(game->threads_running, 1u);
    REQUIRE(threads_run_until(t, rig, disc_columns(4), false));
    CHECK_EQ_U64(game->borrowed_world->map.view_radius, 2u);

    config.render_distance = 64u;
    REQUIRE(game_reconfigure(game, &config));
    CHECK_EQ_U64(game->threads_running, 1u);
    REQUIRE(render->borrowed_lod != nullptr);
    REQUIRE(threads_run_until(t, rig, disc_columns(6), true));

    /* And a size, and an effect, with pictures going all the while. Nothing here has been told to wait. */
    config.render_scale_percent = 50u;
    config.features_off = small_game.features_off | SHADE_FEATURE_SHADOW;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(threads_run_until(t, rig, disc_columns(6), true));
    const uint64_t shown_small = game->pictures_shown;
    config.render_scale_percent = 100u;
    config.render_distance = 6u;
    config.uncapped = 1u;
    REQUIRE(game_reconfigure(game, &config));
    REQUIRE(threads_run_until(t, rig, disc_columns(6), false));
    /* Uncapped: more pictures are drawn than anyone takes, as a picture is drawn with none taken. */
    const uint64_t drawn_before = render->frame.stats.frames;
    time_sleep_ns(300000000);
    CHECK(render->frame.stats.frames > drawn_before + 1u);
    CHECK(game->pictures_shown >= shown_small);
    CHECK(game->pictures_shown > shown);
    REQUIRE(game_loop_stop(game));
    game_close(game);
    game_rig_end(t, rig);
}

/* ---- The whole session, on the real threads, from real input frames ------------------------------------------------------ */

/* The layout at 640 by 480, worked out by hand: tests/run/ui/menu/ and tests/run/settings/ have the same numbers. */
enum : int32_t {
    AT_MIDDLE_X = 320,
    AT_SINGLEPLAYER_Y = 200,
    AT_CREATE_NEW_X = 452,
    AT_CREATE_NEW_Y = 396,
    AT_PAUSE_SETTINGS_Y = 248,
    AT_PAUSE_TITLE_Y = 296,
    AT_ROW_0_Y = 68,
    AT_ROW_1_Y = 116, /* field of view, since plan 009 phase 04 took the second slider out */
    AT_ROW_3_Y = 212, /* render scale */
    AT_DONE_Y = 404,  /* the main page's Done, row 7 */
};

typedef struct session_rig_t {
    game_session_t session;  /* 0 */
    display_buffer_t buffer; /* 144 */
    fake_device_t *mouse;    /* 200 */
    fake_device_t *keyboard; /* 208 */
    int32_t pointer_x;       /* 216 */
    int32_t pointer_y;       /* 220 */
    uint8_t failed;          /* 224 */
    uint8_t _pad0[7];        /* 225 */
} session_rig_t;
static_assert(sizeof(session_rig_t) == 232);

static void session_send(fake_device_t *device, uint16_t type, uint16_t code, int32_t value) {
    const struct input_event events[] = {REPLAY_EVENT(type, code, value), REPLAY_SYN};
    fake_replay(device, events, REPLAY_COUNT(events));
}

/* As a display's loop would, with a little time for the others, as a flip gives them. */
static void session_frames(test_context_t *t, session_rig_t *rig, uint32_t frames) {
    for (uint32_t i = 0u; i < frames; i++) {
        if (!game_session_source(&rig->session, &rig->buffer)) {
            rig->failed = 1u;
            CHECK(!"a frame of the session failed");
            return;
        }
        time_sleep_ns(1000000);
    }
}

static void session_move(test_context_t *t, session_rig_t *rig, int32_t x, int32_t y) {
    session_send(rig->mouse, EV_REL, REL_X, x - rig->pointer_x);
    session_send(rig->mouse, EV_REL, REL_Y, y - rig->pointer_y);
    rig->pointer_x = x;
    rig->pointer_y = y;
    session_frames(t, rig, 2u);
}

static void session_click(test_context_t *t, session_rig_t *rig, int32_t x, int32_t y) {
    session_move(t, rig, x, y);
    session_send(rig->mouse, EV_KEY, BTN_LEFT, 1);
    session_frames(t, rig, 2u);
    session_send(rig->mouse, EV_KEY, BTN_LEFT, 0);
    session_frames(t, rig, 2u);
}

static void session_key(test_context_t *t, session_rig_t *rig, uint16_t code) {
    session_send(rig->keyboard, EV_KEY, code, 1);
    session_frames(t, rig, 2u);
    session_send(rig->keyboard, EV_KEY, code, 0);
    session_frames(t, rig, 2u);
}

/* A frame at a time until this holds, with time to spare or not. */
#define SESSION_WAIT_FOR(condition)                                                                                    \
    do {                                                                                                               \
        const int64_t session_deadline_ns = time_now_ns() + (int64_t)THREADS_WAIT_NS_SECONDS * 1000000000ll;           \
        while (!(condition) && rig->failed == 0u && time_now_ns() < session_deadline_ns) {                             \
            session_frames(t, rig, 1u);                                                                                \
        }                                                                                                              \
        REQUIRE(condition);                                                                                            \
    } while (false)

/* Everything render distance does through a real session: real threads, real frames from fake devices, and the file. */
TEST(render_distance_changes_through_the_screen_in_a_pause_with_the_real_threads_going) {
    gpu_bridge_t *const bridge = aligned_alloc_64(t, sizeof(gpu_bridge_t));
    job_system_t *const job = aligned_alloc_64(t, sizeof(job_system_t));
    world_t *const world = aligned_alloc_64(t, sizeof(world_t));
    terrain_t *const terrain = aligned_alloc_64(t, sizeof(terrain_t));
    render_t *const render = aligned_alloc_64(t, sizeof(render_t));
    texture_t *const texture = aligned_alloc_64(t, sizeof(texture_t));
    game_t *const game = aligned_alloc_64(t, sizeof(game_t));
    physics_t *const physics = aligned_alloc_64(t, sizeof(physics_t));
    player_t *const player = aligned_alloc_64(t, sizeof(player_t));
    entity_t *const entity = aligned_alloc_64(t, sizeof(entity_t));
    item_t *const item = aligned_alloc_64(t, sizeof(item_t));
    ui_t *const ui = aligned_alloc_64(t, sizeof(ui_t));
    input_t *const input = aligned_alloc_64(t, sizeof(input_t));
    settings_t *const settings = aligned_alloc_64(t, sizeof(settings_t));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    session_rig_t *const rig = aligned_alloc_64(t, sizeof(session_rig_t));
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(job, topology));
    REQUIRE(texture_init(texture));
    REQUIRE(world_init(world, world_limits_default()));
    REQUIRE(terrain_init(terrain, TERRAIN_SEED_DEFAULT));
    REQUIRE(physics_init(physics));
    player_init(player, physics);
    REQUIRE(entity_init(entity, physics));
    REQUIRE(item_init(item));
    REQUIRE(render_init(render, &bridge->device_memory, job, render_limits_default()));
    REQUIRE(ui_init(ui, texture));
    REQUIRE(settings_init(settings));
    game_init(game, world, terrain, render, job, texture, topology);
    game_player_set(game, player);
    game_entity_set(game, entity);
    game_item_set(game, item);
    game_ui_set(game, ui);

    fake_evdev_t *const fake = fake_new(t);
    (void)fake_caps_touchpad;
    (void)fake_caps_power_button;
    (void)fake_record;
    memset(rig, 0, sizeof(*rig));
    rig->mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    rig->keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    REQUIRE(input_init(input, FAKE_DIRECTORY, &fake_ops, fake));

    char root[PATH_BYTES];
    snprintf(root, sizeof(root), "/tmp/mc_settings_XXXXXX");
    REQUIRE(mkdtemp(root) != nullptr);
    char saves[PATH_BYTES];
    char file[PATH_BYTES + 16u];
    snprintf(saves, sizeof(saves), "%.200s/saves", root);
    snprintf(file, sizeof(file), "%s/settings.yaml", root);
    settings_load(settings, file);
    settings->values.render_distance = 6u;
    settings->values.effects &= ~(uint32_t)(SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA);

    const uint64_t bytes = 640ull * 480ull * 4ull;
    rig->buffer = (display_buffer_t){
        .owned_map = test_alloc(t, bytes), .map_bytes = bytes, .pitch = 640u * 4u, .width = 640u, .height = 480u};
    const game_config_t config = {.width = 640u, .height = 480u, .show_date = 0u};
    game_session_init(&rig->session, game, ui, terrain, input, &config, saves, 4242u);
    game_session_settings_set(&rig->session, settings);
    session_frames(t, rig, 2u);
    rig->pointer_x = 320;
    rig->pointer_y = 240;

    /* Into a world. */
    session_click(t, rig, AT_MIDDLE_X, AT_SINGLEPLAYER_Y);
    session_click(t, rig, AT_CREATE_NEW_X, AT_CREATE_NEW_Y);
    session_key(t, rig, KEY_A);
    session_key(t, rig, KEY_ENTER);
    SESSION_WAIT_FOR(game->open != 0u && game->pictures_shown > 0u);
    REQUIRE(rig->session.state == GAME_SESSION_PLAYING);
    SESSION_WAIT_FOR(world->map.ready_count == 113u); /* a disc of radius 6 */
    const float64_t walked_to[2] = {player->movement.position[0], player->movement.position[1]};
    const uint64_t worlds = rig->session.worlds_opened;
    CHECK_EQ_U64(world->map.table_radius, 6u + WORLD_MAP_KEEP_EXTRA);

    /* Esc, Settings, in by the wheel. */
    session_key(t, rig, KEY_ESC);
    REQUIRE(rig->session.state == GAME_SESSION_PAUSED);
    session_click(t, rig, AT_MIDDLE_X, AT_PAUSE_SETTINGS_Y);
    REQUIRE(rig->session.settings_up != 0u);
    const uint64_t ticks_paused = game->tick_count;
    const uint8_t *const picture_before = game->borrowed_last_picture;
    session_move(t, rig, AT_MIDDLE_X, AT_ROW_0_Y);
    session_send(rig->mouse, EV_REL, REL_WHEEL, -4);
    /* Not at once: a wheel is several, and this waits for it to end. */
    session_frames(t, rig, 1u);
    CHECK_EQ_U64(settings->values.render_distance, 2u);
    CHECK_EQ_U64(world->map.view_radius, 6u);
    /* And in the next frames, with the world's time still stopped and the threads going. */
    SESSION_WAIT_FOR(world->map.view_radius == 2u);
    CHECK_EQ_U64(game->threads_running, 1u);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PAUSED);
    CHECK_EQ_U64(rig->session.settings_up, 1u);
    SESSION_WAIT_FOR(world->map.ready_count == 49u); /* a disc of radius 4: what is wanted and what is kept past it */
    /* What is behind the screen has been drawn again, in the pause. */
    SESSION_WAIT_FOR(game->borrowed_last_picture != picture_before);
    CHECK_EQ_U64(game->tick_count, ticks_paused);
    CHECK_EQ_I64((int64_t)rig->session.failed, 0);

    /* The field of view needs no restart of the threads, and no one is publishing cameras in a pause: it shows behind the screen all the same. */
    CHECK(game->shown_camera.half_fov_tangent == RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT);
    session_move(t, rig, AT_MIDDLE_X, AT_ROW_1_Y);
    session_send(rig->mouse, EV_REL, REL_WHEEL, 30);
    SESSION_WAIT_FOR(game->shown_camera.half_fov_tangent > 1.19f32 && game->shown_camera.half_fov_tangent < 1.20f32);
    CHECK_EQ_U64(settings->values.fov_degrees, 100u);
    CHECK_EQ_U64(game->tick_count, ticks_paused);

    /* The render scale too, and pictures behind the screen all the while: what a pause shows does not go black, or away. */
    session_move(t, rig, AT_MIDDLE_X, AT_ROW_3_Y);
    session_send(rig->mouse, EV_REL, REL_WHEEL, -50);
    SESSION_WAIT_FOR(game->view.scale_percent == 50u);
    CHECK_EQ_U64(settings->values.render_scale_percent, 50u);
    CHECK(game_picture_held(game) != nullptr);
    session_frames(t, rig, 5u);
    SESSION_WAIT_FOR(game->borrowed_last_picture != nullptr);

    /* Done: kept, and the pause is what it was. */
    session_click(t, rig, AT_MIDDLE_X, AT_DONE_Y);
    REQUIRE(rig->session.settings_up == 0u);
    uint8_t kept[SETTINGS_TEXT_BYTES];
    uint64_t kept_bytes = 0u;
    REQUIRE(file_read_whole(file, kept, sizeof(kept) - 1u, &kept_bytes) == FILE_OK);
    kept[kept_bytes] = '\0';
    CHECK_CONTAINS((const char *)kept, "render_distance: 2\n");
    CHECK_CONTAINS((const char *)kept, "render_scale_percent: 50\n");

    /* Esc: back in the same world, at the same place, and its time goes on. */
    session_key(t, rig, KEY_ESC);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PLAYING);
    SESSION_WAIT_FOR(game->tick_count > ticks_paused + 2u);
    CHECK_EQ_U64(rig->session.worlds_opened, worlds);
    CHECK_NEAR(player->movement.position[0], walked_to[0], 1.0f64);
    CHECK_NEAR(player->movement.position[1], walked_to[1], 1.0f64);

    /* And out, with nothing left. */
    session_key(t, rig, KEY_ESC);
    session_click(t, rig, AT_MIDDLE_X, AT_PAUSE_TITLE_Y);
    CHECK_EQ_U64(game->open, 0u);
    CHECK_EQ_U64(game->threads_running, 0u);
    CHECK_EQ_U64(rig->session.failed, 0u);

    CHECK(game_session_end(&rig->session));
    game_session_deinit(&rig->session);
    input_deinit(input);
    game_deinit(game);
    settings_deinit(settings);
    ui_deinit(ui);
    render_deinit(render);
    item_deinit(item);
    entity_deinit(entity);
    player_deinit(player);
    physics_deinit(physics);
    CHECK(gpu_bridge_exit_check(bridge));
    terrain_deinit(terrain);
    world_deinit(world);
    texture_deinit(texture);
    job_deinit(job);
    gpu_bridge_deinit(bridge);
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", root, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

TEST_MAIN()
