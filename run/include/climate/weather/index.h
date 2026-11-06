/* See run/src/climate/weather/README.md. */
#ifndef MC_RUN_CLIMATE_WEATHER_INDEX_H
#define MC_RUN_CLIMATE_WEATHER_INDEX_H

#include "arena/index.h"
#include "calendar/index.h"
#include "climate/index.h"
#include "climate/systems/index.h"
#include "climate/weather/flow/index.h"
#include "climate/weather/week/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Plan 004 phase 07: each day's weather at any place (pure: normals, weather systems and smooth noise), and the weekly
 * state of every kept cell (the census's: snow, soil water, runoff, ice and the flood index). Numbers from the phase 07
 * file's table; f_sys is phase 06's (climate/systems), the melt rate phase 05's (climate/season), the full-week count
 * phase 01's (CALENDAR_FULL_WEEKS_MAX). */

static const float64_t CLIMATE_SPELL_BLOCKS = 2500.0f64;       /* 7.4: N_1 over x / 2500, y / 2500 */
static const float64_t CLIMATE_SPELL_DAYS = 10.0f64;           /* 7.4: and D / 10 */
static const float64_t CLIMATE_RAIN_AMOUNT_BLOCKS = 2000.0f64; /* phase 07: N_2 over x / 2000, y / 2000 */
static const float64_t CLIMATE_RAIN_AMOUNT_DAYS = 3.0f64;      /* phase 07: and D / 3 */
static const float64_t CLIMATE_RAIN_HOURS_BLOCKS = 1500.0f64;  /* phase 07: N_3 over x / 1500, y / 1500 */
static const float64_t CLIMATE_RAIN_HOURS_HOURS = 6.0f64;      /* phase 07: and (24 D + h) / 6 */
static const float32_t CLIMATE_SIGMA_T_COAST = 2.0f32;         /* 7.4: sigma_T = 2 + 3 kappa */
static const float32_t CLIMATE_SIGMA_T_INLAND_EXTRA = 3.0f32;
static const float32_t CLIMATE_HIGH_ANOMALY_SIGMAS = 0.5f32; /* 7.4: a high's +-0.5 sigma_T at its centre */
static const float32_t CLIMATE_CLOUD_RANGE_CUT = 0.6f32;     /* 7.4: share of the daily range clouds remove */
static const float32_t CLIMATE_RAIN_NOISE_GAIN = 0.9f32;     /* phase 07: g = max(0, 1 + 0.9 N_2) */
static const float32_t CLIMATE_SPELL_CLAMP = 3.0f32;         /* phase 07, Issues found 2: each noise clamped to +-3 */
/* Phase 07 step 2: each field's raw standard deviation, measured over 100,000 points (see README.md), so dividing by it
 * gives a spread of 1. tests/run/climate/weather measures them again and holds them within 3%. */
static const float32_t CLIMATE_SPELL_RAW_SD = 0.2013f32;
static const float32_t CLIMATE_RAIN_AMOUNT_RAW_SD = 0.2008f32;
static const float32_t CLIMATE_RAIN_HOURS_RAW_SD = 0.2015f32;

enum : uint32_t {
    CLIMATE_HOURS_A_DAY = 24u,
    CLIMATE_WEEK_SPINUP_YEARS = 2u,
    CLIMATE_WEEK_SPINUP_WEEKS = 104u,  /* 2 years of 52 weeks */
    CLIMATE_FLOOD_MARGIN_TILES = 2u,   /* phase 07: tiles kept round generated land, for drainage */
    CLIMATE_WEEK_TILES_MAX = 8192u,    /* phase 07: 8192 km^2 */
    CLIMATE_WEEK_TABLE_SLOTS = 16384u, /* the hash table: twice the tiles, a power of two */
    CLIMATE_WEEK_PENDING_MAX = 65536u, /* cells marked while a census runs, kept for after it */
    CLIMATE_WEEK_HURRICANES = 4u,      /* hurricanes remembered per tile, for the report */
    CLIMATE_WEEK_TITLE_BYTES = 24u,
    CLIMATE_WEEK_INSTANTS = 26u, /* a day's 24 hours, then 12:00, then 06:00 */
    CLIMATE_WEEK_INSTANT_NOON = 24u,
    CLIMATE_WEEK_INSTANT_MORNING = 25u,
    CLIMATE_WEEK_VIEW_SYSTEMS = 64u,  /* big systems alive near one block at one instant: a bug past it */
    CLIMATE_WEEK_VIEW_THUNDER = 512u, /* cells alive near one block at one instant */
    CLIMATE_WEEK_TILE_GENERATED = 1u, /* a cell of this tile holds generated land */
    CLIMATE_WEEK_TILE_MARGIN = 2u,    /* within CLIMATE_FLOOD_MARGIN_TILES of one that does: kept for drainage */
    CLIMATE_WEEK_TILE_FLAGS = 3u,     /* every flag a saved tile may carry */
};
static_assert((CLIMATE_WEEK_TABLE_SLOTS & (CLIMATE_WEEK_TABLE_SLOTS - 1u)) == 0);
static_assert(CLIMATE_WEEK_TABLE_SLOTS >= 2u * CLIMATE_WEEK_TILES_MAX);

/* What a cell saw last full week, for the census report's "events within render distance": bits of side.events. */
enum : uint8_t {
    CLIMATE_WEEK_EVENT_FROST = 1u << 0,
    CLIMATE_WEEK_EVENT_HEAT_WAVE = 1u << 1,
    CLIMATE_WEEK_EVENT_THUNDERSTORM = 1u << 2,
    CLIMATE_WEEK_EVENT_STORM = 1u << 3,
    CLIMATE_WEEK_EVENT_HURRICANE = 1u << 4,
    CLIMATE_WEEK_EVENT_FIRST_SNOW = 1u << 5,
    CLIMATE_WEEK_EVENT_LAKE_ICE = 1u << 6,
};

/* The weather at one place and moment, for the HUD (phase 10), what the eye sees (phase 08) and wind on the body (phase 08). */
typedef struct climate_now_t {
    float32_t temperature_c;    /* 0: at the height asked for, with the daily cycle */
    float32_t rain_mm_h;        /* 4: rain and snow as water */
    float32_t snow_share;       /* 8: 0 to 1 */
    float32_t wind[2];          /* 12: u, v, blocks a second: belts, pressure, systems */
    float32_t pressure_hpa;     /* 20 */
    float32_t cloud;            /* 24: 0 clear to 1 overcast */
    float32_t thunder;          /* 28: 0 to 1, how near and strong the nearest thunderstorm cell is */
    float32_t fog;              /* 32: 0 to 1 */
    float32_t dust;             /* 36: 0 to 1 */
    float32_t thunder_distance; /* 40: blocks to the nearest cell's centre, or 0 for none */
    uint32_t thunder_cell;      /* 44: that cell's hash id, 0 for none: what phase 08 times lightning from */
    uint8_t day_kind;           /* 48: climate_day_kind_t of the whole day */
    uint8_t in_hurricane;       /* 49 */
    uint8_t _pad0[14];          /* 50 */
} climate_now_t;
static_assert(sizeof(climate_now_t) == 64);
static_assert(alignof(climate_now_t) == 4);
static_assert(offsetof(climate_now_t, temperature_c) == 0);
static_assert(offsetof(climate_now_t, rain_mm_h) == 4);
static_assert(offsetof(climate_now_t, snow_share) == 8);
static_assert(offsetof(climate_now_t, wind) == 12);
static_assert(offsetof(climate_now_t, pressure_hpa) == 20);
static_assert(offsetof(climate_now_t, cloud) == 24);
static_assert(offsetof(climate_now_t, thunder) == 28);
static_assert(offsetof(climate_now_t, fog) == 32);
static_assert(offsetof(climate_now_t, dust) == 36);
static_assert(offsetof(climate_now_t, thunder_distance) == 40);
static_assert(offsetof(climate_now_t, thunder_cell) == 44);
static_assert(offsetof(climate_now_t, day_kind) == 48);
static_assert(offsetof(climate_now_t, in_hurricane) == 49);
static_assert(offsetof(climate_now_t, _pad0) == 50);

/* One tile of weekly state: 256 cells, and which of them hold generated land. Saved as it is. */
typedef struct climate_week_tile_t {
    int32_t tile_x;                                    /* 0: in tiles of 1024 blocks */
    int32_t tile_y;                                    /* 4 */
    uint32_t week;                                     /* 8: the census week this state is the end of */
    uint32_t flags;                                    /* 12: CLIMATE_WEEK_TILE_* */
    uint64_t generated[CLIMATE_TILE_CELL_COUNT / 64u]; /* 16: one bit per cell, y * 16 + x */
    uint8_t _pad0[16];                                 /* 48 */
    climate_week_t cells[CLIMATE_TILE_CELL_COUNT];     /* 64: y * 16 + x */
} climate_week_tile_t;
static_assert(sizeof(climate_week_tile_t) == 16448);
static_assert(alignof(climate_week_tile_t) == 8);
static_assert(offsetof(climate_week_tile_t, tile_x) == 0);
static_assert(offsetof(climate_week_tile_t, tile_y) == 4);
static_assert(offsetof(climate_week_tile_t, week) == 8);
static_assert(offsetof(climate_week_tile_t, flags) == 12);
static_assert(offsetof(climate_week_tile_t, generated) == 16);
static_assert(offsetof(climate_week_tile_t, _pad0) == 48);
static_assert(offsetof(climate_week_tile_t, cells) == 64);

/* Beside each tile, never saved: it comes back from the seed. Written only by the job working that tile, or by the flow job
 * when no tile job runs. */
typedef struct climate_week_side_t {
    float32_t mean_runoff[CLIMATE_TILE_CELL_COUNT]; /* 0: the spin-up's long-run weekly runoff, mm */
    float32_t z_ground[CLIMATE_TILE_CELL_COUNT];    /* 1024: phase 03's cell height */
    uint8_t sea[CLIMATE_TILE_CELL_COUNT];           /* 2048: 1 for a sea cell, always an outlet */
    uint8_t events[CLIMATE_TILE_CELL_COUNT];        /* 2304: CLIMATE_WEEK_EVENT_* of the last full week */
    char hurricanes[CLIMATE_WEEK_HURRICANES][CLIMATE_WEEK_TITLE_BYTES]; /* 2560: over this tile last full week */
    uint64_t hurricane_ids[CLIMATE_WEEK_HURRICANES];                    /* 2656 */
    uint32_t hurricane_count;                                           /* 2688 */
    uint8_t ground_ready;                                               /* 2692: z_ground and sea are filled */
    uint8_t mean_ready;                                                 /* 2693: mean_runoff is filled */
    uint8_t fresh;    /* 2694: the cells are not yet at spin-up: the next census starts them */
    uint8_t _pad0[9]; /* 2695 */
} climate_week_side_t;
static_assert(sizeof(climate_week_side_t) == 2704);

/* A block's week worked by day on many workers: the block's reads, then its 7 days each on its own, then the update. */
typedef struct climate_week_work_t climate_week_work_t;
enum : uint32_t {
    CLIMATE_WEEK_WORK_SLOTS = 32u, /* blocks worked by day at once, each with one of the store's works */
};

/* The kept tiles: a hash table of up to CLIMATE_WEEK_TILES_MAX, which only grows. Tiles are in the order they were added,
 * which can differ from run to run; whatever reads them in order sorts them (climate_week_store_sorted). */
typedef struct climate_week_store_t {
    climate_week_tile_t *borrowed_tiles; /* 0: CLIMATE_WEEK_TILES_MAX */
    climate_week_side_t *borrowed_sides; /* 8: the same */
    uint32_t *borrowed_table;            /* 16: CLIMATE_WEEK_TABLE_SLOTS: tile index + 1, 0 for empty */
    climate_flow_cell_t *borrowed_flow;  /* 24: CLIMATE_WEEK_TILES_MAX x 256, the flow job's */
    uint32_t *borrowed_flow_index;       /* 32: the same count, twice: order and the sort's scratch */
    int32_t *borrowed_pending;           /* 40: CLIMATE_WEEK_PENDING_MAX cells, x then y */
    climate_week_work_t *borrowed_work;  /* 48: CLIMATE_WEEK_WORK_SLOTS, each the one block's that holds it */
    uint64_t week;                       /* 56: the census week every tile is at; 0 before any */
    uint32_t tile_count;                 /* 64 */
    uint32_t pending_count;              /* 68 */
    uint8_t busy;                        /* 72: a census runs: marks wait in pending */
    uint8_t full;                        /* 73: a tile could not be added: CLIMATE_WEEK_TILES_MAX was reached */
    uint8_t pending_lost;                /* 74: pending overflowed: those cells are marked when next seen */
    uint8_t _pad0[5];                    /* 75 */
} climate_week_store_t;
static_assert(sizeof(climate_week_store_t) == 80);

/* The weather's part of the climate, at the start of the climate's week region: what climate_open seeds. */
typedef struct climate_weather_t {
    arena_t arena_week;                  /* 0: over the rest of the week region; World: reset at each climate_open */
    terrain_noise_bank_t bank;           /* 64: the three noises */
    const terrain_noise_field_t *spell;  /* 88: N_1 */
    const terrain_noise_field_t *amount; /* 96: N_2 */
    const terrain_noise_field_t *hours;  /* 104: N_3 */
    climate_week_store_t store;          /* 112 */
} climate_weather_t;
static_assert(sizeof(climate_weather_t) == 192);
static_assert(alignof(climate_weather_t) == 64);

/* What the week region holds, laid out in this order after climate_weather_t; the Owner reserves the sum. */
enum : uint64_t {
    CLIMATE_WEEK_REGION_BYTES = 320ull << 20,
    /* What one work takes of the region at most: its arrays for a block's cells and 7 days (weather/index.c asserts it). */
    CLIMATE_WEEK_WORK_BYTES = 1ull << 20,
};
static_assert((uint64_t)CLIMATE_WEEK_TILES_MAX * (sizeof(climate_week_tile_t) + sizeof(climate_week_side_t) +
                                                  CLIMATE_TILE_CELL_COUNT * (sizeof(climate_flow_cell_t) + 8u)) +
                  CLIMATE_WEEK_TABLE_SLOTS * 4u + CLIMATE_WEEK_PENDING_MAX * 8u +
                  CLIMATE_WEEK_WORK_SLOTS * CLIMATE_WEEK_WORK_BYTES + (1u << 20) <=
              CLIMATE_WEEK_REGION_BYTES);

/* One block of tiles worked in one job: up to 2 by 2 tiles, which share one list of weather systems a day. */
enum : uint32_t {
    CLIMATE_WEEK_BLOCK_TILES = 4u,
    CLIMATE_WEEK_BLOCK_SIDE = 2u,
};

/* What a block's job did, for the census and the bench. Written only by that job. */
typedef struct climate_week_block_t {
    uint32_t slots[CLIMATE_WEEK_BLOCK_TILES]; /* 0: the store's tile indices, in (tile_y, tile_x) order */
    uint32_t count;                           /* 16 */
    uint32_t incomplete;                      /* 20: days whose cells were not all found (a tile not built): a bug */
    uint32_t overflowed;                      /* 24: day lists or views that ran out of room: a bug */
    uint32_t failed;                          /* 28: a read that found its climate tile not built: a bug */
    uint64_t ns_lists;                        /* 32: building the day lists */
    uint64_t ns_cells;                        /* 40: everything else, which is the four below and the update */
    uint64_t ns_spinup;                       /* 48: side data and spin-up of tiles that lack them */
    uint64_t ns_reads;                        /* 56: the normals and the belt wind of each cell and day */
    uint64_t ns_noise;                        /* 64: the three noises, in bulk */
    uint64_t ns_days;                         /* 72: each day of each cell from its inputs and views */
} climate_week_block_t;
static_assert(sizeof(climate_week_block_t) == 80);

/* The first hurricanes over each tile of a block on one day, each once, in the order of the cells. The week's names are the
 * days' in day order, each once, the first CLIMATE_WEEK_HURRICANES of them: a day's first CLIMATE_WEEK_HURRICANES are
 * enough, as every id seen before it is one of the names already. */
typedef struct climate_week_notes_t {
    uint64_t ids[CLIMATE_WEEK_HURRICANES];                          /* 0 */
    char titles[CLIMATE_WEEK_HURRICANES][CLIMATE_WEEK_TITLE_BYTES]; /* 32: the name at its first hour over the tile */
    uint32_t count;                                                 /* 128 */
    uint8_t _pad0[4];                                               /* 132 */
} climate_week_notes_t;
static_assert(sizeof(climate_week_notes_t) == 136);

/* What one day of a block did, written only by that day's job. */
typedef struct climate_week_day_stats_t {
    uint32_t incomplete;                                  /* 0: as climate_week_block_t's */
    uint32_t overflowed;                                  /* 4 */
    uint32_t failed;                                      /* 8 */
    uint8_t _pad0[4];                                     /* 12 */
    uint64_t ns_lists;                                    /* 16 */
    uint64_t ns_reads;                                    /* 24 */
    uint64_t ns_noise;                                    /* 32 */
    uint64_t ns_days;                                     /* 40 */
    uint64_t ns_total;                                    /* 48: the day's whole call */
    climate_week_notes_t notes[CLIMATE_WEEK_BLOCK_TILES]; /* 56 */
} climate_week_day_stats_t;
static_assert(sizeof(climate_week_day_stats_t) == 600);

/* A place as the days read it: weather/index.c's. */
typedef struct climate_week_place_t climate_week_place_t;

/* One block's week in flight: its cells' places and normals from climate_week_block_begin, each day's results from that
 * day's climate_week_block_day, all read by climate_week_block_end. Arrays in the week region, CLIMATE_WEEK_WORK_BYTES. */
typedef struct climate_week_work_t {
    climate_week_place_t *borrowed_places;             /* 0: CLIMATE_WEEK_BLOCK_TILES x 256 cells */
    climate_normals_at_t *borrowed_normals;            /* 8: the cells x 7 days, cell by cell */
    climate_day_t *borrowed_days;                      /* 16: the same */
    bool *borrowed_placed;                             /* 24: the cells: place and normals were read */
    climate_week_day_stats_t stats[CLIMATE_WEEK_DAYS]; /* 32 */
    uint64_t ns_begin;                                 /* 4232: climate_week_block_begin's whole call */
    uint8_t _pad0[16];                                 /* 4240 */
} climate_week_work_t;
static_assert(sizeof(climate_week_work_t) == 4256);
static_assert(offsetof(climate_week_work_t, stats) == 32);
static_assert(offsetof(climate_week_work_t, ns_begin) == 4232);

/* ---- The weather's own region: the Owner's calls --------------------------------------------------------------------- */

/* Lays the weather out over a whole region of CLIMATE_WEEK_REGION_BYTES (the Owner's week region), seeds the three noises
 * from the world seed and empties the store. False, having logged why, only if a noise cannot be made. */
[[nodiscard]] bool climate_weather_open(climate_weather_t *weather, uint8_t *region, uint64_t bytes, uint64_t seed);
/* Forgets the world. */
void climate_weather_close(climate_weather_t *weather);

/* ---- Daily weather: pure --------------------------------------------------------------------------------------------- */

/* t of the systems (D + f, f the share of the calendar day since 06:00) at a clock reading. Pure. */
float64_t climate_weather_time_of_clock(uint32_t day_zero, calendar_clock_t clock);

/* The three noises at a place and time, each scaled to a spread of 1 and clamped to +-3. Pure. */
float32_t climate_weather_spell(const climate_t *climate, float64_t x, float64_t y, int64_t day);
float32_t climate_weather_amount(const climate_t *climate, float64_t x, float64_t y, int64_t day);
float32_t climate_weather_hours(const climate_t *climate, float64_t x, float64_t y, int64_t day, uint32_t hour);

/* The day's weather at a place, given last week's state there (for fog and dust), or nullptr for a place with none. False,
 * writing nothing, if a climate tile it reads is not built or scratch is too small (it needs about 2 MiB). Pure. */
[[nodiscard]] bool climate_weather_day(const climate_t *climate, int32_t x, int32_t y, int64_t day,
                                       const climate_week_t *last_week, arena_t *scratch, climate_day_t *out);

/* The weather at a place, height and moment t (climate_weather_time_of_clock). Its rain is the hour's rate the day's total
 * adds up: the rates of the day's 24 half-past instants sum to climate_weather_day's rain_mm. False as above. Pure. */
[[nodiscard]] bool climate_weather_at(const climate_t *climate, float64_t x, float64_t y, float64_t z, float64_t t,
                                      const climate_week_t *last_week, arena_t *scratch, climate_now_t *out);

/* ---- The weekly state ------------------------------------------------------------------------------------------------ */

/* A cell's state at the end of census week `week` from normals alone: CLIMATE_WEEK_SPINUP_WEEKS weekly updates from nothing,
 * no systems and no noise, each at its week's middle day. And its long-run mean weekly runoff: the second year of the same
 * from week 1, so it does not depend on the week. Either output may be nullptr. False if a tile is not built. Pure. */
[[nodiscard]] bool climate_week_spinup(const climate_t *climate, int32_t cell_x, int32_t cell_y, uint64_t week,
                                       climate_week_t *out_state, float32_t *out_mean_runoff);

/* The fast week of section 4.2: snow, soil and runoff only, from the week's middle day: normals plus the spell and amount
 * noises, no systems. False if a tile is not built. Pure. */
[[nodiscard]] bool climate_week_fast(const climate_t *climate, int32_t cell_x, int32_t cell_y, uint64_t week,
                                     climate_week_t *state);

/* The store this climate keeps. */
climate_week_store_t *climate_week_store(const climate_t *climate);

/* The tile holding (tile_x, tile_y), or nullptr. Its index into borrowed_tiles through out_index, if not nullptr. */
climate_week_tile_t *climate_week_store_find(const climate_week_store_t *store, int32_t tile_x, int32_t tile_y,
                                             uint32_t *out_index);
/* The tile, added empty and fresh with flags if new (nullptr if the store is full). The sim, or with nothing else on it. */
climate_week_tile_t *climate_week_store_add(climate_week_store_t *store, int32_t tile_x, int32_t tile_y, uint32_t flags,
                                            uint32_t *out_index);
/* The tile a reader wrote in place at borrowed_tiles[tile_count] joins the store as it is, not fresh (its side data comes
 * back at the next census). False if the store is full or holds that tile already. */
[[nodiscard]] bool climate_week_store_adopt(climate_week_store_t *store);
/* A block's 64-block cell holds generated land: sets its bit, adding its tile and every tile within
 * CLIMATE_FLOOD_MARGIN_TILES. While busy, the cell waits in pending. The sim's. */
void climate_week_store_mark(climate_week_store_t *store, int32_t x, int32_t y);
/* After a census: the cells marked meanwhile. The sim's. */
void climate_week_store_pending_apply(climate_week_store_t *store);
/* The tile indices in (tile_y, tile_x) order into out, with scratch, both tile_count long. Any thread, with no tile added. */
void climate_week_store_sorted(const climate_week_store_t *store, uint32_t *out, uint32_t *scratch);
/* Every tile and its side data gone. With nothing else on it. */
void climate_week_store_empty(climate_week_store_t *store);
/* The cell's state, or nullptr if its tile is not kept. */
const climate_week_t *climate_week_store_cell(const climate_week_store_t *store, int32_t cell_x, int32_t cell_y);

/* One census week for one block of up to 4 tiles, in three parts that may run on different workers, each part after the one
 * before (the 7 days in any order, or all at once): the bytes are the same however they are spread. Each reads and writes
 * only its own block's tiles, sides and work. Every climate tile within 6 km of the block must be built.
 *
 * begin: fresh tiles start at the spin-up of week - 1, side data is filled where missing, and record_events clears the
 * tiles' events. In fast mode the fast week is the whole of it: each tile's week becomes week and it returns false.
 * Otherwise it reads every cell's place and normals for the 7 days into work and returns true: the days and end follow. */
bool climate_week_block_begin(const climate_t *climate, climate_week_store_t *store, uint64_t week, bool fast,
                              bool record_events, climate_week_block_t *block, climate_week_work_t *work);
/* day: day i_day (0 to 6) of the 7 ending at the week's 06:00 (days 7 (week - 1) to 7 week - 1) for every cell, into work,
 * the day's lists and scratch from arena (about 22 MiB). */
void climate_week_block_day(const climate_t *climate, const climate_week_store_t *store, uint64_t week, uint32_t i_day,
                            arena_t *arena, const climate_week_block_t *block, climate_week_work_t *work);
/* end: each cell's update from its 7 days, and with record_events side.events and the hurricane names. Each tile's week
 * becomes week, and the days' counts and times are added to block. */
void climate_week_block_end(climate_week_store_t *store, uint64_t week, bool record_events, climate_week_block_t *block,
                            const climate_week_work_t *work);

/* Flow accumulation over every kept cell, and each cell's flood index. One thread, when no block job runs. */
void climate_week_flow_run(climate_week_store_t *store);

/* Whether a census of week on the cell at day_of_year names a first snow: the previous snow 0, now above 0, and the day in
 * the cell's autumn or early winter (4.1's labels, for its hemisphere). Pure. */
bool climate_week_first_snow(float32_t snow_before, float32_t snow_after, uint16_t day_of_year, bool south);

#endif
