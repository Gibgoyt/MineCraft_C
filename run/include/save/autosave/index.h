/* See run/src/save/autosave/README.md. */
#ifndef MC_RUN_SAVE_AUTOSAVE_INDEX_H
#define MC_RUN_SAVE_AUTOSAVE_INDEX_H

#include "job/index.h"
#include "machine/index.h"
#include "save/state/index.h"
#include "save/world/index.h"
#include "world/edit/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : uint64_t {
    /* Thirty seconds of world time: what a kill can lose, past the write under way. */
    SAVE_AUTOSAVE_INTERVAL_TICKS = 30ull * MC_GAME_TICK_HZ,
};

/* A save at a time, in the background. The sim's, but for what the comments give to the worker. */
typedef struct save_autosave_t {
    job_group_t group;        /* 0: the one write */
    _Atomic uint32_t writing; /* 64: sim to worker with release, and worker to sim with release when it has finished */
    _Atomic uint32_t written_status; /* 68: worker to sim, under writing: a save_world_status_t */
    uint32_t started;                /* 72 */
    uint8_t requested;               /* 76: something waits for a save, and should not wait for the interval */
    uint8_t failed_said;             /* 77: the disk refused, and it has said so */
    uint8_t retry; /* 78: the blocks and state of a write that did not finish are still to go, as they were */
    uint8_t write_has_blocks;     /* 79: under writing: this write goes on to the blocks */
    job_system_t *borrowed_job;   /* 80 */
    save_world_t *borrowed_world; /* 88 */
    save_state_t *borrowed_state; /* 96 */
    uint64_t next_tick;           /* 104: when one is due */
    uint64_t interval_ticks;      /* 112 */
    uint64_t write_generation;    /* 120: under writing: of the save being made */
    uint64_t saves_done;          /* 128: the worker's, read under writing */
    uint64_t saves_failed;        /* 136: the same */
    int64_t capture_ns_most;      /* 144: the longest the sim gave one */
    int64_t write_ns_most;        /* 152: the longest a worker gave one: the worker's, read under writing */
    uint8_t outstanding;          /* 160: a write has been handed over, and what it said has not been looked at */
    uint8_t _pad0[31];            /* 161 */
} save_autosave_t;
static_assert(sizeof(save_autosave_t) == 192);
static_assert(alignof(save_autosave_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(save_autosave_t, group) == 0);
static_assert(offsetof(save_autosave_t, writing) == 64);
static_assert(offsetof(save_autosave_t, written_status) == 68);
static_assert(offsetof(save_autosave_t, started) == 72);
static_assert(offsetof(save_autosave_t, requested) == 76);
static_assert(offsetof(save_autosave_t, failed_said) == 77);
static_assert(offsetof(save_autosave_t, retry) == 78);
static_assert(offsetof(save_autosave_t, write_has_blocks) == 79);
static_assert(offsetof(save_autosave_t, borrowed_job) == 80);
static_assert(offsetof(save_autosave_t, borrowed_world) == 88);
static_assert(offsetof(save_autosave_t, borrowed_state) == 96);
static_assert(offsetof(save_autosave_t, next_tick) == 104);
static_assert(offsetof(save_autosave_t, interval_ticks) == 112);
static_assert(offsetof(save_autosave_t, write_generation) == 120);
static_assert(offsetof(save_autosave_t, saves_done) == 128);
static_assert(offsetof(save_autosave_t, saves_failed) == 136);
static_assert(offsetof(save_autosave_t, capture_ns_most) == 144);
static_assert(offsetof(save_autosave_t, write_ns_most) == 152);
static_assert(offsetof(save_autosave_t, outstanding) == 160);
static_assert(offsetof(save_autosave_t, _pad0) == 161);

/* From now on saves come every SAVE_AUTOSAVE_INTERVAL_TICKS after this tick, by save_autosave_step. Before the threads start. All must outlive it. */
void save_autosave_start(save_autosave_t *autosave, job_system_t *job, save_world_t *world, save_state_t *state,
                         uint64_t tick);

/* Sets how often, from now on. A test's way in: the next one is still when it was due. */
void save_autosave_interval_set(save_autosave_t *autosave, uint64_t ticks);

/* Waits for a write under way, and no more come. Not on a worker. Safe on one not started. */
void save_autosave_stop(save_autosave_t *autosave);

/* A save is wanted as soon as it can be, not at the interval: a column cannot leave until there is room. The sim. While the disk is refusing it waits for the
 * interval all the same. */
void save_autosave_request(save_autosave_t *autosave);

/* Whether save_autosave_step would start a save at this tick, for whoever has something to finish first. By the sim, as that is. */
[[nodiscard]] bool save_autosave_due(save_autosave_t *autosave, uint64_t tick);

/* From each step of the sim, with the thread holding the map. Looks at what the last write said. If one is due and none is running, copies out
 * everything and hands the writing to a worker. Never waits, for a worker or a disk, and never touches the heap. */
void save_autosave_step(save_autosave_t *autosave, world_edit_t *edit, const save_state_parts_t *parts, uint64_t tick);

/* All there is, saved now on this thread, once any write has finished: for a world closing, with its threads stopped. Stops it. Anything but
 * SAVE_WORLD_OK has logged why, and what was last saved is as it was. */
[[nodiscard]] save_world_status_t save_autosave_finish(save_autosave_t *autosave, world_edit_t *edit,
                                                       const save_state_parts_t *parts);

#endif
