/* ctest run_game_command. See README.md. */
#include "harness/index.h"

#include "calendar/index.h"
#include "climate/index.h"
#include "display/headless/index.h"
#include "entity/index.h"
#include "game/command/index.h"
#include "game/corner/index.h"
#include "game/fill/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "gpu_bridge/index.h"
#include "item/index.h"
#include "job/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "save/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    GD_WIDTH = 640u, /* the menus' own layout at scale 2, as tests/run/game/census/fixtures/week.script has it */
    GD_HEIGHT = 480u,
    GD_SCRIPT_BYTES = 65536u,
    GD_EVENTS = 8192u,
    GD_PATH_BYTES = 512u,
    GD_JUMPS = 6u,       /* north 30 km, east 40 km, three times */
    GD_JUMP_WAIT = 300u, /* ticks after each Enter: the draw settles, the body arrives and lands, from a fall of 1000 */
    GD_TICKS_MOST = 4000u, /* a script longer than this is a mistake */
};
static const uint64_t GD_SEED = 7u;

static void *gd_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* ---- The parser, pure --------------------------------------------------------------------------------------------------- */

static game_command_parsed_t gd_parse_ok(test_context_t *t, const char *text) {
    game_command_parsed_t parsed;
    const bool ok = game_command_parse(text, &parsed);
    if (!ok) {
        printf("  %s: %s\n", text, parsed.error);
    }
    CHECK(ok);
    return parsed;
}

static const uint32_t GD_ALL = PLAYER_COMMAND_RELATIVE_X | PLAYER_COMMAND_RELATIVE_Y | PLAYER_COMMAND_RELATIVE_Z;

TEST(the_parser_reads_every_command) {
    game_command_parsed_t p = gd_parse_ok(t, "/tp 10 20 30");
    CHECK_EQ_U64(p.kind, GAME_COMMAND_TP);
    CHECK_EQ_U64(p.tp, PLAYER_COMMAND_TP_EXACT);
    CHECK_EQ_U64(p.relative, 0u);
    CHECK_NEAR(p.target[0], 10.5f64, 0.0f64);
    CHECK_NEAR(p.target[1], 20.5f64, 0.0f64);
    CHECK_NEAR(p.target[2], 30.0f64, 0.0f64);
    /* With no slash, in capitals, with spaces, negatives and decimals: a decimal place across is where it says. */
    p = gd_parse_ok(t, "  TP -10 -20.25   -5.5 ");
    CHECK_NEAR(p.target[0], -9.5f64, 0.0f64);
    CHECK_NEAR(p.target[1], -20.25f64, 0.0f64);
    CHECK_NEAR(p.target[2], -5.5f64, 0.0f64);
    /* Relative, any mix: from (100.5, -40.5, 72), /tp ~2000 ~ ~ is (2100.5, -40.5, 72), as the sim adds it. */
    p = gd_parse_ok(t, "/tp ~2000 ~ ~");
    CHECK_EQ_U64(p.relative, GD_ALL);
    CHECK_NEAR(100.5f64 + p.target[0], 2100.5f64, 0.0f64);
    CHECK_NEAR(-40.5f64 + p.target[1], -40.5f64, 0.0f64);
    CHECK_NEAR(72.0f64 + p.target[2], 72.0f64, 0.0f64);
    p = gd_parse_ok(t, "/tp ~ ~ ~300");
    CHECK_EQ_U64(p.relative, GD_ALL);
    CHECK_NEAR(p.target[2], 300.0f64, 0.0f64);
    p = gd_parse_ok(t, "/tp 5 ~-7.5 64");
    CHECK_EQ_U64(p.relative, PLAYER_COMMAND_RELATIVE_Y);
    CHECK_NEAR(p.target[0], 5.5f64, 0.0f64);
    CHECK_NEAR(p.target[1], -7.5f64, 0.0f64);
    CHECK_NEAR(p.target[2], 64.0f64, 0.0f64);
    /* On the ground at x, y: the height is the ground's. */
    p = gd_parse_ok(t, "/tp 0 0");
    CHECK_EQ_U64(p.tp, PLAYER_COMMAND_TP_GROUND);
    CHECK_EQ_U64(p.relative, PLAYER_COMMAND_RELATIVE_Z);
    CHECK_NEAR(p.target[0], 0.5f64, 0.0f64);
    p = gd_parse_ok(t, "/tp top");
    CHECK_EQ_U64(p.tp, PLAYER_COMMAND_TP_GROUND);
    CHECK_EQ_U64(p.relative, GD_ALL);
    p = gd_parse_ok(t, "/tp lat 60");
    CHECK_EQ_U64(p.tp, PLAYER_COMMAND_TP_GROUND);
    CHECK_EQ_U64(p.relative, PLAYER_COMMAND_RELATIVE_X | PLAYER_COMMAND_RELATIVE_Z);
    CHECK_EQ_U64(p.latitude, 1u);
    p = gd_parse_ok(t, "/tp lat -45 -300");
    CHECK_EQ_U64(p.relative, PLAYER_COMMAND_RELATIVE_Z);
    CHECK_NEAR(p.target[0], -299.5f64, 0.0f64);
    CHECK_EQ_U64(gd_parse_ok(t, "/fly").kind, GAME_COMMAND_FLY);
    CHECK_EQ_U64(gd_parse_ok(t, "/speed").kind, GAME_COMMAND_SPEED_SHOW);
    p = gd_parse_ok(t, "/speed 5");
    CHECK_EQ_U64(p.kind, GAME_COMMAND_SPEED);
    CHECK_NEAR(p.value, 5.0f64, 0.0f64);
    p = gd_parse_ok(t, "/time day 172");
    CHECK_EQ_U64(p.kind, GAME_COMMAND_TIME_DAY);
    CHECK_NEAR(p.value, 172.0f64, 0.0f64);
    p = gd_parse_ok(t, "/time hour 6.5");
    CHECK_EQ_U64(p.kind, GAME_COMMAND_TIME_HOUR);
    CHECK_NEAR(p.value, 6.5f64, 0.0f64);
    CHECK_EQ_U64(gd_parse_ok(t, "/help").kind, GAME_COMMAND_HELP);
}

TEST(latitude_gives_the_right_y) {
    const float64_t degrees[4] = {0.0f64, 45.0f64, 60.0f64, -90.0f64};
    const float64_t ys[4] = {0.0f64, 19149.0f64, 25532.0f64, -38298.0f64};
    for (uint32_t i = 0u; i < 4u; i++) {
        char text[64];
        snprintf(text, sizeof(text), "/tp lat %.0f", (libc_double_t)degrees[i]);
        const game_command_parsed_t p = gd_parse_ok(t, text);
        CHECK_NEAR(p.target[1], ys[i], 1.0f64);
        CHECK_NEAR(climate_latitude_deg(p.target[1]), degrees[i], 1.0e-9f64);
    }
    char line[GAME_COMMAND_TEXT_BYTES];
    game_command_preview("/tp lat 60", line, sizeof(line));
    CHECK_STR_EQ(line, "y 25532 \xC2\xB7 60.0\xC2\xB0 N");
    game_command_preview("/tp lat -45 100", line, sizeof(line));
    CHECK_STR_EQ(line, "y -19149 \xC2\xB7 45.0\xC2\xB0 S");
    game_command_preview("/tp 1 2 3", line, sizeof(line));
    CHECK_STR_EQ(line, "");
}

TEST(a_bad_command_is_refused_by_the_parser) {
    static const char *const bad[] = {"/tp",           "/tp a b",   "/tp lat 91", "/tq",      "/time day 400",
                                      "/tp 1 2 3 4",   "/speed 11", "/speed 2.5", "/tp ~x 0", "/tp 1e3 0",
                                      "/time hour 25", "/tp lat",   "/fly now",   "",         "/tp nan 0"};
    for (uint32_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        game_command_parsed_t p;
        CHECK(!game_command_parse(bad[i], &p));
        CHECK(p.error[0] != '\0');
        CHECK_EQ_U64(p.kind, GAME_COMMAND_BAD);
    }
}

TEST(coordinates_are_clamped) {
    game_command_parsed_t p = gd_parse_ok(t, "/tp 0 0 99999");
    CHECK_NEAR(p.target[2], (float64_t)COLUMN_Z_GAMEPLAY_MAX, 0.0f64);
    p = gd_parse_ok(t, "/tp 0 0 -99999");
    CHECK_NEAR(p.target[2], (float64_t)COLUMN_Z_GAMEPLAY_MIN, 0.0f64);
    p = gd_parse_ok(t, "/tp 9999999999999 -9999999999999 0");
    CHECK_NEAR(p.target[0], PLAYER_COMMAND_PLACE_MOST, 0.0f64);
    CHECK_NEAR(p.target[1], -PLAYER_COMMAND_PLACE_MOST, 0.0f64);
}

TEST(the_history_keeps_the_last_sixteen) {
    game_command_t *const command = test_alloc(t, sizeof(game_command_t));
    game_command_start(command);
    CHECK(game_command_history_back(command, 1u) == nullptr);
    for (uint32_t i = 1u; i <= 17u; i++) {
        char text[32];
        snprintf(text, sizeof(text), "/tp %" PRIu32 " 0", i);
        game_command_history_push(command, text);
    }
    CHECK_EQ_U64(command->history_count, 16u);
    CHECK_STR_EQ(game_command_history_back(command, 1u), "/tp 17 0");
    CHECK_STR_EQ(game_command_history_back(command, 16u), "/tp 2 0");
    CHECK(game_command_history_back(command, 17u) == nullptr);
}

/* ---- A rig: a real game on the card, walking, with a climate --------------------------------------------------------------- */

typedef struct gd_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64 */
    player_t *player;            /* 72 */
    entity_t *entity;            /* 80 */
    item_t *item;                /* 88 */
    ui_t *ui;                    /* 96 */
    save_t *save;                /* 104 */
    climate_t *climate;          /* 112 */
    char *root;                  /* 120: a directory of its own, for saves */
} gd_rig_t;
static_assert(sizeof(gd_rig_t) == 128);

static bool gd_rig_begin(test_context_t *t, gd_rig_t *rig, bool climate) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = gd_aligned(t, sizeof(gpu_bridge_t));
    rig->job = gd_aligned(t, sizeof(job_system_t));
    rig->world = gd_aligned(t, sizeof(world_t));
    rig->terrain = gd_aligned(t, sizeof(terrain_t));
    rig->render = gd_aligned(t, sizeof(render_t));
    rig->texture = gd_aligned(t, sizeof(texture_t));
    rig->game = gd_aligned(t, sizeof(game_t));
    rig->save = gd_aligned(t, sizeof(save_t));
    rig->physics = gd_aligned(t, sizeof(physics_t));
    rig->player = gd_aligned(t, sizeof(player_t));
    rig->entity = gd_aligned(t, sizeof(entity_t));
    rig->item = gd_aligned(t, sizeof(item_t));
    rig->ui = gd_aligned(t, sizeof(ui_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->root = test_alloc(t, GD_PATH_BYTES);
    snprintf(rig->root, GD_PATH_BYTES, "/tmp/mc_game_command_XXXXXX");
    if (!CHECK(mkdtemp(rig->root) != nullptr) ||
        !CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, GD_SEED)) || !CHECK(save_init(rig->save)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default())) ||
        !CHECK(physics_init(rig->physics)) || !CHECK(entity_init(rig->entity, rig->physics)) ||
        !CHECK(item_init(rig->item)) || !CHECK(ui_init(rig->ui, rig->texture))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    player_init(rig->player, rig->physics);
    game_player_set(rig->game, rig->player);
    game_entity_set(rig->game, rig->entity);
    game_item_set(rig->game, rig->item);
    game_ui_set(rig->game, rig->ui);
    if (climate) {
        rig->climate = gd_aligned(t, sizeof(climate_t));
        if (!CHECK(climate_init(rig->climate))) {
            return false;
        }
        game_climate_set(rig->game, rig->climate);
    }
    return true;
}

static void gd_rig_end(test_context_t *t, gd_rig_t *rig) {
    game_deinit(rig->game);
    if (rig->climate != nullptr) {
        climate_deinit(rig->climate);
    }
    save_deinit(rig->save);
    ui_deinit(rig->ui);
    item_deinit(rig->item);
    entity_deinit(rig->entity);
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
    if (strncmp(rig->root, "/tmp/mc_game_command_", 21u) == 0) {
        char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", rig->root, nullptr};
        test_process_t process;
        test_process_run(t, argv, 30000u, &process);
        CHECK_EQ_I64(process.exit_code, 0);
    }
}

/* ---- A script that types --------------------------------------------------------------------------------------------------- */

typedef struct gd_script_t {
    char *text;       /* 0 */
    uint64_t length;  /* 8 */
    uint32_t tick;    /* 16: of the last line */
    uint8_t _pad0[4]; /* 20 */
} gd_script_t;
static_assert(sizeof(gd_script_t) == 24);

static void gd_line(gd_script_t *s, const char *line) {
    const int32_t wrote = snprintf(s->text + s->length, GD_SCRIPT_BYTES - s->length, "%s\n", line);
    if (wrote > 0) {
        s->length += (uint64_t)wrote;
    }
}

/* key down and up on one tick, after `after` more ticks. */
static void gd_key(gd_script_t *s, uint32_t after, const char *name) {
    char line[64];
    s->tick += after;
    snprintf(line, sizeof(line), "%" PRIu32 " key %s down", s->tick, name);
    gd_line(s, line);
    snprintf(line, sizeof(line), "%" PRIu32 " key %s up", s->tick, name);
    gd_line(s, line);
}

/* Each character as the key a US keyboard types it with, a tick apart: ~ with Shift held over its tick. */
static void gd_type(gd_script_t *s, const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
        char name[16];
        if (*c == '~') {
            char line[64];
            s->tick += 1u;
            snprintf(line, sizeof(line), "%" PRIu32 " key lshift down", s->tick);
            gd_line(s, line);
            gd_key(s, 0u, "grave");
            snprintf(line, sizeof(line), "%" PRIu32 " key lshift up", s->tick + 1u);
            gd_line(s, line);
            continue;
        }
        if (*c == ' ') {
            snprintf(name, sizeof(name), "space");
        } else if (*c == '/') {
            snprintf(name, sizeof(name), "slash");
        } else if (*c == '-') {
            snprintf(name, sizeof(name), "minus");
        } else if (*c == '.') {
            snprintf(name, sizeof(name), "dot");
        } else {
            snprintf(name, sizeof(name), "%c", *c);
        }
        gd_key(s, *c == '/' ? 1u : 2u, name);
    }
}

/* A new world through the menus, as fixtures/week.script makes one, with seed 7. */
static void gd_menus(gd_script_t *s) {
    static const char *const lines[] = {
        "1 move 0 -40",     "2 button left down", "3 button left up",  "4 move 132 196",      "5 button left down",
        "6 button left up", "7 key w down",       "8 key w up",        "9 key e down",        "10 key e up",
        "11 key k down",    "12 key k up",        "13 move -132 -194", "14 button left down", "15 button left up",
        "16 key 7 down",    "17 key 7 up",        "18 key enter down", "19 key enter up"};
    for (uint32_t i = 0u; i < sizeof(lines) / sizeof(lines[0]); i++) {
        gd_line(s, lines[i]);
    }
    s->tick = 19u;
}

typedef struct gd_box_t gd_box_t;
typedef struct gd_jumps_t gd_jumps_t;
static void gd_box_look(gd_box_t *box, game_session_t *session, uint32_t tick);
static void gd_jumps_look(gd_jumps_t *jumps, game_session_t *session, uint32_t tick);

static bool gd_session_run(test_context_t *t, gd_rig_t *rig, const gd_script_t *s, uint32_t render_distance,
                           gd_box_t *box, gd_jumps_t *jumps) {
    char *const saves = test_alloc(t, GD_PATH_BYTES);
    snprintf(saves, GD_PATH_BYTES, "%s/saves", rig->root);
    const game_config_t config = {.render_distance = render_distance,
                                  .width = GD_WIDTH,
                                  .height = GD_HEIGHT,
                                  .features_off = SHADE_FEATURE_TAA,
                                  .show_date = 1u};
    game_session_t *const session = test_alloc(t, sizeof(game_session_t));
    game_session_init(session, rig->game, rig->ui, rig->terrain, nullptr, &config, saves, GD_SEED);
    display_headless_scene_t scene;
    game_session_headless_scene(session, &scene);
    display_script_event_t *const events = test_alloc(t, GD_EVENTS * sizeof(display_script_event_t));
    display_script_t script;
    display_script_error_t error;
    const bool parsed = display_script_parse(s->text, s->length, MC_GAME_TICK_HZ, events, GD_EVENTS, &script, &error);
    if (!parsed) {
        printf("  script:%" PRIu32 ": %s\n", error.line, error.message);
        return false;
    }
    if (!CHECK(script.last_tick < GD_TICKS_MOST)) {
        return false;
    }
    const uint64_t bytes = post_xrgb_bytes(GD_WIDTH, GD_HEIGHT);
    display_buffer_t frame = {.owned_map = gd_aligned(t, bytes),
                              .map_bytes = bytes,
                              .pitch = GD_WIDTH * 4u,
                              .width = GD_WIDTH,
                              .height = GD_HEIGHT};
    display_script_state_t *const state = test_alloc(t, sizeof(display_script_state_t));
    memset(state, 0, sizeof(*state));
    uint32_t next = 0u;
    bool ok = true;
    for (uint32_t tick = 0u; tick <= script.last_tick && ok; tick++) {
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
        ok = CHECK(scene.tick(scene.borrowed_context, &info)) &&
             CHECK(scene.draw(scene.borrowed_context, &info, &frame));
        /* The test's look at the game after each draw. */
        if (box != nullptr) {
            gd_box_look(box, session, tick);
        }
        if (jumps != nullptr) {
            gd_jumps_look(jumps, session, tick);
        }
    }
    CHECK(game_session_end(session));
    game_session_deinit(session);
    return ok;
}

/* ---- The box: keys, history, a bad command, Esc ------------------------------------------------------------------------------ */

typedef struct gd_box_t {
    uint32_t typed_tick; /* 0: Enter of /tp 0 0 */
    uint32_t up_tick;    /* 4: Up, after T */
    uint32_t bad_tick;   /* 8: Enter of /tq */
    uint32_t esc_tick;   /* 12: Esc with the box up */
    uint32_t pause_tick; /* 16: Esc with nothing up */
    uint32_t checks;     /* 20: each look that happened */
    float64_t before[3]; /* 24: the feet before the bad command */
    uint32_t teleports;  /* 48: before it */
    uint8_t _pad0[4];    /* 52 */
    test_context_t *t;   /* 56 */
} gd_box_t;
static_assert(sizeof(gd_box_t) == 64);

static void gd_box_look(gd_box_t *box, game_session_t *session, uint32_t tick) {
    game_t *const game = session->borrowed_game;
    const game_command_t *const command = game_command_of(game);
    test_context_t *const t = box->t;
    if (command == nullptr) {
        return;
    }
    if (tick == box->typed_tick - 1u) {
        CHECK_EQ_U64(command->up, 1u);
        CHECK_STR_EQ(command->text, "/tp 0 0");
        box->checks++;
    }
    if (tick == box->typed_tick + 1u) {
        CHECK_EQ_U64(command->up, 0u); /* it ran, so it closed */
        CHECK_EQ_U64(command->history_count, 1u);
        box->checks++;
    }
    if (tick == box->up_tick) {
        CHECK_EQ_U64(command->up, 1u);
        CHECK_STR_EQ(command->text, "/tp 0 0");
        box->checks++;
    }
    if (tick == box->bad_tick - 1u) {
        CHECK_STR_EQ(command->text, "/tq");
        memcpy(box->before, game->borrowed_player->movement.position, sizeof(box->before));
        box->teleports = game->borrowed_player->teleports;
    }
    if (tick == box->bad_tick + 3u) {
        CHECK_EQ_U64(command->up, 1u); /* a bad one stays, with its red line */
        CHECK(command->error[0] != '\0');
        CHECK(memcmp(box->before, game->borrowed_player->movement.position, sizeof(box->before)) == 0);
        CHECK_EQ_U64(game->borrowed_player->teleports, box->teleports);
        /* The others, through the same door. Each a red line, and nothing sent. */
        static const char *const bad[] = {"/tp", "/tp a b", "/tp lat 91", "/time day 400"};
        for (uint32_t i = 0u; i < 4u; i++) {
            CHECK(!game_command_run(game, bad[i]));
            CHECK(command->error[0] != '\0');
        }
        CHECK_EQ_U64(atomic_load(&game->borrowed_player->command_word), 0u);
        box->checks++;
    }
    if (tick == box->esc_tick + 1u) {
        CHECK_EQ_U64(command->up, 0u);
        CHECK_EQ_U64(session->state, GAME_SESSION_PLAYING); /* not paused: Esc closed the box first */
        CHECK_EQ_U64(atomic_load(&game->paused), 0u);
        box->checks++;
    }
    if (tick == box->pause_tick + 1u) {
        CHECK_EQ_U64(session->state, GAME_SESSION_PAUSED);
        box->checks++;
    }
}

TEST(the_box_takes_keys_keeps_history_refuses_bad_commands_and_esc_closes_it_first) {
    gd_rig_t rig;
    REQUIRE(gd_rig_begin(t, &rig, false));
    gd_script_t s = {.text = test_alloc(t, GD_SCRIPT_BYTES), .length = 0u, .tick = 0u};
    gd_menus(&s);
    gd_box_t box;
    memset(&box, 0, sizeof(box));
    box.t = t;
    s.tick += 40u;
    gd_type(&s, "/tp 0 0");
    gd_key(&s, 2u, "enter");
    box.typed_tick = s.tick;
    gd_key(&s, 30u, "t");
    gd_key(&s, 2u, "up");
    box.up_tick = s.tick;
    gd_key(&s, 2u, "escape");
    gd_type(&s, "/tq");
    gd_key(&s, 2u, "enter");
    box.bad_tick = s.tick;
    s.tick += 5u;
    gd_key(&s, 2u, "escape");
    box.esc_tick = s.tick;
    gd_key(&s, 4u, "escape");
    box.pause_tick = s.tick;
    char end[32];
    snprintf(end, sizeof(end), "%" PRIu32 " end", s.tick + 3u);
    gd_line(&s, end);
    REQUIRE(gd_session_run(t, &rig, &s, 3u, &box, nullptr));
    CHECK_EQ_U64(box.checks, 6u);
    gd_rig_end(t, &rig);
}

/* ---- Three far jumps ------------------------------------------------------------------------------------------------------ */

typedef struct gd_jumps_t {
    uint32_t enter_tick[GD_JUMPS];   /* 0 */
    uint64_t columns[GD_JUMPS + 1u]; /* 24: columns_made at each Enter, and at the end */
    float64_t feet[GD_JUMPS][3];     /* 80: where the body stood before each next Enter */
    uint32_t tunings;                /* 224: places the tuning lines were checked at */
    uint32_t landed;                 /* 228: jumps whose arrival has ended */
    climate_t *borrowed_climate;     /* 232 */
    test_context_t *t;               /* 240 */
    uint64_t hurt_before;            /* 248: the body's hurt_count when the arrival under way began */
    uint8_t arriving_before;         /* 256: the body's arriving on the last tick looked at */
    uint8_t _pad0[7];                /* 257 */
} gd_jumps_t;
static_assert(sizeof(gd_jumps_t) == 264);

static int32_t gd_tenths(float32_t celsius) {
    return (int32_t)floorf(celsius * 10.0f32 + 0.5f32);
}

/* The tuning lines' words against climate_normals_at at the feet, as the sim works them out. */
static void gd_tuning_check(test_context_t *t, gd_jumps_t *jumps, game_t *game) {
    const game_corner_t *const corner = game_corner_of(game);
    const float64_t *const feet = game->borrowed_player->movement.position;
    climate_normals_at_t normals;
    const calendar_date_t date = game_calendar_date(game);
    if (!CHECK(climate_normals_at(jumps->borrowed_climate, feet[0], feet[1], (float32_t)(feet[2] - MC_WORLD_Z_SEA),
                                  (float32_t)date.day_of_year, &normals))) {
        return;
    }
    /* Plan 006 phase 02: the words are terrain_climate_at on the sample a column job here has, exactly, and near the
     * climate's own normals (the sample's warp is terrain's own, and it blends integers). */
    terrain_climate_sample_t sample;
    game_corner_tuning_climate(game, feet[0], feet[1], &sample);
    const int32_t z = (int32_t)floor(feet[2] - (float64_t)MC_WORLD_Z_SEA);
    const terrain_climate_point_t point =
        terrain_climate_at(&sample, (int64_t)floor(feet[0]), (int64_t)floor(feet[1]), z);
    const uint32_t kinds = atomic_load(&corner->tuning_kinds);
    CHECK_EQ_U64(kinds >> 24, GAME_CORNER_TUNING_CLIMATE);
    CHECK_EQ_U64((kinds >> 16) & 0xFFu, point.climate_class);
    /* Plan 006 phase 03: the biome and the family it was chosen in, and the biome is of that family. */
    const terrain_biome_column_t column = terrain_biome_column_at(game->borrowed_terrain->borrowed_biome, &sample,
                                                                  (int32_t)floor(feet[0]), (int32_t)floor(feet[1]));
    CHECK_EQ_U64((kinds >> 8) & 0xFFu, column.family);
    CHECK_EQ_U64(kinds & 0xFFu, terrain_biome_of_voxel(&column, (int32_t)floor(feet[2])));
    CHECK((biome_defs[column.biome].families & (1u << column.family)) != 0u);
    const uint32_t year = atomic_load(&corner->tuning_year), cold = atomic_load(&corner->tuning_cold);
    CHECK_EQ_I64((int16_t)(uint16_t)(year & 0xFFFFu), gd_tenths((float32_t)point.t_mean_cc / 100.0f32));
    CHECK_EQ_I64((int16_t)(uint16_t)(year >> 16), gd_tenths((float32_t)point.t_warm_cc / 100.0f32));
    CHECK_EQ_I64((int16_t)(uint16_t)(cold & 0xFFFFu), gd_tenths((float32_t)point.t_cold_cc / 100.0f32));
    CHECK_EQ_I64((int16_t)(uint16_t)(cold >> 16), gd_tenths(normals.t_day_c));
    CHECK_EQ_I64(atomic_load(&corner->tuning_z), (int64_t)z);
    CHECK_EQ_U64(atomic_load(&corner->tuning_rain) & 0xFFFFu, (uint64_t)point.rain_mm_year);
    CHECK_EQ_I64(atomic_load(&corner->tuning_snowline), point.snow_line_z);
    CHECK_NEAR((float32_t)point.t_mean_cc / 100.0f32, normals.t_mean_c, 1.5f32);
    printf("  sample against the climate's normals: yearly %.2f against %.2f, warm %.2f against %.2f, rain %d against "
           "%.0f mm\n",
           (libc_double_t)((float64_t)point.t_mean_cc / 100.0f64), (libc_double_t)normals.t_mean_c,
           (libc_double_t)((float64_t)point.t_warm_cc / 100.0f64), (libc_double_t)normals.t_max_month_c,
           point.rain_mm_year, (libc_double_t)normals.rain_mm_year);
    char lines[GAME_CORNER_TUNING_LINES][GAME_CORNER_TUNING_BYTES];
    game_corner_tuning_read(game, lines);
    printf("  at %.0f, %.0f, %.0f:\n    %s\n    %s\n    %s\n", (libc_double_t)feet[0], (libc_double_t)feet[1],
           (libc_double_t)feet[2], lines[0], lines[1], lines[2]);
    jumps->tunings++;
}

static void gd_jumps_look(gd_jumps_t *jumps, game_session_t *session, uint32_t tick) {
    game_t *const game = session->borrowed_game;
    test_context_t *const t = jumps->t;
    const player_t *const body = game->borrowed_player;
    /* The jump itself never hurts: from the start of an arrival (waiting for the column) to its end (landed, swimming or
     * flying, or put out of rock) no hit is taken. What the place does after is the place's: on plan 007 phase 03's land
     * the third jump puts the body in water at 40008, 60008, 132, and it drowns once before the next Enter. */
    const uint8_t arriving = body->survival.arriving;
    if (jumps->arriving_before == PLAYER_SURVIVAL_ARRIVING_NONE && arriving != PLAYER_SURVIVAL_ARRIVING_NONE) {
        jumps->hurt_before = body->survival.hurt_count;
    } else if (jumps->arriving_before != PLAYER_SURVIVAL_ARRIVING_NONE && arriving == PLAYER_SURVIVAL_ARRIVING_NONE) {
        CHECK_EQ_U64(body->survival.hurt_count, jumps->hurt_before);
        jumps->landed++;
        printf("  jump %" PRIu32 " arrived at tick %" PRIu32 " at %.0f, %.0f, %.0f, hurt %" PRIu64 " times so far\n",
               jumps->landed, tick, (libc_double_t)body->movement.position[0],
               (libc_double_t)body->movement.position[1], (libc_double_t)body->movement.position[2],
               body->survival.hurt_count);
    }
    jumps->arriving_before = arriving;
    for (uint32_t j = 0u; j < GD_JUMPS; j++) {
        if (tick == jumps->enter_tick[j] - 1u) {
            CHECK_EQ_U64(jumps->landed, j);
            jumps->columns[j] = game->columns_made;
            if (j != 0u) {
                const player_t *const player = game->borrowed_player;
                memcpy(jumps->feet[j - 1u], player->movement.position, sizeof(jumps->feet[0]));
                /* It has come down where it went (unharmed on the way: above), standing in the open. */
                CHECK_EQ_U64(player->survival.arriving, PLAYER_SURVIVAL_ARRIVING_NONE);
                CHECK(player->movement.waiting_for_column == 0u);
                if (j % 2u == 0u) {
                    gd_tuning_check(t, jumps, game);
                }
            }
        }
    }
    if (tick == jumps->enter_tick[GD_JUMPS - 1u] + GD_JUMP_WAIT) {
        jumps->columns[GD_JUMPS] = game->columns_made;
        memcpy(jumps->feet[GD_JUMPS - 1u], game->borrowed_player->movement.position, sizeof(jumps->feet[0]));
        gd_tuning_check(t, jumps, game);
        const game_fill_t *const fill = game_fill_of(game);
        uint32_t found = 0u;
        for (uint32_t i = 0u; i < fill->history_count && i < GAME_FILL_HISTORY; i++) {
            const game_fill_summary_t *const summary = &fill->history[i];
            if (summary->kind != GAME_FILL_JUMP) {
                continue;
            }
            found++;
            printf("  jump %" PRIu32 ": the nearest rings drawn at %.2f s, all at %.2f s\n", found,
                   (libc_double_t)((float64_t)summary->ring_near_ns / 1.0e9f64),
                   (libc_double_t)((float64_t)summary->near_ns / 1.0e9f64));
            CHECK(summary->ring_near_ns >= 0);
#if defined(NDEBUG)
            CHECK(summary->ring_near_ns < 15000000000);
#else
            CHECK(summary->ring_near_ns < 120000000000); /* -O0 terrain and climate: only that it comes */
#endif
        }
        CHECK_EQ_U64(found, GD_JUMPS);
        CHECK_EQ_U64(jumps->landed, GD_JUMPS);
    }
}

TEST(three_far_jumps_each_redraw_within_fifteen_seconds) {
    gd_rig_t rig;
    REQUIRE(gd_rig_begin(t, &rig, true));
    gd_script_t s = {.text = test_alloc(t, GD_SCRIPT_BYTES), .length = 0u, .tick = 0u};
    gd_menus(&s);
    gd_jumps_t jumps;
    memset(&jumps, 0, sizeof(jumps));
    jumps.borrowed_climate = rig.climate;
    jumps.t = t;
    s.tick += 40u;
    for (uint32_t j = 0u; j < GD_JUMPS; j++) {
        gd_type(&s, j % 2u == 0u ? "/tp ~0 ~30000 ~" : "/tp ~40000 ~ ~");
        gd_key(&s, 2u, "enter");
        jumps.enter_tick[j] = s.tick;
        s.tick += GD_JUMP_WAIT;
    }
    char end[32];
    snprintf(end, sizeof(end), "%" PRIu32 " end", s.tick + 1u);
    gd_line(&s, end);
    const int64_t started_ns = time_now_ns();
    REQUIRE(gd_session_run(t, &rig, &s, 6u, nullptr, &jumps));
    printf("  the whole run took %.1f s\n", (libc_double_t)time_ns_to_seconds(time_now_ns() - started_ns));
    /* Each jump is where it said, from where the body stood, and new columns came after every one. */
    for (uint32_t j = 0u; j < GD_JUMPS; j++) {
        CHECK(jumps.columns[j + 1u] > jumps.columns[j]);
        if (j != 0u) {
            const float64_t dx = jumps.feet[j][0] - jumps.feet[j - 1u][0];
            const float64_t dy = jumps.feet[j][1] - jumps.feet[j - 1u][1];
            CHECK_NEAR(dx, j % 2u == 0u ? 0.0f64 : 40000.0f64, 1.0f64);
            CHECK_NEAR(dy, j % 2u == 0u ? 30000.0f64 : 0.0f64, 1.0f64);
        }
    }
    CHECK_EQ_U64(jumps.tunings, 3u);
    gd_rig_end(t, &rig);
}

/* ---- Saves and the clock ------------------------------------------------------------------------------------------------ */

typedef struct gd_ticker_t {
    display_headless_scene_t scene; /* 0 */
    display_script_state_t state;   /* 24 */
    uint32_t tick;                  /* 176 */
    uint8_t _pad0[4];               /* 180 */
} gd_ticker_t;
static_assert(sizeof(gd_ticker_t) == 184);

static gd_ticker_t *gd_ticker(test_context_t *t, game_t *game) {
    gd_ticker_t *const ticker = test_alloc(t, sizeof(gd_ticker_t));
    memset(ticker, 0, sizeof(*ticker));
    game_headless_scene(game, &ticker->scene);
    return ticker;
}

static bool gd_ticks(gd_ticker_t *ticker, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        ticker->tick++;
        const display_headless_tick_t tick = {
            .events = nullptr, .state = &ticker->state, .event_count = 0u, .tick = ticker->tick};
        if (!ticker->scene.tick(ticker->scene.borrowed_context, &tick)) {
            return false;
        }
    }
    return true;
}

static game_config_t gd_config(void) {
    return (game_config_t){.render_distance = 3u,
                           .width = 160u,
                           .height = 90u,
                           .features_off = SHADE_FEATURE_TAA | SHADE_FEATURE_DAY_CYCLE,
                           .show_date = 0u};
}

TEST(a_save_mid_jump_reloads_standing_and_fly_is_saved) {
    gd_rig_t rig;
    REQUIRE(gd_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    player_t *const player = rig.player;
    game_save_set(game, rig.save);
    const game_config_t config = gd_config();
    REQUIRE(save_open(rig.save, rig.root, GD_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    gd_ticker_t *ticker = gd_ticker(t, game);
    REQUIRE(gd_ticks(ticker, 40u));
    const float32_t health = player->survival.health;
    REQUIRE(game_command_run(game, "/tp ~3000 ~-2000"));
    REQUIRE(gd_ticks(ticker, 1u)); /* the sim takes it: there, and waiting for the ground */
    REQUIRE(player->survival.arriving == PLAYER_SURVIVAL_ARRIVING_WAIT);
    const float64_t there[2] = {player->movement.position[0], player->movement.position[1]};
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(rig.save);

    REQUIRE(save_open(rig.save, rig.root, GD_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(player->survival.arriving, PLAYER_SURVIVAL_ARRIVING_WAIT);
    CHECK_NEAR(player->movement.position[0], there[0], 0.0f64);
    CHECK_NEAR(player->movement.position[1], there[1], 0.0f64);
    ticker = gd_ticker(t, game);
    REQUIRE(gd_ticks(ticker, 200u));
    CHECK_EQ_U64(player->survival.arriving, PLAYER_SURVIVAL_ARRIVING_NONE);
    CHECK_EQ_U64(player->movement.on_ground, 1u);
    collision_box_t box;
    player_movement_box(player->movement.position, PLAYER_MOVEMENT_HEIGHT_STANDING, &box);
    CHECK(physics_box_is_clear(rig.physics, &rig.world->map, &box));
    CHECK_EQ_U64(player->survival.hurt_count, 0u);
    CHECK_NEAR((float64_t)player->survival.health, (float64_t)health, 0.0f64);

    /* Flying, and /speed 4, kept by a save. */
    REQUIRE(game_command_run(game, "/fly"));
    REQUIRE(gd_ticks(ticker, 2u));
    REQUIRE(game_command_run(game, "/speed 4"));
    REQUIRE(gd_ticks(ticker, 2u));
    CHECK_EQ_U64(player->movement.mode, PLAYER_MOVEMENT_MODE_FLY);
    char line[GAME_COMMAND_TEXT_BYTES];
    game_command_flight_line(game, line, sizeof(line));
    CHECK_STR_EQ(line, "Flying \xC2\xB7 466 b/s \xC2\xB7 speed 4x");
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(rig.save);
    REQUIRE(save_open(rig.save, rig.root, GD_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(player->movement.mode, PLAYER_MOVEMENT_MODE_FLY);
    CHECK_EQ_U64(player->movement.speed_factor, 4u);
    ticker = gd_ticker(t, game);
    REQUIRE(gd_ticks(ticker, 20u));
    CHECK_EQ_U64(player->movement.mode, PLAYER_MOVEMENT_MODE_FLY);
    game_command_flight_line(game, line, sizeof(line));
    CHECK_STR_EQ(line, "Flying \xC2\xB7 466 b/s \xC2\xB7 speed 4x");

    /* The clock: on to day 172, then to 06:00, only ever forward. */
    REQUIRE(game_command_run(game, "/time day 172"));
    CHECK_EQ_U64(game_calendar_date(game).day_of_year + 1u, 172u);
    REQUIRE(game_command_run(game, "/time hour 6"));
    calendar_date_t date = game_calendar_date(game);
    CHECK_EQ_U64(date.hour, 6u);
    CHECK_EQ_U64(date.minute, 0u);
    CHECK_EQ_U64(date.day_of_year + 1u, 173u); /* it was past 06:00 on 172, so the next 06:00 */
    REQUIRE(game_command_run(game, "/time day 172"));
    date = game_calendar_date(game);
    CHECK_EQ_U64(date.day_of_year + 1u, 172u);
    CHECK(game_command_run(game, "/speed"));
    CHECK_STR_EQ(game_command_of(game)->line, "Speed 4x (/speed N, 1 to 10, to change it)");
    game_close(game);
    save_close(rig.save);
    gd_rig_end(t, &rig);
}

TEST_MAIN()
