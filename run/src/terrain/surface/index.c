/* See README.md. */
#include "terrain/surface/index.h"

#include "assert/index.h"
#include "machine/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"

#include <string.h>

enum : uint32_t {
    /* A box is worked through in tiles of this many blocks a side, lined up on the world, so what a tile holds fits on the
     * stack whatever size the box is. */
    SURFACE_TILE_SHIFT = 4u,
    SURFACE_TILE = 1u << SURFACE_TILE_SHIFT,
    SURFACE_TILE_COLUMNS = SURFACE_TILE * SURFACE_TILE,
    /* What lies over a tile's box, and no more than that can matter. */
    SURFACE_REACH = BIOME_LAYERS_MAX,
    SURFACE_REACH_VOXELS = SURFACE_TILE_COLUMNS * SURFACE_REACH,
    /* Steepness comes from the ground on the density's lattice, as hydrology's does. */
    SURFACE_CELL_SHIFT = 2u,
    SURFACE_CELL = 1u << SURFACE_CELL_SHIFT,
    SURFACE_LATTICE_SIDE = SURFACE_TILE / SURFACE_CELL + 2u,
    SURFACE_LATTICE = SURFACE_LATTICE_SIDE * SURFACE_LATTICE_SIDE,
    /* Lining caves goes in slices this tall, with a layer more above and below for what is next to them. */
    SURFACE_SLICE_LAYERS = 16u,
    SURFACE_SLICE_VOXELS = SURFACE_TILE_COLUMNS * (SURFACE_SLICE_LAYERS + 2u),
};
static_assert((uint32_t)SURFACE_CELL == (uint32_t)TERRAIN_DENSITY_CELL_XY);
static_assert((uint32_t)SURFACE_TILE_COLUMNS == (uint32_t)TERRAIN_SURFACE_TILE_COLUMNS);

/* The sizes in the README's formulas. Changing any number here changes every world. */
static const terrain_noise_shape_t SURFACE_SHAPE_DEPTH = {
    .frequency_x = 1.0f64 / 32.0f64,
    .frequency_y = 1.0f64 / 32.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t SURFACE_SHAPE_PATCH = {
    .frequency_x = 1.0f64 / 24.0f64,
    .frequency_y = 1.0f64 / 24.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const terrain_noise_shape_t SURFACE_SHAPE_WANDER = {
    .frequency_x = 1.0f64 / 192.0f64,
    .frequency_y = 1.0f64 / 192.0f64,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
static const float64_t SURFACE_DEPTH_LIMIT = 0.42f64;
static const float64_t SURFACE_PATCH_LIMIT = 0.42f64;
static const float64_t SURFACE_WANDER_LIMIT = 0.42f64;
/* How far the bands bend, at most, up or down. */
static const float32_t SURFACE_WANDER_BLOCKS = 6.0f32;
/* Above height + softness plus this there is no ground, and below the same less this there is nothing else: the density's
 * lattice adds at most this. */
static const float32_t SURFACE_LATTICE_SLACK = 9.0f32;

/* The bands: how many of a colour goes in. Plain terracotta is what is left. */
typedef struct surface_band_row_t {
    const char *borrowed_name; /* 0: the row's stream comes from it */
    uint16_t block;            /* 8 */
    uint8_t runs;              /* 10: how many times it is laid */
    uint8_t thickest;          /* 11: each from 1 to this many layers */
    uint8_t _pad0[4];          /* 12 */
} surface_band_row_t;
static_assert(sizeof(surface_band_row_t) == 16);

static const surface_band_row_t SURFACE_BAND_ROWS[] = {
    {.borrowed_name = "surface/band_orange", .block = BLOCK_ID_ORANGE_TERRACOTTA, .runs = 20u, .thickest = 3u},
    {.borrowed_name = "surface/band_yellow", .block = BLOCK_ID_YELLOW_TERRACOTTA, .runs = 12u, .thickest = 2u},
    {.borrowed_name = "surface/band_brown", .block = BLOCK_ID_BROWN_TERRACOTTA, .runs = 10u, .thickest = 3u},
    {.borrowed_name = "surface/band_red", .block = BLOCK_ID_RED_TERRACOTTA, .runs = 12u, .thickest = 2u},
    {.borrowed_name = "surface/band_white", .block = BLOCK_ID_WHITE_TERRACOTTA, .runs = 8u, .thickest = 1u},
    {.borrowed_name = "surface/band_gray", .block = BLOCK_ID_LIGHT_GRAY_TERRACOTTA, .runs = 8u, .thickest = 2u},
};
enum : uint32_t {
    SURFACE_BAND_ROW_COUNT = sizeof(SURFACE_BAND_ROWS) / sizeof(SURFACE_BAND_ROWS[0]),
};

static void surface_bands_draw(uint64_t seed, uint16_t *bands) {
    for (uint32_t i = 0u; i < TERRAIN_SURFACE_BANDS; i++) {
        bands[i] = BLOCK_ID_TERRACOTTA;
    }
    for (uint32_t r = 0u; r < SURFACE_BAND_ROW_COUNT; r++) {
        const surface_band_row_t *const row = &SURFACE_BAND_ROWS[r];
        terrain_rng_t rng = terrain_rng_start(terrain_rng_derive(seed, terrain_rng_salt(row->borrowed_name)));
        for (uint32_t run = 0u; run < row->runs; run++) {
            const uint32_t at = terrain_rng_next_below(&rng, TERRAIN_SURFACE_BANDS);
            const uint32_t thick = 1u + terrain_rng_next_below(&rng, row->thickest);
            for (uint32_t i = 0u; i < thick; i++) {
                bands[(at + i) & (TERRAIN_SURFACE_BANDS - 1u)] = row->block;
            }
        }
    }
}

bool terrain_surface_init(terrain_surface_t **out, arena_t *arena, terrain_noise_bank_t *bank, uint64_t seed,
                          const terrain_biome_t *biome, const terrain_hydrology_t *hydrology,
                          const terrain_cave_t *cave) {
    *out = nullptr;
    terrain_surface_t *const surface = arena_alloc(arena, sizeof(*surface), alignof(terrain_surface_t));
    surface->borrowed_biome = biome;
    surface->borrowed_hydrology = hydrology;
    surface->borrowed_cave = cave;
    surface_bands_draw(seed, surface->bands);
    if (!terrain_noise_bank_add(bank, "surface/depth", &SURFACE_SHAPE_DEPTH, &surface->borrowed_depth) ||
        !terrain_noise_bank_add(bank, "surface/patch", &SURFACE_SHAPE_PATCH, &surface->borrowed_patch) ||
        !terrain_noise_bank_add(bank, "surface/wander", &SURFACE_SHAPE_WANDER, &surface->borrowed_wander)) {
        return false; /* it has said why */
    }
    *out = surface;
    return true;
}

void terrain_surface_deinit(terrain_surface_t *surface) {
    memset(surface, 0, sizeof(*surface));
}

static float32_t surface_spread(float32_t raw, float64_t limit) {
    float32_t u = (float32_t)((float64_t)raw / limit);
    u = u < -1.0f32 ? -1.0f32 : (u > 1.0f32 ? 1.0f32 : u);
    return u * (1.5f32 - 0.5f32 * u * u);
}

static uint16_t surface_patch_of(const biome_def_t *def, float32_t patch, uint16_t otherwise) {
    for (uint32_t i = 0u; i < def->patch_count; i++) {
        const biome_patch_t *const row = &biome_patches[def->first_patch + i];
        if (patch >= row->from && (patch < row->to || (patch >= 1.0f32 && row->to >= 1.0f32))) {
            return row->block;
        }
    }
    return otherwise;
}

/* Below the top, the column's own noise gives from 0 to BIOME_FILLER_WOBBLE layers more. */
static uint32_t surface_filler_layers(const biome_def_t *def, float32_t depth_noise) {
    if (def->filler_depth == 0u) {
        return 0u; /* a biome with no filler has none to vary */
    }
    const uint32_t more = (uint32_t)((depth_noise + 1.0f32) * 0.5f32 * (float32_t)(BIOME_FILLER_WOBBLE + 1u));
    return (uint32_t)def->filler_depth + (more > BIOME_FILLER_WOBBLE ? BIOME_FILLER_WOBBLE : more);
}

uint16_t terrain_surface_block_from(const biome_def_t *def, const uint16_t *bands, const terrain_surface_inputs_t *in) {
    if (in->wet != 0u) {
        /* Under water, a biome's own bed goes over what hydrology laid, and otherwise what hydrology laid stays. */
        return in->depth < def->wet_depth ? def->wet_top : in->block;
    }
    if (in->block != BLOCK_ID_STONE) {
        return in->block; /* hydrology's sand and gravel, and anything that is not ground to cover */
    }
    const uint32_t filler = surface_filler_layers(def, in->depth_noise);
    if (def->steep_from > 0.0f32 && in->steepness >= def->steep_from && in->depth <= filler) {
        return def->steep;
    }
    const bool in_bands = def->bands_depth != 0u && in->z >= def->bands_from;
    if (in->depth == 0u) {
        if (def->high != BLOCK_ID_AIR && in->z >= def->high_from) {
            return surface_patch_of(def, in->patch, def->high);
        }
        if (in_bands) {
            return bands[(uint32_t)in->band_z & (TERRAIN_SURFACE_BANDS - 1u)];
        }
        return surface_patch_of(def, in->patch, def->top);
    }
    if (in_bands) {
        return in->depth < def->bands_depth ? bands[(uint32_t)in->band_z & (TERRAIN_SURFACE_BANDS - 1u)]
                                            : BLOCK_ID_STONE;
    }
    if (in->depth <= filler) {
        return def->filler;
    }
    if (in->depth <= filler + def->under_depth) {
        return def->under;
    }
    return BLOCK_ID_STONE;
}

uint16_t terrain_surface_cave_block_from(const biome_def_t *def, uint16_t block, float32_t patch, bool cave_above,
                                         bool cave_below) {
    if (block != BLOCK_ID_STONE) {
        return block;
    }
    if (cave_above) {
        return surface_patch_of(def, patch, def->floor);
    }
    if (cave_below) {
        return def->ceiling;
    }
    return block;
}

/* ---- One tile ----------------------------------------------------------------------------------------------------- */

/* Where a tile's voxels lie in the box. */
typedef struct surface_span_t {
    int32_t x;        /* 0: of the first column */
    int32_t y;        /* 4 */
    uint32_t ix;      /* 8: in the box */
    uint32_t iy;      /* 12 */
    uint32_t count_x; /* 16 */
    uint32_t count_y; /* 20 */
} surface_span_t;
static_assert(sizeof(surface_span_t) == 24);

/* For each column, how deep the top layer of the box already is and whether what is open over it is water, from the voxels over
 * the box, which there may be ground in. */
[[gnu::noinline]]
static void surface_reach_above(const terrain_surface_t *surface, const surface_span_t *span, int32_t box_top,
                                uint8_t *depth, uint8_t *wet) {
    uint16_t above[SURFACE_REACH_VOXELS];
    const terrain_density_box_t over = {
        .origin_x = span->x,
        .origin_y = span->y,
        .origin_z = box_top + 1,
        .count_x = span->count_x,
        .count_y = span->count_y,
        .count_z = SURFACE_REACH,
    };
    terrain_hydrology_fill_box(surface->borrowed_hydrology, &over, above);
    const uint64_t layer = (uint64_t)span->count_x * span->count_y;
    for (uint32_t i = 0u; i < span->count_x * span->count_y; i++) {
        /* From the top of what was asked, going down, as if the voxel over that were solid: a voxel that deep has no open
         * voxel near enough above it to be anything but deep. */
        uint32_t walking = SURFACE_REACH;
        uint8_t walking_wet = 0u;
        for (uint32_t z = SURFACE_REACH; z-- > 0u;) {
            const uint16_t block = above[i + layer * z];
            if (block == BLOCK_ID_AIR || block == BLOCK_ID_WATER || block == BLOCK_ID_LAVA) {
                walking = 0u;
                walking_wet = block == BLOCK_ID_WATER ? 1u : 0u;
            } else if (walking < SURFACE_REACH) {
                walking++;
            }
        }
        depth[i] = (uint8_t)walking;
        wet[i] = walking_wet;
    }
}

/* What the lattice round a tile says. */
typedef struct surface_bounds_t {
    float32_t solid_top; /* 0: at or under this, no voxel of the tile has been left anything but stone */
    float32_t open_from; /* 4: from here up, nothing of the tile is ground */
} surface_bounds_t;
static_assert(sizeof(surface_bounds_t) == 8);

/* How steep each column is, the steepest edge of its lattice cell as hydrology has it, and what the corners of those cells allow
 * the ground to do. A voxel's density comes from those corners and nothing else, so the bounds hold for any noise at all, where
 * a column's own height and softness would not: on a steep slope, a corner can be far above the column. */
[[gnu::noinline]]
static surface_bounds_t surface_lattice_read(const terrain_surface_t *surface, const surface_span_t *span,
                                             float32_t *steepness) {
    const int64_t cell_x = (int64_t)span->x >> SURFACE_CELL_SHIFT;
    const int64_t cell_y = (int64_t)span->y >> SURFACE_CELL_SHIFT;
    const int64_t x_last = (int64_t)span->x + (int64_t)span->count_x - 1;
    const int64_t y_last = (int64_t)span->y + (int64_t)span->count_y - 1;
    const uint32_t side_x = (uint32_t)((x_last >> SURFACE_CELL_SHIFT) - cell_x) + 2u;
    const uint32_t side_y = (uint32_t)((y_last >> SURFACE_CELL_SHIFT) - cell_y) + 2u;
    terrain_hydrology_column_t lattice[SURFACE_LATTICE];
    const terrain_climate_area_t area = {
        .origin_x = (int32_t)(cell_x * (int64_t)SURFACE_CELL),
        .origin_y = (int32_t)(cell_y * (int64_t)SURFACE_CELL),
        .step_x = (int32_t)SURFACE_CELL,
        .step_y = (int32_t)SURFACE_CELL,
        .count_x = side_x,
        .count_y = side_y,
    };
    terrain_hydrology_columns_area(surface->borrowed_hydrology, &area, lattice);
    surface_bounds_t bounds = {
        .solid_top = lattice[0].height - lattice[0].softness,
        .open_from = lattice[0].height + lattice[0].softness,
    };
    for (uint32_t i = 1u; i < side_x * side_y; i++) {
        const float32_t low = lattice[i].height - lattice[i].softness;
        const float32_t high = lattice[i].height + lattice[i].softness;
        bounds.solid_top = low < bounds.solid_top ? low : bounds.solid_top;
        bounds.open_from = high > bounds.open_from ? high : bounds.open_from;
    }
    bounds.solid_top -= SURFACE_LATTICE_SLACK;
    bounds.open_from += SURFACE_LATTICE_SLACK;
    for (uint32_t iy = 0u; iy < span->count_y; iy++) {
        const uint32_t ly = (uint32_t)((((int64_t)span->y + (int64_t)iy) >> SURFACE_CELL_SHIFT) - cell_y);
        for (uint32_t ix = 0u; ix < span->count_x; ix++) {
            const uint32_t lx = (uint32_t)((((int64_t)span->x + (int64_t)ix) >> SURFACE_CELL_SHIFT) - cell_x);
            const uint32_t at = lx + side_x * ly;
            const float32_t a = lattice[at].ground;
            const float32_t b = lattice[at + 1u].ground;
            const float32_t c = lattice[at + side_x].ground;
            const float32_t d = lattice[at + side_x + 1u].ground;
            const float32_t edges[4] = {b - a, d - c, c - a, d - b};
            float32_t steepest = 0.0f32;
            for (uint32_t e = 0u; e < 4u; e++) {
                const float32_t size = edges[e] < 0.0f32 ? -edges[e] : edges[e];
                steepest = size > steepest ? size : steepest;
            }
            steepness[ix + span->count_x * iy] = steepest / (float32_t)SURFACE_CELL;
        }
    }
    return bounds;
}

[[gnu::noinline]]
static void surface_noise_fill(const terrain_surface_t *surface, const surface_span_t *span,
                               terrain_surface_tile_t *noise) {
    const terrain_noise_grid_t grid = {
        .origin_x = span->x,
        .origin_y = span->y,
        .step_x = 1,
        .step_y = 1,
        .step_z = 1,
        .count_x = span->count_x,
        .count_y = span->count_y,
        .count_z = 1u,
    };
    const uint32_t count = span->count_x * span->count_y;
    terrain_noise_sample_grid(surface->borrowed_depth, &grid, noise->depth);
    terrain_noise_sample_grid(surface->borrowed_patch, &grid, noise->patch);
    terrain_noise_sample_grid(surface->borrowed_wander, &grid, noise->wander);
    for (uint32_t i = 0u; i < count; i++) {
        noise->depth[i] = surface_spread(noise->depth[i], SURFACE_DEPTH_LIMIT);
        noise->patch[i] = surface_spread(noise->patch[i], SURFACE_PATCH_LIMIT);
        noise->wander[i] = surface_spread(noise->wander[i], SURFACE_WANDER_LIMIT);
    }
}

/* z, moved by how the bands bend, as a whole number of layers. */
static int32_t surface_band_z(int32_t z, float32_t wander) {
    const float32_t moved = wander * SURFACE_WANDER_BLOCKS;
    return z + (int32_t)(moved < 0.0f32 ? moved - 0.5f32 : moved + 0.5f32);
}

/* What does not change with the layers. The noise waits until a voxel asks for it. */
static void surface_tile_read(const terrain_surface_t *surface, const surface_span_t *span,
                              terrain_surface_tile_t *tile) {
    const terrain_climate_area_t area = {
        .origin_x = span->x,
        .origin_y = span->y,
        .step_x = 1,
        .step_y = 1,
        .count_x = span->count_x,
        .count_y = span->count_y,
    };
    terrain_biome_columns_area(surface->borrowed_biome, &area, tile->columns);
    surface_bounds_t bounds = surface_lattice_read(surface, span, tile->steepness);
    /* A river's cut is a column's own number, and takes ground away. */
    const uint32_t count = span->count_x * span->count_y;
    for (uint32_t i = 0u; i < count; i++) {
        bounds.solid_top =
            tile->columns[i].carve_top < bounds.solid_top ? tile->columns[i].carve_top : bounds.solid_top;
    }
    tile->solid_top = bounds.solid_top;
    tile->open_from = bounds.open_from;
    tile->has_noise = 0u;
}

static void surface_tile_lay(const terrain_surface_t *surface, terrain_surface_tile_t *tile,
                             const terrain_density_box_t *box, const surface_span_t *span, uint16_t *blocks) {
    /* Over all the ground, or too far under the top of all of it to be reached, the rules change nothing. */
    const int32_t box_top = box->origin_z + (int32_t)box->count_z - 1;
    if ((float32_t)box->origin_z >= tile->open_from ||
        (float32_t)box_top + (float32_t)SURFACE_REACH + 1.0f32 <= tile->solid_top) {
        return;
    }

    uint8_t depth[SURFACE_TILE_COLUMNS];
    uint8_t wet[SURFACE_TILE_COLUMNS];
    if ((float32_t)box_top + 1.0f32 >= tile->open_from) {
        /* Nothing over the box is ground, so it is the sea, or air. */
        memset(depth, 0, sizeof(depth));
        memset(wet, box_top + 1 <= MC_WORLD_Z_SEA ? 1 : 0, sizeof(wet));
    } else {
        surface_reach_above(surface, span, box_top, depth, wet);
    }
    if (tile->has_noise == 0u) {
        surface_noise_fill(surface, span, tile);
        tile->has_noise = 1u;
    }

    const uint64_t layer = (uint64_t)box->count_x * box->count_y;
    for (uint32_t iy = 0u; iy < span->count_y; iy++) {
        for (uint32_t ix = 0u; ix < span->count_x; ix++) {
            const uint32_t i = ix + span->count_x * iy;
            const biome_def_t *const def = &biome_defs[tile->columns[i].biome];
            const uint64_t column_at = (uint64_t)(span->ix + ix) + (uint64_t)box->count_x * (span->iy + iy);
            uint32_t walking = depth[i];
            uint8_t walking_wet = wet[i];
            for (uint32_t iz = box->count_z; iz-- > 0u;) {
                uint16_t *const voxel = &blocks[column_at + layer * iz];
                const uint16_t block = *voxel;
                if (block == BLOCK_ID_AIR || block == BLOCK_ID_WATER || block == BLOCK_ID_LAVA) {
                    walking = 0u;
                    walking_wet = block == BLOCK_ID_WATER ? 1u : 0u;
                    continue;
                }
                if (walking < SURFACE_REACH) {
                    const int32_t z = box->origin_z + (int32_t)iz;
                    const terrain_surface_inputs_t inputs = {
                        .depth_noise = tile->depth[i],
                        .patch = tile->patch[i],
                        .steepness = tile->steepness[i],
                        .z = z,
                        .band_z = surface_band_z(z, tile->wander[i]),
                        .depth = walking,
                        .block = block,
                        .wet = walking_wet,
                    };
                    *voxel = terrain_surface_block_from(def, surface->bands, &inputs);
                    walking++;
                }
            }
        }
    }
}

void terrain_surface_fill_box(const terrain_surface_t *surface, const terrain_density_box_t *box, uint16_t *blocks) {
    const int64_t tile = (int64_t)SURFACE_TILE;
    for (uint32_t iy = 0u; iy < box->count_y;) {
        const int64_t y = (int64_t)box->origin_y + (int64_t)iy;
        const int64_t y_end = ((y >> SURFACE_TILE_SHIFT) + 1) * tile;
        const int64_t y_left = (int64_t)box->count_y - (int64_t)iy;
        const uint32_t count_y = (uint32_t)(y_end - y < y_left ? y_end - y : y_left);
        for (uint32_t ix = 0u; ix < box->count_x;) {
            const int64_t x = (int64_t)box->origin_x + (int64_t)ix;
            const int64_t x_end = ((x >> SURFACE_TILE_SHIFT) + 1) * tile;
            const int64_t x_left = (int64_t)box->count_x - (int64_t)ix;
            const uint32_t count_x = (uint32_t)(x_end - x < x_left ? x_end - x : x_left);
            const surface_span_t span = {
                .x = (int32_t)x,
                .y = (int32_t)y,
                .ix = ix,
                .iy = iy,
                .count_x = count_x,
                .count_y = count_y,
            };
            terrain_surface_tile_t read;
            surface_tile_read(surface, &span, &read);
            surface_tile_lay(surface, &read, box, &span, blocks);
            ix += count_x;
        }
        iy += count_y;
    }
}

void terrain_surface_tile_read(const terrain_surface_t *surface, int32_t x, int32_t y, terrain_surface_tile_t *tile) {
    ASSERT(((uint32_t)x & (SURFACE_TILE - 1u)) == 0u && ((uint32_t)y & (SURFACE_TILE - 1u)) == 0u);
    const surface_span_t span = {.x = x, .y = y, .ix = 0u, .iy = 0u, .count_x = SURFACE_TILE, .count_y = SURFACE_TILE};
    surface_tile_read(surface, &span, tile);
}

void terrain_surface_fill_tile_box(const terrain_surface_t *surface, terrain_surface_tile_t *tile,
                                   const terrain_density_box_t *box, uint16_t *blocks) {
    ASSERT(box->count_x == SURFACE_TILE && box->count_y == SURFACE_TILE);
    const surface_span_t span = {
        .x = box->origin_x, .y = box->origin_y, .ix = 0u, .iy = 0u, .count_x = SURFACE_TILE, .count_y = SURFACE_TILE};
    surface_tile_lay(surface, tile, box, &span, blocks);
}

/* ---- Under the ground --------------------------------------------------------------------------------------------- */

/* Where a run of layers finds what caves want: cut[corner + ix + row * iy + layer * (z - z_origin)]. */
typedef struct surface_cut_t {
    const uint8_t *borrowed_cut; /* 0 */
    uint64_t corner;             /* 8 */
    uint64_t row;                /* 16 */
    uint64_t layer;              /* 24 */
    int64_t z_origin;            /* 32 */
} surface_cut_t;
static_assert(sizeof(surface_cut_t) == 40);

/* One tile's layers from z_first up to z_last, all inside the box, with what caves want on and just beyond each. */
static void surface_line_layers(const terrain_density_box_t *box, const surface_span_t *span,
                                const terrain_biome_column_t *columns, const float32_t *patch, int32_t z_first,
                                int32_t z_last, const surface_cut_t *from, uint16_t *blocks) {
    const uint64_t layer = (uint64_t)box->count_x * box->count_y;
    for (int32_t z = z_first; z <= z_last; z++) {
        const uint64_t bz = (uint64_t)((int64_t)z - (int64_t)box->origin_z);
        const uint64_t cz = (uint64_t)((int64_t)z - from->z_origin);
        for (uint32_t iy = 0u; iy < span->count_y; iy++) {
            for (uint32_t ix = 0u; ix < span->count_x; ix++) {
                const uint32_t i = ix + span->count_x * iy;
                if (columns[i].caves == 0u) {
                    continue;
                }
                const biome_id_t id = terrain_biome_of_voxel(&columns[i], z);
                const biome_def_t *const def = &biome_defs[id];
                if (def->where != BIOME_WHERE_UNDERGROUND) {
                    continue;
                }
                uint16_t *const voxel =
                    &blocks[(uint64_t)(span->ix + ix) + (uint64_t)box->count_x * (span->iy + iy) + layer * bz];
                const uint64_t cut_at = from->corner + ix + from->row * iy + from->layer * cz;
                const bool above = from->borrowed_cut[cut_at + from->layer] != 0u;
                const bool below = from->borrowed_cut[cut_at - from->layer] != 0u;
                *voxel = terrain_surface_cave_block_from(def, *voxel, patch[i], above, below);
            }
        }
    }
}

/* One slice of one tile: from z_first up to z_last, both inside the box, asking the caves for it. */
[[gnu::noinline]]
static void surface_line_slice(const terrain_surface_t *surface, const terrain_density_box_t *box,
                               const surface_span_t *span, const terrain_biome_column_t *columns,
                               const float32_t *patch, int32_t z_first, int32_t z_last, uint16_t *blocks) {
    uint8_t cut[SURFACE_SLICE_VOXELS];
    const uint32_t layers = (uint32_t)(z_last - z_first) + 3u;
    const terrain_density_box_t around = {
        .origin_x = span->x,
        .origin_y = span->y,
        .origin_z = z_first - 1,
        .count_x = span->count_x,
        .count_y = span->count_y,
        .count_z = layers,
    };
    terrain_cave_mask_box(surface->borrowed_cave, &around, cut);
    const surface_cut_t from = {
        .borrowed_cut = cut,
        .corner = 0u,
        .row = span->count_x,
        .layer = (uint64_t)span->count_x * span->count_y,
        .z_origin = (int64_t)z_first - 1,
    };
    surface_line_layers(box, span, columns, patch, z_first, z_last, &from, blocks);
}

static void surface_line_tile(const terrain_surface_t *surface, const terrain_density_box_t *box,
                              const surface_span_t *span, const uint8_t *mask, uint16_t *blocks) {
    terrain_biome_column_t columns[SURFACE_TILE_COLUMNS];
    const terrain_climate_area_t area = {
        .origin_x = span->x,
        .origin_y = span->y,
        .step_x = 1,
        .step_y = 1,
        .count_x = span->count_x,
        .count_y = span->count_y,
    };
    terrain_biome_columns_area(surface->borrowed_biome, &area, columns);

    /* What z any of this tile's columns can hold an underground biome at. Most tiles hold none. */
    const uint32_t count = span->count_x * span->count_y;
    float32_t lowest = (float32_t)MC_WORLD_Z_MAX;
    float32_t highest = (float32_t)MC_WORLD_Z_MIN;
    for (uint32_t i = 0u; i < count; i++) {
        for (uint32_t row = 0u; row < BIOME_UNDERGROUND_COUNT; row++) {
            if ((columns[i].caves & (1u << row)) == 0u) {
                continue;
            }
            const biome_range_t range = biome_defs[biome_underground[row]].height;
            const float32_t ceiling = columns[i].ground - (float32_t)TERRAIN_BIOME_CAVE_ROOF;
            const float32_t top = ceiling < range.high ? ceiling : range.high;
            lowest = range.low < lowest ? range.low : lowest;
            highest = top > highest ? top : highest;
        }
    }
    const float32_t box_top = (float32_t)box->origin_z + (float32_t)box->count_z - 1.0f32;
    if (highest < lowest || highest < (float32_t)box->origin_z || lowest > box_top) {
        return;
    }

    float32_t patch[SURFACE_TILE_COLUMNS];
    const terrain_noise_grid_t grid = {
        .origin_x = span->x,
        .origin_y = span->y,
        .step_x = 1,
        .step_y = 1,
        .step_z = 1,
        .count_x = span->count_x,
        .count_y = span->count_y,
        .count_z = 1u,
    };
    terrain_noise_sample_grid(surface->borrowed_patch, &grid, patch);
    for (uint32_t i = 0u; i < count; i++) {
        patch[i] = surface_spread(patch[i], SURFACE_PATCH_LIMIT);
    }

    /* Slices lined up on the world, so what is asked of the caves does not change with the box. */
    const int32_t first = box->origin_z > (int32_t)lowest - 1 ? box->origin_z : (int32_t)lowest - 1;
    const int32_t last = box_top < highest + 1.0f32 ? (int32_t)box_top : (int32_t)highest + 1;
    const int32_t box_last = box->origin_z + (int32_t)box->count_z - 1;
    const surface_cut_t from_mask = {
        .borrowed_cut = mask,
        .corner = (uint64_t)span->ix + (uint64_t)box->count_x * span->iy,
        .row = box->count_x,
        .layer = (uint64_t)box->count_x * box->count_y,
        .z_origin = (int64_t)box->origin_z,
    };
    for (int32_t z = first; z <= last;) {
        /* With a mask, a layer with both its neighbours in the box has nothing to ask. The box's two end layers still do. */
        if (mask != nullptr && z > box->origin_z && z < box_last) {
            const int32_t inner_last = last < box_last - 1 ? last : box_last - 1;
            surface_line_layers(box, span, columns, patch, z, inner_last, &from_mask, blocks);
            z = inner_last + 1;
            continue;
        }
        const int32_t slice_end = (z >> 4) * 16 + 15;
        const int32_t z_last = mask != nullptr ? z : (slice_end < last ? slice_end : last);
        surface_line_slice(surface, box, span, columns, patch, z, z_last, blocks);
        z = z_last + 1;
    }
}

static void surface_line_box(const terrain_surface_t *surface, const terrain_density_box_t *box, const uint8_t *mask,
                             uint16_t *blocks) {
    const int64_t tile = (int64_t)SURFACE_TILE;
    for (uint32_t iy = 0u; iy < box->count_y;) {
        const int64_t y = (int64_t)box->origin_y + (int64_t)iy;
        const int64_t y_end = ((y >> SURFACE_TILE_SHIFT) + 1) * tile;
        const int64_t y_left = (int64_t)box->count_y - (int64_t)iy;
        const uint32_t count_y = (uint32_t)(y_end - y < y_left ? y_end - y : y_left);
        for (uint32_t ix = 0u; ix < box->count_x;) {
            const int64_t x = (int64_t)box->origin_x + (int64_t)ix;
            const int64_t x_end = ((x >> SURFACE_TILE_SHIFT) + 1) * tile;
            const int64_t x_left = (int64_t)box->count_x - (int64_t)ix;
            const uint32_t count_x = (uint32_t)(x_end - x < x_left ? x_end - x : x_left);
            const surface_span_t span = {
                .x = (int32_t)x,
                .y = (int32_t)y,
                .ix = ix,
                .iy = iy,
                .count_x = count_x,
                .count_y = count_y,
            };
            surface_line_tile(surface, box, &span, mask, blocks);
            ix += count_x;
        }
        iy += count_y;
    }
}

void terrain_surface_line_caves_box(const terrain_surface_t *surface, const terrain_density_box_t *box,
                                    uint16_t *blocks) {
    surface_line_box(surface, box, nullptr, blocks);
}

void terrain_surface_line_caves_with_mask(const terrain_surface_t *surface, const terrain_density_box_t *box,
                                          const uint8_t *mask, uint16_t *blocks) {
    surface_line_box(surface, box, mask, blocks);
}
