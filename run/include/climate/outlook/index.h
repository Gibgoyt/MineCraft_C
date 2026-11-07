/* See run/src/climate/outlook/README.md. */
#ifndef MC_RUN_CLIMATE_OUTLOOK_INDEX_H
#define MC_RUN_CLIMATE_OUTLOOK_INDEX_H

#include "arena/index.h"
#include "climate/index.h"
#include "climate/systems/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 07: chances for the next 7 days at a place, never the actual future (master plan 8). In two halves, so it
 * provably cannot read ahead: climate_outlook_input_fill gathers what a forecaster could know at the census moment into a
 * plain block, and climate_outlook_run works out the chances from that block alone. */

enum : uint32_t {
    CLIMATE_OUTLOOK_DAYS = 7u,         /* 8 */
    CLIMATE_OUTLOOK_SYSTEMS_MAX = 8u,  /* phase 07: named systems on the screen */
    CLIMATE_OUTLOOK_KNOWN_MAX = 64u,   /* phase 07: systems alive now that the input holds */
    CLIMATE_OUTLOOK_NAME_BYTES = 24u,  /* "Tropical storm Tavela" with its NUL */
    CLIMATE_OUTLOOK_DISK_STEPS = 512u, /* the exact footprint's quadrature: even, for Simpson's rule */
};
static const float64_t CLIMATE_OUTLOOK_SIGMA_START = 200.0f64;   /* 8: sigma(t) = 200 + 400 t blocks */
static const float64_t CLIMATE_OUTLOOK_SIGMA_PER_DAY = 400.0f64; /* 8 */
static const float32_t CLIMATE_OUTLOOK_RAIN_MM = 6.0f32;         /* 8: 1 - e^(-P / 6) */
static const float32_t CLIMATE_OUTLOOK_SPREAD_SIGMAS = 1.5f32;   /* 8: +-1.5 sigma_T */
static const float64_t CLIMATE_OUTLOOK_REACH = 50000.0f64; /* phase 07: blocks; any system 7 days out comes from here */
static const float32_t CLIMATE_OUTLOOK_CELL_RADIUS = 275.0f32;  /* phase 07: a cell's mean radius, blocks */
static const float32_t CLIMATE_OUTLOOK_CELL_HOURS = 2.5f32;     /* phase 07: its mean life */
static const float32_t CLIMATE_OUTLOOK_DEEP_LOW_SHARE = 0.5f32; /* phase 07: lows of -10 to -30 hPa past -20 */

/* How a known system's chance of covering the place is worked out: the issue the calibration test decides. */
typedef enum climate_outlook_footprint_t : uint8_t {
    CLIMATE_OUTLOOK_FOOTPRINT_MASTER = 0, /* master plan 8: exp(-d^2 / (2 (sigma^2 + R^2))) */
    CLIMATE_OUTLOOK_FOOTPRINT_EXACT = 1,  /* the chance a Gaussian-scattered centre lands within R: Issues found 1 */
} climate_outlook_footprint_t;

/* The form in force: CLIMATE_OUTLOOK_FOOTPRINT_EXACT since the calibration test (phase 07 file, Issues found 1). */
static const uint8_t CLIMATE_OUTLOOK_FOOTPRINT = CLIMATE_OUTLOOK_FOOTPRINT_MASTER;

typedef struct climate_outlook_system_t {
    char name[CLIMATE_OUTLOOK_NAME_BYTES];  /* 0: "Hurricane Tavela", from phase 06 */
    uint32_t id;                            /* 24: phase 06's system id, folded to 32 bits */
    uint8_t kind;                           /* 28: phase 06's kind */
    uint8_t _pad0[3];                       /* 29 */
    float32_t chance[CLIMATE_OUTLOOK_DAYS]; /* 32: of reaching here, each day */
} climate_outlook_system_t;
static_assert(sizeof(climate_outlook_system_t) == 60);
static_assert(alignof(climate_outlook_system_t) == 4);
static_assert(offsetof(climate_outlook_system_t, name) == 0);
static_assert(offsetof(climate_outlook_system_t, id) == 24);
static_assert(offsetof(climate_outlook_system_t, kind) == 28);
static_assert(offsetof(climate_outlook_system_t, _pad0) == 29);
static_assert(offsetof(climate_outlook_system_t, chance) == 32);

typedef struct climate_outlook_day_t {
    float32_t rain;      /* 0: chance, 0 to 1 */
    float32_t thunder;   /* 4 */
    float32_t storm;     /* 8: a low over 20 hPa deep, or a hurricane */
    float32_t hurricane; /* 12: any hurricane, known or not yet born */
    float32_t high_c;    /* 16: expected */
    float32_t low_c;     /* 20 */
    float32_t spread_c;  /* 24: +-1.5 sigma_T */
} climate_outlook_day_t;
static_assert(sizeof(climate_outlook_day_t) == 28);
static_assert(alignof(climate_outlook_day_t) == 4);
static_assert(offsetof(climate_outlook_day_t, rain) == 0);
static_assert(offsetof(climate_outlook_day_t, thunder) == 4);
static_assert(offsetof(climate_outlook_day_t, storm) == 8);
static_assert(offsetof(climate_outlook_day_t, hurricane) == 12);
static_assert(offsetof(climate_outlook_day_t, high_c) == 16);
static_assert(offsetof(climate_outlook_day_t, low_c) == 20);
static_assert(offsetof(climate_outlook_day_t, spread_c) == 24);

typedef struct climate_outlook_t {
    climate_outlook_day_t days[CLIMATE_OUTLOOK_DAYS];              /* 0 */
    climate_outlook_system_t systems[CLIMATE_OUTLOOK_SYSTEMS_MAX]; /* 196 */
    uint32_t system_count;                                         /* 676 */
    uint32_t week;                                                 /* 680 */
    uint8_t _pad0[4];                                              /* 684 */
} climate_outlook_t;
static_assert(sizeof(climate_outlook_t) == 688);
static_assert(alignof(climate_outlook_t) == 4);
static_assert(offsetof(climate_outlook_t, days) == 0);
static_assert(offsetof(climate_outlook_t, systems) == 196);
static_assert(offsetof(climate_outlook_t, system_count) == 676);
static_assert(offsetof(climate_outlook_t, week) == 680);
static_assert(offsetof(climate_outlook_t, _pad0) == 684);

/* A system alive at the census moment, and where its own steering rules take it: what a forecaster knows of it. */
typedef struct climate_outlook_known_t {
    float64_t x[CLIMATE_OUTLOOK_DAYS];        /* 0: its centre at the instant t days after the census */
    float64_t y[CLIMATE_OUTLOOK_DAYS];        /* 56 */
    uint64_t id;                              /* 112: phase 06's */
    char name[CLIMATE_OUTLOOK_NAME_BYTES];    /* 120: its title at the census moment */
    float32_t pressure[CLIMATE_OUTLOOK_DAYS]; /* 144: A at each instant, signed; 0 once it has died */
    float32_t radius;                         /* 172: R, blocks */
    uint8_t alive[CLIMATE_OUTLOOK_DAYS];      /* 176: 1 if its life lasts to that instant */
    uint8_t kind;                             /* 183: CLIMATE_SYSTEM_* */
} climate_outlook_known_t;
static_assert(sizeof(climate_outlook_known_t) == 184);
static_assert(alignof(climate_outlook_known_t) == 8);
static_assert(offsetof(climate_outlook_known_t, x) == 0);
static_assert(offsetof(climate_outlook_known_t, y) == 56);
static_assert(offsetof(climate_outlook_known_t, id) == 112);
static_assert(offsetof(climate_outlook_known_t, name) == 120);
static_assert(offsetof(climate_outlook_known_t, pressure) == 144);
static_assert(offsetof(climate_outlook_known_t, radius) == 172);
static_assert(offsetof(climate_outlook_known_t, alive) == 176);
static_assert(offsetof(climate_outlook_known_t, kind) == 183);

/* Everything the chances are worked out from: a plain block of fixed size, copied as bytes. Day t (1 to 7) means the
 * instant t days after the census for systems, and the calendar day ending then for rain, thunder, highs and lows. */
typedef struct climate_outlook_input_t {
    float64_t x;                                              /* 0: the place */
    float64_t y;                                              /* 8 */
    float64_t census_t;                                       /* 16: D + f of the census moment */
    uint64_t seed;                                            /* 24: the forecast errors' dice */
    uint64_t week;                                            /* 32: the census week */
    climate_outlook_known_t known[CLIMATE_OUTLOOK_KNOWN_MAX]; /* 40 */
    float32_t high_c[CLIMATE_OUTLOOK_DAYS];                   /* 11816: the normal high of each day */
    float32_t low_c[CLIMATE_OUTLOOK_DAYS];                    /* 11844 */
    float32_t rain_mm[CLIMATE_OUTLOOK_DAYS];                  /* 11872: the day's normal rain */
    float32_t thunder_rate[CLIMATE_OUTLOOK_DAYS];             /* 11900: cells a km^2 a day at the expected heat */
    float32_t wind_blocks_hour[CLIMATE_OUTLOOK_DAYS];         /* 11928: the day's normal wind speed */
    float32_t unborn_hurricane[CLIMATE_OUTLOOK_DAYS]; /* 11956: Lambda: hurricanes not yet born, expected here */
    float32_t unborn_low[CLIMATE_OUTLOOK_DAYS];       /* 11984: lows not yet born, expected here */
    float32_t sigma_t;                                /* 12012 */
    float32_t anomaly;                                /* 12016: today's T' */
    uint32_t known_count;                             /* 12020 */
    uint8_t error_off;                                /* 12024: a test's seam: forecast centres are the real ones */
    uint8_t footprint;                                /* 12025: climate_outlook_footprint_t */
    uint8_t _pad0[6];                                 /* 12026 */
} climate_outlook_input_t;
static_assert(sizeof(climate_outlook_input_t) == 12032);
static_assert(alignof(climate_outlook_input_t) == 8);
static_assert(offsetof(climate_outlook_input_t, x) == 0);
static_assert(offsetof(climate_outlook_input_t, y) == 8);
static_assert(offsetof(climate_outlook_input_t, census_t) == 16);
static_assert(offsetof(climate_outlook_input_t, seed) == 24);
static_assert(offsetof(climate_outlook_input_t, week) == 32);
static_assert(offsetof(climate_outlook_input_t, known) == 40);
static_assert(offsetof(climate_outlook_input_t, high_c) == 11816);
static_assert(offsetof(climate_outlook_input_t, low_c) == 11844);
static_assert(offsetof(climate_outlook_input_t, rain_mm) == 11872);
static_assert(offsetof(climate_outlook_input_t, thunder_rate) == 11900);
static_assert(offsetof(climate_outlook_input_t, wind_blocks_hour) == 11928);
static_assert(offsetof(climate_outlook_input_t, unborn_hurricane) == 11956);
static_assert(offsetof(climate_outlook_input_t, unborn_low) == 11984);
static_assert(offsetof(climate_outlook_input_t, sigma_t) == 12012);
static_assert(offsetof(climate_outlook_input_t, anomaly) == 12016);
static_assert(offsetof(climate_outlook_input_t, known_count) == 12020);
static_assert(offsetof(climate_outlook_input_t, error_off) == 12024);
static_assert(offsetof(climate_outlook_input_t, footprint) == 12025);
static_assert(offsetof(climate_outlook_input_t, _pad0) == 12026);

/* What a forecaster could know at the census moment census_t (D + f) of census week, at (x, y): the systems alive then and
 * their steered tracks, the place's normals for the 7 days, today's anomaly, and the expected count of systems not yet
 * born that reach here by each day (each kind's spawn rate over every spawn square and day before the instant, thinned by
 * the chance a system starting anywhere in the square covers here then). False, with nothing meant in out, if a tile at the
 * place is not built or scratch is too small (about 4 MiB). Reads the climate; never the weather after census_t. */
[[nodiscard]] bool climate_outlook_input_fill(const climate_t *climate, float64_t x, float64_t y, float64_t census_t,
                                              uint64_t week, arena_t *scratch, climate_outlook_input_t *out);

/* The chances, from the input alone: it takes no climate_t. The same input gives the same bytes. */
void climate_outlook_run(const climate_outlook_input_t *input, climate_outlook_t *out);

/* sigma(t) = 200 + 400 t blocks. Pure. */
float64_t climate_outlook_sigma(float64_t days);
/* The chance that a 2D Gaussian of spread sigma on each axis, centred d from the place, lands within radius of it.
 * Simpson's rule over the disk, CLIMATE_OUTLOOK_DISK_STEPS steps. Pure. */
float64_t climate_outlook_disk_chance(float64_t d, float64_t sigma, float64_t radius);
/* A known system's chance of covering the place by the given form. Pure. */
float64_t climate_outlook_footprint(float64_t d, float64_t sigma, float64_t radius, uint8_t footprint);
/* 1 - e^(-P / 6): the chance of rain from a day's normal rain. Pure. */
float32_t climate_outlook_background_rain(float32_t rain_mm_day);
/* 1 - prod(1 - p_i) e^(-Lambda). Pure. */
float64_t climate_outlook_at_least_one(const float64_t *chances, uint32_t count, float64_t lambda);
/* A system's forecast error t days ahead, in units of sigma, drawn by hash from the seed, the system, the week and t. */
void climate_outlook_error(uint64_t seed, uint64_t id, uint64_t week, uint32_t day, float64_t out[2]);

/* ---- Plan 004 phase 10: what the outlook already works out, at moments of the caller's choosing ----------------------- */

/* The forecast errors' dice for this climate's world: climate_outlook_input_t.seed. */
uint64_t climate_outlook_seed(const climate_t *climate);

/* The chance the forecast gives that one known system covers (x, y) at the instant t days after the census at census_t, for any
 * t >= 0 with a fraction: its centre where its own steering takes it then (climate_system_state_at), moved by the forecast
 * error drawn for whole days as climate_outlook_run draws it and going evenly from one whole day's to the next, sigma(t), and the
 * footprint form. At t = 1 to 7 it is the chance climate_outlook_run gives for that day, before the outlook keeps it as a
 * float32_t. 0 if the system is not alive then. Pure. */
float64_t climate_outlook_system_chance(const climate_system_t *system, uint64_t seed, uint64_t week,
                                        float64_t census_t, float64_t t, float64_t x, float64_t y, bool error_off,
                                        uint8_t footprint);

/* The systems a forecaster knows at census_t near (x, y): every big system alive at that moment within CLIMATE_OUTLOOK_REACH,
 * nearest first, then by id, as climate_outlook_input_fill keeps them. list is the census day's, built in scratch; order its
 * indices, also in scratch, *out_count of them (no cap). False if a tile is missing or scratch too small. Reads only up to
 * census_t. */
[[nodiscard]] bool climate_outlook_known_list(const climate_t *climate, float64_t x, float64_t y, float64_t census_t,
                                              arena_t *scratch, climate_systems_day_t *list, uint32_t **out_order,
                                              uint32_t *out_count);

/* Lambda of a kind (CLIMATE_SYSTEM_HURRICANE or CLIMATE_SYSTEM_LOW) at the instants census_t + after[i]: systems not yet born
 * at census_t that are expected to cover (x, y) then, as climate_outlook_input_fill's unborn_* (which asks at after = 1 to 7).
 * Births on every day from the census day up to the last instant are counted. Into out[0 .. count - 1]. */
void climate_outlook_unborn(const climate_t *climate, arena_t *scratch, climate_systems_mask_t *mask, uint8_t kind,
                            float64_t x, float64_t y, float64_t census_t, const float64_t *after, uint32_t count,
                            float64_t *out);

#endif
