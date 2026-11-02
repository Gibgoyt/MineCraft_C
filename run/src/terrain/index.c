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
    if (!terrain_cave_init(&terrain->borrowed_cave, &terrain->arena_tables, &terrain->noise_bank,
                           terrain->borrowed_density)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    terrain_ore_init(&terrain->borrowed_ore, &terrain->arena_tables, seed, terrain->borrowed_density);
    return true;
}

void terrain_deinit(terrain_t *terrain) {
    if (terrain->borrowed_ore != nullptr) {
        terrain_ore_deinit(terrain->borrowed_ore);
    }
    if (terrain->borrowed_cave != nullptr) {
        terrain_cave_deinit(terrain->borrowed_cave);
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
