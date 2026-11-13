/* Dry basins found by reading the land (plan 007 phase 05): the dry columns of the density's profile on a map, every step
 * blocks, joined side to side into basins, each with its deepest column, its floor and the box round it. For the tests
 * that check a basin's rim and what its voxels hold, so they go where the land puts basins, not to a hard-coded place. */
#ifndef MC_TESTS_RUN_TERRAIN_FIXTURES_BASINS_H
#define MC_TESTS_RUN_TERRAIN_FIXTURES_BASINS_H

#include "terrain/index.h"

#include <stdint.h>
#include <string.h>

enum : uint32_t {
    BASINS_ROW_MAX = 512u, /* the widest map a find takes, in columns */
};

/* One basin. */
typedef struct basins_found_t {
    int32_t x;       /* 0: its deepest column */
    int32_t y;       /* 4 */
    float32_t floor; /* 8: the profile's height there */
    uint32_t cells;  /* 12: map columns in it */
    int32_t west;    /* 16: the box round it, in blocks, both ends in it */
    int32_t south;   /* 20 */
    int32_t east;    /* 24 */
    int32_t north;   /* 28 */
} basins_found_t;
static_assert(sizeof(basins_found_t) == 32);

/* The profile at side by side columns from (origin_x, origin_y), step apart (a whole multiple of 4), x fastest: heights,
 * softness and dry each hold side * side values. */
[[maybe_unused]] static void basins_profile_map(const terrain_t *terrain, int32_t origin_x, int32_t origin_y,
                                                uint32_t side, int32_t step, float32_t *heights, float32_t *softness,
                                                uint8_t *dry) {
    float64_t xs[BASINS_ROW_MAX];
    float64_t ys[BASINS_ROW_MAX];
    for (uint32_t j = 0u; j < side; j++) {
        for (uint32_t i = 0u; i < side; i++) {
            xs[i] = (float64_t)((int64_t)origin_x + (int64_t)step * (int64_t)i);
            ys[i] = (float64_t)((int64_t)origin_y + (int64_t)step * (int64_t)j);
        }
        const uint64_t at = (uint64_t)j * side;
        const terrain_density_profile_arrays_t out = {
            .heights = heights + at, .softness = softness + at, .dry = dry + at};
        terrain_density_profile_points_arrays(terrain->borrowed_density, xs, ys, side, &out);
    }
}

/* The basins of a map basins_profile_map filled: its dry columns joined side to side. Writes up to most of them into out, in
 * the order the map first meets them, and returns how many there are. labels and queue hold side * side values each. */
[[maybe_unused]] static uint32_t basins_join(int32_t origin_x, int32_t origin_y, uint32_t side, int32_t step,
                                             const float32_t *heights, const uint8_t *dry, uint32_t *labels,
                                             uint32_t *queue, basins_found_t *out, uint32_t most) {
    const uint32_t count = side * side;
    memset(labels, 0, (uint64_t)count * sizeof(uint32_t));
    uint32_t found = 0u;
    for (uint32_t start = 0u; start < count; start++) {
        if (dry[start] == 0u || labels[start] != 0u) {
            continue;
        }
        found++;
        basins_found_t basin = {
            .floor = heights[start], .west = INT32_MAX, .south = INT32_MAX, .east = INT32_MIN, .north = INT32_MIN};
        uint32_t deepest = start;
        uint32_t head = 0u;
        uint32_t tail = 0u;
        labels[start] = found;
        queue[tail++] = start;
        while (head < tail) {
            const uint32_t at = queue[head++];
            const uint32_t i = at % side;
            const uint32_t j = at / side;
            basin.cells++;
            if (heights[at] < heights[deepest]) {
                deepest = at;
            }
            const int32_t x = origin_x + step * (int32_t)i;
            const int32_t y = origin_y + step * (int32_t)j;
            basin.west = x < basin.west ? x : basin.west;
            basin.east = x > basin.east ? x : basin.east;
            basin.south = y < basin.south ? y : basin.south;
            basin.north = y > basin.north ? y : basin.north;
            const uint32_t next[4] = {i > 0u ? at - 1u : UINT32_MAX, i + 1u < side ? at + 1u : UINT32_MAX,
                                      j > 0u ? at - side : UINT32_MAX, j + 1u < side ? at + side : UINT32_MAX};
            for (uint32_t n = 0u; n < 4u; n++) {
                if (next[n] != UINT32_MAX && dry[next[n]] != 0u && labels[next[n]] == 0u) {
                    labels[next[n]] = found;
                    queue[tail++] = next[n];
                }
            }
        }
        basin.x = origin_x + step * (int32_t)(deepest % side);
        basin.y = origin_y + step * (int32_t)(deepest / side);
        basin.floor = heights[deepest];
        if (found <= most) {
            out[found - 1u] = basin;
        }
    }
    return found;
}

#endif
