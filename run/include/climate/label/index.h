/* See run/src/climate/label/README.md. */
#ifndef MC_RUN_CLIMATE_LABEL_INDEX_H
#define MC_RUN_CLIMATE_LABEL_INDEX_H

#include "types/index.h"

#include <stdint.h>

/* Plan 004 phase 10: which season it is at a place on a day, as master plan 4.1 labels it. Pure: the caller gives it plain
 * numbers, so the sim can call it from what the climate says, and a test from any case it likes. */

typedef enum climate_season_t : uint8_t {
    CLIMATE_SEASON_NONE = 0,
    CLIMATE_SEASON_SPRING_EARLY = 1,
    CLIMATE_SEASON_SPRING_MID = 2,
    CLIMATE_SEASON_SPRING_LATE = 3,
    CLIMATE_SEASON_SUMMER_EARLY = 4,
    CLIMATE_SEASON_SUMMER_MID = 5,
    CLIMATE_SEASON_SUMMER_LATE = 6,
    CLIMATE_SEASON_AUTUMN_EARLY = 7,
    CLIMATE_SEASON_AUTUMN_MID = 8,
    CLIMATE_SEASON_AUTUMN_LATE = 9,
    CLIMATE_SEASON_WINTER_EARLY = 10,
    CLIMATE_SEASON_WINTER_MID = 11,
    CLIMATE_SEASON_WINTER_LATE = 12,
    CLIMATE_SEASON_WET = 13,
    CLIMATE_SEASON_DRY = 14,
    CLIMATE_SEASON_POLAR_NIGHT = 15,
    CLIMATE_SEASON_MIDNIGHT_SUN = 16,
    CLIMATE_SEASON_COUNT = 17,
} climate_season_t;

enum : uint8_t {
    CLIMATE_SEASON_ALSO_HURRICANE = 1u << 0, /* "Hurricane season" */
    CLIMATE_SEASON_ALSO_MONSOON = 1u << 1,   /* "Monsoon" */
    CLIMATE_SEASON_ALSO_ALL = CLIMATE_SEASON_ALSO_HURRICANE | CLIMATE_SEASON_ALSO_MONSOON,
};

enum : uint32_t {
    /* The longest label with its NUL: "Early autumn · Hurricane season · Monsoon" is 44 bytes. */
    CLIMATE_SEASON_TEXT_BYTES = 48u,
    CLIMATE_LABEL_UTF8_LEAD_MASK = 0xC0u, /* phase 11: a byte with both top bits set starts a multi-byte UTF-8 letter */
};

/* Everything the label is decided from, at one place on one day. */
typedef struct climate_season_input_t {
    float32_t lat_deg;          /* 0: phi, phase 02 */
    float32_t day_hours;        /* 4: day length, phase 02: exactly 0 or 24 in polar night or midnight sun */
    float32_t rain_today;       /* 8: P(d) of the cell, mm a week, master plan 6.9 */
    float32_t rain_mean;        /* 12: P0 of the cell */
    float32_t hurricane_season; /* 16: s_hur(d) of the cell, 0 to 1, 6.8; 0 where it has none */
    uint16_t day_of_year;       /* 20: d, 0 to 364 */
    uint8_t monsoon_now;        /* 22: the cell has the monsoon flag and its season wind blows from the sea today */
    uint8_t _pad0[1];           /* 23 */
} climate_season_input_t;
static_assert(sizeof(climate_season_input_t) == 24);

/* The label, as 4.1 decides it. */
typedef struct climate_season_label_t {
    uint8_t season; /* 0: climate_season_t */
    uint8_t also;   /* 1: CLIMATE_SEASON_ALSO_* */
} climate_season_label_t;
static_assert(sizeof(climate_season_label_t) == 2);

/* 4.1's table: past the polar circle on a day of 0 or 24 hours, Polar night or Midnight sun; between the tropics (|phi| up to
 * 23.5 exactly), Wet season while rain_today > rain_mean, else Dry season; otherwise the hemisphere's season in thirds
 * (calendar_season_temperate). Second labels: Hurricane season while hurricane_season > 0.25, Monsoon while monsoon_now.
 * Pure. */
climate_season_label_t climate_season_of(const climate_season_input_t *input);

/* "Mid summer · Hurricane season", into out, the parts joined by " \xC2\xB7 ", the hurricane before the monsoon. Returns the
 * bytes written, not counting the NUL; always ends what it wrote with a NUL when bytes is above 0; never writes past bytes,
 * cutting the text short where it must. A season out of range writes nothing but the NUL. Pure. */
uint32_t climate_season_text(climate_season_label_t label, char *out, uint32_t bytes);

#endif
