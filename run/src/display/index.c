/* See README.md. The Owner of the KMS backend: its parts borrow the arena here, and this is the only file in the system
 * that reserves memory. */
#include "display/index.h"

#include "display/headless/png/index.h"
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
    DISPLAY_CRASH_TEST_FRAMES = 240u, /* two seconds at 120 Hz: long enough to see that it was showing */
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
    /* Takes nothing yet: display_test_pattern_run takes the console, behind it. */
    display_vt_guard_init(&display->guard, &display->device, &display->flip, &display->arena_setup);
    display->crash_test = (uint8_t)config->crash_test;
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
    display_vt_guard_deinit(&display->guard);
    display_flip_deinit(&display->flip);
    display_mode_deinit(&display->mode);
    display_device_deinit(&display->device);
    arena_deinit(&display->arena_setup);
    heap_release(&display->owned_region);
    display->running = 0u;
}

void display_offscreen_config_from_machine(display_headless_config_t *out) {
    *out = (display_headless_config_t){.script_path = nullptr,
                                       .screenshot_dir = nullptr,
                                       .width = MC_DISPLAY_WIDTH,
                                       .height = MC_DISPLAY_HEIGHT,
                                       .tick_count = 0u,
                                       .tick_hz = MC_GAME_TICK_HZ};
}

bool display_offscreen_init(display_offscreen_t *offscreen, const display_headless_config_t *config) {
    memset(offscreen, 0, sizeof(*offscreen));
    const uint64_t bytes = display_headless_arena_bytes(config);
    if (bytes == 0u) {
        LOG_ERROR("display: a frame of %" PRIu32 "x%" PRIu32 " is not possible: each side is 1 to %" PRIu32,
                  config->width, config->height, (uint32_t)DISPLAY_PNG_MAX_DIMENSION);
        return false;
    }
    if (!heap_reserve(bytes, &offscreen->owned_region)) {
        return false;
    }
    arena_init(&offscreen->arena_setup, "display_offscreen", offscreen->owned_region.owned_base,
               offscreen->owned_region.bytes);
    if (!display_headless_init(&offscreen->headless, &offscreen->arena_setup, config)) {
        arena_deinit(&offscreen->arena_setup);
        heap_release(&offscreen->owned_region);
        return false;
    }
    offscreen->running = 1u;
    return true;
}

void display_offscreen_deinit(display_offscreen_t *offscreen) {
    if (offscreen->running == 0u) {
        return;
    }
    display_headless_deinit(&offscreen->headless);
    arena_deinit(&offscreen->arena_setup);
    heap_release(&offscreen->owned_region);
    offscreen->running = 0u;
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

/* The back buffer's picture for this frame. Returns false if the cursor's source says to stop. */
static bool display_draw_frame(display_t *display, uint64_t frame) {
    display_buffer_t *const back = display_flip_back(&display->flip);
    if (display->frame_source != nullptr) {
        return display->frame_source(display->borrowed_cursor_context, back);
    }
    if (display->cursor_source == nullptr) {
        display_flip_pattern_frame(back, frame);
        return true;
    }
    const uint32_t side = display_flip_pattern_cursor_side(back);
    uint32_t x = 0;
    uint32_t y = 0;
    bool active = false;
    if (!display->cursor_source(display->borrowed_cursor_context, back->width - side, back->height - side, &x, &y,
                                &active)) {
        return false;
    }
    /* Erasing the cursor puts back the bare pattern, so if it was over the notice, the notice goes back on as well: it is the
     * only thing on screen that says how to get out. */
    const bool repair_notice = back->has_cursor != 0u && back->cursor_y < display_vt_guard_notice_bottom(back);
    display_flip_pattern_cursor_erase(back);
    display_flip_pattern_frame(back, frame);
    if (repair_notice) {
        display_vt_guard_draw_notice(back);
    }
    display_flip_pattern_cursor_draw(back, x, y, active);
    return true;
}

/* One submitted flip, and waiting for it. Returns false to stop: if for a failure, that is in flip->failed_call. */
static bool display_flip_once(display_t *display, uint64_t frame) {
    display_flip_t *const flip = &display->flip;
    if (!display_draw_frame(display, frame)) {
        return false;
    }
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

/* Nothing here logs: the console shows nothing until the restore, so everything is told afterwards. A duration of 0 has no
 * deadline. */
static void display_run_loop(display_t *display, int64_t started_ns, int64_t duration_ns) {
    display_flip_t *const flip = &display->flip;
    display_vt_guard_t *const guard = &display->guard;
    uint64_t frame = 0;
    bool was_away = false;
    while (atomic_load_explicit(&display_stop_signal, memory_order_relaxed) == 0 &&
           (duration_ns == 0 || time_now_ns() - started_ns < duration_ns)) {
        display_vt_guard_pet(guard);
        const display_vt_guard_state_t state = display_vt_guard_service(guard);
        if (state == DISPLAY_VT_GUARD_FAILED) {
            return;
        }
        if (state == DISPLAY_VT_GUARD_AWAY) {
            was_away = true;
            continue; /* it has paused; the deadline and the stop signals still count */
        }
        if (was_away) {
            was_away = false;
            if (display->cursor_discard != nullptr) {
                display->cursor_discard(display->borrowed_cursor_context);
            }
        }
        if (!display_flip_once(display, frame)) {
            return;
        }
        frame++;
        if (frame == DISPLAY_CRASH_TEST_FRAMES) {
            display_vt_guard_crash_now((display_crash_test_t)display->crash_test);
        }
        if (frame % DISPLAY_VT_CHECK_FRAMES == 0u && !display_device_console_is_visible(&display->device)) {
            flip->failed_call = "the visible virtual console changed during the run";
            return;
        }
    }
}

static void display_run_report(const display_t *display, const char *what) {
    const display_flip_stats_t *const stats = &display->flip.stats;
    LOG_INFO("%s: %" PRIu64 " flips submitted, %" PRIu64 " events, %" PRIu64 " sequence gaps", what,
             stats->flips_submitted, stats->events, stats->sequence_gaps);
    if (stats->intervals == 0u) {
        LOG_WARN("%s: no flip intervals were measured", what);
        return;
    }
    const uint64_t mean_ns = stats->sum_ns / stats->intervals;
    const uint64_t rate_mhz = mean_ns == 0u ? 0u : (uint64_t)TIME_NS_PER_SECOND * 1000u / mean_ns;
    LOG_INFO("%s: %" PRIu64 " intervals, mean %" PRIu64 " ns, min %" PRIu64 " ns, max %" PRIu64 " ns, late %" PRIu64
             ", rate %" PRIu64 ".%03" PRIu64 " Hz against a mode of %" PRIu64 ".%03" PRIu64 " Hz",
             what, stats->intervals, mean_ns, stats->min_ns, stats->max_ns, stats->late, rate_mhz / 1000u,
             rate_mhz % 1000u, display->mode.refresh_mhz / 1000u, display->mode.refresh_mhz % 1000u);
}

void display_test_pattern_set_cursor(display_t *display, display_cursor_source_t source,
                                     display_cursor_discard_t discard, void *context) {
    display->cursor_source = source;
    display->cursor_discard = discard;
    display->borrowed_cursor_context = context;
}

/* Takes the console, runs the loop and gives it back, and says how it went once the console can show that. */
static display_status_t display_run_taken(display_t *display, int64_t started_ns, int64_t duration_ns,
                                          const char *what) {
    display_flip_t *const flip = &display->flip;
    display_vt_guard_t *const guard = &display->guard;
    display_signals_t signals;
    display_signals_install(&signals);
    if (display_vt_guard_take_console(guard)) {
        display_run_loop(display, started_ns, duration_ns);
    }
    display_flip_drain(flip);
    const bool restored = display_vt_guard_give_back(guard);
    const int32_t stopped_by = atomic_load_explicit(&display_stop_signal, memory_order_relaxed);
    display_signals_restore(&signals);

    /* The console is back, so now it can be told. */
    display_run_report(display, what);
    if (stopped_by != 0) {
        LOG_INFO("%s: stopped early by signal %" PRId32, what, stopped_by);
    }
    if (guard->failed_call != nullptr) {
        LOG_ERROR("%s: %s%s%s", what, guard->failed_call, guard->failed_errno != 0 ? ": " : "",
                  guard->failed_errno != 0 ? strerror(guard->failed_errno) : "");
    }
    if (flip->failed_call != nullptr) {
        LOG_ERROR("%s: %s%s%s", what, flip->failed_call, flip->failed_errno != 0 ? ": " : "",
                  flip->failed_errno != 0 ? strerror(flip->failed_errno) : "");
    }
    if (!restored) {
        LOG_ERROR("%s: at least one ioctl failed while giving the display back", what);
    }
    const bool ok = flip->failed_call == nullptr && guard->failed_call == nullptr && restored;
    return ok ? DISPLAY_STATUS_OK : DISPLAY_STATUS_ERROR;
}

display_status_t display_test_pattern_run(display_t *display, int64_t started_ns, int64_t duration_ns) {
    display_flip_t *const flip = &display->flip;
    for (uint32_t i = 0; i < DISPLAY_FLIP_BUFFERS; i++) {
        display_flip_pattern_fill(&flip->buffers[i]);
        display_vt_guard_draw_notice(&flip->buffers[i]);
    }
    return display_run_taken(display, started_ns, duration_ns, "test pattern");
}

display_status_t display_frames_run(display_t *display, display_frame_source_t source, display_cursor_discard_t discard,
                                    void *context) {
    display_flip_t *const flip = &display->flip;
    for (uint32_t i = 0; i < DISPLAY_FLIP_BUFFERS; i++) {
        memset(flip->buffers[i].owned_map, 0, flip->buffers[i].map_bytes);
    }
    display->frame_source = source;
    display->cursor_discard = discard;
    display->borrowed_cursor_context = context;
    return display_run_taken(display, time_now_ns(), 0, "frames");
}
