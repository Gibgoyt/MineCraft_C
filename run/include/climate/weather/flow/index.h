/* See run/src/climate/weather/flow/README.md. */
#ifndef MC_RUN_CLIMATE_WEATHER_FLOW_INDEX_H
#define MC_RUN_CLIMATE_WEATHER_FLOW_INDEX_H

#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 07: flow accumulation by D8 over cells the caller lists, and the flood index (master plan 7.5, and the
 * phase 07 file's "Flow accumulation (D8), with a worked grid" and "The flood index"). Pure, on the caller's arrays. */

static const float32_t CLIMATE_FLOOD_BANKFULL = 3.0f32;     /* phase 07: a channel holds 3 times its usual week */
static const float32_t CLIMATE_FLOOD_CHANNEL_MIN = 20.0f32; /* phase 07: cell-mm, so a dry hillside does not flood */

enum : uint32_t {
    CLIMATE_FLOW_NONE = 0xFFFFFFFFu, /* no receiver: a pit, the sea, or the edge of the kept cells */
    CLIMATE_FLOW_NEIGHBOURS = 8u,
};

/* The fixed neighbour order ties go by: N, NE, E, SE, S, SW, W, NW, with north +y and east +x. */
static const int32_t CLIMATE_FLOW_DX[CLIMATE_FLOW_NEIGHBOURS] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int32_t CLIMATE_FLOW_DY[CLIMATE_FLOW_NEIGHBOURS] = {1, 1, 0, -1, -1, -1, 0, 1};

/* One cell of the graph. The caller fills cell_x to sea; the functions below fill the rest. */
typedef struct climate_flow_cell_t {
    int32_t cell_x;    /* 0: in cells of 64 blocks */
    int32_t cell_y;    /* 4 */
    float32_t height;  /* 8: z_ground, blocks */
    float32_t runoff;  /* 12: this week's, mm over the cell */
    float32_t mean;    /* 16: the long-run mean weekly runoff, mm */
    uint32_t receiver; /* 20: index of the lowest lower neighbour, or CLIMATE_FLOW_NONE */
    float64_t q_week;  /* 24: this week's flow accumulation, cell-mm */
    float64_t q_mean;  /* 32: the same of the long-run means */
    uint8_t sea;       /* 40: always an outlet */
    uint8_t _pad0[7];  /* 41 */
} climate_flow_cell_t;
static_assert(sizeof(climate_flow_cell_t) == 48);

/* Where the cell (cell_x, cell_y) is in the caller's list, or CLIMATE_FLOW_NONE if it is not kept. */
typedef uint32_t (*climate_flow_find_fn_t)(const void *borrowed_context, int32_t cell_x, int32_t cell_y);

/* Each cell's receiver: the lowest of its 8 neighbours that is kept and lower than itself, the first in the fixed order on a
 * tie; none for a sea cell or a cell with no lower neighbour. Pure. */
void climate_flow_receivers(climate_flow_cell_t *cells, uint32_t count, climate_flow_find_fn_t find,
                            const void *find_context);

/* Sorts index[0 .. count - 1] by height descending, then y, then x, with scratch of count; then adds each cell's own runoff
 * and mean to its totals and its totals into its receiver, highest first, so one pass is enough. Fills q_week and q_mean.
 * index and scratch hold count each. No recursion. Pure. */
void climate_flow_accumulate(climate_flow_cell_t *cells, uint32_t count, uint32_t *index, uint32_t *scratch);

/* F = Q_w / max(Q_min, 3 Qbar): over 1 is water over the banks. Pure. */
float32_t climate_flow_flood(float64_t q_week, float64_t q_mean);

#endif
