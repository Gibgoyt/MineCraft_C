/* See README.md. */
#include "world/tick/index.h"

#include "assert/index.h"
#include "tables/blocks/index.h"

#include <string.h>

enum : uint32_t {
    /* Delays, in ticks. Each must be shorter than the wheel. */
    TICK_DELAY_WATER = 5u,
    TICK_DELAY_LAVA = 30u,
    TICK_DELAY_FALL = 2u,
    TICK_DELAY_SUPPORT = 1u,
    TICK_DELAY_RETRY = 20u,

    TICK_NODE_NONE = UINT32_MAX,

    /* How far sideways a fluid looks for a way down, and what each step costs it. */
    TICK_SLOPE_WATER = 4u,
    TICK_SLOPE_LAVA = 2u,
    TICK_STEP_WATER = 1u,
    TICK_STEP_LAVA = 2u,
    TICK_SLOPE_GRID = 2u * TICK_SLOPE_WATER + 1u,
    TICK_SLOPE_NONE = UINT32_MAX,

    /* A leaf this many steps through leaves from a log, or nearer, stays. */
    TICK_LEAF_REACH = 6u,
    TICK_LEAF_BOX = 2u * TICK_LEAF_REACH + 1u,

    /* Light a growing thing needs, at its own voxel or the one over it. */
    TICK_LIGHT_GROW = 9u,
    TICK_LIGHT_GRASS_TAKES = 4u,
    TICK_GRASS_TRIES = 4u,
    TICK_HYDRATE_REACH = 4u,
    /* One random tick in this many grows a crop. */
    TICK_CROP_ODDS_WET = 7u,
    TICK_CROP_ODDS_DRY = 13u,
    /* Farmland with no water and no crop goes back to dirt in one random tick in this many. */
    TICK_FARMLAND_DRY_ODDS = 8u,
};
static_assert(WORLD_TICK_WHEEL == 64u); /* the masks below */
static_assert((uint32_t)TICK_DELAY_WATER < 64u && (uint32_t)TICK_DELAY_LAVA < 64u);
static_assert((uint32_t)TICK_DELAY_FALL < 64u && (uint32_t)TICK_DELAY_RETRY < 64u);
static_assert(BLOCK_COUNT < 0xFFFEu);
static_assert(BLOCK_ID_WHEAT_STAGE_8 == BLOCK_ID_WHEAT_STAGE_1 + 7u); /* a stage's next is its id plus 1 */

/* What tick_look gives for places with no block to look at: above and below gameplay, and a column not loaded. */
enum : uint16_t {
    TICK_LOOK_WALL = 0xFFFEu,
    TICK_LOOK_UNLOADED = 0xFFFFu,
};

/* The four ways sideways. */
static const int8_t TICK_SIDE_X[4] = {1, -1, 0, 0};
static const int8_t TICK_SIDE_Y[4] = {0, 0, 1, -1};

/* ---- Looking ----------------------------------------------------------------------------------------------------- */

static uint16_t tick_look(const world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    uint16_t state = 0u;
    const world_map_status_t status = world_map_block_get(tick->borrowed_map, x, y, z, &state);
    if (status == WORLD_MAP_STATUS_OK) {
        return state;
    }
    return status == WORLD_MAP_STATUS_NOT_LOADED ? TICK_LOOK_UNLOADED : TICK_LOOK_WALL;
}

static bool tick_is_fluid(uint16_t state) {
    return state == BLOCK_ID_WATER || state == BLOCK_ID_LAVA;
}

/* Whether a look found a real block. */
static bool tick_is_block(uint16_t state) {
    return state < BLOCK_COUNT;
}

static bool tick_is_solid(uint16_t state) {
    return state == TICK_LOOK_WALL || (tick_is_block(state) && (block_defs[state].flags & BLOCK_FLAG_SOLID) != 0u);
}

static bool tick_is_opaque(uint16_t state) {
    return state == TICK_LOOK_WALL || (tick_is_block(state) && block_defs[state].opacity >= SECTION_LIGHT_MAX);
}

/* Whether a fluid or a falling block can take this voxel: empty, or something small it would wash away. Plants that live in
 * water are not: they would otherwise go wherever a sea was woken. Nor is a thin block: a ladder holds water back. */
static bool tick_is_open(uint16_t state) {
    if (!tick_is_block(state) || (block_defs[state].flags & BLOCK_FLAG_SOLID) != 0u || state == BLOCK_ID_KELP ||
        state == BLOCK_ID_SEAGRASS) {
        return false;
    }
    const uint8_t shape = block_defs[state].shape;
    return shape == BLOCK_SHAPE_NONE || shape == BLOCK_SHAPE_CROSS || shape == BLOCK_SHAPE_CROP ||
           shape == BLOCK_SHAPE_TORCH;
}

static bool tick_is_log(uint16_t state) {
    switch (state) {
    case BLOCK_ID_ACACIA_LOG:
    case BLOCK_ID_BIRCH_LOG:
    case BLOCK_ID_CHERRY_LOG:
    case BLOCK_ID_DARK_OAK_LOG:
    case BLOCK_ID_JUNGLE_LOG:
    case BLOCK_ID_MANGROVE_LOG:
    case BLOCK_ID_OAK_LOG:
    case BLOCK_ID_SPRUCE_LOG:
        return true;
    default:
        return false;
    }
}

static bool tick_is_leaf(uint16_t state) {
    return tick_is_block(state) && (block_defs[state].flags & BLOCK_FLAG_DECAYS) != 0u;
}

static bool tick_is_wheat(uint16_t state) {
    return state >= BLOCK_ID_WHEAT_STAGE_1 && state <= BLOCK_ID_WHEAT_STAGE_8;
}

/* The brighter of its two kinds of light. None where nothing is loaded. */
static uint32_t tick_light(const world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    uint8_t packed = 0u;
    if (world_map_light_get(tick->borrowed_map, x, y, z, &packed) != WORLD_MAP_STATUS_OK) {
        return 0u;
    }
    const uint32_t sky = section_light_sky(packed), block = section_light_block(packed);
    return sky > block ? sky : block;
}

/* ---- Levels ------------------------------------------------------------------------------------------------------ */

static void tick_page_release(world_tick_t *tick, uint16_t *entry) {
    const uint16_t page = (uint16_t)(*entry - 1u);
    tick->borrowed_page_next[page] = tick->page_free;
    tick->page_free = *entry;
    tick->page_live--;
    *entry = 0u;
}

/* The record of a slot, with the pages of any column that was there before given back: the map knows nothing of this module. */
static world_tick_column_t *tick_column(world_tick_t *tick, uint32_t slot) {
    world_tick_column_t *const column = &tick->borrowed_columns[slot];
    const uint32_t generation = tick->borrowed_map->borrowed_slots[slot].generation;
    if (column->generation != generation) {
        for (uint32_t i = 0u; i < COLUMN_SECTIONS; i++) {
            if (column->pages[i] != 0u) {
                tick_page_release(tick, &column->pages[i]);
            }
        }
        column->generation = generation;
    }
    return column;
}

static bool tick_page_take(world_tick_t *tick, uint16_t *entry) {
    uint32_t page = 0u;
    if (tick->page_free != 0u) {
        page = tick->page_free - 1u;
        tick->page_free = tick->borrowed_page_next[page];
    } else if (tick->page_carved < tick->page_capacity) {
        page = tick->page_carved++;
    } else {
        return false;
    }
    memset(tick->borrowed_pages + (uint64_t)page * WORLD_TICK_PAGE_BYTES, 0, WORLD_TICK_PAGE_BYTES);
    tick->borrowed_page_used[page] = 0u;
    tick->page_live++;
    *entry = (uint16_t)(page + 1u);
    return true;
}

static uint32_t tick_voxel(int32_t x, int32_t y, int32_t z) {
    return section_voxel_index(column_local_of(x), column_local_of(y), column_section_local_of(z));
}

uint8_t world_tick_fluid_level(const world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    const world_map_t *const map = tick->borrowed_map;
    uint32_t slot = 0u;
    if (!column_z_in_gameplay(z) || !world_map_slot_of(map, column_chunk_of(x), column_chunk_of(y), &slot)) {
        return WORLD_TICK_LEVEL_SOURCE;
    }
    const world_tick_column_t *const column = &tick->borrowed_columns[slot];
    const uint16_t entry = column->pages[column_section_of(z)];
    if (entry == 0u || column->generation != map->borrowed_slots[slot].generation) {
        return WORLD_TICK_LEVEL_SOURCE;
    }
    const uint32_t voxel = tick_voxel(x, y, z);
    const uint8_t byte = tick->borrowed_pages[(uint64_t)(entry - 1u) * WORLD_TICK_PAGE_BYTES + voxel / 2u];
    return (uint8_t)(((uint32_t)byte >> ((voxel & 1u) * 4u)) & 15u);
}

/* In a loaded column. False only for want of a page, and then nothing changed. Going to a source never wants one. */
static bool tick_level_set(world_tick_t *tick, int32_t x, int32_t y, int32_t z, uint8_t level) {
    uint32_t slot = 0u;
    const bool in_window = world_map_slot_of(tick->borrowed_map, column_chunk_of(x), column_chunk_of(y), &slot);
    ASSERT_ALWAYS(in_window);
    world_tick_column_t *const column = tick_column(tick, slot);
    uint16_t *const entry = &column->pages[column_section_of(z)];
    if (*entry == 0u) {
        if (level == WORLD_TICK_LEVEL_SOURCE) {
            return true;
        }
        if (!tick_page_take(tick, entry)) {
            tick->page_full_count++;
            return false;
        }
    }
    const uint32_t page = *entry - 1u;
    const uint32_t voxel = tick_voxel(x, y, z);
    const uint32_t shift = (voxel & 1u) * 4u;
    uint8_t *const byte = &tick->borrowed_pages[(uint64_t)page * WORLD_TICK_PAGE_BYTES + voxel / 2u];
    const uint8_t before = (uint8_t)(((uint32_t)*byte >> shift) & 15u);
    *byte = (uint8_t)(((uint32_t)*byte & ~(15u << shift)) | ((uint32_t)level << shift));
    uint16_t *const used = &tick->borrowed_page_used[page];
    if (before == WORLD_TICK_LEVEL_SOURCE && level != WORLD_TICK_LEVEL_SOURCE) {
        (*used)++;
    } else if (before != WORLD_TICK_LEVEL_SOURCE && level == WORLD_TICK_LEVEL_SOURCE) {
        (*used)--;
    }
    if (*used == 0u) {
        tick_page_release(tick, entry);
    }
    return true;
}

/* ---- What is waiting --------------------------------------------------------------------------------------------- */

static uint32_t tick_set_home(const world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    uint64_t mixed = ((uint64_t)(uint32_t)x * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)(uint32_t)y * 0xC2B2AE3D27D4EB4Full) ^
                     ((uint64_t)(uint32_t)z * 0x165667B19E3779F9ull);
    mixed ^= mixed >> 29;
    return (uint32_t)mixed & tick->set_mask;
}

/* The place in the set of this voxel's node, or of the empty place where it would go. The set is twice the nodes, so one is
 * always empty. */
static uint32_t tick_set_find(const world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    uint32_t place = tick_set_home(tick, x, y, z);
    for (;;) {
        const uint32_t held = tick->borrowed_set[place];
        if (held == 0u) {
            return place;
        }
        const world_tick_node_t *const node = &tick->borrowed_nodes[held - 1u];
        if (node->x == x && node->y == y && node->z == z) {
            return place;
        }
        place = (place + 1u) & tick->set_mask;
    }
}

/* Moves up whatever the empty place would have stopped a search for, so that no tombstones build up. */
static void tick_set_remove(world_tick_t *tick, uint32_t place) {
    uint32_t hole = place;
    uint32_t next = (place + 1u) & tick->set_mask;
    while (tick->borrowed_set[next] != 0u) {
        const world_tick_node_t *const node = &tick->borrowed_nodes[tick->borrowed_set[next] - 1u];
        const uint32_t home = tick_set_home(tick, node->x, node->y, node->z);
        if (((next - home) & tick->set_mask) >= ((next - hole) & tick->set_mask)) {
            tick->borrowed_set[hole] = tick->borrowed_set[next];
            hole = next;
        }
        next = (next + 1u) & tick->set_mask;
    }
    tick->borrowed_set[hole] = 0u;
}

static void tick_wheel_append(world_tick_t *tick, uint32_t bucket, uint32_t number) {
    tick->borrowed_nodes[number].next = TICK_NODE_NONE;
    if (tick->wheel_tail[bucket] == TICK_NODE_NONE) {
        tick->wheel_head[bucket] = number;
    } else {
        tick->borrowed_nodes[tick->wheel_tail[bucket]].next = number;
    }
    tick->wheel_tail[bucket] = number;
}

/* Once per voxel: one already waiting comes due when it was going to, and looks at the world as it then is. */
static void tick_schedule(world_tick_t *tick, int32_t x, int32_t y, int32_t z, uint32_t delay) {
    const world_map_t *const map = tick->borrowed_map;
    uint32_t slot = 0u;
    const bool in_window = world_map_slot_of(map, column_chunk_of(x), column_chunk_of(y), &slot);
    ASSERT_ALWAYS(in_window);
    const uint32_t generation = map->borrowed_slots[slot].generation;
    const uint32_t place = tick_set_find(tick, x, y, z);
    if (tick->borrowed_set[place] != 0u) {
        tick->borrowed_nodes[tick->borrowed_set[place] - 1u].generation = generation;
        return;
    }
    uint32_t number = TICK_NODE_NONE;
    if (tick->node_free != TICK_NODE_NONE) {
        number = tick->node_free;
        tick->node_free = tick->borrowed_nodes[number].next;
    } else if (tick->node_carved < tick->node_capacity) {
        number = tick->node_carved++;
    } else {
        tick->node_full_count++;
        return;
    }
    world_tick_node_t *const node = &tick->borrowed_nodes[number];
    *node = (world_tick_node_t){.x = x, .y = y, .next = TICK_NODE_NONE, .generation = generation, .z = (int16_t)z};
    tick->borrowed_set[place] = number + 1u;
    tick->node_live++;
    tick_wheel_append(tick, (uint32_t)((tick->now + delay) & (WORLD_TICK_WHEEL - 1u)), number);
}

/* Whether a block that falls has somewhere to go. */
static bool tick_can_fall_into(uint16_t state) {
    return tick_is_open(state) || tick_is_fluid(state);
}

/* One voxel something next to it changed. Only what could do something about that gets a tick. */
static void tick_wake(world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    const uint16_t state = tick_look(tick, x, y, z);
    if (!tick_is_block(state)) {
        return;
    }
    if (state == BLOCK_ID_WATER) {
        tick_schedule(tick, x, y, z, TICK_DELAY_WATER);
    } else if (state == BLOCK_ID_LAVA) {
        tick_schedule(tick, x, y, z, TICK_DELAY_LAVA);
    } else if ((block_defs[state].flags & BLOCK_FLAG_FALLS) != 0u) {
        if (tick_can_fall_into(tick_look(tick, x, y, z - 1))) {
            tick_schedule(tick, x, y, z, TICK_DELAY_FALL);
        }
    } else if (tick_is_wheat(state) || state == BLOCK_ID_FARMLAND) {
        tick_schedule(tick, x, y, z, TICK_DELAY_SUPPORT);
    }
}

static void tick_wake_around(world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    tick_wake(tick, x, y, z);
    tick_wake(tick, x, y, z + 1);
    tick_wake(tick, x, y, z - 1);
    for (uint32_t side = 0u; side < 4u; side++) {
        tick_wake(tick, x + TICK_SIDE_X[side], y + TICK_SIDE_Y[side], z);
    }
}

/* ---- Writing ----------------------------------------------------------------------------------------------------- */

/* A block with something to give has gone by itself: game comes for it on its next tick. */
static void tick_broken_note(world_tick_t *tick, uint16_t block, int32_t x, int32_t y, int32_t z) {
    if (!tick_is_block(block) || block_defs[block].drop_count == 0u) {
        return;
    }
    if (tick->broken_count == WORLD_TICK_BROKEN) {
        tick->broken_head = (tick->broken_head + 1u) % WORLD_TICK_BROKEN;
        tick->broken_count--;
        tick->broken_lost_count++;
    }
    tick->borrowed_broken[(tick->broken_head + tick->broken_count) % WORLD_TICK_BROKEN] =
        (world_tick_broken_t){.x = x, .y = y, .z = z, .block = block, ._pad0 = {0}};
    tick->broken_count++;
}

bool world_tick_broken_next(world_tick_t *tick, world_tick_broken_t *out) {
    if (tick->broken_count == 0u) {
        return false;
    }
    *out = tick->borrowed_broken[tick->broken_head];
    tick->broken_head = (tick->broken_head + 1u) % WORLD_TICK_BROKEN;
    tick->broken_count--;
    return true;
}

/* A voxel stops holding a fluid here, or holds something that keeps no level. */
static bool tick_block_write(world_tick_t *tick, int32_t x, int32_t y, int32_t z, uint16_t state) {
    const world_edit_status_t status = world_edit_block_set(tick->borrowed_edit, x, y, z, state);
    if (status == WORLD_EDIT_STATUS_NO_MEMORY) {
        tick->no_memory_count++;
        return false;
    }
    if (status == WORLD_EDIT_STATUS_CHANGED) {
        (void)tick_level_set(tick, x, y, z, WORLD_TICK_LEVEL_SOURCE);
    }
    return status == WORLD_EDIT_STATUS_CHANGED;
}

/* A fluid arrives. The level first: without a page, or a pool with room, nothing happens at all. */
static bool tick_fluid_write(world_tick_t *tick, int32_t x, int32_t y, int32_t z, uint16_t fluid, uint8_t level) {
    if (!tick_level_set(tick, x, y, z, level)) {
        return false;
    }
    const uint16_t before = tick_look(tick, x, y, z);
    const world_edit_status_t status = world_edit_block_set(tick->borrowed_edit, x, y, z, fluid);
    if (status != WORLD_EDIT_STATUS_CHANGED) {
        tick->no_memory_count += status == WORLD_EDIT_STATUS_NO_MEMORY ? 1u : 0u;
        (void)tick_level_set(tick, x, y, z, WORLD_TICK_LEVEL_SOURCE);
        return false;
    }
    /* Water washes a plant away and leaves what it gave. Lava burns it to nothing. */
    if (fluid == BLOCK_ID_WATER) {
        tick_broken_note(tick, before, x, y, z);
    }
    return true;
}

/* ---- Fluids ------------------------------------------------------------------------------------------------------ */

/* Whether a fluid, having come here, would have a way down. */
static bool tick_is_hole(const world_tick_t *tick, uint16_t fluid, int32_t x, int32_t y, int32_t z) {
    const uint16_t below = tick_look(tick, x, y, z - 1);
    return below == fluid || tick_is_open(below);
}

/* Whether a fluid can run through a voxel on its way: its own kind, unless a source, or anywhere it could go. */
static bool tick_is_passable(const world_tick_t *tick, uint16_t fluid, uint16_t state, int32_t x, int32_t y,
                             int32_t z) {
    return tick_is_open(state) || (state == fluid && world_tick_fluid_level(tick, x, y, z) != WORLD_TICK_LEVEL_SOURCE);
}

/* How many steps past the voxel beside (x, y) a fluid would go to find a way down, setting out in this direction, or
 * TICK_SLOPE_NONE if none is near enough. Breadth first, so that the first found is the nearest. */
static uint32_t tick_slope_distance(const world_tick_t *tick, uint16_t fluid, int32_t x, int32_t y, int32_t z,
                                    uint32_t side, uint32_t reach) {
    const int32_t start_x = x + TICK_SIDE_X[side], start_y = y + TICK_SIDE_Y[side];
    if (tick_is_hole(tick, fluid, start_x, start_y, z)) {
        return 0u;
    }
    uint8_t seen[TICK_SLOPE_GRID * TICK_SLOPE_GRID];
    uint8_t queue_x[TICK_SLOPE_GRID * TICK_SLOPE_GRID];
    uint8_t queue_y[TICK_SLOPE_GRID * TICK_SLOPE_GRID];
    memset(seen, 0, sizeof(seen));
    const int32_t middle = (int32_t)TICK_SLOPE_WATER;
    seen[(uint32_t)middle * TICK_SLOPE_GRID + (uint32_t)middle] =
        1u; /* it does not come back through where it started */
    const int32_t first_x = middle + TICK_SIDE_X[side], first_y = middle + TICK_SIDE_Y[side];
    seen[(uint32_t)first_y * TICK_SLOPE_GRID + (uint32_t)first_x] = 1u;
    queue_x[0] = (uint8_t)first_x;
    queue_y[0] = (uint8_t)first_y;
    uint32_t head = 0u, tail = 1u, level_end = 1u;
    for (uint32_t depth = 1u; depth < reach && head < tail; depth++) {
        for (; head < level_end; head++) {
            for (uint32_t way = 0u; way < 4u; way++) {
                const int32_t grid_x = (int32_t)queue_x[head] + TICK_SIDE_X[way];
                const int32_t grid_y = (int32_t)queue_y[head] + TICK_SIDE_Y[way];
                if (grid_x < 0 || grid_y < 0 || grid_x >= (int32_t)TICK_SLOPE_GRID ||
                    grid_y >= (int32_t)TICK_SLOPE_GRID) {
                    continue;
                }
                uint8_t *const mark = &seen[(uint32_t)grid_y * TICK_SLOPE_GRID + (uint32_t)grid_x];
                if (*mark != 0u) {
                    continue;
                }
                *mark = 1u;
                const int32_t world_x = x + grid_x - middle, world_y = y + grid_y - middle;
                const uint16_t state = tick_look(tick, world_x, world_y, z);
                if (!tick_is_passable(tick, fluid, state, world_x, world_y, z)) {
                    continue;
                }
                if (tick_is_hole(tick, fluid, world_x, world_y, z)) {
                    return depth;
                }
                queue_x[tail] = (uint8_t)grid_x;
                queue_y[tail] = (uint8_t)grid_y;
                tail++;
            }
        }
        level_end = tail;
    }
    return TICK_SLOPE_NONE;
}

/* What a fluid does beside itself: into each open voxel on the shortest way down, or all of them if there is none. False if it
 * wanted to and could not, so that it should try again. */
static bool tick_fluid_spread_sides(world_tick_t *tick, uint16_t fluid, int32_t x, int32_t y, int32_t z, uint8_t level,
                                    const uint16_t *neighbours) {
    const uint32_t step = fluid == BLOCK_ID_LAVA ? TICK_STEP_LAVA : TICK_STEP_WATER;
    const uint32_t from = level == WORLD_TICK_LEVEL_FALLING ? 0u : level;
    const uint32_t reach = from + step;
    if (reach > WORLD_TICK_LEVEL_FLOW_MAX) {
        return true;
    }
    const uint32_t slope = fluid == BLOCK_ID_LAVA ? TICK_SLOPE_LAVA : TICK_SLOPE_WATER;
    uint32_t cost[4];
    uint32_t best = TICK_SLOPE_NONE;
    bool wanted = false;
    for (uint32_t side = 0u; side < 4u; side++) {
        const int32_t side_x = x + TICK_SIDE_X[side], side_y = y + TICK_SIDE_Y[side];
        cost[side] = TICK_SLOPE_NONE;
        if (!tick_is_passable(tick, fluid, neighbours[side], side_x, side_y, z)) {
            continue;
        }
        wanted = true;
        cost[side] = tick_slope_distance(tick, fluid, x, y, z, side, slope);
        best = cost[side] < best ? cost[side] : best;
    }
    if (!wanted) {
        return true;
    }
    bool all_done = true;
    for (uint32_t side = 0u; side < 4u; side++) {
        const int32_t side_x = x + TICK_SIDE_X[side], side_y = y + TICK_SIDE_Y[side];
        if (cost[side] == best && tick_is_open(neighbours[side]) &&
            tick_is_passable(tick, fluid, neighbours[side], side_x, side_y, z)) {
            if (!tick_fluid_write(tick, side_x, side_y, z, fluid, (uint8_t)reach)) {
                all_done = false;
            }
        }
    }
    return all_done;
}

/* A save holds levels with the blocks, and goes by what is dirty: a level changing with no block changing has to say so itself. */
static void tick_level_save_flag(world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    uint32_t slot = 0u;
    const int32_t chunk_x = column_chunk_of(x), chunk_y = column_chunk_of(y);
    if (world_map_slot_of(tick->borrowed_map, chunk_x, chunk_y, &slot)) {
        const world_edit_dirty_t dirty = {.section_mask = 1ull << column_section_of(z),
                                          .chunk_x = chunk_x,
                                          .chunk_y = chunk_y,
                                          .slot = slot,
                                          .generation = tick->borrowed_map->borrowed_slots[slot].generation};
        world_edit_dirty_restore(tick->borrowed_edit, &dirty, WORLD_EDIT_CONSUMER_SAVE);
    }
}

static void tick_fluid(world_tick_t *tick, uint16_t fluid, int32_t x, int32_t y, int32_t z) {
    const bool lava = fluid == BLOCK_ID_LAVA;
    const uint32_t step = lava ? TICK_STEP_LAVA : TICK_STEP_WATER;
    uint8_t level = world_tick_fluid_level(tick, x, y, z);
    uint16_t neighbours[4];
    bool unknown = false;
    bool water_beside = false;
    for (uint32_t side = 0u; side < 4u; side++) {
        neighbours[side] = tick_look(tick, x + TICK_SIDE_X[side], y + TICK_SIDE_Y[side], z);
        unknown = unknown || neighbours[side] == TICK_LOOK_UNLOADED;
        water_beside = water_beside || neighbours[side] == BLOCK_ID_WATER;
    }
    const uint16_t above = tick_look(tick, x, y, z + 1);
    const uint16_t below = tick_look(tick, x, y, z - 1);

    /* Lava that water reaches is not lava any more. */
    if (lava && (water_beside || above == BLOCK_ID_WATER)) {
        const uint16_t rock = level == WORLD_TICK_LEVEL_SOURCE ? BLOCK_ID_OBSIDIAN : BLOCK_ID_COBBLESTONE;
        if (!tick_block_write(tick, x, y, z, rock)) {
            tick_schedule(tick, x, y, z, TICK_DELAY_RETRY);
        }
        return;
    }

    uint32_t sources = 0u;
    uint32_t nearest = UINT32_MAX;
    for (uint32_t side = 0u; side < 4u; side++) {
        if (neighbours[side] != fluid) {
            continue;
        }
        const uint8_t other = world_tick_fluid_level(tick, x + TICK_SIDE_X[side], y + TICK_SIDE_Y[side], z);
        sources += other == WORLD_TICK_LEVEL_SOURCE ? 1u : 0u;
        const uint32_t distance = other == WORLD_TICK_LEVEL_FALLING ? 0u : other;
        nearest = distance < nearest ? distance : nearest;
    }

    /* What feeds it decides what it now is. A source needs nothing. With a column not there, what feeds it may be there too, so
     * it waits. */
    if (level != WORLD_TICK_LEVEL_SOURCE && !unknown) {
        const bool base = tick_is_solid(below) || (below == fluid && world_tick_fluid_level(tick, x, y, z - 1) == 0u);
        uint32_t wanted = UINT32_MAX;
        if (!lava && sources >= 2u && base) {
            wanted = WORLD_TICK_LEVEL_SOURCE;
        } else if (above == fluid) {
            wanted = WORLD_TICK_LEVEL_FALLING;
        } else if (nearest != UINT32_MAX && nearest + step <= WORLD_TICK_LEVEL_FLOW_MAX) {
            wanted = nearest + step;
        }
        if (wanted == UINT32_MAX) {
            if (!tick_block_write(tick, x, y, z, BLOCK_ID_AIR)) {
                tick_schedule(tick, x, y, z, TICK_DELAY_RETRY);
            }
            return;
        }
        if (wanted != level) {
            /* It has a page already, and going down to a source gives it back. No block changes, so no log record tells anyone. */
            (void)tick_level_set(tick, x, y, z, (uint8_t)wanted);
            level = (uint8_t)wanted;
            tick_level_save_flag(tick, x, y, z);
            tick_wake_around(tick, x, y, z);
        }
    }

    bool done = !unknown;
    bool flows_down = false;
    if (below == BLOCK_ID_WATER && lava) {
        /* Lava falling on water leaves stone in it. */
        flows_down = true;
        if (!tick_block_write(tick, x, y, z - 1, BLOCK_ID_STONE)) {
            done = false;
        }
    } else if (tick_is_open(below)) {
        flows_down = true;
        if (!tick_fluid_write(tick, x, y, z - 1, fluid, WORLD_TICK_LEVEL_FALLING)) {
            done = false;
        }
    }
    /* Going down takes all it has, unless so much feeds it that the edge of a sea runs both ways. A source always spreads, unless
     * it just found a way down; flowing fluid over its own kind has gone as far as it needs. */
    const bool sides = flows_down ? sources >= 3u : (level == WORLD_TICK_LEVEL_SOURCE || below != fluid);
    if (sides && !tick_fluid_spread_sides(tick, fluid, x, y, z, level, neighbours)) {
        done = false;
    }
    if (!done) {
        tick_schedule(tick, x, y, z, TICK_DELAY_RETRY);
    }
}

/* ---- Falling ----------------------------------------------------------------------------------------------------- */

static void tick_fall(world_tick_t *tick, uint16_t state, int32_t x, int32_t y, int32_t z) {
    if (!tick_can_fall_into(tick_look(tick, x, y, z - 1))) {
        return;
    }
    const uint16_t buried = tick_look(tick, x, y, z - 1);
    if (!tick_block_write(tick, x, y, z - 1, state)) {
        tick_schedule(tick, x, y, z, TICK_DELAY_RETRY);
        return;
    }
    tick_broken_note(tick, buried, x, y, z - 1);
    if (!tick_block_write(tick, x, y, z, BLOCK_ID_AIR)) {
        /* A block must not be two blocks. What it fell into is gone, as it would be had it landed there. */
        (void)tick_block_write(tick, x, y, z - 1, BLOCK_ID_AIR);
        tick_schedule(tick, x, y, z, TICK_DELAY_RETRY);
    }
}

/* ---- What needs something under or over it -------------------------------------------------------------------------- */

static void tick_support(world_tick_t *tick, uint16_t state, int32_t x, int32_t y, int32_t z) {
    if (state == BLOCK_ID_FARMLAND) {
        if (tick_is_solid(tick_look(tick, x, y, z + 1))) {
            (void)tick_block_write(tick, x, y, z, BLOCK_ID_DIRT);
        }
    } else if (tick_look(tick, x, y, z - 1) != BLOCK_ID_FARMLAND && tick_block_write(tick, x, y, z, BLOCK_ID_AIR)) {
        tick_broken_note(tick, state, x, y, z);
    }
}

/* ---- Random ticks ------------------------------------------------------------------------------------------------ */

/* The next of a stream of numbers: splitmix64. */
static uint64_t tick_random_next(uint64_t *state) {
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Whether a log holds this leaf up: breadth first through leaves. A column not loaded might hold one, and so counts as if it
 * does, or a tree on the edge of the world would fall apart. */
static bool tick_leaf_held(const world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    uint8_t seen[TICK_LEAF_BOX * TICK_LEAF_BOX * TICK_LEAF_BOX];
    /* Inside a diamond of that reach, so far fewer than the box holds. */
    uint16_t queue[TICK_LEAF_BOX * TICK_LEAF_BOX * TICK_LEAF_BOX];
    memset(seen, 0, sizeof(seen));
    const int32_t middle = (int32_t)TICK_LEAF_REACH;
    const uint32_t box = TICK_LEAF_BOX;
    const uint32_t start = ((uint32_t)middle * box + (uint32_t)middle) * box + (uint32_t)middle;
    seen[start] = 1u;
    queue[0] = (uint16_t)start;
    uint32_t head = 0u, tail = 1u, level_end = 1u;
    for (uint32_t depth = 1u; depth <= TICK_LEAF_REACH && head < tail; depth++) {
        for (; head < level_end; head++) {
            const uint32_t place = queue[head];
            const int32_t at_x = (int32_t)(place % box), at_y = (int32_t)(place / box % box);
            const int32_t at_z = (int32_t)(place / box / box);
            static const int8_t way_x[6] = {1, -1, 0, 0, 0, 0};
            static const int8_t way_y[6] = {0, 0, 1, -1, 0, 0};
            static const int8_t way_z[6] = {0, 0, 0, 0, 1, -1};
            for (uint32_t way = 0u; way < 6u; way++) {
                const int32_t next_x = at_x + way_x[way], next_y = at_y + way_y[way], next_z = at_z + way_z[way];
                const uint32_t next = ((uint32_t)next_z * box + (uint32_t)next_y) * box + (uint32_t)next_x;
                if (seen[next] != 0u) {
                    continue;
                }
                seen[next] = 1u;
                const uint16_t state = tick_look(tick, x + next_x - middle, y + next_y - middle, z + next_z - middle);
                if (state == TICK_LOOK_UNLOADED || tick_is_log(state)) {
                    return true;
                }
                if (tick_is_leaf(state)) {
                    queue[tail++] = (uint16_t)next;
                }
            }
        }
        level_end = tail;
    }
    return false;
}

static bool tick_farmland_hydrated(const world_tick_t *tick, int32_t x, int32_t y, int32_t z) {
    const int32_t reach = (int32_t)TICK_HYDRATE_REACH;
    for (int32_t dz = 0; dz <= 1; dz++) {
        for (int32_t dy = -reach; dy <= reach; dy++) {
            for (int32_t dx = -reach; dx <= reach; dx++) {
                if (tick_look(tick, x + dx, y + dy, z + dz) == BLOCK_ID_WATER) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Grass goes where light and air let it, and stops being grass where they do not. */
static void tick_random_grass(world_tick_t *tick, uint64_t *rng, int32_t x, int32_t y, int32_t z) {
    const uint16_t above = tick_look(tick, x, y, z + 1);
    if (above == TICK_LOOK_UNLOADED) {
        return;
    }
    if (tick_is_opaque(above) || tick_is_fluid(above)) {
        (void)tick_block_write(tick, x, y, z, BLOCK_ID_DIRT);
        return;
    }
    if (tick_light(tick, x, y, z + 1) < TICK_LIGHT_GROW) {
        return;
    }
    for (uint32_t i = 0u; i < TICK_GRASS_TRIES; i++) {
        const uint64_t bits = tick_random_next(rng);
        const int32_t target_x = x + (int32_t)(bits % 3u) - 1;
        const int32_t target_y = y + (int32_t)((bits >> 8) % 3u) - 1;
        const int32_t target_z = z + (int32_t)((bits >> 16) % 5u) - 3;
        if (tick_look(tick, target_x, target_y, target_z) != BLOCK_ID_DIRT) {
            continue;
        }
        const uint16_t over = tick_look(tick, target_x, target_y, target_z + 1);
        if (!tick_is_block(over) || tick_is_opaque(over) || tick_is_fluid(over) ||
            tick_light(tick, target_x, target_y, target_z + 1) < TICK_LIGHT_GRASS_TAKES) {
            continue;
        }
        (void)tick_block_write(tick, target_x, target_y, target_z, BLOCK_ID_GRASS_BLOCK);
    }
}

static void tick_random_wheat(world_tick_t *tick, uint64_t *rng, uint16_t state, int32_t x, int32_t y, int32_t z) {
    if (state == BLOCK_ID_WHEAT_STAGE_8 || tick_light(tick, x, y, z) < TICK_LIGHT_GROW) {
        return;
    }
    const uint64_t odds = tick_farmland_hydrated(tick, x, y, z - 1) ? TICK_CROP_ODDS_WET : TICK_CROP_ODDS_DRY;
    if (tick_random_next(rng) % odds == 0u) {
        (void)tick_block_write(tick, x, y, z, (uint16_t)(state + 1u));
    }
}

static void tick_random_farmland(world_tick_t *tick, uint64_t *rng, int32_t x, int32_t y, int32_t z) {
    const bool crop = tick_is_wheat(tick_look(tick, x, y, z + 1));
    if (!crop && !tick_farmland_hydrated(tick, x, y, z) && tick_random_next(rng) % TICK_FARMLAND_DRY_ODDS == 0u) {
        (void)tick_block_write(tick, x, y, z, BLOCK_ID_DIRT);
    }
}

/* Whether a random tick on this block does anything: a section all of something else need not be looked at. */
static bool tick_is_random(uint16_t state) {
    return state == BLOCK_ID_GRASS_BLOCK || state == BLOCK_ID_FARMLAND || tick_is_wheat(state) || tick_is_leaf(state);
}

static void tick_random_voxel(world_tick_t *tick, uint64_t *rng, uint16_t state, int32_t x, int32_t y, int32_t z) {
    tick->random_count++;
    if (state == BLOCK_ID_GRASS_BLOCK) {
        tick_random_grass(tick, rng, x, y, z);
    } else if (state == BLOCK_ID_FARMLAND) {
        tick_random_farmland(tick, rng, x, y, z);
    } else if (tick_is_wheat(state)) {
        tick_random_wheat(tick, rng, state, x, y, z);
    } else if (!tick_leaf_held(tick, x, y, z) && tick_block_write(tick, x, y, z, BLOCK_ID_AIR)) {
        tick_broken_note(tick, state, x, y, z);
    }
}

/* The numbers belong to the place and the tick and nothing else, so what happens in one column does not depend on which others
 * are loaded. */
static void tick_random_column(world_tick_t *tick, const column_t *column, int32_t chunk_x, int32_t chunk_y) {
    for (uint32_t index = 0u; index < COLUMN_SECTIONS; index++) {
        const section_t *const section = &column->sections[index];
        if (section->bits == SECTION_BITS_UNIFORM && !tick_is_random(section->block_uniform)) {
            continue;
        }
        const int32_t floor_z = COLUMN_Z_STORAGE_MIN + (int32_t)(index * SECTION_SIZE);
        uint64_t rng =
            tick->seed ^ (tick->now * 0xD6E8FEB86659FD93ull) ^ ((uint64_t)(uint32_t)chunk_x * 0xA0761D6478BD642Full) ^
            ((uint64_t)(uint32_t)chunk_y * 0xE7037ED1A0B428DBull) ^ ((uint64_t)index * 0x8EBC6AF09C88C6E3ull);
        for (uint32_t pick = 0u; pick < tick->random_per_section; pick++) {
            const uint32_t voxel = (uint32_t)tick_random_next(&rng) & (SECTION_VOXELS - 1u);
            const uint16_t state = section_block_get(section, voxel);
            if (!tick_is_random(state)) {
                continue;
            }
            const int32_t z = floor_z + (int32_t)(voxel >> 8);
            if (column_z_in_gameplay(z)) {
                const int32_t x = chunk_x * (int32_t)COLUMN_SIZE + (int32_t)(voxel & 15u);
                const int32_t y = chunk_y * (int32_t)COLUMN_SIZE + (int32_t)((voxel >> 4) & 15u);
                tick_random_voxel(tick, &rng, state, x, y, z);
            }
        }
    }
}

/* In order of place, not of the map's list, which is in no order: so the same columns always go the same way. */
static void tick_random_all(world_tick_t *tick) {
    const world_map_t *const map = tick->borrowed_map;
    const int32_t radius = (int32_t)(tick->random_radius < map->table_radius ? tick->random_radius : map->table_radius);
    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            const int32_t chunk_x = map->center_x + dx, chunk_y = map->center_y + dy;
            const column_t *const column = world_map_column_at(map, chunk_x, chunk_y);
            if (column != nullptr) {
                tick_random_column(tick, column, chunk_x, chunk_y);
            }
        }
    }
}

/* ---- A step ------------------------------------------------------------------------------------------------------ */

/* Everything the log has that nothing has looked at: a fluid's level goes with the fluid, and what is next to a change wakes. */
static void tick_log_drain(world_tick_t *tick) {
    uint64_t count = 0u;
    do {
        uint64_t lost = 0u;
        count =
            world_edit_log_read(tick->borrowed_edit, &tick->log_cursor, tick->borrowed_log, WORLD_TICK_RECORDS, &lost);
        tick->lost_count += lost;
        for (uint64_t i = 0u; i < count; i++) {
            const world_edit_record_t *const record = &tick->borrowed_log[i];
            const uint16_t state = tick_look(tick, record->x, record->y, record->z);
            if (tick_is_block(state) && !tick_is_fluid(state)) {
                (void)tick_level_set(tick, record->x, record->y, record->z, WORLD_TICK_LEVEL_SOURCE);
            }
            tick_wake_around(tick, record->x, record->y, record->z);
        }
    } while (count != 0u);
}

/* What was due now, in the order it was asked for. What the limits leave goes first next tick. */
static void tick_fire_due(world_tick_t *tick) {
    const uint32_t bucket = (uint32_t)(tick->now & (WORLD_TICK_WHEEL - 1u));
    const uint64_t writes_end = tick->borrowed_edit->log_next + WORLD_TICK_WRITES_PER_TICK;
    uint32_t number = tick->wheel_head[bucket];
    tick->wheel_head[bucket] = TICK_NODE_NONE;
    tick->wheel_tail[bucket] = TICK_NODE_NONE;
    uint32_t fired = 0u;
    while (number != TICK_NODE_NONE && fired < WORLD_TICK_FIRES_PER_TICK &&
           tick->borrowed_edit->log_next < writes_end) {
        world_tick_node_t *const node = &tick->borrowed_nodes[number];
        const int32_t x = node->x, y = node->y, z = node->z;
        const uint32_t generation = node->generation;
        const uint32_t next = node->next;
        tick_set_remove(tick, tick_set_find(tick, x, y, z));
        node->next = tick->node_free;
        tick->node_free = number;
        tick->node_live--;
        number = next;
        fired++;
        tick->fired_count++;

        uint32_t slot = 0u;
        const world_map_t *const map = tick->borrowed_map;
        if (!world_map_slot_of(map, column_chunk_of(x), column_chunk_of(y), &slot) ||
            map->borrowed_slots[slot].generation != generation) {
            continue; /* it was for a column that has gone */
        }
        const uint16_t state = tick_look(tick, x, y, z);
        if (tick_is_fluid(state)) {
            tick_fluid(tick, state, x, y, z);
        } else if (tick_is_block(state) && (block_defs[state].flags & BLOCK_FLAG_FALLS) != 0u) {
            tick_fall(tick, state, x, y, z);
        } else if (tick_is_wheat(state) || state == BLOCK_ID_FARMLAND) {
            tick_support(tick, state, x, y, z);
        }
    }
    if (number != TICK_NODE_NONE) {
        const uint32_t later = (bucket + 1u) & (WORLD_TICK_WHEEL - 1u);
        uint32_t last = number;
        while (tick->borrowed_nodes[last].next != TICK_NODE_NONE) {
            last = tick->borrowed_nodes[last].next;
        }
        tick->borrowed_nodes[last].next = tick->wheel_head[later];
        if (tick->wheel_tail[later] == TICK_NODE_NONE) {
            tick->wheel_tail[later] = last;
        }
        tick->wheel_head[later] = number;
    }
}

void world_tick_step(world_tick_t *tick) {
    tick->now++;
    tick_log_drain(tick);
    tick_fire_due(tick);
    tick_random_all(tick);
    /* Now, not next tick, so that each delay is exactly what it says. */
    tick_log_drain(tick);
}

/* ---- Starting and ending ----------------------------------------------------------------------------------------- */

void world_tick_init(world_tick_t *tick, arena_t *arena, world_edit_t *edit, uint32_t node_capacity,
                     uint32_t page_capacity) {
    ASSERT_ALWAYS(node_capacity >= 2u && (node_capacity & (node_capacity - 1u)) == 0u);
    ASSERT_ALWAYS(node_capacity <= 1u << 30);
    ASSERT_ALWAYS(page_capacity >= 1u && page_capacity <= UINT16_MAX);
    memset(tick, 0, sizeof(*tick));
    world_map_t *const map = edit->borrowed_map;
    const uint64_t set_size = 2ull * node_capacity;
    tick->borrowed_edit = edit;
    tick->borrowed_map = map;
    tick->borrowed_columns = arena_alloc_zeroed(arena, (uint64_t)map->slot_count * sizeof(world_tick_column_t),
                                                alignof(world_tick_column_t));
    tick->borrowed_nodes =
        arena_alloc_array(arena, node_capacity, sizeof(world_tick_node_t), alignof(world_tick_node_t));
    tick->borrowed_set = arena_alloc_zeroed(arena, set_size * sizeof(uint32_t), alignof(uint32_t));
    tick->borrowed_pages = arena_alloc(arena, (uint64_t)page_capacity * WORLD_TICK_PAGE_BYTES, 64u);
    tick->borrowed_page_next = arena_alloc_array(arena, page_capacity, sizeof(uint16_t), alignof(uint16_t));
    tick->borrowed_page_used = arena_alloc_array(arena, page_capacity, sizeof(uint16_t), alignof(uint16_t));
    tick->borrowed_log =
        arena_alloc_array(arena, WORLD_TICK_RECORDS, sizeof(world_edit_record_t), alignof(world_edit_record_t));
    tick->borrowed_broken =
        arena_alloc_array(arena, WORLD_TICK_BROKEN, sizeof(world_tick_broken_t), alignof(world_tick_broken_t));
    tick->log_cursor = world_edit_log_position(edit);
    tick->node_capacity = node_capacity;
    tick->node_free = TICK_NODE_NONE;
    tick->set_mask = (uint32_t)(set_size - 1u);
    tick->page_capacity = page_capacity;
    tick->random_per_section = WORLD_TICK_RANDOM_PER_SECTION_DEFAULT;
    tick->random_radius = WORLD_TICK_RANDOM_RADIUS_DEFAULT;
    for (uint32_t i = 0u; i < WORLD_TICK_WHEEL; i++) {
        tick->wheel_head[i] = TICK_NODE_NONE;
        tick->wheel_tail[i] = TICK_NODE_NONE;
    }
}

void world_tick_deinit(world_tick_t *tick) {
    memset(tick, 0, sizeof(*tick));
}

void world_tick_seed_set(world_tick_t *tick, uint64_t seed) {
    tick->seed = seed;
}

const uint8_t *world_tick_section_levels(const world_tick_t *tick, uint32_t slot, uint32_t section) {
    ASSERT_ALWAYS(slot < tick->borrowed_map->slot_count && section < COLUMN_SECTIONS);
    const world_tick_column_t *const column = &tick->borrowed_columns[slot];
    const uint16_t entry = column->pages[section];
    if (entry == 0u || column->generation != tick->borrowed_map->borrowed_slots[slot].generation) {
        return nullptr;
    }
    return tick->borrowed_pages + (uint64_t)(entry - 1u) * WORLD_TICK_PAGE_BYTES;
}

void world_tick_section_levels_restore(world_tick_t *tick, int32_t chunk_x, int32_t chunk_y, uint32_t section,
                                       const uint8_t *levels) {
    ASSERT_ALWAYS(section < COLUMN_SECTIONS);
    const int32_t base_x = chunk_x * (int32_t)COLUMN_SIZE, base_y = chunk_y * (int32_t)COLUMN_SIZE;
    const int32_t base_z = COLUMN_Z_STORAGE_MIN + (int32_t)(section * SECTION_SIZE);
    for (uint32_t voxel = 0u; voxel < SECTION_VOXELS; voxel++) {
        const uint8_t level = (uint8_t)(((uint32_t)levels[voxel / 2u] >> ((voxel & 1u) * 4u)) & 15u);
        if (level == WORLD_TICK_LEVEL_SOURCE || level > WORLD_TICK_LEVEL_FALLING) {
            continue;
        }
        const int32_t x = base_x + (int32_t)(voxel & 15u), y = base_y + (int32_t)((voxel >> 4) & 15u);
        const int32_t z = base_z + (int32_t)(voxel >> 8);
        /* Short of pages it stays a source until something changes it, and page_full_count says so. */
        if (tick_is_fluid(tick_look(tick, x, y, z)) && tick_level_set(tick, x, y, z, level)) {
            tick_wake(tick, x, y, z);
        }
    }
}

void world_tick_now_set(world_tick_t *tick, uint64_t now) {
    ASSERT_ALWAYS(tick->node_live == 0u); /* a node is filed under the count it is due at */
    tick->now = now;
}

void world_tick_column_leaving(world_tick_t *tick, uint32_t slot) {
    ASSERT_ALWAYS(slot < tick->borrowed_map->slot_count);
    world_tick_column_t *const column = &tick->borrowed_columns[slot];
    for (uint32_t i = 0u; i < COLUMN_SECTIONS; i++) {
        if (column->pages[i] != 0u) {
            tick_page_release(tick, &column->pages[i]);
        }
    }
}
