/* ctest run_climate_systems_rain. See README.md. Master plan 7.4's calibration: over 10 years on 8 seeds, each test cell's
 * expected rain, background plus weather systems, times the high's cut, is within 15% of its normal. */
#include "harness/index.h"

#include "climate/index.h"
#include "climate/systems/field/index.h"
#include "climate/systems/index.h"
#include "job/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    RAIN_SEEDS = 8u,
    RAIN_CELLS = 16u, /* test cells in each box, 4 by 4, 1 km apart */
    RAIN_YEARS = 10u,
    RAIN_YEARS_LONG = 30u,    /* where the cells' sampling noise alone is over 5% (the phase file's Risks) */
    RAIN_SAMPLES_A_DAY = 96u, /* every 15 minutes */
    RAIN_PARTS = 5u,          /* normal, total, hurricanes, lows, cells */
};
static const float64_t RAIN_BOX_HALF = 2048.0f64;     /* the box is 4 by 4 km, 16 tiles */
static const float64_t RAIN_BIG_BEYOND = 2000.0f64;   /* big systems found 2 km beyond the box */
static const float64_t RAIN_LAND_SEARCH = 20000.0f64; /* the nearest land box within 20 km, else the sea there */
static const float64_t RAIN_TOLERANCE = 0.15f64;
static const float64_t RAIN_NOISE_LIMIT = 0.05f64;
/* 0, 10, 15, 25, 35, 45, 55, 65 degrees north. */
static const float64_t RAIN_LATITUDES[RAIN_SEEDS] = {0.0f64,  10.0f64, 15.0f64, 25.0f64,
                                                     35.0f64, 45.0f64, 55.0f64, 65.0f64};
static const uint64_t RAIN_SEED_LIST[RAIN_SEEDS] = {
    0x1234567890ABCDEFull, 2198175988018515243ull, 0x5EED0001ull, 0x5EED0002ull,
    0x5EED0003ull,         0x5EED0004ull,          0x5EED0005ull, 0x5EED0006ull,
};

typedef struct rain_site_t {
    climate_t *climate;           /* 0 */
    terrain_t *terrain;           /* 8 */
    float64_t box_x;              /* 16 */
    float64_t box_y;              /* 24 */
    float64_t cell_x[RAIN_CELLS]; /* 32 */
    float64_t cell_y[RAIN_CELLS]; /* 160 */
    uint8_t land;                 /* 288 */
    uint8_t _pad0[7];             /* 289 */
} rain_site_t;
static_assert(sizeof(rain_site_t) == 296);

/* One site's one year, on a worker. */
typedef struct rain_job_t {
    const rain_site_t *borrowed_site;        /* 0 */
    float64_t parts[RAIN_CELLS][RAIN_PARTS]; /* 8: mm */
    uint64_t cell_events[RAIN_CELLS];        /* 648: cells that passed within their R of the test cell */
    uint64_t hurricane_days[RAIN_CELLS];     /* 776: days a hurricane rained over 0.1 mm an hour there */
    uint64_t low_days[RAIN_CELLS];           /* 904 */
    uint32_t year;                           /* 1032 */
    uint32_t failures;                       /* 1036: a list that could not be built, or cells incomplete */
    uint32_t overflowed;                     /* 1040 */
    uint8_t _pad0[4];                        /* 1044 */
} rain_job_t;
static_assert(sizeof(rain_job_t) == 1048);

static void *rain_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static void rain_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                        uint32_t count_x, uint32_t count_y, float32_t *heights) {
    terrain_height_estimate_area(borrowed_context, origin_x, origin_y, step, count_x, count_y, heights);
}

/* A view of a day list holding only some of its systems: big systems are sorted by kind, so each kind is a run. */
static climate_systems_day_t rain_view(const climate_systems_day_t *day, uint8_t kind, bool cells) {
    climate_systems_day_t view = *day;
    view.thunder_count = cells ? day->thunder_count : 0u;
    uint32_t first = 0u, count = 0u;
    for (uint32_t i = 0u; i < day->system_count; i++) {
        if (day->borrowed_systems[i].kind == kind && !cells) {
            first = count == 0u ? i : first;
            count++;
        }
    }
    view.borrowed_systems = day->borrowed_systems + first;
    view.system_count = count;
    return view;
}

static void rain_year(job_worker_t *worker, void *borrowed_argument) {
    rain_job_t *const job = borrowed_argument;
    const rain_site_t *const site = job->borrowed_site;
    arena_t *const arena = &worker->arena_job;
    const float64_t box_radius = RAIN_BOX_HALF * 1.41421356237309505f64;
    for (uint32_t day_in_year = 0u; day_in_year < 365u; day_in_year++) {
        const int64_t d = (int64_t)job->year * 365 + (int64_t)day_in_year;
        arena_reset(arena);
        climate_systems_day_t day;
        if (!climate_systems_day_build(site->climate, arena, site->box_x, site->box_y,
                                       (float32_t)(box_radius + RAIN_BIG_BEYOND), (float32_t)box_radius, d,
                                       CLIMATE_SYSTEMS_ALL, &day) ||
            day.thunder_complete == 0u) {
            job->failures++;
            continue;
        }
        job->overflowed |= day.overflowed;
        const climate_systems_day_t hurricanes = rain_view(&day, CLIMATE_SYSTEM_HURRICANE, false);
        const climate_systems_day_t lows = rain_view(&day, CLIMATE_SYSTEM_LOW, false);
        const climate_systems_day_t cells = rain_view(&day, CLIMATE_SYSTEM_THUNDER, true);
        for (uint32_t c = 0u; c < RAIN_CELLS; c++) {
            const float64_t x = site->cell_x[c], y = site->cell_y[c];
            climate_normals_at_t normals;
            if (!climate_normals_at(site->climate, x, y, nanf(""), (float32_t)day_in_year, &normals)) {
                job->failures++;
                continue;
            }
            const float64_t normal = (float64_t)normals.rain_mm_day;
            const float64_t share = (float64_t)climate_systems_rain_share_today(
                (float32_t)climate_latitude_deg(y), (float32_t)day_in_year, normals.t_day_c);
            const float64_t background = normal / 24.0f64 * (1.0f64 - share);
            const float64_t wet = (float64_t)climate_systems_wetness(normals.rain_mm_day);
            const float64_t hours = 24.0f64 / (float64_t)RAIN_SAMPLES_A_DAY;
            bool hurricane_rained = false, low_rained = false;
            for (uint32_t s = 0u; s < RAIN_SAMPLES_A_DAY; s++) {
                const float64_t t = (float64_t)d + ((float64_t)s + 0.5f64) / (float64_t)RAIN_SAMPLES_A_DAY;
                const float64_t cut = (float64_t)climate_systems_rain_factor(&day, x, y, t);
                const float64_t h = (float64_t)climate_systems_rain_at(&hurricanes, x, y, t) * wet * cut;
                const float64_t l = (float64_t)climate_systems_rain_at(&lows, x, y, t) * wet * cut;
                const float64_t k = (float64_t)climate_systems_rain_at(&cells, x, y, t) * wet * cut;
                job->parts[c][1] += (background * cut + h + l + k) * hours;
                job->parts[c][2] += h * hours;
                job->parts[c][3] += l * hours;
                job->parts[c][4] += k * hours;
                hurricane_rained = hurricane_rained || h > 0.1f64;
                low_rained = low_rained || l > 0.1f64;
            }
            job->parts[c][0] += normal;
            job->hurricane_days[c] += hurricane_rained ? 1u : 0u;
            job->low_days[c] += low_rained ? 1u : 0u;
            /* Each cell is counted once, by the list of its spawn day, if it passed within its R of the test cell. */
            for (uint32_t i = 0u; i < day.thunder_count; i++) {
                const climate_thunder_t *const cell = &day.borrowed_thunder[i];
                if (cell->spawn_day < (float64_t)d) {
                    continue;
                }
                bool passed = false;
                for (uint32_t s = 0u; s <= 16u && !passed; s++) {
                    float64_t centre[2], g = 0.0f64;
                    if (climate_systems_thunder_at(cell, cell->spawn_day + (float64_t)cell->life_days * s / 16.0f64,
                                                   centre, &g)) {
                        const float64_t dx = centre[0] - x, dy = centre[1] - y;
                        passed = dx * dx + dy * dy < (float64_t)cell->radius * (float64_t)cell->radius;
                    }
                }
                job->cell_events[c] += passed ? 1u : 0u;
            }
        }
    }
}

/* The box: the nearest x within 20 km of 0 whose box centre is land on the coarse mask, at this latitude's y; else x = 0. */
static void rain_place(rain_site_t *site, float64_t latitude) {
    site->box_y = latitude * CLIMATE_POLE_Y / 90.0f64;
    site->box_x = 0.0f64;
    site->land = 0u;
    for (float64_t off = 0.0f64; off <= RAIN_LAND_SEARCH; off += 1024.0f64) {
        for (int32_t side = -1; side <= 1; side += 2) {
            const float64_t x = off * (float64_t)side;
            if (site->land == 0u && climate_mask_point(site->climate, x, site->box_y) > 0.0f32 &&
                climate_mask_point(site->climate, x - 1024.0f64, site->box_y) > 0.0f32 &&
                climate_mask_point(site->climate, x + 1024.0f64, site->box_y) > 0.0f32) {
                site->box_x = x;
                site->land = 1u;
            }
        }
        if (site->land != 0u) {
            break;
        }
    }
    for (uint32_t c = 0u; c < RAIN_CELLS; c++) {
        site->cell_x[c] = site->box_x - 1536.0f64 + 1024.0f64 * (float64_t)(c % 4u);
        site->cell_y[c] = site->box_y - 1536.0f64 + 1024.0f64 * (float64_t)(c / 4u);
    }
}

TEST(ten_years_of_rain_are_within_15_percent_of_normal_on_8_seeds) {
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    job_system_t *const system = rain_aligned(t, sizeof(job_system_t));
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init_count(system, topology, MC_THREADS_WORKERS));
    rain_site_t *const sites = test_alloc(t, RAIN_SEEDS * sizeof(rain_site_t));
    const int64_t started = time_now_ns();
    bool ready = true;
    for (uint32_t s = 0u; s < RAIN_SEEDS; s++) {
        sites[s].terrain = rain_aligned(t, sizeof(terrain_t));
        sites[s].climate = rain_aligned(t, sizeof(climate_t));
        ready = ready && terrain_init(sites[s].terrain, RAIN_SEED_LIST[s]) && climate_init(sites[s].climate) &&
                climate_open_ground(sites[s].climate, RAIN_SEED_LIST[s], rain_ground, sites[s].terrain);
        if (!ready) {
            break;
        }
        rain_place(&sites[s], RAIN_LATITUDES[s]);
        /* Every tile the cells need: the box, plus the cells' reach and a square, so thunder_complete holds. */
        const float64_t reach =
            RAIN_BOX_HALF * 1.41421356237309505f64 + climate_systems_reach(CLIMATE_SYSTEM_THUNDER) + 1024.0f64;
        climate_prepare_area(sites[s].climate, system, (int32_t)(sites[s].box_x - reach),
                             (int32_t)(sites[s].box_y - reach), (int32_t)(sites[s].box_x + reach),
                             (int32_t)(sites[s].box_y + reach), nullptr, nullptr);
    }
    REQUIRE(ready);
    const int64_t prepared = time_now_ns();
    rain_job_t *const jobs = test_alloc(t, (uint64_t)RAIN_SEEDS * RAIN_YEARS_LONG * sizeof(rain_job_t));
    uint32_t years[RAIN_SEEDS];
    job_group_t *const group = rain_aligned(t, sizeof(job_group_t));
    for (uint32_t round = 0u; round < 2u; round++) {
        job_group_init(group);
        for (uint32_t s = 0u; s < RAIN_SEEDS; s++) {
            if (round == 0u) {
                years[s] = RAIN_YEARS;
            } else {
                /* The phase file's rule: where the cells' sampling noise alone is over 5% at a cell, 30 years. */
                bool noisy = false;
                for (uint32_t c = 0u; c < RAIN_CELLS; c++) {
                    uint64_t events = 0u;
                    for (uint32_t y = 0u; y < RAIN_YEARS; y++) {
                        events += jobs[s * RAIN_YEARS_LONG + y].cell_events[c];
                    }
                    noisy = noisy || (events > 0u && 1.0f64 / sqrt((float64_t)events) > RAIN_NOISE_LIMIT);
                }
                years[s] = noisy ? RAIN_YEARS_LONG : RAIN_YEARS;
            }
            for (uint32_t y = round == 0u ? 0u : RAIN_YEARS; y < years[s]; y++) {
                rain_job_t *const job = &jobs[s * RAIN_YEARS_LONG + y];
                job->borrowed_site = &sites[s];
                job->year = y;
                job_submit(system, group, rain_year, job);
            }
        }
        job_group_wait(system, group);
        job_group_deinit(group);
    }
    printf("tiles %.1f s, the years %.1f s\n", (libc_double_t)(time_ns_to_ms(prepared - started) / 1000.0f64),
           (libc_double_t)(time_ns_to_ms(time_now_ns() - prepared) / 1000.0f64));
    for (uint32_t s = 0u; s < RAIN_SEEDS; s++) {
        printf("seed %u at %.0f N: box (%.0f, %.0f), %s, %u years\n", s, (libc_double_t)RAIN_LATITUDES[s],
               (libc_double_t)sites[s].box_x, (libc_double_t)sites[s].box_y, sites[s].land != 0u ? "land" : "sea",
               years[s]);
    }
    printf("seed lat land cell |   normal mm    total mm  ratio | hurricane%%  low%%  cells%% | hur days low days "
           "cells noise\n");
    uint32_t outside = 0u, failures = 0u, overflowed = 0u;
    float64_t worst = 0.0f64;
    for (uint32_t s = 0u; s < RAIN_SEEDS; s++) {
        for (uint32_t c = 0u; c < RAIN_CELLS; c++) {
            float64_t parts[RAIN_PARTS] = {0};
            uint64_t events = 0u, hurricane_days = 0u, low_days = 0u;
            for (uint32_t y = 0u; y < years[s]; y++) {
                const rain_job_t *const job = &jobs[s * RAIN_YEARS_LONG + y];
                for (uint32_t p = 0u; p < RAIN_PARTS; p++) {
                    parts[p] += job->parts[c][p];
                }
                events += job->cell_events[c];
                hurricane_days += job->hurricane_days[c];
                low_days += job->low_days[c];
                if (c == 0u) {
                    failures += job->failures;
                    overflowed += job->overflowed;
                }
            }
            const float64_t ratio = parts[0] > 0.0f64 ? parts[1] / parts[0] : 0.0f64;
            const float64_t noise = events > 0u ? 1.0f64 / sqrt((float64_t)events) : 0.0f64;
            const float64_t off = fabs(ratio - 1.0f64);
            worst = off > worst ? off : worst;
            outside += off <= RAIN_TOLERANCE ? 0u : 1u;
            printf("%u %4.0f %u %2u | %10.1f %10.1f %6.3f | %6.1f %6.1f %6.1f | %6" PRIu64 " %6" PRIu64 " %6" PRIu64
                   " %.3f%s\n",
                   s, (libc_double_t)RAIN_LATITUDES[s], sites[s].land, c, (libc_double_t)parts[0],
                   (libc_double_t)parts[1], (libc_double_t)ratio,
                   (libc_double_t)(parts[1] > 0.0f64 ? 100.0f64 * parts[2] / parts[1] : 0.0f64),
                   (libc_double_t)(parts[1] > 0.0f64 ? 100.0f64 * parts[3] / parts[1] : 0.0f64),
                   (libc_double_t)(parts[1] > 0.0f64 ? 100.0f64 * parts[4] / parts[1] : 0.0f64), hurricane_days,
                   low_days, events, (libc_double_t)noise, off <= RAIN_TOLERANCE ? "" : "  OUTSIDE");
        }
    }
    printf("cells outside 15%%: %u of %u; worst %.3f; failed days %u\n", outside, RAIN_SEEDS * RAIN_CELLS,
           (libc_double_t)worst, failures);
    CHECK_EQ_U64(failures, 0u);
    CHECK_EQ_U64(overflowed, 0u);
    CHECK_EQ_U64(outside, 0u);
    for (uint32_t s = 0u; s < RAIN_SEEDS; s++) {
        climate_close(sites[s].climate, system);
        climate_deinit(sites[s].climate);
        terrain_deinit(sites[s].terrain);
    }
    job_deinit(system);
}

TEST_MAIN()
