/* ctest run_ui_atlas_game. See README.md. */
#include "harness/index.h"

#include "run/input/fixtures/fake_evdev.h"
#include "run/input/fixtures/replay.h"

#include "display/headless/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/atlas/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "main/index.h"
#include "physics/index.h"
#include "png/index.h"
#include "render/index.h"
#include "save/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    AG_WIDTH = 320u,
    AG_HEIGHT = 240u,
    /* The menus' own layout, at scale 2, as tests/run/ui/menu/fixtures/flow.script works it out. */
    AG_SESSION_WIDTH = 640u,
    AG_SESSION_HEIGHT = 480u,
    AG_EVENTS = 128u,
    AG_TICKS_MOST = 1024u,
    AG_PATH_BYTES = 512u,
    AG_PROGRAM_TIMEOUT_MS = 180000u,
};
static const uint64_t AG_SEED = 4242u;

static void *ag_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

typedef struct ag_dir_t {
    char path[64];
} ag_dir_t;
static_assert(sizeof(ag_dir_t) == 64);

static bool ag_dir_make(ag_dir_t *dir) {
    snprintf(dir->path, sizeof(dir->path), "/tmp/mc_ui_atlas_game_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

/* Every file in it, then it. */
static void ag_dir_remove(test_context_t *t, const ag_dir_t *dir) {
    if (strncmp(dir->path, "/tmp/mc_ui_atlas_game_", 22u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)dir->path, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

/* ---- A rig: a real game on the card, with a body, a ui and a save -------------------------------------------------- */

typedef struct ag_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64: walking only, and the rest down to item */
    player_t *player;            /* 72 */
    entity_t *entity;            /* 80 */
    item_t *item;                /* 88 */
    ui_t *ui;                    /* 96 */
    save_t *save;                /* 104 */
    ag_dir_t root;               /* 112: a directory of its own, for saves */
} ag_rig_t;
static_assert(sizeof(ag_rig_t) == 176);

static bool ag_rig_begin(test_context_t *t, ag_rig_t *rig, bool walking) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = ag_aligned(t, sizeof(gpu_bridge_t));
    rig->job = ag_aligned(t, sizeof(job_system_t));
    rig->world = ag_aligned(t, sizeof(world_t));
    rig->terrain = ag_aligned(t, sizeof(terrain_t));
    rig->render = ag_aligned(t, sizeof(render_t));
    rig->texture = ag_aligned(t, sizeof(texture_t));
    rig->game = ag_aligned(t, sizeof(game_t));
    rig->save = ag_aligned(t, sizeof(save_t));
    rig->ui = ag_aligned(t, sizeof(ui_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK(ag_dir_make(&rig->root)) ||
        !CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, AG_SEED)) || !CHECK(save_init(rig->save)) ||
        !CHECK(ui_init(rig->ui, rig->texture)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_ui_set(rig->game, rig->ui);
    if (walking) {
        rig->physics = ag_aligned(t, sizeof(physics_t));
        rig->player = ag_aligned(t, sizeof(player_t));
        rig->entity = ag_aligned(t, sizeof(entity_t));
        rig->item = ag_aligned(t, sizeof(item_t));
        if (!CHECK(physics_init(rig->physics)) || !CHECK(entity_init(rig->entity, rig->physics)) ||
            !CHECK(item_init(rig->item))) {
            return false;
        }
        player_init(rig->player, rig->physics);
        game_player_set(rig->game, rig->player);
        game_entity_set(rig->game, rig->entity);
        game_item_set(rig->game, rig->item);
        game_save_set(rig->game, rig->save);
    }
    return true;
}

static void ag_rig_end(test_context_t *t, ag_rig_t *rig) {
    game_deinit(rig->game);
    save_deinit(rig->save);
    if (rig->player != nullptr) {
        item_deinit(rig->item);
        entity_deinit(rig->entity);
        player_deinit(rig->player);
        physics_deinit(rig->physics);
    }
    ui_deinit(rig->ui);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
    ag_dir_remove(t, &rig->root);
}

/* ---- A played world through the session, as --menu --headless runs it ---------------------------------------------- */

/* A new world through the menus, with seed 7, as tests/run/game/census/fixtures/week.script makes it: it is open, and
 * playing, from tick 23 on. What follows is each test's own. */
static const char AG_NEW_WORLD[] = "1 move 0 -40\n" /* 320, 200: Singleplayer */
                                   "+1 button left down\n"
                                   "+1 button left up\n"
                                   "+1 move 132 196\n" /* 452, 396: Create New World */
                                   "+1 button left down\n"
                                   "+1 button left up\n"
                                   "+1 key w down\n"
                                   "+1 key w up\n"
                                   "+1 key e down\n"
                                   "+1 key e up\n"
                                   "+1 key e down\n"
                                   "+1 key e up\n"
                                   "+1 key k down\n"
                                   "+1 key k up\n"
                                   "+1 move -132 -194\n" /* 320, 202: the seed field */
                                   "+1 button left down\n"
                                   "+1 button left up\n"
                                   "+1 key 7 down\n"
                                   "+1 key 7 up\n"
                                   "+1 key enter down\n" /* opens the world, between this tick and the next */
                                   "+1 key enter up\n";

/* What the test looks at after each tick of a session's script, drawn too. */
typedef struct ag_seen_t {
    uint64_t tick_count;   /* 0: the game's */
    uint64_t world_ticks;  /* 8: world/tick's */
    uint64_t columns_made; /* 16 */
    float64_t body[3];     /* 24: the body's place */
    uint8_t state;         /* 48: game_session_state_t */
    uint8_t atlas_up;      /* 49 */
    uint8_t paused;        /* 50 */
    uint8_t _pad0[5];      /* 51 */
} ag_seen_t;
static_assert(sizeof(ag_seen_t) == 56);

typedef struct ag_session_t {
    game_session_t session; /* 0 */
    ag_seen_t *seen;        /* 144: by tick */
    char *saves;            /* 152 */
    uint32_t last_tick;     /* 160 */
    uint8_t ran;            /* 164: every tick ticked and drew */
    uint8_t _pad0[3];       /* 165 */
} ag_session_t;
static_assert(sizeof(ag_session_t) == 168);

/* The new world, then after, through the session's headless scene, ticking and drawing every tick. The world stays open. */
static ag_session_t *ag_session_run(test_context_t *t, ag_rig_t *rig, uint32_t render_distance, const char *after) {
    ag_session_t *const run = ag_aligned(t, sizeof(ag_session_t));
    memset(run, 0, sizeof(*run));
    run->saves = test_alloc(t, AG_PATH_BYTES);
    snprintf(run->saves, AG_PATH_BYTES, "%s/saves", rig->root.path);
    const game_config_t config = {.render_distance = render_distance,
                                  .width = AG_SESSION_WIDTH,
                                  .height = AG_SESSION_HEIGHT,
                                  .features_off = SHADE_FEATURE_TAA,
                                  .show_date = 0u};
    game_session_init(&run->session, rig->game, rig->ui, rig->terrain, nullptr, &config, run->saves, AG_SEED);
    display_headless_scene_t scene;
    game_session_headless_scene(&run->session, &scene);

    const uint64_t length = strlen(AG_NEW_WORLD) + strlen(after);
    char *const text = test_alloc(t, length + 1u);
    snprintf(text, length + 1u, "%s%s", AG_NEW_WORLD, after);
    display_script_event_t *const events = test_alloc(t, AG_EVENTS * sizeof(display_script_event_t));
    display_script_t script;
    display_script_error_t error;
    if (!display_script_parse(text, length, MC_GAME_TICK_HZ, events, AG_EVENTS, &script, &error)) {
        printf("  the script, line %" PRIu32 ": %s\n", error.line, error.message);
        CHECK(false);
        return run;
    }
    if (!CHECK(script.last_tick < AG_TICKS_MOST)) {
        return run;
    }
    run->last_tick = script.last_tick;
    run->seen = test_alloc(t, (uint64_t)(script.last_tick + 1u) * sizeof(ag_seen_t));
    memset(run->seen, 0, (uint64_t)(script.last_tick + 1u) * sizeof(ag_seen_t));
    const uint64_t bytes = post_xrgb_bytes(AG_SESSION_WIDTH, AG_SESSION_HEIGHT);
    display_buffer_t frame = {.owned_map = ag_aligned(t, bytes),
                              .map_bytes = bytes,
                              .pitch = AG_SESSION_WIDTH * 4u,
                              .width = AG_SESSION_WIDTH,
                              .height = AG_SESSION_HEIGHT};
    display_script_state_t *const state = test_alloc(t, sizeof(display_script_state_t));
    memset(state, 0, sizeof(*state));
    uint32_t next = 0u;
    game_t *const game = rig->game;
    for (uint32_t tick = 0u; tick <= script.last_tick; tick++) {
        display_script_state_begin_tick(state, tick);
        const uint32_t first = next;
        while (next < script.event_count && events[next].tick == tick) {
            display_script_state_apply(state, &events[next]);
            next++;
        }
        const display_headless_tick_t info = {.events = next > first ? events + first : nullptr,
                                              .state = state,
                                              .event_count = next - first,
                                              .tick = tick};
        if (!CHECK(scene.tick(scene.borrowed_context, &info)) ||
            !CHECK(scene.draw(scene.borrowed_context, &info, &frame))) {
            return run;
        }
        ag_seen_t *const seen = &run->seen[tick];
        seen->tick_count = game->tick_count;
        seen->world_ticks = rig->world->tick.now;
        seen->columns_made = game->columns_made;
        if (rig->player != nullptr) {
            for (uint32_t k = 0u; k < 3u; k++) {
                seen->body[k] = (float64_t)rig->player->movement.position[k];
            }
        }
        seen->state = run->session.state;
        seen->atlas_up = game_atlas_up(game) ? 1u : 0u;
        seen->paused = (uint8_t)atomic_load(&game->paused);
    }
    run->ran = 1u;
    return run;
}

static void ag_session_end(test_context_t *t, ag_session_t *run) {
    CHECK(game_session_end(&run->session));
    game_session_deinit(&run->session);
}

/* The first tick from `from` on whose Atlas is `up`, or UINT32_MAX. */
static uint32_t ag_atlas_first(const ag_session_t *run, uint32_t from, bool up) {
    for (uint32_t tick = from; tick <= run->last_tick; tick++) {
        if ((run->seen[tick].atlas_up != 0u) == up) {
            return tick;
        }
    }
    return UINT32_MAX;
}

/* ---- A test world under real input, on the real threads ------------------------------------------------------------ */

static const struct input_event AG_M_DOWN[] = {REPLAY_EVENT(EV_KEY, KEY_M, 1), REPLAY_SYN};
static const struct input_event AG_M_UP[] = {REPLAY_EVENT(EV_KEY, KEY_M, 0), REPLAY_SYN};
static const struct input_event AG_ESC_DOWN[] = {REPLAY_EVENT(EV_KEY, KEY_ESC, 1), REPLAY_SYN};
static const struct input_event AG_ESC_UP[] = {REPLAY_EVENT(EV_KEY, KEY_ESC, 0), REPLAY_SYN};
static const struct input_event AG_W_DOWN[] = {REPLAY_EVENT(EV_KEY, KEY_W, 1), REPLAY_SYN};

typedef struct ag_loop_t {
    display_buffer_t target;          /* 0 */
    input_t *input;                   /* 56 */
    fake_device_t *borrowed_keyboard; /* 64 */
} ag_loop_t;
static_assert(sizeof(ag_loop_t) == 72);

/* A flying test world, as main_run_test_world runs it: game_loop_source, which ends the run on Esc with nothing up. */
static bool ag_loop_begin(test_context_t *t, ag_rig_t *rig, uint32_t render_distance, ag_loop_t *out) {
    const game_config_t config = {.render_distance = render_distance,
                                  .width = AG_WIDTH,
                                  .height = AG_HEIGHT,
                                  .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                  .show_date = 0u};
    if (!CHECK(game_open(rig->game, &config))) {
        return false;
    }
    fake_evdev_t *const fake = fake_new(t);
    out->borrowed_keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    out->input = ag_aligned(t, sizeof(input_t));
    if (!CHECK(input_init(out->input, FAKE_DIRECTORY, &fake_ops, fake))) {
        game_close(rig->game);
        return false;
    }
    game_loop_source_begin(rig->game, out->input);
    const uint64_t bytes = post_xrgb_bytes(AG_WIDTH, AG_HEIGHT);
    out->target = (display_buffer_t){.owned_map = ag_aligned(t, bytes),
                                     .map_bytes = bytes,
                                     .pitch = AG_WIDTH * 4u,
                                     .width = AG_WIDTH,
                                     .height = AG_HEIGHT};
    return true;
}

static void ag_loop_end(test_context_t *t, ag_rig_t *rig, ag_loop_t *loop) {
    CHECK(game_loop_stop(rig->game));
    input_deinit(loop->input);
    game_close(rig->game);
}

/* The real sim and gpu threads, with M pressed on the first frame, before any column has streamed in, and W held: the
 * world streams in and ticks on behind the Atlas while the eye stands. Read with the threads stopped. */
static void ag_loop_keeps_running(test_context_t *t, ag_rig_t *rig) {
    ag_loop_t loop;
    if (!ag_loop_begin(t, rig, 8u, &loop)) {
        return;
    }
    game_t *const game = rig->game;
    const uint64_t ticks_before = game->tick_count, columns_before = game->columns_made;
    const render_camera_t eye = game->camera;
    fake_replay(loop.borrowed_keyboard, AG_M_DOWN, REPLAY_COUNT(AG_M_DOWN));
    CHECK(game_loop_source(game, &loop.target)); /* the threads start here */
    fake_replay(loop.borrowed_keyboard, AG_M_UP, REPLAY_COUNT(AG_M_UP));
    fake_replay(loop.borrowed_keyboard, AG_W_DOWN, REPLAY_COUNT(AG_W_DOWN));
    for (uint32_t frame = 0u; frame < 150u; frame++) {
        CHECK(game_loop_source(game, &loop.target));
        time_sleep_ns(10 * TIME_NS_PER_MS);
    }
    const bool up = game_atlas_up(game);
    CHECK(game_loop_stop(game));
    printf("  on the real threads, %" PRIu64 " ticks and %" PRIu64
           " columns behind the Atlas; the eye moved %.3f blocks\n",
           game->tick_count - ticks_before, game->columns_made - columns_before,
           (libc_double_t)fabs(game->camera.position[0] - eye.position[0]) +
               (libc_double_t)fabs(game->camera.position[1] - eye.position[1]) +
               (libc_double_t)fabs(game->camera.position[2] - eye.position[2]));
    CHECK(up);
    CHECK(game->tick_count > ticks_before + 10u);
    CHECK(game->columns_made > columns_before);
    CHECK_EQ_U64(atomic_load(&game->paused), 0u);
    for (uint32_t k = 0u; k < 3u; k++) {
        CHECK(game->camera.position[k] == eye.position[k]);
    }
    ag_loop_end(t, rig, &loop);
}

/* Esc with the Atlas up closes it and the run goes on; a second Esc ends it. */
static void ag_loop_esc(test_context_t *t, ag_rig_t *rig) {
    ag_loop_t loop;
    if (!ag_loop_begin(t, rig, 4u, &loop)) {
        return;
    }
    game_t *const game = rig->game;
    CHECK(game_loop_source(game, &loop.target));
    fake_replay(loop.borrowed_keyboard, AG_M_DOWN, REPLAY_COUNT(AG_M_DOWN));
    CHECK(game_loop_source(game, &loop.target));
    fake_replay(loop.borrowed_keyboard, AG_M_UP, REPLAY_COUNT(AG_M_UP));
    CHECK(game_loop_source(game, &loop.target));
    CHECK(game_atlas_up(game));
    fake_replay(loop.borrowed_keyboard, AG_ESC_DOWN, REPLAY_COUNT(AG_ESC_DOWN));
    CHECK(game_loop_source(game, &loop.target));
    CHECK(!game_atlas_up(game));
    fake_replay(loop.borrowed_keyboard, AG_ESC_UP, REPLAY_COUNT(AG_ESC_UP));
    CHECK(game_loop_source(game, &loop.target));
    fake_replay(loop.borrowed_keyboard, AG_ESC_DOWN, REPLAY_COUNT(AG_ESC_DOWN));
    CHECK(!game_loop_source(game, &loop.target));
    ag_loop_end(t, rig, &loop);
}

TEST(while_the_atlas_is_up_the_game_keeps_running_and_the_body_stands) {
    ag_rig_t rig;
    REQUIRE(ag_rig_begin(t, &rig, true));
    /* The world is open from tick 23. The body lands; M; W held for the 100 ticks the Atlas is up; M again, still walking. */
    static const char after[] = "+40 key m down\n"
                                "+1 key m up\n"
                                "+1 key w down\n"
                                "+100 key m down\n"
                                "+1 key m up\n"
                                "+30 key w up\n"
                                "+1 end\n";
    ag_session_t *const run = ag_session_run(t, &rig, 8u, after);
    REQUIRE(run->ran != 0u);
    const uint32_t opened = ag_atlas_first(run, 0u, true);
    REQUIRE(opened != UINT32_MAX);
    const uint32_t closed = ag_atlas_first(run, opened, false);
    REQUIRE(closed != UINT32_MAX);
    const ag_seen_t *const open = &run->seen[opened];
    const ag_seen_t *const shut = &run->seen[closed - 1u];
    printf("  the Atlas was up from tick %" PRIu32 " to %" PRIu32 ": %" PRIu64 " game ticks, %" PRIu64
           " world ticks, %" PRIu64 " columns streamed in; the body from (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)\n",
           opened, closed - 1u, shut->tick_count - open->tick_count, shut->world_ticks - open->world_ticks,
           shut->columns_made - open->columns_made, (libc_double_t)open->body[0], (libc_double_t)open->body[1],
           (libc_double_t)open->body[2], (libc_double_t)shut->body[0], (libc_double_t)shut->body[1],
           (libc_double_t)shut->body[2]);
    /* Up from the tick M went down to the one it went down again: 101 ticks of the script, one tick of the world each. */
    CHECK_EQ_U64(closed - opened, 102u);
    CHECK_EQ_U64(shut->tick_count - open->tick_count, (uint64_t)(closed - 1u - opened));
    CHECK_EQ_U64(shut->world_ticks - open->world_ticks, (uint64_t)(closed - 1u - opened));
    uint64_t paused = 0u, playing = 0u;
    for (uint32_t tick = opened; tick < closed; tick++) {
        paused += run->seen[tick].paused;
        playing += run->seen[tick].state == GAME_SESSION_PLAYING ? 1u : 0u;
    }
    CHECK_EQ_U64(paused, 0u);
    CHECK_EQ_U64(playing, (uint64_t)(closed - opened));
    /* With one thread a tick settles all the eye wants, so with the body standing nothing new streams in here: that is
     * held on the real threads below. */
    /* W was held all through, and the body stood. */
    for (uint32_t k = 0u; k < 3u; k++) {
        CHECK(open->body[k] == shut->body[k]);
    }
    /* Closed, the same W walks it. */
    const ag_seen_t *const last = &run->seen[run->last_tick];
    const float64_t dx = last->body[0] - shut->body[0], dy = last->body[1] - shut->body[1];
    printf("  after the Atlas closed the body walked %.3f blocks\n", (libc_double_t)sqrt(dx * dx + dy * dy));
    CHECK(dx * dx + dy * dy > 1.0f64);
    ag_session_end(t, run);
    ag_rig_end(t, &rig);
    REQUIRE(ag_rig_begin(t, &rig, false));
    ag_loop_keeps_running(t, &rig);
    ag_rig_end(t, &rig);
}

TEST(esc_closes_the_atlas_and_not_the_world) {
    (void)fake_record;
    (void)fake_caps_touchpad;
    (void)fake_caps_power_button;
    (void)fake_caps_mouse;
    {
        ag_rig_t rig;
        REQUIRE(ag_rig_begin(t, &rig, true));
        /* A played world: M, Esc, then Esc again. */
        static const char after[] = "+20 key m down\n"
                                    "+1 key m up\n"
                                    "+10 key escape down\n"
                                    "+1 key escape up\n"
                                    "+10 key escape down\n"
                                    "+1 key escape up\n"
                                    "+10 end\n";
        ag_session_t *const run = ag_session_run(t, &rig, 3u, after);
        REQUIRE(run->ran != 0u);
        const uint32_t opened = ag_atlas_first(run, 0u, true);
        REQUIRE(opened != UINT32_MAX);
        const uint32_t closed = ag_atlas_first(run, opened, false);
        REQUIRE(closed != UINT32_MAX);
        /* The world opened after tick 20; M went down on tick 41, the first Esc on 52 and the second on 63. */
        CHECK_EQ_U64(opened, 41u);
        CHECK_EQ_U64(closed, 52u);
        uint32_t paused_at = UINT32_MAX;
        for (uint32_t tick = 0u; tick <= run->last_tick; tick++) {
            if (paused_at == UINT32_MAX && run->seen[tick].state == GAME_SESSION_PAUSED) {
                paused_at = tick;
            }
        }
        printf("  the Atlas was up from tick %" PRIu32 " to %" PRIu32 ", and the pause screen came on tick %" PRIu32
               "\n",
               opened, closed - 1u, paused_at);
        /* The first Esc closed the Atlas, and the world played on. */
        for (uint32_t tick = closed; tick < 63u; tick++) {
            CHECK_EQ_U64(run->seen[tick].state, GAME_SESSION_PLAYING);
            CHECK_EQ_U64(run->seen[tick].paused, 0u);
        }
        CHECK(run->seen[62].tick_count > run->seen[closed].tick_count);
        /* The second paused it. */
        CHECK_EQ_U64(paused_at, 63u);
        CHECK_EQ_U64(run->seen[run->last_tick].state, GAME_SESSION_PAUSED);
        ag_session_end(t, run);
        ag_rig_end(t, &rig);
    }
    {
        ag_rig_t rig;
        REQUIRE(ag_rig_begin(t, &rig, false));
        ag_loop_esc(t, &rig);
        ag_rig_end(t, &rig);
    }
}

/* The world's directory under saves: the only one there. */
static bool ag_world_directory(const char *saves, char *out, uint64_t bytes) {
    DIR *const dir = opendir(saves);
    if (dir == nullptr) {
        return false;
    }
    bool found = false;
    for (struct dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        if (entry->d_name[0] != '.' && entry->d_type == DT_DIR) {
            snprintf(out, bytes, "%s/%s", saves, entry->d_name);
            found = true;
        }
    }
    closedir(dir);
    return found;
}

TEST(tiles_written_while_playing_are_there_after_a_reopen) {
    ag_rig_t rig;
    REQUIRE(ag_rig_begin(t, &rig, true));
    /* A played world, walking, until well past 50 columns have generated. */
    static const char after[] = "+20 key w down\n"
                                "+120 key w up\n"
                                "+1 end\n";
    ag_session_t *const run = ag_session_run(t, &rig, 4u, after);
    REQUIRE(run->ran != 0u);
    const uint64_t columns = rig.game->columns_made;
    const uint64_t seed = rig.terrain->seed;
    printf("  %" PRIu64 " columns generated\n", columns);
    CHECK(columns >= 50u);
    /* What the store holds round the start, at level 0, before the world closes: 40 by 40 columns, row 0 north. */
    enum : uint32_t { SIDE = 40u * 16u };
    const int64_t west = -20 * 16, north = -20 * 16;
    arena_t *const arena = ag_aligned(t, sizeof(arena_t));
    arena_init(arena, "test_atlas_game", ag_aligned(t, 16ull << 20), 16ull << 20);
    uint32_t *const before = test_alloc(t, (uint64_t)SIDE * SIDE * sizeof(uint32_t));
    uint32_t *const after_reopen = test_alloc(t, (uint64_t)SIDE * SIDE * sizeof(uint32_t));
    ui_atlas_store_read(ui_atlas(rig.ui), 0u, west, north, SIDE, SIDE, before, arena);
    uint64_t generated = 0u;
    for (uint64_t k = 0u; k < (uint64_t)SIDE * SIDE; k++) {
        generated += ui_atlas_pixel_generated(before[k]) ? 1u : 0u;
    }
    printf("  %" PRIu64 " pixels explored round the start\n", generated);
    CHECK(generated >= 50u * UI_ATLAS_COLUMN_PIXELS);
    /* Closing the world saves its tiles. */
    ag_session_end(t, run);
    char world[AG_PATH_BYTES], atlas[AG_PATH_BYTES + 8u];
    REQUIRE(ag_world_directory(run->saves, world, sizeof(world)));
    snprintf(atlas, sizeof(atlas), "%s/atlas", world);
    /* Opened again, with nothing streamed: every one of those pixels as it was. */
    REQUIRE(ui_atlas_world_open(rig.ui, atlas, seed, nullptr, nullptr));
    arena_reset(arena);
    ui_atlas_store_read(ui_atlas(rig.ui), 0u, west, north, SIDE, SIDE, after_reopen, arena);
    uint64_t wrong = 0u;
    for (uint64_t k = 0u; k < (uint64_t)SIDE * SIDE; k++) {
        wrong += before[k] == after_reopen[k] ? 0u : 1u;
    }
    printf("  %" PRIu64 " of them differ after a reopen\n", wrong);
    CHECK_EQ_U64(wrong, 0u);
    CHECK_EQ_U64(ui_atlas(rig.ui)->store.files_refused, 0u);
    ui_atlas_world_close(rig.ui);
    arena_deinit(arena);
    ag_rig_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------- */

static uint8_t *ag_screenshot_read(test_context_t *t, const ag_dir_t *dir, const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir->path, name);
    const uint64_t capacity = display_png_bytes_needed(AG_WIDTH, AG_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        return nullptr;
    }
    png_info_t info;
    png_error_t error;
    if (!CHECK(png_probe(file, length, &info, &error) == PNG_OK) || !CHECK(info.width == AG_WIDTH)) {
        return nullptr;
    }
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    if (!CHECK(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK)) {
        return nullptr;
    }
    return rgba;
}

static uint64_t ag_shots_differ(const uint8_t *a, const uint8_t *b) {
    uint64_t differ = 0u;
    for (uint64_t k = 0u; k < (uint64_t)AG_WIDTH * AG_HEIGHT; k++) {
        differ += memcmp(a + 4u * k, b + 4u * k, 3u) == 0 ? 0u : 1u;
    }
    return differ;
}

TEST(the_program_opens_the_atlas_with_m_and_closes_it_with_esc) {
    REQUIRE(t->arg_count >= 1);
    ag_dir_t dir;
    REQUIRE(ag_dir_make(&dir));
    static const char script[] = "20 screenshot world.png\n"
                                 "21 key m down\n"
                                 "22 key m up\n"
                                 "30 screenshot open.png\n"
                                 "70 screenshot later.png\n"
                                 "71 key escape down\n"
                                 "72 key escape up\n"
                                 "80 screenshot closed.png\n"
                                 "90 end\n";
    char path[128];
    snprintf(path, sizeof(path), "%s/run.script", dir.path);
    REQUIRE(file_write_atomic(path, script, sizeof(script) - 1u) == FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--no-sound",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--script",
                          path,
                          (char *)"--screenshot-dir",
                          dir.path,
                          (char *)"--width",
                          (char *)"320",
                          (char *)"--height",
                          (char *)"240",
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, AG_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK(run.timed_out == 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, " 0 bytes live");
    /* Ticks went on throughout, Atlas or not, to the script's end: ticks 0 to 90. */
    CHECK_CONTAINS(run.output, " after 91 ticks");
    uint8_t *const world = ag_screenshot_read(t, &dir, "world.png");
    uint8_t *const open = ag_screenshot_read(t, &dir, "open.png");
    uint8_t *const later = ag_screenshot_read(t, &dir, "later.png");
    uint8_t *const closed = ag_screenshot_read(t, &dir, "closed.png");
    REQUIRE(world != nullptr && open != nullptr && later != nullptr && closed != nullptr);
    const uint64_t all = (uint64_t)AG_WIDTH * AG_HEIGHT;
    const uint64_t open_differ = ag_shots_differ(world, open), later_differ = ag_shots_differ(world, later);
    const uint64_t moved = ag_shots_differ(open, later), back = ag_shots_differ(world, closed);
    printf("open differs from the world in %" PRIu64 " pixels, later in %" PRIu64 ", open from later in %" PRIu64
           ", closed from the world in %" PRIu64 " of %" PRIu64 "\n",
           open_differ, later_differ, moved, back, all);
    CHECK(open_differ * 10u > all * 9u);
    CHECK(later_differ * 10u > all * 9u);
    /* By 40 ticks on the first picture has landed: the explored land round the player, and grey round it. The world goes on
     * behind (the walk, the clock), so the shots are not held to each other pixel for pixel. */
    uint64_t grey_later = 0u, grey_closed = 0u;
    for (uint64_t k = 0u; k < all; k++) {
        grey_later += later[4u * k] == 0x80u && later[4u * k + 1u] == 0x80u && later[4u * k + 2u] == 0x80u ? 1u : 0u;
        grey_closed +=
            closed[4u * k] == 0x80u && closed[4u * k + 1u] == 0x80u && closed[4u * k + 2u] == 0x80u ? 1u : 0u;
    }
    printf("grey: %" PRIu64 " pixels with the Atlas up, %" PRIu64 " after Esc\n", grey_later, grey_closed);
    CHECK(grey_later * 5u > all);
    /* On terrain, with the body standing, the two Atlas shots may be the same (the plan's "or not at all"), or differ
     * where the first picture had not landed yet when the first was taken: neither is held to anything. */
    /* After Esc the world and the HUD again: no grey, and nearly nothing like the Atlas. */
    CHECK(grey_closed * 100u < all);
    CHECK(ag_shots_differ(later, closed) * 10u > all * 9u);
    if (getenv("MC_ATLAS_PNG_DIR") == nullptr) {
        ag_dir_remove(t, &dir);
    } else {
        printf("the program's screenshots are in %s\n", dir.path);
    }
}

/* A test world has no climate: every layer key must still draw (the ground, for a climate layer), not crash the workers. */
TEST(every_layer_draws_in_a_test_world_with_no_climate) {
    REQUIRE(t->arg_count >= 1);
    ag_dir_t dir;
    REQUIRE(ag_dir_make(&dir));
    char script[2048];
    uint64_t length = (uint64_t)snprintf(script, sizeof(script), "20 key m down\n21 key m up\n");
    uint32_t tick = 30u;
    for (uint32_t group = 1u; group <= 9u; group++) {
        for (uint32_t press = 0u; press < 4u; press++) { /* a repeat cycles: 4 presses reach every layer of a group */
            length += (uint64_t)snprintf(script + length, sizeof(script) - length, "%u key %u down\n%u key %u up\n",
                                         tick, group, tick + 1u, group);
            tick += 4u;
        }
    }
    length += (uint64_t)snprintf(script + length, sizeof(script) - length, "%u screenshot last.png\n%u end\n", tick,
                                 tick + 4u);
    REQUIRE(length < sizeof(script));
    char path[128];
    snprintf(path, sizeof(path), "%s/run.script", dir.path);
    REQUIRE(file_write_atomic(path, script, length) == FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--no-sound",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--script",
                          path,
                          (char *)"--screenshot-dir",
                          dir.path,
                          (char *)"--width",
                          (char *)"320",
                          (char *)"--height",
                          (char *)"240",
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, AG_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK(run.timed_out == 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, " 0 bytes live");
    uint8_t *const last = ag_screenshot_read(t, &dir, "last.png");
    CHECK(last != nullptr);
    ag_dir_remove(t, &dir);
}

TEST_MAIN()
