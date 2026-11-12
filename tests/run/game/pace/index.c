/* See README.md. Plan 008 phase 02's start rule, on a fake clock: no card, no threads. */
#include "game/pace/index.h"

#include "harness/index.h"

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

TEST_MAIN()
