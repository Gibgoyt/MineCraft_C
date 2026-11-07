/* See README.md. The HUD's top right corner: the sim's words and main's text. Allocates only from the game's tick arena, through
 * the climate calls it makes. */
#include "game/corner/index.h"

#include "calendar/index.h"
#include "climate/heat/index.h"
#include "climate/index.h"
#include "climate/sun/index.h"
#include "climate/systems/name/index.h"
#include "climate/weather/index.h"
#include "game/index.h"
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
    if (force || respawned || corner->climate_done == 0u || tick >= corner->climate_tick + GAME_CORNER_CLIMATE_TICKS) {
        if (corner_climate_line(game, climate, corner)) {
            corner->climate_tick = tick;
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
    const bool due = force || respawned || corner->warning_done == 0u || week_seen != corner->warning_week_seen ||
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

/* ---- Main's side ---------------------------------------------------------------------------------------------------- */

void game_hud_corner_read(const game_t *game, ui_hud_corner_t *out) {
    ui_hud_corner_clear(out);
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
