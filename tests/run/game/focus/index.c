/* ctest run_game_focus. See README.md. */
#include "harness/index.h"

/* The fake kernel for input, as tests/run/settings/session/ has it. */
#include "run/input/fixtures/fake_evdev.h"
#include "run/input/fixtures/replay.h"

#include "display/index.h"
#include "entity/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "gpu_bridge/index.h"
#include "item/index.h"
#include "job/index.h"
#include "mob/index.h"
#include "mob/spawn/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "settings/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    /* What is asked of real threads here takes seconds at -O0. */
    FOCUS_WAIT_SECONDS = 90u,
    /* How long the window is left: 100 ticks' worth, at 20 a second. */
    FOCUS_AWAY_SECONDS = 5u,
    FOCUS_PATH_BYTES = 256u,
    FOCUS_BODIES_MAX = 4096u, /* ENTITY_STORE_CAPACITY, past which a store holds none */
};

/* The layout at 640 by 480, worked out by hand: tests/run/settings/session/ has the same numbers. */
enum : int32_t {
    AT_MIDDLE_X = 320,
    AT_SINGLEPLAYER_Y = 200,
    AT_CREATE_NEW_X = 452,
    AT_CREATE_NEW_Y = 396,
    AT_PAUSE_TITLE_Y = 296,
};

static void *aligned_alloc_64(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

typedef struct focus_rig_t {
    game_session_t session;  /* 0 */
    display_buffer_t buffer; /* 144 */
    fake_device_t *mouse;    /* 200 */
    fake_device_t *keyboard; /* 208 */
    display_t *display;      /* 216: a window with no server: only what the session reads of its focus */
    display_x11_t *x11;      /* 224 */
    int32_t pointer_x;       /* 232 */
    int32_t pointer_y;       /* 236 */
    uint8_t failed;          /* 240 */
    uint8_t _pad0[7];        /* 241 */
} focus_rig_t;
static_assert(sizeof(focus_rig_t) == 248);

static void focus_send(fake_device_t *device, uint16_t type, uint16_t code, int32_t value) {
    const struct input_event events[] = {REPLAY_EVENT(type, code, value), REPLAY_SYN};
    fake_replay(device, events, REPLAY_COUNT(events));
}

/* As a display's loop would, with a little time for the others, as a flip gives them. */
static void focus_frames(test_context_t *t, focus_rig_t *rig, uint32_t frames) {
    for (uint32_t i = 0u; i < frames; i++) {
        if (!game_session_source(&rig->session, &rig->buffer)) {
            rig->failed = 1u;
            CHECK(!"a frame of the session failed");
            return;
        }
        time_sleep_ns(1000000);
    }
}

static void focus_click(test_context_t *t, focus_rig_t *rig, int32_t x, int32_t y) {
    focus_send(rig->mouse, EV_REL, REL_X, x - rig->pointer_x);
    focus_send(rig->mouse, EV_REL, REL_Y, y - rig->pointer_y);
    rig->pointer_x = x;
    rig->pointer_y = y;
    focus_frames(t, rig, 2u);
    focus_send(rig->mouse, EV_KEY, BTN_LEFT, 1);
    focus_frames(t, rig, 2u);
    focus_send(rig->mouse, EV_KEY, BTN_LEFT, 0);
    focus_frames(t, rig, 2u);
}

static void focus_key(test_context_t *t, focus_rig_t *rig, uint16_t code) {
    focus_send(rig->keyboard, EV_KEY, code, 1);
    focus_frames(t, rig, 2u);
    focus_send(rig->keyboard, EV_KEY, code, 0);
    focus_frames(t, rig, 2u);
}

/* What display_x11_poll does with a focus event from the window manager, and the frame that sees it. */
static void focus_leave(test_context_t *t, focus_rig_t *rig) {
    rig->x11->focused = 0u;
    rig->x11->focus_losses++;
    focus_frames(t, rig, 1u);
}

static void focus_return(test_context_t *t, focus_rig_t *rig) {
    rig->x11->focused = 1u;
    focus_frames(t, rig, 1u);
}

/* A frame at a time until this holds. */
#define FOCUS_WAIT_FOR(condition)                                                                                      \
    do {                                                                                                               \
        const int64_t focus_deadline_ns = time_now_ns() + (int64_t)FOCUS_WAIT_SECONDS * TIME_NS_PER_SECOND;            \
        while (!(condition) && rig->failed == 0u && time_now_ns() < focus_deadline_ns) {                               \
            focus_frames(t, rig, 1u);                                                                                  \
        }                                                                                                              \
        REQUIRE(condition);                                                                                            \
    } while (false)

/* What the world is, as far as a pause must keep it: its clock and ticks, its mobs where they stand, and the body. */
typedef struct focus_world_t {
    float64_t position[FOCUS_BODIES_MAX][3]; /* 0 */
    float64_t body[3];                       /* 98304 */
    uint64_t tick_count;                     /* 98328 */
    uint64_t clock_units;                    /* 98336 */
    uint64_t spawned;                        /* 98344 */
    uint32_t mobs;                           /* 98352 */
    uint8_t _pad0[4];                        /* 98356 */
} focus_world_t;
static_assert(sizeof(focus_world_t) == 98360);

static void focus_world_read(const game_t *game, const entity_t *entity, const player_t *player, focus_world_t *out) {
    memset(out, 0, sizeof(*out));
    const entity_store_t *const store = &entity->store;
    out->mobs = store->count;
    for (uint32_t i = 0u; i < store->count && i < FOCUS_BODIES_MAX; i++) {
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            out->position[i][axis] = store->borrowed_position[axis][i];
        }
    }
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        out->body[axis] = player->movement.position[axis];
    }
    out->tick_count = game->tick_count;
    out->clock_units = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    out->spawned = game->mob_spawn.spawned_count;
}

/* Plan 005 phase 7: a long time with the window in the background leaves the world as it was, mobs and time and all, and coming back
 * goes on. Real threads, real frames from fake devices, and the window's focus as display_x11_poll leaves it. */
TEST(a_long_time_without_focus_leaves_mobs_and_time_as_they_were_and_coming_back_goes_on) {
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
    mob_t *const mob = aligned_alloc_64(t, sizeof(mob_t));
    item_t *const item = aligned_alloc_64(t, sizeof(item_t));
    ui_t *const ui = aligned_alloc_64(t, sizeof(ui_t));
    input_t *const input = aligned_alloc_64(t, sizeof(input_t));
    settings_t *const settings = aligned_alloc_64(t, sizeof(settings_t));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    focus_rig_t *const rig = aligned_alloc_64(t, sizeof(focus_rig_t));
    focus_world_t *const before = test_alloc(t, sizeof(focus_world_t));
    focus_world_t *const after = test_alloc(t, sizeof(focus_world_t));
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(job, topology));
    REQUIRE(texture_init(texture));
    REQUIRE(world_init(world, world_limits_default()));
    REQUIRE(terrain_init(terrain, TERRAIN_SEED_DEFAULT));
    REQUIRE(physics_init(physics));
    player_init(player, physics);
    REQUIRE(entity_init(entity, physics));
    REQUIRE(mob_init(mob, job));
    REQUIRE(item_init(item));
    REQUIRE(render_init(render, &bridge->device_memory, job, render_limits_default()));
    REQUIRE(ui_init(ui, texture));
    REQUIRE(settings_init(settings));
    game_init(game, world, terrain, render, job, texture, topology);
    game_player_set(game, player);
    game_entity_set(game, entity);
    game_mob_set(game, mob);
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

    char root[FOCUS_PATH_BYTES];
    snprintf(root, sizeof(root), "/tmp/mc_focus_XXXXXX");
    REQUIRE(mkdtemp(root) != nullptr);
    char saves[FOCUS_PATH_BYTES];
    char file[FOCUS_PATH_BYTES + 16u];
    snprintf(saves, sizeof(saves), "%.200s/saves", root);
    snprintf(file, sizeof(file), "%s/settings.yaml", root);
    settings_load(settings, file);
    CHECK_EQ_U64(settings->values.pause_on_focus_loss, 1u); /* a new install pauses */
    settings->values.render_distance = 4u;
    settings->values.full_detail = 4u;
    settings->values.effects &= ~(uint32_t)(SHADE_FEATURE_AO | SHADE_FEATURE_TAA);

    const uint64_t bytes = 640ull * 480ull * 4ull;
    rig->buffer = (display_buffer_t){
        .owned_map = test_alloc(t, bytes), .map_bytes = bytes, .pitch = 640u * 4u, .width = 640u, .height = 480u};
    const game_config_t config = {.width = 640u, .height = 480u, .show_date = 0u};
    game_session_init(&rig->session, game, ui, terrain, input, &config, saves, 4242u);
    game_session_settings_set(&rig->session, settings);
    /* A window, as game_session_run would have it, whose focus the test moves as the window manager would. */
    rig->display = aligned_alloc_64(t, sizeof(display_t));
    rig->x11 = test_alloc(t, sizeof(display_x11_t));
    memset(rig->display, 0, sizeof(*rig->display));
    memset(rig->x11, 0, sizeof(*rig->x11));
    rig->display->window = 1u;
    rig->display->borrowed_x11 = rig->x11;
    rig->x11->focused = 1u;
    rig->session.borrowed_display = rig->display;
    focus_frames(t, rig, 2u);
    rig->pointer_x = 320;
    rig->pointer_y = 240;

    /* Leaving the window on the menus does nothing: there is no world to stop. */
    focus_leave(t, rig);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_MENU);
    focus_return(t, rig);

    /* Into a world, and to the middle of the night, where mobs come. */
    focus_click(t, rig, AT_MIDDLE_X, AT_SINGLEPLAYER_Y);
    focus_click(t, rig, AT_CREATE_NEW_X, AT_CREATE_NEW_Y);
    focus_key(t, rig, KEY_A);
    focus_key(t, rig, KEY_ENTER);
    FOCUS_WAIT_FOR(game->open != 0u && game->pictures_shown > 0u && game->tick_count > 0u);
    REQUIRE(rig->session.state == GAME_SESSION_PLAYING);
    /* Plan 005 phase 3 gave hostile mobs a spawn weight of 0, so none come by themselves: four zombies are put round the body by hand,
     * in a pause, when the sim is not touching the store, and then the world goes on for a second so they move. */
    focus_leave(t, rig);
    REQUIRE(rig->session.state == GAME_SESSION_PAUSED);
    time_sleep_ns(200000000); /* the tick that was running finishes */
    game_clock_set(game, 0u, 0.0f64);
    static const float64_t around[4][2] = {{4.0f64, 0.0f64}, {-4.0f64, 0.0f64}, {0.0f64, 4.0f64}, {0.0f64, -4.0f64}};
    for (uint32_t i = 0u; i < 4u; i++) {
        const float64_t at[3] = {player->movement.position[0] + around[i][0],
                                 player->movement.position[1] + around[i][1], player->movement.position[2] + 1.0f64};
        CHECK(mob_spawn_body(&game->mob_spawn, &entity->store, MOB_ID_ZOMBIE, at, 0.0f32) != ENTITY_HANDLE_NONE);
    }
    focus_return(t, rig);
    REQUIRE(rig->session.state == GAME_SESSION_PLAYING);
    const uint64_t ticks_placed = game->tick_count;
    FOCUS_WAIT_FOR(game->tick_count > ticks_placed + 20u);
    REQUIRE(entity->store.count > 0u);
    printf("  %" PRIu32 " mobs after %" PRIu64 " ticks of night\n", entity->store.count, game->tick_count);
    const uint64_t pauses = rig->session.pauses;

    /* Away: the pause screen comes up, and nothing in the world moves, however long it is left. */
    focus_leave(t, rig);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PAUSED);
    CHECK_EQ_U64(rig->session.focus_paused, 1u);
    CHECK_EQ_U64(rig->session.pauses, pauses + 1u);
    CHECK_EQ_U64(atomic_load_explicit(&game->paused, memory_order_relaxed), 1u);
    CHECK_EQ_U64(ui->menu.screen, UI_MENU_SCREEN_PAUSE);
    /* The tick that was running when the pause came finishes, and then nothing. */
    time_sleep_ns(200000000);
    focus_world_read(game, entity, player, before);
    const int64_t away_until_ns = time_now_ns() + (int64_t)FOCUS_AWAY_SECONDS * TIME_NS_PER_SECOND;
    uint64_t frames_away = 0u;
    while (time_now_ns() < away_until_ns && rig->failed == 0u) {
        focus_frames(t, rig, 1u);
        frames_away++;
    }
    focus_world_read(game, entity, player, after);
    printf("  %" PRIu64 " frames in %" PRIu32 " s away: %" PRIu32 " mobs, tick %" PRIu64 " before and %" PRIu64
           " after\n",
           frames_away, (uint32_t)FOCUS_AWAY_SECONDS, after->mobs, before->tick_count, after->tick_count);
    CHECK_EQ_U64(after->tick_count, before->tick_count);
    CHECK_EQ_U64(after->clock_units, before->clock_units);
    CHECK_EQ_U64(after->mobs, before->mobs);
    CHECK_EQ_U64(after->spawned, before->spawned);
    CHECK_MEM_EQ(after, before, sizeof(*after));
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PAUSED);
    CHECK_EQ_U64(game->threads_running, 1u);

    /* Back: the pause ends by itself, and the world goes on. */
    focus_return(t, rig);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PLAYING);
    CHECK_EQ_U64(rig->session.focus_paused, 0u);
    FOCUS_WAIT_FOR(game->tick_count > before->tick_count + 2u);
    CHECK(atomic_load_explicit(&game->clock_units, memory_order_relaxed) != before->clock_units);

    /* A pause a person made is theirs: the focus coming back does not end it, and Esc does. */
    focus_key(t, rig, KEY_ESC);
    REQUIRE(rig->session.state == GAME_SESSION_PAUSED);
    focus_leave(t, rig);
    focus_return(t, rig);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PAUSED);
    focus_key(t, rig, KEY_ESC);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PLAYING);

    /* With the setting off, leaving the window leaves the world going, as before. */
    settings->values.pause_on_focus_loss = 0u;
    const uint64_t ticks_off = game->tick_count;
    focus_leave(t, rig);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_PLAYING);
    FOCUS_WAIT_FOR(game->tick_count > ticks_off + 2u);
    focus_return(t, rig);
    settings->values.pause_on_focus_loss = 1u;

    /* Left once more, and out to the title from the pause screen it brought up. */
    focus_leave(t, rig);
    REQUIRE(rig->session.state == GAME_SESSION_PAUSED);
    focus_click(t, rig, AT_MIDDLE_X, AT_PAUSE_TITLE_Y);
    CHECK_EQ_U64(game->open, 0u);
    CHECK_EQ_U64(game->threads_running, 0u);
    CHECK_EQ_U64(rig->session.state, GAME_SESSION_MENU);
    CHECK_EQ_U64(rig->session.failed, 0u);

    rig->session.borrowed_display = nullptr;
    CHECK(game_session_end(&rig->session));
    game_session_deinit(&rig->session);
    input_deinit(input);
    game_deinit(game);
    settings_deinit(settings);
    ui_deinit(ui);
    render_deinit(render);
    item_deinit(item);
    mob_deinit(mob);
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
