/* ctest run_climate_outlook. See README.md. The phase 07 file's tests of run/src/climate/outlook/: Hurricane Tavela, the
 * background rain and "at least one" by hand, the outlook needing only its input, the same outlook from a new process, the
 * outlook never reading the actual future, and its calibration over 10 years of real weather. */
#include "harness/index.h"

#include "climate/index.h"
#include "climate/outlook/index.h"
#include "climate/systems/field/index.h"
#include "climate/systems/index.h"
#include "climate/weather/index.h"
#include "job/index.h"
#include "save/climate/index.h"
#include "terrain/index.h"
#include "thread/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint64_t OUTLOOK_SEED = 0x1234567890ABCDEFull;
static const uint64_t OUTLOOK_ARENA_BYTES = 32ull << 20;
/* The place the single-census tests ask about: 45 N, where lows come by every week. */
static const float64_t OUTLOOK_X = 3000.0f64;
static const float64_t OUTLOOK_Y = 19148.936170212766f64;
static const char OUTLOOK_DIGEST_ENV[] = "MC_OUTLOOK_DIGEST_WEEK";

typedef struct outlook_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
    terrain_t *terrain;          /* 24 */
    arena_t *arena;              /* 32 */
} outlook_rig_t;
static_assert(sizeof(outlook_rig_t) == 40);

static void *outlook_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool outlook_rig_start(test_context_t *t, outlook_rig_t *rig, uint64_t seed) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = outlook_aligned(t, sizeof(job_system_t));
    rig->climate = outlook_aligned(t, sizeof(climate_t));
    rig->terrain = outlook_aligned(t, sizeof(terrain_t));
    rig->arena = outlook_aligned(t, sizeof(arena_t));
    arena_init(rig->arena, "test_outlook", outlook_aligned(t, OUTLOOK_ARENA_BYTES), OUTLOOK_ARENA_BYTES);
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, MC_THREADS_WORKERS) ||
        !terrain_init(rig->terrain, seed) || !climate_init(rig->climate)) {
        return false;
    }
    return climate_open(rig->climate, rig->terrain);
}

static void outlook_rig_stop(outlook_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    terrain_deinit(rig->terrain);
    job_deinit(rig->job);
}

static void outlook_prepare(outlook_rig_t *rig, float64_t x, float64_t y, float64_t reach) {
    climate_prepare_area(rig->climate, rig->job, (int32_t)(x - reach), (int32_t)(y - reach), (int32_t)(x + reach),
                         (int32_t)(y + reach), nullptr, nullptr);
}

/* ---- By hand ----------------------------------------------------------------------------------------------------------- */

TEST(tavela_is_93_percent_on_day_2) {
    climate_outlook_input_t *const input = test_alloc(t, sizeof(climate_outlook_input_t));
    memset(input, 0, sizeof(*input));
    input->error_off = 1u; /* the test seam: forecast centres are the steered ones */
    input->footprint = CLIMATE_OUTLOOK_FOOTPRINT;
    input->sigma_t = 3.0f32;
    input->known_count = 1u;
    climate_outlook_known_t *const tavela = &input->known[0];
    snprintf(tavela->name, sizeof(tavela->name), "Hurricane Tavela");
    tavela->id = 42u;
    tavela->kind = CLIMATE_SYSTEM_HURRICANE;
    tavela->radius = 2500.0f32;
    for (uint32_t t_day = 0u; t_day < CLIMATE_OUTLOOK_DAYS; t_day++) {
        tavela->x[t_day] = 6000.0f64 - 3500.0f64 * (float64_t)(t_day + 1u); /* west at 3.5 km a day */
        tavela->y[t_day] = 0.0f64;
        tavela->alive[t_day] = 1u;
        tavela->pressure[t_day] = -60.0f32;
    }
    climate_outlook_t out;
    climate_outlook_run(input, &out);
    REQUIRE(out.system_count == 1u);
    printf("%s: day 2 %.4f (form %u), day 1 %.4f, day 3 %.4f; the day's hurricane chance %.4f\n", out.systems[0].name,
           (libc_double_t)out.systems[0].chance[1], (uint32_t)CLIMATE_OUTLOOK_FOOTPRINT,
           (libc_double_t)out.systems[0].chance[0], (libc_double_t)out.systems[0].chance[2],
           (libc_double_t)out.days[1].hurricane);
    CHECK_STR_EQ(out.systems[0].name, "Hurricane Tavela");
    if (CLIMATE_OUTLOOK_FOOTPRINT == CLIMATE_OUTLOOK_FOOTPRINT_MASTER) {
        CHECK_NEAR(out.systems[0].chance[1], 0.933f32, 0.001f32);
    } else {
        /* Issues found 1: the exact chance a Gaussian-scattered centre lands within R, which the calibration test chose. */
        CHECK_NEAR(out.systems[0].chance[1], 0.879f32, 0.002f32);
    }
    CHECK_NEAR(out.days[1].hurricane, out.systems[0].chance[1], 1.0e-6f32);
    /* Both forms against their own formulas. */
    CHECK_NEAR(climate_outlook_footprint(1000.0f64, 1000.0f64, 2500.0f64, CLIMATE_OUTLOOK_FOOTPRINT_MASTER), 0.9333f64,
               0.0002f64);
    CHECK_NEAR(climate_outlook_disk_chance(1000.0f64, 1000.0f64, 2500.0f64), 0.8791f64, 0.0005f64);
    CHECK_NEAR(climate_outlook_disk_chance(0.0f64, 2800.0f64, 2500.0f64), 0.3289f64, 0.0005f64);
    CHECK_NEAR(climate_outlook_disk_chance(0.0f64, 1000.0f64, 1000.0f64), 1.0f64 - exp(-0.5f64), 0.0002f64);
    CHECK_NEAR(climate_outlook_disk_chance(50000.0f64, 600.0f64, 4000.0f64), 0.0f64, 1.0e-9f64);
}

TEST(background_rain_chance_by_hand) {
    printf("normal 3 mm: %.4f, 12 mm: %.4f, 0: %.4f\n", (libc_double_t)climate_outlook_background_rain(3.0f32),
           (libc_double_t)climate_outlook_background_rain(12.0f32),
           (libc_double_t)climate_outlook_background_rain(0.0f32));
    CHECK_NEAR(climate_outlook_background_rain(3.0f32), 0.393f32, 0.0005f32);
    CHECK_NEAR(climate_outlook_background_rain(12.0f32), 0.865f32, 0.0005f32);
    CHECK_NEAR(climate_outlook_background_rain(0.0f32), 0.0f32, 0.0f32);
}

TEST(at_least_one_by_hand) {
    const float64_t chances[2] = {0.3f64, 0.2f64};
    const float64_t p = climate_outlook_at_least_one(chances, 2u, 0.1f64);
    printf("0.3, 0.2 and Lambda 0.1: %.4f\n", (libc_double_t)p);
    CHECK_NEAR(p, 0.493f64, 0.0005f64);
    CHECK_NEAR(climate_outlook_at_least_one(chances, 0u, 0.0f64), 0.0f64, 0.0f64);
    /* The error draw: a spread of 1 on each axis, the same each time. */
    float64_t sum = 0.0f64, sum2 = 0.0f64, a[2], b[2];
    for (uint32_t i = 0u; i < 20000u; i++) {
        climate_outlook_error(7u, i, 3u, 2u, a);
        sum += a[0] + a[1];
        sum2 += a[0] * a[0] + a[1] * a[1];
    }
    climate_outlook_error(7u, 99u, 3u, 2u, a);
    climate_outlook_error(7u, 99u, 3u, 2u, b);
    const float64_t sd = sqrt(sum2 / 40000.0f64 - (sum / 40000.0f64) * (sum / 40000.0f64));
    printf("forecast error: mean %.4f, spread %.4f in sigmas\n", (libc_double_t)(sum / 40000.0f64), (libc_double_t)sd);
    CHECK_NEAR(sd, 1.0f64, 0.03f64);
    CHECK(a[0] == b[0] && a[1] == b[1]);
}

/* ---- On a real world ----------------------------------------------------------------------------------------------- */

static uint64_t outlook_digest(const void *data, uint64_t bytes) {
    return save_climate_hash(0xCBF29CE484222325ull, data, bytes);
}

TEST(the_outlook_needs_only_its_input) {
    outlook_rig_t rig;
    REQUIRE(outlook_rig_start(t, &rig, OUTLOOK_SEED));
    outlook_prepare(&rig, OUTLOOK_X, OUTLOOK_Y, 7000.0f64);
    climate_outlook_input_t *const input = test_alloc(t, sizeof(climate_outlook_input_t));
    climate_outlook_input_t *const copy = test_alloc(t, sizeof(climate_outlook_input_t));
    climate_outlook_t with, without;
    const uint64_t week = 30u;
    REQUIRE(climate_outlook_input_fill(rig.climate, OUTLOOK_X, OUTLOOK_Y, 7.0f64 * (float64_t)week, week, rig.arena,
                                       input));
    climate_outlook_run(input, &with);
    memcpy(copy, input, sizeof(*copy));
    /* The climate goes, every byte of its arena is scribbled over, and only the copy is left to read. */
    outlook_rig_stop(&rig);
    memset(input, 0xA5, sizeof(*input));
    memset(rig.arena->borrowed_base, 0xA5, rig.arena->capacity);
    climate_outlook_run(copy, &without);
    printf("week %" PRIu64 ": %u systems known, %u named; day 1 rain %.3f, storm %.3f; high %.1f C\n", week,
           copy->known_count, with.system_count, (libc_double_t)with.days[0].rain, (libc_double_t)with.days[0].storm,
           (libc_double_t)with.days[0].high_c);
    CHECK_MEM_EQ(&with, &without, sizeof(with));
    CHECK(copy->known_count > 0u);
}

/* The digest of one census's outlook on a fresh climate: what a new process must say too. */
static uint64_t outlook_census_digest(test_context_t *t, uint64_t week) {
    outlook_rig_t rig;
    if (!outlook_rig_start(t, &rig, OUTLOOK_SEED)) {
        return 0u;
    }
    outlook_prepare(&rig, OUTLOOK_X, OUTLOOK_Y, 7000.0f64);
    climate_outlook_input_t *const input = test_alloc(t, sizeof(climate_outlook_input_t));
    climate_outlook_t out;
    uint64_t digest = 0u;
    if (climate_outlook_input_fill(rig.climate, OUTLOOK_X, OUTLOOK_Y, 7.0f64 * (float64_t)week, week, rig.arena,
                                   input)) {
        climate_outlook_run(input, &out);
        digest = outlook_digest(input, sizeof(*input)) ^ (outlook_digest(&out, sizeof(out)) * 3u);
    }
    outlook_rig_stop(&rig);
    return digest;
}

/* Not a test of its own: what the child process of reopening_gives_the_same_outlook runs, by the variable it is given. */
TEST(outlook_digest_for_a_new_process) {
    const char *const week = getenv(OUTLOOK_DIGEST_ENV);
    if (week == nullptr) {
        CHECK(true);
        return;
    }
    printf("outlook digest %016" PRIx64 "\n", outlook_census_digest(t, strtoull(week, nullptr, 10)));
    CHECK(true);
}

TEST(reopening_gives_the_same_outlook) {
    const uint64_t week = 41u;
    const uint64_t here = outlook_census_digest(t, week);
    const uint64_t again = outlook_census_digest(t, week);
    REQUIRE(here != 0u);
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, week);
    REQUIRE(setenv(OUTLOOK_DIGEST_ENV, text, 1) == 0);
    REQUIRE(setenv("TEST_FILTER", "outlook_digest_for_a_new_process", 1) == 0);
    char *const argv[] = {(char *)"/proc/self/exe", nullptr};
    test_process_t process;
    test_process_run(t, argv, 120000u, &process);
    unsetenv(OUTLOOK_DIGEST_ENV);
    unsetenv("TEST_FILTER");
    char expected[64];
    snprintf(expected, sizeof(expected), "outlook digest %016" PRIx64, here);
    printf("this process twice: %016" PRIx64 ", %016" PRIx64 "; a new process said: %s\n", here, again,
           strstr(process.output, "outlook digest") != nullptr ? strstr(process.output, "outlook digest") : "nothing");
    CHECK(here == again);
    CHECK_EQ_I64(process.exit_code, 0);
    CHECK_CONTAINS(process.output, expected);
}

TEST(the_outlook_never_reads_the_future) {
    /* Over a year of censuses at 45 N: no system in the input was born after the census moment or is dead by it, though the
     * census day's own list holds systems born later that same day; and none of the systems that come here in the 7 days but
     * are born after the census is ever named. The outlook itself reads only the input (the_outlook_needs_only_its_input). */
    outlook_rig_t rig;
    REQUIRE(outlook_rig_start(t, &rig, OUTLOOK_SEED));
    outlook_prepare(&rig, OUTLOOK_X, OUTLOOK_Y, 7000.0f64);
    climate_outlook_input_t *const input = test_alloc(t, sizeof(climate_outlook_input_t));
    uint32_t born_later_same_day = 0u, coming_unborn = 0u, leaked = 0u, known_total = 0u, bad_known = 0u;
    for (uint64_t week = 12u; week < 12u + 52u; week++) {
        const float64_t census_t = 7.0f64 * (float64_t)week;
        arena_reset(rig.arena);
        REQUIRE(climate_outlook_input_fill(rig.climate, OUTLOOK_X, OUTLOOK_Y, census_t, week, rig.arena, input));
        known_total += input->known_count;
        climate_systems_day_t list;
        REQUIRE(climate_systems_day_build(rig.climate, rig.arena, OUTLOOK_X, OUTLOOK_Y,
                                          (float32_t)CLIMATE_OUTLOOK_REACH, 0.0f32, (int64_t)census_t,
                                          CLIMATE_SYSTEMS_BIG, &list));
        for (uint32_t k = 0u; k < input->known_count; k++) {
            bool found = false;
            for (uint32_t i = 0u; i < list.system_count; i++) {
                const climate_system_t *const system = &list.borrowed_systems[i];
                if (system->id != input->known[k].id) {
                    continue;
                }
                climate_system_state_t state;
                climate_system_state_at(system, census_t, &state);
                found = system->spawn_day <= census_t && state.alive != 0u;
            }
            bad_known += found ? 0u : 1u;
        }
        for (uint32_t i = 0u; i < list.system_count; i++) {
            const climate_system_t *const system = &list.borrowed_systems[i];
            if (system->spawn_day > census_t) {
                born_later_same_day++;
                for (uint32_t k = 0u; k < input->known_count; k++) {
                    leaked += input->known[k].id == system->id ? 1u : 0u;
                }
            }
        }
        /* The systems actually here in the coming days, born after the census. */
        for (uint32_t day = 1u; day <= CLIMATE_OUTLOOK_DAYS; day++) {
            climate_systems_day_t later;
            REQUIRE(climate_systems_day_build(rig.climate, rig.arena, OUTLOOK_X, OUTLOOK_Y, 0.0f32, 0.0f32,
                                              (int64_t)census_t + (int64_t)day, CLIMATE_SYSTEMS_BIG, &later));
            for (uint32_t i = 0u; i < later.system_count; i++) {
                if (later.borrowed_systems[i].spawn_day <= census_t) {
                    continue;
                }
                coming_unborn++;
                for (uint32_t k = 0u; k < input->known_count; k++) {
                    leaked += input->known[k].id == later.borrowed_systems[i].id ? 1u : 0u;
                }
            }
        }
    }
    printf("52 censuses: %u systems known in all, %u not alive at the census; %u born later on the census day and %u "
           "coming "
           "within the week born after it; %u of those in an input\n",
           known_total, bad_known, born_later_same_day, coming_unborn, leaked);
    CHECK(known_total > 0u);
    CHECK_EQ_U64(bad_known, 0u);
    CHECK(born_later_same_day + coming_unborn > 0u); /* the test means something only if the future had systems in it */
    CHECK_EQ_U64(leaked, 0u);
    outlook_rig_stop(&rig);
}

/* ---- Calibration over 10 years ------------------------------------------------------------------------------------- */

enum : uint32_t {
    CAL_SEEDS = 2u,
    CAL_PLACES = 16u,
    CAL_CENSUSES = 520u,
    CAL_BINS = 10u,
    CAL_KINDS = 5u, /* rain, thunder, storm, hurricane, named systems */
};
static const char *const CAL_KIND_NAMES[CAL_KINDS] = {"rain", "thunder", "storm", "hurricane", "named"};
static const uint64_t CAL_SEED_LIST[CAL_SEEDS] = {0x1234567890ABCDEFull, 2198175988018515243ull};

/* One place's 10 years, on a worker. */
typedef struct cal_job_t {
    const climate_t *borrowed_climate;       /* 0 */
    climate_outlook_input_t *borrowed_input; /* 8 */
    float64_t x;                             /* 16 */
    float64_t y;                             /* 24 */
    uint64_t bins[CAL_KINDS][CAL_BINS][2];   /* 32: pairs, and how many happened */
    uint64_t middle[CAL_KINDS][2];           /* 832: the 0.65 to 0.75 bin */
    uint32_t failures;                       /* 912 */
    uint8_t _pad0[4];                        /* 916 */
} cal_job_t;
static_assert(sizeof(cal_job_t) == 920);

static void cal_note(cal_job_t *job, uint32_t kind, float64_t p, bool happened) {
    uint32_t bin = (uint32_t)(p * (float64_t)CAL_BINS);
    bin = bin >= CAL_BINS ? CAL_BINS - 1u : bin;
    job->bins[kind][bin][0]++;
    job->bins[kind][bin][1] += happened ? 1u : 0u;
    if (p >= 0.65f64 && p < 0.75f64) {
        job->middle[kind][0]++;
        job->middle[kind][1] += happened ? 1u : 0u;
    }
}

static bool cal_covers(const climate_system_t *system, float64_t t, float64_t x, float64_t y) {
    climate_system_state_t state;
    climate_system_state_at(system, t, &state);
    const float64_t dx = x - state.x, dy = y - state.y;
    return state.alive != 0u && dx * dx + dy * dy < (float64_t)system->radius * (float64_t)system->radius;
}

static void cal_place(job_worker_t *worker, void *borrowed_argument) {
    cal_job_t *const job = borrowed_argument;
    arena_t *const arena = &worker->arena_job;
    const climate_t *const climate = job->borrowed_climate;
    for (uint32_t c = 0u; c < CAL_CENSUSES; c++) {
        const uint64_t week = 12u + c;
        const float64_t census_t = 7.0f64 * (float64_t)week;
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
            /* What happened: the day's rain and its cells, and who covers the place at the instant. */
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
                const float64_t at = (float64_t)day + ((float64_t)h + 0.5f64) / 24.0f64;
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
                if (system->kind == CLIMATE_SYSTEM_HIGH || !cal_covers(system, instant, job->x, job->y)) {
                    continue;
                }
                climate_system_state_t state;
                climate_system_state_at(system, instant, &state);
                hurricane = hurricane || system->kind == CLIMATE_SYSTEM_HURRICANE;
                storm =
                    storm || system->kind == CLIMATE_SYSTEM_HURRICANE || state.pressure_hpa <= -CLIMATE_STORM_DEPTH_HPA;
            }
            cal_note(job, 0u, (float64_t)out.days[d].rain, weather.rain_mm > CLIMATE_DAY_RAIN_MM);
            cal_note(job, 1u, (float64_t)out.days[d].thunder, thunder);
            cal_note(job, 2u, (float64_t)out.days[d].storm, storm);
            cal_note(job, 3u, (float64_t)out.days[d].hurricane, hurricane);
            for (uint32_t s = 0u; s < out.system_count; s++) {
                bool came = false;
                for (uint32_t i = 0u; i < big.system_count; i++) {
                    const climate_system_t *const system = &big.borrowed_systems[i];
                    const uint32_t folded = (uint32_t)(system->id ^ (system->id >> 32));
                    came = came || (folded == out.systems[s].id && cal_covers(system, instant, job->x, job->y));
                }
                cal_note(job, 4u, (float64_t)out.systems[s].chance[d], came);
            }
        }
    }
}

/* Coastal land within the region: a cell on land with the sea under 3 km away, on a grid, nearest to each target. */
static bool cal_place_find(const climate_t *climate, float64_t target_x, float64_t target_y, float64_t *out_x,
                           float64_t *out_y) {
    float64_t best = INFINITY;
    for (int32_t j = -40; j <= 40; j++) {
        for (int32_t i = -40; i <= 40; i++) {
            const float64_t x = target_x + 64.0f64 * (float64_t)i, y = target_y + 64.0f64 * (float64_t)j;
            climate_cell_t cell;
            if (!climate_cell_get(climate, (int32_t)x, (int32_t)y, &cell) || (cell.flags & CLIMATE_CELL_SEA) != 0u ||
                cell.z_ground <= 0.0f32 || cell.sea_distance > 3000.0f32) {
                continue;
            }
            const float64_t d = (float64_t)(i * i + j * j);
            if (d < best) {
                best = d;
                *out_x = x;
                *out_y = y;
            }
        }
    }
    return isfinite(best);
}

TEST(outlook_is_calibrated_over_10_years) {
    cal_job_t *const jobs = test_alloc(t, CAL_SEEDS * CAL_PLACES * sizeof(cal_job_t));
    memset(jobs, 0, CAL_SEEDS * CAL_PLACES * sizeof(cal_job_t));
    /* Hurricane coasts at 8 to 22 degrees, and the mid-latitudes at 38 to 55, within one keep square of tiles. */
    static const float64_t LATITUDES[CAL_PLACES / 2u] = {8.0f64,  12.0f64, 16.0f64, 22.0f64,
                                                         38.0f64, 44.0f64, 50.0f64, 55.0f64};
    for (uint32_t s = 0u; s < CAL_SEEDS; s++) {
        outlook_rig_t rig;
        REQUIRE(outlook_rig_start(t, &rig, CAL_SEED_LIST[s]));
        const float64_t y_lo = LATITUDES[0] * CLIMATE_POLE_Y / 90.0f64;
        const float64_t y_hi = LATITUDES[CAL_PLACES / 2u - 1u] * CLIMATE_POLE_Y / 90.0f64;
        climate_prepare_area(rig.climate, rig.job, -12000, (int32_t)y_lo - 9000, 12000, (int32_t)y_hi + 9000, nullptr,
                             nullptr);
        for (uint32_t p = 0u; p < CAL_PLACES; p++) {
            cal_job_t *const job = &jobs[s * CAL_PLACES + p];
            job->borrowed_climate = rig.climate;
            job->borrowed_input = test_alloc(t, sizeof(climate_outlook_input_t));
            const float64_t target_x = p < CAL_PLACES / 2u ? -4000.0f64 : 4000.0f64;
            const float64_t target_y = LATITUDES[p % (CAL_PLACES / 2u)] * CLIMATE_POLE_Y / 90.0f64;
            if (!cal_place_find(rig.climate, target_x, target_y, &job->x, &job->y)) {
                job->x = target_x; /* no coast near: the place itself, land or sea */
                job->y = target_y;
            }
        }
        job_group_t group;
        job_group_init(&group);
        for (uint32_t p = 0u; p < CAL_PLACES; p++) {
            job_submit(rig.job, &group, cal_place, &jobs[s * CAL_PLACES + p]);
        }
        job_group_wait(rig.job, &group);
        job_group_deinit(&group);
        outlook_rig_stop(&rig);
    }
    uint64_t bins[CAL_KINDS][CAL_BINS][2] = {0}, middle[CAL_KINDS][2] = {0}, all_middle[2] = {0};
    uint32_t failures = 0u;
    for (uint32_t j = 0u; j < CAL_SEEDS * CAL_PLACES; j++) {
        failures += jobs[j].failures;
        for (uint32_t k = 0u; k < CAL_KINDS; k++) {
            for (uint32_t b = 0u; b < CAL_BINS; b++) {
                bins[k][b][0] += jobs[j].bins[k][b][0];
                bins[k][b][1] += jobs[j].bins[k][b][1];
            }
            middle[k][0] += jobs[j].middle[k][0];
            middle[k][1] += jobs[j].middle[k][1];
        }
    }
    printf("footprint form %u; %u censuses could not be worked out\n", (uint32_t)CLIMATE_OUTLOOK_FOOTPRINT, failures);
    printf("%-10s", "bin");
    for (uint32_t b = 0u; b < CAL_BINS; b++) {
        printf(" %4.1f-%3.1f      ", (libc_double_t)((float64_t)b / 10.0f64),
               (libc_double_t)((float64_t)(b + 1u) / 10.0f64));
    }
    printf("\n");
    for (uint32_t k = 0u; k < CAL_KINDS; k++) {
        printf("%-10s", CAL_KIND_NAMES[k]);
        for (uint32_t b = 0u; b < CAL_BINS; b++) {
            const uint64_t n = bins[k][b][0];
            printf(" %7" PRIu64 " %5.3f", n,
                   (libc_double_t)(n > 0u ? (float64_t)bins[k][b][1] / (float64_t)n : 0.0f64));
        }
        printf("   0.65-0.75: %" PRIu64 " pairs, %.3f\n", middle[k][0],
               (libc_double_t)(middle[k][0] > 0u ? (float64_t)middle[k][1] / (float64_t)middle[k][0] : 0.0f64));
        all_middle[0] += middle[k][0];
        all_middle[1] += middle[k][1];
    }
    const float64_t observed = all_middle[0] > 0u ? (float64_t)all_middle[1] / (float64_t)all_middle[0] : 0.0f64;
    printf("events given 0.65 to 0.75: %" PRIu64 " pairs, %" PRIu64 " happened: %.3f\n", all_middle[0], all_middle[1],
           (libc_double_t)observed);
    CHECK(all_middle[0] >= 300u);
    CHECK(observed >= 0.60f64 && observed <= 0.80f64);
}

TEST_MAIN()
