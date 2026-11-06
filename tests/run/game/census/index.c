/* ctest run_game_census. See README.md. */
#include "harness/index.h"

#include "calendar/index.h"
#include "display/headless/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "physics/index.h"
#include "render/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include "climate/weather/flow/index.h"
#include "run/game/census/fixtures/weather.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    GC_WEEKS_MOST = 256u,
    GC_WIDTH = 160u,
    GC_HEIGHT = 90u,
    GC_SESSION_WIDTH =
        640u, /* the menus' own layout, at scale 2, as tests/run/ui/menu/fixtures/flow.script works it out */
    GC_SESSION_HEIGHT = 480u,
    GC_EVENTS = 256u,
    GC_SCRIPT_BYTES = 8192u,
    GC_PATH_BYTES = 512u,
    GC_TICKS_PER_DAY = 24000u,
};
static const uint64_t GC_SEED = 4242u;

/* 06:00 on clock day 5: where week 12, a new world's first census, starts. */
static const calendar_clock_t GC_FIRST_CENSUS = 5ull * CALENDAR_CLOCK_DAY + CALENDAR_CLOCK_DAY_START;
/* One tick of the clock, in its units: 2^32 / 24000, rounded up. */
static const uint64_t GC_TICK_UNITS = (CALENDAR_CLOCK_DAY + GC_TICKS_PER_DAY - 1u) / GC_TICKS_PER_DAY;

static void *gc_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* ---- What the census hook was called with ------------------------------------------------------------------------------ */

/* The test's own: written by whichever thread runs the sim, read by main once that has stopped or the latch has said so. */
typedef struct gc_record_t {
    game_t *borrowed_game;                  /* 0 */
    uint64_t weeks[GC_WEEKS_MOST];          /* 8 */
    calendar_clock_t clocks[GC_WEEKS_MOST]; /* 2056: the clock the sim read as it ran each */
    uint8_t fast[GC_WEEKS_MOST];            /* 4104 */
    uint32_t count;                         /* 4360 */
    uint8_t _pad0[4];                       /* 4364 */
} gc_record_t;
static_assert(sizeof(gc_record_t) == 4368);

static void gc_record_week(void *context, uint64_t week, bool fast) {
    gc_record_t *const record = context;
    if (record->count < GC_WEEKS_MOST) {
        record->weeks[record->count] = week;
        record->clocks[record->count] = atomic_load_explicit(&record->borrowed_game->clock_units, memory_order_relaxed);
        record->fast[record->count] = fast ? 1u : 0u;
    }
    record->count++;
}

/* ---- A rig: a real game on the card --------------------------------------------------------------------------------------- */

typedef struct gc_rig_t {
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
    ui_t *ui;                    /* 96: walking only */
    save_t *save;                /* 104 */
    gc_record_t *record;         /* 112 */
    char *root;                  /* 120: a directory of its own, for saves */
} gc_rig_t;
static_assert(sizeof(gc_rig_t) == 128);

static bool gc_rig_begin(test_context_t *t, gc_rig_t *rig, bool walking) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = gc_aligned(t, sizeof(gpu_bridge_t));
    rig->job = gc_aligned(t, sizeof(job_system_t));
    rig->world = gc_aligned(t, sizeof(world_t));
    rig->terrain = gc_aligned(t, sizeof(terrain_t));
    rig->render = gc_aligned(t, sizeof(render_t));
    rig->texture = gc_aligned(t, sizeof(texture_t));
    rig->game = gc_aligned(t, sizeof(game_t));
    rig->save = gc_aligned(t, sizeof(save_t));
    rig->record = test_alloc(t, sizeof(gc_record_t));
    memset(rig->record, 0, sizeof(*rig->record));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->root = test_alloc(t, GC_PATH_BYTES);
    snprintf(rig->root, GC_PATH_BYTES, "/tmp/mc_game_census_XXXXXX");
    if (!CHECK(mkdtemp(rig->root) != nullptr) ||
        !CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, GC_SEED)) || !CHECK(save_init(rig->save)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    rig->record->borrowed_game = rig->game;
    game_census_hook_set(rig->game, gc_record_week, rig->record);
    if (walking) {
        rig->physics = gc_aligned(t, sizeof(physics_t));
        rig->player = gc_aligned(t, sizeof(player_t));
        rig->entity = gc_aligned(t, sizeof(entity_t));
        rig->item = gc_aligned(t, sizeof(item_t));
        rig->ui = gc_aligned(t, sizeof(ui_t));
        if (!CHECK(physics_init(rig->physics)) || !CHECK(entity_init(rig->entity, rig->physics)) ||
            !CHECK(item_init(rig->item)) || !CHECK(ui_init(rig->ui, rig->texture))) {
            return false;
        }
        player_init(rig->player, rig->physics);
        game_player_set(rig->game, rig->player);
        game_entity_set(rig->game, rig->entity);
        game_item_set(rig->game, rig->item);
    }
    return true;
}

static void gc_rig_end(test_context_t *t, gc_rig_t *rig) {
    game_deinit(rig->game);
    save_deinit(rig->save);
    if (rig->player != nullptr) {
        ui_deinit(rig->ui);
        item_deinit(rig->item);
        entity_deinit(rig->entity);
        player_deinit(rig->player);
        physics_deinit(rig->physics);
    }
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
    if (strncmp(rig->root, "/tmp/mc_game_census_", 20u) == 0) {
        char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", rig->root, nullptr};
        test_process_t process;
        test_process_run(t, argv, 30000u, &process);
        CHECK_EQ_I64(process.exit_code, 0);
    }
}

static game_config_t gc_config(uint32_t features_off) {
    return (game_config_t){.render_distance = 2u,
                           .width = GC_WIDTH,
                           .height = GC_HEIGHT,
                           .features_off = features_off | SHADE_FEATURE_TAA,
                           .show_date = 0u};
}

/* The headless scene's ticks, with no keys down: one tick of the clock and one of the sim each. */
typedef struct gc_ticker_t {
    display_headless_scene_t scene; /* 0 */
    display_script_state_t state;   /* 24 */
    uint32_t tick;                  /* 176 */
    uint8_t _pad0[4];               /* 180 */
} gc_ticker_t;
static_assert(sizeof(gc_ticker_t) == 184);

static gc_ticker_t *gc_ticker(test_context_t *t, game_t *game) {
    gc_ticker_t *const ticker = test_alloc(t, sizeof(gc_ticker_t));
    memset(ticker, 0, sizeof(*ticker));
    game_headless_scene(game, &ticker->scene);
    return ticker;
}

static bool gc_ticks(gc_ticker_t *ticker, uint32_t ticks) {
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

static calendar_clock_t gc_clock(const game_t *game) {
    return atomic_load_explicit(&game->clock_units, memory_order_relaxed);
}

/* ---- Plain ticks ----------------------------------------------------------------------------------------------------------- */

TEST(through_plain_ticks_the_census_fires_once_a_week_at_06_00) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    const game_config_t config = gc_config(0u);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(game->day_zero, CALENDAR_DAY_ZERO);
    CHECK_EQ_U64(game->last_census_week, 11u);
    CHECK_EQ_U64(game->clock_days, 0u);
    const calendar_date_t start = game_calendar_date(game);
    CHECK_EQ_U64(start.day_of_year + 1u, 80u);
    CHECK_EQ_U64(start.year, 1u);
    CHECK_EQ_U64(start.week, 11u);
    gc_ticker_t *const ticker = gc_ticker(t, game);

    /* 8 clock days of plain ticks from a new world, 192 000 of them, timed. */
    const int64_t started_ns = time_now_ns();
    REQUIRE(gc_ticks(ticker, 8u * GC_TICKS_PER_DAY));
    const float64_t seconds = time_ns_to_seconds(time_now_ns() - started_ns);
    printf("  %" PRIu32 " ticks in %.1f s\n", 8u * GC_TICKS_PER_DAY, (libc_double_t)seconds);
    CHECK_EQ_U64(game->tick_count, 8u * GC_TICKS_PER_DAY);
    CHECK_EQ_U64(game->clock_days, 8u);
    REQUIRE(rig.record->count == 1u);
    CHECK_EQ_U64(rig.record->weeks[0], 12u);
    CHECK_EQ_U64(rig.record->fast[0], 0u);
    /* At 06:00 on clock day 5, to within one tick, and not before it. */
    CHECK(rig.record->clocks[0] >= GC_FIRST_CENSUS);
    CHECK(rig.record->clocks[0] - GC_FIRST_CENSUS <= GC_TICK_UNITS);
    CHECK_EQ_U64(game->census_runs, 1u);
    CHECK_EQ_U64(game->last_census_week, 12u);
    /* With no screens, nothing waits and nothing stopped. */
    uint64_t week = 0u;
    CHECK(!game_census_take(game, &week));
    CHECK_EQ_U64(atomic_load(&game->paused), 0u);
    game_close(game);
    gc_rig_end(t, &rig);
}

TEST(with_screens_on_a_census_stops_the_world_and_continue_starts_it) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    game_census_screens_set(game, true);
    const game_config_t config = gc_config(0u);
    REQUIRE(game_open(game, &config));
    gc_ticker_t *const ticker = gc_ticker(t, game);
    /* 5 s before 06:00 on clock day 5: 100 ticks. */
    game_clock_set(game, 5u, 0.25f64 - 5.0f64 / GAME_DAY_SECONDS);
    const uint64_t ticks_before = game->tick_count;
    REQUIRE(gc_ticks(ticker, 200u));
    const uint64_t ran = game->tick_count - ticks_before;
    printf("  %" PRIu64 " ticks ran before the census\n", ran);
    CHECK(ran >= 99u && ran <= 100u);
    CHECK_EQ_U64(atomic_load(&game->paused), 1u);
    REQUIRE(rig.record->count == 1u);
    CHECK_EQ_U64(rig.record->weeks[0], 12u);
    uint64_t week = 0u;
    REQUIRE(game_census_take(game, &week));
    CHECK_EQ_U64(week, 12u);
    CHECK(!game_census_take(game, &week));
    /* However many more ticks come, none runs, and the clock stands. */
    const calendar_clock_t stood = gc_clock(game);
    REQUIRE(gc_ticks(ticker, 500u));
    CHECK_EQ_U64(game->tick_count - ticks_before, ran);
    CHECK_EQ_U64(gc_clock(game), stood);
    /* The date it stopped on is the one the census screen says. */
    const calendar_date_t date = game_calendar_date(game);
    CHECK_EQ_U64(date.day_of_year + 1u, 85u);
    CHECK_EQ_U64(date.hour, 6u);
    CHECK_EQ_U64(date.minute, 0u);
    /* Continue: ticks again, and no second census. */
    game_pause_set(game, false);
    REQUIRE(gc_ticks(ticker, 300u));
    CHECK_EQ_U64(game->tick_count - ticks_before, ran + 300u);
    CHECK_EQ_U64(rig.record->count, 1u);
    CHECK(!game_census_take(game, &week));
    CHECK_EQ_U64(atomic_load(&game->paused), 0u);
    game_close(game);
    gc_rig_end(t, &rig);
}

TEST(with_the_day_cycle_off_no_census_ever_fires) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    const game_config_t config = gc_config(SHADE_FEATURE_DAY_CYCLE);
    REQUIRE(game_open(game, &config));
    gc_ticker_t *const ticker = gc_ticker(t, game);
    /* 5000 ticks before where 06:00 on clock day 5 would be, and 10 000 ticks: across it, if the clock moved. */
    game_clock_set(game, 5u, 0.25f64 - 5000.0f64 / (float64_t)GC_TICKS_PER_DAY);
    const calendar_clock_t set = gc_clock(game);
    REQUIRE(gc_ticks(ticker, 10000u));
    CHECK_EQ_U64(rig.record->count, 0u);
    CHECK_EQ_U64(game->census_runs, 0u);
    CHECK_EQ_U64(game->last_census_week, 11u);
    CHECK_EQ_U64(gc_clock(game), set);
    CHECK_EQ_U64(game->tick_count, 10000u);
    game_close(game);
    gc_rig_end(t, &rig);
}

/* ---- Sleeping through a night ------------------------------------------------------------------------------------------------ */

static void gc_key(gc_ticker_t *ticker, uint32_t code, bool down) {
    const uint64_t bit = 1ull << (code % 64u);
    ticker->state.down[code / 64u] =
        down ? ticker->state.down[code / 64u] | bit : ticker->state.down[code / 64u] & ~bit;
}

TEST(sleeping_through_a_night_fires_the_census_once_on_waking) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    /* The clock moves only by sleeping here, so waking can be seen to land on 06:00 exactly. */
    const game_config_t config = {.render_distance = 4u,
                                  .width = GC_WIDTH,
                                  .height = GC_HEIGHT,
                                  .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_TAA,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    gc_ticker_t *const ticker = gc_ticker(t, game);
    player_survival_t *const survival = &rig.player->survival;
    inventory_t *const inventory = &rig.item->inventory;
    /* A floor of its own with the sky over it, as tests/run/player/survival/ makes one, and a bed put down from the hotbar. */
    const int32_t pad_x = (int32_t)floor(rig.player->movement.position[0]);
    const int32_t pad_y = (int32_t)floor(rig.player->movement.position[1]);
    const int32_t pad_z = (int32_t)floor(rig.player->movement.position[2]);
    for (int32_t x = pad_x - 6; x <= pad_x + 6; x++) {
        for (int32_t y = pad_y - 6; y <= pad_y + 6; y++) {
            for (int32_t z = pad_z - 1; z <= pad_z + 8; z++) {
                const world_edit_status_t status =
                    world_edit_block_set(&rig.world->edit, x, y, z, z < pad_z ? BLOCK_ID_STONE : BLOCK_ID_AIR);
                REQUIRE(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
        }
    }
    REQUIRE(gc_ticks(ticker, 10u));
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_BED, 1u);
    inventory->added_count += 1u;
    game->camera.pitch = -0.8f32;
    REQUIRE(gc_ticks(ticker, 3u));
    REQUIRE(rig.player->target.hit.result == RAYCAST_RESULT_HIT);
    gc_key(ticker, BTN_RIGHT, true);
    REQUIRE(gc_ticks(ticker, 1u));
    gc_key(ticker, BTN_RIGHT, false);
    REQUIRE(gc_ticks(ticker, 3u));
    REQUIRE(rig.player->target.hit.block == BLOCK_ID_BED);

    /* 23:00 on clock day 4, and into bed. */
    game_clock_set(game, 4u, 23.0f64 / 24.0f64);
    REQUIRE(gc_ticks(ticker, 1u));
    CHECK_EQ_U64(rig.record->count, 0u);
    gc_key(ticker, BTN_RIGHT, true);
    REQUIRE(gc_ticks(ticker, 1u));
    gc_key(ticker, BTN_RIGHT, false);
    REQUIRE(survival->state == PLAYER_SURVIVAL_SLEEPING);
    REQUIRE(gc_ticks(ticker, PLAYER_SURVIVAL_SLEEP_TICKS + 2u));
    CHECK_EQ_U64(survival->slept_count, 1u);
    /* Woken: 06:00 on clock day 5 exactly, and the census of week 12 once, on the tick after the morning came. */
    CHECK_EQ_U64(gc_clock(game), GC_FIRST_CENSUS);
    CHECK_EQ_U64(game->clock_days, 5u);
    REQUIRE(rig.record->count == 1u);
    CHECK_EQ_U64(rig.record->weeks[0], 12u);
    CHECK_EQ_U64(rig.record->clocks[0], GC_FIRST_CENSUS);
    REQUIRE(gc_ticks(ticker, 100u));
    CHECK_EQ_U64(rig.record->count, 1u);
    CHECK_EQ_U64(game->last_census_week, 12u);
    game_close(game);
    gc_rig_end(t, &rig);
}

/* ---- A jump of 100 days, on the real threads ------------------------------------------------------------------------------- */

TEST(a_100_day_jump_runs_each_missed_week_once_in_order) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    game_census_screens_set(game, true);
    const game_config_t config = gc_config(0u);
    REQUIRE(game_open(game, &config));
    const uint64_t bytes = post_xrgb_bytes(GC_WIDTH, GC_HEIGHT);
    display_buffer_t target = {.owned_map = gc_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = GC_WIDTH * 4u,
                               .width = GC_WIDTH,
                               .height = GC_HEIGHT};
    REQUIRE(game_loop_start(game, nullptr));
    const render_fly_input_t still = {0};
    /* 120 000 s is 100 days of 1200 s: as tests/cuda/sky tells the loop 120 s. */
    CHECK(game_loop_frame(game, &still, 120000.0f32, &target));
    uint64_t week = 0u;
    bool taken = false;
    const int64_t started_ns = time_now_ns();
    while (!taken && time_now_ns() - started_ns < 20 * TIME_NS_PER_SECOND) {
        CHECK(game_loop_frame(game, &still, 0.0f32, &target));
        taken = game_census_take(game, &week);
        time_sleep_ns(2000000);
    }
    /* More frames: nothing more comes, and the world stays stopped. */
    for (uint32_t i = 0u; i < 50u; i++) {
        CHECK(game_loop_frame(game, &still, 0.05f32, &target));
        uint64_t again = 0u;
        CHECK(!game_census_take(game, &again));
        time_sleep_ns(2000000);
    }
    CHECK(game_loop_stop(game));
    REQUIRE(taken);
    CHECK_EQ_U64(week, 25u);
    CHECK_EQ_U64(game->last_census_week, 25u);
    CHECK_EQ_U64(game->clock_days, 100u);
    REQUIRE(rig.record->count == 14u);
    for (uint32_t i = 0u; i < 14u; i++) {
        CHECK_EQ_U64(rig.record->weeks[i], 12u + i);
        CHECK_EQ_U64(rig.record->fast[i], 0u);
    }
    CHECK_EQ_U64(game->census_runs, 14u);
    CHECK_EQ_U64(atomic_load(&game->paused), 1u);
    game_close(game);
    gc_rig_end(t, &rig);
}

/* ---- Saves ----------------------------------------------------------------------------------------------------------------- */

TEST(a_save_and_load_mid_week_never_fires_the_same_week_twice) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    game_save_set(game, rig.save);
    const game_config_t config = gc_config(0u);
    REQUIRE(save_open(rig.save, rig.root, GC_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    gc_ticker_t *ticker = gc_ticker(t, game);
    game_clock_set(game, 5u, 0.25f64 - 1.0f64 / GAME_DAY_SECONDS);
    REQUIRE(gc_ticks(ticker, 40u));
    REQUIRE(rig.record->count == 1u);
    CHECK_EQ_U64(rig.record->weeks[0], 12u);
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(rig.save);

    REQUIRE(save_open(rig.save, rig.root, GC_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(game->last_census_week, 12u);
    CHECK_EQ_U64(game->clock_days, 5u);
    CHECK_EQ_U64(game->day_zero, CALENDAR_DAY_ZERO);
    CHECK_EQ_U64(game_calendar_date(game).day_of_year + 1u, 85u);
    ticker = gc_ticker(t, game);
    REQUIRE(gc_ticks(ticker, 200u));
    CHECK_EQ_U64(rig.record->count, 1u);
    /* To clock day 7 at noon, still week 12: nothing. */
    game_clock_set(game, 7u, 0.5f64);
    REQUIRE(gc_ticks(ticker, 200u));
    CHECK_EQ_U64(rig.record->count, 1u);
    /* Across 06:00 on clock day 12, Day 92: week 13, once. */
    game_clock_set(game, 12u, 0.25f64 - 2.0f64 / GAME_DAY_SECONDS);
    REQUIRE(gc_ticks(ticker, 200u));
    REQUIRE(rig.record->count == 2u);
    CHECK_EQ_U64(rig.record->weeks[1], 13u);
    CHECK_EQ_U64(game_calendar_date(game).day_of_year + 1u, 92u);
    game_close(game);
    save_close(rig.save);
    gc_rig_end(t, &rig);
}

TEST(a_save_taken_with_the_census_screen_up_does_not_fire_it_again) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    game_save_set(game, rig.save);
    game_census_screens_set(game, true);
    const game_config_t config = gc_config(0u);
    REQUIRE(save_open(rig.save, rig.root, GC_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    gc_ticker_t *ticker = gc_ticker(t, game);
    game_clock_set(game, 5u, 0.25f64 - 1.0f64 / GAME_DAY_SECONDS);
    REQUIRE(gc_ticks(ticker, 40u));
    REQUIRE(rig.record->count == 1u);
    uint64_t week = 0u;
    REQUIRE(game_census_take(game, &week));
    REQUIRE(atomic_load(&game->paused) == 1u);
    /* The screen is up and nobody has answered it: the save is taken now, and the program goes. */
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(rig.save);

    REQUIRE(save_open(rig.save, rig.root, GC_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(game->last_census_week, 12u);
    CHECK_EQ_U64(atomic_load(&game->paused), 0u);
    ticker = gc_ticker(t, game);
    REQUIRE(gc_ticks(ticker, 100u));
    CHECK_EQ_U64(rig.record->count, 1u);
    CHECK(!game_census_take(game, &week));
    CHECK_EQ_U64(atomic_load(&game->paused), 0u);
    game_close(game);
    save_close(rig.save);
    gc_rig_end(t, &rig);
}

/* ---- The date line in the real game ---------------------------------------------------------------------------------------- */

static bool gc_draw(gc_ticker_t *ticker, display_buffer_t *target) {
    const display_headless_tick_t tick = {.events = nullptr, .state = &ticker->state, .event_count = 0u, .tick = 0u};
    return ticker->scene.draw(ticker->scene.borrowed_context, &tick, target);
}

/* The box of what the screen has over the picture the card drew under it, in rows 0 to rows - 1, or right -1 if nothing. */
typedef struct gc_box_t {
    int32_t left;   /* 0 */
    int32_t top;    /* 4 */
    int32_t right;  /* 8 */
    int32_t bottom; /* 12 */
} gc_box_t;
static_assert(sizeof(gc_box_t) == 16);

static gc_box_t gc_over_picture(const display_buffer_t *target, const uint8_t *picture, uint32_t rows) {
    gc_box_t box = {INT32_MAX, INT32_MAX, -1, -1};
    const uint32_t *const shown = (const uint32_t *)(const void *)target->owned_map;
    const uint32_t *const drawn = (const uint32_t *)(const void *)picture;
    for (int32_t y = 0; y < (int32_t)rows; y++) {
        for (int32_t x = 0; x < (int32_t)target->width; x++) {
            const uint64_t at = (uint64_t)y * target->width + (uint64_t)x;
            if ((shown[at] & 0xFFFFFFu) != (drawn[at] & 0xFFFFFFu)) {
                box.left = x < box.left ? x : box.left;
                box.right = x > box.right ? x : box.right;
                box.top = y < box.top ? y : box.top;
                box.bottom = y > box.bottom ? y : box.bottom;
            }
        }
    }
    return box;
}

TEST(the_date_line_follows_the_setting_in_the_real_game) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    game_ui_set(game, rig.ui);
    enum : uint32_t { WIDTH = 640u, HEIGHT = 360u };
    const game_config_t config = {.render_distance = 3u,
                                  .width = WIDTH,
                                  .height = HEIGHT,
                                  .features_off = SHADE_FEATURE_TAA | SHADE_FEATURE_DAY_CYCLE,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    gc_ticker_t *const ticker = gc_ticker(t, game);
    REQUIRE(gc_ticks(ticker, 2u));
    char line[CALENDAR_LINE_BYTES];
    game_calendar_line(game, line, sizeof(line));
    CHECK_STR_EQ(line, game_calendar_south(game) ? "Year 1, Day 80, Early autumn" : "Year 1, Day 80, Early spring");
    printf("  the line: %s\n", line);
    const uint64_t bytes = post_xrgb_bytes(WIDTH, HEIGHT);
    display_buffer_t target = {
        .owned_map = gc_aligned(t, bytes), .map_bytes = bytes, .pitch = WIDTH * 4u, .width = WIDTH, .height = HEIGHT};
    /* Each frame against the card's own picture under it, as two frames are not the same picture to the byte. The top quarter of
     * the screen: the hotbar, hearts and crosshair are below it. */
    const uint32_t rows = HEIGHT / 4u;
    REQUIRE(gc_draw(ticker, &target));
    REQUIRE(game->borrowed_last_picture != nullptr);
    const gc_box_t off = gc_over_picture(&target, game->borrowed_last_picture, rows);
    game->config.show_date = 1u;
    REQUIRE(gc_draw(ticker, &target));
    const gc_box_t on = gc_over_picture(&target, game->borrowed_last_picture, rows);
    game->config.show_date = 0u;
    REQUIRE(gc_draw(ticker, &target));
    const gc_box_t off_again = gc_over_picture(&target, game->borrowed_last_picture, rows);
    printf("  with the line on, x %" PRId32 " to %" PRId32 ", y %" PRId32 " to %" PRId32 " is over the picture\n",
           on.left, on.right, on.top, on.bottom);
    /* Off, the top of the screen is the picture itself, byte for byte. */
    CHECK_EQ_I64(off.right, -1);
    CHECK_EQ_I64(off_again.right, -1);
    /* On, only the top right corner: the right edge less its margin, near the top, and none of the left half. */
    REQUIRE(on.right >= 0);
    CHECK(on.left > (int32_t)WIDTH / 2);
    CHECK(on.right >= (int32_t)WIDTH - 8);
    CHECK(on.top <= 8);
    CHECK(on.bottom < (int32_t)rows - 1);
    game_close(game);
    gc_rig_end(t, &rig);
}

/* ---- The session ----------------------------------------------------------------------------------------------------------- */

/* fixtures/week.script through the session's headless scene, as --menu --headless runs it, ticks then draws. Once the world is open
 * the clock is put 40 ticks before 06:00 on clock day 5, as a console could, since five days of ticks would take minutes. */
TEST(the_session_opens_the_census_screen_and_continue_resumes) {
    gc_rig_t rig;
    REQUIRE(gc_rig_begin(t, &rig, true));
    game_t *const game = rig.game;
    game_ui_set(game, rig.ui);
    char *const saves = test_alloc(t, GC_PATH_BYTES);
    snprintf(saves, GC_PATH_BYTES, "%s/saves", rig.root);
    const game_config_t config = {.render_distance = 3u,
                                  .width = GC_SESSION_WIDTH,
                                  .height = GC_SESSION_HEIGHT,
                                  .features_off = SHADE_FEATURE_TAA,
                                  .show_date = 1u};
    game_session_t *const session = test_alloc(t, sizeof(game_session_t));
    game_session_init(session, game, rig.ui, rig.terrain, nullptr, &config, saves, GC_SEED);
    display_headless_scene_t scene;
    game_session_headless_scene(session, &scene);

    const char *const path = "tests/run/game/census/fixtures/week.script";
    char *const text = test_alloc(t, GC_SCRIPT_BYTES);
    uint64_t length = 0u;
    REQUIRE(file_read_whole(path, text, GC_SCRIPT_BYTES, &length) == FILE_OK);
    display_script_event_t *const events = test_alloc(t, GC_EVENTS * sizeof(display_script_event_t));
    display_script_t script;
    display_script_error_t error;
    const bool parsed = display_script_parse(text, length, MC_GAME_TICK_HZ, events, GC_EVENTS, &script, &error);
    if (!parsed) {
        printf("  %s:%" PRIu32 ": %s\n", path, error.line, error.message);
    }
    REQUIRE(parsed);
    const uint64_t bytes = post_xrgb_bytes(GC_SESSION_WIDTH, GC_SESSION_HEIGHT);
    display_buffer_t frame = {.owned_map = gc_aligned(t, bytes),
                              .map_bytes = bytes,
                              .pitch = GC_SESSION_WIDTH * 4u,
                              .width = GC_SESSION_WIDTH,
                              .height = GC_SESSION_HEIGHT};
    uint8_t *const before = test_alloc(t, bytes);
    uint8_t *const census = test_alloc(t, bytes);
    uint8_t *const after = test_alloc(t, bytes);
    uint32_t shots = 0u;
    bool jumped = false;
    uint64_t ticks_at_census = 0u;
    uint32_t census_tick = 0u;
    display_script_state_t *const state = test_alloc(t, sizeof(display_script_state_t));
    memset(state, 0, sizeof(*state));
    uint32_t next = 0u;
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
        REQUIRE(scene.tick(scene.borrowed_context, &info));
        REQUIRE(scene.draw(scene.borrowed_context, &info, &frame));
        if (!jumped && session->state == GAME_SESSION_PLAYING) {
            game_clock_set(game, 5u, 0.25f64 - 40.0f64 / (float64_t)GC_TICKS_PER_DAY);
            jumped = true;
        }
        if (census_tick == 0u && session->state == GAME_SESSION_CENSUS) {
            census_tick = tick;
            ticks_at_census = game->tick_count;
        }
        for (uint32_t e = first; e < next; e++) {
            if (events[e].kind != DISPLAY_SCRIPT_SCREENSHOT) {
                continue;
            }
            char shot[DISPLAY_SCRIPT_NAME_BYTES];
            display_script_screenshot_name(&script, &events[e], shot);
            uint8_t *const into = strcmp(shot, "before.png") == 0   ? before
                                  : strcmp(shot, "census.png") == 0 ? census
                                  : strcmp(shot, "after.png") == 0  ? after
                                                                    : nullptr;
            if (CHECK(into != nullptr)) {
                memcpy(into, frame.owned_map, bytes);
                shots++;
            }
            if (strcmp(shot, "census.png") == 0) {
                /* The world has stopped, and the screen is up. */
                CHECK_EQ_U64(session->state, GAME_SESSION_CENSUS);
                CHECK_EQ_U64(atomic_load(&game->paused), 1u);
                CHECK_EQ_U64(game->tick_count, ticks_at_census);
            }
        }
    }
    CHECK_EQ_U64(shots, 3u);
    CHECK(jumped);
    printf("  the census came on tick %" PRIu32 "\n", census_tick);
    CHECK(census_tick != 0u);
    CHECK_EQ_U64(rig.record->count, 1u);
    CHECK_EQ_U64(rig.record->weeks[0], 12u);
    /* Continue: playing, with ticks running again and no screen over the world. */
    CHECK_EQ_U64(session->state, GAME_SESSION_PLAYING);
    CHECK_EQ_U64(atomic_load(&game->paused), 0u);
    CHECK(game->tick_count > ticks_at_census + 10u);
    uint64_t different_census = 0u;
    uint64_t different_after = 0u;
    for (uint64_t i = 0u; i < bytes; i++) {
        different_census += census[i] != before[i] ? 1u : 0u;
        different_after += after[i] > census[i] + 24 || census[i] > after[i] + 24 ? 1u : 0u;
    }
    printf("  the census screen changed %" PRIu64 " bytes of %" PRIu64 ", and Continue %" PRIu64 "\n", different_census,
           bytes, different_after);
    /* The screen shades the whole picture, and leaving it takes the shade off again. */
    CHECK(different_census > bytes / 2u);
    CHECK(different_after > bytes / 4u);
    if (getenv("MC_CENSUS_PNG_DIR") != nullptr) {
        /* For a person to look at: the three pictures, as PNG. */
        const char *const names[3] = {"before.png", "census.png", "after.png"};
        const uint8_t *const pictures[3] = {before, census, after};
        for (uint32_t i = 0u; i < 3u; i++) {
            const uint64_t capacity = display_png_bytes_needed(GC_SESSION_WIDTH, GC_SESSION_HEIGHT);
            uint8_t *const png = test_alloc(t, capacity);
            uint64_t written = 0u;
            REQUIRE(display_png_encode(pictures[i], GC_SESSION_WIDTH * 4u, GC_SESSION_WIDTH, GC_SESSION_HEIGHT, png,
                                       capacity, &written));
            char kept[GC_PATH_BYTES];
            snprintf(kept, sizeof(kept), "%s/%s", getenv("MC_CENSUS_PNG_DIR"), names[i]);
            REQUIRE(file_write_atomic(kept, png, written) == FILE_OK);
        }
    }
    CHECK(game_session_end(session));
    game_session_deinit(session);
    gc_rig_end(t, &rig);
}

/* ---- Plan 004 phase 07: the census's weather, with no game and no card ------------------------------------------------- */

/* The phase 07 file's worked grid, north up: rows 0 to 2 are y 2 to 0. */
static void gw_grid(climate_flow_cell_t cells[9], float32_t runoff) {
    static const float32_t HEIGHTS[3][3] = {
        {50.0f32, 48.0f32, 47.0f32}, {45.0f32, 40.0f32, 38.0f32}, {44.0f32, 36.0f32, 30.0f32}};
    memset(cells, 0, 9u * sizeof(climate_flow_cell_t));
    for (uint32_t row = 0u; row < 3u; row++) {
        for (uint32_t col = 0u; col < 3u; col++) {
            climate_flow_cell_t *const cell = &cells[row * 3u + col];
            cell->cell_x = (int32_t)col;
            cell->cell_y = 2 - (int32_t)row;
            cell->height = HEIGHTS[row][col];
            cell->runoff = runoff;
            cell->mean = runoff;
        }
    }
}

static uint32_t gw_grid_find(const void *borrowed_context, int32_t cell_x, int32_t cell_y) {
    (void)borrowed_context;
    if (cell_x < 0 || cell_x > 2 || cell_y < 0 || cell_y > 2) {
        return CLIMATE_FLOW_NONE;
    }
    return (uint32_t)(2 - cell_y) * 3u + (uint32_t)cell_x;
}

TEST(d8_matches_the_worked_grid) {
    climate_flow_cell_t cells[9];
    gw_grid(cells, 10.0f32);
    climate_flow_receivers(cells, 9u, gw_grid_find, nullptr);
    /* Receivers by row and column, as the table says: (1,1), (1,2), (1,2) / (2,1), (2,2), (2,2) / (2,1), (2,2), none. */
    static const uint32_t RECEIVERS[9] = {4u, 5u, 5u, 7u, 8u, 8u, 7u, 8u, CLIMATE_FLOW_NONE};
    static const float64_t TOTALS[9] = {10.0f64, 10.0f64, 10.0f64, 10.0f64, 20.0f64,
                                        30.0f64, 10.0f64, 30.0f64, 90.0f64};
    uint32_t index[9], scratch[9];
    climate_flow_accumulate(cells, 9u, index, scratch);
    for (uint32_t i = 0u; i < 9u; i++) {
        printf("  cell (%u, %u): height %.0f, receiver %d, total %.0f\n", i / 3u, i % 3u,
               (libc_double_t)cells[i].height, cells[i].receiver == CLIMATE_FLOW_NONE ? -1 : (int32_t)cells[i].receiver,
               (libc_double_t)cells[i].q_week);
        CHECK_EQ_U64(cells[i].receiver, RECEIVERS[i]);
        CHECK_NEAR(cells[i].q_week, TOTALS[i], 1.0e-9f64);
    }
    /* A sea cell keeps its water whatever is below it. */
    gw_grid(cells, 10.0f32);
    cells[4].sea = 1u;
    climate_flow_receivers(cells, 9u, gw_grid_find, nullptr);
    CHECK_EQ_U64(cells[4].receiver, CLIMATE_FLOW_NONE);
    CHECK_EQ_U64(cells[0].receiver, 4u);
}

TEST(flood_index_by_hand) {
    const float32_t usual = climate_flow_flood(90.0f64, 40.0f64);
    const float32_t hurricane = climate_flow_flood(1305.0f64, 40.0f64);
    const float32_t drizzle = climate_flow_flood(10.0f64, 0.0f64);
    printf("Qbar 40, Q 90: %.4f; Q 1305: %.4f; Qbar 0, Q 10: %.4f\n", (libc_double_t)usual, (libc_double_t)hurricane,
           (libc_double_t)drizzle);
    CHECK_NEAR(usual, 0.75f32, 1.0e-6f32);
    CHECK_NEAR(hurricane, 10.875f32, 1.0e-5f32);
    CHECK_NEAR(drizzle, 0.5f32, 1.0e-6f32);
}

/* One tile of land by the coast marked as generated, so 25 kept tiles with the margin; weeks 12 to 12 + weeks - 1. */
static uint64_t gw_run_weeks(test_context_t *t, uint32_t workers, uint32_t weeks, const char *directory,
                             uint64_t *out_file_digest) {
    gw_rig_t rig;
    if (!gw_start(t, &rig, workers)) {
        return 0u;
    }
    gw_open(&rig, directory, 11u);
    gw_mark_tiles(&rig, -2, 6, -2, 6);
    gw_weeks(&rig, 12u, 11u + weeks, 0u);
    const uint64_t digest = gw_digest(t, rig.climate);
    if (out_file_digest != nullptr && directory != nullptr) {
        char path[FILE_PATH_BYTES];
        uint64_t bytes = 0u;
        (void)save_climate_path(directory, 11u + weeks, path);
        const uint8_t *const file = gw_file(t, path, &bytes);
        *out_file_digest = file != nullptr ? save_climate_hash(0xCBF29CE484222325ull, file, bytes) : 0u;
    }
    gw_stop(&rig);
    return digest;
}

TEST(one_and_sixteen_workers_give_the_same_bytes) {
    char *const one = gw_directory(t);
    char *const sixteen = gw_directory(t);
    REQUIRE(one != nullptr && sixteen != nullptr);
    uint64_t file_one = 0u, file_sixteen = 0u;
    const int64_t started = time_now_ns();
    const uint64_t a = gw_run_weeks(t, 1u, 3u, one, &file_one);
    const int64_t middle = time_now_ns();
    const uint64_t b = gw_run_weeks(t, MC_THREADS_WORKERS, 3u, sixteen, &file_sixteen);
    printf("3 weeks over 25 tiles: 1 worker %.0f ms, %u workers %.0f ms; store %016" PRIx64 " and %016" PRIx64
           ", file %016" PRIx64 " and %016" PRIx64 "\n",
           (libc_double_t)time_ns_to_ms(middle - started), (uint32_t)MC_THREADS_WORKERS,
           (libc_double_t)time_ns_to_ms(time_now_ns() - middle), a, b, file_one, file_sixteen);
    CHECK(a != 0u && file_one != 0u);
    CHECK(a == b);
    CHECK(file_one == file_sixteen);
    gw_remove(t, one);
    gw_remove(t, sixteen);
}

TEST(catch_up_runs_each_week_in_order) {
    /* --climate-jump 100 from a new world owes weeks 12 to 25: once all together, and once a census at a time. */
    gw_rig_t rig;
    REQUIRE(gw_start(t, &rig, MC_THREADS_WORKERS));
    gw_open(&rig, nullptr, 11u);
    gw_mark_tiles(&rig, -2, 6, -2, 6);
    for (uint64_t w = 12u; w <= 25u; w++) {
        game_census_week(rig.census, w, false);
    }
    CHECK_EQ_U64(game_census_report(rig.census)->weeks_total, 14u);
    gw_settle(&rig, -1500.0f64, (float64_t)GW_Y);
    const ui_census_report_t *const report = game_census_report(rig.census);
    const uint64_t together = gw_digest(t, rig.climate);
    const climate_week_store_t *const store = climate_week_store(rig.climate);
    uint32_t at_25 = 0u;
    for (uint32_t i = 0u; i < store->tile_count; i++) {
        at_25 += store->borrowed_tiles[i].week == 25u ? 1u : 0u;
    }
    printf("14 weeks at once: %u of %u weeks done, %u of %u tiles at week 25, written %u\n", report->weeks_done,
           report->weeks_total, at_25, store->tile_count, (uint32_t)report->written);
    CHECK_EQ_U64(report->weeks_done, 14u);
    CHECK_EQ_U64(report->written, 1u);
    CHECK_EQ_U64(at_25, store->tile_count);
    CHECK_EQ_U64(store->week, 25u);
    gw_stop(&rig);

    REQUIRE(gw_start(t, &rig, MC_THREADS_WORKERS));
    gw_open(&rig, nullptr, 11u);
    gw_mark_tiles(&rig, -2, 6, -2, 6);
    for (uint64_t w = 12u; w <= 25u; w++) {
        gw_weeks(&rig, w, w, 0u);
        CHECK_EQ_U64(climate_week_store(rig.climate)->week, w);
    }
    const uint64_t one_by_one = gw_digest(t, rig.climate);
    printf("together %016" PRIx64 ", one census at a time %016" PRIx64 "\n", together, one_by_one);
    CHECK(together == one_by_one);
    gw_stop(&rig);
}

TEST(fast_mode_past_52_weeks) {
    /* A jump of 60 weeks: the oldest 8 fast, then 52 full, against 60 full. */
    gw_rig_t fast, full;
    REQUIRE(gw_start(t, &fast, MC_THREADS_WORKERS));
    gw_open(&fast, nullptr, 11u);
    gw_mark_tiles(&fast, -2, 6, -2, 6);
    const int64_t started = time_now_ns();
    gw_weeks(&fast, 12u, 71u, 19u);
    const int64_t fast_ns = time_now_ns() - started;
    REQUIRE(gw_start(t, &full, MC_THREADS_WORKERS));
    gw_open(&full, nullptr, 11u);
    gw_mark_tiles(&full, -2, 6, -2, 6);
    gw_weeks(&full, 12u, 71u, 0u);
    const climate_week_store_t *const a = climate_week_store(fast.climate);
    const climate_week_store_t *const b = climate_week_store(full.climate);
    REQUIRE(a->tile_count == b->tile_count);
    uint32_t cells = 0u, close = 0u, kinds_same = 0u, floods = 0u, days = 0u;
    for (uint32_t i = 0u; i < a->tile_count; i++) {
        const climate_week_tile_t *const ta = &a->borrowed_tiles[i];
        uint32_t j = 0u;
        const climate_week_tile_t *const tb = climate_week_store_find(b, ta->tile_x, ta->tile_y, &j);
        REQUIRE(tb != nullptr);
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            const climate_week_t *const ca = &ta->cells[c], *const cb = &tb->cells[c];
            cells++;
            close += fabsf(ca->snow_mm - cb->snow_mm) < 5.0f32 && fabsf(ca->soil_mm - cb->soil_mm) < 5.0f32 ? 1u : 0u;
            kinds_same += memcmp(ca->day_kind, cb->day_kind, sizeof(ca->day_kind)) == 0 ? 1u : 0u;
            floods += ca->flood > 0.0f32 ? 1u : 0u;
            for (uint32_t d = 0u; d < CLIMATE_WEEK_DAYS; d++) {
                days += ca->day_kind[d] != CLIMATE_DAY_CLEAR ? 1u : 0u;
            }
        }
    }
    const float64_t share = (float64_t)close / (float64_t)cells;
    printf("8 fast then 52 full (%.0f ms) against 60 full: snow and soil within 5 mm in %.1f%% of %u cells; the last "
           "week's kinds the same in %u, %u cells with a flood index, %u days not clear\n",
           (libc_double_t)time_ns_to_ms(fast_ns), (libc_double_t)(100.0f64 * share), cells, kinds_same, floods, days);
    /* Fast mode is an approximation; 52 full weeks after it wash most of the difference out. */
    CHECK(share >= 0.95f64);
    CHECK(floods > 0u);
    CHECK(days > 0u);
    gw_stop(&fast);
    gw_stop(&full);
}

TEST(replaying_the_same_weeks_gives_the_same_bytes) {
    gw_rig_t a, b;
    REQUIRE(gw_start(t, &a, MC_THREADS_WORKERS));
    gw_open(&a, nullptr, 11u);
    gw_mark_tiles(&a, -2, 6, -2, 6);
    REQUIRE(gw_start(t, &b, 4u));
    gw_open(&b, nullptr, 11u);
    gw_mark_tiles(&b, -2, 6, -2, 6);
    uint32_t same = 0u;
    for (uint64_t w = 12u; w < 32u; w++) {
        gw_weeks(&a, w, w, 0u);
        gw_weeks(&b, w, w, 0u);
        same += gw_digest(t, a.climate) == gw_digest(t, b.climate) ? 1u : 0u;
    }
    printf("20 censuses replayed on 16 and on 4 workers: %u of 20 stores the same\n", same);
    CHECK_EQ_U64(same, 20u);
    gw_stop(&a);
    gw_stop(&b);
}

/* A hurricane's centre over kept land: the day, and where, within 10 years. */
static bool gw_landfall(gw_rig_t *rig, arena_t *arena, int64_t *out_day, float64_t *out_x, float64_t *out_y) {
    for (int64_t day = 100; day < 3650; day++) {
        arena_reset(arena);
        climate_systems_day_t list;
        if (!climate_systems_day_build(rig->climate, arena, -1500.0f64, (float64_t)GW_Y, 6000.0f32, 0.0f32, day,
                                       1u << CLIMATE_SYSTEM_HURRICANE, &list)) {
            return false;
        }
        for (uint32_t i = 0u; i < list.system_count; i++) {
            const climate_system_t *const system = &list.borrowed_systems[i];
            for (uint32_t k = 0u; k < system->sample_count; k++) {
                const climate_track_sample_t *const s = &system->samples[k];
                const float64_t t_k = system->spawn_day + 0.25f64 * (float64_t)k;
                if (t_k >= (float64_t)day && t_k < (float64_t)day + 1.0f64 && s->over_land != 0u && s->x > -2000.0f64 &&
                    s->x < -200.0f64 && fabs(s->y - (float64_t)GW_Y) < 3000.0f64) {
                    *out_day = day;
                    *out_x = s->x;
                    *out_y = s->y;
                    return true;
                }
            }
        }
    }
    return false;
}

TEST(a_hurricane_landfall_floods_downstream) {
    /* Issues found 8: hurricanes almost never form on real seeds (phase 06, Issues found 21), so a sloped coast at 15 N with
     * the hurricane season forced on all year, by phase 06's test hook. */
    gw_rig_t rig;
    REQUIRE(gw_start(t, &rig, MC_THREADS_WORKERS));
    const climate_systems_hooks_t hooks = {.hurricane_s = 1.0f32,
                                           .hurricane_current_c = 0.0f32,
                                           .hurricane_first = 0u,
                                           .hurricane_last = 364u,
                                           .big_off = 0u};
    climate_systems_hooks_set(rig.climate, &hooks);
    arena_t *const arena = gw_aligned(t, sizeof(arena_t));
    arena_init(arena, "test_landfall", gw_aligned(t, 8ull << 20), 8ull << 20);
    int64_t day = 0;
    float64_t x = 0.0f64, y = 0.0f64;
    REQUIRE(gw_landfall(&rig, arena, &day, &x, &y));
    const uint64_t week = (uint64_t)(day / 7) + 1u; /* the census whose 7 days hold the landfall */
    printf("a hurricane comes ashore on day %" PRId64 " at %.0f, %.0f: census week %" PRIu64 "\n", day,
           (libc_double_t)x, (libc_double_t)y, week);
    gw_open(&rig, nullptr, week - 2u);
    const int32_t tile_x = (int32_t)floor(x / 1024.0f64), tile_y = (int32_t)floor(y / 1024.0f64);
    gw_mark_tiles(&rig, tile_x - 1, tile_y - 1, tile_x + 1, tile_y + 1);
    gw_weeks(&rig, week - 1u, week - 1u, 0u);
    const climate_week_store_t *const store = climate_week_store(rig.climate);
    float32_t *const before = test_alloc(t, store->tile_count * CLIMATE_TILE_CELL_COUNT * sizeof(float32_t));
    for (uint32_t i = 0u; i < store->tile_count; i++) {
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            before[i * CLIMATE_TILE_CELL_COUNT + c] = store->borrowed_tiles[i].cells[c].flood;
        }
    }
    gw_weeks(&rig, week, week, 0u);
    /* The cell with the most flow within 3 km downstream of the landfall: east of it, toward the sea, on land. */
    uint32_t best = CLIMATE_FLOW_NONE;
    for (uint32_t k = 0u; k < store->tile_count * CLIMATE_TILE_CELL_COUNT; k++) {
        const climate_flow_cell_t *const cell = &store->borrowed_flow[k];
        const float64_t cx = (float64_t)cell->cell_x * 64.0f64 + 32.0f64,
                        cy = (float64_t)cell->cell_y * 64.0f64 + 32.0f64;
        const float64_t dx = cx - x, dy = cy - y;
        if (cell->sea != 0u || dx < 0.0f64 || dx * dx + dy * dy > 3000.0f64 * 3000.0f64) {
            continue;
        }
        if (best == CLIMATE_FLOW_NONE || cell->q_week > store->borrowed_flow[best].q_week) {
            best = k;
        }
    }
    REQUIRE(best != CLIMATE_FLOW_NONE);
    const climate_week_t *const cell =
        &store->borrowed_tiles[best / CLIMATE_TILE_CELL_COUNT].cells[best % CLIMATE_TILE_CELL_COUNT];
    const climate_flow_cell_t *const flow = &store->borrowed_flow[best];
    printf(
        "downstream cell %d, %d: flow %.0f cell-mm (long-run %.1f), rain %.0f mm, runoff %.0f mm, flood %.2f, the week "
        "before %.2f; day kinds %u %u %u %u %u %u %u\n",
        flow->cell_x, flow->cell_y, (libc_double_t)flow->q_week, (libc_double_t)flow->q_mean,
        (libc_double_t)cell->rain_mm, (libc_double_t)cell->runoff_mm, (libc_double_t)cell->flood,
        (libc_double_t)before[best], cell->day_kind[0], cell->day_kind[1], cell->day_kind[2], cell->day_kind[3],
        cell->day_kind[4], cell->day_kind[5], cell->day_kind[6]);
    CHECK(flow->q_week > (float64_t)cell->runoff_mm); /* water came from upstream */
    CHECK(cell->flood > 1.0f32);
    CHECK(before[best] < 1.0f32);
    gw_stop(&rig);
}

TEST(the_program_jumps_100_days_and_continues) {
    char *const root = gw_directory(t);
    REQUIRE(root != nullptr);
    char saves[GW_PATH_BYTES], shots[GW_PATH_BYTES];
    snprintf(saves, sizeof(saves), "%s/saves", root);
    snprintf(shots, sizeof(shots), "%s/shots", root);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--menu",
                          (char *)"--headless",
                          (char *)"--no-sound",
                          (char *)"--climate-jump",
                          (char *)"100",
                          (char *)"--render-distance",
                          (char *)"3",
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--saves",
                          saves,
                          (char *)"--script",
                          (char *)"tests/run/game/census/fixtures/jump.script",
                          (char *)"--screenshot-dir",
                          shots,
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, 900000u, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_CONTAINS(run.output, "--climate-jump moves this world's clock on 100 days");
    CHECK_CONTAINS(run.output, "calendar: census for week 25 (14 weeks, 0 of them fast)");
    CHECK_CONTAINS(run.output, "census: weeks 12 to 25 worked out");
    CHECK_CONTAINS(run.output, "the census screen for week 25 is up");
    char path[GW_PATH_BYTES + 16u];
    uint64_t bytes = 0u;
    snprintf(path, sizeof(path), "%s/after.png", shots);
    CHECK(gw_file(t, path, &bytes) != nullptr && bytes > 0u);
    if (t->failure_count != 0u) {
        fputs(run.output, stdout);
    }
    gw_remove(t, root);
}

TEST_MAIN()
