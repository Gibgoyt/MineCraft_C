/* See run/src/game/pace/README.md. */
#ifndef MC_RUN_GAME_PACE_INDEX_H
#define MC_RUN_GAME_PACE_INDEX_H

#include "machine/index.h"
#include "render/frame/index.h"
#include "tuning/render/resolution/index.h"
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

/* ---- Plan 008 phase 05: the resolution ladder, and the rung for each picture ---------------------------------------------------- */

enum : uint32_t {
    GAME_PACE_RUNGS = 4u, /* the top (the render scale) and the three fixed rungs under it */
    GAME_PACE_RUNG_HIGH = MC_TUNE_RENDER_RESOLUTION_RUNG_HIGH,
    GAME_PACE_RUNG_MIDDLE = MC_TUNE_RENDER_RESOLUTION_RUNG_MIDDLE,
    GAME_PACE_RUNG_LOW = MC_TUNE_RENDER_RESOLUTION_RUNG_LOW,
    GAME_PACE_RUNG_STEP_UP = MC_TUNE_RENDER_RESOLUTION_STEP_UP_PICTURES,
    GAME_PACE_RUNG_STILL = MC_TUNE_RENDER_RESOLUTION_STILL_PICTURES,
};
static_assert(GAME_PACE_RUNG_HIGH > GAME_PACE_RUNG_MIDDLE && GAME_PACE_RUNG_MIDDLE > GAME_PACE_RUNG_LOW);
static_assert((uint32_t)GAME_PACE_RUNG_HIGH < (uint32_t)POST_SCALE_WHOLE);
static_assert((uint32_t)GAME_PACE_RUNG_STEP_UP <= (uint32_t)GAME_PACE_SAMPLES); /* the samples it looks back over */
static_assert(GAME_PACE_RUNG_STILL <= UINT8_MAX); /* what game_pace_t's still count holds */
#define GAME_PACE_RUNG_SLOT_SHARE MC_TUNE_RENDER_RESOLUTION_SLOT_SHARE
#define GAME_PACE_RUNG_TURN_DEGREES MC_TUNE_RENDER_RESOLUTION_TURN_DEGREES
#define GAME_PACE_RUNG_STILL_PIXELS MC_TUNE_RENDER_RESOLUTION_STILL_PIXELS

/* The rungs a world may draw at, in percent of each side, from the top down. percent[0] is the top: the render scale. Under it the fixed
 * rungs that are below it and not below the floor. With dynamic resolution off there is the top alone. Plan 009 phase 07 (temporal
 * upscaling) draws at these same sizes. */
typedef struct game_pace_ladder_t {
    uint8_t percent[GAME_PACE_RUNGS]; /* 0 */
    uint8_t count;                    /* 4: at least 1 */
    uint8_t _pad0[3];                 /* 5 */
} game_pace_ladder_t;
static_assert(sizeof(game_pace_ladder_t) == 8);

/* What the controller looks at for the next picture, past what game_pace_t keeps. Plain values: tests drive it alone. */
typedef struct game_pace_rung_in_t {
    int64_t tick_ns; /* 0: between main's takes; the slot is GAME_PACE_JIT_TICKS of them */
    float32_t
        turn_pixels; /* 8: the camera's turn at the picture's centre since the last picture, in the output's pixels */
    float32_t turn_degrees; /* 12: the same in degrees a second */
    uint8_t late;           /* 16: the last picture finished was ready after the take it was aimed at */
    uint8_t late_percent;   /* 17: the rung it was drawn at */
    uint8_t _pad0[6];       /* 18 */
} game_pace_rung_in_t;
static_assert(sizeof(game_pace_rung_in_t) == 24);

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
    /* Plan 008 phase 05: the gpu thread's again. The controller's memory, beside samples_ns at the same places. */
    uint32_t card_us[GAME_PACE_SAMPLES];      /* 256: each sample's G, the card's own time, in microseconds */
    uint8_t drawn_percent[GAME_PACE_SAMPLES]; /* 320: the rung each was drawn at */
    game_pace_ladder_t ladder;                /* 336 */
    uint8_t rung;                             /* 344: the next picture's, an index into ladder.percent */
    uint8_t still;         /* 345: pictures in a row with the camera still, to GAME_PACE_RUNG_STILL */
    uint8_t fits;          /* 346: pictures in a row the next rung up was predicted to fit */
    uint8_t _pad2[5];      /* 347 */
    int64_t last_begun_ns; /* 352: when the last picture's rung was asked for, for degrees a second */
    uint8_t _pad3[24];     /* 360 */
} game_pace_t;
static_assert(sizeof(game_pace_t) == 384);
static_assert(offsetof(game_pace_t, next_take_ns) == 3u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_pace_t, card_us) == 4u * MC_CPU_CACHE_LINE_BYTES);

/* Nothing measured yet. As a world opens, before any thread. */
void game_pace_start(game_pace_t *pace, bool just_in_time);

/* When to start the next picture. Pure: no clock inside, and nothing but in. */
void game_pace_decide(const game_pace_in_t *in, game_pace_out_t *out);

/* The gpu thread's, after each picture: it began at started_ns, was ready at ready_ns, and was aimed at aimed_ns (0 for none). Keeps
 * GAME_PACE_SAMPLES samples and T-hat, their 90th percentile. */
void game_pace_note(game_pace_t *pace, int64_t started_ns, int64_t ready_ns, int64_t aimed_ns);

/* Plan 008 phase 05. The ladder from a top (the render scale; 0 is 100) and a floor (0 for dynamic resolution off), into the pace, with the
 * next picture at the top and nothing counted. Whenever the view changes, with no picture on its way. */
void game_pace_ladder_set(game_pace_t *pace, uint32_t top_percent, uint32_t floor_percent);

/* The ladder alone, for a test or a reader. Pure. */
void game_pace_ladder(uint32_t top_percent, uint32_t floor_percent, game_pace_ladder_t *out);

/* The gpu thread's, after game_pace_note of a picture: that picture's card time and the rung it was drawn at, beside its P + G. */
void game_pace_note_drawn(game_pace_t *pace, int64_t card_ns, uint32_t percent);

/* What a picture at percent is predicted to take, start to ready: P-hat + G-hat at 100 % times its share of the pixels, where G-hat at 100 %
 * is the 90th percentile of each sample's G over the share of the pixels it was drawn with, and P-hat the 90th percentile of P + G less G.
 * 0 with no samples. Pure. */
int64_t game_pace_predict_ns(const game_pace_t *pace, uint32_t percent);

/* The controller: the rung for the next picture, in percent of each side, and the pace's counts moved on. No clock inside: what it sees is
 * the pace and in. With the camera still it never steps down, and after GAME_PACE_RUNG_STILL still pictures it is at the top; turning, it
 * steps down one rung after a late picture drawn at this rung (or below), or when faster than GAME_PACE_RUNG_TURN_DEGREES with this rung
 * predicted over GAME_PACE_RUNG_SLOT_SHARE of the slot; it steps up one when the rung above has been predicted under that share for
 * GAME_PACE_RUNG_STEP_UP pictures in a row. Never past the ladder's ends. See README.md. */
uint32_t game_pace_rung(game_pace_t *pace, const game_pace_rung_in_t *in);

/* The gpu thread, before each picture: game_pace_rung with the turn worked out from the frame's last camera to this one and the time since
 * the last ask, and the late flag from the last picture noted. Sets nothing on the frame: the caller does, with render_frame_scale_set. */
uint32_t game_pace_rung_next(game_pace_t *pace, const render_frame_t *frame, const render_camera_t *camera,
                             int64_t now_ns);

/* The first of main's takes at or after at_ns, on the beat next_take_ns + k tick_ns for k >= 0. 0 if next_take_ns is 0. */
int64_t game_pace_take_at(int64_t next_take_ns, int64_t tick_ns, int64_t at_ns);

typedef struct game_t game_t;

/* The open world's, or nullptr with none open. In game/index.c, which lays out the World arena. */
game_pace_t *game_pace_of(const game_t *game);

#endif
