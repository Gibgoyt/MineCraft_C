/* See README.md. The Owner: it holds the region, and lends its arena to the parts. */
#include "terrain/index.h"

#include "tables/blocks/index.h"

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
    if (!terrain_cave_init(&terrain->borrowed_cave, &terrain->arena_tables, &terrain->noise_bank,
                           terrain->borrowed_density)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    terrain_ore_init(&terrain->borrowed_ore, &terrain->arena_tables, seed, terrain->borrowed_density);
    terrain_biome_init(&terrain->borrowed_biome, &terrain->arena_tables, terrain->borrowed_climate,
                       terrain->borrowed_hydrology);
    if (!terrain_surface_init(&terrain->borrowed_surface, &terrain->arena_tables, &terrain->noise_bank, seed,
                              terrain->borrowed_biome, terrain->borrowed_hydrology, terrain->borrowed_cave)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    terrain_feature_init(&terrain->borrowed_feature, &terrain->arena_tables, seed, terrain->borrowed_biome,
                         terrain->borrowed_hydrology, terrain->borrowed_surface, terrain->borrowed_cave);
    return true;
}

void terrain_generate_box(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                          uint16_t *blocks) {
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, box, blocks);
    terrain_surface_fill_box(terrain->borrowed_surface, box, blocks);
    terrain_cave_mask_box(terrain->borrowed_cave, box, opened);
    const uint64_t voxels = terrain_density_box_voxels(box);
    /* Only what a cave truly cuts takes its fill from the flood. */
    for (uint64_t i = 0u; i < voxels; i++) {
        const uint16_t block = blocks[i];
        const bool rock =
            block != BLOCK_ID_AIR && block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA && block != BLOCK_ID_BEDROCK;
        opened[i] = opened[i] != 0u && rock ? 1u : 0u;
    }
    (void)terrain_cave_carve_box(terrain->borrowed_cave, box, blocks);
    terrain_hydrology_flood_box(terrain->borrowed_hydrology, box, opened, blocks);
    terrain_surface_line_caves_box(terrain->borrowed_surface, box, blocks);
    terrain_ore_fill_box(terrain->borrowed_ore, box, blocks);
    terrain_feature_fill_box(terrain->borrowed_feature, box, blocks);
}

void terrain_deinit(terrain_t *terrain) {
    if (terrain->borrowed_feature != nullptr) {
        terrain_feature_deinit(terrain->borrowed_feature);
    }
    if (terrain->borrowed_surface != nullptr) {
        terrain_surface_deinit(terrain->borrowed_surface);
    }
    if (terrain->borrowed_biome != nullptr) {
        terrain_biome_deinit(terrain->borrowed_biome);
    }
    if (terrain->borrowed_ore != nullptr) {
        terrain_ore_deinit(terrain->borrowed_ore);
    }
    if (terrain->borrowed_cave != nullptr) {
        terrain_cave_deinit(terrain->borrowed_cave);
    }
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
