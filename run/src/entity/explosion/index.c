/* See README.md. The Arena tier by its depth: it takes two arrays from the arena it is given, once, and never frees. */
#include "entity/explosion/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"
#include "world/wall_torch/index.h"

#include <math.h>
#include <string.h>

/* How a ray goes: the published numbers. */
#define EXPLOSION_RAY_STEP 0.3f64
#define EXPLOSION_RAY_FADE 0.225f64
#define EXPLOSION_RAY_SPREAD_MIN 0.7f64
#define EXPLOSION_RAY_SPREAD 0.6f64
/* What any block in the way takes off a ray, on top of its resistance, before the step's share is taken. */
#define EXPLOSION_RESISTANCE_FLOOR 0.3f64
/* What bedrock stands against. */
#define EXPLOSION_RESISTANCE_UNBREAKABLE 3600000.0f64
/* Damage at the middle of a blast is this, times its reach. */
#define EXPLOSION_DAMAGE_SCALE 7.0f64
/* A line's cell is never entered at or past this. */
#define EXPLOSION_LINE_END 1.0f64
/* Past the end of any line, for an axis it never moves along. */
#define EXPLOSION_LINE_NEVER 2.0f64

void explosion_start(explosion_t *explosion, arena_t *arena, uint64_t seed) {
    memset(explosion, 0, sizeof(*explosion));
    explosion->borrowed_seen = arena_alloc_zeroed(arena, (uint64_t)EXPLOSION_SEEN_WORDS * sizeof(uint64_t), 64u);
    explosion->borrowed_cells = arena_alloc_zeroed(arena, (uint64_t)EXPLOSION_CELLS * sizeof(explosion_cell_t), 64u);
    explosion->random = terrain_rng_start(seed);
}

float64_t explosion_resistance(uint16_t block) {
    const block_def_t *const def = &block_defs[block];
    if ((def->flags & BLOCK_FLAG_UNBREAKABLE) != 0u) {
        return EXPLOSION_RESISTANCE_UNBREAKABLE;
    }
    /* A block's own file has no field for this, and most blocks' number is their hardness. These are the ones whose number is not. */
    switch (block) {
    case BLOCK_ID_ANDESITE:
    case BLOCK_ID_BRICKS:
    case BLOCK_ID_COBBLED_DEEPSLATE:
    case BLOCK_ID_COBBLESTONE:
    case BLOCK_ID_COBBLESTONE_STAIRS:
    case BLOCK_ID_DEEPSLATE:
    case BLOCK_ID_DIORITE:
    case BLOCK_ID_GRANITE:
    case BLOCK_ID_MOSSY_COBBLESTONE:
    case BLOCK_ID_SMOOTH_STONE:
    case BLOCK_ID_STONE:
    case BLOCK_ID_STONE_BRICKS:
    case BLOCK_ID_STONE_SLAB:
    case BLOCK_ID_TUFF:
        return 6.0f64;
    case BLOCK_ID_BROWN_TERRACOTTA:
    case BLOCK_ID_LIGHT_GRAY_TERRACOTTA:
    case BLOCK_ID_ORANGE_TERRACOTTA:
    case BLOCK_ID_RED_TERRACOTTA:
    case BLOCK_ID_TERRACOTTA:
    case BLOCK_ID_WHITE_TERRACOTTA:
    case BLOCK_ID_YELLOW_TERRACOTTA:
        return 4.2f64;
    case BLOCK_ID_ACACIA_PLANKS:
    case BLOCK_ID_BIRCH_PLANKS:
    case BLOCK_ID_CHERRY_PLANKS:
    case BLOCK_ID_DARK_OAK_PLANKS:
    case BLOCK_ID_DEEPSLATE_COAL_ORE:
    case BLOCK_ID_DEEPSLATE_COPPER_ORE:
    case BLOCK_ID_DEEPSLATE_DIAMOND_ORE:
    case BLOCK_ID_DEEPSLATE_EMERALD_ORE:
    case BLOCK_ID_DEEPSLATE_GOLD_ORE:
    case BLOCK_ID_DEEPSLATE_IRON_ORE:
    case BLOCK_ID_DEEPSLATE_LAPIS_ORE:
    case BLOCK_ID_JUNGLE_PLANKS:
    case BLOCK_ID_MANGROVE_PLANKS:
    case BLOCK_ID_OAK_PLANKS:
    case BLOCK_ID_OAK_SLAB:
    case BLOCK_ID_OAK_STAIRS:
    case BLOCK_ID_SPRUCE_PLANKS:
        return 3.0f64;
    case BLOCK_ID_DRIPSTONE_BLOCK:
        return 1.0f64;
    case BLOCK_ID_OBSIDIAN:
        return 1200.0f64;
    default:
        return (float64_t)def->hardness;
    }
}

/* Whether a line goes into a box. Touching a face does not. Going along a low face does, as that is in the cell the walk goes through: a
 * body on the floor is not to see under a wall standing on the same floor. Going along a high face does not. */
static bool explosion_line_enters(const float64_t from[COLLISION_AXES], const float64_t delta[COLLISION_AXES],
                                  const collision_box_t *box) {
    float64_t t0 = 0.0f64, t1 = EXPLOSION_LINE_END;
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        if (delta[axis] == 0.0f64) {
            if (from[axis] < box->min[axis] || from[axis] >= box->max[axis]) {
                return false;
            }
            continue;
        }
        const float64_t a = (box->min[axis] - from[axis]) / delta[axis];
        const float64_t b = (box->max[axis] - from[axis]) / delta[axis];
        const float64_t enter = a < b ? a : b, leave = a < b ? b : a;
        t0 = enter > t0 ? enter : t0;
        t1 = leave < t1 ? leave : t1;
    }
    return t0 < t1;
}

/* Whether what is in this cell stops the line, by being there or by not being known. */
static bool explosion_cell_stops(const world_map_t *map, const int32_t cell[COLLISION_AXES],
                                 const float64_t from[COLLISION_AXES], const float64_t delta[COLLISION_AXES]) {
    uint16_t block = BLOCK_ID_AIR;
    const world_map_status_t status = world_map_block_get(map, cell[0], cell[1], cell[2], &block);
    if (status == WORLD_MAP_STATUS_NOT_LOADED) {
        return true;
    }
    if (status != WORLD_MAP_STATUS_OK || (block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u) {
        return false;
    }
    collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
    const uint32_t count = collision_block_boxes(block, cell[0], cell[1], cell[2], boxes);
    for (uint32_t i = 0u; i < count; i++) {
        if (explosion_line_enters(from, delta, &boxes[i])) {
            return true;
        }
    }
    return false;
}

bool explosion_line_clear(const world_map_t *map, const float64_t from[COLLISION_AXES],
                          const float64_t to[COLLISION_AXES]) {
    float64_t delta[COLLISION_AXES], next[COLLISION_AXES], each[COLLISION_AXES];
    int32_t cell[COLLISION_AXES], step[COLLISION_AXES];
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        delta[axis] = to[axis] - from[axis];
        const float64_t whole = floor(from[axis]);
        cell[axis] = (int32_t)whole;
        step[axis] = delta[axis] > 0.0f64 ? 1 : -1;
        if (delta[axis] == 0.0f64) {
            next[axis] = EXPLOSION_LINE_NEVER;
            each[axis] = 0.0f64;
        } else {
            const float64_t face = delta[axis] > 0.0f64 ? whole + 1.0f64 : whole;
            next[axis] = (face - from[axis]) / delta[axis];
            each[axis] = fabs(1.0f64 / delta[axis]);
        }
    }
    for (;;) {
        if (explosion_cell_stops(map, cell, from, delta)) {
            return false;
        }
        uint32_t axis = next[0] < next[1] ? 0u : 1u;
        axis = next[2] < next[axis] ? 2u : axis;
        if (next[axis] >= EXPLOSION_LINE_END) {
            return true;
        }
        cell[axis] += step[axis];
        next[axis] += each[axis];
    }
}

float64_t explosion_exposure(const world_map_t *map, const float64_t center[COLLISION_AXES],
                             const collision_box_t *box) {
    float64_t size[COLLISION_AXES], spacing[COLLISION_AXES], offset[COLLISION_AXES];
    uint32_t samples[COLLISION_AXES];
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        size[axis] = box->max[axis] - box->min[axis];
        spacing[axis] = 1.0f64 / (size[axis] * 2.0f64 + 1.0f64);
        const float64_t whole = floor(1.0f64 / spacing[axis]);
        samples[axis] = (uint32_t)whole + 1u;
        /* Sideways the grid is centered on the box, and it starts at the soles. The published sum. */
        offset[axis] = axis == 2u ? 0.0f64 : (1.0f64 - whole * spacing[axis]) * 0.5f64;
    }
    uint32_t clear = 0u;
    for (uint32_t a = 0u; a < samples[0]; a++) {
        for (uint32_t b = 0u; b < samples[1]; b++) {
            for (uint32_t c = 0u; c < samples[2]; c++) {
                const float64_t point[COLLISION_AXES] = {
                    box->min[0] + (float64_t)a * spacing[0] * size[0] + offset[0],
                    box->min[1] + (float64_t)b * spacing[1] * size[1] + offset[1],
                    box->min[2] + (float64_t)c * spacing[2] * size[2] + offset[2],
                };
                clear += explosion_line_clear(map, point, center) ? 1u : 0u;
            }
        }
    }
    return (float64_t)clear / (float64_t)(samples[0] * samples[1] * samples[2]);
}

bool explosion_blow(const world_map_t *map, const float64_t center[COLLISION_AXES], float64_t power,
                    const float64_t position[COLLISION_AXES], const collision_box_t *box, float64_t eye_height,
                    explosion_blow_t *out) {
    const float64_t reach = 2.0f64 * power;
    const float64_t feet[COLLISION_AXES] = {position[0] - center[0], position[1] - center[1], position[2] - center[2]};
    const float64_t nearness = sqrt(feet[0] * feet[0] + feet[1] * feet[1] + feet[2] * feet[2]) / reach;
    if (nearness > 1.0f64) {
        return false;
    }
    const float64_t aim[COLLISION_AXES] = {feet[0], feet[1], feet[2] + eye_height};
    const float64_t length = sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    if (length == 0.0f64) {
        return false;
    }
    const float64_t impact = (1.0f64 - nearness) * explosion_exposure(map, center, box);
    /* Whole half hearts, and always one: behind a wall a blast still hurts, as it is published to. */
    const float64_t damage = floor((impact * impact + impact) * 0.5f64 * EXPLOSION_DAMAGE_SCALE * reach + 1.0f64);
    *out = (explosion_blow_t){
        .push = {aim[0] / length * impact, aim[1] / length * impact, aim[2] / length * impact},
        .impact = impact,
        .damage = (float32_t)damage,
    };
    return true;
}

/* Remembers a cell, once. */
static void explosion_cell_add(explosion_t *explosion, const int32_t cell[COLLISION_AXES],
                               const int32_t middle[COLLISION_AXES], uint16_t block) {
    const int32_t reach = (int32_t)EXPLOSION_REACH, side = (int32_t)EXPLOSION_SIDE;
    const int32_t a = cell[0] - middle[0] + reach, b = cell[1] - middle[1] + reach, c = cell[2] - middle[2] + reach;
    ASSERT_ALWAYS(a >= 0 && a < side && b >= 0 && b < side && c >= 0 && c < side);
    const uint32_t bit = (uint32_t)((a * side + b) * side + c);
    const uint64_t mask = 1ull << (bit % 64u);
    if ((explosion->borrowed_seen[bit / 64u] & mask) != 0u) {
        return;
    }
    explosion->borrowed_seen[bit / 64u] |= mask;
    explosion->borrowed_cells[explosion->count++] = (explosion_cell_t){
        .x = cell[0],
        .y = cell[1],
        .z = cell[2],
        .block = block,
    };
}

/* One ray, through whatever it is strong enough to get through. A column not loaded ends it, and it cannot be known what is there. */
static void explosion_ray(explosion_t *explosion, const world_map_t *map, const float64_t center[COLLISION_AXES],
                          const int32_t middle[COLLISION_AXES], const float64_t way[COLLISION_AXES], float64_t power) {
    float64_t left =
        power * (EXPLOSION_RAY_SPREAD_MIN + EXPLOSION_RAY_SPREAD * terrain_rng_next_f64(&explosion->random));
    float64_t at[COLLISION_AXES] = {center[0], center[1], center[2]};
    while (left > 0.0f64) {
        const int32_t cell[COLLISION_AXES] = {(int32_t)floor(at[0]), (int32_t)floor(at[1]), (int32_t)floor(at[2])};
        uint16_t block = BLOCK_ID_AIR;
        const world_map_status_t status = world_map_block_get(map, cell[0], cell[1], cell[2], &block);
        if (status == WORLD_MAP_STATUS_NOT_LOADED) {
            return;
        }
        if (status == WORLD_MAP_STATUS_OK && block != BLOCK_ID_AIR) {
            left -= (explosion_resistance(block) + EXPLOSION_RESISTANCE_FLOOR) * EXPLOSION_RAY_STEP;
            if (left > 0.0f64) {
                explosion_cell_add(explosion, cell, middle, block);
            }
        }
        at[0] += way[0] * EXPLOSION_RAY_STEP;
        at[1] += way[1] * EXPLOSION_RAY_STEP;
        at[2] += way[2] * EXPLOSION_RAY_STEP;
        left -= EXPLOSION_RAY_FADE;
    }
}

/* Finds what will break: one ray out through each cell on the faces of a cube, each with a strength of its own. */
static void explosion_cast(explosion_t *explosion, const world_map_t *map, const float64_t center[COLLISION_AXES],
                           float64_t power) {
    memset(explosion->borrowed_seen, 0, (uint64_t)EXPLOSION_SEEN_WORDS * sizeof(uint64_t));
    explosion->count = 0u;
    const int32_t middle[COLLISION_AXES] = {(int32_t)floor(center[0]), (int32_t)floor(center[1]),
                                            (int32_t)floor(center[2])};
    const uint32_t last = EXPLOSION_RAYS_PER_SIDE - 1u;
    for (uint32_t i = 0u; i <= last; i++) {
        for (uint32_t j = 0u; j <= last; j++) {
            for (uint32_t k = 0u; k <= last; k++) {
                if (i != 0u && i != last && j != 0u && j != last && k != 0u && k != last) {
                    continue;
                }
                const float64_t x = (float64_t)i / (float64_t)last * 2.0f64 - 1.0f64;
                const float64_t y = (float64_t)j / (float64_t)last * 2.0f64 - 1.0f64;
                const float64_t z = (float64_t)k / (float64_t)last * 2.0f64 - 1.0f64;
                const float64_t length = sqrt(x * x + y * y + z * z);
                const float64_t way[COLLISION_AXES] = {x / length, y / length, z / length};
                explosion_ray(explosion, map, center, middle, way, power);
            }
        }
    }
}

static void explosion_hit_store(entity_store_t *store, const world_map_t *map, const float64_t center[COLLISION_AXES],
                                float64_t power) {
    for (uint32_t i = 0u; i < store->count; i++) {
        if ((store->borrowed_flags[i] & ENTITY_STORE_FLAG_WAITING) != 0u) {
            continue;
        }
        const float64_t position[COLLISION_AXES] = {store->borrowed_position[0][i], store->borrowed_position[1][i],
                                                    store->borrowed_position[2][i]};
        collision_box_t box;
        entity_store_box(store, i, &box);
        explosion_blow_t blow;
        if (!explosion_blow(map, center, power, position, &box,
                            (float64_t)store->borrowed_height[i] * EXPLOSION_EYE_SHARE, &blow)) {
            continue;
        }
        const float32_t health = store->borrowed_health[i] - blow.damage;
        store->borrowed_health[i] = health > 0.0f32 ? health : 0.0f32;
        for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
            store->borrowed_velocity[axis][i] += blow.push[axis];
        }
    }
}

/* An item with no strength left goes as in lava, and the rest fly. From the end, since a removal moves the last into the gap. */
static void explosion_hit_drops(item_drop_store_t *drops, const world_map_t *map,
                                const float64_t center[COLLISION_AXES], float64_t power) {
    for (uint32_t i = drops->count; i-- > 0u;) {
        item_drop_t *const item = &drops->borrowed_items[i];
        collision_box_t box;
        item_drop_box(item->position, &box);
        explosion_blow_t blow;
        if (!explosion_blow(map, center, power, item->position, &box, 0.0f64, &blow)) {
            continue;
        }
        if (blow.damage >= EXPLOSION_ITEM_HEALTH) {
            drops->destroyed_count += item->count;
            drops->count--;
            *item = drops->borrowed_items[drops->count];
            continue;
        }
        for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
            item->velocity[axis] += blow.push[axis];
        }
        item->on_ground = 0u;
    }
}

void explosion_begin(explosion_t *explosion, const world_map_t *map, entity_store_t *store, item_drop_store_t *drops,
                     const float64_t center[COLLISION_AXES], float64_t power) {
    ASSERT_ALWAYS(power > 0.0f64 && power <= EXPLOSION_POWER_MAX);
    explosion_cast(explosion, map, center, power);
    explosion_hit_store(store, map, center, power);
    explosion_hit_drops(drops, map, center, power);
    explosion->blast_count++;
}

/* What a block gives, one row at a time, as it gives them to a hand: except that a blast asks nothing of a tool. */
static void explosion_drops_spawn(explosion_t *explosion, item_drop_store_t *drops, uint16_t block, int32_t x,
                                  int32_t y, int32_t z) {
    const block_def_t *const def = &block_defs[block];
    const float64_t position[COLLISION_AXES] = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)z + 0.5f64};
    for (uint32_t row = 0u; row < def->drop_count; row++) {
        const block_drop_t *const drop = &block_drops[def->first_drop + row];
        /* Both numbers are taken whatever comes of them, so that one row's luck does not shift the next's. */
        const float64_t roll = terrain_rng_next_f64(&explosion->random);
        const uint32_t spread = terrain_rng_next_u32(&explosion->random);
        if (roll >= (float64_t)drop->chance) {
            continue;
        }
        const uint32_t choices = (uint32_t)drop->count_max - (uint32_t)drop->count_min + 1u;
        item_drop_spawn(drops, drop->item, (uint32_t)drop->count_min + spread % choices, position);
    }
}

/* One block goes, and one in power of them gives what it gives, but for a torch on a wall, which always gives its torch. False if the
 * edit found no room for it. A block already gone is no failure: what stood on one broken earlier may be on the list as well. */
static bool explosion_break_one(explosion_t *explosion, world_edit_t *edit, item_drop_store_t *drops, uint16_t block,
                                const int32_t cell[COLLISION_AXES], float64_t power, uint32_t *broken) {
    const world_edit_status_t status = world_edit_block_set(edit, cell[0], cell[1], cell[2], BLOCK_ID_AIR);
    if (status != WORLD_EDIT_STATUS_CHANGED) {
        return status != WORLD_EDIT_STATUS_NO_MEMORY;
    }
    (*broken)++;
    /* The number is taken whatever comes of it, so that a torch does not shift what the blocks after it give. */
    const bool lucky = terrain_rng_next_f64(&explosion->random) * power < 1.0f64;
    if (lucky || wall_torch_out(block, nullptr, nullptr)) {
        explosion_drops_spawn(explosion, drops, block, cell[0], cell[1], cell[2]);
    }
    return true;
}

/* What stands on the ground, and is gone without it. The same three shapes as for mining. */
static bool explosion_needs_ground(uint16_t block) {
    const uint8_t shape = block_defs[block].shape;
    return shape == BLOCK_SHAPE_CROSS || shape == BLOCK_SHAPE_CROP || shape == BLOCK_SHAPE_TORCH;
}

/* What hung on a block's sides goes with it, whether or not the blast would have reached it. */
static void explosion_wall_torches_break(explosion_t *explosion, world_edit_t *edit, item_drop_store_t *drops,
                                         const int32_t cell[COLLISION_AXES], float64_t power, uint32_t *broken) {
    for (uint32_t side = 0u; side < WALL_TORCH_SIDES; side++) {
        int32_t out_x = 0, out_y = 0;
        (void)wall_torch_out(WALL_TORCH_BLOCKS[side], &out_x, &out_y);
        const int32_t there[COLLISION_AXES] = {cell[0] + out_x, cell[1] + out_y, cell[2]};
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(edit->borrowed_map, there[0], there[1], there[2], &block) == WORLD_MAP_STATUS_OK &&
            block == WALL_TORCH_BLOCKS[side]) {
            (void)explosion_break_one(explosion, edit, drops, block, there, power, broken);
        }
    }
}

uint32_t explosion_finish(explosion_t *explosion, world_edit_t *edit, item_drop_store_t *drops, float64_t power) {
    uint32_t broken = 0u;
    for (uint32_t i = 0u; i < explosion->count; i++) {
        const explosion_cell_t *const listed = &explosion->borrowed_cells[i];
        int32_t cell[COLLISION_AXES] = {listed->x, listed->y, listed->z};
        if (!explosion_break_one(explosion, edit, drops, listed->block, cell, power, &broken)) {
            continue;
        }
        explosion_wall_torches_break(explosion, edit, drops, cell, power, &broken);
        for (uint32_t up = 0u; up < EXPLOSION_STACK_MAX; up++) {
            cell[2]++;
            uint16_t block = BLOCK_ID_AIR;
            if (world_map_block_get(edit->borrowed_map, cell[0], cell[1], cell[2], &block) != WORLD_MAP_STATUS_OK ||
                !explosion_needs_ground(block) ||
                !explosion_break_one(explosion, edit, drops, block, cell, power, &broken)) {
                break;
            }
        }
    }
    explosion->broken_count += broken;
    explosion->count = 0u;
    return broken;
}
