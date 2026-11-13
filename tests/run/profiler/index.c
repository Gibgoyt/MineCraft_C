/* ctest run_profiler. See README.md. */
#include "harness/index.h"

#include "display/flip/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "main/index.h"
#include "profiler/index.h"
#include "render/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "types/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint64_t {
    PR_TRACE_BYTES = 128ull * 1024u * 1024u, /* the most a trace here comes to */
    PR_EVENTS_MAX = 1ull << 20u,
};
enum : uint32_t {
    PR_NAME_BYTES = 80u,
    PR_TIDS_MAX = PROFILER_LANES_MAX + 1u,
    PR_THREADS = 8u,
    PR_THREAD_ROUNDS = 100u,
    PR_JOBS = 1000u,
    PR_PROGRAM_TIMEOUT_MS = 120000u,
    PR_FRAMES_WANTED = 30u,
};

static const profiler_limits_t pr_small = {.records_per_lane = 64u, .lane_count = 4u, ._pad0 = {0}};

/* ---- Reading a trace, knowing nothing of how it was written ----------------------------------------------------------- */

static void pr_skip_space(const char *text, uint64_t length, uint64_t *at) {
    while (*at < length && (text[*at] == ' ' || text[*at] == '\n' || text[*at] == '\t' || text[*at] == '\r')) {
        *at += 1u;
    }
}

static bool pr_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool pr_is_hex(char c) {
    return pr_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool pr_json_string(const char *text, uint64_t length, uint64_t *at) {
    if (*at >= length || text[*at] != '"') {
        return false;
    }
    *at += 1u;
    while (*at < length) {
        const uint8_t c = (uint8_t)text[*at];
        if (c == '"') {
            *at += 1u;
            return true;
        }
        if (c < 0x20u || c >= 0x7fu) {
            return false; /* it says it writes only ASCII, with the rest escaped */
        }
        if (c != '\\') {
            *at += 1u;
            continue;
        }
        if (*at + 1u >= length) {
            return false;
        }
        const char kind = text[*at + 1u];
        if (kind == 'u') {
            if (*at + 6u > length) {
                return false;
            }
            for (uint64_t i = 2u; i < 6u; i++) {
                if (!pr_is_hex(text[*at + i])) {
                    return false;
                }
            }
            *at += 6u;
        } else if (strchr("\"\\/bfnrt", kind) != nullptr && kind != '\0') {
            *at += 2u;
        } else {
            return false;
        }
    }
    return false;
}

static bool pr_json_number(const char *text, uint64_t length, uint64_t *at) {
    uint64_t i = *at;
    if (i < length && text[i] == '-') {
        i++;
    }
    if (i >= length || !pr_is_digit(text[i])) {
        return false;
    }
    /* A leading zero only as the whole of the whole part. */
    if (text[i] == '0' && i + 1u < length && pr_is_digit(text[i + 1u])) {
        return false;
    }
    while (i < length && pr_is_digit(text[i])) {
        i++;
    }
    if (i < length && text[i] == '.') {
        i++;
        if (i >= length || !pr_is_digit(text[i])) {
            return false;
        }
        while (i < length && pr_is_digit(text[i])) {
            i++;
        }
    }
    *at = i;
    return true;
}

/* All of the grammar but exponents, and true, false and null: nothing here writes those. Depth by a stack, not by calling itself. */
static bool pr_json_valid(const char *text, uint64_t length) {
    char stack[16];
    uint32_t depth = 0u;
    uint64_t at = 0u;
    bool want_value = true;
    for (;;) {
        pr_skip_space(text, length, &at);
        if (want_value) {
            if (at >= length) {
                return false;
            }
            const char c = text[at];
            if (c == '{' || c == '[') {
                if (depth == sizeof(stack)) {
                    return false;
                }
                stack[depth++] = c;
                at++;
                pr_skip_space(text, length, &at);
                if (at < length && text[at] == (c == '{' ? '}' : ']')) {
                    at++;
                    depth--;
                    want_value = false;
                } else if (c == '{') {
                    if (!pr_json_string(text, length, &at)) {
                        return false;
                    }
                    pr_skip_space(text, length, &at);
                    if (at >= length || text[at] != ':') {
                        return false;
                    }
                    at++;
                }
                continue;
            }
            if (c == '"' ? !pr_json_string(text, length, &at) : !pr_json_number(text, length, &at)) {
                return false;
            }
            want_value = false;
            continue;
        }
        if (depth == 0u) {
            return at == length;
        }
        if (at >= length) {
            return false;
        }
        const char c = text[at];
        const char open = stack[depth - 1u];
        if (c == (open == '{' ? '}' : ']')) {
            at++;
            depth--;
        } else if (c == ',') {
            at++;
            if (open == '{') {
                pr_skip_space(text, length, &at);
                if (!pr_json_string(text, length, &at)) {
                    return false;
                }
                pr_skip_space(text, length, &at);
                if (at >= length || text[at] != ':') {
                    return false;
                }
                at++;
            }
            want_value = true;
        } else {
            return false;
        }
    }
}

typedef struct pr_event_t {
    int64_t start_ns;         /* 0 */
    int64_t duration_ns;      /* 8 */
    uint32_t tid;             /* 16 */
    char name[PR_NAME_BYTES]; /* 20 */
    uint8_t _pad0[4];         /* 100 */
} pr_event_t;
static_assert(sizeof(pr_event_t) == 104);

typedef struct pr_lane_facts_t {
    uint64_t written;              /* 0 */
    uint64_t lost;                 /* 8 */
    uint64_t unmatched_ends;       /* 16 */
    uint64_t too_deep;             /* 24 */
    uint32_t still_open;           /* 32 */
    uint8_t seen;                  /* 36 */
    char name[PR_NAME_BYTES - 5u]; /* 37 */
    uint8_t _pad0[8];              /* 112 */
} pr_lane_facts_t;
static_assert(sizeof(pr_lane_facts_t) == 120);

typedef struct pr_trace_t {
    pr_lane_facts_t lanes[PR_TIDS_MAX]; /* 0 */
    pr_event_t *events;                 /* 3960 */
    char *text;                         /* 3968 */
    uint64_t event_count;               /* 3976 */
    uint64_t length;                    /* 3984 */
} pr_trace_t;
static_assert(sizeof(pr_trace_t) == 3992);

/* The trace is one event to a line, past the check that it is JSON at all. False if a line is neither of the forms it should be. */
static bool pr_trace_parse(test_context_t *t, pr_trace_t *out) {
    uint64_t line_start = 0u;
    while (line_start < out->length) {
        const char *const line = out->text + line_start;
        const char *const end = strchr(line, '\n');
        if (end == nullptr) {
            return false;
        }
        uint32_t tid = 0u;
        int64_t ts_whole = 0, dur_whole = 0;
        int32_t ts_rest = 0, dur_rest = 0;
        char name[PR_NAME_BYTES] = {0};
        pr_lane_facts_t facts;
        memset(&facts, 0, sizeof(facts));
        if (strncmp(line, "{\"ph\":\"X\"", 9u) == 0) {
            const int32_t got =
                sscanf(line,
                       "{\"ph\":\"X\",\"pid\":1,\"tid\":%" SCNu32 ",\"name\":\"%79[^\"]\",\"ts\":%" SCNd64 ".%" SCNd32
                       ",\"dur\":%" SCNd64 ".%" SCNd32 "}",
                       &tid, name, &ts_whole, &ts_rest, &dur_whole, &dur_rest);
            if (got != 6 || tid >= PR_TIDS_MAX || out->event_count == PR_EVENTS_MAX) {
                return false;
            }
            pr_event_t *const event = &out->events[out->event_count++];
            event->tid = tid;
            memcpy(event->name, name, sizeof(name));
            event->start_ns = ts_whole * 1000 + ts_rest;
            event->duration_ns = dur_whole * 1000 + dur_rest;
        } else if (sscanf(line,
                          "{\"ph\":\"M\",\"pid\":1,\"tid\":%" SCNu32
                          ",\"name\":\"thread_name\",\"args\":{\"name\":\"%74[^\"]\"",
                          &tid, facts.name) == 2 &&
                   tid < PR_TIDS_MAX) {
            memcpy(out->lanes[tid].name, facts.name, sizeof(facts.name));
        } else if (sscanf(line,
                          "{\"ph\":\"M\",\"pid\":1,\"tid\":%" SCNu32
                          ",\"name\":\"mc_lane\",\"args\":{\"written\":%" SCNu64 ",\"lost\":%" SCNu64
                          ",\"unmatched_ends\":%" SCNu64 ",\"too_deep\":%" SCNu64 ",\"still_open\":%" SCNu32 "}}",
                          &tid, &facts.written, &facts.lost, &facts.unmatched_ends, &facts.too_deep,
                          &facts.still_open) == 6 &&
                   tid < PR_TIDS_MAX) {
            memcpy(facts.name, out->lanes[tid].name, sizeof(facts.name));
            facts.seen = 1u;
            out->lanes[tid] = facts;
        }
        line_start = (uint64_t)(end - out->text) + 1u;
    }
    (void)t;
    return true;
}

/* Reads a trace back, or fails the test. */
static bool pr_trace_read(test_context_t *t, const char *path, pr_trace_t *out) {
    memset(out, 0, sizeof(*out));
    out->text = test_alloc(t, PR_TRACE_BYTES + 1u);
    out->events = test_alloc(t, PR_EVENTS_MAX * sizeof(pr_event_t));
    if (!CHECK_EQ_U64(file_read_whole(path, out->text, PR_TRACE_BYTES, &out->length), FILE_OK)) {
        return false;
    }
    out->text[out->length] = '\0';
    return CHECK(pr_json_valid(out->text, out->length)) && CHECK(pr_trace_parse(t, out));
}

static uint32_t pr_tid_named(const pr_trace_t *trace, const char *name) {
    for (uint32_t tid = 1u; tid < PR_TIDS_MAX; tid++) {
        if (strcmp(trace->lanes[tid].name, name) == 0) {
            return tid;
        }
    }
    return 0u;
}

static uint64_t pr_count(const pr_trace_t *trace, uint32_t tid, const char *name) {
    uint64_t count = 0u;
    for (uint64_t i = 0u; i < trace->event_count; i++) {
        const pr_event_t *const event = &trace->events[i];
        count += ((tid == 0u || event->tid == tid) && strcmp(event->name, name) == 0) ? 1u : 0u;
    }
    return count;
}

[[maybe_unused]] static float64_t pr_mean_ms(const pr_trace_t *trace, const char *name) {
    int64_t total = 0;
    uint64_t count = 0u;
    for (uint64_t i = 0u; i < trace->event_count; i++) {
        if (strcmp(trace->events[i].name, name) == 0) {
            total += trace->events[i].duration_ns;
            count++;
        }
    }
    return count == 0u ? 0.0f64 : time_ns_to_ms(total) / (float64_t)count;
}

static void pr_check_lanes_clean(test_context_t *t, const pr_trace_t *trace) {
    for (uint32_t tid = 1u; tid < PR_TIDS_MAX; tid++) {
        const pr_lane_facts_t *const lane = &trace->lanes[tid];
        if (lane->seen != 0u) {
            CHECK_EQ_U64(lane->lost, 0u);
            CHECK_EQ_U64(lane->unmatched_ends, 0u);
            CHECK_EQ_U64(lane->too_deep, 0u);
            CHECK_EQ_U64(lane->still_open, 0u);
        }
    }
}

static const profiler_summary_row_t *pr_row(const profiler_summary_row_t *rows, uint32_t count, const char *name) {
    for (uint32_t i = 0u; i < count; i++) {
        if (strcmp(rows[i].name, name) == 0) {
            return &rows[i];
        }
    }
    return nullptr;
}

/* ---- By hand ---------------------------------------------------------------------------------------------------------- */

TEST(self_time_is_worked_out_by_hand) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    REQUIRE(lane != nullptr);
    /* a holds b and d, and b holds c. */
    profiler_begin_at(lane, "a", 1000);
    profiler_begin_at(lane, "b", 1100);
    profiler_begin_at(lane, "c", 1150);
    profiler_end_at(lane, 1250);
    profiler_end_at(lane, 1400);
    profiler_begin_at(lane, "d", 1500);
    profiler_end_at(lane, 1600);
    profiler_end_at(lane, 2000);
    CHECK_EQ_U64(lane->depth, 0u);
    CHECK_EQ_U64(lane->written, 4u);
    /* Ends write, so the innermost comes first. */
    CHECK_STR_EQ(lane->borrowed_records[0].name, "c");
    CHECK_STR_EQ(lane->borrowed_records[3].name, "a");

    profiler_summary_row_t rows[8];
    uint64_t unlisted = 99u;
    const uint32_t count = profiler_summary_read(&profiler, rows, 8u, &unlisted);
    CHECK_EQ_U64(count, 4u);
    CHECK_EQ_U64(unlisted, 0u);
    /* Most self time first. */
    CHECK_STR_EQ(rows[0].name, "a");
    CHECK_EQ_I64(rows[0].total_ns, 1000);
    CHECK_EQ_I64(rows[0].self_ns, 1000 - 300 - 100);
    CHECK_STR_EQ(rows[1].name, "b");
    CHECK_EQ_I64(rows[1].total_ns, 300);
    CHECK_EQ_I64(rows[1].self_ns, 300 - 100);
    const profiler_summary_row_t *const c = pr_row(rows, count, "c");
    const profiler_summary_row_t *const d = pr_row(rows, count, "d");
    REQUIRE(c != nullptr && d != nullptr);
    CHECK_EQ_I64(c->self_ns, 100);
    CHECK_EQ_I64(d->self_ns, 100);
    CHECK_EQ_U64(d->lane_mask, 1u);
    profiler_deinit(&profiler);
}

TEST(a_row_adds_up_counts_and_the_longest) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const first = profiler_lane_claim(&profiler, "one");
    profiler_lane_t *const second = profiler_lane_claim(&profiler, "two");
    profiler_begin_at(first, "x", 0);
    profiler_end_at(first, 10);
    profiler_begin_at(first, "x", 100);
    profiler_end_at(first, 130);
    profiler_begin_at(second, "x", 0);
    profiler_end_at(second, 20);
    profiler_summary_row_t rows[2];
    REQUIRE(profiler_summary_read(&profiler, rows, 2u, nullptr) == 1u);
    CHECK_EQ_U64(rows[0].count, 3u);
    CHECK_EQ_I64(rows[0].total_ns, 60);
    CHECK_EQ_I64(rows[0].max_ns, 30);
    CHECK_EQ_U64(rows[0].lane_mask, 3u);
    profiler_deinit(&profiler);
}

TEST(a_time_going_backwards_comes_to_nothing_and_not_to_a_negative) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    profiler_begin_at(lane, "x", 500);
    profiler_end_at(lane, 400);
    CHECK_EQ_I64(lane->borrowed_records[0].duration_ns, 0);
    profiler_span_add(lane, "y", 500, -7);
    CHECK_EQ_I64(lane->borrowed_records[1].duration_ns, 0);
    profiler_deinit(&profiler);
}

TEST(a_span_is_not_charged_to_what_is_open) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    profiler_begin_at(lane, "open", 0);
    profiler_span_add(lane, "span", 10, 50);
    profiler_end_at(lane, 100);
    CHECK_EQ_I64(lane->borrowed_records[0].start_ns, 10);
    CHECK_EQ_I64(lane->borrowed_records[0].duration_ns, 50);
    CHECK_EQ_I64(lane->borrowed_records[0].self_ns, 50);
    CHECK_EQ_I64(lane->borrowed_records[1].self_ns, 100);
    profiler_deinit(&profiler);
}

TEST(the_ring_keeps_the_newest_and_counts_what_went) {
    const profiler_limits_t limits = {.records_per_lane = 16u, .lane_count = 1u, ._pad0 = {0}};
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, limits));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    for (int64_t i = 0; i < 40; i++) {
        profiler_begin_at(lane, "x", i * 1000);
        profiler_end_at(lane, i * 1000 + i); /* so each is told apart by its length */
    }
    CHECK_EQ_U64(lane->written, 40u);
    profiler_summary_row_t rows[1];
    REQUIRE(profiler_summary_read(&profiler, rows, 1u, nullptr) == 1u);
    CHECK_EQ_U64(rows[0].count, 16u);
    /* 24 to 39. */
    CHECK_EQ_I64(rows[0].total_ns, (24 + 39) * 16 / 2);
    CHECK_EQ_I64(rows[0].max_ns, 39);
    profiler_deinit(&profiler);
}

TEST(names_past_the_rows_are_counted_and_one_name_in_two_places_is_one_row) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    /* Not literals, so that the two cannot be merged into one by the compiler. */
    static const char same_one[] = "same";
    static const char same_two[] = "same";
    CHECK((const void *)same_one != (const void *)same_two);
    profiler_span_add(lane, same_one, 0, 1);
    profiler_span_add(lane, same_two, 0, 1);
    profiler_span_add(lane, "other", 0, 5);
    profiler_span_add(lane, "other", 0, 5);
    profiler_span_add(lane, "third", 0, 9);
    profiler_summary_row_t rows[2];
    uint64_t unlisted = 0u;
    const uint32_t count = profiler_summary_read(&profiler, rows, 2u, &unlisted);
    CHECK_EQ_U64(count, 2u);
    CHECK_EQ_U64(unlisted, 1u);
    const profiler_summary_row_t *const same = pr_row(rows, count, "same");
    REQUIRE(same != nullptr);
    CHECK_EQ_U64(same->count, 2u);
    profiler_deinit(&profiler);
}

TEST(scopes_that_do_not_balance_are_counted_and_harm_nothing) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    for (uint32_t i = 0u; i < 5u; i++) {
        profiler_end_at(lane, 1);
    }
    CHECK_EQ_U64(lane->unmatched_ends, 5u);
    CHECK_EQ_U64(lane->written, 0u);
    /* Past the deepest it times: those are counted, and what they took is what their parent took. */
    const uint32_t deep = PROFILER_DEPTH_MAX + 8u;
    for (uint32_t i = 0u; i < deep; i++) {
        profiler_begin_at(lane, "deep", (int64_t)i * 10);
    }
    CHECK_EQ_U64(lane->too_deep, 8u);
    CHECK_EQ_U64(lane->depth, deep);
    for (uint32_t i = 0u; i < deep; i++) {
        profiler_end_at(lane, 10000 + (int64_t)i);
    }
    CHECK_EQ_U64(lane->depth, 0u);
    CHECK_EQ_U64(lane->written, PROFILER_DEPTH_MAX);
    /* The first end that writes is the deepest timed: it began at 310, and ended at the ninth end. */
    CHECK_EQ_I64(lane->borrowed_records[0].start_ns, (int64_t)(PROFILER_DEPTH_MAX - 1u) * 10);
    CHECK_EQ_I64(lane->borrowed_records[0].duration_ns, 10000 + 8 - (int64_t)(PROFILER_DEPTH_MAX - 1u) * 10);
    CHECK_EQ_U64(lane->unmatched_ends, 5u);
    /* And the stack it kept is good for what comes after. */
    profiler_begin_at(lane, "after", 20000);
    profiler_end_at(lane, 20007);
    CHECK_EQ_I64(lane->borrowed_records[PROFILER_DEPTH_MAX].self_ns, 7);
    profiler_deinit(&profiler);
}

TEST(no_lane_and_no_profiler_do_nothing_at_all) {
    profiler_begin(nullptr, "x");
    profiler_end(nullptr);
    profiler_begin_at(nullptr, "x", 0);
    profiler_end_at(nullptr, 0);
    profiler_span_add(nullptr, "x", 0, 1);
    CHECK(profiler_lane_claim(nullptr, "x") == nullptr);
    CHECK(profiler_lanes_claim(nullptr, "x", 2u) == nullptr);
    profiler_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    CHECK(profiler_lane_claim(&zeroed, "x") == nullptr);
    profiler_deinit(&zeroed);
}

TEST(limits_that_make_no_sense_are_refused_with_nothing_left_behind) {
    heap_counters_t before, after;
    heap_counters_read(&before);
    profiler_t profiler;
    const profiler_limits_t bad[] = {
        {.records_per_lane = 100u, .lane_count = 2u, ._pad0 = {0}},
        {.records_per_lane = 8u, .lane_count = 2u, ._pad0 = {0}},
        {.records_per_lane = 0u, .lane_count = 2u, ._pad0 = {0}},
        {.records_per_lane = 64u, .lane_count = 0u, ._pad0 = {0}},
        {.records_per_lane = 64u, .lane_count = PROFILER_LANES_MAX + 1u, ._pad0 = {0}},
    };
    for (uint32_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!profiler_init(&profiler, bad[i]));
        profiler_deinit(&profiler);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, before.live_bytes);
}

TEST(lanes_run_out_and_are_named_and_a_row_of_them_comes_whole_or_not_at_all) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const row = profiler_lanes_claim(&profiler, "worker", 3u);
    REQUIRE(row != nullptr);
    CHECK_STR_EQ(row[0].name, "worker 0");
    CHECK_STR_EQ(row[2].name, "worker 2");
    CHECK(profiler_lanes_claim(&profiler, "more", 2u) == nullptr);
    CHECK(profiler_lanes_claim(&profiler, "none", 0u) == nullptr);
    profiler_lane_t *const last = profiler_lane_claim(&profiler, "a name far too long to keep");
    REQUIRE(last != nullptr);
    CHECK_STR_EQ(last->name, "a name far too ");
    CHECK(last == row + 3);
    CHECK(profiler_lane_claim(&profiler, "gone") == nullptr);
    /* Each has its own memory and its own line. */
    CHECK_EQ_U64((uint64_t)((const uint8_t *)row[1].borrowed_records - (const uint8_t *)row[0].borrowed_records),
                 64u * sizeof(profiler_record_t));
    CHECK_EQ_U64((uintptr_t)&row[1] % MC_CPU_CACHE_LINE_BYTES, 0u);
    profiler_deinit(&profiler);
}

/* ---- The trace -------------------------------------------------------------------------------------------------------- */

static void pr_path(test_context_t *t, char *out, uint64_t bytes, const char *name) {
    REQUIRE(t->arg_count >= 2);
    snprintf(out, bytes, "%s/%s", t->borrowed_args[1], name);
}

TEST(a_trace_reads_back_exactly) {
    char path[512];
    pr_path(t, path, sizeof(path), "exact.json");
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const main_lane = profiler_lane_claim(&profiler, "main");
    profiler_lane_t *const card = profiler_lane_claim(&profiler, "card");
    const int64_t zero = profiler.started_ns;
    profiler_begin_at(main_lane, "outer", zero + 1234567);
    profiler_begin_at(main_lane, "inner", zero + 1234999);
    profiler_end_at(main_lane, zero + 2000000);
    profiler_end_at(main_lane, zero + 5000001);
    profiler_span_add(card, "kernel", zero + 7, 1000);
    /* Before the profiler started: shown at its start. */
    profiler_span_add(card, "early", zero - 50, 1);
    REQUIRE(profiler_trace_dump(&profiler, path) == FILE_OK);

    pr_trace_t trace;
    REQUIRE(pr_trace_read(t, path, &trace));
    CHECK_EQ_U64(trace.event_count, 4u);
    pr_check_lanes_clean(t, &trace);
    const uint32_t main_tid = pr_tid_named(&trace, "main");
    const uint32_t card_tid = pr_tid_named(&trace, "card");
    REQUIRE(main_tid != 0u && card_tid != 0u && main_tid != card_tid);
    CHECK_EQ_U64(trace.lanes[main_tid].written, 2u);
    CHECK_EQ_U64(pr_count(&trace, main_tid, "outer"), 1u);
    CHECK_EQ_U64(pr_count(&trace, card_tid, "kernel"), 1u);
    CHECK_EQ_U64(pr_count(&trace, main_tid, "kernel"), 0u);
    CHECK_CONTAINS(trace.text, "\"name\":\"inner\",\"ts\":1234.999,\"dur\":765.001}");
    CHECK_CONTAINS(trace.text, "\"name\":\"outer\",\"ts\":1234.567,\"dur\":3765.434}");
    CHECK_CONTAINS(trace.text, "\"name\":\"kernel\",\"ts\":0.007,\"dur\":1.000}");
    CHECK_CONTAINS(trace.text, "\"name\":\"early\",\"ts\":0.000,\"dur\":0.001}");
    /* What about:tracing needs to read it. */
    CHECK_CONTAINS(trace.text, "\"traceEvents\":[");
    profiler_deinit(&profiler);
}

TEST(a_name_stays_valid_json_whatever_it_holds_and_is_cut_to_fit) {
    char path[512];
    pr_path(t, path, sizeof(path), "escaped.json");
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "la\"ne");
    static const char odd[] = "a\"b\\c\nd\t\x7f\xc3\xa9";
    char whole[300];
    memset(whole, '"', sizeof(whole) - 1u);
    whole[sizeof(whole) - 1u] = '\0';
    profiler_span_add(lane, odd, 0, 1);
    profiler_span_add(lane, whole, 0, 1);
    REQUIRE(profiler_trace_dump(&profiler, path) == FILE_OK);
    char *const text = test_alloc(t, 1u << 20u);
    uint64_t length = 0u;
    REQUIRE(file_read_whole(path, text, (1u << 20u) - 1u, &length) == FILE_OK);
    text[length] = '\0';
    CHECK(pr_json_valid(text, length));
    CHECK_CONTAINS(text, "a\\\"b\\\\c\\u000ad\\u0009\\u007f\\u00c3\\u00a9");
    CHECK_CONTAINS(text, "\"args\":{\"name\":\"la\\\"ne\"}");
    /* Cut at 64 bytes, each escaped into two. */
    char cut[129 + 8];
    memset(cut, 0, sizeof(cut));
    memcpy(cut, "\"name\":\"", 8u);
    for (uint32_t i = 0u; i < 64u; i++) {
        memcpy(cut + 8u + i * 2u, "\\\"", 2u);
    }
    CHECK_CONTAINS(text, cut);
    profiler_deinit(&profiler);
}

TEST(a_trace_longer_than_one_buffer_is_all_there) {
    char path[512];
    pr_path(t, path, sizeof(path), "long.json");
    const profiler_limits_t limits = {.records_per_lane = 1u << 16u, .lane_count = 2u, ._pad0 = {0}};
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, limits));
    profiler_lane_t *const lanes = profiler_lanes_claim(&profiler, "worker", 2u);
    REQUIRE(lanes != nullptr);
    const int64_t zero = profiler.started_ns;
    for (int64_t i = 0; i < (1 << 16) + 10; i++) {
        profiler_span_add(&lanes[0], "first", zero + i * 2000, 1000 + i);
        if (i < 1000) {
            profiler_span_add(&lanes[1], "second", zero + i, 1);
        }
    }
    REQUIRE(profiler_trace_dump(&profiler, path) == FILE_OK);
    pr_trace_t trace;
    REQUIRE(pr_trace_read(t, path, &trace));
    CHECK(trace.length > 1024u * 1024u);
    CHECK_EQ_U64(trace.event_count, (1u << 16u) + 1000u);
    const uint32_t first_tid = pr_tid_named(&trace, "worker 0");
    REQUIRE(first_tid != 0u);
    CHECK_EQ_U64(pr_count(&trace, first_tid, "first"), 1u << 16u);
    CHECK_EQ_U64(trace.lanes[first_tid].written, (1u << 16u) + 10u);
    CHECK_EQ_U64(trace.lanes[first_tid].lost, 10u);
    /* Oldest first, starting past what was lost, and the last is the very last written. */
    CHECK_EQ_I64(trace.events[0].start_ns, 10 * 2000);
    CHECK_EQ_I64(trace.events[(1u << 16u) - 1u].duration_ns, 1000 + (1 << 16) + 9);
    profiler_deinit(&profiler);
}

TEST(a_full_disk_and_a_missing_directory_are_said_and_not_hidden) {
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    profiler_span_add(lane, "x", 0, 1);
    CHECK_EQ_U64(profiler_trace_dump(&profiler, "/dev/full"), FILE_ERROR_DISK_FULL);
    CHECK_EQ_U64(profiler_trace_dump(&profiler, "/no/such/directory/trace.json"), FILE_ERROR_NOT_FOUND);
    file_t full;
    REQUIRE(file_open(&full, "/dev/full", FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(profiler_summary_write(&profiler, &full), FILE_ERROR_DISK_FULL);
    (void)file_close(&full);
    profiler_deinit(&profiler);
}

TEST(the_table_says_what_a_person_needs) {
    char path[512];
    pr_path(t, path, sizeof(path), "table.txt");
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, pr_small));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    profiler_begin_at(lane, "slow", 0);
    profiler_begin_at(lane, "inner", 1000000);
    profiler_end_at(lane, 3000000);
    profiler_end_at(lane, 10000000);
    profiler_begin_at(lane, "left_open", 11000000);
    profiler_end_at(lane, 12000000);
    profiler_begin_at(lane, "never_ended", 13000000);
    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(profiler_summary_write(&profiler, &file), FILE_OK);
    REQUIRE(file_close(&file) == FILE_OK);
    char *const text = test_alloc(t, 1u << 16u);
    uint64_t length = 0u;
    REQUIRE(file_read_whole(path, text, (1u << 16u) - 1u, &length) == FILE_OK);
    text[length] = '\0';
    printf("%s", text);
    CHECK_CONTAINS(text, "scope");
    CHECK_CONTAINS(text, "self ms");
    /* name, count, total, self, mean, max, lanes. */
    CHECK_CONTAINS(text, "slow                              1       10.000        8.000    10.0000    10.0000     1");
    CHECK_CONTAINS(text, "inner                             1        2.000        2.000     2.0000     2.0000     1");
    /* Three written, one still open. */
    CHECK_CONTAINS(text, "main                        3            0          0          0      1");
    /* Slow is first, by its own time. */
    REQUIRE(strstr(text, "slow ") != nullptr && strstr(text, "inner ") != nullptr);
    CHECK(strstr(text, "slow ") < strstr(text, "inner "));
    profiler_deinit(&profiler);
}

/* ---- Real threads ----------------------------------------------------------------------------------------------------- */

typedef struct pr_thread_args_t {
    profiler_lane_t *lane;
    uint64_t index;
} pr_thread_args_t;
static_assert(sizeof(pr_thread_args_t) == 16);

static void *pr_thread(void *argument) {
    const pr_thread_args_t *const args = argument;
    for (uint32_t i = 0u; i < PR_THREAD_ROUNDS; i++) {
        profiler_begin(args->lane, "outer");
        profiler_begin(args->lane, "inner");
        time_sleep_ns(20000);
        profiler_end(args->lane);
        profiler_end(args->lane);
    }
    return nullptr;
}

TEST(threads_each_on_a_lane_do_not_touch_each_other_and_take_no_heap) {
    profiler_t profiler;
    const profiler_limits_t limits = {.records_per_lane = 1024u, .lane_count = PR_THREADS, ._pad0 = {0}};
    REQUIRE(profiler_init(&profiler, limits));
    pthread_t threads[PR_THREADS];
    pr_thread_args_t args[PR_THREADS];
    heap_counters_t before, after;
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < PR_THREADS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "t%" PRIu32, i);
        args[i] = (pr_thread_args_t){.lane = profiler_lane_claim(&profiler, name), .index = i};
        REQUIRE(args[i].lane != nullptr);
        REQUIRE(pthread_create(&threads[i], nullptr, pr_thread, &args[i]) == 0);
    }
    for (uint32_t i = 0u; i < PR_THREADS; i++) {
        REQUIRE(pthread_join(threads[i], nullptr) == 0);
    }
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    for (uint32_t i = 0u; i < PR_THREADS; i++) {
        CHECK_EQ_U64(args[i].lane->written, 2u * PR_THREAD_ROUNDS);
        CHECK_EQ_U64(args[i].lane->depth, 0u);
        /* In order on its own lane: inner, then the outer that held it, which began first and ended after. */
        for (uint32_t r = 0u; r < 2u * PR_THREAD_ROUNDS; r += 2u) {
            const profiler_record_t *const inner = &args[i].lane->borrowed_records[r];
            const profiler_record_t *const outer = &args[i].lane->borrowed_records[r + 1u];
            CHECK_STR_EQ(inner->name, "inner");
            CHECK_STR_EQ(outer->name, "outer");
            CHECK(outer->start_ns <= inner->start_ns);
            CHECK(outer->start_ns + outer->duration_ns >= inner->start_ns + inner->duration_ns);
            /* A sleep's promise is on the short side. */
            CHECK(inner->duration_ns >= 20000);
            CHECK_EQ_I64(outer->self_ns, outer->duration_ns - inner->duration_ns);
        }
    }
    profiler_summary_row_t rows[4];
    const uint32_t count = profiler_summary_read(&profiler, rows, 4u, nullptr);
    CHECK_EQ_U64(count, 2u);
    const profiler_summary_row_t *const outer = pr_row(rows, count, "outer");
    REQUIRE(outer != nullptr);
    CHECK_EQ_U64(outer->count, PR_THREADS * PR_THREAD_ROUNDS);
    CHECK_EQ_U64(outer->lane_mask, (1u << PR_THREADS) - 1u);
    profiler_deinit(&profiler);
}

TEST(a_scope_costs_little_and_no_lane_costs_less) {
    profiler_t profiler;
    const profiler_limits_t limits = {.records_per_lane = 1024u, .lane_count = 1u, ._pad0 = {0}};
    REQUIRE(profiler_init(&profiler, limits));
    profiler_lane_t *const lane = profiler_lane_claim(&profiler, "main");
    enum : uint32_t { ROUNDS = 200000u };
    int64_t started_ns = time_now_ns();
    for (uint32_t i = 0u; i < ROUNDS; i++) {
        profiler_begin(lane, "x");
        profiler_end(lane);
    }
    const int64_t on_ns = (time_now_ns() - started_ns) / ROUNDS;
    started_ns = time_now_ns();
    for (uint32_t i = 0u; i < ROUNDS; i++) {
        profiler_begin(nullptr, "x");
        profiler_end(nullptr);
    }
    const int64_t off_ns = (time_now_ns() - started_ns) / ROUNDS;
    printf("  a scope costs %" PRId64 " ns on a lane, and %" PRId64 " ns on none\n", on_ns, off_ns);
    /* Far over what it costs, in an unoptimised build on a machine that may be building: this is to catch a syscall or a lock. */
    CHECK(on_ns < 5000);
    CHECK(off_ns < 500);
    CHECK_EQ_U64(lane->written, ROUNDS);
    profiler_deinit(&profiler);
}

/* ---- Wired in ---------------------------------------------------------------------------------------------------------- */

#if PROFILER_BUILT

static void pr_job(job_worker_t *worker, void *borrowed_argument) {
    (void)borrowed_argument;
    PROFILER_BEGIN(job_worker_lane(worker), "pr_work");
    time_sleep_ns(1000);
    PROFILER_END(job_worker_lane(worker));
}

TEST(each_job_is_a_scope_on_the_lane_of_its_worker) {
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(thread_topology_read(topology));
    const uintptr_t raw = (uintptr_t)test_alloc(t, sizeof(job_system_t) + 64u);
    job_system_t *const system = (job_system_t *)((raw + 63u) & ~(uintptr_t)63u);
    REQUIRE(job_init(system, topology));
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, profiler_limits_default()));
    job_profiler_set(system, &profiler);
    REQUIRE(system->borrowed_lanes != nullptr);
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0u; i < PR_JOBS; i++) {
        job_submit(system, &group, pr_job, nullptr);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    profiler_summary_row_t rows[8];
    const uint32_t count = profiler_summary_read(&profiler, rows, 8u, nullptr);
    CHECK_EQ_U64(count, 2u);
    const profiler_summary_row_t *const job = pr_row(rows, count, "job");
    const profiler_summary_row_t *const work = pr_row(rows, count, "pr_work");
    REQUIRE(job != nullptr && work != nullptr);
    CHECK_EQ_U64(job->count, PR_JOBS);
    CHECK_EQ_U64(work->count, PR_JOBS);
    CHECK(job->total_ns >= work->total_ns);
    CHECK_EQ_I64(job->self_ns, job->total_ns - work->total_ns);
    CHECK_EQ_U64(job->lane_mask, work->lane_mask);
    job_deinit(system);
    profiler_deinit(&profiler);
}

typedef struct pr_run_t {
    test_process_t process; /* 0 */
    pr_trace_t trace;       /* 24 */
} pr_run_t;
static_assert(sizeof(pr_run_t) == 4016);

/* The program on a world, with these flags after the ones they all have. */
static bool pr_program(test_context_t *t, const char *name, const char *extra_one, const char *extra_two,
                       pr_run_t *out) {
    char path[512];
    pr_path(t, path, sizeof(path), name);
    char *argv[32];
    uint32_t argc = 0u;
    argv[argc++] = t->borrowed_args[0];
    argv[argc++] = (char *)"--headless";
    argv[argc++] = (char *)"--test-world";
    argv[argc++] = (char *)"--render-distance";
    argv[argc++] = (char *)"4";
    argv[argc++] = (char *)"--width";
    argv[argc++] = (char *)"160";
    argv[argc++] = (char *)"--height";
    argv[argc++] = (char *)"90";
    argv[argc++] = (char *)"--ticks";
    argv[argc++] = (char *)"20";
    argv[argc++] = (char *)"--profile";
    argv[argc++] = path;
    if (extra_one != nullptr) {
        argv[argc++] = (char *)extra_one;
    }
    if (extra_two != nullptr) {
        argv[argc++] = (char *)extra_two;
    }
    argv[argc] = nullptr;
    test_process_run(t, argv, PR_PROGRAM_TIMEOUT_MS, &out->process);
    if (!CHECK_EQ_I64(out->process.exit_code, MAIN_EXIT_OK)) {
        printf("%s\n", out->process.output);
        return false;
    }
    return pr_trace_read(t, path, &out->trace);
}

/* What render_frame_report logged: the mean of a pass, and the whole. */
static bool pr_logged_mean(const char *output, const char *pass, float64_t *out_ms) {
    char wanted[64];
    snprintf(wanted, sizeof(wanted), "frame: %-8s mean ", pass);
    const char *const at = strstr(output, wanted);
    if (at == nullptr) {
        return false;
    }
    char *end = nullptr;
    *out_ms = (float64_t)strtod(at + strlen(wanted), &end);
    return end != at + strlen(wanted);
}

static bool pr_logged_total(const char *output, uint64_t *out_frames, float64_t *out_mean_ms) {
    const char *at = output;
    while ((at = strstr(at, "frame: ")) != nullptr) {
        uint64_t frames = 0u;
        char *end = nullptr;
        frames = strtoull(at + 7, &end, 10);
        if (end != at + 7 && strncmp(end, " frames of", 10u) == 0) {
            const char *const mean = strstr(end, "total mean ");
            if (mean == nullptr) {
                return false;
            }
            *out_frames = frames;
            *out_mean_ms = (float64_t)strtod(mean + 11, nullptr);
            return true;
        }
        at += 7;
    }
    return false;
}

TEST(the_program_times_what_it_does_and_the_kernels_are_what_the_frame_says) {
    pr_run_t *const run = test_alloc(t, sizeof(pr_run_t));
    REQUIRE(pr_program(t, "world.json", nullptr, nullptr, run));
    const pr_trace_t *const trace = &run->trace;
    pr_check_lanes_clean(t, trace);
    CHECK_CONTAINS(run->process.output, "profiler: scope");

    /* All on main with no threads, the card apart, and the workers making columns. */
    const uint32_t main_tid = pr_tid_named(trace, "main");
    const uint32_t card_tid = pr_tid_named(trace, "card");
    const uint32_t sim_tid = pr_tid_named(trace, "sim");
    const uint32_t gpu_tid = pr_tid_named(trace, "gpu");
    REQUIRE(main_tid != 0u && card_tid != 0u && sim_tid != 0u && gpu_tid != 0u);
    CHECK_EQ_U64(trace->lanes[sim_tid].written, 0u);
    CHECK_EQ_U64(trace->lanes[gpu_tid].written, 0u);
    CHECK_EQ_U64(pr_count(trace, main_tid, "scene_tick"), 20u);
    CHECK(pr_count(trace, main_tid, "sim_tick") == 20u);
    CHECK(pr_count(trace, main_tid, "sim_step") > 0u);
    CHECK(pr_count(trace, 0u, "column") > 0u);
    CHECK(pr_count(trace, 0u, "job") >= pr_count(trace, 0u, "column"));
    CHECK_EQ_U64(pr_count(trace, main_tid, "column"), 0u);

    uint64_t frames = 0u;
    float64_t total_ms = 0.0f64;
    REQUIRE(pr_logged_total(run->process.output, &frames, &total_ms));
    REQUIRE(frames > 0u);
    printf("  %" PRIu64 " frames, %" PRIu64 " events, total mean %.3f ms\n", frames, trace->event_count,
           (libc_double_t)total_ms);
    CHECK_EQ_U64(pr_count(trace, main_tid, "frame_draw"), frames);
    /* What always runs runs once a frame; a kernel on a run with its feature off is not there. */
    CHECK_EQ_U64(pr_count(trace, card_tid, "coarse"), frames);
    CHECK_EQ_U64(pr_count(trace, card_tid, "primary"), frames);
    CHECK_EQ_U64(pr_count(trace, card_tid, "shade"), frames);
    CHECK_EQ_U64(pr_count(trace, card_tid, "post"), frames);
    CHECK_EQ_U64(pr_count(trace, card_tid, "readback"), frames);
    CHECK_EQ_U64(pr_count(trace, card_tid, "god_rays"), 0u);

    /* Passes of one kernel, on the same clock as the log, to its rounding. */
    float64_t coarse_ms = 0.0f64, readback_ms = 0.0f64;
    REQUIRE(pr_logged_mean(run->process.output, "coarse", &coarse_ms));
    REQUIRE(pr_logged_mean(run->process.output, "readback", &readback_ms));
    CHECK_NEAR(pr_mean_ms(trace, "coarse"), coarse_ms, 0.002f64);
    CHECK_NEAR(pr_mean_ms(trace, "readback"), readback_ms, 0.002f64);

    /* And a frame's kernels are the frame, end to end, less the gaps for what did not run, and inside what the host waited. */
    int64_t kernels_total_ns = 0;
    uint64_t draw = 0u;
    const pr_event_t *previous = nullptr;
    for (uint64_t i = 0u; i < trace->event_count; i++) {
        const pr_event_t *const event = &trace->events[i];
        if (event->tid != card_tid) {
            continue;
        }
        kernels_total_ns += event->duration_ns;
        if (strcmp(event->name, "coarse") != 0 && previous != nullptr) {
            CHECK_EQ_I64(event->start_ns, previous->start_ns + previous->duration_ns);
        }
        previous = event;
        draw += strcmp(event->name, "readback") == 0 ? 1u : 0u;
    }
    CHECK_EQ_U64(draw, frames);
    CHECK_NEAR(time_ns_to_ms(kernels_total_ns) / (float64_t)frames, total_ms, 0.05f64 + total_ms * 0.02f64);

    /* The scopes nest as the code does: each frame_draw inside a scene_draw. */
    uint64_t inside = 0u;
    for (uint64_t i = 0u; i < trace->event_count; i++) {
        const pr_event_t *const event = &trace->events[i];
        if (event->tid != main_tid || strcmp(event->name, "frame_draw") != 0) {
            continue;
        }
        for (uint64_t j = i + 1u; j < trace->event_count; j++) {
            const pr_event_t *const outer = &trace->events[j];
            if (outer->tid == main_tid && strcmp(outer->name, "scene_draw") == 0) {
                inside += outer->start_ns <= event->start_ns &&
                                  outer->start_ns + outer->duration_ns >= event->start_ns + event->duration_ns
                              ? 1u
                              : 0u;
                break;
            }
        }
    }
    CHECK_EQ_U64(inside, frames);
}

TEST(a_kernel_is_in_the_trace_when_its_flag_says_so) {
    pr_run_t *const run = test_alloc(t, sizeof(pr_run_t));
    REQUIRE(pr_program(t, "rays.json", "--god-rays", nullptr, run));
    const uint32_t card = pr_tid_named(&run->trace, "card");
    REQUIRE(card != 0u);
    const uint64_t frames = pr_count(&run->trace, card, "coarse");
    CHECK(frames > 0u);
    CHECK_EQ_U64(pr_count(&run->trace, card, "god_rays"), frames);

    /* Level 0 ending short of how far it can see: the far field, then, has rays of its own. Level 0 is never under 12 chunks (plan 009 phase
     * 04), so the distance goes past it. Building one takes the thread sanitizer about 180 s, past what a run here is given, and it finds
     * nothing to say about it: see the stage file. */
#ifndef __SANITIZE_THREAD__
    REQUIRE(pr_program(t, "far.json", "--render-distance", "14", run));
    const uint32_t far_card = pr_tid_named(&run->trace, "card");
    REQUIRE(far_card != 0u);
    CHECK(pr_count(&run->trace, far_card, "coarse") > 0u);
    CHECK_EQ_U64(pr_count(&run->trace, far_card, "lod"), pr_count(&run->trace, far_card, "coarse"));
#endif

    REQUIRE(pr_program(t, "plain.json", "--no-entities", "--no-lod", run));
    const uint32_t plain_card = pr_tid_named(&run->trace, "card");
    REQUIRE(plain_card != 0u);
    CHECK(pr_count(&run->trace, plain_card, "coarse") > 0u);
    CHECK_EQ_U64(pr_count(&run->trace, plain_card, "god_rays"), 0u);
    CHECK_EQ_U64(pr_count(&run->trace, plain_card, "lod"), 0u);
    CHECK_EQ_U64(pr_count(&run->trace, plain_card, "entity_rays"), 0u);
    CHECK_EQ_U64(pr_count(&run->trace, plain_card, "entity_overlay"), 0u);
}

TEST(walking_times_the_tick_in_its_parts) {
    pr_run_t *const run = test_alloc(t, sizeof(pr_run_t));
    REQUIRE(pr_program(t, "walk.json", "--walk", nullptr, run));
    pr_check_lanes_clean(t, &run->trace);
    const uint32_t main_tid = pr_tid_named(&run->trace, "main");
    CHECK_EQ_U64(pr_count(&run->trace, main_tid, "sim_tick"), 20u);
    CHECK_EQ_U64(pr_count(&run->trace, main_tid, "tick_hands"), 20u);
    CHECK_EQ_U64(pr_count(&run->trace, main_tid, "tick_mobs"), 20u);
    CHECK_EQ_U64(pr_count(&run->trace, main_tid, "tick_player"), 20u);
    CHECK_EQ_U64(pr_count(&run->trace, main_tid, "tick_world"), 20u);
    CHECK(pr_count(&run->trace, main_tid, "hud_compose") > 0u);
    CHECK(pr_count(&run->trace, main_tid, "present") > 0u);
}

/* Looks down at the ground and mines it: an outline, then what was mined lying there in a box. Then, for frames with nothing
 * aimed at, /tp top and a look straight up: at the start itself nothing says a hill or a tree's crown is not in reach (on plan
 * 006 phase 04's land a jungle crown hangs over it), but over the top of its column there is only sky. */
static const char pr_mining_script[] = "1 move 0 1000\n"
                                       "20 button left down\n"
                                       "120 button left up\n"
                                       "130 key slash down\n"
                                       "130 key slash up\n"
                                       "131 key t down\n"
                                       "131 key t up\n"
                                       "132 key p down\n"
                                       "132 key p up\n"
                                       "133 key space down\n"
                                       "133 key space up\n"
                                       "134 key t down\n"
                                       "134 key t up\n"
                                       "135 key o down\n"
                                       "135 key o up\n"
                                       "136 key p down\n"
                                       "136 key p up\n"
                                       "137 key enter down\n"
                                       "137 key enter up\n"
                                       "150 move 0 -2000\n"
                                       "200 end\n";

TEST(what_is_aimed_at_and_what_lies_there_have_kernels_when_they_are_there) {
    char script[512], path[512];
    pr_path(t, script, sizeof(script), "mining.script");
    pr_path(t, path, sizeof(path), "mining.json");
    file_t file;
    REQUIRE(file_open(&file, script, FILE_MODE_WRITE) == FILE_OK);
    REQUIRE(file_write_all(&file, pr_mining_script, sizeof(pr_mining_script) - 1u) == FILE_OK);
    REQUIRE(file_close(&file) == FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--walk",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--width",
                          (char *)"160",
                          (char *)"--height",
                          (char *)"90",
                          (char *)"--script",
                          script,
                          (char *)"--profile",
                          path,
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, PR_PROGRAM_TIMEOUT_MS, &run);
    REQUIRE(CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK));
    pr_trace_t trace;
    REQUIRE(pr_trace_read(t, path, &trace));
    pr_check_lanes_clean(t, &trace);
    const uint32_t card = pr_tid_named(&trace, "card");
    const uint64_t frames = pr_count(&trace, card, "coarse");
    const uint64_t rays = pr_count(&trace, card, "entity_rays");
    const uint64_t overlay = pr_count(&trace, card, "entity_overlay");
    printf("  %" PRIu64 " frames: entity_rays in %" PRIu64 ", entity_overlay in %" PRIu64 "\n", frames, rays, overlay);
    CHECK(rays > 0u && rays < frames);
    CHECK(overlay > 0u && overlay < frames);
}

TEST(a_run_that_opens_no_world_still_times_its_workers) {
    char path[512];
    pr_path(t, path, sizeof(path), "workers.json");
    char *const argv[] = {t->borrowed_args[0], (char *)"--profile", path, nullptr};
    test_process_t run;
    test_process_run(t, argv, PR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    pr_trace_t trace;
    REQUIRE(pr_trace_read(t, path, &trace));
    pr_check_lanes_clean(t, &trace);
    CHECK(pr_count(&trace, 0u, "job") >= MC_THREADS_WORKERS);
    CHECK_EQ_U64(pr_count(&trace, 0u, "frame_draw"), 0u);
}

#endif /* PROFILER_BUILT */

TEST(the_flag_is_held_to_what_it_can_do) {
    char *const with_report[] = {t->borrowed_args[0], (char *)"--report", (char *)"--profile", (char *)"/dev/null",
                                 nullptr};
    char *const with_pattern[] = {t->borrowed_args[0], (char *)"--test-pattern", (char *)"--profile",
                                  (char *)"/dev/null", nullptr};
    char *const no_path[] = {t->borrowed_args[0], (char *)"--profile", nullptr};
    test_process_t run;
    test_process_run(t, with_report, PR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    test_process_run(t, with_pattern, PR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    test_process_run(t, no_path, PR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
#if PROFILER_BUILT
    char *const nowhere[] = {t->borrowed_args[0], (char *)"--profile", (char *)"/no/such/directory/trace.json",
                             nullptr};
    test_process_run(t, nowhere, PR_PROGRAM_TIMEOUT_MS, &run);
    /* A run asked to be measured, and it could not say. */
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_RUNTIME);
    CHECK_CONTAINS(run.output, "could not write the trace");
#else
    char *const refused[] = {t->borrowed_args[0], (char *)"--profile", (char *)"/dev/null", nullptr};
    test_process_run(t, refused, PR_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "profile preset");
#endif
}

#if PROFILER_BUILT

static void *pr_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

TEST(the_real_threads_each_keep_to_their_own_lane) {
    gpu_bridge_t *const bridge = pr_aligned(t, sizeof(gpu_bridge_t));
    job_system_t *const job = pr_aligned(t, sizeof(job_system_t));
    world_t *const world = pr_aligned(t, sizeof(world_t));
    terrain_t *const terrain = pr_aligned(t, sizeof(terrain_t));
    render_t *const render = pr_aligned(t, sizeof(render_t));
    texture_t *const texture = pr_aligned(t, sizeof(texture_t));
    game_t *const game = pr_aligned(t, sizeof(game_t));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    profiler_t profiler;
    REQUIRE(profiler_init(&profiler, profiler_limits_default()));
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    REQUIRE(thread_topology_read(topology));
    REQUIRE(job_init(job, topology));
    job_profiler_set(job, &profiler);
    REQUIRE(texture_init(texture));
    REQUIRE(world_init(world, world_limits_default()));
    REQUIRE(terrain_init(terrain, TERRAIN_SEED_DEFAULT));
    REQUIRE(render_init(render, &bridge->device_memory, job, render_limits_default()));
    game_init(game, world, terrain, render, job, texture, topology);
    game_profiler_set(game, &profiler);
    const profiler_lane_t *const main_lane = game->borrowed_lane_main;
    CHECK(game->borrowed_lane_sim == main_lane);
    CHECK(game->borrowed_lane_gpu == main_lane);

    enum : uint32_t { WIDTH = 160u, HEIGHT = 90u };
    const game_config_t config = {.render_distance = 4u,
                                  .width = WIDTH,
                                  .height = HEIGHT,
                                  .features_off = SHADE_FEATURE_DAY_CYCLE,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    const uint64_t bytes = post_xrgb_bytes(WIDTH, HEIGHT);
    uint8_t *const screen = pr_aligned(t, bytes);
    display_buffer_t target = {
        .owned_map = screen, .map_bytes = bytes, .pitch = WIDTH * 4u, .width = WIDTH, .height = HEIGHT};
    REQUIRE(game_loop_start(game, nullptr));
    CHECK(game->borrowed_lane_gpu == game->borrowed_lane_gpu_thread);
    const render_fly_input_t still = {0};
    const int64_t started_ns = time_now_ns();
    uint64_t shown = 0u;
    while (shown < PR_FRAMES_WANTED) {
        REQUIRE(game_loop_frame(game, &still, 1.0f32 / 120.0f32, &target));
        shown = game->pictures_shown;
        REQUIRE(time_now_ns() - started_ns < 100ll * TIME_NS_PER_SECOND);
        time_sleep_ns(1000000);
    }
    /* A tick's worth of time, so that the sim has had one. */
    time_sleep_ns(200 * TIME_NS_PER_MS);
    REQUIRE(game_loop_stop(game));
    CHECK(game->borrowed_lane_sim == main_lane);
    CHECK(game->borrowed_lane_gpu == main_lane);
    const uint64_t frames = render->frame.stats.frames;

    /* The last frame's timers, against what the books say a frame takes. */
    const frame_timings_t *const timings = &render->frame.last_timings;
    float32_t kernels_ms = 0.0f32, passes_ms = 0.0f32;
    for (uint32_t i = 0u; i < FRAME_KERNEL_COUNT; i++) {
        kernels_ms += timings->kernel_ms[i];
        if ((timings->kernels_ran & (1u << i)) == 0u) {
            CHECK(timings->kernel_ms[i] == 0.0f32);
        }
    }
    for (uint32_t i = 0u; i < FRAME_PASS_COUNT; i++) {
        passes_ms += timings->pass_ms[i];
    }
    CHECK(timings->kernel_ms[FRAME_KERNEL_PRIMARY] > 0.0f32);
    CHECK(timings->kernel_ms[FRAME_KERNEL_SHADE] > 0.0f32);
    CHECK(kernels_ms > 0.0f32);
    CHECK(kernels_ms <= timings->total_ms + 0.001f32);
    CHECK_NEAR(passes_ms, timings->total_ms, 0.001f32);
    CHECK_NEAR(timings->kernel_ms[FRAME_KERNEL_COARSE], timings->pass_ms[FRAME_PASS_COARSE], 0.001f32);
    CHECK_NEAR(timings->kernel_ms[FRAME_KERNEL_READBACK], timings->pass_ms[FRAME_PASS_READBACK], 0.001f32);

    game_close(game);
    job_group_t group;
    job_group_init(&group);
    job_group_deinit(&group);

    char path[512];
    pr_path(t, path, sizeof(path), "threads.json");
    REQUIRE(profiler_trace_dump(&profiler, path) == FILE_OK);
    pr_trace_t trace;
    REQUIRE(pr_trace_read(t, path, &trace));
    pr_check_lanes_clean(t, &trace);
    const uint32_t main_tid = pr_tid_named(&trace, "main");
    const uint32_t sim_tid = pr_tid_named(&trace, "sim");
    const uint32_t gpu_tid = pr_tid_named(&trace, "gpu");
    const uint32_t card_tid = pr_tid_named(&trace, "card");
    REQUIRE(main_tid != 0u && sim_tid != 0u && gpu_tid != 0u && card_tid != 0u);
    CHECK(pr_count(&trace, main_tid, "loop_frame") > 0u);
    CHECK(pr_count(&trace, sim_tid, "sim_step") > 0u);
    CHECK(pr_count(&trace, sim_tid, "sim_tick") > 0u);
    CHECK_EQ_U64(pr_count(&trace, main_tid, "sim_tick"), 0u);
    CHECK_EQ_U64(pr_count(&trace, main_tid, "frame_draw"), 0u);
    CHECK_EQ_U64(pr_count(&trace, gpu_tid, "frame_draw"), frames);
    CHECK(pr_count(&trace, gpu_tid, "upload") >= frames);
    CHECK_EQ_U64(pr_count(&trace, card_tid, "coarse"), frames);
    CHECK_EQ_U64(pr_count(&trace, card_tid, "readback"), frames);
    CHECK(pr_count(&trace, 0u, "column") > 0u);

    game_deinit(game);
    render_deinit(render);
    CHECK(gpu_bridge_exit_check(bridge));
    terrain_deinit(terrain);
    world_deinit(world);
    texture_deinit(texture);
    job_deinit(job);
    gpu_bridge_deinit(bridge);
    profiler_deinit(&profiler);
}

#endif /* PROFILER_BUILT */

TEST_MAIN()
