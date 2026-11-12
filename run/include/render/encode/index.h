/* See run/src/render/encode/README.md. */
#ifndef MC_RUN_RENDER_ENCODE_INDEX_H
#define MC_RUN_RENDER_ENCODE_INDEX_H

#include "gpu_bridge/world_buffers/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "world/edit/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* The two sides name the same shape: a section is 4096 voxels whichever compiler is asked. */
static_assert((uint32_t)SECTION_VOXELS == (uint32_t)WORLD_BUFFERS_SECTION_VOXELS);
static_assert((uint32_t)COLUMN_SECTIONS == (uint32_t)WORLD_BUFFERS_COLUMN_SECTIONS);
static_assert(-(int32_t)WORLD_BUFFERS_Z_ORIGIN == (int32_t)COLUMN_Z_STORAGE_MIN);

/* One section's voxels, in section_voxel_index order, or the one value all of them share. */
typedef struct render_encode_source_t {
    const uint16_t *borrowed_blocks; /* 0: nullptr means block_uniform */
    const uint8_t *borrowed_light;   /* 8: nullptr means light_uniform */
    uint16_t block_uniform;          /* 16 */
    uint8_t light_uniform;           /* 18 */
    uint8_t _pad0[5];                /* 19 */
} render_encode_source_t;
static_assert(sizeof(render_encode_source_t) == 24);

/* Writes a section in the form in gpu_bridge/world_buffers/index.h, and returns its bytes, a multiple of 8, or 0 if it has no
 * block at all and so nothing to send. State 0 is air, and every other state is a block. out is 8-aligned and has room for
 * WORLD_BUFFERS_SECTION_BYTES_MAX. Any thread: it touches only what it is given. */
uint32_t render_encode_section(const render_encode_source_t *source, uint8_t *out);

enum : uint32_t {
    /* A section reduced by two each way is an eighth of the section above it: 8 x 8 x 8 voxels, x fastest, then y, then z. */
    RENDER_ENCODE_OCTANT_SIDE = 8u,
    RENDER_ENCODE_OCTANT_VOXELS = RENDER_ENCODE_OCTANT_SIDE * RENDER_ENCODE_OCTANT_SIDE * RENDER_ENCODE_OCTANT_SIDE,
    /* A group of 2 x 2 x 2 voxels, index x + 2 y + 4 z, that one voxel of the level above stands for. */
    RENDER_ENCODE_GROUP_VOXELS = 8u,
};

/* Plan 009 phase 03: one section's octant of the section above it in the pyramid. Each voxel stands for 2 x 2 x 2 of the section's. */
typedef struct render_encode_octant_t {
    uint16_t material[RENDER_ENCODE_OCTANT_VOXELS]; /* 0: 0 for air */
    uint8_t light[RENDER_ENCODE_OCTANT_VOXELS];     /* 1024: 0 for air */
} render_encode_octant_t;
static_assert(sizeof(render_encode_octant_t) == 1536);

/* Whether a block has body enough to count towards a coarser voxel: cubes, fluids, slabs and stairs do; a flower, a torch, a crop, a pane or a
 * door does not. The far field's half rule and the octant reduction both ask this. */
bool render_encode_block_counts(uint16_t block);

/* One voxel of the level above, from its 8 (index x + 2 y + 4 z). It is a block if at least half of them count (the half rule). Its block is the
 * most common of the counted ones in the top layer, as that is the face seen from above, or in the bottom layer if the top has none; a tie goes
 * to the one met first in index order. Its light is the brightest of the counted ones', sky and block light each taken at their most. Air is
 * 0 and 0. */
void render_encode_octant_voxel(const uint16_t blocks[RENDER_ENCODE_GROUP_VOXELS],
                                const uint8_t light[RENDER_ENCODE_GROUP_VOXELS], uint16_t *out_material,
                                uint8_t *out_light);

/* A section's 4096 voxels to the 512 of its octant, each by render_encode_octant_voxel. Pure; any thread. Returns whether all 512 are one
 * block with one light, which is what a uniform section becomes. The same reduces an assembled level k section to its octant of level k + 1. */
bool render_encode_section_octant(const render_encode_source_t *source, render_encode_octant_t *out);

/* One section of a snapshot view, to be encoded on a worker. Its own cache line, since a worker writes done_bytes while the
 * gpu thread polls it and other workers write their neighbours'. */
typedef struct render_encode_job_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) const world_edit_snapshot_view_t *borrowed_view; /* 0 */
    uint8_t *borrowed_out;                                                            /* 8 */
    uint32_t section;                                                                 /* 16: its place in the view */
    _Atomic uint32_t done_bytes;             /* 20: 0 until the job has finished, then 1 + what it wrote */
    render_encode_octant_t *borrowed_octant; /* 24: where its octant goes as well, or nullptr for none */
    uint8_t _pad0[32];                       /* 32 */
} render_encode_job_t;
static_assert(sizeof(render_encode_job_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(render_encode_job_t) == MC_CPU_CACHE_LINE_BYTES);

/* Gets one ready, before it is given to the job system. */
void render_encode_job_begin(render_encode_job_t *job, const world_edit_snapshot_view_t *view, uint32_t section,
                             uint8_t *out);

/* After render_encode_job_begin, which makes no octant: the job also reduces its section to its octant, here, before it says it is done. */
void render_encode_job_octant_set(render_encode_job_t *job, render_encode_octant_t *octant);

/* A job_function_t, whose argument is a render_encode_job_t. Once done_bytes is set it touches neither the view nor the job,
 * so whoever sees that may let go of both. */
void render_encode_job_run(job_worker_t *worker, void *borrowed_job);

/* The gpu thread. True once the job has finished, with what it wrote. */
bool render_encode_job_done(const render_encode_job_t *job, uint32_t *out_bytes);

#endif
