/* See run/src/game/fill/README.md. */
#ifndef MC_RUN_GAME_FILL_INDEX_H
#define MC_RUN_GAME_FILL_INDEX_H

#include "machine/index.h"
#include "render/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 005 phase 14: how long the world takes to fill in around the eye, after it opens and after each change of render distance or
 * full detail. Ring r is every column whose distance from the eye's column, rounded up, is r: the map loads a circle. A ring is drawn
 * when every one of its columns is on the card with all of its sections, which is when the upload queue has finished a view that had
 * them all. */

enum : uint32_t {
    /* Full detail is at most 48 chunks (the Settings screen's limit). A ring past the last is counted in it. */
    GAME_FILL_RINGS = 64u,
    /* The fills the end-of-session report keeps: the newest. */
    GAME_FILL_HISTORY = 16u,
    /* The targets' "ground near you": the rings out to 4 chunks. */
    GAME_FILL_NEAR_RING = 4u,
};

typedef enum game_fill_kind_t : uint32_t {
    GAME_FILL_OPEN = 0u,   /* a world opened */
    GAME_FILL_CHANGE = 1u, /* render distance or full detail changed in an open world */
} game_fill_kind_t;

/* One fill, as the report and bench_world keep it. Times are from its start, in ns; -1 for never. */
typedef struct game_fill_summary_t {
    int64_t first_ns;         /* 0: the first change drawn: a column come or gone, or a far column built */
    int64_t ring_near_ns;     /* 8: rings 0 to GAME_FILL_NEAR_RING, or to full detail if that is less */
    int64_t near_ns;          /* 16: every ring out to full detail */
    int64_t far_ns;           /* 24: the far field had nothing left to build; 0 with no far field */
    int64_t done_ns;          /* 32: both */
    uint64_t far_columns;     /* 40: far columns built by then, or by the time it was cut short */
    uint32_t render_distance; /* 48 */
    uint32_t full_detail;     /* 52 */
    game_fill_kind_t kind;    /* 56 */
    uint8_t _pad0[4];         /* 60 */
} game_fill_summary_t;
static_assert(sizeof(game_fill_summary_t) == 64);
static_assert(offsetof(game_fill_summary_t, render_distance) == 48);

/* What is being filled now, and the newest fills. Whichever thread uploads writes it (gpu, or main with no threads), and starts it with
 * no thread running. The atomics are for a test or a bench watching from main while the threads run: each is written once a fill, with
 * release, after the rest of what it says. */
typedef struct game_fill_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic int64_t first_ns; /* 0: as in the summary, for the fill going now */
    _Atomic int64_t ring_near_ns;                              /* 8 */
    _Atomic int64_t near_ns;                                   /* 16 */
    _Atomic int64_t far_ns;                                    /* 24 */
    _Atomic int64_t done_ns;                                   /* 32 */
    _Atomic uint32_t sequence;              /* 40: fills started since the world opened, this one included */
    uint32_t rings_done;                    /* 44: rings 0 to this - 1 are all drawn */
    int64_t started_ns;                     /* 48: time_now_ns at the start */
    uint64_t far_built_start;               /* 56: the far field's columns_built at the start */
    int64_t ring_ns[GAME_FILL_RINGS];       /* 64: from the start, -1 for not yet */
    uint16_t ring_columns[GAME_FILL_RINGS]; /* 576: how many columns each ring has */
    uint32_t ready_start;                   /* 704: columns on the card at the start */
    uint32_t render_distance;               /* 708 */
    uint32_t full_detail;                   /* 712 */
    game_fill_kind_t kind;                  /* 716 */
    uint32_t history_count;                 /* 720: fills finished, of which the last GAME_FILL_HISTORY are kept */
    uint8_t has_far;                        /* 724 */
    uint8_t logged;                         /* 725: the summary line has been said */
    uint8_t _pad0[10];                      /* 726 */
    game_fill_summary_t history[GAME_FILL_HISTORY]; /* 736: by history_count, round */
    int64_t scanned_ns;                             /* 1760: from the start, when the table was last looked at */
    uint64_t
        far_built_last; /* 1768: the far field's columns_built at the last step, for a fill cut short by a new far field */
    /* 1776: sim steps in which a column waited for its climate tiles, since the world opened. The sim adds, relaxed; the report reads
     * it after the threads have stopped. */
    _Atomic uint64_t climate_waits;
    uint8_t _pad1[8]; /* 1784 */
} game_fill_t;
static_assert(sizeof(game_fill_t) == 1792);
static_assert(alignof(game_fill_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_fill_t, ring_ns) == 64);
static_assert(offsetof(game_fill_t, ring_columns) == 576);
static_assert(offsetof(game_fill_t, history) == 736);

/* Nothing filled yet, for a world opening. */
void game_fill_reset(game_fill_t *fill);

/* A fill starts now: a world opened, or render distance or full detail changed. render is as the change left it, and no thread may be
 * using it. full_detail is the map's view radius. */
void game_fill_start(game_fill_t *fill, game_fill_kind_t kind, uint32_t render_distance, uint32_t full_detail,
                     const render_t *render);

/* After each round of uploads and far-field work, on the thread that did it. Says a line for each ring as it is first drawn, and one for
 * the fill once all of it is. Costs a look at every slot of the table while a fill goes on and the upload queue is between views, and
 * nothing once it is done. */
void game_fill_step(game_fill_t *fill, const render_t *render);

/* The fill going now as a summary, as far as it has got. */
void game_fill_summary(const game_fill_t *fill, game_fill_summary_t *out);

/* The end-of-session report's lines: one per fill kept, the one going now included if it never finished. */
void game_fill_report(const game_fill_t *fill);

/* ---- Wiring into a game ---------------------------------------------------------------------------------------------- */

typedef struct game_t game_t;

/* The game's fill, beside its column jobs in the World arena, or nullptr with no world open. In game/index.c. */
game_fill_t *game_fill_of(const game_t *game);

#endif
