/* See README.md. */
#include "player/felling/index.h"

#include "tables/blocks/index.h"

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

/* What this walk queued goes again, as it was no tree. A tree still coming down keeps its place. */
static void felling_walk_undo(player_felling_t *felling, uint32_t first) {
    if (first == 0u) {
        felling_reset(felling);
        return;
    }
    memset(felling->seen, 0, sizeof(felling->seen));
    felling->tail = first;
    for (uint32_t i = 0u; i < first; i++) {
        const player_felling_cell_t *const cell = &felling->cells[i];
        felling->seen[felling_seen_slot(felling, cell->x, cell->y, cell->z)] = (uint16_t)(i + 1u);
    }
}

static void felling_box_add(player_felling_t *felling, const player_felling_cell_t *cell) {
    const int32_t at[COLLISION_AXES] = {cell->x, cell->y, cell->z};
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        felling->box_low[axis] = at[axis] < felling->box_low[axis] ? at[axis] : felling->box_low[axis];
        felling->box_high[axis] = at[axis] > felling->box_high[axis] ? at[axis] : felling->box_high[axis];
    }
}

uint32_t player_felling_cut(player_felling_t *felling, const world_map_t *map, uint16_t block, int32_t x, int32_t y,
                            int32_t z) {
    const uint32_t first = felling->tail;
    /* The cut itself goes first: it is air by now, so it costs nothing, but its faces may touch leaves and the walk starts from it. */
    if (!felling_push(felling, x, y, z, block)) {
        return 0u;
    }
    bool leafy = false;
    /* The queue is the walk's own list, so that a log's place in it is how many steps it is from the cut. */
    for (uint32_t i = first; i < felling->tail; i++) {
        const player_felling_cell_t here = felling->cells[i];
        for (int32_t dz = -1; dz <= 1; dz++) {
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    const int32_t next_z = (int32_t)here.z + dz;
                    uint16_t there = BLOCK_ID_AIR;
                    /* Outside gameplay's heights, and in a column that has not come, this is not OK. */
                    if ((dx == 0 && dy == 0 && dz == 0) ||
                        world_map_block_get(map, here.x + dx, here.y + dy, next_z, &there) != WORLD_MAP_STATUS_OK) {
                        continue;
                    }
                    if (there == block && next_z >= z) {
                        (void)felling_push(felling, here.x + dx, here.y + dy, next_z, block);
                    }
                    const bool face = dx * dx + dy * dy + dz * dz == 1;
                    leafy = leafy || (face && (block_defs[there].flags & BLOCK_FLAG_DECAYS) != 0u);
                }
            }
        }
    }
    if (!leafy) {
        felling_walk_undo(felling, first);
        return 0u;
    }
    /* In the box now, in case the axe has broken on this very cut and the queue ends with nothing gone. */
    felling_box_add(felling, &felling->cells[first]);
    return felling->tail - first;
}

/* Whether its drops, and a little over, fit in the ring as it stands. */
static bool felling_has_room(const player_interaction_t *hands, uint16_t block) {
    return hands->drop_count + (uint32_t)block_defs[block].drop_count + FELLING_ROOM_SPARE <= PLAYER_INTERACTION_DROPS;
}

uint32_t player_felling_tick(player_felling_t *felling, player_interaction_t *hands, world_edit_t *edit,
                             world_tick_t *tick, uint32_t uses_left) {
    if (felling->tail == 0u) {
        return 0u;
    }
    uint32_t felled = 0u;
    while (felling->head < felling->tail && felled < PLAYER_FELLING_PER_TICK && felled < uses_left) {
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
    /* What is left of the axe has gone, so what is left of the tree stays. */
    if (felling->head == felling->tail || felled >= uses_left) {
        world_tick_leaves_check(tick, felling->box_low, felling->box_high);
        felling_reset(felling);
    }
    return felled;
}
