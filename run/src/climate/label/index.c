/* See README.md. Master plan 4.1's season labels, from plain numbers. Allocates nothing. */
#include "climate/label/index.h"

#include "calendar/index.h"
#include "climate/index.h"
#include "climate/season/index.h"

#include <math.h>
#include <string.h>

/* The polar circle: 90 degrees less the tilt, 66.5. */
static const float64_t LABEL_POLAR_DEG = CLIMATE_POLAR_CIRCLE_DEG;
static const char LABEL_JOIN[] = " \xC2\xB7 ";

static const char *const LABEL_NAMES[CLIMATE_SEASON_COUNT] = {
    [CLIMATE_SEASON_NONE] = "",
    [CLIMATE_SEASON_WET] = "Wet season",
    [CLIMATE_SEASON_DRY] = "Dry season",
    [CLIMATE_SEASON_POLAR_NIGHT] = "Polar night",
    [CLIMATE_SEASON_MIDNIGHT_SUN] = "Midnight sun",
};

climate_season_label_t climate_season_of(const climate_season_input_t *input) {
    climate_season_label_t label = {.season = CLIMATE_SEASON_NONE, .also = 0u};
    const float64_t lat = (float64_t)input->lat_deg;
    const float64_t away = fabs(lat);
    const uint16_t day = input->day_of_year < CALENDAR_DAYS_PER_YEAR ? input->day_of_year : 0u;
    if (away >= LABEL_POLAR_DEG && input->day_hours == 0.0f32) {
        label.season = CLIMATE_SEASON_POLAR_NIGHT;
    } else if (away >= LABEL_POLAR_DEG && input->day_hours == CALENDAR_HOURS_PER_DAY_F32) {
        label.season = CLIMATE_SEASON_MIDNIGHT_SUN;
    } else if (away <= CLIMATE_TILT_DEG) {
        /* Between the tropics, 23.5 itself included: the season is the rain's. */
        label.season = input->rain_today > input->rain_mean ? CLIMATE_SEASON_WET : CLIMATE_SEASON_DRY;
    } else {
        const calendar_season_label_t hemisphere = calendar_season_temperate(day, lat < 0.0f64);
        label.season =
            (uint8_t)(CLIMATE_SEASON_SPRING_EARLY + hemisphere.season * CALENDAR_SEASON_PARTS + hemisphere.part);
    }
    if (input->hurricane_season > CLIMATE_HURRICANE_SEASON_S) {
        label.also |= CLIMATE_SEASON_ALSO_HURRICANE;
    }
    if (input->monsoon_now != 0u) {
        label.also |= CLIMATE_SEASON_ALSO_MONSOON;
    }
    return label;
}

/* The name of a first label. */
static const char *label_name(uint8_t season) {
    if (season >= CLIMATE_SEASON_SPRING_EARLY && season <= CLIMATE_SEASON_WINTER_LATE) {
        const uint32_t k = (uint32_t)season - CLIMATE_SEASON_SPRING_EARLY;
        return calendar_season_name((calendar_season_label_t){.season = (uint8_t)(k / CALENDAR_SEASON_PARTS),
                                                              .part = (uint8_t)(k % CALENDAR_SEASON_PARTS)});
    }
    return LABEL_NAMES[season];
}

/* As much of text as fits after at, keeping a byte for the NUL. True if all of it did: once cut, nothing more is added. */
static bool label_append(char *out, uint32_t bytes, uint32_t *at, const char *text) {
    const uint32_t length = (uint32_t)strlen(text);
    const uint32_t room = bytes - 1u - *at;
    uint32_t taken = length < room ? length : room;
    /* Never half of the middle dot: a lead byte cut from what follows it is left out too. */
    if (taken < length && taken > 0u &&
        ((uint8_t)text[taken - 1u] & CLIMATE_LABEL_UTF8_LEAD_MASK) == CLIMATE_LABEL_UTF8_LEAD_MASK) {
        taken--;
    }
    memcpy(out + *at, text, taken);
    *at += taken;
    return taken == length;
}

uint32_t climate_season_text(climate_season_label_t label, char *out, uint32_t bytes) {
    if (bytes == 0u) {
        return 0u;
    }
    uint32_t at = 0u;
    if (label.season > CLIMATE_SEASON_NONE && label.season < CLIMATE_SEASON_COUNT) {
        bool whole = label_append(out, bytes, &at, label_name(label.season));
        if (whole && (label.also & CLIMATE_SEASON_ALSO_HURRICANE) != 0u) {
            whole = label_append(out, bytes, &at, LABEL_JOIN) && label_append(out, bytes, &at, "Hurricane season");
        }
        if (whole && (label.also & CLIMATE_SEASON_ALSO_MONSOON) != 0u) {
            whole = label_append(out, bytes, &at, LABEL_JOIN) && label_append(out, bytes, &at, "Monsoon");
        }
        (void)whole;
    }
    out[at] = '\0';
    return at;
}
