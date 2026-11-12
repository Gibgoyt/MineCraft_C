/* See README.md. The Owner: it holds the region, and lends its arena to the parts. */
#include "terrain/index.h"

#include "assert/index.h"
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
    if (!terrain_landform_init(&terrain->borrowed_landform, &terrain->arena_tables, &terrain->noise_bank)) {
        terrain_deinit(terrain); /* it has said why */
        return false;
    }
    if (!terrain_density_init(&terrain->borrowed_density, &terrain->arena_tables, &terrain->noise_bank,
                              terrain->borrowed_fields, terrain->borrowed_landform)) {
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
    terrain_biome_init(&terrain->borrowed_biome, &terrain->arena_tables, seed, terrain->borrowed_fields,
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
static void terrain_stages_run(const terrain_t *terrain, const terrain_climate_sample_t *climate,
                               const terrain_density_box_t *box, uint8_t *opened, uint16_t *blocks) {
    terrain_hydrology_fill_box(terrain->borrowed_hydrology, box, blocks);
    terrain_surface_fill_box(terrain->borrowed_surface, climate, box, blocks);
    terrain_cave_mask_box(terrain->borrowed_cave, box, opened);
    /* Only what a cave truly cuts takes its fill from the flood. Lining reads what caves wanted, which the mask keeps, so it costs no second look at them. */
    terrain_cave_cut_keeping_wanted(box, opened, blocks);
    terrain_hydrology_flood_box(terrain->borrowed_hydrology, box, opened, blocks);
    /* Features ask what these stages left on a chunk's columns. For the chunk in hand it is here, so it costs no second run of them. */
    terrain_feature_chunk_t own;
    const bool own_read = terrain_feature_chunk_read_box(terrain->borrowed_feature, climate, box, blocks, &own);
    terrain_surface_line_caves_with_mask(terrain->borrowed_surface, climate, box, opened, blocks);
    terrain_ore_fill_box(terrain->borrowed_ore, box, blocks);
    if (own_read) {
        terrain_feature_fill_chunk_box(terrain->borrowed_feature, climate, box, &own, blocks);
    } else {
        terrain_feature_fill_box(terrain->borrowed_feature, climate, box, blocks);
    }
}

/* A column is mostly air above what terrain can reach, and the stages walk their layers. So they get the box up to TERRAIN_MADE_TOP, which is the first
 * part of what the caller lent, as z is the slowest way through it, and the rest is air. */
static_assert(BLOCK_ID_AIR == 0u);

void terrain_generate_box(const terrain_t *terrain, const terrain_climate_sample_t *climate,
                          const terrain_density_box_t *box, uint8_t *opened, uint16_t *blocks) {
    ASSERT(climate != nullptr);
    const int64_t layers_made = (int64_t)TERRAIN_MADE_TOP - (int64_t)box->origin_z + 1;
    if (layers_made >= (int64_t)box->count_z) {
        terrain_stages_run(terrain, climate, box, opened, blocks);
        return;
    }
    const uint64_t layer = (uint64_t)box->count_x * box->count_y;
    const uint64_t made = layers_made > 0 ? (uint64_t)layers_made : 0u;
    if (made > 0u) {
        terrain_density_box_t lower = *box;
        lower.count_z = (uint32_t)made;
        terrain_stages_run(terrain, climate, &lower, opened, blocks);
    }
    const uint64_t rest = ((uint64_t)box->count_z - made) * layer;
    memset(opened + made * layer, 0, rest);
    memset(blocks + made * layer, 0, rest * sizeof(uint16_t));
}

void terrain_generate_box_fallback(const terrain_t *terrain, const terrain_density_box_t *box, uint8_t *opened,
                                   uint16_t *blocks) {
    terrain_climate_sample_t climate;
    terrain_climate_fallback(terrain->seed, box->origin_x, box->origin_y, &climate);
    terrain_generate_box(terrain, &climate, box, opened, blocks);
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
    if (terrain->borrowed_landform != nullptr) {
        terrain_landform_deinit(terrain->borrowed_landform);
    }
    if (terrain->borrowed_fields != nullptr) {
        terrain_fields_deinit(terrain->borrowed_fields);
    }
    terrain_noise_bank_deinit(&terrain->noise_bank);
    arena_deinit(&terrain->arena_tables);
    heap_release(&terrain->owned_tables);
    memset(terrain, 0, sizeof(*terrain));
}

/* ---- The height estimate: plan 004 section 6.1 ---------------------------------------------------------------------- */

enum : uint32_t {
    TERRAIN_ESTIMATE_SHIFT = 2u,   /* floor to the lattice, as terrain_density's boxes do */
    TERRAIN_ESTIMATE_PIECE = 128u, /* columns per call of the profile, on the stack */
};
static_assert(TERRAIN_DENSITY_CELL_XY == 1u << TERRAIN_ESTIMATE_SHIFT);

terrain_density_profile_t terrain_height_estimate_profile(const terrain_t *terrain, int32_t x, int32_t y) {
    const int64_t cell = (int64_t)TERRAIN_DENSITY_CELL_XY;
    const int64_t west = ((int64_t)x >> TERRAIN_ESTIMATE_SHIFT) * cell;
    const int64_t south = ((int64_t)y >> TERRAIN_ESTIMATE_SHIFT) * cell;
    const float32_t per_block = 1.0f32 / (float32_t)TERRAIN_DENSITY_CELL_XY;
    const float32_t t_x = (float32_t)(x & (int32_t)(TERRAIN_DENSITY_CELL_XY - 1u)) * per_block;
    const float32_t t_y = (float32_t)(y & (int32_t)(TERRAIN_DENSITY_CELL_XY - 1u)) * per_block;
    /* South-west, south-east, north-west, north-east. */
    const float64_t xs[4] = {(float64_t)west, (float64_t)(west + cell), (float64_t)west, (float64_t)(west + cell)};
    const float64_t ys[4] = {(float64_t)south, (float64_t)south, (float64_t)(south + cell), (float64_t)(south + cell)};
    float32_t h[4];
    float32_t s[4];
    float32_t l[4];
    terrain_density_profile_points_lift(terrain->borrowed_density, xs, ys, 4u, h, s, l);
    /* As density_write_voxels blends, in this order, so the bits are a box's. The lift is blended the same way, so
     * height - lift is relief by the same rule wherever it is read. */
    const float32_t h_near = h[0] + t_x * (h[1] - h[0]);
    const float32_t h_far = h[2] + t_x * (h[3] - h[2]);
    const float32_t s_near = s[0] + t_x * (s[1] - s[0]);
    const float32_t s_far = s[2] + t_x * (s[3] - s[2]);
    const float32_t l_near = l[0] + t_x * (l[1] - l[0]);
    const float32_t l_far = l[2] + t_x * (l[3] - l[2]);
    return (terrain_density_profile_t){
        .height = h_near + t_y * (h_far - h_near),
        .softness = s_near + t_y * (s_far - s_near),
        .lift = l_near + t_y * (l_far - l_near),
    };
}

float32_t terrain_height_estimate(const terrain_t *terrain, int32_t x, int32_t y) {
    return terrain_height_estimate_profile(terrain, x, y).height;
}

float32_t terrain_biome_height_estimate(const terrain_t *terrain, int32_t x, int32_t y) {
    const terrain_density_profile_t profile = terrain_height_estimate_profile(terrain, x, y);
    return terrain_biome_height(profile.height, profile.lift);
}

void terrain_height_estimate_area(const terrain_t *terrain, int32_t origin_x, int32_t origin_y, int32_t step,
                                  uint32_t count_x, uint32_t count_y, float32_t *heights) {
    const int32_t lattice_mask = (int32_t)(TERRAIN_DENSITY_CELL_XY - 1u);
    ASSERT((origin_x & lattice_mask) == 0 && (origin_y & lattice_mask) == 0 && (step & lattice_mask) == 0);
    /* On the lattice t_x = t_y = 0, and a + 0 * (b - a) is a exactly: the profile itself is the blend's answer. */
    float64_t xs[TERRAIN_ESTIMATE_PIECE];
    float64_t ys[TERRAIN_ESTIMATE_PIECE];
    float32_t softness[TERRAIN_ESTIMATE_PIECE];
    uint32_t held = 0u;
    uint64_t done = 0u;
    for (uint32_t iy = 0u; iy < count_y; iy++) {
        const float64_t y = (float64_t)((int64_t)origin_y + (int64_t)step * (int64_t)iy);
        for (uint32_t ix = 0u; ix < count_x; ix++) {
            xs[held] = (float64_t)((int64_t)origin_x + (int64_t)step * (int64_t)ix);
            ys[held] = y;
            held++;
            if (held == TERRAIN_ESTIMATE_PIECE) {
                terrain_density_profile_points(terrain->borrowed_density, xs, ys, held, heights + done, softness);
                done += held;
                held = 0u;
            }
        }
    }
    if (held > 0u) {
        terrain_density_profile_points(terrain->borrowed_density, xs, ys, held, heights + done, softness);
    }
}
