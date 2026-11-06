/* See README.md. The coarse land mask, a page a tile, and a tile's window of pages: plan 004 master plan 6.2, and the phase
 * 03 file's "The land mask, a sparse table, and its pages". */
#include "climate/mask/index.h"

#include <string.h>

void climate_mask_page_build(climate_ground_fn_t ground, const void *ground_context, int32_t tile_x, int32_t tile_y,
                             float32_t *heights) {
    /* In 64 bits first: a tile far out times 1024 is near the edge of 32. */
    const int64_t west = (int64_t)tile_x * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2;
    const int64_t south = (int64_t)tile_y * CLIMATE_TILE_BLOCKS + CLIMATE_CELL_BLOCKS / 2;
    ground(ground_context, (int32_t)west, (int32_t)south, CLIMATE_CELL_BLOCKS, (uint32_t)CLIMATE_TILE_CELLS,
           (uint32_t)CLIMATE_TILE_CELLS, heights);
}

climate_mask_source_t climate_mask_page_get(const climate_mask_t *mask, int32_t tile_x, int32_t tile_y, int32_t keep_x,
                                            int32_t keep_y, float32_t *out) {
    const uint64_t page_bytes = (uint64_t)CLIMATE_TILE_CELL_COUNT * sizeof(float32_t);
    const uint32_t index = climate_cache_slot_index(tile_x, tile_y, CLIMATE_MASK_SIDE);
    climate_slot_t *const slot = &mask->borrowed_slots[index];
    float32_t *const kept = mask->borrowed_heights + (uint64_t)index * (uint64_t)CLIMATE_TILE_CELL_COUNT;
    const uint64_t key = climate_cache_key(tile_x, tile_y);
    if (climate_cache_read_begin(slot, key)) {
        memcpy(out, kept, page_bytes);
        climate_cache_read_end(slot);
        return CLIMATE_MASK_FROM_TABLE;
    }
    const bool keeps = climate_cache_keeps(tile_x, tile_y, keep_x, keep_y, CLIMATE_MASK_SIDE);
    const climate_claim_t claim = climate_cache_claim(slot, key, keeps);
    if (claim == CLIMATE_CLAIM_OURS) {
        climate_mask_page_build(mask->ground, mask->borrowed_ground_context, tile_x, tile_y, kept);
        /* Copied before publishing: from then on only readers may touch it, and this is no reader. */
        memcpy(out, kept, page_bytes);
        climate_cache_publish(slot);
        return CLIMATE_MASK_BUILT_KEPT;
    }
    if (claim == CLIMATE_CLAIM_HAVE && climate_cache_read_begin(slot, key)) {
        /* Published between the two looks. */
        memcpy(out, kept, page_bytes);
        climate_cache_read_end(slot);
        return CLIMATE_MASK_FROM_TABLE;
    }
    /* Someone else is building it, or it may not be kept: a page is a pure function, so a private copy is the same
     * bytes, and nothing waits. */
    climate_mask_page_build(mask->ground, mask->borrowed_ground_context, tile_x, tile_y, out);
    return CLIMATE_MASK_BUILT_COPY;
}

void climate_mask_window(const climate_mask_t *mask, int32_t tile_x, int32_t tile_y, int32_t keep_x, int32_t keep_y,
                         float32_t *window, uint32_t *sources) {
    float32_t page[CLIMATE_TILE_CELL_COUNT];
    const int32_t reach = CLIMATE_WINDOW_TILES / 2;
    const uint32_t side = (uint32_t)CLIMATE_WINDOW_CELLS;
    const uint32_t cells = (uint32_t)CLIMATE_TILE_CELLS;
    for (int32_t py = 0; py < CLIMATE_WINDOW_TILES; py++) {
        for (int32_t px = 0; px < CLIMATE_WINDOW_TILES; px++) {
            const climate_mask_source_t source =
                climate_mask_page_get(mask, tile_x - reach + px, tile_y - reach + py, keep_x, keep_y, page);
            if (sources != nullptr) {
                sources[source]++;
            }
            for (uint32_t j = 0u; j < cells; j++) {
                float32_t *const row = window + ((uint32_t)py * cells + j) * side + (uint32_t)px * cells;
                memcpy(row, page + j * cells, cells * sizeof(float32_t));
            }
        }
    }
}
