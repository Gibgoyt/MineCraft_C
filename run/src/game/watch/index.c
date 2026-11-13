/* See README.md. */
#include "game/watch/index.h"

#include "game/index.h"
#include "log/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    watch->log_file.fd = -1;
}

static void game_watch_minute_take(game_watch_t *watch, float64_t *out_rate, float64_t *out_p99_ms, uint32_t *out_late);

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
    float64_t rate = 0.0f64, p99_ms = 0.0f64;
    uint32_t late = 0u;
    game_watch_minute_take(watch, &rate, &p99_ms, &late);
    LOG_INFO("watch: ticks %" PRIu64 " to %" PRIu64
             ": a tick took %.2f ms on average and %.2f ms at most (%s); %" PRIu32
             " frames took %.2f ms on average and %.2f ms at most; %.1f pictures a second, interval p99 %.0f ms, late "
             "%" PRIu32 "; %" PRIu32 " mobs, %" PRIu32 " drops, %" PRIu32 " sections waiting to upload, %" PRIu32
             " far jobs",
             tick_count - watch->span_ticks + 1u, tick_count,
             game_watch_ms(watch->span_tick_ns / (int64_t)watch->span_ticks), game_watch_ms(watch->span_tick_most_ns),
             game_watch_tick_stage_name((game_watch_tick_stage_t)watch->span_most_stage), frames,
             game_watch_ms(frames != 0u ? frame_ns / (int64_t)frames : 0), game_watch_ms(frame_most_ns),
             (libc_double_t)rate, (libc_double_t)p99_ms, late, atomic_load_explicit(&watch->mobs, memory_order_relaxed),
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
    game_watch_rings_read(watch, now_ns, false);
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
        if ((timings->kernels_ran & (1u << i)) == 0u) {
            continue;
        }
        const int64_t duration_ns = (int64_t)((float64_t)timings->kernel_ms[i] * (float64_t)TIME_NS_PER_MS);
        profiler_span_add(lane, frame_kernel_name((frame_kernel_t)i), start_ns, duration_ns);
        start_ns += duration_ns;
    }
}

/* ---- Plan 008 phase 01: picture by picture ------------------------------------------------------------------------- */

/* The frame log's columns, in order. A new column goes on the end. See README.md. */
static const char game_watch_log_header[] =
    "seq,taken_s,interval_ms,ticks_waited,late,idle_ms,upload_ms,lod_plan_ms,fill_ms,draw_cpu_ms,card_wait_ms,p_ms,"
    "ready_to_take_ms,card_ms,coarse_ms,primary_ms,lod_ms,shade_ms,water_ms,surfaces_ms,sun_ms,ao_ms,ao_fresh_ms,"
    "post_ms,readback_ms,fresh,card_mhz,clock_read,turn_deg_s,x_put_ms,x_wait_ms,x_idle_ms,pace,aimed_ms,start_late_ms,"
    "missed,puts_skipped,rung\n";

static float64_t game_watch_ms_of(int64_t ns) {
    return (float64_t)ns / (float64_t)TIME_NS_PER_MS;
}

void game_watch_open(game_watch_t *watch, arena_t *arena, const char *log_path) {
    watch->borrowed_pictures = arena_alloc_zeroed(arena, (uint64_t)GAME_WATCH_RING * sizeof(game_watch_picture_t),
                                                  alignof(game_watch_picture_t));
    watch->borrowed_takes =
        arena_alloc_zeroed(arena, (uint64_t)GAME_WATCH_RING * sizeof(game_watch_take_t), alignof(game_watch_take_t));
    watch->opened_ns = time_now_ns();
    frame_marks_t *const marks = arena_alloc(arena, sizeof(frame_marks_t), alignof(frame_marks_t));
    gpu_error_t error = {0};
    if (frame_marks_init(marks, &error) == GPU_STATUS_OK) {
        watch->borrowed_marks = marks;
    } else {
        gpu_error_log(&error);
        LOG_WARN("watch: no marks on the card, so shade's kernels, the fresh AO count and the card's clock go unsaid");
    }
    if (log_path == nullptr) {
        return;
    }
    file_status_t status = file_open(&watch->log_file, log_path, FILE_MODE_APPEND);
    uint64_t bytes = 0u;
    if (status == FILE_OK) {
        status = file_size(&watch->log_file, &bytes);
    }
    if (status == FILE_OK && bytes == 0u) {
        status = file_write_all(&watch->log_file, game_watch_log_header, sizeof(game_watch_log_header) - 1u);
    }
    if (status != FILE_OK) {
        LOG_WARN("watch: no frame log at %s: %s", log_path, file_status_name(status));
        (void)file_close(&watch->log_file);
        return;
    }
    watch->borrowed_log_text = arena_alloc(arena, GAME_WATCH_LOG_BYTES, 64u);
    watch->log_written_ns = watch->opened_ns;
    LOG_INFO("watch: a line for every picture shown goes to %s", log_path);
}

/* What is waiting goes to the file. A failure stops the log, and says so once. */
static void game_watch_log_write(game_watch_t *watch, int64_t now_ns) {
    if (watch->log_used != 0u && watch->log_failed == 0u) {
        const file_status_t status = file_write_all(&watch->log_file, watch->borrowed_log_text, watch->log_used);
        if (status != FILE_OK) {
            LOG_WARN("watch: the frame log stops here: %s", file_status_name(status));
            watch->log_failed = 1u;
        }
    }
    watch->log_used = 0u;
    watch->log_written_ns = now_ns;
}

void game_watch_close(game_watch_t *watch) {
    if (watch->borrowed_takes != nullptr) {
        game_watch_rings_read(watch, time_now_ns(), true);
    }
    if (watch->log_file.fd >= 0) {
        if (file_close(&watch->log_file) != FILE_OK) {
            LOG_WARN("watch: the frame log may not all have reached the disk");
        }
        if (watch->takes_lost != 0u) {
            LOG_WARN("watch: %" PRIu64
                     " pictures were never in the frame log, as the ring went round before the sim read them",
                     watch->takes_lost);
        }
    }
    if (watch->borrowed_marks != nullptr) {
        gpu_error_t error = {0};
        if (frame_marks_deinit(watch->borrowed_marks, &error) != GPU_STATUS_OK) {
            gpu_error_log(&error);
        }
        watch->borrowed_marks = nullptr;
    }
}

void game_watch_marks_arm(game_watch_t *watch, frame_gpu_t *gpu, int64_t now_ns) {
    frame_marks_t *const marks = watch->borrowed_marks;
    gpu->borrowed_marks = marks;
    if (marks != nullptr && (watch->clock_asked_ns == 0 || now_ns - watch->clock_asked_ns >= GAME_WATCH_CLOCK_NS)) {
        marks->clock_due = 1u;
        watch->clock_asked_ns = now_ns;
    }
}

game_watch_picture_t *game_watch_picture_begin(game_watch_t *watch, int64_t waited_ns, int64_t start_ns) {
    if (watch->borrowed_pictures == nullptr) {
        return nullptr;
    }
    /* Not pictures_drawn: with a frame on its way (plan 008 phase 03) the next begins before the last is drawn. */
    const uint64_t seq = watch->pictures_begun++;
    game_watch_picture_t *const picture = &watch->borrowed_pictures[seq % GAME_WATCH_RING];
    memset(picture, 0, sizeof(*picture));
    picture->seq = seq;
    picture->stamp_ns[GAME_WATCH_STAMP_WAITED] = waited_ns;
    picture->stamp_ns[GAME_WATCH_STAMP_START] = start_ns;
    watch->start_frame = atomic_load_explicit(&watch->main_frames, memory_order_relaxed);
    return picture;
}

void game_watch_stamp(game_watch_picture_t *picture, game_watch_stamp_t stamp) {
    if (picture != nullptr) {
        picture->stamp_ns[stamp] = time_now_ns();
    }
}

void game_watch_slot_note(game_watch_t *watch, uint32_t slot, const game_watch_picture_t *picture) {
    if (picture == nullptr) {
        return;
    }
    watch->slots[slot] = (game_watch_slot_t){
        .seq = picture->seq, .start_frame = watch->start_frame, .start_ns = picture->stamp_ns[GAME_WATCH_STAMP_START]};
}

/* Where a camera looks, unit length. */
static void game_watch_forward(float32_t yaw, float32_t pitch, float64_t out[3]) {
    out[0] = cos((float64_t)pitch) * cos((float64_t)yaw);
    out[1] = cos((float64_t)pitch) * sin((float64_t)yaw);
    out[2] = sin((float64_t)pitch);
}

int64_t game_watch_picture_end(game_watch_t *watch, game_watch_picture_t *picture, const frame_timings_t *timings,
                               const render_camera_t *camera) {
    const int64_t now_ns = time_now_ns();
    if (picture == nullptr) {
        return now_ns;
    }
    picture->stamp_ns[GAME_WATCH_STAMP_READY] = now_ns;
    picture->missed = picture->aimed_ns != 0 && now_ns > picture->aimed_ns ? 1u : 0u;
    for (uint32_t pass = 0u; pass < FRAME_PASS_COUNT; pass++) {
        picture->pass_ms[pass] = timings->pass_ms[pass];
    }
    picture->total_ms = timings->total_ms;
    picture->lod_ms = timings->kernel_ms[FRAME_KERNEL_LOD];
    const frame_marks_t *const marks = watch->borrowed_marks;
    if (marks != nullptr) {
        const int64_t queued_ns = marks->queued_ns;
        picture->stamp_ns[GAME_WATCH_STAMP_QUEUED] =
            queued_ns >= picture->stamp_ns[GAME_WATCH_STAMP_FILL] && queued_ns <= now_ns ? queued_ns : 0;
        for (uint32_t span = 0u; span < SHADE_SPAN_COUNT; span++) {
            picture->shade_ms[span] = marks->shade_ms[span];
        }
        picture->fresh = marks->fresh_count;
        picture->clock_read = marks->clock_read;
        if (marks->clock_read != 0u) {
            watch->card_mhz = marks->card_mhz;
        }
    }
    picture->card_mhz = watch->card_mhz;
    const int64_t start_ns = picture->stamp_ns[GAME_WATCH_STAMP_START];
    if (watch->last_start_ns != 0 && start_ns > watch->last_start_ns) {
        float64_t from[3], to[3];
        game_watch_forward(watch->last_yaw, watch->last_pitch, from);
        game_watch_forward(camera->yaw, camera->pitch, to);
        float64_t dot = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
        dot = dot > 1.0f64 ? 1.0f64 : (dot < -1.0f64 ? -1.0f64 : dot);
        const float64_t degrees = acos(dot) * 57.29577951308232f64;
        picture->turn_deg_s =
            (float32_t)(degrees / ((float64_t)(start_ns - watch->last_start_ns) / (float64_t)TIME_NS_PER_SECOND));
    }
    watch->last_start_ns = start_ns;
    watch->last_yaw = camera->yaw;
    watch->last_pitch = camera->pitch;
    atomic_store_explicit(&watch->pictures_drawn, picture->seq + 1u, memory_order_release);
    return now_ns;
}

void game_watch_main_frame(game_watch_t *watch) {
    atomic_store_explicit(&watch->main_frames, atomic_load_explicit(&watch->main_frames, memory_order_relaxed) + 1u,
                          memory_order_relaxed);
}

void game_watch_display_note(game_watch_t *watch, int64_t put_sent_ns, int64_t put_done_ns, int64_t wait_from_ns,
                             int64_t wait_ns, uint64_t puts_skipped) {
    watch->x_puts_skipped = puts_skipped;
    watch->x_put_sent_ns = put_sent_ns;
    watch->x_put_done_ns = put_done_ns;
    watch->x_wait_from_ns = wait_from_ns;
    watch->x_wait_ns = wait_ns;
}

void game_watch_take_note(game_watch_t *watch, uint32_t slot, int64_t taken_ns) {
    if (watch->borrowed_takes == nullptr) {
        return;
    }
    const uint64_t made = atomic_load_explicit(&watch->takes_made, memory_order_relaxed);
    const game_watch_slot_t *const from = &watch->slots[slot];
    const int64_t sent_ns = watch->x_put_sent_ns, done_ns = watch->x_put_done_ns, wait_from_ns = watch->x_wait_from_ns;
    watch->borrowed_takes[made % GAME_WATCH_RING] = (game_watch_take_t){
        .seq = from->seq,
        .frame = atomic_load_explicit(&watch->main_frames, memory_order_relaxed),
        .start_frame = from->start_frame,
        .taken_ns = taken_ns,
        .start_ns = from->start_ns,
        .x_put_ns = sent_ns != 0 && done_ns >= sent_ns ? done_ns - sent_ns : 0,
        .x_wait_ns = watch->x_wait_ns,
        .x_idle_ns = sent_ns != 0 && wait_from_ns >= sent_ns ? wait_from_ns - sent_ns : 0,
        .puts_skipped = watch->x_puts_skipped,
    };
    watch->last_taken_ns = taken_ns;
    atomic_store_explicit(&watch->takes_made, made + 1u, memory_order_release);
}

/* A picture main took was late if it was ready after the take the start rule aimed it at (plan 008 phase 02). With no aim, as under today's
 * rule or with its picture gone from the ring, if it was shown past the slot after the one it began in (phase 01). */
static bool game_watch_late(const game_watch_take_t *take, const game_watch_picture_t *picture) {
    if (picture != nullptr && picture->aimed_ns != 0) {
        return picture->missed != 0u;
    }
    return take->frame > take->start_frame + 1u;
}

/* One take's line of the log, with its picture if the ring still has it. */
static void game_watch_log_line(game_watch_t *watch, const game_watch_take_t *take, const game_watch_picture_t *picture,
                                int64_t interval_ns) {
    if (GAME_WATCH_LOG_BYTES - watch->log_used < GAME_WATCH_LOG_LINE_BYTES) {
        game_watch_log_write(watch, take->taken_ns);
    }
    const game_watch_picture_t none = {0};
    const game_watch_picture_t *const p = picture != nullptr ? picture : &none;
    const int64_t *const at = p->stamp_ns;
    const int64_t queued_ns =
        at[GAME_WATCH_STAMP_QUEUED] != 0 ? at[GAME_WATCH_STAMP_QUEUED] : at[GAME_WATCH_STAMP_READY];
    const float64_t drawn_ms = game_watch_ms_of(at[GAME_WATCH_STAMP_READY] - at[GAME_WATCH_STAMP_START]);
    const int32_t written = snprintf(
        watch->borrowed_log_text + watch->log_used, GAME_WATCH_LOG_LINE_BYTES,
        "%" PRIu64 ",%.6f,%.3f,%" PRIu64
        ",%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
        "%.3f,%.3f,%.3f,%.3f,%.3f,%" PRIu32 ",%.0f,%u,%.1f,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%u,%" PRIu64 ",%u\n",
        take->seq, (libc_double_t)(game_watch_ms_of(take->taken_ns - watch->opened_ns) / 1000.0f64),
        (libc_double_t)game_watch_ms_of(interval_ns), take->frame - take->start_frame,
        game_watch_late(take, picture) ? 1u : 0u,
        (libc_double_t)game_watch_ms_of(at[GAME_WATCH_STAMP_START] - at[GAME_WATCH_STAMP_WAITED]),
        (libc_double_t)game_watch_ms_of(at[GAME_WATCH_STAMP_UPLOAD] - at[GAME_WATCH_STAMP_START]),
        (libc_double_t)game_watch_ms_of(at[GAME_WATCH_STAMP_LOD] - at[GAME_WATCH_STAMP_UPLOAD]),
        (libc_double_t)game_watch_ms_of(at[GAME_WATCH_STAMP_FILL] - at[GAME_WATCH_STAMP_LOD]),
        (libc_double_t)game_watch_ms_of(queued_ns - at[GAME_WATCH_STAMP_FILL]),
        (libc_double_t)game_watch_ms_of(at[GAME_WATCH_STAMP_READY] - queued_ns),
        (libc_double_t)(drawn_ms - (float64_t)p->total_ms),
        (libc_double_t)game_watch_ms_of(take->taken_ns - at[GAME_WATCH_STAMP_READY]), (libc_double_t)p->total_ms,
        (libc_double_t)p->pass_ms[FRAME_PASS_COARSE], (libc_double_t)p->pass_ms[FRAME_PASS_PRIMARY],
        (libc_double_t)p->lod_ms, (libc_double_t)p->pass_ms[FRAME_PASS_SHADE],
        (libc_double_t)p->shade_ms[SHADE_SPAN_WATER], (libc_double_t)p->shade_ms[SHADE_SPAN_SURFACES],
        (libc_double_t)p->shade_ms[SHADE_SPAN_SUN], (libc_double_t)p->shade_ms[SHADE_SPAN_AO],
        (libc_double_t)p->shade_ms[SHADE_SPAN_FRESH], (libc_double_t)p->pass_ms[FRAME_PASS_POST],
        (libc_double_t)p->pass_ms[FRAME_PASS_READBACK], p->fresh, (libc_double_t)p->card_mhz, (uint32_t)p->clock_read,
        (libc_double_t)p->turn_deg_s, (libc_double_t)game_watch_ms_of(take->x_put_ns),
        (libc_double_t)game_watch_ms_of(take->x_wait_ns), (libc_double_t)game_watch_ms_of(take->x_idle_ns),
        (uint32_t)p->pace_how,
        (libc_double_t)(p->aimed_ns != 0 ? game_watch_ms_of(p->aimed_ns - at[GAME_WATCH_STAMP_START]) : 0.0f64),
        (libc_double_t)(p->start_at_ns != 0 ? game_watch_ms_of(at[GAME_WATCH_STAMP_START] - p->start_at_ns) : 0.0f64),
        (uint32_t)p->missed, take->puts_skipped, (uint32_t)p->percent);
    if (written > 0 && (uint32_t)written < GAME_WATCH_LOG_LINE_BYTES) {
        watch->log_used += (uint64_t)written;
    }
}

void game_watch_rings_read(game_watch_t *watch, int64_t now_ns, bool final) {
    if (watch->borrowed_takes == nullptr) {
        return;
    }
    const uint64_t made = atomic_load_explicit(&watch->takes_made, memory_order_acquire);
    const uint64_t drawn = atomic_load_explicit(&watch->pictures_drawn, memory_order_acquire);
    /* Far enough behind that main may be writing where the next would be read: those are gone. */
    const uint64_t keep = GAME_WATCH_RING - 64u;
    if (made - watch->takes_read > keep) {
        watch->takes_lost += made - keep - watch->takes_read;
        watch->takes_read = made - keep;
        watch->read_taken_ns = 0;
    }
    while (watch->takes_read < made) {
        const game_watch_take_t *const take = &watch->borrowed_takes[watch->takes_read % GAME_WATCH_RING];
        if (take->seq >= drawn && !final) {
            break; /* main took it as it was published, and the gpu thread is about to count it */
        }
        const game_watch_picture_t *picture = nullptr;
        if (take->seq < drawn && drawn - take->seq < keep) {
            picture = &watch->borrowed_pictures[take->seq % GAME_WATCH_RING];
            picture = picture->seq == take->seq ? picture : nullptr;
        }
        const int64_t interval_ns = watch->read_taken_ns != 0 ? take->taken_ns - watch->read_taken_ns : 0;
        if (watch->minute_pictures == 0u) {
            watch->minute_from_ns = take->taken_ns;
        }
        watch->minute_pictures++;
        watch->minute_late += game_watch_late(take, picture) ? 1u : 0u;
        if (interval_ns != 0) {
            const int64_t whole_ms = interval_ns / TIME_NS_PER_MS;
            watch->minute_bins[whole_ms < (int64_t)GAME_WATCH_BINS ? (uint32_t)whole_ms : GAME_WATCH_BINS - 1u]++;
        }
        if (watch->borrowed_log_text != nullptr && watch->log_failed == 0u) {
            game_watch_log_line(watch, take, picture, interval_ns);
        }
        watch->read_taken_ns = take->taken_ns;
        watch->takes_read++;
    }
    if (watch->borrowed_log_text != nullptr &&
        (final || watch->log_used >= GAME_WATCH_LOG_BYTES / 2u ||
         (watch->log_used != 0u && now_ns - watch->log_written_ns >= GAME_WATCH_LOG_NS))) {
        game_watch_log_write(watch, now_ns);
    }
}

/* The minute's pictures for its line, and a new minute. */
static void game_watch_minute_take(game_watch_t *watch, float64_t *out_rate, float64_t *out_p99_ms,
                                   uint32_t *out_late) {
    uint32_t intervals = 0u;
    for (uint32_t i = 0u; i < GAME_WATCH_BINS; i++) {
        intervals += watch->minute_bins[i];
    }
    const uint32_t wanted = intervals - intervals / 100u;
    uint32_t seen = 0u, p99 = 0u;
    for (uint32_t i = 0u; i < GAME_WATCH_BINS && intervals != 0u; i++) {
        seen += watch->minute_bins[i];
        if (seen >= wanted) {
            p99 = i;
            break;
        }
    }
    const float64_t seconds = (float64_t)(watch->read_taken_ns - watch->minute_from_ns) / (float64_t)TIME_NS_PER_SECOND;
    *out_rate =
        seconds > 0.0f64 && watch->minute_pictures > 1u ? (float64_t)(watch->minute_pictures - 1u) / seconds : 0.0f64;
    /* A bin is a whole ms, so its top says how long its intervals were at most. */
    *out_p99_ms = intervals != 0u ? (float64_t)(p99 + 1u) : 0.0f64;
    *out_late = watch->minute_late;
    watch->minute_pictures = 0u;
    watch->minute_late = 0u;
    memset(watch->minute_bins, 0, sizeof(watch->minute_bins));
}

static int32_t game_watch_compare(const void *left, const void *right) {
    const float32_t a = *(const float32_t *)left, b = *(const float32_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* The 99th percentile of count values, which it sorts. */
static float32_t game_watch_p99(float32_t *values, uint32_t count) {
    if (count == 0u) {
        return 0.0f32;
    }
    qsort(values, count, sizeof(values[0]), game_watch_compare);
    return values[(uint32_t)((uint64_t)count * 99u / 100u)];
}

bool game_watch_recent_read(const game_watch_t *watch, int64_t now_ns, game_watch_recent_t *out) {
    memset(out, 0, sizeof(*out));
    if (watch->borrowed_takes == nullptr) {
        return false;
    }
    const int64_t since_ns = now_ns - TIME_NS_PER_SECOND;
    float32_t values[GAME_WATCH_RECENT];
    /* What was on the screen: main's own takes. */
    const uint64_t made = atomic_load_explicit(&watch->takes_made, memory_order_relaxed);
    uint32_t count = 0u;
    float64_t put_sum_ms = 0.0f64;
    for (uint64_t i = made; i > 1u && count < GAME_WATCH_RECENT && made - i < GAME_WATCH_RECENT; i--) {
        const game_watch_take_t *const take = &watch->borrowed_takes[(i - 1u) % GAME_WATCH_RING];
        if (take->taken_ns < since_ns) {
            break;
        }
        const game_watch_take_t *const before = &watch->borrowed_takes[(i - 2u) % GAME_WATCH_RING];
        values[count] = (float32_t)game_watch_ms_of(take->taken_ns - before->taken_ns);
        out->interval_most_ms = values[count] > out->interval_most_ms ? values[count] : out->interval_most_ms;
        const game_watch_picture_t *const picture = &watch->borrowed_pictures[take->seq % GAME_WATCH_RING];
        out->late += game_watch_late(take, picture->seq == take->seq ? picture : nullptr) ? 1u : 0u;
        put_sum_ms += game_watch_ms_of(take->x_put_ns);
        count++;
    }
    out->puts_skipped = watch->x_puts_skipped;
    if (count != 0u) {
        out->interval_last_ms = values[0];
        out->x_put_mean_ms = (float32_t)(put_sum_ms / (float64_t)count);
        out->interval_p99_ms = game_watch_p99(values, count);
    }
    out->pictures = count;
    /* What the card did: the gpu thread's pictures, taken or not. */
    const uint64_t drawn = atomic_load_explicit(&watch->pictures_drawn, memory_order_acquire);
    uint32_t cards = 0u, turns = 0u;
    float64_t card_sum = 0.0f64, idle_sum = 0.0f64, turn_sum = 0.0f64, fresh_sum = 0.0f64;
    float64_t pass_sum[FRAME_PASS_COUNT] = {0.0f64};
    for (uint64_t i = drawn; i > 0u && cards < GAME_WATCH_RECENT; i--) {
        const game_watch_picture_t *const picture = &watch->borrowed_pictures[(i - 1u) % GAME_WATCH_RING];
        const int64_t ready_ns = picture->stamp_ns[GAME_WATCH_STAMP_READY];
        if (ready_ns < since_ns) {
            break;
        }
        if (cards == 0u) {
            out->card_mhz = picture->card_mhz;
            out->percent_last = picture->percent;
        }
        values[cards] = picture->total_ms;
        out->card_most_ms = picture->total_ms > out->card_most_ms ? picture->total_ms : out->card_most_ms;
        card_sum += (float64_t)picture->total_ms;
        for (uint32_t pass = 0u; pass < FRAME_PASS_COUNT; pass++) {
            pass_sum[pass] += (float64_t)picture->pass_ms[pass];
        }
        idle_sum +=
            game_watch_ms_of(picture->stamp_ns[GAME_WATCH_STAMP_START] - picture->stamp_ns[GAME_WATCH_STAMP_WAITED]);
        fresh_sum += (float64_t)picture->fresh;
        if (ready_ns >= now_ns - GAME_WATCH_TURN_NS) {
            turn_sum += (float64_t)picture->turn_deg_s;
            turns++;
        }
        cards++;
    }
    if (cards != 0u) {
        out->card_mean_ms = (float32_t)(card_sum / (float64_t)cards);
        out->idle_mean_ms = (float32_t)(idle_sum / (float64_t)cards);
        out->fresh_mean = (uint32_t)(fresh_sum / (float64_t)cards);
        for (uint32_t pass = 0u; pass < FRAME_PASS_COUNT; pass++) {
            out->pass_ms[pass] = (float32_t)(pass_sum[pass] / (float64_t)cards);
        }
        out->card_p99_ms = game_watch_p99(values, cards);
    }
    out->turn_deg_s = turns != 0u ? (float32_t)(turn_sum / (float64_t)turns) : 0.0f32;
    return count != 0u || cards != 0u;
}
