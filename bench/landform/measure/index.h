/* See README.md beside this. bench_landform's measures: what the program, its pictures and its self-test share. */
#ifndef MC_BENCH_LANDFORM_MEASURE_INDEX_H
#define MC_BENCH_LANDFORM_MEASURE_INDEX_H

#include "job/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "types/index.h"
#include "ui/font/index.h"
#include "climate/normals/index.h"
#include "world/column/index.h"

#include <stdint.h>

enum : uint32_t {
    LF_WORKERS = 16u,
    LF_SIDE_MAX = 4096u,
    LF_PIXELS_MAX = LF_SIDE_MAX * LF_SIDE_MAX,
    /* A window: 1024 blocks a side at 4 blocks a column, with one more column all round for the slope. */
    LF_WINDOW_STEP = 4u,
    LF_WINDOW_SIDE = 256u,
    LF_WINDOW_GRID = LF_WINDOW_SIDE + 2u,
    LF_WINDOW_COLUMNS = LF_WINDOW_GRID * LF_WINDOW_GRID,
    LF_WINDOWS_MAX = 64u,
    LF_WINDOW_GAP = 4u,
    LF_CLASSES = 7u,
    LF_DECILES = 10u,
    /* Slopes for a median: bin 0 is under 10^-4, then a bin every thousandth of a decade up to 100. */
    LF_MEDIAN_BINS = 6002u,
    LF_RAYS = 16u,
    LF_RAY_SAMPLES = 1024u,
    LF_PEAKS_MAX = 8192u,
    LF_SEEDS_MAX = 16u,
    LF_PATH_BYTES = 512u,
    LF_TEXT_BYTES = 8192u,
    LF_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    LF_BAND = 12u,
    LF_BAND_WIDEN = 128u,
    LF_STRIP_STEP = 8u,
    LF_STRIP_WIDE = 512u,
    LF_STRIP_HIGH = 4788u,
    LF_STRIP_LEFT = 64u,
    LF_STRIP_TOP = 40u,
    LF_PANEL_WIDE = 4096u,
    LF_PANEL_MOST = 1500u,
    LF_PANELS = 4u,
    LF_PICTURE_PIXELS = LF_PANEL_WIDE * 6400u,
    LF_SCRATCH_BYTES = 2u << 20,
    LF_CHUNK = 16u,
    LF_COST_SIDE = 6u,
    LF_COST_CHUNKS = LF_COST_SIDE * LF_COST_SIDE,
    LF_COST_LATTICE = LF_COST_CHUNKS * 25u,
    LF_COST_ROUNDS = 3u,
    LF_COST_REPEATS = 20u,
    LF_SELF_STEP = 4u,
};
static_assert(MC_THREADS_WORKERS >= LF_WORKERS);
static_assert(LF_PICTURE_PIXELS >= LF_PIXELS_MAX);
static_assert(LF_PANELS * (LF_PANEL_MOST + 32u) <= LF_PICTURE_PIXELS / LF_PANEL_WIDE);

static const float32_t LF_HUGE = 3.0e38f32;
static const uint32_t LF_COLOUR_SEA = 0x2F78D8u;
static const uint32_t LF_COLOUR_DEEP = 0x0C2460u;
extern const float64_t LF_CLASS_FROM[LF_CLASSES - 1u];
extern const uint32_t LF_CLASS_COLOUR[LF_CLASSES];
extern const char *const LF_CLASS_NAME[LF_CLASSES];
extern const int32_t LF_REACH[5];

/* Every number of the table, by name: one row of the CSV each. */
typedef enum lf_value_t : uint32_t {
    LF_V_PROFILE = 0,                         /* 7: % of land in each class, the profile at 4 in the windows */
    LF_V_MAP = LF_V_PROFILE + LF_CLASSES,     /* 7: the same over the whole map, at its step, 8 blocks across */
    LF_V_REAL = LF_V_MAP + LF_CLASSES,        /* 7: the same from real blocks in the windows */
    LF_V_DIFFERENCE = LF_V_REAL + LF_CLASSES, /* points: the largest class difference, profile against blocks */
    LF_V_STEEP_TOP,                           /* % of ground steeper than 1:2 in the top 30% of local relief */
    LF_V_DECILE_RATIO,                        /* top decile's median slope over the bottom 3's */
    LF_V_DECILE,                              /* 10: median slope in each decile of relative height */
    LF_V_PEAKS = LF_V_DECILE + LF_DECILES,
    LF_V_CONCAVITY,
    LF_V_WIDTH,
    LF_V_PLATEAUS,
    LF_V_PLATEAUS_BIG,
    LF_V_PLATEAU_KM2,
    LF_V_PLATEAU_ACROSS,
    LF_V_ESCARPMENT,
    LF_V_RANGES,
    LF_V_RANGE_KM,
    LF_V_RANGE_KM2,
    LF_V_RANGE_ELONGATION,
    LF_V_KNOBS,
    LF_V_KNOB_BASE, /* 3: under 150, 150 to 250, 250 and over */
    LF_V_SEA = LF_V_KNOB_BASE + 3u,
    LF_V_REACH, /* 5 */
    LF_V_TOP = LF_V_REACH + 5u,
    LF_V_BASINS,           /* dry basins of at least 1 km^2 below the sea's level (plan 007 phase 05) */
    LF_V_BASIN_FLOOR_LOW,  /* the deepest of their floors */
    LF_V_BASIN_FLOOR_HIGH, /* the shallowest */
    LF_V_BASIN_ACROSS_MIN, /* km, the larger of a basin's two extents: the narrowest basin's */
    LF_V_BASIN_ACROSS_MAX, /* the widest's */
    LF_V_RANGE_PEAKS,      /* peaks the concavity is measured on: 500 over their 3 km, off plateaus and knobs */
    LF_V_SECONDS,          /* from here on, times: never compared */
    LF_V_PROFILE_US,
    LF_V_PROFILE_SHARE,
    LF_V_TALL_COLUMNS,
    LF_V_PLAIN_COLUMNS,
    LF_V_COUNT,
} lf_value_t;

extern const char *const LF_VALUE_NAME[LF_V_COUNT];

typedef enum lf_place_kind_t : uint32_t {
    LF_PLACE_PLATEAU = 0,
    LF_PLACE_PEAK,
    LF_PLACE_RANGE,
    LF_PLACE_STEEP_WINDOW,
    LF_PLACE_KNOB_HIGH,
    LF_PLACE_BELOW_SEA,
    LF_PLACE_PLAINS,
    LF_PLACES,
} lf_place_kind_t;
extern const char *const LF_PLACE_NAME[LF_PLACES];

typedef struct lf_place_t {
    int32_t x;        /* 0 */
    int32_t y;        /* 4 */
    float32_t z;      /* 8 */
    uint8_t found;    /* 12 */
    uint8_t _pad0[3]; /* 13 */
} lf_place_t;
static_assert(sizeof(lf_place_t) == 16);

typedef struct lf_result_t {
    float64_t v[LF_V_COUNT];     /* 0 */
    lf_place_t place[LF_PLACES]; /* 544 */
    uint64_t seed;               /* 656 */
    uint32_t range_direction;    /* 664: 0 along x, 1 along y, 2 and 3 the diagonals */
    uint32_t plateau_edge;       /* 668: an escarpment pixel nearest the largest plateau's middle, or UINT32_MAX */
} lf_result_t;
static_assert(sizeof(lf_result_t) == 672);

/* A square of heights, row after row, y growing down the rows. */
typedef struct lf_grid_t {
    float32_t *h;     /* 0 */
    float32_t *g;     /* 8: the gradient, rise over run */
    uint32_t side;    /* 16 */
    uint32_t step;    /* 20: blocks from one column to the next */
    int32_t origin_x; /* 24 */
    int32_t origin_y; /* 28 */
    uint8_t *dry;     /* 32: 1 in a dry basin (plan 007 phase 05), or nullptr for none */
    float32_t *knob;  /* 40: what a knob adds to each column (plan 007 phase 06), or nullptr for none */
} lf_grid_t;
static_assert(sizeof(lf_grid_t) == 48);

typedef struct lf_peak_t {
    uint32_t at; /* 0 */
    float32_t h; /* 4 */
} lf_peak_t;
static_assert(sizeof(lf_peak_t) == 8);

typedef enum lf_stage_t : uint32_t {
    LF_STAGE_SWEEP = 0,
    LF_STAGE_SLIDE_ROWS,
    LF_STAGE_SLIDE_COLUMNS,
    LF_STAGE_KNOBS,
    LF_STAGE_WINDOWS_PROFILE,
    LF_STAGE_WINDOWS_REAL,
    LF_STAGE_STRIP,
    LF_STAGE_COST,
} lf_stage_t;

/* What the jobs share. */
typedef struct lf_run_t {
    const terrain_t *borrowed_terrain;      /* 0 */
    const lf_grid_t *borrowed_grid;         /* 8 */
    const float32_t *borrowed_src;          /* 16 */
    float32_t *borrowed_dst;                /* 24 */
    uint8_t *borrowed_scratch;              /* 32: LF_SCRATCH_BYTES a worker */
    float32_t *borrowed_windows_profile;    /* 40 */
    float32_t *borrowed_windows_real;       /* 48 */
    uint8_t *borrowed_windows_land;         /* 56 */
    float32_t *borrowed_strip;              /* 64 */
    uint8_t *borrowed_knobs;                /* 72 */
    const int32_t *borrowed_window_origins; /* 80: x, y of each window's first inner column */
    _Atomic uint32_t next;                  /* 88 */
    _Atomic uint32_t next_worker;           /* 92 */
    lf_stage_t stage;                       /* 96 */
    uint32_t items;                         /* 100 */
    uint32_t jobs;                          /* 104 */
    uint32_t half;                          /* 108: of a sliding square, in columns */
    int32_t cost_x;                         /* 112 */
    int32_t cost_y;                         /* 116 */
    uint8_t is_max;                         /* 120 */
    uint8_t _pad0[7];                       /* 121 */
} lf_run_t;
static_assert(sizeof(lf_run_t) == 128);

/* Buffers, all in the one region. */
typedef struct lf_memory_t {
    float32_t *h;                              /* 0: LF_PIXELS_MAX each, to the queue */
    float32_t *g;                              /* 8 */
    float32_t *a;                              /* 16 */
    float32_t *b;                              /* 24 */
    float32_t *c;                              /* 32 */
    float32_t *low;                            /* 40: lowest within LF_LOW_HALF */
    uint8_t *plateau;                          /* 48: 1 plateau, 2 its escarpment edge */
    uint8_t *range;                            /* 56 */
    uint8_t *knobs;                            /* 64: 1 + the band of its base */
    uint32_t *queue;                           /* 72 */
    uint32_t *pixels;                          /* 80: LF_PICTURE_PIXELS */
    uint8_t *stream;                           /* 88 */
    uint64_t stream_bytes;                     /* 96 */
    float32_t *windows_profile;                /* 104: LF_WINDOWS_MAX * LF_WINDOW_COLUMNS */
    float32_t *windows_real;                   /* 112 */
    uint8_t *windows_land;                     /* 120 */
    float32_t *strip;                          /* 128: LF_STRIP_WIDE * LF_STRIP_HIGH */
    uint32_t *median;                          /* 136: (LF_DECILES + 1) * LF_MEDIAN_BINS */
    lf_peak_t *peaks;                          /* 144: LF_PEAKS_MAX */
    float32_t *ray;                            /* 152: 2 * LF_RAY_SAMPLES, then 3 * LF_PEAKS_MAX */
    int32_t *window_origins;                   /* 160: 2 * LF_WINDOWS_MAX */
    const ui_font_t *borrowed_font;            /* 168: or nullptr: no labels */
    const climate_normals_t *borrowed_normals; /* 176 */
    const char *directory;                     /* 184 */
    uint8_t *dry;                              /* 192: LF_PIXELS_MAX */
    float32_t *knob;                           /* 200: LF_PIXELS_MAX */
    float32_t *surface;                        /* 208: LF_PIXELS_MAX: the ground less its knobs, for plateaus */
} lf_memory_t;
static_assert(sizeof(lf_memory_t) == 216);

/* The class of a slope, 0 flat to 6 sheer. */
uint32_t lf_class(float64_t g);

float64_t lf_percent(uint64_t part, uint64_t whole);

/* The gradient from the heights either side, across blocks apart. */
float64_t lf_gradient(float64_t x_minus, float64_t x_plus, float64_t y_minus, float64_t y_plus, float64_t across);

/* A column of the grid as a place. */
lf_place_t lf_place_at(const lf_grid_t *grid, uint32_t at);

/* Runs a stage over items on run->jobs jobs. */
void lf_jobs(job_system_t *system, lf_run_t *run, lf_stage_t stage, uint32_t items);

/* Everything the table holds that run->borrowed_grid alone gives, into r. Uses every buffer of m but the pictures'. */
void lf_measure(job_system_t *system, lf_run_t *run, const lf_memory_t *m, lf_result_t *r);

/* The windows of 1 km^2 spread evenly over the grid, from the profile and from real blocks: their slope classes, the
 * steepest of them, and the lowest dry land below 0. */
void lf_windows(job_system_t *system, lf_run_t *run, const lf_memory_t *m, const lf_grid_t *grid, uint32_t windows_side,
                lf_result_t *r);

#endif
