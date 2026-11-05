/* ctest run_calendar. See README.md. */
#include "harness/index.h"

#include "calendar/index.h"

#include <pthread.h>
#include <string.h>

enum : uint32_t {
    PURE_THREADS = 16u,
    PURE_CLOCKS = 100000u,
};

/* The clock at c days, to the nearest unit. 0.35 is not exact in binary, and rounding to the nearest unit is what keeps
 * 08:24 from reading 08:23: see the phase 01 file, Issues found 7. */
static calendar_clock_t clock_of(uint64_t days, uint64_t units_of_day) {
    return days * CALENDAR_CLOCK_DAY + units_of_day;
}

/* 0.35 of a day, rounded to the nearest 1/2^32: 1503238553.6 goes to ...554. */
static const uint64_t UNITS_0_35 = 1503238554u;

/* A calendar day count D's clock at 08:24, with D0 = 79. */
static calendar_clock_t clock_of_day(uint64_t day) {
    return clock_of(day - CALENDAR_DAY_ZERO, UNITS_0_35);
}

TEST(a_new_world_starts_on_day_80_of_year_1_in_week_11_at_08_24) {
    const calendar_date_t date = calendar_date_of(CALENDAR_DAY_ZERO, clock_of(0u, UNITS_0_35));
    CHECK_EQ_U64(date.day, 79u);
    CHECK_EQ_U64(date.year, 1u);
    CHECK_EQ_U64(date.day_of_year, 79u);
    CHECK_EQ_U64(date.week, 11u);
    CHECK_EQ_U64(date.hour, 8u);
    CHECK_EQ_U64(date.minute, 24u);
    CHECK_EQ_U64(calendar_week_of(CALENDAR_DAY_ZERO, clock_of(0u, UNITS_0_35)), 11u);
}

TEST(a_world_1000_days_old_is_year_3_day_350_week_154) {
    const calendar_date_t date = calendar_date_of(CALENDAR_DAY_ZERO, clock_of(1000u, UNITS_0_35));
    CHECK_EQ_U64(date.day, 1079u);
    CHECK_EQ_U64(date.year, 3u);
    CHECK_EQ_U64(date.day_of_year, 349u);
    CHECK_EQ_U64(date.week, 154u);
    /* The worked example's clock in units, as the phase file writes it out for 5.25. */
    CHECK_EQ_U64(clock_of(5u, CALENDAR_CLOCK_DAY_START), 22548578304ull);
}

TEST(the_calendar_day_starts_at_06_00_and_not_at_midnight) {
    /* 0.1 s is 0.1 / 86400 of a day, 4971 units. */
    const calendar_date_t before = calendar_date_of(CALENDAR_DAY_ZERO, clock_of(5u, CALENDAR_CLOCK_DAY_START - 4971u));
    const calendar_date_t at = calendar_date_of(CALENDAR_DAY_ZERO, clock_of(5u, CALENDAR_CLOCK_DAY_START));
    CHECK_EQ_U64(before.day, 83u);
    CHECK_EQ_U64(at.day, 84u);
    CHECK_EQ_U64(before.hour, 5u);
    CHECK_EQ_U64(before.minute, 59u);
    CHECK_EQ_U64(at.hour, 6u);
    CHECK_EQ_U64(at.minute, 0u);
    /* One unit is enough: off by one would show here. */
    CHECK_EQ_U64(calendar_day_of(CALENDAR_DAY_ZERO, clock_of(5u, CALENDAR_CLOCK_DAY_START - 1u)), 83u);
    /* Midnight is still the day before. */
    CHECK_EQ_U64(calendar_day_of(CALENDAR_DAY_ZERO, clock_of(5u, 0u)), 83u);
}

TEST(day_364_rolls_to_day_0_of_the_next_year_and_weeks_carry_across) {
    const uint64_t days[5] = {364u, 365u, 371u, 729u, 730u};
    const uint64_t years[5] = {1u, 2u, 2u, 2u, 3u};
    const uint64_t of_year[5] = {364u, 0u, 6u, 364u, 0u};
    const uint64_t weeks[5] = {52u, 52u, 53u, 104u, 104u};
    for (uint32_t i = 0u; i < 5u; i++) {
        const calendar_date_t date = calendar_date_of(CALENDAR_DAY_ZERO, clock_of_day(days[i]));
        CHECK_EQ_U64(date.day, days[i]);
        CHECK_EQ_U64(date.year, years[i]);
        CHECK_EQ_U64(date.day_of_year, of_year[i]);
        CHECK_EQ_U64(date.week, weeks[i]);
    }
}

TEST(every_week_is_seven_days_across_ten_years) {
    uint64_t changes = 0u;
    uint64_t run = 1u;
    uint64_t longest = 0u;
    uint64_t shortest = UINT64_MAX;
    uint64_t last = calendar_date_of(0u, clock_of(0u, UNITS_0_35)).week;
    CHECK_EQ_U64(last, 0u);
    for (uint64_t day = 1u; day <= 3650u; day++) {
        const uint64_t week = calendar_date_of(0u, clock_of(day, UNITS_0_35)).week;
        if (week != last) {
            CHECK_EQ_U64(week, last + 1u);
            CHECK_EQ_U64(day % 7u, 0u);
            changes++;
            longest = run > longest ? run : longest;
            shortest = run < shortest ? run : shortest;
            run = 0u;
        }
        run++;
        last = week;
    }
    CHECK_EQ_U64(changes, 521u);
    CHECK_EQ_U64(longest, 7u);
    CHECK_EQ_U64(shortest, 7u);
}

TEST(a_clock_before_the_first_morning_is_day_zero_and_not_a_huge_number) {
    /* 0.1 of a day is 02:24, before the first 06:00. */
    CHECK_EQ_U64(calendar_day_of(0u, clock_of(0u, 429496730u)), 0u);
    CHECK_EQ_U64(calendar_week_of(0u, clock_of(0u, 429496730u)), 0u);
    CHECK_EQ_U64(calendar_date_of(0u, 0u).year, 1u);
    /* With D0 79 it is the day before: 78. */
    CHECK_EQ_U64(calendar_day_of(CALENDAR_DAY_ZERO, 0u), 78u);
    /* The largest clock the game keeps still works. */
    CHECK_EQ_U64(calendar_day_of(CALENDAR_DAY_ZERO, UINT64_MAX), CALENDAR_DAY_ZERO + 0xFFFFFFFFull);
}

static void check_label(test_context_t *t, uint16_t day_of_year, bool south, uint8_t season, uint8_t part) {
    const calendar_season_label_t label = calendar_season_temperate(day_of_year, south);
    /* The day and side go in the high bits, so a failure names them: day * 1000 + south * 100 + the value. */
    const uint64_t tag = (uint64_t)day_of_year * 1000u + (south ? 100u : 0u);
    CHECK_EQ_U64(tag + label.season, tag + season);
    CHECK_EQ_U64(tag + label.part, tag + part);
}

TEST(seasons_start_on_days_79_170_262_and_353_with_three_parts_each) {
    const uint16_t starts[12] = {79u, 110u, 140u, 170u, 201u, 232u, 262u, 293u, 323u, 353u, 19u, 49u};
    for (uint32_t i = 0u; i < 12u; i++) {
        const uint8_t season = (uint8_t)(i / 3u);
        const uint8_t part = (uint8_t)(i % 3u);
        check_label(t, starts[i], false, season, part);
        /* The day before each part starts is the part before it. */
        const uint16_t before = (uint16_t)((starts[i] + CALENDAR_DAYS_PER_YEAR - 1u) % CALENDAR_DAYS_PER_YEAR);
        const uint32_t previous = (i + 11u) % 12u;
        check_label(t, before, false, (uint8_t)(previous / 3u), (uint8_t)(previous % 3u));
        /* The south has the opposite season, and the same part. */
        check_label(t, starts[i], true, (uint8_t)((season + 2u) % 4u), part);
    }
    check_label(t, 262u, true, CALENDAR_SEASON_SPRING, CALENDAR_PART_EARLY);
    check_label(t, 79u, true, CALENDAR_SEASON_AUTUMN, CALENDAR_PART_EARLY);
    /* Winter across the year's end: day 10 is 22 days in, early. Day 0 and day 364 are early winter too. */
    check_label(t, 10u, false, CALENDAR_SEASON_WINTER, CALENDAR_PART_EARLY);
    check_label(t, 0u, false, CALENDAR_SEASON_WINTER, CALENDAR_PART_EARLY);
    check_label(t, 364u, false, CALENDAR_SEASON_WINTER, CALENDAR_PART_EARLY);
    check_label(t, 78u, false, CALENDAR_SEASON_WINTER, CALENDAR_PART_LATE);
    /* Every day of the year has a name. */
    for (uint16_t d = 0u; d < CALENDAR_DAYS_PER_YEAR; d++) {
        CHECK(calendar_season_name(calendar_season_temperate(d, false)) != nullptr);
        CHECK(calendar_season_name(calendar_season_temperate(d, true)) != nullptr);
    }
}

TEST(day_350_is_late_autumn_in_the_north_and_late_spring_in_the_south) {
    CHECK_STR_EQ(calendar_season_name(calendar_season_temperate(349u, false)), "Late autumn");
    CHECK_STR_EQ(calendar_season_name(calendar_season_temperate(349u, true)), "Late spring");
}

TEST(the_date_line_reads_as_the_hud_shows_it) {
    char line[CALENDAR_LINE_BYTES];
    const calendar_date_t old = calendar_date_of(CALENDAR_DAY_ZERO, clock_of(1000u, UNITS_0_35));
    uint32_t length = calendar_date_line(&old, calendar_season_temperate(old.day_of_year, false), line, sizeof(line));
    CHECK_STR_EQ(line, "Year 3, Day 350, Late autumn");
    CHECK_EQ_U64(length, strlen("Year 3, Day 350, Late autumn"));
    const calendar_date_t fresh = calendar_date_of(CALENDAR_DAY_ZERO, clock_of(0u, UNITS_0_35));
    length = calendar_date_line(&fresh, calendar_season_temperate(fresh.day_of_year, false), line, sizeof(line));
    CHECK_STR_EQ(line, "Year 1, Day 80, Early spring");
    CHECK_EQ_U64(length, 28u);

    /* Never past bytes, always ended. */
    char small[12];
    memset(small, 'x', sizeof(small));
    length = calendar_date_line(&fresh, calendar_season_temperate(fresh.day_of_year, false), small, 8u);
    CHECK_STR_EQ(small, "Year 1,");
    CHECK_EQ_U64(length, 7u);
    for (uint32_t i = 8u; i < sizeof(small); i++) {
        CHECK(small[i] == 'x');
    }
    small[0] = 'x';
    CHECK_EQ_U64(calendar_date_line(&fresh, calendar_season_temperate(fresh.day_of_year, false), small, 0u), 0u);
    CHECK(small[0] == 'x');
    /* The longest line fits: year 4294967295, day 365, an "Early ..." name. */
    const calendar_date_t far = {.year = UINT32_MAX, .day_of_year = 364u};
    length = calendar_date_line(&far, (calendar_season_label_t){CALENDAR_SEASON_SPRING, CALENDAR_PART_EARLY}, line,
                                sizeof(line));
    CHECK_STR_EQ(line, "Year 4294967295, Day 365, Early spring");
    CHECK(length < CALENDAR_LINE_BYTES);
}

static void check_plan(test_context_t *t, uint64_t last, uint64_t now, uint64_t first, uint64_t count, uint64_t fast) {
    const calendar_census_plan_t plan = calendar_census_plan(last, now);
    CHECK_EQ_U64(plan.count, count);
    CHECK_EQ_U64(plan.fast_weeks, fast);
    if (count > 0u) {
        CHECK_EQ_U64(plan.first_week, first);
        CHECK_EQ_U64(plan.last_week, now);
        CHECK_EQ_U64(plan.last_week - plan.first_week + 1u, plan.count);
    }
}

TEST(the_census_plan_runs_each_missed_week_once_in_order_and_the_oldest_past_52_fast) {
    check_plan(t, 11u, 12u, 12u, 1u, 0u);
    check_plan(t, 11u, 25u, 12u, 14u, 0u);
    check_plan(t, 11u, 63u, 12u, 52u, 0u);
    check_plan(t, 11u, 64u, 12u, 53u, 1u);
    check_plan(t, 11u, 111u, 12u, 100u, 48u);
    check_plan(t, 20u, 15u, 0u, 0u, 0u);
    check_plan(t, 20u, 20u, 0u, 0u, 0u);

    /* (11, 111): weeks 12 to 59 fast, 60 to 111 full, as the game's loop reads the plan. */
    const calendar_census_plan_t plan = calendar_census_plan(11u, 111u);
    uint64_t fast = 0u;
    uint64_t full = 0u;
    uint64_t expected = 12u;
    for (uint64_t w = plan.first_week; w <= plan.last_week; w++) {
        CHECK_EQ_U64(w, expected);
        expected++;
        const bool is_fast = w < plan.first_week + plan.fast_weeks;
        CHECK(is_fast == (w <= 59u));
        fast += is_fast ? 1u : 0u;
        full += is_fast ? 0u : 1u;
    }
    CHECK_EQ_U64(fast, 48u);
    CHECK_EQ_U64(full, 52u);
}

typedef struct pure_run_t {
    const calendar_clock_t *borrowed_clocks; /* 0 */
    calendar_date_t *borrowed_out;           /* 8 */
} pure_run_t;
static_assert(sizeof(pure_run_t) == 16);

static void *pure_worker(void *argument) {
    const pure_run_t *const run = argument;
    for (uint32_t i = 0u; i < PURE_CLOCKS; i++) {
        run->borrowed_out[i] = calendar_date_of(CALENDAR_DAY_ZERO, run->borrowed_clocks[i]);
    }
    return nullptr;
}

TEST(the_same_clock_gives_the_same_date_on_any_thread) {
    calendar_clock_t *const clocks = test_alloc(t, PURE_CLOCKS * sizeof(calendar_clock_t));
    calendar_date_t *const expected = test_alloc(t, PURE_CLOCKS * sizeof(calendar_date_t));
    calendar_date_t *const got = test_alloc(t, (uint64_t)PURE_THREADS * PURE_CLOCKS * sizeof(calendar_date_t));
    /* splitmix64, seeded the same every run. Clocks up to 2^40 days' worth of units would overflow the days, so the top
     * byte is cleared: up to 2^24 days. */
    uint64_t state = 0x6A09E667F3BCC909ull;
    for (uint32_t i = 0u; i < PURE_CLOCKS; i++) {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        clocks[i] = (z ^ (z >> 31)) >> 8;
        expected[i] = calendar_date_of(CALENDAR_DAY_ZERO, clocks[i]);
    }
    pthread_t threads[PURE_THREADS];
    pure_run_t runs[PURE_THREADS];
    for (uint32_t w = 0u; w < PURE_THREADS; w++) {
        runs[w] = (pure_run_t){.borrowed_clocks = clocks, .borrowed_out = got + (uint64_t)w * PURE_CLOCKS};
        REQUIRE(pthread_create(&threads[w], nullptr, pure_worker, &runs[w]) == 0);
    }
    for (uint32_t w = 0u; w < PURE_THREADS; w++) {
        REQUIRE(pthread_join(threads[w], nullptr) == 0);
    }
    for (uint32_t w = 0u; w < PURE_THREADS; w++) {
        CHECK_MEM_EQ(got + (uint64_t)w * PURE_CLOCKS, expected, PURE_CLOCKS * sizeof(calendar_date_t));
    }
}

TEST_MAIN()
