/* See README.md. D8 flow accumulation and the flood index on the caller's cells: fixed neighbour order, fixed processing order
 * (height descending, then y, then x), one thread, so the same cells give the same bytes whoever calls. */
#include "climate/weather/flow/index.h"

#include <string.h>

void climate_flow_receivers(climate_flow_cell_t *cells, uint32_t count, climate_flow_find_fn_t find,
                            const void *find_context) {
    for (uint32_t i = 0u; i < count; i++) {
        climate_flow_cell_t *const cell = &cells[i];
        cell->receiver = CLIMATE_FLOW_NONE;
        if (cell->sea != 0u) {
            continue; /* the sea is always an outlet */
        }
        float32_t lowest = cell->height;
        for (uint32_t k = 0u; k < CLIMATE_FLOW_NEIGHBOURS; k++) {
            const uint32_t j = find(find_context, cell->cell_x + CLIMATE_FLOW_DX[k], cell->cell_y + CLIMATE_FLOW_DY[k]);
            if (j == CLIMATE_FLOW_NONE) {
                continue;
            }
            /* Strictly lower than the lowest so far: the first in the fixed order wins a tie. */
            if (cells[j].height < lowest) {
                lowest = cells[j].height;
                cell->receiver = j;
            }
        }
    }
}

/* Negative if cell a comes before cell b: higher first, then smaller y, then smaller x. Total on distinct cells. */
static int32_t flow_compare(const climate_flow_cell_t *a, const climate_flow_cell_t *b) {
    if (a->height != b->height) {
        return a->height > b->height ? -1 : 1;
    }
    if (a->cell_y != b->cell_y) {
        return a->cell_y < b->cell_y ? -1 : 1;
    }
    if (a->cell_x != b->cell_x) {
        return a->cell_x < b->cell_x ? -1 : 1;
    }
    return 0;
}

/* A stable bottom-up merge sort, as climate/systems sorts its lists. */
static void flow_sort(const climate_flow_cell_t *cells, uint32_t *index, uint32_t *scratch, uint32_t count) {
    uint32_t *from = index, *to = scratch;
    for (uint32_t width = 1u; width < count; width *= 2u) {
        for (uint32_t left = 0u; left < count; left += 2u * width) {
            const uint32_t middle = left + width < count ? left + width : count;
            const uint32_t right = left + 2u * width < count ? left + 2u * width : count;
            uint32_t a = left, b = middle, o = left;
            while (a < middle && b < right) {
                to[o++] = flow_compare(&cells[from[b]], &cells[from[a]]) < 0 ? from[b++] : from[a++];
            }
            while (a < middle) {
                to[o++] = from[a++];
            }
            while (b < right) {
                to[o++] = from[b++];
            }
        }
        uint32_t *const swap = from;
        from = to;
        to = swap;
    }
    if (from != index) {
        memcpy(index, from, (uint64_t)count * sizeof(uint32_t));
    }
}

void climate_flow_accumulate(climate_flow_cell_t *cells, uint32_t count, uint32_t *index, uint32_t *scratch) {
    for (uint32_t i = 0u; i < count; i++) {
        index[i] = i;
        cells[i].q_week = (float64_t)cells[i].runoff;
        cells[i].q_mean = (float64_t)cells[i].mean;
    }
    flow_sort(cells, index, scratch, count);
    /* Each cell is finished before anything below it, because its receiver is strictly lower and so comes later. */
    for (uint32_t k = 0u; k < count; k++) {
        const climate_flow_cell_t *const cell = &cells[index[k]];
        if (cell->receiver != CLIMATE_FLOW_NONE) {
            cells[cell->receiver].q_week += cell->q_week;
            cells[cell->receiver].q_mean += cell->q_mean;
        }
    }
}

float32_t climate_flow_flood(float64_t q_week, float64_t q_mean) {
    const float64_t bank = (float64_t)CLIMATE_FLOOD_BANKFULL * q_mean;
    const float64_t channel = bank > (float64_t)CLIMATE_FLOOD_CHANNEL_MIN ? bank : (float64_t)CLIMATE_FLOOD_CHANNEL_MIN;
    return (float32_t)(q_week / channel);
}
