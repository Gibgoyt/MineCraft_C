/* See README.md. The Arena tier: its arrays come once from the arena its Owner gives it, and nothing here takes more. */
#include "entity/grid/index.h"

#include <math.h>
#include <string.h>

/* The published shove: how much, per tick, and how near counts as in one place. */
#define ENTITY_GRID_PUSH_STRENGTH 0.05f64
#define ENTITY_GRID_PUSH_NEAREST 0.01f64
/* Two in the very same place have no way apart to go, so each pair gets one of its own: this many radians per slot, the golden
 * angle, which lands no two near each other. */
#define ENTITY_GRID_PAIR_TURN 2.399963229728653f64

enum : uint32_t {
    ENTITY_GRID_NONE = UINT32_MAX,
    /* A key tells apart 2^26 cells on x and y, past the world's limits, and 2^12 on z. A query wider than that never walks cells. */
    ENTITY_GRID_KEY_BITS_XY = 26u,
    ENTITY_GRID_KEY_BITS_Z = 12u,
};

static int64_t entity_grid_cell_of(float64_t value) {
    return (int64_t)floor(value / ENTITY_GRID_CELL_BLOCKS);
}

static uint64_t entity_grid_key(int64_t x, int64_t y, int64_t z) {
    const uint64_t mask_xy = (1ull << ENTITY_GRID_KEY_BITS_XY) - 1ull;
    const uint64_t mask_z = (1ull << ENTITY_GRID_KEY_BITS_Z) - 1ull;
    return (((uint64_t)x & mask_xy) << (ENTITY_GRID_KEY_BITS_XY + ENTITY_GRID_KEY_BITS_Z)) |
           (((uint64_t)y & mask_xy) << ENTITY_GRID_KEY_BITS_Z) | ((uint64_t)z & mask_z);
}

static uint32_t entity_grid_bucket_of(uint64_t key) {
    static_assert((ENTITY_GRID_BUCKETS & (ENTITY_GRID_BUCKETS - 1u)) == 0u);
    /* High bits of a multiply by the golden ratio: neighbouring cells land far apart. */
    return (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 40u) & (ENTITY_GRID_BUCKETS - 1u);
}

static bool entity_grid_boxes_overlap(const collision_box_t *a, const collision_box_t *b) {
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        if (a->max[axis] <= b->min[axis] || b->max[axis] <= a->min[axis]) {
            return false;
        }
    }
    return true;
}

void entity_grid_start(entity_grid_t *grid, arena_t *arena) {
    memset(grid, 0, sizeof(*grid));
    grid->borrowed_entries =
        arena_alloc_array(arena, ENTITY_STORE_CAPACITY, sizeof(entity_grid_entry_t), alignof(entity_grid_entry_t));
    grid->borrowed_buckets = arena_alloc_array(arena, ENTITY_GRID_BUCKETS, sizeof(uint32_t), alignof(uint32_t));
    memset(grid->borrowed_buckets, 0xFF, (uint64_t)ENTITY_GRID_BUCKETS * sizeof(uint32_t));
}

void entity_grid_build(entity_grid_t *grid, const entity_store_t *store) {
    if (store->count == 0u && grid->count == 0u) {
        return; /* a world with nobody in it costs nothing */
    }
    memset(grid->borrowed_buckets, 0xFF, (uint64_t)ENTITY_GRID_BUCKETS * sizeof(uint32_t));
    grid->count = store->count;
    memset(grid->reach, 0, sizeof(grid->reach));
    /* From the end, so that each bucket's chain runs in the store's order. */
    for (uint32_t i = store->count; i-- > 0u;) {
        entity_grid_entry_t *const entry = &grid->borrowed_entries[i];
        entity_store_box(store, i, &entry->box);
        int64_t cell[COLLISION_AXES];
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            const float64_t reach = (entry->box.max[a] - entry->box.min[a]) * 0.5f64;
            grid->reach[a] = reach > grid->reach[a] ? reach : grid->reach[a];
            cell[a] = entity_grid_cell_of((entry->box.min[a] + entry->box.max[a]) * 0.5f64);
        }
        entry->cell = entity_grid_key(cell[0], cell[1], cell[2]);
        entry->handle = store->borrowed_handle[i];
        const uint32_t bucket = entity_grid_bucket_of(entry->cell);
        entry->next = grid->borrowed_buckets[bucket];
        grid->borrowed_buckets[bucket] = i;
    }
}

/* Whether this entry answers the query, and if so, counts it. */
static uint32_t entity_grid_take(const entity_store_t *store, const entity_grid_entry_t *entry,
                                 const collision_box_t *box, entity_handle_t *out, uint32_t max, uint32_t found) {
    uint32_t alive_at = 0u;
    if (!entity_grid_boxes_overlap(&entry->box, box) || !entity_store_index_of(store, entry->handle, &alive_at)) {
        return found;
    }
    if (found < max) {
        out[found] = entry->handle;
    }
    return found + 1u;
}

uint32_t entity_grid_query(entity_grid_t *grid, const entity_store_t *store, const collision_box_t *box,
                           entity_handle_t *out, uint32_t max) {
    float64_t from[COLLISION_AXES], to[COLLISION_AXES];
    float64_t cells = 1.0f64;
    for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
        from[a] = floor((box->min[a] - grid->reach[a]) / ENTITY_GRID_CELL_BLOCKS);
        to[a] = floor((box->max[a] + grid->reach[a]) / ENTITY_GRID_CELL_BLOCKS);
        cells *= to[a] - from[a] + 1.0f64;
    }
    uint32_t found = 0u;
    /* With more cells than bodies, looking at each entry is cheaper. So no walk is wider than a key can tell apart, and, written this
     * way round, a box with a NaN or an infinity in it comes here too. */
    if (!(cells <= (float64_t)grid->count)) {
        grid->list_walks++;
        for (uint32_t i = 0u; i < grid->count; i++) {
            found = entity_grid_take(store, &grid->borrowed_entries[i], box, out, max, found);
        }
        return found;
    }
    grid->cell_walks++;
    for (int64_t x = (int64_t)from[0]; x <= (int64_t)to[0]; x++) {
        for (int64_t y = (int64_t)from[1]; y <= (int64_t)to[1]; y++) {
            for (int64_t z = (int64_t)from[2]; z <= (int64_t)to[2]; z++) {
                const uint64_t key = entity_grid_key(x, y, z);
                uint32_t i = grid->borrowed_buckets[entity_grid_bucket_of(key)];
                while (i != ENTITY_GRID_NONE) {
                    const entity_grid_entry_t *const entry = &grid->borrowed_entries[i];
                    /* A bucket holds other cells too, which another turn of this walk may come to. */
                    found = entry->cell == key ? entity_grid_take(store, entry, box, out, max, found) : found;
                    i = entry->next;
                }
            }
        }
    }
    return found;
}

/* ---- Pushing apart ------------------------------------------------------------------------------------------------- */

static void entity_grid_push_pair(entity_grid_t *grid, entity_store_t *store, const entity_grid_entry_t *a,
                                  const entity_grid_entry_t *b) {
    uint32_t ia = 0u, ib = 0u;
    if (!entity_store_index_of(store, a->handle, &ia) || !entity_store_index_of(store, b->handle, &ib) ||
        ((store->borrowed_flags[ia] | store->borrowed_flags[ib]) & ENTITY_STORE_FLAG_WAITING) != 0u) {
        return;
    }
    float64_t x = (b->box.min[0] + b->box.max[0]) * 0.5f64 - (a->box.min[0] + a->box.max[0]) * 0.5f64;
    float64_t y = (b->box.min[1] + b->box.max[1]) * 0.5f64 - (a->box.min[1] + a->box.max[1]) * 0.5f64;
    float64_t most = fabs(x) > fabs(y) ? fabs(x) : fabs(y);
    if (most < ENTITY_GRID_PUSH_NEAREST) {
        const float64_t turn =
            ENTITY_GRID_PAIR_TURN * (float64_t)(entity_handle_slot(a->handle) + entity_handle_slot(b->handle));
        x = cos(turn);
        y = sin(turn);
        most = 1.0f64;
    }
    /* As published: a way apart of the root of its size, and weaker past a block apart. */
    const float64_t root = sqrt(most);
    const float64_t strength = ENTITY_GRID_PUSH_STRENGTH / root * (root > 1.0f64 ? 1.0f64 / root : 1.0f64);
    store->borrowed_velocity[0][ia] -= x * strength;
    store->borrowed_velocity[1][ia] -= y * strength;
    store->borrowed_velocity[0][ib] += x * strength;
    store->borrowed_velocity[1][ib] += y * strength;
    grid->pushes++;
}

void entity_grid_push_apart(entity_grid_t *grid, entity_store_t *store) {
    for (uint32_t i = 0u; i < grid->count; i++) {
        const entity_grid_entry_t *const entry = &grid->borrowed_entries[i];
        int64_t from[COLLISION_AXES], to[COLLISION_AXES];
        for (uint32_t a = 0u; a < COLLISION_AXES; a++) {
            from[a] = entity_grid_cell_of(entry->box.min[a] - grid->reach[a]);
            to[a] = entity_grid_cell_of(entry->box.max[a] + grid->reach[a]);
        }
        uint32_t pushed = 0u;
        for (int64_t x = from[0]; x <= to[0] && pushed < ENTITY_GRID_PUSHES_MAX; x++) {
            for (int64_t y = from[1]; y <= to[1] && pushed < ENTITY_GRID_PUSHES_MAX; y++) {
                for (int64_t z = from[2]; z <= to[2] && pushed < ENTITY_GRID_PUSHES_MAX; z++) {
                    const uint64_t key = entity_grid_key(x, y, z);
                    uint32_t other = grid->borrowed_buckets[entity_grid_bucket_of(key)];
                    while (other != ENTITY_GRID_NONE && pushed < ENTITY_GRID_PUSHES_MAX) {
                        const entity_grid_entry_t *const candidate = &grid->borrowed_entries[other];
                        /* Each pair once: the earlier of the two goes looking for the later. */
                        if (other > i && candidate->cell == key &&
                            entity_grid_boxes_overlap(&candidate->box, &entry->box)) {
                            entity_grid_push_pair(grid, store, entry, candidate);
                            pushed++;
                        }
                        other = candidate->next;
                    }
                }
            }
        }
    }
}
