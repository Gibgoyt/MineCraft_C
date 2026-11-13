/* See run/src/climate/mask/README.md. */
#ifndef MC_RUN_CLIMATE_MASK_INDEX_H
#define MC_RUN_CLIMATE_MASK_INDEX_H

#include "climate/cache/index.h"
#include "climate/cell/index.h"
#include "types/index.h"

#include <stdint.h>

/* The coarse land mask's sparse table, as the Owner lends it: CLIMATE_MASK_SLOTS slots, and slot i's page at
 * borrowed_heights + i * 256. Nothing here owns any of it. */
typedef struct climate_mask_t {
    climate_slot_t *borrowed_slots;      /* 0 */
    float32_t *borrowed_heights;         /* 8 */
    climate_ground_fn_t ground;          /* 16 */
    const void *borrowed_ground_context; /* 24 */
} climate_mask_t;
static_assert(sizeof(climate_mask_t) == 32);

/* Where a page came from, for tests and the bench. */
typedef enum climate_mask_source_t : uint32_t {
    CLIMATE_MASK_FROM_TABLE = 0, /* it was there */
    CLIMATE_MASK_BUILT_KEPT = 1, /* built into the table, and published */
    CLIMATE_MASK_BUILT_COPY = 2, /* built privately: someone else held its claim, or it is outside the keep square */
} climate_mask_source_t;

/* A dry basin's cell holds its ground at this at the least: the mask says where the land is, and a dry basin is land, however
 * far below the sea's level its floor is (plan 007 phase 05). The cells' own ground, which their heat reads, is not this. */
static const float32_t CLIMATE_MASK_DRY_Z = 1.0f32;

/* The mask's ground at count_x by count_y cell centres from (x, y), CLIMATE_CELL_BLOCKS apart (at most a page's 256): ground's
 * heights, with each dry column's held to CLIMATE_MASK_DRY_Z at the least. Pure. */
void climate_mask_ground(climate_ground_fn_t ground, const void *ground_context, int32_t x, int32_t y, uint32_t count_x,
                         uint32_t count_y, float32_t *heights);

/* One page: the mask's ground at the 256 cell centres of tile (tile_x, tile_y), 16 by 16, south row first, x fastest:
 * x = tile_x * 1024 + 32 + 64 i, y = tile_y * 1024 + 32 + 64 j. 256 estimates. Pure. */
void climate_mask_page_build(climate_ground_fn_t ground, const void *ground_context, int32_t tile_x, int32_t tile_y,
                             float32_t *heights);

/* One page into out (256 values), from the table if it is there, or built: kept in the table when this claims its slot
 * (keep_x, keep_y is the mask keep square's middle), otherwise privately. Never waits. Any thread. */
climate_mask_source_t climate_mask_page_get(const climate_mask_t *mask, int32_t tile_x, int32_t tile_y, int32_t keep_x,
                                            int32_t keep_y, float32_t *out);

/* Tile (tile_x, tile_y)'s window: its 17 by 17 pages, tile - 8 to tile + 8 each way, in row-major order, into window
 * (CLIMATE_WINDOW_CELL_COUNT values, cell (i, j) at j * 272 + i, row 0 at the south). Counts where its pages came from
 * into sources[3] if that is not nullptr. Never waits. Any thread. */
void climate_mask_window(const climate_mask_t *mask, int32_t tile_x, int32_t tile_y, int32_t keep_x, int32_t keep_y,
                         float32_t *window, uint32_t *sources);

#endif
