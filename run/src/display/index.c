/* See README.md. The Owner of the KMS backend: its parts borrow the arena here, and this is the only file in the system
 * that reserves memory. */
#include "display/index.h"

#include "log/index.h"
#include "machine/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>

enum : uint64_t {
    DISPLAY_SETUP_BYTES = HEAP_REGION_ALIGNMENT,
    DISPLAY_VT_CHECK_FRAMES = 120u,
};
enum : int32_t {
    DISPLAY_POLL_MS = 100,
    DISPLAY_SIGNAL_COUNT = 8,
    DISPLAY_SIGNALS_THAT_STOP =
        4, /* the first four; being stopped with the console dark would be worse, so not those */
};
enum : int64_t {
    DISPLAY_FLIP_STALL_NS = 1000000000, /* no event for this long means flips have stopped */
};

void display_config_from_machine(display_config_t *out) {
    *out = (display_config_t){.card_path = MC_DISPLAY_DEVICE,
                              .connector_name = MC_DISPLAY_CONNECTOR,
                              .width = MC_DISPLAY_WIDTH,
                              .height = MC_DISPLAY_HEIGHT,
                              .refresh_hz = MC_DISPLAY_REFRESH_HZ,
                              .console_fd = 0};
}

display_status_t display_init(display_t *display, const display_config_t *config, const display_ops_t *ops,
                              void *context) {
    memset(display, 0, sizeof(*display));
    /* First, and before any memory: under a display server this is as far as the program gets. */
    uint32_t console_minor = 0;
    const display_refusal_t refusal = display_device_check_console(ops, context, config->console_fd, &console_minor);
    if (refusal != DISPLAY_REFUSAL_NONE) {
        LOG_ERROR("display: %s", display_device_refusal_text(refusal));
        display->refusal = refusal;
        return DISPLAY_STATUS_REFUSED;
    }
    if (!heap_reserve(DISPLAY_SETUP_BYTES, &display->owned_region)) {
        return DISPLAY_STATUS_ERROR;
    }
    arena_init(&display->arena_setup, "display_setup", display->owned_region.owned_base, display->owned_region.bytes);

    display_status_t status =
        display_device_init(&display->device, ops, context, config->card_path, config->console_fd, console_minor);
    if (status != DISPLAY_STATUS_OK) {
        display->refusal = display->device.refusal;
        goto cleanup_memory;
    }
    const display_mode_wanted_t wanted = {.connector_name = config->connector_name,
                                          .refresh_mhz = (uint64_t)config->refresh_hz * 1000u,
                                          .width = config->width,
                                          .height = config->height};
    status = display_mode_init(&display->mode, &display->device, &display->arena_setup, &wanted);
    if (status != DISPLAY_STATUS_OK) {
        goto cleanup_device;
    }
    status = display_flip_init(&display->flip, &display->device, &display->mode);
    if (status != DISPLAY_STATUS_OK) {
        goto cleanup_mode;
    }
    display->running = 1u;
    return DISPLAY_STATUS_OK;

cleanup_mode:
    display_mode_deinit(&display->mode);
cleanup_device:
    display_device_deinit(&display->device);
cleanup_memory:
    arena_deinit(&display->arena_setup);
    heap_release(&display->owned_region);
    return status;
}

void display_deinit(display_t *display) {
    if (display->running == 0u) {
        return;
    }
    display_flip_deinit(&display->flip);
    display_mode_deinit(&display->mode);
    display_device_deinit(&display->device);
    arena_deinit(&display->arena_setup);
    heap_release(&display->owned_region);
    display->running = 0u;
}

/* ---- The test pattern -------------------------------------------------------------------------------------------- */

/* Not a pointer, so nothing owned: a handler can only reach the outside through a static. Lock-free, so safe in one. */
static _Atomic int32_t display_stop_signal;

static void display_on_stop(int32_t signal_number) {
    atomic_store_explicit(&display_stop_signal, signal_number, memory_order_relaxed);
}

typedef struct display_signals_t {
    struct sigaction saved[DISPLAY_SIGNAL_COUNT];
} display_signals_t;
static_assert(sizeof(display_signals_t) == sizeof(struct sigaction) * DISPLAY_SIGNAL_COUNT);

static const int32_t display_signal_numbers[DISPLAY_SIGNAL_COUNT] = {SIGTERM, SIGINT,  SIGHUP,  SIGQUIT,
                                                                     SIGPIPE, SIGTSTP, SIGTTOU, SIGTTIN};

static void display_signals_install(display_signals_t *signals) {
    atomic_store_explicit(&display_stop_signal, 0, memory_order_relaxed);
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    for (int32_t i = 0; i < DISPLAY_SIGNAL_COUNT; i++) {
        /* No SA_RESTART: a wait should wake, and look. */
        action.sa_handler = i < DISPLAY_SIGNALS_THAT_STOP ? display_on_stop : SIG_IGN;
        sigaction(display_signal_numbers[i], &action, &signals->saved[i]);
    }
}

static void display_signals_restore(const display_signals_t *signals) {
    for (int32_t i = 0; i < DISPLAY_SIGNAL_COUNT; i++) {
        sigaction(display_signal_numbers[i], &signals->saved[i], nullptr);
    }
}

/* One submitted flip, and waiting for it. Returns false to stop: the failure is in flip->failed_call. */
static bool display_flip_once(display_flip_t *flip, uint64_t frame) {
    display_flip_pattern_frame(display_flip_back(flip), frame);
    if (!display_flip_submit(flip)) {
        return false;
    }
    const int64_t submitted_ns = time_now_ns();
    while (flip->flip_pending != 0u && atomic_load_explicit(&display_stop_signal, memory_order_relaxed) == 0) {
        if (display_flip_wait(flip, DISPLAY_POLL_MS) == DISPLAY_FLIP_WAIT_FAILED) {
            return false;
        }
        if (flip->flip_pending != 0u && time_now_ns() - submitted_ns > DISPLAY_FLIP_STALL_NS) {
            flip->failed_call = "no flip event within a second";
            return false;
        }
    }
    return true;
}

/* Nothing here logs: the console shows nothing until the restore, so everything is told afterwards. */
static void display_test_pattern_loop(display_t *display, int64_t started_ns, int64_t duration_ns) {
    display_flip_t *const flip = &display->flip;
    uint64_t frame = 0;
    while (atomic_load_explicit(&display_stop_signal, memory_order_relaxed) == 0 &&
           time_now_ns() - started_ns < duration_ns) {
        if (!display_flip_once(flip, frame)) {
            return;
        }
        frame++;
        if (frame % DISPLAY_VT_CHECK_FRAMES == 0u && !display_device_console_is_visible(&display->device)) {
            flip->failed_call = "the visible virtual console changed during the run";
            return;
        }
    }
}

static void display_test_pattern_report(const display_t *display) {
    const display_flip_stats_t *const stats = &display->flip.stats;
    LOG_INFO("test pattern: %" PRIu64 " flips submitted, %" PRIu64 " events, %" PRIu64 " sequence gaps",
             stats->flips_submitted, stats->events, stats->sequence_gaps);
    if (stats->intervals == 0u) {
        LOG_WARN("test pattern: no flip intervals were measured");
        return;
    }
    const uint64_t mean_ns = stats->sum_ns / stats->intervals;
    const uint64_t rate_mhz = mean_ns == 0u ? 0u : (uint64_t)TIME_NS_PER_SECOND * 1000u / mean_ns;
    LOG_INFO("test pattern: %" PRIu64 " intervals, mean %" PRIu64 " ns, min %" PRIu64 " ns, max %" PRIu64
             " ns, late %" PRIu64 ", rate %" PRIu64 ".%03" PRIu64 " Hz against a mode of %" PRIu64 ".%03" PRIu64 " Hz",
             stats->intervals, mean_ns, stats->min_ns, stats->max_ns, stats->late, rate_mhz / 1000u, rate_mhz % 1000u,
             display->mode.refresh_mhz / 1000u, display->mode.refresh_mhz % 1000u);
}

display_status_t display_test_pattern_run(display_t *display, int64_t started_ns, int64_t duration_ns) {
    display_flip_t *const flip = &display->flip;
    for (uint32_t i = 0; i < DISPLAY_FLIP_BUFFERS; i++) {
        display_flip_pattern_fill(&flip->buffers[i]);
    }
    display_signals_t signals;
    display_signals_install(&signals);
    if (display_flip_take_over(flip)) {
        display_test_pattern_loop(display, started_ns, duration_ns);
    }
    display_flip_drain(flip);
    const bool restored = display_flip_restore(flip);
    const int32_t stopped_by = atomic_load_explicit(&display_stop_signal, memory_order_relaxed);
    display_signals_restore(&signals);

    /* The console is back, so now it can be told. */
    display_test_pattern_report(display);
    if (stopped_by != 0) {
        LOG_INFO("test pattern: stopped early by signal %" PRId32, stopped_by);
    }
    if (flip->failed_call != nullptr) {
        LOG_ERROR("test pattern: %s%s%s", flip->failed_call, flip->failed_errno != 0 ? ": " : "",
                  flip->failed_errno != 0 ? strerror(flip->failed_errno) : "");
    }
    if (!restored) {
        LOG_ERROR("test pattern: at least one ioctl failed while giving the display back");
    }
    return flip->failed_call == nullptr && restored ? DISPLAY_STATUS_OK : DISPLAY_STATUS_ERROR;
}
