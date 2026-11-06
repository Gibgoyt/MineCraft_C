/* See README.md. One tile's cells from its window, and the blend between cells: plan 004 master plan 6.2 and 6.9, and the
 * phase 03 file's "What a tile job does, in order", steps 2 to 7. */
#include "climate/cell/index.h"

#include "climate/index.h"
#include "climate/sea/index.h"

#include <math.h>
#include <string.h>

static_assert(CLIMATE_WINDOW_CELLS > 0 && CLIMATE_MARGIN_CELLS > 0);

/* What one of the two transforms gives a cell: blocks, and the unit vector toward the nearest marked cell. */
typedef struct cell_reach_t {
    float32_t distance;     /* 0 */
    float32_t direction[2]; /* 8 */
} cell_reach_t;
static_assert(sizeof(cell_reach_t) == 12);

/* From cells to blocks: the coast is taken as half a cell in from the marked cell's centre, and the distance capped. */
static cell_reach_t cell_reach_of(uint32_t at, uint32_t distance2, uint32_t nearest) {
    if (distance2 == CLIMATE_SEA_FAR) {
        return (cell_reach_t){.distance = CLIMATE_SEA_DISTANCE_MAX, .direction = {0.0f32, 0.0f32}};
    }
    const float64_t cells = sqrt((float64_t)distance2);
    float64_t blocks = (float64_t)CLIMATE_CELL_BLOCKS * cells - 0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS;
    blocks = blocks < 0.0f64 ? 0.0f64 : blocks;
    blocks = blocks > (float64_t)CLIMATE_SEA_DISTANCE_MAX ? (float64_t)CLIMATE_SEA_DISTANCE_MAX : blocks;
    cell_reach_t reach = {.distance = (float32_t)blocks, .direction = {0.0f32, 0.0f32}};
    if (distance2 != 0u) {
        const uint32_t side = (uint32_t)CLIMATE_WINDOW_CELLS;
        const float64_t dx = (float64_t)(int64_t)(nearest % side) - (float64_t)(int64_t)(at % side);
        const float64_t dy = (float64_t)(int64_t)(nearest / side) - (float64_t)(int64_t)(at / side);
        reach.direction[0] = (float32_t)(dx / cells);
        reach.direction[1] = (float32_t)(dy / cells);
    }
    return reach;
}

void climate_cell_build_tile(arena_t *arena, climate_ground_fn_t ground, const void *ground_context, int32_t tile_x,
                             int32_t tile_y, const float32_t *window_heights, climate_cell_t *cells) {
    const uint32_t side = (uint32_t)CLIMATE_WINDOW_CELLS;
    const uint32_t count = (uint32_t)CLIMATE_WINDOW_CELL_COUNT;
    uint8_t *const water = arena_alloc(arena, count, 64u);
    uint8_t *const kind = arena_alloc(arena, count, 64u);
    uint8_t *const marked = arena_alloc(arena, count, 64u);
    uint32_t *const labels = arena_alloc_array(arena, count, sizeof(uint32_t), 64u);
    uint32_t *const queue = arena_alloc_array(arena, count, sizeof(uint32_t), 64u);
    uint32_t *const sea_distance2 = arena_alloc_array(arena, count, sizeof(uint32_t), 64u);
    uint32_t *const sea_nearest = arena_alloc_array(arena, count, sizeof(uint32_t), 64u);
    uint32_t *const land_distance2 = arena_alloc_array(arena, count, sizeof(uint32_t), 64u);
    uint32_t *const land_nearest = arena_alloc_array(arena, count, sizeof(uint32_t), 64u);
    const climate_sea_scratch_t scratch = {
        .words = arena_alloc_array(arena, 5u * side + 1u, sizeof(uint32_t), 64u),
        .crossings = arena_alloc_array(arena, side + 1u, sizeof(float64_t), 64u),
    };
    const uint32_t samples_side = (uint32_t)CLIMATE_TILE_SAMPLES_SIDE;
    float32_t *const samples = arena_alloc_array(arena, samples_side * samples_side, sizeof(float32_t), 64u);

    /* 2. Water is ground at or below sea level, MC_WORLD_Z_SEA = 0, at the cell's centre. */
    for (uint32_t i = 0u; i < count; i++) {
        water[i] = window_heights[i] <= 0.0f32 ? 1u : 0u;
    }
    /* 3. Which water is sea, in this window alone. */
    (void)climate_sea_classify(water, side, labels, queue, kind);
    /* 4. To the sea. */
    for (uint32_t i = 0u; i < count; i++) {
        marked[i] = kind[i] == CLIMATE_SEA_KIND_SEA ? 1u : 0u;
    }
    climate_sea_distance(marked, side, sea_distance2, sea_nearest, &scratch);
    /* 5. To land, where a small lake counts as land, as the sea rule says. */
    for (uint32_t i = 0u; i < count; i++) {
        marked[i] = (kind[i] == CLIMATE_SEA_KIND_LAND || kind[i] == CLIMATE_SEA_KIND_LAKE_SMALL) ? 1u : 0u;
    }
    climate_sea_distance(marked, side, land_distance2, land_nearest, &scratch);
    /* 6. The ground samples, 16 blocks apart from 8 blocks in: all lattice columns. */
    const int64_t west = (int64_t)tile_x * CLIMATE_TILE_BLOCKS;
    const int64_t south = (int64_t)tile_y * CLIMATE_TILE_BLOCKS;
    ground(ground_context, (int32_t)(west + CLIMATE_GROUND_SAMPLE_STEP / 2),
           (int32_t)(south + CLIMATE_GROUND_SAMPLE_STEP / 2), CLIMATE_GROUND_SAMPLE_STEP, samples_side, samples_side,
           samples);

    /* 7. The cells, in a fixed order, so every sum adds in the same order on any worker. */
    memset(cells, 0, (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(climate_cell_t));
    const uint32_t per = (uint32_t)CLIMATE_GROUND_SAMPLES_SIDE;
    const float32_t sample_count = (float32_t)(per * per);
    for (uint32_t iy = 0u; iy < (uint32_t)CLIMATE_TILE_CELLS; iy++) {
        for (uint32_t ix = 0u; ix < (uint32_t)CLIMATE_TILE_CELLS; ix++) {
            climate_cell_t *const cell = &cells[iy * (uint32_t)CLIMATE_TILE_CELLS + ix];
            const uint32_t at = ((uint32_t)CLIMATE_MARGIN_CELLS + iy) * side + (uint32_t)CLIMATE_MARGIN_CELLS + ix;
            const int64_t centre_y = south + (int64_t)iy * CLIMATE_CELL_BLOCKS + CLIMATE_CELL_BLOCKS / 2;
            cell->lat_deg = (float32_t)climate_latitude_deg((float64_t)centre_y);
            float32_t sum = 0.0f32;
            for (uint32_t sy = 0u; sy < per; sy++) {
                for (uint32_t sx = 0u; sx < per; sx++) {
                    sum += samples[(iy * per + sy) * samples_side + ix * per + sx];
                }
            }
            cell->z_ground = sum / sample_count;
            const cell_reach_t to_land = cell_reach_of(at, land_distance2[at], land_nearest[at]);
            if (kind[at] == CLIMATE_SEA_KIND_SEA) {
                cell->flags = CLIMATE_CELL_SEA;
                cell->land_distance = to_land.distance;
                cell->land_direction[0] = to_land.direction[0];
                cell->land_direction[1] = to_land.direction[1];
            } else {
                const cell_reach_t to_sea = cell_reach_of(at, sea_distance2[at], sea_nearest[at]);
                cell->sea_distance = to_sea.distance;
                cell->sea_direction[0] = to_sea.direction[0];
                cell->sea_direction[1] = to_sea.direction[1];
                cell->kappa = 1.0f32 - expf(-to_sea.distance / CLIMATE_KAPPA_SCALE);
                if (kind[at] == CLIMATE_SEA_KIND_LAKE_LARGE) {
                    /* A large lake is neither: it has both distances, and is water for moisture in phase 05. */
                    cell->flags = CLIMATE_CELL_LAKE;
                    cell->land_distance = to_land.distance;
                    cell->land_direction[0] = to_land.direction[0];
                    cell->land_direction[1] = to_land.direction[1];
                } else if (kind[at] == CLIMATE_SEA_KIND_LAKE_SMALL) {
                    cell->flags = CLIMATE_CELL_LAKE_SMALL;
                }
            }
            cell->hurricane_days[0] = CLIMATE_DAY_NONE;
            cell->hurricane_days[1] = CLIMATE_DAY_NONE;
        }
    }
}

void climate_cell_blend(const climate_cell_t *const corners[4], float32_t t_x, float32_t t_y, climate_sample_t *out) {
    memset(out, 0, sizeof(*out));
#define CELL_BLEND(field)                                                                                              \
    ((corners[0]->field + t_x * (corners[1]->field - corners[0]->field)) +                                             \
     t_y * ((corners[2]->field + t_x * (corners[3]->field - corners[2]->field)) -                                      \
            (corners[0]->field + t_x * (corners[1]->field - corners[0]->field))))
    out->z_ground = CELL_BLEND(z_ground);
    out->sea_distance = CELL_BLEND(sea_distance);
    out->kappa = CELL_BLEND(kappa);
    out->land_distance = CELL_BLEND(land_distance);
    const float32_t dx = CELL_BLEND(sea_direction[0]);
    const float32_t dy = CELL_BLEND(sea_direction[1]);
#undef CELL_BLEND
    const float32_t length = sqrtf(dx * dx + dy * dy);
    if (length > 1.0e-6f32) {
        out->sea_direction[0] = dx / length;
        out->sea_direction[1] = dy / length;
    }
    const uint32_t nearest = (t_x >= 0.5f32 ? 1u : 0u) + (t_y >= 0.5f32 ? 2u : 0u);
    out->flags = corners[nearest]->flags;
}
