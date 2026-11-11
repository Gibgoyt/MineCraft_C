/* See README.md. Everything runs in forked children, against a fake kernel that reports down a pipe, so that a child can really
 * die of a real signal and the test can still see what it, and the guardian after it, asked for. */
#include "run/display/mode/fixtures/fake_card.h"

#include "display/vt_guard/index.h"
#include "main/index.h"
#include "time/index.h"

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

enum : int32_t {
    RIG_CONSOLE_FD = 5,
    RIG_CARD_FD = 7,
    RIG_CONSOLE_MINOR = 3,
    RIG_KB_MODE = K_UNICODE,
    RIG_CHILD_FINE = 42,
    RIG_ALARM_SECONDS = 30, /* so that nothing outlives a test that went wrong */
    RIG_READ_MS = 20000,
};
enum : uint32_t {
    RIG_SAVED_FB = 77u,
    RIG_MY_FB = 0u, /* the rig has no buffers: buffers[0].fb_id */
    RIG_CRTC = 50u,
    RIG_MAX_RECORDS = 256u,
    RIG_ARENA_BYTES = 256u * 1024u,
};
enum : uint64_t {
    RIG_STACK_MARK = 0xa17u,      /* not a real request: says which stack a keyboard ioctl was made from */
    RIG_EMPTY_SCAN_MARK = 0xa18u, /* not a real request either: the guardian looked for keyboards and found none */
};
enum : int64_t {
    RIG_WATCHDOG_NS = 200000000,
};

/* One call the guard made, whichever process made it. */
typedef struct rig_record_t {
    uint64_t request;
    uint32_t
        value; /* KDSETMODE, KDSKBMODE, VT_RELDISP, VT_ACTIVATE: the argument. VT_SETMODE: the mode. SETCRTC: the fb */
    int32_t pid;
} rig_record_t;
static_assert(sizeof(rig_record_t) == 16);

typedef struct rig_t {
    display_device_t device;
    display_mode_t mode;
    display_flip_t flip;
    display_vt_guard_t guard;
    arena_t *borrowed_arena;
    uint64_t fail_request;  /* this one fails with fail_errno */
    uint64_t fault_request; /* this one is a real SIGSEGV, in the process that has fault_armed set */
    rig_record_t records[RIG_MAX_RECORDS];
    uint32_t record_count;
    int32_t fail_errno;
    int32_t report_read;
    int32_t report_write;
    int32_t keyboard_read;
    int32_t keyboard_write;
    int32_t output_read;
    int32_t output_write;
    int32_t active_vt;
    uint32_t keyboards;
    int32_t master_refusals;
    int32_t second_keyboard_read;
    int32_t second_keyboard_write;
    int32_t master_pid; /* who "opened the card", and so the only one whose DROP_MASTER the kernel takes */
    uint8_t fault_armed;
    uint8_t replugged;   /* open_keyboards gives the second keyboard from now on */
    uint8_t empty_scans; /* and before that, comes back with nothing this many times */
    uint8_t _pad0[5];
} rig_t;
static_assert(sizeof(rig_t) == 32 + 304 + 248 + 112 + 24 + 16 * RIG_MAX_RECORDS + 64);

static void rig_report(rig_t *rig, uint64_t request, uint32_t value) {
    const rig_record_t record = {.request = request, .value = value, .pid = getpid()};
    const int64_t written = write(rig->report_write, &record, sizeof(record));
    (void)written;
}

static int32_t rig_ioctl(void *context, int32_t fd, uint64_t request, void *argument) {
    rig_t *const rig = context;
    if (fd != RIG_CONSOLE_FD && fd != RIG_CARD_FD) {
        return -EBADF;
    }
    if (rig->fault_armed != 0u && request == rig->fault_request) {
        rig->fault_armed = 0u;
        volatile uintptr_t nowhere = 8u;
        *(volatile uint8_t *)nowhere = 1u;
    }
    if (request == rig->fail_request) {
        rig_report(rig, request, UINT32_MAX);
        return -rig->fail_errno;
    }
    if (request == KDSKBMODE) {
        /* The crash handler's stack comes from the arena, so this is how to see which stack is running. */
        const uintptr_t here = (uintptr_t)__builtin_frame_address(0);
        const uintptr_t base = (uintptr_t)rig->borrowed_arena->borrowed_base;
        rig_report(rig, RIG_STACK_MARK, here >= base && here < base + rig->borrowed_arena->capacity ? 1u : 0u);
    }
    switch (request) {
    case KDGKBMODE:
        *(int32_t *)argument = RIG_KB_MODE;
        return 0;
    case VT_GETMODE: {
        struct vt_mode *const mode = argument;
        memset(mode, 0, sizeof(*mode));
        mode->mode = VT_AUTO;
        return 0;
    }
    case VT_GETSTATE:
        ((struct vt_stat *)argument)->v_active = (uint16_t)rig->active_vt;
        return 0;
    case VT_SETMODE:
        rig_report(rig, request, (uint32_t)((const struct vt_mode *)argument)->mode);
        return 0;
    case DRM_IOCTL_MODE_SETCRTC:
        rig_report(rig, request, ((const struct drm_mode_crtc *)argument)->fb_id);
        return 0;
    case DRM_IOCTL_SET_MASTER:
        rig_report(rig, request, 0u);
        if (rig->master_refusals > 0) {
            rig->master_refusals--;
            return -EBUSY;
        }
        return 0;
    case DRM_IOCTL_DROP_MASTER:
        /* As drm_master_check_perm does for anyone but root: the guardian shares the descriptor, not the right. */
        rig_report(rig, request, 0u);
        return getpid() == rig->master_pid ? 0 : -EACCES;
    case VT_RELDISP:
        rig_report(rig, request, (uint32_t)(uintptr_t)argument);
        return 0;
    default:
        rig_report(rig, request, (uint32_t)(uintptr_t)argument);
        return 0;
    }
}

static uint32_t rig_open_keyboards(void *context, int32_t *out_fds, uint32_t capacity) {
    rig_t *const rig = context;
    if (rig->keyboards == 0u || capacity == 0u) {
        return 0u;
    }
    if (rig->replugged != 0u && getpid() != rig->master_pid) {
        if (rig->empty_scans > 0u) {
            rig->empty_scans--;
            rig_report(rig, RIG_EMPTY_SCAN_MARK, 0u);
            return 0u;
        }
        /* Two, the quiet one first, so that the guardian has to go on and watch as many as it was given. The writing end
         * stays open until the guardian exits, which is what keeps it quiet. */
        int32_t quiet[2];
        if (pipe2(quiet, O_NONBLOCK) != 0) {
            abort();
        }
        out_fds[0] = quiet[0];
        out_fds[1] = dup(rig->second_keyboard_read);
        return 2u;
    }
    out_fds[0] = dup(rig->keyboard_read);
    return 1u;
}

static void rig_pause(void *context, int32_t milliseconds) {
    (void)context;
    (void)milliseconds;
}

static const display_ops_t rig_ops = {
    .ioctl_call = rig_ioctl,
    .open_keyboards = rig_open_keyboards,
    .pause_ms = rig_pause,
};

/* test_alloc gives 16 bytes of alignment, and arena_t and display_t ask for 64. */
static void *rig_alloc_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static rig_t *rig_new(test_context_t *t) {
    rig_t *const rig = test_alloc(t, sizeof(rig_t));
    int32_t ends[2];
    if (pipe2(ends, O_CLOEXEC) != 0) {
        abort();
    }
    rig->report_read = ends[0];
    rig->report_write = ends[1];
    if (pipe2(ends, O_CLOEXEC | O_NONBLOCK) != 0) {
        abort();
    }
    rig->keyboard_read = ends[0];
    rig->keyboard_write = ends[1];
    if (pipe2(ends, O_CLOEXEC | O_NONBLOCK) != 0) {
        abort();
    }
    rig->second_keyboard_read = ends[0];
    rig->second_keyboard_write = ends[1];
    if (pipe2(ends, O_CLOEXEC) != 0) {
        abort();
    }
    rig->output_read = ends[0];
    rig->output_write = ends[1];
    rig->active_vt = RIG_CONSOLE_MINOR;
    rig->keyboards = 1u;
    rig->device.borrowed_ops = &rig_ops;
    rig->device.borrowed_context = rig;
    rig->device.card_fd = RIG_CARD_FD;
    rig->device.console_fd = RIG_CONSOLE_FD;
    rig->device.console_minor = RIG_CONSOLE_MINOR;
    rig->device.is_master = 1u;
    rig->mode.crtc_id = RIG_CRTC;
    rig->mode.refresh_mhz = 120000u;
    rig->mode.saved_crtc.fb_id = RIG_SAVED_FB;
    rig->mode.saved_crtc.mode_valid = 1u;
    rig->mode.saved_connector_count = 1u;
    rig->flip.borrowed_device = &rig->device;
    rig->flip.borrowed_mode = &rig->mode;
    rig->borrowed_arena = rig_alloc_aligned(t, sizeof(arena_t));
    arena_init(rig->borrowed_arena, "rig", test_alloc(t, RIG_ARENA_BYTES), RIG_ARENA_BYTES);
    display_vt_guard_init(&rig->guard, &rig->device, &rig->flip, rig->borrowed_arena);
    rig->guard.watchdog_ns = RIG_WATCHDOG_NS;
    return rig;
}

/* After this, the report pipe's end is seen only when every process holding it, the guardian included, is gone. Closing our
 * copy of the keyboard's writing end leaves the test the only one holding it, so that it can unplug the keyboard. */
static void rig_child_begin(rig_t *rig) {
    alarm(RIG_ALARM_SECONDS);
    dup2(rig->output_write, STDERR_FILENO);
    close(rig->keyboard_write);
    close(rig->second_keyboard_write);
    rig->master_pid = getpid();
}

/* Waits for the child and for every process that held the report pipe, and takes in what they said. So a guardian left
 * running shows up as a failure and not as a test that passes and leaves something behind. */
static void rig_collect(test_context_t *t, rig_t *rig, int32_t child, int32_t *out_status, char *output,
                        uint64_t output_capacity) {
    close(rig->report_write);
    close(rig->output_write);
    close(rig->keyboard_read);
    close(rig->second_keyboard_read);
    rig->report_write = -1;
    rig->record_count = 0u;
    uint64_t output_length = 0;
    bool report_open = true;
    bool output_open = true;
    const int64_t started_ns = time_now_ns();
    while ((report_open || output_open) && time_now_ns() - started_ns < (int64_t)RIG_READ_MS * 1000000) {
        struct pollfd fds[2] = {{.fd = report_open ? rig->report_read : -1, .events = POLLIN, .revents = 0},
                                {.fd = output_open ? rig->output_read : -1, .events = POLLIN, .revents = 0}};
        if (poll(fds, 2u, 100) <= 0) {
            continue;
        }
        if (fds[0].revents != 0) {
            rig_record_t record;
            const int64_t count = read(rig->report_read, &record, sizeof(record));
            if (count == (int64_t)sizeof(record)) {
                if (rig->record_count < RIG_MAX_RECORDS) {
                    rig->records[rig->record_count] = record;
                    rig->record_count++;
                }
            } else {
                report_open = false;
            }
        }
        if (fds[1].revents != 0) {
            const int64_t count = read(rig->output_read, output + output_length, output_capacity - 1u - output_length);
            if (count > 0) {
                output_length += (uint64_t)count;
            } else {
                output_open = false;
            }
        }
    }
    output[output_length] = '\0';
    CHECK(!report_open);
    CHECK(!output_open);
    close(rig->report_read);
    close(rig->output_read);
    close(rig->keyboard_write);
    close(rig->second_keyboard_write);
    const int32_t waited = waitpid(child, out_status, 0);
    CHECK_EQ_I64(waited, child);
    arena_deinit(rig->borrowed_arena); /* or the harness, clearing the test arena, trips over its poison */
}

static int32_t rig_find(const rig_t *rig, uint64_t request, uint32_t value, int32_t from) {
    for (uint32_t i = (uint32_t)from; i < rig->record_count; i++) {
        if (rig->records[i].request == request && rig->records[i].value == value) {
            return (int32_t)i;
        }
    }
    return -1;
}

static uint32_t rig_count(const rig_t *rig, uint64_t request, uint32_t value) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < rig->record_count; i++) {
        count += rig->records[i].request == request && rig->records[i].value == value ? 1u : 0u;
    }
    return count;
}

/* The whole give-back, in the only order that works, from wherever `from` says it starts. */
static void check_given_back_from(test_context_t *t, const rig_t *rig, int32_t from) {
    const int32_t crtc = rig_find(rig, DRM_IOCTL_MODE_SETCRTC, RIG_SAVED_FB, from);
    const int32_t drop = rig_find(rig, DRM_IOCTL_DROP_MASTER, 0u, from);
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, from);
    const int32_t keyboard = rig_find(rig, KDSKBMODE, (uint32_t)RIG_KB_MODE, from);
    const int32_t vt_answer = rig_find(rig, VT_RELDISP, 1u, from);
    const int32_t vt_mode = rig_find(rig, VT_SETMODE, VT_AUTO, from);
    CHECK(crtc >= 0);
    CHECK(drop > crtc);
    CHECK(text > drop);
    CHECK(keyboard > text);
    CHECK(vt_answer > keyboard);
    CHECK(vt_mode > vt_answer);
}

static void check_taken_in_order(test_context_t *t, const rig_t *rig) {
    const int32_t vt_process = rig_find(rig, VT_SETMODE, VT_PROCESS, 0);
    const int32_t keyboard_off = rig_find(rig, KDSKBMODE, K_OFF, 0);
    const int32_t graphics = rig_find(rig, KDSETMODE, KD_GRAPHICS, 0);
    const int32_t modeset = rig_find(rig, DRM_IOCTL_MODE_SETCRTC, RIG_MY_FB, 0);
    CHECK(vt_process >= 0);
    CHECK(keyboard_off > vt_process);
    CHECK(graphics > keyboard_off);
    CHECK(modeset > graphics);
}

/* ---- Normal running ---------------------------------------------------------------------------------------------- */

TEST(taking_and_giving_back_run_in_order_and_the_guardian_does_nothing_and_ends) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        const bool taken = display_vt_guard_take_console(&rig->guard);
        const bool given = display_vt_guard_give_back(&rig->guard);
        _exit(taken && given && rig->guard.guardian_pid == 0 ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFEXITED(status));
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    check_taken_in_order(t, rig);
    check_given_back_from(t, rig, 0);
    /* Once each: the guardian added nothing. */
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 1u);
    CHECK_EQ_U64(rig_count(rig, DRM_IOCTL_DROP_MASTER, 0u), 1u);
    CHECK_EQ_U64(rig_count(rig, VT_SETMODE, VT_AUTO), 1u);
    /* The way out is said before the console goes dark. */
    CHECK_CONTAINS(output, "EMERGENCY EXIT");
    CHECK_CONTAINS(output, "Ctrl+Alt");
    CHECK_CONTAINS(output, "Backspace");
    CHECK_CONTAINS(output, "1 keyboard");
}

TEST(with_no_keyboard_it_says_so_and_leaves_the_keyboard_on) {
    rig_t *const rig = rig_new(t);
    rig->keyboards = 0u;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        const bool taken = display_vt_guard_take_console(&rig->guard);
        const bool given = display_vt_guard_give_back(&rig->guard);
        _exit(taken && given ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, KDSKBMODE, K_OFF), 0u);
    CHECK_EQ_U64(rig_count(rig, KDSKBMODE, (uint32_t)RIG_KB_MODE), 0u);
    CHECK_CONTAINS(output, "NO KEYBOARD");
    CHECK_CONTAINS(output, "WILL NOT WORK");
}

/* A failure of `request` while taking: nothing after it happens, what came before is undone, and it is named. */
static void check_failed_take(test_context_t *t, uint64_t request, int32_t error) {
    rig_t *const rig = rig_new(t);
    rig->fail_request = request;
    rig->fail_errno = error;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        const bool taken = display_vt_guard_take_console(&rig->guard);
        const bool named = rig->guard.failed_call != nullptr;
        const bool given = display_vt_guard_give_back(&rig->guard);
        (void)given; /* a failure of the same call may repeat on the way back */
        _exit(!taken && named ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_GRAPHICS), 0u);
    CHECK_EQ_U64(rig_count(rig, DRM_IOCTL_MODE_SETCRTC, RIG_MY_FB), 0u);
}

TEST(nothing_is_changed_if_the_modes_to_go_back_to_cannot_be_read) {
    check_failed_take(t, KDGKBMODE, EPERM);
    check_failed_take(t, VT_GETMODE, EPERM);
}

TEST(a_console_that_refuses_vt_process_or_k_off_is_not_taken) {
    check_failed_take(t, VT_SETMODE, EPERM);
    check_failed_take(t, KDSKBMODE, EPERM);
}

TEST(failing_to_save_the_keyboard_mode_leaves_it_alone) {
    rig_t *const rig = rig_new(t);
    rig->fail_request = KDGKBMODE;
    rig->fail_errno = EPERM;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        const bool taken = display_vt_guard_take_console(&rig->guard);
        (void)display_vt_guard_give_back(&rig->guard);
        _exit(!taken ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, KDSKBMODE, K_OFF), 0u);
    CHECK_EQ_U64(rig_count(rig, VT_SETMODE, VT_PROCESS), 0u);
    CHECK_EQ_U64(rig_count(rig, VT_SETMODE, VT_AUTO), 0u);
}

/* ---- VT switching ------------------------------------------------------------------------------------------------ */

TEST(a_vt_switch_lets_go_and_comes_back_in_order) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        bool ok = display_vt_guard_take_console(&rig->guard);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_SHOWING && ok;
        raise(SIGUSR1);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_AWAY && ok;
        ok = rig->flip.suspended == 1u && rig->device.is_master == 0u && ok;
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_AWAY && ok; /* stays away until told */
        raise(SIGUSR2);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_SHOWING && ok;
        ok = rig->flip.suspended == 0u && rig->device.is_master == 1u && rig->flip.skip_interval == 1u && ok;
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    const int32_t saved = rig_find(rig, DRM_IOCTL_MODE_SETCRTC, RIG_SAVED_FB, 0);
    const int32_t dropped = rig_find(rig, DRM_IOCTL_DROP_MASTER, 0u, 0);
    const int32_t released = rig_find(rig, VT_RELDISP, 1u, 0);
    const int32_t acknowledged = rig_find(rig, VT_RELDISP, VT_ACKACQ, 0);
    const int32_t master = rig_find(rig, DRM_IOCTL_SET_MASTER, 0u, 0);
    REQUIRE(saved >= 0);
    CHECK(dropped > saved);
    CHECK(released > dropped);
    CHECK(acknowledged > released);
    CHECK(master > acknowledged);
    const int32_t shown_again = rig_find(rig, DRM_IOCTL_MODE_SETCRTC, RIG_MY_FB, master);
    CHECK(shown_again > master);
    /* And the way out after it is whole. */
    check_given_back_from(t, rig, shown_again);
}

TEST(leaving_while_on_another_vt_leaves_the_crtc_alone) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        bool ok = display_vt_guard_take_console(&rig->guard);
        raise(SIGUSR1);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_AWAY && ok;
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    /* The one from the switch, and none from the way out: we are not master, and that screen is somebody else's. */
    CHECK_EQ_U64(rig_count(rig, DRM_IOCTL_MODE_SETCRTC, RIG_SAVED_FB), 1u);
    CHECK_EQ_U64(rig_count(rig, DRM_IOCTL_DROP_MASTER, 0u), 1u);
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 1u);
    CHECK_EQ_U64(rig_count(rig, VT_SETMODE, VT_AUTO), 1u);
}

TEST(a_slow_master_is_waited_for_and_one_never_given_is_named) {
    rig_t *const rig = rig_new(t);
    rig->master_refusals = 3;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        bool ok = display_vt_guard_take_console(&rig->guard);
        raise(SIGUSR1);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_AWAY && ok;
        raise(SIGUSR2);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_SHOWING && ok;
        /* Now one that never comes. */
        rig->master_refusals = 1000000;
        raise(SIGUSR1);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_AWAY && ok;
        raise(SIGUSR2);
        ok = display_vt_guard_service(&rig->guard) == DISPLAY_VT_GUARD_FAILED && ok;
        ok = rig->guard.failed_call != nullptr && ok;
        (void)display_vt_guard_give_back(&rig->guard);
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
}

/* ---- Crashes ----------------------------------------------------------------------------------------------------- */

static void check_dies_and_restores(test_context_t *t, int32_t signal_number, bool real_fault) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        if (real_fault) {
            display_vt_guard_crash_now(DISPLAY_CRASH_TEST_SEGV);
        } else {
            raise(signal_number);
        }
        _exit(2);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), signal_number);
    check_taken_in_order(t, rig);
    check_given_back_from(t, rig, 0);
    CHECK_CONTAINS(output, "the program crashed; the console has been given back");
    /* The program did it, and told the guardian, which added nothing. */
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 1u);
    CHECK_EQ_I64(rig->records[rig_find(rig, KDSETMODE, KD_TEXT, 0)].pid, child);
}

TEST(a_real_segfault_restores_and_still_dies_of_it) {
    check_dies_and_restores(t, SIGSEGV, true);
}

TEST(every_crash_signal_restores_and_still_dies_of_it) {
    static const int32_t signals[] = {SIGSEGV, SIGBUS,  SIGFPE,  SIGILL,    SIGABRT, SIGSYS,   SIGTRAP,
                                      SIGXCPU, SIGXFSZ, SIGALRM, SIGVTALRM, SIGPROF, SIGSTKFLT};
    for (uint32_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        check_dies_and_restores(t, signals[i], false);
    }
}

/* What lets a stack overflow be survived. No overflow is made here, since recursion is banned and it would prove the same. */
TEST(the_crash_handler_runs_on_its_own_stack) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        display_vt_guard_crash_now(DISPLAY_CRASH_TEST_SEGV);
        _exit(2);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WTERMSIG(status), SIGSEGV);
    CHECK_EQ_U64(rig_count(rig, RIG_STACK_MARK, 0u), 1u); /* K_OFF, from the ordinary stack */
    CHECK_EQ_U64(rig_count(rig, RIG_STACK_MARK, 1u), 1u); /* putting it back, from the handler's */
}

TEST(the_time_away_on_another_vt_is_not_counted_as_a_late_frame) {
    rig_t *const rig = rig_new(t);
    display_flip_t *const flip = &rig->flip;
    rig->master_pid = getpid();
    rig->device.timestamp_monotonic = 1u;
    flip->period_ns = 8333333u;
    const struct drm_event_vblank feed = {
        .base = {.type = DRM_EVENT_FLIP_COMPLETE, .length = sizeof(struct drm_event_vblank)}, .crtc_id = RIG_CRTC};
    /* The warm-up, then one ordinary interval. */
    for (uint32_t i = 1u; i <= DISPLAY_FLIP_WARMUP_EVENTS + 2u; i++) {
        struct drm_event_vblank event = feed;
        event.tv_usec = i * 8333u;
        flip->flip_pending = 1u;
        display_flip_take_events(flip, (const uint8_t *)&event, sizeof(event), 0);
    }
    const uint64_t before = flip->stats.intervals;
    REQUIRE(before > 0u);
    REQUIRE(display_flip_suspend(flip));
    REQUIRE(display_flip_resume(flip));
    /* Ten seconds later: not an interval. The next one is. */
    struct drm_event_vblank event = feed;
    event.tv_sec = 10u;
    flip->flip_pending = 1u;
    display_flip_take_events(flip, (const uint8_t *)&event, sizeof(event), 0);
    CHECK_EQ_U64(flip->stats.intervals, before);
    CHECK_EQ_U64(flip->stats.late, 0u);
    event.tv_usec = 8333u;
    flip->flip_pending = 1u;
    display_flip_take_events(flip, (const uint8_t *)&event, sizeof(event), 0);
    CHECK_EQ_U64(flip->stats.intervals, before + 1u);
    CHECK_EQ_U64(flip->stats.late, 0u);
    arena_deinit(rig->borrowed_arena);
}

/* How many of these one process asked for. */
static uint32_t rig_count_by(const rig_t *rig, uint64_t request, uint32_t value, int32_t pid) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < rig->record_count; i++) {
        const rig_record_t *const record = &rig->records[i];
        count += record->request == request && record->value == value && record->pid == pid ? 1u : 0u;
    }
    return count;
}

/* A fault inside one of the handler's steps, under the crash's own signal and under a different one. The handler is never
 * entered again: every crash signal is blocked while it runs, so the fault ends the process there with the kernel's default
 * action, SIGSEGV either way. The steps before it have done their part; the guardian does the faulted step and the rest. */
static void check_fault_inside_the_restore(test_context_t *t, display_crash_test_t how, uint64_t fault_request) {
    rig_t *const rig = rig_new(t);
    rig->fault_request = fault_request;
    /* The process dies of SIGSEGV with no handler, which dumps core: that can take longer than the rig's watchdog, which
     * would then answer instead of the pipe closing. No core, and a watchdog that cannot come first. */
    rig->guard.watchdog_ns = 60000000000;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        const struct rlimit no_core = {.rlim_cur = 0, .rlim_max = 0};
        (void)setrlimit(RLIMIT_CORE, &no_core);
        rig_child_begin(rig);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        /* After the takeover's own KDSETMODE and VT_SETMODE. So this fault is in the handler's restore. Not in the
         * guardian: it is forked earlier, and has fault_armed clear in its own copy. */
        rig->fault_armed = 1u;
        display_vt_guard_crash_now(how);
        _exit(2);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    REQUIRE(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), SIGSEGV);
    /* The handler did the steps before the fault. */
    CHECK_EQ_U64(rig_count_by(rig, DRM_IOCTL_MODE_SETCRTC, RIG_SAVED_FB, child), 1u);
    const uint32_t handler_text = rig_count_by(rig, KDSETMODE, KD_TEXT, child);
    const uint32_t handler_keyboard = rig_count_by(rig, KDSKBMODE, (uint32_t)RIG_KB_MODE, child);
    if (fault_request == KDSETMODE) {
        CHECK_EQ_U64(handler_text, 0u);
        CHECK_EQ_U64(handler_keyboard, 0u);
    } else {
        CHECK_EQ_U64(handler_text, 1u);
        CHECK_EQ_U64(handler_keyboard, 1u);
        CHECK_EQ_U64(rig_count_by(rig, VT_RELDISP, 1u, child), 1u);
    }
    /* And nothing after it: no step ran twice or past the fault in the program. */
    CHECK_EQ_U64(rig_count_by(rig, VT_SETMODE, VT_AUTO, child), 0u);
    /* The guardian, which never heard "done", did the whole give-back from its copy. */
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, 0);
    const int32_t vt_mode = rig_find(rig, VT_SETMODE, VT_AUTO, 0);
    REQUIRE(text >= 0);
    REQUIRE(vt_mode >= 0);
    CHECK(rig->records[vt_mode].pid != child);
    CHECK(output != nullptr && strstr(output, "the program crashed; the console has been given back") == nullptr);
    CHECK_CONTAINS(output, "the program ended without giving the console back");
}

TEST(a_fault_inside_the_restore_ends_the_process_and_the_guardian_finishes) {
    check_fault_inside_the_restore(t, DISPLAY_CRASH_TEST_ABORT, KDSETMODE);
    check_fault_inside_the_restore(t, DISPLAY_CRASH_TEST_SEGV, KDSETMODE);
    check_fault_inside_the_restore(t, DISPLAY_CRASH_TEST_ABORT, VT_SETMODE);
    check_fault_inside_the_restore(t, DISPLAY_CRASH_TEST_SEGV, VT_SETMODE);
}

/* ---- What no handler can catch ----------------------------------------------------------------------------------- */

TEST(sigkill_is_answered_by_the_guardian_and_it_then_ends) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        display_vt_guard_crash_now(DISPLAY_CRASH_TEST_KILL);
        _exit(2);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), SIGKILL);
    check_taken_in_order(t, rig);
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, 0);
    REQUIRE(text >= 0);
    CHECK(rig->records[text].pid != child);
    check_given_back_from(t, rig, text - 2);
    CHECK_CONTAINS(output, "the program ended without giving the console back");
    CHECK_CONTAINS(output, "the console has been given back");
}

/* `timeout` signals its whole process group, and `timeout -s KILL` means nothing in that group gets to clean up. */
static void check_group_signal(test_context_t *t, int32_t signal_number) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        setpgid(0, 0);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        kill(0, signal_number);
        _exit(2);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), signal_number);
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, 0);
    REQUIRE(text >= 0);
    CHECK(rig->records[text].pid != child);
    check_given_back_from(t, rig, text - 2);
}

TEST(a_signal_to_the_whole_group_does_not_reach_the_guardian) {
    check_group_signal(t, SIGTERM);
    check_group_signal(t, SIGKILL);
}

TEST(the_guardian_ignores_being_asked_to_stop) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        static const int32_t asked[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT};
        for (uint32_t i = 0; i < sizeof(asked) / sizeof(asked[0]); i++) {
            kill(rig->guard.guardian_pid, asked[i]);
        }
        usleep(50000u);
        kill(getpid(), SIGKILL);
        _exit(2);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WTERMSIG(status), SIGKILL);
    CHECK(rig_find(rig, KDSETMODE, KD_TEXT, 0) >= 0);
}

TEST(a_program_that_returns_without_giving_back_is_cleaned_up_after) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        _exit(RIG_CHILD_FINE);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 1u);
    CHECK_EQ_U64(rig_count(rig, VT_SETMODE, VT_AUTO), 1u);
}

/* A child that hangs, one way or another, and what the watchdog needs to end it. */
static void check_watchdog(test_context_t *t, bool ignore_term, bool stop_itself, int32_t expected_signal) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        if (ignore_term) {
            signal(SIGTERM, SIG_IGN);
        }
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        if (stop_itself) {
            raise(SIGSTOP);
        }
        for (;;) {
            pause(); /* hung: never pets again. alarm() in rig_child_begin ends this if the watchdog never does */
        }
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    const int64_t started_ns = time_now_ns();
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), expected_signal);
    CHECK(time_now_ns() - started_ns >= RIG_WATCHDOG_NS);
    CHECK_CONTAINS(output, "the watchdog saw the program stop responding");
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, 0);
    REQUIRE(text >= 0);
    CHECK(rig->records[text].pid != child);
    check_given_back_from(t, rig, text - 2);
}

TEST(a_hang_answers_the_watchdog_with_sigterm_if_it_can) {
    check_watchdog(t, false, false, SIGTERM);
}

TEST(a_hang_that_ignores_sigterm_gets_sigkill) {
    check_watchdog(t, true, false, SIGKILL);
}

TEST(a_stopped_program_gets_sigkill_too) {
    check_watchdog(t, false, true, SIGKILL);
}

TEST(petting_keeps_the_watchdog_away) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        rig->guard.watchdog_ns = 500000000; /* more than twice GUARD_PET_EVERY_NS, with a margin */
        bool ok = display_vt_guard_take_console(&rig->guard);
        const int64_t started_ns = time_now_ns();
        while (time_now_ns() - started_ns < 2000000000) {
            display_vt_guard_pet(&rig->guard);
            usleep(10000u);
        }
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFEXITED(status));
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 1u);
}

/* ---- The keys ---------------------------------------------------------------------------------------------------- */

TEST(the_keys_mean_only_what_they_should) {
    display_vt_guard_keys_t keys = {0};
    /* Alone, or with one modifier, Backspace is just Backspace. */
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_BACKSPACE, 1), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_LEFTCTRL, 1), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_BACKSPACE, 1), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_F3, 1), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_LEFTALT, 1), 0);
    /* Both: only on the press. */
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_BACKSPACE, 1), DISPLAY_VT_GUARD_KEY_EMERGENCY);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_BACKSPACE, 2), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_BACKSPACE, 0), 0);
    /* Other keys do nothing, and the F keys count from 1, including the two that are not next to the others. */
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_A, 1), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_F1, 1), 1);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_F10, 1), 10);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_F11, 1), 11);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_F12, 1), 12);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_F3, 2), 0);
    /* Let go of Alt, and it is over. */
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_LEFTALT, 0), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_BACKSPACE, 1), 0);
    /* The right-hand ones count. Holding two of a kind, letting go of one leaves the other. */
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_RIGHTALT, 1), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_RIGHTCTRL, 1), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_LEFTCTRL, 0), 0);
    CHECK_EQ_I64(display_vt_guard_keys_take(&keys, KEY_BACKSPACE, 1), DISPLAY_VT_GUARD_KEY_EMERGENCY);
}

static void rig_press(const rig_t *rig, uint16_t code, int32_t value) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = EV_KEY;
    event.code = code;
    event.value = value;
    const int64_t written = write(rig->keyboard_write, &event, sizeof(event));
    (void)written;
}

static void rig_press_emergency(const rig_t *rig) {
    /* Other events on the stream, as a real keyboard sends, must be stepped over. */
    struct input_event sync;
    memset(&sync, 0, sizeof(sync));
    sync.type = EV_SYN;
    sync.code = KEY_BACKSPACE;
    sync.value = 1;
    const int64_t written = write(rig->keyboard_write, &sync, sizeof(sync));
    (void)written;
    rig_press(rig, KEY_LEFTCTRL, 1);
    rig_press(rig, KEY_LEFTALT, 1);
    rig_press(rig, KEY_BACKSPACE, 1);
}

TEST(the_emergency_keys_end_a_hung_program_and_give_the_console_back) {
    rig_t *const rig = rig_new(t);
    rig->guard.watchdog_ns = 60000000000; /* so that this is the keys' doing */
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        signal(SIGTERM, SIG_IGN);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        for (;;) {
            pause();
        }
    }
    usleep(100000u);
    rig_press_emergency(rig);
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    const int64_t started_ns = time_now_ns();
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(time_now_ns() - started_ns < 10000000000);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), SIGKILL);
    CHECK_CONTAINS(output, "Ctrl+Alt+Backspace was pressed");
    CHECK_CONTAINS(output, "the console has been given back");
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, 0);
    REQUIRE(text >= 0);
    check_given_back_from(t, rig, text - 2);
}

TEST(the_emergency_keys_let_a_program_that_can_answer_clean_up_itself) {
    rig_t *const rig = rig_new(t);
    rig->guard.watchdog_ns = 60000000000;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        /* As display/ does: SIGTERM is a request to stop, and the loop then gives back. */
        sigset_t term;
        sigemptyset(&term);
        sigaddset(&term, SIGTERM);
        sigprocmask(SIG_BLOCK, &term, nullptr);
        bool ok = display_vt_guard_take_console(&rig->guard);
        int32_t signal_number = 0;
        sigwait(&term, &signal_number);
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok && signal_number == SIGTERM ? RIG_CHILD_FINE : 1);
    }
    usleep(100000u);
    rig_press_emergency(rig);
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFEXITED(status));
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_CONTAINS(output, "the program stopped when asked");
    /* All the program's own. */
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, 0);
    REQUIRE(text >= 0);
    CHECK_EQ_I64(rig->records[text].pid, child);
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 1u);
}

/* Presses keys meant for X: it must carry on, and nothing may be asked of the VT. */
TEST(the_keys_mean_nothing_while_another_vt_is_showing) {
    rig_t *const rig = rig_new(t);
    rig->active_vt = RIG_CONSOLE_MINOR + 1;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        rig->guard.watchdog_ns = 60000000000;
        bool ok = display_vt_guard_take_console(&rig->guard);
        usleep(400000u);
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    usleep(100000u);
    rig_press_emergency(rig);
    rig_press(rig, KEY_F2, 1);
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFEXITED(status));
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, VT_ACTIVATE, 2u), 0u);
    /* With another VT showing the console is not taken at all until ours is back (Stage 18.2), so nothing was asked to go back to text either, and the emergency keys
     * did not end it. */
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_GRAPHICS), 0u);
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 0u);
}

TEST(ctrl_alt_f_asks_for_that_vt_but_not_our_own) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        rig->guard.watchdog_ns = 60000000000;
        bool ok = display_vt_guard_take_console(&rig->guard);
        usleep(400000u);
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    usleep(100000u);
    rig_press(rig, KEY_LEFTCTRL, 1);
    rig_press(rig, KEY_RIGHTALT, 1);
    rig_press(rig, KEY_F3, 1);
    rig_press(rig, KEY_F3, 0);
    rig_press(rig, KEY_F12, 1);
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, VT_ACTIVATE, (uint32_t)RIG_CONSOLE_MINOR), 0u);
    CHECK_EQ_U64(rig_count(rig, VT_ACTIVATE, 12u), 1u);
}

TEST(an_unplugged_keyboard_does_not_stop_the_guardian) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        rig->guard.watchdog_ns = 60000000000;
        bool ok = display_vt_guard_take_console(&rig->guard);
        usleep(300000u);
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    /* The end of the keyboard's pipe is what unplugging looks like: read gives 0, and the guardian must step over it. */
    close(rig->keyboard_write);
    rig->keyboard_write = open("/dev/null", O_WRONLY | O_CLOEXEC);
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK_EQ_U64(rig_count(rig, KDSETMODE, KD_TEXT), 1u);
}

/* The keyboard's pipe ends, and the second pipe is what is there when the guardian looks again. */
static void rig_replug(rig_t *rig) {
    close(rig->keyboard_write);
    rig->keyboard_write = rig->second_keyboard_write;
    rig->second_keyboard_write = -1;
}

/* Under K_OFF a keyboard that went away and came back is the only way out there is. It must count, even if it took a while. */
static void check_keyboard_comes_back(test_context_t *t, uint8_t empty_scans) {
    rig_t *const rig = rig_new(t);
    rig->guard.watchdog_ns = 60000000000; /* so that this is the keys' doing */
    rig->replugged = 1u;
    rig->empty_scans = empty_scans;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        signal(SIGTERM, SIG_IGN);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        for (;;) {
            pause();
        }
    }
    usleep(100000u);
    rig_replug(rig);
    rig_press_emergency(rig);
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), SIGKILL);
    CHECK_CONTAINS(output, "Ctrl+Alt+Backspace was pressed");
    CHECK_EQ_U64(rig_count(rig, RIG_EMPTY_SCAN_MARK, 0u), empty_scans);
    const int32_t text = rig_find(rig, KDSETMODE, KD_TEXT, 0);
    REQUIRE(text >= 0);
    check_given_back_from(t, rig, text - 2);
}

TEST(a_keyboard_that_comes_back_still_has_the_emergency_exit) {
    check_keyboard_comes_back(t, 0u);
}

TEST(the_guardian_keeps_looking_until_a_keyboard_comes_back) {
    check_keyboard_comes_back(t, 2u);
}

TEST(ctrl_held_on_a_keyboard_that_went_away_does_not_count_on_the_next) {
    rig_t *const rig = rig_new(t);
    rig->guard.watchdog_ns = 60000000000;
    rig->replugged = 1u;
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        bool ok = display_vt_guard_take_console(&rig->guard);
        usleep(600000u);
        ok = display_vt_guard_give_back(&rig->guard) && ok;
        _exit(ok ? RIG_CHILD_FINE : 1);
    }
    usleep(100000u);
    rig_press(rig, KEY_LEFTCTRL, 1);
    rig_press(rig, KEY_LEFTALT, 1);
    usleep(100000u); /* so that the guardian has taken them before the keyboard goes */
    rig_replug(rig);
    rig_press(rig, KEY_BACKSPACE, 1);
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    CHECK(WIFEXITED(status));
    CHECK_EQ_I64(WEXITSTATUS(status), RIG_CHILD_FINE);
    CHECK(strstr(output, "was pressed") == nullptr);
}

/* A guardian that is not root is refused DROP_MASTER, in every test here, as the kernel would. Nothing after it may be lost. */
TEST(the_guardian_being_refused_drop_master_loses_nothing_else) {
    rig_t *const rig = rig_new(t);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        rig_child_begin(rig);
        if (!display_vt_guard_take_console(&rig->guard)) {
            _exit(1);
        }
        display_vt_guard_crash_now(DISPLAY_CRASH_TEST_KILL);
        _exit(2);
    }
    char *const output = test_alloc(t, 8192u);
    int32_t status = 0;
    rig_collect(t, rig, child, &status, output, 8192u);
    const int32_t drop = rig_find(rig, DRM_IOCTL_DROP_MASTER, 0u, 0);
    REQUIRE(drop >= 0);
    CHECK(rig->records[drop].pid != child);
    CHECK(rig_find(rig, KDSETMODE, KD_TEXT, drop) > drop);
    CHECK(rig_find(rig, KDSKBMODE, (uint32_t)RIG_KB_MODE, drop) > drop);
    CHECK(rig_find(rig, VT_SETMODE, VT_AUTO, drop) > drop);
    CHECK_CONTAINS(output, "the console has been given back");
}

/* ---- The notice on screen ---------------------------------------------------------------------------------------- */

static uint32_t notice_count(const display_buffer_t *buffer, uint32_t colour) {
    uint32_t count = 0;
    for (uint32_t y = 0; y < buffer->height; y++) {
        const uint32_t *const row = (const uint32_t *)(const void *)(buffer->owned_map + (uint64_t)y * buffer->pitch);
        for (uint32_t x = 0; x < buffer->width; x++) {
            count += row[x] == colour ? 1u : 0u;
        }
    }
    return count;
}

static display_buffer_t notice_buffer(test_context_t *t, uint32_t width, uint32_t height) {
    display_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.width = width;
    buffer.height = height;
    buffer.pitch = width * 4u + 16u;
    buffer.map_bytes = (uint64_t)buffer.pitch * height;
    buffer.owned_map = test_alloc(t, buffer.map_bytes);
    return buffer;
}

TEST(the_notice_is_on_screen_large_and_above_the_moving_square) {
    display_buffer_t buffer = notice_buffer(t, 1920u, 1080u);
    display_vt_guard_draw_notice(&buffer);
    const uint32_t white = notice_count(&buffer, 0x00ffffffu);
    const uint32_t red = notice_count(&buffer, 0x00c00000u);
    /* At scale 6: 35 cells wide by 9 rows high, each 36 pixels. */
    CHECK_EQ_U64(white + red, 35u * 6u * 6u * 9u * 6u);
    CHECK(white > 10000u);
    CHECK(red > white);
    /* Nothing past the square's band, which starts a good way down. */
    const uint32_t *const below = (const uint32_t *)(const void *)(buffer.owned_map + (uint64_t)135u * buffer.pitch);
    for (uint32_t x = 0; x < buffer.width; x++) {
        CHECK_EQ_U64(below[x], 0u);
    }
}

TEST(the_notice_shows_the_text_it_says) {
    display_buffer_t buffer = notice_buffer(t, 1920u, 1080u);
    display_vt_guard_draw_notice(&buffer);
    /* The C of CTRL: at scale 6 the band starts 8 rows down, and glyph row 1 is one scaled row in. Five columns of row 0 of C
     * are 0x0e, so off, on, on, on, off. */
    const uint32_t left = (1920u - 35u * 6u * 6u) / 2u;
    const uint32_t y = 8u + 6u + 3u;
    const uint32_t *const row = (const uint32_t *)(const void *)(buffer.owned_map + (uint64_t)y * buffer.pitch);
    CHECK_EQ_U64(row[left + 3u], 0x00c00000u);
    CHECK_EQ_U64(row[left + 6u + 3u], 0x00ffffffu);
    CHECK_EQ_U64(row[left + 12u + 3u], 0x00ffffffu);
    CHECK_EQ_U64(row[left + 18u + 3u], 0x00ffffffu);
    CHECK_EQ_U64(row[left + 24u + 3u], 0x00c00000u);
}

TEST(the_notice_says_where_it_ends_or_that_it_is_not_there) {
    display_buffer_t buffer = notice_buffer(t, 1920u, 1080u);
    CHECK_EQ_U64(display_vt_guard_notice_bottom(&buffer), 8u + 9u * 6u);
    display_vt_guard_draw_notice(&buffer);
    const uint32_t *const last =
        (const uint32_t *)(const void *)(buffer.owned_map + (uint64_t)(8u + 9u * 6u - 1u) * buffer.pitch);
    const uint32_t *const after =
        (const uint32_t *)(const void *)(buffer.owned_map + (uint64_t)(8u + 9u * 6u) * buffer.pitch);
    CHECK_EQ_U64(last[960], 0x00c00000u);
    CHECK_EQ_U64(after[960], 0u);
    display_buffer_t tiny = notice_buffer(t, 100u, 100u);
    CHECK_EQ_U64(display_vt_guard_notice_bottom(&tiny), 0u);
}

TEST(the_notice_keeps_to_small_screens_or_stays_off) {
    /* Each is in a buffer with a gap past the row: a write outside the width would be found by the count. */
    static const uint32_t sizes[][2] = {{1u, 1u}, {16u, 16u}, {100u, 100u}, {320u, 200u}, {640u, 480u}, {3840u, 2160u}};
    for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        display_buffer_t buffer = notice_buffer(t, sizes[i][0], sizes[i][1]);
        display_vt_guard_draw_notice(&buffer);
        const uint32_t drawn = notice_count(&buffer, 0x00ffffffu) + notice_count(&buffer, 0x00c00000u);
        CHECK(drawn <= (uint64_t)buffer.width * buffer.height / 8u);
    }
}

/* ---- The whole test pattern, on the fake card ---------------------------------------------------------------------- */

static display_status_t run_card_pattern(fake_card_t *card, display_t *display, display_crash_test_t crash_test,
                                         int64_t watchdog_ns, int64_t duration_ns) {
    display_config_t config = fake_config();
    config.crash_test = crash_test;
    const display_status_t status = display_init(display, &config, &fake_ops, card);
    if (status != DISPLAY_STATUS_OK) {
        return status;
    }
    if (watchdog_ns != 0) {
        display->guard.watchdog_ns = watchdog_ns;
    }
    return display_test_pattern_run(display, time_now_ns(), duration_ns);
}

TEST(the_test_pattern_goes_through_a_vt_switch_and_carries_on) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    card->vt_release_after_flips = 20u;
    display_t *const display = rig_alloc_aligned(t, sizeof(display_t));
    const display_status_t status = run_card_pattern(card, display, DISPLAY_CRASH_TEST_NONE, 0, 100000000);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    /* Flips went on after it, and the time away is not counted as a late frame. */
    CHECK(card->flips > 20u);
    const int32_t released = fake_find_call(card, VT_RELDISP, 1u);
    const int32_t acknowledged = fake_find_call(card, VT_RELDISP, VT_ACKACQ);
    REQUIRE(released >= 0);
    CHECK(acknowledged > released);
    CHECK_EQ_U64(display->flip.stats.late, 0u);
    display_deinit(display);
    check_nothing_left(t, card);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    CHECK_EQ_U64(card->is_master, 0u);
}

typedef struct card_frames_t {
    uint32_t drawn;      /* 0 */
    uint32_t stop_after; /* 4 */
    uint32_t discards;   /* 8 */
    uint32_t wrong_size; /* 12 */
} card_frames_t;
static_assert(sizeof(card_frames_t) == 16);

static bool card_frames_draw(void *context, display_buffer_t *back) {
    card_frames_t *const frames = context;
    if (back->width == 0u || back->owned_map == nullptr) {
        frames->wrong_size++;
    }
    memset(back->owned_map, (int32_t)(frames->drawn & 255u), back->map_bytes);
    frames->drawn++;
    return frames->drawn < frames->stop_after;
}

static void card_frames_discard(void *context) {
    card_frames_t *const frames = context;
    frames->discards++;
}

/* What game draws through: every frame comes from the source, the run ends when it says so, a VT switch is told, and the console
 * goes back as for the test pattern. */
TEST(frames_from_a_source_reach_the_card_and_the_display_goes_back) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    card->vt_release_after_flips = 20u;
    display_t *const display = rig_alloc_aligned(t, sizeof(display_t));
    display_config_t config = fake_config();
    REQUIRE(display_init(display, &config, &fake_ops, card) == DISPLAY_STATUS_OK);
    card_frames_t frames = {.stop_after = 60u};
    const display_status_t status = display_frames_run(display, card_frames_draw, card_frames_discard, &frames);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(frames.drawn, 60u);
    CHECK_EQ_U64(frames.wrong_size, 0u);
    /* The source's 60th call said to stop, so it put up 59 flips. */
    CHECK(card->flips >= 59u);
    CHECK_EQ_U64(frames.discards, 1u);
    display_deinit(display);
    check_nothing_left(t, card);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    CHECK_EQ_U64(card->is_master, 0u);
}

typedef struct card_cursor_t {
    uint32_t asked;
    uint32_t discards;
    uint32_t asked_at_discard;
    uint32_t top_until; /* asks to spend in the top corner, on the notice, before going to the bottom one */
    uint32_t stop_after;
} card_cursor_t;
static_assert(sizeof(card_cursor_t) == 20);

static bool card_cursor_source(void *context, uint32_t room_x, uint32_t room_y, uint32_t *out_x, uint32_t *out_y,
                               bool *out_active) {
    card_cursor_t *const cursor = context;
    cursor->asked++;
    /* Across the notice, a row of cursor's width at a time, and then well away from it. */
    const bool on_notice = cursor->asked <= cursor->top_until;
    *out_x = on_notice ? cursor->asked * 8u % room_x : room_x;
    *out_y = on_notice ? 10u : room_y;
    *out_active = false;
    return cursor->stop_after == 0u || cursor->asked < cursor->stop_after;
}

static void card_cursor_discard(void *context) {
    card_cursor_t *const cursor = context;
    cursor->discards++;
    cursor->asked_at_discard = cursor->asked;
}

static display_status_t run_card_pattern_with_cursor(fake_card_t *card, display_t *display, card_cursor_t *cursor,
                                                     int64_t duration_ns) {
    display_config_t config = fake_config();
    const display_status_t status = display_init(display, &config, &fake_ops, card);
    if (status != DISPLAY_STATUS_OK) {
        return status;
    }
    display_test_pattern_set_cursor(display, card_cursor_source, card_cursor_discard, cursor);
    return display_test_pattern_run(display, time_now_ns(), duration_ns);
}

TEST(what_the_cursor_source_has_waiting_is_thrown_away_once_on_coming_back_from_another_vt) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    card->vt_release_after_flips = 20u;
    display_t *const display = rig_alloc_aligned(t, sizeof(display_t));
    card_cursor_t cursor = {.stop_after = 40u};
    CHECK_EQ_U64(run_card_pattern_with_cursor(card, display, &cursor, 60000000000), DISPLAY_STATUS_OK);
    CHECK_EQ_U64(cursor.discards, 1u);
    /* Not asked while away: every flip before the switch had its ask, and nothing came between. */
    CHECK(cursor.asked_at_discard >= 20u);
    CHECK(cursor.asked_at_discard <= 21u);
    CHECK_EQ_U64(cursor.asked, 40u);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(with_no_vt_switch_nothing_the_cursor_source_has_is_thrown_away) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_t *const display = rig_alloc_aligned(t, sizeof(display_t));
    card_cursor_t cursor = {.stop_after = 30u};
    CHECK_EQ_U64(run_card_pattern_with_cursor(card, display, &cursor, 60000000000), DISPLAY_STATUS_OK);
    CHECK_EQ_U64(cursor.discards, 0u);
    display_deinit(display);
    check_nothing_left(t, card);
}

/* Erasing the cursor puts back the bare pattern, so without care it would eat the one thing that says how to get out. */
TEST(the_cursor_crossing_the_notice_leaves_it_whole_on_both_buffers) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_t *const display = rig_alloc_aligned(t, sizeof(display_t));
    card_cursor_t cursor = {.top_until = 24u, .stop_after = 40u};
    CHECK_EQ_U64(run_card_pattern_with_cursor(card, display, &cursor, 60000000000), DISPLAY_STATUS_OK);
    for (uint32_t i = 0; i < DISPLAY_FLIP_BUFFERS; i++) {
        const display_buffer_t *const shown = &display->flip.buffers[i];
        const uint32_t bottom = display_vt_guard_notice_bottom(shown);
        REQUIRE(bottom != 0u);
        CHECK(shown->cursor_y >= bottom);
        display_buffer_t expected = notice_buffer(t, shown->width, shown->height);
        display_flip_pattern_fill(&expected);
        display_vt_guard_draw_notice(&expected);
        uint32_t differences = 0;
        for (uint32_t y = 0; y < bottom; y++) {
            const uint32_t *const a = (const uint32_t *)(const void *)(shown->owned_map + (uint64_t)y * shown->pitch);
            const uint32_t *const b =
                (const uint32_t *)(const void *)(expected.owned_map + (uint64_t)y * expected.pitch);
            for (uint32_t x = 0; x < shown->width; x++) {
                differences += a[x] != b[x] ? 1u : 0u;
            }
        }
        CHECK_EQ_U64(differences, 0u);
    }
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(the_test_pattern_ending_leaves_handlers_as_it_found_them) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    struct sigaction before;
    REQUIRE(sigaction(SIGSEGV, nullptr, &before) == 0);
    display_t *const display = rig_alloc_aligned(t, sizeof(display_t));
    CHECK_EQ_U64(run_card_pattern(card, display, DISPLAY_CRASH_TEST_NONE, 0, 20000000), DISPLAY_STATUS_OK);
    struct sigaction after;
    REQUIRE(sigaction(SIGSEGV, nullptr, &after) == 0);
    CHECK(before.sa_handler == after.sa_handler);
    display_deinit(display);
    check_nothing_left(t, card);
}

/* Each --crash-test, all the way from the display's own loop, in a child. Only the guardian's output says what became of it. */
static void check_crash_test(test_context_t *t, display_crash_test_t how, int32_t expected_signal,
                             const char *expected) {
    int32_t ends[2];
    REQUIRE(pipe2(ends, O_CLOEXEC) == 0);
    const int32_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        alarm(RIG_ALARM_SECONDS);
        dup2(ends[1], STDERR_FILENO);
        fake_card_t *const card = fake_card_new(t);
        fake_card_use_small_mode(card);
        display_t *const display = rig_alloc_aligned(t, sizeof(display_t));
        (void)run_card_pattern(card, display, how, RIG_WATCHDOG_NS, 30000000000);
        _exit(RIG_CHILD_FINE);
    }
    close(ends[1]);
    char *const output = test_alloc(t, 8192u);
    uint64_t length = 0;
    for (;;) {
        const int64_t count = read(ends[0], output + length, 8191u - length);
        if (count <= 0) {
            break;
        }
        length += (uint64_t)count;
    }
    output[length] = '\0';
    close(ends[0]);
    int32_t status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ_I64(WTERMSIG(status), expected_signal);
    CHECK_CONTAINS(output, expected);
}

TEST(crash_test_segv_dies_after_its_frames_and_says_the_console_is_back) {
    check_crash_test(t, DISPLAY_CRASH_TEST_SEGV, SIGSEGV, "the program crashed; the console has been given back");
}

TEST(crash_test_abort_dies_of_abort) {
    check_crash_test(t, DISPLAY_CRASH_TEST_ABORT, SIGABRT, "the program crashed; the console has been given back");
}

TEST(crash_test_kill_is_answered_by_the_guardian) {
    check_crash_test(t, DISPLAY_CRASH_TEST_KILL, SIGKILL, "the program ended without giving the console back");
}

TEST(crash_test_hang_is_ended_by_the_watchdog) {
    check_crash_test(t, DISPLAY_CRASH_TEST_HANG, SIGKILL, "the watchdog saw the program stop responding");
}

TEST(crash_test_names_are_what_the_usage_says) {
    CHECK_EQ_U64(display_vt_guard_crash_test_from_name(""), DISPLAY_CRASH_TEST_SEGV);
    CHECK_EQ_U64(display_vt_guard_crash_test_from_name("segv"), DISPLAY_CRASH_TEST_SEGV);
    CHECK_EQ_U64(display_vt_guard_crash_test_from_name("abort"), DISPLAY_CRASH_TEST_ABORT);
    CHECK_EQ_U64(display_vt_guard_crash_test_from_name("kill"), DISPLAY_CRASH_TEST_KILL);
    CHECK_EQ_U64(display_vt_guard_crash_test_from_name("hang"), DISPLAY_CRASH_TEST_HANG);
    CHECK_EQ_U64(display_vt_guard_crash_test_from_name("nope"), DISPLAY_CRASH_TEST_NONE);
    display_vt_guard_crash_now(DISPLAY_CRASH_TEST_NONE); /* returns */
}

/* ---- The program ------------------------------------------------------------------------------------------------- */

static char flag_pattern[] = "--test-pattern";
static char flag_crash[] = "--crash-test";
static char flag_crash_kill[] = "--crash-test=kill";
static char flag_crash_bad[] = "--crash-test=nope";
static char flag_card[] = "--card";
static char no_card[] = "/nonexistent/mc_c_no_such_card";

TEST(the_program_refuses_before_any_crash_test_can_start) {
    REQUIRE(t->arg_count == 1);
    REQUIRE(unsetenv("DISPLAY") == 0);
    REQUIRE(unsetenv("WAYLAND_DISPLAY") == 0);
    char *const argv[] = {t->borrowed_args[0], flag_pattern, flag_crash, flag_card, no_card, nullptr};
    test_process_t result;
    test_process_run(t, argv, 30000u, &result);
    REQUIRE(result.started != 0u);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_DISPLAY_REFUSED);
    CHECK_CONTAINS(result.output, "stdin is not a Linux virtual console");
    /* Refused before the guard: nothing was forked, and no notice. */
    CHECK(strstr(result.output, "EMERGENCY") == nullptr);
    CHECK_CONTAINS(result.output, "heap at exit: 0 reserved, 0 released, 0 bytes live");
}

TEST(the_program_takes_each_form_of_the_flag) {
    REQUIRE(t->arg_count == 1);
    REQUIRE(unsetenv("DISPLAY") == 0);
    REQUIRE(unsetenv("WAYLAND_DISPLAY") == 0);
    char *const argv[] = {t->borrowed_args[0], flag_pattern, flag_crash_kill, flag_card, no_card, nullptr};
    test_process_t result;
    test_process_run(t, argv, 30000u, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_DISPLAY_REFUSED);
}

TEST(crash_test_without_the_pattern_or_with_a_bad_name_is_a_usage_error) {
    REQUIRE(t->arg_count == 1);
    char *const alone[] = {t->borrowed_args[0], flag_crash, nullptr};
    test_process_t result;
    test_process_run(t, alone, 30000u, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(result.output, "only means something with --test-pattern");
    char *const bad[] = {t->borrowed_args[0], flag_pattern, flag_crash_bad, flag_card, no_card, nullptr};
    test_process_run(t, bad, 30000u, &result);
    CHECK_EQ_I64(result.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(result.output, "takes segv, abort, kill or hang");
    CHECK_CONTAINS(result.output, "Ctrl+Alt+Backspace");
}

TEST_MAIN()
