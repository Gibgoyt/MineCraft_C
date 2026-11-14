/* See run/src/climate/systems/README.md. */
#ifndef MC_RUN_CLIMATE_SYSTEMS_INDEX_H
#define MC_RUN_CLIMATE_SYSTEMS_INDEX_H

#include "arena/index.h"
#include "climate/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 06: weather systems, spawned by hash and steered by rules (master plan 7.2 to 7.4). Every number below is
 * the master plan's, or says "this phase" and why in the phase 06 file. One home each (vision rule 5). */

enum : uint8_t {
    CLIMATE_SYSTEM_HURRICANE = 0,
    CLIMATE_SYSTEM_LOW = 1,
    CLIMATE_SYSTEM_HIGH = 2,
    CLIMATE_SYSTEM_THUNDER = 3, /* thunderstorm cells: kept in their own array, climate_thunder_t */
    CLIMATE_SYSTEM_KINDS = 4,
};
enum : uint32_t { /* bits for the kinds argument of climate_systems_day_build */
                  CLIMATE_SYSTEMS_BIG =
                      (1u << CLIMATE_SYSTEM_HURRICANE) | (1u << CLIMATE_SYSTEM_LOW) | (1u << CLIMATE_SYSTEM_HIGH),
                  CLIMATE_SYSTEMS_ALL = CLIMATE_SYSTEMS_BIG | (1u << CLIMATE_SYSTEM_THUNDER),
};

/* Each kind's dice: terrain_rng_derive(world seed, terrain_rng_salt(name)). Apart from every terrain field's name. */
static const char *const CLIMATE_SYSTEMS_SALT_NAMES[CLIMATE_SYSTEM_KINDS] = {
    "climate_systems/hurricane", "climate_systems/low", "climate_systems/high", "climate_systems/thunder"};

/* Time. */
static const float64_t CLIMATE_TRACK_STEP_DAYS = 0.25f64;      /* 7.3: steps of a quarter of a day */
static const float32_t CLIMATE_REAL_SECONDS_A_DAY = 1200.0f32; /* 24000 ticks at 20 Hz: run/include/game/index.h */
static const float64_t CLIMATE_SYSTEMS_YEAR_DAYS = CALENDAR_DAYS_PER_YEAR_F64;
/* 7.2: a system's life in thirds: it grows over the first, CLIMATE_LIFE_GROW_SHARE, and fades over the last. */
static const float64_t CLIMATE_LIFE_THIRDS = 3.0f64;
static const float64_t CLIMATE_LIFE_GROW_SHARE = 1.0f64 / 3.0f64;
static const float64_t CLIMATE_LIFE_FADE_SHARE = 1.0f64 / 3.0f64;
enum : uint32_t {
    CLIMATE_TRACK_SAMPLES_MAX = 57u,  /* 14 days x 4 + 1: the longest life, a high's */
    CLIMATE_SPAWNS_MAX = 8u,          /* this phase: the inverse CDF's cap per square-day, big systems */
    CLIMATE_THUNDER_SPAWNS_MAX = 64u, /* this phase: the cap for cells, lambda up to 45 before tuning */
    CLIMATE_SYSTEMS_LIST_MAX = 128u,  /* this phase: big systems in one day list */
    CLIMATE_THUNDER_LIST_MAX = 8192u, /* this phase: cells in one day list */
    CLIMATE_SYSTEMS_DRAWS = 11u,      /* the uniform draws of a candidate, before its name seed */
};
static const float32_t CLIMATE_THUNDER_QUERY_MAX = 8192.0f32; /* this phase: blocks, the largest thunder_radius */
/* This phase: big systems are found this much further out than cells are asked for, beyond the cells' reach, so a cell
 * started up to 4 clock hours before the day can still see the high or front it started under (7204 / 6 = 1201). */
static const float32_t CLIMATE_THUNDER_BIG_MARGIN = 1250.0f32;

/* Spawn squares, blocks: 7.2. */
static const float64_t CLIMATE_SQUARE_HURRICANE = 16384.0f64; /* 16 km */
static const float64_t CLIMATE_SQUARE_LOW = 16384.0f64;       /* 16 km */
static const float64_t CLIMATE_SQUARE_HIGH = 24576.0f64;      /* 24 km */
static const float64_t CLIMATE_SQUARE_THUNDER = 1024.0f64;    /* 1 km */

/* Rates, spawns per square per day: 7.2 and 7.2.1. The ones the 10-year test tunes (7.4) are marked; the phase 06 file
 * records each change with its before and after. */
/* x s_hur, over sea at the point. 0.02 in 7.2; 0.034 by phase 06 (Issues found 1) behind its open-sea rule; 0.3 by the final
 * check with that rule gone, so a warm coast square gets 7.2's 2.4 a season on the survey's 8 seeds (Q5: about 7 a coast). */
static const float32_t CLIMATE_HURRICANE_RATE = 0.3f32;
static const float32_t CLIMATE_LOW_RATE_WINTER = 0.18f32; /* tuned: 0.08 in 7.2 */
static const float32_t CLIMATE_LOW_RATE_SUMMER = 0.09f32; /* tuned: 0.04 in 7.2 */
static const float32_t CLIMATE_HIGH_RATE = 0.03f32;       /* tuned */
static const float32_t CLIMATE_THUNDER_RATE = 3.0f32;     /* the leading 3 of 7.2.1 */
/* This phase: the one knob 7.4 tunes for cells; tuned from 1 to 0.22, then 0.205 by the final check (Issues found 22). */
static const float32_t CLIMATE_THUNDER_RATE_GAIN = 0.205f32;
static const float32_t CLIMATE_THUNDER_HOT_C = 18.0f32; /* 7.2.1: (T - 18) / 10 */
static const float32_t CLIMATE_THUNDER_HOT_RANGE_C = 10.0f32;
static const float32_t CLIMATE_THUNDER_MONSOON = 2.0f32; /* 7.2.1: 1 + 2 f_monsoon */
static const float32_t CLIMATE_THUNDER_FRONT = 4.0f32;   /* 7.2.1: 1 + 4 f_front */
#define CLIMATE_THUNDER_HIGH CLIMATE_HIGH_RAIN_SHARE     /* 7.2.1: 0.2^{f_high}: the same 0.2 as rain under a high */
/* This phase (Issues found 12): a cell's rain scale divides by heat no lower than this, so the rare cells of a day barely
 * over 18 C are at most 5 times as heavy, not without bound. */
static const float32_t CLIMATE_THUNDER_HOT_FLOOR = 0.2f32;
/* This phase, tuned (Issues found 12): every cell's rain scale times this, so cells are three times as many and each a
 * third as heavy as the bare scale would make them: the same rain, and a third of the sampling noise squared. */
static const float32_t CLIMATE_THUNDER_RAIN_SCALE_BASE = 0.333333333f32;
static const float32_t CLIMATE_LOW_PEAK_DAY = 200.0f32; /* this phase: the low rate is lowest here in the north */

/* Where: 7.2. */
static const float32_t CLIMATE_LOW_PSI_MIN_DEG = 35.0f32;
static const float32_t CLIMATE_LOW_PSI_MAX_DEG = 65.0f32;
static const float32_t CLIMATE_HIGH_PSI_MIN_DEG = 20.0f32;
static const float32_t CLIMATE_HIGH_PSI_MAX_DEG = 45.0f32;
static const float32_t CLIMATE_HIGH_INTERIOR_PSI_MAX_DEG = 70.0f32; /* this phase: winter interior highs up to here */
static const float32_t CLIMATE_HIGH_INTERIOR_BLOCKS = 3000.0f32;    /* this phase: land this far all round */
/* Water this far all round as well as at the point: 0, the point alone, master plan 7.2's "Sea". Phase 06 had 2048, which
 * the terrain's seas almost never meet, so no hurricane formed on any real seed (the final check, phase 06 Issues found 22). */
static const float32_t CLIMATE_HURRICANE_OPEN_SEA_BLOCKS = 0.0f32;

/* Sizes, strengths and lives: 7.2. Each a uniform range, {lo, hi}: v = lo + u (hi - lo). */
static const float32_t CLIMATE_HURRICANE_RADIUS[2] = {1500.0f32, 4000.0f32};
static const float32_t CLIMATE_HURRICANE_EYE[2] = {80.0f32, 150.0f32};
static const float32_t CLIMATE_HURRICANE_HPA[2] = {-30.0f32, -90.0f32};
static const float32_t CLIMATE_HURRICANE_LIFE_DAYS[2] = {4.0f32, 10.0f32};
static const float32_t CLIMATE_LOW_RADIUS[2] = {3000.0f32, 7000.0f32};
static const float32_t CLIMATE_LOW_HPA[2] = {-10.0f32, -30.0f32};
static const float32_t CLIMATE_LOW_LIFE_DAYS[2] = {3.0f32, 6.0f32};
static const float32_t CLIMATE_LOW_FRONT_BLOCKS[2] = {4000.0f32, 8000.0f32};
static const float32_t CLIMATE_HIGH_RADIUS[2] = {6000.0f32, 12000.0f32};
static const float32_t CLIMATE_HIGH_HPA[2] = {8.0f32, 20.0f32};
static const float32_t CLIMATE_HIGH_LIFE_DAYS[2] = {6.0f32, 14.0f32};
static const float32_t CLIMATE_THUNDER_RADIUS[2] = {150.0f32, 400.0f32};
static const float32_t CLIMATE_THUNDER_HPA[2] = {-1.0f32, -3.0f32};
static const float32_t CLIMATE_THUNDER_LIFE_HOURS[2] = {1.0f32, 4.0f32};
static const float32_t CLIMATE_THUNDER_MM_HOUR[2] = {20.0f32, 60.0f32};
static const float32_t CLIMATE_THUNDER_START_HOURS[2] = {13.0f32, 19.0f32}; /* clock hours, off fronts */
static const float32_t CLIMATE_DAY_START_HOUR = CALENDAR_CENSUS_HOUR_F32;   /* phase 01: a day starts at 06:00 */

/* Motion, blocks a game day: 7.3. */
static const float32_t CLIMATE_HURRICANE_SPEED_WEST = 3500.0f32;
static const float32_t CLIMATE_HURRICANE_SPEED_EAST = 6000.0f32;
static const float32_t CLIMATE_HURRICANE_POLEWARD_WEST =
    0.15f32; /* this phase: drift while going west, Issues found 3 */
static const float32_t CLIMATE_HURRICANE_POLEWARD_EAST = 0.3f32; /* this phase: the westerlies' own 0.3, 6.5 */
static const float32_t CLIMATE_HURRICANE_TURN_PSI_DEG[2] = {20.0f32, 28.0f32};
static const float32_t CLIMATE_HURRICANE_TURN_DAYS = 2.0f32;
static const float32_t CLIMATE_LOW_SPEED[2] = {5000.0f32, 7000.0f32}; /* this phase: "about 6 km a day", +-1/6 */
static const float32_t CLIMATE_LOW_POLEWARD = 0.08f32; /* this phase: "a slow drift poleward"; tuned from 0.15 */
static const float32_t CLIMATE_HIGH_SPEED[2] = {1500.0f32, 2500.0f32}; /* this phase: "about 2 km a day", +-1/4 */
static const float32_t CLIMATE_HIGH_EQUATORWARD = 0.1f32;              /* this phase: "or equatorward (highs)" */
static const float32_t CLIMATE_SPEED_SCALE[2] = {0.85f32, 1.15f32};    /* this phase: a hurricane's own speed */
static const float32_t CLIMATE_THUNDER_SPEED_MAX = 15.0f32;            /* this phase: blocks a second, caps reach */
static const float32_t CLIMATE_DRIFT_POLE_STOP_DEG = 85.0f32;          /* this phase: no poleward drift past here */
static const float32_t CLIMATE_DRIFT_EQUATOR_STOP_DEG = 5.0f32;        /* this phase: no equatorward drift inside */

/* Weakening: 7.2. */
static const float32_t CLIMATE_HURRICANE_HALF_LIFE_DAYS = 1.0f32; /* halves each game day over land */
static const float32_t CLIMATE_HURRICANE_COOL_SEA_C = 26.0f32;    /* and over sea below this */
static const float32_t CLIMATE_SYSTEM_DEAD_HPA = 5.0f32;          /* this phase: under this, a big system ends */

/* Fields: 7.3. */
static const float32_t CLIMATE_SYSTEM_REACH_RADII = 3.0f32;      /* this phase: nothing past 3 R */
static const float32_t CLIMATE_SYSTEM_WIND_TAPER_RADII = 2.0f32; /* this phase: wind tapers to 0 from 2 R to 3 R */
static const float32_t CLIMATE_HURRICANE_VMAX_AT_30 = 33.0f32;   /* blocks a second at -30 hPa */
static const float32_t CLIMATE_HURRICANE_VMAX_AT_90 = 70.0f32;   /* at -90 */
static const float32_t CLIMATE_HURRICANE_STORM_VMAX = 33.0f32;   /* under this it is a tropical storm */
static const float32_t CLIMATE_SYSTEM_VMAX_PER_HPA = 0.4f32;     /* lows and highs */
static const float32_t CLIMATE_SYSTEM_CORE_SHARE = 0.5f32;       /* lows and highs: r_m = 0.5 R */
static const float32_t CLIMATE_WIND_PROFILE_POWER = 0.6f32;

/* Rain: 7.3. */
static const float32_t CLIMATE_HURRICANE_EYEWALL_MM_HOUR = 40.0f32; /* at -90 hPa; scaled by |A| / 90 */
static const float32_t CLIMATE_HURRICANE_EYEWALL_HPA = 90.0f32;
static const float32_t CLIMATE_HURRICANE_BAND_ARMS = 3.0f32; /* this phase: three spiral arms */
static const float32_t CLIMATE_HURRICANE_BAND_WIND = 2.5f32; /* this phase: how tightly they wind */
static const float32_t CLIMATE_LOW_SHIELD_MM_HOUR[2] = {1.0f32, 4.0f32};
static const float32_t CLIMATE_LOW_SHIELD_MEAN = 2.5f32;    /* this phase: the front's showers go with shield / this */
static const float32_t CLIMATE_LOW_SHIELD_AHEAD = 0.5f32;   /* this phase: shield centred 0.5 R ahead */
static const float32_t CLIMATE_LOW_SHIELD_WIDTH = 0.6f32;   /* this phase: shield's width, x R */
static const float32_t CLIMATE_FRONT_MM_HOUR = 3.0f32;      /* this phase: showers on the front line */
static const float32_t CLIMATE_FRONT_HALF_WIDTH = 400.0f32; /* this phase: blocks either side of the line */
static const float32_t CLIMATE_FRONT_TURN_X = -0.5f32;      /* this phase: equatorward turned 30 degrees west */
static const float32_t CLIMATE_FRONT_TURN_Y = 0.8660254f32;
static const float32_t CLIMATE_HIGH_RAIN_SHARE = 0.2f32;                            /* 7.2 and 7.3 */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_TROPICS = 0.3f32;                  /* f_sys, 7.4 */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_MID = 0.5f32;                      /* f_sys, 7.4 */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_BLEND_DEG[2] = {25.0f32, 50.0f32}; /* this phase; tuned from 20, 35 */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_POLAR = 0.4f32; /* this phase, tuned: f_sys toward the poles */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_POLAR_DEG[2] = {60.0f32, 70.0f32}; /* this phase: smoothstep between */
/* This phase: lows bring rain this far outside their band, being up to 7 km across and drifting poleward. */
static const float32_t CLIMATE_SYSTEM_RAIN_SEASON_MARGIN_DEG = 5.0f32;
/* The final check (Issues found 22): what more of the normal systems bring on a day of hurricane season, times s_hur there,
 * as hurricanes at 0.3 a square-day add their rain to the cells' (the 10-year test's measure). */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_HURRICANE = 0.25f32;
/* This phase (Issues found 11): the rain a system brings to a place is its raw rain times the place's wetness, the day's
 * normal over this, so storms rain hard where it is usually wet and little where it is usually dry, and the long-run total
 * follows the normal down to a cell's scale. 5 mm a day is about 1800 mm a year: a wet place sees the raw 20 to 60. */
static const float32_t CLIMATE_SYSTEM_RAIN_WET_MM_DAY = 5.0f32;

/* One track sample: where the centre is, a quarter day apart. */
typedef struct climate_track_sample_t {
    float64_t x;       /* 0: blocks */
    float64_t y;       /* 8 */
    float32_t weaken;  /* 16: hurricanes: the product of weakening so far, 1 at birth; 1 for other kinds */
    uint8_t over_land; /* 20: the mask's height > 0 here */
    uint8_t stage;     /* 21: hurricanes: 0 westward, 1 turning, 2 eastward; 0 otherwise */
    uint8_t _pad0[2];  /* 22 */
} climate_track_sample_t;
static_assert(sizeof(climate_track_sample_t) == 24);
static_assert(alignof(climate_track_sample_t) == 8);
static_assert(offsetof(climate_track_sample_t, weaken) == 16);
static_assert(offsetof(climate_track_sample_t, over_land) == 20);
static_assert(offsetof(climate_track_sample_t, stage) == 21);

/* A hurricane, low or high: everything about it, from its spawn to its death. Worked out from the seed; never saved. */
typedef struct climate_system_t {
    float64_t spawn_day;      /* 0: t = D + f when it formed */
    float64_t start_x;        /* 8: blocks */
    float64_t start_y;        /* 16 */
    uint64_t id;              /* 24: the stream's seed: stable, unique per kind, square, day and index */
    float32_t radius;         /* 32: R, blocks */
    float32_t core_radius;    /* 36: r_m: the eye's edge for hurricanes, 0.5 R otherwise */
    float32_t peak_hpa;       /* 40: A at full strength, signed */
    float32_t life_days;      /* 44: as drawn; the track may end sooner if it weakens to death */
    float32_t speed;          /* 48: blocks a day: lows' and highs' own speed; hurricanes' scale 0.85 to 1.15 */
    float32_t turn_psi_deg;   /* 52: hurricanes: |psi| where the recurve starts; 0 otherwise */
    float32_t front_length;   /* 56: lows: blocks; 0 otherwise */
    float32_t shield_mm_hour; /* 60: lows: the rain shield at full strength; 0 otherwise */
    uint32_t name_seed;       /* 64 */
    int32_t square_x;         /* 68 */
    int32_t square_y;         /* 72 */
    uint8_t kind;             /* 76: CLIMATE_SYSTEM_* */
    uint8_t spawn_index;      /* 77: which of its square-day's candidates */
    uint8_t sample_count;     /* 78: samples[0 .. sample_count - 1] are its whole track */
    uint8_t _pad0[1];         /* 79 */
    climate_track_sample_t samples[CLIMATE_TRACK_SAMPLES_MAX]; /* 80 */
} climate_system_t;
static_assert(sizeof(climate_system_t) == 80 + 57 * 24); /* 1448 */
static_assert(alignof(climate_system_t) == 8);
static_assert(offsetof(climate_system_t, id) == 24);
static_assert(offsetof(climate_system_t, name_seed) == 64);
static_assert(offsetof(climate_system_t, kind) == 76);
static_assert(offsetof(climate_system_t, samples) == 80);

/* A thunderstorm cell: a straight line at a constant velocity, so no samples. */
typedef struct climate_thunder_t {
    float64_t spawn_day;    /* 0 */
    float64_t start_x;      /* 8 */
    float64_t start_y;      /* 16 */
    uint64_t id;            /* 24: unique per square, day, process and index; the list's tie-break */
    float32_t velocity[2];  /* 32: blocks a game day: the day's wind at the start, capped at 15 blocks a second */
    float32_t radius;       /* 40 */
    float32_t peak_mm_hour; /* 44 */
    float32_t peak_hpa;     /* 48 */
    float32_t life_days;    /* 52: 1 to 4 clock hours, over 24 */
    uint8_t on_front;       /* 56: 1 if it came from the front process */
    uint8_t _pad0[7];       /* 57 */
} climate_thunder_t;
static_assert(sizeof(climate_thunder_t) == 64);
static_assert(alignof(climate_thunder_t) == 8);
static_assert(offsetof(climate_thunder_t, id) == 24);
static_assert(offsetof(climate_thunder_t, velocity) == 32);
static_assert(offsetof(climate_thunder_t, on_front) == 56);

/* Everything near a place on one day. Built once a day by whoever needs it, in their arena, and read all day. */
typedef struct climate_systems_day_t {
    climate_system_t *borrowed_systems;  /* 0: system_capacity of them, in the builder's arena */
    climate_thunder_t *borrowed_thunder; /* 8 */
    float64_t centre_x;                  /* 16: blocks */
    float64_t centre_y;                  /* 24 */
    int64_t day;                         /* 32: D */
    float32_t radius;                    /* 40: big systems whose 3 R footprint comes within this, at any moment of D */
    float32_t thunder_radius;            /* 44: the same for cells; 0 if cells were not asked for */
    uint32_t system_count;               /* 48 */
    uint32_t system_capacity;            /* 52 */
    uint32_t thunder_count;              /* 56 */
    uint32_t thunder_capacity;           /* 60 */
    uint8_t thunder_complete;            /* 64: 1 if every tile cells needed was built (cells read normals) */
    uint8_t overflowed;                  /* 65: 1 if a capacity was reached: a bug, tests hold it at 0 */
    uint8_t _pad0[6];                    /* 66 */
} climate_systems_day_t;
static_assert(sizeof(climate_systems_day_t) == 72);

/* A big system at a moment. */
typedef struct climate_system_state_t {
    float64_t x;            /* 0 */
    float64_t y;            /* 8 */
    float32_t pressure_hpa; /* 16: A(t), signed */
    float32_t wind_max;     /* 20: v_max, blocks a second */
    float32_t heading[2];   /* 24: unit direction of motion */
    float32_t life_share;   /* 32: tau */
    uint8_t alive;          /* 36 */
    uint8_t over_land;      /* 37 */
    uint8_t _pad0[2];       /* 38 */
} climate_system_state_t;
static_assert(sizeof(climate_system_state_t) == 40);

/* What a build did, for the bench and the tests. */
typedef struct climate_systems_stats_t {
    uint64_t hashed;      /* 0: square-day counts worked out */
    uint64_t candidates;  /* 8: candidates drawn */
    uint64_t kept;        /* 16: kept by thinning */
    uint64_t track_steps; /* 24: track samples integrated */
    uint64_t mask_reads;  /* 32: mask heights read */
    uint64_t mask_pages;  /* 40: mask pages fetched into the build's own cache */
    uint64_t _pad0[2];    /* 48 */
} climate_systems_stats_t;
static_assert(sizeof(climate_systems_stats_t) == 64);

/* The coarse land mask as a build reads it: a small direct-mapped cache of pages in the builder's arena, each page from
 * the climate's table, or claimed and built there, or built privately: the same heights whichever way. */
enum : uint32_t {
    CLIMATE_SYSTEMS_MASK_PAGES = 64u, /* this phase: 64 KiB; a track crosses at most a few dozen pages */
    /* Outside the keep square no page is kept, and each read is one column of the ground. The outlook's sums start a typical
     * track from the same square's centre on each day of the week, so they read the same cells day after day: the columns
     * are kept here, direct-mapped by the cell's low bits (plan 006 phase 05: a dry basin's gate made each column dearer). */
    CLIMATE_SYSTEMS_MASK_POINTS = 1024u,
};
typedef struct climate_systems_mask_t {
    const climate_t *borrowed_climate;          /* 0 */
    float32_t *borrowed_pages;                  /* 8: CLIMATE_SYSTEMS_MASK_PAGES x 256, in the builder's arena */
    climate_systems_stats_t *borrowed_stats;    /* 16: may be nullptr */
    int32_t tile_x[CLIMATE_SYSTEMS_MASK_PAGES]; /* 24 */
    int32_t tile_y[CLIMATE_SYSTEMS_MASK_PAGES]; /* 280 */
    uint8_t held[CLIMATE_SYSTEMS_MASK_PAGES];   /* 536 */
    int64_t *borrowed_point_cells;              /* 600: CLIMATE_SYSTEMS_MASK_POINTS x (x, y) */
    float32_t *borrowed_point_heights;          /* 608: CLIMATE_SYSTEMS_MASK_POINTS */
    uint64_t *borrowed_point_held; /* 616: a bit an entry, so starting a build clears 128 bytes, not 20 KiB */
} climate_systems_mask_t;
static_assert(sizeof(climate_systems_mask_t) == 624);

/* ---- The list -------------------------------------------------------------------------------------------------------- */

/* Every system of the asked kinds near (x, y) at any moment of day D: big systems whose 3 R footprint comes within radius,
 * cells within thunder_radius (at most CLIMATE_THUNDER_QUERY_MAX, 8192 blocks). Memory for the list and for any mask page
 * it must build privately comes from arena. A pure function of the climate's seed and its arguments, with one exception:
 * cells read phase 05's normals, so if a tile they need is not built, those squares are skipped and thunder_complete is 0.
 * When cells are asked for, big systems are always found too, out to thunder_radius + 4200 + 1250 at least. False only if
 * arena is too small. Any thread. */
[[nodiscard]] bool climate_systems_day_build(const climate_t *climate, arena_t *arena, float64_t x, float64_t y,
                                             float32_t radius, float32_t thunder_radius, int64_t day, uint32_t kinds,
                                             climate_systems_day_t *out);
/* The same, adding what it did to *stats (may be nullptr). */
[[nodiscard]] bool climate_systems_day_build_stats(const climate_t *climate, arena_t *arena, float64_t x, float64_t y,
                                                   float32_t radius, float32_t thunder_radius, int64_t day,
                                                   uint32_t kinds, climate_systems_day_t *out,
                                                   climate_systems_stats_t *stats);

/* A big system at time t (any time; alive is 0 outside its life). Interpolates between samples. */
void climate_system_state_at(const climate_system_t *system, float64_t t, climate_system_state_t *out);

/* ---- Rates ----------------------------------------------------------------------------------------------------------- */

/* 7.2's rates, exposed for tests, the outlook (phase 07) and the bench. Spawns per square per day, at a point and time. */
float32_t climate_systems_hurricane_rate(const climate_t *climate, arena_t *arena, float64_t x, float64_t y,
                                         float64_t t);
/* The same, reading the land through a build's mask: the same number. */
float32_t climate_systems_hurricane_rate_mask(const climate_t *climate, climate_systems_mask_t *mask, float64_t x,
                                              float64_t y, float64_t t);
float32_t climate_systems_low_rate(const climate_t *climate, float64_t y, float64_t t);
float32_t climate_systems_high_rate(const climate_t *climate, arena_t *arena, float64_t x, float64_t y, float64_t t);
/* 7.2.1 at a 1 km square's centre on day D, before thinning by fronts and highs. Writes false to *ready if its tile is not
 * built. Phase 07 changes T_day here, and only here, by the day's anomaly. */
float32_t climate_systems_thunder_rate(const climate_t *climate, int32_t square_x, int32_t square_y, int64_t day,
                                       bool *ready);
/* The same, and the square-day's cells' rain scale (below) into *rain_scale. */
float32_t climate_systems_thunder_rate_scale(const climate_t *climate, int32_t square_x, int32_t square_y, int64_t day,
                                             bool *ready, float32_t *rain_scale);
/* 7.2.1's lambda_base from its three inputs: G 3 clamp((T - 18) / 10, 0, 1) rain_share (1 + 2 monsoon). Pure. */
float32_t climate_systems_thunder_base(float32_t t_day_c, float32_t rain_share, bool monsoon);
/* A cell's rain scale, 1 / (max(hot, 0.2) rain_share (1 + 2 monsoon)), hot being clamp((T - 18) / 10, 0, 1): heat, the wet
 * season and the monsoon make cells more often, and each one this much lighter, so the rain they bring follows the normal
 * and is not counted twice (Issues found 12). 0 when no cell forms. Pure. */
float32_t climate_systems_thunder_rain_scale(float32_t t_day_c, float32_t rain_share, bool monsoon);

/* The track-time sea temperature: T_lat(|phi|) + A_sea(|phi|) s_h cos(2 pi (d - 215) / 365) + the hooks' current, deg C,
 * with no current of a cell's: what spawning and weakening read (Issues found 1 of the phase 06 file). */
float32_t climate_systems_sea_temperature(const climate_t *climate, float64_t y, float64_t t);
/* s_hur from that sea temperature at y and t, or the hooks' forced season. */
float32_t climate_systems_hurricane_s(const climate_t *climate, float64_t y, float64_t t);

/* The longest distance a system of this kind can reach from its spawn square, blocks: track plus 3 R. */
float64_t climate_systems_reach(uint8_t kind);
/* The square side of a kind, blocks. */
float64_t climate_systems_square(uint8_t kind);

/* ---- Pieces, for tests and phase 07 ------------------------------------------------------------------------------------ */

/* The day of the year of a time t = D + f, 0 to below 365. */
float64_t climate_systems_day_of_year(float64_t t);
/* 3x^2 - 2x^3 on 0 to 1, 0 below, 1 above. */
float64_t climate_systems_smoothstep(float64_t x);
/* The life curve g(tau): grows over the first third, holds, fades over the last third. 0 outside 0 to 1. */
float64_t climate_systems_life(float64_t tau);
/* u from a hash: its top 53 bits times 2^-53, as terrain_rng_next_f64 makes it. */
float64_t climate_systems_uniform(uint64_t hash);
/* The inverse CDF of a Poisson of mean lambda at u, with e^-lambda given, capped at cap. */
uint32_t climate_systems_poisson(float64_t u, float64_t lambda, float64_t exp_minus_lambda, uint32_t cap);
/* A hurricane's v_max from |A|: 33 |A| / 30 under 30 hPa, 33 + 37 (|A| - 30) / 60 from there. */
float32_t climate_systems_hurricane_vmax(float32_t abs_hpa);
/* A cell's velocity, blocks a game day, from the wind at its start (blocks a second): the wind times 1200, its direction
 * kept and its speed capped at CLIMATE_THUNDER_SPEED_MAX (15 blocks a second, 18000 blocks a day). Pure. */
void climate_systems_thunder_velocity(float32_t wind_x, float32_t wind_y, float32_t out[2]);

/* The build's mask: starts empty over pages from arena. */
void climate_systems_mask_start(climate_systems_mask_t *mask, const climate_t *climate, arena_t *arena,
                                climate_systems_stats_t *stats);
/* The mask's height at the 64-block cell holding (x, y), as climate_mask_height_at gives it. */
float32_t climate_systems_mask_height(climate_systems_mask_t *mask, float64_t x, float64_t y);

/* Kept big systems of one kind spawned in one square on one day, tracks integrated, in candidate order, into out (at most
 * capacity; the count is returned and *overflowed set if more were kept). The building block of the list. */
uint32_t climate_systems_square_spawns(const climate_t *climate, climate_systems_mask_t *mask, uint8_t kind,
                                       int32_t square_x, int32_t square_y, int64_t day, climate_system_t *out,
                                       uint32_t capacity, bool *overflowed);
/* Integrates a system's track from its start fields (kind, spawn_day, start, radius, peak, life, speed, turn): fills
 * samples and sample_count. For hand-placed systems in tests, and what the spawner calls. */
void climate_systems_track(const climate_t *climate, climate_systems_mask_t *mask, climate_system_t *system);

/* Cells of one 1 km square on one day at base rate lambda_base, both processes, thinned by the fronts and highs of big (a
 * day list, may hold none), into out (at most capacity), each cell's peak rain times rain_scale. Writes false to
 * *air_ready, and spawns none, if the wind at a kept cell's start is not built. Returns the count; *overflowed set if more
 * were kept. */
uint32_t climate_systems_thunder_square(const climate_t *climate, const climate_systems_day_t *big, int32_t square_x,
                                        int32_t square_y, int64_t day, float32_t lambda_base, float32_t rain_scale,
                                        climate_thunder_t *out, uint32_t capacity, bool *air_ready, bool *overflowed);

#endif
