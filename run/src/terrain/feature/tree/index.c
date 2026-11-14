/* See README.md. */
#include "terrain/feature/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "terrain/rng/index.h"

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
        terrain_feature_put(canvas, x, y, z, block, TERRAIN_FEATURE_PUT_LOG);
    }
}

enum : int32_t {
    /* The game takes leaves with no log within this many steps through leaves (run/src/world/tick/, TICK_LEAF_REACH), so no
     * crown is drawn past it. */
    FEATURE_LEAF_REACH = 6,
};

/* How many steps a leaf at (dx, dy, dz) from a wide crown's middle, its trunk's last log one under it, lies from that trunk:
 * across to the nearest of the four logs' columns, then up from the last log. */
static int32_t feature_wide_crown_steps(int32_t dx, int32_t dy, int32_t dz) {
    const int32_t across_x = dx < 0 ? -dx : (dx > 1 ? dx - 1 : 0);
    const int32_t across_y = dy < 0 ? -dy : (dy > 1 ? dy - 1 : 0);
    return across_x + across_y + (dz + 1 > 0 ? dz + 1 : 0);
}

/* Leaves in a rounded blob. A wide blob is centred between x and x + 1, so it is the same round a wide trunk. Its outer shell is
 * bitten here and there, from the tree's seed. A reach above 0 leaves out what lies more steps than that from a wide trunk under
 * its middle (feature_wide_crown_steps). */
static void feature_blob(const terrain_feature_canvas_t *canvas, int32_t x, int32_t y, int32_t z, int32_t across,
                         int32_t tall, bool wide, uint16_t block, uint64_t seed, int32_t reach) {
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
                if (reach > 0 && feature_wide_crown_steps(dx, dy, dz) > reach) {
                    continue; /* plan 006 phase 04: a leaf the game's decay would take, as no log is near enough */
                }
                terrain_feature_put(canvas, x + dx, y + dy, z + dz, block, TERRAIN_FEATURE_PUT_LEAF);
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
            terrain_feature_put(canvas, x + dx, y + dy, z, block, TERRAIN_FEATURE_PUT_LEAF);
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
                    terrain_feature_put(canvas, site->x + dx, site->y + dy, z, BLOCK_ID_SPRUCE_LEAVES,
                                        TERRAIN_FEATURE_PUT_LEAF);
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
    feature_blob(canvas, site->x, site->y, top, 5, 3, true, BLOCK_ID_JUNGLE_LEAVES, site->seed,
                 (int32_t)FEATURE_LEAF_REACH);
    const uint32_t branches = 2u + terrain_rng_next_below(rng, 2u);
    for (uint32_t b = 0u; b < branches; b++) {
        const int32_t dx = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
        const int32_t dy = terrain_rng_next_below(rng, 2u) == 0u ? -1 : 1;
        const int32_t length = 2 + (int32_t)terrain_rng_next_below(rng, 2u);
        const int32_t from = site->z + trunk / 2 + (int32_t)terrain_rng_next_below(rng, (uint32_t)(trunk / 2 - 3));
        const int32_t sx = dx > 0 ? site->x + 1 : site->x;
        const int32_t sy = dy > 0 ? site->y + 1 : site->y;
        for (int32_t i = 1; i <= length; i++) {
            terrain_feature_put(canvas, sx + dx * i, sy + dy * i, from + i, BLOCK_ID_JUNGLE_LOG,
                                TERRAIN_FEATURE_PUT_LOG);
        }
        feature_blob(canvas, sx + dx * length, sy + dy * length, from + length + 1, 2, 1, false, BLOCK_ID_JUNGLE_LEAVES,
                     site->seed + b + 1u, 0);
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
        terrain_feature_put(canvas, site->x + dx * i, site->y + dy * i, site->z + straight - 1 + i, BLOCK_ID_ACACIA_LOG,
                            TERRAIN_FEATURE_PUT_LOG);
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
    feature_blob(canvas, site->x, site->y, top - 1, 3, 2, true, BLOCK_ID_DARK_OAK_LEAVES, site->seed, 0);
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
    feature_blob(canvas, site->x, site->y, top, 3, 2, false, BLOCK_ID_MANGROVE_LEAVES, site->seed, 0);
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
            terrain_feature_put(canvas, site->x + sign * dx * i, site->y + sign * dy * i, top - 1 + i,
                                BLOCK_ID_CHERRY_LOG, TERRAIN_FEATURE_PUT_LOG);
        }
        feature_blob(canvas, site->x + sign * dx * length, site->y + sign * dy * length, top + length, 3, 2, false,
                     BLOCK_ID_CHERRY_LEAVES, site->seed + (uint64_t)bough + 1u, 0);
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
            terrain_feature_put(canvas, x, y, site->z + (int32_t)site->lift + start - k, BLOCK_ID_VINE,
                                TERRAIN_FEATURE_PUT_VINE);
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
