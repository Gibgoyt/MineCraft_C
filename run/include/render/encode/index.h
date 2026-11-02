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

/* One section of a snapshot view, to be encoded on a worker. Its own cache line, since a worker writes done_bytes while the
 * gpu thread polls it and other workers write their neighbours'. */
typedef struct render_encode_job_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) const world_edit_snapshot_view_t *borrowed_view; /* 0 */
    uint8_t *borrowed_out;                                                            /* 8 */
    uint32_t section;                                                                 /* 16: its place in the view */
    _Atomic uint32_t done_bytes; /* 20: 0 until the job has finished, then 1 + what it wrote */
    uint8_t _pad0[40];           /* 24 */
} render_encode_job_t;
static_assert(sizeof(render_encode_job_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(render_encode_job_t) == MC_CPU_CACHE_LINE_BYTES);

/* Gets one ready, before it is given to the job system. */
void render_encode_job_begin(render_encode_job_t *job, const world_edit_snapshot_view_t *view, uint32_t section,
                             uint8_t *out);

/* A job_function_t, whose argument is a render_encode_job_t. Once done_bytes is set it touches neither the view nor the job,
 * so whoever sees that may let go of both. */
void render_encode_job_run(job_worker_t *worker, void *borrowed_job);

/* The gpu thread. True once the job has finished, with what it wrote. */
bool render_encode_job_done(const render_encode_job_t *job, uint32_t *out_bytes);

#endif
