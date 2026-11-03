/* See README.md. */
#include "save/autosave/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "time/index.h"

#include <string.h>

void save_autosave_start(save_autosave_t *autosave, job_system_t *job, save_world_t *world, save_state_t *state,
                         uint64_t tick) {
    ASSERT_ALWAYS(autosave->started == 0u);
    memset(autosave, 0, sizeof(*autosave));
    job_group_init(&autosave->group);
    autosave->borrowed_job = job;
    autosave->borrowed_world = world;
    autosave->borrowed_state = state;
    autosave->interval_ticks = SAVE_AUTOSAVE_INTERVAL_TICKS;
    autosave->next_tick = tick + SAVE_AUTOSAVE_INTERVAL_TICKS;
    autosave->started = 1u;
}

void save_autosave_interval_set(save_autosave_t *autosave, uint64_t ticks) {
    ASSERT_ALWAYS(ticks > 0u);
    autosave->interval_ticks = ticks;
}

void save_autosave_stop(save_autosave_t *autosave) {
    if (autosave->started == 0u) {
        return;
    }
    job_group_wait(autosave->borrowed_job, &autosave->group);
    job_group_deinit(&autosave->group);
    autosave->started = 0u;
}

void save_autosave_request(save_autosave_t *autosave) {
    autosave->requested = 1u;
}

/* The state goes first, under the name of the save it goes with. The manifest's rename then makes both of them the world at once: see the README. */
static save_world_status_t save_autosave_write(save_world_t *world, save_state_t *state, uint64_t generation,
                                               bool has_blocks) {
    const save_world_status_t stated = save_state_write(state, generation);
    if (stated != SAVE_WORLD_OK || !has_blocks) {
        return stated;
    }
    const save_world_status_t written = save_world_commit_write(world);
    if (written == SAVE_WORLD_OK) {
        save_state_forget(state, generation - 1u);
    }
    return written;
}

/* On a worker. The sim touches neither image nor writing set until it sees writing go to 0. */
static void save_autosave_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    save_autosave_t *const autosave = borrowed_argument;
    const int64_t started_ns = time_now_ns();
    const save_world_status_t status =
        save_autosave_write(autosave->borrowed_world, autosave->borrowed_state, autosave->write_generation,
                            autosave->write_has_blocks != 0u);
    const int64_t took_ns = time_now_ns() - started_ns;
    if (took_ns > autosave->write_ns_most) {
        autosave->write_ns_most = took_ns;
    }
    if (status == SAVE_WORLD_OK) {
        autosave->saves_done++;
    } else {
        autosave->saves_failed++;
    }
    atomic_store_explicit(&autosave->written_status, status, memory_order_relaxed);
    atomic_store_explicit(&autosave->writing, 0u, memory_order_release);
}

/* What the last write came to, once it has finished. */
static void save_autosave_look(save_autosave_t *autosave, uint64_t tick) {
    if (autosave->outstanding == 0u) {
        return;
    }
    autosave->outstanding = 0u;
    const save_world_status_t status =
        (save_world_status_t)atomic_load_explicit(&autosave->written_status, memory_order_relaxed);
    if (status == SAVE_WORLD_OK) {
        autosave->failed_said = 0u;
        autosave->retry = 0u;
        return;
    }
    if (autosave->failed_said == 0u) {
        LOG_ERROR("autosave: this save failed, and the game goes on, with what it has kept in memory: %s",
                  save_world_status_name(status));
        autosave->failed_said = 1u;
    }
    /* A set that was not written is still held, and the state that goes with it is in the image. With no set there is nothing to keep them together for. */
    autosave->retry = autosave->write_has_blocks;
    autosave->next_tick = tick + autosave->interval_ticks;
}

bool save_autosave_due(save_autosave_t *autosave, uint64_t tick) {
    if (autosave->started == 0u || atomic_load_explicit(&autosave->writing, memory_order_acquire) != 0u) {
        return false;
    }
    save_autosave_look(autosave, tick);
    /* While the disk is refusing, what is asking waits with everything else, or a full disk is written to at every step. */
    return tick >= autosave->next_tick || (autosave->requested != 0u && autosave->failed_said == 0u);
}

void save_autosave_step(save_autosave_t *autosave, world_edit_t *edit, const save_state_parts_t *parts, uint64_t tick) {
    if (!save_autosave_due(autosave, tick)) {
        return;
    }
    const int64_t started_ns = time_now_ns();
    autosave->requested = 0u;
    autosave->next_tick = tick + autosave->interval_ticks;
    save_world_t *const world = autosave->borrowed_world;
    if (autosave->retry == 0u) {
        if (save_world_capture_dirty(world, edit) == SAVE_WORLD_FULL) {
            autosave->requested = 1u; /* what did not fit is still dirty, and goes as soon as this has gone */
        }
        save_state_capture(autosave->borrowed_state, parts);
    }
    const bool has_blocks = save_world_commit_begin(world);
    autosave->write_has_blocks = has_blocks ? 1u : 0u;
    autosave->write_generation = world->generation + (has_blocks ? 1u : 0u);
    atomic_store_explicit(&autosave->written_status, SAVE_WORLD_OK, memory_order_relaxed);
    atomic_store_explicit(&autosave->writing, 1u, memory_order_release);
    if (job_try_submit(autosave->borrowed_job, &autosave->group, save_autosave_job, autosave)) {
        autosave->outstanding = 1u;
    } else {
        /* The shared queue is full for a moment. What was copied waits, as it was, for the next step. */
        atomic_store_explicit(&autosave->writing, 0u, memory_order_relaxed);
        autosave->retry = has_blocks ? 1u : 0u;
        autosave->requested = 1u;
    }
    const int64_t took_ns = time_now_ns() - started_ns;
    if (took_ns > autosave->capture_ns_most) {
        autosave->capture_ns_most = took_ns;
    }
}

save_world_status_t save_autosave_finish(save_autosave_t *autosave, world_edit_t *edit,
                                         const save_state_parts_t *parts) {
    ASSERT_ALWAYS(autosave->borrowed_world != nullptr);
    save_autosave_stop(autosave);
    save_world_t *const world = autosave->borrowed_world;
    save_state_t *const state = autosave->borrowed_state;
    save_world_status_t captured = SAVE_WORLD_OK;
    do {
        captured = save_world_capture_dirty(world, edit);
        save_state_capture(state, parts);
        const bool has_blocks = save_world_commit_begin(world);
        const save_world_status_t written =
            save_autosave_write(world, state, world->generation + (has_blocks ? 1u : 0u), has_blocks);
        if (written != SAVE_WORLD_OK) {
            autosave->saves_failed++;
            return written;
        }
        autosave->saves_done++;
        /* One that had failed went first, and what has been captured since is still to go. */
    } while (captured == SAVE_WORLD_FULL || world->pending[world->active].count != 0u);
    return SAVE_WORLD_OK;
}
