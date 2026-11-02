/* See run/src/render/upload/README.md. */
#ifndef MC_RUN_RENDER_UPLOAD_INDEX_H
#define MC_RUN_RENDER_UPLOAD_INDEX_H

#include "arena/index.h"
#include "gpu_bridge/world_buffers/index.h"
#include "job/index.h"
#include "render/encode/index.h"
#include "thread/ring/index.h"
#include "world/edit/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint64_t {
    /* What one frame may send of sections. About 0.7 ms at the 6 GB/s measured in Stage 08.2 issue 5, a tenth of a frame at
     * 120 FPS. Measured on one card with no frame running: Stage 17.3 owes this a real one. */
    RENDER_UPLOAD_BUDGET_BYTES_DEFAULT = 4ull << 20,
    /* Sections it may hold back a pool-full answer for at once: see render_upload_resend_drain. A power of two. */
    RENDER_UPLOAD_RESEND_CAPACITY = 4096u,
};
enum : uint32_t {
    /* The other limit, for small sections: at about 3 microseconds to start a copy, 256 is about 0.8 ms too. */
    RENDER_UPLOAD_BUDGET_SECTIONS_DEFAULT = 256u,
};

typedef struct render_upload_budget_t {
    uint64_t bytes; /* 0: of sections, per frame. At least WORLD_BUFFERS_SECTION_BYTES_MAX, or nothing would ever fit */
    uint32_t sections; /* 8: copies, per frame. At least 1 */
    uint8_t _pad0[4];  /* 12 */
} render_upload_budget_t;
static_assert(sizeof(render_upload_budget_t) == 16);

/* What has happened since init, and in the last frame. */
typedef struct render_upload_stats_t {
    uint64_t views;             /* 0: views taken from the snapshots */
    uint64_t frames;            /* 8 */
    uint64_t sections_sent;     /* 16: copied to the card */
    uint64_t sections_emptied;  /* 24: encoded to nothing, so that any bytes it had went back to the pool */
    uint64_t sections_stale;    /* 32: their column had gone by the time they were ready */
    uint64_t sections_resent;   /* 40: no slab for them, so they were handed back to be sent again */
    uint64_t bytes_sent;        /* 48 */
    uint64_t frame_bytes;       /* 56: of the last frame */
    uint64_t budget_stop_count; /* 64: frames that stopped with sections ready and no budget left */
    uint32_t frame_sections;    /* 72: copies of the last frame */
    uint8_t _pad0[4];           /* 76 */
} render_upload_stats_t;
static_assert(sizeof(render_upload_stats_t) == 80);

/* From a snapshot, through the workers, to the card. The gpu thread calls everything here, except that render_upload_resend_
 * drain belongs to the thread holding the map. */
typedef struct render_upload_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) job_group_t group; /* 0 */
    ring_t resend;                                      /* 64: gpu thread to the map's thread: world_edit_dirty_t */
    world_edit_snapshot_t *borrowed_snapshot;           /* 256 */
    job_system_t *borrowed_job;                         /* 264 */
    world_buffers_t *borrowed_buffers;                  /* 272 */
    render_encode_job_t *borrowed_jobs;                 /* 280: section_capacity */
    uint8_t *borrowed_staging;                          /* 288: section_capacity * WORLD_BUFFERS_SECTION_BYTES_MAX */
    const world_edit_snapshot_view_t *borrowed_view;    /* 296: the view whose sections are on their way */
    render_upload_stats_t stats;                        /* 304 */
    render_upload_budget_t budget;                      /* 384 */
    uint32_t section_capacity;                          /* 400 */
    uint32_t submitted;                                 /* 404: of the view's sections, those given to workers */
    uint32_t finished;                                  /* 408: of them, those dealt with */
    uint8_t _pad0[36];                                  /* 412 */
} render_upload_t;
static_assert(sizeof(render_upload_t) == 448);
static_assert(alignof(render_upload_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(render_upload_t, resend) == 64);
static_assert(offsetof(render_upload_t, borrowed_snapshot) == 256);
static_assert(offsetof(render_upload_t, stats) == 304);
static_assert(offsetof(render_upload_t, budget) == 384);

/* What render_upload_init takes from its arena. */
uint64_t render_upload_arena_bytes(uint32_t section_capacity);

/* section_capacity is that of the snapshots, which is the most one view can carry. Takes from the arena, which must outlive
 * this, as must everything else it is given. */
void render_upload_init(render_upload_t *upload, arena_t *arena, world_edit_snapshot_t *snapshot, job_system_t *job,
                        world_buffers_t *buffers, uint32_t section_capacity, render_upload_budget_t budget);

/* Waits for any job still running, since its memory is about to go. */
void render_upload_deinit(render_upload_t *upload);

/* One frame's worth, without ever waiting for a worker: takes the newest view once the last is dealt with, and puts its table
 * in place, starts its sections encoding, sends what is ready up to the budget, and flushes the table. Call it between
 * frames, on the stream the kernels run on. Anything but GPU_STATUS_OK is the card failing, with world_buffers' last_error
 * saying how, and nothing more should be asked of it. */
gpu_status_t render_upload_frame(render_upload_t *upload);

/* Whether there is nothing on its way: every section of the last view is dealt with. */
bool render_upload_idle(const render_upload_t *upload);

/* The thread holding the map, between its writes. A section the pool had no slab for is handed back here as dirty, so that it
 * goes out again in a later view, by which time something may have left. Returns how many. */
uint32_t render_upload_resend_drain(render_upload_t *upload, world_edit_t *edit);

#endif
