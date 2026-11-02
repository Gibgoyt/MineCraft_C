/* See README.md. */
#include "world/column/index.h"

#include "tables/blocks/index.h"

#include <string.h>

/* A zeroed section and a zeroed column must read as air: see section_t. */
static_assert(BLOCK_ID_AIR == 0);

column_t *column_array_alloc(arena_t *arena, uint64_t count) {
    return arena_alloc_array(arena, count, sizeof(column_t), alignof(column_t));
}

void column_init(column_t *column, int32_t chunk_x, int32_t chunk_y) {
    for (uint32_t i = 0; i < COLUMN_SECTIONS; i++) {
        section_init(&column->sections[i], BLOCK_ID_AIR, SECTION_LIGHT_OPEN_SKY);
    }
    column->chunk_x = chunk_x;
    column->chunk_y = chunk_y;
}

void column_deinit(column_t *column, section_pool_t *pool) {
    for (uint32_t i = 0; i < COLUMN_SECTIONS; i++) {
        section_deinit(&column->sections[i], pool);
    }
    memset(column, 0, sizeof(*column));
}

static uint32_t column_voxel_of(uint32_t x, uint32_t y, int32_t z) {
    ASSERT(x < COLUMN_SIZE && y < COLUMN_SIZE);
    return section_voxel_index(x, y, column_section_local_of(z));
}

bool column_block_get(const column_t *column, uint32_t x, uint32_t y, int32_t z, uint16_t *out) {
    if (!column_z_in_gameplay(z)) {
        return false;
    }
    *out = section_block_get(&column->sections[column_section_of(z)], column_voxel_of(x, y, z));
    return true;
}

bool column_light_get(const column_t *column, uint32_t x, uint32_t y, int32_t z, uint8_t *out) {
    if (!column_z_in_gameplay(z)) {
        return false;
    }
    *out = section_light_get(&column->sections[column_section_of(z)], column_voxel_of(x, y, z));
    return true;
}

column_status_t column_block_set(column_t *column, section_pool_t *pool, uint32_t x, uint32_t y, int32_t z,
                                 uint16_t state) {
    if (!column_z_in_gameplay(z)) {
        return COLUMN_STATUS_Z_OUT_OF_RANGE;
    }
    section_t *const section = &column->sections[column_section_of(z)];
    return section_block_set(section, pool, column_voxel_of(x, y, z), state) ? COLUMN_STATUS_OK
                                                                             : COLUMN_STATUS_NO_MEMORY;
}

column_status_t column_light_set(column_t *column, section_pool_t *pool, uint32_t x, uint32_t y, int32_t z,
                                 uint8_t packed) {
    if (!column_z_in_gameplay(z)) {
        return COLUMN_STATUS_Z_OUT_OF_RANGE;
    }
    section_t *const section = &column->sections[column_section_of(z)];
    return section_light_set(section, pool, column_voxel_of(x, y, z), packed) ? COLUMN_STATUS_OK
                                                                              : COLUMN_STATUS_NO_MEMORY;
}

uint64_t column_memory_bytes(const column_t *column) {
    uint64_t bytes = sizeof(*column);
    for (uint32_t i = 0; i < COLUMN_SECTIONS; i++) {
        bytes += section_pool_bytes(&column->sections[i]);
    }
    return bytes;
}
