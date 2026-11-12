/* See README.md. */
#include "game/pace/index.h"

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
