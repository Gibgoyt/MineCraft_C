/* ctest run_playthrough. See README.md. */
#include "harness/index.h"
#include "run/game/fixtures/report.h"

#include "climate/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/spawn/index.h"
#include "job/index.h"
#include "player/movement/index.h"
#include "render/frame/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    PT_PATH_BYTES = 512u,
    PT_DAY_MS = 240000u,
    PT_NIGHT_MS = 800000u,
};
enum : uint64_t {
    PT_FILE_BYTES = 1ull << 20,
};

static char *pt_temp(test_context_t *t) {
    char *const directory = test_alloc(t, PT_PATH_BYTES);
    snprintf(directory, PT_PATH_BYTES, "/tmp/mc_playthrough_XXXXXX");
    return mkdtemp(directory);
}

static void pt_temp_remove(test_context_t *t, char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_playthrough_", 20u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static char *pt_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, PT_PATH_BYTES);
    snprintf(path, PT_PATH_BYTES, "%s/%s", left, right);
    return path;
}

static uint64_t pt_count(const char *text, const char *needle) {
    uint64_t count = 0u;
    for (const char *at = strstr(text, needle); at != nullptr; at = strstr(at + 1, needle)) {
        count++;
    }
    return count;
}

/* ---- The tree the day starts at ----------------------------------------------------------------------------------------- */

/* The day's opening needs 9 logs from one tree: 4 for the table, 3 for the bench and 2 for planks (plan 005 phase 6). Seed 36's
 * spawn had a mangrove of 10 in reach; since plan 006 phase 03 it is an acacia of 4, and the land will move again (plan 007).
 * So, as the dive finds its sea (tests/cuda/under_water/), the test finds the tree from the terrain itself, on the climate as a
 * menu world opens it, and the scripts go there by /tp, facing its root. Each script has a line "@tree" where that goes. */
enum : uint32_t {
    PT_SEED = 36u,          /* what the scripts type for a new world's seed */
    PT_TREE_LOGS_MIN = 12u, /* 9 used, and 3 more that may land out of reach */
    PT_TILE = 48u,          /* blocks across a tile of the search, a whole number of chunks */
    PT_MARGIN = 16u,        /* blocks made round a tile, so its trees and the ground round them are whole */
    /* Squares of tiles round the start's: out to about 1150 blocks. Plan 007 phase 03's land has no fit tree within 500 of seed
     * 36's start (dense jungle on slopes, spread or crowded), and the first square with one is the 16th, a mangrove of 16 logs
     * 883 blocks off; the search stops at the first square that has one. */
    PT_RINGS = 24u,
    PT_BOX_BELOW = 24u, /* the box's blocks under the spawn's ground */
    PT_BOX_Z = 96u,     /* and its height */
    PT_TREE_LOGS_MOST = 256u,
    PT_TREE_LOGS_MANY =
        64u, /* at most: one stack, in hotbar slot 2, with what the leaves give in slot 3; the scripts use 4 to 6 */
    PT_SPREAD = 3u, /* blocks across from the root a log may be */
    PT_CLEAR = 3u,  /* the ground round the trunk, flat and clear, where the table and the bench go */
    PT_ALONE = 3u,  /* no other tree's log this near the trunk */
    PT_SCRIPT_BYTES = 1u << 16,
    PT_NEAR = 64u,       /* blocks from the start whose columns are drawn by the time the script gets there */
    PT_FAR_TICKS = 500u, /* what a tree further than that waits after the /tp */
};
#define PT_START_YAW 0.6f64      /* GAME_START_YAW: a new world's first look, which a /tp keeps */
#define PT_LEVEL_PITCH (-0.3f64) /* GAME_START_PITCH, where the script's look is back to before the @tree line */
#define PT_PI 3.14159265358979323846f64
/* Where the feet stand in the trunk's block once the logs are in, as fractions of it along the way they face and to their right.
 * The script's pitch meets the ground 1.57 blocks out. The bench goes 86 degrees to the right, where 0.7 + 1.57 puts it two
 * blocks on; the door's look, 43 degrees past that, meets the ground at 0.7 + 1.15, the block before the bench's right half,
 * not the bench; and the look back at the bench's west half, 7 degrees left of the bench's way, crosses its front 0.16 left of
 * the eye, which from the middle of the block (0.5 ahead, so half a block to the bench's left) is still on that half. */
#define PT_FEET_AHEAD 0.5f64
#define PT_FEET_RIGHT 0.7f64
#define PT_WALK_PER_TICK 0.2f64 /* blocks a walk covers in a tick, a little under its speed */

typedef struct pt_tree_t {
    int32_t root_x, root_y, ground_z; /* 0: the root log, and the ground block under it */
    int32_t stand_x, stand_y;         /* 12: where the feet go, on the same ground */
    uint32_t logs;                    /* 20 */
    uint32_t far;                     /* 24: blocks from the start */
    uint16_t log;                     /* 28: its block */
    uint8_t _pad0[2];                 /* 30 */
} pt_tree_t;
static_assert(sizeof(pt_tree_t) == 32);

/* The search's blocks: every chunk round the spawn as a column job makes it, on the box's z only. */
typedef struct pt_box_t {
    uint16_t *blocks;   /* 0: x fastest, then y, then z */
    uint32_t *tree;     /* 8: which tree a log is in, 0 for none yet */
    int32_t x0, y0, z0; /* 16 */
    uint32_t nx, ny;    /* 28 */
    uint8_t _pad0[4];   /* 36 */
} pt_box_t;
static_assert(sizeof(pt_box_t) == 40);

static uint64_t pt_at(const pt_box_t *box, int32_t x, int32_t y, int32_t z) {
    return ((uint64_t)(z - box->z0) * box->ny + (uint64_t)(y - box->y0)) * box->nx + (uint64_t)(x - box->x0);
}

static bool pt_inside(const pt_box_t *box, int32_t x, int32_t y, int32_t z) {
    return x >= box->x0 && y >= box->y0 && z >= box->z0 && x < box->x0 + (int32_t)box->nx &&
           y < box->y0 + (int32_t)box->ny && z < box->z0 + (int32_t)PT_BOX_Z;
}

static uint16_t pt_block(const pt_box_t *box, int32_t x, int32_t y, int32_t z) {
    return pt_inside(box, x, y, z) ? box->blocks[pt_at(box, x, y, z)] : BLOCK_ID_AIR;
}

static bool pt_flag(uint16_t block, uint32_t flag) {
    return (block_defs[block].flags & flag) != 0u;
}

static void *pt_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* Floods one tree from a log: the same wood, touching at a face, edge or corner, as the feller walks it. Returns how many logs,
 * with the lowest in root (the one with the least x, then y, at the least z) and whether any touches leaves. */
static uint32_t pt_flood(int32_t (*stack)[3], pt_box_t *box, int32_t x, int32_t y, int32_t z, uint32_t id,
                         int32_t root[3], bool *out_leaves, bool *out_cut_off) {
    const uint16_t wood = pt_block(box, x, y, z);
    uint32_t top = 0u, count = 0u;
    box->tree[pt_at(box, x, y, z)] = id;
    stack[top][0] = x, stack[top][1] = y, stack[top][2] = z, top++;
    root[0] = x, root[1] = y, root[2] = z;
    *out_leaves = false;
    *out_cut_off = false;
    while (top > 0u) {
        top--;
        const int32_t cx = stack[top][0], cy = stack[top][1], cz = stack[top][2];
        count++;
        if (cz < root[2] || (cz == root[2] && (cx < root[0] || (cx == root[0] && cy < root[1])))) {
            root[0] = cx, root[1] = cy, root[2] = cz;
        }
        for (int32_t dz = -1; dz <= 1; dz++) {
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    const int32_t nx = cx + dx, ny = cy + dy, nz = cz + dz;
                    if (!pt_inside(box, nx, ny, nz)) {
                        *out_cut_off = true;
                        continue;
                    }
                    const uint16_t there = box->blocks[pt_at(box, nx, ny, nz)];
                    const int32_t faces = (dx != 0) + (dy != 0) + (dz != 0);
                    if (faces == 1 && pt_flag(there, BLOCK_FLAG_DECAYS)) {
                        *out_leaves = true;
                    }
                    if (there != wood || box->tree[pt_at(box, nx, ny, nz)] != 0u) {
                        continue;
                    }
                    box->tree[pt_at(box, nx, ny, nz)] = id;
                    if (top >= PT_TREE_LOGS_MOST * 4u - 1u) {
                        *out_cut_off = true; /* too big for this search: not one to stand next to */
                        continue;
                    }
                    stack[top][0] = nx, stack[top][1] = ny, stack[top][2] = nz, top++;
                }
            }
        }
    }
    return count;
}

/* Every log of tree id within PT_SPREAD across of the root's column, so that most fall where a walk in takes them. */
static bool pt_compact(const pt_box_t *box, uint32_t id, const int32_t root[3]) {
    for (int32_t z = box->z0; z < box->z0 + (int32_t)PT_BOX_Z; z++) {
        for (int32_t y = box->y0; y < box->y0 + (int32_t)box->ny; y++) {
            for (int32_t x = box->x0; x < box->x0 + (int32_t)box->nx; x++) {
                if (box->tree[pt_at(box, x, y, z)] == id &&
                    (abs(x - root[0]) > (int32_t)PT_SPREAD || abs(y - root[1]) > (int32_t)PT_SPREAD)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* The ground round the trunk: no other tree's log near, and within PT_CLEAR every column's top at the root's ground, firm, with
 * room for a body over it once this tree is gone. */
static bool pt_clear_round(const pt_box_t *box, uint32_t id, const int32_t root[3]) {
    const int32_t ground = root[2] - 1;
    for (int32_t dy = -(int32_t)PT_ALONE; dy <= (int32_t)PT_ALONE; dy++) {
        for (int32_t dx = -(int32_t)PT_ALONE; dx <= (int32_t)PT_ALONE; dx++) {
            const int32_t x = root[0] + dx, y = root[1] + dy;
            for (int32_t z = box->z0; z < box->z0 + (int32_t)PT_BOX_Z; z++) {
                if (!pt_inside(box, x, y, z)) {
                    return false;
                }
                const uint64_t i = pt_at(box, x, y, z);
                if (pt_flag(box->blocks[i], BLOCK_FLAG_LOG) && box->tree[i] != id) {
                    return false;
                }
            }
            if (abs(dx) > (int32_t)PT_CLEAR || abs(dy) > (int32_t)PT_CLEAR) {
                continue;
            }
            const uint16_t under = pt_block(box, x, y, ground);
            if (!pt_flag(under, BLOCK_FLAG_SOLID) || pt_flag(under, BLOCK_FLAG_DECAYS) ||
                pt_flag(under, BLOCK_FLAG_LOG)) {
                return false;
            }
            for (int32_t z = ground + 1; z <= ground + 2; z++) {
                const uint16_t there = pt_block(box, x, y, z);
                const bool goes = pt_flag(there, BLOCK_FLAG_DECAYS) || box->tree[pt_at(box, x, y, z)] == id;
                if ((pt_flag(there, BLOCK_FLAG_SOLID) && !goes) || there == BLOCK_ID_WATER || there == BLOCK_ID_LAVA) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* Whether the eye at a stand sees the root's middle with nothing solid before it. */
static bool pt_sees(const pt_box_t *box, const int32_t root[3], int32_t stand_x, int32_t stand_y) {
    const float64_t from[3] = {(float64_t)stand_x + 0.5f64, (float64_t)stand_y + 0.5f64,
                               (float64_t)root[2] + PLAYER_MOVEMENT_EYE_STANDING};
    const float64_t to[3] = {(float64_t)root[0] + 0.5f64, (float64_t)root[1] + 0.5f64, (float64_t)root[2] + 0.5f64};
    for (uint32_t step = 0u; step <= 200u; step++) {
        const float64_t f = (float64_t)step / 200.0f64;
        const int32_t x = (int32_t)floor(from[0] + (to[0] - from[0]) * f);
        const int32_t y = (int32_t)floor(from[1] + (to[1] - from[1]) * f);
        const int32_t z = (int32_t)floor(from[2] + (to[2] - from[2]) * f);
        if (x == root[0] && y == root[1] && z == root[2]) {
            return true;
        }
        if (pt_flag(pt_block(box, x, y, z), BLOCK_FLAG_SOLID)) {
            return false;
        }
    }
    return false;
}

/* The stand 2 to 3 blocks from the trunk that needs the least turn from a new world's look and sees the root. */
static bool pt_stand(const pt_box_t *box, const int32_t root[3], int32_t *out_x, int32_t *out_y) {
    float64_t best = 10.0f64;
    for (int32_t dy = -3; dy <= 3; dy++) {
        for (int32_t dx = -3; dx <= 3; dx++) {
            const float64_t across = sqrt((float64_t)(dx * dx + dy * dy));
            if (across < 2.0f64 || across > 3.0f64 || !pt_sees(box, root, root[0] + dx, root[1] + dy)) {
                continue;
            }
            float64_t turn = atan2((float64_t)-dy, (float64_t)-dx) - PT_START_YAW;
            turn = fabs(remainder(turn, 2.0f64 * PT_PI));
            if (turn < best) {
                best = turn;
                *out_x = root[0] + dx;
                *out_y = root[1] + dy;
            }
        }
    }
    return best < 10.0f64;
}

/* Makes the box's blocks, chunk by chunk as a column job makes them: its climate sample from the chunk's corner; only the z differs,
 * and a voxel is the same in any box. */
static void pt_box_make(pt_box_t *box, const terrain_t *terrain, const climate_t *climate, game_column_job_t *one) {
    memset(box->tree, 0, (uint64_t)box->nx * box->ny * PT_BOX_Z * sizeof(uint32_t));
    for (int32_t cy = box->y0; cy < box->y0 + (int32_t)box->ny; cy += (int32_t)COLUMN_SIZE) {
        for (int32_t cx = box->x0; cx < box->x0 + (int32_t)box->nx; cx += (int32_t)COLUMN_SIZE) {
            const terrain_density_box_t chunk = {.origin_x = cx,
                                                 .origin_y = cy,
                                                 .origin_z = box->z0,
                                                 .count_x = COLUMN_SIZE,
                                                 .count_y = COLUMN_SIZE,
                                                 .count_z = PT_BOX_Z};
            terrain_climate_sample_t sample;
            (void)climate_terrain_sample(climate, terrain->seed, cx, cy, &sample);
            terrain_generate_box(terrain, &sample, &chunk, one->borrowed_opened, one->borrowed_states);
            for (uint32_t z = 0u; z < PT_BOX_Z; z++) {
                for (uint32_t y = 0u; y < COLUMN_SIZE; y++) {
                    memcpy(&box->blocks[pt_at(box, cx, cy + (int32_t)y, box->z0 + (int32_t)z)],
                           &one->borrowed_states[((uint64_t)z * COLUMN_SIZE + y) * COLUMN_SIZE],
                           COLUMN_SIZE * sizeof(uint16_t));
                }
            }
        }
    }
}

/* The fittest tree whose root is in the box's middle (inside its margin): PT_TREE_LOGS_MIN to PT_TREE_LOGS_MANY logs close round its trunk, alone
 * on clear flat ground, with a stand that sees its root; the nearest the start of those. False if there is none. */
static bool pt_box_tree(pt_box_t *box, int32_t (*stack)[3], int32_t start_x, int32_t start_y, pt_tree_t *out,
                        float64_t *out_far) {
    float64_t best = 1e30f64;
    uint32_t id = 0u;
    for (int32_t z = box->z0; z < box->z0 + (int32_t)PT_BOX_Z; z++) {
        for (int32_t y = box->y0; y < box->y0 + (int32_t)box->ny; y++) {
            for (int32_t x = box->x0; x < box->x0 + (int32_t)box->nx; x++) {
                const uint64_t i = pt_at(box, x, y, z);
                if (!pt_flag(box->blocks[i], BLOCK_FLAG_LOG) || box->tree[i] != 0u) {
                    continue;
                }
                id++;
                int32_t root[3];
                bool leaves = false, cut_off = false;
                const uint32_t logs = pt_flood(stack, box, x, y, z, id, root, &leaves, &cut_off);
                const float64_t far = hypot((float64_t)(root[0] - start_x), (float64_t)(root[1] - start_y));
                const bool middle = root[0] >= box->x0 + (int32_t)PT_MARGIN &&
                                    root[1] >= box->y0 + (int32_t)PT_MARGIN &&
                                    root[0] < box->x0 + (int32_t)(box->nx - PT_MARGIN) &&
                                    root[1] < box->y0 + (int32_t)(box->ny - PT_MARGIN);
                if (!middle || !leaves || cut_off || logs < PT_TREE_LOGS_MIN || logs > PT_TREE_LOGS_MANY ||
                    far >= best || pt_flag(pt_block(box, root[0], root[1], root[2] - 1), BLOCK_FLAG_LOG) ||
                    !pt_compact(box, id, root) || !pt_clear_round(box, id, root)) {
                    continue;
                }
                int32_t stand_x = 0, stand_y = 0;
                if (!pt_stand(box, root, &stand_x, &stand_y)) {
                    continue;
                }
                best = far;
                *out = (pt_tree_t){.root_x = root[0],
                                   .root_y = root[1],
                                   .ground_z = root[2] - 1,
                                   .stand_x = stand_x,
                                   .stand_y = stand_y,
                                   .logs = logs,
                                   .log = box->blocks[i]};
            }
        }
    }
    *out_far = best;
    return best < 1e30f64;
}

/* The tree near seed 36's start: tiles of PT_TILE blocks in squares round the start's, nearest square first, each made with a
 * margin round it, and the nearest fit tree of the first square that has one. */
static bool pt_tree_find(test_context_t *t, pt_tree_t *out) {
    memset(out, 0, sizeof(*out));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    job_system_t *const job = pt_aligned(t, sizeof(job_system_t));
    climate_t *const climate = pt_aligned(t, sizeof(climate_t));
    terrain_t *const terrain = pt_aligned(t, sizeof(terrain_t));
    if (!CHECK(thread_topology_read(topology)) || !CHECK(job_init(job, topology))) {
        return false;
    }
    if (!CHECK(climate_init(climate)) || !CHECK(terrain_init(terrain, PT_SEED)) ||
        !CHECK(climate_open(climate, terrain))) {
        return false;
    }
    /* As game_open has it for a new world: the climate round the middle, then the search for land. */
    climate_prepare_area(climate, job, GAME_WORLD_MIDDLE - GAME_SPAWN_CLIMATE_BLOCKS,
                         GAME_WORLD_MIDDLE - GAME_SPAWN_CLIMATE_BLOCKS, GAME_WORLD_MIDDLE + GAME_SPAWN_CLIMATE_BLOCKS,
                         GAME_WORLD_MIDDLE + GAME_SPAWN_CLIMATE_BLOCKS, nullptr, nullptr);
    const uint32_t column_voxels = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN);
    game_column_job_t *const one = pt_aligned(t, sizeof(game_column_job_t));
    memset(one, 0, sizeof(*one));
    one->borrowed_terrain = terrain;
    one->borrowed_climate = climate;
    one->borrowed_states = test_alloc(t, (uint64_t)COLUMN_SIZE * COLUMN_SIZE * column_voxels * sizeof(uint16_t));
    one->borrowed_opened = test_alloc(t, (uint64_t)COLUMN_SIZE * COLUMN_SIZE * column_voxels);
    int32_t start_x = 0, start_y = 0;
    CHECK(game_spawn_find(nullptr, terrain, one, GAME_WORLD_MIDDLE, GAME_WORLD_MIDDLE, &start_x, &start_y));
    printf("  seed %u starts at %" PRId32 ", %" PRId32 ", ground %" PRId32 "\n", PT_SEED, start_x, start_y,
           game_ground_z(terrain, one, start_x, start_y));

    pt_box_t box = {.nx = PT_TILE + 2u * PT_MARGIN, .ny = PT_TILE + 2u * PT_MARGIN};
    const uint64_t voxels = (uint64_t)box.nx * box.ny * PT_BOX_Z;
    box.blocks = test_alloc(t, voxels * sizeof(uint16_t));
    box.tree = test_alloc(t, voxels * sizeof(uint32_t));
    int32_t (*const stack)[3] = test_alloc(t, (uint64_t)PT_TREE_LOGS_MOST * 4u * sizeof(int32_t[3]));
    /* The start's tile is its chunk and the chunk on each side, so that every box starts on a chunk's corner, as a column job's. */
    static_assert(PT_TILE % COLUMN_SIZE == 0u && PT_MARGIN % COLUMN_SIZE == 0u);
    const int32_t tile_x = (column_chunk_of(start_x) - 1) * (int32_t)COLUMN_SIZE;
    const int32_t tile_y = (column_chunk_of(start_y) - 1) * (int32_t)COLUMN_SIZE;
    float64_t best = 1e30f64;
    uint32_t tiles = 0u;
    for (int32_t ring = 0; ring <= (int32_t)PT_RINGS && best >= 1e30f64; ring++) {
        for (int32_t dy = -ring; dy <= ring; dy++) {
            for (int32_t dx = -ring; dx <= ring; dx++) {
                if (dx != -ring && dx != ring && dy != -ring && dy != ring) {
                    continue;
                }
                box.x0 = tile_x + dx * (int32_t)PT_TILE - (int32_t)PT_MARGIN;
                box.y0 = tile_y + dy * (int32_t)PT_TILE - (int32_t)PT_MARGIN;
                const float32_t ground =
                    terrain_height_estimate(terrain, box.x0 + (int32_t)box.nx / 2, box.y0 + (int32_t)box.ny / 2);
                box.z0 = (int32_t)floorf(ground) - (int32_t)PT_BOX_BELOW;
                pt_box_make(&box, terrain, climate, one);
                tiles++;
                pt_tree_t tree;
                float64_t far = 0.0f64;
                if (pt_box_tree(&box, stack, start_x, start_y, &tree, &far) && far < best) {
                    best = far;
                    *out = tree;
                }
            }
        }
    }
    climate_close(climate, job);
    climate_deinit(climate);
    terrain_deinit(terrain);
    job_deinit(job);
    printf("  the day's tree, from %u tiles of %u blocks: %s, %u logs, root at %" PRId32 ", %" PRId32 ", %" PRId32
           ", %.0f blocks from the start, stood at %" PRId32 ", %" PRId32 "\n",
           tiles, PT_TILE, block_defs[out->log].name, out->logs, out->root_x, out->root_y, out->ground_z + 1,
           (libc_double_t)best, out->stand_x, out->stand_y);
    out->far = (uint32_t)lround(best < 1e30f64 ? best : 0.0f64);
    return CHECK(out->logs >= PT_TREE_LOGS_MIN);
}

static void pt_append(char *text, uint64_t *length, const char *line) {
    const int32_t wrote = snprintf(text + *length, PT_SCRIPT_BYTES - *length, "%s\n", line);
    if (wrote > 0) {
        *length += (uint64_t)wrote;
    }
}

/* Each character as the key a US keyboard types it with, as tests/run/game/command/ types its commands. */
static void pt_type(char *text, uint64_t *length, const char *typed) {
    for (const char *c = typed; *c != '\0'; c++) {
        char line[64];
        const char *const name =
            *c == ' ' ? "space" : (*c == '/' ? "slash" : (*c == '-' ? "minus" : (*c == '.' ? "dot" : nullptr)));
        if (name != nullptr) {
            snprintf(line, sizeof(line), "+1 key %s down\n+0 key %s up", name, name);
        } else {
            snprintf(line, sizeof(line), "+1 key %c down\n+0 key %c up", *c, *c);
        }
        pt_append(text, length, line);
    }
}

/* What goes where the script says "@tree": the /tp to the stand, the turn to the root, one cut that brings the whole tree down
 * (plan 005 phase 1), a walk in over the logs, the look back to level, and a turn back to the stand, where the ground is clear.
 * Where it says "@fell" (the felling script), the /tp and the turn, a look up at the tree and back, and the one cut. */
static void pt_tree_lines(const pt_tree_t *tree, bool fell, char *text, uint64_t *length) {
    char line[160];
    snprintf(line, sizeof(line),
             "# To the tree the test found: %s, %u logs, its root at %" PRId32 ", %" PRId32 ", %" PRId32 ".",
             block_defs[tree->log].name, tree->logs, tree->root_x, tree->root_y, tree->ground_z + 1);
    pt_append(text, length, line);
    snprintf(line, sizeof(line), "/tp %" PRId32 " %" PRId32 " %" PRId32, tree->stand_x, tree->stand_y,
             tree->ground_z + 1);
    pt_type(text, length, line);
    pt_append(text, length, "+1 key enter down\n+0 key enter up");
    const float64_t across = (float64_t)(tree->root_x - tree->stand_x),
                    along = (float64_t)(tree->root_y - tree->stand_y);
    const float64_t distance = hypot(across, along);
    const float64_t yaw = atan2(along, across);
    const float64_t pitch = atan2(0.5f64 - PLAYER_MOVEMENT_EYE_STANDING, distance);
    const int64_t turn =
        (int64_t)lround(-remainder(yaw - PT_START_YAW, 2.0f64 * PT_PI) / (float64_t)RENDER_CAMERA_RADIANS_PER_COUNT);
    const int64_t down = (int64_t)lround((PT_LEVEL_PITCH - pitch) / (float64_t)RENDER_CAMERA_RADIANS_PER_COUNT);
    /* A tree further than the columns drawn round the start waits for its own, as the dive does (plan 006 phase 01: 15 s). */
    snprintf(line, sizeof(line), "+%u move %" PRId64 " %" PRId64, tree->far > PT_NEAR ? PT_FAR_TICKS : 20u, turn, down);
    pt_append(text, length, line);
    pt_append(text, length, "+6 screenshot at_tree.png");
    if (fell) {
        /* Looking up from where it stands, to see the tree before, and back down to the root; then the left button held with
         * nothing in hand for the 60 ticks a hand takes on a log, with some to spare. */
        pt_append(text, length, "+8 move 0 -220\n+6 screenshot before.png\n+1 move 0 220\n+6 screenshot aimed.png");
        pt_append(text, length, "+1 button left down\n+75 button left up\n+1 screenshot cut.png");
        return;
    }
    pt_append(text, length, "+1 button left down\n+70 button left up\n+1 screenshot log1.png");
    /* The logs fall, and the leaves go over about a second and a half (run/src/player/felling/). */
    const uint32_t walk = (uint32_t)ceil(distance / PT_WALK_PER_TICK);
    snprintf(line, sizeof(line), "+40 move 0 %" PRId64 "\n+1 key w down\n+%u key w up", -down, walk);
    pt_append(text, length, line);
    pt_append(text, length, "+20 screenshot walked_in.png");
    /* Then to one place on the trunk's block, facing along the axis nearest the way back to the stand: what follows was worked out
     * from screenshots for one place and one look, and lands the table, the bench and what goes beside them by where the eye is
     * in its block (PT_FEET_AHEAD, PT_FEET_RIGHT). */
    const int32_t back_x = tree->stand_x - tree->root_x, back_y = tree->stand_y - tree->root_y;
    const bool along_x = abs(back_x) >= abs(back_y);
    const float64_t ahead[2] = {along_x ? (back_x > 0 ? 1.0f64 : -1.0f64) : 0.0f64,
                                along_x ? 0.0f64 : (back_y > 0 ? 1.0f64 : -1.0f64)};
    const float64_t right[2] = {ahead[1], -ahead[0]};
    const float64_t on = PT_FEET_AHEAD - 0.5f64, aside = PT_FEET_RIGHT - 0.5f64;
    const float64_t feet_x = (float64_t)tree->root_x + 0.5f64 + ahead[0] * on + right[0] * aside;
    const float64_t feet_y = (float64_t)tree->root_y + 0.5f64 + ahead[1] * on + right[1] * aside;
    snprintf(line, sizeof(line), "/tp %.2f %.2f %" PRId32, (libc_double_t)feet_x, (libc_double_t)feet_y,
             tree->ground_z + 1);
    pt_type(text, length, line);
    pt_append(text, length, "+1 key enter down\n+0 key enter up");
    const float64_t facing = atan2(ahead[1], ahead[0]);
    const int64_t back =
        (int64_t)lround(remainder(yaw - facing, 2.0f64 * PT_PI) / (float64_t)RENDER_CAMERA_RADIANS_PER_COUNT);
    snprintf(line, sizeof(line), "+20 move %" PRId64 " 0", back);
    pt_append(text, length, line);
}

/* The script at fixture, with its "@tree" line made for this tree, written into root. Returns its path. */
static const char *pt_script(test_context_t *t, const char *fixture, const char *root, const pt_tree_t *tree) {
    char *const text = test_alloc(t, PT_SCRIPT_BYTES);
    char *const out = test_alloc(t, PT_SCRIPT_BYTES);
    uint64_t bytes = 0u, length = 0u;
    if (!CHECK(file_read_whole(fixture, (uint8_t *)text, PT_SCRIPT_BYTES - 1u, &bytes) == FILE_OK)) {
        return nullptr;
    }
    text[bytes] = '\0';
    char *at = strstr(text, "\n@tree\n");
    const bool fell = at == nullptr;
    at = fell ? strstr(text, "\n@fell\n") : at;
    if (!CHECK(at != nullptr)) {
        return nullptr;
    }
    at[1] = '\0';
    pt_append(out, &length, text);
    length--; /* the line break pt_append added: text ends with its own */
    pt_tree_lines(tree, fell, out, &length);
    pt_append(out, &length, at + strlen("\n@tree\n"));
    const char *const path = pt_join(t, root, "day.script");
    return CHECK(file_write_atomic(path, out, length) == FILE_OK) ? path : nullptr;
}

/* The tree, found once for every test here, and the script at fixture made for it in root. */
static const char *pt_day(test_context_t *t, const char *fixture, const char *root, pt_tree_t *out) {
    static pt_tree_t found;
    static bool searched = false;
    if (!searched) {
        searched = true;
        (void)pt_tree_find(t, &found);
    }
    *out = found;
    if (!CHECK(found.logs >= PT_TREE_LOGS_MIN)) {
        return nullptr;
    }
    return pt_script(t, fixture, root, &found);
}

/* The logs the one cut brought down past itself, from the report's line. */
static uint64_t pt_felled(const char *output) {
    const char *const felled = strstr(output, "the tree feller felled ");
    return felled == nullptr ? 0u : strtoull(felled + strlen("the tree feller felled "), nullptr, 10);
}

/* The nth (from 0) "the inventory holds X items in Y stacks" in output, or false if there are not that many. */
static bool pt_held(const char *output, uint32_t nth, uint64_t *out_items, uint64_t *out_stacks) {
    const char *at = output;
    for (uint32_t i = 0u; i <= nth; i++) {
        at = strstr(at, "the inventory holds ");
        if (at == nullptr) {
            return false;
        }
        at += strlen("the inventory holds ");
    }
    uint32_t stacks = 0u;
    if (sscanf(at, "%" SCNu64 " items in %" SCNu32 " stacks", out_items, &stacks) != 2) {
        return false;
    }
    *out_stacks = stacks;
    return true;
}

/* The tree's wood, its log's name without "_log", as its planks and stairs are named. */
static void pt_wood(const pt_tree_t *tree, char *out, uint32_t bytes) {
    const char *const name = block_defs[tree->log].name;
    const char *const log = strstr(name, "_log");
    snprintf(out, bytes, "%.*s", log == nullptr ? (int32_t)strlen(name) : (int32_t)(log - name), name);
}

/* titles is how many times the script comes back to the title screen, the last on the way to Quit Game. */
static void pt_run(test_context_t *t, const char *script, const char *root, uint32_t timeout_ms, uint64_t titles,
                   test_process_t *process) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          pt_join(t, root, "saves"),
                          (char *)"--settings",
                          pt_join(t, root, "settings.yaml"),
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          pt_join(t, root, "shots"),
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"8",
                          nullptr};
    test_process_run(t, argv, timeout_ms, process);
    REQUIRE(process->started != 0u);
    CHECK(process->timed_out == 0u);
    if (!CHECK_EQ_I64(process->exit_code, 0)) {
        printf("%s\n", process->output);
    }
    CHECK_CONTAINS(process->output, " 0 bytes live");
    /* Saved on the way to the title, opened from what was saved, and out by Quit Game. */
    CHECK_EQ_U64(pt_count(process->output, "session: back on the title screen"), titles);
    CHECK_EQ_U64(pt_count(process->output, "session: quit"), 1u);
    CHECK_CONTAINS(process->output, "game: the player goes on from");
    CHECK_CONTAINS(process->output, " was read");
    CHECK_EQ_U64(pt_count(process->output, " failed;"), pt_count(process->output, " 0 failed;"));
}

TEST(a_first_day_mines_dirt_picks_it_up_crafts_changes_the_distance_and_comes_back) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_tree_t tree;
    const char *const script = pt_day(t, "tests/run/playthrough/fixtures/day.script", root, &tree);
    REQUIRE(script != nullptr);
    pt_run(t, script, root, PT_DAY_MS, 2u, &process);
    const char *const output = process.output;

    /* The trunk of the tree the test found, on seed 36: the one line a person would look for. */
    char line[256];
    snprintf(line, sizeof(line), "the last mined was %s", block_defs[tree.log].name);
    CHECK_EQ_U64(pt_count(output, line), 2u);
    /* The first log cut brings its whole tree down by hand (plan 005 phase 1), at least the 9 the day needs. */
    const uint64_t felled = pt_felled(output);
    CHECK(felled + 1u >= 9u);
    /* The mud and the tree's logs; a table and a bench, the bench two blocks for one placing. */
    snprintf(line, sizeof(line), "the player mined %" PRIu64 " blocks and placed 2", 2u + felled);
    CHECK_EQ_U64(pt_count(output, line), 2u);
    /* Mud, the pickaxe, three planks and two sticks, once on the way out and the same again from the save: the 9 logs went to the
     * table's 4, the bench's 3 and 2 for planks (plan 005 phase 6). Whatever else came down and was picked up (logs past 9, a
     * sapling or a stick the leaves gave) stays, so these are the least. */
    uint64_t items = 0u, stacks = 0u, items_again = 0u, stacks_again = 0u;
    REQUIRE(pt_held(output, 0u, &items, &stacks));
    REQUIRE(pt_held(output, 1u, &items_again, &stacks_again));
    printf("  the inventory holds %" PRIu64 " items in %" PRIu64 " stacks\n", items, stacks);
    CHECK(items >= 7u && stacks >= 4u);
    CHECK_EQ_U64(items_again, items);
    CHECK_EQ_U64(stacks_again, stacks);
    CHECK_EQ_U64(pt_count(output, "hotbar slot 4 is in hand, with 1 of wood_pickaxe"), 2u);
    CHECK_CONTAINS(output, "render distance 2");
    CHECK_CONTAINS(output, "render distance 8");

    uint8_t *const settings = test_alloc(t, PT_FILE_BYTES);
    uint64_t bytes = 0u;
    REQUIRE(file_read_whole(pt_join(t, root, "settings.yaml"), settings, PT_FILE_BYTES - 1u, &bytes) == FILE_OK);
    settings[bytes] = 0u;
    CHECK_CONTAINS((const char *)settings, "render_distance: 8");

    /* What a person sees of it is the picture, so each is at least there. A person reads them with MC_PLAYTHROUGH_KEEP. */
    static const char *const shots[] = {
        "spawn.png",      "picked_up.png",    "logs.png",          "crafted.png", "table.png",   "table_no_planks.png",
        "bench_made.png", "bench_placed.png", "bench_open.png",    "planks.png",  "pickaxe.png", "settings.png",
        "at_2.png",       "at_8.png",         "back_on_title.png", "reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

/* Ahmed's words: "creating stairs with wood on a crafting table is not possible", and then "all wood working is done on this bench". Seed 36's
 * tree was a mangrove, whose planks had no stairs; it is whichever tree the test finds now, and its stairs are of its wood. Since plan 005 phase 6 the table makes no planks at all: it makes the bench, whose planks make
 * four stairs on it. */
TEST(the_trees_planks_make_stairs_on_a_bench_that_is_placed_broken_and_all_of_it_saved) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_tree_t tree;
    const char *const script = pt_day(t, "tests/run/playthrough/fixtures/stairs.script", root, &tree);
    REQUIRE(script != nullptr);
    pt_run(t, script, root, PT_DAY_MS, 2u, &process);
    const char *const output = process.output;

    /* Four made and in slot 4, two put down and one back: three in hand, once on the way out and once again from the save. */
    char wood[64], line[256];
    pt_wood(&tree, wood, sizeof(wood));
    snprintf(line, sizeof(line), "hotbar slot 4 is in hand, with 3 of %s_stairs", wood);
    CHECK_EQ_U64(pt_count(output, line), 2u);
    /* The mud and the whole tree the first cut brings down by hand, then one stair and one half of the bench; the table, the bench and
     * two stairs placed. */
    snprintf(line, sizeof(line), "the player mined %" PRIu64 " blocks and placed 4", 4u + pt_felled(output));
    CHECK_EQ_U64(pt_count(output, line), 2u);
    /* Mining one half is one block in the books, and the half is the one aimed at: since plan 005 phase 12 the bench runs to the
     * right of whoever placed it, and the turn back to it meets its west half. */
    CHECK_EQ_U64(pt_count(output, "the last mined was woodworking_bench_west"), 2u);
    /* Mud, two planks, three stairs, and the one bench that came down and was picked up: the 9 logs went to the table's 4, the bench's 3
     * and 2 for planks. What else was picked up stays, so these are the least, and the save gives back the same. */
    uint64_t items = 0u, stacks = 0u, items_again = 0u, stacks_again = 0u;
    REQUIRE(pt_held(output, 0u, &items, &stacks));
    REQUIRE(pt_held(output, 1u, &items_again, &stacks_again));
    CHECK(items >= 7u && stacks >= 4u);
    CHECK_EQ_U64(items_again, items);
    CHECK_EQ_U64(stacks_again, stacks);

    static const char *const shots[] = {"table_no_planks.png",    "bench_made.png",     "bench_placed.png",
                                        "bench_open.png",         "stairs_ready.png",   "stairs_in_slot.png",
                                        "stairs_placed.png",      "stairs_mined.png",   "bench_broken.png",
                                        "bench_in_inventory.png", "stairs_reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

/* What the stairs do not reach: a bench closed with planks on its grid and on the pointer, a bench that stands through a save, and its west half. */
TEST(a_bench_gives_its_grid_back_stands_through_a_save_and_goes_by_its_west_half) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_tree_t tree;
    const char *const script = pt_day(t, "tests/run/playthrough/fixtures/bench.script", root, &tree);
    REQUIRE(script != nullptr);
    pt_run(t, script, root, PT_DAY_MS, 3u, &process);
    const char *const output = process.output;

    /* Saved with it standing. Mud and the two logs left of the 9 after the table's 4 and the bench's 3: the two on the grid came back.
     * Mined: the mud and that tree's logs. What else was picked up stays, so the first save holds at least 3 in 2 stacks. */
    const uint64_t felled = pt_felled(output);
    char line[256];
    snprintf(line, sizeof(line), "the player mined %" PRIu64 " blocks and placed 2", 2u + felled);
    CHECK_EQ_U64(pt_count(output, line), 1u);
    uint64_t items = 0u, stacks = 0u;
    REQUIRE(pt_held(output, 0u, &items, &stacks));
    CHECK(items >= 3u && stacks >= 2u);
    /* From that save its west half is still there to be mined, once in the books, and the one bench it gives is picked up: one stack
     * more, and at least one item (more of the logs the tree left lying may be picked up on the way). So it is after the second. */
    snprintf(line, sizeof(line), "the player mined %" PRIu64 " blocks and placed 2", 3u + felled);
    CHECK_EQ_U64(pt_count(output, line), 2u);
    CHECK_EQ_U64(pt_count(output, "the last mined was woodworking_bench_west"), 2u);
    uint64_t items_now = 0u, stacks_now = 0u, items_again = 0u, stacks_again = 0u;
    REQUIRE(pt_held(output, 1u, &items_now, &stacks_now));
    REQUIRE(pt_held(output, 2u, &items_again, &stacks_again));
    CHECK(items_now >= items + 1u);
    /* At least the bench's stack more: on plan 007 phase 04's land something of another kind the tree left lying (a sapling
     * or a stick) is picked up on the way too, and makes a stack of its own. */
    CHECK(stacks_now >= stacks + 1u);
    CHECK_EQ_U64(items_again, items_now);
    CHECK_EQ_U64(stacks_again, stacks_now);

    static const char *const shots[] = {"bench_placed.png", "grid_two.png", "grid_back.png",   "reloaded.png",
                                        "reopened.png",     "west_aim.png", "west_broken.png", "gone_reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

/* Ahmed's words: "doors can not be placed down for some reason??? let's please fix this!" (plan 005 phase 2). Three doors from six planks on
 * the bench, one placed, opened, shut, opened and shut again, and mined by its top half: both halves go, and one door drops. */
TEST(a_wooden_door_is_made_placed_opened_shut_and_mined_by_its_top_half) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_tree_t tree;
    const char *const script = pt_day(t, "tests/run/player/interaction/door/fixtures/door.script", root, &tree);
    REQUIRE(script != nullptr);
    pt_run(t, script, root, PT_DAY_MS, 2u, &process);
    const char *const output = process.output;
    CHECK_EQ_U64(pt_count(output, "the last mined was wood_door"), 2u);
    /* Three made, one placed: two in hand, and since plan 005 phase 6 the one that came down is walked over and picked up: three. */
    CHECK_EQ_U64(pt_count(output, "hotbar slot 4 is in hand, with 3 of wood_door"), 2u);

    static const char *const shots[] = {"doors_ready.png", "doors_in_slot.png",  "door_placed.png",
                                        "door_open.png",   "door_shut.png",      "door_open_again.png",
                                        "door_broken.png", "door_on_ground.png", "door_reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

/* Ahmed's words: "tree feller is not a mod in my minecraft it is a built in setting please make it always on!!". So the cut is with bare hands,
 * the hand holding only the mud from the hole, and the tree comes down all the same. */
TEST(bare_hands_bring_down_a_real_tree_with_one_cut_and_it_stays_down_through_a_save) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    /* The tree is the one the day's scripts go to, found from the land (plan 007 phase 03 moved it again), by /tp. */
    pt_tree_t tree;
    const char *const script = pt_day(t, "tests/run/player/felling/fixtures/fell.script", root, &tree);
    REQUIRE(script != nullptr);
    test_process_t process;
    pt_run(t, script, root, PT_DAY_MS, 2u, &process);
    const char *const output = process.output;

    /* What the hole was dug in and which tree it is are the land's (plan 006 phase 03: mud and a mangrove before, coarse dirt
     * and an acacia since), so the report says: the hand holds the one thing the hole gave, and the last cut was a log. */
    report_mined_t mined;
    REQUIRE(report_mined_read(output, &mined));
    printf("  the last mined %s, the hand %" PRIu64 " of %s\n", mined.last, mined.hand_count, mined.hand_item);
    CHECK_EQ_U64(mined.hand_count, 1u);
    CHECK(strstr(mined.last, "_log") != nullptr);
    char line[256];
    snprintf(line, sizeof(line), "hotbar slot 1 is in hand, with 1 of %s", mined.hand_item);
    CHECK_EQ_U64(pt_count(output, line), 2u);
    snprintf(line, sizeof(line), "the last mined was %s, and the hand holds 1 of %s", mined.last, mined.hand_item);
    CHECK_EQ_U64(pt_count(output, line), 2u);
    /* One cut brings down more than its own log: hands that took one log would leave this at 0. A tree is at least the cut and
     * two more. */
    const char *const felled = strstr(output, "the tree feller felled ");
    REQUIRE(felled != nullptr);
    CHECK(strtoull(felled + strlen("the tree feller felled "), nullptr, 10) >= 2u);

    static const char *const shots[] = {"at_tree.png", "before.png", "aimed.png", "cut.png",
                                        "during.png",  "after.png",  "later.png", "reloaded.png"};
    for (uint64_t i = 0u; i < sizeof(shots) / sizeof(shots[0]); i++) {
        uint64_t size = 0u;
        CHECK_EQ_U64(file_path_size(pt_join(t, pt_join(t, root, "shots"), shots[i]), &size), FILE_OK);
        CHECK(size > 1000u);
    }
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

/* Plan 005 phase 3: zombies, skeletons and creepers are turned off, so a whole night in the open brings none, on the way out and again
 * from the save. The swings that once fought them and pressed to come back meet the night air. */
TEST(a_night_in_the_open_meets_no_zombie_skeleton_or_creeper_and_is_saved) {
    REQUIRE(t->arg_count >= 1);
    char *const root = pt_temp(t);
    REQUIRE(root != nullptr);
    test_process_t process;
    pt_run(t, "tests/run/playthrough/fixtures/night.script", root, PT_NIGHT_MS, 2u, &process);
    const char *const output = process.output;

    /* A report on the way to the title and one at Quit Game: each says nothing came, struck, shot or burst. */
    const uint64_t reports = pt_count(output, "mobs came");
    CHECK(reports >= 2u);
    /* Animals may come: which herds a place has is its biome's (plan 006 phase 03 moved them), and none of them is hostile. So
     * the count of mobs is not held; the hostile ones are, each by its own line. */
    CHECK_EQ_U64(pt_count(output, "zombies struck 0 blows, 0 died"), reports);
    CHECK_EQ_U64(pt_count(output, "skeletons loosed 0 arrows, 0 died"), reports);
    CHECK_EQ_U64(pt_count(output, " 0 creepers burst, and 0 died"), reports);
    CHECK_EQ_U64(pt_count(output, " 0 blasts broke 0 blocks"), reports);
    /* Nothing to die of, and nothing taken from the save, as it never held one. */
    CHECK_EQ_U64(pt_count(output, " died 0, came back 0,"), reports);
    CHECK(strstr(output, "out of the saved world") == nullptr);
    if (getenv("MC_PLAYTHROUGH_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        pt_temp_remove(t, root);
    }
}

TEST_MAIN()
