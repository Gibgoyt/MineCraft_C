/* See README.md. */
#include "game/spawn/index.h"

#include "machine/index.h"
#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <stddef.h>

/* A square of the search every this many blocks, with its places this many apart, out to this many blocks. The land nearest the middle is
 * worth looking closely for. Further out the question is only whether there is any, and looking as closely would be 16 million places. */
typedef struct game_spawn_band_t {
    int32_t step;  /* 0 */
    int32_t reach; /* 4 */
} game_spawn_band_t;
static_assert(sizeof(game_spawn_band_t) == 8);

static const game_spawn_band_t GAME_SPAWN_BANDS[] = {
    {.step = 16, .reach = 1024},
    {.step = 64, .reach = 8192},
    {.step = 256, .reach = GAME_SPAWN_REACH},
};
enum : uint32_t { GAME_SPAWN_BAND_COUNT = sizeof(GAME_SPAWN_BANDS) / sizeof(GAME_SPAWN_BANDS[0]) };

/* What the search's probe needs. */
typedef struct game_spawn_look_t {
    const terrain_t *borrowed_terrain;     /* 0 */
    const game_column_job_t *borrowed_job; /* 8 */
} game_spawn_look_t;
static_assert(sizeof(game_spawn_look_t) == 16);

/* All of a column's storage: terrain_generate_box's order is section_voxel_index's. */
int32_t game_ground_z(const terrain_t *terrain, const game_column_job_t *job, int32_t x, int32_t y) {
    const terrain_density_box_t box = {
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = 1u,
        .count_y = 1u,
        .count_z = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    };
    terrain_generate_box(terrain, &box, job->borrowed_opened, job->borrowed_states);
    for (int32_t storage = (int32_t)box.count_z - 1; storage > 0; storage--) {
        if (job->borrowed_states[storage] != BLOCK_ID_AIR) {
            return storage + COLUMN_Z_STORAGE_MIN;
        }
    }
    return COLUMN_Z_STORAGE_MIN;
}

static bool game_spawn_is_ice(uint16_t block) {
    return block == BLOCK_ID_ICE || block == BLOCK_ID_PACKED_ICE || block == BLOCK_ID_BLUE_ICE;
}

bool game_spawn_top_is_dry_land(const uint16_t *states, int32_t ground_z) {
    int32_t storage = ground_z - COLUMN_Z_STORAGE_MIN;
    const uint16_t top = states[storage];
    if ((block_defs[top].flags & BLOCK_FLAG_SOLID) == 0u || (block_defs[top].flags & BLOCK_FLAG_DECAYS) != 0u ||
        game_spawn_is_ice(top) || top == BLOCK_ID_CACTUS) {
        return false;
    }
    /* Snow is only as dry as what it lies on. */
    while (storage > 0 && states[storage] == BLOCK_ID_SNOW_BLOCK) {
        storage--;
    }
    if (game_spawn_is_ice(states[storage])) {
        return false;
    }
    return storage == 0 || (states[storage - 1] != BLOCK_ID_WATER && states[storage - 1] != BLOCK_ID_LAVA);
}

bool game_spawn_may_be_dry(const terrain_hydrology_column_t *column) {
    /* The most ground the density and the river leave here, with a block in hand for how a voxel rounds. A lower top has the sea on it. */
    const float32_t highest = column->height + column->softness;
    const float32_t cut = column->carve_top < highest ? column->carve_top : highest;
    return cut + 1.0f32 >= (float32_t)MC_WORLD_Z_SEA;
}

bool game_spawn_walk(const game_t *game, game_spawn_probe_t probe, void *borrowed_context, int32_t middle_x,
                     int32_t middle_y, int32_t *out_x, int32_t *out_y) {
    *out_x = middle_x;
    *out_y = middle_y;
    int32_t done_blocks = -1;
    for (uint32_t band = 0u; band < GAME_SPAWN_BAND_COUNT; band++) {
        const int32_t step = GAME_SPAWN_BANDS[band].step;
        /* The first square outside the last band's. */
        const int32_t first = done_blocks < 0 ? 0 : done_blocks / step + 1;
        const int32_t last = GAME_SPAWN_BANDS[band].reach / step;
        for (int32_t ring = first; ring <= last; ring++) {
            if (game != nullptr) {
                game_open_alive_tell(game);
            }
            for (int32_t dy = -ring; dy <= ring; dy++) {
                /* The inside of a square has been looked at already: only its first and last columns remain on these rows. */
                const int32_t across = dy == -ring || dy == ring ? 1 : 2 * ring;
                for (int32_t dx = -ring; dx <= ring; dx += across) {
                    const int32_t x = middle_x + dx * step, y = middle_y + dy * step;
                    if (probe(borrowed_context, x, y)) {
                        *out_x = x;
                        *out_y = y;
                        return true;
                    }
                }
            }
        }
        done_blocks = GAME_SPAWN_BANDS[band].reach;
    }
    return false;
}

static bool game_spawn_look(void *borrowed_context, int32_t x, int32_t y) {
    const game_spawn_look_t *const look = borrowed_context;
    const terrain_t *const terrain = look->borrowed_terrain;
    const terrain_hydrology_column_t column = terrain_hydrology_column_at(terrain->borrowed_hydrology, x, y);
    if (!game_spawn_may_be_dry(&column)) {
        return false;
    }
    const int32_t ground_z = game_ground_z(terrain, look->borrowed_job, x, y);
    return game_spawn_top_is_dry_land(look->borrowed_job->borrowed_states, ground_z);
}

bool game_spawn_find(const game_t *game, const terrain_t *terrain, const game_column_job_t *job, int32_t middle_x,
                     int32_t middle_y, int32_t *out_x, int32_t *out_y) {
    game_spawn_look_t look = {.borrowed_terrain = terrain, .borrowed_job = job};
    return game_spawn_walk(game, game_spawn_look, &look, middle_x, middle_y, out_x, out_y);
}
