/* See README.md. */
#include "game/watch/index.h"

#include "game/index.h"
#include "log/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <string.h>

static const char *const game_watch_stage_names[GAME_WATCH_TICK_STAGES] = {
    [GAME_WATCH_TICK_WEATHER] = "weather", [GAME_WATCH_TICK_HANDS] = "hands", [GAME_WATCH_TICK_MOBS] = "mobs",
    [GAME_WATCH_TICK_PLAYER] = "player",   [GAME_WATCH_TICK_WORLD] = "world", [GAME_WATCH_TICK_PUBLISH] = "publish",
};

const char *game_watch_tick_stage_name(game_watch_tick_stage_t stage) {
    return (uint32_t)stage < (uint32_t)GAME_WATCH_TICK_STAGES ? game_watch_stage_names[stage] : "?";
}

void game_watch_start(game_watch_t *watch) {
    memset(watch, 0, sizeof(*watch));
}

/* Whether a slow line may be said now, with said_ns when the last was: never more than one in GAME_WATCH_SAY_GAP_NS. */
static bool game_watch_may_say(int64_t said_ns, int64_t now_ns) {
    return said_ns == 0 || now_ns - said_ns >= GAME_WATCH_SAY_GAP_NS;
}

static libc_double_t game_watch_ms(int64_t ns) {
    return (libc_double_t)time_ns_to_ms(ns);
}

/* How the last GAME_WATCH_SPAN_TICKS ticks went, and the frames main drew meanwhile, which it hands over here. */
static void game_watch_span_say(game_watch_t *watch, uint64_t tick_count) {
    const uint32_t frames = atomic_exchange_explicit(&watch->span_frames, 0u, memory_order_relaxed);
    const int64_t frame_ns = atomic_exchange_explicit(&watch->span_frame_ns, 0, memory_order_relaxed);
    const int64_t frame_most_ns = atomic_exchange_explicit(&watch->span_frame_most_ns, 0, memory_order_relaxed);
    LOG_INFO("watch: ticks %" PRIu64 " to %" PRIu64
             ": a tick took %.2f ms on average and %.2f ms at most (%s); %" PRIu32
             " frames took %.2f ms on average and %.2f ms at most; %" PRIu32 " mobs, %" PRIu32 " drops, %" PRIu32
             " sections waiting to upload, %" PRIu32 " far jobs",
             tick_count - watch->span_ticks + 1u, tick_count,
             game_watch_ms(watch->span_tick_ns / (int64_t)watch->span_ticks), game_watch_ms(watch->span_tick_most_ns),
             game_watch_tick_stage_name((game_watch_tick_stage_t)watch->span_most_stage), frames,
             game_watch_ms(frames != 0u ? frame_ns / (int64_t)frames : 0), game_watch_ms(frame_most_ns),
             atomic_load_explicit(&watch->mobs, memory_order_relaxed),
             atomic_load_explicit(&watch->drops, memory_order_relaxed),
             atomic_load_explicit(&watch->uploads, memory_order_relaxed),
             atomic_load_explicit(&watch->far_jobs, memory_order_relaxed));
    watch->span_ticks = 0u;
    watch->span_tick_ns = 0;
    watch->span_tick_most_ns = 0;
    watch->span_most_stage = 0u;
}

void game_watch_tick_note(game_watch_t *watch, const int64_t stage_ns[GAME_WATCH_TICK_STAGES], uint64_t tick_count,
                          uint32_t mobs, uint32_t drops, int64_t now_ns) {
    atomic_store_explicit(&watch->mobs, mobs, memory_order_relaxed);
    atomic_store_explicit(&watch->drops, drops, memory_order_relaxed);
    int64_t total_ns = 0;
    uint32_t slowest = 0u;
    for (uint32_t i = 0u; i < GAME_WATCH_TICK_STAGES; i++) {
        total_ns += stage_ns[i];
        slowest = stage_ns[i] > stage_ns[slowest] ? i : slowest;
    }
    watch->span_ticks++;
    watch->span_tick_ns += total_ns;
    if (total_ns > watch->span_tick_most_ns) {
        watch->span_tick_most_ns = total_ns;
        watch->span_most_stage = (uint8_t)slowest;
    }
    if (total_ns > GAME_WATCH_SLOW_TICK_NS) {
        if (game_watch_may_say(watch->tick_said_ns, now_ns)) {
            LOG_WARN("watch: tick %" PRIu64
                     " took %.1f ms, and the slowest part was %s at %.1f ms (weather %.1f, hands "
                     "%.1f, mobs %.1f, player %.1f, world %.1f, publish %.1f); %" PRIu32 " mobs, %" PRIu32
                     " drops; %" PRIu64 " more slow ticks since the last line",
                     tick_count, game_watch_ms(total_ns), game_watch_tick_stage_name((game_watch_tick_stage_t)slowest),
                     game_watch_ms(stage_ns[slowest]), game_watch_ms(stage_ns[GAME_WATCH_TICK_WEATHER]),
                     game_watch_ms(stage_ns[GAME_WATCH_TICK_HANDS]), game_watch_ms(stage_ns[GAME_WATCH_TICK_MOBS]),
                     game_watch_ms(stage_ns[GAME_WATCH_TICK_PLAYER]), game_watch_ms(stage_ns[GAME_WATCH_TICK_WORLD]),
                     game_watch_ms(stage_ns[GAME_WATCH_TICK_PUBLISH]), mobs, drops, watch->ticks_unsaid);
            watch->tick_said_ns = now_ns;
            watch->ticks_unsaid = 0u;
        } else {
            watch->ticks_unsaid++;
        }
    }
    if (watch->span_ticks >= GAME_WATCH_SPAN_TICKS) {
        game_watch_span_say(watch, tick_count);
    }
}

void game_watch_gpu_note(game_watch_t *watch, uint32_t uploads, uint32_t far_jobs) {
    atomic_store_explicit(&watch->uploads, uploads, memory_order_relaxed);
    atomic_store_explicit(&watch->far_jobs, far_jobs, memory_order_relaxed);
}

bool game_watch_frame_note(game_watch_t *watch, int64_t frame_ns, int64_t now_ns) {
    atomic_fetch_add_explicit(&watch->span_frames, 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&watch->span_frame_ns, frame_ns, memory_order_relaxed);
    /* Main is the one writer but for the sim's exchange to 0, so a load and a store do. */
    if (frame_ns > atomic_load_explicit(&watch->span_frame_most_ns, memory_order_relaxed)) {
        atomic_store_explicit(&watch->span_frame_most_ns, frame_ns, memory_order_relaxed);
    }
    if (frame_ns <= GAME_WATCH_SLOW_FRAME_NS) {
        return false;
    }
    if (!game_watch_may_say(watch->frame_said_ns, now_ns)) {
        watch->frames_unsaid++;
        return false;
    }
    watch->frame_said_ns = now_ns;
    return true;
}

void game_watch_frame_say(game_watch_t *watch, int64_t frame_ns, const ui_hud_debug_t *debug) {
    /* What main did past drawing on the picture and copying it out: the display's wait, input, and whatever else came round. */
    const float32_t frame_ms = (float32_t)time_ns_to_ms(frame_ns);
    float32_t rest_ms = frame_ms - debug->stage_ms[UI_HUD_STAGE_HUD] - debug->stage_ms[UI_HUD_STAGE_PRESENT];
    rest_ms = rest_ms > 0.0f32 ? rest_ms : 0.0f32;
    const char *slowest = "the rest of main's frame";
    float32_t slowest_ms = rest_ms;
    for (uint32_t stage = 0u; stage < UI_HUD_STAGE_COUNT; stage++) {
        if (stage != UI_HUD_STAGE_GPU_TOTAL && debug->stage_ms[stage] > slowest_ms) {
            slowest_ms = debug->stage_ms[stage];
            slowest = ui_hud_stage_name((ui_hud_stage_t)stage);
        }
    }
    LOG_WARN(
        "watch: a frame took %.1f ms, and the slowest part was %s at %.1f ms (coarse %.1f, primary %.1f, shade %.1f, "
        "post %.1f, readback %.1f, gpu %.1f in all, sim tick %.1f, hud %.1f, present %.1f, the rest of main's frame "
        "%.1f); %" PRIu32 " mobs, %" PRIu32 " drops, %" PRIu32 " sections waiting to upload, %" PRIu32
        " far jobs; %" PRIu64 " more slow frames since the last line",
        (libc_double_t)frame_ms, slowest, (libc_double_t)slowest_ms,
        (libc_double_t)debug->stage_ms[UI_HUD_STAGE_COARSE], (libc_double_t)debug->stage_ms[UI_HUD_STAGE_PRIMARY],
        (libc_double_t)debug->stage_ms[UI_HUD_STAGE_SHADE], (libc_double_t)debug->stage_ms[UI_HUD_STAGE_POST],
        (libc_double_t)debug->stage_ms[UI_HUD_STAGE_READBACK], (libc_double_t)debug->stage_ms[UI_HUD_STAGE_GPU_TOTAL],
        (libc_double_t)debug->stage_ms[UI_HUD_STAGE_SIM_TICK], (libc_double_t)debug->stage_ms[UI_HUD_STAGE_HUD],
        (libc_double_t)debug->stage_ms[UI_HUD_STAGE_PRESENT], (libc_double_t)rest_ms, debug->mobs, debug->drops,
        debug->uploads, debug->far_jobs, watch->frames_unsaid);
    watch->frames_unsaid = 0u;
}

void game_watch_debug_read(const game_watch_t *watch, ui_hud_debug_t *out) {
    out->mobs = atomic_load_explicit(&watch->mobs, memory_order_relaxed);
    out->drops = atomic_load_explicit(&watch->drops, memory_order_relaxed);
    out->uploads = atomic_load_explicit(&watch->uploads, memory_order_relaxed);
    out->far_jobs = atomic_load_explicit(&watch->far_jobs, memory_order_relaxed);
}

void game_watch_gpu_look(game_t *game) {
    const render_t *const render = game->borrowed_render;
    const render_upload_t *const upload = &render->upload;
    const uint32_t uploads =
        upload->borrowed_view != nullptr ? upload->borrowed_view->section_count - upload->finished : 0u;
    game_watch_gpu_note(game_watch_of(game), uploads,
                        render->borrowed_lod != nullptr ? render->borrowed_lod->jobs_in_use : 0u);
}

void game_watch_frame_look(game_t *game, int64_t frame_ns) {
    game_watch_t *const watch = game_watch_of(game);
    if (watch == nullptr || !game_watch_frame_note(watch, frame_ns, time_now_ns())) {
        return;
    }
    ui_hud_debug_t debug;
    game_hud_debug_read(game, &debug);
    game_watch_frame_say(watch, frame_ns, &debug);
}

/* See run/src/profiler/README.md. Moved here from game/index.c to keep that under 1500 lines. */
void game_watch_kernels_note(game_t *game, const frame_timings_t *timings) {
    profiler_lane_t *const lane = game->borrowed_lane_card;
    if (lane == nullptr) {
        return;
    }
    int64_t start_ns = time_now_ns() - (int64_t)((float64_t)timings->total_ms * (float64_t)TIME_NS_PER_MS);
    for (uint32_t i = 0u; i < FRAME_KERNEL_COUNT; i++) {
        if ((timings->kernels_ran & (1u << i)) != 0u) {
            const int64_t duration_ns = (int64_t)((float64_t)timings->kernel_ms[i] * (float64_t)TIME_NS_PER_MS);
            profiler_span_add(lane, frame_kernel_name((frame_kernel_t)i), start_ns, duration_ns);
            start_ns += duration_ns;
        }
    }
}
