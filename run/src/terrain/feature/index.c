/* See README.md. */
#include "terrain/feature/index.h"

#include "assert/index.h"
#include "machine/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"

#include <string.h>

enum : uint32_t {
    /* Walking down a chunk goes in slices this tall, lined up on the world. */
    FEATURE_SLICE_LAYERS = 16u,
    FEATURE_SLICE_VOXELS = TERRAIN_FEATURE_CHUNK_COLUMNS * FEATURE_SLICE_LAYERS,
    /* The lattice round a chunk: the density's corners, every 4 blocks. */
    FEATURE_LATTICE_STEP = 4u,
    FEATURE_LATTICE_SIDE = TERRAIN_FEATURE_CHUNK / FEATURE_LATTICE_STEP + 1u,
    FEATURE_LATTICE = FEATURE_LATTICE_SIDE * FEATURE_LATTICE_SIDE,
    /* One chunk in this many tries for a lake. */
    FEATURE_LAKE_ONE_IN = 6u,
    /* A lake's ellipses stay at least this far inside the chunk, so all that holds it is in the chunk. */
    FEATURE_LAKE_MARGIN = 1u,
    FEATURE_LAKE_RADIUS_MIN = 3u,
    FEATURE_LAKE_RADIUS_MAX = 6u,
    FEATURE_LAKE_DEPTH_MIN = 2u,
    FEATURE_LAKE_LAYERS = TERRAIN_FEATURE_LAKE_DEPTH_MAX + 1u,
    /* Vines one tree can carry. */
    FEATURE_STRANDS_MAX = 8u,
    /* The deepest water a mangrove stands in, a sea plant asks for and kelp asks for. */
    FEATURE_MANGROVE_WATER = 5u,
    FEATURE_SEAGRASS_WATER = 2u,
    FEATURE_KELP_WATER = 3u,
    FEATURE_KELP_TALLEST = 12u,
};
static_assert(FEATURE_LAKE_RADIUS_MAX * 2u + 1u + 2u * FEATURE_LAKE_MARGIN <= TERRAIN_FEATURE_CHUNK);

enum : int32_t {
    /* What the density can do past a lattice corner's height and softness: the surface's own number, and one for rounding. */
    FEATURE_LATTICE_SLACK = 10,
    /* Below a column's ground, how far anything here can write: a lake's bed and the foot of a spike. */
    FEATURE_BELOW = (int32_t)TERRAIN_FEATURE_LAKE_DEPTH_MAX + 4,
};

/* What a site asks of its column and of what it can be next to. Rows with sites only. */
typedef struct feature_margin_t {
    uint8_t low;  /* 0: sites keep this far from the chunk's low edges */
    uint8_t high; /* 1: and its high edges */
} feature_margin_t;
static_assert(sizeof(feature_margin_t) == 2);

/* A row's margins: what it looks at or stands on next to its site stays inside its chunk. */
static feature_margin_t feature_margin_of(uint32_t row) {
    switch (row) {
    case BIOME_FEATURE_TREE_JUNGLE:
    case BIOME_FEATURE_TREE_DARK_OAK:
        return (feature_margin_t){.low = 0u, .high = 1u}; /* a wide trunk */
    case BIOME_FEATURE_TREE_MANGROVE:
    case BIOME_FEATURE_CACTUS:
    case BIOME_FEATURE_SUGAR_CANE:
        return (feature_margin_t){.low = 1u, .high = 1u}; /* roots, or what is beside it */
    case TERRAIN_FEATURE_ROW_SPIKE:
        return (feature_margin_t){.low = 2u, .high = 2u}; /* its whole foot */
    default:
        return (feature_margin_t){.low = 0u, .high = 0u};
    }
}

/* ---- What may go over what ---------------------------------------------------------------------------------------- */

typedef enum feature_class_t : uint8_t {
    FEATURE_CLASS_SOLID = 0, /* nothing here replaces it. Logs are, so that the first tree keeps its trunk */
    FEATURE_CLASS_AIR = 1,
    FEATURE_CLASS_WATER = 2,
    FEATURE_CLASS_PLANT = 3,
    FEATURE_CLASS_VINE = 4,
    FEATURE_CLASS_LEAF = 5,
} feature_class_t;

static const uint8_t FEATURE_CLASSES[BLOCK_COUNT] = {
    [BLOCK_ID_AIR] = FEATURE_CLASS_AIR,
    [BLOCK_ID_WATER] = FEATURE_CLASS_WATER,
    [BLOCK_ID_TALL_GRASS] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_FERN] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_DANDELION] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_ROSE] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_ALLIUM] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_AZURE_BLUET] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_BLUE_ORCHID] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_TULIP] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_OXEYE_DAISY] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_CORNFLOWER] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_LILY_OF_THE_VALLEY] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_CACTUS] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_SUGAR_CANE] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_DEAD_BUSH] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_BROWN_MUSHROOM] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_RED_MUSHROOM] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_LILY_PAD] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_MELON] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_PUMPKIN] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_SEAGRASS] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_KELP] = FEATURE_CLASS_PLANT,
    [BLOCK_ID_VINE] = FEATURE_CLASS_VINE,
    [BLOCK_ID_OAK_LEAVES] = FEATURE_CLASS_LEAF,
    [BLOCK_ID_BIRCH_LEAVES] = FEATURE_CLASS_LEAF,
    [BLOCK_ID_SPRUCE_LEAVES] = FEATURE_CLASS_LEAF,
    [BLOCK_ID_JUNGLE_LEAVES] = FEATURE_CLASS_LEAF,
    [BLOCK_ID_ACACIA_LEAVES] = FEATURE_CLASS_LEAF,
    [BLOCK_ID_DARK_OAK_LEAVES] = FEATURE_CLASS_LEAF,
    [BLOCK_ID_MANGROVE_LEAVES] = FEATURE_CLASS_LEAF,
    [BLOCK_ID_CHERRY_LEAVES] = FEATURE_CLASS_LEAF,
};

typedef enum feature_put_t : uint8_t {
    FEATURE_PUT_LOG = 0,   /* over all but solid ground and other logs: water too, for a mangrove */
    FEATURE_PUT_LEAF = 1,  /* over air, plants and vines */
    FEATURE_PUT_VINE = 2,  /* over air */
    FEATURE_PUT_PLANT = 3, /* over air */
    FEATURE_PUT_WATER = 4, /* a sea plant: over water */
} feature_put_t;

static void feature_put(const terrain_feature_canvas_t *canvas, int32_t x, int32_t y, int32_t z, uint16_t block,
                        feature_put_t mode) {
    if (x < canvas->x_low || x > canvas->x_high || y < canvas->y_low || y > canvas->y_high || z < canvas->z_low ||
        z > canvas->z_high) {
        return;
    }
    const terrain_density_box_t *const box = canvas->borrowed_box;
    const uint64_t at = (uint64_t)(x - box->origin_x) +
                        (uint64_t)box->count_x *
                            ((uint64_t)(y - box->origin_y) + (uint64_t)box->count_y * (uint64_t)(z - box->origin_z));
    uint16_t *const voxel = &canvas->borrowed_blocks[at];
    const uint8_t there = FEATURE_CLASSES[*voxel];
    bool takes = false;
    switch (mode) {
    case FEATURE_PUT_LOG:
        takes = there != FEATURE_CLASS_SOLID;
        break;
    case FEATURE_PUT_LEAF:
        takes = there == FEATURE_CLASS_AIR || there == FEATURE_CLASS_PLANT || there == FEATURE_CLASS_VINE;
        break;
    case FEATURE_PUT_VINE:
    case FEATURE_PUT_PLANT:
        takes = there == FEATURE_CLASS_AIR;
        break;
    case FEATURE_PUT_WATER:
        takes = there == FEATURE_CLASS_WATER;
        break;
    }
    if (takes) {
        *voxel = block;
    }
}

terrain_feature_canvas_t terrain_feature_canvas_of(const terrain_density_box_t *box, uint16_t *blocks) {
    const int32_t z_high = box->origin_z + (int32_t)box->count_z - 1;
    return (terrain_feature_canvas_t){
        .borrowed_box = box,
        .borrowed_blocks = blocks,
        .x_low = box->origin_x,
        .x_high = box->origin_x + (int32_t)box->count_x - 1,
        .y_low = box->origin_y,
        .y_high = box->origin_y + (int32_t)box->count_y - 1,
        .z_low = box->origin_z > MC_WORLD_Z_MIN ? box->origin_z : MC_WORLD_Z_MIN,
        .z_high = z_high < MC_WORLD_Z_MAX ? z_high : MC_WORLD_Z_MAX,
    };
}

/* ---- Trees -------------------------------------------------------------------------------------------------------- */

static const int32_t FEATURE_TREE_REACH[TERRAIN_FEATURE_TREES] = {
    [TERRAIN_FEATURE_TREE_OAK] = 2,      [TERRAIN_FEATURE_TREE_BIRCH] = 2,  [TERRAIN_FEATURE_TREE_SPRUCE] = 3,
    [TERRAIN_FEATURE_TREE_JUNGLE] = 7,   [TERRAIN_FEATURE_TREE_ACACIA] = 5, [TERRAIN_FEATURE_TREE_DARK_OAK] = 4,
    [TERRAIN_FEATURE_TREE_MANGROVE] = 3, [TERRAIN_FEATURE_TREE_CHERRY] = 6,
};

int32_t terrain_feature_tree_reach(terrain_feature_tree_t kind) {
    ASSERT_ALWAYS((uint32_t)kind < TERRAIN_FEATURE_TREES);
    return FEATURE_TREE_REACH[kind];
}

bool terrain_feature_tree_is_wide(terrain_feature_tree_t kind, uint64_t seed) {
    switch (kind) {
    case TERRAIN_FEATURE_TREE_DARK_OAK:
        return true;
    case TERRAIN_FEATURE_TREE_JUNGLE:
        return (terrain_rng_mix(seed) & 3u) != 0u; /* the giants, 3 in 4 */
    default:
        return false;
    }
}

/* One log upon another, both ends included. */
static void feature_logs(const terrain_feature_canvas_t *canvas, int32_t x, int32_t y, int32_t z_first, int32_t z_last,
                         uint16_t block) {
    for (int32_t z = z_first; z <= z_last; z++) {
        feature_put(canvas, x, y, z, block, FEATURE_PUT_LOG);
    }
}

/* Leaves in a rounded blob. A wide blob is centred between x and x + 1, so it is the same round a wide trunk. Its outer shell is
 * bitten here and there, from the tree's seed. */
static void feature_blob(const terrain_feature_canvas_t *canvas, int32_t x, int32_t y, int32_t z, int32_t across,
                         int32_t tall, bool wide, uint16_t block, uint64_t seed) {
    const int32_t half = wide ? 1 : 0;
    const int32_t limit = 4 * across * across * tall * tall;
    const int32_t shell = 4 * (across - 1) * (across - 1) * tall * tall;
    for (int32_t dz = -tall; dz <= tall; dz++) {
        for (int32_t dy = -across; dy <= across + half; dy++) {
            for (int32_t dx = -across; dx <= across + half; dx++) {
                const int32_t ax = 2 * dx - half;
                const int32_t ay = 2 * dy - half;
                const int32_t distance = (ax * ax + ay * ay) * tall * tall + 4 * dz * dz * across * across;
                if (distance > limit) {
                    continue;
                }
                if (distance > shell && (terrain_rng_hash3(seed, dx, dy, dz) & 3u) == 0u) {
                    continue;
                }
                feature_put(canvas, x + dx, y + dy, z + dz, block, FEATURE_PUT_LEAF);
            }
        }
    }
}

/* A flat layer of leaves, its corners bitten from the tree's seed or always when sharp is false. */
static void feature_layer(const terrain_feature_canvas_t *canvas, int32_t x, int32_t y, int32_t z, int32_t across,
                          bool sharp, uint16_t block, uint64_t seed) {
    for (int32_t dy = -across; dy <= across; dy++) {
        for (int32_t dx = -across; dx <= across; dx++) {
            const bool corner = (dx == -across || dx == across) && (dy == -across || dy == across);
            if (corner && (!sharp || (terrain_rng_hash3(seed, dx, dy, z) & 1u) == 0u)) {
                continue;
            }
            feature_put(canvas, x + dx, y + dy, z, block, FEATURE_PUT_LEAF);
        }
    }
}

/* The oak's plan, which the birch and a small jungle tree share: a trunk, two wide layers and two narrow. */
static void feature_tree_plain(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                               uint32_t trunk, uint16_t log, uint16_t leaf) {
    const int32_t top = site->z + (int32_t)trunk;
    feature_logs(canvas, site->x, site->y, site->z, top - 1, log);
    feature_layer(canvas, site->x, site->y, top - 3, 2, true, leaf, site->seed);
    feature_layer(canvas, site->x, site->y, top - 2, 2, true, leaf, site->seed);
    feature_layer(canvas, site->x, site->y, top - 1, 1, true, leaf, site->seed);
    feature_layer(canvas, site->x, site->y, top, 1, false, leaf, site->seed);
}

/* A cone: layers widening from the tip, narrowing by one every other layer, for the look of boughs. */
static void feature_tree_spruce(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                                uint32_t trunk) {
    const int32_t top = site->z + (int32_t)trunk;
    feature_logs(canvas, site->x, site->y, site->z, top - 1, BLOCK_ID_SPRUCE_LOG);
    const int32_t skirt = 2 + (int32_t)(terrain_rng_mix(site->seed) & 1u);
    for (int32_t z = top; z >= site->z + skirt; z--) {
        const int32_t down = top - z;
        const int32_t widest = 1 + down * 2 / ((int32_t)trunk - skirt);
        const int32_t across = (down & 1) == 0 ? widest - 1 : widest;
        for (int32_t dy = -across; dy <= across; dy++) {
            for (int32_t dx = -across; dx <= across; dx++) {
                if (dx * dx + dy * dy <= across * across + 1) {
                    feature_put(canvas, site->x + dx, site->y + dy, z, BLOCK_ID_SPRUCE_LEAVES, FEATURE_PUT_LEAF);
                }
            }
        }
    }
}

/* A wide trunk from the ground each of its columns has. */
static void feature_wide_trunk(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                               int32_t last, uint16_t log) {
    feature_logs(canvas, site->x, site->y, site->z, last, log);
    feature_logs(canvas, site->x + 1, site->y, (int32_t)site->foot[0] + 1, last, log);
    feature_logs(canvas, site->x, site->y + 1, (int32_t)site->foot[1] + 1, last, log);
    feature_logs(canvas, site->x + 1, site->y + 1, (int32_t)site->foot[2] + 1, last, log);
}

static void feature_tree_jungle(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                                terrain_rng_t *rng) {
    if (!terrain_feature_tree_is_wide(TERRAIN_FEATURE_TREE_JUNGLE, site->seed)) {
        feature_tree_plain(canvas, site, 5u + terrain_rng_next_below(rng, 4u), BLOCK_ID_JUNGLE_LOG,
                           BLOCK_ID_JUNGLE_LEAVES);
        return;
    }
    const int32_t trunk = 12 + (int32_t)terrain_rng_next_below(rng, 10u);
    const int32_t top = site->z + trunk;
    feature_wide_trunk(canvas, site, top - 1, BLOCK_ID_JUNGLE_LOG);
    feature_blob(canvas, site->x, site->y, top, 5, 3, true, BLOCK_ID_JUNGLE_LEAVES, site->seed);
    const uint32_t branches = 2u + terrain_rng_next_below(rng, 2u);
    for (uint32_t b = 0u; b < branches; b++) {
        const int32_t dx = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
        const int32_t dy = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
        const int32_t length = 2 + (int32_t)terrain_rng_next_below(rng, 2u);
        const int32_t from = site->z + trunk / 2 + (int32_t)terrain_rng_next_below(rng, (uint32_t)(trunk / 2 - 3));
        const int32_t sx = dx > 0 ? site->x + 1 : site->x;
        const int32_t sy = dy > 0 ? site->y + 1 : site->y;
        for (int32_t i = 1; i <= length; i++) {
            feature_put(canvas, sx + dx * i, sy + dy * i, from + i, BLOCK_ID_JUNGLE_LOG, FEATURE_PUT_LOG);
        }
        feature_blob(canvas, sx + dx * length, sy + dy * length, from + length + 1, 2, 1, false, BLOCK_ID_JUNGLE_LEAVES,
                     site->seed + b + 1u);
    }
}

/* A trunk that leans, and a flat top where it stops. */
static void feature_tree_acacia(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                                terrain_rng_t *rng) {
    const int32_t straight = 3 + (int32_t)terrain_rng_next_below(rng, 3u);
    const int32_t lean = 1 + (int32_t)terrain_rng_next_below(rng, 2u);
    const int32_t dx = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
    const int32_t dy = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
    feature_logs(canvas, site->x, site->y, site->z, site->z + straight - 1, BLOCK_ID_ACACIA_LOG);
    for (int32_t i = 1; i <= lean; i++) {
        feature_put(canvas, site->x + dx * i, site->y + dy * i, site->z + straight - 1 + i, BLOCK_ID_ACACIA_LOG,
                    FEATURE_PUT_LOG);
    }
    const int32_t x = site->x + dx * lean;
    const int32_t y = site->y + dy * lean;
    const int32_t z = site->z + straight + lean;
    feature_layer(canvas, x, y, z, 3, false, BLOCK_ID_ACACIA_LEAVES, site->seed);
    feature_layer(canvas, x, y, z + 1, 2, true, BLOCK_ID_ACACIA_LEAVES, site->seed);
}

static void feature_tree_dark_oak(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                                  terrain_rng_t *rng) {
    const int32_t trunk = 6 + (int32_t)terrain_rng_next_below(rng, 4u);
    const int32_t top = site->z + trunk;
    feature_wide_trunk(canvas, site, top - 1, BLOCK_ID_DARK_OAK_LOG);
    feature_blob(canvas, site->x, site->y, top - 1, 3, 2, true, BLOCK_ID_DARK_OAK_LEAVES, site->seed);
}

/* Stands in water up to its knees, on roots that arc from the trunk down to the ground beside it. */
static void feature_tree_mangrove(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                                  terrain_rng_t *rng) {
    static const int32_t SIDE_X[4] = {1, -1, 0, 0};
    static const int32_t SIDE_Y[4] = {0, 0, 1, -1};
    const int32_t trunk = 5 + (int32_t)terrain_rng_next_below(rng, 4u);
    const int32_t above = site->z + (int32_t)site->lift;
    const int32_t top = above + trunk;
    feature_logs(canvas, site->x, site->y, site->z, top - 1, BLOCK_ID_MANGROVE_LOG);
    for (uint32_t side = 0u; side < 4u; side++) {
        if (site->foot[side] == TERRAIN_FEATURE_NO_GROUND || terrain_rng_next_below(rng, 4u) == 0u) {
            continue;
        }
        feature_logs(canvas, site->x + SIDE_X[side], site->y + SIDE_Y[side], (int32_t)site->foot[side] + 1, above + 1,
                     BLOCK_ID_MANGROVE_LOG);
    }
    feature_blob(canvas, site->x, site->y, top, 3, 2, false, BLOCK_ID_MANGROVE_LEAVES, site->seed);
}

/* Two boughs going opposite ways, each with a wide pink cloud on it. */
static void feature_tree_cherry(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                                terrain_rng_t *rng) {
    const int32_t trunk = 4 + (int32_t)terrain_rng_next_below(rng, 3u);
    const int32_t top = site->z + trunk;
    feature_logs(canvas, site->x, site->y, site->z, top - 1, BLOCK_ID_CHERRY_LOG);
    const int32_t dx = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
    const int32_t dy = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
    for (int32_t bough = 0; bough < 2; bough++) {
        const int32_t sign = bough == 0 ? 1 : -1;
        const int32_t length = 2 + (int32_t)terrain_rng_next_below(rng, 2u);
        for (int32_t i = 1; i <= length; i++) {
            feature_put(canvas, site->x + sign * dx * i, site->y + sign * dy * i, top - 1 + i, BLOCK_ID_CHERRY_LOG,
                        FEATURE_PUT_LOG);
        }
        feature_blob(canvas, site->x + sign * dx * length, site->y + sign * dy * length, top + length, 3, 2, false,
                     BLOCK_ID_CHERRY_LEAVES, site->seed + (uint64_t)bough + 1u);
    }
}

/* Strands up the trunk: on a side of it, from a height down to where it runs out. */
static void feature_vines(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site,
                          terrain_rng_t *rng) {
    const int32_t wide = terrain_feature_tree_is_wide((terrain_feature_tree_t)site->kind, site->seed) ? 1 : 0;
    for (uint32_t i = 0u; i < site->strands; i++) {
        const uint32_t side = terrain_rng_next_below(rng, 4u);
        const int32_t start = 1 + (int32_t)terrain_rng_next_below(rng, 6u);
        const int32_t length = 2 + (int32_t)terrain_rng_next_below(rng, 5u);
        const int32_t x = side == 0u ? site->x + 1 + wide : (side == 1u ? site->x - 1 : site->x);
        const int32_t y = side == 2u ? site->y + 1 + wide : (side == 3u ? site->y - 1 : site->y);
        for (int32_t k = 0; k < length && start - k >= 1; k++) {
            feature_put(canvas, x, y, site->z + (int32_t)site->lift + start - k, BLOCK_ID_VINE, FEATURE_PUT_VINE);
        }
    }
}

void terrain_feature_paint_tree(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site) {
    ASSERT_ALWAYS((uint32_t)site->kind < TERRAIN_FEATURE_TREES);
    terrain_rng_t rng = terrain_rng_start(site->seed);
    switch ((terrain_feature_tree_t)site->kind) {
    case TERRAIN_FEATURE_TREE_OAK:
        feature_tree_plain(canvas, site, 4u + terrain_rng_next_below(&rng, 3u), BLOCK_ID_OAK_LOG, BLOCK_ID_OAK_LEAVES);
        break;
    case TERRAIN_FEATURE_TREE_BIRCH:
        feature_tree_plain(canvas, site, 5u + terrain_rng_next_below(&rng, 3u), BLOCK_ID_BIRCH_LOG,
                           BLOCK_ID_BIRCH_LEAVES);
        break;
    case TERRAIN_FEATURE_TREE_SPRUCE:
        feature_tree_spruce(canvas, site, 6u + terrain_rng_next_below(&rng, 5u));
        break;
    case TERRAIN_FEATURE_TREE_JUNGLE:
        feature_tree_jungle(canvas, site, &rng);
        break;
    case TERRAIN_FEATURE_TREE_ACACIA:
        feature_tree_acacia(canvas, site, &rng);
        break;
    case TERRAIN_FEATURE_TREE_DARK_OAK:
        feature_tree_dark_oak(canvas, site, &rng);
        break;
    case TERRAIN_FEATURE_TREE_MANGROVE:
        feature_tree_mangrove(canvas, site, &rng);
        break;
    case TERRAIN_FEATURE_TREE_CHERRY:
        feature_tree_cherry(canvas, site, &rng);
        break;
    }
    feature_vines(canvas, site, &rng);
}

/* ---- What a chunk has on it --------------------------------------------------------------------------------------- */

/* Every stage whose blocks say what a site stands on: see the README's order. Lining and ore change only rock. */
[[gnu::noinline]]
static void feature_stages(const terrain_feature_t *feature, terrain_surface_tile_t *tile,
                           const terrain_density_box_t *box, uint8_t *opened, uint16_t *blocks) {
    terrain_hydrology_fill_box(feature->borrowed_hydrology, box, blocks);
    terrain_surface_fill_tile_box(feature->borrowed_surface, tile, box, blocks);
    terrain_cave_mask_box(feature->borrowed_cave, box, opened);
    terrain_cave_cut_with_mask(box, opened, blocks);
    terrain_hydrology_flood_box(feature->borrowed_hydrology, box, opened, blocks);
}

/* The biome of each column, and from the density's own corners, what z the chunk's ground can be between. */
[[gnu::noinline]]
static void feature_chunk_load(const terrain_feature_t *feature, int32_t chunk_x, int32_t chunk_y,
                               terrain_feature_chunk_t *chunk) {
    terrain_biome_column_t columns[TERRAIN_FEATURE_CHUNK_COLUMNS];
    const terrain_climate_area_t area = {
        .origin_x = chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
        .origin_y = chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK,
        .step_x = 1,
        .step_y = 1,
        .count_x = TERRAIN_FEATURE_CHUNK,
        .count_y = TERRAIN_FEATURE_CHUNK,
    };
    terrain_biome_columns_area(feature->borrowed_biome, &area, columns);

    terrain_hydrology_column_t lattice[FEATURE_LATTICE];
    const terrain_climate_area_t around = {
        .origin_x = area.origin_x,
        .origin_y = area.origin_y,
        .step_x = (int32_t)FEATURE_LATTICE_STEP,
        .step_y = (int32_t)FEATURE_LATTICE_STEP,
        .count_x = FEATURE_LATTICE_SIDE,
        .count_y = FEATURE_LATTICE_SIDE,
    };
    terrain_hydrology_columns_area(feature->borrowed_hydrology, &around, lattice);
    float32_t low = lattice[0].height - lattice[0].softness;
    float32_t high = lattice[0].height + lattice[0].softness;
    for (uint32_t i = 1u; i < FEATURE_LATTICE; i++) {
        const float32_t a = lattice[i].height - lattice[i].softness;
        const float32_t b = lattice[i].height + lattice[i].softness;
        low = a < low ? a : low;
        high = b > high ? b : high;
    }
    for (uint32_t i = 0u; i < TERRAIN_FEATURE_CHUNK_COLUMNS; i++) {
        low = columns[i].carve_top < low ? columns[i].carve_top : low;
        chunk->columns[i] = (terrain_feature_column_t){
            .top = TERRAIN_FEATURE_NO_GROUND,
            .water = 0u,
            .block = BLOCK_ID_AIR,
            .biome = columns[i].biome,
        };
    }
    const int32_t z_low = (int32_t)low - FEATURE_LATTICE_SLACK;
    const int32_t z_high = (int32_t)high + FEATURE_LATTICE_SLACK;
    chunk->lake = (terrain_feature_lake_t){0};
    chunk->chunk_x = chunk_x;
    chunk->chunk_y = chunk_y;
    chunk->z_low = z_low > MC_WORLD_Z_MIN ? z_low : MC_WORLD_Z_MIN;
    chunk->z_high = z_high < MC_WORLD_Z_MAX ? z_high : MC_WORLD_Z_MAX;
    chunk->walked = 0u;
    chunk->tops_known = 0u;
    memset(chunk->_pad0, 0, sizeof(chunk->_pad0));
}

/* From z_last down to z_first in voxels laid out as a chunk's, starting with z_first: what each column still looking finds there.
 * Returns how many are still looking. */
static uint32_t feature_columns_read(terrain_feature_chunk_t *chunk, const uint16_t *blocks, int32_t z_first,
                                     int32_t z_last, uint32_t looking) {
    for (uint32_t i = 0u; i < TERRAIN_FEATURE_CHUNK_COLUMNS; i++) {
        terrain_feature_column_t *const column = &chunk->columns[i];
        if (column->top != TERRAIN_FEATURE_NO_GROUND) {
            continue;
        }
        for (int32_t z = z_last; z >= z_first; z--) {
            const uint16_t block = blocks[i + TERRAIN_FEATURE_CHUNK_COLUMNS * (uint32_t)(z - z_first)];
            if (block == BLOCK_ID_AIR) {
                column->water = 0u;
            } else if (block == BLOCK_ID_WATER) {
                column->water++;
            } else {
                column->top = (int16_t)z;
                column->block = block;
                looking--;
                break;
            }
        }
    }
    return looking;
}

/* Over a chunk's range, up to the sea, there is only water. */
static void feature_columns_start(terrain_feature_chunk_t *chunk) {
    const uint16_t water = chunk->z_high < MC_WORLD_Z_SEA ? (uint16_t)(MC_WORLD_Z_SEA - chunk->z_high) : 0u;
    for (uint32_t i = 0u; i < TERRAIN_FEATURE_CHUNK_COLUMNS; i++) {
        chunk->columns[i].water = water;
    }
}

/* A column with nothing in the chunk's range has had its ground cut away by a cave, and grows nothing. */
static void feature_columns_finish(terrain_feature_chunk_t *chunk, uint32_t looking) {
    if (looking == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < TERRAIN_FEATURE_CHUNK_COLUMNS; i++) {
        if (chunk->columns[i].top == TERRAIN_FEATURE_NO_GROUND) {
            chunk->columns[i].water = 0u;
        }
    }
}

/* One slice of the walk down. Returns how many columns are still looking. */
[[gnu::noinline]]
static uint32_t feature_walk_slice(const terrain_feature_t *feature, terrain_surface_tile_t *tile,
                                   terrain_feature_chunk_t *chunk, int32_t z_first, int32_t z_last, uint32_t looking) {
    uint16_t blocks[FEATURE_SLICE_VOXELS];
    uint8_t opened[FEATURE_SLICE_VOXELS];
    const terrain_density_box_t box = {
        .origin_x = chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
        .origin_y = chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK,
        .origin_z = z_first,
        .count_x = TERRAIN_FEATURE_CHUNK,
        .count_y = TERRAIN_FEATURE_CHUNK,
        .count_z = (uint32_t)(z_last - z_first) + 1u,
    };
    feature_stages(feature, tile, &box, opened, blocks);
    return feature_columns_read(chunk, blocks, z_first, z_last, looking);
}

/* From over all the ground, down to under all that can be its top: each column's top and the water on it. */
static void feature_chunk_columns_walk(const terrain_feature_t *feature, terrain_surface_tile_t *tile,
                                       terrain_feature_chunk_t *chunk) {
    feature_columns_start(chunk);
    uint32_t looking = TERRAIN_FEATURE_CHUNK_COLUMNS;
    for (int32_t z = chunk->z_high; z >= chunk->z_low && looking > 0u;) {
        const int32_t slice_low = (z >> 4) * (int32_t)FEATURE_SLICE_LAYERS;
        const int32_t z_first = slice_low > chunk->z_low ? slice_low : chunk->z_low;
        looking = feature_walk_slice(feature, tile, chunk, z_first, z, looking);
        z = z_first - 1;
    }
    feature_columns_finish(chunk, looking);
}

/* ---- Lakes -------------------------------------------------------------------------------------------------------- */

uint32_t terrain_feature_lake_depth(const terrain_feature_lake_t *lake, uint32_t x, uint32_t y) {
    if (lake->exists == 0u) {
        return 0u;
    }
    uint32_t deepest = 0u;
    for (uint32_t e = 0u; e < 2u; e++) {
        const int32_t dx = (int32_t)x - (int32_t)lake->centres[2u * e];
        const int32_t dy = (int32_t)y - (int32_t)lake->centres[2u * e + 1u];
        const int32_t rx = (int32_t)lake->radii[2u * e];
        const int32_t ry = (int32_t)lake->radii[2u * e + 1u];
        const int32_t whole = rx * rx * ry * ry;
        const int32_t reached = dx * dx * ry * ry + dy * dy * rx * rx;
        if (reached < whole) {
            const uint32_t depth = 1u + (uint32_t)(((int32_t)lake->depth - 1) * (whole - reached) / whole);
            deepest = depth > deepest ? depth : deepest;
        }
    }
    return deepest;
}

/* A lake's plan, which comes from where it is and nothing else. */
static bool feature_lake_plan(const terrain_feature_t *feature, int32_t chunk_x, int32_t chunk_y,
                              terrain_feature_lake_t *lake) {
    terrain_rng_t rng = terrain_rng_start(terrain_rng_hash2(feature->lake_seed, chunk_x, chunk_y));
    if (terrain_rng_next_below(&rng, FEATURE_LAKE_ONE_IN) != 0u) {
        return false;
    }
    const uint32_t span = FEATURE_LAKE_RADIUS_MAX - FEATURE_LAKE_RADIUS_MIN + 1u;
    const uint32_t rx = FEATURE_LAKE_RADIUS_MIN + terrain_rng_next_below(&rng, span);
    const uint32_t ry = FEATURE_LAKE_RADIUS_MIN + terrain_rng_next_below(&rng, span);
    const uint32_t cx = FEATURE_LAKE_MARGIN + rx +
                        terrain_rng_next_below(&rng, TERRAIN_FEATURE_CHUNK - 2u * (FEATURE_LAKE_MARGIN + rx));
    const uint32_t cy = FEATURE_LAKE_MARGIN + ry +
                        terrain_rng_next_below(&rng, TERRAIN_FEATURE_CHUNK - 2u * (FEATURE_LAKE_MARGIN + ry));
    const uint32_t small_rx = rx - 1u;
    const uint32_t small_ry = ry - 1u;
    const uint32_t small_x_low = FEATURE_LAKE_MARGIN + small_rx;
    const uint32_t small_y_low = FEATURE_LAKE_MARGIN + small_ry;
    uint32_t small_x = cx + terrain_rng_next_below(&rng, 5u);
    uint32_t small_y = cy + terrain_rng_next_below(&rng, 5u);
    small_x = small_x >= 2u ? small_x - 2u : 0u;
    small_y = small_y >= 2u ? small_y - 2u : 0u;
    small_x = small_x < small_x_low ? small_x_low : small_x;
    small_y = small_y < small_y_low ? small_y_low : small_y;
    small_x = small_x > TERRAIN_FEATURE_CHUNK - 1u - small_x_low ? TERRAIN_FEATURE_CHUNK - 1u - small_x_low : small_x;
    small_y = small_y > TERRAIN_FEATURE_CHUNK - 1u - small_y_low ? TERRAIN_FEATURE_CHUNK - 1u - small_y_low : small_y;
    *lake = (terrain_feature_lake_t){
        .exists = 1u,
        .depth = (uint8_t)(FEATURE_LAKE_DEPTH_MIN +
                           terrain_rng_next_below(&rng, TERRAIN_FEATURE_LAKE_DEPTH_MAX - FEATURE_LAKE_DEPTH_MIN + 1u)),
        .centres = {(uint8_t)cx, (uint8_t)cy, (uint8_t)small_x, (uint8_t)small_y},
        .radii = {(uint8_t)rx, (uint8_t)ry, (uint8_t)small_rx, (uint8_t)small_ry},
    };
    return true;
}

/* The lowest voxel of water in a column of a lake: where its bed is cut to, or where the ground is, if that is lower. */
static int32_t feature_lake_bottom(const terrain_feature_lake_t *lake, int32_t top, uint32_t depth) {
    const int32_t cut = (int32_t)lake->level - (int32_t)depth + 1;
    return top + 1 < cut ? top + 1 : cut;
}

static bool feature_is_ground(uint16_t block) {
    return block != BLOCK_ID_AIR && block != BLOCK_ID_WATER && block != BLOCK_ID_LAVA;
}

/* Does each voxel of water have ground or more of the lake on every side and below? From the real voxels, as a cave can pass
 * under a shore without changing any column's top. */
[[gnu::noinline]]
static bool feature_lake_holds(const terrain_feature_t *feature, terrain_surface_tile_t *tile,
                               const terrain_feature_chunk_t *chunk, const terrain_feature_lake_t *lake) {
    uint16_t blocks[TERRAIN_FEATURE_CHUNK_COLUMNS * FEATURE_LAKE_LAYERS];
    uint8_t opened[TERRAIN_FEATURE_CHUNK_COLUMNS * FEATURE_LAKE_LAYERS];
    const int32_t z_low = (int32_t)lake->level - (int32_t)lake->depth;
    const terrain_density_box_t box = {
        .origin_x = chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
        .origin_y = chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK,
        .origin_z = z_low,
        .count_x = TERRAIN_FEATURE_CHUNK,
        .count_y = TERRAIN_FEATURE_CHUNK,
        .count_z = (uint32_t)lake->depth + 1u,
    };
    feature_stages(feature, tile, &box, opened, blocks);
    static const int32_t SIDE_X[4] = {1, -1, 0, 0};
    static const int32_t SIDE_Y[4] = {0, 0, 1, -1};
    for (uint32_t y = 0u; y < TERRAIN_FEATURE_CHUNK; y++) {
        for (uint32_t x = 0u; x < TERRAIN_FEATURE_CHUNK; x++) {
            const uint32_t depth = terrain_feature_lake_depth(lake, x, y);
            if (depth == 0u) {
                continue;
            }
            const uint32_t at = x + TERRAIN_FEATURE_CHUNK * y;
            const int32_t bottom = feature_lake_bottom(lake, (int32_t)chunk->columns[at].top, depth);
            const uint32_t under = at + TERRAIN_FEATURE_CHUNK_COLUMNS * (uint32_t)(bottom - 1 - z_low);
            if (!feature_is_ground(blocks[under])) {
                return false;
            }
            for (int32_t z = bottom; z <= (int32_t)lake->level; z++) {
                for (uint32_t side = 0u; side < 4u; side++) {
                    const uint32_t nx = (uint32_t)((int32_t)x + SIDE_X[side]);
                    const uint32_t ny = (uint32_t)((int32_t)y + SIDE_Y[side]);
                    const uint32_t next = nx + TERRAIN_FEATURE_CHUNK * ny;
                    const uint32_t next_depth = terrain_feature_lake_depth(lake, nx, ny);
                    if (next_depth > 0u &&
                        z >= feature_lake_bottom(lake, (int32_t)chunk->columns[next].top, next_depth)) {
                        continue;
                    }
                    if (!feature_is_ground(blocks[next + TERRAIN_FEATURE_CHUNK_COLUMNS * (uint32_t)(z - z_low)])) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

/* What the columns say first, as it costs nothing: the lake's level is the lowest of its shore, so no column of the shore can
 * let it out, and a lake needs dry land that is all near that height. A chunk whose tops came from its box has read no tile, so
 * tile is nullptr, and it is read here, for the few that get this far. */
static void feature_chunk_lake_find(const terrain_feature_t *feature, terrain_surface_tile_t *tile,
                                    terrain_feature_chunk_t *chunk) {
    terrain_feature_lake_t lake;
    if (!feature_lake_plan(feature, chunk->chunk_x, chunk->chunk_y, &lake)) {
        return;
    }
    int32_t level = INT32_MAX;
    for (uint32_t y = 0u; y < TERRAIN_FEATURE_CHUNK; y++) {
        for (uint32_t x = 0u; x < TERRAIN_FEATURE_CHUNK; x++) {
            const terrain_feature_column_t *const column = &chunk->columns[x + TERRAIN_FEATURE_CHUNK * y];
            const bool inside = terrain_feature_lake_depth(&lake, x, y) > 0u;
            const bool edge = x == 0u || y == 0u || x == TERRAIN_FEATURE_CHUNK - 1u || y == TERRAIN_FEATURE_CHUNK - 1u;
            const bool shore = !inside && !edge &&
                               (terrain_feature_lake_depth(&lake, x + 1u, y) > 0u ||
                                terrain_feature_lake_depth(&lake, x - 1u, y) > 0u ||
                                terrain_feature_lake_depth(&lake, x, y + 1u) > 0u ||
                                terrain_feature_lake_depth(&lake, x, y - 1u) > 0u);
            if (!inside && !shore) {
                continue;
            }
            if (column->top == TERRAIN_FEATURE_NO_GROUND || column->water != 0u ||
                biome_defs[column->biome].where != BIOME_WHERE_LAND || !feature_is_ground(column->block)) {
                return;
            }
            if (shore && (int32_t)column->top < level) {
                level = (int32_t)column->top;
            }
        }
    }
    if (level == INT32_MAX || level <= MC_WORLD_Z_SEA || level - (int32_t)lake.depth <= MC_WORLD_Z_MIN) {
        return;
    }
    for (uint32_t y = 0u; y < TERRAIN_FEATURE_CHUNK; y++) {
        for (uint32_t x = 0u; x < TERRAIN_FEATURE_CHUNK; x++) {
            if (terrain_feature_lake_depth(&lake, x, y) == 0u) {
                continue;
            }
            const int32_t top = (int32_t)chunk->columns[x + TERRAIN_FEATURE_CHUNK * y].top;
            if (top > level + TERRAIN_FEATURE_LAKE_CLEARS || top < level - (int32_t)lake.depth) {
                return;
            }
        }
    }
    lake.level = (int16_t)level;
    terrain_surface_tile_t read;
    if (tile == nullptr) {
        terrain_surface_tile_read(feature->borrowed_surface, chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
                                  chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK, &read);
        tile = &read;
    }
    if (feature_lake_holds(feature, tile, chunk, &lake)) {
        chunk->lake = lake;
    }
}

/* The biomes and noise of a chunk do not change as the walk goes down, so its slices share one read of them. */
[[gnu::noinline]]
static void feature_chunk_walk(const terrain_feature_t *feature, terrain_feature_chunk_t *chunk) {
    if (chunk->walked != 0u) {
        return;
    }
    if (chunk->tops_known != 0u) {
        feature_chunk_lake_find(feature, nullptr, chunk);
        chunk->walked = 1u;
        return;
    }
    terrain_surface_tile_t tile;
    terrain_surface_tile_read(feature->borrowed_surface, chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
                              chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK, &tile);
    feature_chunk_columns_walk(feature, &tile, chunk);
    feature_chunk_lake_find(feature, &tile, chunk);
    chunk->walked = 1u;
}

void terrain_feature_chunk_columns(const terrain_feature_t *feature, int32_t chunk_x, int32_t chunk_y,
                                   terrain_feature_column_t *out) {
    terrain_feature_chunk_t chunk;
    feature_chunk_load(feature, chunk_x, chunk_y, &chunk);
    terrain_surface_tile_t tile;
    terrain_surface_tile_read(feature->borrowed_surface, chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK,
                              chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK, &tile);
    feature_chunk_columns_walk(feature, &tile, &chunk);
    memcpy(out, chunk.columns, sizeof(chunk.columns));
}

terrain_feature_lake_t terrain_feature_chunk_lake(const terrain_feature_t *feature, int32_t chunk_x, int32_t chunk_y) {
    terrain_feature_chunk_t chunk;
    feature_chunk_load(feature, chunk_x, chunk_y, &chunk);
    feature_chunk_walk(feature, &chunk);
    return chunk.lake;
}

/* ---- Plants ------------------------------------------------------------------------------------------------------- */

static bool feature_is_soil(uint16_t block) {
    return block == BLOCK_ID_GRASS_BLOCK || block == BLOCK_ID_DIRT || block == BLOCK_ID_PODZOL ||
           block == BLOCK_ID_COARSE_DIRT || block == BLOCK_ID_ROOTED_DIRT || block == BLOCK_ID_MOSS_BLOCK ||
           block == BLOCK_ID_MUD;
}

static bool feature_is_sand(uint16_t block) {
    return block == BLOCK_ID_SAND || block == BLOCK_ID_RED_SAND;
}

/* Is there water next to this block, level with it or over it? What a lake is about to hold counts. */
static bool feature_cane_has_water(const terrain_feature_chunk_t *chunk, uint32_t x, uint32_t y, int32_t top) {
    static const int32_t SIDE_X[4] = {1, -1, 0, 0};
    static const int32_t SIDE_Y[4] = {0, 0, 1, -1};
    for (uint32_t side = 0u; side < 4u; side++) {
        const uint32_t nx = (uint32_t)((int32_t)x + SIDE_X[side]);
        const uint32_t ny = (uint32_t)((int32_t)y + SIDE_Y[side]);
        const terrain_feature_column_t *const next = &chunk->columns[nx + TERRAIN_FEATURE_CHUNK * ny];
        if (next->water > 0u && (int32_t)next->top < top && (int32_t)next->top + (int32_t)next->water >= top) {
            return true;
        }
        if (terrain_feature_lake_depth(&chunk->lake, nx, ny) > 0u && (int32_t)chunk->lake.level == top) {
            return true;
        }
    }
    return false;
}

/* Nothing but what is lower beside it, as a cactus would otherwise grow out of a bank. */
static bool feature_cactus_has_room(const terrain_feature_chunk_t *chunk, uint32_t x, uint32_t y, int32_t top) {
    return (int32_t)chunk->columns[x + 1u + TERRAIN_FEATURE_CHUNK * y].top <= top &&
           (int32_t)chunk->columns[x - 1u + TERRAIN_FEATURE_CHUNK * y].top <= top &&
           (int32_t)chunk->columns[x + TERRAIN_FEATURE_CHUNK * (y + 1u)].top <= top &&
           (int32_t)chunk->columns[x + TERRAIN_FEATURE_CHUNK * (y - 1u)].top <= top;
}

static const uint16_t FEATURE_FLOWERS[] = {
    BLOCK_ID_DANDELION, BLOCK_ID_ROSE,        BLOCK_ID_ALLIUM,     BLOCK_ID_AZURE_BLUET,
    BLOCK_ID_TULIP,     BLOCK_ID_OXEYE_DAISY, BLOCK_ID_CORNFLOWER, BLOCK_ID_LILY_OF_THE_VALLEY,
};
enum : uint32_t { FEATURE_FLOWER_COUNT = sizeof(FEATURE_FLOWERS) / sizeof(FEATURE_FLOWERS[0]) };

/* The spike of an ice spikes biome: narrowing as it goes, down past its foot so a slope cannot leave it hanging. */
static void feature_spike(const terrain_feature_canvas_t *canvas, int32_t x, int32_t y, int32_t z, terrain_rng_t *rng) {
    const int32_t tall = 6 + (int32_t)terrain_rng_next_below(rng, 15u);
    for (int32_t k = -4; k < tall; k++) {
        const int32_t across = k < tall / 3 ? 2 : (k < 2 * tall / 3 ? 1 : 0);
        for (int32_t dy = -across; dy <= across; dy++) {
            for (int32_t dx = -across; dx <= across; dx++) {
                if (dx * dx + dy * dy <= across * across + 1) {
                    feature_put(canvas, x + dx, y + dy, z + k, BLOCK_ID_PACKED_ICE, FEATURE_PUT_LOG);
                }
            }
        }
    }
}

/* One site of a row that has no tree: what its row wants of the column, and if it gets that, what it puts there. */
static void feature_grow(const terrain_feature_canvas_t *canvas, const terrain_feature_chunk_t *chunk, uint32_t row,
                         uint32_t ox, uint32_t oy, uint64_t seed) {
    const terrain_feature_column_t *const column = &chunk->columns[ox + TERRAIN_FEATURE_CHUNK * oy];
    const int32_t x = chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)ox;
    const int32_t y = chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)oy;
    const int32_t top = (int32_t)column->top;
    const int32_t z = top + 1;
    const uint16_t ground = column->block;
    const bool dry = column->water == 0u;
    terrain_rng_t rng = terrain_rng_start(seed);
    switch (row) {
    case BIOME_FEATURE_GRASS:
        if (dry && feature_is_soil(ground)) {
            feature_put(canvas, x, y, z, BLOCK_ID_TALL_GRASS, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_FERN:
        if (dry && (feature_is_soil(ground) || ground == BLOCK_ID_SNOW_BLOCK)) {
            feature_put(canvas, x, y, z, BLOCK_ID_FERN, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_FLOWER:
        if (dry && feature_is_soil(ground)) {
            const uint16_t flower = column->biome == BIOME_ID_SWAMP
                                        ? (uint16_t)BLOCK_ID_BLUE_ORCHID
                                        : FEATURE_FLOWERS[terrain_rng_next_below(&rng, FEATURE_FLOWER_COUNT)];
            feature_put(canvas, x, y, z, flower, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_CACTUS:
        if (dry && feature_is_sand(ground) && feature_cactus_has_room(chunk, ox, oy, top)) {
            const int32_t tall = 1 + (int32_t)terrain_rng_next_below(&rng, 3u);
            for (int32_t k = 0; k < tall; k++) {
                feature_put(canvas, x, y, z + k, BLOCK_ID_CACTUS, FEATURE_PUT_PLANT);
            }
        }
        break;
    case BIOME_FEATURE_SUGAR_CANE:
        if (dry && (feature_is_soil(ground) || feature_is_sand(ground)) && feature_cane_has_water(chunk, ox, oy, top)) {
            const int32_t tall = 1 + (int32_t)terrain_rng_next_below(&rng, 3u);
            for (int32_t k = 0; k < tall; k++) {
                feature_put(canvas, x, y, z + k, BLOCK_ID_SUGAR_CANE, FEATURE_PUT_PLANT);
            }
        }
        break;
    case BIOME_FEATURE_DEAD_BUSH:
        if (dry && (feature_is_sand(ground) || ground == BLOCK_ID_COARSE_DIRT || ground == BLOCK_ID_DIRT ||
                    ground == BLOCK_ID_TERRACOTTA)) {
            feature_put(canvas, x, y, z, BLOCK_ID_DEAD_BUSH, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_MUSHROOM:
        if (dry && (feature_is_soil(ground) || ground == BLOCK_ID_MYCELIUM)) {
            const uint16_t kind = terrain_rng_next_below(&rng, 2u) == 0u ? (uint16_t)BLOCK_ID_BROWN_MUSHROOM
                                                                         : (uint16_t)BLOCK_ID_RED_MUSHROOM;
            feature_put(canvas, x, y, z, kind, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_LILY_PAD:
        if (!dry) {
            feature_put(canvas, x, y, top + (int32_t)column->water + 1, BLOCK_ID_LILY_PAD, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_MELON:
        if (dry && ground == BLOCK_ID_GRASS_BLOCK) {
            feature_put(canvas, x, y, z, BLOCK_ID_MELON, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_PUMPKIN:
        if (dry && ground == BLOCK_ID_GRASS_BLOCK) {
            feature_put(canvas, x, y, z, BLOCK_ID_PUMPKIN, FEATURE_PUT_PLANT);
        }
        break;
    case BIOME_FEATURE_SEAGRASS:
        if (column->water >= FEATURE_SEAGRASS_WATER && ground != BLOCK_ID_LAVA) {
            feature_put(canvas, x, y, z, BLOCK_ID_SEAGRASS, FEATURE_PUT_WATER);
        }
        break;
    case BIOME_FEATURE_KELP:
        if (column->water >= FEATURE_KELP_WATER && ground != BLOCK_ID_LAVA) {
            uint32_t most = (uint32_t)column->water - 1u;
            most = most < FEATURE_KELP_TALLEST ? most : FEATURE_KELP_TALLEST;
            const int32_t tall = 1 + (int32_t)terrain_rng_next_below(&rng, most);
            for (int32_t k = 0; k < tall; k++) {
                feature_put(canvas, x, y, z + k, BLOCK_ID_KELP, FEATURE_PUT_WATER);
            }
        }
        break;
    case TERRAIN_FEATURE_ROW_SPIKE:
        if (dry && ground == BLOCK_ID_SNOW_BLOCK) {
            feature_spike(canvas, x, y, z, &rng);
        }
        break;
    default:
        break;
    }
}

/* ---- Sites -------------------------------------------------------------------------------------------------------- */

/* How many of a row a biome tries in a chunk. The biome table's number, but for what it does not list. */
static float32_t feature_rate(uint32_t row, uint8_t biome) {
    if (row == TERRAIN_FEATURE_ROW_SPIKE) {
        return biome == BIOME_ID_ICE_SPIKES ? 1.0f32 : 0.0f32;
    }
    return biome_features[biome][row];
}

/* What is left of a tree's vines per tree of the biome's. */
static uint32_t feature_strands(uint8_t biome, terrain_rng_t *rng) {
    float32_t trees = 0.0f32;
    for (uint32_t kind = 0u; kind < TERRAIN_FEATURE_TREES; kind++) {
        trees += biome_features[biome][kind];
    }
    const float32_t vines = biome_features[biome][BIOME_FEATURE_VINE];
    const float32_t each = trees > 0.0f32 ? vines / trees : 0.0f32;
    uint32_t strands = (uint32_t)each;
    if (terrain_rng_next_f32(rng) < each - (float32_t)strands) {
        strands++;
    }
    return strands < FEATURE_STRANDS_MAX ? strands : FEATURE_STRANDS_MAX;
}

/* Does the tree stand? If so, fills in what it needs of the ground round it. */
static bool feature_tree_stands(const terrain_feature_chunk_t *chunk, terrain_feature_tree_t kind, uint32_t ox,
                                uint32_t oy, terrain_feature_tree_site_t *site) {
    const terrain_feature_column_t *const column = &chunk->columns[ox + TERRAIN_FEATURE_CHUNK * oy];
    const bool mangrove = kind == TERRAIN_FEATURE_TREE_MANGROVE;
    if (column->water > (mangrove ? FEATURE_MANGROVE_WATER : 0u)) {
        return false;
    }
    if (!feature_is_soil(column->block) &&
        !(kind == TERRAIN_FEATURE_TREE_SPRUCE && column->block == BLOCK_ID_SNOW_BLOCK)) {
        return false;
    }
    const int32_t top = (int32_t)column->top;
    site->lift = (uint8_t)column->water;
    if (mangrove) {
        site->foot[0] = chunk->columns[ox + 1u + TERRAIN_FEATURE_CHUNK * oy].top;
        site->foot[1] = chunk->columns[ox - 1u + TERRAIN_FEATURE_CHUNK * oy].top;
        site->foot[2] = chunk->columns[ox + TERRAIN_FEATURE_CHUNK * (oy + 1u)].top;
        site->foot[3] = chunk->columns[ox + TERRAIN_FEATURE_CHUNK * (oy - 1u)].top;
        return true;
    }
    if (terrain_feature_tree_is_wide(kind, site->seed)) {
        const uint32_t others[3] = {ox + 1u + TERRAIN_FEATURE_CHUNK * oy, ox + TERRAIN_FEATURE_CHUNK * (oy + 1u),
                                    ox + 1u + TERRAIN_FEATURE_CHUNK * (oy + 1u)};
        for (uint32_t i = 0u; i < 3u; i++) {
            const terrain_feature_column_t *const other = &chunk->columns[others[i]];
            const int32_t gap = (int32_t)other->top - top;
            if (other->top == TERRAIN_FEATURE_NO_GROUND || other->water != 0u || gap < -2 || gap > 2 ||
                terrain_feature_lake_depth(&chunk->lake, others[i] & 15u, others[i] >> 4) > 0u) {
                return false;
            }
            site->foot[i] = other->top;
        }
    }
    return true;
}

/* What a row reaches beyond its column. */
static int32_t feature_row_reach(uint32_t row) {
    if (row < TERRAIN_FEATURE_TREES) {
        return FEATURE_TREE_REACH[row];
    }
    return row == TERRAIN_FEATURE_ROW_SPIKE ? 2 : 0;
}

/* Everything a row grows in a chunk that can touch the canvas. The draws never depend on the canvas, only what is done with
 * them, so a site is the same site whoever asks. */
static void feature_row_chunk(const terrain_feature_t *feature, const terrain_feature_canvas_t *canvas,
                              terrain_feature_chunk_t *chunk, uint32_t row, float32_t most) {
    uint32_t tries = (uint32_t)most;
    tries += (float32_t)tries < most ? 1u : 0u;
    const feature_margin_t margin = feature_margin_of(row);
    const uint32_t span_low = TERRAIN_FEATURE_CHUNK - margin.low - margin.high;
    const int32_t reach = feature_row_reach(row);
    terrain_rng_t rng = terrain_rng_start(terrain_rng_hash2(feature->row_seeds[row], chunk->chunk_x, chunk->chunk_y));
    for (uint32_t i = 0u; i < tries; i++) {
        const uint32_t ox = margin.low + terrain_rng_next_below(&rng, span_low);
        const uint32_t oy = margin.low + terrain_rng_next_below(&rng, span_low);
        const float32_t luck = terrain_rng_next_f32(&rng);
        const uint64_t seed = terrain_rng_next_u64(&rng);
        const int32_t x = chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)ox;
        const int32_t y = chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)oy;
        if (x + reach < canvas->x_low || x - reach > canvas->x_high || y + reach < canvas->y_low ||
            y - reach > canvas->y_high) {
            continue;
        }
        const uint8_t biome = chunk->columns[ox + TERRAIN_FEATURE_CHUNK * oy].biome;
        if (luck * (float32_t)tries >= feature_rate(row, biome)) {
            continue;
        }
        feature_chunk_walk(feature, chunk);
        const terrain_feature_column_t *const column = &chunk->columns[ox + TERRAIN_FEATURE_CHUNK * oy];
        if (column->top == TERRAIN_FEATURE_NO_GROUND || terrain_feature_lake_depth(&chunk->lake, ox, oy) > 0u) {
            continue;
        }
        const int32_t top = (int32_t)column->top;
        const int32_t surface = top + (int32_t)column->water;
        if (surface + TERRAIN_FEATURE_TALLEST + 1 < canvas->z_low || top - FEATURE_BELOW > canvas->z_high) {
            continue;
        }
        if (row >= TERRAIN_FEATURE_TREES) {
            feature_grow(canvas, chunk, row, ox, oy, seed);
            continue;
        }
        terrain_feature_tree_site_t site = {
            .seed = seed,
            .x = x,
            .y = y,
            .z = top + 1,
            .foot = {0, 0, 0, 0},
            .kind = (uint8_t)row,
        };
        if (feature_tree_stands(chunk, (terrain_feature_tree_t)row, ox, oy, &site)) {
            terrain_rng_t vines = terrain_rng_start(terrain_rng_mix(seed ^ 0x7669u));
            site.strands = (uint8_t)feature_strands(biome, &vines);
            terrain_feature_paint_tree(canvas, &site);
        }
    }
}

/* Is this row placed by sites at all? Vines go with trees, and ice and snow are every column's. */
static bool feature_row_has_sites(uint32_t row) {
    return row != BIOME_FEATURE_VINE && row != BIOME_FEATURE_SNOW && row != BIOME_FEATURE_ICE;
}

/* All the rows a chunk has that can reach a canvas. Only a tree can leave its chunk. */
static void feature_chunk_rows(const terrain_feature_t *feature, const terrain_feature_canvas_t *canvas,
                               terrain_feature_chunk_t *chunk, bool own) {
    uint8_t seen[BIOME_COUNT];
    memset(seen, 0, sizeof(seen));
    float32_t most[TERRAIN_FEATURE_ROWS];
    memset(most, 0, sizeof(most));
    for (uint32_t i = 0u; i < TERRAIN_FEATURE_CHUNK_COLUMNS; i++) {
        const uint8_t biome = chunk->columns[i].biome;
        if (seen[biome] != 0u) {
            continue;
        }
        seen[biome] = 1u;
        for (uint32_t row = 0u; row < TERRAIN_FEATURE_ROWS; row++) {
            const float32_t rate = feature_rate(row, biome);
            most[row] = rate > most[row] ? rate : most[row];
        }
    }
    const uint32_t rows = own ? TERRAIN_FEATURE_ROWS : TERRAIN_FEATURE_TREES;
    for (uint32_t row = 0u; row < rows; row++) {
        if (most[row] > 0.0f32 && feature_row_has_sites(row)) {
            feature_row_chunk(feature, canvas, chunk, row, most[row]);
        }
    }
}

/* ---- Lakes, ice and snow on the canvas ---------------------------------------------------------------------------- */

static void feature_set(const terrain_feature_canvas_t *canvas, int32_t x, int32_t y, int32_t z, uint16_t block) {
    if (x < canvas->x_low || x > canvas->x_high || y < canvas->y_low || y > canvas->y_high || z < canvas->z_low ||
        z > canvas->z_high) {
        return;
    }
    const terrain_density_box_t *const box = canvas->borrowed_box;
    canvas->borrowed_blocks[(uint64_t)(x - box->origin_x) +
                            (uint64_t)box->count_x * ((uint64_t)(y - box->origin_y) +
                                                      (uint64_t)box->count_y * (uint64_t)(z - box->origin_z))] = block;
}

/* Water up to the level, and nothing over it where the ground stood higher. */
static void feature_lake_paint(const terrain_feature_canvas_t *canvas, const terrain_feature_chunk_t *chunk) {
    if (chunk->lake.exists == 0u) {
        return;
    }
    const int32_t level = (int32_t)chunk->lake.level;
    for (uint32_t oy = 0u; oy < TERRAIN_FEATURE_CHUNK; oy++) {
        for (uint32_t ox = 0u; ox < TERRAIN_FEATURE_CHUNK; ox++) {
            const uint32_t depth = terrain_feature_lake_depth(&chunk->lake, ox, oy);
            if (depth == 0u) {
                continue;
            }
            const int32_t x = chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)ox;
            const int32_t y = chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)oy;
            const int32_t top = (int32_t)chunk->columns[ox + TERRAIN_FEATURE_CHUNK * oy].top;
            for (int32_t z = feature_lake_bottom(&chunk->lake, top, depth); z <= level; z++) {
                feature_set(canvas, x, y, z, BLOCK_ID_WATER);
            }
            for (int32_t z = level + 1; z <= top; z++) {
                feature_set(canvas, x, y, z, BLOCK_ID_AIR);
            }
        }
    }
}

/* Snow does not lie on what is itself frozen, on what cannot hold it, or under water. */
static bool feature_takes_snow(uint16_t block) {
    return block != BLOCK_ID_SNOW_BLOCK && block != BLOCK_ID_ICE && block != BLOCK_ID_PACKED_ICE &&
           block != BLOCK_ID_BLUE_ICE && block != BLOCK_ID_LAVA;
}

/* Every column of a biome that is cold: its open water freezes on top, and its dry ground takes a layer of snow. Goes last, so
 * neither ends up under anything that grew. */
static void feature_cold_paint(const terrain_feature_canvas_t *canvas, const terrain_feature_chunk_t *chunk) {
    for (uint32_t oy = 0u; oy < TERRAIN_FEATURE_CHUNK; oy++) {
        for (uint32_t ox = 0u; ox < TERRAIN_FEATURE_CHUNK; ox++) {
            const terrain_feature_column_t *const column = &chunk->columns[ox + TERRAIN_FEATURE_CHUNK * oy];
            if (column->top == TERRAIN_FEATURE_NO_GROUND) {
                continue;
            }
            const float32_t *const rates = biome_features[column->biome];
            const int32_t x = chunk->chunk_x * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)ox;
            const int32_t y = chunk->chunk_y * (int32_t)TERRAIN_FEATURE_CHUNK + (int32_t)oy;
            if (terrain_feature_lake_depth(&chunk->lake, ox, oy) > 0u) {
                if (rates[BIOME_FEATURE_ICE] > 0.0f32) {
                    feature_set(canvas, x, y, (int32_t)chunk->lake.level, BLOCK_ID_ICE);
                }
                continue;
            }
            if (column->water > 0u) {
                if (rates[BIOME_FEATURE_ICE] > 0.0f32) {
                    feature_put(canvas, x, y, (int32_t)column->top + (int32_t)column->water, BLOCK_ID_ICE,
                                FEATURE_PUT_WATER);
                }
                continue;
            }
            if (rates[BIOME_FEATURE_SNOW] > 0.0f32 && feature_takes_snow(column->block)) {
                feature_put(canvas, x, y, (int32_t)column->top + 1, BLOCK_ID_SNOW_BLOCK, FEATURE_PUT_PLANT);
            }
        }
    }
}

/* ---- One tile ----------------------------------------------------------------------------------------------------- */

/* Can what a chunk grows touch the canvas's layers at all? */
static bool terrain_feature_chunk_touches(const terrain_feature_chunk_t *chunk,
                                          const terrain_feature_canvas_t *canvas) {
    const int32_t high = chunk->z_high > MC_WORLD_Z_SEA ? chunk->z_high : MC_WORLD_Z_SEA;
    return high + TERRAIN_FEATURE_TALLEST + 1 >= canvas->z_low && chunk->z_low - FEATURE_BELOW <= canvas->z_high;
}

/* Everything whose voxels are in one chunk's part of a box. The canvas is within that chunk, which is own, loaded. What each step
 * writes at a voxel depends only on what is at that voxel and on the chunks round this one, and they always go in this order. */
static void feature_tile(const terrain_feature_t *feature, const terrain_feature_canvas_t *canvas,
                         terrain_feature_chunk_t *own) {
    const int32_t chunk_x = own->chunk_x;
    const int32_t chunk_y = own->chunk_y;
    const bool own_touches = terrain_feature_chunk_touches(own, canvas);
    if (own_touches) {
        feature_chunk_walk(feature, own);
        feature_lake_paint(canvas, own);
        feature_chunk_rows(feature, canvas, own, true);
    }
    terrain_feature_chunk_t next;
    for (int32_t dy = -1; dy <= 1; dy++) {
        for (int32_t dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            feature_chunk_load(feature, chunk_x + dx, chunk_y + dy, &next);
            if (terrain_feature_chunk_touches(&next, canvas)) {
                feature_chunk_rows(feature, canvas, &next, false);
            }
        }
    }
    if (own_touches) {
        feature_cold_paint(canvas, own);
    }
}

void terrain_feature_fill_box(const terrain_feature_t *feature, const terrain_density_box_t *box, uint16_t *blocks) {
    const terrain_feature_canvas_t whole = terrain_feature_canvas_of(box, blocks);
    if (whole.z_low > whole.z_high) {
        return;
    }
    const int64_t tile = (int64_t)TERRAIN_FEATURE_CHUNK;
    for (uint32_t iy = 0u; iy < box->count_y;) {
        const int64_t y = (int64_t)box->origin_y + (int64_t)iy;
        const int64_t chunk_y = y >> TERRAIN_FEATURE_CHUNK_SHIFT;
        const int64_t y_end = (chunk_y + 1) * tile;
        const int64_t y_left = (int64_t)box->count_y - (int64_t)iy;
        const uint32_t count_y = (uint32_t)(y_end - y < y_left ? y_end - y : y_left);
        for (uint32_t ix = 0u; ix < box->count_x;) {
            const int64_t x = (int64_t)box->origin_x + (int64_t)ix;
            const int64_t chunk_x = x >> TERRAIN_FEATURE_CHUNK_SHIFT;
            const int64_t x_end = (chunk_x + 1) * tile;
            const int64_t x_left = (int64_t)box->count_x - (int64_t)ix;
            const uint32_t count_x = (uint32_t)(x_end - x < x_left ? x_end - x : x_left);
            terrain_feature_canvas_t canvas = whole;
            canvas.x_low = (int32_t)x;
            canvas.x_high = (int32_t)x + (int32_t)count_x - 1;
            canvas.y_low = (int32_t)y;
            canvas.y_high = (int32_t)y + (int32_t)count_y - 1;
            terrain_feature_chunk_t own;
            feature_chunk_load(feature, (int32_t)chunk_x, (int32_t)chunk_y, &own);
            feature_tile(feature, &canvas, &own);
            ix += count_x;
        }
        iy += count_y;
    }
}

bool terrain_feature_chunk_read_box(const terrain_feature_t *feature, const terrain_density_box_t *box,
                                    const uint16_t *blocks, terrain_feature_chunk_t *out) {
    const int32_t mask = (int32_t)TERRAIN_FEATURE_CHUNK - 1;
    if (box->count_x != TERRAIN_FEATURE_CHUNK || box->count_y != TERRAIN_FEATURE_CHUNK || (box->origin_x & mask) != 0 ||
        (box->origin_y & mask) != 0) {
        return false;
    }
    feature_chunk_load(feature, box->origin_x >> TERRAIN_FEATURE_CHUNK_SHIFT,
                       box->origin_y >> TERRAIN_FEATURE_CHUNK_SHIFT, out);
    const int64_t box_top = (int64_t)box->origin_z + (int64_t)box->count_z - 1;
    if (out->z_low > out->z_high || out->z_low < box->origin_z || (int64_t)out->z_high > box_top) {
        return false;
    }
    feature_columns_start(out);
    const uint64_t skipped = (uint64_t)(out->z_low - box->origin_z) * TERRAIN_FEATURE_CHUNK_COLUMNS;
    feature_columns_finish(
        out, feature_columns_read(out, blocks + skipped, out->z_low, out->z_high, TERRAIN_FEATURE_CHUNK_COLUMNS));
    out->tops_known = 1u;
    return true;
}

void terrain_feature_fill_chunk_box(const terrain_feature_t *feature, const terrain_density_box_t *box,
                                    terrain_feature_chunk_t *chunk, uint16_t *blocks) {
    const terrain_feature_canvas_t canvas = terrain_feature_canvas_of(box, blocks);
    if (canvas.z_low <= canvas.z_high) {
        feature_tile(feature, &canvas, chunk);
    }
}

/* ---- The world ---------------------------------------------------------------------------------------------------- */

void terrain_feature_init(terrain_feature_t **out, arena_t *arena, uint64_t seed, const terrain_biome_t *biome,
                          const terrain_hydrology_t *hydrology, const terrain_surface_t *surface,
                          const terrain_cave_t *cave) {
    terrain_feature_t *const feature = arena_alloc(arena, sizeof(*feature), alignof(terrain_feature_t));
    /* Each row's own stream, from its row number, so no row's numbers depend on any other. */
    const uint64_t rows = terrain_rng_derive(seed, terrain_rng_salt("feature/rows"));
    for (uint32_t row = 0u; row < TERRAIN_FEATURE_ROWS; row++) {
        feature->row_seeds[row] = terrain_rng_derive(rows, (uint64_t)row);
    }
    feature->lake_seed = terrain_rng_derive(seed, terrain_rng_salt("feature/lake"));
    feature->borrowed_biome = biome;
    feature->borrowed_hydrology = hydrology;
    feature->borrowed_surface = surface;
    feature->borrowed_cave = cave;
    *out = feature;
}

void terrain_feature_deinit(terrain_feature_t *feature) {
    memset(feature, 0, sizeof(*feature));
}
