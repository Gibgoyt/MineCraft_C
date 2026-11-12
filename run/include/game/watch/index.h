/* See run/src/game/watch/README.md. */
#ifndef MC_RUN_GAME_WATCH_INDEX_H
#define MC_RUN_GAME_WATCH_INDEX_H

#include "arena/index.h"
#include "file/index.h"
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

/* Plan 008 phase 01: where a picture's time goes, picture by picture, for F3, the minute line and --frame-log. */
enum : uint32_t {
    GAME_WATCH_RING = 4096u, /* pictures kept, and takes: 34 s at 120 a second; the sim reads them 20 times a second */
    GAME_WATCH_LOG_BYTES = 1u << 20,  /* the frame log's lines wait here until there are enough to write */
    GAME_WATCH_LOG_LINE_BYTES = 512u, /* the most one line can be */
    GAME_WATCH_BINS = 256u,   /* the minute's intervals on the screen, in whole ms; the last bin holds all past it */
    GAME_WATCH_RECENT = 512u, /* the most pictures F3 looks at from its last second */
};
enum : int64_t {
    GAME_WATCH_SLOT_NS = 1000000000 / (int64_t)MC_DISPLAY_REFRESH_HZ, /* the display's slot: 8.33 ms at 120 Hz */
    GAME_WATCH_CLOCK_NS = 1000000000,                                 /* the card's clock speed is read this often */
    GAME_WATCH_LOG_NS = 10000000000,                                  /* the frame log is written at least this often */
    GAME_WATCH_TURN_NS = 250000000,                                   /* F3's turn speed is the mean over this long */
};

/* When each part of a picture's way on the gpu thread ended, on time_now_ns's clock. */
typedef enum game_watch_stamp_t : uint8_t {
    GAME_WATCH_STAMP_WAITED = 0, /* the last picture was done, and the thread began to wait for main to take it */
    GAME_WATCH_STAMP_START = 1,  /* the wait was over, and this picture began: its camera was read just after */
    GAME_WATCH_STAMP_UPLOAD = 2,
    GAME_WATCH_STAMP_LOD = 3,    /* the far field's plan */
    GAME_WATCH_STAMP_FILL = 4,   /* the fill-in report and the light of the day */
    GAME_WATCH_STAMP_QUEUED = 5, /* every kernel was queued, entities and weather sent: the wait for the card began */
    GAME_WATCH_STAMP_READY = 6,  /* the card was done and the picture handed to main */
    GAME_WATCH_STAMPS = 7,
} game_watch_stamp_t;

/* A picture the gpu thread drew, taken or not. Written by the gpu thread alone, into pictures[seq % GAME_WATCH_RING], before it
 * counts it in pictures_drawn with a release: whoever reads one under that count reads it whole. */
typedef struct game_watch_picture_t {
    uint64_t seq;                         /* 0: from 0, a picture each */
    int64_t stamp_ns[GAME_WATCH_STAMPS];  /* 8 */
    float32_t pass_ms[FRAME_PASS_COUNT];  /* 64: the card's clock */
    float32_t total_ms;                   /* 84 */
    float32_t lod_ms;                     /* 88: the far field's rays, which are in primary */
    float32_t shade_ms[SHADE_SPAN_COUNT]; /* 92: shade's own kernels, which add up to its pass */
    float32_t card_mhz;                   /* 112: the last reading of the card's clock speed; 0 before one */
    float32_t turn_deg_s; /* 116: from the last picture's forward to this one's, over the time between their starts */
    uint32_t fresh;       /* 120: pixels on the fresh AO list */
    uint8_t clock_read;   /* 124: card_mhz was read on this very picture */
    uint8_t pace_how;     /* 125: plan 008 phase 02: how the start rule started it, a game_pace_how_t */
    uint8_t missed;       /* 126: it was ready after the take it was aimed at */
    uint8_t _pad0[1];     /* 127 */
    int64_t aimed_ns;     /* 128: the take the rule aimed it at; 0 for none */
    int64_t start_at_ns;  /* 136: when the rule said to start it: the start less this is the sleep's overshoot */
} game_watch_picture_t;
static_assert(sizeof(game_watch_picture_t) == 144);

/* What the gpu thread says of a picture before drawing it, in the exchange slot it goes into: the publish hands it to main with it. */
typedef struct game_watch_slot_t {
    uint64_t seq;         /* 0 */
    uint64_t start_frame; /* 8: main's frames when it began */
    int64_t start_ns;     /* 16 */
} game_watch_slot_t;
static_assert(sizeof(game_watch_slot_t) == 24);

/* A picture main took, which is what the frame log has a line for. Written by main alone, into takes[n % GAME_WATCH_RING], before it
 * counts it in takes_made with a release. */
typedef struct game_watch_take_t {
    uint64_t seq;         /* 0: the picture's */
    uint64_t frame;       /* 8: main's frames at the take */
    uint64_t start_frame; /* 16: and when the picture began */
    int64_t taken_ns;     /* 24 */
    int64_t start_ns;     /* 32 */
    int64_t x_put_ns;  /* 40: the X server's last ShmPutImage, from its send to its completion being read; 0 for none */
    int64_t x_wait_ns; /* 48: what main blocked this frame waiting for that completion */
    int64_t x_idle_ns; /* 56: from that put's send to main's next look: its sleep to the slot, mostly */
    uint64_t
        puts_skipped; /* 64: plan 008 phase 02: X puts skipped since the window opened, both segments being with the server */
} game_watch_take_t;
static_assert(sizeof(game_watch_take_t) == 72);

/* What F3 says of the last second, from the two rings. */
typedef struct game_watch_recent_t {
    float32_t interval_last_ms;          /* 0: between the last two pictures main took: what is on the screen */
    float32_t interval_p99_ms;           /* 4 */
    float32_t interval_most_ms;          /* 8 */
    float32_t card_mean_ms;              /* 12 */
    float32_t card_p99_ms;               /* 16 */
    float32_t card_most_ms;              /* 20 */
    float32_t idle_mean_ms;              /* 24: the gpu thread waiting for main to take the last picture */
    float32_t x_put_mean_ms;             /* 28 */
    float32_t turn_deg_s;                /* 32: the mean over GAME_WATCH_TURN_NS */
    float32_t card_mhz;                  /* 36 */
    float32_t pass_ms[FRAME_PASS_COUNT]; /* 40: means over the second */
    uint32_t late;                       /* 60: plan 008 phase 02: pictures ready after the take they were aimed at */
    uint32_t pictures;                   /* 64 */
    uint32_t fresh_mean;                 /* 68 */
    uint64_t puts_skipped;               /* 72: X puts skipped since the window opened */
} game_watch_recent_t;
static_assert(sizeof(game_watch_recent_t) == 80);

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
    /* The gpu thread's line, read by main and the sim, relaxed, for the same reason; but pictures_drawn, release. */
    _Atomic uint32_t uploads;        /* 64: sections of the newest snapshot not yet dealt with, after the last upload */
    _Atomic uint32_t far_jobs;       /* 68: far-field columns on workers, the same */
    _Atomic uint64_t pictures_drawn; /* 72: pictures written to borrowed_pictures */
    int64_t clock_asked_ns;          /* 80: when the card's clock speed was last asked for */
    int64_t last_start_ns;           /* 88: when the last picture began; 0 before one */
    float32_t last_yaw;              /* 96: its camera */
    float32_t last_pitch;            /* 100 */
    float32_t card_mhz;              /* 104: the last reading */
    uint8_t _pad1[4];                /* 108 */
    uint64_t start_frame;            /* 112: main's frames when the picture being drawn began */
    uint8_t _pad6[8];                /* 120 */
    /* Main's line, but that the sim takes the span's frames from it, each word by exchange. A frame's numbers landing either side of
     * the exchange, or a slowest lost to it, puts a frame in the next span, which only the log depends on. */
    _Atomic int64_t span_frame_ns;      /* 128: the frames of this span, added up */
    _Atomic int64_t span_frame_most_ns; /* 136: the slowest of them */
    _Atomic uint32_t span_frames;       /* 144 */
    uint32_t compose_us;                /* 148: what drawing on the last picture took (moved from game_t, plan 008) */
    int64_t frame_said_ns;              /* 152: when a slow frame was last said; 0 for never */
    uint64_t frames_unsaid;             /* 160: slow frames since then, not said */
    _Atomic uint64_t takes_made;        /* 168: takes written to borrowed_takes */
    _Atomic uint64_t
        main_frames;     /* 176: main's frames of play: the gpu thread reads it, relaxed, as it begins a picture */
    uint32_t present_us; /* 184: what copying it out took */
    uint8_t _pad2[4];    /* 188 */
    /* Main's too: what the window last said of its puts (game_watch_display_note), and the last take. */
    int64_t x_put_sent_ns;  /* 192: the last ShmPutImage's send */
    int64_t x_put_done_ns;  /* 200: its completion read; 0 while it is out */
    int64_t x_wait_from_ns; /* 208: when main last began to wait for one */
    int64_t x_wait_ns;      /* 216: and what it blocked */
    int64_t last_taken_ns;  /* 224: 0 before a take */
    uint64_t
        x_puts_skipped; /* 232: plan 008 phase 02: the window's puts skipped, both segments being with the server */
    uint8_t _pad3[16];  /* 240 */
    /* The gpu thread's, read by main after a take: GAME_WATCH_SLOTS are the exchange's. */
    game_watch_slot_t slots[RENDER_EXCHANGE_SLOTS]; /* 256 */
    uint8_t _pad4[56];                              /* 328 */
    /* The sim's, or whoever reads the rings with no threads: the frame log and the minute's pictures. */
    uint64_t takes_read;      /* 384: takes the log and the minute have had */
    uint64_t takes_lost;      /* 392: overwritten before they were read */
    uint64_t log_used;        /* 400: bytes waiting in borrowed_log_text */
    int64_t log_written_ns;   /* 408: when the log was last written */
    file_t log_file;          /* 416: fd -1 for no log */
    uint32_t minute_pictures; /* 424 */
    uint32_t minute_late;     /* 428 */
    int64_t minute_from_ns;   /* 432: the first take of the minute's */
    int64_t read_taken_ns;    /* 440: the last take read, for its interval */
    /* Set as the world opens, before any thread, and only read after. */
    game_watch_picture_t *borrowed_pictures; /* 448: GAME_WATCH_RING */
    game_watch_take_t *borrowed_takes;       /* 456: GAME_WATCH_RING */
    char *borrowed_log_text;                 /* 464: GAME_WATCH_LOG_BYTES, or nullptr with no log */
    frame_marks_t *borrowed_marks;           /* 472: or nullptr if the card would not make them */
    int64_t opened_ns;                       /* 480: the frame log's times are from here */
    uint8_t log_failed;                      /* 488: a write failed, and the log has stopped */
    uint8_t _pad5[23];                       /* 489 */
    uint32_t minute_bins[GAME_WATCH_BINS];   /* 512: the sim's: the minute's intervals on the screen, by whole ms */
} game_watch_t;
static_assert(sizeof(game_watch_t) == 512u + 4u * GAME_WATCH_BINS);
static_assert(alignof(game_watch_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, uploads) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, span_frame_ns) == 2u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, x_put_sent_ns) == 3u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, slots) == 4u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, takes_read) == 6u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, borrowed_pictures) == 7u * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_watch_t, minute_bins) == 8u * MC_CPU_CACHE_LINE_BYTES);

/* What a world's watch takes of its World arena past the watch itself. */
#define GAME_WATCH_ARENA_BYTES                                                                                         \
    (1ull * GAME_WATCH_RING * (sizeof(game_watch_picture_t) + sizeof(game_watch_take_t)) + GAME_WATCH_LOG_BYTES +      \
     sizeof(frame_marks_t) + 4096ull)

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

/* Plan 008 phase 01. As a world opens, after game_watch_start and once the card is up: the rings and the marks from arena, and the frame
 * log at log_path opened to add to, with its header if it is new, unless log_path is nullptr. Nothing it cannot have stops the world:
 * it says so, and goes without. */
void game_watch_open(game_watch_t *watch, arena_t *arena, const char *log_path);

/* As the world closes, with no threads: what is left of the log written, the file closed and the marks given back. */
void game_watch_close(game_watch_t *watch);

/* Whoever draws, before each picture: the marks for the frame, with the card's clock asked for if it is time. */
void game_watch_marks_arm(game_watch_t *watch, frame_gpu_t *gpu, int64_t now_ns);

/* The gpu thread's: a picture begins. waited_ns is when the last one was done. Returns its record to stamp, which it alone writes. */
game_watch_picture_t *game_watch_picture_begin(game_watch_t *watch, int64_t waited_ns, int64_t start_ns);

/* The gpu thread's: a part of picture's way has ended now. Nothing for nullptr. */
void game_watch_stamp(game_watch_picture_t *picture, game_watch_stamp_t stamp);

/* The gpu thread's, before the draw that publishes into slot: what main is handed with the picture. */
void game_watch_slot_note(game_watch_t *watch, uint32_t slot, const game_watch_picture_t *picture);

/* The gpu thread's, after the draw: the card's numbers, the turn from the last picture, and the count. Returns the time it was done. */
int64_t game_watch_picture_end(game_watch_t *watch, game_watch_picture_t *picture, const frame_timings_t *timings,
                               const render_camera_t *camera);

/* Main's, once a frame of play, before it takes. */
void game_watch_main_frame(game_watch_t *watch);

/* Main's, on taking the picture in slot. */
void game_watch_take_note(game_watch_t *watch, uint32_t slot, int64_t taken_ns);

/* Main's, before the frame: what the window said of its last put and of main's wait for it. Nothing comes under KMS. */
void game_watch_display_note(game_watch_t *watch, int64_t put_sent_ns, int64_t put_done_ns, int64_t wait_from_ns,
                             int64_t wait_ns, uint64_t puts_skipped);

/* The sim's, once a tick, or whoever has no threads: the takes since the last into the minute's numbers and the log's lines, which are
 * written when there are enough or it is time. final writes all there is, and is only for no threads. */
void game_watch_rings_read(game_watch_t *watch, int64_t now_ns, bool final);

/* Main's: the last second, from the rings. False with nothing in it. */
bool game_watch_recent_read(const game_watch_t *watch, int64_t now_ns, game_watch_recent_t *out);

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
