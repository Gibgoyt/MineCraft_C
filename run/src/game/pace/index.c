/* See README.md. */
#include "game/pace/index.h"

#include "time/index.h"

#include <math.h>
#include <string.h>

void game_pace_start(game_pace_t *pace, bool just_in_time) {
    memset(pace, 0, sizeof(*pace));
    pace->just_in_time = just_in_time ? 1u : 0u;
    atomic_store_explicit(&pace->next_take_ns, 0, memory_order_relaxed);
}

int64_t game_pace_take_at(int64_t next_take_ns, int64_t tick_ns, int64_t at_ns) {
    if (next_take_ns <= 0 || tick_ns <= 0) {
        return 0;
    }
    if (at_ns <= next_take_ns) {
        return next_take_ns;
    }
    const int64_t ticks = (at_ns - next_take_ns + tick_ns - 1) / tick_ns;
    return next_take_ns + ticks * tick_ns;
}

void game_pace_decide(const game_pace_in_t *in, game_pace_out_t *out) {
    *out = (game_pace_out_t){.start_at_ns = in->now_ns, .aimed_ns = 0, .hold = 0u, .how = GAME_PACE_HOW_TODAY};
    /* Uncapped is someone asking for back to back. With no take known, today's rule: a picture no one has taken is plenty. */
    if (in->uncapped != 0u || in->next_take_ns <= 0 || in->tick_ns <= 0) {
        out->hold = in->uncapped == 0u && in->picture_waiting != 0u ? 1u : 0u;
        return;
    }
    const int64_t lead_ns = in->predicted_ns + in->margin_ns;
    /* The earliest take a picture started now makes, margin and all. */
    const int64_t reach_ns = game_pace_take_at(in->next_take_ns, in->tick_ns, in->now_ns + lead_ns);
    out->aimed_ns = reach_ns;
    if (in->last_aimed_ns != 0 && in->last_ready_ns > in->last_aimed_ns) {
        out->how =
            GAME_PACE_HOW_AFTER_LATE; /* it missed its take: waiting a slot more would only make this one late too */
        return;
    }
    if (lead_ns > (int64_t)GAME_PACE_JIT_TICKS * in->tick_ns) {
        out->how = GAME_PACE_HOW_BACK_TO_BACK; /* the card cannot keep 60 a second: every picture it can give */
        return;
    }
    if (in->picture_waiting != 0u) {
        /* Main takes it at its next take; the next one would then be just in time for the earliest take it can make from there. */
        const int64_t taken_ns = game_pace_take_at(in->next_take_ns, in->tick_ns, in->now_ns);
        const int64_t then_ns = game_pace_take_at(in->next_take_ns, in->tick_ns, taken_ns + lead_ns);
        out->hold = 1u;
        out->aimed_ns = then_ns;
        out->start_at_ns = in->just_in_time != 0u && then_ns - lead_ns > taken_ns ? then_ns - lead_ns : taken_ns;
        out->how = in->just_in_time != 0u ? GAME_PACE_HOW_JUST_IN_TIME : GAME_PACE_HOW_NOW;
        return;
    }
    if (in->just_in_time == 0u) {
        out->how = GAME_PACE_HOW_NOW;
        return;
    }
    out->how = GAME_PACE_HOW_JUST_IN_TIME;
    out->start_at_ns = reach_ns - lead_ns > in->now_ns ? reach_ns - lead_ns : in->now_ns;
}

void game_pace_note(game_pace_t *pace, int64_t started_ns, int64_t ready_ns, int64_t aimed_ns) {
    pace->samples_ns[pace->next] = ready_ns > started_ns ? ready_ns - started_ns : 0;
    pace->next = (pace->next + 1u) % GAME_PACE_SAMPLES;
    pace->count = pace->count < GAME_PACE_SAMPLES ? pace->count + 1u : GAME_PACE_SAMPLES;
    pace->last_ready_ns = ready_ns;
    pace->last_aimed_ns = aimed_ns;
    /* The 90th percentile: the smallest sample with at least nine in ten of them at or under it. Sixteen, so a sort by insertion. */
    int64_t sorted[GAME_PACE_SAMPLES];
    for (uint32_t i = 0u; i < pace->count; i++) {
        const int64_t value = pace->samples_ns[i];
        uint32_t at = i;
        while (at > 0u && sorted[at - 1u] > value) {
            sorted[at] = sorted[at - 1u];
            at--;
        }
        sorted[at] = value;
    }
    const uint32_t rank = (pace->count * 9u + 9u) / 10u; /* ceil(0.9 count), 1-based */
    pace->predicted_ns = sorted[rank - 1u];
}

/* ---- Plan 008 phase 05: the rung for each picture ------------------------------------------------------------------------------ */

void game_pace_ladder(uint32_t top_percent, uint32_t floor_percent, game_pace_ladder_t *out) {
    memset(out, 0, sizeof(*out));
    const uint32_t top = top_percent == 0u || top_percent > POST_SCALE_WHOLE ? POST_SCALE_WHOLE : top_percent;
    out->percent[0] = (uint8_t)top;
    out->count = 1u;
    if (floor_percent == 0u) {
        return; /* dynamic resolution off */
    }
    static const uint32_t rungs[GAME_PACE_RUNGS - 1u] = {GAME_PACE_RUNG_HIGH, GAME_PACE_RUNG_MIDDLE,
                                                         GAME_PACE_RUNG_LOW};
    for (uint32_t i = 0u; i < GAME_PACE_RUNGS - 1u; i++) {
        if (rungs[i] < top && rungs[i] >= floor_percent) {
            out->percent[out->count] = (uint8_t)rungs[i];
            out->count++;
        }
    }
}

void game_pace_ladder_set(game_pace_t *pace, uint32_t top_percent, uint32_t floor_percent) {
    game_pace_ladder(top_percent, floor_percent, &pace->ladder);
    pace->rung = 0u;
    pace->still = 0u;
    pace->fits = 0u;
}

void game_pace_note_drawn(game_pace_t *pace, int64_t card_ns, uint32_t percent) {
    const uint32_t at = (pace->next + GAME_PACE_SAMPLES - 1u) % GAME_PACE_SAMPLES;
    const int64_t card_us = card_ns > 0 ? card_ns / 1000 : 0;
    pace->card_us[at] = card_us < (int64_t)UINT32_MAX ? (uint32_t)card_us : UINT32_MAX;
    pace->drawn_percent[at] = (uint8_t)(percent == 0u || percent > POST_SCALE_WHOLE ? POST_SCALE_WHOLE : percent);
}

/* The share of the output's pixels a rung draws. */
static float64_t game_pace_share(uint32_t percent) {
    const uint32_t held = percent == 0u || percent > POST_SCALE_WHOLE ? POST_SCALE_WHOLE : percent;
    const float64_t side = (float64_t)held / (float64_t)POST_SCALE_WHOLE;
    return side * side;
}

/* The 90th percentile of count values, as game_pace_note takes T-hat's: the smallest with nine in ten at or under it. Sorts them. */
static float64_t game_pace_p90(float64_t *values, uint32_t count) {
    for (uint32_t i = 1u; i < count; i++) {
        const float64_t value = values[i];
        uint32_t at = i;
        while (at > 0u && values[at - 1u] > value) {
            values[at] = values[at - 1u];
            at--;
        }
        values[at] = value;
    }
    return values[(count * 9u + 9u) / 10u - 1u];
}

int64_t game_pace_predict_ns(const game_pace_t *pace, uint32_t percent) {
    if (pace->count == 0u) {
        return 0;
    }
    float64_t rest[GAME_PACE_SAMPLES];
    float64_t card[GAME_PACE_SAMPLES];
    for (uint32_t i = 0u; i < pace->count; i++) {
        const float64_t g = (float64_t)pace->card_us[i] * 1000.0f64;
        const float64_t all = (float64_t)pace->samples_ns[i];
        rest[i] = all > g ? all - g : 0.0f64;
        card[i] = g / game_pace_share(pace->drawn_percent[i]);
    }
    const float64_t p_hat = game_pace_p90(rest, pace->count);
    const float64_t g_hat = game_pace_p90(card, pace->count);
    return (int64_t)(p_hat + g_hat * game_pace_share(percent));
}

uint32_t game_pace_rung(game_pace_t *pace, const game_pace_rung_in_t *in) {
    const game_pace_ladder_t *const ladder = &pace->ladder;
    if (ladder->count <= 1u) {
        pace->rung = 0u;
        pace->fits = 0u;
        return ladder->count == 0u ? POST_SCALE_WHOLE : ladder->percent[0];
    }
    const bool still = !(in->turn_pixels >= GAME_PACE_RUNG_STILL_PIXELS);
    pace->still = still ? (uint8_t)(pace->still < UINT8_MAX ? pace->still + 1u : UINT8_MAX) : 0u;
    if (pace->still >= GAME_PACE_RUNG_STILL) {
        /* Still long enough: the whole picture, whatever it costs, and phase 02's rule paces it. */
        pace->rung = 0u;
        pace->fits = 0u;
        return ladder->percent[0];
    }
    const float64_t slot_ns = (float64_t)((int64_t)GAME_PACE_JIT_TICKS * in->tick_ns);
    const int64_t budget_ns = (int64_t)(slot_ns * (float64_t)GAME_PACE_RUNG_SLOT_SHARE);
    const uint32_t now = ladder->percent[pace->rung];
    /* A late picture drawn above this rung (one that was on its way as the rung stepped down) has been answered already. */
    const bool late = in->late != 0u && in->late_percent <= now;
    const bool dear = in->turn_degrees > GAME_PACE_RUNG_TURN_DEGREES && game_pace_predict_ns(pace, now) > budget_ns;
    if (!still && (late || dear) && pace->rung + 1u < ladder->count) {
        pace->rung++;
        pace->fits = 0u;
        return ladder->percent[pace->rung];
    }
    if (pace->rung == 0u) {
        pace->fits = 0u;
        return now;
    }
    const bool up_fits = game_pace_predict_ns(pace, ladder->percent[pace->rung - 1u]) < budget_ns;
    pace->fits = up_fits ? (uint8_t)(pace->fits + 1u) : 0u;
    if (pace->fits >= GAME_PACE_RUNG_STEP_UP) {
        pace->rung--;
        pace->fits = 0u;
    }
    return ladder->percent[pace->rung];
}

uint32_t game_pace_rung_next(game_pace_t *pace, const render_frame_t *frame, const render_camera_t *camera,
                             int64_t now_ns) {
    traverse_camera_t view;
    render_camera_to_traverse(camera, frame->width, frame->height, &view);
    /* The frame's last camera has no jitter, and post_turn_pixels counts in this one's pixels: the output's. */
    const float64_t pixels = post_turn_pixels(&frame->temporal.previous, &view);
    const float64_t rows = (float64_t)(frame->height != 0u ? frame->height : 1u);
    const float64_t pixel_radians = atan(2.0f64 * (float64_t)camera->half_fov_tangent / rows);
    const int64_t since_ns = pace->last_begun_ns != 0 ? now_ns - pace->last_begun_ns : 0;
    pace->last_begun_ns = now_ns;
    const float64_t degrees_per_radian = 180.0f64 / 3.14159265358979f64;
    const float64_t seconds = (float64_t)since_ns / (float64_t)TIME_NS_PER_SECOND;
    const float64_t degrees = since_ns > 0 ? pixels * pixel_radians * degrees_per_radian / seconds : 0.0f64;
    const uint32_t last = (pace->next + GAME_PACE_SAMPLES - 1u) % GAME_PACE_SAMPLES;
    const game_pace_rung_in_t in = {
        .tick_ns = (int64_t)TIME_NS_PER_SECOND / (int64_t)MC_DISPLAY_REFRESH_HZ,
        .turn_pixels = (float32_t)pixels,
        .turn_degrees = (float32_t)degrees,
        .late = pace->last_aimed_ns != 0 && pace->last_ready_ns > pace->last_aimed_ns ? 1u : 0u,
        .late_percent = pace->count != 0u ? pace->drawn_percent[last] : (uint8_t)POST_SCALE_WHOLE,
    };
    return game_pace_rung(pace, &in);
}
