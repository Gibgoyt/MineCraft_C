/* See README.md. The sim's side of the weather the player sees and feels: samples, lightning, the rain top map and the wind, sent
 * to the thread that draws once a tick. Everything here is a pure function of the seed, the world, the clock and the ticks. */
#include "game/weather/index.h"

#include "climate/systems/field/index.h"
#include "game/index.h"
#include "log/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"
#include "time/index.h"

#include <math.h>
#include <string.h>

#define WEATHER_TWO_PI 6.283185307179586f64
/* What the forced kinds are, as climate_weather_at would give them: rain rates in mm an hour, winds in blocks a second. */
#define WEATHER_UNIT_OF_HASH 1.1102230246251565e-16f64 /* 2^-53 */

static const char *const WEATHER_KIND_NAMES[GAME_WEATHER_KIND_COUNT] = {
    "sampled", "clear", "rain", "snow", "sleet", "storm", "thunder", "fog", "dust", "hurricane"};

bool game_weather_kind_parse(const char *word, game_weather_kind_t *out) {
    for (uint32_t k = GAME_WEATHER_CLEAR; k < GAME_WEATHER_KIND_COUNT; k++) {
        if (strcmp(word, WEATHER_KIND_NAMES[k]) == 0) {
            *out = (game_weather_kind_t)k;
            return true;
        }
    }
    return false;
}

const char *game_weather_kind_name(game_weather_kind_t kind) {
    return (uint32_t)kind < GAME_WEATHER_KIND_COUNT ? WEATHER_KIND_NAMES[kind] : "unknown";
}

void game_weather_forced_now(game_weather_kind_t kind, climate_now_t *out) {
    memset(out, 0, sizeof(*out));
    out->temperature_c = 15.0f32;
    out->pressure_hpa = 1013.0f32;
    out->cloud = 0.05f32;
    out->day_kind = CLIMATE_DAY_CLEAR;
    switch (kind) {
    case GAME_WEATHER_RAIN:
        *out = (climate_now_t){.temperature_c = 12.0f32,
                               .rain_mm_h = 8.0f32,
                               .wind = {4.0f32, 2.0f32},
                               .pressure_hpa = 1004.0f32,
                               .cloud = 1.0f32,
                               .day_kind = CLIMATE_DAY_RAIN};
        break;
    case GAME_WEATHER_SNOW:
        *out = (climate_now_t){.temperature_c = -6.0f32,
                               .rain_mm_h = 3.0f32,
                               .snow_share = 1.0f32,
                               .wind = {3.0f32, 1.0f32},
                               .pressure_hpa = 1008.0f32,
                               .cloud = 0.9f32,
                               .day_kind = CLIMATE_DAY_SNOW};
        break;
    case GAME_WEATHER_SLEET:
        *out = (climate_now_t){.temperature_c = 0.0f32,
                               .rain_mm_h = 5.0f32,
                               .snow_share = 0.5f32,
                               .wind = {5.0f32, 0.0f32},
                               .pressure_hpa = 1005.0f32,
                               .cloud = 1.0f32,
                               .day_kind = CLIMATE_DAY_SLEET};
        break;
    case GAME_WEATHER_STORM:
        *out = (climate_now_t){.temperature_c = 9.0f32,
                               .rain_mm_h = 15.0f32,
                               .wind = {-22.0f32, 8.0f32},
                               .pressure_hpa = 985.0f32,
                               .cloud = 1.0f32,
                               .day_kind = CLIMATE_DAY_STORM};
        break;
    case GAME_WEATHER_THUNDER:
        *out = (climate_now_t){.temperature_c = 26.0f32,
                               .rain_mm_h = 30.0f32,
                               .wind = {-6.0f32, 3.0f32},
                               .pressure_hpa = 1002.0f32,
                               .cloud = 1.0f32,
                               .thunder = 1.0f32,
                               .thunder_distance = (float32_t)GAME_LIGHTNING_FORCED_BLOCKS,
                               .thunder_cell = 1u,
                               .day_kind = CLIMATE_DAY_THUNDERSTORM};
        break;
    case GAME_WEATHER_FOG:
        *out = (climate_now_t){.temperature_c = 8.0f32,
                               .pressure_hpa = 1020.0f32,
                               .cloud = 0.6f32,
                               .fog = 0.9f32,
                               .day_kind = CLIMATE_DAY_FOG};
        break;
    case GAME_WEATHER_DUST:
        *out = (climate_now_t){.temperature_c = 33.0f32,
                               .wind = {14.0f32, 0.0f32},
                               .pressure_hpa = 1006.0f32,
                               .cloud = 0.2f32,
                               .dust = 0.9f32,
                               .day_kind = CLIMATE_DAY_DUST_STORM};
        break;
    case GAME_WEATHER_HURRICANE:
        *out = (climate_now_t){.temperature_c = 27.0f32,
                               .rain_mm_h = 40.0f32,
                               .wind = {-45.0f32, 25.0f32},
                               .pressure_hpa = 950.0f32,
                               .cloud = 1.0f32,
                               .in_hurricane = 1u,
                               .day_kind = CLIMATE_DAY_HURRICANE};
        break;
    case GAME_WEATHER_SAMPLED:
    case GAME_WEATHER_CLEAR:
    case GAME_WEATHER_KIND_COUNT:
        break;
    }
}

void game_weather_start(game_weather_t *weather, game_weather_kind_t kind) {
    memset(weather, 0, sizeof(*weather));
    render_exchange_start(&weather->exchange);
    weather->kind = kind;
}

/* ---- The rain top map ------------------------------------------------------------------------------------------------ */

int16_t game_weather_column_top(world_t *world, int32_t x, int32_t y) {
    const world_map_t *const map = &world->map;
    const int32_t chunk_x = column_chunk_of(x), chunk_y = column_chunk_of(y);
    uint32_t slot = 0u;
    const column_t *const column = world_map_column_at(map, chunk_x, chunk_y);
    if (column == nullptr || !world_map_slot_of(map, chunk_x, chunk_y, &slot)) {
        return POST_WEATHER_MAP_COVERED;
    }
    const uint32_t local_x = column_local_of(x), local_y = column_local_of(y);
    int32_t z = world_edit_column_top_z(&world->edit, slot);
    while (z >= COLUMN_Z_STORAGE_MIN) {
        const uint32_t s = column_section_of(z);
        const section_t *const section = &column->sections[s];
        if (section->bits == SECTION_BITS_UNIFORM && section->block_uniform == BLOCK_ID_AIR) {
            z = COLUMN_Z_STORAGE_MIN + (int32_t)(s * SECTION_SIZE) - 1; /* the whole section is air */
            continue;
        }
        const uint16_t state =
            section_block_get(section, section_voxel_index(local_x, local_y, column_section_local_of(z)));
        if (state != BLOCK_ID_AIR) {
            return (int16_t)z;
        }
        z--;
    }
    return WORLD_EDIT_TOP_Z_NONE;
}

/* Every column of the map again, round the eye's column. */
static void weather_map_build(game_weather_t *weather, world_t *world, int32_t eye_x, int32_t eye_y) {
    const int32_t half = (int32_t)POST_WEATHER_MAP_SIDE / 2;
    weather->map_origin[0] = eye_x - half;
    weather->map_origin[1] = eye_y - half;
    uint32_t unloaded = 0u;
    for (uint32_t row = 0u; row < POST_WEATHER_MAP_SIDE; row++) {
        for (uint32_t across = 0u; across < POST_WEATHER_MAP_SIDE; across++) {
            const int16_t top = game_weather_column_top(world, weather->map_origin[0] + (int32_t)across,
                                                        weather->map_origin[1] + (int32_t)row);
            unloaded += top == POST_WEATHER_MAP_COVERED ? 1u : 0u;
            weather->map[row * POST_WEATHER_MAP_SIDE + across] = top;
        }
    }
    weather->map_unloaded = unloaded;
    weather->map_ready_count = world->map.ready_count;
    weather->log_cursor = world_edit_log_position(&world->edit);
    weather->map_built = 1u;
    weather->map_revision++;
}

/* The map follows the eye and the world: made again when the eye has gone GAME_WEATHER_MAP_MOVE from its middle, when a column it
 * lacked may have come (only on a sample tick, as streaming changes that often), or when the log lost count; otherwise only the
 * columns the log says changed are looked at again. */
static void weather_map_update(game_weather_t *weather, world_t *world, int32_t eye_x, int32_t eye_y,
                               bool sample_tick) {
    const int32_t half = (int32_t)POST_WEATHER_MAP_SIDE / 2;
    const int32_t off_x = eye_x - (weather->map_origin[0] + half), off_y = eye_y - (weather->map_origin[1] + half);
    const bool moved = off_x > (int32_t)GAME_WEATHER_MAP_MOVE || off_x < -(int32_t)GAME_WEATHER_MAP_MOVE ||
                       off_y > (int32_t)GAME_WEATHER_MAP_MOVE || off_y < -(int32_t)GAME_WEATHER_MAP_MOVE;
    const bool gaps = weather->map_unloaded > 0u && sample_tick && world->map.ready_count != weather->map_ready_count;
    if (weather->map_built == 0u || moved || gaps) {
        weather_map_build(weather, world, eye_x, eye_y);
        return;
    }
    enum : uint32_t { WEATHER_RECORDS = 256u };
    world_edit_record_t records[WEATHER_RECORDS];
    bool changed = false;
    for (;;) {
        uint64_t lost = 0u;
        const uint64_t count = world_edit_log_read(&world->edit, &weather->log_cursor, records, WEATHER_RECORDS, &lost);
        if (lost != 0u) {
            weather_map_build(weather, world, eye_x, eye_y);
            return;
        }
        for (uint64_t r = 0u; r < count; r++) {
            const int32_t across = records[r].x - weather->map_origin[0], row = records[r].y - weather->map_origin[1];
            if (across < 0 || row < 0 || across >= (int32_t)POST_WEATHER_MAP_SIDE ||
                row >= (int32_t)POST_WEATHER_MAP_SIDE) {
                continue;
            }
            int16_t *const entry = &weather->map[(uint32_t)row * POST_WEATHER_MAP_SIDE + (uint32_t)across];
            const int16_t top = game_weather_column_top(world, records[r].x, records[r].y);
            changed = changed || top != *entry;
            *entry = top;
        }
        if (count < WEATHER_RECORDS) {
            break;
        }
    }
    weather->map_revision += changed ? 1u : 0u;
}

/* ---- Lightning ------------------------------------------------------------------------------------------------------- */

static float64_t weather_unit(uint64_t hash) {
    return (float64_t)(hash >> 11) * WEATHER_UNIT_OF_HASH;
}

bool game_weather_cell_strikes(uint64_t seed, const climate_thunder_t *cell, uint64_t slot, float64_t t,
                               float64_t out_place[2]) {
    float64_t centre[2], share = 0.0f64;
    if (!climate_systems_thunder_at(cell, t, centre, &share)) {
        return false;
    }
    const uint64_t hash =
        terrain_rng_mix(seed ^ terrain_rng_mix(cell->id ^ terrain_rng_mix(slot + 0x9E3779B97F4A7C15ull)));
    if (!(weather_unit(hash) < GAME_LIGHTNING_RATE * share)) {
        return false;
    }
    const float64_t reach = (float64_t)cell->radius * sqrt(weather_unit(terrain_rng_mix(hash ^ 0x5851F42D4C957F2Dull)));
    const float64_t angle = WEATHER_TWO_PI * weather_unit(terrain_rng_mix(hash ^ 0x14057B7EF767814Full));
    out_place[0] = centre[0] + reach * cos(angle);
    out_place[1] = centre[1] + reach * sin(angle);
    return true;
}

void game_weather_bolt_make(uint64_t seed, uint64_t slot, const float64_t place[2], float64_t ground_z,
                            float64_t world_top, const float64_t eye[3], game_weather_strike_t *out) {
    memset(out, 0, sizeof(*out));
    out->origin[0] = (int32_t)floor(place[0]);
    out->origin[1] = (int32_t)floor(place[1]);
    out->origin[2] = (int32_t)floor(ground_z);
    const float64_t cloud = fmin(ground_z + (float64_t)GAME_LIGHTNING_CLOUD_BASE, world_top);
    const float64_t height = cloud > ground_z ? cloud - ground_z : 0.0f64;
    const float64_t base[3] = {place[0] - (float64_t)out->origin[0], place[1] - (float64_t)out->origin[1],
                               ground_z - (float64_t)out->origin[2]};
    uint64_t hash = terrain_rng_mix(seed ^ terrain_rng_mix(slot ^ 0xD1B54A32D192ED03ull));
    for (uint32_t j = 0u; j <= POST_BOLT_SEGMENTS; j++) {
        float64_t side[2] = {0.0f64, 0.0f64};
        if (j < POST_BOLT_SEGMENTS) {
            hash = terrain_rng_mix(hash + (uint64_t)j);
            side[0] = (2.0f64 * weather_unit(hash) - 1.0f64) * (float64_t)GAME_LIGHTNING_JITTER;
            hash = terrain_rng_mix(hash ^ 0x9E3779B97F4A7C15ull);
            side[1] = (2.0f64 * weather_unit(hash) - 1.0f64) * (float64_t)GAME_LIGHTNING_JITTER;
        }
        const float64_t down = (float64_t)j / (float64_t)POST_BOLT_SEGMENTS;
        out->bolt[j][0] = (float32_t)(base[0] + side[0]);
        out->bolt[j][1] = (float32_t)(base[1] + side[1]);
        out->bolt[j][2] = (float32_t)(base[2] + height * (1.0f64 - down));
    }
    const float64_t dx = place[0] - eye[0], dy = place[1] - eye[1], dz = ground_z - eye[2];
    out->distance = (float32_t)sqrt(dx * dx + dy * dy + dz * dz);
}

float32_t game_weather_flash(float32_t distance) {
    const float32_t ratio = distance / RENDER_FLASH_DISTANCE;
    return 1.0f32 / (1.0f32 + ratio * ratio);
}

/* ---- Wiring into a game ---------------------------------------------------------------------------------------------- */

static const climate_t *weather_climate(const game_t *game) {
    const climate_t *const climate = game->borrowed_climate;
    return climate != nullptr && climate_is_open(climate) ? climate : nullptr;
}

/* Where the weather is for: the body's eyes, or with no body the middle of the column the map is centred on, at its top. */
static void weather_eye(const game_t *game, float64_t out[3], bool *out_body) {
    const player_t *const player = game->borrowed_player;
    if (player != nullptr) {
        out[0] = player->movement.position[0];
        out[1] = player->movement.position[1];
        out[2] = player->movement.position[2] + player->movement.eye_height;
        *out_body = true;
        return;
    }
    const world_map_t *const map = &game->borrowed_world->map;
    out[0] = (float64_t)map->center_x * (float64_t)COLUMN_SIZE + 0.5f64 * (float64_t)COLUMN_SIZE;
    out[1] = (float64_t)map->center_y * (float64_t)COLUMN_SIZE + 0.5f64 * (float64_t)COLUMN_SIZE;
    const int16_t top = game_weather_column_top(game->borrowed_world, (int32_t)floor(out[0]), (int32_t)floor(out[1]));
    out[2] = top == POST_WEATHER_MAP_COVERED ? 0.0f64 : (float64_t)top + 2.0f64;
    *out_body = false;
}

/* The ground a strike meets: the rain top where the map has it, else the terrain's estimate. */
static float64_t weather_ground(const game_t *game, const game_weather_t *weather, const float64_t place[2]) {
    const int32_t x = (int32_t)floor(place[0]), y = (int32_t)floor(place[1]);
    const int32_t across = x - weather->map_origin[0], row = y - weather->map_origin[1];
    if (weather->map_built != 0u && across >= 0 && row >= 0 && across < (int32_t)POST_WEATHER_MAP_SIDE &&
        row < (int32_t)POST_WEATHER_MAP_SIDE) {
        const int16_t top = weather->map[(uint32_t)row * POST_WEATHER_MAP_SIDE + (uint32_t)across];
        if (top != POST_WEATHER_MAP_COVERED) {
            return (float64_t)top + 1.0f64;
        }
    }
    const float32_t estimate = terrain_height_estimate(game->borrowed_terrain, x, y);
    return floor((float64_t)estimate) + 1.0f64;
}

/* A day's cells near the player, kept: made again on a new day, or when the player has gone far from where it was made. */
static void weather_cells_update(game_t *game, game_weather_t *weather, const climate_t *climate,
                                 const float64_t eye[3], int64_t day) {
    const float64_t dx = eye[0] - weather->cells_x, dy = eye[1] - weather->cells_y;
    const float64_t move = (float64_t)GAME_WEATHER_CELLS_MOVE;
    if (weather->cells_ready != 0u && weather->cells_day == day && dx * dx + dy * dy < move * move) {
        return;
    }
    climate_systems_day_t list;
    const float32_t range = (float32_t)(GAME_LIGHTNING_RANGE + GAME_WEATHER_CELLS_MOVE);
    if (!climate_systems_day_build(climate, &game->arena_tick, eye[0], eye[1], range, range, day,
                                   1u << CLIMATE_SYSTEM_THUNDER, &list)) {
        LOG_WARN("weather: the cells near the player did not fit the tick's arena");
        return;
    }
    uint32_t count = list.thunder_count;
    if (count > GAME_WEATHER_CELLS_MAX) {
        if (weather->cells_full_said == 0u) {
            LOG_WARN("weather: %u thunderstorm cells near the player, of which %u are kept", list.thunder_count,
                     GAME_WEATHER_CELLS_MAX);
            weather->cells_full_said = 1u;
        }
        count = GAME_WEATHER_CELLS_MAX;
    }
    memcpy(weather->cells, list.borrowed_thunder, (uint64_t)count * sizeof(climate_thunder_t));
    weather->cell_count = count;
    weather->cells_x = eye[0];
    weather->cells_y = eye[1];
    weather->cells_day = day;
    /* A list with squares whose tiles were not built is made again next time: those cells were left out. */
    weather->cells_ready = list.thunder_complete;
}

/* The strike seen this tick, if any: the nearest in the current second's slot, while its bolt lasts. */
static bool weather_strike(game_t *game, game_weather_t *weather, const climate_t *climate, const float64_t eye[3],
                           float64_t t_days, game_weather_strike_t *out) {
    const uint64_t tick = game->tick_count;
    const float64_t world_top = (float64_t)COLUMN_Z_GAMEPLAY_MAX;
    if (weather->kind == GAME_WEATHER_THUNDER) {
        const uint64_t slot = tick / GAME_LIGHTNING_FORCED_TICKS, age = tick % GAME_LIGHTNING_FORCED_TICKS;
        if (age >= GAME_LIGHTNING_BOLT_TICKS) {
            return false;
        }
        const float64_t place[2] = {eye[0] + (float64_t)GAME_LIGHTNING_FORCED_BLOCKS, eye[1]};
        game_weather_bolt_make(game->borrowed_terrain->seed, slot, place, weather_ground(game, weather, place),
                               world_top, eye, out);
        out->age = (float32_t)age / (float32_t)MC_GAME_TICK_HZ;
        return true;
    }
    if (climate == nullptr || weather->kind != GAME_WEATHER_SAMPLED) {
        return false;
    }
    const uint64_t slot = tick / GAME_LIGHTNING_SLOT_TICKS, age = tick % GAME_LIGHTNING_SLOT_TICKS;
    if (age >= GAME_LIGHTNING_BOLT_TICKS) {
        return false;
    }
    weather_cells_update(game, weather, climate, eye, (int64_t)floor(t_days));
    /* At the slot's start, so the strike stays where it struck for the whole of its bolt. */
    const float64_t t_slot = t_days - (float64_t)age / (float64_t)MC_GAME_TICK_HZ / GAME_DAY_SECONDS;
    const uint64_t seed = climate_seed(climate);
    const float64_t range = (float64_t)GAME_LIGHTNING_RANGE;
    float64_t best = -1.0f64, best_place[2] = {0.0f64, 0.0f64};
    for (uint32_t c = 0u; c < weather->cell_count; c++) {
        float64_t place[2];
        if (!game_weather_cell_strikes(seed, &weather->cells[c], slot, t_slot, place)) {
            continue;
        }
        const float64_t dx = place[0] - eye[0], dy = place[1] - eye[1];
        const float64_t distance = sqrt(dx * dx + dy * dy);
        if (distance <= range && (best < 0.0f64 || distance < best)) {
            best = distance;
            best_place[0] = place[0];
            best_place[1] = place[1];
        }
    }
    if (best < 0.0f64) {
        return false;
    }
    game_weather_bolt_make(seed, slot, best_place, weather_ground(game, weather, best_place), world_top, eye, out);
    out->age = (float32_t)age / (float32_t)MC_GAME_TICK_HZ;
    return true;
}

void game_weather_tick(game_t *game, int64_t tick_ns) {
    game_weather_t *const weather = game_weather_of(game);
    if (weather == nullptr) {
        return;
    }
    const climate_t *const climate = weather_climate(game);
    float64_t eye[3];
    bool body = false;
    weather_eye(game, eye, &body);
    const uint64_t tick = game->tick_count;
    const bool forced = weather->kind != GAME_WEATHER_SAMPLED;
    const bool sample_tick = weather->sampled == 0u || tick >= weather->sample_tick + GAME_WEATHER_SAMPLE_TICKS;
    const float64_t t_days =
        climate_weather_time_of_clock(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed));
    if (forced && weather->sampled == 0u) {
        game_weather_forced_now((game_weather_kind_t)weather->kind, &weather->now);
        memcpy(weather->wind_from, weather->now.wind, sizeof(weather->wind_from));
        memcpy(weather->wind_to, weather->now.wind, sizeof(weather->wind_to));
        weather->sample_tick = tick;
        weather->sampled = 1u;
    } else if (!forced && climate != nullptr && sample_tick) {
        const climate_week_t *const last_week = climate_week_store_cell(
            climate_week_store(climate), (int32_t)floor(eye[0] / (float64_t)CLIMATE_CELL_BLOCKS),
            (int32_t)floor(eye[1] / (float64_t)CLIMATE_CELL_BLOCKS));
        climate_now_t now;
        /* A flying eye asks at the ground's height; a body at its eyes'. */
        if (climate_weather_at(climate, eye[0], eye[1], body ? eye[2] : (float64_t)nanf(""), t_days, last_week,
                               &game->arena_tick, &now)) {
            memcpy(weather->wind_from, weather->sampled != 0u ? weather->wind : now.wind, sizeof(weather->wind_from));
            memcpy(weather->wind_to, now.wind, sizeof(weather->wind_to));
            weather->now = now;
            weather->sample_tick = tick;
            weather->sampled = 1u;
        }
    }
    /* The wind between samples, so a body is not shoved anew every half second. */
    if (weather->sampled != 0u) {
        const uint64_t since = tick - weather->sample_tick;
        const float32_t share =
            since >= GAME_WEATHER_SAMPLE_TICKS ? 1.0f32 : (float32_t)since / (float32_t)GAME_WEATHER_SAMPLE_TICKS;
        weather->wind[0] = weather->wind_from[0] + (weather->wind_to[0] - weather->wind_from[0]) * share;
        weather->wind[1] = weather->wind_from[1] + (weather->wind_to[1] - weather->wind_from[1]) * share;
    }
    const bool sent = forced || (climate != nullptr && weather->sampled != 0u);
    if (sent) {
        weather_map_update(weather, game->borrowed_world, (int32_t)floor(eye[0]), (int32_t)floor(eye[1]), sample_tick);
    }
    game_weather_strike_t strike;
    const bool struck = sent && weather_strike(game, weather, climate, eye, t_days, &strike);

    render_weather_slot_t *const slot = &weather->slots[weather->exchange.back];
    memset(slot, 0, offsetof(render_weather_slot_t, map));
    slot->world_ticks = tick;
    slot->tick_ns = game->threads_running != 0u ? tick_ns : 0;
    slot->sent = sent ? 1u : 0u;
    slot->forced = forced ? 1u : 0u;
    slot->bolt_age = -1.0f32;
    if (sent) {
        const climate_now_t *const now = &weather->now;
        slot->rain_mm_h = now->rain_mm_h;
        slot->snow_share = now->snow_share;
        memcpy(slot->wind, weather->wind, sizeof(slot->wind));
        slot->cloud = now->cloud;
        slot->fog = now->fog;
        slot->dust = now->dust;
        memcpy(slot->map_origin, weather->map_origin, sizeof(slot->map_origin));
        slot->map_revision = weather->map_revision;
        memcpy(slot->map, weather->map, sizeof(slot->map));
        if (struck) {
            slot->flash = game_weather_flash(strike.distance);
            slot->bolt_age = strike.age;
            slot->bolt_distance = strike.distance;
            memcpy(slot->bolt, strike.bolt, sizeof(slot->bolt));
            memcpy(slot->bolt_origin, strike.origin, sizeof(slot->bolt_origin));
        }
    }
    (void)render_exchange_publish(&weather->exchange);
}

void game_weather_force(game_t *game, game_weather_kind_t kind) {
    game_weather_t *const weather = game_weather_of(game);
    if (weather == nullptr) {
        return;
    }
    weather->kind = (uint8_t)kind;
    weather->sampled = 0u;
}

void game_weather_wind(const game_t *game, float32_t out[2]) {
    const game_weather_t *const weather = game_weather_of(game);
    out[0] = weather != nullptr ? weather->wind[0] : 0.0f32;
    out[1] = weather != nullptr ? weather->wind[1] : 0.0f32;
}

/* The weather's slot taken and its map sent, then the frame drawn, or only queued (plan 008 phase 03). */
static gpu_status_t game_weather_frame(game_t *game, const render_camera_t *camera, int64_t now_ns, bool queue_only) {
    render_t *const render = game->borrowed_render;
    game_weather_t *const weather = game_weather_of(game);
    if (weather == nullptr) {
        return queue_only ? render_frame_queue(&render->frame, &render->buffers, camera, nullptr, nullptr)
                          : render_frame_draw(&render->frame, &render->buffers, camera);
    }
    uint32_t index = 0u;
    const render_weather_slot_t *const slot =
        render_exchange_take(&weather->exchange, &index) ? &weather->slots[index] : nullptr;
    const gpu_status_t status = render_weather_take_staged(&weather->gpu, slot, now_ns, render->borrowed_memory,
                                                           &render->frame.gpu.staging, &render->owned_weather_map);
    if (status != GPU_STATUS_OK) {
        render->frame.gpu.last_error = render->borrowed_memory->last_error;
        return status;
    }
    return queue_only
               ? render_frame_queue(&render->frame, &render->buffers, camera, &weather->gpu, &render->owned_weather_map)
               : render_frame_draw_weather(&render->frame, &render->buffers, camera, &weather->gpu,
                                           &render->owned_weather_map);
}

gpu_status_t game_weather_draw(game_t *game, const render_camera_t *camera, int64_t now_ns) {
    return game_weather_frame(game, camera, now_ns, false);
}

gpu_status_t game_weather_queue(game_t *game, const render_camera_t *camera, int64_t now_ns) {
    return game_weather_frame(game, camera, now_ns, true);
}
