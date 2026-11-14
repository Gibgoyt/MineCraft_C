/* See README.md. */
#include "player/felling/index.h"

#include "tables/blocks/index.h"
#include "world/wall_torch/index.h"

#include <string.h>

enum : uint32_t {
    /* A log's own rows, and this over, for what stood on it: so that a push never reaches a drop still waiting. */
    FELLING_ROOM_SPARE = 1u,
};

static void felling_reset(player_felling_t *felling) {
    felling->head = 0u;
    felling->tail = 0u;
    memset(felling->seen, 0, sizeof(felling->seen));
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        felling->box_low[axis] = INT32_MAX;
        felling->box_high[axis] = INT32_MIN;
    }
}

void player_felling_start(player_felling_t *felling) {
    memset(felling, 0, sizeof(*felling));
    felling_reset(felling);
}

static uint32_t felling_hash(int32_t x, int32_t y, int32_t z) {
    const uint32_t mixed = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
    return (mixed ^ (mixed >> 13u)) & (PLAYER_FELLING_SEEN - 1u);
}

/* Where this cell's number is in seen, or the empty place it would go. */
static uint32_t felling_seen_slot(const player_felling_t *felling, int32_t x, int32_t y, int32_t z) {
    uint32_t slot = felling_hash(x, y, z);
    while (felling->seen[slot] != 0u) {
        const player_felling_cell_t *const cell = &felling->cells[felling->seen[slot] - 1u];
        if (cell->x == x && cell->y == y && (int32_t)cell->z == z) {
            break;
        }
        slot = (slot + 1u) & (PLAYER_FELLING_SEEN - 1u);
    }
    return slot;
}

/* In the queue from now on, unless it already is or there is no room. */
static bool felling_push(player_felling_t *felling, int32_t x, int32_t y, int32_t z, uint16_t block) {
    const uint32_t slot = felling_seen_slot(felling, x, y, z);
    if (felling->seen[slot] != 0u) {
        return false;
    }
    if (felling->tail == PLAYER_FELLING_LOGS_MAX) {
        felling->turned_away_count++;
        return false;
    }
    felling->cells[felling->tail] = (player_felling_cell_t){.x = x, .y = y, .z = (int16_t)z, .block = block};
    felling->tail++;
    felling->seen[slot] = (uint16_t)felling->tail;
    return true;
}

static void felling_box_add(player_felling_t *felling, const player_felling_cell_t *cell) {
    const int32_t at[COLLISION_AXES] = {cell->x, cell->y, cell->z};
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        felling->box_low[axis] = at[axis] < felling->box_low[axis] ? at[axis] : felling->box_low[axis];
        felling->box_high[axis] = at[axis] > felling->box_high[axis] ? at[axis] : felling->box_high[axis];
    }
}

enum : uint8_t {
    FELLING_FOUND_ROOT = 1u,   /* ground under it, and ground or the same wood on 3 of its 4 sides */
    FELLING_FOUND_FALLS = 2u,  /* to fall */
    FELLING_FOUND_JOINED = 4u, /* a flood has been here */
    FELLING_FOUND_CUT = 8u,    /* the cut itself, air by now: it joins nothing */
};

enum : uint32_t {
    /* Of a root log's 4 sides, how many must be ground or its own wood. */
    FELLING_ROOT_SIDES = 3u,
};

/* What the find made of the cut. */
typedef enum felling_find_t : uint32_t {
    FELLING_FIND_TREE = 0,
    FELLING_FIND_UNLOADED = 1,
    FELLING_FIND_OVERFLOW = 2,
    FELLING_FIND_NOT_A_TREE = 3,
} felling_find_t;

/* Solid, and neither a log nor leaves. */
static bool felling_is_ground(uint16_t block) {
    const uint32_t flags = block_defs[block].flags;
    return (flags & BLOCK_FLAG_SOLID) != 0u && (flags & (BLOCK_FLAG_LOG | BLOCK_FLAG_DECAYS)) == 0u;
}

/* Master 2.1: a log set in the ground. around is its 26 neighbours, as [dz + 1][dy + 1][dx + 1]. A log a player puts on the ground has
 * air beside it, and one on another log has no ground under it. */
static bool felling_is_root(const uint16_t around[3][3][3], uint16_t block) {
    if (!felling_is_ground(around[0][1][1])) {
        return false;
    }
    const uint16_t sides[4] = {around[1][1][0], around[1][1][2], around[1][0][1], around[1][2][1]};
    uint32_t held = 0u;
    for (uint32_t i = 0u; i < 4u; i++) {
        held += sides[i] == block || felling_is_ground(sides[i]) ? 1u : 0u;
    }
    return held >= FELLING_ROOT_SIDES;
}

/* Where this cell's number is in found_seen, or the empty place it would go. */
static uint32_t felling_find_slot(const player_felling_t *felling, int32_t x, int32_t y, int32_t z) {
    uint32_t slot = felling_hash(x, y, z) * 4u & (PLAYER_FELLING_FIND_SEEN - 1u);
    while (felling->found_seen[slot] != 0u) {
        const player_felling_cell_t *const cell = &felling->found[felling->found_seen[slot] - 1u];
        if (cell->x == x && cell->y == y && (int32_t)cell->z == z) {
            break;
        }
        slot = (slot + 1u) & (PLAYER_FELLING_FIND_SEEN - 1u);
    }
    return slot;
}

/* The log's number in found, or UINT32_MAX. */
static uint32_t felling_found_at(const player_felling_t *felling, int32_t x, int32_t y, int32_t z) {
    const uint16_t number = felling->found_seen[felling_find_slot(felling, x, y, z)];
    return number == 0u ? UINT32_MAX : (uint32_t)number - 1u;
}

/* Into found, unless it is there. False when found is full. */
static bool felling_find_push(player_felling_t *felling, int32_t x, int32_t y, int32_t z, uint16_t block) {
    const uint32_t slot = felling_find_slot(felling, x, y, z);
    if (felling->found_seen[slot] != 0u) {
        return true;
    }
    if (felling->found_count == PLAYER_FELLING_FIND_MAX) {
        return false;
    }
    const uint32_t index = felling->found_count++;
    felling->found[index] = (player_felling_cell_t){.x = x, .y = y, .z = (int16_t)z, .block = block};
    felling->found_flags[index] = 0u;
    felling->found_column[index] = 0u;
    felling->found_seen[slot] = (uint16_t)(index + 1u);
    return true;
}

/* A log's 26 neighbours, air outside gameplay's heights. False when one is in a column not loaded. */
static bool felling_around(const world_map_t *map, const player_felling_cell_t *here, uint16_t around[3][3][3]) {
    for (int32_t dz = -1; dz <= 1; dz++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                uint16_t there = BLOCK_ID_AIR;
                const world_map_status_t status =
                    world_map_block_get(map, here->x + dx, here->y + dy, (int32_t)here->z + dz, &there);
                if (status == WORLD_MAP_STATUS_NOT_LOADED) {
                    return false;
                }
                around[dz + 1][dy + 1][dx + 1] = status == WORLD_MAP_STATUS_OK ? there : BLOCK_ID_AIR;
            }
        }
    }
    around[1][1][1] = BLOCK_ID_AIR;
    return true;
}

/* Step 1: the tree, up and down from the cut, with its root logs. Unsure is no tree. found[0] is the cut. */
static felling_find_t felling_find(player_felling_t *felling, const world_map_t *map, uint16_t block, int32_t x,
                                   int32_t y, int32_t z) {
    memset(felling->found_seen, 0, sizeof(felling->found_seen));
    felling->found_count = 0u;
    (void)felling_find_push(felling, x, y, z, block);
    felling->found_flags[0] = FELLING_FOUND_CUT;
    bool leafy = false, rooted = false;
    for (uint32_t i = 0u; i < felling->found_count; i++) {
        const player_felling_cell_t here = felling->found[i];
        uint16_t around[3][3][3];
        if (!felling_around(map, &here, around)) {
            return FELLING_FIND_UNLOADED;
        }
        for (int32_t dz = -1; dz <= 1; dz++) {
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    const uint16_t there = around[dz + 1][dy + 1][dx + 1];
                    if (there == block &&
                        !felling_find_push(felling, here.x + dx, here.y + dy, (int32_t)here.z + dz, block)) {
                        return FELLING_FIND_OVERFLOW;
                    }
                    const bool face = dx * dx + dy * dy + dz * dz == 1;
                    leafy = leafy || (face && (block_defs[there].flags & BLOCK_FLAG_DECAYS) != 0u);
                }
            }
        }
        if (i != 0u && felling_is_root(around, block)) {
            felling->found_flags[i] |= FELLING_FOUND_ROOT;
            rooted = true;
        }
    }
    return leafy && rooted ? FELLING_FIND_TREE : FELLING_FIND_NOT_A_TREE;
}

/* The trunk column at x, y, plus 1, or 0. */
static uint8_t felling_column_of(const player_felling_t *felling, int32_t x, int32_t y) {
    for (uint32_t c = 0u; c < felling->trunk_count; c++) {
        if (felling->trunk_x[c] == x && felling->trunk_y[c] == y) {
            return (uint8_t)(c + 1u);
        }
    }
    return 0u;
}

/* The columns holding a root log, and each log's column. False past PLAYER_FELLING_TRUNK_COLUMNS. */
static bool felling_columns(player_felling_t *felling) {
    felling->trunk_count = 0u;
    for (uint32_t i = 0u; i < felling->found_count; i++) {
        const player_felling_cell_t *const log = &felling->found[i];
        if ((felling->found_flags[i] & FELLING_FOUND_ROOT) == 0u || felling_column_of(felling, log->x, log->y) != 0u) {
            continue;
        }
        if (felling->trunk_count == PLAYER_FELLING_TRUNK_COLUMNS) {
            return false;
        }
        felling->trunk_x[felling->trunk_count] = log->x;
        felling->trunk_y[felling->trunk_count] = log->y;
        felling->trunk_in[felling->trunk_count] = 0u;
        felling->trunk_count++;
    }
    for (uint32_t i = 0u; i < felling->found_count; i++) {
        felling->found_column[i] = felling_column_of(felling, felling->found[i].x, felling->found[i].y);
    }
    return true;
}

/* The trunk: the trunk columns joined to this one by a side or corner, through trunk columns. */
static void felling_trunk(player_felling_t *felling, uint8_t column) {
    felling->trunk_in[column - 1u] = 1u;
    for (bool grew = true; grew;) {
        grew = false;
        for (uint32_t c = 0u; c < felling->trunk_count; c++) {
            for (uint32_t d = 0u; d < felling->trunk_count && felling->trunk_in[c] == 0u; d++) {
                const int32_t dx = felling->trunk_x[c] - felling->trunk_x[d];
                const int32_t dy = felling->trunk_y[c] - felling->trunk_y[d];
                if (felling->trunk_in[d] != 0u && dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
                    felling->trunk_in[c] = 1u;
                    grew = true;
                }
            }
        }
    }
}

/* Above the layer at h and in no other trunk's columns, for the layer's flood; or, with h at INT32_MIN, not falling, for nothing-floats. */
static bool felling_may_step(const player_felling_t *felling, uint32_t index, int32_t h) {
    if (h == INT32_MIN) {
        return (felling->found_flags[index] & FELLING_FOUND_FALLS) == 0u;
    }
    const uint8_t column = felling->found_column[index];
    return (int32_t)felling->found[index].z > h && (column == 0u || felling->trunk_in[column - 1u] != 0u);
}

/* From every log in found_queue[0, tail), through the 26 neighbours felling_may_step allows, marking each FELLING_FOUND_JOINED. Never
 * through the cut. */
static void felling_flood(player_felling_t *felling, uint32_t tail, int32_t h) {
    for (uint32_t head = 0u; head < tail; head++) {
        const player_felling_cell_t here = felling->found[felling->found_queue[head]];
        for (int32_t dz = -1; dz <= 1; dz++) {
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    const uint32_t next = felling_found_at(felling, here.x + dx, here.y + dy, (int32_t)here.z + dz);
                    if (next == UINT32_MAX ||
                        (felling->found_flags[next] & (FELLING_FOUND_CUT | FELLING_FOUND_JOINED)) != 0u ||
                        !felling_may_step(felling, next, h)) {
                        continue;
                    }
                    felling->found_flags[next] |= FELLING_FOUND_JOINED;
                    felling->found_queue[tail++] = (uint16_t)next;
                }
            }
        }
    }
}

/* Step 2: when the cut emptied a layer of its trunk, everything above that the trunk's logs above join falls. Returns the logs left in
 * the layer, or UINT32_MAX for a cut outside every trunk column. */
static uint32_t felling_layer(player_felling_t *felling) {
    const player_felling_cell_t cut = felling->found[0];
    const uint8_t column = felling_column_of(felling, cut.x, cut.y);
    if (column == 0u) {
        return UINT32_MAX;
    }
    felling_trunk(felling, column);
    const int32_t h = (int32_t)cut.z;
    uint32_t layer = 0u, tail = 0u;
    for (uint32_t i = 1u; i < felling->found_count; i++) {
        const uint8_t at = felling->found_column[i];
        if (at == 0u || felling->trunk_in[at - 1u] == 0u) {
            continue;
        }
        if ((int32_t)felling->found[i].z == h) {
            layer++;
        } else if ((int32_t)felling->found[i].z > h) {
            felling->found_flags[i] |= FELLING_FOUND_JOINED;
            felling->found_queue[tail++] = (uint16_t)i;
        }
    }
    if (layer == 0u) {
        felling_flood(felling, tail, h);
    }
    for (uint32_t i = 1u; i < felling->found_count; i++) {
        if (layer == 0u && (felling->found_flags[i] & FELLING_FOUND_JOINED) != 0u) {
            felling->found_flags[i] |= FELLING_FOUND_FALLS;
        }
        felling->found_flags[i] &= (uint8_t)~FELLING_FOUND_JOINED;
    }
    return layer;
}

/* Step 3: what no longer joins a root log falls too. Returns the logs that stay. */
static uint32_t felling_nothing_floats(player_felling_t *felling) {
    uint32_t tail = 0u;
    for (uint32_t i = 1u; i < felling->found_count; i++) {
        if ((felling->found_flags[i] & (FELLING_FOUND_ROOT | FELLING_FOUND_FALLS)) == FELLING_FOUND_ROOT) {
            felling->found_flags[i] |= FELLING_FOUND_JOINED;
            felling->found_queue[tail++] = (uint16_t)i;
        }
    }
    felling_flood(felling, tail, INT32_MIN);
    uint32_t stays = 0u;
    for (uint32_t i = 1u; i < felling->found_count; i++) {
        if ((felling->found_flags[i] & FELLING_FOUND_JOINED) != 0u) {
            stays++;
        } else {
            felling->found_flags[i] |= FELLING_FOUND_FALLS;
        }
        felling->found_flags[i] &= (uint8_t)~FELLING_FOUND_JOINED;
    }
    return stays;
}

/* Steps 1 to 3, counting what made a cut fell nothing. Returns how many fall, the cut not among them, or UINT32_MAX for no tree. */
static uint32_t felling_decide(player_felling_t *felling, const world_map_t *map, uint16_t block, int32_t x, int32_t y,
                               int32_t z) {
    felling->last_layer = UINT32_MAX;
    felling->last_stump = 0u;
    switch (felling_find(felling, map, block, x, y, z)) {
    case FELLING_FIND_UNLOADED:
        felling->find_unloaded_count++;
        return UINT32_MAX;
    case FELLING_FIND_OVERFLOW:
        felling->find_overflow_count++;
        return UINT32_MAX;
    case FELLING_FIND_NOT_A_TREE:
        felling->not_a_tree_count++;
        return UINT32_MAX;
    case FELLING_FIND_TREE:
        break;
    }
    if (!felling_columns(felling)) {
        felling->find_overflow_count++;
        return UINT32_MAX;
    }
    felling->last_layer = felling_layer(felling);
    felling->last_stump = felling_nothing_floats(felling);
    uint32_t falls = 0u;
    for (uint32_t i = 1u; i < felling->found_count; i++) {
        falls += (felling->found_flags[i] & FELLING_FOUND_FALLS) != 0u ? 1u : 0u;
    }
    return falls;
}

uint32_t player_felling_cut(player_felling_t *felling, const world_map_t *map, uint16_t block, int32_t x, int32_t y,
                            int32_t z) {
    uint32_t falls = felling_decide(felling, map, block, x, y, z);
    if (falls == UINT32_MAX) {
        return 0u;
    }
    /* All of it or none: a tree half down would float. */
    if (felling->tail + 1u + falls > PLAYER_FELLING_LOGS_MAX) {
        felling->turned_away_count += falls;
        falls = 0u;
    }
    const uint32_t first = felling->tail;
    /* The cut itself goes first: it is air by now, so it costs nothing, but its faces may touch leaves. found is in walk order, which
     * is how many steps each is from the cut, so the rest go nearest first. */
    if (!felling_push(felling, x, y, z, block)) {
        return 0u;
    }
    if (falls == 0u) {
        felling->stood_count++;
    }
    for (uint32_t i = 1u; i < felling->found_count && falls > 0u; i++) {
        const player_felling_cell_t *const log = &felling->found[i];
        if ((felling->found_flags[i] & FELLING_FOUND_FALLS) != 0u) {
            (void)felling_push(felling, log->x, log->y, log->z, log->block);
        }
    }
    /* In the box now, in case the rest go by other hands first and the queue ends with nothing gone. */
    felling_box_add(felling, &felling->cells[first]);
    return felling->tail - first;
}

/* Whether its drops, and a little over, fit in the ring as it stands. */
static bool felling_has_room(const player_interaction_t *hands, uint16_t block) {
    return hands->drop_count + (uint32_t)block_defs[block].drop_count + FELLING_ROOM_SPARE + WALL_TORCH_SIDES <=
           PLAYER_INTERACTION_DROPS;
}

uint32_t player_felling_tick(player_felling_t *felling, player_interaction_t *hands, world_edit_t *edit,
                             world_tick_t *tick) {
    if (felling->tail == 0u) {
        return 0u;
    }
    uint32_t felled = 0u;
    while (felling->head < felling->tail && felled < PLAYER_FELLING_PER_TICK) {
        const player_felling_cell_t cell = felling->cells[felling->head];
        uint16_t there = BLOCK_ID_AIR;
        if (world_map_block_get(edit->borrowed_map, cell.x, cell.y, cell.z, &there) != WORLD_MAP_STATUS_OK ||
            there != cell.block) {
            /* The cut itself, one the hands got to first, or a column that has gone. */
            felling->head++;
            continue;
        }
        if (!felling_has_room(hands, cell.block)) {
            break;
        }
        const world_edit_status_t status = world_edit_block_set(edit, cell.x, cell.y, cell.z, BLOCK_ID_AIR);
        if (status == WORLD_EDIT_STATUS_NO_MEMORY) {
            /* It stays in the queue, and the next tick tries again. */
            hands->no_memory_count++;
            break;
        }
        felling->head++;
        if (status != WORLD_EDIT_STATUS_CHANGED) {
            continue;
        }
        felling_box_add(felling, &cell);
        hands->mined_count++;
        felling->felled_count++;
        felling->last_block = cell.block;
        felled++;
        if (player_interaction_harvests(cell.block, hands->held_tool, hands->held_tier)) {
            player_interaction_drops_spawn(hands, cell.block, cell.x, cell.y, cell.z);
        }
        player_interaction_stack_break(hands, edit, cell.x, cell.y, cell.z);
    }
    if (felling->head == felling->tail) {
        world_tick_leaves_check(tick, felling->box_low, felling->box_high);
        felling_reset(felling);
    }
    return felled;
}
