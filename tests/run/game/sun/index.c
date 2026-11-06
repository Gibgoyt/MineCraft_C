/* ctest run_game_sun. See README.md. */
#include "harness/index.h"

#include "calendar/index.h"
#include "climate/index.h"
#include "climate/sun/index.h"
#include "display/headless/index.h"
#include "entity/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "item/index.h"
#include "job/index.h"
#include "main/index.h"
#include "mob/spawn/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    GS_WIDTH = 160u,
    GS_HEIGHT = 90u,
    GS_PROGRAM_TIMEOUT_MS = 240000u,
};
static const uint64_t GS_SEED = 4242u;

/* The places of the phase 02 file's tests, by their y. */
#define GS_EQUATOR_Y 8.0f64
#define GS_60_NORTH_Y 25532.0f64
#define GS_70_NORTH_Y 30000.0f64
#define GS_80_NORTH_Y 34042.0f64
#define GS_45_SOUTH_Y (-19149.0f64)
/* Floors are put this high, over any land the seed has there, so that nothing stands between them and the sky. */
#define GS_FLOOR_Z 1000

/* Clock days for a day of the year as the HUD shows it, with a world's D0 of 79: from 06:00 on that day it is that day. */
static uint64_t gs_clock_days(uint32_t shown_day) {
    return ((uint64_t)shown_day - 1u + CALENDAR_DAYS_PER_YEAR - CALENDAR_DAY_ZERO) % CALENDAR_DAYS_PER_YEAR;
}

static void *gs_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* ---- A rig: a real game on the card --------------------------------------------------------------------------------------- */

typedef struct gs_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64: walking only, and the rest down to ui */
    player_t *player;            /* 72 */
    entity_t *entity;            /* 80 */
    item_t *item;                /* 88 */
    ui_t *ui;                    /* 96 */
    uint8_t _pad0[24];           /* 104 */
} gs_rig_t;
static_assert(sizeof(gs_rig_t) == 128);

static bool gs_rig_begin(test_context_t *t, gs_rig_t *rig, bool walking) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = gs_aligned(t, sizeof(gpu_bridge_t));
    rig->job = gs_aligned(t, sizeof(job_system_t));
    rig->world = gs_aligned(t, sizeof(world_t));
    rig->terrain = gs_aligned(t, sizeof(terrain_t));
    rig->render = gs_aligned(t, sizeof(render_t));
    rig->texture = gs_aligned(t, sizeof(texture_t));
    rig->game = gs_aligned(t, sizeof(game_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, GS_SEED)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    if (walking) {
        rig->physics = gs_aligned(t, sizeof(physics_t));
        rig->player = gs_aligned(t, sizeof(player_t));
        rig->entity = gs_aligned(t, sizeof(entity_t));
        rig->item = gs_aligned(t, sizeof(item_t));
        rig->ui = gs_aligned(t, sizeof(ui_t));
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

static void gs_rig_end(test_context_t *t, gs_rig_t *rig) {
    game_deinit(rig->game);
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
}

/* The clock stands still, so that only game_clock_set moves it. */
static game_config_t gs_config(void) {
    return (game_config_t){.render_distance = 3u,
                           .width = GS_WIDTH,
                           .height = GS_HEIGHT,
                           .features_off = SHADE_FEATURE_TAA | SHADE_FEATURE_DAY_CYCLE,
                           .show_date = 0u};
}

/* The headless scene's ticks: one tick of the clock and one of the sim each. */
typedef struct gs_ticker_t {
    display_headless_scene_t scene; /* 0 */
    display_script_state_t state;   /* 24 */
    uint32_t tick;                  /* 176 */
    uint8_t _pad0[4];               /* 180 */
} gs_ticker_t;
static_assert(sizeof(gs_ticker_t) == 184);

static gs_ticker_t *gs_ticker(test_context_t *t, game_t *game) {
    gs_ticker_t *const ticker = test_alloc(t, sizeof(gs_ticker_t));
    memset(ticker, 0, sizeof(*ticker));
    game_headless_scene(game, &ticker->scene);
    return ticker;
}

static bool gs_ticks(gs_ticker_t *ticker, uint32_t ticks) {
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

static void gs_key(gs_ticker_t *ticker, uint32_t code, bool down) {
    const uint64_t bit = 1ull << (code % 64u);
    ticker->state.down[code / 64u] =
        down ? ticker->state.down[code / 64u] | bit : ticker->state.down[code / 64u] & ~bit;
}

static bool gs_draw(gs_ticker_t *ticker, display_buffer_t *target) {
    const display_headless_tick_t tick = {.events = nullptr, .state = &ticker->state, .event_count = 0u, .tick = 0u};
    return ticker->scene.draw(ticker->scene.borrowed_context, &tick, target);
}

static display_buffer_t gs_target(test_context_t *t) {
    const uint64_t bytes = post_xrgb_bytes(GS_WIDTH, GS_HEIGHT);
    return (display_buffer_t){.owned_map = gs_aligned(t, bytes),
                              .map_bytes = bytes,
                              .pitch = GS_WIDTH * 4u,
                              .width = GS_WIDTH,
                              .height = GS_HEIGHT};
}

/* The mean of the three channels over the top half of a picture: with the eye looking up, all sky. */
static float64_t gs_sky_brightness(const display_buffer_t *target) {
    uint64_t sum = 0u;
    const uint8_t *const pixels = target->owned_map;
    for (uint32_t y = 0u; y < GS_HEIGHT / 2u; y++) {
        for (uint32_t x = 0u; x < GS_WIDTH; x++) {
            const uint32_t at = y * target->pitch + x * 4u;
            sum += (uint64_t)pixels[at] + pixels[at + 1u] + pixels[at + 2u];
        }
    }
    return (float64_t)sum / (float64_t)(3u * GS_WIDTH * (GS_HEIGHT / 2u));
}

/* The flying eye to a y, looking up, and a picture drawn from it. */
static bool gs_fly_draw(gs_ticker_t *ticker, game_t *game, float64_t y, display_buffer_t *target) {
    game->camera.position[1] = y;
    game->camera.pitch = 0.9f32;
    return gs_draw(ticker, target);
}

/* The body to a place, with a floor of stone under it and the sky over it, out to across blocks east, and the world there loaded:
 * as tests/run/player/survival/ makes a floor, but far from where the seed starts. */
static bool gs_body_to(test_context_t *t, gs_rig_t *rig, gs_ticker_t *ticker, float64_t x, float64_t y, int32_t east) {
    game_t *const game = rig->game;
    player_movement_t *const movement = &rig->player->movement;
    const float64_t z = (float64_t)GS_FLOOR_Z;
    movement->position[0] = x;
    movement->position[1] = y;
    movement->position[2] = z;
    for (uint32_t a = 0u; a < 3u; a++) {
        movement->velocity[a] = 0.0f64;
    }
    movement->fall_distance = 0.0f64;
    /* A body whose column is not loaded waits where it is: a few ticks bring the eye, and so the middle of the map, over to it,
     * and then the world there is loaded around it. */
    if (!CHECK(gs_ticks(ticker, 3u)) || !CHECK(game_settle(game))) {
        return false;
    }
    /* The headless ticks step the world too, so it may have loaded under the body and let it fall: back up, onto the floor. */
    movement->position[2] = z;
    for (uint32_t a = 0u; a < 3u; a++) {
        movement->velocity[a] = 0.0f64;
    }
    movement->fall_distance = 0.0f64;
    const int32_t pad_x = (int32_t)floor(x), pad_y = (int32_t)floor(y);
    for (int32_t bx = pad_x - 6; bx <= pad_x + east; bx++) {
        for (int32_t by = pad_y - 6; by <= pad_y + 6; by++) {
            for (int32_t bz = GS_FLOOR_Z - 1; bz <= GS_FLOOR_Z + 8; bz++) {
                const world_edit_status_t status = world_edit_block_set(
                    &rig->world->edit, bx, by, bz, bz < GS_FLOOR_Z ? BLOCK_ID_STONE : BLOCK_ID_AIR);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    /* Light worked out, and the body standing where it was put. */
    const bool ran = CHECK(game_settle(game)) && CHECK(gs_ticks(ticker, 10u));
    printf("  the body is at %.3f, %.3f, %.3f; the map's middle is chunk %" PRId32 ", %" PRId32 " with %" PRIu32
           " columns ready\n",
           (libc_double_t)movement->position[0], (libc_double_t)movement->position[1],
           (libc_double_t)movement->position[2], rig->world->map.center_x, rig->world->map.center_y,
           rig->world->map.ready_count);
    return ran && CHECK(fabs(movement->position[1] - y) < 1.0f64) && CHECK(fabs(movement->position[2] - z) < 0.01f64);
}

/* ---- The picture ----------------------------------------------------------------------------------------------------------- */

TEST(the_picture_takes_the_sun_at_the_eyes_latitude) {
    gs_rig_t rig;
    REQUIRE(gs_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    const game_config_t config = gs_config();
    REQUIRE(game_open(game, &config));
    gs_ticker_t *const ticker = gs_ticker(t, game);
    display_buffer_t target = gs_target(t);
    /* Midnight before day 171's morning: clock day 91, 00:00. */
    game_clock_set(game, gs_clock_days(171u), 0.0f64);
    REQUIRE(gs_fly_draw(ticker, game, GS_EQUATOR_Y, &target));
    const render_frame_t *const frame = &rig.render->frame;
    CHECK(frame->sun_from_outside != 0u);
    const float32_t equator[3] = {frame->sun_given[0], frame->sun_given[1], frame->sun_given[2]};
    REQUIRE(gs_fly_draw(ticker, game, GS_70_NORTH_Y, &target));
    const float32_t north[3] = {frame->sun_given[0], frame->sun_given[1], frame->sun_given[2]};
    printf("  midnight, day 171: the sun at y = 8 is (%.3f, %.3f, %.3f), at y = 30000 (%.3f, %.3f, %.3f)\n",
           (libc_double_t)equator[0], (libc_double_t)equator[1], (libc_double_t)equator[2], (libc_double_t)north[0],
           (libc_double_t)north[1], (libc_double_t)north[2]);
    CHECK_NEAR(equator[2], -0.917f32, 0.01f32);
    CHECK_NEAR(north[2], 0.070f32, 0.01f32);
    CHECK(north[1] > 0.0f32);
    /* Exactly what the climate says for that eye and that clock. */
    climate_sun_t sun;
    climate_sun_of_clock(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed), GS_70_NORTH_Y,
                         &sun);
    for (uint32_t a = 0u; a < 3u; a++) {
        CHECK_NEAR(north[a], (float32_t)sun.direction[a], 0.0f32);
    }
    game_close(game);
    gs_rig_end(t, &rig);
}

TEST(the_sky_is_lit_at_midnight_past_the_polar_circle_in_summer) {
    gs_rig_t rig;
    REQUIRE(gs_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    const game_config_t config = gs_config();
    REQUIRE(game_open(game, &config));
    gs_ticker_t *const ticker = gs_ticker(t, game);
    display_buffer_t target = gs_target(t);
    game_clock_set(game, gs_clock_days(171u), 0.0f64);
    REQUIRE(gs_fly_draw(ticker, game, GS_EQUATOR_Y, &target));
    const float64_t equator = gs_sky_brightness(&target);
    REQUIRE(gs_fly_draw(ticker, game, GS_70_NORTH_Y, &target));
    const float64_t north = gs_sky_brightness(&target);
    printf("  the midnight sky's mean brightness: %.2f at the equator, %.2f at 70.5 degrees north\n",
           (libc_double_t)equator, (libc_double_t)north);
    CHECK(north > equator * 3.0f64 + 10.0f64);
    game_close(game);
    gs_rig_end(t, &rig);
}

/* ---- Beds and mobs ----------------------------------------------------------------------------------------------------------- */

/* A walking world opened, a body put through it, and everything closed again whatever the body found. */
static void gs_walk(test_context_t *t, void (*body)(test_context_t *, gs_rig_t *, gs_ticker_t *)) {
    gs_rig_t rig;
    if (gs_rig_begin(t, &rig, true)) {
        const game_config_t config = gs_config();
        if (CHECK(game_open(rig.game, &config))) {
            if (CHECK(game_settle(rig.game))) {
                body(t, &rig, gs_ticker(t, rig.game));
            }
            game_close(rig.game);
        }
    }
    gs_rig_end(t, &rig);
}

static void gs_bed_at_60_north(test_context_t *t, gs_rig_t *rig_pointer, gs_ticker_t *ticker) {
    gs_rig_t rig = *rig_pointer;
    game_t *const game = rig.game;
    player_survival_t *const survival = &rig.player->survival;
    REQUIRE(gs_body_to(t, &rig, ticker, 0.5f64, GS_60_NORTH_Y + 0.5f64, 6));
    CHECK_NEAR(climate_latitude_deg(rig.player->movement.position[1]), 60.0f64, 0.01f64);
    /* A bed put down from the hotbar, as in run_game_census. */
    inventory_t *const inventory = &rig.item->inventory;
    *inventory_slot(inventory, 0u) = item_stack_make(ITEM_ID_BED, 1u);
    inventory->added_count += 1u;
    game->camera.pitch = -0.8f32;
    REQUIRE(gs_ticks(ticker, 3u));
    REQUIRE(rig.player->target.hit.result == RAYCAST_RESULT_HIT);
    gs_key(ticker, BTN_RIGHT, true);
    REQUIRE(gs_ticks(ticker, 1u));
    gs_key(ticker, BTN_RIGHT, false);
    REQUIRE(gs_ticks(ticker, 3u));
    REQUIRE(rig.player->target.hit.block == BLOCK_ID_BED);

    /* Midsummer midnight: the sun 6.5 degrees under the northern horizon, a white night. */
    game_clock_set(game, gs_clock_days(171u), 0.0f64);
    REQUIRE(gs_ticks(ticker, 1u));
    printf("  60 N, midsummer midnight: the sun's height at the body is %.4f\n", (libc_double_t)survival->sun_height);
    CHECK_NEAR(survival->sun_height, -0.1132f64, 0.002f64);
    gs_key(ticker, BTN_RIGHT, true);
    REQUIRE(gs_ticks(ticker, 1u));
    gs_key(ticker, BTN_RIGHT, false);
    CHECK_EQ_U64(survival->bed_answer, PLAYER_SURVIVAL_BED_NOT_NIGHT);
    CHECK(survival->state != PLAYER_SURVIVAL_SLEEPING);
    REQUIRE(gs_ticks(ticker, 3u));

    /* Midwinter at 18:00, the same bed: well dark, and it sleeps. */
    game_clock_set(game, gs_clock_days(354u), 0.75f64);
    REQUIRE(gs_ticks(ticker, 1u));
    printf("  60 N, midwinter 18:00: the sun's height at the body is %.4f\n", (libc_double_t)survival->sun_height);
    CHECK(survival->sun_height < PLAYER_SURVIVAL_NIGHT_SUN);
    gs_key(ticker, BTN_RIGHT, true);
    REQUIRE(gs_ticks(ticker, 1u));
    gs_key(ticker, BTN_RIGHT, false);
    CHECK_EQ_U64(survival->bed_answer, PLAYER_SURVIVAL_BED_ASLEEP);
    CHECK(survival->state == PLAYER_SURVIVAL_SLEEPING);
}

TEST(a_bed_does_not_work_at_60_north_on_a_midsummer_midnight) {
    gs_walk(t, gs_bed_at_60_north);
}

/* A zombie's try on the floor 30 blocks east of the body, with the view the tick hands mob_spawn_tick: the body's own sun. */
static mob_spawn_status_t gs_zombie_try(gs_rig_t *rig, uint32_t roll) {
    const float64_t *const at = rig->player->movement.position;
    const mob_spawn_view_t view = {.position = {at[0], at[1], at[2]}, .sun_height = rig->player->survival.sun_height};
    const float64_t place[COLLISION_AXES] = {floor(at[0]) + 30.5f64, floor(at[1]) + 0.5f64, (float64_t)GS_FLOOR_Z};
    return mob_spawn_check(rig->physics, &rig->world->map, MOB_ID_ZOMBIE, place, &view, roll);
}

static void gs_mobs_at_80_north(test_context_t *t, gs_rig_t *rig_pointer, gs_ticker_t *ticker) {
    gs_rig_t rig = *rig_pointer;
    game_t *const game = rig.game;
    /* Midwinter noon. */
    game_clock_set(game, gs_clock_days(354u), 0.5f64);

    /* On the equator, noon is noon. */
    REQUIRE(gs_body_to(t, &rig, ticker, 0.5f64, GS_EQUATOR_Y + 0.5f64, 36));
    REQUIRE(gs_ticks(ticker, 1u));
    printf("  the equator, midwinter noon: the sun's height %.4f, darken %" PRIu32 "\n",
           (libc_double_t)rig.player->survival.sun_height, mob_spawn_sky_darken(rig.player->survival.sun_height));
    CHECK_EQ_U64(gs_zombie_try(&rig, 7u), MOB_SPAWN_TOO_BRIGHT);

    /* At 80 north the sun is 13.5 degrees under the horizon at noon: darken 10, sky light 5. */
    REQUIRE(gs_body_to(t, &rig, ticker, 0.5f64, GS_80_NORTH_Y + 0.5f64, 36));
    REQUIRE(gs_ticks(ticker, 1u));
    const float64_t height = rig.player->survival.sun_height;
    printf("  80 N, midwinter noon: the sun's height %.4f, darken %" PRIu32 "\n", (libc_double_t)height,
           mob_spawn_sky_darken(height));
    CHECK_NEAR(height, -0.2334f64, 0.003f64);
    CHECK_EQ_U64(mob_spawn_sky_darken(height), 10u);
    CHECK_EQ_U64(gs_zombie_try(&rig, 7u), MOB_SPAWN_OK);
    CHECK_EQ_U64(gs_zombie_try(&rig, 5u), MOB_SPAWN_OK);
    CHECK_EQ_U64(gs_zombie_try(&rig, 4u), MOB_SPAWN_TOO_BRIGHT);
}

TEST(mobs_find_open_ground_dark_at_noon_at_80_north_in_midwinter) {
    gs_walk(t, gs_mobs_at_80_north);
}

/* ---- The season label ------------------------------------------------------------------------------------------------------ */

static void gs_line_at(game_t *game, float64_t y, uint32_t shown_day, char *line) {
    game->camera.position[1] = y;
    game_clock_set(game, gs_clock_days(shown_day), 0.5f64);
    game_calendar_line(game, line, CALENDAR_LINE_BYTES);
    printf("  y %.0f, day %" PRIu32 " at noon: %s\n", (libc_double_t)y, shown_day, line);
}

TEST(the_season_label_says_midnight_sun_and_polar_night) {
    gs_rig_t rig;
    REQUIRE(gs_rig_begin(t, &rig, false));
    game_t *const game = rig.game;
    const game_config_t config = gs_config();
    REQUIRE(game_open(game, &config));
    char line[CALENDAR_LINE_BYTES];
    gs_line_at(game, GS_70_NORTH_Y, 171u, line);
    CHECK_STR_EQ(line, "Year 1, Day 171, Midnight sun");
    gs_line_at(game, GS_80_NORTH_Y, 354u, line);
    CHECK_STR_EQ(line, "Year 1, Day 354, Polar night");
    gs_line_at(game, GS_EQUATOR_Y, 171u, line);
    CHECK_STR_EQ(line, "Year 1, Day 171, Early summer");
    gs_line_at(game, GS_45_SOUTH_Y, 171u, line);
    CHECK_STR_EQ(line, "Year 1, Day 171, Early winter");
    CHECK(game_calendar_south(game));
    /* Past the equator at y = 76,596 y is still positive, and the latitude is south. */
    gs_line_at(game, 95745.0f64, 171u, line);
    CHECK_STR_EQ(line, "Year 1, Day 171, Early winter");
    /* And the south has its own: midsummer is its polar night. */
    gs_line_at(game, -GS_80_NORTH_Y, 171u, line);
    CHECK_STR_EQ(line, "Year 1, Day 171, Polar night");
    game_close(game);
    gs_rig_end(t, &rig);
}

/* ---- The flag ----------------------------------------------------------------------------------------------------------------- */

TEST(the_day_of_year_flag_is_taken_with_test_world_and_refused_without) {
    REQUIRE(t->arg_count >= 1);
    char *const taken[] = {t->borrowed_args[0],
                           (char *)"--headless",
                           (char *)"--test-world",
                           (char *)"--day-of-year",
                           (char *)"171",
                           (char *)"--no-sound",
                           (char *)"--render-distance",
                           (char *)"2",
                           (char *)"--width",
                           (char *)"160",
                           (char *)"--height",
                           (char *)"90",
                           (char *)"--ticks",
                           (char *)"2",
                           nullptr};
    test_process_t run;
    test_process_run(t, taken, GS_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    CHECK_CONTAINS(run.output, "starts on day 171 of the year, clock day 91");
    if (t->failure_count != 0u) {
        fputs(run.output, stdout);
    }
    char *const refused[] = {t->borrowed_args[0],     (char *)"--headless", (char *)"--menu", (char *)"--no-sound",
                             (char *)"--day-of-year", (char *)"171",        nullptr};
    test_process_t menu;
    test_process_run(t, refused, GS_PROGRAM_TIMEOUT_MS, &menu);
    REQUIRE(menu.started != 0u);
    CHECK(menu.exit_code != MAIN_EXIT_OK);
    CHECK_CONTAINS(menu.output, "--day-of-year only means something with --test-world");
    char *const out_of_range[] = {t->borrowed_args[0],     (char *)"--headless", (char *)"--test-world",
                                  (char *)"--day-of-year", (char *)"366",        nullptr};
    test_process_t range;
    test_process_run(t, out_of_range, GS_PROGRAM_TIMEOUT_MS, &range);
    REQUIRE(range.started != 0u);
    CHECK(range.exit_code != MAIN_EXIT_OK);
    CHECK_CONTAINS(range.output, "--day-of-year takes a day from 1 to 365");
}

TEST_MAIN()
