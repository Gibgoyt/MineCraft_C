/* See README.md. Which water is sea, and how far everything is from it: plan 004 master plan 6.2, and the phase 03 file's
 * "Flood fill", "Which water is sea" and "Distance transform". Grids only: nothing here knows of tiles or terrain. */
#include "climate/sea/index.h"

#include <string.h>

/* ---- Components -------------------------------------------------------------------------------------------------- */

uint32_t climate_sea_classify(const uint8_t *water, uint32_t side, uint32_t *labels, uint32_t *queue, uint8_t *kind) {
    const uint32_t cells = side * side;
    memset(labels, 0, (uint64_t)cells * sizeof(uint32_t));
    memset(kind, CLIMATE_SEA_KIND_LAND, cells);
    uint32_t count = 0u;
    for (uint32_t first = 0u; first < cells; first++) {
        if (water[first] == 0u || labels[first] != 0u) {
            continue;
        }
        /* A breadth-first fill from an explicit queue: recursion is not allowed, and the queue is then this one component,
         * in order, which is what the kinds are written from. */
        count++;
        uint32_t head = 0u;
        uint32_t tail = 0u;
        queue[tail++] = first;
        labels[first] = count;
        bool deep = false;
        while (head < tail) {
            const uint32_t at = queue[head++];
            const uint32_t x = at % side;
            const uint32_t y = at / side;
            deep = deep || water[at] >= CLIMATE_SEA_WATER_DEEP;
            /* North, south, east and west only: water does not leak through a corner where two land cells meet. */
            const uint32_t next[4] = {
                y + 1u < side ? at + side : at,
                y > 0u ? at - side : at,
                x + 1u < side ? at + 1u : at,
                x > 0u ? at - 1u : at,
            };
            for (uint32_t n = 0u; n < 4u; n++) {
                const uint32_t to = next[n];
                if (water[to] != 0u && labels[to] == 0u) {
                    labels[to] = count;
                    queue[tail++] = to;
                }
            }
        }
        uint8_t component = CLIMATE_SEA_KIND_LAKE_SMALL;
        if (deep || tail >= CLIMATE_SEA_AREA_MIN_CELLS) {
            component = CLIMATE_SEA_KIND_SEA;
        } else if (tail >= CLIMATE_LAKE_LARGE_CELLS) {
            component = CLIMATE_SEA_KIND_LAKE_LARGE;
        }
        for (uint32_t i = 0u; i < tail; i++) {
            kind[queue[i]] = component;
        }
    }
    return count;
}

/* ---- The distance transform -------------------------------------------------------------------------------------- */

/* Below any crossing two parabolas on a grid this small can have, and above. */
static const float64_t SEA_CROSSING_LOW = -1.0e300f64;
static const float64_t SEA_CROSSING_HIGH = 1.0e300f64;

/* One sweep along n points: d[q] = min over p of (q - p)^2 + f[p], with f[p] == CLIMATE_SEA_FAR taking no part, and arg[q]
 * that p. The lower envelope of the parabolas, walked once to build and once to read. v and z are working memory. */
static void sea_envelope(const uint32_t *f, uint32_t n, uint32_t *d, uint32_t *arg, uint32_t *v, float64_t *z) {
    uint32_t k = 0u;
    bool any = false;
    for (uint32_t q = 0u; q < n; q++) {
        if (f[q] == CLIMATE_SEA_FAR) {
            continue;
        }
        const float64_t from_q = (float64_t)f[q] + (float64_t)q * (float64_t)q;
        if (!any) {
            any = true;
            k = 0u;
            v[0] = q;
            z[0] = SEA_CROSSING_LOW;
            z[1] = SEA_CROSSING_HIGH;
            continue;
        }
        for (;;) {
            const uint32_t p = v[k];
            const float64_t from_p = (float64_t)f[p] + (float64_t)p * (float64_t)p;
            const float64_t s = (from_q - from_p) / (2.0f64 * (float64_t)q - 2.0f64 * (float64_t)p);
            /* z[0] is below everything, so k never goes under 0. A crossing equal to z[k] drops v[k]: it is never lower
             * than both its neighbours, and the earlier one keeps the tie. */
            if (s <= z[k]) {
                k--;
                continue;
            }
            k++;
            v[k] = q;
            z[k] = s;
            z[k + 1u] = SEA_CROSSING_HIGH;
            break;
        }
    }
    if (!any) {
        for (uint32_t q = 0u; q < n; q++) {
            d[q] = CLIMATE_SEA_FAR;
            arg[q] = CLIMATE_SEA_FAR;
        }
        return;
    }
    k = 0u;
    for (uint32_t q = 0u; q < n; q++) {
        /* Strictly below: at a crossing that falls on q exactly, the earlier parabola, so the lower index, keeps q. */
        while (z[k + 1u] < (float64_t)q) {
            k++;
        }
        const uint32_t p = v[k];
        const uint32_t gap = q > p ? q - p : p - q;
        d[q] = gap * gap + f[p];
        arg[q] = p;
    }
}

void climate_sea_distance(const uint8_t *marked, uint32_t side, uint32_t *distance2, uint32_t *nearest,
                          const climate_sea_scratch_t *scratch) {
    uint32_t *const f = scratch->words;
    uint32_t *const d = f + side;
    uint32_t *const arg = d + side;
    uint32_t *const v = arg + side;
    uint32_t *const columns = v + side + 1u; /* side + 1 for v, as z has */
    float64_t *const z = scratch->crossings;

    /* Rows: the squared distance along the row to the nearest marked cell in it, and that cell's x. */
    for (uint32_t y = 0u; y < side; y++) {
        const uint32_t row = y * side;
        for (uint32_t x = 0u; x < side; x++) {
            f[x] = marked[row + x] != 0u ? 0u : CLIMATE_SEA_FAR;
        }
        sea_envelope(f, side, distance2 + row, nearest + row, v, z);
    }
    /* Columns, over the rows' answers: (x - xs)^2 + (y - ys)^2 splits into a row part and a column part, so this is the
     * exact squared distance. The nearest is then (the row pass's x in the row chosen, that row). */
    for (uint32_t x = 0u; x < side; x++) {
        for (uint32_t y = 0u; y < side; y++) {
            f[y] = distance2[y * side + x];
            columns[y] = nearest[y * side + x];
        }
        sea_envelope(f, side, d, arg, v, z);
        for (uint32_t y = 0u; y < side; y++) {
            distance2[y * side + x] = d[y];
            nearest[y * side + x] = arg[y] == CLIMATE_SEA_FAR ? CLIMATE_SEA_FAR : arg[y] * side + columns[arg[y]];
        }
    }
}
