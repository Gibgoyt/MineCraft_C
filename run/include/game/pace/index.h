/* See run/src/game/pace/README.md. */
#ifndef MC_RUN_GAME_PACE_INDEX_H
#define MC_RUN_GAME_PACE_INDEX_H

#include "machine/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 008 phase 02: when the gpu thread starts each picture. A late picture no longer waits a whole slot, and the card no longer idles
 * waiting for main to take the last one when starting now would show the next one sooner. */

enum : uint32_t {
    GAME_PACE_SAMPLES = 16u, /* T-hat is the 90th percentile of this many pictures' P + G */
    /* Pictures that fit this many of main's ticks, margin and all, are started just in time, one each that many ticks: 120 or 60 a second
     * at 120 Hz, evenly. Dearer ones are drawn back to back, as more pictures a second are worth more there than an even beat. */
    GAME_PACE_JIT_TICKS = 2u,
};
enum : int64_t {
    GAME_PACE_MARGIN_NS = 1000000, /* Q3: a picture just in time is aimed to be ready this long before its take */
    GAME_PACE_SLICE_NS =
        500000, /* the gpu thread sleeps toward a start in slices of at most this, looking again each time */
};

/* How a picture was started: in the frame log, so phase 07 can see the rule work. */
typedef enum game_pace_how_t : uint8_t {
    GAME_PACE_HOW_TODAY = 0, /* no next take known (or uncapped, or paused): at once, or when main has taken the last */
    GAME_PACE_HOW_JUST_IN_TIME = 1, /* aimed to be ready GAME_PACE_MARGIN_NS before the earliest take it can make */
    GAME_PACE_HOW_AFTER_LATE = 2,   /* at once, because the last one was ready after the take it was aimed at */
    GAME_PACE_HOW_BACK_TO_BACK = 3, /* at once, because a picture takes more than GAME_PACE_JIT_TICKS ticks */
    GAME_PACE_HOW_NOW = 4,          /* Q3 B: at once, once main has taken the last */
} game_pace_how_t;

/* Everything the rule looks at. Plain values, no clock: tests drive it with a fake one. */
typedef struct game_pace_in_t {
    int64_t now_ns;          /* 0 */
    int64_t next_take_ns;    /* 8: main's next take, from game_pace_t's next_take_ns; 0 when not known */
    int64_t tick_ns;         /* 16: between main's takes: 1e9 / MC_DISPLAY_REFRESH_HZ */
    int64_t predicted_ns;    /* 24: T-hat; 0 before any picture */
    int64_t last_ready_ns;   /* 32: when the last picture was ready */
    int64_t last_aimed_ns;   /* 40: the take it was aimed at; 0 for none */
    int64_t margin_ns;       /* 48: GAME_PACE_MARGIN_NS, but for a test */
    uint8_t picture_waiting; /* 56: main has not taken the last picture yet */
    uint8_t uncapped;        /* 57 */
    uint8_t just_in_time;    /* 58: Q3: 1 for A, 0 for B */
    uint8_t _pad0[5];        /* 59 */
} game_pace_in_t;
static_assert(sizeof(game_pace_in_t) == 64);

/* What it decided. */
typedef struct game_pace_out_t {
    int64_t
        start_at_ns; /* 0: sleep until this, then draw; with hold, only when it would be if main took the last on time */
    int64_t aimed_ns; /* 8: the take this picture is for; 0 when not known */
    uint8_t hold;     /* 16: do not draw yet whatever the clock says: a picture waits for main, so ask again */
    uint8_t how;      /* 17: game_pace_how_t */
    uint8_t _pad0[6]; /* 18 */
} game_pace_out_t;
static_assert(sizeof(game_pace_out_t) == 24);

/* The gpu thread's line, and main's. In the game's World arena beside game_watch_t (game_pace_of), as game_t is at its size. */
typedef struct game_pace_t {
    /* The gpu thread's. */
    alignas(MC_CPU_CACHE_LINE_BYTES) int64_t samples_ns[GAME_PACE_SAMPLES]; /* 0: the last pictures' P + G, a ring */
    int64_t predicted_ns;                                                   /* 128: T-hat */
    int64_t last_ready_ns;                                                  /* 136 */
    int64_t last_aimed_ns;                                                  /* 144 */
    uint32_t count;       /* 152: samples held, to GAME_PACE_SAMPLES */
    uint32_t next;        /* 156: where the next sample goes */
    uint8_t just_in_time; /* 160: Q3, set as the world opens: 1 for A (the default), 0 for B */
    uint8_t _pad0[31];    /* 161 */
    /* Main's: written every frame of play, read by the gpu thread, relaxed: a stale value is an earlier take on the same beat. */
    _Atomic int64_t next_take_ns; /* 192: main's next take; 0 when not known, and then the rule is today's */
    int64_t
        take_hint_ns;  /* 200: main's: what whoever runs its frames said of the next, for game_loop_frame to publish */
    uint8_t _pad1[48]; /* 208 */
} game_pace_t;
static_assert(sizeof(game_pace_t) == 256);
static_assert(offsetof(game_pace_t, next_take_ns) == 3u * MC_CPU_CACHE_LINE_BYTES);

/* Nothing measured yet. As a world opens, before any thread. */
void game_pace_start(game_pace_t *pace, bool just_in_time);

/* When to start the next picture. Pure: no clock inside, and nothing but in. */
void game_pace_decide(const game_pace_in_t *in, game_pace_out_t *out);

/* The gpu thread's, after each picture: it began at started_ns, was ready at ready_ns, and was aimed at aimed_ns (0 for none). Keeps
 * GAME_PACE_SAMPLES samples and T-hat, their 90th percentile. */
void game_pace_note(game_pace_t *pace, int64_t started_ns, int64_t ready_ns, int64_t aimed_ns);

/* The first of main's takes at or after at_ns, on the beat next_take_ns + k tick_ns for k >= 0. 0 if next_take_ns is 0. */
int64_t game_pace_take_at(int64_t next_take_ns, int64_t tick_ns, int64_t at_ns);

#endif
