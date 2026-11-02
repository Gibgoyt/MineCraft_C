/* See README.md. Everything here is whole numbers but the rolls, so that a block takes the same ticks on any machine: 0.6 in a
 * float32_t is a little above 0.6, and would otherwise make 18 ticks into 19. */
#include "player/interaction/index.h"

#include "physics/collision/index.h"
#include "tables/blocks/index.h"

#include <math.h>
#include <string.h>

enum : uint64_t {
    /* A bare hand's work in one tick. A block needs its hardness in millionths times the divisor below, in the same units. */
    INTERACTION_UNITS_PER_HAND_TICK = 1000000ull,
    /* Minecraft's published rule: a block takes hardness * 1.5 seconds if it can be harvested and hardness * 5 if not, which at 20
     * ticks a second is hardness times these. */
    INTERACTION_TICKS_PER_HARDNESS_HARVEST = 30ull,
    INTERACTION_TICKS_PER_HARDNESS_NO_HARVEST = 100ull,
    /* Off the ground takes 5 times as long, and so do eyes under water. */
    INTERACTION_PENALTY = 5ull,
};

enum : uint32_t {
    INTERACTION_SPEED_HAND = 1000u,
    INTERACTION_SPEED_SWORD = 1500u,
    INTERACTION_SPEED_SHEARS = 2000u,
    INTERACTION_SPEED_SHEARS_LEAVES = 15000u,
};

/* A tool's published speed on its own blocks, by block_tier_t. */
static const uint32_t INTERACTION_TIER_SPEED[] = {
    [BLOCK_TIER_NONE] = INTERACTION_SPEED_HAND,
    [BLOCK_TIER_WOOD] = 2000u,
    [BLOCK_TIER_STONE] = 4000u,
    [BLOCK_TIER_IRON] = 6000u,
    [BLOCK_TIER_DIAMOND] = 8000u,
};

static uint64_t interaction_random_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void player_interaction_start(player_interaction_t *interaction, uint64_t seed) {
    memset(interaction, 0, sizeof(*interaction));
    interaction->random = seed;
    interaction->held_tool = BLOCK_TOOL_NONE;
    interaction->held_tier = BLOCK_TIER_NONE;
}

void player_interaction_hold_tool(player_interaction_t *interaction, uint8_t tool, uint8_t tier) {
    interaction->held_tool = tool;
    interaction->held_tier = tier;
}

bool player_interaction_harvests(uint16_t block, uint8_t tool, uint8_t tier) {
    const block_def_t *const def = &block_defs[block];
    return def->tier == BLOCK_TIER_NONE || (def->tool == tool && tier >= def->tier);
}

uint32_t player_interaction_speed_milli(uint16_t block, uint8_t tool, uint8_t tier) {
    const block_def_t *const def = &block_defs[block];
    if (tool == BLOCK_TOOL_NONE || def->tool != tool) {
        return INTERACTION_SPEED_HAND;
    }
    if (tool == BLOCK_TOOL_SHEARS) {
        return (def->flags & BLOCK_FLAG_DECAYS) != 0u ? INTERACTION_SPEED_SHEARS_LEAVES : INTERACTION_SPEED_SHEARS;
    }
    if (tool == BLOCK_TOOL_SWORD) {
        return INTERACTION_SPEED_SWORD;
    }
    return INTERACTION_TIER_SPEED[tier];
}

/* What breaking this block takes, in the units of one tick's work below. */
static uint64_t interaction_required(uint16_t block, uint8_t tool, uint8_t tier) {
    const float64_t hardness = (float64_t)block_defs[block].hardness;
    /* A millionth is the finest a block's file may say, so that this gives back the decimal it wrote. */
    const uint64_t millionths = (uint64_t)llround(hardness * 1000000.0f64);
    return millionths * (player_interaction_harvests(block, tool, tier) ? INTERACTION_TICKS_PER_HARDNESS_HARVEST
                                                                        : INTERACTION_TICKS_PER_HARDNESS_NO_HARVEST);
}

/* One tick's work. Never 0: the slowest is a hand, over 25. */
static uint64_t interaction_work(uint16_t block, uint8_t tool, uint8_t tier, bool on_ground, bool eyes_under_water) {
    uint64_t work =
        (uint64_t)player_interaction_speed_milli(block, tool, tier) * INTERACTION_UNITS_PER_HAND_TICK / 1000u;
    work /= on_ground ? 1u : INTERACTION_PENALTY;
    work /= eyes_under_water ? INTERACTION_PENALTY : 1u;
    return work;
}

uint32_t player_interaction_ticks(uint16_t block, uint8_t tool, uint8_t tier, bool on_ground, bool eyes_under_water) {
    if ((block_defs[block].flags & BLOCK_FLAG_UNBREAKABLE) != 0u) {
        return PLAYER_INTERACTION_TICKS_NEVER;
    }
    const uint64_t required = interaction_required(block, tool, tier);
    const uint64_t work = interaction_work(block, tool, tier, on_ground, eyes_under_water);
    return required == 0u ? 1u : (uint32_t)((required + work - 1u) / work);
}

float32_t player_interaction_progress(const player_interaction_t *interaction) {
    if (interaction->mining == 0u || interaction->progress == 0u) {
        return 0.0f32;
    }
    const uint64_t required =
        interaction_required(interaction->mining_block, interaction->held_tool, interaction->held_tier);
    return interaction->progress >= required ? 1.0f32
                                             : (float32_t)((float64_t)interaction->progress / (float64_t)required);
}

/* ---- Drops -------------------------------------------------------------------------------------------------------- */

static void interaction_drop_push(player_interaction_t *interaction, const player_drop_t *drop) {
    if (interaction->drop_count == PLAYER_INTERACTION_DROPS) {
        interaction->drop_head = (interaction->drop_head + 1u) % PLAYER_INTERACTION_DROPS;
        interaction->drop_count--;
        interaction->drops_lost_count++;
    }
    interaction->drops[(interaction->drop_head + interaction->drop_count) % PLAYER_INTERACTION_DROPS] = *drop;
    interaction->drop_count++;
}

void player_interaction_drops_spawn(player_interaction_t *interaction, uint16_t block, int32_t x, int32_t y,
                                    int32_t z) {
    const block_def_t *const def = &block_defs[block];
    for (uint32_t row = 0u; row < def->drop_count; row++) {
        const block_drop_t *const drop = &block_drops[def->first_drop + row];
        /* Both numbers are taken whatever comes of them, so that one row's luck does not shift the next's. */
        const uint64_t roll = interaction_random_next(&interaction->random);
        const uint64_t spread = interaction_random_next(&interaction->random);
        /* 53 bits, the most a float64_t holds: from 0 up to just below 1, so that a chance of 1 always passes. */
        if ((float64_t)(roll >> 11u) * 0x1.0p-53f64 >= (float64_t)drop->chance) {
            continue;
        }
        const uint64_t choices = (uint64_t)drop->count_max - (uint64_t)drop->count_min + 1u;
        const player_drop_t out = {
            .position = {(float64_t)x + 0.5f64, (float64_t)y + 0.5f64, (float64_t)z + 0.5f64},
            .item = drop->item,
            .count = (uint8_t)(drop->count_min + spread % choices),
            ._pad0 = {0},
        };
        interaction_drop_push(interaction, &out);
    }
}

bool player_interaction_drop_next(player_interaction_t *interaction, player_drop_t *out) {
    if (interaction->drop_count == 0u) {
        return false;
    }
    *out = interaction->drops[interaction->drop_head];
    interaction->drop_head = (interaction->drop_head + 1u) % PLAYER_INTERACTION_DROPS;
    interaction->drop_count--;
    return true;
}

void player_interaction_hand_give(player_interaction_t *interaction, uint16_t block, uint32_t count) {
    if (interaction->hand_count == 0u || interaction->hand_block != block) {
        interaction->hand_block = block;
        interaction->hand_count = 0u;
    }
    const uint32_t total = (uint32_t)interaction->hand_count + count;
    interaction->hand_count = (uint8_t)(total > PLAYER_INTERACTION_HAND_MAX ? PLAYER_INTERACTION_HAND_MAX : total);
}

uint32_t player_interaction_hand_take(player_interaction_t *interaction, uint16_t item, uint32_t count) {
    const uint16_t block = block_drop_items[item].block;
    if (block == BLOCK_NO_ID || (interaction->hand_count != 0u && interaction->hand_block != block)) {
        return 0u;
    }
    const uint32_t room = PLAYER_INTERACTION_HAND_MAX - (uint32_t)interaction->hand_count;
    const uint32_t taken = count < room ? count : room;
    if (taken != 0u) {
        player_interaction_hand_give(interaction, block, taken);
        interaction->picked_up_count += taken;
    }
    return taken;
}

/* ---- Mining ------------------------------------------------------------------------------------------------------- */

static void interaction_mining_stop(player_interaction_t *interaction) {
    interaction->mining = 0u;
    interaction->progress = 0u;
}

/* What stands on the ground and is gone without it. A crop too: world/tick would see to it, but with nothing to show for it. */
static bool interaction_needs_ground(uint16_t block) {
    const uint8_t shape = block_defs[block].shape;
    return shape == BLOCK_SHAPE_CROSS || shape == BLOCK_SHAPE_CROP || shape == BLOCK_SHAPE_TORCH;
}

/* What stood on a block just broken goes with it, and gives what it gives to a hand. */
static void interaction_stack_break(player_interaction_t *interaction, world_edit_t *edit, int32_t x, int32_t y,
                                    int32_t z) {
    for (int32_t above = z + 1; above <= z + (int32_t)PLAYER_INTERACTION_STACK_MAX; above++) {
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(edit->borrowed_map, x, y, above, &block) != WORLD_MAP_STATUS_OK ||
            !interaction_needs_ground(block)) {
            return;
        }
        if (world_edit_block_set(edit, x, y, above, BLOCK_ID_AIR) != WORLD_EDIT_STATUS_CHANGED) {
            interaction->no_memory_count++;
            return;
        }
        if (player_interaction_harvests(block, BLOCK_TOOL_NONE, BLOCK_TIER_NONE)) {
            player_interaction_drops_spawn(interaction, block, x, y, above);
        }
    }
}

static void interaction_mine(player_interaction_t *interaction, world_edit_t *edit,
                             const player_interaction_input_t *input, const raycast_hit_t *hit) {
    const int32_t x = hit->cell[0], y = hit->cell[1], z = hit->cell[2];
    const uint16_t block = hit->block;
    const bool same = interaction->mining != 0u && interaction->mining_block == block && interaction->cell[0] == x &&
                      interaction->cell[1] == y && interaction->cell[2] == z;
    if (!same) {
        interaction->mining = 1u;
        interaction->mining_block = block;
        interaction->cell[0] = x;
        interaction->cell[1] = y;
        interaction->cell[2] = z;
        interaction->progress = 0u;
    }
    if ((block_defs[block].flags & BLOCK_FLAG_UNBREAKABLE) != 0u) {
        return;
    }
    const uint8_t tool = interaction->held_tool, tier = interaction->held_tier;
    const uint64_t required = interaction_required(block, tool, tier);
    const uint64_t work = interaction_work(block, tool, tier, input->borrowed_movement->on_ground != 0u,
                                           input->borrowed_movement->eyes_under_water != 0u);
    interaction->progress += work;
    if (interaction->progress < required) {
        return;
    }
    const world_edit_status_t status = world_edit_block_set(edit, x, y, z, BLOCK_ID_AIR);
    if (status == WORLD_EDIT_STATUS_NO_MEMORY) {
        /* It stays all but broken, and the next tick tries again. */
        interaction->no_memory_count++;
        return;
    }
    interaction_mining_stop(interaction);
    if (status != WORLD_EDIT_STATUS_CHANGED) {
        return;
    }
    interaction->mined_count++;
    interaction->last_mined = block;
    if (player_interaction_harvests(block, tool, tier)) {
        player_interaction_drops_spawn(interaction, block, x, y, z);
    }
    interaction_stack_break(interaction, edit, x, y, z);
    /* What breaks at a touch can be swept through. */
    interaction->mine_delay = required > work ? PLAYER_INTERACTION_MINE_DELAY_TICKS : 0u;
}

/* ---- Placing ------------------------------------------------------------------------------------------------------ */

static bool interaction_fits(uint16_t block, int32_t x, int32_t y, int32_t z, const player_movement_t *movement) {
    collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
    const uint32_t count = collision_block_boxes(block, x, y, z, boxes);
    const float64_t height =
        movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
    collision_box_t body;
    player_movement_box(movement->position, height, &body);
    return !collision_overlaps_any(boxes, count, &body);
}

/* What the cell can take: a non-solid block does not wash a fluid away, and what needs ground has to have it. */
static bool interaction_cell_takes(const world_map_t *map, uint16_t block, uint16_t there, int32_t x, int32_t y,
                                   int32_t z) {
    if ((block_defs[there].flags & BLOCK_FLAG_REPLACEABLE) == 0u) {
        return false;
    }
    const bool solid = (block_defs[block].flags & BLOCK_FLAG_SOLID) != 0u;
    if (!solid && block_defs[there].shape == BLOCK_SHAPE_FLUID) {
        return false;
    }
    if (!interaction_needs_ground(block)) {
        return true;
    }
    uint16_t below = BLOCK_ID_AIR;
    return world_map_block_get(map, x, y, z - 1, &below) == WORLD_MAP_STATUS_OK &&
           (block_defs[below].flags & BLOCK_FLAG_SOLID) != 0u;
}

static void interaction_place(player_interaction_t *interaction, world_edit_t *edit,
                              const player_interaction_input_t *input, const raycast_hit_t *hit) {
    if (interaction->hand_count == 0u || hit->axis == RAYCAST_AXIS_NONE) {
        return;
    }
    /* Into what can be overwritten if that is what it aims at, like tall grass, and otherwise on the face it came in through. */
    int32_t cell[COLLISION_AXES] = {hit->cell[0], hit->cell[1], hit->cell[2]};
    if ((block_defs[hit->block].flags & BLOCK_FLAG_REPLACEABLE) == 0u) {
        raycast_hit_in_front(hit, cell);
    }
    uint16_t there = BLOCK_ID_AIR;
    const uint16_t block = interaction->hand_block;
    /* Outside gameplay's heights, and in a column that has not come, this is not OK. */
    if (world_map_block_get(edit->borrowed_map, cell[0], cell[1], cell[2], &there) != WORLD_MAP_STATUS_OK ||
        !interaction_cell_takes(edit->borrowed_map, block, there, cell[0], cell[1], cell[2]) ||
        !interaction_fits(block, cell[0], cell[1], cell[2], input->borrowed_movement)) {
        return;
    }
    const world_edit_status_t status = world_edit_block_set(edit, cell[0], cell[1], cell[2], block);
    if (status == WORLD_EDIT_STATUS_NO_MEMORY) {
        interaction->no_memory_count++;
    }
    if (status != WORLD_EDIT_STATUS_CHANGED) {
        return;
    }
    interaction->hand_count--;
    interaction->placed_count++;
    interaction->place_delay = PLAYER_INTERACTION_PLACE_EVERY_TICKS - 1u;
}

/* ---- One tick ------------------------------------------------------------------------------------------------------ */

void player_interaction_tick(player_interaction_t *interaction, world_edit_t *edit,
                             const player_interaction_input_t *input) {
    const bool may_mine = interaction->mine_delay == 0u, may_place = interaction->place_delay == 0u;
    interaction->mine_delay = may_mine ? 0u : (uint8_t)(interaction->mine_delay - 1u);
    interaction->place_delay = may_place ? 0u : (uint8_t)(interaction->place_delay - 1u);

    const raycast_hit_t *const hit = &input->borrowed_target->hit;
    uint16_t there = BLOCK_ID_AIR;
    /* The aim is a tick old, and the world may have moved on: water came in, or sand fell. */
    const bool aimed = hit->result == RAYCAST_RESULT_HIT &&
                       world_map_block_get(edit->borrowed_map, hit->cell[0], hit->cell[1], hit->cell[2], &there) ==
                           WORLD_MAP_STATUS_OK &&
                       there == hit->block;
    if (input->attack == 0u || !aimed) {
        interaction_mining_stop(interaction);
    }
    if (!aimed) {
        return;
    }
    if (input->attack != 0u) {
        if (may_mine) {
            interaction_mine(interaction, edit, input, hit);
        }
    } else if (input->place != 0u && may_place) {
        interaction_place(interaction, edit, input, hit);
    }
}
