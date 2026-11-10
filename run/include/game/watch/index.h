/* See run/src/game/watch/README.md. */
#ifndef MC_RUN_GAME_WATCH_INDEX_H
#define MC_RUN_GAME_WATCH_INDEX_H

#include "machine/index.h"
#include "render/frame/index.h"
#include "types/index.h"
#include "ui/hud/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 005 phase 7: what a world left alone piles up, for the F3 overlay, and a line in the log when a frame or a tick is slow. */

enum : uint32_t {
    /* The sim says how the last this many ticks went: a minute at 20 Hz. */
    GAME_WATCH_SPAN_TICKS = 60u * (uint32_t)MC_GAME_TICK_HZ,
};
enum : int64_t {
    GAME_WATCH_SLOW_FRAME_NS = 33000000, /* a frame past this is slow: under 30 a second */
    GAME_WATCH_SLOW_TICK_NS = 50000000,  /* a tick past this is slow: the whole of its 20 Hz slot */
    GAME_WATCH_SAY_GAP_NS = 5000000000, /* at most one slow line of each kind in this long, so the log stays readable */
};

/* The parts of a sim tick, in its order, each timed. */
typedef enum game_watch_tick_stage_t : uint8_t {
    GAME_WATCH_TICK_WEATHER = 0, /* the weather where the eye is, and the sun at the feet */
    GAME_WATCH_TICK_HANDS = 1,   /* mining, placing, screens, chests and furnaces */
    GAME_WATCH_TICK_MOBS = 2,    /* paths, minds, spawning, and the bodies and drops moving */
    GAME_WATCH_TICK_PLAYER = 3,  /* the body moving, and its health */
    GAME_WATCH_TICK_WORLD = 4,   /* block ticks: leaves, water, crops */
    GAME_WATCH_TICK_PUBLISH = 5, /* what the renderer and the HUD are handed */
    GAME_WATCH_TICK_STAGES = 6,
} game_watch_tick_stage_t;

/* One line per thread that writes it, as each writes its own all the time. In the game's World arena beside the column jobs
 * (game_watch_of), as game_t is at its size. */
typedef struct game_watch_t {
    /* The sim's line. mobs and drops are read by main, relaxed: only the overlay depends on them. */
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic uint32_t mobs; /* 0: in the entity store, after the last tick */
    _Atomic uint32_t drops;                                 /* 4: stacks on the ground, the same */
    int64_t tick_said_ns;                                   /* 8: when a slow tick was last said; 0 for never */
    uint64_t ticks_unsaid;                                  /* 16: slow ticks since then, not said */
    int64_t span_tick_ns;                                   /* 24: the ticks of this span, added up */
    int64_t span_tick_most_ns;                              /* 32: the slowest of them */
    uint32_t span_ticks;                                    /* 40 */
    uint8_t span_most_stage; /* 44: the slowest part of that tick: game_watch_tick_stage_t */
    uint8_t _pad0[19];       /* 45 */
    /* The gpu thread's line, read by main and the sim, relaxed, for the same reason. */
    _Atomic uint32_t uploads;  /* 64: sections of the newest snapshot not yet dealt with, after the last upload */
    _Atomic uint32_t far_jobs; /* 68: far-field columns on workers, the same */
    uint8_t _pad1[56];         /* 72 */
    /* Main's line, but that the sim takes the span's frames from it, each word by exchange. A frame's numbers landing either side of
     * the exchange, or a slowest lost to it, puts a frame in the next span, which only the log depends on. */
    _Atomic int64_t span_frame_ns;      /* 128: the frames of this span, added up */
    _Atomic int64_t span_frame_most_ns; /* 136: the slowest of them */
    _Atomic uint32_t span_frames;       /* 144 */
    uint8_t _pad2[4];                   /* 148 */
    int64_t frame_said_ns;              /* 152: when a slow frame was last said; 0 for never */
    uint64_t frames_unsaid;             /* 160: slow frames since then, not said */
    uint8_t _pad3[24];                  /* 168 */
} game_watch_t;
static_assert(sizeof(game_watch_t) == 3u * MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(game_watch_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, uploads) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, span_frame_ns) == 2u * MC_CPU_CACHE_LINE_BYTES);

/* Nothing seen yet. As a world opens, before any thread. */
void game_watch_start(game_watch_t *watch);

/* The sim's, after each tick that ran: what each part took, and what is in the world now. Says a slow tick, at most once in
 * GAME_WATCH_SAY_GAP_NS, naming its slowest part, and every GAME_WATCH_SPAN_TICKS ticks how they and the frames went. tick_count is the
 * game's, counting this one. */
void game_watch_tick_note(game_watch_t *watch, const int64_t stage_ns[GAME_WATCH_TICK_STAGES], uint64_t tick_count,
                          uint32_t mobs, uint32_t drops, int64_t now_ns);

/* The gpu thread's, after each upload. */
void game_watch_gpu_note(game_watch_t *watch, uint32_t uploads, uint32_t far_jobs);

/* Main's, after each frame of play: frame_ns is how long it took. True if it was slow and a line is due: then call
 * game_watch_frame_say with what the overlay would show, so that working that out is only paid for then. */
[[nodiscard]] bool game_watch_frame_note(game_watch_t *watch, int64_t frame_ns, int64_t now_ns);

/* The slow frame's line: the slowest part of those debug has, main's own time past the hud and present, and what piles up. */
void game_watch_frame_say(game_watch_t *watch, int64_t frame_ns, const ui_hud_debug_t *debug);

/* What the overlay shows of it: mobs, drops, uploads and far jobs. */
void game_watch_debug_read(const game_watch_t *watch, ui_hud_debug_t *out);

/* A part's name, for the log. */
const char *game_watch_tick_stage_name(game_watch_tick_stage_t stage);

typedef struct game_t game_t;

/* The open world's, or nullptr with none open. In game/index.c, which lays out the World arena. */
game_watch_t *game_watch_of(const game_t *game);

/* The gpu thread's, after each upload: what the upload and the far field have still to do, read on that thread. */
void game_watch_gpu_look(game_t *game);

/* What the card's clock said a frame's kernels took, on the profiler's card lane in the host's timeline: end to end, the last ending
 * now, as the wait for it has just come back. Nothing with no lane. Whoever drew the frame calls it. */
void game_watch_kernels_note(game_t *game, const frame_timings_t *timings);

/* Main's, after a frame of play that took frame_ns: a slow one is said, now and then, with what the overlay would show. Nothing with
 * no world open. */
void game_watch_frame_look(game_t *game, int64_t frame_ns);

#endif
