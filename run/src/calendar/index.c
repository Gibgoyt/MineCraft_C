/* See README.md. The date from the world clock: plan 004 section 4, and its phase 01 file. All integers, so the same clock is
 * the same date on any machine and any thread. */
#include "calendar/index.h"

#include "assert/index.h"

#include <stdio.h>

/* Each season's first day of the year and its length, north of the equator, in calendar_season_t order. */
static const uint16_t CALENDAR_SEASON_FROM[CALENDAR_SEASONS] = {
    CALENDAR_SPRING_FROM,
    CALENDAR_SUMMER_FROM,
    CALENDAR_AUTUMN_FROM,
    CALENDAR_WINTER_FROM,
};
static const uint16_t CALENDAR_SEASON_LENGTH[CALENDAR_SEASONS] = {
    CALENDAR_SUMMER_FROM - CALENDAR_SPRING_FROM,                          /* 91 */
    CALENDAR_AUTUMN_FROM - CALENDAR_SUMMER_FROM,                          /* 92 */
    CALENDAR_WINTER_FROM - CALENDAR_AUTUMN_FROM,                          /* 91 */
    CALENDAR_DAYS_PER_YEAR - CALENDAR_WINTER_FROM + CALENDAR_SPRING_FROM, /* 91, across the year's end */
};

/* Read-only strings in the program's data: owned by nothing. Index season * 3 + part. */
static const char *const CALENDAR_SEASON_NAMES[CALENDAR_SEASONS * CALENDAR_SEASON_PARTS] = {
    "Early spring", "Mid spring", "Late spring", "Early summer", "Mid summer", "Late summer",
    "Early autumn", "Mid autumn", "Late autumn", "Early winter", "Mid winter", "Late winter",
};

uint64_t calendar_day_of(uint32_t day_zero, calendar_clock_t clock) {
    ASSERT(day_zero < CALENDAR_DAYS_PER_YEAR);
    /* Unsigned all the way: a clock before the first 06:00 is the day before day_zero, which is below 0 only for day 0. */
    if (clock < CALENDAR_CLOCK_DAY_START) {
        return day_zero == 0u ? 0u : (uint64_t)day_zero - 1u;
    }
    return (uint64_t)day_zero + ((clock - CALENDAR_CLOCK_DAY_START) >> CALENDAR_CLOCK_FRACTION_BITS);
}

uint64_t calendar_week_of(uint32_t day_zero, calendar_clock_t clock) {
    return calendar_day_of(day_zero, clock) / CALENDAR_DAYS_PER_WEEK;
}

calendar_date_t calendar_date_of(uint32_t day_zero, calendar_clock_t clock) {
    const uint64_t day = calendar_day_of(day_zero, clock);
    /* Minutes since midnight, rounded down: the time of day times 1440, out of 2^32. Fits: 2^32 * 1440 < 2^43. */
    const uint64_t minutes =
        ((clock & (CALENDAR_CLOCK_DAY - 1u)) * CALENDAR_MINUTES_PER_DAY) >> CALENDAR_CLOCK_FRACTION_BITS;
    return (calendar_date_t){
        .day = day,
        .week = day / CALENDAR_DAYS_PER_WEEK,
        .year = (uint32_t)(day / CALENDAR_DAYS_PER_YEAR + 1u),
        .day_of_year = (uint16_t)(day % CALENDAR_DAYS_PER_YEAR),
        .hour = (uint8_t)(minutes / CALENDAR_MINUTES_PER_HOUR),
        .minute = (uint8_t)(minutes % CALENDAR_MINUTES_PER_HOUR),
    };
}

float64_t calendar_year_position(uint32_t day_zero, calendar_clock_t clock) {
    ASSERT(day_zero < CALENDAR_DAYS_PER_YEAR);
    const uint64_t year = (uint64_t)CALENDAR_DAYS_PER_YEAR * CALENDAR_CLOCK_DAY;
    /* The clock into one year first, so nothing below can pass 2^64. A whole year is added before 06:00 is taken off, so a clock
     * before a day-0 world's first morning wraps to the end of the year before and not through 2^64. */
    const uint64_t into = clock % year;
    const uint64_t position = (into + (uint64_t)day_zero * CALENDAR_CLOCK_DAY + year - CALENDAR_CLOCK_DAY_START) % year;
    /* Under 365 * 2^32 < 2^53, so exact as a float64_t, and the division by a power of two is exact too. */
    return (float64_t)position / (float64_t)CALENDAR_CLOCK_DAY;
}

calendar_season_label_t calendar_season_temperate(uint16_t day_of_year, bool south) {
    ASSERT(day_of_year < CALENDAR_DAYS_PER_YEAR);
    /* Winter until spring starts, and from its own first day: it wraps past the year's end. */
    uint32_t season = CALENDAR_SEASON_WINTER;
    for (uint32_t s = CALENDAR_SEASON_SPRING; s < CALENDAR_SEASON_WINTER; s++) {
        if (day_of_year >= CALENDAR_SEASON_FROM[s] && day_of_year < CALENDAR_SEASON_FROM[s + 1u]) {
            season = s;
        }
    }
    const uint32_t into =
        ((uint32_t)day_of_year + CALENDAR_DAYS_PER_YEAR - CALENDAR_SEASON_FROM[season]) % CALENDAR_DAYS_PER_YEAR;
    const uint32_t part = CALENDAR_SEASON_PARTS * into / CALENDAR_SEASON_LENGTH[season];
    ASSERT(part <= CALENDAR_PART_LATE);
    /* The south has the opposite season on the same days: spring and autumn swap, summer and winter swap. */
    const uint32_t named = south ? (season + CALENDAR_SOUTH_SEASON_SHIFT) % CALENDAR_SEASONS : season;
    return (calendar_season_label_t){.season = (uint8_t)named, .part = (uint8_t)part};
}

const char *calendar_season_name(calendar_season_label_t label) {
    ASSERT(label.season <= CALENDAR_SEASON_WINTER && label.part <= CALENDAR_PART_LATE);
    return CALENDAR_SEASON_NAMES[label.season * CALENDAR_SEASON_PARTS + label.part];
}

uint32_t calendar_date_line(const calendar_date_t *date, calendar_season_label_t label, char *out, uint64_t bytes) {
    if (bytes == 0u) {
        return 0u;
    }
    const int32_t written = snprintf(out, bytes, "Year %u, Day %u, %s", (uint32_t)date->year,
                                     (uint32_t)date->day_of_year + 1u, calendar_season_name(label));
    if (written < 0) {
        out[0] = '\0';
        return 0u;
    }
    /* snprintf says how long the whole line would be; what is in out is cut at bytes - 1. */
    return (uint64_t)written < bytes ? (uint32_t)written : (uint32_t)(bytes - 1u);
}

calendar_census_plan_t calendar_census_plan(uint64_t last_census_week, uint64_t week_now) {
    if (week_now <= last_census_week) {
        return (calendar_census_plan_t){.first_week = last_census_week + 1u, .last_week = week_now};
    }
    const uint64_t count = week_now - last_census_week;
    return (calendar_census_plan_t){
        .first_week = last_census_week + 1u,
        .last_week = week_now,
        .count = count,
        /* The newest CALENDAR_FULL_WEEKS_MAX run in full, since they decide the snow on the ground now. */
        .fast_weeks = count > CALENDAR_FULL_WEEKS_MAX ? count - CALENDAR_FULL_WEEKS_MAX : 0u,
    };
}
