/* See run/src/profiler/README.md. */
#ifndef MC_RUN_PROFILER_INDEX_H
#define MC_RUN_PROFILER_INDEX_H

#include "file/index.h"
#include "heap/index.h"
#include "machine/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static_assert(MC_CPU_CACHE_LINE_BYTES == 64);

/* The scopes written into the program exist in the profile preset, and in the presets that check things, so that ctest can
 * see them work. In a release build PROFILER_BEGIN and PROFILER_END are nothing at all, and evaluate nothing. The functions
 * below are always there: only the macros go away. */
#if defined(MC_PROFILE) || !defined(NDEBUG)
#define PROFILER_BUILT 1
#else
#define PROFILER_BUILT 0
#endif

enum : uint32_t {
    PROFILER_DEPTH_MAX = 32u,         /* scopes open at once on a lane; deeper ones are counted, not timed */
    PROFILER_LANES_MAX = 32u,         /* what a row's lane_mask has bits for */
    PROFILER_LANE_NAME_BYTES = 16u,   /* with its NUL: a thread's name is no longer than this */
    PROFILER_SUMMARY_ROWS_MAX = 256u, /* different scope names the table keeps count of */
};

/* One finished scope, or one span on the card's clock. name is borrowed for good: a string literal. */
typedef struct profiler_record_t {
    const char *name;    /* 0 */
    int64_t start_ns;    /* 8: on time_now_ns's clock */
    int64_t duration_ns; /* 16 */
    int64_t self_ns;     /* 24: duration, less the scopes directly inside it */
} profiler_record_t;
static_assert(sizeof(profiler_record_t) == 32);

/* A scope that has begun and not ended. */
typedef struct profiler_open_t {
    const char *name; /* 0 */
    int64_t start_ns; /* 8 */
    int64_t child_ns; /* 16: what the scopes directly inside it have taken so far */
} profiler_open_t;
static_assert(sizeof(profiler_open_t) == 24);

/* One thread's scopes. One thread at a time writes a lane, and nothing reads it until that thread has been joined or its jobs waited
 * for. So nothing here is atomic, and a scope is two clock reads and a few stores. Its own cache lines, as each is written by another
 * thread. */
typedef struct profiler_lane_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) profiler_record_t *borrowed_records; /* 0: capacity, in the profiler's region */
    uint64_t capacity;                                                    /* 8: a power of two */
    uint64_t written;                                                     /* 16: ever: past capacity, the oldest went */
    uint64_t unmatched_ends;                                              /* 24: an end with nothing open */
    uint64_t too_deep;                                                    /* 32: begins past PROFILER_DEPTH_MAX */
    uint32_t depth;                                                       /* 40: scopes open now, too deep or not */
    uint8_t _pad0[4];                                                     /* 44 */
    char name[PROFILER_LANE_NAME_BYTES];                                  /* 48 */
    profiler_open_t stack[PROFILER_DEPTH_MAX];                            /* 64 */
} profiler_lane_t;
static_assert(sizeof(profiler_lane_t) == 832);
static_assert(alignof(profiler_lane_t) == MC_CPU_CACHE_LINE_BYTES);

typedef struct profiler_limits_t {
    uint64_t records_per_lane; /* 0: a power of two, from 16 up. The newest this many are kept */
    uint32_t lane_count;       /* 8: from 1 to PROFILER_LANES_MAX */
    uint8_t _pad0[4];          /* 12 */
} profiler_limits_t;
static_assert(sizeof(profiler_limits_t) == 16);

/* The whole profiler. One, under app_t, and only for a run that asked for it. Zeroed means not started. */
typedef struct profiler_t {
    heap_region_t owned_region;      /* 0: the lanes, their records, and room to write out */
    profiler_lane_t *borrowed_lanes; /* 16: lane_count, in owned_region */
    uint8_t *borrowed_write_bytes;   /* 24: room to write out, in owned_region */
    profiler_limits_t limits;        /* 32 */
    int64_t started_ns;              /* 48: what a trace's times count from */
    _Atomic uint32_t lanes_claimed;  /* 56 */
    uint8_t _pad0[4];                /* 60 */
} profiler_t;
static_assert(sizeof(profiler_t) == 64);

/* One scope name, over every lane: a row of the table. */
typedef struct profiler_summary_row_t {
    const char *name;   /* 0 */
    uint64_t count;     /* 8 */
    int64_t total_ns;   /* 16 */
    int64_t self_ns;    /* 24 */
    int64_t max_ns;     /* 32 */
    uint32_t lane_mask; /* 40: a bit for each lane it was seen on */
    uint8_t _pad0[4];   /* 44 */
} profiler_summary_row_t;
static_assert(sizeof(profiler_summary_row_t) == 48);

/* A lane for each thread the game has and for the card, and 4 MiB of records in each. */
profiler_limits_t profiler_limits_default(void);

/* Reserves everything now, so that no scope ever touches the heap. False, having said why and left nothing behind. */
[[nodiscard]] bool profiler_init(profiler_t *profiler, profiler_limits_t limits);
/* Safe on a zeroed profiler_t. Nothing may be writing a lane. */
void profiler_deinit(profiler_t *profiler);

/* A lane for a thread, named for it. nullptr if there is no profiler, it is not started, or it has none left, having said so if so:
 * every call below takes nullptr and does nothing, which is what a run with no profiler has. From any thread. */
profiler_lane_t *profiler_lane_claim(profiler_t *profiler, const char *name);
/* count lanes in a row, for a pool of threads: "prefix 0", "prefix 1" and on. nullptr, as above, if there is not room for them all. */
profiler_lane_t *profiler_lanes_claim(profiler_t *profiler, const char *prefix, uint32_t count);

/* A scope on the lane's thread. An end closes what began last. name must last: a string literal, as it is first looked at when the run
 * ends. */
void profiler_begin(profiler_lane_t *lane, const char *name);
void profiler_end(profiler_lane_t *lane);
/* The same at a time the caller says: what tests use for a clock that does what it is told. */
void profiler_begin_at(profiler_lane_t *lane, const char *name, int64_t now_ns);
void profiler_end_at(profiler_lane_t *lane, int64_t now_ns);
/* Something that has already happened, as the card's kernels have by the time their clock can be read. Whatever is open on the lane is
 * not charged for it. */
void profiler_span_add(profiler_lane_t *lane, const char *name, int64_t start_ns, int64_t duration_ns);

#if PROFILER_BUILT
#define PROFILER_BEGIN(lane, scope_name) profiler_begin((lane), (scope_name))
#define PROFILER_END(lane) profiler_end(lane)
#else
#define PROFILER_BEGIN(lane, scope_name) ((void)sizeof(lane))
#define PROFILER_END(lane) ((void)sizeof(lane))
#endif

/* What follows reads the lanes, so every thread that wrote one must have been joined, or its jobs waited for. */

/* What the kept records come to, by scope name, most self time first. Fills up to capacity rows, and says how many it filled. Records
 * of names past capacity are counted in out_unlisted, which may be nullptr. */
uint32_t profiler_summary_read(const profiler_t *profiler, profiler_summary_row_t *out, uint32_t capacity,
                               uint64_t *out_unlisted);

/* Chrome's trace format, which about:tracing and Perfetto read: every kept record, a lane to a thread, in microseconds from
 * profiler_init, with what each lane lost as an "mc_lane" metadata event. */
[[nodiscard]] file_status_t profiler_trace_write(const profiler_t *profiler, const file_t *file);
/* The same into a file at path, made or emptied, having said why if it could not. */
[[nodiscard]] file_status_t profiler_trace_dump(const profiler_t *profiler, const char *path);

/* A table for a person: each lane, then each scope name by self time. */
[[nodiscard]] file_status_t profiler_summary_write(const profiler_t *profiler, const file_t *file);

#endif
