/* See README.md. The budgeted upload queue: views come in whole, sections go out as the budget allows, and no call here ever
 * waits for a worker. */
#include "render/upload/index.h"

#include "assert/index.h"

#include <string.h>

uint64_t render_upload_arena_bytes(uint32_t section_capacity) {
    /* Each alignment can waste up to its size. */
    return (uint64_t)section_capacity * sizeof(render_encode_job_t) +
           (uint64_t)section_capacity * WORLD_BUFFERS_SECTION_BYTES_MAX +
           RENDER_UPLOAD_RESEND_CAPACITY * sizeof(world_edit_dirty_t) + 3u * MC_CPU_CACHE_LINE_BYTES;
}

void render_upload_init(render_upload_t *upload, arena_t *arena, world_edit_snapshot_t *snapshot, job_system_t *job,
                        world_buffers_t *buffers, uint32_t section_capacity, render_upload_budget_t budget) {
    ASSERT_ALWAYS(section_capacity > 0u);
    ASSERT_ALWAYS(budget.bytes >= WORLD_BUFFERS_SECTION_BYTES_MAX && budget.sections > 0u);
    memset(upload, 0, sizeof(*upload));
    upload->borrowed_snapshot = snapshot;
    upload->borrowed_job = job;
    upload->borrowed_buffers = buffers;
    upload->section_capacity = section_capacity;
    upload->budget = budget;
    upload->borrowed_jobs =
        arena_alloc_array(arena, section_capacity, sizeof(render_encode_job_t), alignof(render_encode_job_t));
    /* Encoded sections hold uint64_t. The pages stay untouched until a section of that size is really encoded. */
    upload->borrowed_staging =
        arena_alloc_array(arena, section_capacity, WORLD_BUFFERS_SECTION_BYTES_MAX, MC_CPU_CACHE_LINE_BYTES);
    const uint64_t ring_bytes = ring_storage_bytes(RENDER_UPLOAD_RESEND_CAPACITY, sizeof(world_edit_dirty_t));
    void *const storage = arena_alloc(arena, ring_bytes, alignof(world_edit_dirty_t));
    ASSERT_ALWAYS(
        ring_init(&upload->resend, storage, ring_bytes, RENDER_UPLOAD_RESEND_CAPACITY, sizeof(world_edit_dirty_t)));
    job_group_init(&upload->group);
}

void render_upload_deinit(render_upload_t *upload) {
    if (upload->borrowed_job == nullptr) {
        return;
    }
    job_group_wait(upload->borrowed_job, &upload->group);
    job_group_deinit(&upload->group);
    ring_deinit(&upload->resend);
    memset(upload, 0, sizeof(*upload));
}

bool render_upload_idle(const render_upload_t *upload) {
    return upload->borrowed_view == nullptr || upload->finished == upload->borrowed_view->section_count;
}

/* Costs one look at every slot of the table, per view, and only those that changed go further. */
static void upload_table_apply(render_upload_t *upload, const world_edit_snapshot_view_t *view) {
    world_buffers_t *const buffers = upload->borrowed_buffers;
    ASSERT_ALWAYS(view->slot_count == buffers->window.slot_count);
    world_buffers_set_window(buffers, (world_buffers_window_t){
                                          .min_x = view->min_x,
                                          .min_y = view->min_y,
                                          .min_slot_x = view->min_slot_x,
                                          .min_slot_y = view->min_slot_y,
                                          .side = view->side,
                                          .slot_count = view->slot_count,
                                      });
    for (uint32_t slot = 0; slot < view->slot_count; slot++) {
        const world_edit_snapshot_column_t *const column = &view->borrowed_columns[slot];
        if (column->state == WORLD_MAP_STATE_READY) {
            world_buffers_column_set(buffers, slot, column->chunk_x, column->chunk_y, column->generation,
                                     column->top_z);
        } else {
            world_buffers_column_clear(buffers, slot);
        }
    }
    /* After all of them, as a square's top is about its neighbours as well. A column that changed is the one with no top now. */
    for (uint32_t slot = 0; slot < view->slot_count; slot++) {
        const world_buffers_column_t *const column = &buffers->borrowed_shadow[slot];
        if (column->ready != 0u && column->region_top == 0u) {
            world_buffers_region_note(buffers, slot);
        }
    }
}

/* Workers read the view until each says it is finished, so the next can only be taken once all of this one's are dealt with. */
static void upload_view_take(render_upload_t *upload) {
    if (!render_upload_idle(upload)) {
        return;
    }
    const world_edit_snapshot_view_t *const view = world_edit_snapshot_acquire(upload->borrowed_snapshot);
    if (view == nullptr) {
        return;
    }
    ASSERT_ALWAYS(view->section_count <= upload->section_capacity);
    upload->borrowed_view = view;
    upload->submitted = 0u;
    upload->finished = 0u;
    upload->stats.views++;
    upload_table_apply(upload, view);
}

/* A shared queue full of other work means the rest wait for the next frame, rather than this thread waiting for it. */
static void upload_jobs_submit(render_upload_t *upload) {
    const world_edit_snapshot_view_t *const view = upload->borrowed_view;
    if (view == nullptr) {
        return;
    }
    while (upload->submitted < view->section_count) {
        const uint32_t section = upload->submitted;
        render_encode_job_t *const job = &upload->borrowed_jobs[section];
        render_encode_job_begin(job, view, section,
                                upload->borrowed_staging + (uint64_t)section * WORLD_BUFFERS_SECTION_BYTES_MAX);
        if (!job_try_submit(upload->borrowed_job, &upload->group, render_encode_job_run, job)) {
            return;
        }
        upload->submitted++;
    }
}

/* A section whose column has gone since it was published has nowhere to go. */
static bool upload_section_is_stale(const world_buffers_t *buffers, const world_edit_snapshot_section_t *entry) {
    const world_buffers_column_t *const column = &buffers->borrowed_shadow[entry->slot];
    return column->ready == 0u || column->generation != entry->generation;
}

/* In the order of the view, so that what the budget holds back is what came last. */
static gpu_status_t upload_sections_send(render_upload_t *upload) {
    const world_edit_snapshot_view_t *const view = upload->borrowed_view;
    render_upload_stats_t *const stats = &upload->stats;
    world_buffers_t *const buffers = upload->borrowed_buffers;
    while (view != nullptr && upload->finished < upload->submitted) {
        const uint32_t section = upload->finished;
        uint32_t bytes = 0u;
        if (!render_encode_job_done(&upload->borrowed_jobs[section], &bytes)) {
            break; /* still with a worker */
        }
        const world_edit_snapshot_section_t *const entry = &view->borrowed_sections[section];
        if (upload_section_is_stale(buffers, entry)) {
            stats->sections_stale++;
            upload->finished++;
            continue;
        }
        if (bytes == 0u) {
            /* Nothing to copy, so it costs no budget, and frees a slab if it held one. */
            const gpu_status_t emptied =
                world_buffers_section_write(buffers, entry->slot, entry->section_index, nullptr, 0u);
            if (emptied != GPU_STATUS_OK) {
                return emptied;
            }
            stats->sections_emptied++;
            upload->finished++;
            continue;
        }
        if (stats->frame_sections >= upload->budget.sections || stats->frame_bytes + bytes > upload->budget.bytes) {
            stats->budget_stop_count++;
            break;
        }
        const gpu_status_t written = world_buffers_section_write(
            buffers, entry->slot, entry->section_index,
            upload->borrowed_staging + (uint64_t)section * WORLD_BUFFERS_SECTION_BYTES_MAX, bytes);
        if (written == GPU_STATUS_OVER_BUDGET) {
            const world_edit_dirty_t dirty = {
                .section_mask = column_mask_of(entry->section_index),
                .chunk_x = entry->chunk_x,
                .chunk_y = entry->chunk_y,
                .slot = entry->slot,
                .generation = entry->generation,
            };
            if (!ring_try_push(&upload->resend, &dirty)) {
                break; /* its thread has not been by: this one keeps its place, and is tried again next frame */
            }
            stats->sections_resent++;
            upload->finished++;
            continue;
        }
        if (written != GPU_STATUS_OK) {
            return written;
        }
        stats->sections_sent++;
        stats->bytes_sent += bytes;
        stats->frame_bytes += bytes;
        stats->frame_sections++;
        upload->finished++;
    }
    return GPU_STATUS_OK;
}

gpu_status_t render_upload_frame(render_upload_t *upload) {
    upload->stats.frames++;
    upload->stats.frame_bytes = 0u;
    upload->stats.frame_sections = 0u;
    upload_view_take(upload);
    upload_jobs_submit(upload);
    const gpu_status_t sent = upload_sections_send(upload);
    if (sent != GPU_STATUS_OK) {
        return sent;
    }
    return world_buffers_flush(upload->borrowed_buffers);
}

uint32_t render_upload_resend_drain(render_upload_t *upload, world_edit_t *edit) {
    uint32_t count = 0u;
    world_edit_dirty_t dirty;
    while (ring_try_pop(&upload->resend, &dirty)) {
        world_edit_dirty_restore(edit, &dirty, WORLD_EDIT_CONSUMER_RENDER);
        count++;
    }
    return count;
}
