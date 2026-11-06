/* See run/src/main/README.md. */
#ifndef MC_RUN_MAIN_INDEX_H
#define MC_RUN_MAIN_INDEX_H

#include "audio/index.h"
#include "climate/index.h"
#include "display/index.h"
#include "entity/index.h"
#include "mob/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "input/index.h"
#include "item/index.h"
#include "job/index.h"
#include "math/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "profiler/index.h"
#include "render/index.h"
#include "save/index.h"
#include "settings/index.h"
#include "simd/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <stdint.h>

/* The one root of ownership: plans/CODING_STANDARD.md section 4. There is one of these, on main's stack, and every heap
 * block in the program traces back to it through struct fields. Each system has its field here. */
typedef struct app_t {
    job_system_t job; /* first: its alignment is what gives app_t its own */
    simd_t simd;
    texture_t texture;
    ui_t ui; /* reads its textures at start; game draws the HUD and the game screens with it, and --menu its menus */
    world_t world;           /* nothing runs in it until game does (Stage 08.4) */
    terrain_t terrain;       /* the same */
    climate_t climate;       /* its region only: a world's menu session opens it over the terrain, game borrows it */
    physics_t physics;       /* borrowed by player, entity and the world's columns */
    player_t player;         /* the sim moves it */
    entity_t entity;         /* the sim's bodies, drops and arrows */
    mob_t mob;               /* the same */
    item_t item;             /* the same */
    gpu_bridge_t gpu_bridge; /* the default run and --headless start it, unless gpu.enabled says otherwise */
    render_t render;         /* takes nothing on the card until game opens a world */
    game_t game;             /* takes nothing until a world opens */
    display_t display;       /* whatever draws to a screen starts it, never --headless */
    display_offscreen_t offscreen; /* only --headless starts it */
    input_t input;                 /* only after display_init said OK */
    thread_topology_t topology;    /* read once, before main pins itself, and kept for the threads game starts */
    uint32_t cpu_required;         /* CPU_FEATURE_* the build uses */
    uint32_t cpu_present;          /* CPU_FEATURE_* this machine has */
    math_t math;
    audio_t
        audio; /* only a game with hands starts it, and then it takes its own region: nothing can stop the game for it */
    settings_t settings; /* read only for --menu, which is the default: everything else runs on its flags */
    save_t save;         /* only --menu, the default, has worlds to keep */
    profiler_t profiler; /* only --profile starts it, first of all, and it stops last: job and game hold lanes in it */
} app_t;
static_assert(sizeof(app_t) == 16128);
static_assert(alignof(app_t) == 64);

/* The exit codes of minecraft_c. */
typedef enum main_exit_t : uint8_t {
    MAIN_EXIT_OK = 0,
    MAIN_EXIT_USAGE = 2,
    MAIN_EXIT_CPU_MISMATCH = 3, /* the early guard in asm/src/cpuid/ uses this number too */
    MAIN_EXIT_LEAK = 4,
    MAIN_EXIT_RUNTIME = 5,         /* the core runtime would not start, or did not do what it was given */
    MAIN_EXIT_DISPLAY_REFUSED = 6, /* the display is not ours to take: nothing on it was changed */
    MAIN_EXIT_DISPLAY_ERROR = 7,   /* KMS failed partway; the console was given back */
    MAIN_EXIT_HEADLESS = 8,        /* --headless: a bad script, or a screenshot that could not be written */
    MAIN_EXIT_GPU_LOST =
        9, /* the card went during the run. The world was saved if the log says so, and the console was given back */
} main_exit_t;

#endif
