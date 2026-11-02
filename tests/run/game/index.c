/* ctest run_game. See README.md. */
#include "harness/index.h"

/* Both fake kernels. Their helpers share names, so the one for input is renamed coming in. */
#include "run/display/mode/fixtures/fake_card.h"
#define fake_close_fd evdev_close_fd
#define fake_ioctl_call evdev_ioctl_call
#define fake_read_fd evdev_read_fd
#define fake_record evdev_record
#define fake_ops evdev_ops
#include "run/input/fixtures/fake_evdev.h"
#undef fake_close_fd
#undef fake_ioctl_call
#undef fake_read_fd
#undef fake_record
#undef fake_ops
#include "run/input/fixtures/replay.h"

#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "render/index.h"
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
    GL_WIDTH = 320u,
    GL_HEIGHT = 180u,
};

/* A rig: everything a real run has, on memory of its own. */
typedef struct gl_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
} gl_rig_t;
static_assert(sizeof(gl_rig_t) == 64);

static void *gl_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool gl_rig_begin(test_context_t *t, gl_rig_t *rig) {
    rig->bridge = gl_aligned(t, sizeof(gpu_bridge_t));
    rig->job = gl_aligned(t, sizeof(job_system_t));
    rig->world = gl_aligned(t, sizeof(world_t));
    rig->terrain = gl_aligned(t, sizeof(terrain_t));
    rig->render = gl_aligned(t, sizeof(render_t));
    rig->texture = gl_aligned(t, sizeof(texture_t));
    rig->game = gl_aligned(t, sizeof(game_t));
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

static void gl_rig_end(test_context_t *t, gl_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static const uint8_t *gl_draw(test_context_t *t, gl_rig_t *rig, const render_camera_t *camera) {
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

static const game_config_t gl_small_game = {.render_distance = 6u,
                                            .width = GL_WIDTH,
                                            .height = GL_HEIGHT,
                                            .features_off =
                                                SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                            ._pad0 = {0}};

static input_t *gl_input_start(test_context_t *t, fake_evdev_t *fake) {
    input_t *const input = gl_aligned(t, sizeof(input_t));
    CHECK(input_init(input, FAKE_DIRECTORY, &evdev_ops, fake));
    return input;
}

static float64_t gl_flown(const render_camera_t *from, const render_camera_t *to) {
    const float64_t x = to->position[0] - from->position[0];
    const float64_t y = to->position[1] - from->position[1];
    return sqrt(x * x + y * y);
}

/* Discard throws away what waited, and the time away, and a first frame is what starts the threads. */
TEST(the_loop_source_starts_the_threads_on_its_first_frame_and_flies_from_real_input) {
    /* The fixtures' other helpers, which only their own tests need. */
    (void)evdev_record;
    (void)fake_caps_touchpad;
    (void)fake_caps_power_button;
    (void)fake_find_call;
    gl_rig_t rig;
    REQUIRE(gl_rig_begin(t, &rig));
    game_t *const game = rig.game;
    REQUIRE(game_open(game, &gl_small_game));
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    fake_device_t *const keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = gl_input_start(t, fake);

    /* A run that ended before any frame has nothing to stop. */
    game_loop_source_begin(game, input);
    CHECK_EQ_U64(game->threads_running, 0u);
    CHECK(game_loop_stop(game));

    const uint64_t bytes = post_xrgb_bytes(GL_WIDTH, GL_HEIGHT);
    display_buffer_t target = {.owned_map = gl_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = GL_WIDTH * 4u,
                               .width = GL_WIDTH,
                               .height = GL_HEIGHT};
    CHECK(game_loop_source(game, &target));
    CHECK_EQ_U64(game->threads_running, 1u);

    /* The mouse turns it, unless another VT was on screen. */
    const float32_t yaw_start = game->camera.yaw;
    fake_replay(mouse, replay_mouse_moves, REPLAY_COUNT(replay_mouse_moves));
    game_loop_discard(game);
    CHECK(game_loop_source(game, &target));
    CHECK(game->camera.yaw == yaw_start);
    fake_replay(mouse, replay_mouse_moves, REPLAY_COUNT(replay_mouse_moves));
    CHECK(game_loop_source(game, &target));
    CHECK(game->camera.yaw != yaw_start);

    /* W flies it along where it looks, by the clock, and the time spent away is not time spent flying. */
    fake_replay(keyboard, replay_w_held, REPLAY_COUNT(replay_w_held));
    CHECK(game_loop_source(game, &target));
    render_camera_t before = game->camera;
    time_sleep_ns(400000000);
    CHECK(game_loop_source(game, &target));
    const float64_t flown = gl_flown(&before, &game->camera);
    CHECK(flown > 0.0f64);
    before = game->camera;
    time_sleep_ns(400000000);
    game_loop_discard(game);
    CHECK(game_loop_source(game, &target));
    CHECK(gl_flown(&before, &game->camera) < flown / 2.0f64);

    static const struct input_event esc[] = {REPLAY_EVENT(EV_KEY, KEY_ESC, 1), REPLAY_SYN};
    fake_replay(keyboard, esc, REPLAY_COUNT(esc));
    CHECK(!game_loop_source(game, &target));
    CHECK(game_loop_stop(game));
    /* world/tick is wired in: one tick of the world for each of the game, and the terrain's seed. */
    CHECK_EQ_U64(rig.world->tick.now, game->tick_count);
    CHECK_EQ_U64(rig.world->tick.seed, rig.terrain->seed);
    CHECK_EQ_U64(game->threads_running, 0u);
    input_deinit(input);
    game_close(game);
    gl_rig_end(t, &rig);
}

typedef enum gl_run_stage_t : uint32_t {
    GL_RUN_WANT_FIRST_PICTURE, /* until the screen is what was drawn before the fork */
    GL_RUN_FLYING,             /* W and Ctrl held, until three chunks away */
    GL_RUN_SETTLING,           /* let go, until the screen stops changing */
} gl_run_stage_t;

typedef struct gl_run_t {
    game_t *borrowed_game;             /* 0 */
    const display_t *borrowed_display; /* 8 */
    fake_device_t *borrowed_keyboard;  /* 16 */
    const uint8_t *borrowed_expected;  /* 24: from before the fork */
    uint8_t *borrowed_last;            /* 32: what the screen last showed */
    uint64_t bytes;                    /* 40 */
    uint64_t frames;                   /* 48 */
    int64_t started_ns;                /* 56 */
    render_camera_t start;             /* 64 */
    gl_run_stage_t stage;              /* 104 */
    uint32_t same;                     /* 108 */
    uint8_t guardian_before_threads;   /* 112 */
    uint8_t first_picture_true;        /* 113 */
    uint8_t timed_out;                 /* 114 */
    uint8_t _pad0[5];                  /* 115 */
} gl_run_t;
static_assert(sizeof(gl_run_t) == 120);

static bool gl_run_source(void *context, display_buffer_t *back) {
    static const struct input_event ctrl_w_down[] = {REPLAY_EVENT(EV_KEY, KEY_LEFTCTRL, 1),
                                                     REPLAY_EVENT(EV_KEY, KEY_W, 1), REPLAY_SYN};
    static const struct input_event ctrl_w_up[] = {REPLAY_EVENT(EV_KEY, KEY_LEFTCTRL, 0),
                                                   REPLAY_EVENT(EV_KEY, KEY_W, 0), REPLAY_SYN};
    static const struct input_event esc[] = {REPLAY_EVENT(EV_KEY, KEY_ESC, 1), REPLAY_SYN};
    gl_run_t *const run = context;
    game_t *const game = run->borrowed_game;
    if (run->frames == 0u) {
        run->guardian_before_threads = run->borrowed_display->guard.guardian_pid != 0 && game->threads_running == 0u;
    }
    run->frames++;
    if (!game_loop_source(game, back)) {
        return false;
    }
    if (time_now_ns() - run->started_ns > 120ll * TIME_NS_PER_SECOND) {
        run->timed_out = 1u;
        return false;
    }
    switch (run->stage) {
    case GL_RUN_WANT_FIRST_PICTURE:
        if (memcmp(back->owned_map, run->borrowed_expected, run->bytes) == 0) {
            run->first_picture_true = 1u;
            run->start = game->camera;
            run->stage = GL_RUN_FLYING;
            fake_replay(run->borrowed_keyboard, ctrl_w_down, REPLAY_COUNT(ctrl_w_down));
        }
        break;
    case GL_RUN_FLYING:
        if (gl_flown(&run->start, &game->camera) > 3.0f64 * COLUMN_SIZE) {
            run->stage = GL_RUN_SETTLING;
            fake_replay(run->borrowed_keyboard, ctrl_w_up, REPLAY_COUNT(ctrl_w_up));
        }
        break;
    case GL_RUN_SETTLING:
        run->same = memcmp(back->owned_map, run->borrowed_last, run->bytes) == 0 ? run->same + 1u : 0u;
        memcpy(run->borrowed_last, back->owned_map, run->bytes);
        if (run->same == 400u) {
            fake_replay(run->borrowed_keyboard, esc, REPLAY_COUNT(esc));
        }
        break;
    }
    time_sleep_ns(1000000);
    return true;
}

/* All of main_run_test_world but the kernel's side: the guardian is really forked, with the card's runtime live and the pictures
 * pinned before it, and it stays alive for the whole run. So a picture written after the fork that reaches this process's memory
 * shows that the pin survived: the picture to match was made before the fork, by the same pictures. */
TEST(a_world_runs_through_the_display_loop_with_a_real_guardian_forked) {
    gl_rig_t rig;
    REQUIRE(gl_rig_begin(t, &rig));
    game_t *const game = rig.game;
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_t *const display = gl_aligned(t, sizeof(display_t));
    const display_config_t config = fake_config();
    REQUIRE(display_init(display, &config, &fake_ops, card) == DISPLAY_STATUS_OK);
    const display_buffer_t *const buffer = &display->flip.buffers[0];
    REQUIRE(buffer->pitch == buffer->width * 4u);
    const game_config_t small = {.render_distance = 6u,
                                 .width = buffer->width,
                                 .height = buffer->height,
                                 .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                 ._pad0 = {0}};
    REQUIRE(game_open(game, &small));
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = gl_input_start(t, fake);

    /* Everything the eye's first place wants goes to the card before the fork, so the picture to match is known. */
    REQUIRE(game_settle(game));
    const uint64_t bytes = post_xrgb_bytes(buffer->width, buffer->height);
    uint8_t *const expected = gl_aligned(t, bytes);
    const uint8_t *const before_fork = gl_draw(t, &rig, &game->camera);
    REQUIRE(before_fork != nullptr);
    memcpy(expected, before_fork, bytes);

    game_loop_source_begin(game, input);
    gl_run_t run = {.borrowed_game = game,
                    .borrowed_display = display,
                    .borrowed_keyboard = keyboard,
                    .borrowed_expected = expected,
                    .borrowed_last = gl_aligned(t, bytes),
                    .bytes = bytes,
                    .started_ns = time_now_ns()};
    const display_status_t status = display_frames_run(display, gl_run_source, game_loop_discard, &run);
    CHECK(game_loop_stop(game));
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(run.guardian_before_threads, 1u);
    CHECK_EQ_U64(run.first_picture_true, 1u);
    CHECK_EQ_U64(run.stage, GL_RUN_SETTLING);
    CHECK_EQ_I64(display->guard.guardian_pid, 0);
    CHECK(card->flips > 400u);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    /* What the end of a run logs as its FPS: every new picture taken and no more, over a real length of time. */
    CHECK(game->pictures_shown > 0u);
    CHECK(game->pictures_shown <= game->borrowed_render->frame.stats.frames);
    CHECK(game->loop_ended_ns > game->loop_started_ns);
    /* What it flew to: streamed and uploaded after the fork, and what one thread draws from there. */
    const uint64_t columns_before_settle = game->columns_made;
    CHECK(columns_before_settle > 0u);
    REQUIRE(game_settle(game));
    const uint8_t *const after = gl_draw(t, &rig, &game->camera);
    REQUIRE(after != nullptr);
    CHECK_MEM_EQ(after, run.borrowed_last, bytes);
    CHECK(memcmp(after, expected, bytes) != 0);
    /* A page the card had stopped writing to would still read as it did at the fork: untouched, or that one old picture. */
    uint64_t lit = 0u;
    for (uint64_t i = 0u; i < bytes; i++) {
        lit += after[i] != 0u ? 1u : 0u;
    }
    CHECK(lit > bytes / 2u);

    input_deinit(input);
    game_close(game);
    display_deinit(display);
    gl_rig_end(t, &rig);
    check_nothing_left(t, card);
}

/* The world at the real distance on the real threads, from nothing, as a measurement to read and not a check to pass: too slow
 * for every run, and it prints what it saw. MC_FRAME_MEASURE_THREADS=1 on the release preset. */
TEST(the_real_threads_load_the_default_render_distance_when_asked) {
    if (getenv("MC_FRAME_MEASURE_THREADS") == nullptr) {
        return;
    }
    gl_rig_t rig;
    REQUIRE(gl_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t full = {
        .render_distance = MC_GAME_RENDER_DISTANCE_DEFAULT, .width = 1920u, .height = 1080u, ._pad0 = {0}};
    REQUIRE(game_open(game, &full));
    const uint64_t bytes = post_xrgb_bytes(full.width, full.height);
    uint8_t *const screen = gl_aligned(t, bytes);
    display_buffer_t target = {
        .owned_map = screen, .map_bytes = bytes, .pitch = full.width * 4u, .width = full.width, .height = full.height};
    const int64_t started_ns = time_now_ns();
    REQUIRE(game_loop_start(game, nullptr));
    const render_fly_input_t still = {0};
    const uint32_t wanted = rig.world->map.slot_count;
    int64_t first_picture_ns = 0, longest_frame_ns = 0, all_in_ns = 0;
    uint64_t frames = 0u;
    while (all_in_ns == 0) {
        const int64_t frame_start_ns = time_now_ns();
        REQUIRE(game_loop_frame(game, &still, 1.0f32 / 120.0f32, &target));
        const int64_t frame_ns = time_now_ns() - frame_start_ns;
        longest_frame_ns = frame_ns > longest_frame_ns ? frame_ns : longest_frame_ns;
        frames++;
        first_picture_ns = first_picture_ns == 0 && game->borrowed_last_picture != nullptr ? time_now_ns() - started_ns
                                                                                           : first_picture_ns;
        /* The sim's numbers, read from outside it: this only decides when to stop looking. */
        if (rig.render->upload.stats.sections_sent > 0u && rig.world->map.pending_count == 0u &&
            game->stream_quiet != 0u && rig.world->edit.queue_count[WORLD_EDIT_CONSUMER_RENDER] == 0u) {
            all_in_ns = time_now_ns() - started_ns;
        }
        REQUIRE(time_now_ns() - started_ns < 200ll * TIME_NS_PER_SECOND);
        time_sleep_ns(8333333);
    }
    CHECK(game_loop_stop(game));
    printf("  %" PRIu32 " slots, %" PRIu64 " columns, first picture %.0f ms, all in %.0f ms, %" PRIu64
           " frames, longest game_loop_frame %.3f ms, %.1f MiB of slabs, pool full %" PRIu64 "\n",
           wanted, game->columns_made, (libc_double_t)time_ns_to_ms(first_picture_ns),
           (libc_double_t)time_ns_to_ms(all_in_ns), frames, (libc_double_t)time_ns_to_ms(longest_frame_ns),
           (libc_double_t)rig.render->buffers.slab_bytes / (libc_double_t)(1u << 20),
           rig.render->buffers.pool_full_count);
    CHECK_EQ_U64(rig.render->buffers.pool_full_count, 0u);
    game_report(game);
    game_close(game);
    gl_rig_end(t, &rig);
}

TEST_MAIN()
