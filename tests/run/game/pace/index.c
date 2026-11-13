/* See README.md. Plan 008 phase 02's start rule, on a fake clock: no card, no threads. */
#include "game/pace/index.h"

#include "harness/index.h"

#include <stdio.h>
#include <string.h>

enum : int64_t {
    PACE_MS = 1000000,
    PACE_TICK = 1000000000 / 120, /* main's tick at 120 Hz: 8.33 ms */
};

/* ms as ns, from a ms count with three decimals: 16670 is 16.670 ms. */
static int64_t pace_us(int64_t microseconds) {
    return microseconds * 1000;
}

static game_pace_in_t pace_in(int64_t now_ns, int64_t next_take_ns, int64_t predicted_ns, bool just_in_time) {
    return (game_pace_in_t){.now_ns = now_ns,
                            .next_take_ns = next_take_ns,
                            .tick_ns = PACE_TICK,
                            .predicted_ns = predicted_ns,
                            .margin_ns = GAME_PACE_MARGIN_NS,
                            .just_in_time = just_in_time ? 1u : 0u};
}

TEST(a_late_picture_starts_the_next_at_once) {
    game_pace_in_t in = pace_in(pace_us(17100), 2 * PACE_TICK + PACE_TICK, 17 * PACE_MS, true);
    in.last_ready_ns = pace_us(17100);
    in.last_aimed_ns = 2 * PACE_TICK; /* 16.67 */
    in.picture_waiting = 1u;          /* main takes it at 25 */
    game_pace_out_t out;
    game_pace_decide(&in, &out);
    CHECK_EQ_I64(out.start_at_ns, in.now_ns);
    CHECK_EQ_U64(out.hold, 0u);
    CHECK_EQ_U64(out.how, GAME_PACE_HOW_AFTER_LATE);
    /* The same with a cheap picture: missing its take is what counts, not the picture's cost. */
    in.predicted_ns = 5 * PACE_MS;
    game_pace_decide(&in, &out);
    CHECK_EQ_I64(out.start_at_ns, in.now_ns);
    CHECK_EQ_U64(out.hold, 0u);
}

TEST(fifteen_ms_pictures_start_just_in_time) {
    game_pace_in_t in = pace_in(pace_us(100), 2 * PACE_TICK, 15 * PACE_MS, true);
    game_pace_out_t out;
    game_pace_decide(&in, &out);
    CHECK_NEAR(out.start_at_ns, pace_us(667), 1000);
    CHECK_EQ_I64(out.aimed_ns, 2 * PACE_TICK);
    CHECK_EQ_U64(out.hold, 0u);
    CHECK_EQ_U64(out.how, GAME_PACE_HOW_JUST_IN_TIME);
    /* Main's very next take is at 8.33: the earliest a 15 ms picture can make is still 16.67. */
    in.next_take_ns = PACE_TICK;
    game_pace_decide(&in, &out);
    CHECK_NEAR(out.start_at_ns, pace_us(667), 1000);
    CHECK_EQ_I64(out.aimed_ns, 2 * PACE_TICK);
    /* Q3 B: at once. */
    in.just_in_time = 0u;
    game_pace_decide(&in, &out);
    CHECK_EQ_I64(out.start_at_ns, pace_us(100));
    CHECK_EQ_U64(out.how, GAME_PACE_HOW_NOW);
}

/* A run of pictures on the fake clock: main takes at k * PACE_TICK for k >= 1, the newest picture ready by then; the gpu thread asks the
 * rule, sleeps or waits as it says, and draws for cost_ns(i). today runs today's rule: no next take known. */
typedef struct pace_run_t {
    int64_t start_ns[2048]; /* 0 */
    int64_t ready_ns[2048]; /* 16384 */
    uint32_t count;         /* 32768 */
    uint8_t _pad0[4];       /* 32772 */
} pace_run_t;
static_assert(sizeof(pace_run_t) == 32776);

/* What each picture costs: constant_ns, or 5 to 30 ms at random with constant_ns 0. */
typedef struct pace_cost_t {
    int64_t constant_ns; /* 0 */
    uint64_t state;      /* 8 */
} pace_cost_t;
static_assert(sizeof(pace_cost_t) == 16);

static int64_t pace_cost(pace_cost_t *cost) {
    if (cost->constant_ns != 0) {
        return cost->constant_ns;
    }
    cost->state = cost->state * 6364136223846793005ull + 1442695040888963407ull;
    return 5 * PACE_MS + (int64_t)((cost->state >> 33) % (uint64_t)(25 * PACE_MS));
}

static int64_t pace_next_take(int64_t now_ns) {
    return (now_ns / PACE_TICK + 1) * PACE_TICK;
}

/* The first take at or after ready_ns. */
static int64_t pace_shown_at(int64_t ready_ns) {
    return (ready_ns + PACE_TICK - 1) / PACE_TICK * PACE_TICK;
}

static void pace_simulate(test_context_t *t, pace_run_t *run, uint32_t pictures, pace_cost_t *cost, bool today) {
    game_pace_t pace;
    game_pace_start(&pace, true);
    memset(run, 0, sizeof(*run));
    int64_t now_ns = PACE_TICK; /* main has just made its first take */
    uint32_t asks = 0u;
    while (run->count < pictures && asks < 1000000u) {
        asks++;
        const int64_t last_ready_ns = run->count != 0u ? run->ready_ns[run->count - 1u] : 0;
        game_pace_in_t in = pace_in(now_ns, today ? 0 : pace_next_take(now_ns), pace.predicted_ns, true);
        in.last_ready_ns = pace.last_ready_ns;
        in.last_aimed_ns = pace.last_aimed_ns;
        in.picture_waiting = run->count != 0u && pace_shown_at(last_ready_ns) > now_ns ? 1u : 0u;
        game_pace_out_t out;
        game_pace_decide(&in, &out);
        if (out.start_at_ns < now_ns) {
            CHECK(out.start_at_ns >= now_ns);
            return;
        }
        if (out.hold != 0u) {
            now_ns = pace_shown_at(last_ready_ns); /* until main takes it */
            continue;
        }
        if (out.start_at_ns > now_ns) {
            now_ns = out.start_at_ns; /* sleeps, then asks again */
            continue;
        }
        const uint32_t i = run->count++;
        run->start_ns[i] = now_ns;
        run->ready_ns[i] = now_ns + pace_cost(cost);
        if (i != 0u && run->start_ns[i] < run->ready_ns[i - 1u]) {
            CHECK(run->start_ns[i] >= run->ready_ns[i - 1u]); /* never two in flight */
        }
        game_pace_note(&pace, run->start_ns[i], run->ready_ns[i], out.aimed_ns);
        now_ns = run->ready_ns[i];
    }
    CHECK_EQ_U64(run->count, pictures);
}

/* Mean ms between readies, and between new pictures main showed, past the first ten. */
static void pace_means(const pace_run_t *run, float64_t *out_ready_ms, float64_t *out_shown_ms, int64_t *out_shown_min,
                       int64_t *out_shown_max) {
    const uint32_t from = 10u;
    *out_ready_ms = (float64_t)(run->ready_ns[run->count - 1u] - run->ready_ns[from]) /
                    (float64_t)(run->count - 1u - from) / (float64_t)PACE_MS;
    int64_t first = 0, last = 0, shown_before = 0;
    uint32_t shown = 0u;
    *out_shown_min = INT64_MAX;
    *out_shown_max = 0;
    for (uint32_t i = from; i < run->count; i++) {
        const int64_t at = pace_shown_at(run->ready_ns[i]);
        if (i + 1u < run->count && pace_shown_at(run->ready_ns[i + 1u]) == at) {
            continue; /* replaced before main took it */
        }
        if (shown != 0u) {
            const int64_t gap = at - shown_before;
            *out_shown_min = gap < *out_shown_min ? gap : *out_shown_min;
            *out_shown_max = gap > *out_shown_max ? gap : *out_shown_max;
        } else {
            first = at;
        }
        last = at;
        shown_before = at;
        shown++;
    }
    *out_shown_ms = (float64_t)(last - first) / (float64_t)(shown - 1u) / (float64_t)PACE_MS;
}

TEST(seventeen_ms_pictures_give_seventeen_ms_intervals) {
    static pace_run_t run;
    pace_cost_t cost = {.constant_ns = 17 * PACE_MS};
    pace_simulate(t, &run, 600u, &cost, false);
    float64_t ready_ms = 0.0f64, shown_ms = 0.0f64;
    int64_t least = 0, most = 0;
    pace_means(&run, &ready_ms, &shown_ms, &least, &most);
    CHECK_NEAR(ready_ms, 17.0f64, 0.1f64);
    CHECK_NEAR(shown_ms, 17.0f64, 0.3f64);
    CHECK_NEAR(least, 2 * PACE_TICK, 1); /* 16.67 or 25 */
    CHECK_NEAR(most, 3 * PACE_TICK, 1);
    /* Today's rule in the same run: a picture each three ticks. */
    pace_simulate(t, &run, 600u, &cost, true);
    pace_means(&run, &ready_ms, &shown_ms, &least, &most);
    CHECK_NEAR(shown_ms, 25.0f64, 0.01f64);
    CHECK_NEAR(ready_ms, 25.0f64, 0.01f64);
}

/* At 14 ms a picture fits two ticks: one each two, started just in time, never two in one tick. The plan's "one a slot" is per two ticks,
 * as no 14 ms picture can fit one 8.33 ms tick. */
TEST(fourteen_ms_pictures_are_one_a_slot) {
    static pace_run_t run;
    pace_cost_t cost = {.constant_ns = 14 * PACE_MS};
    pace_simulate(t, &run, 300u, &cost, false);
    const int64_t span_ns = run.start_ns[run.count - 1u] - run.start_ns[0];
    CHECK(span_ns / PACE_TICK >= 598 && span_ns / PACE_TICK <= 600); /* 300 pictures over 600 ticks */
    for (uint32_t i = 1u; i < run.count; i++) {
        CHECK(run.start_ns[i] / PACE_TICK != run.start_ns[i - 1u] / PACE_TICK);
        if (i > 2u) {
            CHECK_NEAR(run.start_ns[i] - run.start_ns[i - 1u], 2 * PACE_TICK, 1);
            /* Just in time: ready 1 ms before the take it is for. */
            CHECK_NEAR(pace_shown_at(run.ready_ns[i]) - run.ready_ns[i], GAME_PACE_MARGIN_NS, 2);
        }
    }
}

TEST(never_two_in_flight) {
    static pace_run_t run;
    pace_cost_t random = {.constant_ns = 0, .state = 12345u};
    pace_simulate(t, &run, 2000u, &random, false);
    for (uint32_t i = 1u; i < run.count; i++) {
        CHECK(run.start_ns[i] >= run.ready_ns[i - 1u]);
    }
    /* And the rule alone never starts before now, whatever it is asked. */
    for (uint32_t i = 0u; i < 20000u; i++) {
        game_pace_in_t in = pace_in(0, 0, 0, (i & 1u) != 0u);
        in.now_ns = (int64_t)(pace_cost(&random) * 7);
        in.next_take_ns = (i % 5u) == 0u ? 0 : in.now_ns - 20 * PACE_MS + pace_cost(&random);
        in.predicted_ns = pace_cost(&random) - 5 * PACE_MS;
        in.last_ready_ns = in.now_ns - (pace_cost(&random) - 5 * PACE_MS);
        in.last_aimed_ns = (i % 3u) == 0u ? 0 : in.last_ready_ns + pace_cost(&random) - 15 * PACE_MS;
        in.picture_waiting = (uint8_t)((i >> 1u) & 1u);
        in.uncapped = (uint8_t)((i % 7u) == 0u ? 1u : 0u);
        game_pace_out_t out;
        game_pace_decide(&in, &out);
        CHECK(out.start_at_ns >= in.now_ns);
    }
}

TEST(the_ninetieth_percentile_of_sixteen) {
    game_pace_t pace;
    game_pace_start(&pace, true);
    /* 1 to 16 ms, out of order. Nine in ten of sixteen is 14.4: the 15th smallest. */
    const int64_t order[GAME_PACE_SAMPLES] = {7, 3, 16, 1, 9, 12, 2, 15, 5, 11, 4, 14, 6, 13, 8, 10};
    for (uint32_t i = 0u; i < GAME_PACE_SAMPLES; i++) {
        game_pace_note(&pace, 100 * PACE_MS, 100 * PACE_MS + order[i] * PACE_MS, 0);
    }
    CHECK_EQ_I64(pace.predicted_ns, 15 * PACE_MS);
    /* Past sixteen, the oldest go: sixteen of 20 ms, and it is 20. */
    for (uint32_t i = 0u; i < GAME_PACE_SAMPLES; i++) {
        game_pace_note(&pace, 0, 20 * PACE_MS, 0);
    }
    CHECK_EQ_I64(pace.predicted_ns, 20 * PACE_MS);
    /* With fewer than sixteen: one sample is itself; ten of them is the 9th. */
    game_pace_start(&pace, true);
    game_pace_note(&pace, 0, 3 * PACE_MS, 0);
    CHECK_EQ_I64(pace.predicted_ns, 3 * PACE_MS);
    for (int64_t i = 2; i <= 10; i++) {
        game_pace_note(&pace, 0, i * PACE_MS, 0);
    }
    CHECK_EQ_I64(pace.predicted_ns, 9 * PACE_MS); /* 2, 3, 3, ..., 10: the 9th of 10 */
    CHECK_EQ_I64(pace.last_ready_ns, 10 * PACE_MS);
}

TEST(uncapped_starts_now) {
    game_pace_in_t in = pace_in(pace_us(3000), PACE_TICK, 15 * PACE_MS, true);
    in.uncapped = 1u;
    in.picture_waiting = 1u;
    game_pace_out_t out;
    game_pace_decide(&in, &out);
    CHECK_EQ_I64(out.start_at_ns, in.now_ns);
    CHECK_EQ_U64(out.hold, 0u);
}

TEST(with_no_take_known_the_rule_is_todays) {
    game_pace_in_t in = pace_in(pace_us(3000), 0, 15 * PACE_MS, true);
    in.picture_waiting = 1u;
    game_pace_out_t out;
    game_pace_decide(&in, &out);
    CHECK_EQ_U64(out.hold, 1u);
    CHECK_EQ_U64(out.how, GAME_PACE_HOW_TODAY);
    in.picture_waiting = 0u;
    game_pace_decide(&in, &out);
    CHECK_EQ_U64(out.hold, 0u);
    CHECK_EQ_I64(out.start_at_ns, in.now_ns);
}

TEST(a_picture_waiting_and_on_time_holds_until_main_takes_it) {
    /* 15 ms, ready at 15.67 for the take at 16.67: starting now would make 33.33, as starting just in time after the take would. */
    game_pace_in_t in = pace_in(pace_us(15670), 2 * PACE_TICK, 15 * PACE_MS, true);
    in.picture_waiting = 1u;
    in.last_ready_ns = pace_us(15670);
    in.last_aimed_ns = 2 * PACE_TICK;
    game_pace_out_t out;
    game_pace_decide(&in, &out);
    CHECK_EQ_U64(out.hold, 1u);
    CHECK_EQ_I64(out.aimed_ns, 4 * PACE_TICK);
    CHECK_NEAR(out.start_at_ns, 4 * PACE_TICK - 16 * PACE_MS, 1);
    /* A stale next take, from before the last one, is the same beat. */
    in.next_take_ns = PACE_TICK;
    game_pace_decide(&in, &out);
    CHECK_EQ_I64(out.aimed_ns, 4 * PACE_TICK);
}

/* ---- Plan 008 phase 05: the rung for each picture ------------------------------------------------------------------------------ */

#define PACE_TURNING_PIXELS                                                                                            \
    40.0f32 /* 720 degrees a second at 60 pictures and 1080 rows: 12 degrees, about 40 pixels at the centre */
#define PACE_TURNING_DEGREES 720.0f32

/* A pace with a ladder from 100 to a floor, and the last picture taken p_ms of the gpu thread's work and g_ms of the card's at a rung. */
static void pace_rung_start(game_pace_t *pace, uint32_t floor_percent) {
    game_pace_start(pace, true);
    game_pace_ladder_set(pace, 100u, floor_percent);
}

static void pace_drawn(game_pace_t *pace, float64_t p_ms, float64_t g_ms, uint32_t percent, bool late) {
    const int64_t started = pace->last_ready_ns + 1000;
    const int64_t ready = started + (int64_t)((p_ms + g_ms) * (float64_t)PACE_MS);
    game_pace_note(pace, started, ready, late ? ready - 1 : ready + 1);
    game_pace_note_drawn(pace, (int64_t)(g_ms * (float64_t)PACE_MS), percent);
}

static game_pace_rung_in_t pace_rung_in(float32_t pixels, float32_t degrees, const game_pace_t *pace) {
    const uint32_t last = (pace->next + GAME_PACE_SAMPLES - 1u) % GAME_PACE_SAMPLES;
    return (game_pace_rung_in_t){
        .tick_ns = PACE_TICK,
        .turn_pixels = pixels,
        .turn_degrees = degrees,
        .late = pace->last_aimed_ns != 0 && pace->last_ready_ns > pace->last_aimed_ns ? 1u : 0u,
        .late_percent = pace->count != 0u ? pace->drawn_percent[last] : 100u,
    };
}

TEST(the_ladder_is_the_scale_then_the_rungs_down_to_the_floor) {
    game_pace_ladder_t ladder;
    game_pace_ladder(100u, 50u, &ladder);
    CHECK_EQ_U64(ladder.count, 4u);
    CHECK(ladder.percent[0] == 100u && ladder.percent[1] == 85u && ladder.percent[2] == 70u &&
          ladder.percent[3] == 50u);
    game_pace_ladder(75u, 50u, &ladder); /* a render scale of 75: never above it */
    CHECK_EQ_U64(ladder.count, 3u);
    CHECK(ladder.percent[0] == 75u && ladder.percent[1] == 70u && ladder.percent[2] == 50u);
    game_pace_ladder(100u, 100u, &ladder); /* a floor of 100: on, with nowhere to go */
    CHECK_EQ_U64(ladder.count, 1u);
    game_pace_ladder(0u, 0u, &ladder); /* off, and a scale of 0 is 100 */
    CHECK_EQ_U64(ladder.count, 1u);
    CHECK_EQ_U64(ladder.percent[0], 100u);
}

TEST(the_cost_model_scales_the_card_by_its_pixels) {
    game_pace_t pace;
    pace_rung_start(&pace, 50u);
    for (uint32_t i = 0u; i < GAME_PACE_SAMPLES; i++) {
        pace_drawn(&pace, 1.0f64, 18.0f64 * 0.7225f64, 85u, false); /* 18 ms at 100, drawn at 85 */
    }
    CHECK_NEAR(game_pace_predict_ns(&pace, 100u), 19 * PACE_MS, 1000);
    CHECK_NEAR(game_pace_predict_ns(&pace, 85u), (int64_t)(14.005f64 * (float64_t)PACE_MS), 1000); /* the plan's 14.0 */
    CHECK_NEAR(game_pace_predict_ns(&pace, 50u), (int64_t)(5.5f64 * (float64_t)PACE_MS), 1000);
}

TEST(a_late_picture_steps_down_within_two) {
    game_pace_t pace;
    pace_rung_start(&pace, 50u);
    pace_drawn(&pace, 1.0f64, 12.0f64, 100u, false);
    game_pace_rung_in_t in = pace_rung_in(PACE_TURNING_PIXELS, 90.0f32, &pace); /* slow: only lateness decides */
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 100u);
    pace_drawn(&pace, 1.0f64, 16.5f64, 100u, true); /* picture 0, late */
    in = pace_rung_in(PACE_TURNING_PIXELS, 90.0f32, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 85u);
    /* A picture still on its way at 100 when it stepped comes in late too: it was drawn at a rung already left, and does not count. */
    pace_drawn(&pace, 1.0f64, 16.5f64, 100u, true);
    in = pace_rung_in(PACE_TURNING_PIXELS, 90.0f32, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 85u);
}

TEST(a_fast_turn_over_the_slot_steps_down_before_a_late_one) {
    game_pace_t pace;
    pace_rung_start(&pace, 50u);
    for (uint32_t i = 0u; i < GAME_PACE_SAMPLES; i++) {
        pace_drawn(&pace, 1.0f64, 16.0f64, 100u, false); /* 17 ms: over 80 % of the slot, 13.3, and none late (yet) */
    }
    game_pace_rung_in_t in = pace_rung_in(PACE_TURNING_PIXELS, PACE_TURNING_DEGREES, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 85u); /* 12.6 fits */
    in = pace_rung_in(PACE_TURNING_PIXELS, PACE_TURNING_DEGREES, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 85u); /* and stays there */
    /* Under 180 degrees a second the prediction alone does not step down. */
    pace_rung_start(&pace, 50u);
    for (uint32_t i = 0u; i < GAME_PACE_SAMPLES; i++) {
        pace_drawn(&pace, 1.0f64, 16.0f64, 100u, false);
    }
    in = pace_rung_in(PACE_TURNING_PIXELS, 179.0f32, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 100u);
}

TEST(a_still_camera_returns_to_full_in_fifteen) {
    game_pace_t pace;
    pace_rung_start(&pace, 50u);
    for (uint32_t i = 0u; i < GAME_PACE_SAMPLES; i++) {
        pace_drawn(&pace, 1.0f64, 40.0f64 * 0.25f64, 50u, false); /* 40 ms at 100: no rung above 50 fits */
    }
    pace.rung = 3u;
    for (uint32_t i = 1u; i <= GAME_PACE_RUNG_STILL; i++) {
        const game_pace_rung_in_t in = pace_rung_in(0.5f32, 15.0f32, &pace);
        const uint32_t rung = game_pace_rung(&pace, &in);
        CHECK_EQ_U64(rung, i < GAME_PACE_RUNG_STILL ? 50u : 100u);
        pace_drawn(&pace, 1.0f64, 40.0f64 * (float64_t)(rung * rung) / 10000.0f64, rung, rung == 100u);
    }
    /* And stays there while still, late or not. */
    const game_pace_rung_in_t in = pace_rung_in(0.5f32, 15.0f32, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 100u);
    /* Where the cost allows, it is back before the fifteenth, one rung each GAME_PACE_RUNG_STEP_UP pictures. */
    pace_rung_start(&pace, 50u);
    pace.rung = 3u;
    for (uint32_t i = 0u; i < GAME_PACE_SAMPLES; i++) {
        pace_drawn(&pace, 1.0f64, 8.0f64 * 0.25f64, 50u, false); /* 9 ms at 100 */
    }
    uint32_t rung = 50u;
    for (uint32_t i = 0u; i < GAME_PACE_RUNG_STEP_UP; i++) {
        const game_pace_rung_in_t moving = pace_rung_in(PACE_TURNING_PIXELS, PACE_TURNING_DEGREES, &pace);
        rung = game_pace_rung(&pace, &moving);
    }
    CHECK_EQ_U64(rung, 70u);
}

TEST(a_still_camera_never_steps_down) {
    game_pace_t pace;
    pace_rung_start(&pace, 50u);
    for (uint32_t i = 0u; i < 100u; i++) {
        pace_drawn(&pace, 1.0f64, 30.0f64, 100u, true);
        const game_pace_rung_in_t in = pace_rung_in(0.0f32, 0.0f32, &pace);
        CHECK_EQ_U64(game_pace_rung(&pace, &in), 100u);
    }
    /* Under GAME_PACE_RUNG_STILL_PIXELS is still too: a hand resting on the mouse. */
    pace_drawn(&pace, 1.0f64, 30.0f64, 100u, true);
    const game_pace_rung_in_t in = pace_rung_in(GAME_PACE_RUNG_STILL_PIXELS * 0.99f32, 0.0f32, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 100u);
}

TEST(the_floor_holds) {
    game_pace_t pace;
    pace_rung_start(&pace, 70u);
    uint32_t least = 100u;
    for (uint32_t i = 0u; i < 50u; i++) {
        const game_pace_rung_in_t in = pace_rung_in(PACE_TURNING_PIXELS, PACE_TURNING_DEGREES, &pace);
        const uint32_t rung = game_pace_rung(&pace, &in);
        least = rung < least ? rung : least;
        pace_drawn(&pace, 1.0f64, 80.0f64 * (float64_t)(rung * rung) / 10000.0f64, rung, true);
    }
    CHECK_EQ_U64(least, 70u);
}

TEST(off_is_off) {
    game_pace_t pace;
    game_pace_start(&pace, true);
    game_pace_ladder_set(&pace, 100u, 0u);
    for (uint32_t i = 0u; i < 50u; i++) {
        pace_drawn(&pace, 1.0f64, 80.0f64, 100u, true);
        const game_pace_rung_in_t in = pace_rung_in(PACE_TURNING_PIXELS, PACE_TURNING_DEGREES, &pace);
        CHECK_EQ_U64(game_pace_rung(&pace, &in), 100u);
    }
    /* Off with a render scale under 100: always the scale. */
    game_pace_ladder_set(&pace, 60u, 0u);
    const game_pace_rung_in_t in = pace_rung_in(PACE_TURNING_PIXELS, PACE_TURNING_DEGREES, &pace);
    CHECK_EQ_U64(game_pace_rung(&pace, &in), 60u);
}

/* The card's time at 100 % in a direction of a shake about Ahmed's spot, at 600 MHz: phase 01's cost rose at render distance 64 (median
 * 10.9 ms, p90 16, worst 33 up at the peak), with phase 04's turning costs on it. Phase 01 has no per-picture log at 600 MHz (the card
 * sat at 148), so this is the shake rebuilt from its numbers: a triangle of +-60 degrees whose dear side reaches 24 ms. */
static float64_t pace_shake_card_ms(float64_t yaw_degrees) {
    const float64_t side = (yaw_degrees + 60.0f64) / 120.0f64; /* 0 at -60, 1 at +60 */
    return 9.0f64 + 15.0f64 * side * side;
}

TEST(phase_01s_shake_stays_under_the_slot) {
    game_pace_t pace;
    pace_rung_start(&pace, 50u);
    float32_t *const intervals = test_alloc(t, 600u * sizeof(float32_t));
    float64_t yaw = 0.0f64, direction = 1.0f64;
    uint32_t rungs[4] = {0u, 0u, 0u, 0u};
    for (uint32_t i = 0u; i < 600u; i++) {
        /* What the last picture took sets how far the camera turned before this one. */
        const float64_t last_ms = i == 0u ? 16.67f64 : (float64_t)intervals[i - 1u];
        const float64_t turned = 720.0f64 * last_ms / 1000.0f64;
        yaw += direction * turned;
        if (yaw > 60.0f64 || yaw < -60.0f64) {
            direction = -direction;
            yaw = yaw > 60.0f64 ? 120.0f64 - yaw : -120.0f64 - yaw;
        }
        const game_pace_rung_in_t in =
            pace_rung_in((float32_t)(turned / 0.0648f64), PACE_TURNING_DEGREES, &pace); /* 0.0648 degrees a pixel */
        const uint32_t rung = game_pace_rung(&pace, &in);
        rungs[rung == 100u ? 0u : (rung == 85u ? 1u : (rung == 70u ? 2u : 3u))]++;
        const float64_t g_ms = pace_shake_card_ms(yaw) * (float64_t)(rung * rung) / 10000.0f64;
        intervals[i] = (float32_t)(1.0f64 + g_ms);
        pace_drawn(&pace, 1.0f64, g_ms, rung, intervals[i] > 16.67f32);
    }
    /* p99 of the predicted intervals: the 594th of 600 sorted. */
    for (uint32_t i = 1u; i < 600u; i++) {
        const float32_t value = intervals[i];
        uint32_t at = i;
        while (at > 0u && intervals[at - 1u] > value) {
            intervals[at] = intervals[at - 1u];
            at--;
        }
        intervals[at] = value;
    }
    printf("  shake at 600 MHz: p50 %.2f, p99 %.2f, worst %.2f ms; rungs 100/85/70/50: %u/%u/%u/%u of 600\n",
           (libc_double_t)intervals[300], (libc_double_t)intervals[593], (libc_double_t)intervals[599], rungs[0],
           rungs[1], rungs[2], rungs[3]);
    CHECK(intervals[593] <= 16.7f32);
    CHECK(rungs[0] < 600u); /* it did draw smaller */
}

TEST_MAIN()
