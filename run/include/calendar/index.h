/* See run/src/calendar/README.md. */
#ifndef MC_RUN_CALENDAR_INDEX_H
#define MC_RUN_CALENDAR_INDEX_H

#include "types/index.h"

#include <stdint.h>

enum : uint32_t {
    CALENDAR_DAYS_PER_YEAR = 365u,
    CALENDAR_DAYS_PER_WEEK = 7u,
    /* D0: the calendar day a new world starts on. 79 is the spring equinox, shown as day 80 (plan 004, Q2). */
    CALENDAR_DAY_ZERO = 79u,
    /* A census more than this many weeks late runs the older ones fast: plan 004 section 4.2, and Risks in its phase 01
     * file. */
    CALENDAR_FULL_WEEKS_MAX = 52u,
    /* Where each season starts, north of the equator, as a day of the year. */
    CALENDAR_SPRING_FROM = 79u,
    CALENDAR_SUMMER_FROM = 170u,
    CALENDAR_AUTUMN_FROM = 262u,
    CALENDAR_WINTER_FROM = 353u,
    CALENDAR_LINE_BYTES = 64u, /* the date line, with its NUL */
};

enum : uint64_t {
    /* The clock: whole days in the top 32 bits, the time of day in 1/2^32 of a day below, as game_t.day_units has it. */
    CALENDAR_CLOCK_DAY = 1ull << 32,
    /* 06:00, where each calendar day starts: the master plan's t0. */
    CALENDAR_CLOCK_DAY_START = 1ull << 30,
};

/* The world clock: CALENDAR_CLOCK_DAY per day since the midnight before the world's first morning. */
typedef uint64_t calendar_clock_t;

/* What a clock reading comes to. */
typedef struct calendar_date_t {
    uint64_t day;         /* 0: D, calendar days since day 0 */
    uint64_t week;        /* 8: floor(D / 7) */
    uint32_t year;        /* 16: floor(D / 365) + 1 */
    uint16_t day_of_year; /* 20: d, 0 to 364; shown as d + 1 */
    uint8_t hour;         /* 22: on the clock, 0 to 23 */
    uint8_t minute;       /* 23: 0 to 59 */
} calendar_date_t;
static_assert(sizeof(calendar_date_t) == 24);
static_assert(alignof(calendar_date_t) == 8);

typedef enum calendar_season_t : uint8_t {
    CALENDAR_SEASON_SPRING = 0,
    CALENDAR_SEASON_SUMMER = 1,
    CALENDAR_SEASON_AUTUMN = 2,
    CALENDAR_SEASON_WINTER = 3,
} calendar_season_t;

typedef enum calendar_part_t : uint8_t {
    CALENDAR_PART_EARLY = 0,
    CALENDAR_PART_MID = 1,
    CALENDAR_PART_LATE = 2,
} calendar_part_t;

typedef struct calendar_season_label_t {
    uint8_t season; /* 0: calendar_season_t */
    uint8_t part;   /* 1: calendar_part_t */
} calendar_season_label_t;
static_assert(sizeof(calendar_season_label_t) == 2);

/* Which weeks a census late by some weeks runs. Week w runs fast if w < first_week + fast_weeks. */
typedef struct calendar_census_plan_t {
    uint64_t first_week; /* 0: last_census_week + 1 */
    uint64_t last_week;  /* 8: the week now */
    uint64_t count;      /* 16: last_week - first_week + 1, or 0 for no census */
    uint64_t fast_weeks; /* 24: the oldest this many run fast; count - CALENDAR_FULL_WEEKS_MAX if that is above 0 */
} calendar_census_plan_t;
static_assert(sizeof(calendar_census_plan_t) == 32);

/* The calendar day of a clock reading: day_zero + floor((clock - CALENDAR_CLOCK_DAY_START) / CALENDAR_CLOCK_DAY), and 0 if
 * that would be below 0. day_zero must be below CALENDAR_DAYS_PER_YEAR. */
uint64_t calendar_day_of(uint32_t day_zero, calendar_clock_t clock);

/* floor(calendar_day_of / 7). */
uint64_t calendar_week_of(uint32_t day_zero, calendar_clock_t clock);

/* All of it at once. The hour and minute are of the clock, not of the calendar day: 05:59 is still the day before. Both
 * are rounded down. */
calendar_date_t calendar_date_of(uint32_t day_zero, calendar_clock_t clock);

/* The season by the table in plan 004 section 4.1, for a place north (south false) or south of the equator. day_of_year
 * must be below CALENDAR_DAYS_PER_YEAR. */
calendar_season_label_t calendar_season_temperate(uint16_t day_of_year, bool south);

/* "Early spring" ... "Late winter". Never nullptr. */
const char *calendar_season_name(calendar_season_label_t label);

/* "Year 3, Day 350, Late autumn" into out, always ended with a NUL when bytes is above 0. Returns the length written,
 * which is less than the whole line's if bytes was too small for it. */
uint32_t calendar_date_line(const calendar_date_t *date, calendar_season_label_t label, char *out, uint64_t bytes);

/* Which weeks to run when the week is now week_now and the last census was last_census_week. count 0 if week_now is not
 * above it. */
calendar_census_plan_t calendar_census_plan(uint64_t last_census_week, uint64_t week_now);

#endif
