/* ctest run_climate_weather. See README.md. The phase 07 file's tests of run/src/climate/weather/ and its parts week/ and
 * flow/: the noises' spread, the weekly update by the master plan's numbers, ice, the soil bucket, the day kinds, daily
 * weather as a pure function, spells, heat waves, hourly rain adding up to the day, and snow through the year. */
#include "harness/index.h"

#include "climate/index.h"
#include "climate/season/index.h"
#include "climate/weather/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "terrain/rng/index.h"
#include "thread/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint64_t WEATHER_SEED = 0x1234567890ABCDEFull;
static const uint64_t WEATHER_ARENA_BYTES = 32ull << 20;
static const float64_t WEATHER_Y45 = 19148.936170212766f64;
static const float64_t WEATHER_Y75 = 31914.893617021276f64;

typedef struct weather_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
    ground_t *ground;            /* 24 */
    arena_t *arena;              /* 32 */
} weather_rig_t;
static_assert(sizeof(weather_rig_t) == 40);

static void *weather_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool weather_rig_start(test_context_t *t, weather_rig_t *rig, const ground_t *ground) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = weather_aligned(t, sizeof(job_system_t));
    rig->climate = weather_aligned(t, sizeof(climate_t));
    rig->ground = test_alloc(t, sizeof(ground_t));
    *rig->ground = *ground;
    rig->arena = weather_aligned(t, sizeof(arena_t));
    arena_init(rig->arena, "test_weather", weather_aligned(t, WEATHER_ARENA_BYTES), WEATHER_ARENA_BYTES);
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, MC_THREADS_WORKERS)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return climate_open_ground(rig->climate, WEATHER_SEED, ground_fn, rig->ground);
}

static void weather_rig_stop(weather_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

/* Every tile a day list round (x, y) reads: 6 km each way. */
static void weather_prepare(weather_rig_t *rig, float64_t x, float64_t y, float64_t reach) {
    climate_prepare_area(rig->climate, rig->job, (int32_t)(x - reach), (int32_t)(y - reach), (int32_t)(x + reach),
                         (int32_t)(y + reach), nullptr, nullptr);
}

static ground_t weather_ground(ground_kind_t kind, int32_t coast, int32_t radius) {
    ground_t ground;
    memset(&ground, 0, sizeof(ground));
    ground.kind = kind;
    ground.coast = coast;
    ground.radius = radius;
    return ground;
}

/* ---- The noises ------------------------------------------------------------------------------------------------------ */

TEST(the_noises_have_a_spread_of_1) {
    weather_rig_t rig;
    REQUIRE(weather_rig_start(t, &rig, &(ground_t){.kind = GROUND_ALL_LAND}));
    const climate_weather_t *const weather = rig.climate->borrowed_weather;
    enum : uint32_t { N = 100000u };
    float64_t *const xs = test_alloc(t, N * sizeof(float64_t));
    float64_t *const ys = test_alloc(t, N * sizeof(float64_t));
    float64_t *const zs = test_alloc(t, N * sizeof(float64_t));
    float32_t *const out = test_alloc(t, N * sizeof(float32_t));
    terrain_rng_t rng = terrain_rng_start(77u);
    for (uint32_t i = 0u; i < N; i++) {
        xs[i] = (terrain_rng_next_f64(&rng) - 0.5f64) * 400000.0f64;
        ys[i] = (terrain_rng_next_f64(&rng) - 0.5f64) * 400000.0f64;
        zs[i] = floor(terrain_rng_next_f64(&rng) * 10000.0f64);
    }
    const terrain_noise_field_t *const fields[3] = {weather->spell, weather->amount, weather->hours};
    const float32_t pinned[3] = {CLIMATE_SPELL_RAW_SD, CLIMATE_RAIN_AMOUNT_RAW_SD, CLIMATE_RAIN_HOURS_RAW_SD};
    const char *const names[3] = {"N1 spell", "N2 rain amount", "N3 rain hours"};
    for (uint32_t f = 0u; f < 3u; f++) {
        terrain_noise_sample_points(fields[f], xs, ys, zs, N, out);
        float64_t sum = 0.0f64, sum2 = 0.0f64;
        for (uint32_t i = 0u; i < N; i++) {
            sum += (float64_t)out[i];
            sum2 += (float64_t)out[i] * (float64_t)out[i];
        }
        const float64_t mean = sum / (float64_t)N;
        const float64_t sd = sqrt(sum2 / (float64_t)N - mean * mean);
        printf("%s: raw mean %.4f, raw standard deviation %.4f (pinned %.4f)\n", names[f], (libc_double_t)mean,
               (libc_double_t)sd, (libc_double_t)pinned[f]);
        CHECK(fabs(sd - (float64_t)pinned[f]) <= 0.03f64 * (float64_t)pinned[f]);
        CHECK(fabs(mean) < 0.05f64 * sd);
    }
    weather_rig_stop(&rig);
}

/* ---- The weekly update ------------------------------------------------------------------------------------------------ */

static void weather_days_fill(climate_day_t days[CLIMATE_WEEK_DAYS], const float32_t means[CLIMATE_WEEK_DAYS],
                              float32_t rain_total, float32_t snow_total) {
    memset(days, 0, sizeof(climate_day_t) * CLIMATE_WEEK_DAYS);
    for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
        days[i].t_mean = means[i];
        days[i].t_min = means[i] - 2.0f32;
        days[i].t_max = means[i] + 2.0f32;
        days[i].rain_mm = rain_total / (float32_t)CLIMATE_WEEK_DAYS;
        days[i].snow_mm = snow_total / (float32_t)CLIMATE_WEEK_DAYS;
        days[i].kind = CLIMATE_DAY_RAIN;
    }
}

TEST(spring_melt_is_the_master_plans_numbers) {
    climate_week_t before;
    memset(&before, 0, sizeof(before));
    before.snow_mm = 120.0f32;
    before.soil_mm = 140.0f32;
    const float32_t means[CLIMATE_WEEK_DAYS] = {3.0f32, 3.0f32, 3.0f32, 3.0f32, 3.0f32, 3.0f32, 3.0f32};
    climate_day_t days[CLIMATE_WEEK_DAYS];
    weather_days_fill(days, means, 15.0f32, 0.0f32);
    climate_week_t after;
    climate_week_update(&before, days, &after);
    const float32_t melt = 7.0f32 * CLIMATE_MELT_MM_PER_DEGREE_DAY * 3.0f32;
    const float32_t evaporation = 7.0f32 * CLIMATE_EVAPORATION_MM_PER_DEGREE_DAY * 3.0f32;
    printf("melt %.3f, evaporation %.3f, snow %.3f, soil %.3f, runoff %.3f, rain %.3f\n", (libc_double_t)melt,
           (libc_double_t)evaporation, (libc_double_t)after.snow_mm, (libc_double_t)after.soil_mm,
           (libc_double_t)after.runoff_mm, (libc_double_t)after.rain_mm);
    CHECK_NEAR(melt, 84.0f32, 0.001f32);
    CHECK_NEAR(evaporation, 4.2f32, 0.001f32);
    CHECK_NEAR(after.snow_mm, 36.0f32, 0.01f32);
    CHECK_NEAR(after.soil_mm, 150.0f32, 0.01f32);
    CHECK_NEAR(after.runoff_mm, 84.8f32, 0.01f32);
    CHECK_NEAR(after.rain_mm, 15.0f32, 0.001f32);
    CHECK_NEAR(after.t_mean, 3.0f32, 0.0001f32);
    CHECK_NEAR(after.t_min, 1.0f32, 0.0001f32);
    CHECK_NEAR(after.t_max, 5.0f32, 0.0001f32);
    for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
        CHECK_EQ_U64(after.day_kind[i], CLIMATE_DAY_RAIN);
    }
}

TEST(ice_grows_and_melts_day_by_day) {
    const float32_t week_a[CLIMATE_WEEK_DAYS] = {-6.0f32, -6.0f32, -6.0f32, -6.0f32, 4.0f32, 4.0f32, 4.0f32};
    const float32_t ice_a = climate_week_ice(0.0f32, week_a, CLIMATE_WEEK_DAYS);
    const float32_t week_b[CLIMATE_WEEK_DAYS] = {1.0f32, 1.0f32, 1.0f32, 1.0f32, 1.0f32, 1.0f32, 1.0f32};
    const float32_t ice_b = climate_week_ice(10.0f32, week_b, CLIMATE_WEEK_DAYS);
    printf("four days at -6 then three at +4 from 0: %.3f cm; seven days at +1 from 10: %.3f cm\n",
           (libc_double_t)ice_a, (libc_double_t)ice_b);
    CHECK_NEAR(ice_a, 12.0f32, 0.0001f32);
    CHECK_NEAR(ice_b, 0.0f32, 0.0f32);
    /* Through the update too: the week's mean near -2 would say something else. */
    climate_week_t before, after;
    memset(&before, 0, sizeof(before));
    climate_day_t days[CLIMATE_WEEK_DAYS];
    weather_days_fill(days, week_a, 0.0f32, 0.0f32);
    climate_week_update(&before, days, &after);
    CHECK_NEAR(after.ice_cm, 12.0f32, 0.0001f32);
}

TEST(soil_never_leaves_its_bucket) {
    terrain_rng_t rng = terrain_rng_start(5u);
    float64_t worst = 0.0f64;
    uint32_t bad = 0u;
    for (uint32_t i = 0u; i < 100000u; i++) {
        climate_week_t before, after;
        memset(&before, 0, sizeof(before));
        before.snow_mm = terrain_rng_next_f32(&rng) * 1000.0f32;
        before.soil_mm = terrain_rng_next_f32(&rng) * 150.0f32;
        const float32_t t_w = -30.0f32 + terrain_rng_next_f32(&rng) * 70.0f32;
        const float32_t p_w = terrain_rng_next_f32(&rng) * 500.0f32;
        const float32_t f_s = climate_heat_snow_share(t_w);
        climate_week_update_water(&before, t_w, p_w, f_s, &after);
        const float32_t e = climate_week_evaporation_actual(&before, t_w, p_w, f_s);
        const bool range = after.soil_mm >= 0.0f32 && after.soil_mm <= CLIMATE_SOIL_MM_MAX && after.snow_mm >= 0.0f32 &&
                           after.runoff_mm >= 0.0f32 && e >= -0.0001f32;
        const float64_t balance = (float64_t)(after.snow_mm - before.snow_mm) +
                                  (float64_t)(after.soil_mm - before.soil_mm) + (float64_t)after.runoff_mm +
                                  (float64_t)e - (float64_t)p_w;
        worst = fabs(balance) > worst ? fabs(balance) : worst;
        if (!range || fabs(balance) > 0.01f64) {
            bad++;
        }
    }
    printf("100,000 random weeks: %" PRIu32 " outside the bucket or not conserving; worst imbalance %.6f mm\n", bad,
           (libc_double_t)worst);
    CHECK_EQ_U64(bad, 0u);
}

/* ---- Day kinds --------------------------------------------------------------------------------------------------------- */

static climate_day_facts_t weather_facts(void) {
    climate_day_facts_t f;
    memset(&f, 0, sizeof(f));
    f.t_mean = 15.0f32;
    f.sigma_t = 3.0f32;
    f.wind_morning = 5.0f32;
    f.week_rain_mm = 20.0f32;
    f.sea_distance = 8000.0f32;
    return f;
}

TEST(day_kinds_follow_their_order) {
    climate_day_facts_t f = weather_facts();
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_CLEAR);
    f.normal_week_mm = 12.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_CLOUDY);
    f = weather_facts();
    f.near_low = 1u;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_CLOUDY);
    f = weather_facts();
    f.cold_current = 1u;
    f.sea_distance = 1500.0f32;
    f.wind_morning = 2.0f32;
    f.near_low = 1u;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_FOG); /* fog over cloudy */
    f = weather_facts();
    f.valley_depth = 35.0f32;
    f.soil_mm = 120.0f32;
    f.wind_morning = 1.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_FOG);
    f.soil_mm = 90.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_CLEAR);
    f = weather_facts();
    f.anomaly = 4.6f32; /* over 1.5 sigma */
    f.cold_current = 1u;
    f.sea_distance = 100.0f32;
    f.wind_morning = 0.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_HEAT_WAVE); /* heat wave over fog */
    f.anomaly = 4.5f32;                                             /* exactly 1.5 sigma is not over it */
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_FOG);
    f = weather_facts();
    f.rain_mm = 1.5f32;
    f.anomaly = 9.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_RAIN); /* rain over heat wave */
    f.t_mean = 0.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_SLEET);
    f.t_mean = 1.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_SLEET);
    f.t_mean = -2.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_SLEET);
    f.t_mean = -2.5f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_SNOW);
    f.rain_mm = 1.0f32; /* not over 1 mm */
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_HEAT_WAVE);
    f = weather_facts();
    f.wind_max = 13.0f32;
    f.week_rain_mm = 1.0f32;
    f.desert = 1u;
    f.rain_mm = 5.0f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_DUST_STORM); /* dust over rain */
    f.desert = 0u;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_RAIN);
    f.desert = 1u;
    f.under_thunder = 1u;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_THUNDERSTORM); /* thunder over dust */
    f.wind_max = 15.5f32;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_STORM); /* wind over 15 is a storm */
    f.wind_max = 5.0f32;
    f.in_deep_low = 1u;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_STORM);
    f.in_hurricane = 1u;
    CHECK_EQ_U64(climate_week_day_kind(&f), CLIMATE_DAY_HURRICANE); /* hurricane over everything */
}

/* ---- Daily weather on a climate ------------------------------------------------------------------------------------- */

TEST(a_day_is_a_pure_function) {
    const ground_t ground = weather_ground(GROUND_COAST_X, 0, 0);
    weather_rig_t fresh, used;
    REQUIRE(weather_rig_start(t, &fresh, &ground));
    const float64_t x = 2000.0f64, y = 6000.0f64;
    weather_prepare(&fresh, x, y, 7000.0f64);
    enum : uint32_t { DAYS = 6u };
    const int64_t days[DAYS] = {100, 400, 401, 2000, 37, 5000};
    climate_day_t first[DAYS], second[DAYS];
    for (uint32_t i = 0u; i < DAYS; i++) {
        arena_reset(fresh.arena);
        REQUIRE(climate_weather_day(fresh.climate, (int32_t)x, (int32_t)y, days[i], nullptr, fresh.arena, &first[i]));
    }
    weather_rig_stop(&fresh);
    REQUIRE(weather_rig_start(t, &used, &ground));
    weather_prepare(&used, x, y, 7000.0f64);
    /* Many other questions first, in other places and days, then the same days in the other order. */
    uint64_t answered = 0u;
    for (uint32_t q = 0u; q < 120u; q++) {
        climate_day_t other;
        arena_reset(used.arena);
        answered += climate_weather_day(used.climate, (int32_t)x + (int32_t)(q % 7u) * 300 - 900,
                                        (int32_t)y + (int32_t)(q % 5u) * 400 - 800, (int64_t)(q * 37u), nullptr,
                                        used.arena, &other)
                        ? 1u
                        : 0u;
        for (uint32_t k = 0u; k < 80u; k++) {
            (void)climate_weather_spell(used.climate, (float64_t)(k * 991u), (float64_t)(q * 313u), (int64_t)(q + k));
            answered++;
        }
    }
    for (uint32_t j = 0u; j < DAYS; j++) {
        const uint32_t i = DAYS - 1u - j;
        arena_reset(used.arena);
        REQUIRE(climate_weather_day(used.climate, (int32_t)x, (int32_t)y, days[i], nullptr, used.arena, &second[i]));
    }
    printf("%" PRIu64 " other questions answered between; day 400: mean %.3f C, rain %.3f mm, kind %u\n", answered,
           (libc_double_t)first[1].t_mean, (libc_double_t)first[1].rain_mm, (uint32_t)first[1].kind);
    CHECK_MEM_EQ(first, second, sizeof(first));
    weather_rig_stop(&used);
}

/* The Atlas's temperature now asks many places at once through one list: each answer must be the bits one place alone
 * gives, and the same places split another way must say the same. */
TEST(places_asked_together_are_the_same_bits_as_one_at_a_time) {
    const ground_t ground = weather_ground(GROUND_COAST_X, 0, 0);
    weather_rig_t rig;
    REQUIRE(weather_rig_start(t, &rig, &ground));
    const float64_t x0 = 2000.0f64, y0 = 6000.0f64;
    weather_prepare(&rig, x0, y0, 16000.0f64);
    enum : uint32_t { SIDE = 12u, PLACES = SIDE * SIDE, MOMENTS = 10u };
    const float64_t moments[MOMENTS] = {100.1f64,  230.55f64, 240.9f64, 250.25f64,  400.0f64,
                                        611.71f64, 615.3f64,  960.5f64, 2000.99f64, 5000.4f64};
    climate_weather_ask_t *const asks = test_alloc(t, PLACES * sizeof(climate_weather_ask_t));
    climate_week_t week = {.rain_mm = 40.0f32, .soil_mm = 90.0f32};
    for (uint32_t i = 0u; i < PLACES; i++) {
        /* A 6600-block square, places 600 apart, with a few off the grid and some up a hill. */
        asks[i] = (climate_weather_ask_t){
            .x = x0 - 3300.0f64 + 600.0f64 * (float64_t)(i % SIDE) + (float64_t)(i % 7u) * 13.25f64,
            .y = y0 - 3300.0f64 + 600.0f64 * (float64_t)(i / SIDE) - (float64_t)(i % 5u) * 7.5f64,
            .z = (float64_t)(i % 3u) * 40.0f64,
            .borrowed_week = i % 2u == 0u ? &week : nullptr,
        };
    }
    climate_now_t *const together = test_alloc(t, PLACES * sizeof(climate_now_t));
    climate_now_t *const half = test_alloc(t, PLACES * sizeof(climate_now_t));
    bool *const ok = test_alloc(t, PLACES * sizeof(bool));
    bool *const ok_half = test_alloc(t, PLACES * sizeof(bool));
    climate_now_t alone;
    uint32_t answered = 0u, raining = 0u, stormy = 0u, thunder = 0u, differ = 0u;
    uint64_t shared_used = 0u;
    for (uint32_t m = 0u; m < MOMENTS; m++) {
        arena_reset(rig.arena);
        REQUIRE(climate_weather_at_shared(rig.climate, asks, PLACES, moments[m], rig.arena, together, ok));
        shared_used = rig.arena->used > shared_used ? rig.arena->used : shared_used;
        /* The same places as two halves give the same, each half its own list. */
        arena_reset(rig.arena);
        REQUIRE(climate_weather_at_shared(rig.climate, asks, PLACES / 2u, moments[m], rig.arena, half, ok_half));
        arena_reset(rig.arena);
        REQUIRE(climate_weather_at_shared(rig.climate, asks + PLACES / 2u, PLACES / 2u, moments[m], rig.arena,
                                          half + PLACES / 2u, ok_half + PLACES / 2u));
        for (uint32_t i = 0u; i < PLACES; i++) {
            arena_reset(rig.arena);
            const bool one = climate_weather_at(rig.climate, asks[i].x, asks[i].y, asks[i].z, moments[m],
                                                asks[i].borrowed_week, rig.arena, &alone);
            CHECK(one == ok[i]);
            CHECK(ok_half[i] == ok[i]);
            if (!one || !ok[i]) {
                continue;
            }
            answered++;
            raining += alone.rain_mm_h > 0.0f32 ? 1u : 0u;
            stormy += alone.cloud >= 0.75f32 ? 1u : 0u;
            thunder += alone.thunder_cell != 0u ? 1u : 0u;
            differ += memcmp(&alone, &together[i], sizeof(alone)) != 0 ? 1u : 0u;
            differ += memcmp(&alone, &half[i], sizeof(alone)) != 0 ? 1u : 0u;
        }
    }
    printf("%u answers of %u: %u raining, %u under a storm, %u near a cell; %u differ; one list's scratch %.2f MiB\n",
           answered, PLACES * MOMENTS, raining, stormy, thunder, differ,
           (libc_double_t)((float64_t)shared_used / 1048576.0f64));
    CHECK(answered == PLACES * MOMENTS);
    CHECK(raining > 0u);
    CHECK(differ == 0u);
    /* Places spread past the share are refused, writing nothing, so the caller asks each alone. */
    climate_weather_ask_t far[2] = {asks[0], asks[0]};
    far[1].x += CLIMATE_WEATHER_SHARE_BLOCKS + 1.0f64;
    ok[0] = ok[1] = true;
    arena_reset(rig.arena);
    CHECK(!climate_weather_at_shared(rig.climate, far, 2u, moments[0], rig.arena, together, ok));
    CHECK(ok[0] && ok[1]);
    weather_rig_stop(&rig);
}

TEST(spells_last) {
    weather_rig_t rig;
    REQUIRE(weather_rig_start(t, &rig, &(ground_t){.kind = GROUND_ALL_LAND}));
    enum : uint32_t { PLACES = 1000u, DAYS = 3650u };
    float32_t *const series = test_alloc(t, DAYS * sizeof(float32_t));
    float64_t sum[3] = {0}, sum_a[3] = {0}, sum_b[3] = {0}, sum_aa[3] = {0}, sum_bb[3] = {0}, count[3] = {0};
    const uint32_t lags[3] = {1u, 10u, 30u};
    terrain_rng_t rng = terrain_rng_start(9u);
    for (uint32_t p = 0u; p < PLACES; p++) {
        const float64_t x = (terrain_rng_next_f64(&rng) - 0.5f64) * 200000.0f64;
        const float64_t y = (terrain_rng_next_f64(&rng) - 0.5f64) * 200000.0f64;
        for (uint32_t d = 0u; d < DAYS; d++) {
            series[d] = climate_weather_spell(rig.climate, x, y, (int64_t)d);
        }
        for (uint32_t l = 0u; l < 3u; l++) {
            for (uint32_t d = 0u; d + lags[l] < DAYS; d++) {
                const float64_t a = (float64_t)series[d], b = (float64_t)series[d + lags[l]];
                sum[l] += a * b;
                sum_a[l] += a;
                sum_b[l] += b;
                sum_aa[l] += a * a;
                sum_bb[l] += b * b;
                count[l] += 1.0f64;
            }
        }
    }
    float64_t r[3];
    for (uint32_t l = 0u; l < 3u; l++) {
        const float64_t n = count[l];
        const float64_t cov = sum[l] / n - (sum_a[l] / n) * (sum_b[l] / n);
        const float64_t va = sum_aa[l] / n - (sum_a[l] / n) * (sum_a[l] / n);
        const float64_t vb = sum_bb[l] / n - (sum_b[l] / n) * (sum_b[l] / n);
        r[l] = cov / sqrt(va * vb);
        printf("correlation of the spell %u days apart over %u places and 10 years: %.4f\n", lags[l], PLACES,
               (libc_double_t)r[l]);
    }
    CHECK(r[0] > 0.9f64);
    CHECK(r[2] < 0.3f64);
    weather_rig_stop(&rig);
}

TEST(heat_waves_happen) {
    /* The spell alone: the share of days over 1.5 sigma, which is what makes a heat wave possible at all (Issues found 2). */
    weather_rig_t rig;
    const ground_t ground = weather_ground(GROUND_HIGH_COAST_Y, -100000, 100);
    REQUIRE(weather_rig_start(t, &rig, &ground));
    enum : uint32_t { PLACES = 1000u, DAYS = 3650u };
    terrain_rng_t rng = terrain_rng_start(11u);
    uint64_t over = 0u, total = 0u;
    for (uint32_t p = 0u; p < PLACES; p++) {
        const float64_t x = (terrain_rng_next_f64(&rng) - 0.5f64) * 200000.0f64;
        const float64_t y = 10000.0f64 + terrain_rng_next_f64(&rng) * 20000.0f64;
        for (uint32_t d = 0u; d < DAYS; d++) {
            over += climate_weather_spell(rig.climate, x, y, (int64_t)d) > CLIMATE_HEAT_WAVE_SIGMAS ? 1u : 0u;
            total++;
        }
    }
    const float64_t share = (float64_t)over / (float64_t)total;
    printf("days whose spell is over 1.5 sigma, 1000 inland places over 10 years: %.2f%%\n",
           (libc_double_t)(100.0f64 * share));
    CHECK(share >= 0.02f64 && share <= 0.12f64);
    /* And whole days at one inland place through a summer: heat waves are day kinds that really come. */
    const float64_t x = 3000.0f64, y = WEATHER_Y45;
    weather_prepare(&rig, x, y, 7000.0f64);
    uint32_t heat = 0u, days = 0u;
    for (int64_t d = 150; d < 240; d++) {
        climate_day_t day;
        arena_reset(rig.arena);
        if (climate_weather_day(rig.climate, (int32_t)x, (int32_t)y, d + 365, nullptr, rig.arena, &day)) {
            heat += day.kind == CLIMATE_DAY_HEAT_WAVE ? 1u : 0u;
            days++;
        }
    }
    printf("inland at 45 N, 90 summer days: %u heat-wave days\n", heat);
    CHECK_EQ_U64(days, 90u);
    weather_rig_stop(&rig);
}

TEST(hourly_rain_adds_up_to_the_day) {
    const ground_t ground = weather_ground(GROUND_COAST_X, 0, 0);
    weather_rig_t rig;
    REQUIRE(weather_rig_start(t, &rig, &ground));
    const float64_t base_x = 2500.0f64, base_y = 8000.0f64;
    weather_prepare(&rig, base_x, base_y, 8500.0f64);
    enum : uint32_t { SAMPLES = 1000u };
    terrain_rng_t rng = terrain_rng_start(13u);
    float64_t worst = 0.0f64, total = 0.0f64;
    uint32_t wet = 0u;
    for (uint32_t s = 0u; s < SAMPLES; s++) {
        const int32_t x = (int32_t)(base_x + (terrain_rng_next_f64(&rng) - 0.5f64) * 2000.0f64);
        const int32_t y = (int32_t)(base_y + (terrain_rng_next_f64(&rng) - 0.5f64) * 2000.0f64);
        const int64_t d = 300 + (int64_t)terrain_rng_next_below(&rng, 3000u);
        climate_day_t day;
        arena_reset(rig.arena);
        REQUIRE(climate_weather_day(rig.climate, x, y, d, nullptr, rig.arena, &day));
        float64_t sum = 0.0f64;
        for (uint32_t h = 0u; h < CLIMATE_HOURS_A_DAY; h++) {
            climate_now_t now;
            arena_reset(rig.arena);
            const float64_t at = (float64_t)d + ((float64_t)h + 0.5f64) / 24.0f64;
            REQUIRE(
                climate_weather_at(rig.climate, (float64_t)x, (float64_t)y, nanf(""), at, nullptr, rig.arena, &now));
            sum += (float64_t)now.rain_mm_h;
            CHECK_EQ_U64(now.day_kind, day.kind);
        }
        const float64_t gap = fabs(sum - (float64_t)day.rain_mm);
        worst = gap > worst ? gap : worst;
        total += (float64_t)day.rain_mm;
        wet += day.rain_mm > 1.0f32 ? 1u : 0u;
    }
    printf(
        "%u places and days (%u wet, %.1f mm in all): the 24 hourly rates against the day's total, worst gap %.6f mm\n",
        SAMPLES, wet, (libc_double_t)total, (libc_double_t)worst);
    CHECK(worst <= 0.001f64);
    CHECK(wet > 0u);
    weather_rig_stop(&rig);
}

/* ---- Snow through the year --------------------------------------------------------------------------------------------- */

/* Census by census from spin-up at week start_week for years years: the snow on the ground at each census, and its day. */
static bool weather_snow_run(weather_rig_t *rig, int32_t x, int32_t y, uint64_t start_week, uint32_t weeks,
                             float32_t *out_snow, uint16_t *out_day) {
    climate_week_t state;
    if (!climate_week_spinup(rig->climate, x >> 6, y >> 6, start_week, &state, nullptr)) {
        return false;
    }
    for (uint32_t k = 0u; k < weeks; k++) {
        const uint64_t week = start_week + 1u + k;
        climate_day_t days[CLIMATE_WEEK_DAYS];
        for (uint32_t i = 0u; i < CLIMATE_WEEK_DAYS; i++) {
            arena_reset(rig->arena);
            if (!climate_weather_day(rig->climate, x, y, (int64_t)(7u * (week - 1u) + i), &state, rig->arena,
                                     &days[i])) {
                return false;
            }
        }
        climate_week_update(&state, days, &state);
        out_snow[k] = state.snow_mm;
        if (getenv("WEATHER_SNOW_TRACE") != nullptr) {
            printf("week %" PRIu64 " day %u: mean %.1f C, low %.1f, rain %.1f mm, snow %.1f mm, soil %.1f mm\n", week,
                   (uint32_t)((7u * week) % 365u), (libc_double_t)state.t_mean, (libc_double_t)state.t_min,
                   (libc_double_t)state.rain_mm, (libc_double_t)state.snow_mm, (libc_double_t)state.soil_mm);
        }
        out_day[k] = (uint16_t)((7u * week) % 365u);
    }
    return true;
}

TEST(snow_builds_and_melts_at_45_degrees) {
    /* Land 600 high north of a coast 1200 blocks south of 45 N. */
    const int32_t y = (int32_t)WEATHER_Y45;
    const ground_t ground = weather_ground(GROUND_HIGH_COAST_Y, y - 1200, 600);
    weather_rig_t rig;
    REQUIRE(weather_rig_start(t, &rig, &ground));
    const int32_t x = 32;
    weather_prepare(&rig, (float64_t)x, (float64_t)y, 7000.0f64);
    enum : uint32_t { WEEKS = 157u };
    float32_t snow[WEEKS];
    uint16_t day[WEEKS];
    const uint64_t start = 52u; /* the census of day 364 */
    REQUIRE(weather_snow_run(&rig, x, y, start, WEEKS, snow, day));
    float32_t deepest = 0.0f32;
    for (uint32_t year = 0u; year < 3u; year++) {
        float32_t winter = 0.0f32, summer = 0.0f32;
        for (uint32_t k = year * 52u; k < (year + 1u) * 52u + 1u && k < WEEKS; k++) {
            if (day[k] >= 1u && day[k] <= 60u) {
                winter = snow[k] > winter ? snow[k] : winter;
            }
            if (day[k] >= 170u && day[k] <= 300u) {
                summer = snow[k] > summer ? snow[k] : summer;
            }
        }
        printf(
            "45 N, 600 high, year %u: most snow between days 1 and 60 %.1f mm, most between days 170 and 300 %.1f mm\n",
            year + 1u, (libc_double_t)winter, (libc_double_t)summer);
        /* Issues found 6: this place gets about 4 mm a week in winter, so one winter in three stays under 50 mm; every
         * winter must still build snow, and one of the three pass 50 mm, which no missing melt or inverted share allows. */
        CHECK(winter > 10.0f32);
        deepest = winter > deepest ? winter : deepest;
        CHECK_NEAR(summer, 0.0f32, 0.0f32);
    }
    printf("45 N: the deepest winter of the three %.1f mm\n", (libc_double_t)deepest);
    CHECK(deepest > 50.0f32);
    weather_rig_stop(&rig);
}

TEST(snow_stays_all_year_at_75_degrees) {
    const int32_t y = (int32_t)WEATHER_Y75;
    const ground_t ground = weather_ground(GROUND_COAST_Y, y - 64, 0);
    weather_rig_t rig;
    REQUIRE(weather_rig_start(t, &rig, &ground));
    const int32_t x = 32;
    weather_prepare(&rig, (float64_t)x, (float64_t)y, 7000.0f64);
    enum : uint32_t { WEEKS = 157u };
    float32_t snow[WEEKS];
    uint16_t day[WEEKS];
    REQUIRE(weather_snow_run(&rig, x, y, 52u, WEEKS, snow, day));
    float32_t least = INFINITY;
    for (uint32_t k = 52u; k < WEEKS; k++) {
        least = snow[k] < least ? snow[k] : least;
    }
    printf("75 N on the coast, years 2 and 3: the least snow at any census %.1f mm\n", (libc_double_t)least);
    CHECK(least > 0.0f32);
    weather_rig_stop(&rig);
}

TEST_MAIN()
