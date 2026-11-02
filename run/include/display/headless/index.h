/* See run/src/display/headless/README.md. */
#ifndef MC_RUN_DISPLAY_HEADLESS_INDEX_H
#define MC_RUN_DISPLAY_HEADLESS_INDEX_H

#include "arena/index.h"
#include "display/flip/index.h"
#include "display/headless/script/index.h"

#include <stdint.h>

/* What a run is. */
typedef struct display_headless_config_t {
    const char *script_path;    /* nullptr for no script: then tick_count says how long */
    const char *screenshot_dir; /* created if missing; a script that takes screenshots must have one */
    uint32_t width;
    uint32_t height;
    uint32_t tick_count; /* 0 to run until the script's last line. Refused if it would cut the script off */
    uint32_t tick_hz;    /* only what "500ms" in a script means: a run does not wait for the wall clock */
} display_headless_config_t;
static_assert(sizeof(display_headless_config_t) == 32);

/* The no-display backend. Zeroed means not started, so _deinit is safe either way. */
typedef struct display_headless_t {
    /* The one frame. The same type as a KMS back buffer, so that what draws here draws there. Its owned_map is memory
     * from the arena the parent gave, and the parent gets it back: nothing here unmaps it. */
    display_buffer_t frame;
    display_script_t script;
    display_script_state_t state;
    uint8_t *borrowed_png; /* from the arena; a screenshot's file is made here before it is written */
    uint64_t png_capacity;
    char *borrowed_path; /* from the arena; FILE_PATH_BYTES */
    const char *borrowed_screenshot_dir;
    uint64_t frames_presented;
    uint64_t screenshots_written;
    uint32_t tick_count;
    uint8_t _pad0[4];
} display_headless_t;
static_assert(sizeof(display_headless_t) == 296);

/* What a scene gets for one tick. */
typedef struct display_headless_tick_t {
    const display_script_event_t *events; /* this tick's, screenshots too, already in state; nullptr if none */
    const display_script_state_t *state;
    uint32_t event_count;
    uint32_t tick;
} display_headless_tick_t;
static_assert(sizeof(display_headless_tick_t) == 24);

/* What runs under the backend: the demo, or game with --test-world. Either function may be nullptr. Returning
 * false ends the run as failed, and the function has said why. */
typedef struct display_headless_scene_t {
    void *borrowed_context; /* passed through untouched */
    bool (*tick)(void *context, const display_headless_tick_t *tick);
    bool (*draw)(void *context, const display_headless_tick_t *tick, display_buffer_t *frame);
} display_headless_scene_t;
static_assert(sizeof(display_headless_scene_t) == 24);

/* How much arena display_headless_init will ask for, or 0 for a size that cannot be: see display_png_bytes_needed. */
uint64_t display_headless_arena_bytes(const display_headless_config_t *config);

/* Makes the frame, reads and parses the script, and makes the screenshot directory. Opens no display and no input
 * device. arena must hold display_headless_arena_bytes(config). On false it has logged why. */
bool display_headless_init(display_headless_t *headless, arena_t *arena, const display_headless_config_t *config);

/* Forgets everything. The memory goes back with the parent's arena. */
void display_headless_deinit(display_headless_t *headless);

/* Writes the frame as it is now to a PNG at path, atomically, so nothing can ever read half a file. Logs on failure. */
bool display_headless_screenshot(display_headless_t *headless, const char *path);

/* Runs every tick in order, as fast as it can: applies that tick's events, then scene->tick, then scene->draw, then
 * writes that tick's screenshots. Nothing reads a clock, so one script always gives the same frames. Returns false
 * if the scene failed or a screenshot could not be written. */
bool display_headless_run(display_headless_t *headless, const display_headless_scene_t *scene);

#endif
