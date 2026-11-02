/* See run/src/render/README.md. */
#ifndef MC_RUN_RENDER_INDEX_H
#define MC_RUN_RENDER_INDEX_H

#include "arena/index.h"
#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "render/encode/index.h"
#include "render/entities/index.h"
#include "render/frame/index.h"
#include "render/lod/index.h"
#include "render/upload/index.h"
#include "texture/index.h"
#include "world/edit/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint64_t {
    /* What the table holds at the largest render distance. */
    RENDER_COLUMN_COUNT_DEFAULT =
        (2ull * MC_GAME_RENDER_DISTANCE_MAX + 1ull) * (2ull * MC_GAME_RENDER_DISTANCE_MAX + 1ull),
    RENDER_VOXEL_BUDGET_BYTES = (uint64_t)MC_GPU_BUDGET_VOXELS_MIB << 20,
    /* What is left of gpu.budget.voxels_mib for the pool after a table of that size, on a whole page. */
    RENDER_POOL_BYTES_DEFAULT =
        (RENDER_VOXEL_BUDGET_BYTES - RENDER_COLUMN_COUNT_DEFAULT * sizeof(world_buffers_column_t)) /
        WORLD_BUFFERS_PAGE_BYTES * WORLD_BUFFERS_PAGE_BYTES,
    /* The pool render_pool_bytes_for gives each column. Real terrain, measured in the phase 10 gate on 100 columns scattered over
     * 1280 blocks: a mean of 216 KiB a column, 20.2 sections with something other than one block all through, at about 10.7 KiB
     * each. This is 24 sections, in 16 KiB of slab each, for slab waste and for columns under mountains. */
    RENDER_POOL_BYTES_PER_COLUMN = 24ull * 16384ull,
    /* Never smaller than this: a world of a few columns still has every slab size to serve, each with a page of its own. */
    RENDER_POOL_BYTES_MIN = (uint64_t)WORLD_BUFFERS_CLASS_COUNT * WORLD_BUFFERS_PAGE_BYTES,
};

/* The most a world may ask for. Host memory is sized from these at init and costs nothing until touched; device memory is
 * taken at render_open, for that world only. */
typedef struct render_limits_t {
    uint64_t pool_bytes;    /* 0 */
    uint64_t column_count;  /* 8 */
    uint32_t section_count; /* 16: what one snapshot view can carry */
    uint8_t _pad0[4];       /* 20 */
} render_limits_t;
static_assert(sizeof(render_limits_t) == 24);

/* What the renderer holds. There is one, under app_t. Everything past owned_host is zero bytes outside render_open. */
typedef struct render_t {
    arena_t arena_host;               /* 0: over owned_host; a world's, reset at render_close */
    render_upload_t upload;           /* 64 */
    heap_region_t owned_host;         /* 512 */
    device_block_t owned_pool;        /* 528: the slab pool on the card */
    device_block_t owned_table;       /* 552: the column table on the card */
    world_buffers_t buffers;          /* 576 */
    device_memory_t *borrowed_memory; /* 808 */
    job_system_t *borrowed_job;       /* 816 */
    render_limits_t limits;           /* 824 */
    uint8_t open;                     /* 848 */
    uint8_t frame_open;               /* 849 */
    uint8_t _pad0[46];                /* 850 */

    arena_t arena_frame;                  /* 896: over owned_frame_host */
    render_frame_t frame;                 /* 960 */
    heap_region_t owned_frame_host;       /* 1792 */
    device_block_t owned_textures;        /* 1808: what shading reads, from DEVICE_POOL_TEXTURES */
    device_block_t owned_faces;           /* 1832 */
    device_block_t owned_tex_pixels;      /* 1856 */
    device_block_t owned_coarse;          /* 1880: what a frame writes, from DEVICE_POOL_FRAMES */
    device_block_t owned_hits;            /* 1904 */
    device_block_t owned_shaded;          /* 1928 */
    device_block_t owned_xrgb;            /* 1952 */
    device_block_t owned_shapes;          /* 1976: from DEVICE_POOL_TEXTURES, with the other tables */
    device_block_t owned_ao[2];           /* 2000: what goes from frame to frame, from DEVICE_POOL_FRAMES */
    device_block_t owned_history[2];      /* 2048 */
    device_block_t owned_shafts;          /* 2096 */
    device_block_t owned_entity_lists;    /* 2120: the same pool: render_entities_lists_bytes */
    device_block_t owned_entity_hits;     /* 2144 */
    render_entities_t *borrowed_entities; /* 2168: from arena_frame, from render_open_frame to render_close_frame */

    arena_t arena_lod;                               /* 2176: over owned_lod_host */
    heap_region_t owned_lod_host;                    /* 2240 */
    device_block_t owned_lod_tables[LOD_LEVELS_MAX]; /* 2256: from DEVICE_POOL_VOXELS, with the world's */
    device_block_t owned_lod_pools[LOD_LEVELS_MAX];  /* 2400 */
    device_block_t owned_lod_hits;                   /* 2544: from DEVICE_POOL_FRAMES */
    render_lod_t *borrowed_lod; /* 2568: from arena_lod, from render_open_lod to render_close_lod */
    uint8_t _pad1[48];          /* 2576 */
} render_t;
static_assert(sizeof(render_t) == 2624);
static_assert(alignof(render_t) == 64);
static_assert(offsetof(render_t, upload) == 64);
static_assert(offsetof(render_t, owned_host) == 512);
static_assert(offsetof(render_t, buffers) == 576);
static_assert(offsetof(render_t, open) == 848);
static_assert(offsetof(render_t, frame) == 960);
static_assert(offsetof(render_t, owned_frame_host) == 1792);
static_assert(offsetof(render_t, owned_entity_lists) == 2120);
static_assert(offsetof(render_t, borrowed_entities) == 2168);
static_assert(offsetof(render_t, arena_lod) == 2176);
static_assert(offsetof(render_t, borrowed_lod) == 2568);

/* What a real game runs with: gpu.budget in config/machine.yaml. */
render_limits_t render_limits_default(void);

/* Reserves the host memory those limits could ever need, and takes nothing on the card. memory and job must outlive this. On
 * failure logs why, leaves nothing behind and returns false. */
bool render_init(render_t *render, device_memory_t *memory, job_system_t *job, render_limits_t limits);

/* Closes any world still open and releases all of it. Safe on a zeroed render_t. */
void render_deinit(render_t *render);

/* A pool for a table with this many slots: what a first guess says it would hold, within the limits and on a whole page. */
uint64_t render_pool_bytes_for(const render_t *render, uint32_t slot_count);

/* Starts on a world: takes pool_bytes and the table for this snapshot's map on the card, from DEVICE_POOL_VOXELS. pool_bytes is
 * a whole number of pages. The snapshot must outlive render_close, and its map's table must fit limits. On failure logs why,
 * takes nothing and returns what went wrong. */
gpu_status_t render_open(render_t *render, world_edit_snapshot_t *snapshot, uint64_t pool_bytes,
                         render_upload_budget_t budget);

/* Gives all of that back, and a frame's too if it is still open. Safe if none is open. The gpu thread must have stopped using
 * it. */
void render_close(render_t *render);

/* Starts drawing pictures of this size: takes the textures and tables from DEVICE_POOL_TEXTURES, the frame's targets from
 * DEVICE_POOL_FRAMES, and host memory for the pictures. Apart from render_open, since a test can want one without the other.
 * On failure logs why, takes nothing and returns what went wrong. */
gpu_status_t render_open_frame(render_t *render, const texture_t *texture, uint32_t width, uint32_t height,
                               float32_t fog_end);

/* Gives all of that back, the far field's too. Safe if none is open. The gpu thread must have stopped using it. */
void render_close_frame(render_t *render);

/* Starts on the far field of a world whose frame is open, in these levels: takes their tables and pools from DEVICE_POOL_VOXELS, their hits
 * from DEVICE_POOL_FRAMES, and host memory. From here on each frame draws it. sample and its context must outlive it. On failure logs
 * why, takes nothing and returns what went wrong, and the frames go on without it. */
gpu_status_t render_open_lod(render_t *render, const render_lod_plan_t *plan, render_lod_sample_t sample,
                             const void *sample_context);

/* Gives all of that back. Safe if none is open. The gpu thread must have stopped using it. */
void render_close_lod(render_t *render);

#endif
