/* See run/src/render/lod/README.md. */
#ifndef MC_RUN_RENDER_LOD_INDEX_H
#define MC_RUN_RENDER_LOD_INDEX_H

#include "arena/index.h"
#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/lod/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "render/upload/index.h"
#include "terrain/feature/index.h"
#include "types/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* What a world draws block by block unless told otherwise, in chunks. Past it the far field takes over. */
    RENDER_LOD_FULL_DETAIL_DEFAULT = 32u,
    /* Each way from the eye's column, in a level's own columns of 16 voxels: a level is at most 33 wide. */
    RENDER_LOD_RADIUS_COLUMNS = 16u,
    /* How many columns past what rays are let into are kept built, so that an eye moving on finds the next row already there. */
    RENDER_LOD_MARGIN_COLUMNS = 2u,
    /* Columns being built at once, across all levels. */
    RENDER_LOD_JOBS = 48u,
    /* Column heights in blocks, and in sections at the finest level: 1536 blocks in voxels of 2. */
    RENDER_LOD_HEIGHT_BLOCKS = 1536u,
    RENDER_LOD_SECTIONS_MAX = (RENDER_LOD_HEIGHT_BLOCKS >> LOD_SHIFT_MIN) / 16u,
    /* The coarsest voxel a world's ground grows anything for, as a shift: 8 blocks. What grows is nearly all of what a sample of real
     * terrain costs, and past this a tree is under half of its voxel, so it would not be drawn: the stage file's cost section. */
    RENDER_LOD_FEATURES_SHIFT_MAX = 3u,
};
enum : uint64_t {
    /* What a level's pool gives each of its columns: real ground measured up to 38 KiB in voxels of 2 among mountains, and less the coarser
     * they get. The numbers are in the stage file's cost section. */
    RENDER_LOD_POOL_BYTES_PER_COLUMN = 64ull << 10,
    /* What one frame may send of finished columns: as much again as the world's own queue. */
    RENDER_LOD_FRAME_BYTES = RENDER_UPLOAD_BUDGET_BYTES_DEFAULT,
    /* What a sample may keep from one line of a column to the next: render_lod_sample_terrain's chunks of what grows. */
    RENDER_LOD_SAMPLE_SCRATCH_BYTES = (sizeof(terrain_feature_cache_t) + MC_CPU_CACHE_LINE_BYTES - 1u) /
                                      MC_CPU_CACHE_LINE_BYTES * MC_CPU_CACHE_LINE_BYTES,
};
static_assert(RENDER_LOD_RADIUS_COLUMNS > RENDER_LOD_MARGIN_COLUMNS);

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

/* What has happened since the far field opened. */
typedef struct render_lod_stats_t {
    uint64_t columns_built;   /* 0: on workers, and sent */
    uint64_t columns_dropped; /* 8: finished after they had left the window */
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
    uint8_t _pad0[21]; /* 43 */
} render_lod_job_t;
static_assert(sizeof(render_lod_job_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(render_lod_job_t) == MC_CPU_CACHE_LINE_BYTES);

/* One grid. */
typedef struct render_lod_level_t {
    world_buffers_t buffers;   /* 0 */
    uint8_t *borrowed_state;   /* 232: per slot of the table: render_lod_column_state_t */
    int16_t *borrowed_offsets; /* 240: dx and dy of each column in the table, the nearest first */
    int32_t center_x;          /* 248: the column the eye is in, in this level's columns */
    int32_t center_y;          /* 252 */
    int32_t shown_min_x;       /* 256 */
    int32_t shown_min_y;       /* 260 */
    uint32_t shown_side;       /* 264 */
    uint32_t request_cursor;   /* 268: every offset before this is built, on its way, or has no room */
    uint32_t column_count;     /* 272: side * side */
    uint32_t generation;       /* 276: counts what has been put in the table */
    int32_t top_voxel;         /* 280: the highest voxel any column here ever had, or -1 */
    uint8_t shift;             /* 284 */
    uint8_t radius;            /* 285 */
    uint8_t shown_stale;       /* 286: a column has come or gone since shown_side was worked out */
    uint8_t centered;          /* 287: center_x and center_y mean something yet */
} render_lod_level_t;
static_assert(sizeof(render_lod_level_t) == 288);

/* All of the far field. The gpu thread, or whichever one thread a run has, calls everything here. */
typedef struct render_lod_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) job_group_t group; /* 0 */
    render_lod_level_t levels[LOD_LEVELS_MAX];          /* 64 */
    render_lod_stats_t stats;                           /* 1792 */
    job_system_t *borrowed_job;                         /* 1840 */
    render_lod_job_t *borrowed_jobs;                    /* 1848: RENDER_LOD_JOBS */
    const device_block_t *borrowed_hits;                /* 1856 */
    render_lod_sample_t sample;                         /* 1864 */
    const void *borrowed_sample_context;                /* 1872 */
    uint32_t level_count;                               /* 1880 */
    uint32_t jobs_in_use;                               /* 1884 */
    render_lod_overlay_t overlay;                       /* 1888: or nullptr */
    void *borrowed_overlay_context;                     /* 1896 */
    uint64_t overlay_scratch_bytes;                     /* 1904 */
    uint8_t no_room_said;                               /* 1912 */
    uint8_t _pad0[7];                                   /* 1913 */
} render_lod_t;
static_assert(sizeof(render_lod_t) == 1920);
static_assert(alignof(render_lod_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_lod_t, levels) == 64);
static_assert(offsetof(render_lod_t, stats) == 1792);
static_assert(offsetof(render_lod_t, level_count) == 1880);

/* The levels for this much of the world at full detail and this much in all, in chunks. No far field if full detail reaches it all. A
 * level whose reach full detail covers is left out, and the last is cut to what the distance needs. */
render_lod_plan_t render_lod_plan(uint32_t full_detail_chunks, uint32_t render_distance_chunks);

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

/* Takes what it needs from the arena, and starts each level empty, on the card too. All of it must outlive this. tables and pools are the
 * Owner's blocks, one of each per level; hits is lod_hits_bytes of the picture. On failure logs why, and render_lod_deinit is still safe. */
gpu_status_t render_lod_init(render_lod_t *lod, arena_t *arena, device_memory_t *memory, job_system_t *job,
                             const render_lod_plan_t *plan, const device_block_t *tables, const device_block_t *pools,
                             const device_block_t *hits, render_lod_sample_t sample, const void *sample_context);

/* From now on what is sampled has this written over it. Before the first render_lod_frame. */
void render_lod_overlay_set(render_lod_t *lod, render_lod_overlay_t overlay, void *borrowed_context,
                            uint64_t scratch_bytes);

/* What is under a column of the world, in chunks, has changed: each level makes the column it falls in again, showing the old one until the new is ready. */
void render_lod_column_changed(render_lod_t *lod, int32_t chunk_x, int32_t chunk_y);

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

#endif
