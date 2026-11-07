/* See README.md. Snow and ice on loaded ground from the census's week: one idempotent rule per block column, every change a block
 * edit, so the card redraws it, the save keeps it and light looks again, as for anything a player does. */
#include "game/cover/index.h"

#include "game/census/index.h"
#include "game/index.h"
#include "log/index.h"
#include "tables/blocks/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

/* ---- The rule ---------------------------------------------------------------------------------------------------------- */

/* Whether a section may hold a state at all: a palette without it, or one block of something else, cannot. */
static bool cover_section_may_hold(const section_t *section, uint16_t state) {
    if (section->bits == SECTION_BITS_UNIFORM) {
        return section->block_uniform == state;
    }
    if (section->bits == SECTION_BITS_DIRECT) {
        return true;
    }
    const uint16_t *const palette = section_palette(section);
    const uint32_t capacity = section_palette_capacity(section);
    for (uint32_t i = 0u; i < capacity; i++) {
        if (palette[i] == state) {
            return true;
        }
    }
    return false;
}

static uint16_t cover_block(const column_t *column, uint32_t local_x, uint32_t local_y, int32_t z) {
    if (!column_z_in_storage(z)) {
        return BLOCK_ID_AIR;
    }
    const section_t *const section = &column->sections[column_section_of(z)];
    return section_block_get(section, section_voxel_index(local_x, local_y, column_section_local_of(z)));
}

/* The highest voxel of a block column that is not air, from no higher than top_z, or below the storage if there is none. */
static int32_t cover_top(const column_t *column, uint32_t local_x, uint32_t local_y, int32_t top_z) {
    int32_t z = top_z;
    while (z >= COLUMN_Z_STORAGE_MIN) {
        const uint32_t s = column_section_of(z);
        const section_t *const section = &column->sections[s];
        if (section->bits == SECTION_BITS_UNIFORM && section->block_uniform == BLOCK_ID_AIR) {
            z = COLUMN_Z_STORAGE_MIN + (int32_t)(s * SECTION_SIZE) - 1;
            continue;
        }
        if (section_block_get(section, section_voxel_index(local_x, local_y, column_section_local_of(z))) !=
            BLOCK_ID_AIR) {
            return z;
        }
        z--;
    }
    return COLUMN_Z_STORAGE_MIN - 1;
}

/* What snow may lie on: the top of a whole solid cube, not leaves, and not snow, ice or lava (terrain/feature's snow rule). */
static bool cover_takes_snow(uint16_t block) {
    if (block >= BLOCK_COUNT || block == BLOCK_ID_SNOW_BLOCK || block == BLOCK_ID_ICE || block == BLOCK_ID_PACKED_ICE ||
        block == BLOCK_ID_BLUE_ICE || block == BLOCK_ID_LAVA) {
        return false;
    }
    const block_def_t *const def = &block_defs[block];
    return def->shape == BLOCK_SHAPE_CUBE && (def->flags & BLOCK_FLAG_SOLID) != 0u &&
           (def->flags & BLOCK_FLAG_DECAYS) == 0u;
}

static void cover_set(world_t *world, int32_t x, int32_t y, int32_t z, uint16_t state, uint64_t *count) {
    if (world_edit_block_set(&world->edit, x, y, z, state) == WORLD_EDIT_STATUS_CHANGED) {
        (*count)++;
    }
}

/* Whether ice at the top of a block column was water: water under it, or water or ice beside it at its height. A shore or a
 * pond a block deep freezes onto its bed, so water under it alone would keep that ice for ever. Ice beside counts as well as
 * water, so a pass thaws all of a sheet of ice whichever end it starts from, and a second pass finds nothing; a lone ice block a
 * player laid on dry ground is left. */
static bool cover_ice_was_water(const world_map_t *map, const column_t *column, int32_t x, int32_t y, int32_t z) {
    if (cover_block(column, column_local_of(x), column_local_of(y), z - 1) == BLOCK_ID_WATER) {
        return true;
    }
    static const int32_t beside[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (uint32_t i = 0u; i < 4u; i++) {
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(map, x + beside[i][0], y + beside[i][1], z, &block) == WORLD_MAP_STATUS_OK &&
            (block == BLOCK_ID_WATER || block == BLOCK_ID_ICE)) {
            return true;
        }
    }
    return false;
}

/* Every snow layer in a block column back to air: only the census lays them, so any found is its own. */
static void cover_snow_off(world_t *world, const column_t *column, int32_t x, int32_t y, int32_t top_z,
                           game_cover_counts_t *counts) {
    const uint32_t local_x = column_local_of(x), local_y = column_local_of(y);
    const uint32_t top_section = column_z_in_storage(top_z) ? column_section_of(top_z) : COLUMN_SECTIONS - 1u;
    for (uint32_t s = 0u; s <= top_section; s++) {
        const section_t *const section = &column->sections[s];
        if (!cover_section_may_hold(section, BLOCK_ID_SNOW_LAYER)) {
            continue;
        }
        for (uint32_t local_z = 0u; local_z < SECTION_SIZE; local_z++) {
            if (section_block_get(section, section_voxel_index(local_x, local_y, local_z)) == BLOCK_ID_SNOW_LAYER) {
                cover_set(world, x, y, COLUMN_Z_STORAGE_MIN + (int32_t)(s * SECTION_SIZE + local_z), BLOCK_ID_AIR,
                          &counts->snow_off);
            }
        }
    }
}

void game_cover_column(world_t *world, game_cover_week_fn *week, void *context, int32_t chunk_x, int32_t chunk_y,
                       game_cover_counts_t *counts) {
    world_map_t *const map = &world->map;
    uint32_t slot = 0u;
    const column_t *const column = world_map_column_at(map, chunk_x, chunk_y);
    if (column == nullptr || !world_map_slot_of(map, chunk_x, chunk_y, &slot)) {
        return;
    }
    counts->columns++;
    for (uint32_t local_y = 0u; local_y < COLUMN_SIZE; local_y++) {
        for (uint32_t local_x = 0u; local_x < COLUMN_SIZE; local_x++) {
            const int32_t x = chunk_x * (int32_t)COLUMN_SIZE + (int32_t)local_x;
            const int32_t y = chunk_y * (int32_t)COLUMN_SIZE + (int32_t)local_y;
            game_cover_week_t here;
            week(context, x, y, &here);
            if (here.known == 0u) {
                continue;
            }
            /* The column's top as the edits leave it: a write may have raised it. */
            int32_t top_z = world_edit_column_top_z(&world->edit, slot);
            if (here.snow_mm <= GAME_COVER_SNOW_OFF_MM) {
                cover_snow_off(world, column, x, y, top_z, counts);
            }
            top_z = world_edit_column_top_z(&world->edit, slot);
            const int32_t z_top = cover_top(column, local_x, local_y, top_z);
            if (z_top < COLUMN_Z_GAMEPLAY_MIN || z_top + 1 > COLUMN_Z_GAMEPLAY_MAX) {
                continue;
            }
            uint16_t block = cover_block(column, local_x, local_y, z_top);
            if (block == BLOCK_ID_SNOW_LAYER) {
                continue; /* snow lies here already, and stays until the week is under the off line */
            }
            const float32_t ice_on = here.sea != 0u ? GAME_COVER_SEA_ICE_ON_CM : GAME_COVER_ICE_ON_CM;
            if (block == BLOCK_ID_WATER && here.ice_cm > ice_on &&
                world_tick_fluid_level(&world->tick, x, y, z_top) == WORLD_TICK_LEVEL_SOURCE) {
                cover_set(world, x, y, z_top, BLOCK_ID_ICE, &counts->ice_on);
                block = BLOCK_ID_ICE;
            } else if (block == BLOCK_ID_ICE && here.ice_cm <= GAME_COVER_ICE_OFF_CM &&
                       cover_ice_was_water(map, column, x, y, z_top)) {
                cover_set(world, x, y, z_top, BLOCK_ID_WATER, &counts->ice_off);
                block = BLOCK_ID_WATER;
            }
            if (here.snow_mm > GAME_COVER_SNOW_ON_MM && cover_takes_snow(block)) {
                cover_set(world, x, y, z_top + 1, BLOCK_ID_SNOW_LAYER, &counts->snow_on);
            }
        }
    }
}

/* ---- The week from the climate ----------------------------------------------------------------------------------------- */

static int32_t cover_floor_div(int32_t value, int32_t by) {
    const int32_t q = value / by;
    return (value % by != 0 && value < 0) ? q - 1 : q;
}

/* A cell's week, if its tile is kept and has been through a census: a fresh tile's zeros are no week at all, and would melt
 * the ice an icy biome made. Its sea flag from the tile's side data, filled at the census. */
static const climate_week_t *cover_kept_cell(const climate_week_store_t *store, int32_t cell_x, int32_t cell_y,
                                             uint8_t *out_sea) {
    const int32_t tile_x = cover_floor_div(cell_x, (int32_t)CLIMATE_TILE_CELLS);
    const int32_t tile_y = cover_floor_div(cell_y, (int32_t)CLIMATE_TILE_CELLS);
    uint32_t index = 0u;
    const climate_week_tile_t *const tile = climate_week_store_find(store, tile_x, tile_y, &index);
    *out_sea = 0u;
    if (tile == nullptr) {
        return nullptr;
    }
    const climate_week_side_t *const side = &store->borrowed_sides[index];
    if (side->fresh != 0u) {
        return nullptr;
    }
    const uint32_t in = (uint32_t)(cell_y - tile_y * (int32_t)CLIMATE_TILE_CELLS) * CLIMATE_TILE_CELLS +
                        (uint32_t)(cell_x - tile_x * (int32_t)CLIMATE_TILE_CELLS);
    *out_sea = side->ground_ready != 0u ? side->sea[in] : 0u;
    return &tile->cells[in];
}

void game_cover_week_of_climate(void *context, int32_t x, int32_t y, game_cover_week_t *out) {
    const climate_t *const climate = context;
    const climate_week_store_t *const store = climate_week_store(climate);
    memset(out, 0, sizeof(*out));
    float64_t warped_x = 0.0f64, warped_y = 0.0f64;
    climate_warp_at(climate, (float64_t)x + 0.5f64, (float64_t)y + 0.5f64, &warped_x, &warped_y);
    /* Between the centres of the four cells round the point, as every other climate value is read. */
    const float64_t cells_x = warped_x / (float64_t)CLIMATE_CELL_BLOCKS - 0.5f64;
    const float64_t cells_y = warped_y / (float64_t)CLIMATE_CELL_BLOCKS - 0.5f64;
    const float64_t floor_x = floor(cells_x), floor_y = floor(cells_y);
    const float32_t fx = (float32_t)(cells_x - floor_x), fy = (float32_t)(cells_y - floor_y);
    const int32_t cell_x = (int32_t)floor_x, cell_y = (int32_t)floor_y;
    float32_t weight_sum = 0.0f32, snow = 0.0f32, ice = 0.0f32;
    for (int32_t j = 0; j < 2; j++) {
        for (int32_t i = 0; i < 2; i++) {
            uint8_t sea = 0u;
            const climate_week_t *const cell = cover_kept_cell(store, cell_x + i, cell_y + j, &sea);
            if (cell == nullptr) {
                continue;
            }
            const float32_t weight = (i == 0 ? 1.0f32 - fx : fx) * (j == 0 ? 1.0f32 - fy : fy);
            weight_sum += weight;
            snow += weight * cell->snow_mm;
            ice += weight * cell->ice_cm;
        }
    }
    if (!(weight_sum > 0.0f32)) {
        return; /* no kept cell near: nothing is known, so nothing changes */
    }
    out->snow_mm = snow / weight_sum;
    out->ice_cm = ice / weight_sum;
    out->known = 1u;
    /* The sea flag of the cell the point lands in. */
    uint8_t sea = 0u;
    (void)cover_kept_cell(store, (int32_t)floor(warped_x / (float64_t)CLIMATE_CELL_BLOCKS),
                          (int32_t)floor(warped_y / (float64_t)CLIMATE_CELL_BLOCKS), &sea);
    out->sea = sea;
}

/* ---- Wiring into a game -------------------------------------------------------------------------------------------------- */

static climate_t *cover_climate(const game_t *game) {
    climate_t *const climate = game->borrowed_climate;
    return climate != nullptr && climate_is_open(climate) && game_census_of(game) != nullptr ? climate : nullptr;
}

void game_cover_step(game_t *game) {
    game_cover_t *const cover = game_cover_of(game);
    climate_t *const climate = cover_climate(game);
    if (cover == nullptr || climate == nullptr) {
        return;
    }
    const game_census_t *const census = game_census_of(game);
    world_map_t *const map = &game->borrowed_world->map;
    if (climate_week_store(climate)->busy != 0u) {
        return; /* a census is writing the week: the pass waits for it */
    }
    if (atomic_load_explicit(&cover->busy, memory_order_relaxed) == 0u) {
        if (census->runs == cover->runs_seen || !game_census_idle(census)) {
            return;
        }
        cover->runs_seen = census->runs;
        memset(&cover->counts, 0, sizeof(cover->counts));
        cover->started_ns = time_now_ns();
        atomic_store_explicit(&cover->cursor, 0u, memory_order_relaxed);
        atomic_store_explicit(&cover->total, map->view_offset_count, memory_order_relaxed);
        atomic_store_explicit(&cover->busy, 1u, memory_order_relaxed);
    }
    uint32_t cursor = atomic_load_explicit(&cover->cursor, memory_order_relaxed);
    const uint32_t end = map->view_offset_count;
    for (uint32_t covered = 0u; covered < GAME_COVER_COLUMNS_PER_STEP && cursor < end; cursor++) {
        const world_map_offset_t offset = map->borrowed_offsets[cursor];
        const int32_t chunk_x = map->center_x + offset.dx, chunk_y = map->center_y + offset.dy;
        if (world_map_column_at(map, chunk_x, chunk_y) == nullptr) {
            continue;
        }
        game_cover_column(game->borrowed_world, game_cover_week_of_climate, climate, chunk_x, chunk_y, &cover->counts);
        covered++;
    }
    atomic_store_explicit(&cover->cursor, cursor, memory_order_relaxed);
    if (cursor >= end) {
        const game_cover_counts_t *const counts = &cover->counts;
        LOG_INFO("cover: %" PRIu64 " columns in %.0f ms: %" PRIu64 " snow layers laid and %" PRIu64 " gone, %" PRIu64
                 " blocks of water frozen and %" PRIu64 " thawed",
                 counts->columns, (libc_double_t)time_ns_to_ms(time_now_ns() - cover->started_ns), counts->snow_on,
                 counts->snow_off, counts->ice_on, counts->ice_off);
        atomic_store_explicit(&cover->busy, 0u, memory_order_relaxed);
    }
}

void game_cover_stream_in(game_t *game, int32_t chunk_x, int32_t chunk_y) {
    climate_t *const climate = cover_climate(game);
    game_cover_t *const cover = game_cover_of(game);
    /* While a census writes the week, the pass after it covers this column. A tile never through a census has no week. */
    if (climate == nullptr || cover == nullptr || climate_week_store(climate)->busy != 0u) {
        return;
    }
    game_cover_counts_t counts;
    memset(&counts, 0, sizeof(counts));
    game_cover_column(game->borrowed_world, game_cover_week_of_climate, climate, chunk_x, chunk_y, &counts);
}

bool game_cover_busy(const game_t *game, uint32_t *out_done, uint32_t *out_total) {
    const game_cover_t *const cover = game_cover_of(game);
    if (cover == nullptr || atomic_load_explicit(&cover->busy, memory_order_relaxed) == 0u) {
        return false;
    }
    *out_done = atomic_load_explicit(&cover->cursor, memory_order_relaxed);
    *out_total = atomic_load_explicit(&cover->total, memory_order_relaxed);
    return true;
}
