/* Synthetic grounds for the climate's tests, here and in plan 004 phase 04: pure functions of x and y with the shape of a
 * climate_ground_fn_t, so a test builds real tiles over land whose answer it knows by hand. Heights are in blocks; water is
 * at or below 0. */
#ifndef MC_TESTS_RUN_CLIMATE_FIXTURES_GROUND_H
#define MC_TESTS_RUN_CLIMATE_FIXTURES_GROUND_H

#include "climate/index.h"

#include <stdint.h>

typedef enum ground_kind_t : uint32_t {
    GROUND_PLANE = 0,    /* h = x - 3 y */
    GROUND_RAMP_X = 1,   /* h = x */
    GROUND_ALL_LAND = 2, /* 20 */
    GROUND_ALL_SEA = 3,  /* -50 */
    GROUND_COAST_X = 4,  /* sea (-50) west of x = coast, land (20) east of it */
    GROUND_CORNER = 5,   /* sea where x < 64 and y < 0: cell x <= 0 and cell y <= -1, land elsewhere */
    GROUND_LAKES = 6,    /* land, with a sea west of x = coast and up to GROUND_RECTS_MAX rectangles of water */
    GROUND_ISLAND = 7,   /* sea, with a round island of radius blocks at (centre_x, centre_y) */
    /* Plan 004 phase 04's grounds. */
    GROUND_SQUARE = 8,    /* sea (-60), with land (30) inside rects[0], both ends included: a synthetic continent */
    GROUND_COAST_Y = 9,   /* land (20) for y >= coast, sea (-60) south of it, all x */
    GROUND_OPEN_SEA = 10, /* -100 everywhere */
    GROUND_RIDGE = 11,    /* land: 20 west of x = 10,000, rising 0.8 a block to 420 at x = 10,500, 420 east of it */
    /* Plan 004 phase 05's grounds. */
    GROUND_SOUTH_AFRICA =
        12, /* Ahmed's sketch by x alone: west sea, a 500 plateau, a 750 crest, the escarpment, east sea */
    GROUND_PEAK_RIDGE =
        13,            /* land 20, with a ridge 400 high along x = 10,000, falling straight to 20 1000 blocks out */
    GROUND_LEVEL = 14, /* land at radius blocks everywhere */
    GROUND_HIGH_COAST_Y = 15, /* land at radius blocks for y >= coast, sea (-60) south of it, all x */
    /* Plan 007 phase 05's ground. */
    GROUND_HOLLOW =
        16, /* GROUND_COAST_Y, with a round hollow to -100 of radius blocks at (centre_x, centre_y): dry if dry */
} ground_kind_t;

enum : uint32_t { GROUND_RECTS_MAX = 4u };

/* A rectangle of water in blocks, both ends included. */
typedef struct ground_rect_t {
    int32_t x_min; /* 0 */
    int32_t y_min; /* 4 */
    int32_t x_max; /* 8 */
    int32_t y_max; /* 12 */
} ground_rect_t;
static_assert(sizeof(ground_rect_t) == 16);

typedef struct ground_t {
    ground_rect_t rects[GROUND_RECTS_MAX]; /* 0 */
    uint32_t kind;                         /* 64: ground_kind_t */
    uint32_t rect_count;                   /* 68 */
    int32_t coast;                         /* 72: the coasts of the COAST kinds and LAKES; INT32_MIN for no sea */
    int32_t radius;                        /* 76: GROUND_ISLAND; the height of GROUND_LEVEL and GROUND_HIGH_COAST_Y */
    int32_t centre_x;                      /* 80: GROUND_ISLAND */
    int32_t centre_y;                      /* 84 */
    uint32_t dry;                          /* 88: GROUND_HOLLOW: 1 says the hollow is a dry basin */
    uint8_t _pad0[4];                      /* 92 */
} ground_t;
static_assert(sizeof(ground_t) == 96);

/* Whether (x, y) is in GROUND_HOLLOW's hollow. */
[[maybe_unused]] static bool ground_in_hollow(const ground_t *ground, int64_t x, int64_t y) {
    const int64_t dx = x - ground->centre_x, dy = y - ground->centre_y;
    return ground->kind == GROUND_HOLLOW && dx * dx + dy * dy <= (int64_t)ground->radius * ground->radius;
}

/* The phase 05 file's South Africa fixture, by x alone, piecewise straight: the west sea (-50) to x = 0, the coast rising
 * from 1 to 500 by 1000, the plateau at 500 to 10,000, the crest up to 750 at 10,400, down to 250 at 10,912, the
 * escarpment face down to 1 at 11,742 (250 over 830 blocks), the coastal strip at 1 to 11,968, then the east sea (-50). */
[[maybe_unused]] static float32_t ground_south_africa(int64_t x) {
    typedef struct ground_knot_t {
        float32_t x; /* 0 */
        float32_t h; /* 4 */
    } ground_knot_t;
    static_assert(sizeof(ground_knot_t) == 8);
    static const ground_knot_t KNOTS[7] = {
        {0.0f32, 1.0f32},       {1000.0f32, 500.0f32}, {10000.0f32, 500.0f32}, {10400.0f32, 750.0f32},
        {10912.0f32, 250.0f32}, {11742.0f32, 1.0f32},  {11968.0f32, 1.0f32},
    };
    if (x < 0 || x >= 11968) {
        return -50.0f32;
    }
    const float32_t at = (float32_t)x;
    for (uint32_t k = 1u; k < 7u; k++) {
        if (at < KNOTS[k].x) {
            const float32_t t = (at - KNOTS[k - 1u].x) / (KNOTS[k].x - KNOTS[k - 1u].x);
            return KNOTS[k - 1u].h + t * (KNOTS[k].h - KNOTS[k - 1u].h);
        }
    }
    return 1.0f32;
}

[[maybe_unused]] static float32_t ground_height(const ground_t *ground, int64_t x, int64_t y) {
    switch ((ground_kind_t)ground->kind) {
    case GROUND_PLANE:
        return (float32_t)(x - 3 * y);
    case GROUND_RAMP_X:
        return (float32_t)x;
    case GROUND_ALL_LAND:
        return 20.0f32;
    case GROUND_ALL_SEA:
        return -50.0f32;
    case GROUND_COAST_X:
        return x < (int64_t)ground->coast ? -50.0f32 : 20.0f32;
    case GROUND_CORNER:
        return (x < 64 && y < 0) ? -50.0f32 : 20.0f32;
    case GROUND_LAKES:
        if (x < (int64_t)ground->coast) {
            return -50.0f32;
        }
        for (uint32_t r = 0u; r < ground->rect_count; r++) {
            const ground_rect_t *const rect = &ground->rects[r];
            if (x >= rect->x_min && x <= rect->x_max && y >= rect->y_min && y <= rect->y_max) {
                return -10.0f32;
            }
        }
        return 20.0f32;
    case GROUND_ISLAND: {
        const int64_t dx = x - ground->centre_x, dy = y - ground->centre_y;
        return dx * dx + dy * dy <= (int64_t)ground->radius * ground->radius ? 20.0f32 : -50.0f32;
    }
    case GROUND_SQUARE: {
        const ground_rect_t *const land = &ground->rects[0];
        return (x >= land->x_min && x <= land->x_max && y >= land->y_min && y <= land->y_max) ? 30.0f32 : -60.0f32;
    }
    case GROUND_COAST_Y:
        return y >= (int64_t)ground->coast ? 20.0f32 : -60.0f32;
    case GROUND_OPEN_SEA:
        return -100.0f32;
    case GROUND_RIDGE:
        return x < 10000 ? 20.0f32 : (x < 10500 ? 20.0f32 + 0.8f32 * (float32_t)(x - 10000) : 420.0f32);
    case GROUND_SOUTH_AFRICA:
        return ground_south_africa(x);
    case GROUND_PEAK_RIDGE: {
        const int64_t off = x > 10000 ? x - 10000 : 10000 - x;
        return off >= 1000 ? 20.0f32 : 20.0f32 + 400.0f32 * (float32_t)(1000 - off) / 1000.0f32;
    }
    case GROUND_LEVEL:
        return (float32_t)ground->radius;
    case GROUND_HIGH_COAST_Y:
        return y >= (int64_t)ground->coast ? (float32_t)ground->radius : -60.0f32;
    case GROUND_HOLLOW:
        return ground_in_hollow(ground, x, y) ? -100.0f32 : (y >= (int64_t)ground->coast ? 20.0f32 : -60.0f32);
    }
    return 0.0f32;
}

/* A climate_ground_fn_t over a ground_t. */
[[maybe_unused]] static void ground_fn(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                       uint32_t count_x, uint32_t count_y, float32_t *heights, uint8_t *dry) {
    const ground_t *const ground = borrowed_context;
    for (uint32_t j = 0u; j < count_y; j++) {
        for (uint32_t i = 0u; i < count_x; i++) {
            const int64_t x = (int64_t)origin_x + (int64_t)step * i, y = (int64_t)origin_y + (int64_t)step * j;
            heights[(uint64_t)j * count_x + i] = ground_height(ground, x, y);
            if (dry != nullptr && ground->dry != 0u && ground_in_hollow(ground, x, y)) {
                dry[(uint64_t)j * count_x + i] = 1u;
            }
        }
    }
}

/* A rectangle of water w by h cells, its south-west cell at cell (cell_x, cell_y): every centre in it is water and none
 * outside it. */
/* A synthetic continent: land for x_min <= x < x_max and y_min <= y < y_max, half-open as the phase 04 file gives them. */
[[maybe_unused]] static ground_t ground_continent(int32_t x_min, int32_t y_min, int32_t x_max, int32_t y_max) {
    return (ground_t){
        .kind = GROUND_SQUARE,
        .rect_count = 1u,
        .rects = {{.x_min = x_min, .y_min = y_min, .x_max = x_max - 1, .y_max = y_max - 1}},
    };
}

[[maybe_unused]] static ground_rect_t ground_cells(int32_t cell_x, int32_t cell_y, int32_t w, int32_t h) {
    return (ground_rect_t){
        .x_min = cell_x * CLIMATE_CELL_BLOCKS,
        .y_min = cell_y * CLIMATE_CELL_BLOCKS,
        .x_max = (cell_x + w) * CLIMATE_CELL_BLOCKS - 1,
        .y_max = (cell_y + h) * CLIMATE_CELL_BLOCKS - 1,
    };
}

#endif
