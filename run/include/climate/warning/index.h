/* See run/src/climate/warning/README.md. */
#ifndef MC_RUN_CLIMATE_WARNING_INDEX_H
#define MC_RUN_CLIMATE_WARNING_INDEX_H

#include "arena/index.h"
#include "climate/index.h"
#include "climate/systems/index.h"
#include "types/index.h"

#include <stdint.h>

/* Plan 004 phase 10: the HUD's storm warning, master plan 9.1, "when a forecast gives over 50% for a hurricane or storm within the
 * next day". The chances are the outlook's own (phase 07), at now and every quarter day up to a day ahead; nothing here reads
 * the weather after the census moment the forecast was made at. */

enum : uint32_t {
    CLIMATE_WARNING_STEPS = 5u,   /* now, and every quarter day to one day ahead */
    CLIMATE_WARNING_TICKS = 200u, /* the sim works it out this often: 10 s at 20 Hz */
};
#define CLIMATE_WARNING_THRESHOLD 0.5f64 /* over this, strictly: 9.1 */
#define CLIMATE_WARNING_STEP_DAYS 0.25f64

typedef enum climate_warning_kind_t : uint8_t {
    CLIMATE_WARNING_NONE = 0,
    CLIMATE_WARNING_NAMED = 1,            /* "Storm warning: " and the hurricane's title */
    CLIMATE_WARNING_HURRICANE_LIKELY = 2, /* "Storm warning: hurricane likely" */
    CLIMATE_WARNING_STORM = 3,            /* "Storm warning: storm" */
    CLIMATE_WARNING_KINDS = 4,
} climate_warning_kind_t;

/* What the rule is decided from: each known storm's best chance over the day, and the expected unborn arrivals. */
typedef struct climate_warning_input_t {
    const float64_t *hurricane_chances; /* 0: q_i of each known hurricane (tropical storms included) */
    const uint64_t *hurricane_ids;      /* 8: the same storms' ids, as phase 06 gives them */
    const float64_t *storm_chances; /* 16: q_i of each known low, over the moments it is CLIMATE_STORM_DEPTH_HPA deep */
    float64_t hurricane_unborn;     /* 24: Delta Lambda for hurricanes */
    float64_t storm_unborn;         /* 32: Delta Lambda for storm lows */
    uint32_t hurricane_count;       /* 40 */
    uint32_t storm_count;           /* 44 */
} climate_warning_input_t;
static_assert(sizeof(climate_warning_input_t) == 48);

typedef struct climate_warning_t {
    uint64_t system_id; /* 0: for CLIMATE_WARNING_NAMED; 0 otherwise */
    float64_t chance;   /* 8: what decided it, 0 to 1; with no warning, the highest of the three it looked at */
    uint32_t name_seed; /* 16: NAMED: phase 06's name seed of that system, for climate_system_name; 0 otherwise */
    uint8_t kind;       /* 20: climate_warning_kind_t */
    uint8_t hurricane; /* 21: NAMED: 1 if it is a hurricane now, 0 if a tropical storm (CLIMATE_HURRICANE_STORM_VMAX) */
    uint8_t _pad0[2];  /* 22 */
} climate_warning_t;
static_assert(sizeof(climate_warning_t) == 24);

/* The rule, steps 1 to 4 of the phase 10 file: the known hurricane with the largest q over 0.5 is named; else at least one
 * hurricane, known or unborn, over 0.5 is "hurricane likely"; else at least one storm low over 0.5 is "storm"; else none.
 * Strictly over: exactly 0.5 does not warn. Pure. */
climate_warning_t climate_warning_decide(const climate_warning_input_t *input);

/* One system's chance at the five moments t0, t0 + 1/4 ... t0 + 1 days after the census at census_t, through the outlook's own
 * per-system chance (climate_outlook_system_chance), into out_chances; out_deep[k] says whether the system is at least
 * CLIMATE_STORM_DEPTH_HPA deep then (a storm, 7.4). The best of the first is a hurricane's q; of those where out_deep holds, a
 * low's. Pure. */
void climate_warning_steps(const climate_system_t *system, uint64_t seed, uint64_t week, float64_t census_t,
                           float64_t t0, float64_t x, float64_t y, bool error_off,
                           float64_t out_chances[CLIMATE_WARNING_STEPS], bool out_deep[CLIMATE_WARNING_STEPS]);

/* The whole thing at (x, y) at the moment t (D + f, climate_weather_time_of_clock): the forecast is the last census's, made at
 * 06:00 on the first day of t's week (t0 = t - 7 floor(D / 7)), so its known systems (climate_outlook_known_list), each one's q
 * over the five moments, and Delta Lambda = Lambda(t0 + 1) - Lambda(t0) from the outlook's own rate sums (climate_outlook_unborn),
 * held at 0 or above, then the rule. scratch holds the lists: the sim's tick arena, about 4 MiB. With no tile built at the place,
 * kind NONE and false. */
bool climate_warning_at(const climate_t *climate, arena_t *scratch, float64_t x, float64_t y, float64_t t,
                        climate_warning_t *out);

/* The census moment of the week holding t, as the outlook makes its forecast: 7 floor(floor(t) / 7). Pure. */
float64_t climate_warning_census_t(float64_t t);

#endif
