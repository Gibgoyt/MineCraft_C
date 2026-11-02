/* See run/src/display/vt_guard/README.md. */
#ifndef MC_RUN_DISPLAY_VT_GUARD_INDEX_H
#define MC_RUN_DISPLAY_VT_GUARD_INDEX_H

#include "arena/index.h"
#include "display/device/index.h"
#include "display/flip/index.h"

#include <linux/vt.h>
#include <stdatomic.h>
#include <stdint.h>

enum : int64_t {
    /* The watchdog: this long without a display_vt_guard_pet and the guardian takes the program down. Well above the slowest
     * modeset we expect, and short enough that a hang is over before anybody reaches for the power button. */
    DISPLAY_VT_GUARD_WATCHDOG_NS = 8000000000,
};
enum : int32_t {
    DISPLAY_VT_GUARD_NOTICE_MS = 3000, /* how long the emergency exit is on the console before it goes dark */
    DISPLAY_VT_GUARD_AWAY_MS = 20,     /* how often to look, while another VT is on screen */
    DISPLAY_VT_GUARD_KEY_EMERGENCY = -1,
};

/* How --crash-test dies. */
typedef enum display_crash_test_t : uint8_t {
    DISPLAY_CRASH_TEST_NONE = 0,
    DISPLAY_CRASH_TEST_SEGV = 1,  /* a real fault: the in-process handler */
    DISPLAY_CRASH_TEST_ABORT = 2, /* the same handler, through abort() */
    DISPLAY_CRASH_TEST_KILL = 3,  /* SIGKILL, which no handler sees: the guardian */
    DISPLAY_CRASH_TEST_HANG = 4,  /* stops petting: the watchdog */
} display_crash_test_t;

/* What display_vt_guard_service found. */
typedef enum display_vt_guard_state_t : uint8_t {
    DISPLAY_VT_GUARD_SHOWING = 0, /* our VT is on screen: draw */
    DISPLAY_VT_GUARD_AWAY = 1,    /* another VT is on screen: touch nothing, come back. It has already paused */
    DISPLAY_VT_GUARD_FAILED = 2,  /* could not get the display back: see failed_call, or flip->failed_call */
} display_vt_guard_state_t;

/* Which of the keys that matter are down. Merged across keyboards. */
typedef struct display_vt_guard_keys_t {
    uint8_t held;
} display_vt_guard_keys_t;
static_assert(sizeof(display_vt_guard_keys_t) == 1);

/* Signal dispositions, and the stack the crash handler runs on. Private; from the arena. */
typedef struct display_vt_guard_saved_t display_vt_guard_saved_t;

typedef struct display_vt_guard_t {
    const display_ops_t *borrowed_ops;
    void *borrowed_context;
    display_device_t *borrowed_device;
    display_flip_t *borrowed_flip;
    display_vt_guard_saved_t *borrowed_saved; /* from the arena given to _init */
    const char *failed_call;                  /* the first call that failed, for after the console is back */
    int64_t last_pet_ns;
    int64_t watchdog_ns;
    struct vt_mode saved_vt_mode;
    int32_t failed_errno;
    int32_t saved_kb_mode;
    int32_t guardian_pid; /* 0 when there is none */
    int32_t pipe_fd;      /* our end of the guardian's pipe. Its closing is what tells the guardian we are gone */
    /* The restore is a list of steps, each claimed once, so that any thread may run it, a fault inside one step does not lose
     * the others, and a second caller does not repeat it. */
    _Atomic uint32_t restore_next;
    _Atomic uint32_t restore_done;
    uint32_t keyboard_count;
    /* Set by handlers, taken by display_vt_guard_service. Relaxed: each is one flag that guards no other data. */
    _Atomic uint8_t release_wanted;
    _Atomic uint8_t acquire_wanted;
    _Atomic uint8_t restore_failed;
    uint8_t armed;        /* handlers installed, guardian started */
    uint8_t away;         /* another VT is on screen */
    uint8_t keyboard_off; /* the VT is in K_OFF because of us */
    uint8_t vt_process;   /* the VT is in VT_PROCESS because of us */
    uint8_t _pad0[5];
} display_vt_guard_t;
static_assert(sizeof(display_vt_guard_t) == 112);

/* Takes nothing yet. Asks the arena for what a crash will need, since a crash is no time to ask. */
void display_vt_guard_init(display_vt_guard_t *guard, display_device_t *device, display_flip_t *flip, arena_t *arena);

/* Gives everything back if that is still owed. A zeroed guard does nothing. */
void display_vt_guard_deinit(display_vt_guard_t *guard);

/* Takes the console, in this order: saves the keyboard and VT modes; opens the keyboards; says on the console how to get
 * out, and pauses so that can be read; starts the guardian; installs the crash and VT handlers; VT_PROCESS; K_OFF; then
 * display_flip_take_over, which is KD_GRAPHICS and the modeset. On false, call display_vt_guard_give_back all the same. */
bool display_vt_guard_take_console(display_vt_guard_t *guard);

/* The normal way out: the display, the keyboard, the VT mode, then the handlers and the guardian. Runs once. Returns whether
 * every call worked. */
bool display_vt_guard_give_back(display_vt_guard_t *guard);

/* Says the program is alive. Call it every frame: it costs a clock read, and a write to a pipe four times a second. */
void display_vt_guard_pet(display_vt_guard_t *guard);

/* Handles a VT switch in either direction, if one was asked for. Call it every frame, from the thread that flips. */
display_vt_guard_state_t display_vt_guard_service(display_vt_guard_t *guard);

/* Takes one key event and says what it asks for: 0 for nothing, DISPLAY_VT_GUARD_KEY_EMERGENCY, or a VT number to switch to.
 * Pure, and K_OFF is why it exists: the kernel no longer sees Ctrl+Alt+Fn, so we must. */
int32_t display_vt_guard_keys_take(display_vt_guard_keys_t *keys, uint32_t code, int32_t value);

/* Writes "CTRL+ALT+BACKSPACE = EMERGENCY EXIT" across the top of a buffer, so that it is on screen with the picture. */
void display_vt_guard_draw_notice(display_buffer_t *buffer);

/* "" is segv. NONE for anything it does not know. */
display_crash_test_t display_vt_guard_crash_test_from_name(const char *name);

/* Dies as asked. NONE returns at once, and so does a hang once a minute has gone with nobody ending it. */
void display_vt_guard_crash_now(display_crash_test_t how);

#endif
