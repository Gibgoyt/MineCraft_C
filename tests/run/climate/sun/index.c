/* ctest run_climate_sun. See README.md. */
#include "harness/index.h"

#include "climate/index.h"
#include "climate/sun/index.h"
#include "render/frame/index.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>

enum : uint32_t {
    SUN_THREADS = 16u,
    SUN_SAMPLES = 100000u,
};

/* The tilt of render_day_fill's fixed path, run/src/render/frame/index.c's RENDER_DAY_TILT, as the float32_t it is there. */
#define SUN_OLD_TILT_RADIANS ((float64_t)0.35f32)
#define SUN_MIDSUMMER 170.25f64
#define SUN_MIDWINTER 352.75f64

static uint64_t sun_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static float64_t sun_unit(uint64_t *state) {
    return (float64_t)(sun_next(state) >> 11) / 9007199254740992.0f64;
}

static float64_t sun_rad(float64_t degrees) {
    return degrees * CLIMATE_PI / 180.0f64;
}

TEST(declination_is_0_at_the_equinoxes_and_23_5_at_the_solstices) {
    CHECK_NEAR(climate_sun_declination_deg(79.0f64), 0.0f64, 1e-9f64);
    CHECK_NEAR(climate_sun_declination_deg(SUN_MIDSUMMER), 23.5f64, 1e-9f64);
    CHECK_NEAR(climate_sun_declination_deg(261.5f64), 0.0f64, 1e-9f64);
    CHECK_NEAR(climate_sun_declination_deg(SUN_MIDWINTER), -23.5f64, 1e-9f64);
    /* The phase file's table, by whole days. */
    CHECK_NEAR(climate_sun_declination_deg(170.0f64), 23.4998f64, 1e-4f64);
    CHECK_NEAR(climate_sun_declination_deg(262.0f64), -0.2023f64, 1e-3f64);
    CHECK_NEAR(climate_sun_declination_deg(353.0f64), -23.4998f64, 1e-4f64);
}

TEST(at_60_north_midsummer_is_18_51_hours_and_midwinter_5_49) {
    const float64_t summer = climate_sun_day_hours(60.0f64, 23.5f64);
    const float64_t winter = climate_sun_day_hours(60.0f64, -23.5f64);
    printf("  60 N: h0 %.4f rad; %.4f h in midsummer, %.4f h in midwinter\n",
           (libc_double_t)climate_sun_sunset_hour_angle(60.0f64, 23.5f64), (libc_double_t)summer,
           (libc_double_t)winter);
    CHECK_NEAR(climate_sun_sunset_hour_angle(60.0f64, 23.5f64), 2.4236f64, 1e-4f64);
    CHECK_NEAR(summer, 18.515f64, 0.001f64);
    CHECK_NEAR(winter, 5.485f64, 0.001f64);
    CHECK_NEAR(summer + winter, 24.0f64, 1e-9f64);
}

TEST(every_latitude_has_12_hour_days_at_the_equinox) {
    for (int32_t tenth = -900; tenth <= 900; tenth++) {
        const float64_t hours = climate_sun_day_hours((float64_t)tenth / 10.0f64, 0.0f64);
        CHECK(isfinite(hours));
        CHECK_NEAR(hours, 12.0f64, 1e-9f64);
    }
}

TEST(past_66_5_midsummer_has_24_hours_and_midwinter_none) {
    const float64_t delta = climate_sun_declination_deg(SUN_MIDSUMMER);
    for (int32_t tenth = 666; tenth <= 900; tenth++) {
        const float64_t phi = (float64_t)tenth / 10.0f64;
        CHECK_NEAR(climate_sun_day_hours(phi, delta), 24.0f64, 0.0f64);
        CHECK_NEAR(climate_sun_day_hours(-phi, delta), 0.0f64, 0.0f64);
        /* Half a year on, the other way round. */
        CHECK_NEAR(climate_sun_day_hours(phi, -delta), 0.0f64, 0.0f64);
        CHECK_NEAR(climate_sun_day_hours(-phi, -delta), 24.0f64, 0.0f64);
    }
    /* The polar circle itself: tan(66.5) tan(23.5) is 1 to rounding. */
    CHECK_NEAR(climate_sun_day_hours(66.5f64, 23.5f64), 24.0f64, 0.001f64);
}

TEST(the_south_is_the_north_half_a_year_apart) {
    uint64_t state = 0xA4093822299F31D0ull;
    for (uint32_t i = 0u; i < 1000u; i++) {
        const float64_t phi = sun_unit(&state) * 180.0f64 - 90.0f64;
        const float64_t delta = sun_unit(&state) * 47.0f64 - 23.5f64;
        const float64_t north = climate_sun_day_hours(phi, delta);
        const float64_t south = climate_sun_day_hours(-phi, -delta);
        CHECK(north == south);
    }
}

TEST(the_sun_is_a_unit_vector_and_its_height_is_the_master_plans_formula) {
    uint64_t state = 0x082EFA98EC4E6C89ull;
    float64_t worst_length = 0.0f64, worst_height = 0.0f64;
    for (uint32_t i = 0u; i < SUN_SAMPLES; i++) {
        const float64_t phi = sun_unit(&state) * 180.0f64 - 90.0f64;
        const float64_t day = sun_unit(&state) * 365.0f64;
        const float64_t hour = sun_unit(&state) * 24.0f64;
        climate_sun_t sun;
        climate_sun_at(phi, day, hour, &sun);
        const float64_t *const s = sun.direction;
        const float64_t length = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        const float64_t delta = sun_rad(climate_sun_declination_deg(day));
        const float64_t h = 2.0f64 * CLIMATE_PI * (hour - 12.0f64) / 24.0f64;
        const float64_t master = sin(sun_rad(phi)) * sin(delta) + cos(sun_rad(phi)) * cos(delta) * cos(h);
        worst_length = fabs(length - 1.0f64) > worst_length ? fabs(length - 1.0f64) : worst_length;
        worst_height = fabs(s[2] - master) > worst_height ? fabs(s[2] - master) : worst_height;
        CHECK(sun.azimuth_deg >= 0.0f64 && sun.azimuth_deg < 360.0f64);
        CHECK(sun.day_hours >= 0.0f64 && sun.day_hours <= 24.0f64);
    }
    printf("  worst |s| - 1: %.3g; worst height against the formula: %.3g\n", (libc_double_t)worst_length,
           (libc_double_t)worst_height);
    CHECK(worst_length <= 1e-12f64);
    CHECK(worst_height <= 1e-12f64);
}

TEST(at_45_north_on_the_equinox_the_9_oclock_sun_is_30_degrees_up_in_the_south_east) {
    climate_sun_t sun;
    climate_sun_at(45.0f64, 79.0f64, 9.0f64, &sun);
    CHECK_NEAR(sun.direction[0], 0.70711f64, 0.001f64);
    CHECK_NEAR(sun.direction[1], -0.5f64, 0.001f64);
    CHECK_NEAR(sun.direction[2], 0.5f64, 0.001f64);
    CHECK_NEAR(sun.elevation_deg, 30.0f64, 0.001f64);
    CHECK_NEAR(sun.azimuth_deg, 125.264f64, 0.001f64);
}

TEST(the_sun_rises_in_the_east_and_sets_in_the_west_at_the_equinox) {
    climate_sun_t rise, set;
    climate_sun_at(0.0f64, 79.0f64, 6.0f64, &rise);
    climate_sun_at(0.0f64, 79.0f64, 18.0f64, &set);
    CHECK_NEAR(rise.direction[0], 1.0f64, 1e-12f64);
    CHECK_NEAR(rise.direction[1], 0.0f64, 1e-12f64);
    CHECK_NEAR(rise.direction[2], 0.0f64, 1e-12f64);
    CHECK_NEAR(set.direction[0], -1.0f64, 1e-12f64);
    CHECK_NEAR(set.direction[1], 0.0f64, 1e-12f64);
    CHECK_NEAR(set.direction[2], 0.0f64, 1e-12f64);
    CHECK_NEAR(rise.azimuth_deg, 90.0f64, 1e-9f64);
    CHECK_NEAR(set.azimuth_deg, 270.0f64, 1e-9f64);
}

TEST(at_noon_the_sun_is_south_in_the_north_and_north_in_the_south) {
    climate_sun_t north, south;
    climate_sun_at(45.0f64, 79.0f64, 12.0f64, &north);
    climate_sun_at(-45.0f64, 79.0f64, 12.0f64, &south);
    CHECK_NEAR(north.azimuth_deg, 180.0f64, 1e-9f64);
    CHECK(south.azimuth_deg < 1e-9f64 || south.azimuth_deg > 360.0f64 - 1e-9f64);
    CHECK_NEAR(north.elevation_deg, 45.0f64, 1e-9f64);
    CHECK_NEAR(south.elevation_deg, 45.0f64, 1e-9f64);
}

TEST(the_midnight_sun_at_70_north_and_polar_night_at_80_north) {
    climate_sun_t sun;
    climate_sun_at(60.0f64, SUN_MIDSUMMER, 12.0f64, &sun);
    CHECK_NEAR(sun.elevation_deg, 53.5f64, 0.01f64);
    CHECK_NEAR(sun.azimuth_deg, 180.0f64, 0.01f64);
    climate_sun_at(60.0f64, SUN_MIDSUMMER, 0.0f64, &sun);
    CHECK_NEAR(sun.elevation_deg, -6.5f64, 0.01f64);
    climate_sun_at(70.0f64, SUN_MIDSUMMER, 0.0f64, &sun);
    CHECK_NEAR(sun.elevation_deg, 3.5f64, 0.01f64);
    CHECK(sun.azimuth_deg < 0.01f64 || sun.azimuth_deg > 359.99f64);
    CHECK_NEAR(sun.day_hours, 24.0f64, 0.0f64);
    climate_sun_at(80.0f64, SUN_MIDWINTER, 12.0f64, &sun);
    CHECK_NEAR(sun.elevation_deg, -13.5f64, 0.01f64);
    CHECK_NEAR(sun.day_hours, 0.0f64, 0.0f64);
    /* Worked examples 11 and 12: the heights the bed and mob rules then see. */
    climate_sun_at(60.0f64, SUN_MIDSUMMER, 0.0f64, &sun);
    CHECK_NEAR(sun.direction[2], -0.1132f64, 1e-4f64);
    climate_sun_at(80.0f64, SUN_MIDWINTER, 12.0f64, &sun);
    CHECK_NEAR(sun.direction[2], -0.2334f64, 1e-4f64);
}

TEST(the_fixed_sun_of_today_is_this_sun_at_20_degrees_north_on_the_equinox) {
    const float64_t phi = SUN_OLD_TILT_RADIANS * 180.0f64 / CLIMATE_PI;
    float64_t worst = 0.0f64;
    for (uint32_t step = 0u; step < 1000u; step++) {
        const float32_t fraction = (float32_t)step / 1000.0f32;
        post_settings_t post = {0};
        shade_sun_t shade = {0};
        render_day_fill(fraction, &post, &shade);
        climate_sun_t sun;
        climate_sun_at(phi, CLIMATE_SUN_EQUINOX_DAY, 24.0f64 * (float64_t)fraction, &sun);
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            const float64_t gap = fabs((float64_t)post.sun_direction[axis] - sun.direction[axis]);
            worst = gap > worst ? gap : worst;
        }
    }
    printf("  phi %.4f degrees; worst gap from render_day_fill %.3g\n", (libc_double_t)phi, (libc_double_t)worst);
    CHECK(worst <= 1e-6f64);
}

TEST(from_the_clock_a_new_world_at_08_24_on_the_equator_has_the_sun_36_degrees_up_in_the_east) {
    /* 0.35 of a day, to the nearest unit, as the calendar's test has it. */
    const calendar_clock_t clock = 1503238554u;
    climate_sun_t sun;
    climate_sun_of_clock(CALENDAR_DAY_ZERO, clock, 8.0f64, &sun);
    printf("  d_c %.4f, delta %.4f, elevation %.3f, azimuth %.3f\n",
           (libc_double_t)calendar_year_position(CALENDAR_DAY_ZERO, clock), (libc_double_t)sun.declination_deg,
           (libc_double_t)sun.elevation_deg, (libc_double_t)sun.azimuth_deg);
    CHECK_NEAR(calendar_year_position(CALENDAR_DAY_ZERO, clock), 79.1f64, 1e-6f64);
    CHECK_NEAR(sun.declination_deg, 0.0405f64, 1e-4f64);
    CHECK_NEAR(sun.elevation_deg, 36.0f64, 0.05f64);
    CHECK_NEAR(sun.azimuth_deg, 90.0f64, 0.1f64);
    CHECK(climate_sun_height_of_clock(CALENDAR_DAY_ZERO, clock, 8.0f64) == sun.direction[2]);
}

typedef struct sun_run_t {
    const calendar_clock_t *borrowed_clocks; /* 0 */
    const float64_t *borrowed_ys;            /* 8 */
    climate_sun_t *borrowed_out;             /* 16 */
    float64_t *borrowed_heights;             /* 24 */
} sun_run_t;
static_assert(sizeof(sun_run_t) == 32);

static void *sun_worker(void *argument) {
    const sun_run_t *const run = argument;
    for (uint32_t i = 0u; i < SUN_SAMPLES; i++) {
        climate_sun_of_clock(CALENDAR_DAY_ZERO, run->borrowed_clocks[i], run->borrowed_ys[i], &run->borrowed_out[i]);
        run->borrowed_heights[i] =
            climate_sun_height_of_clock(CALENDAR_DAY_ZERO, run->borrowed_clocks[i], run->borrowed_ys[i]);
    }
    return nullptr;
}

TEST(the_sun_is_the_same_on_any_thread) {
    calendar_clock_t *const clocks = test_alloc(t, SUN_SAMPLES * sizeof(calendar_clock_t));
    float64_t *const ys = test_alloc(t, SUN_SAMPLES * sizeof(float64_t));
    climate_sun_t *const expected = test_alloc(t, SUN_SAMPLES * sizeof(climate_sun_t));
    climate_sun_t *const got = test_alloc(t, (uint64_t)SUN_THREADS * SUN_SAMPLES * sizeof(climate_sun_t));
    float64_t *const heights = test_alloc(t, (uint64_t)SUN_THREADS * SUN_SAMPLES * sizeof(float64_t));
    uint64_t state = 0x452821E638D01377ull;
    for (uint32_t i = 0u; i < SUN_SAMPLES; i++) {
        clocks[i] = sun_next(&state) >> 8; /* up to 2^24 days */
        ys[i] = (sun_unit(&state) * 2.0f64 - 1.0f64) * 3.0e7f64;
        climate_sun_of_clock(CALENDAR_DAY_ZERO, clocks[i], ys[i], &expected[i]);
    }
    pthread_t threads[SUN_THREADS];
    sun_run_t runs[SUN_THREADS];
    for (uint32_t w = 0u; w < SUN_THREADS; w++) {
        runs[w] = (sun_run_t){.borrowed_clocks = clocks,
                              .borrowed_ys = ys,
                              .borrowed_out = got + (uint64_t)w * SUN_SAMPLES,
                              .borrowed_heights = heights + (uint64_t)w * SUN_SAMPLES};
        REQUIRE(pthread_create(&threads[w], nullptr, sun_worker, &runs[w]) == 0);
    }
    for (uint32_t w = 0u; w < SUN_THREADS; w++) {
        REQUIRE(pthread_join(threads[w], nullptr) == 0);
    }
    for (uint32_t w = 0u; w < SUN_THREADS; w++) {
        CHECK_MEM_EQ(got + (uint64_t)w * SUN_SAMPLES, expected, SUN_SAMPLES * sizeof(climate_sun_t));
        uint64_t height_differs = 0u;
        for (uint32_t i = 0u; i < SUN_SAMPLES; i++) {
            height_differs += heights[(uint64_t)w * SUN_SAMPLES + i] != expected[i].direction[2] ? 1u : 0u;
        }
        CHECK_EQ_U64(height_differs, 0u);
    }
}

TEST_MAIN()
