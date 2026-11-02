/* See run/src/game/README.md. */
#ifndef MC_RUN_GAME_INDEX_H
#define MC_RUN_GAME_INDEX_H

#include "arena/index.h"
#include "display/index.h"
#include "entity/index.h"
#include "heap/index.h"
#include "input/index.h"
#include "item/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "player/index.h"
#include "render/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Columns being generated at once. One worker's worth each, with some waiting so that no worker goes idle. */
    GAME_STREAM_JOBS = 64u,
};

/* How long a day is, as a familiar game has it: 24000 ticks at 20 Hz. */
#define GAME_DAY_SECONDS 1200.0f64
/* Where a world's first day starts: mid-morning, with the sun well up and shadows to see. */
#define GAME_DAY_START 0.35f64

/* What a world is opened with. */
typedef struct game_config_t {
    uint32_t render_distance; /* 0: in chunks */
    uint32_t width;           /* 4: of the picture */
    uint32_t height;          /* 8 */
    uint16_t features_off;    /* 12: SHADE_FEATURE_* to leave off; 0 draws all of SHADE_FEATURES_PLAY */
    uint8_t god_rays;         /* 14: and these on top, being optional */
    uint8_t _pad0[1];         /* 15 */
    /* 16: how far the world is drawn block by block, in chunks. Past it, to render_distance, coarse grids take over. 0 means
     * RENDER_LOD_FULL_DETAIL_DEFAULT. */
    uint32_t full_detail;
} game_config_t;
static_assert(sizeof(game_config_t) == 20);

/* One column being made on a worker. Its own cache line: a worker writes done while the sim thread polls it. */
typedef struct game_column_job_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) const terrain_t *borrowed_terrain; /* 0 */
    uint16_t *borrowed_states;                                          /* 8: a whole column's */
    uint8_t *borrowed_opened;    /* 16: a byte per voxel, for terrain to work in */
    world_map_request_t request; /* 24 */
    _Atomic uint32_t done;       /* 40: release when finished */
    uint8_t in_use;              /* 44 */
    uint8_t _pad0[19];           /* 45 */
} game_column_job_t;
static_assert(sizeof(game_column_job_t) == MC_CPU_CACHE_LINE_BYTES);

/* The game: what the sim, gpu and main threads share, and the three arenas whose lifetimes it decides. There is one, under
 * app_t. Everything past the borrowed pointers is zero bytes outside game_open. */
typedef struct game_t {
    arena_t arena_world;                                 /* 0: World: reset at game_close */
    arena_t arena_tick;                                  /* 64: Tick: reset at the start of every sim tick */
    arena_t arena_frame;                                 /* 128: Frame: reset at the start of every frame */
    job_group_t group;                                   /* 192: the columns on workers */
    render_exchange_t cameras;                           /* 256: main to gpu, over camera_slots */
    render_camera_t camera_slots[RENDER_EXCHANGE_SLOTS]; /* 448 */
    render_camera_t camera;                              /* 568: main's, or the one thread's in a run with no threads */
    heap_region_t owned_region;                          /* 608 */
    thread_t sim_thread;                                 /* 624 */
    thread_t gpu_thread;                                 /* 672 */
    world_t *borrowed_world;                             /* 720 */
    terrain_t *borrowed_terrain;                         /* 728 */
    render_t *borrowed_render;                           /* 736 */
    job_system_t *borrowed_job;                          /* 744 */
    const texture_t *borrowed_texture;                   /* 752 */
    const thread_topology_t *borrowed_topology;          /* 760 */
    input_t *borrowed_input;                             /* 768: only while the threads run from real input */
    game_column_job_t *borrowed_jobs;                    /* 776: GAME_STREAM_JOBS */
    const uint8_t *borrowed_last_picture;                /* 784: main's: what to show until a newer one is ready */
    int64_t last_frame_ns;                               /* 792: main's */
    int64_t loop_started_ns;                             /* 800: main's */
    uint64_t pictures_shown;                             /* 808: main's: new pictures it has put on the screen */
    _Atomic int32_t center_x;                            /* 816: main to sim: the chunk the eye is in */
    _Atomic int32_t center_y;                            /* 820 */
    _Atomic uint32_t stop;                               /* 824: main to sim and gpu */
    _Atomic uint32_t failed; /* 828: sim or gpu to main: the card failed, and it has said why */
    /* A line of its own from here: the sim writes these all the time, and main writes the line above every frame. */
    uint64_t tick_count;       /* 832: the sim's */
    uint64_t columns_made;     /* 840: the sim's */
    int64_t loop_ended_ns;     /* 848: main's, once the threads have stopped */
    uint32_t jobs_in_use;      /* 856 */
    uint8_t open;              /* 860 */
    uint8_t threads_running;   /* 861 */
    uint8_t stream_quiet;      /* 862: nothing more to generate, last time the sim looked */
    uint8_t pool_full_said;    /* 863 */
    player_t *borrowed_player; /* 864: walking if not nullptr; set before a world opens, and only read after */
    uint32_t commit_next;      /* 872: the job the next step looks at first */
    uint8_t _pad0[4];          /* 876 */
    entity_t *borrowed_entity; /* 880: what a player's drops go into; set before a world opens, and only read after */
    item_t *borrowed_item;     /* 888: what a player carries; set before a world opens, and only read after */
    /* A line of its own, written by main every frame. */
    float64_t day_fraction; /* 896: main's: the time of day, from 0 to 1 */
    _Atomic uint32_t
        day_units;     /* 904: main to gpu: the same, in 1/2^32 of a day, relaxed: only the look depends on it */
    uint8_t _pad1[52]; /* 908 */
    /* A line of its own from here: the gpu thread writes these, and main what is after them. */
    render_camera_t picture_cameras[RENDER_EXCHANGE_SLOTS]; /* 960: what the picture in each slot was drawn from */
    uint8_t _pad2[8];                                       /* 1080 */
    render_camera_t shown_camera; /* 1088: main's: what borrowed_last_picture was drawn from */
    uint8_t _pad3[24];            /* 1128 */
} game_t;
static_assert(sizeof(game_t) == 1152);
static_assert(alignof(game_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_t, group) == 192);
static_assert(offsetof(game_t, cameras) == 256);
static_assert(offsetof(game_t, owned_region) == 608);
static_assert(offsetof(game_t, center_x) == 816);
static_assert(offsetof(game_t, tick_count) == 832);
static_assert(offsetof(game_t, day_fraction) == 896);

/* Remembers what it will run on, and takes nothing. All must outlive it, and topology must have been read before the caller
 * pinned itself. */
void game_init(game_t *game, world_t *world, terrain_t *terrain, render_t *render, job_system_t *job,
               const texture_t *texture, const thread_topology_t *topology);

/* From here on a world is walked through and not flown through, by this player, which must outlive it. Before game_open. */
void game_player_set(game_t *game, player_t *player);

/* From here on what a player breaks comes out as items in the world, which it can pick up, and these open and close with the world.
 * Must outlive it. Before game_open. Without it the drops stay in the player's ring, and nothing reaches its hand. */
void game_entity_set(game_t *game, entity_t *entity);

/* From here on what a player picks up goes into this inventory, the hotbar slot in hand is what it places, and these open and close
 * with the world. Must outlive it. Before game_open. Without it what is picked up goes straight to the player's one-slot hand. */
void game_item_set(game_t *game, item_t *item);

/* Stops whatever is running, and closes any world. Safe on a zeroed game_t. */
void game_deinit(game_t *game);

/* Opens a world around the place the eye starts: its map, its blocks on the card, its pictures. On failure logs why, leaves
 * nothing behind and returns false. */
[[nodiscard]] bool game_open(game_t *game, const game_config_t *config);

/* Gives it all back. Safe if none is open. The threads must have stopped. */
void game_close(game_t *game);

/* One step of the sim's: what has been generated goes into the world, what is wanted next goes to workers, what has left goes,
 * and the world as it now is goes out as a snapshot. Never waits for a worker. The one thread holding the map. */
void game_sim_step(game_t *game);

/* Sets the time of day: see render_frame_time_set. Before the threads start, or from the thread that owns input. With
 * SHADE_FEATURE_DAY_CYCLE on, it goes on from here by a day per GAME_DAY_SECONDS of what game_loop_frame and the headless scene are
 * told has passed, and with it off it stays. */
void game_day_set(game_t *game, float64_t day_fraction);

/* Moves the eye. The thread that owns input. */
void game_fly(game_t *game, const render_fly_input_t *input, float32_t seconds);

/* Runs the sim and the upload queue, on this thread, until everything the eye's place wants is on the card. A run with no
 * threads calls this, so that a picture never depends on how fast a worker was. False if the card failed, having said why. */
[[nodiscard]] bool game_settle(game_t *game);

/* The scene display_headless_run drives: tick flies the camera from the script, and draw settles the world and puts a whole
 * frame into the buffer. One thread, nothing reads a clock, and one script gives one set of pictures. */
void game_headless_scene(game_t *game, display_headless_scene_t *out);

/* The real loop's threads. game_loop_start starts sim and gpu, pinned where config/machine.yaml says. Then the main thread calls
 * game_loop_frame once per frame, which moves the eye, tells the others, and puts the newest finished picture in target. With a
 * player the eye goes where the last ticks say, between them, and input is not looked at. It never waits for either. game_loop_stop stops and joins them if they are running, and says whether they ran clean. input may be
 * nullptr, for a caller that polls its own. */
[[nodiscard]] bool game_loop_start(game_t *game, input_t *input);
[[nodiscard]] bool game_loop_frame(game_t *game, const render_fly_input_t *input, float32_t seconds,
                                   display_buffer_t *target);
[[nodiscard]] bool game_loop_stop(game_t *game);

/* What display_frames_run calls, with a game_t * as context, after game_loop_source_begin: on its first call it starts the
 * threads, which is after display_frames_run has forked its guardian; then one poll of the real keys and mouse, Esc to end the
 * run, and game_loop_frame by the clock. game_loop_stop still ends it, and says if the threads could not start. */
void game_loop_source_begin(game_t *game, input_t *input);
bool game_loop_source(void *context, display_buffer_t *back);

/* What display_frames_run calls on coming back from another VT, with a game_t * as context: nothing typed there counts here. */
void game_loop_discard(void *context);

/* Walking: the mouse looks at once, and the keys go to the sim thread for its next tick. Forward, right and fast mean what they
 * do for flying, and fast is a sprint. The thread that owns input. A caller of game_loop_frame calls this first, and
 * game_loop_source does. */
void game_walk_send(game_t *game, const render_fly_input_t *input, bool jump, bool sneak, int64_t now_ns);

/* Walking: what the left and right mouse buttons are doing, which mine and place on the sim thread's next tick. The thread that owns
 * input, right after game_walk_send. */
void game_hands_send(game_t *game, bool attack, bool place);

/* Walking: the number keys choose a hotbar slot, and the wheel moves along it, as of the sim thread's next tick. The thread that owns
 * input. Away from the user is to the left, as in Minecraft. Does nothing with no inventory. */
void game_hotbar_send(game_t *game, const bool keys[INVENTORY_HOTBAR_SLOTS], int64_t wheel);

/* Turning either kind of input into a step of flying. */
void game_fly_from_script(const display_script_state_t *state, render_fly_input_t *out);
void game_fly_from_input(const input_frame_t *frame, render_fly_input_t *out);

/* Logs what the frames took, for the end of a run. */
void game_report(game_t *game);

#endif
