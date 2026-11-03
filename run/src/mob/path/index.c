/* See README.md. The Arena tier: its arrays come from the arena its Owner gives it, and a worker's scratch from the Job arena it has. */
#include "mob/path/index.h"

#include "assert/index.h"
#include "physics/collision/index.h"
#include "tables/blocks/index.h"
#include "world/column/index.h"

#include <math.h>
#include <sched.h>
#include <string.h>

/* What a cell is to a body. The first three let one through. */
typedef enum mp_class_t : uint8_t {
    MP_CLASS_OPEN = 0,
    MP_CLASS_WATER = 1,
    MP_CLASS_CLIMB = 2,
    MP_CLASS_SLAB = 3,
    MP_CLASS_FLOOR = 4,   /* something solid to stand on */
    MP_CLASS_BLOCKED = 5, /* not loaded, or it hurts: no body goes in or stands on it */
} mp_class_t;

enum : uint32_t {
    MP_RISE_MAX_HALVES = 2u, /* a step of 0.6 walks up one, a jump of 1.25 reaches two */
    MP_EDIT_BATCH = 64u,
    MP_TABLE_SIZE_MIN = 16u,
    MP_HASH_FACTOR = 0x9E3779B1u,
    MP_NOT_A_NODE = UINT32_MAX,
};

/* How near a waypoint counts as reached, how far above or below it, and when a rise or a fluid wants a jump. */
#define MP_FOLLOW_REACH 0.4f64
#define MP_FOLLOW_ABOVE 0.7f64
#define MP_FOLLOW_BELOW 0.3f64
#define MP_FOLLOW_CENTERED 0.05f64
#define MP_FOLLOW_STEP 0.6f64
#define MP_FOLLOW_CLIMB 0.05f64
#define MP_FOLLOW_SWIM (-0.2f64)
/* A body resting exactly on a face counts as above it, whatever rounding did to its position. */
#define MP_FEET_INSET 1.0e-6f64

/* One search under way. */
typedef struct mp_search_t {
    const world_map_t *map;      /* 0 */
    const mob_path_ask_t *ask;   /* 8: with the goal moved to where a body there would stand */
    mob_path_scratch_t *scratch; /* 16 */
    int32_t start[3];            /* 24: where it stands */
    int32_t box_min[3];          /* 36 */
    int32_t box_max[3];          /* 48 */
    int32_t goal_half;           /* 60: the goal's feet, in half blocks */
    uint32_t table_mask;         /* 64 */
    uint32_t node_limit;         /* 68 */
    uint32_t node_count;         /* 72 */
    uint32_t heap_count;         /* 76 */
    uint32_t best;               /* 80: the node nearest the goal so far */
    uint32_t best_h;             /* 84 */
} mp_search_t;
static_assert(sizeof(mp_search_t) == 88);

/* ---- What a cell means ------------------------------------------------------------------------------------------- */

static uint8_t mp_class_of(uint16_t block) {
    if (block == BLOCK_ID_AIR) {
        return MP_CLASS_OPEN;
    }
    if (block >= BLOCK_COUNT || block == BLOCK_ID_LAVA || block == BLOCK_ID_CACTUS) {
        return MP_CLASS_BLOCKED;
    }
    if (block == BLOCK_ID_WATER) {
        return MP_CLASS_WATER;
    }
    if (block == BLOCK_ID_LADDER || block == BLOCK_ID_VINE) {
        return MP_CLASS_CLIMB;
    }
    if ((block_defs[block].flags & BLOCK_FLAG_SOLID) == 0u) {
        return MP_CLASS_OPEN;
    }
    /* Stairs climb to one side, and a body in the middle of the cell ends on their top half: to this search, a block. */
    return block_defs[block].shape == BLOCK_SHAPE_SLAB ? MP_CLASS_SLAB : MP_CLASS_FLOOR;
}

static uint8_t mp_class_at(const world_map_t *map, int32_t x, int32_t y, int32_t z) {
    uint16_t block = BLOCK_ID_AIR;
    return collision_cell_read(map, x, y, z, &block) ? mp_class_of(block) : (uint8_t)MP_CLASS_BLOCKED;
}

static bool mp_passes(uint8_t class) {
    return class <= MP_CLASS_CLIMB;
}

/* The highest cell a body with its feet at this many half blocks has any part in. */
static int32_t mp_top(int32_t half, uint32_t height) {
    return (half + (int32_t)height - 1) >> 1;
}

static bool mp_clear(const world_map_t *map, int32_t x, int32_t y, int32_t z_from, int32_t z_to) {
    for (int32_t z = z_from; z <= z_to; z++) {
        if (!mp_passes(mp_class_at(map, x, y, z))) {
            return false;
        }
    }
    return true;
}

/* Whether a body can stay with its feet in this cell, and where they would be. */
static bool mp_stand(const world_map_t *map, uint32_t height, int32_t x, int32_t y, int32_t z, int32_t *out_half) {
    const uint8_t class = mp_class_at(map, x, y, z);
    if (class == MP_CLASS_SLAB) {
        *out_half = 2 * z + 1;
    } else if (mp_passes(class)) {
        if (class == MP_CLASS_OPEN && mp_class_at(map, x, y, z - 1) != MP_CLASS_FLOOR) {
            return false;
        }
        *out_half = 2 * z;
    } else {
        return false;
    }
    return mp_clear(map, x, y, z + 1, mp_top(*out_half, height));
}

/* ---- The search's memory: an open-addressed table of places, and a heap of the ones to look at ------------------- */

static uint32_t mp_abs(int32_t value) {
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static uint32_t mp_past(uint32_t distance, uint32_t allowed) {
    return distance > allowed ? distance - allowed : 0u;
}

/* The least it can still cost. It never overstates, and falls by no more than any one move costs, which is what makes the first
 * path to the goal the cheapest, with nothing looked at twice. */
static uint32_t mp_heuristic(const mp_search_t *s, int32_t x, int32_t y, int32_t half) {
    const uint32_t radius = s->ask->goal_radius;
    const uint32_t dx = mp_past(mp_abs(x - s->ask->goal[0]), radius);
    const uint32_t dy = mp_past(mp_abs(y - s->ask->goal[1]), radius);
    const uint32_t longer = dx > dy ? dx : dy;
    const uint32_t shorter = dx > dy ? dy : dx;
    /* A cell near enough may hold feet half a block further than the radius in halves. */
    const uint32_t allowed = radius != 0u ? 2u * radius + 1u : 0u;
    const int32_t rise = s->goal_half - half;
    const uint32_t vertical = rise > 0 ? mp_past((uint32_t)rise, allowed) * MOB_PATH_COST_RISE_HALF
                                       : mp_past(mp_abs(rise), allowed) * MOB_PATH_COST_FALL_HALF;
    return longer * MOB_PATH_COST_STRAIGHT + shorter * (MOB_PATH_COST_DIAGONAL - MOB_PATH_COST_STRAIGHT) + vertical;
}

static bool mp_heap_before(const mob_path_node_t *nodes, uint32_t a, uint32_t b) {
    if (nodes[a].f != nodes[b].f) {
        return nodes[a].f < nodes[b].f;
    }
    /* Of two equally good, the one further along. And never a tie: the same bytes from any run. */
    if (nodes[a].g != nodes[b].g) {
        return nodes[a].g > nodes[b].g;
    }
    return a < b;
}

static void mp_heap_up(mp_search_t *s, uint32_t at) {
    uint32_t *const heap = s->scratch->heap;
    mob_path_node_t *const nodes = s->scratch->nodes;
    const uint32_t node = heap[at];
    while (at > 0u) {
        const uint32_t parent = (at - 1u) / 2u;
        if (!mp_heap_before(nodes, node, heap[parent])) {
            break;
        }
        heap[at] = heap[parent];
        nodes[heap[at]].heap_at = at;
        at = parent;
    }
    heap[at] = node;
    nodes[node].heap_at = at;
}

static uint32_t mp_heap_pop(mp_search_t *s) {
    uint32_t *const heap = s->scratch->heap;
    mob_path_node_t *const nodes = s->scratch->nodes;
    const uint32_t first = heap[0];
    const uint32_t node = heap[--s->heap_count];
    if (s->heap_count == 0u) {
        return first;
    }
    uint32_t at = 0u;
    for (;;) {
        uint32_t child = 2u * at + 1u;
        if (child >= s->heap_count) {
            break;
        }
        if (child + 1u < s->heap_count && mp_heap_before(nodes, heap[child + 1u], heap[child])) {
            child++;
        }
        if (!mp_heap_before(nodes, heap[child], node)) {
            break;
        }
        heap[at] = heap[child];
        nodes[heap[at]].heap_at = at;
        at = child;
    }
    heap[at] = node;
    nodes[node].heap_at = at;
    return first;
}

/* A move to this place costing this much more than the node it is from. A place past a bound is simply not written down. */
static void mp_add(mp_search_t *s, uint32_t from, int32_t x, int32_t y, int32_t z, int32_t half, uint32_t move) {
    const int32_t dx = x - s->start[0], dy = y - s->start[1], dz = z - s->start[2];
    if (mp_abs(dx) > MOB_PATH_REACH_XY || mp_abs(dy) > MOB_PATH_REACH_XY || mp_abs(dz) > MOB_PATH_REACH_Z) {
        return;
    }
    mob_path_node_t *const nodes = s->scratch->nodes;
    uint32_t *const table = s->scratch->table;
    const uint32_t key = (uint32_t)(dx + 128) | (uint32_t)(dy + 128) << 8u | (uint32_t)(dz + 128) << 16u;
    uint32_t at = ((key * MP_HASH_FACTOR) >> 16u) & s->table_mask;
    const uint32_t g = (from == MP_NOT_A_NODE ? 0u : nodes[from].g) + move;
    while (table[at] != 0u) {
        mob_path_node_t *const node = &nodes[table[at] - 1u];
        if (node->dx == dx && node->dy == dy && node->dz == dz) {
            if (node->closed == 0u && g < node->g) {
                node->f = node->f - node->g + g;
                node->g = g;
                node->parent = from;
                mp_heap_up(s, node->heap_at);
            }
            return;
        }
        at = (at + 1u) & s->table_mask;
    }
    if (s->node_count == s->node_limit) {
        return;
    }
    const uint32_t index = s->node_count++;
    const uint32_t h = mp_heuristic(s, x, y, half);
    table[at] = index + 1u;
    nodes[index] = (mob_path_node_t){
        .dx = (int16_t)dx,
        .dy = (int16_t)dy,
        .dz = (int16_t)dz,
        .half = (uint8_t)(half - 2 * z),
        .closed = 0u,
        .g = g,
        .f = g + h,
        .parent = from,
        .heap_at = s->heap_count,
    };
    s->scratch->heap[s->heap_count++] = index;
    mp_heap_up(s, nodes[index].heap_at);
    if (h < s->best_h) {
        s->best_h = h;
        s->best = index;
    }
}

/* ---- The moves ---------------------------------------------------------------------------------------------------- */

static uint32_t mp_vertical_cost(int32_t rise) {
    return rise > 0 ? (uint32_t)rise * MOB_PATH_COST_RISE_HALF : mp_abs(rise) * MOB_PATH_COST_FALL_HALF;
}

/* To a place in another column, moving across for this much. */
static void mp_add_across(mp_search_t *s, uint32_t from, int32_t x, int32_t y, int32_t z, int32_t half, int32_t to_half,
                          uint32_t across) {
    const uint32_t water = mp_class_at(s->map, x, y, z) == MP_CLASS_WATER ? MOB_PATH_COST_WATER : 0u;
    mp_add(s, from, x, y, z, to_half, across + water + mp_vertical_cost(to_half - half));
}

/* One step along x or y: up on to something, on the level, or off an edge down to the first thing there is. */
static void mp_expand_straight(mp_search_t *s, uint32_t from, int32_t x, int32_t y, int32_t z, int32_t half, int32_t sx,
                               int32_t sy) {
    const uint32_t height = s->ask->height;
    const int32_t nx = x + sx, ny = y + sy;
    const int32_t top = mp_top(half, height);
    int32_t to = 0;
    /* A step and a jump both rise here first, so they need room above this cell as well as room there. */
    if (mp_stand(s->map, height, nx, ny, z + 1, &to) && to - half <= (int32_t)MP_RISE_MAX_HALVES &&
        mp_clear(s->map, x, y, top + 1, mp_top(to, height))) {
        mp_add_across(s, from, nx, ny, z + 1, half, to, MOB_PATH_COST_STRAIGHT);
    }
    if (mp_stand(s->map, height, nx, ny, z, &to)) {
        const bool fits =
            to > half ? mp_clear(s->map, x, y, top + 1, mp_top(to, height)) : mp_clear(s->map, nx, ny, z + 1, top);
        if (fits) {
            mp_add_across(s, from, nx, ny, z, half, to, MOB_PATH_COST_STRAIGHT);
        }
        return;
    }
    /* It walks out at the height it is and comes down. */
    if (!mp_clear(s->map, nx, ny, z, top)) {
        return;
    }
    for (uint32_t d = 1u; d <= s->ask->drop; d++) {
        const int32_t below = z - (int32_t)d;
        if (mp_stand(s->map, height, nx, ny, below, &to)) {
            if (half - to <= 2 * (int32_t)s->ask->drop) {
                mp_add_across(s, from, nx, ny, below, half, to, MOB_PATH_COST_STRAIGHT);
            }
            return;
        }
        if (!mp_passes(mp_class_at(s->map, nx, ny, below))) {
            return;
        }
    }
}

/* A step on the diagonal, on the level or up what a step walks. A body is too wide to cut a corner, so both sides must be open. */
static void mp_expand_diagonal(mp_search_t *s, uint32_t from, int32_t x, int32_t y, int32_t z, int32_t half, int32_t sx,
                               int32_t sy) {
    const uint32_t height = s->ask->height;
    const int32_t nx = x + sx, ny = y + sy;
    int32_t to = 0;
    if (!mp_stand(s->map, height, nx, ny, z, &to)) {
        return;
    }
    const int32_t top = mp_top(to > half ? to : half, height);
    if (mp_clear(s->map, nx, y, z, top) && mp_clear(s->map, x, ny, z, top) && mp_clear(s->map, nx, ny, z + 1, top) &&
        mp_clear(s->map, x, y, z + 1, top)) {
        mp_add_across(s, from, nx, ny, z, half, to, MOB_PATH_COST_DIAGONAL);
    }
}

/* Straight up or down, in water or on a ladder. */
static void mp_expand_vertical(mp_search_t *s, uint32_t from, int32_t x, int32_t y, int32_t z) {
    const uint8_t class = mp_class_at(s->map, x, y, z);
    if (class != MP_CLASS_WATER && class != MP_CLASS_CLIMB) {
        return;
    }
    int32_t to = 0;
    if (mp_stand(s->map, s->ask->height, x, y, z + 1, &to)) {
        mp_add(s, from, x, y, z + 1, to, MOB_PATH_COST_CLIMB_UP);
    }
    if (mp_stand(s->map, s->ask->height, x, y, z - 1, &to)) {
        mp_add(s, from, x, y, z - 1, to, MOB_PATH_COST_CLIMB_DOWN);
    }
}

static void mp_box_add(mp_search_t *s, int32_t x, int32_t y, int32_t z_low, int32_t z_high) {
    const int32_t low[3] = {x, y, z_low}, high[3] = {x, y, z_high};
    for (uint32_t a = 0u; a < 3u; a++) {
        s->box_min[a] = low[a] < s->box_min[a] ? low[a] : s->box_min[a];
        s->box_max[a] = high[a] > s->box_max[a] ? high[a] : s->box_max[a];
    }
}

static void mp_expand(mp_search_t *s, uint32_t from) {
    const mob_path_node_t node = s->scratch->nodes[from];
    const int32_t x = s->start[0] + node.dx, y = s->start[1] + node.dy, z = s->start[2] + node.dz;
    const int32_t half = 2 * z + node.half;
    /* Every cell any of the moves below reads. */
    const int32_t reach_up = z + (int32_t)s->ask->height / 2 + 3;
    const int32_t reach_down = z - (int32_t)s->ask->drop - 2;
    mp_box_add(s, x - 1, y - 1, reach_down, reach_up);
    mp_box_add(s, x + 1, y + 1, reach_down, reach_up);
    static const int8_t straight[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static const int8_t diagonal[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (uint32_t i = 0u; i < 4u; i++) {
        mp_expand_straight(s, from, x, y, z, half, straight[i][0], straight[i][1]);
    }
    for (uint32_t i = 0u; i < 4u; i++) {
        mp_expand_diagonal(s, from, x, y, z, half, diagonal[i][0], diagonal[i][1]);
    }
    mp_expand_vertical(s, from, x, y, z);
}

/* ---- The search --------------------------------------------------------------------------------------------------- */

/* The ground under a place: a body in the air is over it, and a body sunk a little is just under it. */
static bool mp_settle(const world_map_t *map, uint32_t height, int32_t x, int32_t y, int32_t z, int32_t *out_z,
                      int32_t *out_half) {
    for (uint32_t d = 0u; d <= MOB_PATH_SETTLE_BLOCKS; d++) {
        const int32_t below = z - (int32_t)d;
        if (mp_stand(map, height, x, y, below, out_half)) {
            *out_z = below;
            return true;
        }
        if (!mp_passes(mp_class_at(map, x, y, below))) {
            break;
        }
    }
    if (mp_stand(map, height, x, y, z + 1, out_half)) {
        *out_z = z + 1;
        return true;
    }
    return false;
}

static void mp_box_settle(mp_search_t *s, const int32_t cell[3]) {
    mp_box_add(s, cell[0], cell[1], cell[2] - (int32_t)MOB_PATH_SETTLE_BLOCKS - 2,
               cell[2] + (int32_t)s->ask->height / 2 + 3);
}

static mob_path_ask_t mp_ask_normal(const mob_path_ask_t *ask) {
    mob_path_ask_t normal = *ask;
    normal.nodes = ask->nodes == 0u ? (uint16_t)MOB_PATH_NODES_DEFAULT
                                    : (ask->nodes > MOB_PATH_NODES_MAX ? (uint16_t)MOB_PATH_NODES_MAX : ask->nodes);
    normal.height = ask->height == 0u
                        ? (uint8_t)1u
                        : (ask->height > MOB_PATH_HEIGHT_MAX ? (uint8_t)MOB_PATH_HEIGHT_MAX : ask->height);
    normal.drop = ask->drop > MOB_PATH_DROP_MAX ? (uint8_t)MOB_PATH_DROP_MAX : ask->drop;
    normal.goal_radius = ask->goal_radius > MOB_PATH_RADIUS_MAX ? (uint8_t)MOB_PATH_RADIUS_MAX : ask->goal_radius;
    memset(normal._pad0, 0, sizeof(normal._pad0));
    return normal;
}

static bool mp_at_goal(const mob_path_ask_t *ask, int32_t x, int32_t y, int32_t z) {
    return mp_abs(x - ask->goal[0]) <= ask->goal_radius && mp_abs(y - ask->goal[1]) <= ask->goal_radius &&
           mp_abs(z - ask->goal[2]) <= ask->goal_radius;
}

/* The way back from a node, turned around. A way too long for the points keeps its first ones, and is no longer a whole way. */
static void mp_points_write(mp_search_t *s, uint32_t last, mob_path_found_t *out) {
    uint32_t *const trail = s->scratch->heap; /* the search is over */
    const mob_path_node_t *const nodes = s->scratch->nodes;
    uint32_t length = 0u;
    for (uint32_t at = last; at != MP_NOT_A_NODE; at = nodes[at].parent) {
        trail[length++] = at;
    }
    const uint32_t kept = length > MOB_PATH_POINTS_MAX ? MOB_PATH_POINTS_MAX : length;
    for (uint32_t i = 0u; i < kept; i++) {
        const mob_path_node_t *const node = &nodes[trail[length - 1u - i]];
        out->points[i] = (mob_path_point_t){
            .x = s->start[0] + node->dx,
            .y = s->start[1] + node->dy,
            .z = s->start[2] + node->dz,
            .cost = (uint16_t)node->g,
            .half = node->half,
        };
    }
    out->point_count = (uint16_t)kept;
    if (length > MOB_PATH_POINTS_MAX) {
        out->status = MOB_PATH_STATUS_PARTIAL;
    }
}

void mob_path_search(const world_map_t *map, const mob_path_ask_t *given, mob_path_scratch_t *scratch,
                     mob_path_found_t *out) {
    const mob_path_ask_t ask = mp_ask_normal(given);
    mp_search_t s = {
        .map = map,
        .ask = &ask,
        .scratch = scratch,
        .start = {ask.start[0], ask.start[1], ask.start[2]},
        .box_min = {ask.start[0], ask.start[1], ask.start[2]},
        .box_max = {ask.start[0], ask.start[1], ask.start[2]},
        .node_limit = ask.nodes,
    };
    /* Every field, padding and unused points too, so that the same search is the same bytes. */
    memset(out, 0, sizeof(*out));
    mp_box_settle(&s, ask.start);
    mp_box_settle(&s, ask.goal);
    memcpy(out->box_min, s.box_min, sizeof(s.box_min));
    memcpy(out->box_max, s.box_max, sizeof(s.box_max));
    int32_t start_half = 0;
    if (!mp_settle(map, ask.height, ask.start[0], ask.start[1], ask.start[2], &s.start[2], &start_half)) {
        out->status = MOB_PATH_STATUS_START_BLOCKED;
        return;
    }
    /* The goal it aims for is where a body there would stand: what it chases may be in the air. */
    mob_path_ask_t aimed = ask;
    if (!mp_settle(map, ask.height, ask.goal[0], ask.goal[1], ask.goal[2], &aimed.goal[2], &s.goal_half)) {
        s.goal_half = 2 * ask.goal[2];
    }
    s.ask = &aimed;

    /* Half full at most, however small the budget, so that a probe always ends. */
    uint32_t table_size = MP_TABLE_SIZE_MIN;
    while (table_size < 2u * s.node_limit) {
        table_size *= 2u;
    }
    s.table_mask = table_size - 1u;
    memset(scratch->table, 0, (uint64_t)table_size * sizeof(uint32_t));

    s.best_h = UINT32_MAX;
    mp_add(&s, MP_NOT_A_NODE, s.start[0], s.start[1], s.start[2], start_half, 0u);
    uint32_t reached = MP_NOT_A_NODE;
    while (s.heap_count != 0u) {
        const uint32_t node = mp_heap_pop(&s);
        const mob_path_node_t *const at = &scratch->nodes[node];
        scratch->nodes[node].closed = 1u;
        if (mp_at_goal(&aimed, s.start[0] + at->dx, s.start[1] + at->dy, s.start[2] + at->dz)) {
            reached = node;
            break;
        }
        mp_expand(&s, node);
    }
    out->nodes = s.node_count;
    memcpy(out->box_min, s.box_min, sizeof(s.box_min));
    memcpy(out->box_max, s.box_max, sizeof(s.box_max));
    if (reached != MP_NOT_A_NODE) {
        out->status = MOB_PATH_STATUS_COMPLETE;
        mp_points_write(&s, reached, out);
    } else if (s.best != 0u) {
        out->status = MOB_PATH_STATUS_PARTIAL;
        mp_points_write(&s, s.best, out);
    } else {
        out->status = MOB_PATH_STATUS_NONE;
    }
}

/* ---- The cache ---------------------------------------------------------------------------------------------------- */

static bool mp_same_goal(const mob_path_ask_t *a, const mob_path_ask_t *b) {
    return a->goal[0] == b->goal[0] && a->goal[1] == b->goal[1] && a->goal[2] == b->goal[2] && a->nodes == b->nodes &&
           a->height == b->height && a->drop == b->drop && a->goal_radius == b->goal_radius;
}

static bool mp_same_start(const mob_path_ask_t *a, const mob_path_ask_t *b) {
    return a->start[0] == b->start[0] && a->start[1] == b->start[1] && a->start[2] == b->start[2];
}

static void mp_entry_drop(mob_path_t *path, mob_path_entry_t *entry) {
    entry->last_used = 0u;
    path->dropped++;
}

/* Columns have come or gone. An answer that met what was not there may be wrong now in ways no edit log says, so only a whole way, with
 * every step of it still standing, is kept. */
static bool mp_entry_fresh(mob_path_t *path, mob_path_entry_t *entry) {
    const uint64_t change = path->borrowed_map->change_count;
    if (entry->map_change == change) {
        return true;
    }
    if (entry->found.status != MOB_PATH_STATUS_COMPLETE) {
        mp_entry_drop(path, entry);
        return false;
    }
    for (uint32_t i = 0u; i < entry->found.point_count; i++) {
        const mob_path_point_t *const point = &entry->found.points[i];
        int32_t half = 0;
        if (!mp_stand(path->borrowed_map, entry->ask.height, point->x, point->y, point->z, &half) ||
            half != 2 * point->z + point->half) {
            mp_entry_drop(path, entry);
            return false;
        }
    }
    entry->map_change = change;
    return true;
}

static uint32_t mp_suffix_at(const mob_path_entry_t *entry, const mob_path_ask_t *ask) {
    for (uint32_t i = 0u; i < entry->found.point_count; i++) {
        const mob_path_point_t *const point = &entry->found.points[i];
        if (point->x == ask->start[0] && point->y == ask->start[1] && point->z == ask->start[2]) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void mp_suffix_write(const mob_path_entry_t *entry, uint32_t first, mob_path_found_t *out) {
    const mob_path_found_t *const whole = &entry->found;
    memset(out, 0, sizeof(*out));
    memcpy(out->box_min, whole->box_min, sizeof(out->box_min));
    memcpy(out->box_max, whole->box_max, sizeof(out->box_max));
    out->nodes = whole->nodes;
    out->status = whole->status;
    out->from_cache = 1u;
    out->point_count = (uint16_t)(whole->point_count - first);
    const uint16_t spent = whole->points[first].cost;
    for (uint32_t i = 0u; i < out->point_count; i++) {
        out->points[i] = whole->points[first + i];
        out->points[i].cost = (uint16_t)(out->points[i].cost - spent);
    }
}

typedef enum mp_hit_t : uint8_t {
    MP_HIT_NONE = 0,
    MP_HIT_EXACT = 1,
    MP_HIT_SUFFIX = 2,
} mp_hit_t;

/* Its exact ask if that is there, or else the rest of a complete way through where it stands. The way on from any point of a cheapest way
 * is itself a cheapest way, so a body that walks a path and asks again costs nothing. Out is left alone if there is neither. */
static mp_hit_t mp_cache_answer(mob_path_t *path, const mob_path_ask_t *ask, mob_path_found_t *out) {
    mob_path_entry_t *suffix_entry = nullptr;
    uint32_t suffix_first = 0u;
    for (uint32_t i = 0u; i < MOB_PATH_CACHE_ENTRIES; i++) {
        mob_path_entry_t *const entry = &path->borrowed_entries[i];
        if (entry->last_used == 0u || !mp_same_goal(&entry->ask, ask) || !mp_entry_fresh(path, entry)) {
            continue;
        }
        if (mp_same_start(&entry->ask, ask)) {
            entry->last_used = ++path->use_clock;
            *out = entry->found;
            out->from_cache = 1u;
            return MP_HIT_EXACT;
        }
        if (suffix_entry == nullptr && entry->found.status == MOB_PATH_STATUS_COMPLETE) {
            const uint32_t first = mp_suffix_at(entry, ask);
            if (first != UINT32_MAX) {
                suffix_entry = entry;
                suffix_first = first;
            }
        }
    }
    if (suffix_entry == nullptr) {
        return MP_HIT_NONE;
    }
    suffix_entry->last_used = ++path->use_clock;
    mp_suffix_write(suffix_entry, suffix_first, out);
    return MP_HIT_SUFFIX;
}

/* In the place of the longest unused. An empty one has a use of 0, so those go first. */
static void mp_cache_keep(mob_path_t *path, const mob_path_item_t *item) {
    mob_path_entry_t *victim = &path->borrowed_entries[0];
    for (uint32_t i = 1u; i < MOB_PATH_CACHE_ENTRIES; i++) {
        mob_path_entry_t *const entry = &path->borrowed_entries[i];
        victim = entry->last_used < victim->last_used ? entry : victim;
    }
    victim->ask = item->ask;
    victim->found = item->found;
    victim->map_change = path->borrowed_map->change_count;
    victim->last_used = ++path->use_clock;
}

static void mp_cache_empty(mob_path_t *path) {
    for (uint32_t i = 0u; i < MOB_PATH_CACHE_ENTRIES; i++) {
        path->borrowed_entries[i].last_used = 0u;
    }
    path->emptied++;
}

static void mp_cache_edit(mob_path_t *path, const world_edit_record_t *record) {
    /* A flower for grass, or stone for dirt, is the same to every search. */
    if (mp_class_of(record->before) == mp_class_of(record->after)) {
        return;
    }
    for (uint32_t i = 0u; i < MOB_PATH_CACHE_ENTRIES; i++) {
        mob_path_entry_t *const entry = &path->borrowed_entries[i];
        const mob_path_found_t *const found = &entry->found;
        if (entry->last_used != 0u && record->x >= found->box_min[0] && record->x <= found->box_max[0] &&
            record->y >= found->box_min[1] && record->y <= found->box_max[1] && record->z >= found->box_min[2] &&
            record->z <= found->box_max[2]) {
            mp_entry_drop(path, entry);
        }
    }
}

static void mp_edits_read(mob_path_t *path) {
    const uint64_t position = world_edit_log_position(path->borrowed_edit);
    if (position - path->edit_cursor > MOB_PATH_EDITS_MAX) {
        mp_cache_empty(path);
        path->edit_cursor = position;
        return;
    }
    while (path->edit_cursor != position) {
        world_edit_record_t records[MP_EDIT_BATCH];
        uint64_t lost = 0u;
        const uint64_t count =
            world_edit_log_read(path->borrowed_edit, &path->edit_cursor, records, MP_EDIT_BATCH, &lost);
        if (lost != 0u) {
            mp_cache_empty(path);
        }
        for (uint64_t i = 0u; i < count; i++) {
            mp_cache_edit(path, &records[i]);
        }
    }
}

/* ---- A round: everyone claims searches until none are left -------------------------------------------------------- */

static bool mp_claim(mob_path_t *path, uint32_t *out_item) {
    uint64_t cursor = atomic_load_explicit(&path->round_cursor, memory_order_acquire);
    for (;;) {
        const uint32_t next = (uint32_t)cursor;
        if (next >= (uint32_t)(cursor >> 32u)) {
            return false;
        }
        if (atomic_compare_exchange_weak_explicit(&path->round_cursor, &cursor, cursor + 1u, memory_order_acq_rel,
                                                  memory_order_acquire)) {
            *out_item = next;
            return true;
        }
    }
}

static void mp_item_run(mob_path_t *path, uint32_t index, mob_path_scratch_t *scratch) {
    mob_path_item_t *const item = &path->borrowed_items[index];
    mob_path_search(path->borrowed_map, &item->ask, scratch, &item->found);
    /* Release: the thread that waits for the round reads what was just written. */
    atomic_fetch_add_explicit(&path->round_done, 1u, memory_order_release);
}

/* A job may start long after its round has ended, when the workers were busy with something else. It then finds nothing to claim, or helps
 * with whatever round is on now, and either is right: so nobody needs to wait for it but mob_path_stop. */
static void mp_job(job_worker_t *worker, void *borrowed_argument) {
    mob_path_t *const path = borrowed_argument;
    mob_path_scratch_t *scratch = nullptr;
    uint32_t index = 0u;
    while (mp_claim(path, &index)) {
        if (scratch == nullptr) {
            scratch = arena_alloc(&worker->arena_job, sizeof(*scratch), alignof(mob_path_scratch_t));
        }
        /* Before the search's own count: the waiter reads this after the round, and must find it whole. */
        atomic_fetch_add_explicit(&path->worker_searches, 1u, memory_order_relaxed);
        mp_item_run(path, index, scratch);
    }
    atomic_fetch_sub_explicit(&path->jobs_out, 1u, memory_order_relaxed);
}

static void mp_round_run(mob_path_t *path, uint32_t count) {
    atomic_store_explicit(&path->round_done, 0u, memory_order_relaxed);
    /* Release: whoever claims from this cursor sees the items, and the map as it stays until this function returns. */
    atomic_store_explicit(&path->round_cursor, (uint64_t)count << 32u, memory_order_release);
    if (path->borrowed_job != nullptr) {
        const uint32_t out = atomic_load_explicit(&path->jobs_out, memory_order_relaxed);
        const uint32_t room = MOB_PATH_JOBS_MAX > out ? MOB_PATH_JOBS_MAX - out : 0u;
        const uint32_t wanted = count - 1u < room ? count - 1u : room;
        for (uint32_t i = 0u; i < wanted; i++) {
            atomic_fetch_add_explicit(&path->jobs_out, 1u, memory_order_relaxed);
            if (!job_try_submit(path->borrowed_job, &path->group, mp_job, path)) {
                atomic_fetch_sub_explicit(&path->jobs_out, 1u, memory_order_relaxed);
                break;
            }
        }
    }
    /* This thread too, so that a tick does not wait on jobs stuck behind a queue of slow ones. */
    uint32_t index = 0u;
    while (mp_claim(path, &index)) {
        mp_item_run(path, index, path->borrowed_scratch);
    }
    /* All claimed: what is left is searches already running, each of them bounded. */
    while (atomic_load_explicit(&path->round_done, memory_order_acquire) != count) {
        (void)sched_yield();
    }
}

/* The body's answer has just been written to its found. */
static void mp_body_answered(mob_path_t *path, mob_path_body_t *body) {
    body->found.serial = ++path->serial;
    body->owner = body->asker;
    body->cursor = 1u;
    body->stuck = 0u;
    body->queued = 0u;
}

static bool mp_round_has(const mob_path_t *path, uint32_t count, const mob_path_ask_t *ask) {
    for (uint32_t i = 0u; i < count; i++) {
        const mob_path_ask_t *const other = &path->borrowed_items[i].ask;
        if (mp_same_goal(other, ask) && mp_same_start(other, ask)) {
            return true;
        }
    }
    return false;
}

/* Answers what the cache can, in the order asked. What it cannot joins the round, as far as the budget goes, and stays in the queue in its
 * order, ready for mp_queue_after_round. Returns how many searches the round has. */
static uint32_t mp_queue_before_round(mob_path_t *path) {
    uint32_t kept = 0u, count = 0u, nodes = 0u;
    for (uint32_t q = 0u; q < path->queue_count; q++) {
        const uint32_t slot = path->borrowed_queue[q];
        mob_path_body_t *const body = &path->borrowed_bodies[slot];
        const mp_hit_t hit = mp_cache_answer(path, &body->ask, &body->found);
        if (hit != MP_HIT_NONE) {
            path->exact_hits += hit == MP_HIT_EXACT ? 1u : 0u;
            path->suffix_hits += hit == MP_HIT_SUFFIX ? 1u : 0u;
            mp_body_answered(path, body);
            continue;
        }
        path->borrowed_queue[kept++] = slot;
        const bool has_room =
            count < MOB_PATH_ROUND_SEARCHES && (count == 0u || nodes + body->ask.nodes <= MOB_PATH_ROUND_NODES);
        if (has_room && !mp_round_has(path, count, &body->ask)) {
            mob_path_item_t *const item = &path->borrowed_items[count++];
            item->ask = body->ask;
            nodes += body->ask.nodes;
        }
    }
    path->queue_count = kept;
    return count;
}

/* The round's answers are in the cache now, so whoever asked gets one, and so does anyone who asked the same, or stands on a way just
 * found. The rest keep their place for the next tick. */
static void mp_queue_after_round(mob_path_t *path, uint32_t count) {
    uint32_t kept = 0u;
    uint64_t exact = 0u;
    for (uint32_t q = 0u; q < path->queue_count; q++) {
        const uint32_t slot = path->borrowed_queue[q];
        mob_path_body_t *const body = &path->borrowed_bodies[slot];
        const mp_hit_t hit = mp_cache_answer(path, &body->ask, &body->found);
        if (hit == MP_HIT_NONE) {
            path->borrowed_queue[kept++] = slot;
            continue;
        }
        if (hit == MP_HIT_EXACT) {
            exact++;
            body->found.from_cache = 0u; /* a search ran for this, this tick */
        } else {
            path->suffix_hits++;
        }
        mp_body_answered(path, body);
    }
    path->queue_count = kept;
    path->shared += exact - count;
}

void mob_path_tick(mob_path_t *path) {
    mp_edits_read(path);
    if (path->queue_count == 0u) {
        return;
    }
    const uint32_t count = mp_queue_before_round(path);
    if (count == 0u) {
        return;
    }
    mp_round_run(path, count);
    for (uint32_t i = 0u; i < count; i++) {
        mp_cache_keep(path, &path->borrowed_items[i]);
        path->nodes += path->borrowed_items[i].found.nodes;
    }
    path->searches += count;
    path->rounds++;
    mp_queue_after_round(path, count);
}

/* ---- Asking ------------------------------------------------------------------------------------------------------- */

void mob_path_start(mob_path_t *path, arena_t *arena, const world_map_t *map, const world_edit_t *edit,
                    job_system_t *job) {
    memset(path, 0, sizeof(*path));
    path->borrowed_map = map;
    path->borrowed_edit = edit;
    path->borrowed_job = job;
    path->borrowed_bodies =
        arena_alloc_zeroed(arena, (uint64_t)MOB_PATH_BODIES * sizeof(mob_path_body_t), MC_CPU_CACHE_LINE_BYTES);
    path->borrowed_queue = arena_alloc_array(arena, MOB_PATH_BODIES, sizeof(uint32_t), MC_CPU_CACHE_LINE_BYTES);
    path->borrowed_entries =
        arena_alloc_zeroed(arena, (uint64_t)MOB_PATH_CACHE_ENTRIES * sizeof(mob_path_entry_t), MC_CPU_CACHE_LINE_BYTES);
    path->borrowed_items =
        arena_alloc_array(arena, MOB_PATH_ROUND_SEARCHES, sizeof(mob_path_item_t), MC_CPU_CACHE_LINE_BYTES);
    path->borrowed_scratch = arena_alloc(arena, sizeof(mob_path_scratch_t), MC_CPU_CACHE_LINE_BYTES);
    path->edit_cursor = world_edit_log_position(edit);
    job_group_init(&path->group);
}

void mob_path_stop(mob_path_t *path) {
    if (path->borrowed_map == nullptr) {
        return;
    }
    if (path->borrowed_job != nullptr) {
        job_group_wait(path->borrowed_job, &path->group);
    }
    job_group_deinit(&path->group);
    memset(path, 0, sizeof(*path));
}

void mob_path_ask_for_body(const entity_store_t *store, uint32_t index, const float64_t goal[3], uint8_t goal_radius,
                           mob_path_ask_t *out) {
    const float64_t height = ceil(2.0f64 * (float64_t)store->borrowed_height[index]);
    *out = (mob_path_ask_t){
        .start = {(int32_t)floor(store->borrowed_position[0][index]),
                  (int32_t)floor(store->borrowed_position[1][index]),
                  (int32_t)floor(store->borrowed_position[2][index] + MP_FEET_INSET)},
        .goal = {(int32_t)floor(goal[0]), (int32_t)floor(goal[1]), (int32_t)floor(goal[2] + MP_FEET_INSET)},
        .nodes = (uint16_t)MOB_PATH_NODES_DEFAULT,
        .height = height >= (float64_t)MOB_PATH_HEIGHT_MAX ? (uint8_t)MOB_PATH_HEIGHT_MAX : (uint8_t)height,
        .drop = (uint8_t)MOB_PATH_DROP_DEFAULT,
        .goal_radius = goal_radius,
    };
}

void mob_path_request(mob_path_t *path, entity_handle_t handle, const mob_path_ask_t *ask) {
    ASSERT_ALWAYS(handle != ENTITY_HANDLE_NONE);
    const uint32_t slot = entity_handle_slot(handle);
    mob_path_body_t *const body = &path->borrowed_bodies[slot];
    body->ask = mp_ask_normal(ask);
    body->asker = handle;
    path->requests++;
    if (body->queued == 0u) {
        body->queued = 1u;
        path->borrowed_queue[path->queue_count++] = slot;
    }
}

const mob_path_found_t *mob_path_found(const mob_path_t *path, entity_handle_t handle) {
    if (handle == ENTITY_HANDLE_NONE) {
        return nullptr;
    }
    const mob_path_body_t *const body = &path->borrowed_bodies[entity_handle_slot(handle)];
    return body->owner == handle ? &body->found : nullptr;
}

/* ---- Walking it --------------------------------------------------------------------------------------------------- */

static bool mp_point_reached(const entity_store_t *store, uint32_t index, const mob_path_point_t *point) {
    const float64_t dx = (float64_t)point->x + 0.5f64 - store->borrowed_position[0][index];
    const float64_t dy = (float64_t)point->y + 0.5f64 - store->borrowed_position[1][index];
    const float64_t feet = (float64_t)point->z + 0.5f64 * (float64_t)point->half;
    const float64_t above = store->borrowed_position[2][index] - feet;
    return dx * dx + dy * dy < MP_FOLLOW_REACH * MP_FOLLOW_REACH && above < MP_FOLLOW_ABOVE && above > -MP_FOLLOW_BELOW;
}

mob_path_follow_t mob_path_follow(mob_path_t *path, entity_store_t *store, entity_handle_t handle, float64_t speed) {
    uint32_t index = 0u;
    const mob_path_found_t *const found = mob_path_found(path, handle);
    if (found == nullptr || !entity_store_index_of(store, handle, &index) || found->point_count == 0u) {
        return MOB_PATH_FOLLOW_NONE;
    }
    mob_path_body_t *const body = &path->borrowed_bodies[entity_handle_slot(handle)];
    while (body->cursor < found->point_count && mp_point_reached(store, index, &found->points[body->cursor])) {
        body->cursor++;
        body->stuck = 0u;
    }
    if (body->cursor >= found->point_count) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        return found->status == MOB_PATH_STATUS_COMPLETE ? MOB_PATH_FOLLOW_ARRIVED : MOB_PATH_FOLLOW_ENDED;
    }
    if (body->stuck >= MOB_PATH_STUCK_TICKS) {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
        return MOB_PATH_FOLLOW_STUCK;
    }
    body->stuck++;
    const mob_path_point_t *const point = &found->points[body->cursor];
    const float64_t dx = (float64_t)point->x + 0.5f64 - store->borrowed_position[0][index];
    const float64_t dy = (float64_t)point->y + 0.5f64 - store->borrowed_position[1][index];
    const float64_t length = sqrt(dx * dx + dy * dy);
    if (length > MP_FOLLOW_CENTERED) {
        entity_store_wish_set(store, index, dx / length * speed, dy / length * speed);
        store->borrowed_yaw[index] = (float32_t)atan2(dy, dx);
    } else {
        entity_store_wish_set(store, index, 0.0f64, 0.0f64);
    }
    const float64_t rise = (float64_t)point->z + 0.5f64 * (float64_t)point->half - store->borrowed_position[2][index];
    const uint8_t flags = store->borrowed_flags[index];
    const bool wants_jump = (flags & ENTITY_STORE_FLAG_IN_WATER) != 0u   ? rise > MP_FOLLOW_SWIM
                            : (flags & ENTITY_STORE_FLAG_CLIMBING) != 0u ? rise > MP_FOLLOW_CLIMB
                                                                         : rise > MP_FOLLOW_STEP;
    if (wants_jump) {
        entity_store_jump(store, index);
    }
    return MOB_PATH_FOLLOW_WALKING;
}

void mob_path_stats_read(const mob_path_t *path, mob_path_stats_t *out) {
    *out = (mob_path_stats_t){
        .requests = path->requests,
        .searches = path->searches,
        .worker_searches = atomic_load_explicit(&path->worker_searches, memory_order_relaxed),
        .exact_hits = path->exact_hits,
        .suffix_hits = path->suffix_hits,
        .shared = path->shared,
        .dropped = path->dropped,
        .emptied = path->emptied,
        .rounds = path->rounds,
        .nodes = path->nodes,
    };
}
