/* See README.md. Everything here is whole numbers but the rolls, so that a block takes the same ticks on any machine: 0.6 in a
 * float32_t is a little above 0.6, and would otherwise make 18 ticks into 19. */
#include "player/interaction/index.h"

#include "physics/collision/index.h"
#include "player/felling/index.h"
#include "tables/blocks/index.h"
#include "world/bench/index.h"
#include "world/block_state/index.h"
#include "world/door/index.h"
#include "world/wall_torch/index.h"

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

void player_interaction_hold_speed(player_interaction_t *interaction, uint32_t speed_milli) {
    interaction->held_speed_milli = speed_milli;
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
static uint64_t interaction_work(uint32_t speed_milli, bool on_ground, bool eyes_under_water) {
    uint64_t work = (uint64_t)speed_milli * INTERACTION_UNITS_PER_HAND_TICK / 1000u;
    work /= on_ground ? 1u : INTERACTION_PENALTY;
    work /= eyes_under_water ? INTERACTION_PENALTY : 1u;
    return work;
}

uint32_t player_interaction_ticks(uint16_t block, uint8_t tool, uint8_t tier, bool on_ground, bool eyes_under_water) {
    if ((block_defs[block].flags & BLOCK_FLAG_UNBREAKABLE) != 0u) {
        return PLAYER_INTERACTION_TICKS_NEVER;
    }
    const uint64_t required = interaction_required(block, tool, tier);
    const uint64_t work =
        interaction_work(player_interaction_speed_milli(block, tool, tier), on_ground, eyes_under_water);
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
    if (interaction->hand_block != block) {
        /* R's turns are for what was held: something else starts as the player faces. */
        interaction->turns = 0u;
    }
    if (interaction->hand_count == 0u || interaction->hand_block != block) {
        interaction->hand_block = block;
        interaction->hand_count = 0u;
    }
    const uint32_t total = (uint32_t)interaction->hand_count + count;
    interaction->hand_count = (uint8_t)(total > PLAYER_INTERACTION_HAND_MAX ? PLAYER_INTERACTION_HAND_MAX : total);
}

void player_interaction_hand_set(player_interaction_t *interaction, uint16_t block, uint32_t count) {
    interaction->hand_count = 0u;
    player_interaction_hand_give(interaction, block, count);
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

/* ---- Sounds ------------------------------------------------------------------------------------------------------- */

void player_sounds_post(player_sounds_t *sounds, uint16_t block, sound_kind_t kind, int32_t x, int32_t y, int32_t z) {
    const uint8_t family = block_defs[block].sound;
    if (family == SOUND_FAMILY_NONE) {
        return;
    }
    if (sounds->count == PLAYER_SOUNDS_RING) {
        sounds->head = (sounds->head + 1u) % PLAYER_SOUNDS_RING;
        sounds->count--;
        sounds->lost_count++;
    }
    sounds->sounds[(sounds->head + sounds->count) % PLAYER_SOUNDS_RING] = (player_sound_t){
        .cell = {x, y, z},
        .family = family,
        .kind = (uint8_t)kind,
        ._pad0 = {0},
    };
    sounds->count++;
}

bool player_sounds_next(player_sounds_t *sounds, player_sound_t *out) {
    if (sounds->count == 0u) {
        return false;
    }
    *out = sounds->sounds[sounds->head];
    sounds->head = (sounds->head + 1u) % PLAYER_SOUNDS_RING;
    sounds->count--;
    return true;
}

/* Into the input's, if it came with somewhere to put them. */
static void interaction_sound(const player_interaction_input_t *input, uint16_t block, sound_kind_t kind, int32_t x,
                              int32_t y, int32_t z) {
    if (input->borrowed_sounds != nullptr) {
        player_sounds_post(input->borrowed_sounds, block, kind, x, y, z);
    }
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

/* What hung on the sides of a block just broken. Only a torch that faces it: another beside it has its own wall. */
static void interaction_wall_torches_break(player_interaction_t *interaction, world_edit_t *edit, int32_t x, int32_t y,
                                           int32_t z) {
    for (uint32_t side = 0u; side < WALL_TORCH_SIDES; side++) {
        const int32_t out_x = block_facing_step_x(side), out_y = block_facing_step_y(side);
        uint16_t voxel = BLOCK_ID_AIR;
        if (world_map_voxel_get(edit->borrowed_map, x + out_x, y + out_y, z, &voxel) != WORLD_MAP_STATUS_OK ||
            !wall_torch_hangs_from(voxel, out_x, out_y)) {
            continue;
        }
        const uint16_t torch = block_state_id(voxel);
        if (world_edit_block_set(edit, x + out_x, y + out_y, z, BLOCK_ID_AIR) != WORLD_EDIT_STATUS_CHANGED) {
            interaction->no_memory_count++;
            continue;
        }
        player_interaction_drops_spawn(interaction, torch, x + out_x, y + out_y, z);
    }
}

void player_interaction_stack_break(player_interaction_t *interaction, world_edit_t *edit, int32_t x, int32_t y,
                                    int32_t z) {
    interaction_wall_torches_break(interaction, edit, x, y, z);
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

/* The other cells of a block more than one cell big, a station's other parts beside it (a bench's other half, the smith table's other
 * three) or a door's other half above or below it: the step to each from this voxel and what must be there. Returns how many, 0 for a
 * block of one cell. */
static uint32_t interaction_parts_other(uint16_t voxel, int32_t steps[PLAYER_PLACE_CELLS - 1u][COLLISION_AXES],
                                        uint16_t others[PLAYER_PLACE_CELLS - 1u]) {
    static_assert((uint32_t)BENCH_PARTS_MAX <= (uint32_t)PLAYER_PLACE_CELLS);
    int32_t bench_steps[BENCH_PARTS_MAX][2];
    uint16_t bench_voxels[BENCH_PARTS_MAX];
    const uint32_t parts = bench_parts(voxel, bench_steps, bench_voxels);
    uint32_t count = 0u;
    for (uint32_t p = 0u; p < parts; p++) {
        if (bench_steps[p][0] == 0 && bench_steps[p][1] == 0) {
            continue; /* this one */
        }
        steps[count][0] = bench_steps[p][0];
        steps[count][1] = bench_steps[p][1];
        steps[count][2] = 0;
        others[count] = bench_voxels[p];
        count++;
    }
    if (count == 0u) {
        steps[0][0] = 0;
        steps[0][1] = 0;
        if (door_half_other(voxel, &steps[0][2], &others[0])) {
            count = 1u;
        }
    }
    return count;
}

/* The other parts of a station or the other half of a door go first and give nothing, as the one mined gives the one station or door.
 * False if they cannot go yet, a column not loaded, which leaves it whole and all but broken for the next tick. If the part mined then
 * finds no room, the next tick breaks it alone, and it drops. The part mined's state says where the others are. */
static bool interaction_parts_other_break(player_interaction_t *interaction, world_edit_t *edit, uint16_t voxel,
                                          int32_t x, int32_t y, int32_t z) {
    int32_t steps[PLAYER_PLACE_CELLS - 1u][COLLISION_AXES];
    uint16_t others[PLAYER_PLACE_CELLS - 1u];
    const uint32_t count = interaction_parts_other(voxel, steps, others);
    bool present[PLAYER_PLACE_CELLS - 1u] = {false};
    for (uint32_t k = 0u; k < count; k++) {
        uint16_t there = BLOCK_ID_AIR;
        if (world_map_voxel_get(edit->borrowed_map, x + steps[k][0], y + steps[k][1], z + steps[k][2], &there) !=
            WORLD_MAP_STATUS_OK) {
            return false;
        }
        present[k] = there == others[k];
    }
    for (uint32_t k = 0u; k < count; k++) {
        if (!present[k]) {
            continue;
        }
        const int32_t ox = x + steps[k][0], oy = y + steps[k][1], oz = z + steps[k][2];
        if (world_edit_block_set(edit, ox, oy, oz, BLOCK_ID_AIR) != WORLD_EDIT_STATUS_CHANGED) {
            interaction->no_memory_count++;
            return false;
        }
        player_interaction_stack_break(interaction, edit, ox, oy, oz);
    }
    return true;
}

/* The button is on this block and it has not gone: a short hit, every so often, starting at once. */
static void interaction_hit(player_interaction_t *interaction, const player_interaction_input_t *input, uint16_t block,
                            int32_t x, int32_t y, int32_t z) {
    if (interaction->hit_wait != 0u) {
        interaction->hit_wait--;
        return;
    }
    interaction->hit_wait = PLAYER_INTERACTION_HIT_EVERY_TICKS - 1u;
    interaction_sound(input, block, SOUND_KIND_HIT, x, y, z);
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
        interaction->hit_wait = 0u;
    }
    if ((block_defs[block].flags & BLOCK_FLAG_UNBREAKABLE) != 0u) {
        interaction_hit(interaction, input, block, x, y, z);
        return;
    }
    const uint8_t tool = interaction->held_tool, tier = interaction->held_tier;
    const uint64_t required = interaction_required(block, tool, tier);
    const bool own_block = tool != BLOCK_TOOL_NONE && block_defs[block].tool == tool;
    const uint32_t speed_milli = own_block && interaction->held_speed_milli != 0u
                                     ? interaction->held_speed_milli
                                     : player_interaction_speed_milli(block, tool, tier);
    const uint64_t work = interaction_work(speed_milli, input->borrowed_movement->on_ground != 0u,
                                           input->borrowed_movement->eyes_under_water != 0u);
    interaction->progress += work;
    if (interaction->progress < required) {
        interaction_hit(interaction, input, block, x, y, z);
        return;
    }
    uint16_t voxel = block;
    if (world_map_voxel_get(edit->borrowed_map, x, y, z, &voxel) != WORLD_MAP_STATUS_OK ||
        !interaction_parts_other_break(interaction, edit, voxel, x, y, z)) {
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
    interaction_sound(input, block, SOUND_KIND_BREAK, x, y, z);
    /* Shears on their own block give that block and nothing else. run/src/game/ brings it out, as only it knows items. */
    const bool shorn = tool == BLOCK_TOOL_SHEARS && block_defs[block].tool == BLOCK_TOOL_SHEARS;
    if (!shorn && player_interaction_harvests(block, tool, tier)) {
        player_interaction_drops_spawn(interaction, block, x, y, z);
    }
    player_interaction_stack_break(interaction, edit, x, y, z);
    /* A log cut brings the rest of its tree down, with any tool or none, sneaking or not: it is part of the game. */
    if (input->borrowed_felling != nullptr && (block_defs[block].flags & BLOCK_FLAG_LOG) != 0u) {
        (void)player_felling_cut(input->borrowed_felling, edit->borrowed_map, block, x, y, z);
    }
    /* What breaks at a touch can be swept through. */
    interaction->mine_delay = required > work ? PLAYER_INTERACTION_MINE_DELAY_TICKS : 0u;
}

/* ---- Placing ------------------------------------------------------------------------------------------------------ */

static bool interaction_fits(uint16_t voxel, int32_t x, int32_t y, int32_t z, const player_movement_t *movement) {
    collision_box_t boxes[COLLISION_BOXES_PER_BLOCK];
    const uint32_t count = collision_block_boxes(voxel, x, y, z, boxes);
    const float64_t height =
        movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
    collision_box_t body;
    player_movement_box(movement->position, height, &body);
    return !collision_overlaps_any(boxes, count, &body);
}

/* What the cell can take: a non-solid block does not wash a fluid away, and what needs ground has to have it. Both are ids. */
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

/* The facing a turning block goes down with: the way the player looks, the nearest of four, turned by R. A plate put against the side of
 * a block, a ladder or a vine, faces that side instead, so that it lies on the wall aimed at, and R turns it from there. */
static uint32_t interaction_facing(const player_interaction_t *interaction, const player_interaction_input_t *input,
                                   const raycast_hit_t *hit, uint16_t held) {
    uint32_t facing = input->look_facing;
    if (block_defs[held].shape == BLOCK_SHAPE_THIN &&
        (hit->axis == COLLISION_AXIS_X || hit->axis == COLLISION_AXIS_Y)) {
        /* Into the face: the other way from where it looks. */
        const int32_t into = -(int32_t)hit->normal_sign;
        facing =
            block_facing_of_step(hit->axis == COLLISION_AXIS_X ? into : 0, hit->axis == COLLISION_AXIS_Y ? into : 0);
    }
    return block_facing_turn(facing, interaction->turns);
}

/* What a hand puts down, aiming at this face, into this cell, with its state. A torch goes by the face: on a side, it hangs from the block
 * behind it if that is solid, and otherwise, as on a top, it stands on the floor. On a block's underside it has nothing to stand on or hang
 * from, so BLOCK_NO_ID. Anything else goes as it is, facing the way interaction_facing says if it turns. */
static uint16_t interaction_voxel_to_place(const world_map_t *map, const player_interaction_t *interaction,
                                           const player_interaction_input_t *input, const raycast_hit_t *hit,
                                           uint16_t held, const int32_t cell[COLLISION_AXES]) {
    if (held != BLOCK_ID_TORCH) {
        return block_state_placed(held, interaction_facing(interaction, input, hit, held));
    }
    if (hit->axis == COLLISION_AXIS_Z) {
        const bool aimed_into = cell[COLLISION_AXIS_Z] == hit->cell[COLLISION_AXIS_Z];
        return hit->normal_sign < 0 && !aimed_into ? BLOCK_NO_ID : BLOCK_ID_TORCH;
    }
    const int32_t out_x = hit->axis == COLLISION_AXIS_X ? (int32_t)hit->normal_sign : 0;
    const int32_t out_y = hit->axis == COLLISION_AXIS_Y ? (int32_t)hit->normal_sign : 0;
    uint16_t behind = BLOCK_ID_AIR;
    if (world_map_block_get(map, cell[0] - out_x, cell[1] - out_y, cell[2], &behind) == WORLD_MAP_STATUS_OK &&
        (block_defs[behind].flags & BLOCK_FLAG_SOLID) != 0u) {
        return wall_torch_of(out_x, out_y);
    }
    return BLOCK_ID_TORCH;
}

/* How much of a wall is at one side of a door going into cell, beside its lower and upper halves, and whether a door's lower half is beside
 * its own. A column not loaded is no wall. */
static void interaction_door_side(const world_map_t *map, const int32_t cell[COLLISION_AXES], uint32_t side,
                                  bool *out_door, uint32_t *out_walls) {
    const int32_t x = cell[0] + block_facing_step_x(side), y = cell[1] + block_facing_step_y(side);
    *out_door = false;
    *out_walls = 0u;
    for (int32_t up = 0; up < 2; up++) {
        uint16_t there = BLOCK_ID_AIR;
        if (world_map_voxel_get(map, x, y, cell[2] + up, &there) != WORLD_MAP_STATUS_OK) {
            continue;
        }
        const uint16_t id = block_state_id(there);
        *out_walls += (block_defs[id].flags & BLOCK_FLAG_SOLID) != 0u ? 1u : 0u;
        *out_door = *out_door || (up == 0 && door_is(id) && !block_state_upper(there));
    }
}

/* A door's lower half as it is to go into cell: turned round if its hinge is to be on the right, by what is beside it
 * (run/src/world/door/README.md). */
static uint16_t interaction_door_hinged(const world_map_t *map, uint16_t voxel, const int32_t cell[COLLISION_AXES]) {
    const uint32_t facing = block_state_facing(voxel);
    bool door_left = false, door_right = false;
    uint32_t left_walls = 0u, right_walls = 0u;
    interaction_door_side(map, cell, block_facing_turn(facing, 3u), &door_left, &left_walls);
    interaction_door_side(map, cell, block_facing_turn(facing, 1u), &door_right, &right_walls);
    return block_state_make(block_state_id(voxel),
                            door_hinged_facing(facing, door_left, door_right, left_walls, right_walls), false, false);
}

/* ---- Placing ahead, for bridging (plan 005 phase 13) ------------------------------------------------------------------ */

/* Below the soles, so that feet standing on a top at a whole number find the block under it and not the air above. */
#define INTERACTION_AHEAD_BELOW_FEET 1.0e-3f64

/* The solid block the feet stand on: of the cells under the body's footprint at the height just below the soles, the nearest to the
 * middle of the feet, so that a body sneaking out past an edge still stands on the block it hangs from. False if none is solid. */
static bool interaction_ahead_support(const world_map_t *map, const player_movement_t *movement,
                                      int32_t out[COLLISION_AXES], uint16_t *out_block) {
    const float64_t half = PLAYER_MOVEMENT_WIDTH * 0.5f64 - INTERACTION_AHEAD_BELOW_FEET;
    const float64_t px = movement->position[0], py = movement->position[1];
    const int32_t z = (int32_t)floor(movement->position[2] - INTERACTION_AHEAD_BELOW_FEET);
    const int32_t x0 = (int32_t)floor(px - half), x1 = (int32_t)floor(px + half);
    const int32_t y0 = (int32_t)floor(py - half), y1 = (int32_t)floor(py + half);
    bool found = false;
    float64_t best = 0.0f64;
    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t x = x0; x <= x1; x++) {
            uint16_t block = BLOCK_ID_AIR;
            if (world_map_block_get(map, x, y, z, &block) != WORLD_MAP_STATUS_OK ||
                (block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u) {
                continue;
            }
            const float64_t dx = (float64_t)x + 0.5f64 - px, dy = (float64_t)y + 0.5f64 - py;
            const float64_t distance = dx * dx + dy * dy;
            if (!found || distance < best) {
                found = true;
                best = distance;
                out[0] = x;
                out[1] = y;
                out[2] = z;
                *out_block = block;
            }
        }
    }
    return found;
}

/* Whether any of the cell is within PLAYER_CAMERA_REACH of the eye. */
static bool interaction_ahead_in_reach(const player_movement_t *movement, const int32_t cell[COLLISION_AXES]) {
    float64_t squared = 0.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        const float64_t eye = movement->position[a] + (a == COLLISION_AXIS_Z ? movement->eye_height : 0.0f64);
        const float64_t low = (float64_t)cell[a], high = low + 1.0f64;
        const float64_t gap = eye < low ? low - eye : (eye > high ? eye - high : 0.0f64);
        squared += gap * gap;
    }
    return squared <= PLAYER_CAMERA_REACH * PLAYER_CAMERA_REACH;
}

/* When the aim finds nothing in reach, looking level or down: the cell beside the block underfoot, the way the player faces (the
 * nearest of four), at that block's height, if it is loaded, empty or replaceable, and in reach. Given as the hit the aim would have
 * made on that block's side, so that everything after, the voxel, its facing, the fit and a bench's other half, is placing's own.
 * False, and nothing to show, otherwise: walking on ground looking ahead shows nothing, and only an edge shows the cell beyond. */
static bool interaction_ahead_hit(const world_map_t *map, const player_interaction_input_t *input, raycast_hit_t *out) {
    if (input->look_ahead == 0u || input->borrowed_target->hit.result != RAYCAST_RESULT_MISS) {
        return false;
    }
    int32_t support[COLLISION_AXES];
    uint16_t block = BLOCK_ID_AIR;
    if (!interaction_ahead_support(map, input->borrowed_movement, support, &block)) {
        return false;
    }
    const int32_t step_x = block_facing_step_x(input->look_facing), step_y = block_facing_step_y(input->look_facing);
    const int32_t cell[COLLISION_AXES] = {support[0] + step_x, support[1] + step_y, support[2]};
    uint16_t there = BLOCK_ID_AIR;
    if (world_map_block_get(map, cell[0], cell[1], cell[2], &there) != WORLD_MAP_STATUS_OK ||
        (block_defs[there].flags & BLOCK_FLAG_REPLACEABLE) == 0u ||
        !interaction_ahead_in_reach(input->borrowed_movement, cell)) {
        return false;
    }
    *out = (raycast_hit_t){
        .cell = {support[0], support[1], support[2]},
        .block = block,
        .result = RAYCAST_RESULT_HIT,
        .axis = step_x != 0 ? (uint8_t)COLLISION_AXIS_X : (uint8_t)COLLISION_AXIS_Y,
        .normal_sign = (int8_t)(step_x != 0 ? step_x : step_y),
    };
    return true;
}

player_place_status_t player_interaction_place_plan(const player_interaction_t *interaction, const world_map_t *map,
                                                    const player_interaction_input_t *input, player_place_plan_t *out) {
    *out = (player_place_plan_t){.status = PLAYER_PLACE_NONE};
    const raycast_hit_t *hit = &input->borrowed_target->hit;
    raycast_hit_t ahead;
    if (interaction_ahead_hit(map, input, &ahead)) {
        hit = &ahead;
    }
    if (interaction->hand_count == 0u || hit->result != RAYCAST_RESULT_HIT || hit->axis == RAYCAST_AXIS_NONE) {
        return PLAYER_PLACE_NONE;
    }
    /* Into what can be overwritten if that is what it aims at, like tall grass, and otherwise on the face it came in through. */
    int32_t *const cell = out->cells[0];
    cell[0] = hit->cell[0];
    cell[1] = hit->cell[1];
    cell[2] = hit->cell[2];
    if ((block_defs[hit->block].flags & BLOCK_FLAG_REPLACEABLE) == 0u) {
        raycast_hit_in_front(hit, cell);
    }
    uint16_t voxel = interaction_voxel_to_place(map, interaction, input, hit, interaction->hand_block, cell);
    if (voxel == BLOCK_NO_ID) {
        return PLAYER_PLACE_NONE;
    }
    if (door_is(block_state_id(voxel))) {
        voxel = interaction_door_hinged(map, voxel, cell);
    }
    const uint16_t block = block_state_id(voxel);
    out->voxels[0] = voxel;
    out->count = 1u;
    out->facing = (uint8_t)block_state_facing(voxel);
    out->status = PLAYER_PLACE_OK;
    /* Outside gameplay's heights, and in a column that has not come, this is not OK. */
    uint16_t there = BLOCK_ID_AIR;
    if (world_map_voxel_get(map, cell[0], cell[1], cell[2], &there) != WORLD_MAP_STATUS_OK ||
        !interaction_cell_takes(map, block, block_state_id(there), cell[0], cell[1], cell[2]) ||
        !interaction_fits(voxel, cell[0], cell[1], cell[2], input->borrowed_movement)) {
        out->status = PLAYER_PLACE_BLOCKED;
    }
    out->was_there[0] = there;
    /* A station's first part brings its others, a bench's other half or the smith table's other three, and the lower half of a door its
     * upper, which must all go down too: a part of either is never left. */
    int32_t steps[PLAYER_PLACE_CELLS - 1u][COLLISION_AXES];
    uint16_t others[PLAYER_PLACE_CELLS - 1u];
    const uint32_t extra = interaction_parts_other(voxel, steps, others);
    for (uint32_t k = 0u; k < extra; k++) {
        int32_t *const next = out->cells[k + 1u];
        next[0] = cell[0] + steps[k][0];
        next[1] = cell[1] + steps[k][1];
        next[2] = cell[2] + steps[k][2];
        out->voxels[k + 1u] = others[k];
        uint16_t there_next = BLOCK_ID_AIR;
        const bool loaded = world_map_voxel_get(map, next[0], next[1], next[2], &there_next) == WORLD_MAP_STATUS_OK;
        out->was_there[k + 1u] = there_next;
        if (!loaded ||
            !interaction_cell_takes(map, block_state_id(others[k]), block_state_id(there_next), next[0], next[1],
                                    next[2]) ||
            !interaction_fits(others[k], next[0], next[1], next[2], input->borrowed_movement)) {
            out->status = PLAYER_PLACE_BLOCKED;
        }
    }
    out->count = (uint8_t)(1u + extra);
    return (player_place_status_t)out->status;
}

void player_interaction_turn(player_interaction_t *interaction) {
    interaction->turns = (uint8_t)block_facing_turn(interaction->turns, 1u);
}

static void interaction_place(player_interaction_t *interaction, world_edit_t *edit,
                              const player_interaction_input_t *input) {
    player_place_plan_t plan;
    if (player_interaction_place_plan(interaction, edit->borrowed_map, input, &plan) != PLAYER_PLACE_OK) {
        return;
    }
    const int32_t *const cell = plan.cells[0];
    const world_edit_status_t status = world_edit_block_set(edit, cell[0], cell[1], cell[2], plan.voxels[0]);
    if (status == WORLD_EDIT_STATUS_NO_MEMORY) {
        interaction->no_memory_count++;
    }
    if (status != WORLD_EDIT_STATUS_CHANGED) {
        return;
    }
    for (uint32_t k = 1u; k < plan.count; k++) {
        const int32_t *const next = plan.cells[k];
        if (world_edit_block_set(edit, next[0], next[1], next[2], plan.voxels[k]) != WORLD_EDIT_STATUS_CHANGED) {
            /* No room for another part, so what went down comes back out and nothing was placed. */
            interaction->no_memory_count++;
            for (uint32_t back = k; back > 0u; back--) {
                const int32_t *const undo = plan.cells[back - 1u];
                (void)world_edit_block_set(edit, undo[0], undo[1], undo[2], plan.was_there[back - 1u]);
            }
            return;
        }
    }
    interaction->hand_count--;
    interaction->placed_count++;
    interaction_sound(input, block_state_id(plan.voxels[0]), SOUND_KIND_PLACE, cell[0], cell[1], cell[2]);
    interaction->place_delay = PLAYER_INTERACTION_PLACE_EVERY_TICKS - 1u;
}

/* ---- Doors -------------------------------------------------------------------------------------------------------- */

/* The right button on a door that opens by hand: both halves open, or both shut. A half whose other half is not there as it should be,
 * from a file written by hand, turns alone; one whose other half's column is not loaded waits. */
static void interaction_door_use(player_interaction_t *interaction, world_edit_t *edit,
                                 const player_interaction_input_t *input, const int32_t cell[COLLISION_AXES]) {
    uint16_t voxel = BLOCK_ID_AIR;
    int32_t step_z = 0;
    uint16_t other = BLOCK_ID_AIR;
    if (world_map_voxel_get(edit->borrowed_map, cell[0], cell[1], cell[2], &voxel) != WORLD_MAP_STATUS_OK ||
        !door_half_other(voxel, &step_z, &other)) {
        return;
    }
    const bool open = !block_state_open(voxel);
    uint16_t there = BLOCK_ID_AIR;
    if (world_map_voxel_get(edit->borrowed_map, cell[0], cell[1], cell[2] + step_z, &there) != WORLD_MAP_STATUS_OK) {
        return;
    }
    if (there == other && world_edit_block_set(edit, cell[0], cell[1], cell[2] + step_z,
                                               block_state_with_open(other, open)) != WORLD_EDIT_STATUS_CHANGED) {
        interaction->no_memory_count++;
        return;
    }
    if (world_edit_block_set(edit, cell[0], cell[1], cell[2], block_state_with_open(voxel, open)) !=
        WORLD_EDIT_STATUS_CHANGED) {
        interaction->no_memory_count++;
        if (there == other) {
            (void)world_edit_block_set(edit, cell[0], cell[1], cell[2] + step_z, other);
        }
        return;
    }
    interaction_sound(input, block_state_id(voxel), SOUND_KIND_PLACE, cell[0], cell[1], cell[2]);
}

/* ---- One tick ------------------------------------------------------------------------------------------------------ */

void player_interaction_tick(player_interaction_t *interaction, world_edit_t *edit,
                             const player_interaction_input_t *input) {
    const bool may_mine = interaction->mine_delay == 0u, may_place = interaction->place_delay == 0u;
    interaction->mine_delay = may_mine ? 0u : (uint8_t)(interaction->mine_delay - 1u);
    interaction->place_delay = may_place ? 0u : (uint8_t)(interaction->place_delay - 1u);
    /* A door opens once a press: held on after placing one, or after opening it, the button leaves it be. */
    const bool place_pressed = input->place != 0u && interaction->place_held == 0u;
    interaction->place_held = input->place != 0u ? 1u : 0u;

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
        /* Nothing in reach along the aim: the plan may still place ahead, for bridging. */
        if (hit->result == RAYCAST_RESULT_MISS && input->attack == 0u && input->place != 0u && may_place) {
            interaction_place(interaction, edit, input);
        }
        return;
    }
    if (input->attack != 0u) {
        if (may_mine) {
            interaction_mine(interaction, edit, input, hit);
        }
    } else if (input->place != 0u && door_opens_by_hand(hit->block)) {
        if (place_pressed) {
            interaction_door_use(interaction, edit, input, hit->cell);
        }
    } else if (input->place != 0u && may_place) {
        interaction_place(interaction, edit, input);
    }
}
