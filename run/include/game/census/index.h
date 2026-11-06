/* See run/src/game/census/README.md. */
#ifndef MC_RUN_GAME_CENSUS_INDEX_H
#define MC_RUN_GAME_CENSUS_INDEX_H

#include "arena/index.h"
#include "climate/index.h"
#include "climate/outlook/index.h"
#include "climate/weather/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "types/index.h"
#include "ui/screen_census/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 07: a census run from the sim. Phase 01's trigger owes weeks; this works them out on the workers, one week at
 * a time, oldest first, each week's tiles in blocks, then flow accumulation, then the outlook and the report, then the file.
 * The sim polls; nothing here ever waits for a job. */

enum : uint32_t {
    /* Tiles round a batch's that are built before it runs: a day list reads normals 4.2 km out for cells, plus the read reach. */
    GAME_CENSUS_PREPARE_TILES = 6u,
    /* A batch's tiles fit in a square this many tiles across, so it and its prepare margin fit the climate's keep square. */
    GAME_CENSUS_BATCH_TILES = 32u,
    GAME_CENSUS_BATCHES_MAX = 4096u,
    GAME_CENSUS_REPORTS = 3u, /* the triple buffer from the sim to main */
};
enum : uint64_t {
    /* What game_census_open takes from the World arena: the census, a job per block, the orders, the outlook's input and the
     * reports, with room for alignment. */
    GAME_CENSUS_ARENA_BYTES = 2ull << 20,
};

typedef enum game_census_stage_t : uint8_t {
    GAME_CENSUS_IDLE = 0,
    GAME_CENSUS_PREPARE = 1, /* a batch's climate tiles are being built */
    GAME_CENSUS_BLOCKS = 2,  /* its blocks' jobs are in */
    GAME_CENSUS_FLOW = 3,    /* the week's flow accumulation */
    GAME_CENSUS_OUTLOOK = 4, /* the outlook and the report */
    GAME_CENSUS_WRITE = 5,   /* the file */
} game_census_stage_t;

typedef struct game_census_t game_census_t;

/* One block's job: its own cache line, as a worker writes done while the sim polls it. */
typedef struct game_census_job_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) climate_week_block_t block; /* 0 */
    game_census_t *borrowed_census;                              /* 80 */
    _Atomic uint32_t done;                                       /* 88: release when finished */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 28];                 /* 92 */
} game_census_job_t;
static_assert(sizeof(game_census_job_t) == 2u * MC_CPU_CACHE_LINE_BYTES);

/* A census's state: the sim's, but for done flags and the report buffers, whose owners are said. In the game's World arena. */
typedef struct game_census_t {
    job_group_t group;                       /* 0: every census job */
    climate_t *borrowed_climate;             /* 64 */
    job_system_t *borrowed_job;              /* 72 */
    const char *borrowed_directory;          /* 80: <world>, or nullptr for no file */
    game_census_job_t *borrowed_jobs;        /* 88: CLIMATE_WEEK_TILES_MAX */
    uint32_t *borrowed_sorted;               /* 96: CLIMATE_WEEK_TILES_MAX: tiles in block order */
    uint32_t *borrowed_scratch;              /* 104: the same */
    uint32_t *borrowed_batches;              /* 112: GAME_CENSUS_BATCHES_MAX + 1: each batch's first job */
    climate_outlook_input_t *borrowed_input; /* 120: the outlook job's */
    ui_census_report_t *borrowed_reports;    /* 128: GAME_CENSUS_REPORTS, the triple buffer */
    ui_census_report_t *borrowed_work;       /* 136: the sim's, and the outlook job's while it runs */
    uint64_t seed;                           /* 144 */
    uint64_t next_week;                      /* 152: the oldest week owed, or 0 for none */
    uint64_t last_week;                      /* 160: the newest week owed */
    uint64_t fast_until;                     /* 168: weeks up to this run fast; 0 for none */
    uint64_t week;                           /* 176: the week being worked */
    uint64_t first_week;                     /* 184: the first week of this run */
    uint64_t runs;                           /* 192: censuses finished since the world opened */
    float64_t player_x;                      /* 200: where the outlook and the events are for */
    float64_t player_y;                      /* 208 */
    int64_t started_ns;                      /* 216: when this run began, for the log */
    uint32_t job_count;                      /* 224 */
    uint32_t batch_count;                    /* 228 */
    uint32_t batch;                          /* 232 */
    uint32_t submitted;                      /* 236: of this batch's jobs */
    uint32_t radius_blocks;                  /* 240: the render distance, for the events */
    uint32_t job_week;                       /* 244: what this batch's jobs work: set before they go in */
    uint8_t job_fast;                        /* 248 */
    uint8_t job_events;                      /* 249: the last week of a run records the report's events */
    uint8_t stage;                           /* 250: game_census_stage_t */
    uint8_t single_submitted;                /* 251: the flow, outlook or file job is in */
    uint8_t file_status;                     /* 252: a file_status_t, the file job's */
    uint8_t refused;                         /* 253: the file at open was refused */
    uint8_t prepare_begun;                   /* 254 */
    uint8_t _pad0[1];                        /* 255 */
    /* A line of its own: the single jobs write done, main and the sim swap the report buffers. */
    _Atomic uint32_t single_done;                /* 256: release when the flow, outlook or file job has finished */
    _Atomic uint32_t middle;                     /* 260: the report buffer between the two, and a bit if it is new */
    uint32_t back;                               /* 264: the sim's buffer */
    uint32_t front;                              /* 268: main's buffer */
    _Atomic uint32_t input_done;                 /* 272: release when the outlook's input job has finished */
    uint32_t input_known;                        /* 276: it could be filled */
    uint64_t input_week;                         /* 280: the week it was filled for; 0 for none */
    uint8_t input_submitted;                     /* 288: the sim's */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 33]; /* 289 */
} game_census_t;
static_assert(sizeof(game_census_t) == 320);
static_assert(alignof(game_census_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(game_census_t, single_done) == 256);

/* Takes what it needs from arena (World), loads the weekly state saved at or before last_census_week from directory (nullptr
 * for none), and owes the weeks between that file and last_census_week, if a crash left any. Not on a worker. */
void game_census_open(game_census_t *census, arena_t *arena, climate_t *climate, job_system_t *job,
                      const char *directory, uint64_t seed, uint64_t last_census_week);

/* Waits for its jobs. Not on a worker. Safe on one that never opened. */
void game_census_close(game_census_t *census);

/* Phase 01's hook: a census owes this week. The sim, from the tick's trigger, in order. context is the census. */
void game_census_week(void *context, uint64_t week, bool fast);

/* One step of the sim's: moves the census on without waiting. player_x, player_y and radius_blocks are where the report is
 * for and how far its events reach. */
void game_census_step(game_census_t *census, float64_t player_x, float64_t player_y, uint32_t radius_blocks);

/* Whether nothing is owed and nothing runs. The sim, or with it stopped. */
bool game_census_idle(const game_census_t *census);

/* Whether the census is building climate tiles, so the sim must not move the climate's keep square meanwhile. The sim. */
bool game_census_quiet(const game_census_t *census);

/* Main's: the newest report the sim has published. Good until the next call. Never nullptr. */
const ui_census_report_t *game_census_report(game_census_t *census);

/* ---- Wiring into a game ---------------------------------------------------------------------------------------------- */

typedef struct game_t game_t;

/* At the end of game_open, with the save's calendar applied: a world with a climate and no other census hook gets a census
 * (in the World arena), hooked to phase 01's trigger, and any --climate-jump moves the clock on now. */
void game_census_attach(game_t *game);
/* In game_close, before the climate closes: waits for its jobs and unhooks it. */
void game_census_detach(game_t *game);
/* The game's census, or nullptr if this world has none. Any thread, once the world is open. */
game_census_t *game_census_of(const game_t *game);
/* The sim's step: the census's step for where the body or eye is. True if the climate's keep square must stay put. */
bool game_census_sim(game_t *game);
/* Whether the game has no census, or it is idle: what a headless session steps the sim until. */
bool game_census_settled(const game_t *game);
/* The sim, as a column commits: its 64-block cell holds generated land. */
void game_census_mark(game_t *game, int32_t chunk_x, int32_t chunk_y);

#endif
