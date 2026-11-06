/* See run/src/game/README.md. */
#ifndef MC_RUN_GAME_INDEX_H
#define MC_RUN_GAME_INDEX_H

#include "arena/index.h"
#include "audio/index.h"
#include "calendar/index.h"
#include "climate/index.h"
#include "display/index.h"
#include "entity/index.h"
#include "mob/index.h"
#include "heap/index.h"
#include "input/index.h"
#include "item/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "mob/spawn/index.h"
#include "player/index.h"
#include "profiler/index.h"
#include "render/index.h"
#include "save/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "ui/index.h"
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
    uint32_t features_off;    /* 12: SHADE_FEATURE_* to leave off; 0 draws all of SHADE_FEATURES_PLAY */
    /* 16: how far the world is drawn block by block, in chunks. Past it, to render_distance, coarse grids take over. 0 means
     * RENDER_LOD_FULL_DETAIL_DEFAULT. */
    uint32_t full_detail;
    /* 20: how far full detail may ever reach in this world without opening it again, in chunks, since the table on the card is sized once.
     * 0 means only what this config asks for. */
    uint32_t table_chunks;
    uint32_t fov_degrees;         /* 24: the vertical field of view; 0 means RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT */
    uint32_t sensitivity_percent; /* 28: of RENDER_CAMERA_RADIANS_PER_COUNT; 0 means 100 */
    uint32_t
        render_scale_percent; /* 32: pictures are drawn this much of width and height, and brought up; 0 means 100 */
    uint8_t uncapped; /* 36: the gpu thread draws as fast as it can, and does not wait for a picture to be taken */
    uint8_t show_coordinates; /* 37: x, y and z in the top left corner of what a body sees */
    uint8_t god_rays;         /* 38: on top of what features_off leaves, being optional */
    uint8_t show_date;        /* 39: the date line in the top right corner of what a body sees */
} game_config_t;
static_assert(sizeof(game_config_t) == 40);

/* What a config comes to, for a table of a given size. */
typedef struct game_view_t {
    render_lod_plan_t lod; /* 0: no levels if there is no far field */
    uint32_t full_detail;  /* 16: the map's view radius */
    uint32_t width;        /* 20: what is drawn, before it is brought up to the config's */
    uint32_t height;       /* 24 */
    float32_t fog_end;     /* 28: in blocks */
    uint32_t features;     /* 32: SHADE_FEATURE_* */
    uint8_t _pad0[4];      /* 36 */
} game_view_t;
static_assert(sizeof(game_view_t) == 40);

/* One column being made on a worker. Its own cache line: a worker writes done while the sim thread polls it. */
typedef struct game_column_job_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) const terrain_t *borrowed_terrain; /* 0 */
    uint16_t *borrowed_states;                                          /* 8: a whole column's */
    uint8_t *borrowed_opened;     /* 16: a byte per voxel, for terrain to work in */
    world_map_request_t request;  /* 24 */
    _Atomic uint32_t done;        /* 40: release when finished */
    uint8_t in_use;               /* 44 */
    uint8_t _pad0[3];             /* 45 */
    save_world_t *borrowed_save;  /* 48: what has changed there, or nullptr */
    column_mask_t levels_mask;    /* 56: which sections the save gave levels for, in borrowed_opened past its scratch */
    column_mask_t saved_sections; /* 72: which sections the save gave at all */
    uint8_t _pad1[40];            /* 88 */
} game_column_job_t;
static_assert(sizeof(game_column_job_t) == 2u * MC_CPU_CACHE_LINE_BYTES);

enum : int32_t {
    /* The x and the y of the column a new world starts looking for its body's place from. */
    GAME_WORLD_MIDDLE = 8,
};

/* Plan 004 phase 07's work for one census week, run on the sim thread, in order, once per week. fast is plan 004 section
 * 4.2's fast mode, for weeks more than CALENDAR_FULL_WEEKS_MAX late. */
typedef void game_census_week_fn(void *context, uint64_t week, bool fast);

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
    uint8_t use_was_held;      /* 876: the sim's: the right button, last tick */
    uint8_t save_failed_said;  /* 877: the sim's: saving a column on its way out failed, and it has said so */
    /* 878: a census stops the world and waits for its screen; set before a world opens, and only read after */
    uint8_t census_screens;
    uint8_t open_percent; /* 879: how much of a new world's climate is worked out, 0 to 100, while game_open runs */
    entity_t *borrowed_entity; /* 880: what a player's drops go into; set before a world opens, and only read after */
    item_t *borrowed_item;     /* 888: what a player carries; set before a world opens, and only read after */
    /* A line of its own, written by main every frame. */
    float64_t day_fraction; /* 896: main's: the time of day, from 0 to 1 */
    _Atomic uint32_t
        day_units; /* 904: main to gpu and sim: the same, in 1/2^32 of a day, relaxed: the look and whether a bed works depend on it, and neither to the tick */
    _Atomic uint32_t
        morning_latch; /* 908: sim to main: a night was slept through, so the day goes on from morning; relaxed */
    /* 912: main to sim, and the sim to itself as a census opens: no ticks while it is set, and streaming goes on; relaxed */
    _Atomic uint32_t paused;
    uint32_t day_zero; /* 916: D0 of this world; set as it opens, before any thread, and only read after */
    /* The sim's: a tree on its way down, from arena_world while a world is open. Only the pointer's here, as this is under app_t on main's stack. */
    player_felling_t *borrowed_felling; /* 920 */
    player_sounds_t
        *borrowed_sounds; /* 928: the same: what the hands did that can be heard, and each tick it is taken */
    /* 936: main to gpu and sim: clock_days << 32 | day_units, a calendar_clock_t; relaxed, as only the date depends on it */
    _Atomic uint64_t clock_units;
    uint64_t clock_days; /* 944: main's: whole days since the midnight before the world's first morning */
    /* 952: sim to main: the week whose census screen should open, plus 1; 0 for none. Release, and taken by an acquire exchange */
    _Atomic uint64_t census_latch;
    /* A line of its own from here: the gpu thread writes these, and main what is after them. */
    render_camera_t picture_cameras[RENDER_EXCHANGE_SLOTS]; /* 960: what the picture in each slot was drawn from */
    /* 1080: plan 004's climate cells, or nullptr for none; set before a world opens, and only read after */
    climate_t *borrowed_climate;
    render_camera_t shown_camera;      /* 1088: main's: what borrowed_last_picture was drawn from */
    void (*open_alive)(void *context); /* 1128: called now and then during a long game_open; nullptr for nobody */
    void *borrowed_open_alive_context; /* 1136 */
    /* Read once a tick by the sim. Set before a world opens, so no write of main's touches it, only its line. */
    mob_t *borrowed_mob; /* 1144: what asks for paths; set before a world opens, and only read after */
    /* What the HUD shows. All relaxed: only what is on the screen depends on it, each word stays whole, and a frame of a tick-old slot
     * beside a new one is gone by the next. A line of its own from here: the sim writes these, once a tick. */
    _Atomic uint64_t hud_hotbar[INVENTORY_HOTBAR_SLOTS]; /* 1152: sim to main: each an item_stack_t's bytes */
    _Atomic uint32_t hud_selected;                       /* 1224: sim to main: the hotbar slot in hand */
    _Atomic uint32_t hud_tick_us;                        /* 1228: sim to main: what its last tick took */
    profiler_lane_t *borrowed_lane_sim;                  /* 1232: whichever thread is doing the sim's work now */
    profiler_lane_t *borrowed_lane_sim_thread;           /* 1240: that of the sim thread, while it runs */
    _Atomic int32_t hud_block[3];     /* 1248: sim to main: the block the body's feet are in, z from sea level */
    uint8_t _pad4[4];                 /* 1260 */
    game_census_week_fn *census_week; /* 1264: nullptr for none; set before a world opens, and only read after */
    void *borrowed_census_context;    /* 1272: passed to census_week */
    /* A line of its own: the gpu thread writes these, once a picture. */
    _Atomic uint32_t hud_pass_us[FRAME_PASS_COUNT]; /* 1280: gpu to main: the card's clock, on the last picture */
    _Atomic uint32_t hud_total_us;                  /* 1300 */
    profiler_lane_t *borrowed_lane_gpu;             /* 1304: whichever thread is doing the gpu's work now */
    profiler_lane_t *borrowed_lane_gpu_thread;      /* 1312: that of the gpu thread, while it runs */
    profiler_lane_t *borrowed_lane_card;            /* 1320: the kernels, written by whichever that is */
    _Atomic uint64_t gpu_beats;    /* 1328: gpu to main, relaxed: once round its loop, picture or no picture */
    _Atomic uint32_t gpu_failure;  /* 1336: gpu to main, read under failed: a gpu_status_t, OK until something failed */
    uint32_t gpu_pretend_pictures; /* 1340: pictures until the card goes, on purpose; 0 for never. Whoever draws */
    /* A line of its own: main's. */
    ui_t *borrowed_ui;         /* 1344: draws on each picture shown if not nullptr; set before a world opens */
    float64_t hud_fps_seconds; /* 1352: of the window being counted */
    float32_t hud_fps;         /* 1360: new pictures a second, over the last whole window */
    uint32_t hud_fps_pictures; /* 1364: new pictures in the window being counted */
    uint32_t hud_compose_us;   /* 1368: what drawing on the last picture took */
    uint32_t hud_present_us;   /* 1372: what copying it out took */
    uint8_t hud_debug;         /* 1376: the debug overlay is up */
    uint8_t hud_debug_key;     /* 1377: its key was down, last time anyone looked */
    uint8_t _pad6[2];          /* 1378 */
    uint32_t
        census_runs; /* 1380: the sim's, on a census only: weeks run since the world opened, for tests and the log */
    profiler_lane_t *borrowed_lane_main; /* 1384: this thread's, and the others' while there are no threads */
    uint64_t gpu_beats_seen;             /* 1392: main's */
    int64_t gpu_beats_seen_ns;           /* 1400: main's: when that last changed */
    /* A line of its own from here: the sim's. */
    mob_spawn_t mob_spawn; /* 1408: which mobs come and go in the entity store; only with a borrowed_entity */
    save_t *borrowed_save; /* 1448: where a world's changes go; set before a world opens, and only read after */
    audio_t *
        borrowed_audio; /* 1456: what the hands did is posted here, or nullptr; set before a world opens, and only read after */
    uint64_t last_census_week; /* 1464: the sim's: the newest week a census has run for; saved */
    /* A line of its own and some: main's, but for uncapped. */
    heap_region_t owned_view; /* 1472: a picture at the display's size, once something has needed one */
    game_config_t config;     /* 1488: what this world is running with now */
    game_view_t view;         /* 1528: what that came to */
    float64_t
        look_rest[2]; /* 1568: what the mouse's counts left over, past whole ones, at a sensitivity that is not 100 */
    _Atomic uint32_t uncapped; /* 1584: main to gpu: relaxed, as a picture more or less depends on it */
    uint8_t view_held;         /* 1588: owned_view holds the last picture, as the frame it came from has gone */
    uint8_t view_stale;        /* 1589: with no threads: a config changed, and nothing has been drawn since */
    uint8_t gpu_stuck;         /* 1590: main's: the gpu thread has stopped answering, and will not be joined */
    uint8_t gpu_pretend_stuck; /* 1591: when the pictures run out the thread stops answering, in place of a fault */
    int64_t gpu_watched_ns;    /* 1592: main's: when it last looked */
} game_t;
static_assert(sizeof(game_t) == 1600);
static_assert(alignof(game_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_t, group) == 192);
static_assert(offsetof(game_t, cameras) == 256);
static_assert(offsetof(game_t, owned_region) == 608);
static_assert(offsetof(game_t, center_x) == 816);
static_assert(offsetof(game_t, tick_count) == 832);
static_assert(offsetof(game_t, day_fraction) == 896);
static_assert(offsetof(game_t, hud_hotbar) == 1152);
static_assert(offsetof(game_t, hud_pass_us) == 1280);
static_assert(offsetof(game_t, borrowed_ui) == 1344);
static_assert(offsetof(game_t, mob_spawn) == 1408);
static_assert(offsetof(game_t, census_screens) == 878);
static_assert(offsetof(game_t, day_zero) == 916);
static_assert(offsetof(game_t, clock_units) == 936);
static_assert(offsetof(game_t, census_latch) == 952);
static_assert(offsetof(game_t, census_week) == 1264);
static_assert(offsetof(game_t, census_runs) == 1380);
static_assert(offsetof(game_t, last_census_week) == 1464);

/* Remembers what it will run on, and takes nothing. All must outlive it, and topology must have been read before the caller
 * pinned itself. */
void game_init(game_t *game, world_t *world, terrain_t *terrain, render_t *render, job_system_t *job,
               const texture_t *texture, const thread_topology_t *topology);

/* From here on a world is walked through and not flown through, by this player, which must outlive it. Before game_open. */
void game_player_set(game_t *game, player_t *player);

/* From here on what a player breaks comes out as items in the world, which it can pick up, and these open and close with the world.
 * Must outlive it. Before game_open. Without it the drops stay in the player's ring, and nothing reaches its hand. */
void game_entity_set(game_t *game, entity_t *entity);

/* From here on the minds of what walks the world open and close with it, and have a tick before the bodies move. Must outlive it. Before
 * game_open. */
void game_mob_set(game_t *game, mob_t *mob);

/* From here on what a player picks up goes into this inventory, the hotbar slot in hand is what it places, and these open and close
 * with the world. Must outlive it. Before game_open. Without it what is picked up goes straight to the player's one-slot hand. */
void game_item_set(game_t *game, item_t *item);

/* From here on each picture shown has the HUD drawn on a copy of it with this, if a player is walking, and the debug overlay if asked
 * for, and E, and the right button on a crafting table, furnace or chest, bring up the game screens on it. Must outlive it. Before
 * game_open. Without it pictures go out as they were drawn. The screens take a game_item_set and a game_player_set as well, and without
 * them there are none. */
void game_ui_set(game_t *game, ui_t *ui);

/* From here on, a column that comes in has what a world open in this holds for it over what generates there, and one that goes out leaves its
 * changes with it; a game_open takes up the player, inventory, entities and time where a world open in this left them, and from then on all of
 * it is saved in the background every SAVE_AUTOSAVE_INTERVAL_TICKS. With no world open in it, none of that happens. Must outlive it. Before
 * game_open. */
void game_save_set(game_t *game, save_t *save);

/* Where what a player's hands did is heard. A run that does not call this is silent. Call it before a world opens: only the sim reads it after. */
void game_audio_set(game_t *game, audio_t *audio);

/* From here on a world has climate cells (plan 004 phase 03): whoever opens the world opens this climate over its terrain
 * first. game_open works out the tiles round the place the eye starts before it returns, telling open_alive as it goes;
 * the sim keeps tiles coming ahead of the player; and no column is asked for until its tiles are built. game_close waits
 * for the climate's jobs and closes it. Must outlive it. Before game_open. Without it, or with it closed, none of that. */
void game_climate_set(game_t *game, climate_t *climate);

/* From here on what its threads do, and what the card does, is timed in this profiler's lanes. The profiler must outlive it. Before
 * game_open. Without it, nothing is timed. */
void game_profiler_set(game_t *game, profiler_t *profiler);

/* Stops whatever is running, and closes any world. Safe on a zeroed game_t. */
void game_deinit(game_t *game);

/* All there is, saved now, on this thread: the threads must have stopped, and the world be open. SAVE_WORLD_OK if there is no save to make, or
 * it is all on disk. */
[[nodiscard]] save_world_status_t game_save_finish(game_t *game);

/* What the far field shows may have changed: every column captured for a save since this was last called is made again. By whichever thread calls render_lod_frame,
 * just before it. */
void game_far_changes_take(game_t *game);

/* Someone to tell, from the thread calling it, that a game_open that may take seconds is still going: looking for dry land in an
 * ocean does. A frame source that opens a world needs this, for vt_guard's watchdog and for a VT switch asked for meanwhile. nullptr for
 * nobody. */
void game_open_alive_set(game_t *game, void (*alive)(void *context), void *context);

/* Tells them, if anyone is listening. For whatever else takes long inside a frame. */
void game_open_alive_tell(const game_t *game);

/* A failure from the card, from whichever thread was talking to it. Before failed is set, for whoever reads this under it. */
void game_gpu_failed_note(game_t *game, gpu_status_t status);

/* Main's, once a frame with the threads running: whether the gpu thread still goes round. A card that hangs and returns nothing can only be seen
 * like this. False once it has been silent for GAME_GPU_STUCK_NS, having said so, and with failed set. */
bool game_gpu_alive(game_t *game);

/* The card has gone, for this process: nothing more will be drawn, and only ending the process and starting another can change that. What is on
 * the CPU's side is whole, so the world can still be saved. */
[[nodiscard]] bool game_gpu_lost(const game_t *game);

/* The card has gone with a thread inside a call that does not return. The world can still be saved, as nothing the save needs is that thread's, but
 * nothing it may be using can be given back: game_close must not run, and the process ends with _exit. */
[[nodiscard]] bool game_gpu_stuck(const game_t *game);

/* After this many pictures the card goes for real, or if stuck, the thread that draws stops answering for ever: --pretend-gpu-lost and tests. Before any
 * threads start. Stuck is only for a game with threads, as with none nothing is left to notice. */
void game_gpu_pretend_set(game_t *game, uint32_t pictures, bool stuck);

/* Opens a world around the place the eye starts: its map, its blocks on the card, its pictures. On failure logs why, leaves
 * nothing behind and returns false. */
[[nodiscard]] bool game_open(game_t *game, const game_config_t *config);

/* Gives it all back. Safe if none is open. The threads must have stopped. */
void game_close(game_t *game);

/* The world goes on with this config, the same size as it opened with, without closing. The player, the columns that are still in reach and
 * every edit in them stay. Render distance, effects, field of view, sensitivity, render scale and uncapped all take at once, and full detail as
 * far as table_chunks reaches. The thread that owns input, between frames: anything that needs them stopped stops the threads and starts them
 * again. Takes as long as the card does to give back and take a far field, and each frame after goes on filling it in. False, having said why,
 * only if the card or a thread failed, which nothing more should be asked of. */
[[nodiscard]] bool game_reconfigure(game_t *game, const game_config_t *config);

/* What a changed config has made of the picture, for one with no game_loop_frame to show it, as behind a screen in a pause. With threads it
 * takes whatever is the newest. With none it settles and draws one, if a config has changed since the last. False if the card failed, having
 * said why. */
[[nodiscard]] bool game_view_refresh(game_t *game);

/* The last picture, at the config's size, or nullptr if there is none yet. Good until the next call to anything that shows a picture. */
const uint8_t *game_picture_held(game_t *game);

/* One step of the sim's: what has been generated goes into the world, what is wanted next goes to workers, what has left goes,
 * and the world as it now is goes out as a snapshot. Never waits for a worker. The one thread holding the map. */
void game_sim_step(game_t *game);

/* Sets the time of day: see render_frame_time_set. Before the threads start, or from the thread that owns input. With
 * SHADE_FEATURE_DAY_CYCLE on, it goes on from here by a day per GAME_DAY_SECONDS of what game_loop_frame and the headless scene are
 * told has passed, and with it off it stays, until a night is slept through. */
void game_day_set(game_t *game, float64_t day_fraction);

/* Sets the whole world clock: clock_days whole days since the midnight before the world's first morning, and the time of day,
 * from 0 to 1. game_day_set leaves the days alone. The thread that owns input, or before the threads start. clock_days must be
 * below 2^32. */
void game_clock_set(game_t *game, uint64_t clock_days, float64_t day_fraction);

/* From here on a census stops the world and waits: game_census_take says when. Without it a census runs and the world goes on.
 * Before game_open. */
void game_census_screens_set(game_t *game, bool screens);

/* What a census runs for each week: plan 004 phase 07's. nullptr for nothing. Before game_open. */
void game_census_hook_set(game_t *game, game_census_week_fn *week, void *context);

/* True once for each census that has stopped the world, with the week it was for. Main, any time: an exchange, so never
 * twice. */
bool game_census_take(game_t *game, uint64_t *out_week);

/* Main's clock as a date, and the date line for the HUD at the eye's place: its season is named for the half of the world
 * the body or the eye is in, y >= 0 north, until plan 004 phase 02 brings latitude. */
calendar_date_t game_calendar_date(const game_t *game);
bool game_calendar_south(const game_t *game);
void game_calendar_line(const game_t *game, char *out, uint64_t bytes);

/* Moves the eye. The thread that owns input. */
void game_fly(game_t *game, const render_fly_input_t *input, float32_t seconds);

/* One tick of the world: run/src/game/tick/. The sim thread, or the one thread of a run with none. */
void game_sim_tick(game_t *game, int64_t tick_ns);
/* Tells main where the body is now: each tick, and once as a world opens, so that no picture says 0, 0, 0 before the first tick. The sim's, or before
 * it runs, whoever is opening the world. */
void game_hud_position_publish(game_t *game);

/* What a tick or a screen could not keep goes on the ground at this cell. The sim's. */
void game_spill(void *borrowed_game, const item_stack_t *stack, const int32_t cell[3]);

/* What a screen is told of the sim, this moment. The sim's. */
void game_screen_sim(game_t *game, ui_screen_sim_t *out);

/* Whether a game has screens at all: they move what is carried, so they need something carried, and a body to carry it. */
[[nodiscard]] bool game_has_screens(const game_t *game);

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

/* What game_loop_source does after the poll and the Esc, for a caller that wants Esc for itself: threads_ensure on each frame
 * first, then one input_frame_t to the player and the eye, and game_loop_frame by the clock. */
[[nodiscard]] bool game_loop_threads_ensure(game_t *game);
[[nodiscard]] bool game_loop_input_frame(game_t *game, const input_frame_t *frame, display_buffer_t *back);

/* Walking: nothing is held and no button is down, as far as the sim's next tick knows. The thread that owns input. */
void game_loop_input_clear(game_t *game);

/* Stops the world's clock, or starts it again: no ticks and no time of day, while columns go on streaming so that a pause does
 * not leave holes. A caller that pauses does not call game_loop_frame, or the headless scene's tick, until it is over. The time
 * in between does not count as time spent, since the last frame's clock starts again here. */
void game_pause_set(game_t *game, bool paused);

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

/* Whether the debug overlay's key is down now: each press turns the overlay on or off. The thread that owns input. */
void game_debug_send(game_t *game, bool down);

/* What the HUD and the debug overlay show now, from what the other threads last said. The main thread. Never waits. */
void game_hud_state_read(const game_t *game, ui_hud_state_t *out);
/* The whole block the body's feet are in, or with no body the block the flying eye is in; z counts from sea level. */
void game_hud_block_read(const game_t *game, int32_t out[3]);
void game_hud_debug_read(const game_t *game, ui_hud_debug_t *out);

/* One poll's input for the game screens. True if one is up, which has this input: the caller then sends the player no keys or buttons,
 * and Esc does not end the run. The thread that owns input, before game_walk_send. */
bool game_screen_input(game_t *game, const input_frame_t *frame);

/* Whether a game screen is up, which has Esc if so. The thread that owns input, before that poll's game_screen_input. */
bool game_screen_up(game_t *game);

/* What a script's tick looks like as a poll of real input. */
void game_input_from_script(const display_headless_tick_t *tick, input_frame_t *out);

/* Turning either kind of input into a step of flying. */
void game_fly_from_script(const display_script_state_t *state, render_fly_input_t *out);
void game_fly_from_input(const input_frame_t *frame, render_fly_input_t *out);

/* Logs what the frames took, for the end of a run. */
void game_report(game_t *game);

#endif
