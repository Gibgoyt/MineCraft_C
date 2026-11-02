/* See run/src/display/README.md. */
#ifndef MC_RUN_DISPLAY_INDEX_H
#define MC_RUN_DISPLAY_INDEX_H

#include "arena/index.h"
#include "display/device/index.h"
#include "display/flip/index.h"
#include "display/mode/index.h"
#include "display/vt_guard/index.h"
#include "heap/index.h"

#include <stdint.h>

enum : int64_t {
    /* How long --test-pattern runs, from process start, and so below the `timeout 30` it is run under: a slow modeset
     * must not use up the time, and a run that ends by itself gets to restore the console. */
    DISPLAY_TEST_PATTERN_NS = 20000000000,
};

/* What the program wants. display_config_from_machine fills it from config/machine.yaml. */
typedef struct display_config_t {
    const char *card_path;
    const char *connector_name;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_hz;
    int32_t console_fd;              /* the console that must be ours: stdin */
    display_crash_test_t crash_test; /* --crash-test: how the test pattern is to die */
    uint8_t _pad0[7];
} display_config_t;
static_assert(sizeof(display_config_t) == 40);

/* The KMS backend. Zeroed means not running, so _deinit is safe whether or not _init ever ran. */
typedef struct display_t {
    arena_t arena_setup; /* Process lifetime: what the parts ask for at _init */
    heap_region_t owned_region;
    display_device_t device;
    display_mode_t mode;
    display_flip_t flip;
    display_vt_guard_t guard;
    uint8_t refusal; /* display_refusal_t, when _init refused */
    uint8_t running;
    uint8_t crash_test; /* display_crash_test_t */
    uint8_t _pad0[5];
} display_t;
static_assert(sizeof(display_t) == 768);
static_assert(alignof(display_t) == 64);

void display_config_from_machine(display_config_t *out);

/* Refuses, with a message naming what to do, unless this process is on the visible text console and can become the
 * card's master. Then picks an output and makes its buffers, and changes nothing on screen. ops is
 * display_device_real_ops() outside tests. */
display_status_t display_init(display_t *display, const display_config_t *config, const display_ops_t *ops,
                              void *context);

/* Gives the display back if that is still owed, and releases everything. */
void display_deinit(display_t *display);

/* Runs the test pattern until started_ns + duration_ns or a stop signal, then gives the display back and logs what it
 * measured. Takes the display over, behind display/vt_guard, so VT switches work and a crash gives the console back: run
 * only after display_init said OK. Returns OK if everything worked. */
display_status_t display_test_pattern_run(display_t *display, int64_t started_ns, int64_t duration_ns);

#endif
