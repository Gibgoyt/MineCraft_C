/* See README.md. The orderings below come from Le, Pop, Cohen and Zappa Nardelli, "Correct and Efficient Work-Stealing for
 * Weak Memory Models" (PPoPP 2013), for a worker's own queue, and from Vyukov's bounded multi-producer multi-consumer
 * queue for the shared one. */
#include "job/index.h"

#include "assert/index.h"
#include "time/index.h"

#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

enum : uint32_t {
    JOB_GROUP_WAITER = 1u << 31,
    JOB_GROUP_COUNT_MASK = (1u << 31) - 1u,
    /* Rounds of looking for work, each with a sched_yield, before an idle worker asks to sleep. A round costs a few
     * cache misses, so this keeps a worker ready through a short gap without spinning through a long one. */
    JOB_IDLE_ROUNDS = 64u,
};

/* ---- Futex ------------------------------------------------------------------------------------------------------- */

/* Sleeps while *word == expected, and for at most timeout_ns when that is above zero. Returning for no reason is allowed, so
 * every caller loops. */
static void job_futex_wait(_Atomic uint32_t *word, uint32_t expected, int64_t timeout_ns) {
    const struct timespec timeout = {
        .tv_sec = timeout_ns / TIME_NS_PER_SECOND,
        .tv_nsec = timeout_ns % TIME_NS_PER_SECOND,
    };
    const int64_t result = syscall(SYS_futex, (uint32_t *)word, FUTEX_WAIT_PRIVATE, expected,
                                   timeout_ns > 0 ? &timeout : nullptr, nullptr, 0);
    /* EAGAIN: it had already changed. EINTR: a signal. ETIMEDOUT: the time was up. Anything else is a bad address or a bad
     * call: our bug. */
    ASSERT_ALWAYS(result == 0 || errno == EAGAIN || errno == EINTR || errno == ETIMEDOUT);
}

/* Takes only the address and never reads through it, so this is safe on a group whose owner has already seen it reach zero
 * and let it go: at worst the kernel reports a fault, or wakes whatever now sleeps there, and every futex waiter loops. */
static void job_futex_wake(_Atomic uint32_t *word, int32_t count) {
    (void)syscall(SYS_futex, (uint32_t *)word, FUTEX_WAKE_PRIVATE, count, nullptr, nullptr, 0);
}

/* ---- Groups ------------------------------------------------------------------------------------------------------ */

void job_group_init(job_group_t *group) {
    atomic_init(&group->state, 0u);
    memset(group->_pad0, 0, sizeof(group->_pad0));
}

void job_group_deinit(job_group_t *group) {
    ASSERT_ALWAYS((atomic_load_explicit(&group->state, memory_order_acquire) & JOB_GROUP_COUNT_MASK) == 0);
}

/* Relaxed: the job itself carries everything to the worker, and a waiter only needs to see the count fall to zero, which
 * cannot happen before this add while the caller is the one thread submitting or a job that the group still counts. */
static void job_group_add(job_group_t *group) {
    const uint32_t before = atomic_fetch_add_explicit(&group->state, 1u, memory_order_relaxed);
    ASSERT_ALWAYS((before & JOB_GROUP_COUNT_MASK) != JOB_GROUP_COUNT_MASK);
}

/* The state is one word so that this is one operation. Once the count reaches zero the waiter may free the group, so
 * nothing past the fetch_sub may read it, and a separate waiter flag would have to be read after. Release so that a job's
 * writes reach the waiter, acquire so that the last one to finish holds all of them. */
static void job_group_finish(job_group_t *group) {
    const uint32_t before = atomic_fetch_sub_explicit(&group->state, 1u, memory_order_acq_rel);
    ASSERT_ALWAYS((before & JOB_GROUP_COUNT_MASK) != 0);
    if ((before & JOB_GROUP_COUNT_MASK) == 1u && (before & JOB_GROUP_WAITER) != 0) {
        job_futex_wake(&group->state, INT32_MAX);
    }
}

void job_group_wait(const job_system_t *system, job_group_t *group) {
    const pthread_t self = pthread_self();
    for (uint32_t i = 0; i < system->worker_count; i++) {
        ASSERT_ALWAYS(!pthread_equal(self, system->borrowed_workers[i].thread.handle));
    }
    bool flagged = false;
    while (true) {
        uint32_t state = atomic_load_explicit(&group->state, memory_order_acquire);
        if ((state & JOB_GROUP_COUNT_MASK) == 0) {
            break;
        }
        ASSERT_ALWAYS(flagged || (state & JOB_GROUP_WAITER) == 0); /* one waiter per group */
        if ((state & JOB_GROUP_WAITER) == 0) {
            if (!atomic_compare_exchange_weak_explicit(&group->state, &state, state | JOB_GROUP_WAITER,
                                                       memory_order_relaxed, memory_order_relaxed)) {
                continue;
            }
            state |= JOB_GROUP_WAITER;
            flagged = true;
        }
        job_futex_wait(&group->state, state, 0);
    }
    if (flagged) {
        atomic_fetch_and_explicit(&group->state, ~(uint32_t)JOB_GROUP_WAITER, memory_order_relaxed);
    }
}

/* ---- Sleeping ---------------------------------------------------------------------------------------------------- */

/* The fence here and the one in job_worker_sleep make a pair. Either this one comes first, and the worker's scan sees the
 * job just queued and stays up, or that one does, and this load sees the worker's count. The epoch moves before the wake
 * so that a worker between its scan and its futex call returns at once. */
static void job_wake_one(job_system_t *system) {
    thread_fence_seq_cst();
    if (atomic_load_explicit(&system->sleeper_count, memory_order_relaxed) != 0) {
        atomic_fetch_add_explicit(&system->wake_epoch, 1u, memory_order_release);
        job_futex_wake(&system->wake_epoch, 1);
    }
}

static bool job_injector_empty(const job_injector_t *injector) {
    return atomic_load_explicit(&injector->tail, memory_order_relaxed) ==
           atomic_load_explicit(&injector->head, memory_order_relaxed);
}

/* The most workers a priority may hold right now. A cap holds while a job of a higher priority is queued or running, and
 * for JOB_CAP_HOLD_NS after: then the rest of the workers stay free for what the player needs next. With nothing above it
 * for that long, a capped priority may have every worker, so that the far field alone on the machine is not made to crawl.
 * The clock is read only once nothing above is queued or running. */
static uint32_t job_cap_now(const job_system_t *system, uint32_t priority) {
    const job_injector_t *const injector = &system->borrowed_injectors[priority];
    if (injector->cap >= system->worker_count) {
        return injector->cap;
    }
    int64_t busy_ns = INT64_MIN;
    for (uint32_t p = 0; p < priority; p++) {
        const job_injector_t *const above = &system->borrowed_injectors[p];
        if (atomic_load_explicit(&above->running, memory_order_relaxed) != 0u || !job_injector_empty(above)) {
            return injector->cap;
        }
        const int64_t above_ns = atomic_load_explicit(&above->busy_ns, memory_order_relaxed);
        busy_ns = above_ns > busy_ns ? above_ns : busy_ns;
    }
    if (busy_ns != INT64_MIN && time_now_ns() - busy_ns < JOB_CAP_HOLD_NS) {
        return injector->cap;
    }
    return system->worker_count;
}

/* How long until a hold alone keeps a worker from a capped queue that has jobs: then no job finishing will come to wake one
 * when the hold runs out, so a worker going to sleep sets an alarm for it. Zero when no hold is all that stands in the way. */
static int64_t job_hold_left_ns(const job_system_t *system) {
    int64_t left_ns = 0;
    for (uint32_t priority = 0; priority < JOB_PRIORITY_COUNT; priority++) {
        const job_injector_t *const injector = &system->borrowed_injectors[priority];
        if (injector->cap >= system->worker_count || job_injector_empty(injector) ||
            atomic_load_explicit(&injector->running, memory_order_relaxed) < injector->cap) {
            continue; /* uncapped, empty, or with room even under its cap */
        }
        int64_t busy_ns = INT64_MIN;
        bool above_active = false;
        for (uint32_t p = 0; p < priority && !above_active; p++) {
            const job_injector_t *const above = &system->borrowed_injectors[p];
            above_active =
                atomic_load_explicit(&above->running, memory_order_relaxed) != 0u || !job_injector_empty(above);
            const int64_t above_ns = atomic_load_explicit(&above->busy_ns, memory_order_relaxed);
            busy_ns = above_ns > busy_ns ? above_ns : busy_ns;
        }
        if (above_active || busy_ns == INT64_MIN) {
            continue; /* a job above finishing wakes a worker for it, in job_run */
        }
        const int64_t until_ns = JOB_CAP_HOLD_NS - (time_now_ns() - busy_ns);
        if (until_ns > 0 && (left_ns == 0 || until_ns < left_ns)) {
            left_ns = until_ns;
        }
    }
    return left_ns;
}

/* For job_cap_now's hold. Only the queues below a capped one need it, and a store of the time is cheap beside a job. */
static void job_busy_note(job_system_t *system, uint32_t priority) {
    atomic_store_explicit(&system->borrowed_injectors[priority].busy_ns, time_now_ns(), memory_order_relaxed);
}

/* A capped queue with jobs and room under its cap now, as a job finishing anywhere may be what made the room. Only then is a
 * worker woken, so that a worker the cap keeps out is not woken by every job that finishes, only to sleep again. */
static bool job_capped_room(const job_system_t *system) {
    for (uint32_t p = 0; p < JOB_PRIORITY_COUNT; p++) {
        const job_injector_t *const injector = &system->borrowed_injectors[p];
        if (injector->cap < system->worker_count && !job_injector_empty(injector) &&
            atomic_load_explicit(&injector->running, memory_order_relaxed) < job_cap_now(system, p)) {
            return true;
        }
    }
    return false;
}

/* Looks without taking. Any job this finds is one a worker should stay up for. A capped queue with all its workers busy has
 * nothing for this one: whichever job finishes first and lets one more in wakes a worker for it, in job_run. */
static bool job_any_queued(const job_system_t *system) {
    for (uint32_t p = 0; p < JOB_PRIORITY_COUNT; p++) {
        const job_injector_t *const injector = &system->borrowed_injectors[p];
        if (!job_injector_empty(injector) &&
            atomic_load_explicit(&injector->running, memory_order_relaxed) < job_cap_now(system, p)) {
            return true;
        }
    }
    for (uint32_t i = 0; i < system->worker_count; i++) {
        const job_worker_t *const worker = &system->borrowed_workers[i];
        if (atomic_load_explicit(&worker->bottom, memory_order_relaxed) >
            atomic_load_explicit(&worker->top, memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

/* Only one thread writes each counter, so it needs no read-modify-write and pays for none. */
static void job_count(_Atomic uint64_t *counter) {
    atomic_store_explicit(counter, atomic_load_explicit(counter, memory_order_relaxed) + 1u, memory_order_relaxed);
}

/* The epoch is read before anything else. Whatever wakes after that read moves it, so the futex call returns at once. An
 * acquire load that sees a later epoch also sees the job or the stopping flag that moved it. */
static void job_worker_sleep(job_worker_t *worker) {
    job_system_t *const system = worker->borrowed_system;
    const uint32_t epoch = atomic_load_explicit(&system->wake_epoch, memory_order_acquire);
    atomic_fetch_add_explicit(&system->sleeper_count, 1u, memory_order_relaxed);
    thread_fence_seq_cst();
    if (!job_any_queued(system) && atomic_load_explicit(&system->stopping, memory_order_acquire) == 0) {
        job_count(&worker->sleeps);
        job_futex_wait(&system->wake_epoch, epoch, job_hold_left_ns(system));
    }
    atomic_fetch_sub_explicit(&system->sleeper_count, 1u, memory_order_relaxed);
}

/* ---- A worker's own queue ---------------------------------------------------------------------------------------- */

/* Owner only. */
static bool job_queue_push(job_worker_t *worker, job_t job) {
    const int64_t bottom = atomic_load_explicit(&worker->bottom, memory_order_relaxed);
    const int64_t top = atomic_load_explicit(&worker->top, memory_order_acquire);
    if ((uint64_t)(bottom - top) >= JOB_QUEUE_CAPACITY) {
        return false;
    }
    job_slot_t *const slot = &worker->borrowed_slots[(uint64_t)bottom & (JOB_QUEUE_CAPACITY - 1u)];
    atomic_store_explicit(&slot->function, job.function, memory_order_relaxed);
    atomic_store_explicit(&slot->borrowed_argument, job.borrowed_argument, memory_order_relaxed);
    atomic_store_explicit(&slot->borrowed_group, job.borrowed_group, memory_order_relaxed);
    atomic_store_explicit(&slot->tag, (uint64_t)job.priority | (uint64_t)job.epoch << 32, memory_order_relaxed);
    atomic_store_explicit(&worker->bottom, bottom + 1, memory_order_release);
    return true;
}

static job_t job_slot_read(job_slot_t *slot) {
    const uint64_t tag = atomic_load_explicit(&slot->tag, memory_order_relaxed);
    return (job_t){
        .function = atomic_load_explicit(&slot->function, memory_order_relaxed),
        .borrowed_argument = atomic_load_explicit(&slot->borrowed_argument, memory_order_relaxed),
        .borrowed_group = atomic_load_explicit(&slot->borrowed_group, memory_order_relaxed),
        .priority = (uint32_t)tag,
        .epoch = (uint32_t)(tag >> 32),
    };
}

/* Owner only. The priority of the job job_queue_pop would take, or JOB_PRIORITY_COUNT with none. Only the owner writes the
 * slots, so what this reads is that job's, unless a thief takes it first, and then the pop finds another or none. */
static uint32_t job_queue_peek_priority(job_worker_t *worker) {
    const int64_t bottom = atomic_load_explicit(&worker->bottom, memory_order_relaxed);
    const int64_t top = atomic_load_explicit(&worker->top, memory_order_relaxed);
    if (top >= bottom) {
        return JOB_PRIORITY_COUNT;
    }
    const job_slot_t *const slot = &worker->borrowed_slots[(uint64_t)(bottom - 1) & (JOB_QUEUE_CAPACITY - 1u)];
    return (uint32_t)atomic_load_explicit(&slot->tag, memory_order_relaxed);
}

/* Owner only. The newest job, whose memory is the most likely to still be in this core's cache.
 *
 * Differs from the paper in one way. The paper's stores to bottom here are relaxed and rely on a later store by the same
 * thread continuing a release sequence, which C11 allowed and C23 no longer does. So they are release stores, which cost
 * nothing on x86, and a thief's acquire load of bottom synchronises with whichever store it reads. */
static bool job_queue_pop(job_worker_t *worker, job_t *out) {
    const int64_t bottom = atomic_load_explicit(&worker->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&worker->bottom, bottom, memory_order_release);
    thread_fence_seq_cst();
    int64_t top = atomic_load_explicit(&worker->top, memory_order_relaxed);
    if (top > bottom) {
        atomic_store_explicit(&worker->bottom, bottom + 1, memory_order_release);
        return false;
    }
    *out = job_slot_read(&worker->borrowed_slots[(uint64_t)bottom & (JOB_QUEUE_CAPACITY - 1u)]);
    if (top < bottom) {
        return true;
    }
    /* The last job, so a thief may be after it too. Whoever moves top has it. */
    const bool won = atomic_compare_exchange_strong_explicit(&worker->top, &top, top + 1, memory_order_seq_cst,
                                                             memory_order_relaxed);
    atomic_store_explicit(&worker->bottom, bottom + 1, memory_order_release);
    return won;
}

/* Any other worker. The oldest job, which tends to be the largest piece of whatever the owner is working through. The
 * owner never overwrites the entry at top until top has moved past it, so if the compare-exchange succeeds then what was
 * read is the job, and if it fails it is thrown away. */
static bool job_queue_steal(job_worker_t *victim, job_t *out) {
    int64_t top = atomic_load_explicit(&victim->top, memory_order_acquire);
    thread_fence_seq_cst();
    const int64_t bottom = atomic_load_explicit(&victim->bottom, memory_order_acquire);
    if (top >= bottom) {
        return false;
    }
    *out = job_slot_read(&victim->borrowed_slots[(uint64_t)top & (JOB_QUEUE_CAPACITY - 1u)]);
    return atomic_compare_exchange_strong_explicit(&victim->top, &top, top + 1, memory_order_seq_cst,
                                                   memory_order_relaxed);
}

/* ---- The shared queues ------------------------------------------------------------------------------------------- */

/* A cell whose sequence equals the tail is free for that push. One whose sequence is the head plus one holds a job for that
 * pop, which then sets it a whole lap ahead, ready for the push that lands there next. */
static bool job_injector_push(job_system_t *system, job_t job) {
    job_injector_t *const injector = &system->borrowed_injectors[job.priority];
    uint64_t tail = atomic_load_explicit(&injector->tail, memory_order_relaxed);
    while (true) {
        job_cell_t *const cell = &injector->borrowed_cells[tail & (JOB_INJECTOR_CAPACITY - 1u)];
        const uint64_t sequence = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        if (sequence == tail) {
            if (atomic_compare_exchange_weak_explicit(&injector->tail, &tail, tail + 1u, memory_order_relaxed,
                                                      memory_order_relaxed)) {
                cell->job = job;
                atomic_store_explicit(&cell->sequence, tail + 1u, memory_order_release);
                return true;
            }
        } else if (sequence < tail) {
            return false;
        } else {
            tail = atomic_load_explicit(&injector->tail, memory_order_relaxed);
        }
    }
}

static bool job_injector_pop(job_injector_t *injector, job_t *out) {
    uint64_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    while (true) {
        job_cell_t *const cell = &injector->borrowed_cells[head & (JOB_INJECTOR_CAPACITY - 1u)];
        const uint64_t sequence = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        if (sequence == head + 1u) {
            if (atomic_compare_exchange_weak_explicit(&injector->head, &head, head + 1u, memory_order_relaxed,
                                                      memory_order_relaxed)) {
                *out = cell->job;
                atomic_store_explicit(&cell->sequence, head + JOB_INJECTOR_CAPACITY, memory_order_release);
                return true;
            }
        } else if (sequence < head + 1u) {
            return false;
        } else {
            head = atomic_load_explicit(&injector->head, memory_order_relaxed);
        }
    }
}

/* A place under the cap is claimed before the pop, so that two workers cannot both pass a cap with room for one. A pop that
 * then finds nothing gives the place back. Every job taken here is counted in running until job_run has finished it. */
static bool job_injector_take(job_system_t *system, uint32_t priority, job_t *out) {
    job_injector_t *const injector = &system->borrowed_injectors[priority];
    if (job_injector_empty(injector)) {
        return false;
    }
    const uint32_t cap = job_cap_now(system, priority);
    uint32_t running = atomic_load_explicit(&injector->running, memory_order_relaxed);
    do {
        if (running >= cap) {
            return false;
        }
    } while (!atomic_compare_exchange_weak_explicit(&injector->running, &running, running + 1u, memory_order_relaxed,
                                                    memory_order_relaxed));
    if (!job_injector_pop(injector, out)) {
        atomic_fetch_sub_explicit(&injector->running, 1u, memory_order_relaxed);
        return false;
    }
    /* A cap lifting makes room for many at once, and job_run wakes only one worker for it: each that takes a job wakes the
     * next while there is room, as a submit wakes one for each job. */
    if (injector->cap < system->worker_count && !job_injector_empty(injector) &&
        atomic_load_explicit(&injector->running, memory_order_relaxed) < job_cap_now(system, priority)) {
        job_wake_one(system);
    }
    return true;
}

bool job_try_submit_at(job_system_t *system, job_group_t *group, job_priority_t priority, job_function_t function,
                       void *borrowed_argument) {
    ASSERT_ALWAYS(priority < JOB_PRIORITY_COUNT);
    job_group_add(group);
    const job_t job = {
        .function = function,
        .borrowed_argument = borrowed_argument,
        .borrowed_group = group,
        .priority = priority,
        .epoch = atomic_load_explicit(&system->borrowed_injectors[priority].epoch, memory_order_relaxed),
    };
    if (!job_injector_push(system, job)) {
        job_group_finish(group);
        return false;
    }
    job_busy_note(system, priority);
    job_wake_one(system);
    return true;
}

void job_submit_at(job_system_t *system, job_group_t *group, job_priority_t priority, job_function_t function,
                   void *borrowed_argument) {
    while (!job_try_submit_at(system, group, priority, function, borrowed_argument)) {
        (void)sched_yield(); /* always succeeds on Linux, and there is nothing else to do either way */
    }
}

bool job_try_submit(job_system_t *system, job_group_t *group, job_function_t function, void *borrowed_argument) {
    return job_try_submit_at(system, group, JOB_PRIORITY_NORMAL, function, borrowed_argument);
}

void job_submit(job_system_t *system, job_group_t *group, job_function_t function, void *borrowed_argument) {
    job_submit_at(system, group, JOB_PRIORITY_NORMAL, function, borrowed_argument);
}

void job_priority_drop(job_system_t *system, job_priority_t priority) {
    ASSERT_ALWAYS(priority < JOB_PRIORITY_COUNT);
    atomic_fetch_add_explicit(&system->borrowed_injectors[priority].epoch, 1u, memory_order_relaxed);
}

bool job_dropped(const job_worker_t *worker) {
    const job_injector_t *const injector = &worker->borrowed_system->borrowed_injectors[worker->running_priority];
    return atomic_load_explicit(&injector->epoch, memory_order_relaxed) != worker->running_epoch;
}

/* ---- Running ----------------------------------------------------------------------------------------------------- */

/* The arena is reset before the group finishes: once a waiter wakes, everything this job did is over, and no later job
 * finds what this one left. The count goes first for the same reason, so job_stats_read is exact after a wait.
 *
 * from_injector says the job holds a place in its queue's running count. Giving it back may let a worker past a cap, its
 * own queue's or, if it was the last job above a capped queue, that one's, so one is woken if a capped queue has jobs and
 * room. The fence after giving it back pairs with the one in job_worker_sleep, so a worker that saw no room and went to
 * sleep is either seen by job_wake_one or sees the place given back. It also pairs with the same fence in a job finishing
 * at the same moment, so that of two last jobs above a capped queue, at least one sees the other gone. */
static void job_run(job_worker_t *worker, job_t job, bool from_injector) {
    worker->running_priority = job.priority;
    worker->running_epoch = job.epoch;
    PROFILER_BEGIN(job_worker_lane(worker), "job");
    job.function(worker, job.borrowed_argument);
    PROFILER_END(job_worker_lane(worker));
    arena_reset(&worker->arena_job);
    job_count(&worker->jobs_run);
    if (from_injector) {
        job_system_t *const system = worker->borrowed_system;
        job_injector_t *const injector = &system->borrowed_injectors[job.priority];
        job_busy_note(system, job.priority);
        atomic_fetch_sub_explicit(&injector->running, 1u, memory_order_relaxed);
        thread_fence_seq_cst();
        if (job_capped_room(system)) {
            job_wake_one(system);
        }
    }
    job_group_finish(job.borrowed_group);
}

bool job_spawn(job_worker_t *worker, job_group_t *group, job_function_t function, void *borrowed_argument) {
    job_group_add(group);
    const job_t job = {
        .function = function,
        .borrowed_argument = borrowed_argument,
        .borrowed_group = group,
        .priority = worker->running_priority,
        .epoch = worker->running_epoch,
    };
    if (job_queue_push(worker, job) || job_injector_push(worker->borrowed_system, job)) {
        job_wake_one(worker->borrowed_system);
        return true;
    }
    job_group_finish(group);
    job_count(&worker->jobs_refused);
    return false;
}

/* xorshift64: it only has to spread the workers' first choices apart, so that they do not all fall on one victim. */
static uint32_t job_next_victim(job_worker_t *worker) {
    uint64_t random = worker->random;
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    worker->random = random;
    return (uint32_t)(random % worker->borrowed_system->worker_count);
}

/* The shared queues of a higher priority than its own queue's newest job first, then its own queue, so that a worker
 * finishes what it started unless something more urgent waits. The other shared queues next, or workers with each other's
 * spawned jobs to steal would hold up what other threads have been waiting on. Stealing takes no account of priority: it
 * only happens once every shared queue is empty or capped. A lost race during a steal reads as no work: the winner made
 * progress, and the idle loop comes back. */
static bool job_find(job_worker_t *worker, job_t *out, bool *out_from_injector) {
    job_system_t *const system = worker->borrowed_system;
    const uint32_t own = job_queue_peek_priority(worker);
    *out_from_injector = true;
    for (uint32_t p = 0; p < own; p++) {
        if (job_injector_take(system, p, out)) {
            job_count(&worker->jobs_from_injector);
            return true;
        }
    }
    *out_from_injector = false;
    if (job_queue_pop(worker, out)) {
        return true;
    }
    *out_from_injector = true;
    for (uint32_t p = own; p < JOB_PRIORITY_COUNT; p++) {
        if (job_injector_take(system, p, out)) {
            job_count(&worker->jobs_from_injector);
            return true;
        }
    }
    *out_from_injector = false;
    uint32_t victim = job_next_victim(worker);
    for (uint32_t i = 0; i < system->worker_count; i++) {
        if (victim != worker->index && job_queue_steal(&system->borrowed_workers[victim], out)) {
            job_count(&worker->jobs_stolen);
            return true;
        }
        victim = victim + 1u == system->worker_count ? 0u : victim + 1u;
    }
    return false;
}

/* Stopping is read before looking for work, and everything queued from outside was queued before stopping was set. So a
 * worker that saw stopping and then found nothing leaves behind only jobs in the hands of another worker, and a worker
 * still running sees to whatever it queued itself before it leaves. */
static void job_worker_main(void *borrowed_argument) {
    job_worker_t *const worker = borrowed_argument;
    job_system_t *const system = worker->borrowed_system;
    uint32_t idle_rounds = 0;
    while (true) {
        const bool stopping = atomic_load_explicit(&system->stopping, memory_order_acquire) != 0;
        job_t job;
        bool from_injector = false;
        if (job_find(worker, &job, &from_injector)) {
            job_run(worker, job, from_injector);
            idle_rounds = 0;
        } else if (stopping) {
            return;
        } else if (idle_rounds < JOB_IDLE_ROUNDS) {
            idle_rounds++;
            (void)sched_yield();
        } else {
            job_worker_sleep(worker);
        }
    }
}

/* ---- Lifetime ---------------------------------------------------------------------------------------------------- */

bool job_init(job_system_t *system, const thread_topology_t *topology) {
    return job_init_count(system, topology, MC_THREADS_WORKERS);
}

bool job_init_count(job_system_t *system, const thread_topology_t *topology, uint32_t worker_count) {
    memset(system, 0, sizeof(*system));
    system->worker_count =
        worker_count == 0u ? 1u : (worker_count > MC_THREADS_WORKERS ? MC_THREADS_WORKERS : worker_count);
    if (!heap_reserve(JOB_REGION_BYTES, &system->owned_region)) {
        return false; /* heap_reserve said why */
    }
    uint8_t *const base = system->owned_region.owned_base;
    system->borrowed_injectors = (job_injector_t *)base;
    system->borrowed_workers = (job_worker_t *)(base + JOB_INJECTORS_BYTES);
    job_cell_t *const cells = (job_cell_t *)(base + JOB_INJECTORS_BYTES + JOB_WORKERS_BYTES);
    job_slot_t *const slots = (job_slot_t *)(base + JOB_INJECTORS_BYTES + JOB_WORKERS_BYTES + JOB_CELLS_BYTES);

    const uint32_t far_cap = (system->worker_count + JOB_FAR_WORKERS_DIVISOR - 1u) / JOB_FAR_WORKERS_DIVISOR;
    for (uint32_t p = 0; p < JOB_PRIORITY_COUNT; p++) {
        job_injector_t *const injector = &system->borrowed_injectors[p];
        injector->borrowed_cells = &cells[(uint64_t)p * JOB_INJECTOR_CAPACITY];
        injector->cap = p == JOB_PRIORITY_FAR ? far_cap : system->worker_count;
        atomic_init(&injector->busy_ns, INT64_MIN);
        for (uint64_t i = 0; i < JOB_INJECTOR_CAPACITY; i++) {
            atomic_init(&injector->borrowed_cells[i].sequence, i);
        }
    }
    for (uint32_t i = 0; i < system->worker_count; i++) {
        job_worker_t *const worker = &system->borrowed_workers[i];
        worker->index = i;
        worker->random = ((uint64_t)i + 1u) * 0x9E3779B97F4A7C15ull;
        worker->borrowed_system = system;
        worker->borrowed_slots = &slots[(uint64_t)i * JOB_QUEUE_CAPACITY];
        worker->running_priority = JOB_PRIORITY_NORMAL;
        arena_init(&worker->arena_job, "job", base + JOB_ARENAS_OFFSET + (uint64_t)i * JOB_ARENA_BYTES,
                   JOB_ARENA_BYTES);
    }
    /* In a second loop, since a worker that has started looks at every other worker's queue. */
    for (uint32_t i = 0; i < system->worker_count; i++) {
        job_worker_t *const worker = &system->borrowed_workers[i];
        if (!thread_start(&worker->thread, topology, THREAD_ROLE_WORKER, i, job_worker_main, worker)) {
            job_deinit(system);
            return false;
        }
    }
    return true;
}

void job_profiler_set(job_system_t *system, profiler_t *profiler) {
    system->borrowed_lanes = profiler_lanes_claim(profiler, "worker", system->worker_count);
}

void job_deinit(job_system_t *system) {
    if (system->owned_region.owned_base == nullptr) {
        return;
    }
    /* A sleeping worker reads the epoch before stopping, so moving the epoch after setting stopping cannot miss one. */
    atomic_store_explicit(&system->stopping, 1u, memory_order_seq_cst);
    atomic_fetch_add_explicit(&system->wake_epoch, 1u, memory_order_seq_cst);
    job_futex_wake(&system->wake_epoch, INT32_MAX);

    for (uint32_t i = 0; i < system->worker_count; i++) {
        thread_join(&system->borrowed_workers[i].thread);
    }
    /* Every worker left only when it could find nothing, so anything still here was lost, and its group would never end. */
    for (uint32_t p = 0; p < JOB_PRIORITY_COUNT; p++) {
        const job_injector_t *const injector = &system->borrowed_injectors[p];
        ASSERT_ALWAYS(atomic_load_explicit(&injector->tail, memory_order_acquire) ==
                      atomic_load_explicit(&injector->head, memory_order_acquire));
    }
    for (uint32_t i = 0; i < system->worker_count; i++) {
        job_worker_t *const worker = &system->borrowed_workers[i];
        ASSERT_ALWAYS(atomic_load_explicit(&worker->bottom, memory_order_acquire) ==
                      atomic_load_explicit(&worker->top, memory_order_acquire));
        arena_deinit(&worker->arena_job);
    }
    heap_release(&system->owned_region);
    memset(system, 0, sizeof(*system));
}

/* ---- Reading ----------------------------------------------------------------------------------------------------- */

void job_stats_read(const job_system_t *system, uint32_t worker_index, job_stats_t *out) {
    ASSERT_ALWAYS(worker_index < system->worker_count);
    const job_worker_t *const worker = &system->borrowed_workers[worker_index];
    *out = (job_stats_t){
        .jobs_run = atomic_load_explicit(&worker->jobs_run, memory_order_relaxed),
        .jobs_stolen = atomic_load_explicit(&worker->jobs_stolen, memory_order_relaxed),
        .jobs_from_injector = atomic_load_explicit(&worker->jobs_from_injector, memory_order_relaxed),
        .jobs_refused = atomic_load_explicit(&worker->jobs_refused, memory_order_relaxed),
        .sleeps = atomic_load_explicit(&worker->sleeps, memory_order_relaxed),
        .arena_resets = worker->arena_job.reset_count,
        .arena_peak = worker->arena_job.peak,
        .cpu = worker->thread.cpu,
    };
}

uint32_t job_sleeper_count(const job_system_t *system) {
    return atomic_load_explicit(&system->sleeper_count, memory_order_relaxed);
}

uint64_t job_priority_queued(const job_system_t *system, job_priority_t priority) {
    ASSERT_ALWAYS(priority < JOB_PRIORITY_COUNT);
    const job_injector_t *const injector = &system->borrowed_injectors[priority];
    const uint64_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    const uint64_t tail = atomic_load_explicit(&injector->tail, memory_order_relaxed);
    return tail > head ? tail - head : 0u; /* read apart, so a pop between the two can put head past the tail read */
}

uint32_t job_priority_running(const job_system_t *system, job_priority_t priority) {
    ASSERT_ALWAYS(priority < JOB_PRIORITY_COUNT);
    return atomic_load_explicit(&system->borrowed_injectors[priority].running, memory_order_relaxed);
}

uint32_t job_priority_cap(const job_system_t *system, job_priority_t priority) {
    ASSERT_ALWAYS(priority < JOB_PRIORITY_COUNT);
    return system->borrowed_injectors[priority].cap;
}
