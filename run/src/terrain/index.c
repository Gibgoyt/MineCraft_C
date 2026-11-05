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
    if (!terrain_fields_init(&terrain->borrowed_fields, &terrain->arena_tables, &terrain->noise_bank)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    if (!terrain_density_init(&terrain->borrowed_density, &terrain->arena_tables, &terrain->noise_bank,
                              terrain->borrowed_fields)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    if (!terrain_hydrology_init(&terrain->borrowed_hydrology, &terrain->arena_tables, &terrain->noise_bank,
                                terrain->borrowed_density, terrain->borrowed_fields)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    if (!terrain_cave_init(&terrain->borrowed_cave, &terrain->arena_tables, &terrain->noise_bank,
                           terrain->borrowed_density)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    terrain_ore_init(&terrain->borrowed_ore, &terrain->arena_tables, seed, terrain->borrowed_density);
    terrain_biome_init(&terrain->borrowed_biome, &terrain->arena_tables, terrain->borrowed_fields,
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

/* Every stage, on a box that may reach TERRAIN_MADE_TOP and no further. */
static void terrain_stages_run(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                               uint16_t *blocks) {
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, box, blocks);
    terrain_surface_fill_box(terrain->borrowed_surface, box, blocks);
    terrain_cave_mask_box(terrain->borrowed_cave, box, opened);
    /* Only what a cave truly cuts takes its fill from the flood. Lining reads what caves wanted, which the mask keeps, so it costs no second look at them. */
    terrain_cave_cut_keeping_wanted(box, opened, blocks);
    terrain_hydrology_flood_box(terrain->borrowed_hydrology, box, opened, blocks);
    /* Features ask what these stages left on a chunk's columns. For the chunk in hand it is here, so it costs no second run of them. */
    terrain_feature_chunk_t own;
    const bool own_read = terrain_feature_chunk_read_box(terrain->borrowed_feature, box, blocks, &own);
    terrain_surface_line_caves_with_mask(terrain->borrowed_surface, box, opened, blocks);
    terrain_ore_fill_box(terrain->borrowed_ore, box, blocks);
    if (own_read) {
        terrain_feature_fill_chunk_box(terrain->borrowed_feature, box, &own, blocks);
    } else {
        terrain_feature_fill_box(terrain->borrowed_feature, box, blocks);
    }
}

/* A column is mostly air above what terrain can reach, and the stages walk their layers. So they get the box up to TERRAIN_MADE_TOP, which is the first
 * part of what the caller lent, as z is the slowest way through it, and the rest is air. */
static_assert(BLOCK_ID_AIR == 0u);

void terrain_generate_box(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                          uint16_t *blocks) {
    const int64_t layers_made = (int64_t)TERRAIN_MADE_TOP - (int64_t)box->origin_z + 1;
    if (layers_made >= (int64_t)box->count_z) {
        terrain_stages_run(terrain, box, opened, blocks);
        return;
    }
    const uint64_t layer = (uint64_t)box->count_x * box->count_y;
    const uint64_t made = layers_made > 0 ? (uint64_t)layers_made : 0u;
    if (made > 0u) {
        terrain_density_box_t lower = *box;
        lower.count_z = (uint32_t)made;
        terrain_stages_run(terrain, &lower, opened, blocks);
    }
    const uint64_t rest = ((uint64_t)box->count_z - made) * layer;
    memset(opened + made * layer, 0, rest);
    memset(blocks + made * layer, 0, rest * sizeof(uint16_t));
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
    if (terrain->borrowed_fields != nullptr) {
        terrain_fields_deinit(terrain->borrowed_fields);
    }
    terrain_noise_bank_deinit(&terrain->noise_bank);
    arena_deinit(&terrain->arena_tables);
    heap_release(&terrain->owned_tables);
    memset(terrain, 0, sizeof(*terrain));
}
