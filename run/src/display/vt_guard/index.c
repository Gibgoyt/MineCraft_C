/* See README.md. The console must come back whatever happens to the program, so two things stand behind it: a handler in
 * this process for what can be caught, and a guardian process for what cannot. Console and card ioctls go through
 * display_ops_t, which is what lets tests run every path here for real, down to dying of a real signal, against a fake
 * kernel. Pipes, polls and reads of the keyboard descriptors are plain system calls, on descriptors the ops table gave us. */
#include "display/vt_guard/index.h"

#include "log/index.h"
#include "time/index.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum : uint32_t {
    GUARD_MAX_KEYBOARDS = 32u,
    GUARD_IOCTL_TRIES = 256u,
    GUARD_EVENTS_AT_ONCE = 32u,
    GUARD_PIPE_BYTES_AT_ONCE = 64u,
    GUARD_RESUME_TRIES = 20u,
    GUARD_FATAL_SIGNALS = 13u,
    GUARD_SAVED_SIGNALS = GUARD_FATAL_SIGNALS + 3u, /* and the two VT signals, and SIGPIPE */
    GUARD_ALT_STACK_BYTES = 65536u,
    GUARD_HELD_LEFT_CTRL = 1u,
    GUARD_HELD_RIGHT_CTRL = 2u,
    GUARD_HELD_LEFT_ALT = 4u,
    GUARD_HELD_RIGHT_ALT = 8u,
    GUARD_NOTICE_GLYPH_ROWS = 7u,
    GUARD_NOTICE_GLYPH_COLUMNS = 5u,
    GUARD_NOTICE_CELL = 6u, /* columns from one character to the next */
    GUARD_NOTICE_MARGIN = 8u,
    GUARD_NOTICE_SCALE_MAX = 6u,
    GUARD_NOTICE_WHITE = 0x00ffffffu,
    GUARD_NOTICE_RED = 0x00c00000u,
};
enum : int32_t {
    GUARD_POLL_MS = 100,
    GUARD_TAKE_DOWN_POLL_MS = 50,
    GUARD_WAIT_STEP_MS = 1,
};
enum : int64_t {
    GUARD_PET_EVERY_NS = 250000000,
    GUARD_TAKE_DOWN_NS = 1000000000, /* for each of SIGTERM and SIGKILL to work, before going ahead without them */
    GUARD_RESTORE_WAIT_NS = 2000000000,
    GUARD_REAP_WAIT_NS = 2000000000,
    GUARD_HANG_NS = 60000000000,
    GUARD_RESCAN_EVERY_NS = 1000000000,
};
enum : uint8_t {
    GUARD_PIPE_PET = 'p',
    GUARD_PIPE_DONE_BYTE = 'D', /* given back by the program itself: the guardian has nothing left to do */
};

/* The order is the point: the display first, since it decides whether anything can be seen; only then the rest. */
typedef enum guard_step_t : uint32_t {
    GUARD_STEP_DISPLAY = 0,
    GUARD_STEP_KEYBOARD = 1,
    GUARD_STEP_VT_MODE = 2,
    GUARD_STEP_TELL_GUARDIAN = 3,
    GUARD_STEP_COUNT = 4,
} guard_step_t;

typedef struct display_vt_guard_saved_t {
    struct sigaction saved[GUARD_SAVED_SIGNALS];
    stack_t saved_stack;
    uint8_t
        alt_stack[GUARD_ALT_STACK_BYTES]; /* a stack overflow is a crash too, and the handler needs somewhere to run */
} display_vt_guard_saved_t;
static_assert(sizeof(display_vt_guard_saved_t) ==
              sizeof(struct sigaction) * GUARD_SAVED_SIGNALS + sizeof(stack_t) + GUARD_ALT_STACK_BYTES);

/* Everything that kills a process by default and can be caught, apart from the four that display/ handles as a request to
 * stop: SIGTERM, SIGINT, SIGHUP and SIGQUIT. */
static const int32_t guard_fatal_signals[GUARD_FATAL_SIGNALS] = {
    SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGSYS, SIGTRAP, SIGXCPU, SIGXFSZ, SIGALRM, SIGVTALRM, SIGPROF, SIGSTKFLT,
};

/* The guard, for the handlers. A signal handler is given nothing but the signal number, so this static is the only way a
 * crash can reach the state it must restore: the one case plans/CODING_STANDARD.md section 4 allows. It owns nothing. The
 * guard lives in app_t, on main's stack, and this is cleared before that frame goes away. */
static _Atomic(display_vt_guard_t *) borrowed_vt_guard;

/* ---- What a signal handler may safely use ------------------------------------------------------------------------ */

static void guard_say(const char *text) {
    const int64_t written = write(STDERR_FILENO, text, strlen(text));
    (void)written; /* there is nobody left to tell */
}

static void guard_wait_ms(int32_t milliseconds) {
    const struct timespec wait = {.tv_sec = milliseconds / 1000, .tv_nsec = (int64_t)(milliseconds % 1000) * 1000000};
    nanosleep(&wait, nullptr);
}

static int32_t guard_console_ioctl(const display_vt_guard_t *guard, uint64_t request, void *argument) {
    const display_device_t *const device = guard->borrowed_device;
    int32_t result = -EINTR;
    for (uint32_t i = 0; i < GUARD_IOCTL_TRIES && (result == -EINTR || result == -EAGAIN); i++) {
        result = guard->borrowed_ops->ioctl_call(guard->borrowed_context, device->console_fd, request, argument);
    }
    return result;
}

static void guard_run_step(display_vt_guard_t *guard, uint32_t step) {
    bool ok = true;
    switch (step) {
    case GUARD_STEP_DISPLAY:
        ok = display_flip_restore(guard->borrowed_flip);
        break;
    case GUARD_STEP_KEYBOARD:
        if (guard->keyboard_off != 0u) {
            /* KDSKBMODE takes the value itself. */
            ok = guard_console_ioctl(guard, KDSKBMODE, (void *)(uintptr_t)guard->saved_kb_mode) == 0;
        }
        break;
    case GUARD_STEP_VT_MODE:
        if (guard->vt_process != 0u) {
            /* A switch may be waiting on our answer, and nobody else will give it: let it through. With none waiting the
             * kernel says EINVAL, which means nothing here. Before VT_SETMODE, which would make it say so regardless. */
            (void)guard_console_ioctl(guard, VT_RELDISP, (void *)(uintptr_t)1u);
            ok = guard_console_ioctl(guard, VT_SETMODE, &guard->saved_vt_mode) == 0;
        }
        break;
    case GUARD_STEP_TELL_GUARDIAN:
        /* Only if every step before this finished. One that faulted is still owed, and the guardian can still do it. */
        if (guard->pipe_fd >= 0 && atomic_load_explicit(&guard->restore_done, memory_order_acquire) == step) {
            const uint8_t done = GUARD_PIPE_DONE_BYTE;
            const int64_t written = write(guard->pipe_fd, &done, 1u);
            (void)written; /* failing means it will see us gone instead, and do it again harmlessly */
        }
        break;
    default:
        break;
    }
    if (!ok) {
        atomic_store_explicit(&guard->restore_failed, 1u, memory_order_relaxed);
    }
}

/* Safe in a signal handler, from any thread, at any time. One flat pass over the steps, each claimed once: a second caller,
 * such as another thread's crash, picks up at the next unclaimed step and never repeats one. Nothing here runs inside
 * itself: the crash handler blocks every crash signal while it runs, so a fault inside a step ends the process there, with
 * every earlier step's part of the console already back, and the guardian finishes the rest. */
static void guard_restore_all(display_vt_guard_t *guard) {
    for (;;) {
        const uint32_t step = atomic_fetch_add_explicit(&guard->restore_next, 1u, memory_order_acq_rel);
        if (step >= GUARD_STEP_COUNT) {
            break;
        }
        guard_run_step(guard, step);
        atomic_fetch_add_explicit(&guard->restore_done, 1u, memory_order_acq_rel);
    }
    /* Another thread may hold a step. Dying now would cut it off, so wait for it, but not for ever: that thread may have
     * died inside it, and then the guardian finishes the job. */
    const int64_t started_ns = time_now_ns();
    while (atomic_load_explicit(&guard->restore_done, memory_order_acquire) < GUARD_STEP_COUNT &&
           time_now_ns() - started_ns < GUARD_RESTORE_WAIT_NS) {
        guard_wait_ms(GUARD_WAIT_STEP_MS);
    }
}

/* ---- The handlers ------------------------------------------------------------------------------------------------ */

static void guard_on_crash(int32_t signal_number) {
    display_vt_guard_t *const guard = atomic_load_explicit(&borrowed_vt_guard, memory_order_acquire);
    if (guard != nullptr) {
        guard_restore_all(guard);
        guard_say("\nvt_guard: the program crashed; the console has been given back\n");
    }
    /* Die of the same signal, so that the exit status and any core dump say what really happened. It is blocked while
     * this handler runs, so it is let through here, with no handler, and arrives at once. If it somehow does not, the
     * faulting instruction runs again on return, with no handler. */
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signal_number, &action, nullptr);
    sigset_t own;
    sigemptyset(&own);
    sigaddset(&own, signal_number);
    raise(signal_number);
    sigprocmask(SIG_UNBLOCK, &own, nullptr);
}

static void guard_on_release(int32_t signal_number) {
    (void)signal_number;
    display_vt_guard_t *const guard = atomic_load_explicit(&borrowed_vt_guard, memory_order_acquire);
    if (guard != nullptr) {
        atomic_store_explicit(&guard->release_wanted, 1u, memory_order_relaxed);
    }
}

static void guard_on_acquire(int32_t signal_number) {
    (void)signal_number;
    display_vt_guard_t *const guard = atomic_load_explicit(&borrowed_vt_guard, memory_order_acquire);
    if (guard != nullptr) {
        atomic_store_explicit(&guard->acquire_wanted, 1u, memory_order_relaxed);
    }
}

static void guard_install_handlers(display_vt_guard_t *guard) {
    display_vt_guard_saved_t *const saved = guard->borrowed_saved;
    atomic_store_explicit(&borrowed_vt_guard, guard, memory_order_release);

    const stack_t stack = {.ss_sp = saved->alt_stack, .ss_flags = 0, .ss_size = sizeof(saved->alt_stack)};
    sigaltstack(&stack, &saved->saved_stack);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    /* Every crash signal is blocked while the handler runs, so it is never entered again from inside itself. A fault inside
     * it, of any of them, then gets the kernel's default action: the process ends there, and the guardian finishes. */
    sigemptyset(&action.sa_mask);
    for (uint32_t i = 0; i < GUARD_FATAL_SIGNALS; i++) {
        sigaddset(&action.sa_mask, guard_fatal_signals[i]);
    }
    action.sa_flags = SA_ONSTACK;
    action.sa_handler = guard_on_crash;
    for (uint32_t i = 0; i < GUARD_FATAL_SIGNALS; i++) {
        sigaction(guard_fatal_signals[i], &action, &saved->saved[i]);
    }
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    action.sa_handler = guard_on_release;
    sigaction(SIGUSR1, &action, &saved->saved[GUARD_FATAL_SIGNALS]);
    action.sa_handler = guard_on_acquire;
    sigaction(SIGUSR2, &action, &saved->saved[GUARD_FATAL_SIGNALS + 1u]);
    /* The guardian may be dead when we pet it. That is no reason to die as well. */
    action.sa_flags = 0;
    action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &action, &saved->saved[GUARD_FATAL_SIGNALS + 2u]);
}

static void guard_remove_handlers(display_vt_guard_t *guard) {
    display_vt_guard_saved_t *const saved = guard->borrowed_saved;
    for (uint32_t i = 0; i < GUARD_FATAL_SIGNALS; i++) {
        sigaction(guard_fatal_signals[i], &saved->saved[i], nullptr);
    }
    sigaction(SIGUSR1, &saved->saved[GUARD_FATAL_SIGNALS], nullptr);
    sigaction(SIGUSR2, &saved->saved[GUARD_FATAL_SIGNALS + 1u], nullptr);
    sigaction(SIGPIPE, &saved->saved[GUARD_FATAL_SIGNALS + 2u], nullptr);
    sigaltstack(&saved->saved_stack, nullptr);
    atomic_store_explicit(&borrowed_vt_guard, nullptr, memory_order_release);
}

/* ---- The keys ---------------------------------------------------------------------------------------------------- */

static uint8_t guard_held_bit(uint32_t code) {
    switch (code) {
    case KEY_LEFTCTRL:
        return GUARD_HELD_LEFT_CTRL;
    case KEY_RIGHTCTRL:
        return GUARD_HELD_RIGHT_CTRL;
    case KEY_LEFTALT:
        return GUARD_HELD_LEFT_ALT;
    case KEY_RIGHTALT:
        return GUARD_HELD_RIGHT_ALT;
    default:
        return 0u;
    }
}

int32_t display_vt_guard_keys_take(display_vt_guard_keys_t *keys, uint32_t code, int32_t value) {
    const uint8_t bit = guard_held_bit(code);
    if (bit != 0u) {
        keys->held = (uint8_t)(value != 0 ? keys->held | bit : keys->held & ~bit);
        return 0;
    }
    /* Pressed, not held: a key repeat must not switch again, and neither may a key let go of. */
    const bool ctrl = (keys->held & (GUARD_HELD_LEFT_CTRL | GUARD_HELD_RIGHT_CTRL)) != 0u;
    const bool alt = (keys->held & (GUARD_HELD_LEFT_ALT | GUARD_HELD_RIGHT_ALT)) != 0u;
    if (value != 1 || !ctrl || !alt) {
        return 0;
    }
    if (code == KEY_BACKSPACE) {
        return DISPLAY_VT_GUARD_KEY_EMERGENCY;
    }
    if (code >= KEY_F1 && code <= KEY_F10) {
        return (int32_t)(code - KEY_F1) + 1;
    }
    if (code == KEY_F11) {
        return 11;
    }
    return code == KEY_F12 ? 12 : 0;
}

/* ---- The guardian ------------------------------------------------------------------------------------------------ */

/* A second process, forked before the display is taken, which holds its own copy of everything needed to give it back and
 * shares the card and console descriptors. What it adds over the handler above: it outlives SIGKILL, and a stack overflow on
 * any thread, and whatever scribbled over our memory; it reads the keyboard however stuck the program is; and it can give
 * the console back even with the program stuck in a driver call, where no signal would ever run. It touches no shared state
 * and calls only async-signal-safe functions, since it may be forked from a program with other threads. */
typedef struct guard_guardian_t {
    display_vt_guard_t *borrowed_guard; /* the guardian's copy */
    int64_t last_pet_ns;
    int64_t last_scan_ns;
    struct pollfd fds[GUARD_MAX_KEYBOARDS + 1u]; /* the pipe first, then the keyboards */
    uint32_t fd_count;
    int32_t program_pid;
    display_vt_guard_keys_t keys;
    uint8_t rescan_wanted; /* a keyboard went away, or the last look found none */
    uint8_t _pad0[6];
} guard_guardian_t;
static_assert(sizeof(guard_guardian_t) == 40 + sizeof(struct pollfd) * (GUARD_MAX_KEYBOARDS + 1u));

typedef enum guard_pipe_news_t : uint8_t {
    GUARD_PIPE_ALIVE = 0,
    GUARD_PIPE_DONE = 1,  /* the program gave everything back by itself */
    GUARD_PIPE_ENDED = 2, /* the program is gone, and it did not say so first */
} guard_pipe_news_t;

static guard_pipe_news_t guard_pipe_read(guard_guardian_t *guardian) {
    uint8_t bytes[GUARD_PIPE_BYTES_AT_ONCE];
    for (;;) {
        const int64_t count = read(guardian->fds[0].fd, bytes, sizeof(bytes));
        if (count == 0) {
            return GUARD_PIPE_ENDED;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return errno == EAGAIN ? GUARD_PIPE_ALIVE : GUARD_PIPE_ENDED;
        }
        guardian->last_pet_ns = time_now_ns();
        if (memchr(bytes, GUARD_PIPE_DONE_BYTE, (uint64_t)count) != nullptr) {
            return GUARD_PIPE_DONE;
        }
    }
}

/* The guardian's whole job, once it is sure the program is not going to do it. Never returns. */
[[noreturn]] static void guard_guardian_give_back(guard_guardian_t *guardian, const char *why) {
    display_vt_guard_t *const guard = guardian->borrowed_guard;
    /* This copy was made before the takeover, so it does not know what the program went on to take. Every step is safe
     * whether or not it is still owed: on another VT's time, SETCRTC fails since we are not master, and KD_TEXT and the
     * keyboard mode apply only to our VT.
     * Unless this is root, the kernel answers this process's DROP_MASTER with EACCES: drm_master_check_perm wants the thread
     * group that opened the card, and that was the program's. SETCRTC is not held to that, so the saved picture still goes
     * back. Master itself goes when the card's last descriptor does, and that is this process's, in the _exit below. */
    display_flip_assume_taken(guard->borrowed_flip);
    guard->keyboard_off = (uint8_t)(guard->keyboard_count != 0u);
    guard->vt_process = 1u;
    guard->pipe_fd = -1;
    guard_restore_all(guard);
    guard_say("\nvt_guard: ");
    guard_say(why);
    guard_say("; the console has been given back\n");
    _exit(0);
}

/* Waits for the program to go. True if it ended, whether by telling us or by the pipe closing. On DONE, exits. */
static bool guard_guardian_wait_gone(guard_guardian_t *guardian) {
    const int64_t started_ns = time_now_ns();
    while (time_now_ns() - started_ns < GUARD_TAKE_DOWN_NS) {
        struct pollfd wait = {.fd = guardian->fds[0].fd, .events = POLLIN, .revents = 0};
        if (poll(&wait, 1u, GUARD_TAKE_DOWN_POLL_MS) > 0) {
            const guard_pipe_news_t news = guard_pipe_read(guardian);
            if (news == GUARD_PIPE_DONE) {
                guard_say("\nvt_guard: the program stopped when asked, and gave the console back\n");
                _exit(0);
            }
            if (news == GUARD_PIPE_ENDED) {
                return true;
            }
        }
    }
    return false;
}

/* SIGTERM first, so that a program able to answer gets to report what it measured. One that is hung gets SIGKILL, and if
 * even that does nothing, it is stuck in a driver call, and the console is given back regardless. */
[[noreturn]] static void guard_guardian_take_down(guard_guardian_t *guardian, const char *why) {
    guard_say("\nvt_guard: ");
    guard_say(why);
    guard_say(": ending the program\n");
    kill(guardian->program_pid, SIGTERM);
    if (!guard_guardian_wait_gone(guardian)) {
        kill(guardian->program_pid, SIGKILL);
        (void)guard_guardian_wait_gone(guardian);
    }
    guard_guardian_give_back(guardian, why);
}

static bool guard_guardian_our_vt_is_showing(const guard_guardian_t *guardian) {
    const display_vt_guard_t *const guard = guardian->borrowed_guard;
    struct vt_stat state = {.v_active = 0, .v_signal = 0, .v_state = 0};
    return guard_console_ioctl(guard, VT_GETSTATE, &state) != 0 ||
           (uint32_t)state.v_active == guard->borrowed_device->console_minor;
}

static void guard_guardian_take_key(guard_guardian_t *guardian, const struct input_event *event) {
    if (event->type != EV_KEY) {
        return;
    }
    const int32_t asked = display_vt_guard_keys_take(&guardian->keys, event->code, event->value);
    /* The keyboard is read whichever VT is showing, so these keys may be meant for X. Only ours count. */
    if (asked == 0 || !guard_guardian_our_vt_is_showing(guardian)) {
        return;
    }
    if (asked == DISPLAY_VT_GUARD_KEY_EMERGENCY) {
        guard_guardian_take_down(guardian, "Ctrl+Alt+Backspace was pressed");
    }
    /* The kernel sends us SIGUSR1 for this, the same as for a switch from anywhere else. */
    const display_vt_guard_t *const guard = guardian->borrowed_guard;
    if ((uint32_t)asked != guard->borrowed_device->console_minor) {
        (void)guard_console_ioctl(guard, VT_ACTIVATE, (void *)(uintptr_t)(uint32_t)asked);
    }
}

static void guard_guardian_read_keyboard(guard_guardian_t *guardian, struct pollfd *keyboard) {
    struct input_event events[GUARD_EVENTS_AT_ONCE];
    const int64_t count = read(keyboard->fd, events, sizeof(events));
    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
        /* Unplugged. poll ignores -1 from now on, and K_OFF has left nothing else to get out with, so go and look for it. */
        close(keyboard->fd);
        keyboard->fd = -1;
        guardian->rescan_wanted = 1u;
        return;
    }
    for (int64_t i = 0; i < count / (int64_t)sizeof(events[0]); i++) {
        guard_guardian_take_key(guardian, &events[i]);
    }
}

/* Opens what is there now and only then lets go of the old ones, after one last read, so that no key falls in between; a key
 * seen on both counts the same twice. A key let go of on a keyboard that has gone was never seen, so nothing counts as held
 * any more: the combination must be pressed afresh. If nothing is there, the main loop comes back here until there is. */
static void guard_guardian_rescan_keyboards(guard_guardian_t *guardian) {
    const display_vt_guard_t *const guard = guardian->borrowed_guard;
    int32_t fresh[GUARD_MAX_KEYBOARDS];
    const uint32_t count = guard->borrowed_ops->open_keyboards(guard->borrowed_context, fresh, GUARD_MAX_KEYBOARDS);
    for (uint32_t i = 1; i < guardian->fd_count; i++) {
        if (guardian->fds[i].fd >= 0) {
            guard_guardian_read_keyboard(guardian, &guardian->fds[i]);
        }
        if (guardian->fds[i].fd >= 0) {
            close(guardian->fds[i].fd);
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        guardian->fds[i + 1u] = (struct pollfd){.fd = fresh[i], .events = POLLIN, .revents = 0};
    }
    guardian->fd_count = count + 1u;
    guardian->keys.held = 0u;
    guardian->rescan_wanted = (uint8_t)(count == 0u);
    guardian->last_scan_ns = time_now_ns();
}

[[noreturn]] static void guard_guardian_main(guard_guardian_t *guardian) {
    display_vt_guard_t *const guard = guardian->borrowed_guard;
    /* A group of its own, since `timeout` signals its whole group, and this must outlive the program. No setsid: the KD and VT
     * ioctls need this tty as the controlling terminal. It ends when the pipe does, so it cannot be left behind. */
    setpgid(0, 0);
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    static const int32_t ignored[] = {SIGTERM, SIGINT,  SIGHUP,  SIGQUIT, SIGTSTP,
                                      SIGTTOU, SIGTTIN, SIGPIPE, SIGUSR1, SIGUSR2};
    for (uint32_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
        sigaction(ignored[i], &action, nullptr);
    }
    sigset_t none;
    sigemptyset(&none);
    pthread_sigmask(SIG_SETMASK, &none, nullptr); /* blocked by guard_start_guardian, until those were ignored */
    for (;;) {
        const int32_t ready = poll(guardian->fds, guardian->fd_count, GUARD_POLL_MS);
        if (ready > 0) {
            if (guardian->fds[0].revents != 0) {
                const guard_pipe_news_t news = guard_pipe_read(guardian);
                if (news == GUARD_PIPE_DONE) {
                    _exit(0);
                }
                if (news == GUARD_PIPE_ENDED) {
                    guard_guardian_give_back(guardian, "the program ended without giving the console back");
                }
            }
            for (uint32_t i = 1; i < guardian->fd_count; i++) {
                if (guardian->fds[i].fd >= 0 && guardian->fds[i].revents != 0) {
                    guard_guardian_read_keyboard(guardian, &guardian->fds[i]);
                }
            }
        }
        if (guardian->rescan_wanted != 0u && time_now_ns() - guardian->last_scan_ns >= GUARD_RESCAN_EVERY_NS) {
            guard_guardian_rescan_keyboards(guardian);
        }
        if (time_now_ns() - guardian->last_pet_ns > guard->watchdog_ns) {
            guard_guardian_take_down(guardian, "the watchdog saw the program stop responding");
        }
    }
}

static bool guard_start_guardian(display_vt_guard_t *guard, const int32_t *keyboards, uint32_t keyboard_count) {
    int32_t ends[2];
    if (pipe2(ends, O_CLOEXEC | O_NONBLOCK) != 0) {
        guard->failed_call = "pipe for the guardian";
        guard->failed_errno = errno;
        return false;
    }
    const int32_t program_pid = getpid();
    /* Blocked across the fork: a signal sent to the guardian before it has set up to ignore them would otherwise kill it. */
    sigset_t all_signals;
    sigset_t previous_signals;
    sigfillset(&all_signals);
    pthread_sigmask(SIG_SETMASK, &all_signals, &previous_signals);
    const int32_t pid = fork();
    if (pid != 0) {
        pthread_sigmask(SIG_SETMASK, &previous_signals, nullptr);
    }
    if (pid < 0) {
        guard->failed_call = "fork of the guardian";
        guard->failed_errno = errno;
        close(ends[0]);
        close(ends[1]);
        return false;
    }
    if (pid == 0) {
        close(ends[1]); /* or it would hold its own pipe open, and never see the program go */
        guard_guardian_t guardian;
        memset(&guardian, 0, sizeof(guardian));
        guardian.borrowed_guard = guard;
        guardian.program_pid = program_pid;
        guardian.last_pet_ns = time_now_ns();
        guardian.fd_count = keyboard_count + 1u;
        guardian.fds[0] = (struct pollfd){.fd = ends[0], .events = POLLIN, .revents = 0};
        for (uint32_t i = 0; i < keyboard_count; i++) {
            guardian.fds[i + 1u] = (struct pollfd){.fd = keyboards[i], .events = POLLIN, .revents = 0};
        }
        guard_guardian_main(&guardian);
    }
    close(ends[0]);
    setpgid(pid, pid); /* here too, or a signal to our group straight after the fork could still reach it */
    guard->guardian_pid = pid;
    guard->pipe_fd = ends[1];
    return true;
}

static bool guard_reap_guardian(display_vt_guard_t *guard) {
    if (guard->guardian_pid == 0) {
        return true;
    }
    const int64_t started_ns = time_now_ns();
    bool gone = false;
    while (!gone && time_now_ns() - started_ns < GUARD_REAP_WAIT_NS) {
        const int32_t waited = waitpid(guard->guardian_pid, nullptr, WNOHANG);
        gone = waited == guard->guardian_pid || (waited < 0 && errno != EINTR);
        if (!gone) {
            guard_wait_ms(GUARD_WAIT_STEP_MS);
        }
    }
    if (!gone) {
        kill(guard->guardian_pid, SIGKILL);
        waitpid(guard->guardian_pid, nullptr, 0);
    }
    guard->guardian_pid = 0;
    return gone;
}

/* ---- Taking the console and giving it back ----------------------------------------------------------------------- */

void display_vt_guard_init(display_vt_guard_t *guard, display_device_t *device, display_flip_t *flip, arena_t *arena) {
    memset(guard, 0, sizeof(*guard));
    guard->borrowed_ops = device->borrowed_ops;
    guard->borrowed_context = device->borrowed_context;
    guard->borrowed_device = device;
    guard->borrowed_flip = flip;
    guard->borrowed_saved = arena_alloc(arena, sizeof(display_vt_guard_saved_t), alignof(display_vt_guard_saved_t));
    guard->watchdog_ns = DISPLAY_VT_GUARD_WATCHDOG_NS;
    guard->pipe_fd = -1;
}

void display_vt_guard_deinit(display_vt_guard_t *guard) {
    if (guard->borrowed_ops != nullptr && guard->armed != 0u) {
        if (!display_vt_guard_give_back(guard)) {
            LOG_WARN("vt_guard: at least one ioctl failed while giving the console back");
        }
    }
    memset(guard, 0, sizeof(*guard));
    guard->pipe_fd = -1;
}

/* Said before the console goes dark, and held there, because after that there is nowhere to say it. */
static void guard_print_notice(const display_vt_guard_t *guard) {
    fprintf(stderr, "\n"
                    "=================================================================\n");
    if (guard->keyboard_count != 0u) {
        fprintf(stderr,
                "  EMERGENCY EXIT:  hold Ctrl+Alt and press Backspace\n"
                "  It ends the program and gives the console back, at any time.\n"
                "  Watching %" PRIu32 " keyboard(s). The keyboard is otherwise off for this console:\n"
                "  Ctrl+Alt+F1 to F12 still switch virtual console, and nothing else\n"
                "  you type reaches the shell.\n",
                guard->keyboard_count);
    } else {
        fprintf(stderr, "  NO KEYBOARD COULD BE OPENED under /dev/input, so Ctrl+Alt+Backspace WILL NOT WORK.\n"
                        "  The keyboard is left on instead: Ctrl+C ends the program, and\n"
                        "  Ctrl+Alt+F1 to F12 switch virtual console. Join group input to fix this.\n");
    }
    fprintf(stderr,
            "  A hang of %" PRId64 " seconds ends it too, by itself.\n"
            "=================================================================\n\n",
            guard->watchdog_ns / (int64_t)TIME_NS_PER_SECOND);
    fflush(stderr);
}

static void guard_record_failure(display_vt_guard_t *guard, const char *call, int32_t error) {
    if (guard->failed_call == nullptr) {
        guard->failed_call = call;
        guard->failed_errno = error;
    }
}

bool display_vt_guard_take_console(display_vt_guard_t *guard) {
    /* Both saved first: nothing is changed until we know what to change it back to. */
    int32_t result = guard_console_ioctl(guard, KDGKBMODE, &guard->saved_kb_mode);
    if (result != 0) {
        guard_record_failure(guard, "KDGKBMODE", -result);
        return false;
    }
    result = guard_console_ioctl(guard, VT_GETMODE, &guard->saved_vt_mode);
    if (result != 0) {
        guard_record_failure(guard, "VT_GETMODE", -result);
        return false;
    }

    int32_t keyboards[GUARD_MAX_KEYBOARDS];
    guard->keyboard_count =
        guard->borrowed_ops->open_keyboards(guard->borrowed_context, keyboards, GUARD_MAX_KEYBOARDS);
    guard_print_notice(guard);
    guard->borrowed_ops->pause_ms(guard->borrowed_context, DISPLAY_VT_GUARD_NOTICE_MS);

    const bool started = guard_start_guardian(guard, keyboards, guard->keyboard_count);
    for (uint32_t i = 0; i < guard->keyboard_count; i++) {
        close(keyboards[i]); /* the guardian has its own, and this process has no use for them */
    }
    if (!started) {
        return false; /* nothing is taken without the guardian: the display stays as it was */
    }
    guard_install_handlers(guard);
    guard->armed = 1u;
    guard->last_pet_ns = time_now_ns();

    struct vt_mode wanted = {.mode = VT_PROCESS, .waitv = 0, .relsig = SIGUSR1, .acqsig = SIGUSR2, .frsig = 0};
    /* Flagged before the call, so that a call that half worked is undone. */
    guard->vt_process = 1u;
    result = guard_console_ioctl(guard, VT_SETMODE, &wanted);
    if (result != 0) {
        guard_record_failure(guard, "VT_SETMODE(VT_PROCESS)", -result);
        return false;
    }
    /* K_OFF only with a keyboard to read: otherwise it would leave nothing at all to get out with. */
    if (guard->keyboard_count != 0u) {
        guard->keyboard_off = 1u;
        result = guard_console_ioctl(guard, KDSKBMODE, (void *)(uintptr_t)K_OFF);
        if (result != 0) {
            guard_record_failure(guard, "KDSKBMODE(K_OFF)", -result);
            return false;
        }
    }
    /* A switch before VT_PROCESS was the kernel's to make, and nobody here heard of it. The picture must not go over whatever is showing now, and that VT's
     * owner needs the card: this has held master since display_init. */
    if (!display_device_console_is_visible(guard->borrowed_device)) {
        (void)display_device_drop_master(guard->borrowed_device);
        guard->take_over_owed = 1u;
        guard->away = 1u;
        return true;
    }
    /* Back just as VT_PROCESS came in: the kernel is waiting on the answer. */
    if (atomic_exchange_explicit(&guard->acquire_wanted, 0u, memory_order_relaxed) != 0u) {
        (void)guard_console_ioctl(guard, VT_RELDISP, (void *)(uintptr_t)VT_ACKACQ);
    }
    return display_flip_take_over(guard->borrowed_flip);
}

bool display_vt_guard_give_back(display_vt_guard_t *guard) {
    if (guard->armed == 0u) {
        /* Never armed, or already done. display_flip_restore keeps its own answer. */
        return display_flip_restore(guard->borrowed_flip);
    }
    guard_restore_all(guard);
    guard_remove_handlers(guard);
    guard->armed = 0u;
    if (guard->pipe_fd >= 0) {
        close(guard->pipe_fd);
        guard->pipe_fd = -1;
    }
    const bool reaped = guard_reap_guardian(guard);
    if (!reaped) {
        guard_record_failure(guard, "the guardian did not end, and was killed", 0);
    }
    return atomic_load_explicit(&guard->restore_failed, memory_order_relaxed) == 0u;
}

void display_vt_guard_pet(display_vt_guard_t *guard) {
    const int64_t now_ns = time_now_ns();
    if (guard->pipe_fd < 0 || now_ns - guard->last_pet_ns < GUARD_PET_EVERY_NS) {
        return;
    }
    guard->last_pet_ns = now_ns;
    const uint8_t pet = GUARD_PIPE_PET;
    const int64_t written = write(guard->pipe_fd, &pet, 1u);
    (void)written; /* full means it is far behind; gone means there is no one to pet. Neither stops the game */
}

static display_vt_guard_state_t guard_come_back(display_vt_guard_t *guard) {
    display_flip_t *const flip = guard->borrowed_flip;
    const int32_t acknowledged = guard_console_ioctl(guard, VT_RELDISP, (void *)(uintptr_t)VT_ACKACQ);
    if (acknowledged != 0) {
        guard_record_failure(guard, "VT_RELDISP(VT_ACKACQ)", -acknowledged);
        return DISPLAY_VT_GUARD_FAILED;
    }
    /* Whoever had the card may take a moment to let go of it. */
    for (uint32_t i = 0; i < GUARD_RESUME_TRIES; i++) {
        if (guard->take_over_owed != 0u && display_device_set_master(guard->borrowed_device) == 0) {
            guard->take_over_owed = 0u;
            guard->away = 0u;
            guard->came_back = 1u;
            return display_flip_take_over(flip) ? DISPLAY_VT_GUARD_SHOWING : DISPLAY_VT_GUARD_FAILED;
        }
        if (guard->take_over_owed == 0u && display_flip_resume(flip)) {
            guard->away = 0u;
            guard->came_back = 1u;
            return DISPLAY_VT_GUARD_SHOWING;
        }
        if (flip->failed_call != nullptr) {
            return DISPLAY_VT_GUARD_FAILED;
        }
        display_vt_guard_pet(guard);
        guard->borrowed_ops->pause_ms(guard->borrowed_context, DISPLAY_VT_GUARD_AWAY_MS);
    }
    guard_record_failure(guard, "SET_MASTER after a VT switch: another client kept the card", 0);
    return DISPLAY_VT_GUARD_FAILED;
}

display_vt_guard_state_t display_vt_guard_service(display_vt_guard_t *guard) {
    if (guard->away != 0u) {
        if (atomic_exchange_explicit(&guard->acquire_wanted, 0u, memory_order_relaxed) != 0u) {
            return guard_come_back(guard);
        }
        guard->borrowed_ops->pause_ms(guard->borrowed_context, DISPLAY_VT_GUARD_AWAY_MS);
        return DISPLAY_VT_GUARD_AWAY;
    }
    if (atomic_exchange_explicit(&guard->release_wanted, 0u, memory_order_relaxed) == 0u) {
        return DISPLAY_VT_GUARD_SHOWING;
    }
    display_flip_drain(guard->borrowed_flip);
    /* Its failures are not worth stopping for: the other VT cannot appear until the answer below is given. */
    (void)display_flip_suspend(guard->borrowed_flip);
    guard->away = 1u;
    const int32_t released = guard_console_ioctl(guard, VT_RELDISP, (void *)(uintptr_t)1u);
    if (released != 0) {
        guard_record_failure(guard, "VT_RELDISP", -released);
        return DISPLAY_VT_GUARD_FAILED;
    }
    return DISPLAY_VT_GUARD_AWAY;
}

void display_vt_guard_long_work(display_vt_guard_t *guard) {
    display_vt_guard_pet(guard);
    /* Failed is for the frame's end to act on. Asked again, it would wait on a switch that will never be answered. */
    if (guard->failed_call == nullptr && guard->borrowed_flip->failed_call == nullptr) {
        (void)display_vt_guard_service(guard);
    }
}

bool display_vt_guard_came_back_take(display_vt_guard_t *guard) {
    const bool came_back = guard->came_back != 0u;
    guard->came_back = 0u;
    return came_back;
}

/* ---- The notice on screen ---------------------------------------------------------------------------------------- */

typedef struct guard_glyph_t {
    uint8_t character;
    uint8_t rows[GUARD_NOTICE_GLYPH_ROWS]; /* five columns each, in the low bits, the leftmost highest */
} guard_glyph_t;
static_assert(sizeof(guard_glyph_t) == 8);

/* Only what the notice needs. ui/font is for the game (Stage 14.1), and this must not depend on anything that high. */
static const guard_glyph_t guard_glyphs[] = {
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}}, {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}}, {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}}, {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}}, {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}}, {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}}, {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
    {'+', {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}}, {'=', {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00}},
};

static const char guard_notice_text[] = "CTRL+ALT+BACKSPACE = EMERGENCY EXIT";

static const guard_glyph_t *guard_glyph_for(char character) {
    for (uint32_t i = 0; i < sizeof(guard_glyphs) / sizeof(guard_glyphs[0]); i++) {
        if (guard_glyphs[i].character == (uint8_t)character) {
            return &guard_glyphs[i];
        }
    }
    return nullptr; /* a space */
}

/* 0 if the notice does not fit. */
static uint32_t guard_notice_scale(const display_buffer_t *buffer) {
    const uint32_t cells = ((uint32_t)sizeof(guard_notice_text) - 1u) * GUARD_NOTICE_CELL;
    if (buffer->width <= GUARD_NOTICE_MARGIN * 2u || buffer->height <= GUARD_NOTICE_MARGIN * 2u) {
        return 0u;
    }
    uint32_t scale = (buffer->width - GUARD_NOTICE_MARGIN * 2u) / cells;
    scale = scale < GUARD_NOTICE_SCALE_MAX ? scale : GUARD_NOTICE_SCALE_MAX;
    /* A band at most an eighth of the screen, so that the moving square in the middle never reaches it. */
    return (GUARD_NOTICE_GLYPH_ROWS + 2u) * scale > buffer->height / 8u ? 0u : scale;
}

uint32_t display_vt_guard_notice_bottom(const display_buffer_t *buffer) {
    const uint32_t scale = guard_notice_scale(buffer);
    return scale == 0u ? 0u : GUARD_NOTICE_MARGIN + (GUARD_NOTICE_GLYPH_ROWS + 2u) * scale;
}

void display_vt_guard_draw_notice(display_buffer_t *buffer) {
    const uint32_t length = (uint32_t)sizeof(guard_notice_text) - 1u;
    const uint32_t cells = length * GUARD_NOTICE_CELL;
    const uint32_t scale = guard_notice_scale(buffer);
    if (scale == 0u) {
        return;
    }
    const uint32_t band_height = (GUARD_NOTICE_GLYPH_ROWS + 2u) * scale;
    const uint32_t left = (buffer->width - cells * scale) / 2u;
    for (uint32_t y = 0; y < band_height; y++) {
        uint32_t *const row =
            (uint32_t *)(void *)(buffer->owned_map + (uint64_t)(y + GUARD_NOTICE_MARGIN) * buffer->pitch);
        for (uint32_t x = 0; x < cells * scale; x++) {
            const uint32_t cell = x / scale / GUARD_NOTICE_CELL;
            const uint32_t column = x / scale % GUARD_NOTICE_CELL;
            const uint32_t glyph_row = y / scale;
            const guard_glyph_t *const glyph = guard_glyph_for(guard_notice_text[cell]);
            bool lit = false;
            if (glyph != nullptr && glyph_row >= 1u && glyph_row <= GUARD_NOTICE_GLYPH_ROWS &&
                column < GUARD_NOTICE_GLYPH_COLUMNS) {
                lit = (glyph->rows[glyph_row - 1u] >> (GUARD_NOTICE_GLYPH_COLUMNS - 1u - column) & 1u) != 0u;
            }
            row[left + x] = lit ? GUARD_NOTICE_WHITE : GUARD_NOTICE_RED;
        }
    }
}

/* ---- --crash-test ------------------------------------------------------------------------------------------------ */

display_crash_test_t display_vt_guard_crash_test_from_name(const char *name) {
    if (name[0] == '\0' || strcmp(name, "segv") == 0) {
        return DISPLAY_CRASH_TEST_SEGV;
    }
    if (strcmp(name, "abort") == 0) {
        return DISPLAY_CRASH_TEST_ABORT;
    }
    if (strcmp(name, "kill") == 0) {
        return DISPLAY_CRASH_TEST_KILL;
    }
    return strcmp(name, "hang") == 0 ? DISPLAY_CRASH_TEST_HANG : DISPLAY_CRASH_TEST_NONE;
}

void display_vt_guard_crash_now(display_crash_test_t how) {
    switch (how) {
    case DISPLAY_CRASH_TEST_NONE:
        return;
    case DISPLAY_CRASH_TEST_SEGV: {
        /* volatile, so that the compiler cannot remove it. Not 0: the sanitize preset checks for null and exits, with no fault. */
        volatile uintptr_t nowhere = 8u;
        *(volatile uint8_t *)nowhere = 1u;
        return;
    }
    case DISPLAY_CRASH_TEST_ABORT:
        abort();
    case DISPLAY_CRASH_TEST_KILL:
        kill(getpid(), SIGKILL);
        return;
    case DISPLAY_CRASH_TEST_HANG: {
        const int64_t started_ns = time_now_ns();
        while (time_now_ns() - started_ns < GUARD_HANG_NS) {
            guard_wait_ms(GUARD_POLL_MS);
        }
        return;
    }
    }
}
