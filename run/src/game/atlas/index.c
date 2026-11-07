/* See README.md. The game's side of the Atlas (plan 004 phase 09). It allocates nothing: the Atlas's memory is ui's. */
#include "game/atlas/index.h"

#include "calendar/index.h"
#include "climate/weather/index.h"
#include "file/index.h"
#include "log/index.h"
#include "save/index.h"
#include "ui/index.h"

#include <stdio.h>
#include <string.h>

ui_atlas_t *game_atlas(const game_t *game) {
    return game->borrowed_ui != nullptr ? ui_atlas(game->borrowed_ui) : nullptr;
}

void game_atlas_open(game_t *game) {
    ui_t *const ui = game->borrowed_ui;
    if (ui == nullptr) {
        return;
    }
    /* The world's directory is its blocks' parent: save/world keeps <world>/blocks. */
    char directory[FILE_PATH_BYTES];
    const char *where = nullptr;
    const save_t *const save = game->borrowed_save;
    if (save != nullptr && save->world.open != 0u && save->world.borrowed_directory != nullptr) {
        const char *const blocks = save->world.borrowed_directory;
        const uint64_t length = strlen(blocks);
        const uint64_t suffix = sizeof("/blocks") - 1u;
        if (length > suffix && strcmp(blocks + length - suffix, "/blocks") == 0 &&
            length - suffix + sizeof("/atlas") <= sizeof(directory)) {
            memcpy(directory, blocks, length - suffix);
            memcpy(directory + length - suffix, "/atlas", sizeof("/atlas"));
            where = directory;
        }
    }
    const climate_t *const climate = game->borrowed_climate;
    if (!ui_atlas_world_open(ui, where, game->borrowed_terrain->seed, game->borrowed_job, climate)) {
        LOG_WARN("game: the Atlas could not start, so M shows nothing in this world");
    }
}

void game_atlas_close(game_t *game) {
    if (game->borrowed_ui != nullptr) {
        ui_atlas_world_close(game->borrowed_ui);
    }
}

void game_atlas_world(const game_t *game, ui_atlas_world_t *out) {
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const calendar_date_t date = calendar_date_of(game->day_zero, clock);
    /* A game day of the clock is 1200 seconds of the world's time, at any speed the clock runs. */
    const float64_t days = (float64_t)clock / (float64_t)CALENDAR_CLOCK_DAY;
    *out = (ui_atlas_world_t){
        .player = {game->camera.position[0], game->camera.position[1]},
        .t = climate_weather_time_of_clock(game->day_zero, clock),
        .world_ns = (int64_t)(days * 1200.0e9f64),
        .census_week = calendar_week_of(game->day_zero, clock),
        .yaw = game->camera.yaw,
        .day_of_year = date.day_of_year,
        ._pad0 = {0},
    };
}

bool game_atlas_up(const game_t *game) {
    return ui_atlas_up(game_atlas(game));
}

bool game_atlas_input(game_t *game, const input_frame_t *frame) {
    ui_atlas_t *const atlas = game_atlas(game);
    if (atlas == nullptr) {
        return false;
    }
    ui_atlas_world_t world;
    game_atlas_world(game, &world);
    const bool screen_up = game_has_screens(game) && ui_screen_up(game->borrowed_ui);
    return ui_atlas_input(atlas, &game->borrowed_ui->widget, frame, game->config.width, game->config.height, &world,
                          screen_up);
}

const uint8_t *game_atlas_compose(game_t *game, const uint8_t *picture) {
    ui_t *const ui = game->borrowed_ui;
    ui_atlas_t *const atlas = game_atlas(game);
    if (!ui_frame_begin_over(ui, picture, game->config.width, game->config.height)) {
        return picture;
    }
    ui_atlas_world_t world;
    game_atlas_world(game, &world);
    ui_atlas_frame(atlas, game->config.width, game->config.height, &world);
    ui_atlas_frame_draw(atlas, &ui->widget, &ui->canvas, &ui->font, &world);
    if (game->hud_debug != 0u) {
        ui_hud_debug_t debug;
        game_hud_debug_read(game, &debug);
        ui_hud_debug_draw(&ui->canvas, &ui->font, &debug);
    }
    return ui_frame_end(ui, true);
}
