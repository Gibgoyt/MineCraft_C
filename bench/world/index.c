/* bench_world: the standard worlds and camera paths, the whole game on the real card with no display, and a history of what they
 * came to. See README.md. */
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "log/index.h"
#include "main/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "tables/mobs/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The plan's section 2: 120 pictures a second, and a tick at 100 mobs in 5 ms. */
#define BENCH_TARGET_FPS 120.0f64
#define BENCH_TARGET_TICK_MS 5.0f64
/* A real display asks for a picture this often, so the live rows' main thread does. */
#define BENCH_LIVE_FRAME_SECONDS (1.0f64 / 120.0f64)
#define BENCH_TWO_PI 6.283185307179586f64
/* Each frame of a settled flight is this far on: 30 blocks a second, at 60 pictures a second. */
#define BENCH_FLIGHT_SETTLED_STEP 0.5f64
/* A live flight's speed: the fly camera's top speed unless the wheel is turned, RENDER_CAMERA_SPEED_DEFAULT times its fast factor. */
#define BENCH_FLIGHT_LIVE_SPEED 100.0f64
#define BENCH_FLIGHT_HEADING 0.6f32
#define BENCH_FLIGHT_PITCH (-0.12f32)
#define BENCH_ORBIT_PITCH (-0.15f32)
#define BENCH_DAY_MORNING 0.35f64
#define BENCH_DAY_MIDNIGHT 0.0f64
/* No hit in the game is this big. */
#define BENCH_HURT_BELOW 1000000.0f32

enum : uint32_t {
    BENCH_WIDTH = 1920u,
    BENCH_HEIGHT = 1080u,
    BENCH_DISTANCE_TARGET = 32u, /* the plan's render distance, and the game's default */
    BENCH_DISTANCE_FAR = 128u,
    BENCH_DISTANCE_MOST = 512u,
    BENCH_WARM_FRAMES = 30u,  /* before a settled row counts: TAA has no history to begin with */
    BENCH_PATH_FRAMES = 240u, /* an orbit or a settled flight */
    BENCH_WALK_FRAMES = 400u, /* a walk, at a tick to a frame: 20 seconds of the world */
    BENCH_LIVE_SECONDS = 10u, /* a live flight */
    BENCH_SAMPLES = 4096u,
    BENCH_MOBS = 100u,
    BENCH_PLACE_STEP = 128u,       /* the search for a biome: columns this far apart, */
    BENCH_PLACE_RINGS = 256u,      /* in square rings out to this many steps from the world's middle */
    BENCH_PLACE_INSIDE = 64u,      /* and this far around it the same biome, so that the view is of that biome */
    BENCH_CAVE_STEP = 16u,         /* the search for a cave: columns this far apart */
    BENCH_CAVE_RINGS = 48u,        /* in rings */
    BENCH_CAVE_BELOW = 12u,        /* at least this far under the ground */
    BENCH_CAVE_HEIGHT = 5u,        /* with this much air overhead */
    BENCH_CAVE_AROUND = 5u,        /* and at eye height this far each way, so that it is a room and not a crack */
    BENCH_ORBIT_CLEAR_RINGS = 3u,  /* an orbit clears the ground this many rings out, */
    BENCH_ORBIT_CLEAR_STEP = 12u,  /* this far apart, */
    BENCH_ORBIT_CLEAR_POINTS = 8u, /* looking at this many points on each */
    BENCH_GROUND_STEP = 32u,       /* a flight clears the ground it can see at this spacing */
    BENCH_DISTINCT_WANTED = 8u,    /* colours on the grid below that say a picture has something in it */
    BENCH_DISTINCT_ACROSS = 64u,
    BENCH_DISTINCT_DOWN = 36u,
    BENCH_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    BENCH_COLUMN_VOXELS = 16u * 16u * BENCH_LAYERS,
    BENCH_PATH_BYTES = 512u,
    BENCH_ROWS = 14u,
    BENCH_WORLDS = 6u,
    /* The card is shared with a desktop: this and the budget's textures and frames stay well under 4 GiB. */
    BENCH_VOXELS_MIB = 3072u,
    BENCH_WALK_TURN_COUNTS = 8, /* of the mouse, a tick: it walks a wide circle, so that no answer stays good */
};
static_assert(MC_GPU_BUDGET_TEXTURES_MIB + MC_GPU_BUDGET_FRAMES_MIB + BENCH_VOXELS_MIB < 4096u);

typedef enum bench_place_kind_t : uint8_t {
    BENCH_PLACE_BIOME = 0, /* well inside the nearest piece of a biome */
    BENCH_PLACE_CAVE = 1,  /* the nearest room underground */
} bench_place_kind_t;

typedef enum bench_path_t : uint8_t {
    BENCH_PATH_ORBIT = 0,  /* stands still and turns right round, looking a little down: no streaming, all rendering */
    BENCH_PATH_FLIGHT = 1, /* straight on, above whatever is on the way: streaming and uploads too */
    BENCH_PATH_WALK = 2,   /* a player on foot among mobs, at night */
} bench_path_t;

typedef enum bench_mode_t : uint8_t {
    BENCH_MODE_SETTLED = 0, /* one thread, the world whole before every picture: the same pictures every run */
    BENCH_MODE_LIVE = 1,    /* the game's own threads against the clock: what a player gets */
} bench_mode_t;

/* A standard world: which seed is the whole game's default, so it is a place, a height and a time. */
typedef struct bench_world_t {
    const char *name;       /* 0 */
    float64_t day_fraction; /* 8: it stays there */
    float32_t eye_above;    /* 16: blocks over the ground */
    uint8_t biome;          /* 20: a biome_id_t */
    uint8_t kind;           /* 21: a bench_place_kind_t */
    uint8_t _pad0[2];       /* 22 */
} bench_world_t;
static_assert(sizeof(bench_world_t) == 24);

enum : uint8_t {
    BENCH_WORLD_PLAINS = 0,
    BENCH_WORLD_JUNGLE = 1,
    BENCH_WORLD_PEAKS = 2,
    BENCH_WORLD_OCEAN = 3,
    BENCH_WORLD_CAVE = 4,
    BENCH_WORLD_NIGHT = 5,
};

static const bench_world_t BENCH_WORLD_LIST[BENCH_WORLDS] = {
    {"plains", BENCH_DAY_MORNING, 24.0f32, (uint8_t)BIOME_ID_PLAINS, BENCH_PLACE_BIOME, {0u, 0u}},
    {"jungle", BENCH_DAY_MORNING, 16.0f32, (uint8_t)BIOME_ID_JUNGLE, BENCH_PLACE_BIOME, {0u, 0u}},
    {"jagged_peaks", BENCH_DAY_MORNING, 12.0f32, (uint8_t)BIOME_ID_JAGGED_PEAKS, BENCH_PLACE_BIOME, {0u, 0u}},
    {"deep_ocean", BENCH_DAY_MORNING, 10.0f32, (uint8_t)BIOME_ID_DEEP_OCEAN, BENCH_PLACE_BIOME, {0u, 0u}},
    {"cave", BENCH_DAY_MORNING, 0.0f32, 0u, BENCH_PLACE_CAVE, {0u, 0u}},
    {"night", BENCH_DAY_MIDNIGHT, 0.0f32, (uint8_t)BIOME_ID_PLAINS, BENCH_PLACE_BIOME, {0u, 0u}},
};

/* What is timed: one world, one path, in one way. */
typedef struct bench_row_t {
    const char *name;         /* 0 */
    uint32_t render_distance; /* 8: 0 for the target's */
    uint8_t world;            /* 12: into BENCH_WORLD_LIST */
    uint8_t path;             /* 13: a bench_path_t */
    uint8_t mode;             /* 14: a bench_mode_t */
    uint8_t god_rays;         /* 15: and the one optional effect on top */
} bench_row_t;
static_assert(sizeof(bench_row_t) == 16);

static const bench_row_t BENCH_ROW_LIST[BENCH_ROWS] = {
    {"plains/orbit", BENCH_DISTANCE_TARGET, BENCH_WORLD_PLAINS, BENCH_PATH_ORBIT, BENCH_MODE_SETTLED, 0u},
    {"plains/flight", BENCH_DISTANCE_TARGET, BENCH_WORLD_PLAINS, BENCH_PATH_FLIGHT, BENCH_MODE_SETTLED, 0u},
    {"plains/flight-live", BENCH_DISTANCE_TARGET, BENCH_WORLD_PLAINS, BENCH_PATH_FLIGHT, BENCH_MODE_LIVE, 0u},
    {"plains/orbit-godrays", BENCH_DISTANCE_TARGET, BENCH_WORLD_PLAINS, BENCH_PATH_ORBIT, BENCH_MODE_SETTLED, 1u},
    {"jungle/orbit", BENCH_DISTANCE_TARGET, BENCH_WORLD_JUNGLE, BENCH_PATH_ORBIT, BENCH_MODE_SETTLED, 0u},
    {"jungle/flight-live", BENCH_DISTANCE_TARGET, BENCH_WORLD_JUNGLE, BENCH_PATH_FLIGHT, BENCH_MODE_LIVE, 0u},
    {"jagged_peaks/orbit", BENCH_DISTANCE_TARGET, BENCH_WORLD_PEAKS, BENCH_PATH_ORBIT, BENCH_MODE_SETTLED, 0u},
    {"jagged_peaks/flight-live", BENCH_DISTANCE_TARGET, BENCH_WORLD_PEAKS, BENCH_PATH_FLIGHT, BENCH_MODE_LIVE, 0u},
    {"deep_ocean/orbit", BENCH_DISTANCE_TARGET, BENCH_WORLD_OCEAN, BENCH_PATH_ORBIT, BENCH_MODE_SETTLED, 0u},
    {"deep_ocean/flight-live", BENCH_DISTANCE_TARGET, BENCH_WORLD_OCEAN, BENCH_PATH_FLIGHT, BENCH_MODE_LIVE, 0u},
    {"cave/orbit", BENCH_DISTANCE_TARGET, BENCH_WORLD_CAVE, BENCH_PATH_ORBIT, BENCH_MODE_SETTLED, 0u},
    {"night/walk-mobs", BENCH_DISTANCE_TARGET, BENCH_WORLD_NIGHT, BENCH_PATH_WALK, BENCH_MODE_SETTLED, 0u},
    {"plains/flight-live-r128", BENCH_DISTANCE_FAR, BENCH_WORLD_PLAINS, BENCH_PATH_FLIGHT, BENCH_MODE_LIVE, 0u},
    {"plains/orbit-r512", BENCH_DISTANCE_MOST, BENCH_WORLD_PLAINS, BENCH_PATH_ORBIT, BENCH_MODE_SETTLED, 0u},
};

/* Where a world is, once it has been looked for. */
typedef struct bench_place_t {
    int32_t x;      /* 0 */
    int32_t y;      /* 4 */
    int32_t z;      /* 8: the ground, or a cave's eye */
    uint8_t found;  /* 12 */
    uint8_t failed; /* 13 */
    uint8_t _pad0[2];
} bench_place_t;
static_assert(sizeof(bench_place_t) == 16);

/* What a row came to. */
typedef struct bench_result_t {
    float64_t fps;            /* 0: settled, what the card's clock allows; live, what came out */
    float64_t card_mean_ms;   /* 8: the card's own clock on a whole frame */
    float64_t card_median_ms; /* 16 */
    float64_t card_p99_ms;    /* 24 */
    float64_t card_worst_ms;  /* 32 */
    float64_t host_mean_ms;   /* 40: settled: what a draw took past the card: settling, the HUD, the copy out */
    float64_t pass_ms[FRAME_PASS_COUNT];     /* 48 */
    float64_t kernel_ms[FRAME_KERNEL_COUNT]; /* 88: settled only */
    float64_t tick_mean_ms;                  /* 160 */
    float64_t tick_p99_ms;                   /* 168 */
    float64_t tick_worst_ms;                 /* 176 */
    float64_t worst_gap_ms;                  /* 184: live: longest without a new picture, to the display's rate */
    float64_t open_seconds;                  /* 192: game_open and the whole world on the card */
    float64_t seconds;                       /* 200: what was measured took */
    uint64_t frames;                         /* 208 */
    uint64_t columns_made;                   /* 216: while it was measured */
    uint64_t upload_bytes;                   /* 224: while it was measured */
    uint64_t budget_stops;                   /* 232: frames that left sections for the next, for lack of budget */
    uint64_t heap_calls;                     /* 240: while it was measured */
    uint64_t vram_bytes[DEVICE_POOL_COUNT];  /* 248 */
    uint64_t picture_hash; /* 272: of the last picture: the same on a settled row is the same pictures */
    uint64_t searches;     /* 280: what the mobs' minds asked for */
    uint64_t voxels_used;  /* 288: of the voxel pool's block, what sections have ever been put in */
    int32_t place_x;       /* 296 */
    int32_t place_y;       /* 300 */
    uint32_t mobs_fewest;  /* 304: walking, at the lowest */
    uint8_t ran;           /* 308 */
    uint8_t sound;         /* 309: what it measured was what it was meant to */
    uint8_t met_fps;       /* 310 */
    uint8_t met_tick;      /* 311 */
} bench_result_t;
static_assert(sizeof(bench_result_t) == 312);

/* What the command line asked for. */
typedef struct bench_options_t {
    const char *only;   /* 0: rows whose name holds this */
    const char *record; /* 8: the history to add to */
    const char *label;  /* 16: says what was being measured, in that history */
    const char *trace;  /* 24: a profile of the one row */
    const char *shots;  /* 32: a picture of the end of each row goes here */
} bench_options_t;
static_assert(sizeof(bench_options_t) == 40);

typedef struct bench_t {
    heap_region_t owned_memory;                   /* 0 */
    app_t *borrowed_app;                          /* 16: what main would have on its stack */
    float32_t *borrowed_card_ms;                  /* 24: BENCH_SAMPLES */
    float32_t *borrowed_tick_ms;                  /* 32: BENCH_SAMPLES */
    uint8_t _pad1[8];                             /* 40 */
    uint16_t *borrowed_states;                    /* 48: a column's, for looking at the ground */
    uint8_t *borrowed_opened;                     /* 56: terrain's scratch for that */
    uint8_t *borrowed_picture;                    /* 64: stands where the display's buffer would */
    uint8_t *borrowed_png;                        /* 72 */
    float64_t (*borrowed_spawns)[COLLISION_AXES]; /* 80: BENCH_MOBS: where mobs come in on a walk */
    uint64_t png_bytes;                           /* 88 */
    uint32_t started;                             /* 96: how many of the steps of bench_start are to be undone */
    uint8_t _pad0[4];                             /* 100 */
    bench_place_t places[BENCH_WORLDS];           /* 104 */
} bench_t;
static_assert(sizeof(bench_t) == 200);

/* ---- Starting and stopping everything main does ------------------------------------------------------------------ */

/* The order is main's app_init, and for the same reasons. All of it stays up for every row, as each row opens a world of its own. */
static bool bench_start(bench_t *bench, bool profiled) {
    app_t *const app = bench->borrowed_app;
    if (profiled && !profiler_init(&app->profiler, profiler_limits_default())) {
        return false;
    }
    bench->started = 1u;
    if (!math_init(&app->math)) {
        return false;
    }
    bench->started = 2u;
    if (!simd_init(&app->simd)) {
        return false;
    }
    bench->started = 3u;
    if (!texture_init(&app->texture)) {
        return false;
    }
    bench->started = 4u;
    if (!thread_topology_read(&app->topology) || !job_init(&app->job, &app->topology)) {
        return false;
    }
    bench->started = 5u;
    if (profiled) {
        job_profiler_set(&app->job, &app->profiler);
    }
    gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    limits.device.pool_bytes[DEVICE_POOL_VOXELS] = (uint64_t)BENCH_VOXELS_MIB << 20;
    if (gpu_bridge_init(&app->gpu_bridge, limits) != GPU_STATUS_OK) {
        return false;
    }
    bench->started = 6u;
    if (!thread_pin_self(thread_cpu_for(&app->topology, THREAD_ROLE_MAIN, 0u)) ||
        !world_init(&app->world, world_limits_default())) {
        return false;
    }
    bench->started = 7u;
    if (!terrain_init(&app->terrain, TERRAIN_SEED_DEFAULT)) {
        return false;
    }
    bench->started = 8u;
    if (!physics_init(&app->physics)) {
        return false;
    }
    player_init(&app->player, &app->physics);
    bench->started = 9u;
    if (!entity_init(&app->entity, &app->physics)) {
        return false;
    }
    bench->started = 10u;
    if (!item_init(&app->item)) {
        return false;
    }
    bench->started = 11u;
    if (!mob_init(&app->mob, &app->job)) {
        return false;
    }
    bench->started = 12u;
    if (!render_init(&app->render, &app->gpu_bridge.device_memory, &app->job, render_limits_default())) {
        return false;
    }
    bench->started = 13u;
    if (!ui_init(&app->ui, &app->texture)) {
        return false;
    }
    bench->started = 14u;
    return true;
}

static void bench_stop(bench_t *bench) {
    app_t *const app = bench->borrowed_app;
    const uint32_t started = bench->started;
    game_deinit(&app->game);
    if (started >= 14u) {
        ui_deinit(&app->ui);
    }
    if (started >= 13u) {
        render_deinit(&app->render);
    }
    if (started >= 12u) {
        mob_deinit(&app->mob);
    }
    if (started >= 11u) {
        item_deinit(&app->item);
    }
    if (started >= 10u) {
        entity_deinit(&app->entity);
    }
    if (started >= 9u) {
        player_deinit(&app->player);
        physics_deinit(&app->physics);
    }
    if (started >= 8u) {
        terrain_deinit(&app->terrain);
    }
    if (started >= 7u) {
        world_deinit(&app->world);
    }
    if (started >= 6u) {
        gpu_bridge_deinit(&app->gpu_bridge);
    }
    if (started >= 5u) {
        job_deinit(&app->job);
    }
    if (started >= 4u) {
        texture_deinit(&app->texture);
    }
    if (started >= 3u) {
        simd_deinit(&app->simd);
    }
    if (started >= 2u) {
        math_deinit(&app->math);
    }
}

/* ---- Finding a standard world's place ---------------------------------------------------------------------------- */

static terrain_density_box_t bench_column_box(int32_t x, int32_t y) {
    return (terrain_density_box_t){
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = 1u,
        .count_y = 1u,
        .count_z = BENCH_LAYERS,
    };
}

/* The highest z with anything in it at this x and y, water and what grows included. Leaves that column in borrowed_states. */
static int32_t bench_ground_z(const bench_t *bench, int32_t x, int32_t y) {
    const terrain_density_box_t box = bench_column_box(x, y);
    terrain_generate_box(&bench->borrowed_app->terrain, &box, bench->borrowed_opened, bench->borrowed_states);
    for (int32_t storage = (int32_t)BENCH_LAYERS - 1; storage > 0; storage--) {
        if (bench->borrowed_states[storage] != BLOCK_ID_AIR) {
            return storage + COLUMN_Z_STORAGE_MIN;
        }
    }
    return COLUMN_Z_STORAGE_MIN;
}

static bool bench_biome_is(const terrain_t *terrain, int32_t x, int32_t y, uint8_t biome) {
    const terrain_climate_area_t area = {
        .origin_x = x - (int32_t)BENCH_PLACE_INSIDE,
        .origin_y = y - (int32_t)BENCH_PLACE_INSIDE,
        .step_x = (int32_t)BENCH_PLACE_INSIDE,
        .step_y = (int32_t)BENCH_PLACE_INSIDE,
        .count_x = 3u,
        .count_y = 3u,
    };
    terrain_biome_column_t columns[9];
    terrain_biome_columns_area(terrain->borrowed_biome, &area, columns);
    for (uint32_t i = 0u; i < 9u; i++) {
        if (columns[i].biome != biome) {
            return false;
        }
    }
    return true;
}

/* The next point out from (0, 0), a square ring at a time, which both searches go by: the nearest place is the one found. */
static void bench_ring_next(int32_t *dx, int32_t *dy, int32_t *ring) {
    if (*dx == *ring && *dy == *ring) {
        (*ring)++;
        *dx = -*ring;
        *dy = -*ring;
    } else if (*dx == *ring) {
        *dx = -*ring;
        (*dy)++;
    } else if (*dy == -*ring || *dy == *ring) {
        (*dx)++;
    } else {
        *dx = *ring; /* the inside of a square has been looked at */
    }
}

static bool bench_find_biome(const bench_t *bench, uint8_t biome, bench_place_t *out) {
    const terrain_t *const terrain = &bench->borrowed_app->terrain;
    int32_t dx = 0, dy = 0, ring = 0;
    while (ring <= (int32_t)BENCH_PLACE_RINGS) {
        const int32_t x = dx * (int32_t)BENCH_PLACE_STEP, y = dy * (int32_t)BENCH_PLACE_STEP;
        if (bench_biome_is(terrain, x, y, biome)) {
            out->x = x;
            out->y = y;
            out->z = bench_ground_z(bench, x, y);
            return true;
        }
        bench_ring_next(&dx, &dy, &ring);
    }
    return false;
}

static bool bench_is_air_at(const bench_t *bench, int32_t x, int32_t y, int32_t z) {
    const terrain_density_box_t box = bench_column_box(x, y);
    terrain_generate_box(&bench->borrowed_app->terrain, &box, bench->borrowed_opened, bench->borrowed_states);
    return bench->borrowed_states[z - COLUMN_Z_STORAGE_MIN] == BLOCK_ID_AIR;
}

/* In the column just looked at: the eye's z in a tall enough gap well under the ground, or false. The lowest first is no better than
 * the highest, so it goes down from the top. */
static bool bench_cave_eye(const bench_t *bench, int32_t ground_z, int32_t *out_z) {
    const uint16_t *const states = bench->borrowed_states;
    int32_t air = 0;
    for (int32_t z = ground_z - (int32_t)BENCH_CAVE_BELOW; z > COLUMN_Z_STORAGE_MIN; z--) {
        const uint16_t state = states[z - COLUMN_Z_STORAGE_MIN];
        if (state == BLOCK_ID_AIR) {
            air++;
        } else {
            if (air >= (int32_t)BENCH_CAVE_HEIGHT && (block_defs[state].flags & BLOCK_FLAG_SOLID) != 0u) {
                *out_z = z + 3;
                return true;
            }
            air = 0;
        }
    }
    return false;
}

static bool bench_find_cave(const bench_t *bench, bench_place_t *out) {
    int32_t dx = 0, dy = 0, ring = 0;
    const int32_t around = (int32_t)BENCH_CAVE_AROUND;
    while (ring <= (int32_t)BENCH_CAVE_RINGS) {
        const int32_t x = dx * (int32_t)BENCH_CAVE_STEP, y = dy * (int32_t)BENCH_CAVE_STEP;
        int32_t eye_z = 0;
        if (bench_cave_eye(bench, bench_ground_z(bench, x, y), &eye_z) &&
            bench_is_air_at(bench, x + around, y, eye_z) && bench_is_air_at(bench, x - around, y, eye_z) &&
            bench_is_air_at(bench, x, y + around, eye_z) && bench_is_air_at(bench, x, y - around, eye_z)) {
            out->x = x;
            out->y = y;
            out->z = eye_z;
            return true;
        }
        bench_ring_next(&dx, &dy, &ring);
    }
    return false;
}

/* Remembered, as a place does not change within a run. False if there is none in the search. */
static bool bench_place_of(bench_t *bench, uint32_t world, bench_place_t *out) {
    bench_place_t *const place = &bench->places[world];
    const bench_world_t *const standard = &BENCH_WORLD_LIST[world];
    if (place->found == 0u && place->failed == 0u) {
        const bool found = standard->kind == BENCH_PLACE_BIOME ? bench_find_biome(bench, standard->biome, place)
                                                               : bench_find_cave(bench, place);
        place->found = found ? 1u : 0u;
        place->failed = (uint8_t)(found ? 0u : 1u);
    }
    *out = *place;
    return place->found != 0u;
}

/* ---- The paths --------------------------------------------------------------------------------------------------- */

/* How far a flight on this row goes. */
static float64_t bench_flight_length(const bench_row_t *row) {
    return row->mode == BENCH_MODE_LIVE
               ? BENCH_FLIGHT_LIVE_SPEED * (float64_t)BENCH_LIVE_SECONDS
               : BENCH_FLIGHT_SETTLED_STEP * (float64_t)(BENCH_PATH_FRAMES + BENCH_WARM_FRAMES);
}

/* A flight goes at one height, above the highest ground along it, so that the eye is never inside a mountain, where every ray stops at
 * once and the number would say nothing. */
static float64_t bench_flight_z(const bench_t *bench, const bench_row_t *row, const bench_place_t *place,
                                float32_t eye_above) {
    const float64_t length = bench_flight_length(row);
    const float64_t forward_x = cos((float64_t)BENCH_FLIGHT_HEADING), forward_y = sin((float64_t)BENCH_FLIGHT_HEADING);
    int32_t highest = place->z;
    for (float64_t along = 0.0f64; along <= length; along += (float64_t)BENCH_GROUND_STEP) {
        const int32_t ground =
            bench_ground_z(bench, place->x + (int32_t)(along * forward_x), place->y + (int32_t)(along * forward_y));
        highest = ground > highest ? ground : highest;
    }
    return (float64_t)highest + (float64_t)eye_above;
}

/* An orbit stands clear of what is near it, on the same reasoning, or it looks at one wall for half its turn. A cave's eye is where it is. */
static float64_t bench_orbit_z(const bench_t *bench, const bench_place_t *place, float32_t eye_above) {
    int32_t highest = place->z;
    for (uint32_t ring = 1u; ring <= BENCH_ORBIT_CLEAR_RINGS; ring++) {
        for (uint32_t i = 0u; i < BENCH_ORBIT_CLEAR_POINTS; i++) {
            const float64_t angle = BENCH_TWO_PI * (float64_t)i / (float64_t)BENCH_ORBIT_CLEAR_POINTS;
            const float64_t radius = (float64_t)(ring * BENCH_ORBIT_CLEAR_STEP);
            const int32_t ground = bench_ground_z(bench, place->x + (int32_t)(radius * cos(angle)),
                                                  place->y + (int32_t)(radius * sin(angle)));
            highest = ground > highest ? ground : highest;
        }
    }
    return (float64_t)highest + (float64_t)eye_above;
}

/* Where an orbit looks, part of the way round. */
static void bench_orbit_at(render_camera_t *camera, const bench_place_t *place, float64_t height, float64_t part) {
    camera->position[0] = (float64_t)place->x + 0.5f64;
    camera->position[1] = (float64_t)place->y + 0.5f64;
    camera->position[2] = height;
    camera->yaw = (float32_t)(BENCH_TWO_PI * part);
    camera->pitch = BENCH_ORBIT_PITCH;
}

static void bench_flight_at(render_camera_t *camera, const bench_place_t *place, float64_t height, float64_t along) {
    camera->position[0] = (float64_t)place->x + 0.5f64 + along * cos((float64_t)BENCH_FLIGHT_HEADING);
    camera->position[1] = (float64_t)place->y + 0.5f64 + along * sin((float64_t)BENCH_FLIGHT_HEADING);
    camera->position[2] = height;
    camera->yaw = BENCH_FLIGHT_HEADING;
    camera->pitch = BENCH_FLIGHT_PITCH;
}

/* ---- What was measured, and whether it means anything --------------------------------------------------------------- */

static int32_t bench_compare(const void *left, const void *right) {
    const float32_t a = *(const float32_t *)left, b = *(const float32_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static void bench_stats(float32_t *samples, uint32_t count, float64_t *mean, float64_t *median, float64_t *p99,
                        float64_t *worst) {
    *mean = 0.0f64;
    *median = 0.0f64;
    *p99 = 0.0f64;
    *worst = 0.0f64;
    if (count == 0u) {
        return;
    }
    float64_t sum = 0.0f64;
    for (uint32_t i = 0u; i < count; i++) {
        sum += (float64_t)samples[i];
    }
    qsort(samples, count, sizeof(samples[0]), bench_compare);
    *mean = sum / (float64_t)count;
    *median = (float64_t)samples[count / 2u];
    *p99 = (float64_t)samples[(uint32_t)((uint64_t)count * 99u / 100u)];
    *worst = (float64_t)samples[count - 1u];
}

/* FNV-1a: to say whether two runs drew the same picture, not to keep anything out. */
static uint64_t bench_hash(const uint8_t *bytes, uint64_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < count; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

/* Whether a picture has anything in it: an eye inside a wall is very fast, and says nothing. */
static bool bench_picture_has_things(const uint8_t *picture) {
    uint32_t seen[BENCH_DISTINCT_WANTED];
    uint32_t count = 0u;
    for (uint32_t row = 0u; row < BENCH_DISTINCT_DOWN && count < BENCH_DISTINCT_WANTED; row++) {
        const uint32_t y = (row * 2u + 1u) * BENCH_HEIGHT / (BENCH_DISTINCT_DOWN * 2u);
        for (uint32_t across = 0u; across < BENCH_DISTINCT_ACROSS && count < BENCH_DISTINCT_WANTED; across++) {
            const uint32_t x = (across * 2u + 1u) * BENCH_WIDTH / (BENCH_DISTINCT_ACROSS * 2u);
            uint32_t pixel = 0u;
            memcpy(&pixel, picture + (uint64_t)y * BENCH_WIDTH * 4u + (uint64_t)x * 4u, sizeof(pixel));
            pixel &= 0x00FFFFFFu;
            bool known = false;
            for (uint32_t i = 0u; i < count; i++) {
                known = known || seen[i] == pixel;
            }
            if (!known) {
                seen[count] = pixel;
                count++;
            }
        }
    }
    return count >= BENCH_DISTINCT_WANTED;
}

/* What the frame's books say happened between two looks at them. */
static void bench_passes_note(bench_result_t *result, const render_frame_stats_t *before,
                              const render_frame_stats_t *after) {
    const uint64_t frames = after->frames - before->frames;
    result->frames = frames;
    if (frames == 0u) {
        return;
    }
    for (uint32_t pass = 0u; pass < FRAME_PASS_COUNT; pass++) {
        result->pass_ms[pass] = (after->sum_ms[pass] - before->sum_ms[pass]) / (float64_t)frames;
    }
}

static uint64_t bench_heap_calls(const heap_counters_t *before, const heap_counters_t *after) {
    return (after->reserve_calls - before->reserve_calls) + (after->release_calls - before->release_calls);
}

/* ---- A settled row ----------------------------------------------------------------------------------------------- */

/* Mobs come in near the player, on top of whatever is there, so that every one is near enough to think and to be drawn. Mostly what the
 * row will measure is not this, so the ground is looked at once, here. */
static void bench_spawns_work_out(bench_t *bench, const float64_t feet[COLLISION_AXES]) {
    for (uint32_t i = 0u; i < BENCH_MOBS; i++) {
        /* The golden angle spreads them evenly at any count. */
        const float64_t angle = (float64_t)i * 2.399963229728653f64;
        const float64_t radius = 8.0f64 + (float64_t)(i % 25u);
        const float64_t x = feet[0] + radius * cos(angle), y = feet[1] + radius * sin(angle);
        bench->borrowed_spawns[i][0] = x;
        bench->borrowed_spawns[i][1] = y;
        bench->borrowed_spawns[i][2] = (float64_t)bench_ground_z(bench, (int32_t)floor(x), (int32_t)floor(y)) + 1.0f64;
    }
}

/* The count stands at the row's: bursts and despawns are in what is timed, and their refill is not. */
static void bench_mobs_refill(bench_t *bench, uint32_t *turn) {
    app_t *const app = bench->borrowed_app;
    entity_store_t *const store = &app->entity.store;
    const mob_id_t kinds[3] = {MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER};
    while (store->count < BENCH_MOBS) {
        const uint32_t before = store->count;
        (void)mob_spawn_body(&app->game.mob_spawn, store, kinds[*turn % 3u], bench->borrowed_spawns[*turn % BENCH_MOBS],
                             0.0f32);
        (*turn)++;
        if (store->count == before) {
            return;
        }
    }
}

/* Between ticks, so that it stays something to chase: a dead player has no one after it, and the row would then be timing idle minds. Each
 * blow still gets as far as it being asked for. Outside what is timed: one thread, so nothing else is looking. */
static void bench_player_heal(player_t *player) {
    player->survival.invulnerable = UINT8_MAX;
    player->survival.last_hurt = BENCH_HURT_BELOW;
    player->survival.health = (float32_t)PLAYER_SURVIVAL_HEALTH_MAX;
    player->survival.food = PLAYER_SURVIVAL_FOOD_MAX;
    player->survival.air = (int32_t)PLAYER_SURVIVAL_AIR_MAX;
}

static bool bench_row_settled(bench_t *bench, const bench_row_t *row, const bench_place_t *place, float64_t height,
                              bench_result_t *result) {
    app_t *const app = bench->borrowed_app;
    game_t *const game = &app->game;
    render_t *const render = &app->render;
    const bool walking = row->path == BENCH_PATH_WALK;
    const uint32_t measured = walking ? BENCH_WALK_FRAMES : BENCH_PATH_FRAMES;
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    display_buffer_t target = {.owned_map = bench->borrowed_picture,
                               .map_bytes = (uint64_t)BENCH_WIDTH * BENCH_HEIGHT * 4u,
                               .pitch = BENCH_WIDTH * 4u,
                               .width = BENCH_WIDTH,
                               .height = BENCH_HEIGHT};
    display_script_state_t state;
    memset(&state, 0, sizeof(state));
    if (walking) {
        state.down[KEY_W / 64u] |= 1ull << (KEY_W % 64u);
        state.tick_dx = BENCH_WALK_TURN_COUNTS;
    }
    display_headless_tick_t tick = {.events = nullptr, .state = &state, .event_count = 0u, .tick = 0u};
    uint32_t turn = 0u;
    if (walking) {
        bench_spawns_work_out(bench, app->player.movement.position);
        bench_mobs_refill(bench, &turn);
    }

    render_frame_stats_t before = render->frame.stats;
    heap_counters_t heap_before = {0};
    uint64_t columns_before = 0u, upload_before = 0u, stops_before = 0u;
    float64_t kernel_sum[FRAME_KERNEL_COUNT] = {0.0f64};
    float64_t host_sum = 0.0f64;
    int64_t measuring_ns = 0;
    uint32_t mobs_fewest = UINT32_MAX;
    for (uint32_t frame = 0u; frame < BENCH_WARM_FRAMES + measured; frame++) {
        const bool counted = frame >= BENCH_WARM_FRAMES;
        if (frame == BENCH_WARM_FRAMES) {
            before = render->frame.stats;
            columns_before = game->columns_made;
            upload_before = render->upload.stats.bytes_sent;
            stops_before = render->upload.stats.budget_stop_count;
            heap_counters_read(&heap_before);
            measuring_ns = time_now_ns();
        }
        if (row->path == BENCH_PATH_ORBIT) {
            bench_orbit_at(&game->camera, place, height, (float64_t)frame / (float64_t)(BENCH_WARM_FRAMES + measured));
        } else if (row->path == BENCH_PATH_FLIGHT) {
            bench_flight_at(&game->camera, place, height, BENCH_FLIGHT_SETTLED_STEP * (float64_t)frame);
        }
        tick.tick = frame;
        state.tick = frame;
        const int64_t tick_ns = time_now_ns();
        if (!scene.tick(scene.borrowed_context, &tick)) {
            return false;
        }
        const int64_t draw_ns = time_now_ns();
        if (!scene.draw(scene.borrowed_context, &tick, &target)) {
            return false;
        }
        const int64_t done_ns = time_now_ns();
        if (counted) {
            const uint32_t i = frame - BENCH_WARM_FRAMES;
            const frame_timings_t *const timings = &render->frame.last_timings;
            bench->borrowed_card_ms[i] = timings->total_ms;
            bench->borrowed_tick_ms[i] = (float32_t)time_ns_to_ms(draw_ns - tick_ns);
            for (uint32_t k = 0u; k < FRAME_KERNEL_COUNT; k++) {
                kernel_sum[k] += (float64_t)timings->kernel_ms[k];
            }
            host_sum += time_ns_to_ms(done_ns - draw_ns) - (float64_t)timings->total_ms;
        }
        if (walking) {
            bench_mobs_refill(bench, &turn);
            const uint32_t count = app->entity.store.count;
            mobs_fewest = counted && count < mobs_fewest ? count : mobs_fewest;
            bench_player_heal(&app->player);
        }
    }
    const int64_t ended_ns = time_now_ns();
    heap_counters_t heap_after;
    heap_counters_read(&heap_after);

    bench_passes_note(result, &before, &render->frame.stats);
    result->seconds = time_ns_to_ms(ended_ns - measuring_ns) / 1000.0f64;
    result->columns_made = game->columns_made - columns_before;
    result->upload_bytes = render->upload.stats.bytes_sent - upload_before;
    result->budget_stops = render->upload.stats.budget_stop_count - stops_before;
    result->heap_calls = bench_heap_calls(&heap_before, &heap_after);
    for (uint32_t k = 0u; k < FRAME_KERNEL_COUNT; k++) {
        result->kernel_ms[k] = kernel_sum[k] / (float64_t)measured;
    }
    result->host_mean_ms = host_sum / (float64_t)measured;
    float64_t tick_median = 0.0f64;
    bench_stats(bench->borrowed_card_ms, measured, &result->card_mean_ms, &result->card_median_ms, &result->card_p99_ms,
                &result->card_worst_ms);
    bench_stats(bench->borrowed_tick_ms, measured, &result->tick_mean_ms, &tick_median, &result->tick_p99_ms,
                &result->tick_worst_ms);
    result->fps = result->card_mean_ms > 0.0f64 ? 1000.0f64 / result->card_mean_ms : 0.0f64;
    if (walking) {
        mob_path_stats_t paths;
        mob_path_stats_read(&app->mob.path, &paths);
        result->searches = paths.searches;
        result->mobs_fewest = mobs_fewest;
    }
    return true;
}

/* ---- A live row -------------------------------------------------------------------------------------------------- */

static bool bench_row_live(bench_t *bench, const bench_row_t *row, const bench_place_t *place, float64_t height,
                           bench_result_t *result) {
    (void)row;
    app_t *const app = bench->borrowed_app;
    game_t *const game = &app->game;
    render_t *const render = &app->render;
    display_buffer_t target = {.owned_map = bench->borrowed_picture,
                               .map_bytes = (uint64_t)BENCH_WIDTH * BENCH_HEIGHT * 4u,
                               .pitch = BENCH_WIDTH * 4u,
                               .width = BENCH_WIDTH,
                               .height = BENCH_HEIGHT};
    const render_frame_stats_t before = render->frame.stats;
    const uint64_t columns_before = game->columns_made;
    const uint64_t upload_before = render->upload.stats.bytes_sent;
    const uint64_t stops_before = render->upload.stats.budget_stop_count;
    if (!game_loop_start(game, nullptr)) {
        return false;
    }
    heap_counters_t heap_before;
    heap_counters_read(&heap_before);
    const render_fly_input_t still = {0};
    const int64_t frame_ns = (int64_t)(BENCH_LIVE_FRAME_SECONDS * (float64_t)TIME_NS_PER_SECOND);
    const int64_t total_ns = (int64_t)BENCH_LIVE_SECONDS * TIME_NS_PER_SECOND;
    const int64_t started_ns = time_now_ns();
    int64_t now_ns = started_ns, last_ns = started_ns, picture_ns = started_ns, next_ns = started_ns;
    uint64_t shown_seen = 0u;
    uint32_t card_count = 0u, tick_count = 0u, tick_seen = 0u;
    int64_t worst_gap_ns = 0;
    bool ok = true;
    while (ok && now_ns - started_ns < total_ns) {
        const float64_t seconds_in = (float64_t)(now_ns - started_ns) / (float64_t)TIME_NS_PER_SECOND;
        bench_flight_at(&game->camera, place, height, BENCH_FLIGHT_LIVE_SPEED * seconds_in);
        ok = game_loop_frame(game, &still, (float32_t)((float64_t)(now_ns - last_ns) / (float64_t)TIME_NS_PER_SECOND),
                             &target);
        last_ns = now_ns;
        now_ns = time_now_ns();
        if (game->pictures_shown != shown_seen) {
            shown_seen = game->pictures_shown;
            const int64_t gap_ns = now_ns - picture_ns;
            worst_gap_ns = gap_ns > worst_gap_ns ? gap_ns : worst_gap_ns;
            picture_ns = now_ns;
            if (card_count < BENCH_SAMPLES) {
                const uint32_t card_us = atomic_load_explicit(&game->hud_total_us, memory_order_relaxed);
                bench->borrowed_card_ms[card_count] = (float32_t)card_us / 1000.0f32;
                card_count++;
            }
        }
        /* Only its last tick can be asked, so this counts one each time the answer changes. */
        const uint32_t tick_us = atomic_load_explicit(&game->hud_tick_us, memory_order_relaxed);
        if (tick_us != tick_seen && tick_count < BENCH_SAMPLES) {
            tick_seen = tick_us;
            bench->borrowed_tick_ms[tick_count] = (float32_t)tick_us / 1000.0f32;
            tick_count++;
        }
        next_ns += frame_ns;
        if (next_ns > now_ns) {
            time_sleep_ns(next_ns - now_ns);
            now_ns = time_now_ns();
        }
    }
    const int64_t ended_ns = time_now_ns();
    heap_counters_t heap_after;
    heap_counters_read(&heap_after);
    ok = game_loop_stop(game) && ok;
    if (!ok) {
        return false;
    }
    bench_passes_note(result, &before, &render->frame.stats);
    result->seconds = time_ns_to_ms(ended_ns - started_ns) / 1000.0f64;
    result->fps = (float64_t)result->frames / result->seconds;
    result->worst_gap_ms = time_ns_to_ms(worst_gap_ns);
    result->columns_made = game->columns_made - columns_before;
    result->upload_bytes = render->upload.stats.bytes_sent - upload_before;
    result->budget_stops = render->upload.stats.budget_stop_count - stops_before;
    result->heap_calls = bench_heap_calls(&heap_before, &heap_after);
    float64_t tick_median = 0.0f64;
    bench_stats(bench->borrowed_card_ms, card_count, &result->card_mean_ms, &result->card_median_ms,
                &result->card_p99_ms, &result->card_worst_ms);
    bench_stats(bench->borrowed_tick_ms, tick_count, &result->tick_mean_ms, &tick_median, &result->tick_p99_ms,
                &result->tick_worst_ms);
    return true;
}

/* ---- One row, and what is said about it ---------------------------------------------------------------------------- */

static void bench_shot(const bench_t *bench, const char *directory, const char *row_name) {
    char path[BENCH_PATH_BYTES];
    const int32_t length = snprintf(path, sizeof(path), "%s/%s.png", directory, row_name);
    if (length < 0 || (uint64_t)length >= sizeof(path)) {
        LOG_WARN("bench_world: the path for %s's picture is too long", row_name);
        return;
    }
    /* A row's name has one slash, between its world and its path. */
    char *const slash = strchr(path + strlen(directory) + 1u, '/');
    if (slash != nullptr) {
        *slash = '-';
    }
    uint64_t bytes = 0u;
    if (!display_png_encode(bench->borrowed_picture, BENCH_WIDTH * 4u, BENCH_WIDTH, BENCH_HEIGHT, bench->borrowed_png,
                            bench->png_bytes, &bytes) ||
        file_write_atomic(path, bench->borrowed_png, bytes) != FILE_OK) {
        LOG_WARN("bench_world: could not write %s", path);
    }
}

static float64_t bench_mib(uint64_t bytes) {
    return (float64_t)bytes / 1048576.0f64;
}

static const char *bench_mode_name(uint8_t mode) {
    return mode == BENCH_MODE_LIVE ? "live" : "settled";
}

static void bench_row_print(const bench_row_t *row, const bench_result_t *r) {
    if (r->ran == 0u) {
        printf("%-26s FAILED: its process died, or the log says why\n", row->name);
        return;
    }
    const bool targeted = row->render_distance == BENCH_DISTANCE_TARGET;
    printf("%-26s %-7s r%-3" PRIu32 " %7.1f fps  card mean %6.2f median %6.2f p99 %6.2f worst %6.2f ms  %s\n",
           row->name, bench_mode_name(row->mode), row->render_distance, (libc_double_t)r->fps,
           (libc_double_t)r->card_mean_ms, (libc_double_t)r->card_median_ms, (libc_double_t)r->card_p99_ms,
           (libc_double_t)r->card_worst_ms, targeted ? (r->met_fps != 0u ? "met" : "MISSED") : "(no target)");
    printf("%-26s passes coarse %.2f primary %.2f shade %.2f post %.2f readback %.2f ms; %" PRIu64
           " frames in %.1f s, at %" PRId32 ", %" PRId32 "\n",
           "", (libc_double_t)r->pass_ms[FRAME_PASS_COARSE], (libc_double_t)r->pass_ms[FRAME_PASS_PRIMARY],
           (libc_double_t)r->pass_ms[FRAME_PASS_SHADE], (libc_double_t)r->pass_ms[FRAME_PASS_POST],
           (libc_double_t)r->pass_ms[FRAME_PASS_READBACK], r->frames, (libc_double_t)r->seconds, r->place_x,
           r->place_y);
    printf("%-26s tick mean %.3f p99 %.3f worst %.3f ms; %" PRIu64 " columns, %.1f MiB uploaded, %" PRIu64
           " budget stops; heap calls %" PRIu64 "; opened in %.1f s\n",
           "", (libc_double_t)r->tick_mean_ms, (libc_double_t)r->tick_p99_ms, (libc_double_t)r->tick_worst_ms,
           r->columns_made, (libc_double_t)bench_mib(r->upload_bytes), r->budget_stops, r->heap_calls,
           (libc_double_t)r->open_seconds);
    printf("%-26s vram voxels %.0f (%.0f in use) textures %.0f frames %.0f MiB; ", "",
           (libc_double_t)bench_mib(r->vram_bytes[DEVICE_POOL_VOXELS]), (libc_double_t)bench_mib(r->voxels_used),
           (libc_double_t)bench_mib(r->vram_bytes[DEVICE_POOL_TEXTURES]),
           (libc_double_t)bench_mib(r->vram_bytes[DEVICE_POOL_FRAMES]));
    if (row->mode == BENCH_MODE_LIVE) {
        printf("longest without a new picture %.1f ms", (libc_double_t)r->worst_gap_ms);
    } else {
        printf("past the card %.2f ms a draw; picture %016" PRIX64, (libc_double_t)r->host_mean_ms, r->picture_hash);
    }
    if (row->path == BENCH_PATH_WALK) {
        printf("; at least %" PRIu32 " in the store, %" PRIu64 " searches, tick %s", r->mobs_fewest, r->searches,
               r->met_tick != 0u ? "met" : "MISSED");
    }
    printf("%s\n", r->sound != 0u ? "" : "; NOT SOUND: see README.md");
}

static bool bench_row(bench_t *bench, const bench_row_t *row, const bench_options_t *options, bool profiled,
                      bench_result_t *result) {
    app_t *const app = bench->borrowed_app;
    game_t *const game = &app->game;
    memset(result, 0, sizeof(*result));
    const bench_world_t *const standard = &BENCH_WORLD_LIST[row->world];
    const bool walking = row->path == BENCH_PATH_WALK;
    bench_place_t place;
    if (!bench_place_of(bench, row->world, &place)) {
        LOG_ERROR("bench_world: no place for %s within the search", standard->name);
        return false;
    }
    /* Before anything is open, as it uses what a world's jobs would. */
    float64_t height = (float64_t)place.z + (float64_t)standard->eye_above;
    if (row->path == BENCH_PATH_FLIGHT) {
        height = bench_flight_z(bench, row, &place, standard->eye_above);
    } else if (standard->kind == BENCH_PLACE_BIOME) {
        height = bench_orbit_z(bench, &place, standard->eye_above);
    }

    game_init(game, &app->world, &app->terrain, &app->render, &app->job, &app->texture, &app->topology);
    game_ui_set(game, &app->ui);
    if (profiled) {
        game_profiler_set(game, &app->profiler);
    }
    if (walking) {
        game_player_set(game, &app->player);
        game_entity_set(game, &app->entity);
        game_mob_set(game, &app->mob);
        game_item_set(game, &app->item);
    }
    const game_config_t config = {.render_distance = row->render_distance,
                                  .width = BENCH_WIDTH,
                                  .height = BENCH_HEIGHT,
                                  .features_off = (uint16_t)SHADE_FEATURE_DAY_CYCLE,
                                  .god_rays = row->god_rays,
                                  .uncapped = row->mode == BENCH_MODE_LIVE ? 1u : 0u};
    const int64_t opening_ns = time_now_ns();
    if (!game_open(game, &config)) {
        return false;
    }
    game_day_set(game, standard->day_fraction);
    if (row->path == BENCH_PATH_ORBIT) {
        bench_orbit_at(&game->camera, &place, height, 0.0f64);
    } else {
        bench_flight_at(&game->camera, &place, height, 0.0f64);
    }
    if (walking) {
        /* On the flat, and not where game_open looks for land, so that a walk stays among the mobs that came for it. */
        player_spawn(&app->player, (float64_t)place.x + 0.5f64, (float64_t)place.y + 0.5f64,
                     (float64_t)place.z + 1.0f64);
    }
    const render_fly_input_t still = {0};
    game_fly(game, &still, 0.0f32); /* which tells the world where the eye now is */
    bool ok = game_settle(game);
    result->open_seconds = time_ns_to_ms(time_now_ns() - opening_ns) / 1000.0f64;
    if (ok) {
        ok = row->mode == BENCH_MODE_LIVE ? bench_row_live(bench, row, &place, height, result)
                                          : bench_row_settled(bench, row, &place, height, result);
    }
    if (ok) {
        for (uint32_t pool = 0u; pool < DEVICE_POOL_COUNT; pool++) {
            result->vram_bytes[pool] = app->gpu_bridge.device_memory.pools[pool].live_bytes;
        }
        result->voxels_used = (uint64_t)app->render.buffers.pages_used * WORLD_BUFFERS_PAGE_BYTES;
        result->picture_hash = bench_hash(bench->borrowed_picture, (uint64_t)BENCH_WIDTH * BENCH_HEIGHT * 4u);
        result->place_x = place.x;
        result->place_y = place.y;
        const bool has_things = bench_picture_has_things(bench->borrowed_picture);
        const bool minds_worked = !walking || (result->searches != 0u && result->mobs_fewest >= BENCH_MOBS &&
                                               app->player.survival.died_count == 0u);
        result->sound = has_things && minds_worked && result->heap_calls == 0u && result->frames != 0u ? 1u : 0u;
        result->met_fps = result->fps >= BENCH_TARGET_FPS ? 1u : 0u;
        result->met_tick = result->tick_p99_ms <= BENCH_TARGET_TICK_MS ? 1u : 0u;
        result->ran = 1u;
        if (options->shots != nullptr) {
            bench_shot(bench, options->shots, row->name);
        }
    }
    game_report(game);
    game_close(game);
    return ok;
}

/* ---- The history --------------------------------------------------------------------------------------------------- */

static const char *bench_preset_name(void) {
#if defined(MC_PROFILE)
    return "profile";
#elif defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

/* A line per row, tab separated, the same columns forever: a new column goes on the end. See README.md. */
static bool bench_record(const bench_result_t *results, const bench_options_t *options) {
    FILE *const file = fopen(options->record, "a");
    if (file == nullptr) {
        LOG_ERROR("bench_world: could not open %s to add to it", options->record);
        return false;
    }
    (void)fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(
            file,
            "date\tlabel\tpreset\trow\tmode\tseed\twidth\theight\trender_distance\tplace_x\tplace_y\tfps\tcard_mean_"
            "ms\t"
            "card_median_ms\tcard_p99_ms\tcard_worst_ms\tcoarse_ms\tprimary_ms\tshade_ms\tpost_ms\treadback_ms\t"
            "tick_mean_ms\ttick_p99_ms\ttick_worst_ms\tworst_gap_ms\tframes\tseconds\topen_seconds\tcolumns\tupload_"
            "bytes\t"
            "budget_stops\theap_calls\tvram_voxels\tvram_textures\tvram_frames\tpicture\tsound\tmobs_"
            "fewest\tsearches\tvoxels_used");
        for (uint32_t k = 0u; k < FRAME_KERNEL_COUNT; k++) {
            fprintf(file, "\t%s_ms", frame_kernel_name((frame_kernel_t)k));
        }
        fprintf(file, "\n");
    }
    char date[32] = "";
    const time_t now = time(nullptr);
    struct tm parts;
    if (gmtime_r(&now, &parts) != nullptr) {
        (void)strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &parts);
    }
    for (uint32_t i = 0u; i < BENCH_ROWS; i++) {
        const bench_row_t *const row = &BENCH_ROW_LIST[i];
        const bench_result_t *const r = &results[i];
        if (r->ran == 0u) {
            continue;
        }
        fprintf(file,
                "%s\t%s\t%s\t%s\t%s\t%" PRIX64 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRId32 "\t%" PRId32
                "\t%.2f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.2f\t%" PRIu64
                "\t%.2f\t%.2f\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
                "\t%016" PRIX64 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu64 "\t%" PRIu64,
                date, options->label != nullptr ? options->label : "-", bench_preset_name(), row->name,
                bench_mode_name(row->mode), (uint64_t)TERRAIN_SEED_DEFAULT, (uint32_t)BENCH_WIDTH,
                (uint32_t)BENCH_HEIGHT, row->render_distance, r->place_x, r->place_y, (libc_double_t)r->fps,
                (libc_double_t)r->card_mean_ms, (libc_double_t)r->card_median_ms, (libc_double_t)r->card_p99_ms,
                (libc_double_t)r->card_worst_ms, (libc_double_t)r->pass_ms[FRAME_PASS_COARSE],
                (libc_double_t)r->pass_ms[FRAME_PASS_PRIMARY], (libc_double_t)r->pass_ms[FRAME_PASS_SHADE],
                (libc_double_t)r->pass_ms[FRAME_PASS_POST], (libc_double_t)r->pass_ms[FRAME_PASS_READBACK],
                (libc_double_t)r->tick_mean_ms, (libc_double_t)r->tick_p99_ms, (libc_double_t)r->tick_worst_ms,
                (libc_double_t)r->worst_gap_ms, r->frames, (libc_double_t)r->seconds, (libc_double_t)r->open_seconds,
                r->columns_made, r->upload_bytes, r->budget_stops, r->heap_calls, r->vram_bytes[DEVICE_POOL_VOXELS],
                r->vram_bytes[DEVICE_POOL_TEXTURES], r->vram_bytes[DEVICE_POOL_FRAMES],
                row->mode == BENCH_MODE_SETTLED ? r->picture_hash : 0u, (uint32_t)r->sound, r->mobs_fewest, r->searches,
                r->voxels_used);
        for (uint32_t k = 0u; k < FRAME_KERNEL_COUNT; k++) {
            fprintf(file, "\t%.3f", (libc_double_t)r->kernel_ms[k]);
        }
        fprintf(file, "\n");
    }
    const bool closed = fclose(file) == 0;
    if (!closed) {
        LOG_ERROR("bench_world: could not finish writing %s", options->record);
    }
    return closed;
}

/* ---- Running it ---------------------------------------------------------------------------------------------------- */

static void bench_usage(const char *program) {
    fprintf(stderr,
            "usage: %s [--only <text>] [--record <file>] [--label <text>] [--shots <dir>] [--trace <file>] "
            "[--list]\n"
            "  --only <text>    only the rows whose name holds this\n"
            "  --record <file>  add a line per row to this history: bench/world/history.tsv is the repository's\n"
            "  --label <text>   says what was measured, in the history: no tabs\n"
            "  --shots <dir>    write the last picture of each row here, as a PNG\n"
            "  --trace <file>   with --only that picks one row: its Chrome trace here, and the profiler's table on "
            "stdout.\n"
            "                   Not in a release build\n"
            "  --list           print the rows, and do nothing\n",
            program);
}

static bool bench_row_chosen(uint32_t row, const char *only) {
    return only == nullptr || strstr(BENCH_ROW_LIST[row].name, only) != nullptr;
}

static uint32_t bench_rows_chosen(const char *only) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < BENCH_ROWS; i++) {
        count += bench_row_chosen(i, only) ? 1u : 0u;
    }
    return count;
}

static uint64_t bench_page(uint64_t bytes) {
    return (bytes + 4095ull) & ~4095ull;
}

/* The heap region, cut up. */
static bool bench_memory(bench_t *bench) {
    const uint64_t png_bytes = display_png_bytes_needed(BENCH_WIDTH, BENCH_HEIGHT);
    const uint64_t sizes[] = {
        bench_page(sizeof(app_t)),
        bench_page(BENCH_SAMPLES * sizeof(float32_t)),
        bench_page(BENCH_SAMPLES * sizeof(float32_t)),
        bench_page((uint64_t)BENCH_COLUMN_VOXELS * sizeof(uint16_t)),
        bench_page(BENCH_COLUMN_VOXELS),
        bench_page((uint64_t)BENCH_WIDTH * BENCH_HEIGHT * 4u),
        bench_page(png_bytes),
        bench_page((uint64_t)BENCH_MOBS * COLLISION_AXES * sizeof(float64_t)),
    };
    uint64_t total = 0u;
    for (uint32_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        total += sizes[i];
    }
    if (png_bytes == 0u || !heap_reserve(total, &bench->owned_memory)) {
        return false;
    }
    uint8_t *at = bench->owned_memory.owned_base;
    bench->borrowed_app = (app_t *)at;
    at += sizes[0];
    bench->borrowed_card_ms = (float32_t *)at;
    at += sizes[1];
    bench->borrowed_tick_ms = (float32_t *)at;
    at += sizes[2];
    bench->borrowed_states = (uint16_t *)at;
    at += sizes[3];
    bench->borrowed_opened = at;
    at += sizes[4];
    bench->borrowed_picture = at;
    at += sizes[5];
    bench->borrowed_png = at;
    at += sizes[6];
    bench->borrowed_spawns = (float64_t(*)[COLLISION_AXES])at;
    bench->png_bytes = png_bytes;
    memset(bench->borrowed_app, 0, sizeof(app_t));
    return true;
}

/* What it comes to, plainly. */
static void bench_summary(const bench_result_t *results, const char *only) {
    uint32_t aimed = 0u, met = 0u;
    float64_t worst_fps = -1.0f64;
    const char *worst_row = "none";
    for (uint32_t i = 0u; i < BENCH_ROWS; i++) {
        const bench_row_t *const row = &BENCH_ROW_LIST[i];
        const bench_result_t *const r = &results[i];
        if (r->ran == 0u || row->render_distance != BENCH_DISTANCE_TARGET ||
            (only != nullptr && strstr(row->name, only) == nullptr)) {
            continue;
        }
        aimed++;
        met += r->met_fps != 0u ? 1u : 0u;
        if (worst_fps < 0.0f64 || r->fps < worst_fps) {
            worst_fps = r->fps;
            worst_row = row->name;
        }
    }
    if (aimed == 0u) {
        printf("target: no row that has one ran\n");
        return;
    }
    printf("target: %.0f fps at %ux%u, render distance %u, all effects: %" PRIu32 " of %" PRIu32
           " rows met it; slowest %s at %.1f fps\n",
           (libc_double_t)BENCH_TARGET_FPS, BENCH_WIDTH, BENCH_HEIGHT, BENCH_DISTANCE_TARGET, met, aimed, worst_row,
           (libc_double_t)worst_fps);
}

static bool bench_parse(int32_t argc, char **argv, bench_options_t *out, bool *out_list) {
    memset(out, 0, sizeof(*out));
    *out_list = false;
    for (int32_t i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--list") == 0) {
            *out_list = true;
        } else if (strcmp(argv[i], "--only") == 0 && has_value) {
            out->only = argv[++i];
        } else if (strcmp(argv[i], "--record") == 0 && has_value) {
            out->record = argv[++i];
        } else if (strcmp(argv[i], "--label") == 0 && has_value) {
            out->label = argv[++i];
        } else if (strcmp(argv[i], "--shots") == 0 && has_value) {
            out->shots = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0 && has_value) {
            out->trace = argv[++i];
        } else {
            return false;
        }
    }
    if (out->label != nullptr && strchr(out->label, '\t') != nullptr) {
        fprintf(stderr, "--label takes no tabs: the history is split on them\n");
        return false;
    }
    /* A world claims the profiler's lanes as it opens, and they are few. */
    if (out->trace != nullptr && (PROFILER_BUILT == 0 || bench_rows_chosen(out->only) != 1u)) {
        fprintf(stderr, "--trace needs a build with scopes to time, and an --only that picks one row\n");
        return false;
    }
    return true;
}

/* Everything for one row, in this process, which must hold nothing yet. Returns the exit status. */
static int32_t bench_process(const bench_options_t *options, uint32_t row, bool say_card, bench_result_t *result) {
    bench_t bench;
    memset(&bench, 0, sizeof(bench));
    if (!bench_memory(&bench)) {
        return 2;
    }
    const bool profiled = options->trace != nullptr;
    int32_t status = 2;
    if (bench_start(&bench, profiled)) {
        status = 0;
        const device_memory_t *const memory = &bench.borrowed_app->gpu_bridge.device_memory;
        if (say_card) {
            printf("build %s, seed %" PRIX64 ", %ux%u, card %s with %.0f MiB free at the start\n", bench_preset_name(),
                   (uint64_t)TERRAIN_SEED_DEFAULT, BENCH_WIDTH, BENCH_HEIGHT, memory->device_name,
                   (libc_double_t)bench_mib(memory->free_bytes_at_init));
            (void)fflush(stdout);
        }
        const bool ran = bench_row(&bench, &BENCH_ROW_LIST[row], options, profiled, result);
        status = ran && result->sound != 0u ? 0 : 1;
    }
    const bool gpu_clean = bench.started < 6u || gpu_bridge_exit_check(&bench.borrowed_app->gpu_bridge);
    bench_stop(&bench);
    if (profiled && bench.started >= 1u) {
        profiler_t *const profiler = &bench.borrowed_app->profiler;
        if (profiler_trace_dump(profiler, options->trace) != FILE_OK) {
            status = 1;
        }
        const file_t out_file = {.fd = STDOUT_FILENO, ._pad0 = {0}};
        (void)fflush(stdout);
        if (profiler_summary_write(profiler, &out_file) != FILE_OK) {
            status = 1;
        }
        profiler_deinit(profiler);
    }
    heap_release(&bench.owned_memory);
    const bool heap_clean = heap_exit_check();
    return gpu_clean && heap_clean ? status : 1;
}

/* One row in a process of its own, so that a row that dies costs that row and not the rest of a run of minutes, and nothing a world leaves behind
 * reaches the next. This process has no threads and no card, as a context does not survive a fork. A child that died has told it nothing, and
 * the row counts as not having run. */
static int32_t bench_row_forked(const bench_options_t *options, uint32_t row, bool say_card, bench_result_t *results) {
    int32_t ends[2];
    if (pipe(ends) != 0) {
        fprintf(stderr, "bench_world: no pipe for a row\n");
        return 2;
    }
    (void)fflush(stdout);
    (void)fflush(stderr);
    const pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "bench_world: no process for a row\n");
        (void)close(ends[0]);
        (void)close(ends[1]);
        return 2;
    }
    if (child == 0) {
        (void)close(ends[0]);
        const int32_t status = bench_process(options, row, say_card, &results[row]);
        const bool sent = write(ends[1], &results[row], sizeof(results[row])) == (int64_t)sizeof(results[row]);
        (void)fflush(stdout);
        (void)fflush(stderr);
        _exit(sent ? status : 1);
    }
    (void)close(ends[1]);
    bench_result_t received;
    const int64_t got = read(ends[0], &received, sizeof(received));
    (void)close(ends[0]);
    int32_t wait_status = 0;
    const pid_t waited = waitpid(child, &wait_status, 0);
    if (got == (int64_t)sizeof(received)) {
        results[row] = received;
    }
    if (waited != child || !WIFEXITED(wait_status)) {
        fprintf(stderr, "bench_world: %s's process did not finish\n", BENCH_ROW_LIST[row].name);
        return 1;
    }
    return WEXITSTATUS(wait_status);
}

int32_t main(int32_t argc, char **argv) {
    bench_options_t options;
    bool list = false;
    if (!bench_parse(argc, argv, &options, &list)) {
        bench_usage(argv[0]);
        return 2;
    }
    if (list) {
        for (uint32_t i = 0u; i < BENCH_ROWS; i++) {
            const bench_row_t *const row = &BENCH_ROW_LIST[i];
            printf("%-26s %-7s render distance %" PRIu32 "\n", row->name, bench_mode_name(row->mode),
                   row->render_distance);
        }
        return 0;
    }
    if (bench_rows_chosen(options.only) == 0u) {
        fprintf(stderr, "no row's name holds '%s': --list prints them\n", options.only);
        return 2;
    }
    if (options.shots != nullptr && mkdir(options.shots, 0755) != 0 && access(options.shots, W_OK) != 0) {
        fprintf(stderr, "could not make or write to %s\n", options.shots);
        return 2;
    }
    bench_result_t results[BENCH_ROWS];
    memset(results, 0, sizeof(results));
    const int64_t run_ns = time_now_ns();
    int32_t status = 0;
    bool said_card = false;
    for (uint32_t i = 0u; i < BENCH_ROWS; i++) {
        if (!bench_row_chosen(i, options.only)) {
            continue;
        }
        const int32_t row_status = bench_row_forked(&options, i, !said_card, results);
        said_card = true;
        status = row_status > status ? row_status : status;
        bench_row_print(&BENCH_ROW_LIST[i], &results[i]);
        (void)fflush(stdout);
    }
    bench_summary(results, options.only);
    printf("the whole run took %.1f s\n", (libc_double_t)(time_ns_to_ms(time_now_ns() - run_ns) / 1000.0f64));
    if (options.record != nullptr && !bench_record(results, &options)) {
        status = status > 1 ? status : 1;
    }
    return status;
}
