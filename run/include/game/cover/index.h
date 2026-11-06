/* See run/src/game/cover/README.md. */
#ifndef MC_RUN_GAME_COVER_INDEX_H
#define MC_RUN_GAME_COVER_INDEX_H

#include "climate/index.h"
#include "climate/weather/index.h"
#include "types/index.h"
#include "world/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 08: snow and ice the census lays on loaded ground, as block edits saved like any other. At each census, every
 * loaded column, nearest the player first; and each column as it streams in. Running the rule twice changes nothing the second
 * time. On and off thresholds differ (Risks R4), so the edge of the snow does not flicker week to week. */

#define GAME_COVER_SNOW_ON_MM 20.0f32    /* mm of water: master plan 10 */
#define GAME_COVER_SNOW_OFF_MM 10.0f32   /* Risks R4 */
#define GAME_COVER_ICE_ON_CM 5.0f32      /* master plan 10 */
#define GAME_COVER_ICE_OFF_CM 2.0f32     /* Risks R4 */
#define GAME_COVER_SEA_ICE_ON_CM 30.0f32 /* Risks R5: the sea freezes only in the cold of the poles */
enum : uint32_t {
    GAME_COVER_COLUMNS_PER_STEP = 64u, /* columns a sim step covers during the census */
};

/* What the week left at one block column: snow on the ground as water, ice on still water, and whether it is a sea cell. */
typedef struct game_cover_week_t {
    float32_t snow_mm; /* 0 */
    float32_t ice_cm;  /* 4 */
    uint8_t sea;       /* 8 */
    uint8_t known;     /* 9: 0 if no kept cell is near: nothing is changed there */
    uint8_t _pad0[2];  /* 10 */
} game_cover_week_t;
static_assert(sizeof(game_cover_week_t) == 12);

/* Where the week's numbers for a block column come from: the climate's store in the game, a fixed week in a test. */
typedef void game_cover_week_fn(void *context, int32_t x, int32_t y, game_cover_week_t *out);

/* What one pass or one column did. */
typedef struct game_cover_counts_t {
    uint64_t columns;  /* 0: chunk columns covered */
    uint64_t snow_on;  /* 8: layers laid */
    uint64_t snow_off; /* 16: layers gone */
    uint64_t ice_on;   /* 24: water frozen */
    uint64_t ice_off;  /* 32: ice melted */
} game_cover_counts_t;
static_assert(sizeof(game_cover_counts_t) == 40);

/* A census's pass over the loaded columns, the sim's, in the game's World arena. */
typedef struct game_cover_t {
    game_cover_counts_t counts; /* 0: of the pass running, or the last */
    uint64_t runs_seen;         /* 40: the census's finished runs when the last pass began */
    int64_t started_ns;         /* 48 */
    _Atomic uint32_t cursor;    /* 56: into the map's offsets, nearest first; the sim writes, main reads, relaxed */
    _Atomic uint32_t busy;      /* 60: the same */
    _Atomic uint32_t total;     /* 64: the offsets a pass goes through; the same */
    uint8_t _pad0[4];           /* 68 */
} game_cover_t;
static_assert(sizeof(game_cover_t) == 72);

/* The rule for one loaded chunk column, for each of its 256 block columns: snow off at or under GAME_COVER_SNOW_OFF_MM (every
 * layer in the column goes), ice on still water sources over GAME_COVER_ICE_ON_CM (GAME_COVER_SEA_ICE_ON_CM at sea), ice off at or
 * under GAME_COVER_ICE_OFF_CM where water is under it, snow on over GAME_COVER_SNOW_ON_MM on the top of a whole solid cube that is
 * not leaves, snow, ice or lava. Every change is world_edit_block_set. Adds what it did to counts. Nothing if it is not loaded.
 * The thread holding the map. */
void game_cover_column(world_t *world, game_cover_week_fn *week, void *context, int32_t chunk_x, int32_t chunk_y,
                       game_cover_counts_t *counts);

/* The game's week at a block column: the store's snow and ice, bilinear between cell centres at the point moved by phase 03's warp
 * (master plan 6.9), and the sea flag of the cell it lands in. context is the climate. */
void game_cover_week_of_climate(void *context, int32_t x, int32_t y, game_cover_week_t *out);

/* ---- Wiring into a game ---------------------------------------------------------------------------------------------- */

typedef struct game_t game_t;

/* The game's pass, beside its column jobs in the World arena, or nullptr with no world open. In game/index.c. */
game_cover_t *game_cover_of(const game_t *game);

/* The sim's step: a census that has finished starts a pass, and each step covers GAME_COVER_COLUMNS_PER_STEP more. */
void game_cover_step(game_t *game);

/* A column just committed and filled: the same rule, so it is in step with the week. The sim. */
void game_cover_stream_in(game_t *game, int32_t chunk_x, int32_t chunk_y);

/* Whether a pass is running, and how far: columns done and the most there may be. Main may ask; the numbers are the sim's,
 * read without a lock, and only a progress bar depends on them. */
bool game_cover_busy(const game_t *game, uint32_t *out_done, uint32_t *out_total);

#endif
