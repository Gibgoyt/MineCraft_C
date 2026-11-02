/* See run/src/game/test_world/README.md. */
#ifndef MC_RUN_GAME_TEST_WORLD_INDEX_H
#define MC_RUN_GAME_TEST_WORLD_INDEX_H

#include "terrain/density/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <stdint.h>

enum : uint64_t {
    /* A whole column of states, section after section, each in section_voxel_index order. */
    GAME_TEST_WORLD_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
};

/* The blocks of a column: the terrain's own shape, with a surface over its stone. Any thread, touching only what it is given.
 * states holds GAME_TEST_WORLD_COLUMN_VOXELS. */
void game_test_world_fill(const terrain_density_t *density, int32_t chunk_x, int32_t chunk_y, uint16_t *states);

/* The highest z with anything in it at this x and y, water included. Asks one voxel at a time, so not for a hot path. */
int32_t game_test_world_ground_z(const terrain_density_t *density, int32_t x, int32_t y);

#endif
