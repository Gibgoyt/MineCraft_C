/* See README.md. The Owner: it holds the region, and lends its arena to the parts. */
#include "terrain/index.h"

#include <string.h>

bool terrain_init(terrain_t *terrain, uint64_t seed) {
    memset(terrain, 0, sizeof(*terrain));
    if (!heap_reserve(TERRAIN_TABLES_BYTES, &terrain->owned_tables)) {
        return false; /* it has said why */
    }
    arena_init(&terrain->arena_tables, "terrain_tables", terrain->owned_tables.owned_base, terrain->owned_tables.bytes);
    terrain->seed = seed;
    terrain_noise_bank_init(&terrain->noise_bank, &terrain->arena_tables, seed, TERRAIN_NOISE_FIELDS_MAX);
    if (!terrain_climate_init(&terrain->borrowed_climate, &terrain->arena_tables, &terrain->noise_bank)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    if (!terrain_density_init(&terrain->borrowed_density, &terrain->arena_tables, &terrain->noise_bank,
                              terrain->borrowed_climate)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    if (!terrain_hydrology_init(&terrain->borrowed_hydrology, &terrain->arena_tables, &terrain->noise_bank,
                                terrain->borrowed_density, terrain->borrowed_climate)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    return true;
}

void terrain_deinit(terrain_t *terrain) {
    if (terrain->borrowed_hydrology != nullptr) {
        terrain_hydrology_deinit(terrain->borrowed_hydrology);
    }
    if (terrain->borrowed_density != nullptr) {
        terrain_density_deinit(terrain->borrowed_density);
    }
    if (terrain->borrowed_climate != nullptr) {
        terrain_climate_deinit(terrain->borrowed_climate);
    }
    terrain_noise_bank_deinit(&terrain->noise_bank);
    arena_deinit(&terrain->arena_tables);
    heap_release(&terrain->owned_tables);
    memset(terrain, 0, sizeof(*terrain));
}
