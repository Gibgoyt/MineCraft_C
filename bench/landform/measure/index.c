/* bench_landform's measures. See README.md. */
#include "landform/measure/index.h"

#include "tables/blocks/index.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>

/* The measures' sizes, in blocks: plan 007 phase 01, "Background, explained". */
static const int32_t LF_RELIEF_HALF = 1024; /* local relief: a square of 2048 */
static const float32_t LF_MOUNTAIN_RELIEF = 150.0f32;
static const int32_t LF_PEAK_HALF = 256; /* a peak is the highest in a square of 512 */
static const int32_t LF_LOW_HALF = 1500; /* the lowest in a square of 3 km: a peak's relief, an edge's drop */
static const float32_t LF_PEAK_RELIEF = 150.0f32;
static const float64_t LF_RAY_REACH = 3000.0f64;
static const float64_t LF_RAY_FLAT = 64.0f64;
static const float32_t LF_PLATEAU_Z = 250.0f32;
static const float32_t LF_PLATEAU_RELIEF = 80.0f32;
static const int32_t LF_PLATEAU_HALF = 256; /* relief within a square of 512 */
static const float32_t LF_ESCARPMENT_DROP = 300.0f32;
static const float32_t LF_RANGE_Z = 500.0f32;
static const int32_t LF_KNOB_HALF = 64; /* a knob is the highest in a square of 128 */
static const int32_t LF_KNOB_FLOOD_HALF = 512;
static const float32_t LF_KNOB_LEAST = 15.0f32;
static const float32_t LF_KNOB_MOST = 150.0f32;
static const float32_t LF_KNOB_BASE_FROM[2] = {150.0f32, 250.0f32};
static const float64_t LF_PIECE_LEAST_KM2 = 0.1f64;
static const float64_t LF_PLATEAU_BIG_KM2 = 25.0f64;
static const float32_t LF_TOP_SHARE = 0.7f32; /* the top 30% of local relief */
const float64_t LF_CLASS_FROM[LF_CLASSES - 1u] = {0.05f64, 0.2f64, 0.5f64, 1.0f64, 2.0f64, 5.0f64};
const uint32_t LF_CLASS_COLOUR[LF_CLASSES] = {0xE8F0D8u, 0xA8D890u, 0xF0E070u, 0xF0A040u,
                                              0xE06030u, 0xB02020u, 0x500010u};
const char *const LF_CLASS_NAME[LF_CLASSES] = {"flat", "gentle", "moderate", "steep", "v.steep", "cliff", "sheer"};
const int32_t LF_REACH[5] = {150, 300, 500, 750, 1000};

const char *const LF_VALUE_NAME[LF_V_COUNT] = {"profile_flat",
                                               "profile_gentle",
                                               "profile_moderate",
                                               "profile_steep",
                                               "profile_very_steep",
                                               "profile_cliff",
                                               "profile_sheer",
                                               "map_flat",
                                               "map_gentle",
                                               "map_moderate",
                                               "map_steep",
                                               "map_very_steep",
                                               "map_cliff",
                                               "map_sheer",
                                               "blocks_flat",
                                               "blocks_gentle",
                                               "blocks_moderate",
                                               "blocks_steep",
                                               "blocks_very_steep",
                                               "blocks_cliff",
                                               "blocks_sheer",
                                               "profile_against_blocks_points",
                                               "steeper_than_1_2_in_top_30_percent",
                                               "decile_ratio",
                                               "decile_0_median",
                                               "decile_1_median",
                                               "decile_2_median",
                                               "decile_3_median",
                                               "decile_4_median",
                                               "decile_5_median",
                                               "decile_6_median",
                                               "decile_7_median",
                                               "decile_8_median",
                                               "decile_9_median",
                                               "peaks",
                                               "concavity_median",
                                               "w50_over_h_median",
                                               "plateau_pieces",
                                               "plateau_pieces_25_km2",
                                               "plateau_largest_km2",
                                               "plateau_largest_across_km",
                                               "escarpment_km",
                                               "range_pieces_500",
                                               "range_longest_km",
                                               "range_longest_km2",
                                               "range_longest_elongation",
                                               "knobs",
                                               "knobs_base_under_150",
                                               "knobs_base_150_to_250",
                                               "knobs_base_250_up",
                                               "sea_percent",
                                               "reach_150_percent",
                                               "reach_300_percent",
                                               "reach_500_percent",
                                               "reach_750_percent",
                                               "reach_1000_percent",
                                               "top",
                                               "seconds",
                                               "profile_us_a_lattice_column",
                                               "profile_share_of_box_percent",
                                               "tallest_place_columns_a_second",
                                               "plains_place_columns_a_second"};

const char *const LF_PLACE_NAME[LF_PLACES] = {"largest plateau piece",       "tallest peak",
                                              "longest range piece",         "steepest 1 km window",
                                              "a knob on a base >= 250",     "lowest dry land below 0",
                                              "a plains place, for the cost"};

typedef struct lf_pieces_t {
    uint32_t count;             /* 0: of at least LF_PIECE_LEAST_KM2 */
    uint32_t big;               /* 4: of at least LF_PLATEAU_BIG_KM2 */
    uint32_t largest_pixels;    /* 8 */
    uint32_t largest_across;    /* 12: the longer side of its box, in pixels */
    uint32_t largest_middle;    /* 16: its pixel nearest its centroid */
    uint32_t longest_pixels;    /* 20 */
    uint32_t longest_middle;    /* 24 */
    uint32_t longest_direction; /* 28 */
    float64_t longest_extent;   /* 32: pixels, along x, y or a diagonal, whichever is longest */
} lf_pieces_t;
static_assert(sizeof(lf_pieces_t) == 40);

/* ---- Small things -------------------------------------------------------------------------------------------------- */

uint32_t lf_class(float64_t g) {
    uint32_t c = 0u;
    while (c < LF_CLASSES - 1u && g >= LF_CLASS_FROM[c]) {
        c++;
    }
    return c;
}

float64_t lf_percent(uint64_t part, uint64_t whole) {
    return whole > 0u ? 100.0f64 * (float64_t)part / (float64_t)whole : 0.0f64;
}

float64_t lf_gradient(float64_t x_minus, float64_t x_plus, float64_t y_minus, float64_t y_plus, float64_t across) {
    const float64_t gx = (x_plus - x_minus) / across, gy = (y_plus - y_minus) / across;
    return sqrt(gx * gx + gy * gy);
}

static uint32_t lf_median_bin(float64_t g) {
    if (g < 1.0e-4f64) {
        return 0u;
    }
    const float64_t at = (log10(g) + 4.0f64) * 1000.0f64;
    return at >= (float64_t)(LF_MEDIAN_BINS - 2u) ? LF_MEDIAN_BINS - 1u : 1u + (uint32_t)at;
}

static float64_t lf_median_of_bins(const uint32_t *bins) {
    uint64_t total = 0u;
    for (uint32_t i = 0u; i < LF_MEDIAN_BINS; i++) {
        total += bins[i];
    }
    if (total == 0u) {
        return 0.0f64;
    }
    uint64_t below = 0u;
    for (uint32_t i = 0u; i < LF_MEDIAN_BINS; i++) {
        below += bins[i];
        if (2u * below >= total) {
            return i == 0u ? 0.0f64 : pow(10.0f64, ((float64_t)i - 0.5f64) / 1000.0f64 - 4.0f64);
        }
    }
    return 0.0f64;
}

/* The median of a few values, sorted in place. */
static float64_t lf_median(float32_t *values, uint32_t count) {
    for (uint32_t i = 1u; i < count; i++) {
        const float32_t v = values[i];
        uint32_t j = i;
        while (j > 0u && values[j - 1u] > v) {
            values[j] = values[j - 1u];
            j--;
        }
        values[j] = v;
    }
    if (count == 0u) {
        return 0.0f64;
    }
    return count % 2u == 1u ? (float64_t)values[count / 2u]
                            : 0.5f64 * ((float64_t)values[count / 2u - 1u] + (float64_t)values[count / 2u]);
}

/* ---- Real blocks, as bench_world_map reads them --------------------------------------------------------------------- */

static bool lf_is_growth(uint16_t block) {
    switch (block) {
    case BLOCK_ID_AIR:
    case BLOCK_ID_OAK_LOG:
    case BLOCK_ID_BIRCH_LOG:
    case BLOCK_ID_SPRUCE_LOG:
    case BLOCK_ID_JUNGLE_LOG:
    case BLOCK_ID_ACACIA_LOG:
    case BLOCK_ID_DARK_OAK_LOG:
    case BLOCK_ID_MANGROVE_LOG:
    case BLOCK_ID_CHERRY_LOG:
    case BLOCK_ID_CACTUS:
    case BLOCK_ID_MELON:
    case BLOCK_ID_PUMPKIN:
        return true;
    case BLOCK_ID_WATER:
    case BLOCK_ID_LAVA:
        return false;
    default:
        break;
    }
    return (block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u || (block_defs[block].flags & BLOCK_FLAG_DECAYS) != 0u;
}

static bool lf_is_ice(uint16_t block) {
    return block == BLOCK_ID_ICE || block == BLOCK_ID_PACKED_ICE || block == BLOCK_ID_BLUE_ICE;
}

/* The top solid block of a run of a column, with what grows looked through, and whether water lies on it. False if the run
 * holds none, or its very top is already ground and it may go on above. */
static bool lf_column_read(const uint16_t *blocks, int32_t low, uint32_t layers, float32_t *out_ground, bool *out_wet) {
    int32_t at = (int32_t)layers - 1;
    while (at >= 0 && lf_is_growth(blocks[at])) {
        at--;
    }
    if (at < 0 || at == (int32_t)layers - 1) {
        return false;
    }
    bool wet = false;
    for (int32_t below = at; below >= 0 && (blocks[below] == BLOCK_ID_WATER || lf_is_ice(blocks[below])); below--) {
        wet = wet || blocks[below] == BLOCK_ID_WATER;
    }
    *out_wet = wet || (blocks[at] == BLOCK_ID_ICE && low + at == MC_WORLD_Z_SEA);
    *out_ground = (float32_t)(low + at);
    return true;
}

/* Every stage that decides a column's ground, as bench_world_map runs them: no features and no ore. */
static void lf_real_column(const terrain_t *terrain, const terrain_hydrology_column_t *field, int32_t x, int32_t y,
                           uint16_t *blocks, uint8_t *opened, float32_t *out_ground, uint8_t *out_land) {
    const float32_t lowest = field->ground < field->height ? field->ground : field->height;
    const float32_t highest = field->height > 0.0f32 ? field->height : 0.0f32;
    int32_t low = (int32_t)floorf(lowest - field->softness) - (int32_t)LF_BAND;
    int32_t high = (int32_t)ceilf(highest + field->softness) + (int32_t)LF_BAND;
    low = low < COLUMN_Z_STORAGE_MIN ? COLUMN_Z_STORAGE_MIN : low;
    high = high > COLUMN_Z_STORAGE_END - 1 ? COLUMN_Z_STORAGE_END - 1 : high;
    for (;;) {
        const terrain_density_box_t box = {.origin_x = x,
                                           .origin_y = y,
                                           .origin_z = low,
                                           .count_x = 1u,
                                           .count_y = 1u,
                                           .count_z = (uint32_t)(high - low + 1)};
        terrain_hydrology_fill_box(terrain->borrowed_hydrology, &box, blocks);
        terrain_surface_fill_box(terrain->borrowed_surface, nullptr, &box, blocks);
        terrain_cave_mask_box(terrain->borrowed_cave, &box, opened);
        terrain_cave_cut_with_mask(&box, opened, blocks);
        terrain_hydrology_flood_box(terrain->borrowed_hydrology, &box, opened, blocks);
        terrain_surface_line_caves_box(terrain->borrowed_surface, nullptr, &box, blocks);
        bool wet = false;
        if (lf_column_read(blocks, low, box.count_z, out_ground, &wet)) {
            *out_land = (uint8_t)(wet ? 0u : 1u);
            return;
        }
        if (low == COLUMN_Z_STORAGE_MIN && high == COLUMN_Z_STORAGE_END - 1) {
            *out_ground = (float32_t)COLUMN_Z_STORAGE_MIN;
            *out_land = 0u;
            return;
        }
        low -= (int32_t)LF_BAND_WIDEN;
        low = low < COLUMN_Z_STORAGE_MIN ? COLUMN_Z_STORAGE_MIN : low;
        high = COLUMN_Z_STORAGE_END - 1;
    }
}

/* ---- The jobs ------------------------------------------------------------------------------------------------------ */

/* One row of the map from the profile, and its gradient over 8 blocks: the row itself every 4 blocks, and the rows 4 north
 * and 4 south at the map's step. */
static void lf_sweep_row(const lf_run_t *run, float32_t *scratch, uint32_t row) {
    const lf_grid_t *const grid = run->borrowed_grid;
    const uint32_t per = grid->step / 4u;
    const uint32_t along_count = (grid->side - 1u) * per + 3u;
    float32_t *const along = scratch;
    float32_t *const north = along + along_count;
    float32_t *const south = north + grid->side;
    const int32_t y = grid->origin_y + (int32_t)(row * grid->step);
    terrain_height_estimate_area(run->borrowed_terrain, grid->origin_x - 4, y, 4, along_count, 1u, along);
    terrain_height_estimate_area(run->borrowed_terrain, grid->origin_x, y - 4, (int32_t)grid->step, grid->side, 1u,
                                 north);
    terrain_height_estimate_area(run->borrowed_terrain, grid->origin_x, y + 4, (int32_t)grid->step, grid->side, 1u,
                                 south);
    for (uint32_t i = 0u; i < grid->side; i++) {
        const uint32_t k = i * per + 1u;
        const uint64_t at = (uint64_t)row * grid->side + i;
        grid->h[at] = along[k];
        grid->g[at] = (float32_t)lf_gradient((float64_t)along[k - 1u], (float64_t)along[k + 1u], (float64_t)north[i],
                                             (float64_t)south[i], 8.0f64);
    }
}

/* The highest (or, with is_max false, lowest) value in a window half to each side, for each of n values stride apart: van
 * Herk's two sweeps over blocks of the window's width, so the cost does not grow with the window. scratch holds 3 (n + 2 half). */
static void lf_slide_line(const float32_t *src, uint64_t stride, uint32_t n, uint32_t half, bool is_max, float32_t *dst,
                          float32_t *scratch) {
    const uint32_t k = 2u * half + 1u, m = n + 2u * half;
    float32_t *const p = scratch, *const pre = scratch + m, *const suf = scratch + 2u * m;
    const float32_t sign = is_max ? 1.0f32 : -1.0f32;
    for (uint32_t i = 0u; i < m; i++) {
        p[i] = i >= half && i - half < n ? sign * src[(uint64_t)(i - half) * stride] : -LF_HUGE;
    }
    for (uint32_t i = 0u; i < m; i++) {
        pre[i] = i % k == 0u ? p[i] : fmaxf(pre[i - 1u], p[i]);
    }
    for (uint32_t i = m; i-- > 0u;) {
        suf[i] = i % k == k - 1u || i == m - 1u ? p[i] : fmaxf(suf[i + 1u], p[i]);
    }
    for (uint32_t i = 0u; i < n; i++) {
        dst[(uint64_t)i * stride] = sign * fmaxf(suf[i], pre[i + 2u * half]);
    }
}

/* The highest saddle from the column at to ground higher than it, within LF_KNOB_FLOOD_HALF: a flood that always takes
 * the highest ground it has reached next, so the lowest it has taken when it first meets higher ground is the saddle. With
 * no higher ground in reach, the lowest it reached. It stops once that is more than LF_KNOB_MOST down, which no knob is. */
static float32_t lf_knob_saddle(const lf_grid_t *grid, uint32_t at, uint32_t reach, uint8_t *seen, uint32_t *heap) {
    const uint32_t side = grid->side, across = 2u * reach + 1u;
    const float32_t *const h = grid->h;
    const int32_t cx = (int32_t)(at % side), cy = (int32_t)(at / side);
    const float32_t top = h[at];
    memset(seen, 0, (uint64_t)across * across);
    uint32_t count = 0u;
    heap[count++] = at;
    seen[reach + across * reach] = 1u;
    float32_t low = top;
    static const int32_t DX[4] = {1, -1, 0, 0};
    static const int32_t DY[4] = {0, 0, 1, -1};
    while (count > 0u) {
        const uint32_t q = heap[0];
        heap[0] = heap[--count];
        for (uint32_t i = 0u;;) {
            const uint32_t l = 2u * i + 1u, r = l + 1u;
            uint32_t best = i;
            best = l < count && h[heap[l]] > h[heap[best]] ? l : best;
            best = r < count && h[heap[r]] > h[heap[best]] ? r : best;
            if (best == i) {
                break;
            }
            const uint32_t swap = heap[i];
            heap[i] = heap[best];
            heap[best] = swap;
            i = best;
        }
        if (h[q] > top) {
            return low;
        }
        low = h[q] < low ? h[q] : low;
        if (top - low > LF_KNOB_MOST) {
            return low;
        }
        for (uint32_t d = 0u; d < 4u; d++) {
            const int32_t nx = (int32_t)(q % side) + DX[d], ny = (int32_t)(q / side) + DY[d];
            if (nx < 0 || ny < 0 || nx >= (int32_t)side || ny >= (int32_t)side || abs(nx - cx) > (int32_t)reach ||
                abs(ny - cy) > (int32_t)reach) {
                continue;
            }
            const uint32_t local = (uint32_t)(nx - cx + (int32_t)reach) + across * (uint32_t)(ny - cy + (int32_t)reach);
            if (seen[local] != 0u) {
                continue;
            }
            seen[local] = 1u;
            uint32_t i = count++;
            heap[i] = (uint32_t)nx + side * (uint32_t)ny;
            while (i > 0u && h[heap[(i - 1u) / 2u]] < h[heap[i]]) {
                const uint32_t up = (i - 1u) / 2u, swap = heap[up];
                heap[up] = heap[i];
                heap[i] = swap;
                i = up;
            }
        }
    }
    return low;
}

/* The knobs on one row. borrowed_src holds the highest within LF_KNOB_HALF, borrowed_dst the lowest. */
static void lf_knob_row(const lf_run_t *run, uint8_t *scratch, uint32_t row) {
    const lf_grid_t *const grid = run->borrowed_grid;
    const uint32_t side = grid->side, reach = (uint32_t)LF_KNOB_FLOOD_HALF / grid->step;
    const uint32_t across = 2u * reach + 1u;
    uint32_t *const heap = (uint32_t *)(void *)scratch;
    uint8_t *const seen = scratch + (uint64_t)across * across * 4u;
    for (uint32_t i = 0u; i < side; i++) {
        const uint32_t at = row * side + i;
        const float32_t z = grid->h[at];
        run->borrowed_knobs[at] = 0u;
        if (z <= 0.0f32 || z != run->borrowed_src[at] || z - run->borrowed_dst[at] < LF_KNOB_LEAST) {
            continue;
        }
        /* Strictly above its neighbours: a flat top has no one highest column. */
        if ((i > 0u && grid->h[at - 1u] >= z) || (i + 1u < side && grid->h[at + 1u] >= z) ||
            (row > 0u && grid->h[at - side] >= z) || (row + 1u < side && grid->h[at + side] >= z)) {
            continue;
        }
        const float32_t saddle = lf_knob_saddle(grid, at, reach, seen, heap);
        const float32_t prominence = z - saddle;
        if (prominence >= LF_KNOB_LEAST && prominence <= LF_KNOB_MOST) {
            run->borrowed_knobs[at] =
                (uint8_t)(1u + (saddle >= LF_KNOB_BASE_FROM[0] ? 1u : 0u) + (saddle >= LF_KNOB_BASE_FROM[1] ? 1u : 0u));
        }
    }
}

static void lf_window_row(const lf_run_t *run, uint8_t *scratch, uint32_t item, bool real) {
    const uint32_t window = item / LF_WINDOW_GRID, row = item % LF_WINDOW_GRID;
    const int32_t x = run->borrowed_window_origins[2u * window] - (int32_t)LF_WINDOW_STEP;
    const int32_t y =
        run->borrowed_window_origins[2u * window + 1u] - (int32_t)LF_WINDOW_STEP + (int32_t)(row * LF_WINDOW_STEP);
    const uint64_t at = ((uint64_t)window * LF_WINDOW_GRID + row) * LF_WINDOW_GRID;
    if (!real) {
        terrain_height_estimate_area(run->borrowed_terrain, x, y, (int32_t)LF_WINDOW_STEP, LF_WINDOW_GRID, 1u,
                                     run->borrowed_windows_profile + at);
        return;
    }
    terrain_hydrology_column_t *const fields = (terrain_hydrology_column_t *)(void *)scratch;
    uint16_t *const blocks = (uint16_t *)(void *)(scratch + 65536u);
    uint8_t *const opened = scratch + 65536u + (uint64_t)LF_LAYERS * 2u;
    const terrain_fields_area_t area = {.origin_x = x,
                                        .origin_y = y,
                                        .step_x = (int32_t)LF_WINDOW_STEP,
                                        .step_y = 1,
                                        .count_x = LF_WINDOW_GRID,
                                        .count_y = 1u};
    terrain_hydrology_columns_area(run->borrowed_terrain->borrowed_hydrology, &area, fields);
    for (uint32_t i = 0u; i < LF_WINDOW_GRID; i++) {
        lf_real_column(run->borrowed_terrain, &fields[i], x + (int32_t)(i * LF_WINDOW_STEP), y, blocks, opened,
                       &run->borrowed_windows_real[at + i], &run->borrowed_windows_land[at + i]);
    }
}

static void lf_cost_box(const lf_run_t *run, uint8_t *scratch, uint32_t item) {
    const terrain_density_box_t box = {.origin_x = run->cost_x + (int32_t)((item % LF_COST_SIDE) * LF_CHUNK),
                                       .origin_y = run->cost_y + (int32_t)((item / LF_COST_SIDE) * LF_CHUNK),
                                       .origin_z = COLUMN_Z_STORAGE_MIN,
                                       .count_x = LF_CHUNK,
                                       .count_y = LF_CHUNK,
                                       .count_z = LF_LAYERS};
    uint16_t *const blocks = (uint16_t *)(void *)scratch;
    uint8_t *const opened = scratch + (uint64_t)LF_CHUNK * LF_CHUNK * LF_LAYERS * 2u;
    terrain_generate_box_fallback(run->borrowed_terrain, &box, opened, blocks);
}

static void lf_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    lf_run_t *const run = borrowed_argument;
    const uint32_t me = atomic_fetch_add_explicit(&run->next_worker, 1u, memory_order_relaxed);
    uint8_t *const scratch = run->borrowed_scratch + (uint64_t)me * LF_SCRATCH_BYTES;
    float32_t *const floats = (float32_t *)(void *)scratch;
    const uint32_t side = run->borrowed_grid != nullptr ? run->borrowed_grid->side : 0u;
    for (;;) {
        const uint32_t item = atomic_fetch_add_explicit(&run->next, 1u, memory_order_relaxed);
        if (item >= run->items) {
            break;
        }
        switch (run->stage) {
        case LF_STAGE_SWEEP:
            lf_sweep_row(run, floats, item);
            break;
        case LF_STAGE_SLIDE_ROWS:
            lf_slide_line(run->borrowed_src + (uint64_t)item * side, 1u, side, run->half, run->is_max != 0u,
                          run->borrowed_dst + (uint64_t)item * side, floats);
            break;
        case LF_STAGE_SLIDE_COLUMNS:
            lf_slide_line(run->borrowed_src + item, side, side, run->half, run->is_max != 0u, run->borrowed_dst + item,
                          floats);
            break;
        case LF_STAGE_KNOBS:
            lf_knob_row(run, scratch, item);
            break;
        case LF_STAGE_WINDOWS_PROFILE:
            lf_window_row(run, scratch, item, false);
            break;
        case LF_STAGE_WINDOWS_REAL:
            lf_window_row(run, scratch, item, true);
            break;
        case LF_STAGE_STRIP:
            terrain_height_estimate_area(run->borrowed_terrain, -(int32_t)(LF_STRIP_WIDE * LF_STRIP_STEP / 2u),
                                         (int32_t)(item * LF_STRIP_STEP), (int32_t)LF_STRIP_STEP, LF_STRIP_WIDE, 1u,
                                         run->borrowed_strip + (uint64_t)item * LF_STRIP_WIDE);
            break;
        case LF_STAGE_COST:
            lf_cost_box(run, scratch, item);
            break;
        }
    }
}

void lf_jobs(job_system_t *system, lf_run_t *run, lf_stage_t stage, uint32_t items) {
    run->stage = stage;
    run->items = items;
    atomic_store_explicit(&run->next, 0u, memory_order_relaxed);
    atomic_store_explicit(&run->next_worker, 0u, memory_order_relaxed);
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < run->jobs; i++) {
        job_submit(system, &group, lf_job, run);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
}

/* The highest or lowest within a square of 2 half + 1 blocks round each column, src to dst through tmp. dst may be src. */
static void lf_slide(job_system_t *system, lf_run_t *run, const float32_t *src, float32_t *tmp, float32_t *dst,
                     int32_t half, bool is_max) {
    run->half = (uint32_t)half / run->borrowed_grid->step;
    run->is_max = is_max ? 1u : 0u;
    run->borrowed_src = src;
    run->borrowed_dst = tmp;
    lf_jobs(system, run, LF_STAGE_SLIDE_ROWS, run->borrowed_grid->side);
    run->borrowed_src = tmp;
    run->borrowed_dst = dst;
    lf_jobs(system, run, LF_STAGE_SLIDE_COLUMNS, run->borrowed_grid->side);
}

/* ---- Measures ------------------------------------------------------------------------------------------------------ */

/* The height at a point in blocks from the grid's first column, between its four columns. False off the grid. */
static bool lf_sample(const lf_grid_t *grid, float64_t x, float64_t y, float64_t *out) {
    const float64_t u = x / (float64_t)grid->step, v = y / (float64_t)grid->step;
    const float64_t last = (float64_t)(grid->side - 1u);
    if (u < 0.0f64 || v < 0.0f64 || u > last || v > last) {
        return false;
    }
    uint32_t i = (uint32_t)u, j = (uint32_t)v;
    i = i >= grid->side - 1u ? grid->side - 2u : i;
    j = j >= grid->side - 1u ? grid->side - 2u : j;
    const float64_t fu = u - (float64_t)i, fv = v - (float64_t)j;
    const float32_t *const h = grid->h + (uint64_t)j * grid->side + i;
    const float64_t top = (float64_t)h[0] + ((float64_t)h[1] - (float64_t)h[0]) * fu;
    const float64_t bottom = (float64_t)h[grid->side] + ((float64_t)h[grid->side + 1u] - (float64_t)h[grid->side]) * fu;
    *out = top + (bottom - top) * fv;
    return true;
}

/* A ray from a peak, out until the ground first stops falling for LF_RAY_FLAT, or LF_RAY_REACH: where it has fallen half
 * its drop (between samples), where it is lowest, and the drop. False for a ray that never falls. */
static bool lf_ray(const lf_grid_t *grid, float64_t px, float64_t py, float64_t top, float64_t ax, float64_t ay,
                   float32_t *ts, float32_t *hs, float64_t *out_r50, float64_t *out_r100, float64_t *out_drop) {
    const float64_t step = (float64_t)grid->step;
    uint32_t count = 0u;
    float64_t low = top, low_t = 0.0f64;
    for (float64_t t = step; t <= LF_RAY_REACH && count < LF_RAY_SAMPLES; t += step) {
        float64_t z = 0.0f64;
        if (!lf_sample(grid, px + t * ax, py + t * ay, &z)) {
            break;
        }
        ts[count] = (float32_t)t;
        hs[count] = (float32_t)z;
        count++;
        if (z < low) {
            low = z;
            low_t = t;
        } else if (t - low_t >= LF_RAY_FLAT) {
            break;
        }
    }
    const float64_t drop = top - low;
    if (drop <= 0.0f64) {
        return false;
    }
    const float64_t target = top - 0.5f64 * drop;
    float64_t t_before = 0.0f64, z_before = top;
    for (uint32_t k = 0u; k < count; k++) {
        if ((float64_t)hs[k] <= target) {
            *out_r50 = t_before + ((float64_t)ts[k] - t_before) * (z_before - target) / (z_before - (float64_t)hs[k]);
            break;
        }
        t_before = (float64_t)ts[k];
        z_before = (float64_t)hs[k];
    }
    *out_r100 = low_t;
    *out_drop = drop;
    return true;
}

/* Peaks: highest in their square, at least LF_PEAK_RELIEF over the lowest in theirs, the highest first, and one of any
 * that tie in one square. highest holds the highest in LF_PEAK_HALF. */
static void lf_peaks(const lf_grid_t *grid, const float32_t *highest, const lf_memory_t *m, lf_result_t *r) {
    const uint64_t n = (uint64_t)grid->side * grid->side;
    uint32_t count = 0u;
    for (uint64_t i = 0u; i < n && count < LF_PEAKS_MAX; i++) {
        const float32_t z = grid->h[i];
        if (z <= 0.0f32 || z != highest[i] || z - m->low[i] < LF_PEAK_RELIEF) {
            continue;
        }
        /* Strictly above its neighbours: a flat top, or the rim of one, is not a peak. */
        const uint64_t x = i % grid->side, side = grid->side;
        if ((x > 0u && grid->h[i - 1u] >= z) || (x + 1u < side && grid->h[i + 1u] >= z) ||
            (i >= side && grid->h[i - side] >= z) || (i + side < n && grid->h[i + side] >= z)) {
            continue;
        }
        uint32_t j = count++;
        while (j > 0u && m->peaks[j - 1u].h < z) {
            m->peaks[j] = m->peaks[j - 1u];
            j--;
        }
        m->peaks[j] = (lf_peak_t){.at = (uint32_t)i, .h = z};
    }
    const int32_t near = LF_PEAK_HALF / (int32_t)grid->step;
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        const int32_t x = (int32_t)(m->peaks[i].at % grid->side), y = (int32_t)(m->peaks[i].at / grid->side);
        bool alone = true;
        for (uint32_t k = 0u; k < kept && alone; k++) {
            const int32_t kx = (int32_t)(m->peaks[k].at % grid->side), ky = (int32_t)(m->peaks[k].at / grid->side);
            alone = abs(kx - x) > near || abs(ky - y) > near;
        }
        if (alone) {
            m->peaks[kept++] = m->peaks[i];
        }
    }
    float32_t *const ts = m->ray, *const hs = m->ray + LF_RAY_SAMPLES;
    float32_t *const concavity = m->ray + 2u * LF_RAY_SAMPLES, *const width = concavity + LF_PEAKS_MAX;
    uint32_t measured = 0u;
    for (uint32_t i = 0u; i < kept; i++) {
        const float64_t px = (float64_t)((m->peaks[i].at % grid->side) * grid->step);
        const float64_t py = (float64_t)((m->peaks[i].at / grid->side) * grid->step);
        float32_t r50s[LF_RAYS], r100s[LF_RAYS], drops[LF_RAYS];
        uint32_t rays = 0u;
        for (uint32_t k = 0u; k < LF_RAYS; k++) {
            const float64_t angle = 6.283185307179586f64 * (float64_t)k / (float64_t)LF_RAYS;
            float64_t r50 = 0.0f64, r100 = 0.0f64, drop = 0.0f64;
            if (lf_ray(grid, px, py, (float64_t)m->peaks[i].h, cos(angle), sin(angle), ts, hs, &r50, &r100, &drop)) {
                r50s[rays] = (float32_t)r50;
                r100s[rays] = (float32_t)r100;
                drops[rays] = (float32_t)drop;
                rays++;
            }
        }
        if (rays < LF_RAYS / 2u) {
            continue;
        }
        const float64_t r50 = lf_median(r50s, rays), r100 = lf_median(r100s, rays), drop = lf_median(drops, rays);
        if (r50 <= 0.0f64) {
            continue;
        }
        concavity[measured] = (float32_t)((r100 - r50) / r50);
        width[measured] = (float32_t)(2.0f64 * r50 / drop);
        measured++;
    }
    r->v[LF_V_PEAKS] = (float64_t)kept;
    r->v[LF_V_CONCAVITY] = lf_median(concavity, measured);
    r->v[LF_V_WIDTH] = lf_median(width, measured);
    if (kept > 0u) {
        r->place[LF_PLACE_PEAK] =
            (lf_place_t){.x = grid->origin_x + (int32_t)((m->peaks[0].at % grid->side) * grid->step),
                         .y = grid->origin_y + (int32_t)((m->peaks[0].at / grid->side) * grid->step),
                         .z = m->peaks[0].h,
                         .found = 1u};
    }
}

/* Connected pieces where mask is 1, by faces. Leaves mask as it was. */
static void lf_pieces(uint8_t *mask, uint32_t side, uint32_t step, uint32_t *queue, lf_pieces_t *out) {
    memset(out, 0, sizeof(*out));
    const uint64_t n = (uint64_t)side * side;
    const float64_t pixel_km2 = (float64_t)step * (float64_t)step / 1.0e6f64;
    for (uint64_t start = 0u; start < n; start++) {
        if (mask[start] != 1u) {
            continue;
        }
        uint32_t head = 0u, tail = 0u;
        queue[tail++] = (uint32_t)start;
        mask[start] |= 0x80u;
        int64_t left = INT64_MAX, right = INT64_MIN, up = INT64_MAX, down = INT64_MIN;
        int64_t s_low = INT64_MAX, s_high = INT64_MIN, d_low = INT64_MAX, d_high = INT64_MIN;
        float64_t sum_x = 0.0f64, sum_y = 0.0f64;
        while (head < tail) {
            const uint32_t at = queue[head++];
            const int64_t x = (int64_t)(at % side), y = (int64_t)(at / side);
            left = x < left ? x : left;
            right = x > right ? x : right;
            up = y < up ? y : up;
            down = y > down ? y : down;
            s_low = x + y < s_low ? x + y : s_low;
            s_high = x + y > s_high ? x + y : s_high;
            d_low = x - y < d_low ? x - y : d_low;
            d_high = x - y > d_high ? x - y : d_high;
            sum_x += (float64_t)x;
            sum_y += (float64_t)y;
            const uint32_t next_to[4] = {x > 0 ? at - 1u : at, x + 1 < (int64_t)side ? at + 1u : at,
                                         y > 0 ? at - side : at, y + 1 < (int64_t)side ? at + side : at};
            for (uint32_t k = 0u; k < 4u; k++) {
                if (mask[next_to[k]] == 1u) {
                    mask[next_to[k]] |= 0x80u;
                    queue[tail++] = next_to[k];
                }
            }
        }
        const float64_t km2 = (float64_t)tail * pixel_km2;
        if (km2 < LF_PIECE_LEAST_KM2) {
            continue;
        }
        out->count++;
        out->big += km2 >= LF_PLATEAU_BIG_KM2 ? 1u : 0u;
        const float64_t extents[4] = {(float64_t)(right - left + 1), (float64_t)(down - up + 1),
                                      (float64_t)(s_high - s_low) / 1.41421356f64 + 1.0f64,
                                      (float64_t)(d_high - d_low) / 1.41421356f64 + 1.0f64};
        uint32_t direction = 0u;
        for (uint32_t k = 1u; k < 4u; k++) {
            direction = extents[k] > extents[direction] ? k : direction;
        }
        const bool largest = tail > out->largest_pixels;
        const bool longest = extents[direction] > out->longest_extent;
        if (!largest && !longest) {
            continue;
        }
        const float64_t mx = sum_x / (float64_t)tail, my = sum_y / (float64_t)tail;
        uint32_t middle = queue[0];
        float64_t best = 1.0e30f64;
        for (uint32_t k = 0u; k < tail; k++) {
            const float64_t dx = (float64_t)(queue[k] % side) - mx, dy = (float64_t)(queue[k] / side) - my;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                middle = queue[k];
            }
        }
        if (largest) {
            out->largest_pixels = tail;
            out->largest_across = (uint32_t)(extents[0] > extents[1] ? extents[0] : extents[1]);
            out->largest_middle = middle;
        }
        if (longest) {
            out->longest_pixels = tail;
            out->longest_extent = extents[direction];
            out->longest_direction = direction;
            out->longest_middle = middle;
        }
    }
    for (uint64_t i = 0u; i < n; i++) {
        mask[i] &= 0x7Fu;
    }
}

lf_place_t lf_place_at(const lf_grid_t *grid, uint32_t at) {
    return (lf_place_t){.x = grid->origin_x + (int32_t)((at % grid->side) * grid->step),
                        .y = grid->origin_y + (int32_t)((at / grid->side) * grid->step),
                        .z = grid->h[at],
                        .found = 1u};
}

/* Everything the table holds that the grid alone gives. */
void lf_measure(job_system_t *system, lf_run_t *run, const lf_memory_t *m, lf_result_t *r) {
    const lf_grid_t *const grid = run->borrowed_grid;
    const uint32_t side = grid->side, step = grid->step;
    const uint64_t n = (uint64_t)side * side;
    const float32_t *const h = grid->h, *const g = grid->g;
    float64_t *const v = r->v;

    /* The sea, the bands and the slope over the whole map. */
    uint64_t land = 0u, reach[5] = {0u}, classes[LF_CLASSES] = {0u};
    uint32_t top_at = 0u;
    for (uint64_t i = 0u; i < n; i++) {
        if (h[i] > h[top_at]) {
            top_at = (uint32_t)i;
        }
        if (h[i] <= 0.0f32) {
            continue;
        }
        land++;
        classes[lf_class((float64_t)g[i])]++;
        for (uint32_t k = 0u; k < 5u; k++) {
            reach[k] += h[i] >= (float32_t)LF_REACH[k] ? 1u : 0u;
        }
    }
    v[LF_V_SEA] = lf_percent(n - land, n);
    for (uint32_t k = 0u; k < 5u; k++) {
        v[LF_V_REACH + k] = lf_percent(reach[k], n);
    }
    for (uint32_t c = 0u; c < LF_CLASSES; c++) {
        v[LF_V_MAP + c] = lf_percent(classes[c], land);
    }
    v[LF_V_TOP] = (float64_t)h[top_at];
    r->place[LF_PLACE_PEAK] = lf_place_at(grid, top_at);

    /* Relative height in a square of 2048, and slope by its deciles where the relief is a mountain's. */
    lf_slide(system, run, h, m->c, m->a, LF_RELIEF_HALF, true);
    lf_slide(system, run, h, m->c, m->b, LF_RELIEF_HALF, false);
    memset(m->median, 0, (uint64_t)(LF_DECILES + 1u) * LF_MEDIAN_BINS * sizeof(uint32_t));
    uint64_t steep = 0u, steep_top = 0u;
    float32_t plains_relief = LF_HUGE;
    for (uint64_t i = 0u; i < n; i++) {
        if (h[i] <= 0.0f32) {
            continue;
        }
        const float32_t relief = m->a[i] - m->b[i];
        const float32_t share = relief > 0.0f32 ? (h[i] - m->b[i]) / relief : 0.0f32;
        if ((float64_t)g[i] > LF_CLASS_FROM[4]) {
            steep++;
            steep_top += share >= LF_TOP_SHARE ? 1u : 0u;
        }
        if (relief >= LF_MOUNTAIN_RELIEF) {
            uint32_t d = (uint32_t)(share * (float32_t)LF_DECILES);
            d = d >= LF_DECILES ? LF_DECILES - 1u : d;
            m->median[d * LF_MEDIAN_BINS + lf_median_bin((float64_t)g[i])]++;
        }
        if (h[i] >= 10.0f32 && h[i] <= 80.0f32 && relief < plains_relief) {
            plains_relief = relief;
            r->place[LF_PLACE_PLAINS] = lf_place_at(grid, (uint32_t)i);
        }
    }
    uint32_t *const pooled = m->median + LF_DECILES * LF_MEDIAN_BINS;
    for (uint32_t d = 0u; d < LF_DECILES; d++) {
        v[LF_V_DECILE + d] = lf_median_of_bins(m->median + d * LF_MEDIAN_BINS);
        for (uint32_t b = 0u; d < 3u && b < LF_MEDIAN_BINS; b++) {
            pooled[b] += m->median[d * LF_MEDIAN_BINS + b];
        }
    }
    const float64_t bottom = lf_median_of_bins(pooled);
    v[LF_V_DECILE_RATIO] = bottom > 0.0f64 ? v[LF_V_DECILE + LF_DECILES - 1u] / bottom
                                           : (v[LF_V_DECILE + LF_DECILES - 1u] > 0.0f64 ? 999.0f64 : 0.0f64);
    v[LF_V_STEEP_TOP] = lf_percent(steep_top, steep);

    /* Peaks. a is kept, the highest in a square of 512, for the plateaus. */
    lf_slide(system, run, h, m->c, m->a, LF_PEAK_HALF, true);
    lf_slide(system, run, h, m->c, m->low, LF_LOW_HALF, false);
    lf_peaks(grid, m->a, m, r);

    /* Plateaus: at LF_PLATEAU_Z or over, and in some square of 512 whose relief is at most LF_PLATEAU_RELIEF. */
    lf_slide(system, run, h, m->c, m->b, LF_PLATEAU_HALF, false);
    for (uint64_t i = 0u; i < n; i++) {
        m->b[i] = m->a[i] - m->b[i] <= LF_PLATEAU_RELIEF ? 1.0f32 : 0.0f32;
    }
    lf_slide(system, run, m->b, m->c, m->b, LF_PLATEAU_HALF, true);
    for (uint64_t i = 0u; i < n; i++) {
        m->plateau[i] = m->b[i] > 0.5f32 && h[i] >= LF_PLATEAU_Z ? 1u : 0u;
    }
    lf_pieces_t pieces;
    lf_pieces(m->plateau, side, step, m->queue, &pieces);
    const float64_t pixel_km2 = (float64_t)step * (float64_t)step / 1.0e6f64;
    v[LF_V_PLATEAUS] = (float64_t)pieces.count;
    v[LF_V_PLATEAUS_BIG] = (float64_t)pieces.big;
    v[LF_V_PLATEAU_KM2] = (float64_t)pieces.largest_pixels * pixel_km2;
    v[LF_V_PLATEAU_ACROSS] = (float64_t)(pieces.largest_across * step) / 1000.0f64;
    if (pieces.count > 0u) {
        r->place[LF_PLACE_PLATEAU] = lf_place_at(grid, pieces.largest_middle);
    }
    /* Its edge, where it falls LF_ESCARPMENT_DROP within the square of 3 km: a pixel's width of it each. */
    uint64_t edge = 0u;
    float64_t edge_best = 1.0e30f64;
    r->plateau_edge = UINT32_MAX;
    for (uint64_t i = 0u; i < n; i++) {
        const uint32_t x = (uint32_t)(i % side), y = (uint32_t)(i / side);
        if (m->plateau[i] == 0u || x == 0u || y == 0u || x + 1u == side || y + 1u == side) {
            continue;
        }
        const bool rim = m->plateau[i - 1u] == 0u || m->plateau[i + 1u] == 0u || m->plateau[i - side] == 0u ||
                         m->plateau[i + side] == 0u;
        if (rim && h[i] - m->low[i] >= LF_ESCARPMENT_DROP) {
            m->plateau[i] = 2u;
            edge++;
            const float64_t dx = (float64_t)x - (float64_t)(pieces.largest_middle % side);
            const float64_t dy = (float64_t)y - (float64_t)(pieces.largest_middle / side);
            if (dx * dx + dy * dy < edge_best) {
                edge_best = dx * dx + dy * dy;
                r->plateau_edge = (uint32_t)i;
            }
        }
    }
    v[LF_V_ESCARPMENT] = (float64_t)(edge * step) / 1000.0f64;

    /* Ranges: connected land at LF_RANGE_Z or over. */
    for (uint64_t i = 0u; i < n; i++) {
        m->range[i] = h[i] >= LF_RANGE_Z ? 1u : 0u;
    }
    lf_pieces(m->range, side, step, m->queue, &pieces);
    v[LF_V_RANGES] = (float64_t)pieces.count;
    v[LF_V_RANGE_KM] = pieces.longest_extent * (float64_t)step / 1000.0f64;
    v[LF_V_RANGE_KM2] = (float64_t)pieces.longest_pixels * pixel_km2;
    v[LF_V_RANGE_ELONGATION] =
        pieces.longest_pixels > 0u ? pieces.longest_extent / sqrt((float64_t)pieces.longest_pixels) : 0.0f64;
    r->range_direction = pieces.longest_direction;
    if (pieces.count > 0u) {
        r->place[LF_PLACE_RANGE] = lf_place_at(grid, pieces.longest_middle);
    }

    /* Knobs, by the band of their base. */
    lf_slide(system, run, h, m->c, m->a, LF_KNOB_HALF, true);
    lf_slide(system, run, h, m->c, m->b, LF_KNOB_HALF, false);
    run->borrowed_src = m->a;
    run->borrowed_dst = m->b;
    run->borrowed_knobs = m->knobs;
    lf_jobs(system, run, LF_STAGE_KNOBS, side);
    uint64_t bands[3] = {0u};
    for (uint64_t i = 0u; i < n; i++) {
        if (m->knobs[i] == 0u) {
            continue;
        }
        bands[m->knobs[i] - 1u]++;
        if (m->knobs[i] == 3u && (r->place[LF_PLACE_KNOB_HIGH].found == 0u || h[i] > r->place[LF_PLACE_KNOB_HIGH].z)) {
            r->place[LF_PLACE_KNOB_HIGH] = lf_place_at(grid, (uint32_t)i);
        }
    }
    v[LF_V_KNOBS] = (float64_t)(bands[0] + bands[1] + bands[2]);
    for (uint32_t k = 0u; k < 3u; k++) {
        v[LF_V_KNOB_BASE + k] = (float64_t)bands[k];
    }
}

/* The slope classes of a window's land, from its heights every 4 blocks, over 8. land nullptr: land is above 0. */
static void lf_window_slopes(const float32_t *heights, const uint8_t *land, uint64_t *classes, uint64_t *out_land,
                             uint64_t *out_steep) {
    for (uint32_t row = 1u; row <= LF_WINDOW_SIDE; row++) {
        for (uint32_t column = 1u; column <= LF_WINDOW_SIDE; column++) {
            const uint32_t at = row * LF_WINDOW_GRID + column;
            if (land != nullptr ? land[at] == 0u : heights[at] <= 0.0f32) {
                continue;
            }
            const float64_t g =
                lf_gradient((float64_t)heights[at - 1u], (float64_t)heights[at + 1u],
                            (float64_t)heights[at - LF_WINDOW_GRID], (float64_t)heights[at + LF_WINDOW_GRID], 8.0f64);
            classes[lf_class(g)]++;
            (*out_land)++;
            *out_steep += g >= LF_CLASS_FROM[3] ? 1u : 0u;
        }
    }
}

void lf_windows(job_system_t *system, lf_run_t *run, const lf_memory_t *m, const lf_grid_t *grid, uint32_t windows_side,
                lf_result_t *r) {
    const uint32_t windows = windows_side * windows_side;
    const uint32_t share = grid->side * grid->step / windows_side;
    for (uint32_t w = 0u; w < windows; w++) {
        m->window_origins[2u * w] =
            grid->origin_x +
            (int32_t)(((w % windows_side) * share + (share - LF_WINDOW_SIDE * LF_WINDOW_STEP) / 2u) & ~15u);
        m->window_origins[2u * w + 1u] =
            grid->origin_y +
            (int32_t)(((w / windows_side) * share + (share - LF_WINDOW_SIDE * LF_WINDOW_STEP) / 2u) & ~15u);
    }
    run->borrowed_window_origins = m->window_origins;
    run->borrowed_windows_profile = m->windows_profile;
    run->borrowed_windows_real = m->windows_real;
    run->borrowed_windows_land = m->windows_land;
    lf_jobs(system, run, LF_STAGE_WINDOWS_PROFILE, windows * LF_WINDOW_GRID);
    lf_jobs(system, run, LF_STAGE_WINDOWS_REAL, windows * LF_WINDOW_GRID);
    uint64_t profile[LF_CLASSES] = {0u}, real[LF_CLASSES] = {0u}, profile_land = 0u, real_land = 0u, steep = 0u;
    float64_t steepest = -1.0f64;
    for (uint32_t w = 0u; w < windows; w++) {
        const uint64_t at = (uint64_t)w * LF_WINDOW_COLUMNS;
        uint64_t one_land = 0u, one_steep = 0u;
        lf_window_slopes(m->windows_profile + at, nullptr, profile, &profile_land, &steep);
        lf_window_slopes(m->windows_real + at, m->windows_land + at, real, &one_land, &one_steep);
        real_land += one_land;
        const float64_t share_steep = lf_percent(one_steep, one_land);
        if (one_land > 0u && share_steep > steepest) {
            steepest = share_steep;
            r->place[LF_PLACE_STEEP_WINDOW] =
                (lf_place_t){.x = m->window_origins[2u * w] + (int32_t)(LF_WINDOW_SIDE * LF_WINDOW_STEP / 2u),
                             .y = m->window_origins[2u * w + 1u] + (int32_t)(LF_WINDOW_SIDE * LF_WINDOW_STEP / 2u),
                             .z = (float32_t)share_steep,
                             .found = 1u};
        }
        for (uint32_t row = 1u; row <= LF_WINDOW_SIDE; row++) {
            for (uint32_t column = 1u; column <= LF_WINDOW_SIDE; column++) {
                const uint64_t k = at + row * LF_WINDOW_GRID + column;
                lf_place_t *const below = &r->place[LF_PLACE_BELOW_SEA];
                if (m->windows_land[k] != 0u && m->windows_real[k] < 0.0f32 &&
                    (below->found == 0u || m->windows_real[k] < below->z)) {
                    *below = (lf_place_t){.x = m->window_origins[2u * w] + (int32_t)((column - 1u) * LF_WINDOW_STEP),
                                          .y = m->window_origins[2u * w + 1u] + (int32_t)((row - 1u) * LF_WINDOW_STEP),
                                          .z = m->windows_real[k],
                                          .found = 1u};
                }
            }
        }
    }
    r->v[LF_V_DIFFERENCE] = 0.0f64;
    for (uint32_t c = 0u; c < LF_CLASSES; c++) {
        r->v[LF_V_PROFILE + c] = lf_percent(profile[c], profile_land);
        r->v[LF_V_REAL + c] = lf_percent(real[c], real_land);
        const float64_t difference = fabs(r->v[LF_V_PROFILE + c] - r->v[LF_V_REAL + c]);
        r->v[LF_V_DIFFERENCE] = difference > r->v[LF_V_DIFFERENCE] ? difference : r->v[LF_V_DIFFERENCE];
    }
}
