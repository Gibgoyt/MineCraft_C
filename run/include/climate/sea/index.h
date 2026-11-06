/* See run/src/climate/sea/README.md. */
#ifndef MC_RUN_CLIMATE_SEA_INDEX_H
#define MC_RUN_CLIMATE_SEA_INDEX_H

#include "types/index.h"

#include <stdint.h>

/* What each cell of a grid is, once its water has been joined up. */
enum : uint8_t {
    CLIMATE_SEA_KIND_LAND = 0u,
    CLIMATE_SEA_KIND_SEA = 1u,        /* touches the grid's edge, or is at least CLIMATE_SEA_AREA_MIN_CELLS */
    CLIMATE_SEA_KIND_LAKE_LARGE = 2u, /* enclosed, and at least CLIMATE_LAKE_LARGE_CELLS */
    CLIMATE_SEA_KIND_LAKE_SMALL = 3u, /* enclosed and smaller: land for every climate purpose */
};

enum : uint32_t {
    /* Phase 03: water this big is sea even wholly inside the window, 8192 * 64^2 blocks^2 = 33.6 km^2. */
    CLIMATE_SEA_AREA_MIN_CELLS = 8192u,
    /* Master plan 6.2's "lakes over 2 km^2", in 64-block cells, rounded up: 489 * 4096 = 2.003 km^2. */
    CLIMATE_LAKE_LARGE_CELLS = 489u,
    /* A squared distance with no marked cell anywhere in the grid. */
    CLIMATE_SEA_FAR = UINT32_MAX,
};

/* Working memory for climate_sea_distance on a grid side cells across. The caller lends it, from an arena. */
typedef struct climate_sea_scratch_t {
    uint32_t *words;      /* 0: 5 * side + 1 */
    float64_t *crossings; /* 8: side + 1 */
} climate_sea_scratch_t;
static_assert(sizeof(climate_sea_scratch_t) == 16);

/* Labels the 4-connected water components of a side by side grid (water[i] != 0), row 0 first and x fastest, numbering
 * them 1, 2, ... in row-major order of their first cell; land is 0. Then says which are sea: a component that touches the
 * grid's edge, or has at least CLIMATE_SEA_AREA_MIN_CELLS cells. Out: kind[i], CLIMATE_SEA_KIND_*. labels and queue hold
 * side * side entries each, kind side * side bytes. Returns the number of components. Pure, allocates nothing. */
uint32_t climate_sea_classify(const uint8_t *water, uint32_t side, uint32_t *labels, uint32_t *queue, uint8_t *kind);

/* Exact squared distance, in cells^2, from every cell of a side by side grid to the nearest marked one (marked[i] != 0),
 * and that one's index, by Felzenszwalb and Huttenlocher's two sweeps: rows, then columns. Ties go to the lower index in
 * each sweep. CLIMATE_SEA_FAR in both where nothing is marked. Pure, allocates nothing. */
void climate_sea_distance(const uint8_t *marked, uint32_t side, uint32_t *distance2, uint32_t *nearest,
                          const climate_sea_scratch_t *scratch);

#endif
