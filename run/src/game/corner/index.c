/* See README.md. The HUD's top right corner: the sim's words and main's text. Allocates only from the game's tick arena, through
 * the climate calls it makes. */
#include "game/corner/index.h"

#include "calendar/index.h"
#include "climate/cell/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/sun/index.h"
#include "climate/systems/name/index.h"
#include "climate/season/index.h"
#include "climate/weather/index.h"
#include "game/command/index.h"
#include "game/index.h"
#include "terrain/biome/index.h"
#include "settings/persist/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float32_t CORNER_TWO_PI = 6.28318530717958647692f32;
static const float32_t CORNER_DEGREES = 57.2957795130823208768f32; /* 180 / pi */
static const char CORNER_DOT[] = " \xC2\xB7 ";

void game_corner_start(game_corner_t *corner) {
    memset(corner, 0, sizeof(*corner));
    job_group_init(&corner->group);
}

void game_corner_stop(game_corner_t *corner, const job_system_t *job) {
    job_group_wait(job, &corner->group);
    job_group_deinit(&corner->group);
}

/* ---- The words ------------------------------------------------------------------------------------------------------- */

uint32_t game_corner_wind_pack(float32_t u, float32_t v) {
    const float32_t speed = sqrtf(u * u + v * v);
    if (!(speed > 0.0f32)) {
        return 0u; /* still, or no number: calm, toward east */
    }
    const float32_t tenths = floorf(speed * 10.0f32 + 0.5f32);
    const uint32_t packed_speed = tenths >= 65535.0f32 ? 65535u : (uint32_t)tenths;
    float32_t toward = atan2f(v, u);
    toward = toward < 0.0f32 ? toward + CORNER_TWO_PI : toward;
    const uint32_t turn = (uint32_t)floorf(toward / CORNER_TWO_PI * 65536.0f32 + 0.5f32) & 0xFFFFu;
    return packed_speed | turn << 16;
}

void game_corner_wind_unpack(uint32_t word, float32_t *out_speed, float32_t *out_toward) {
    *out_speed = (float32_t)(word & 0xFFFFu) / 10.0f32;
    *out_toward = (float32_t)(word >> 16) / 65536.0f32 * CORNER_TWO_PI;
}

const char *game_corner_compass(float32_t toward) {
    static const char *const points[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    /* Where it comes from, clockwise from north: atan2(-u, -v) with (u, v) along toward. */
    float32_t from = atan2f(-cosf(toward), -sinf(toward)) * CORNER_DEGREES;
    from = from < 0.0f32 ? from + 360.0f32 : from;
    const uint32_t point = (uint32_t)floorf((from + 22.5f32) / 45.0f32) % 8u;
    return points[point];
}

void game_corner_arrow(float32_t toward, float32_t yaw, float32_t out[2]) {
    const float32_t alpha = toward - yaw;
    out[0] = -sinf(alpha);
    out[1] = -cosf(alpha);
}

void game_corner_climate_text(int32_t temperature_dc, uint32_t wind, char *out, uint32_t bytes) {
    if (bytes == 0u) {
        return;
    }
    /* In whole tenths, so a temperature that rounds to nothing is "0.0" and never "-0.0". */
    const bool below = temperature_dc < 0;
    const uint32_t magnitude = below ? (uint32_t)(-(int64_t)temperature_dc) : (uint32_t)temperature_dc;
    float32_t speed = 0.0f32, toward = 0.0f32;
    game_corner_wind_unpack(wind, &speed, &toward);
    const uint32_t speed_tenths = wind & 0xFFFFu;
    int32_t written = 0;
    if (speed < GAME_CORNER_CALM) {
        written = snprintf(out, bytes,
                           "%s%" PRIu32 ".%" PRIu32 " \xC2\xB0"
                           "C  wind calm",
                           below ? "-" : "", magnitude / 10u, magnitude % 10u);
    } else {
        written = snprintf(out, bytes,
                           "%s%" PRIu32 ".%" PRIu32 " \xC2\xB0"
                           "C  wind %" PRIu32 ".%" PRIu32 " m/s from %s",
                           below ? "-" : "", magnitude / 10u, magnitude % 10u, speed_tenths / 10u, speed_tenths % 10u,
                           game_corner_compass(toward));
    }
    if (written < 0) {
        out[0] = '\0';
    }
}

uint64_t game_corner_warning_pack(const climate_warning_t *warning) {
    if (warning->kind == CLIMATE_WARNING_NONE || warning->kind >= CLIMATE_WARNING_KINDS) {
        return 0u;
    }
    uint64_t word = (uint64_t)warning->kind << GAME_CORNER_WARNING_SHIFT;
    if (warning->kind == CLIMATE_WARNING_NAMED) {
        word |= (uint64_t)warning->name_seed;
        word |= warning->hurricane != 0u ? (uint64_t)GAME_CORNER_WARNING_HURRICANE << 32 : 0u;
    }
    return word;
}

void game_corner_warning_text(uint64_t word, char *out, uint32_t bytes) {
    if (bytes == 0u) {
        return;
    }
    out[0] = '\0';
    const uint64_t kind = word >> GAME_CORNER_WARNING_SHIFT;
    int32_t written = 0;
    if (kind == CLIMATE_WARNING_NAMED) {
        char name[CLIMATE_SYSTEM_NAME_BYTES];
        climate_system_name((uint32_t)(word & 0xFFFFFFFFull), name);
        const bool hurricane = ((word >> 32) & GAME_CORNER_WARNING_HURRICANE) != 0u;
        written = snprintf(out, bytes, "Storm warning: %s %s", hurricane ? "Hurricane" : "Tropical storm", name);
    } else if (kind == CLIMATE_WARNING_HURRICANE_LIKELY) {
        written = snprintf(out, bytes, "Storm warning: hurricane likely");
    } else if (kind == CLIMATE_WARNING_STORM) {
        written = snprintf(out, bytes, "Storm warning: storm");
    }
    if (written < 0) {
        out[0] = '\0';
    }
}

/* ---- The sim's side ------------------------------------------------------------------------------------------------- */

static climate_t *corner_climate(const game_t *game) {
    climate_t *const climate = game->borrowed_climate;
    return climate != nullptr && climate_is_open(climate) ? climate : nullptr;
}

/* The temperature, wind and season at the body, or false if a tile they need is not built yet. */
static bool corner_climate_line(game_t *game, climate_t *climate, game_corner_t *corner) {
    const float64_t *const feet = game->borrowed_player->movement.position;
    const float64_t x = feet[0], y = feet[1], z = feet[2] - (float64_t)MC_WORLD_Z_SEA;
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const calendar_date_t date = calendar_date_of(game->day_zero, clock);
    const float64_t t = climate_weather_time_of_clock(game->day_zero, clock);
    const climate_week_t *const last_week =
        climate_week_store_cell(climate_week_store(climate), (int32_t)floor(x / (float64_t)CLIMATE_CELL_BLOCKS),
                                (int32_t)floor(y / (float64_t)CLIMATE_CELL_BLOCKS));
    climate_now_t now;
    climate_normals_at_t normals;
    /* T now at the ground there (NaN asks at the blended ground), and that ground's height: master plan 6.6's lapse from it to the
     * body. Below-sea-level heat belongs to the ground, so it stays as the ground has it. */
    if (!climate_weather_at(climate, x, y, (float64_t)nanf(""), t, last_week, &game->arena_tick, &now) ||
        !climate_normals_at(climate, x, y, nanf(""), (float32_t)date.day_of_year, &normals)) {
        return false;
    }
    const float64_t t_hud =
        (float64_t)now.temperature_c - (float64_t)CLIMATE_LAPSE_C_PER_BLOCK * (z - (float64_t)normals.z);
    const float64_t lat = climate_latitude_deg(y);
    float64_t day_hours =
        climate_sun_day_hours(lat, climate_sun_declination_deg(calendar_year_position(game->day_zero, clock)));
    day_hours = day_hours <= 0.0f64 ? 0.0f64 : (day_hours >= 24.0f64 ? 24.0f64 : day_hours);
    /* The cell's rain today against its mean, both in mm a week: P(d) = 7 rain_mm_day, P0 = 7 rain_mm_year / 365. A coast in
     * season has the stored season; a sea cell its s_hur today. */
    const climate_season_input_t input = {
        .lat_deg = (float32_t)lat,
        .day_hours = (float32_t)day_hours,
        .rain_today = normals.rain_mm_day * 7.0f32,
        .rain_mean = normals.rain_mm_year * 7.0f32 / (float32_t)CALENDAR_DAYS_PER_YEAR,
        .hurricane_season = normals.hurricane_season != 0u ? 1.0f32 : normals.hurricane_s,
        .day_of_year = date.day_of_year,
        .monsoon_now = normals.monsoon_today,
        ._pad0 = {0},
    };
    const climate_season_label_t label = climate_season_of(&input);
    const float64_t tenths = floor(t_hud * 10.0f64 + 0.5f64);
    if (!(fabs(tenths) < 1.0e6f64)) {
        return false; /* no number a climate could give: the last words stay */
    }
    atomic_store_explicit(&corner->hud_temperature_dc, (int32_t)tenths, memory_order_relaxed);
    atomic_store_explicit(&corner->hud_wind, game_corner_wind_pack(now.wind[0], now.wind[1]), memory_order_relaxed);
    atomic_store_explicit(&corner->hud_season,
                          (uint32_t)label.season | (uint32_t)label.also << 8 | GAME_CORNER_SEASON_READY,
                          memory_order_relaxed);
    return true;
}

/* The warning at the place and moment the sim asked, on a worker, in its job arena. */
static void corner_warning_job(job_worker_t *worker, void *borrowed_argument) {
    game_corner_job_t *const job = borrowed_argument;
    job->ok =
        climate_warning_at(job->borrowed_climate, &worker->arena_job, job->x, job->y, job->t, &job->answer) ? 1u : 0u;
    atomic_store_explicit(&job->state, GAME_CORNER_JOB_DONE, memory_order_release);
}

void game_hud_climate_publish(game_t *game, bool force) {
    game_corner_t *const corner = game_corner_of(game);
    climate_t *const climate = corner_climate(game);
    player_t *const player = game->borrowed_player;
    if (corner == nullptr || climate == nullptr || player == nullptr) {
        return;
    }
    const uint64_t tick = game->tick_count;
    const uint64_t respawns = player->survival.respawned_count;
    const bool respawned = respawns != corner->respawns_seen;
    corner->respawns_seen = respawns;
    /* Not ready yet (a tile it needs is still being built) is tried again on the same beat, not every tick: a try builds the
     * day's list of systems round the body, which is too much to spend each tick while the tiles come in. */
    if (force || respawned || tick >= corner->climate_tick + GAME_CORNER_CLIMATE_TICKS) {
        corner->climate_tick = tick;
        if (corner_climate_line(game, climate, corner)) {
            corner->climate_done = 1u;
        }
    }
    /* A job that has finished: its answer goes out, if it had one. */
    game_corner_job_t *const job = &corner->job;
    uint32_t state = atomic_load_explicit(&job->state, memory_order_acquire);
    if (state == GAME_CORNER_JOB_DONE) {
        if (job->ok != 0u) {
            atomic_store_explicit(&corner->hud_warning, game_corner_warning_pack(&job->answer), memory_order_relaxed);
            corner->warning_done = 1u;
        }
        state = GAME_CORNER_JOB_IDLE;
        atomic_store_explicit(&job->state, state, memory_order_relaxed);
    }
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const uint32_t week_seen = (uint32_t)(calendar_week_of(game->day_zero, clock) + 1u);
    /* A warning never yet worked out is tried again on the climate line's beat, not each tick: each try is milliseconds of a
     * worker's time. */
    const bool due = force || respawned || week_seen != corner->warning_week_seen ||
                     (corner->warning_done == 0u && tick >= corner->warning_tick + GAME_CORNER_CLIMATE_TICKS) ||
                     tick >= corner->warning_tick + CLIMATE_WARNING_TICKS;
    /* One at a time: a warning still being worked out is the one that goes out next. */
    if (due && state == GAME_CORNER_JOB_IDLE) {
        const float64_t *const feet = player->movement.position;
        job->borrowed_climate = climate;
        job->x = feet[0];
        job->y = feet[1];
        job->t = climate_weather_time_of_clock(game->day_zero, clock);
        atomic_store_explicit(&job->state, GAME_CORNER_JOB_RUNNING, memory_order_relaxed);
        if (job_try_submit(game->borrowed_job, &corner->group, corner_warning_job, job)) {
            corner->warning_tick = tick;
            corner->warning_week_seen = week_seen;
        } else {
            atomic_store_explicit(&job->state, GAME_CORNER_JOB_IDLE,
                                  memory_order_relaxed); /* the queue was full: next tick */
        }
    }
}

/* ---- Plan 006 phase 01: the F3 tuning lines ------------------------------------------------------------------------------ */

static const char *const CORNER_CLASS_NAMES[CLIMATE_CLASS_COUNT] = {
    "Sea",     "Ice",     "Tundra",        "Desert",    "Steppe",      "Rainforest",
    "Monsoon", "Savanna", "Mediterranean", "Temperate", "Continental", "Boreal"};

const char *game_corner_class_name(uint32_t climate_class) {
    return climate_class < CLIMATE_CLASS_COUNT ? CORNER_CLASS_NAMES[climate_class] : "?";
}

/* Tenths of a degree, as an int16_t in a word's half. */
static uint32_t corner_tenths(float32_t celsius) {
    const float32_t tenths = floorf(celsius * 10.0f32 + 0.5f32);
    const float32_t held = tenths > 32767.0f32 ? 32767.0f32 : (tenths < -32768.0f32 ? -32768.0f32 : tenths);
    return (uint32_t)(uint16_t)(int16_t)held;
}

static uint32_t corner_millimetres(float32_t mm) {
    const float32_t whole = floorf(mm + 0.5f32);
    return whole >= 65535.0f32 ? 65535u : (whole > 0.0f32 ? (uint32_t)whole : 0u);
}

void game_corner_tuning_publish(game_t *game) {
    game_corner_t *const corner = game_corner_of(game);
    player_t *const player = game->borrowed_player;
    if (corner == nullptr || player == nullptr) {
        return;
    }
    const uint64_t tick = game->tick_count;
    if (corner->tuning_tick != 0u && tick < corner->tuning_tick + GAME_CORNER_CLIMATE_TICKS) {
        return;
    }
    corner->tuning_tick = tick + (tick == 0u ? 1u : 0u);
    const float64_t *const feet = player->movement.position;
    const float64_t x = feet[0], y = feet[1];
    const float32_t z = (float32_t)(feet[2] - (float64_t)MC_WORLD_Z_SEA);
    const uint32_t biome = game->borrowed_terrain != nullptr && game->borrowed_terrain->borrowed_biome != nullptr
                               ? (uint32_t)terrain_biome_at(game->borrowed_terrain->borrowed_biome, (int32_t)floor(x),
                                                            (int32_t)floor(y), (int32_t)floor(feet[2]))
                               : 0u;
    const uint32_t kinds = atomic_load_explicit(&corner->tuning_kinds, memory_order_relaxed);
    climate_t *const climate = corner_climate(game);
    if (climate == nullptr) {
        atomic_store_explicit(&corner->tuning_kinds, biome | GAME_CORNER_TUNING_NONE << 24, memory_order_relaxed);
        return;
    }
    const calendar_date_t date = game_calendar_date(game);
    climate_normals_at_t normals;
    climate_cell_t cell;
    if (!climate_normals_at(climate, x, y, z, (float32_t)date.day_of_year, &normals) ||
        !climate_cell_get(climate, (int32_t)floor(x), (int32_t)floor(y), &cell)) {
        /* Not built yet: the biome now, the rest as it was. */
        atomic_store_explicit(&corner->tuning_kinds, (kinds & ~0xFFu) | biome, memory_order_relaxed);
        return;
    }
    const bool land = (cell.flags & CLIMATE_CELL_SEA) == 0u;
    climate_season_months_t months;
    climate_season_months(&cell, z, land, &months);
    /* The family is the class the numbers at the feet's own height give, by the same first match as a cell's. */
    const uint32_t family =
        land ? climate_season_classify(months.t_mean_c, months.t_max_month_c, months.t_min_month_c, months.rain_mm_year,
                                       months.driest_month_mm, months.dry_summer != 0u,
                                       (cell.flags & CLIMATE_CELL_MONSOON) != 0u)
             : (uint32_t)CLIMATE_CLASS_NONE;
    atomic_store_explicit(&corner->tuning_kinds,
                          biome | family << 8 | (uint32_t)normals.climate_class << 16 |
                              GAME_CORNER_TUNING_CLIMATE << 24,
                          memory_order_relaxed);
    atomic_store_explicit(&corner->tuning_year,
                          corner_tenths(normals.t_mean_c) | corner_tenths(normals.t_max_month_c) << 16,
                          memory_order_relaxed);
    atomic_store_explicit(&corner->tuning_cold,
                          corner_tenths(normals.t_min_month_c) | corner_tenths(normals.t_day_c) << 16,
                          memory_order_relaxed);
    atomic_store_explicit(&corner->tuning_z, (int32_t)floorf(normals.z), memory_order_relaxed);
    atomic_store_explicit(&corner->tuning_rain,
                          corner_millimetres(normals.rain_mm_year) | corner_millimetres(months.driest_month_mm) << 16,
                          memory_order_relaxed);
    atomic_store_explicit(&corner->tuning_snowline, (int32_t)floorf(normals.snow_balance_z + 0.5f32),
                          memory_order_relaxed);
}

/* "24.6", "-0.3", never "-0.0", from tenths. */
static void corner_degrees(uint32_t half, char *out, uint32_t bytes) {
    const int32_t tenths = (int32_t)(int16_t)(uint16_t)half;
    const uint32_t magnitude = tenths < 0 ? (uint32_t)(-tenths) : (uint32_t)tenths;
    (void)snprintf(out, bytes, "%s%" PRIu32 ".%" PRIu32, tenths < 0 ? "-" : "", magnitude / 10u, magnitude % 10u);
}

void game_corner_tuning_read(const game_t *game, char lines[GAME_CORNER_TUNING_LINES][GAME_CORNER_TUNING_BYTES]) {
    for (uint32_t i = 0u; i < GAME_CORNER_TUNING_LINES; i++) {
        lines[i][0] = '\0';
    }
    const game_corner_t *const corner = game_corner_of(game);
    if (corner == nullptr) {
        return;
    }
    const uint32_t kinds = atomic_load_explicit(&corner->tuning_kinds, memory_order_relaxed);
    const uint32_t biome = kinds & 0xFFu, source = kinds >> 24;
    const char *const biome_name = biome < BIOME_COUNT ? biome_defs[biome].name : "?";
    if (source == GAME_CORNER_TUNING_NONE) {
        (void)snprintf(lines[0], GAME_CORNER_TUNING_BYTES, "biome %s  (no climate)", biome_name);
        return;
    }
    (void)snprintf(lines[0], GAME_CORNER_TUNING_BYTES, "biome %s  family %s  class %s (%s)", biome_name,
                   game_corner_class_name((kinds >> 8) & 0xFFu), game_corner_class_name((kinds >> 16) & 0xFFu),
                   source == GAME_CORNER_TUNING_FALLBACK ? "fallback" : "climate");
    const uint32_t year = atomic_load_explicit(&corner->tuning_year, memory_order_relaxed);
    const uint32_t cold = atomic_load_explicit(&corner->tuning_cold, memory_order_relaxed);
    char mean[16], warm[16], coldest[16], today[16];
    corner_degrees(year & 0xFFFFu, mean, sizeof(mean));
    corner_degrees(year >> 16, warm, sizeof(warm));
    corner_degrees(cold & 0xFFFFu, coldest, sizeof(coldest));
    corner_degrees(cold >> 16, today, sizeof(today));
    (void)snprintf(lines[1], GAME_CORNER_TUNING_BYTES,
                   "yearly %s \xC2\xB0"
                   "C  warm %s  cold %s at z %" PRId32 "   today %s",
                   mean, warm, coldest, atomic_load_explicit(&corner->tuning_z, memory_order_relaxed), today);
    const uint32_t rain = atomic_load_explicit(&corner->tuning_rain, memory_order_relaxed);
    const int32_t snowline = atomic_load_explicit(&corner->tuning_snowline, memory_order_relaxed);
    char snow[24];
    if ((float32_t)snowline >= CLIMATE_SNOW_BALANCE_NONE) {
        (void)snprintf(snow, sizeof(snow), "none");
    } else {
        (void)snprintf(snow, sizeof(snow), "z %" PRId32, snowline);
    }
    (void)snprintf(lines[2], GAME_CORNER_TUNING_BYTES, "rain %" PRIu32 " mm/yr  dry month %" PRIu32 " mm   snowline %s",
                   rain & 0xFFFFu, rain >> 16, snow);
}

/* ---- Main's side ---------------------------------------------------------------------------------------------------- */

void game_hud_corner_read(const game_t *game, ui_hud_corner_t *out) {
    ui_hud_corner_clear(out);
    /* Plan 006 phase 01: flying and the speed factor, on the line plan 004 left for a settlement's name, as there is none yet. */
    game_command_flight_line(game, out->lines[UI_HUD_CORNER_SETTLEMENT], sizeof(out->lines[UI_HUD_CORNER_SETTLEMENT]));
    const game_corner_t *const corner = game_corner_of(game);
    const uint32_t season = corner != nullptr ? atomic_load_explicit(&corner->hud_season, memory_order_relaxed) : 0u;
    const bool ready = (season & GAME_CORNER_SEASON_READY) != 0u;
    if (game->config.show_date != 0u) {
        const calendar_date_t date = game_calendar_date(game);
        char label[CLIMATE_SEASON_TEXT_BYTES];
        if (ready) {
            (void)climate_season_text(
                (climate_season_label_t){.season = (uint8_t)(season & 0xFFu), .also = (uint8_t)((season >> 8) & 0xFFu)},
                label, sizeof(label));
        } else {
            /* Until the sim has said, phase 01's: the hemisphere's season or the polar day, from the date and latitude alone. */
            (void)snprintf(label, sizeof(label), "%s", game_calendar_season_name(game));
        }
        (void)snprintf(out->lines[UI_HUD_CORNER_DATE], sizeof(out->lines[UI_HUD_CORNER_DATE]),
                       "Year %" PRIu32 "%sDay %" PRIu32 "%s%s", date.year, CORNER_DOT, (uint32_t)date.day_of_year + 1u,
                       label[0] != '\0' ? CORNER_DOT : "", label);
    }
    if (corner == nullptr) {
        return;
    }
    if ((game->config.climate_hud & SETTINGS_CLIMATE_HUD_LINE) != 0u && ready) {
        const uint32_t wind = atomic_load_explicit(&corner->hud_wind, memory_order_relaxed);
        game_corner_climate_text(atomic_load_explicit(&corner->hud_temperature_dc, memory_order_relaxed), wind,
                                 out->lines[UI_HUD_CORNER_CLIMATE], sizeof(out->lines[UI_HUD_CORNER_CLIMATE]));
        float32_t speed = 0.0f32, toward = 0.0f32;
        game_corner_wind_unpack(wind, &speed, &toward);
        if (!(speed < GAME_CORNER_CALM)) {
            /* Main's own eye, so the arrow turns as the player looks round, between the sim's words. */
            game_corner_arrow(toward, game->camera.yaw, out->arrow);
            out->arrow_line = (uint8_t)UI_HUD_CORNER_CLIMATE;
        }
    }
    if ((game->config.climate_hud & SETTINGS_CLIMATE_HUD_WARNINGS) != 0u) {
        game_corner_warning_text(atomic_load_explicit(&corner->hud_warning, memory_order_relaxed),
                                 out->lines[UI_HUD_CORNER_WARNING], sizeof(out->lines[UI_HUD_CORNER_WARNING]));
        if (out->lines[UI_HUD_CORNER_WARNING][0] != '\0') {
            out->warning_line = (uint8_t)UI_HUD_CORNER_WARNING;
        }
    }
}

void game_hud_corners_draw(game_t *game, bool below_notice) {
    ui_t *const ui = game->borrowed_ui;
    int32_t block[3];
    game_hud_block_read(game, block);
    ui_rect_t avoid = {0, 0, 0, 0};
    if (game->hud_debug != 0u) {
        avoid = ui_hud_debug_panel(ui->canvas.width, ui->canvas.height);
    } else if (game->config.show_coordinates != 0u) {
        /* The overlay has them too, on a panel in the same corner. */
        ui_hud_coordinates_draw(&ui->canvas, &ui->font, block, below_notice);
        avoid = ui_hud_coordinates_panel(&ui->canvas, &ui->font, block, below_notice);
    }
    ui_hud_corner_t corner;
    game_hud_corner_read(game, &corner);
    (void)ui_hud_corner_draw(&ui->canvas, &ui->font, &corner, avoid, below_notice);
}
