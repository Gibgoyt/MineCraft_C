/* bench_climate_survey: plan 004 phase 11's four measurements over 8 seeds. See README.md.
 *
 * For each seed, a strip of the world from x = 0 to the strip's width (16384 blocks) and over a whole latitude cycle from
 * the south pole to the north pole, worked out through the climate functions alone (no chunks): the climate class of every
 * land cell and whether the terrain's biome disagrees with it; hurricanes a season in each warm coast square; thunderstorm
 * days a year by class at cells picked by hash; and how well the outlook's chances match what then happened, at places
 * picked by hash. The tables go to stdout, as Markdown, and are the same bytes on 1 worker or 16: every place is one job,
 * its counts its own, summed in a fixed order. Times go to stderr. */
#include "climate/index.h"
#include "climate/outlook/index.h"
#include "climate/systems/field/index.h"
#include "climate/systems/index.h"
#include "climate/weather/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "terrain/index.h"
#include "terrain/rng/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum : uint32_t {
    SURVEY_SEEDS_MAX = 8u,
    SURVEY_STRIP_BLOCKS = 16384u, /* the strip's width: one 16 km spawn square */
    SURVEY_STRIP_BLOCKS_MAX = 32768u,
    SURVEY_MARGIN_MAX_BLOCKS = 12288u,
    /* --margin at most this: strip, band and margins inside the 64 km keep square */ /* --width at most this */
    SURVEY_BAND_CELLS = 256u, /* rows worked out at a time: 16 km of latitude, inside the keep square */
    SURVEY_CELL_MARGIN_BLOCKS =
        1024u, /* tiles built round a band for phase A: one tile, past a read's 96-block reach */
    /* Tiles built round a band for phase C, by default: past the reach of a thunderstorm cell that can pass a place in the
     * day (4200 blocks: 4 hours at 15 blocks a second, and 3 R), a square's half (512) and a read's reach (96). --margin
     * changes it; a larger margin must print the same tables, which is how this one was checked. */
    SURVEY_PLACE_MARGIN_BLOCKS = 5120u,
    SURVEY_PLACES = 200u,          /* outlook places per seed */
    SURVEY_CELLS_PER_CLASS = 200u, /* thunderstorm cells per class per seed */
    SURVEY_YEARS = 10u,
    SURVEY_FIRST_WEEK = 12u,  /* as the phase 07 calibration test: the world's twelfth week on */
    SURVEY_BINS = 10u,        /* chances in tenths */
    SURVEY_KINDS = 5u,        /* rain, thunder, storm, hurricane, each named system */
    SURVEY_JUDGED_MIN = 100u, /* a bin with fewer forecasts is reported, not judged */
    SURVEY_SQUARES_MAX = 16u, /* hurricane squares a strip's column holds, pole to pole */
    SURVEY_SPAWNS_MAX = 32u,  /* hurricanes kept in one square on one day */
    SURVEY_WORKSPACE_BYTES = 64u << 20,
};
/* The 8 seeds of plan 003 phase 8 stage 1 (bench/world_map/README.md), Ahmed's first. */
static const uint64_t SURVEY_SEEDS[SURVEY_SEEDS_MAX] = {
    2198175988018515243ull, 12636146071196537873ull, 5569068473609503046ull, 1ull, 42ull, 20261104ull,
    9999999999999ull,       18446744073709551557ull};
static const char *const SURVEY_CLASS_NAMES[CLIMATE_CLASS_COUNT] = {
    "Sea",     "Ice",     "Tundra",        "Desert",    "Steppe",      "Rainforest",
    "Monsoon", "Savanna", "Mediterranean", "Temperate", "Continental", "Boreal"};
static const char *const SURVEY_KIND_NAMES[SURVEY_KINDS] = {"rain", "thunderstorms", "storms", "hurricanes",
                                                            "named systems"};
/* The plan's bound (phase 07): events given 60 to 80% happen 60 to 80% of the time. */
static const float64_t SURVEY_BIN_LOW = 0.6f64;
static const float64_t SURVEY_BIN_HIGH = 0.8f64;
static const float64_t SURVEY_RAIN_TOLERANCE = 0.15f64; /* 7.4: long-run rain within 15% of the normal */
static const float64_t SURVEY_PLAN_PER_SQUARE = 2.4f64; /* 7.2's worked example */
static const float64_t SURVEY_PLAN_PER_COAST = 7.0f64;
/* The salt of the hash that picks cells, apart from every other use of the seed. Both picks use it. */
static const int32_t SURVEY_SALT_PLACES = 0x0CA1;

/* One cell of the strip, from phase A. */
typedef struct survey_cell_t {
    uint64_t hash;    /* 0: picks cells */
    uint8_t klass;    /* 8: CLIMATE_CLASS_*; NONE for sea and large lakes */
    uint8_t flags;    /* 9 */
    uint8_t current;  /* 10 */
    uint8_t biome;    /* 11 */
    uint8_t warm_sea; /* 12: a sea cell with a warm current and a hurricane season */
    uint8_t _pad0[3]; /* 13 */
} survey_cell_t;
static_assert(sizeof(survey_cell_t) == 16);

typedef struct survey_strip_t {
    survey_cell_t *borrowed_cells; /* 0: rows south to north, x fastest */
    terrain_t *borrowed_terrain;   /* 8 */
    climate_t *borrowed_climate;   /* 16 */
    int32_t row_min;               /* 24: the cell row of the southernmost row */
    uint32_t columns;              /* 28 */
    uint32_t rows;                 /* 32 */
    uint8_t _pad0[4];              /* 36 */
} survey_strip_t;
static_assert(sizeof(survey_strip_t) == 40);

/* Phase A's job: one row of the strip. */
typedef struct survey_row_job_t {
    const survey_strip_t *borrowed_strip; /* 0 */
    uint64_t seed;                        /* 8 */
    uint32_t row;                         /* 16 */
    uint32_t missing;                     /* 20: cells whose tile was not built: a bug */
} survey_row_job_t;
static_assert(sizeof(survey_row_job_t) == 24);

/* A place's 10 years: thunder days and rain, or the outlook's pairs. */
typedef struct survey_place_job_t {
    const climate_t *borrowed_climate;               /* 0 */
    climate_outlook_input_t *borrowed_input;         /* 8: calibration jobs only */
    float64_t x;                                     /* 16 */
    float64_t y;                                     /* 24 */
    float64_t rain_mm;                               /* 32: thunder jobs: every day's rain summed */
    float64_t normal_mm_year;                        /* 40 */
    uint64_t bins[SURVEY_KINDS][SURVEY_BINS][2];     /* 48: calibration: pairs, and how many happened */
    float64_t chance_sum[SURVEY_KINDS][SURVEY_BINS]; /* 848: for each bin's mean chance */
    uint32_t thunder_days;                           /* 1248 */
    uint32_t failures;                               /* 1252 */
    uint32_t years;                                  /* 1256 */
    uint8_t klass;                                   /* 1260 */
    uint8_t calibration;                             /* 1261: 1 for an outlook place, 0 for a thunder cell */
    uint8_t _pad0[2];                                /* 1262 */
} survey_place_job_t;
static_assert(sizeof(survey_place_job_t) == 1264);

/* A warm coast square's years: hurricanes spawned in it, and how many made landfall. */
typedef struct survey_square_job_t {
    const climate_t *borrowed_climate; /* 0 */
    int32_t square_x;                  /* 8 */
    int32_t square_y;                  /* 12 */
    uint32_t year;                     /* 16 */
    uint32_t spawned;                  /* 20 */
    uint32_t landfall;                 /* 24 */
    uint32_t overflowed;               /* 28 */
} survey_square_job_t;
static_assert(sizeof(survey_square_job_t) == 32);

typedef struct survey_options_t {
    const char *record; /* 0: --record: the TSV to add a line to */
    uint32_t seeds;     /* 8 */
    uint32_t years;     /* 12 */
    uint32_t workers;   /* 16 */
    uint32_t width;     /* 20: blocks */
    uint32_t places;    /* 24 */
    uint32_t per_class; /* 28 */
    uint32_t margin;    /* 32: blocks of tiles built round a band for the places */
    uint32_t first;     /* 36: --first: the index of the first seed of the list */
} survey_options_t;
static_assert(sizeof(survey_options_t) == 40);

/* Everything summed over the seeds, in seed order. */
typedef struct survey_totals_t {
    uint64_t class_cells[SURVEY_SEEDS_MAX][CLIMATE_CLASS_COUNT];     /* 0 */
    uint64_t class_disagree[SURVEY_SEEDS_MAX][CLIMATE_CLASS_COUNT];  /* 768 */
    uint64_t land_cells[SURVEY_SEEDS_MAX];                           /* 1536 */
    uint64_t bins[SURVEY_KINDS][SURVEY_BINS][2];                     /* 1600 */
    float64_t chance_sum[SURVEY_KINDS][SURVEY_BINS];                 /* 2400 */
    float64_t square_seasons[SURVEY_SEEDS_MAX * SURVEY_SQUARES_MAX]; /* 2800: hurricanes a season, each warm square */
    float64_t coast_seasons[SURVEY_SEEDS_MAX * SURVEY_SQUARES_MAX];  /* 3824 */
    float64_t coast_landfall[SURVEY_SEEDS_MAX * SURVEY_SQUARES_MAX]; /* 4848 */
    float64_t thunder_sum[CLIMATE_CLASS_COUNT];                      /* 5872 */
    float64_t thunder_min[CLIMATE_CLASS_COUNT];                      /* 5968 */
    float64_t thunder_max[CLIMATE_CLASS_COUNT];                      /* 6064 */
    uint64_t thunder_cells[CLIMATE_CLASS_COUNT];                     /* 6160 */
    uint64_t thunder_short[CLIMATE_CLASS_COUNT];                     /* 6256: seeds with fewer cells than asked */
    float64_t rain_worst;                                            /* 6352 */
    uint64_t rain_cells;                                             /* 6360 */
    uint64_t rain_within;                                            /* 6368 */
    uint64_t failures;                                               /* 6376 */
    uint32_t squares;                                                /* 6384 */
    uint32_t coasts;                                                 /* 6388 */
    uint32_t band_squares;                                           /* 6392: squares in the hurricane band at all */
    uint32_t missing;                                                /* 6396 */
} survey_totals_t;
static_assert(sizeof(survey_totals_t) == 6400);

/* ---- Phase A: every cell of the strip ---------------------------------------------------------------------------------- */

static bool survey_desert_biome(uint8_t biome) {
    return biome == BIOME_ID_DESERT || biome == BIOME_ID_BADLANDS || biome == BIOME_ID_ERODED_BADLANDS ||
           biome == BIOME_ID_WOODED_BADLANDS;
}

static void survey_row(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    survey_row_job_t *const job = borrowed_argument;
    const survey_strip_t *const strip = job->borrowed_strip;
    const int32_t j = strip->row_min + (int32_t)job->row;
    const int32_t y = j * CLIMATE_CELL_BLOCKS + CLIMATE_HALF_CELL_BLOCKS;
    for (uint32_t i = 0u; i < strip->columns; i++) {
        const int32_t x = (int32_t)i * CLIMATE_CELL_BLOCKS + CLIMATE_HALF_CELL_BLOCKS;
        survey_cell_t *const out = &strip->borrowed_cells[(uint64_t)job->row * strip->columns + i];
        memset(out, 0, sizeof(*out));
        out->hash = terrain_rng_hash3(job->seed, (int32_t)i, j, SURVEY_SALT_PLACES);
        climate_cell_t cell;
        if (!climate_cell_get(strip->borrowed_climate, x, y, &cell)) {
            job->missing++;
            continue;
        }
        const bool water = (cell.flags & (CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE)) != 0u;
        out->klass = water ? (uint8_t)CLIMATE_CLASS_NONE : cell.climate_class;
        out->flags = cell.flags;
        out->current = cell.current;
        out->warm_sea = (uint8_t)((cell.flags & CLIMATE_CELL_SEA) != 0u && cell.current == CLIMATE_CURRENT_WARM &&
                                  cell.hurricane_days[0] != CLIMATE_DAY_NONE);
        if (!water) {
            out->biome = terrain_biome_column_at(strip->borrowed_terrain->borrowed_biome, x, y).biome;
        }
    }
}

/* ---- Hurricanes in a warm coast square ------------------------------------------------------------------------------- */

static void survey_square(job_worker_t *worker, void *borrowed_argument) {
    survey_square_job_t *const job = borrowed_argument;
    arena_t *const arena = &worker->arena_job;
    const uint64_t first =
        (uint64_t)SURVEY_FIRST_WEEK * CALENDAR_DAYS_PER_WEEK + (uint64_t)job->year * CALENDAR_DAYS_PER_YEAR;
    for (uint64_t d = first; d < first + CALENDAR_DAYS_PER_YEAR; d++) {
        arena_reset(arena);
        climate_systems_mask_t mask;
        climate_systems_mask_start(&mask, job->borrowed_climate, arena, nullptr);
        climate_system_t *const spawns =
            arena_alloc_array(arena, SURVEY_SPAWNS_MAX, sizeof(climate_system_t), alignof(climate_system_t));
        bool overflowed = false;
        const uint32_t n =
            climate_systems_square_spawns(job->borrowed_climate, &mask, CLIMATE_SYSTEM_HURRICANE, job->square_x,
                                          job->square_y, (int64_t)d, spawns, SURVEY_SPAWNS_MAX, &overflowed);
        job->overflowed += overflowed ? 1u : 0u;
        for (uint32_t k = 0u; k < n; k++) {
            const climate_system_t *const system = &spawns[k];
            job->spawned++;
            /* Landfall: a track sample over land while the storm is of hurricane strength. */
            for (uint32_t s = 0u; s < system->sample_count; s++) {
                if (system->samples[s].over_land == 0u) {
                    continue;
                }
                climate_system_state_t state;
                climate_system_state_at(system, system->spawn_day + CLIMATE_TRACK_STEP_DAYS * (float64_t)s, &state);
                if (state.alive != 0u && state.wind_max >= CLIMATE_HURRICANE_STORM_VMAX) {
                    job->landfall++;
                    break;
                }
            }
        }
    }
}

/* ---- A place's 10 years ---------------------------------------------------------------------------------------------- */

static void survey_note(survey_place_job_t *job, uint32_t kind, float64_t p, bool happened) {
    uint32_t bin = (uint32_t)(p * (float64_t)SURVEY_BINS);
    bin = bin >= SURVEY_BINS ? SURVEY_BINS - 1u : bin;
    job->bins[kind][bin][0]++;
    job->bins[kind][bin][1] += happened ? 1u : 0u;
    job->chance_sum[kind][bin] += p;
}

static bool survey_covers(const climate_system_t *system, float64_t t, float64_t x, float64_t y) {
    climate_system_state_t state;
    climate_system_state_at(system, t, &state);
    const float64_t dx = x - state.x, dy = y - state.y;
    return state.alive != 0u && dx * dx + dy * dy < (float64_t)system->radius * (float64_t)system->radius;
}

/* Thunder days and rain, day by day. */
static void survey_thunder_place(survey_place_job_t *job, arena_t *arena) {
    const climate_t *const climate = job->borrowed_climate;
    climate_normals_at_t normals;
    arena_reset(arena);
    if (!climate_normals_at(climate, job->x, job->y, nanf(""), 0.0f32, &normals)) {
        job->failures++;
        return;
    }
    job->normal_mm_year = (float64_t)normals.rain_mm_year;
    const int64_t first = (int64_t)SURVEY_FIRST_WEEK * (int64_t)CALENDAR_DAYS_PER_WEEK;
    const int64_t days = (int64_t)job->years * (int64_t)CALENDAR_DAYS_PER_YEAR;
    for (int64_t d = first; d < first + days; d++) {
        arena_reset(arena);
        climate_day_t weather;
        if (!climate_weather_day(climate, (int32_t)job->x, (int32_t)job->y, d, nullptr, arena, &weather)) {
            job->failures++;
            continue;
        }
        job->thunder_days += weather.kind == CLIMATE_DAY_THUNDERSTORM ? 1u : 0u;
        job->rain_mm += (float64_t)weather.rain_mm;
    }
}

/* The outlook at every census, then what really happened: the phase 07 calibration test's rules, at this place. */
static void survey_outlook_place(survey_place_job_t *job, arena_t *arena) {
    const climate_t *const climate = job->borrowed_climate;
    const uint32_t censuses = job->years * (CALENDAR_DAYS_PER_YEAR / CALENDAR_DAYS_PER_WEEK);
    for (uint32_t c = 0u; c < censuses; c++) {
        const uint64_t week = SURVEY_FIRST_WEEK + c;
        const float64_t census_t = (float64_t)CALENDAR_DAYS_PER_WEEK * (float64_t)week;
        arena_reset(arena);
        climate_outlook_t out;
        if (!climate_outlook_input_fill(climate, job->x, job->y, census_t, week, arena, job->borrowed_input)) {
            job->failures++;
            continue;
        }
        climate_outlook_run(job->borrowed_input, &out);
        for (uint32_t d = 0u; d < CLIMATE_OUTLOOK_DAYS; d++) {
            const float64_t instant = census_t + (float64_t)(d + 1u);
            const int64_t day = (int64_t)census_t + (int64_t)d; /* the calendar day ending at the instant */
            arena_reset(arena);
            climate_day_t weather;
            climate_systems_day_t cells, big;
            if (!climate_weather_day(climate, (int32_t)job->x, (int32_t)job->y, day, nullptr, arena, &weather) ||
                !climate_systems_day_build(climate, arena, job->x, job->y, 1.0f32, 1.0f32, day, CLIMATE_SYSTEMS_ALL,
                                           &cells) ||
                !climate_systems_day_build(climate, arena, job->x, job->y, 0.0f32, 0.0f32, (int64_t)floor(instant),
                                           CLIMATE_SYSTEMS_BIG, &big)) {
                job->failures++;
                continue;
            }
            bool thunder = false;
            for (uint32_t h = 0u; h < CLIMATE_HOURS_A_DAY && !thunder; h++) {
                const float64_t at = (float64_t)day + ((float64_t)h + 0.5f64) / CALENDAR_HOURS_PER_DAY_F64;
                for (uint32_t i = 0u; i < cells.thunder_count && !thunder; i++) {
                    float64_t centre[2], share = 0.0f64;
                    const climate_thunder_t *const cell = &cells.borrowed_thunder[i];
                    if (climate_systems_thunder_at(cell, at, centre, &share)) {
                        const float64_t dx = job->x - centre[0], dy = job->y - centre[1];
                        thunder = dx * dx + dy * dy < (float64_t)cell->radius * (float64_t)cell->radius;
                    }
                }
            }
            bool storm = false, hurricane = false;
            for (uint32_t i = 0u; i < big.system_count; i++) {
                const climate_system_t *const system = &big.borrowed_systems[i];
                if (system->kind == CLIMATE_SYSTEM_HIGH || !survey_covers(system, instant, job->x, job->y)) {
                    continue;
                }
                climate_system_state_t state;
                climate_system_state_at(system, instant, &state);
                hurricane = hurricane || system->kind == CLIMATE_SYSTEM_HURRICANE;
                storm =
                    storm || system->kind == CLIMATE_SYSTEM_HURRICANE || state.pressure_hpa <= -CLIMATE_STORM_DEPTH_HPA;
            }
            survey_note(job, 0u, (float64_t)out.days[d].rain, weather.rain_mm > CLIMATE_DAY_RAIN_MM);
            survey_note(job, 1u, (float64_t)out.days[d].thunder, thunder);
            survey_note(job, 2u, (float64_t)out.days[d].storm, storm);
            survey_note(job, 3u, (float64_t)out.days[d].hurricane, hurricane);
            for (uint32_t s = 0u; s < out.system_count; s++) {
                bool came = false;
                for (uint32_t i = 0u; i < big.system_count; i++) {
                    const climate_system_t *const system = &big.borrowed_systems[i];
                    const uint32_t folded = (uint32_t)(system->id ^ (system->id >> CLIMATE_KEY_HALF_BITS));
                    came = came || (folded == out.systems[s].id && survey_covers(system, instant, job->x, job->y));
                }
                survey_note(job, 4u, (float64_t)out.systems[s].chance[d], came);
            }
        }
    }
}

static void survey_place(job_worker_t *worker, void *borrowed_argument) {
    survey_place_job_t *const job = borrowed_argument;
    if (job->calibration != 0u) {
        survey_outlook_place(job, &worker->arena_job);
    } else {
        survey_thunder_place(job, &worker->arena_job);
    }
}

/* ---- One seed -------------------------------------------------------------------------------------------------------- */

/* The cells of a band of rows, both ends of the strip and a margin round it, all built. */
static void survey_prepare(climate_t *climate, job_system_t *job, const survey_strip_t *strip, uint32_t row0,
                           uint32_t row1, uint32_t margin) {
    const int32_t y0 = (strip->row_min + (int32_t)row0) * CLIMATE_CELL_BLOCKS - (int32_t)margin;
    const int32_t y1 = (strip->row_min + (int32_t)row1) * CLIMATE_CELL_BLOCKS + (int32_t)margin;
    const int32_t x1 = (int32_t)strip->columns * CLIMATE_CELL_BLOCKS + (int32_t)margin;
    climate_prepare_area(climate, job, -(int32_t)margin, y0, x1, y1, nullptr, nullptr);
}

/* By hash, then by place in the strip, so the order is the same whatever qsort does with ties. */
static int32_t survey_hash_compare(const void *a, const void *b) {
    const survey_cell_t *const p = *(const survey_cell_t *const *)a, *const q = *(const survey_cell_t *const *)b;
    if (p->hash != q->hash) {
        return p->hash < q->hash ? -1 : 1;
    }
    return p < q ? -1 : (p > q ? 1 : 0);
}

typedef struct survey_seed_work_t {
    survey_row_job_t *rows;          /* 0 */
    survey_place_job_t *places;      /* 8 */
    survey_cell_t **order;           /* 16 */
    climate_outlook_input_t *inputs; /* 24 */
    uint32_t place_count;            /* 32 */
    uint8_t _pad0[4];                /* 36 */
} survey_seed_work_t;
static_assert(sizeof(survey_seed_work_t) == 40);

static void survey_run_jobs(job_system_t *job, job_function_t function, void *jobs, uint64_t stride, uint32_t count) {
    job_group_t group;
    job_group_init(&group);
    for (uint32_t k = 0u; k < count; k++) {
        job_submit(job, &group, function, (uint8_t *)jobs + (uint64_t)k * stride);
    }
    job_group_wait(job, &group);
    job_group_deinit(&group);
}

/* Picks up to want cells by the smallest hash among those that keep says yes to, marking each with mark. */
static uint32_t survey_pick(survey_cell_t **order, uint32_t total, uint8_t klass, bool any_land, uint32_t want,
                            survey_cell_t **out) {
    uint32_t n = 0u;
    for (uint32_t k = 0u; k < total && n < want; k++) {
        const survey_cell_t *const cell = order[k];
        const bool land = cell->klass != CLIMATE_CLASS_NONE;
        if ((any_land && land) || (!any_land && cell->klass == klass)) {
            out[n++] = order[k];
        }
    }
    return n;
}

static void survey_seed(uint32_t s, uint64_t seed, const survey_options_t *options, job_system_t *job,
                        climate_t *climate, terrain_t *terrain, survey_strip_t *strip, survey_seed_work_t *work,
                        survey_totals_t *totals) {
    const int64_t started = time_now_ns();
    if (!terrain_init(terrain, seed) || !climate_open(climate, terrain)) {
        fprintf(stderr, "bench_climate_survey: seed %" PRIu64 " would not open\n", seed);
        totals->failures++;
        return;
    }
    strip->borrowed_terrain = terrain;
    const uint32_t cells = strip->columns * strip->rows;
    /* Phase A: every cell, a band at a time. */
    for (uint32_t row0 = 0u; row0 < strip->rows; row0 += SURVEY_BAND_CELLS) {
        const uint32_t row1 = row0 + SURVEY_BAND_CELLS < strip->rows ? row0 + SURVEY_BAND_CELLS : strip->rows;
        survey_prepare(climate, job, strip, row0, row1, SURVEY_CELL_MARGIN_BLOCKS);
        for (uint32_t r = row0; r < row1; r++) {
            work->rows[r] = (survey_row_job_t){.borrowed_strip = strip, .seed = seed, .row = r};
        }
        survey_run_jobs(job, survey_row, work->rows + row0, sizeof(survey_row_job_t), row1 - row0);
    }
    for (uint32_t r = 0u; r < strip->rows; r++) {
        totals->missing += work->rows[r].missing;
    }
    for (uint32_t c = 0u; c < cells; c++) {
        const survey_cell_t *const cell = &strip->borrowed_cells[c];
        if (cell->klass == CLIMATE_CLASS_NONE) {
            continue;
        }
        totals->land_cells[s]++;
        totals->class_cells[s][cell->klass]++;
        const bool dry = cell->klass == CLIMATE_CLASS_DESERT || cell->klass == CLIMATE_CLASS_STEPPE;
        totals->class_disagree[s][cell->klass] += survey_desert_biome(cell->biome) != dry ? 1u : 0u;
    }
    const float64_t phase_a = time_ns_to_ms(time_now_ns() - started) / 1000.0f64;

    /* Hurricanes: the strip's column of 16 km squares, south to north. */
    const int32_t square_x = 0;
    const int32_t sq_min = (int32_t)floor((float64_t)(strip->row_min * CLIMATE_CELL_BLOCKS) / CLIMATE_SQUARE_HURRICANE);
    const int32_t sq_max = (int32_t)floor(
        (float64_t)((strip->row_min + (int32_t)strip->rows) * CLIMATE_CELL_BLOCKS - 1) / CLIMATE_SQUARE_HURRICANE);
    bool warm[SURVEY_SQUARES_MAX] = {false};
    uint32_t warm_count = 0u;
    for (int32_t sq = sq_min; sq <= sq_max && sq - sq_min < (int32_t)SURVEY_SQUARES_MAX; sq++) {
        const float64_t lo = (float64_t)sq * CLIMATE_SQUARE_HURRICANE, hi = lo + CLIMATE_SQUARE_HURRICANE;
        /* Between the poles latitude rises with y, so the square spans phi(lo) to phi(hi). */
        const float64_t phi_lo = climate_latitude_deg(lo), phi_hi = climate_latitude_deg(hi);
        const float64_t band_lo = (float64_t)CLIMATE_HURRICANE_LAT_MIN_DEG,
                        band_hi = (float64_t)CLIMATE_HURRICANE_LAT_MAX_DEG;
        const bool band = (phi_hi >= band_lo && phi_lo <= band_hi) || (phi_hi >= -band_hi && phi_lo <= -band_lo);
        totals->band_squares += band ? 1u : 0u;
        for (uint32_t c = 0u; c < cells && !warm[sq - sq_min]; c++) {
            const int32_t j = strip->row_min + (int32_t)(c / strip->columns);
            const float64_t y = (float64_t)(j * CLIMATE_CELL_BLOCKS + CLIMATE_HALF_CELL_BLOCKS);
            warm[sq - sq_min] = strip->borrowed_cells[c].warm_sea != 0u && y >= lo && y < hi;
        }
        warm_count += warm[sq - sq_min] ? 1u : 0u;
    }
    survey_square_job_t *const squares = (survey_square_job_t *)work->places; /* the place jobs' room, before them */
    uint32_t square_jobs = 0u;
    for (int32_t sq = sq_min; sq <= sq_max && sq - sq_min < (int32_t)SURVEY_SQUARES_MAX; sq++) {
        for (uint32_t year = 0u; warm[sq - sq_min] && year < options->years; year++) {
            squares[square_jobs++] =
                (survey_square_job_t){.borrowed_climate = climate, .square_x = square_x, .square_y = sq, .year = year};
        }
    }
    survey_run_jobs(job, survey_square, squares, sizeof(survey_square_job_t), square_jobs);
    {
        uint32_t k = 0u;
        float64_t coast = 0.0f64, coast_landfall = 0.0f64;
        bool in_coast = false;
        for (int32_t sq = sq_min; sq <= sq_max + 1; sq++) {
            const bool is_warm = sq <= sq_max && sq - sq_min < (int32_t)SURVEY_SQUARES_MAX && warm[sq - sq_min];
            if (is_warm) {
                uint32_t spawned = 0u, landfall = 0u;
                for (uint32_t year = 0u; year < options->years; year++, k++) {
                    spawned += squares[k].spawned;
                    landfall += squares[k].landfall;
                    totals->failures += squares[k].overflowed;
                }
                const float64_t per_season = (float64_t)spawned / (float64_t)options->years;
                totals->square_seasons[totals->squares++] = per_season;
                coast += per_season;
                coast_landfall += (float64_t)landfall / (float64_t)options->years;
                in_coast = true;
            } else if (in_coast) {
                totals->coast_landfall[totals->coasts] = coast_landfall;
                totals->coast_seasons[totals->coasts++] = coast;
                coast = 0.0f64;
                coast_landfall = 0.0f64;
                in_coast = false;
            }
        }
    }
    const float64_t phase_h = time_ns_to_ms(time_now_ns() - started) / 1000.0f64;

    /* Picks: cells in hash order, the 200 smallest of each class for thunder, and of all land for the outlook. */
    for (uint32_t c = 0u; c < cells; c++) {
        work->order[c] = &strip->borrowed_cells[c];
    }
    qsort(work->order, cells, sizeof(work->order[0]), survey_hash_compare);
    survey_cell_t **const picked = work->order + cells;
    uint32_t n_jobs = 0u;
    for (uint8_t klass = CLIMATE_CLASS_ICE; klass < CLIMATE_CLASS_COUNT; klass++) {
        const uint32_t n = survey_pick(work->order, cells, klass, false, options->per_class, picked);
        totals->thunder_short[klass] += n < options->per_class ? 1u : 0u;
        for (uint32_t k = 0u; k < n; k++, n_jobs++) {
            const uint64_t index = (uint64_t)(picked[k] - strip->borrowed_cells);
            work->places[n_jobs] = (survey_place_job_t){
                .borrowed_climate = climate,
                .x = (float64_t)((int32_t)(index % strip->columns) * CLIMATE_CELL_BLOCKS + CLIMATE_HALF_CELL_BLOCKS),
                .y = (float64_t)((strip->row_min + (int32_t)(index / strip->columns)) * CLIMATE_CELL_BLOCKS +
                                 CLIMATE_HALF_CELL_BLOCKS),
                .years = options->years,
                .klass = klass,
            };
        }
    }
    const uint32_t n_places = survey_pick(work->order, cells, 0u, true, options->places, picked);
    for (uint32_t k = 0u; k < n_places; k++, n_jobs++) {
        const uint64_t index = (uint64_t)(picked[k] - strip->borrowed_cells);
        work->places[n_jobs] = (survey_place_job_t){
            .borrowed_climate = climate,
            .borrowed_input = &work->inputs[k],
            .x = (float64_t)((int32_t)(index % strip->columns) * CLIMATE_CELL_BLOCKS + CLIMATE_HALF_CELL_BLOCKS),
            .y = (float64_t)((strip->row_min + (int32_t)(index / strip->columns)) * CLIMATE_CELL_BLOCKS +
                             CLIMATE_HALF_CELL_BLOCKS),
            .years = options->years,
            .klass = picked[k]->klass,
            .calibration = 1u,
        };
    }
    /* Phase C: the places, a band at a time, each band's tiles built first. */
    survey_place_job_t **const band_jobs = (survey_place_job_t **)picked;
    for (uint32_t row0 = 0u; row0 < strip->rows; row0 += SURVEY_BAND_CELLS) {
        const uint32_t row1 = row0 + SURVEY_BAND_CELLS < strip->rows ? row0 + SURVEY_BAND_CELLS : strip->rows;
        const float64_t y_lo = (float64_t)((strip->row_min + (int32_t)row0) * CLIMATE_CELL_BLOCKS);
        const float64_t y_hi = (float64_t)((strip->row_min + (int32_t)row1) * CLIMATE_CELL_BLOCKS);
        uint32_t n = 0u;
        for (uint32_t k = 0u; k < n_jobs; k++) {
            if (work->places[k].y >= y_lo && work->places[k].y < y_hi) {
                band_jobs[n++] = &work->places[k];
            }
        }
        if (n == 0u) {
            continue;
        }
        survey_prepare(climate, job, strip, row0, row1, options->margin);
        job_group_t group;
        job_group_init(&group);
        /* The outlook places, the longest jobs, are last in the array: queued first, so no worker waits on one at the end. */
        for (uint32_t k = n; k > 0u; k--) {
            job_submit(job, &group, survey_place, band_jobs[k - 1u]);
        }
        job_group_wait(job, &group);
        job_group_deinit(&group);
    }
    /* Sums, in job order. */
    for (uint8_t klass = 0u; klass < CLIMATE_CLASS_COUNT; klass++) {
        totals->thunder_min[klass] = s == 0u ? INFINITY : totals->thunder_min[klass];
        totals->thunder_max[klass] = s == 0u ? -INFINITY : totals->thunder_max[klass];
    }
    for (uint32_t k = 0u; k < n_jobs; k++) {
        const survey_place_job_t *const place = &work->places[k];
        totals->failures += place->failures;
        if (place->calibration != 0u) {
            for (uint32_t kind = 0u; kind < SURVEY_KINDS; kind++) {
                for (uint32_t b = 0u; b < SURVEY_BINS; b++) {
                    totals->bins[kind][b][0] += place->bins[kind][b][0];
                    totals->bins[kind][b][1] += place->bins[kind][b][1];
                    totals->chance_sum[kind][b] += place->chance_sum[kind][b];
                }
            }
            continue;
        }
        const float64_t days_a_year = (float64_t)place->thunder_days / (float64_t)place->years;
        totals->thunder_sum[place->klass] += days_a_year;
        totals->thunder_min[place->klass] = fmin(totals->thunder_min[place->klass], days_a_year);
        totals->thunder_max[place->klass] = fmax(totals->thunder_max[place->klass], days_a_year);
        totals->thunder_cells[place->klass]++;
        if (place->normal_mm_year > 0.0f64) {
            const float64_t error =
                fabs(place->rain_mm / (float64_t)place->years - place->normal_mm_year) / place->normal_mm_year;
            totals->rain_worst = fmax(totals->rain_worst, error);
            totals->rain_cells++;
            totals->rain_within += error <= SURVEY_RAIN_TOLERANCE ? 1u : 0u;
        }
    }
    work->place_count = n_jobs;
    climate_close(climate, job);
    terrain_deinit(terrain);
    fprintf(stderr,
            "seed %" PRIu64 ": %u cells (%" PRIu64 " land), %u warm coast squares, %u thunder cells and %u places; "
            "cells %.1f s, hurricanes %.1f s, all %.1f s\n",
            seed, cells, totals->land_cells[s], warm_count, n_jobs - n_places, n_places, (libc_double_t)phase_a,
            (libc_double_t)(phase_h - phase_a), (libc_double_t)(time_ns_to_ms(time_now_ns() - started) / 1000.0f64));
}

/* ---- The tables ------------------------------------------------------------------------------------------------------ */

static void survey_range(const float64_t *values, uint32_t count, float64_t *mean, float64_t *lo, float64_t *hi) {
    float64_t sum = 0.0f64;
    *lo = count > 0u ? INFINITY : 0.0f64;
    *hi = count > 0u ? -INFINITY : 0.0f64;
    for (uint32_t k = 0u; k < count; k++) {
        sum += values[k];
        *lo = fmin(*lo, values[k]);
        *hi = fmax(*hi, values[k]);
    }
    *mean = count > 0u ? sum / (float64_t)count : 0.0f64;
}

static float64_t survey_two_se(uint64_t happened, uint64_t n) {
    if (n == 0u) {
        return 0.0f64;
    }
    const float64_t f = (float64_t)happened / (float64_t)n;
    return 2.0f64 * sqrt(f * (1.0f64 - f) / (float64_t)n);
}

/* One calibration row: the bins from first to last (inclusive) of one kind, or all kinds if kind is SURVEY_KINDS. */
static void survey_bin_row(const survey_totals_t *totals, const char *label, uint32_t kind, uint32_t first,
                           uint32_t last, uint64_t *out_n, uint64_t *out_happened) {
    uint64_t n = 0u, happened = 0u;
    float64_t chance = 0.0f64;
    for (uint32_t k = kind == SURVEY_KINDS ? 0u : kind; k <= (kind == SURVEY_KINDS ? SURVEY_KINDS - 1u : kind); k++) {
        for (uint32_t b = first; b <= last; b++) {
            n += totals->bins[k][b][0];
            happened += totals->bins[k][b][1];
            chance += totals->chance_sum[k][b];
        }
    }
    const float64_t share = n > 0u ? (float64_t)happened / (float64_t)n : 0.0f64;
    printf("| %s | %" PRIu64 " | %.1f%% | %.1f%% | %.1f%% | %s |\n", label, n,
           (libc_double_t)(n > 0u ? 100.0f64 * chance / (float64_t)n : 0.0f64), (libc_double_t)(100.0f64 * share),
           (libc_double_t)(100.0f64 * survey_two_se(happened, n)),
           n < SURVEY_JUDGED_MIN ? "not judged (under 100)" : "");
    *out_n = n;
    *out_happened = happened;
}

static void survey_print(const survey_totals_t *totals, const survey_options_t *options, float64_t *out_middle) {
    printf("## bench_climate_survey: %u seeds, %u years, strip %u blocks wide, pole to pole\n\n", options->seeds,
           options->years, options->width);
    printf("**Seeds used:**");
    for (uint32_t s = 0u; s < options->seeds; s++) {
        printf(" `%" PRIu64 "`%s", SURVEY_SEEDS[options->first + s], s + 1u < options->seeds ? "," : "\n\n");
    }
    printf("Land cells a seed:");
    for (uint32_t s = 0u; s < options->seeds; s++) {
        printf(" %" PRIu64 "%s", totals->land_cells[s], s + 1u < options->seeds ? "," : "");
    }
    printf(". Cells whose tile was not built: %u. Days or censuses that could not be worked out: %" PRIu64 ".\n\n",
           totals->missing, totals->failures);

    printf("**Climate class shares,** %% of land, over the seeds:\n\n");
    printf("| Class | Mean | Smallest | Largest | Biome disagrees, %% |\n|---|---|---|---|---|\n");
    for (uint32_t c = CLIMATE_CLASS_ICE; c < CLIMATE_CLASS_COUNT; c++) {
        float64_t shares[SURVEY_SEEDS_MAX];
        uint64_t cells = 0u, disagree = 0u;
        for (uint32_t s = 0u; s < options->seeds; s++) {
            shares[s] = totals->land_cells[s] > 0u
                            ? 100.0f64 * (float64_t)totals->class_cells[s][c] / (float64_t)totals->land_cells[s]
                            : 0.0f64;
            cells += totals->class_cells[s][c];
            disagree += totals->class_disagree[s][c];
        }
        float64_t mean, lo, hi;
        survey_range(shares, options->seeds, &mean, &lo, &hi);
        printf("| %s | %.2f | %.2f | %.2f | %.1f |\n", SURVEY_CLASS_NAMES[c], (libc_double_t)mean, (libc_double_t)lo,
               (libc_double_t)hi,
               (libc_double_t)(cells > 0u ? 100.0f64 * (float64_t)disagree / (float64_t)cells : 0.0f64));
    }
    printf("\nBiome disagrees: a desert or badlands biome in a class that is not Desert or Steppe, or another biome in "
           "Desert or Steppe; pooled over the seeds.\n\n");

    float64_t mean, lo, hi;
    printf(
        "**Hurricanes,** %u warm coast squares and %u coasts over the seeds (%u squares reach the hurricane band):\n\n",
        totals->squares, totals->coasts, totals->band_squares);
    printf("| Measure | Plan | Mean | Smallest | Largest |\n|---|---|---|---|---|\n");
    survey_range(totals->square_seasons, totals->squares, &mean, &lo, &hi);
    printf("| Per warm coast square, a season | %.1f | %.2f | %.2f | %.2f |\n", (libc_double_t)SURVEY_PLAN_PER_SQUARE,
           (libc_double_t)mean, (libc_double_t)lo, (libc_double_t)hi);
    survey_range(totals->coast_seasons, totals->coasts, &mean, &lo, &hi);
    printf("| Per warm coast, a season | about %.0f (three squares) | %.2f | %.2f | %.2f |\n",
           (libc_double_t)SURVEY_PLAN_PER_COAST, (libc_double_t)mean, (libc_double_t)lo, (libc_double_t)hi);
    survey_range(totals->coast_landfall, totals->coasts, &mean, &lo, &hi);
    printf("| Making landfall, a season per coast | - | %.2f | %.2f | %.2f |\n\n", (libc_double_t)mean,
           (libc_double_t)lo, (libc_double_t)hi);

    printf("**Thunderstorm days a year,** at up to %u land cells per class per seed, picked by hash:\n\n",
           options->per_class);
    printf("| Class | Cells | Mean | Smallest | Largest |\n|---|---|---|---|---|\n");
    for (uint32_t c = CLIMATE_CLASS_ICE; c < CLIMATE_CLASS_COUNT; c++) {
        const uint64_t n = totals->thunder_cells[c];
        printf("| %s | %" PRIu64 "%s | %.2f | %.2f | %.2f |\n", SURVEY_CLASS_NAMES[c], n,
               totals->thunder_short[c] > 0u ? " (fewer on some seeds)" : "",
               (libc_double_t)(n > 0u ? totals->thunder_sum[c] / (float64_t)n : 0.0f64),
               (libc_double_t)(n > 0u ? totals->thunder_min[c] : 0.0f64),
               (libc_double_t)(n > 0u ? totals->thunder_max[c] : 0.0f64));
    }

    printf("\n**Outlook calibration,** %u places per seed, every census of %u years, the 7 days ahead, every kind "
           "together:\n\n",
           options->places, options->years);
    printf("| Chance | Count | Mean chance | Happened | Two standard errors | |\n|---|---|---|---|---|---|\n");
    for (uint32_t b = 0u; b < SURVEY_BINS; b++) {
        char label[32];
        snprintf(label, sizeof(label), "%u to %u%%", b * 10u, (b + 1u) * 10u);
        uint64_t n = 0u, happened = 0u;
        survey_bin_row(totals, label, SURVEY_KINDS, b, b, &n, &happened);
    }
    printf("\nThe 60 to 80%% bin, by kind (the plan's bound: it happens 60 to 80%% of the time):\n\n");
    printf("| Kind | Count | Mean chance | Happened | Two standard errors | |\n|---|---|---|---|---|---|\n");
    const uint32_t b_lo = (uint32_t)(SURVEY_BIN_LOW * (float64_t)SURVEY_BINS + 0.5f64);
    const uint32_t b_hi = (uint32_t)(SURVEY_BIN_HIGH * (float64_t)SURVEY_BINS + 0.5f64) - 1u;
    uint64_t all_n = 0u, all_happened = 0u;
    for (uint32_t k = 0u; k <= SURVEY_KINDS; k++) {
        uint64_t n = 0u, happened = 0u;
        survey_bin_row(totals, k == SURVEY_KINDS ? "all together" : SURVEY_KIND_NAMES[k], k, b_lo, b_hi, &n, &happened);
        if (k == SURVEY_KINDS) {
            all_n = n;
            all_happened = happened;
        }
    }
    const float64_t middle = all_n > 0u ? (float64_t)all_happened / (float64_t)all_n : 0.0f64;
    *out_middle = middle;
    printf("\nAll kinds, 60 to 80%%: %.1f%% happened, %s.\n\n", (libc_double_t)(100.0f64 * middle),
           middle >= SURVEY_BIN_LOW && middle <= SURVEY_BIN_HIGH ? "within the bound" : "OUTSIDE the bound");

    printf("**Long-run rain against normals,** at the thunderstorm cells, %u years: the largest error %.1f%%, and "
           "%" PRIu64 " of %" PRIu64 " cells (%.1f%%) within 15%%.\n\n",
           options->years, (libc_double_t)(100.0f64 * totals->rain_worst), totals->rain_within, totals->rain_cells,
           (libc_double_t)(totals->rain_cells > 0u
                               ? 100.0f64 * (float64_t)totals->rain_within / (float64_t)totals->rain_cells
                               : 0.0f64));
    const float64_t tile_km2 = (float64_t)CLIMATE_TILE_BLOCKS * (float64_t)CLIMATE_TILE_BLOCKS / 1.0e6f64;
    printf(
        "**Climate memory:** %u bytes a cell, %u cells a tile of %.4f km^2: %.2f KiB a km^2 of cells (target 32), "
        "and %.2f KiB a km^2 of land mask.\n",
        (uint32_t)sizeof(climate_cell_t), (uint32_t)CLIMATE_TILE_CELL_COUNT, (libc_double_t)tile_km2,
        (libc_double_t)((float64_t)(sizeof(climate_cell_t) * (uint64_t)CLIMATE_TILE_CELL_COUNT) / 1024.0f64 / tile_km2),
        (libc_double_t)((float64_t)(sizeof(float32_t) * (uint64_t)CLIMATE_TILE_CELL_COUNT) / 1024.0f64 / tile_km2));
}

static bool survey_record(const survey_totals_t *totals, const survey_options_t *options, float64_t middle,
                          float64_t seconds) {
    FILE *const file = fopen(options->record, "a");
    if (file == nullptr) {
        fprintf(stderr, "bench_climate_survey: could not open %s\n", options->record);
        return false;
    }
    char date[32];
    const time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
    float64_t square, coast, landfall, lo, hi;
    survey_range(totals->square_seasons, totals->squares, &square, &lo, &hi);
    survey_range(totals->coast_seasons, totals->coasts, &coast, &lo, &hi);
    survey_range(totals->coast_landfall, totals->coasts, &landfall, &lo, &hi);
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(file,
                "date\tfirst\tseeds\tyears\twidth\tworkers\twarm_squares\tper_square\tper_coast\tlandfall\tcal_60_80\t"
                "rain_within_15\train_worst\tfailures\tseconds\n");
    }
    fprintf(file, "%s\t%u\t%u\t%u\t%u\t%u\t%u\t%.3f\t%.3f\t%.3f\t%.4f\t%.4f\t%.4f\t%" PRIu64 "\t%.0f\n", date,
            options->first, options->seeds, options->years, options->width, options->workers, totals->squares,
            (libc_double_t)square, (libc_double_t)coast, (libc_double_t)landfall, (libc_double_t)middle,
            (libc_double_t)(totals->rain_cells > 0u ? (float64_t)totals->rain_within / (float64_t)totals->rain_cells
                                                    : 0.0f64),
            (libc_double_t)totals->rain_worst, totals->failures, (libc_double_t)seconds);
    return fclose(file) == 0;
}

/* The next piece of the bench's one region, on its own cache lines. */
static void *survey_take(uint8_t **cursor, uint64_t bytes) {
    void *const at = *cursor;
    *cursor += (bytes + MC_CPU_CACHE_LINE_BYTES - 1u) / MC_CPU_CACHE_LINE_BYTES * MC_CPU_CACHE_LINE_BYTES;
    return at;
}

static bool survey_options(int32_t argc, char **argv, survey_options_t *out) {
    *out = (survey_options_t){.seeds = SURVEY_SEEDS_MAX,
                              .years = SURVEY_YEARS,
                              .workers = MC_THREADS_WORKERS,
                              .width = SURVEY_STRIP_BLOCKS,
                              .places = SURVEY_PLACES,
                              .per_class = SURVEY_CELLS_PER_CLASS,
                              .margin = SURVEY_PLACE_MARGIN_BLOCKS};
    for (int32_t i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        uint32_t *field = nullptr;
        if (strcmp(argv[i], "--record") == 0 && has_value) {
            out->record = argv[++i];
            continue;
        }
        field = strcmp(argv[i], "--seeds") == 0       ? &out->seeds
                : strcmp(argv[i], "--years") == 0     ? &out->years
                : strcmp(argv[i], "--workers") == 0   ? &out->workers
                : strcmp(argv[i], "--width") == 0     ? &out->width
                : strcmp(argv[i], "--places") == 0    ? &out->places
                : strcmp(argv[i], "--per-class") == 0 ? &out->per_class
                : strcmp(argv[i], "--margin") == 0    ? &out->margin
                : strcmp(argv[i], "--first") == 0     ? &out->first
                                                      : nullptr;
        if (field == nullptr || !has_value) {
            return false;
        }
        *field = (uint32_t)strtoul(argv[++i], nullptr, 10);
    }
    return out->seeds >= 1u && out->first + out->seeds <= SURVEY_SEEDS_MAX && out->years >= 1u && out->workers >= 1u &&
           out->workers <= MC_THREADS_WORKERS && out->width >= (uint32_t)CLIMATE_TILE_BLOCKS &&
           out->width <= SURVEY_STRIP_BLOCKS_MAX && out->width % (uint32_t)CLIMATE_CELL_BLOCKS == 0u &&
           out->margin <= SURVEY_MARGIN_MAX_BLOCKS;
}

int32_t main(int32_t argc, char **argv) {
    survey_options_t options;
    if (!survey_options(argc, argv, &options)) {
        fprintf(stderr,
                "usage: %s [--first 0-7] [--seeds 1-8] [--years N] [--workers 1-%u] [--width blocks] [--places N] "
                "[--per-class N] [--margin blocks] [--record file.tsv]\n",
                argv[0], (uint32_t)MC_THREADS_WORKERS);
        return 2;
    }
    const int64_t started = time_now_ns();
    /* The rows of cells whose centres lie between the poles. */
    const int32_t row_min =
        (int32_t)ceil((-CLIMATE_POLE_Y - (float64_t)CLIMATE_HALF_CELL_BLOCKS) / CLIMATE_CELL_BLOCKS);
    const int32_t row_max =
        (int32_t)floor((CLIMATE_POLE_Y - (float64_t)CLIMATE_HALF_CELL_BLOCKS) / CLIMATE_CELL_BLOCKS);
    const uint32_t rows = (uint32_t)(row_max - row_min + 1);
    const uint32_t columns = options.width / (uint32_t)CLIMATE_CELL_BLOCKS;
    const uint64_t cells = (uint64_t)rows * columns;
    const uint64_t jobs_max = (uint64_t)CLIMATE_CLASS_COUNT * options.per_class + options.places;
    const uint64_t bytes = SURVEY_WORKSPACE_BYTES + cells * (sizeof(survey_cell_t) + 2u * sizeof(survey_cell_t *)) +
                           jobs_max * sizeof(survey_place_job_t) + options.places * sizeof(climate_outlook_input_t);
    heap_region_t region;
    if (!heap_reserve((bytes + HEAP_REGION_ALIGNMENT - 1u) / HEAP_REGION_ALIGNMENT * HEAP_REGION_ALIGNMENT, &region)) {
        fprintf(stderr, "bench_climate_survey: no memory\n");
        return 2;
    }
    memset(region.owned_base, 0, bytes); /* every piece taken below starts zeroed: the totals above all */
    uint8_t *cursor = region.owned_base;
#define SURVEY_TAKE(type, count) ((type *)survey_take(&cursor, (uint64_t)(count) * sizeof(type)))
    thread_topology_t *const topology = SURVEY_TAKE(thread_topology_t, 1u);
    job_system_t *const job = SURVEY_TAKE(job_system_t, 1u);
    climate_t *const climate = SURVEY_TAKE(climate_t, 1u);
    terrain_t *const terrain = SURVEY_TAKE(terrain_t, 1u);
    survey_totals_t *const totals = SURVEY_TAKE(survey_totals_t, 1u);
    survey_strip_t *const strip = SURVEY_TAKE(survey_strip_t, 1u);
    survey_seed_work_t *const work = SURVEY_TAKE(survey_seed_work_t, 1u);
    strip->borrowed_cells = SURVEY_TAKE(survey_cell_t, cells);
    work->rows = SURVEY_TAKE(survey_row_job_t, rows);
    work->order = SURVEY_TAKE(survey_cell_t *, 2u * cells);
    work->places = SURVEY_TAKE(survey_place_job_t, jobs_max);
    work->inputs = SURVEY_TAKE(climate_outlook_input_t, options.places);
#undef SURVEY_TAKE
    strip->borrowed_climate = climate;
    strip->row_min = row_min;
    strip->rows = rows;
    strip->columns = columns;
    if (!thread_topology_read(topology) || !job_init_count(job, topology, options.workers) || !climate_init(climate)) {
        fprintf(stderr, "bench_climate_survey: could not start\n");
        return 2;
    }
    fprintf(stderr, "bench_climate_survey: %u seeds, %u years, %u workers, %u by %u cells\n", options.seeds,
            options.years, job->worker_count, columns, rows);
    for (uint32_t s = 0u; s < options.seeds; s++) {
        survey_seed(s, SURVEY_SEEDS[options.first + s], &options, job, climate, terrain, strip, work, totals);
    }
    const float64_t seconds = time_ns_to_ms(time_now_ns() - started) / 1000.0f64;
    float64_t middle = 0.0f64;
    survey_print(totals, &options, &middle);
    fprintf(stderr, "bench_climate_survey: %.0f s in all\n", (libc_double_t)seconds);
    const bool recorded = options.record == nullptr || survey_record(totals, &options, middle, seconds);
    const bool whole = totals->missing == 0u;
    climate_deinit(climate);
    job_deinit(job);
    heap_release(&region);
    return recorded && whole ? 0 : 1;
}
