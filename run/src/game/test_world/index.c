/* See README.md. The world to fly through until Stages 10.4 to 10.7 make the real one: the density's own shape, whose only blocks
 * are bedrock, stone and water, with grass, dirt, sand and snow laid over the stone so that it can be told what it is looking at. */
#include "game/test_world/index.h"

#include "machine/index.h"
#include "tables/blocks/index.h"

enum : int32_t {
    /* How much dirt is under the grass. */
    TEST_WORLD_SOIL_DEPTH = 4,
    /* At or below this it is a beach or a sea bed, whatever is above. */
    TEST_WORLD_BEACH_TOP = MC_WORLD_Z_SEA + 2,
    TEST_WORLD_SNOW_LINE = 170,
};

/* One column of blocks, from the top down. A stone voxel with no stone above is a surface, including under an overhang. */
static void test_world_lay_surface(uint16_t *states, uint32_t x, uint32_t y) {
    const uint32_t per_layer = COLUMN_SIZE * COLUMN_SIZE;
    const int32_t height = COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN;
    int32_t depth = -1;
    uint16_t cover = BLOCK_ID_DIRT;
    for (int32_t storage = height - 1; storage >= 0; storage--) {
        uint16_t *const state = &states[(uint64_t)storage * per_layer + y * COLUMN_SIZE + x];
        if (*state != BLOCK_ID_STONE) {
            depth = -1;
            continue;
        }
        depth++;
        if (depth >= TEST_WORLD_SOIL_DEPTH) {
            continue;
        }
        if (depth == 0) {
            const int32_t z = storage + COLUMN_Z_STORAGE_MIN;
            const uint16_t above = storage + 1 < height
                                       ? states[(uint64_t)(storage + 1) * per_layer + y * COLUMN_SIZE + x]
                                       : (uint16_t)BLOCK_ID_AIR;
            if (above == BLOCK_ID_WATER || z <= TEST_WORLD_BEACH_TOP) {
                *state = BLOCK_ID_SAND;
                cover = BLOCK_ID_SAND;
            } else if (z >= TEST_WORLD_SNOW_LINE) {
                *state = BLOCK_ID_SNOW_BLOCK;
                cover = BLOCK_ID_STONE; /* snow straight on rock */
            } else {
                *state = BLOCK_ID_GRASS_BLOCK;
                cover = BLOCK_ID_DIRT;
            }
            continue;
        }
        *state = cover;
    }
}

void game_test_world_fill(const terrain_density_t *density, int32_t chunk_x, int32_t chunk_y, uint16_t *states) {
    const terrain_density_box_t box = {
        .origin_x = chunk_x * (int32_t)COLUMN_SIZE,
        .origin_y = chunk_y * (int32_t)COLUMN_SIZE,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = COLUMN_SIZE,
        .count_y = COLUMN_SIZE,
        .count_z = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    };
    terrain_density_fill_box(density, &box, states);
    for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
        for (uint32_t x = 0u; x < COLUMN_SIZE; x++) {
            test_world_lay_surface(states, x, y);
        }
    }
}

int32_t game_test_world_ground_z(const terrain_density_t *density, int32_t x, int32_t y) {
    for (int32_t z = TERRAIN_DENSITY_NEVER_SOLID_FROM; z > MC_WORLD_Z_MIN; z--) {
        if (terrain_density_block_at(density, x, y, z) != BLOCK_ID_AIR) {
            return z;
        }
    }
    return MC_WORLD_Z_MIN;
}
