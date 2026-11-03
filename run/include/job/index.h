/* See run/src/job/README.md. */
#ifndef MC_RUN_JOB_INDEX_H
#define MC_RUN_JOB_INDEX_H

#include "arena/index.h"
#include "heap/index.h"
#include "machine/index.h"
#include "thread/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* The field-by-field layouts below are written for this line size, and arena_t already assumes it. */
static_assert(MC_CPU_CACHE_LINE_BYTES == 64);
static_assert(MC_THREADS_WORKERS > 0);

enum : uint64_t {
    /* Jobs waiting on one worker's own queue. A power of two. */
    JOB_QUEUE_CAPACITY = 4096u,
    /* Jobs waiting on the queue shared by everything that is not a worker. A power of two. */
    JOB_INJECTOR_CAPACITY = 16384u,
    /* One worker's scratch space per job. Virtual: only the pages a job touches cost anything. */
    JOB_ARENA_BYTES = 32ull * 1024u * 1024u,
};
enum : uint32_t {
    /* How deep job_spawn may run jobs on the spawner's own stack once every queue is full. */
    JOB_INLINE_DEPTH_MAX = 32u,
};
static_assert((JOB_QUEUE_CAPACITY & (JOB_QUEUE_CAPACITY - 1u)) == 0);
static_assert((JOB_INJECTOR_CAPACITY & (JOB_INJECTOR_CAPACITY - 1u)) == 0);
static_assert(JOB_ARENA_BYTES % HEAP_REGION_ALIGNMENT == 0);

typedef struct job_system_t job_system_t;
typedef struct job_worker_t job_worker_t;

/* What a job is. worker is the one running it, for worker->arena_job and job_spawn; it is good only until the function
 * returns. borrowed_argument must stay valid until the job's group finishes. */
typedef void (*job_function_t)(job_worker_t *worker, void *borrowed_argument);

/* Counts jobs not yet finished, so that one thread can wait for all of them. Its own cache line, since every worker that
 * finishes a job writes here. */
typedef struct job_group_t {
    alignas(
        MC_CPU_CACHE_LINE_BYTES) _Atomic uint32_t state; /* unfinished jobs, and the top bit if someone is waiting */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 4];
} job_group_t;
static_assert(sizeof(job_group_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(job_group_t) == MC_CPU_CACHE_LINE_BYTES);

/* One queued job, as functions pass it. */
typedef struct job_t {
    job_function_t function;
    void *borrowed_argument;
    job_group_t *borrowed_group;
} job_t;
static_assert(sizeof(job_t) == 24);

/* One entry of a worker's queue. Atomic, because a thief reads an entry before it knows whether the entry is still its to
 * take, and the owner can be writing that same entry for a later job by then. The thief's compare-exchange then fails and
 * it throws away what it read. */
typedef struct job_slot_t {
    _Atomic(job_function_t) function;
    void *_Atomic borrowed_argument;
    _Atomic(job_group_t *) borrowed_group;
} job_slot_t;
static_assert(sizeof(job_slot_t) == 24);

/* One entry of the shared queue. sequence says whose turn it is, so job needs no atomics: exactly one thread has it. */
typedef struct job_cell_t {
    _Atomic uint64_t sequence;
    job_t job;
} job_cell_t;
static_assert(sizeof(job_cell_t) == 32);

/* One worker thread. Jobs may use arena_job and index. Everything else belongs to this module's functions. */
typedef struct job_worker_t {
    /* Line 0: written by this worker alone. Thieves read bottom, and job_stats_read the counters. */
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic int64_t bottom; /* one past the newest job in this worker's queue */
    _Atomic uint64_t jobs_run;
    _Atomic uint64_t jobs_stolen;
    _Atomic uint64_t jobs_from_injector;
    _Atomic uint64_t jobs_inlined;
    _Atomic uint64_t sleeps;
    uint64_t random;       /* where the next round of stealing starts */
    uint32_t inline_depth; /* jobs running on this worker's stack right now */
    uint32_t index;        /* 0 to MC_THREADS_WORKERS - 1, fixed for the system's life */

    /* Line 1: written by any thief. */
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic int64_t top; /* the oldest job in this worker's queue */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 8];

    /* Line 2: this worker's Job arena. Reset after every job, so a job finds it empty and nothing in it lasts. */
    arena_t arena_job;

    /* Line 3: set by job_init and only read after it. */
    job_system_t *borrowed_system;
    job_slot_t *borrowed_slots;
    thread_t thread;
} job_worker_t;
static_assert(sizeof(job_worker_t) == 4 * MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(job_worker_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(job_worker_t, top) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(job_worker_t, arena_job) == 2 * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(job_worker_t, borrowed_system) == 3 * MC_CPU_CACHE_LINE_BYTES);

/* All the workers, on memory this holds. Must stay where it is from job_init to job_deinit. */
typedef struct job_system_t {
    /* Written by anything submitting. */
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic uint64_t injector_tail;
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 8];

    /* Written by any worker taking from the shared queue. */
    _Atomic uint64_t injector_head;
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 8];

    /* Written only around sleeping, and read by every submit. */
    _Atomic uint32_t wake_epoch;    /* the futex idle workers sleep on */
    _Atomic uint32_t sleeper_count; /* workers asleep, or about to be */
    uint8_t _pad2[MC_CPU_CACHE_LINE_BYTES - 8];

    /* Set by job_init, apart from stopping, and only read after it. */
    heap_region_t owned_region;
    job_worker_t *borrowed_workers; /* MC_THREADS_WORKERS, in owned_region */
    job_cell_t *borrowed_cells;     /* JOB_INJECTOR_CAPACITY, in owned_region */
    _Atomic uint8_t stopping;
    uint8_t _pad3[3];
    uint32_t worker_count; /* how many of borrowed_workers run: from 1 to MC_THREADS_WORKERS */
    uint8_t _pad4[MC_CPU_CACHE_LINE_BYTES - 40];
} job_system_t;
static_assert(sizeof(job_system_t) == 4 * MC_CPU_CACHE_LINE_BYTES);
static_assert(alignof(job_system_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(job_system_t, injector_head) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(job_system_t, wake_epoch) == 2 * MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(job_system_t, owned_region) == 3 * MC_CPU_CACHE_LINE_BYTES);

/* What job_init reserves, laid out in this order. The arenas start on a huge page each, so that each worker's first touch
 * faults in its own huge page. */
enum : uint64_t {
    JOB_WORKERS_BYTES = (uint64_t)MC_THREADS_WORKERS * sizeof(job_worker_t),
    JOB_CELLS_BYTES = JOB_INJECTOR_CAPACITY * sizeof(job_cell_t),
    JOB_SLOTS_BYTES = (uint64_t)MC_THREADS_WORKERS * JOB_QUEUE_CAPACITY * sizeof(job_slot_t),
    JOB_TABLES_BYTES = JOB_WORKERS_BYTES + JOB_CELLS_BYTES + JOB_SLOTS_BYTES,
    JOB_ARENAS_OFFSET = (JOB_TABLES_BYTES + HEAP_REGION_ALIGNMENT - 1u) / HEAP_REGION_ALIGNMENT * HEAP_REGION_ALIGNMENT,
    JOB_REGION_BYTES = JOB_ARENAS_OFFSET + (uint64_t)MC_THREADS_WORKERS * JOB_ARENA_BYTES,
};

/* One worker's counters since job_init. */
typedef struct job_stats_t {
    uint64_t jobs_run;           /* finished, however it got them */
    uint64_t jobs_stolen;        /* taken from another worker's queue */
    uint64_t jobs_from_injector; /* taken from the shared queue */
    uint64_t jobs_inlined;       /* run on job_spawn's stack, because every queue was full */
    uint64_t sleeps;             /* times it went idle long enough to ask the kernel to put it to sleep */
    uint64_t arena_resets;       /* of its Job arena */
    uint64_t arena_peak;         /* the most of its Job arena any job used */
    uint64_t cpu;                /* what it is pinned to */
} job_stats_t;
static_assert(sizeof(job_stats_t) == 64);

/* Reserves all the memory and starts MC_THREADS_WORKERS pinned workers, which sleep until there is work. On failure logs
 * why, leaves nothing running or reserved, and returns false. */
[[nodiscard]] bool job_init(job_system_t *system, const thread_topology_t *topology);

/* The same with fewer workers, for the setting: from 1 to MC_THREADS_WORKERS, and anything else is that limit's nearest. All the memory
 * is still reserved, since only pages touched cost anything, and it is the system's for its whole life. */
[[nodiscard]] bool job_init_count(job_system_t *system, const thread_topology_t *topology, uint32_t worker_count);

/* Runs what is still queued, including whatever those jobs spawn, then stops the workers and releases everything.
 * Nothing may submit from outside a job once this has been called. Does nothing on a system that is not running. */
void job_deinit(job_system_t *system);

/* A group starts with no jobs. It may be used for any number of rounds of submitting and waiting. */
void job_group_init(job_group_t *group);
/* Aborts if a job in it has not finished: its memory is about to go, and the job will write to it. */
void job_group_deinit(job_group_t *group);

/* Any thread. Queues a job on the shared queue and wakes a worker. Returns false, having done nothing, if that queue is
 * full. */
[[nodiscard]] bool job_try_submit(job_system_t *system, job_group_t *group, job_function_t function,
                                  void *borrowed_argument);
/* Any thread that is not a worker. The same, but when the queue is full it waits for room. */
void job_submit(job_system_t *system, job_group_t *group, job_function_t function, void *borrowed_argument);

/* From inside a job, with the worker it was given. Queues a job on this worker's own queue, where it runs next unless
 * another worker steals it. Spawning into the job's own group keeps that group open until the new job is done too.
 *
 * Never fails. With this queue and the shared one both full, it runs the new job here and now, on this stack and in this
 * job's arena, which is not reset until the outermost job ends. Past JOB_INLINE_DEPTH_MAX of those it aborts: the spawned
 * jobs are spawning without end, and that is a bug in them. */
void job_spawn(job_worker_t *worker, job_group_t *group, job_function_t function, void *borrowed_argument);

/* Sleeps until every job in the group has finished. Any thread that is not a worker, and one at a time for each group.
 * On a worker it aborts: if every worker did that, none would be left to run the jobs. A job that needs more done after
 * its children finish spawns a job for that, or leaves it to whoever waits on the group. */
void job_group_wait(const job_system_t *system, job_group_t *group);

/* Any thread. Each field is exact, but an idle moment is the only time they all fit together. An index past the workers
 * asserts. The arena fields are good only while no job can be running: after job_group_wait, with nothing else queued. */
void job_stats_read(const job_system_t *system, uint32_t worker_index, job_stats_t *out);

/* Any thread. How many workers are asleep or just about to be, for tests and the debug overlay. */
uint32_t job_sleeper_count(const job_system_t *system);

#endif
