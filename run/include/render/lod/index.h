/* See run/src/render/lod/README.md. */
#ifndef MC_RUN_RENDER_LOD_INDEX_H
#define MC_RUN_RENDER_LOD_INDEX_H

#include "arena/index.h"
#include "climate/index.h"
#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/lod/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "render/octant/index.h"
#include "render/upload/index.h"
#include "terrain/feature/index.h"
#include "tuning/render/budget/index.h"
#include "tuning/render/lod/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* How many columns past what rays are let into are kept built: compile/config/render/lod/index.yaml. */
    RENDER_LOD_MARGIN_COLUMNS = MC_TUNE_RENDER_LOD_MARGIN_COLUMNS,
    /* Columns being built at once, across all levels: compile/config/render/budget/index.yaml. */
    RENDER_LOD_JOBS = MC_TUNE_RENDER_BUDGET_LOD_JOBS,
    /* Column heights in blocks, and in sections at the finest level: 1536 blocks in voxels of 2. */
    RENDER_LOD_HEIGHT_BLOCKS = 1536u,
    RENDER_LOD_SECTIONS_MAX = (RENDER_LOD_HEIGHT_BLOCKS >> LOD_SHIFT_MIN) / 16u,
    /* The coarsest voxel a world's ground grows anything for, as a shift: compile/config/render/lod/index.yaml says why. It must come in
     * well under a second after a change of render distance (plan 005 phase 14). */
    RENDER_LOD_FEATURES_SHIFT_MAX = MC_TUNE_RENDER_LOD_FEATURES_SHIFT_MAX,
    /* Plan 009 phase 03, the pyramid: compile/config/render/lod/index.yaml. The near guarantee in blocks, the most levels above the
     * blocks, the widest a level may be in its own columns, and how many levels are made from the blocks where they are loaded. */
    RENDER_LOD_NEAR_GUARANTEE = MC_TUNE_RENDER_LOD_NEAR_GUARANTEE,
    RENDER_LOD_LEVEL_COUNT = MC_TUNE_RENDER_LOD_LEVEL_COUNT,
    RENDER_LOD_RADIUS_COLUMNS_MAX = MC_TUNE_RENDER_LOD_RADIUS_COLUMNS_MAX,
    RENDER_LOD_OCTANT_LEVELS = MC_TUNE_RENDER_LOD_OCTANT_LEVELS,
    /* Plan 009 phase 04: what the game holds block by block is d_1 and this many chunks more, never less than the least nor more than the
     * most, nor more than the render distance: compile/config/render/lod/index.yaml. */
    RENDER_LOD_BLOCK_MARGIN_CHUNKS = MC_TUNE_RENDER_LOD_BLOCK_MARGIN_CHUNKS,
    RENDER_LOD_BLOCK_RADIUS_MIN_CHUNKS = MC_TUNE_RENDER_LOD_BLOCK_RADIUS_MIN_CHUNKS,
    RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS = MC_TUNE_RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS,
};
/* Plan 009 phase 04: the pixel error and the band dither a far field starts with: compile/config/render/lod/index.yaml. */
#define RENDER_LOD_PIXEL_ERROR MC_TUNE_RENDER_LOD_PIXEL_ERROR
#define RENDER_LOD_BAND_DITHER MC_TUNE_RENDER_LOD_BAND_DITHER
enum : uint64_t {
    /* What a level's pool gives each of its columns, and what one frame may send of finished columns: compile/config/render/budget/index.yaml.
     * The pool's numbers are in the stage file's cost section. */
    RENDER_LOD_POOL_BYTES_PER_COLUMN = (uint64_t)MC_TUNE_RENDER_BUDGET_LOD_POOL_KIB_PER_COLUMN << 10,
    RENDER_LOD_FRAME_BYTES = MC_TUNE_RENDER_BUDGET_LOD_FRAME_BYTES,
    /* What a sample may keep from one line of a column to the next: render_lod_sample_terrain's chunks of what grows, then
     * (plan 006 phase 02) the column's climate sample and a line saying it is filled. */
    RENDER_LOD_SAMPLE_FEATURE_BYTES = (sizeof(terrain_feature_cache_t) + MC_CPU_CACHE_LINE_BYTES - 1u) /
                                      MC_CPU_CACHE_LINE_BYTES * MC_CPU_CACHE_LINE_BYTES,
    RENDER_LOD_SAMPLE_CLIMATE_BYTES = (sizeof(terrain_climate_sample_t) + MC_CPU_CACHE_LINE_BYTES - 1u) /
                                      MC_CPU_CACHE_LINE_BYTES * MC_CPU_CACHE_LINE_BYTES,
    RENDER_LOD_SAMPLE_SCRATCH_BYTES =
        RENDER_LOD_SAMPLE_FEATURE_BYTES + RENDER_LOD_SAMPLE_CLIMATE_BYTES + MC_CPU_CACHE_LINE_BYTES,
};
/* Ahmed's floor, beside the YAML's min. */
static_assert(RENDER_LOD_NEAR_GUARANTEE >= 16u);
static_assert(RENDER_LOD_LEVEL_COUNT >= 1u && (uint32_t)RENDER_LOD_LEVEL_COUNT <= (uint32_t)LOD_LEVELS_MAX);
static_assert(RENDER_LOD_RADIUS_COLUMNS_MAX > RENDER_LOD_MARGIN_COLUMNS && RENDER_LOD_RADIUS_COLUMNS_MAX <= UINT8_MAX);
/* A level made from the blocks is one of the pyramid's, and the one above the last of them is made from terrain. */
static_assert(RENDER_LOD_OCTANT_LEVELS < RENDER_LOD_LEVEL_COUNT);
static_assert(RENDER_LOD_BLOCK_RADIUS_MIN_CHUNKS <= RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS);

/* One column's worth of a place in the world, as storage heights from 0 to RENDER_LOD_HEIGHT_BLOCKS - 1, which a voxel of 1 << shift blocks
 * is made from. Called on workers, so it reads only what cannot change, and writes only out and scratch: RENDER_LOD_SAMPLE_SCRATCH_BYTES,
 * 64 byte aligned, all zero at the start of each column and kept from one of its lines to the next. */
typedef void (*render_lod_sample_t)(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                                    uint8_t *scratch);

/* What may have changed from what sample says, written over out, for the same place. Also on workers, at the same time as each other, unlike sample free to read
 * what is changing. scratch is the job's, from one call to the next, 64 byte aligned and as big as was asked for, with its first 64 bytes zero to begin with. */
typedef void (*render_lod_overlay_t)(void *borrowed_context, int32_t x, int32_t y, uint16_t *out, uint8_t *scratch);

/* Which levels a world has. */
typedef struct render_lod_plan_t {
    uint8_t shifts[LOD_LEVELS_MAX]; /* 0: a voxel is 1 << shift blocks each way, finest first */
    uint8_t radii[LOD_LEVELS_MAX];  /* 6: in that level's columns, each way from the eye's */
    uint32_t level_count;           /* 12: 0 for no far field */
} render_lod_plan_t;
static_assert(sizeof(render_lod_plan_t) == 16);

/* Where each level of the pyramid starts along a ray, in blocks from the eye (master 2.2 and 4.4): from[k] is d_k, the distance past which
 * level k moves a surface by at most the pixel error, never nearer than the near guarantee. from[0] is 0, the blocks, and from[level_count + 1]
 * is where the last level's band would end. */
typedef struct render_lod_bands_t {
    float64_t from[LOD_LEVELS_MAX + 2u]; /* 0 */
    uint32_t level_count;                /* 64: levels above the blocks */
    uint8_t _pad0[4];                    /* 68 */
} render_lod_bands_t;
static_assert(sizeof(render_lod_bands_t) == 72);

/* What has happened since the far field opened. */
typedef struct render_lod_stats_t {
    uint64_t columns_built;   /* 0: on workers, and sent */
    uint64_t columns_dropped; /* 8: finished after they had left the window, or dropped before they began */
    uint64_t columns_no_room; /* 16: their level's pool had no slab for them */
    uint64_t bytes_sent;      /* 24 */
    uint64_t build_ns;        /* 32: workers' time, all of them added up */
    uint64_t budget_stops;    /* 40: frames that left finished columns waiting */
} render_lod_stats_t;
static_assert(sizeof(render_lod_stats_t) == 48);

/* One column being made on a worker. Its own cache line: a worker writes done while the gpu thread polls it. */
typedef struct render_lod_job_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) const void *borrowed_lod; /* 0: the render_lod_t, as a job has only one pointer */
    uint8_t *borrowed_staging;                                 /* 8: its encoded sections, one after another */
    int64_t build_ns;                                          /* 16 */
    int32_t chunk_x;                                           /* 24: in its level's columns */
    int32_t chunk_y;                                           /* 28 */
    int32_t top_voxel;                                         /* 32: the highest voxel with a block, or -1 */
    _Atomic uint32_t done;                                     /* 36: release when finished */
    uint8_t level;                                             /* 40 */
    uint8_t in_use;                                            /* 41: the gpu thread's */
    uint8_t stale;     /* 42: the gpu thread's: what this reads changed once it had begun */
    uint8_t dropped;   /* 43: the worker's, before done: dropped before it began, so nothing was made */
    uint8_t _pad0[20]; /* 44 */
} render_lod_job_t;
static_assert(sizeof(render_lod_job_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(render_lod_job_t) == MC_CPU_CACHE_LINE_BYTES);

/* One grid. */
typedef struct render_lod_level_t {
    world_buffers_t buffers;   /* 0 */
    uint8_t *borrowed_state;   /* 240: per slot of the table: render_lod_column_state_t */
    int16_t *borrowed_offsets; /* 248: dx and dy of each column in the table, the nearest first */
    int32_t center_x;          /* 256: the column the eye is in, in this level's columns */
    int32_t center_y;          /* 260 */
    int32_t shown_min_x;       /* 264 */
    int32_t shown_min_y;       /* 268 */
    uint32_t shown_side;       /* 272 */
    uint32_t request_cursor;   /* 276: every offset before this is built, on its way, or has no room */
    uint32_t column_count;     /* 280: side * side */
    uint32_t generation;       /* 284: counts what has been put in the table */
    int32_t top_voxel;         /* 288: the highest voxel any column here ever had, or -1 */
    uint8_t shift;             /* 292 */
    uint8_t radius;            /* 293 */
    uint8_t shown_stale;       /* 294: a column has come or gone since shown_side was worked out */
    uint8_t centered;          /* 295: center_x and center_y mean something yet */
} render_lod_level_t;
static_assert(sizeof(render_lod_level_t) == 296);

/* All of the far field. The gpu thread, or whichever one thread a run has, calls everything here. */
typedef struct render_lod_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) job_group_t group; /* 0 */
    render_lod_level_t levels[LOD_LEVELS_MAX];          /* 64 */
    render_lod_stats_t stats;                           /* 1840 */
    job_system_t *borrowed_job;                         /* 1888 */
    render_lod_job_t *borrowed_jobs;                    /* 1896: RENDER_LOD_JOBS */
    const device_block_t *borrowed_hits;                /* 1904 */
    render_lod_sample_t sample;                         /* 1912 */
    const void *borrowed_sample_context;                /* 1920 */
    uint32_t level_count;                               /* 1928 */
    uint32_t jobs_in_use;                               /* 1932 */
    render_lod_overlay_t overlay;                       /* 1936: or nullptr */
    void *borrowed_overlay_context;                     /* 1944 */
    uint64_t overlay_scratch_bytes;                     /* 1952 */
    uint8_t no_room_said;                               /* 1960 */
    uint8_t covered_pass;                               /* 1961: what is covered is asked for too */
    uint8_t _pad0[6];                                   /* 1962 */
    const world_buffers_t *borrowed_fine;               /* 1968: what level 0 has on the card, or nullptr */
    /* 1976: plan 009 phase 03, the pyramid's alone: where its levels up to RENDER_LOD_OCTANT_LEVELS are made from the blocks, or nullptr */
    render_octant_t *borrowed_octants;
    uint8_t jobs_held;                /* 1984: no column goes to a worker, as the far field rays use is busy */
    uint8_t _pad1[3];                 /* 1985 */
    uint32_t octant_pending;          /* 1988: columns the last frame could have made from octants and left */
    uint16_t *borrowed_octant_cells;  /* 1992: a level 1 column's voxels, assembled */
    uint8_t *borrowed_octant_light;   /* 2000 */
    uint8_t *borrowed_octant_staging; /* 2008: what one column encodes to, as a job's staging */
    render_octant_section_t *borrowed_octant_sections; /* 2016 */
    device_staging_t
        *borrowed_staging; /* 2024: plan 008 phase 03: the frame's, for the levels' copies; or nullptr to wait */
    /* 2032: plan 009 phase 04: the pixel error its bands are worked out by each frame, and how far a pixel's band edges move. They start as
     * the tuning file says; a test may set others. */
    float32_t pixel_error;
    float32_t band_dither; /* 2036 */
    /* 2040: the last frame's traverse_world_t band_base, or 0 before one: a column nearer than any ray of its level starts, over loaded
     * blocks, is asked for last */
    float32_t band_base;
    uint8_t _pad2[4]; /* 2044 */
} render_lod_t;
static_assert(sizeof(render_lod_t) == 2048);
static_assert(alignof(render_lod_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_lod_t, levels) == 64);
static_assert(offsetof(render_lod_t, stats) == 1840);
static_assert(offsetof(render_lod_t, level_count) == 1928);

/* The bands for this pixel error in pixels, near guarantee in blocks, camera and picture: one pixel is theta = 2 half_fov_tangent /
 * height_pixels radians, and level k, of voxels of s = 2^k blocks, may start at s / (2 pixel_error theta). RENDER_LOD_LEVEL_COUNT levels. */
render_lod_bands_t render_lod_bands(float64_t pixel_error, uint32_t near_guarantee, float64_t half_fov_tangent,
                                    uint32_t height_pixels);

/* The pyramid's levels for these bands: every level from 1 up whose band starts inside the render distance, each as wide as its band's far
 * edge (or the render distance, if nearer) plus RENDER_LOD_MARGIN_COLUMNS, up to RENDER_LOD_RADIUS_COLUMNS_MAX. No levels when level 1's band
 * starts past the render distance. */
render_lod_plan_t render_lod_plan_by_bands(const render_lod_bands_t *bands, uint32_t render_distance_chunks);

/* Plan 009 phase 04: traverse_world_t's band_base for this pixel error, camera and picture: 1 / (2 E theta), theta = 2 half_fov_tangent /
 * height_pixels, so that level k starts at band_base 2^k blocks, render_lod_bands' from[k] before its floor. */
float32_t render_lod_band_base(float32_t pixel_error, float32_t half_fov_tangent, uint32_t height_pixels);

/* Plan 009 phase 04: how far the game holds blocks, in chunks, for these bands: d_1 and RENDER_LOD_BLOCK_MARGIN_CHUNKS more, at least
 * RENDER_LOD_BLOCK_RADIUS_MIN_CHUNKS and at most RENDER_LOD_BLOCK_RADIUS_MAX_CHUNKS, and never past the render distance. */
uint32_t render_lod_block_radius(const render_lod_bands_t *bands, uint32_t render_distance_chunks);

/* Sizes, for the Owner to take memory and blocks. */
uint64_t render_lod_table_bytes(const render_lod_plan_t *plan, uint32_t level);
uint64_t render_lod_pool_bytes(const render_lod_plan_t *plan, uint32_t level);
uint64_t render_lod_arena_bytes(const render_lod_plan_t *plan);

/* From one sampled column, the voxels of one coarse column above each other: out[z * stride], with no more than
 * RENDER_LOD_HEIGHT_BLOCKS >> shift of them. A voxel is a block if at least half of the blocks in its height are: so ground comes out at
 * the height nearest the truth, neither grown nor shrunk. What it is drawn as is the highest of those, as that is the face seen from
 * outside. What has no body to speak of, a flower or a torch, counts as air. Returns the highest voxel with a block, or -1. */
int32_t render_lod_cells_from_column(const uint16_t *column, uint32_t shift, uint64_t stride, uint16_t *out);

/* A render_lod_sample_t for a world's own ground: its context is a terrain_t. All of terrain_generate_box that can be seen from far off: not
 * caves and ores, which take up to a tenth of what is left, are under the ground and cannot be told from it at this size. The chunks of what
 * grows are kept in scratch, a terrain_feature_cache_t, so a column walks each once and not once for each of its lines: the same blocks. */
void render_lod_sample_terrain(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                               uint8_t *scratch);

/* What render_lod_sample_world reads: a world's terrain, and its climate or nullptr. */
typedef struct render_lod_world_t {
    const terrain_t *borrowed_terrain; /* 0 */
    const climate_t *borrowed_climate; /* 8 */
} render_lod_world_t;
static_assert(sizeof(render_lod_world_t) == 16);

/* Plan 006 phase 02: render_lod_sample_terrain for the game, its context a render_lod_world_t. Each far column's climate
 * sample, filled once at the column's middle into scratch, is copied from the climate's tiles where built and is the fallback
 * past them; render_lod_sample_terrain's is always the fallback. No stage reads it yet, so the blocks are the same. */
void render_lod_sample_world(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out,
                             uint8_t *scratch);

/* Takes what it needs from the arena, and starts each level empty, on the card too. All of it must outlive this. tables and pools are the
 * Owner's blocks, one of each per level; hits is lod_hits_bytes of the picture. On failure logs why, and render_lod_deinit is still safe. */
gpu_status_t render_lod_init(render_lod_t *lod, arena_t *arena, device_memory_t *memory, job_system_t *job,
                             const render_lod_plan_t *plan, const device_block_t *tables, const device_block_t *pools,
                             const device_block_t *hits, render_lod_sample_t sample, const void *sample_context);

/* From now on what is sampled has this written over it. Before the first render_lod_frame. */
void render_lod_overlay_set(render_lod_t *lod, render_lod_overlay_t overlay, void *borrowed_context,
                            uint64_t scratch_bytes);

/* What level 0 has on the card, the near world's buffers, read on this same thread: a far column it holds all of, nearer than any ray of
 * that level starts, is asked for after every other, as nothing of it shows; and the levels made from octants are made from it. nullptr, as
 * at init, for none. */
void render_lod_fine_set(render_lod_t *lod, const world_buffers_t *fine);

/* What is under a column of the world, in chunks, has changed: each level makes the column it falls in again, showing the old one until the new is ready. */
void render_lod_column_changed(render_lod_t *lod, int32_t chunk_x, int32_t chunk_y);

/* Every column over this square of chunks, both ends included: for when it is known only that what changed is somewhere in it. */
void render_lod_area_changed(render_lod_t *lod, int32_t min_chunk_x, int32_t min_chunk_y, int32_t max_chunk_x,
                             int32_t max_chunk_y);

/* All of them, for when it is no longer known which. */
void render_lod_all_changed(render_lod_t *lod);

/* Waits for every job, and forgets what they made: each column one was making is wanted again, and what was shown stays shown. Then
 * nothing of it is on a worker, for render_lod_level_adopt. Safe on zero bytes. */
void render_lod_quiesce(render_lod_t *lod);

/* Which of lod's levels has this shift and radius, or UINT32_MAX for none. */
uint32_t render_lod_level_matching(const render_lod_t *lod, uint32_t shift, uint32_t radius);

/* Just after render_lod_init, before any render_lod_frame: level index of lod takes over level from_index of from, which has the same shift
 * and radius, has been quiesced, and has given lod its table and pool blocks. Every column it had built is shown as it was, and asked for
 * again only as from would have. from's own memory is only read, and may go once this returns. Anything but GPU_STATUS_OK has logged why. */
gpu_status_t render_lod_level_adopt(render_lod_t *lod, uint32_t index, const render_lod_t *from, uint32_t from_index);

/* Waits for any job still running, since its memory is about to go. Safe on zero bytes. */
void render_lod_deinit(render_lod_t *lod);

/* One frame's worth, without ever waiting for a worker: each level follows the eye, what workers finished goes to the card up to the
 * budget, and what is wanted next goes to workers, the coarsest level first, the nearest column first. Between frames, on the stream the
 * kernels run on. Anything but GPU_STATUS_OK is the card failing, and nothing more should be asked of it. */
gpu_status_t render_lod_frame(render_lod_t *lod, const float64_t position[3]);

/* Whether nothing is wanted that is not already there. */
bool render_lod_idle(const render_lod_t *lod);

/* What this frame's kernels read. */
void render_lod_scene(const render_lod_t *lod, lod_scene_t *out);

/* Which of a level's columns a place along x or y is in. */
int32_t render_lod_column_of(float64_t blocks, uint32_t shift);

/* ---- The pyramid (plan 009 phase 03) --------------------------------------------------------------------------------- */

enum : uint32_t {
    /* Columns made from octants and sent in one frame: compile/config/render/budget/index.yaml. */
    RENDER_LOD_OCTANT_COLUMNS_PER_FRAME = MC_TUNE_RENDER_BUDGET_OCTANT_COLUMNS_PER_FRAME,
};

enum : uint64_t {
    /* What a pyramid level 1 column made from the blocks gets of its pool, halved each level up: compile/config/render/budget/index.yaml. */
    RENDER_LOD_OCTANT_POOL_BYTES_PER_COLUMN = (uint64_t)MC_TUNE_RENDER_BUDGET_OCTANT_POOL_KIB_PER_COLUMN << 10,
};

/* A pyramid level's pool, for blocks loaded this many chunks each way: the columns over them are made from the blocks, caves and all, and get
 * RENDER_LOD_OCTANT_POOL_BYTES_PER_COLUMN (halved each level up); the rest RENDER_LOD_POOL_BYTES_PER_COLUMN as a far level does. */
uint64_t render_lod_pyramid_pool_bytes(const render_lod_plan_t *plan, uint32_t level, uint32_t block_radius_chunks);

/* What render_lod_octants_set takes from the arena on top of render_lod_arena_bytes. */
uint64_t render_lod_octant_arena_bytes(void);

/* Just after render_lod_init, before any render_lod_frame, for the pyramid only: its levels of shift 1 to RENDER_LOD_OCTANT_LEVELS are made
 * from the store where the four columns under one are there (level 1 over the blocks, render_lod_fine_set's), by the gpu thread, up to
 * RENDER_LOD_OCTANT_COLUMNS_PER_FRAME a frame, and from terrain samples on workers elsewhere. Each level 0 column the store says changed is
 * made again in level 1, and each level made from octants hands its own octants to the next. Forgets what the store held above level 0. */
void render_lod_octants_set(render_lod_t *lod, arena_t *arena, render_octant_t *octants);

/* While held, no column goes to a worker; columns made from octants still come. For the pyramid, while the far field rays read is busy. */
void render_lod_jobs_hold(render_lod_t *lod, bool held);

#endif
