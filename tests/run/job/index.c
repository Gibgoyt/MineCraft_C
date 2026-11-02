/* ctest run_job. See README.md. Jobs report through atomics and the test's main thread does the checking, because harness
 * checks are not thread-safe. */
#include "harness/index.h"

#include "assert/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#endif

static_assert(MC_THREADS_WORKERS <= 64); /* the worker masks below */

/* A job that waits on something gives up after this and reports it, so that the check can say what never happened. Work that
 * is lost altogether leaves main in job_group_wait, and it is the ctest timeout that ends that. */
static const int64_t WAIT_NS = 60 * TIME_NS_PER_SECOND;

static bool system_start(job_system_t *system) {
    thread_topology_t topology;
    return thread_topology_read(&topology) && job_init(system, &topology);
}

/* All workers added up, with the arena's peak as the highest and cpu left at zero. */
static job_stats_t stats_total(const job_system_t *system) {
    job_stats_t total;
    memset(&total, 0, sizeof(total));
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_stats_t one;
        job_stats_read(system, i, &one);
        total.jobs_run += one.jobs_run;
        total.jobs_stolen += one.jobs_stolen;
        total.jobs_from_injector += one.jobs_from_injector;
        total.jobs_inlined += one.jobs_inlined;
        total.sleeps += one.sleeps;
        total.arena_resets += one.arena_resets;
        total.arena_peak = one.arena_peak > total.arena_peak ? one.arena_peak : total.arena_peak;
    }
    return total;
}

static bool wait_for_all_asleep(const job_system_t *system) {
    const int64_t deadline = time_now_ns() + WAIT_NS;
    while (job_sleeper_count(system) != MC_THREADS_WORKERS) {
        if (time_now_ns() > deadline) {
            return false;
        }
        time_sleep_ns(TIME_NS_PER_MS);
    }
    return true;
}

static uint32_t count_bits(uint64_t bits) {
    uint32_t count = 0;
    while (bits != 0) {
        bits &= bits - 1u;
        count++;
    }
    return count;
}

/* Threads of this process, from the kernel. */
static uint32_t threads_alive(void) {
    FILE *const status = fopen("/proc/self/status", "r");
    if (status == nullptr) {
        return 0;
    }
    char line[256];
    uint32_t threads = 0;
    while (fgets(line, sizeof(line), status) != nullptr) {
        if (strncmp(line, "Threads:", 8) == 0) {
            threads = (uint32_t)strtoul(line + 8, nullptr, 10);
        }
    }
    (void)fclose(status);
    return threads;
}

/* pthread_join returns as the thread clears its id, a moment before the kernel takes it off the count, so the count is
 * allowed that moment to come right. Returns the last one read. */
static uint32_t threads_alive_settled(uint32_t expected) {
    const int64_t deadline = time_now_ns() + WAIT_NS;
    uint32_t threads = threads_alive();
    while (threads != expected && time_now_ns() < deadline) {
        time_sleep_ns(TIME_NS_PER_MS);
        threads = threads_alive();
    }
    return threads;
}

/* ---- Every worker, at the same moment ---------------------------------------------------------------------------- */

typedef struct barrier_t {
    _Atomic uint64_t seen;       /* a bit per worker */
    _Atomic uint64_t arena_mine; /* a bit per worker whose arena was its own, empty, and a whole budget */
    _Atomic uint32_t arrived;
    _Atomic uint32_t wrong_cpu;
    _Atomic uint32_t timed_out;
    uint32_t _pad0;
} barrier_t;
static_assert(sizeof(barrier_t) == 32);

static void job_barrier(job_worker_t *worker, void *borrowed_argument) {
    barrier_t *const barrier = borrowed_argument;
    if ((uint64_t)sched_getcpu() != worker->thread.cpu) {
        atomic_fetch_add(&barrier->wrong_cpu, 1u);
    }
    if (worker->arena_job.used == 0 && worker->arena_job.capacity == JOB_ARENA_BYTES) {
        atomic_fetch_or(&barrier->arena_mine, 1ull << worker->index);
    }
    atomic_fetch_or(&barrier->seen, 1ull << worker->index);
    atomic_fetch_add(&barrier->arrived, 1u);
    const int64_t deadline = time_now_ns() + WAIT_NS;
    while (atomic_load(&barrier->arrived) < MC_THREADS_WORKERS) {
        if (time_now_ns() > deadline) {
            atomic_fetch_add(&barrier->timed_out, 1u);
            return;
        }
        (void)sched_yield();
    }
}

/* Nobody can leave until all have arrived, so it takes every worker awake and running at once. From all asleep, that means
 * each submit really woke one. */
TEST(every_worker_runs_at_once_from_asleep_and_on_its_own_cpu) {
    job_system_t system;
    REQUIRE(system_start(&system));
    CHECK(wait_for_all_asleep(&system));

    barrier_t barrier;
    memset(&barrier, 0, sizeof(barrier));
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_submit(&system, &group, job_barrier, &barrier);
    }
    job_group_wait(&system, &group);
    job_group_deinit(&group);

    const uint64_t all = MC_THREADS_WORKERS == 64 ? UINT64_MAX : (1ull << MC_THREADS_WORKERS) - 1u;
    CHECK_EQ_U64(atomic_load(&barrier.timed_out), 0u);
    CHECK_EQ_U64(atomic_load(&barrier.seen), all);
    CHECK_EQ_U64(atomic_load(&barrier.arena_mine), all);
    CHECK_EQ_U64(atomic_load(&barrier.wrong_cpu), 0u);

    /* Their arenas are slices that do not overlap. */
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        const uint8_t *const base = system.borrowed_workers[i].arena_job.borrowed_base;
        for (uint32_t j = i + 1u; j < MC_THREADS_WORKERS; j++) {
            const uint8_t *const other = system.borrowed_workers[j].arena_job.borrowed_base;
            CHECK(base + JOB_ARENA_BYTES <= other || other + JOB_ARENA_BYTES <= base);
        }
    }
    job_deinit(&system);
}

/* ---- The stress test --------------------------------------------------------------------------------------------- */

enum : uint32_t {
    STRESS_PRODUCERS = 3u,
    STRESS_JOBS_PER_PRODUCER = 500000u,
    STRESS_TREE_FAN = 4u,
    STRESS_TREE_NODES = 87381u, /* nine levels of four */
    STRESS_TREE_ROUNDS = 20u,
};

typedef struct stress_shared_t {
    _Atomic uint64_t arena_not_empty; /* a job found what another left */
    _Atomic uint64_t arena_corrupted; /* a job's arena memory changed under it */
} stress_shared_t;
static_assert(sizeof(stress_shared_t) == 16);

typedef struct stress_item_t {
    stress_shared_t *borrowed_shared;
    _Atomic uint32_t hits;
    uint32_t index;
} stress_item_t;
static_assert(sizeof(stress_item_t) == 16);

/* Takes some of the arena and fills it, then yields so that others can run, and then finds it unchanged. Sizes vary, so an
 * arena that was not reset, or was shared, comes apart. */
static void job_stress(job_worker_t *worker, void *borrowed_argument) {
    stress_item_t *const item = borrowed_argument;
    stress_shared_t *const shared = item->borrowed_shared;
    if (worker->arena_job.used != 0) {
        atomic_fetch_add(&shared->arena_not_empty, 1u);
    }
    const uint64_t bytes = 64u + (uint64_t)(item->index % 16u) * 256u;
    uint8_t *const block = arena_alloc(&worker->arena_job, bytes, 64u);
    const uint8_t mark = (uint8_t)(item->index * 31u + 1u);
    memset(block, mark, bytes);
    if (item->index % 256u == 0u) {
        (void)sched_yield();
    }
    for (uint64_t i = 0; i < bytes; i += 32u) {
        if (block[i] != mark) {
            atomic_fetch_add(&shared->arena_corrupted, 1u);
            break;
        }
    }
    atomic_fetch_add(&item->hits, 1u);
}

typedef struct stress_producer_t {
    thread_t thread;
    job_system_t *borrowed_system;
    stress_item_t *borrowed_items;
    uint64_t count;
} stress_producer_t;
static_assert(sizeof(stress_producer_t) == 72);

/* A thread outside the workers, as sim and gpu are, with its own group. */
static void stress_producer_main(void *borrowed_argument) {
    stress_producer_t *const producer = borrowed_argument;
    job_group_t group;
    job_group_init(&group);
    for (uint64_t i = 0; i < producer->count; i++) {
        job_submit(producer->borrowed_system, &group, job_stress, &producer->borrowed_items[i]);
    }
    job_group_wait(producer->borrowed_system, &group);
    job_group_deinit(&group);
}

typedef struct tree_t tree_t;

typedef struct tree_node_t {
    tree_t *borrowed_tree;
    _Atomic uint32_t visits;
    uint32_t index;
} tree_node_t;
static_assert(sizeof(tree_node_t) == 16);

typedef struct tree_t {
    tree_node_t *borrowed_nodes;
    job_group_t *borrowed_group;
} tree_t;
static_assert(sizeof(tree_t) == 16);

/* Each node spawns its children, so most queues run down to one entry over and over, and each of those is a race between
 * the owner's pop and a thief. Children count in the same group, which must stay open until the last leaf. */
static void job_tree(job_worker_t *worker, void *borrowed_argument) {
    tree_node_t *const node = borrowed_argument;
    tree_t *const tree = node->borrowed_tree;
    atomic_fetch_add(&node->visits, 1u);
    for (uint32_t i = 1u; i <= STRESS_TREE_FAN; i++) {
        const uint64_t child = (uint64_t)node->index * STRESS_TREE_FAN + i;
        if (child < STRESS_TREE_NODES) {
            job_spawn(worker, tree->borrowed_group, job_tree, &tree->borrowed_nodes[child]);
        }
    }
}

/* Everything at once and on every worker: outside threads submitting, jobs spawning, stealing, sleeping, arenas. Each job
 * has to run exactly once, and the hot path must not touch the heap. */
TEST(stress_on_all_workers_runs_every_job_exactly_once) {
    const uint64_t item_total = (uint64_t)STRESS_PRODUCERS * STRESS_JOBS_PER_PRODUCER;
    stress_item_t *const items = test_alloc(t, item_total * sizeof(stress_item_t));
    tree_node_t *const nodes = test_alloc(t, (uint64_t)STRESS_TREE_NODES * sizeof(tree_node_t));
    stress_producer_t *const producers = test_alloc(t, STRESS_PRODUCERS * sizeof(stress_producer_t));
    stress_shared_t shared;
    memset(&shared, 0, sizeof(shared));
    for (uint64_t i = 0; i < item_total; i++) {
        items[i].borrowed_shared = &shared;
        items[i].index = (uint32_t)i;
    }

    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    job_system_t system;
    REQUIRE(job_init(&system, &topology));
    heap_counters_t before;
    heap_counters_read(&before);

    job_group_t tree_group;
    job_group_init(&tree_group);
    tree_t tree = {.borrowed_nodes = nodes, .borrowed_group = &tree_group};
    for (uint64_t i = 0; i < STRESS_TREE_NODES; i++) {
        nodes[i].borrowed_tree = &tree;
        nodes[i].index = (uint32_t)i;
    }

    /* Roles past main only name and place these: each runs where the real thread of that role would. */
    static const thread_role_t roles[STRESS_PRODUCERS] = {THREAD_ROLE_MAIN, THREAD_ROLE_SIM, THREAD_ROLE_GPU};
    for (uint32_t i = 0; i < STRESS_PRODUCERS; i++) {
        producers[i].borrowed_system = &system;
        producers[i].borrowed_items = &items[(uint64_t)i * STRESS_JOBS_PER_PRODUCER];
        producers[i].count = STRESS_JOBS_PER_PRODUCER;
        REQUIRE(thread_start(&producers[i].thread, &topology, roles[i], 0u, stress_producer_main, &producers[i]));
    }
    for (uint32_t round = 0; round < STRESS_TREE_ROUNDS; round++) {
        job_submit(&system, &tree_group, job_tree, &nodes[0]);
        job_group_wait(&system, &tree_group);
    }
    for (uint32_t i = 0; i < STRESS_PRODUCERS; i++) {
        thread_join(&producers[i].thread);
    }
    /* Workers take from the shared queue before they steal, so while the producers were busy nobody had to steal. Now that
     * queue is empty and stealing is the only way to get anything. */
    for (uint32_t round = 0; round < STRESS_TREE_ROUNDS; round++) {
        job_submit(&system, &tree_group, job_tree, &nodes[0]);
        job_group_wait(&system, &tree_group);
    }
    job_group_deinit(&tree_group);

    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);

    uint64_t wrong_items = 0;
    for (uint64_t i = 0; i < item_total; i++) {
        wrong_items += atomic_load(&items[i].hits) != 1u ? 1u : 0u;
    }
    CHECK_EQ_U64(wrong_items, 0u);
    uint64_t wrong_nodes = 0;
    for (uint64_t i = 0; i < STRESS_TREE_NODES; i++) {
        wrong_nodes += atomic_load(&nodes[i].visits) != 2u * STRESS_TREE_ROUNDS ? 1u : 0u;
    }
    CHECK_EQ_U64(wrong_nodes, 0u);
    CHECK_EQ_U64(atomic_load(&shared.arena_not_empty), 0u);
    CHECK_EQ_U64(atomic_load(&shared.arena_corrupted), 0u);

    const uint64_t jobs_total = item_total + (uint64_t)STRESS_TREE_NODES * 2u * STRESS_TREE_ROUNDS;
    const job_stats_t total = stats_total(&system);
    CHECK_EQ_U64(total.jobs_run, jobs_total);
    CHECK_EQ_U64(total.arena_resets, jobs_total);
    CHECK_EQ_U64(total.jobs_inlined, 0u);
    CHECK(total.jobs_stolen > 0u);
    CHECK_EQ_U64(total.jobs_from_injector, item_total + 2u * STRESS_TREE_ROUNDS);
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_stats_t one;
        job_stats_read(&system, i, &one);
        CHECK(one.jobs_run > 0u);
        CHECK_EQ_U64(one.arena_resets, one.jobs_run);
        printf("  worker %2" PRIu32 " cpu %2" PRIu64 ": %8" PRIu64 " run, %8" PRIu64 " stolen, %8" PRIu64
               " from injector, %5" PRIu64 " sleeps\n",
               i, one.cpu, one.jobs_run, one.jobs_stolen, one.jobs_from_injector, one.sleeps);
    }
    job_deinit(&system);
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, 0u);
}

/* ---- Stealing ---------------------------------------------------------------------------------------------------- */

typedef struct steal_t {
    job_group_t *borrowed_group;
    _Atomic uint64_t seen; /* a bit per worker that ran a child */
    _Atomic uint64_t parent_ran_child;
    _Atomic uint32_t parent_index;
    _Atomic uint32_t done;
    _Atomic uint32_t timed_out;
    uint32_t _pad0;
} steal_t;
static_assert(sizeof(steal_t) == 40);

static void job_steal_child(job_worker_t *worker, void *borrowed_argument) {
    steal_t *const steal = borrowed_argument;
    if (worker->index == atomic_load(&steal->parent_index)) {
        atomic_fetch_add(&steal->parent_ran_child, 1u);
    }
    atomic_fetch_or(&steal->seen, 1ull << worker->index);
    const int64_t deadline = time_now_ns() + WAIT_NS;
    while (count_bits(atomic_load(&steal->seen)) < 2u) {
        if (time_now_ns() > deadline) {
            atomic_fetch_add(&steal->timed_out, 1u);
            break;
        }
        (void)sched_yield();
    }
    atomic_fetch_add(&steal->done, 1u);
}

static void job_steal_parent(job_worker_t *worker, void *borrowed_argument) {
    steal_t *const steal = borrowed_argument;
    atomic_store(&steal->parent_index, worker->index);
    for (uint64_t i = 0; i < JOB_QUEUE_CAPACITY; i++) {
        job_spawn(worker, steal->borrowed_group, job_steal_child, steal);
    }
    const int64_t deadline = time_now_ns() + WAIT_NS;
    while (atomic_load(&steal->done) < JOB_QUEUE_CAPACITY) {
        if (time_now_ns() > deadline) {
            atomic_fetch_add(&steal->timed_out, 1u);
            return;
        }
        (void)sched_yield();
    }
}

/* The parent stays inside its job until all its children are done, so it never pops one: every child has to be stolen, and
 * by more than one worker, since the first waits for a second. It fills the queue exactly, so all of it fits. */
TEST(a_busy_workers_queue_is_emptied_by_the_others) {
    job_system_t system;
    REQUIRE(system_start(&system));
    job_group_t group;
    job_group_init(&group);
    steal_t steal;
    memset(&steal, 0, sizeof(steal));
    steal.borrowed_group = &group;
    atomic_store(&steal.parent_index, UINT32_MAX);

    job_submit(&system, &group, job_steal_parent, &steal);
    job_group_wait(&system, &group);
    job_group_deinit(&group);

    CHECK_EQ_U64(atomic_load(&steal.timed_out), 0u);
    CHECK_EQ_U64(atomic_load(&steal.done), JOB_QUEUE_CAPACITY);
    CHECK_EQ_U64(atomic_load(&steal.parent_ran_child), 0u);
    CHECK(count_bits(atomic_load(&steal.seen)) >= 2u);
    const job_stats_t total = stats_total(&system);
    CHECK_EQ_U64(total.jobs_stolen, JOB_QUEUE_CAPACITY);
    CHECK_EQ_U64(total.jobs_from_injector, 1u);
    CHECK_EQ_U64(total.jobs_run, JOB_QUEUE_CAPACITY + 1u);
    job_deinit(&system);
}

/* ---- Job arenas -------------------------------------------------------------------------------------------------- */

typedef struct arena_probe_t {
    uint8_t *block;
    uint64_t worker_index;
    uint64_t used_inside;
} arena_probe_t;
static_assert(sizeof(arena_probe_t) == 24);

static void job_arena_probe(job_worker_t *worker, void *borrowed_argument) {
    arena_probe_t *const probe = borrowed_argument;
    probe->block = arena_alloc_zeroed(&worker->arena_job, 1024u * 1024u, 4096u);
    probe->used_inside = worker->arena_job.used;
    probe->worker_index = worker->index;
}

TEST(a_job_arena_is_reset_by_the_time_its_job_is_finished) {
    job_system_t system;
    REQUIRE(system_start(&system));
    job_group_t group;
    job_group_init(&group);
    arena_probe_t probe;
    memset(&probe, 0, sizeof(probe));
    job_submit(&system, &group, job_arena_probe, &probe);
    job_group_wait(&system, &group);
    job_group_deinit(&group);

    CHECK(probe.used_inside >= 1024u * 1024u);
    REQUIRE(probe.worker_index < MC_THREADS_WORKERS);
    const uint32_t index = (uint32_t)probe.worker_index;
    const arena_t *const arena = &system.borrowed_workers[index].arena_job;
    CHECK_EQ_U64(arena->used, 0u);
    CHECK(probe.block >= arena->borrowed_base && probe.block < arena->borrowed_base + arena->capacity);
    job_stats_t one;
    job_stats_read(&system, index, &one);
    CHECK_EQ_U64(one.arena_resets, 1u);
    CHECK(one.arena_peak >= 1024u * 1024u);
#ifdef __SANITIZE_ADDRESS__
    /* The pointer the job kept is dead, and the sanitize preset would catch anyone using it. */
    CHECK(__asan_address_is_poisoned(probe.block));
#endif
    job_deinit(&system);
}

/* ---- Queues running out ------------------------------------------------------------------------------------------ */

typedef struct gate_t {
    _Atomic uint64_t ran;
    _Atomic uint32_t blocked;
    _Atomic uint32_t open;
    _Atomic uint32_t timed_out;
    uint32_t _pad0;
} gate_t;
static_assert(sizeof(gate_t) == 24);

static void gate_wait_open(gate_t *gate) {
    const int64_t deadline = time_now_ns() + WAIT_NS;
    while (atomic_load(&gate->open) == 0u) {
        if (time_now_ns() > deadline) {
            atomic_fetch_add(&gate->timed_out, 1u);
            return;
        }
        (void)sched_yield();
    }
}

static void job_gate_blocker(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    gate_t *const gate = borrowed_argument;
    atomic_fetch_add(&gate->blocked, 1u);
    gate_wait_open(gate);
    atomic_fetch_add(&gate->ran, 1u);
}

static void job_gate_quick(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    gate_t *const gate = borrowed_argument;
    atomic_fetch_add(&gate->ran, 1u);
}

/* Blocks every worker so that nothing leaves the shared queue. Returns false if they did not all block in time. */
static bool gate_block_all(job_system_t *system, job_group_t *group, gate_t *gate) {
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_submit(system, group, job_gate_blocker, gate);
    }
    const int64_t deadline = time_now_ns() + WAIT_NS;
    while (atomic_load(&gate->blocked) != MC_THREADS_WORKERS) {
        if (time_now_ns() > deadline) {
            return false;
        }
        (void)sched_yield();
    }
    return true;
}

typedef struct gate_opener_t {
    thread_t thread;
    gate_t *borrowed_gate;
    uint64_t _pad0;
} gate_opener_t;
static_assert(sizeof(gate_opener_t) == 64);

static void gate_opener_main(void *borrowed_argument) {
    gate_opener_t *const opener = borrowed_argument;
    time_sleep_ns(50 * TIME_NS_PER_MS);
    atomic_store(&opener->borrowed_gate->open, 1u);
}

TEST(the_shared_queue_holds_its_capacity_then_refuses_then_waits) {
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    job_system_t system;
    REQUIRE(job_init(&system, &topology));
    job_group_t group;
    job_group_init(&group);
    gate_t gate;
    memset(&gate, 0, sizeof(gate));
    REQUIRE(gate_block_all(&system, &group, &gate));

    uint64_t accepted = 0;
    while (accepted <= JOB_INJECTOR_CAPACITY && job_try_submit(&system, &group, job_gate_quick, &gate)) {
        accepted++;
    }
    CHECK_EQ_U64(accepted, JOB_INJECTOR_CAPACITY);
    /* A refused job was taken out of the group's count again. */
    CHECK_EQ_U64(atomic_load(&group.state), (uint64_t)MC_THREADS_WORKERS + JOB_INJECTOR_CAPACITY);

    /* The queue is full, so this can only return once the gate opens and workers drain it. */
    gate_opener_t opener;
    memset(&opener, 0, sizeof(opener));
    opener.borrowed_gate = &gate;
    REQUIRE(thread_start(&opener.thread, &topology, THREAD_ROLE_SIM, 0u, gate_opener_main, &opener));
    job_submit(&system, &group, job_gate_quick, &gate);
    CHECK_EQ_U64(atomic_load(&gate.open), 1u);
    thread_join(&opener.thread);

    job_group_wait(&system, &group);
    job_group_deinit(&group);
    CHECK_EQ_U64(atomic_load(&gate.timed_out), 0u);
    CHECK_EQ_U64(atomic_load(&gate.ran), (uint64_t)MC_THREADS_WORKERS + JOB_INJECTOR_CAPACITY + 1u);
    job_deinit(&system);
}

enum : uint64_t {
    OVERFLOW_BLOCK_BYTES = 4096u,
    /* Other workers each take one child and block, so this many are sure to find every queue full. */
    OVERFLOW_EXTRA = 1000u,
    OVERFLOW_CHILDREN = JOB_QUEUE_CAPACITY + JOB_INJECTOR_CAPACITY + MC_THREADS_WORKERS + OVERFLOW_EXTRA,
};

typedef struct overflow_t {
    job_group_t *borrowed_group;
    gate_t gate;
    _Atomic uint64_t inline_found_arena_empty;
    _Atomic uint64_t block_changed;
    _Atomic uint32_t parent_index;
    _Atomic uint32_t parent_running;
} overflow_t;
static_assert(sizeof(overflow_t) == 56);

/* On the parent's worker while the parent runs, it can only have been run inline, on the parent's stack. Elsewhere it holds
 * its worker until all the children have been spawned, so the queues stay full. */
static void job_overflow_child(job_worker_t *worker, void *borrowed_argument) {
    overflow_t *const overflow = borrowed_argument;
    if (worker->index == atomic_load(&overflow->parent_index)) {
        if (atomic_load(&overflow->parent_running) != 0u && worker->arena_job.used == 0u) {
            atomic_fetch_add(&overflow->inline_found_arena_empty, 1u);
        }
    } else {
        gate_wait_open(&overflow->gate);
    }
    (void)arena_alloc(&worker->arena_job, 128u, 16u);
    atomic_fetch_add(&overflow->gate.ran, 1u);
}

static void job_overflow_parent(job_worker_t *worker, void *borrowed_argument) {
    overflow_t *const overflow = borrowed_argument;
    uint8_t *const block = arena_alloc(&worker->arena_job, OVERFLOW_BLOCK_BYTES, 64u);
    memset(block, 0xA5, OVERFLOW_BLOCK_BYTES);
    atomic_store(&overflow->parent_index, worker->index);
    atomic_store(&overflow->parent_running, 1u);
    for (uint64_t i = 0; i < OVERFLOW_CHILDREN; i++) {
        job_spawn(worker, overflow->borrowed_group, job_overflow_child, overflow);
    }
    /* The children run inline shared this arena, and must not have reset it or written over this. */
    for (uint64_t i = 0; i < OVERFLOW_BLOCK_BYTES; i++) {
        if (block[i] != 0xA5) {
            atomic_fetch_add(&overflow->block_changed, 1u);
            break;
        }
    }
    atomic_store(&overflow->parent_running, 0u);
    atomic_store(&overflow->gate.open, 1u);
}

TEST(spawning_past_every_queue_runs_inline_in_the_same_arena) {
    job_system_t system;
    REQUIRE(system_start(&system));
    job_group_t group;
    job_group_init(&group);
    overflow_t overflow;
    memset(&overflow, 0, sizeof(overflow));
    overflow.borrowed_group = &group;
    atomic_store(&overflow.parent_index, UINT32_MAX);

    job_submit(&system, &group, job_overflow_parent, &overflow);
    job_group_wait(&system, &group);
    job_group_deinit(&group);

    CHECK_EQ_U64(atomic_load(&overflow.gate.timed_out), 0u);
    CHECK_EQ_U64(atomic_load(&overflow.gate.ran), OVERFLOW_CHILDREN);
    CHECK_EQ_U64(atomic_load(&overflow.inline_found_arena_empty), 0u);
    CHECK_EQ_U64(atomic_load(&overflow.block_changed), 0u);
    const job_stats_t total = stats_total(&system);
    CHECK_EQ_U64(total.jobs_run, OVERFLOW_CHILDREN + 1u);
    CHECK(total.jobs_inlined >= OVERFLOW_EXTRA);
    /* An inlined job does not end the arena's lifetime. Every other job does. */
    CHECK_EQ_U64(total.arena_resets, total.jobs_run - total.jobs_inlined);
    job_deinit(&system);
}

/* ---- Sleeping and waking ----------------------------------------------------------------------------------------- */

static void job_count_one(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    atomic_fetch_add((_Atomic uint64_t *)borrowed_argument, 1u);
}

/* Each wait hangs if a wakeup is lost. The gaps run across the length of the idle spin, so that submits keep landing just as
 * workers are deciding to sleep, and every so often they land on all of them fully asleep. */
TEST(no_wakeup_is_lost_around_going_to_sleep) {
    enum : uint32_t { ROUNDS = 4000u };
    job_system_t system;
    REQUIRE(system_start(&system));
    job_group_t group;
    job_group_init(&group);
    _Atomic uint64_t ran = 0;
    uint64_t submitted = 0;
    for (uint32_t round = 0; round < ROUNDS; round++) {
        const uint32_t burst = 1u + round % 4u;
        for (uint32_t i = 0; i < burst; i++) {
            job_submit(&system, &group, job_count_one, (void *)&ran);
        }
        submitted += burst;
        job_group_wait(&system, &group);
        if (round % 512u == 511u) {
            REQUIRE(wait_for_all_asleep(&system));
        } else {
            time_sleep_ns((int64_t)(round % 8u) * 50000);
        }
    }
    job_group_deinit(&group);
    CHECK_EQ_U64(atomic_load(&ran), submitted);
    CHECK(stats_total(&system).sleeps > 0u);
    job_deinit(&system);
}

static int64_t process_cpu_ns(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
    const int64_t seconds = (int64_t)usage.ru_utime.tv_sec + (int64_t)usage.ru_stime.tv_sec;
    const int64_t micros = (int64_t)usage.ru_utime.tv_usec + (int64_t)usage.ru_stime.tv_usec;
    return seconds * TIME_NS_PER_SECOND + micros * 1000;
}

/* Ahmed's desktop shares these CPUs, so idle has to mean idle. 16 spinning workers would use 16 x 300 ms of CPU here. */
TEST(idle_workers_use_no_cpu) {
    job_system_t system;
    REQUIRE(system_start(&system));
    REQUIRE(wait_for_all_asleep(&system));
    const int64_t before = process_cpu_ns();
    time_sleep_ns(300 * TIME_NS_PER_MS);
    const int64_t after = process_cpu_ns();
    REQUIRE(before >= 0 && after >= 0);
    CHECK(after - before < 30 * TIME_NS_PER_MS);
    job_deinit(&system);
}

/* ---- Lifetime ---------------------------------------------------------------------------------------------------- */

typedef struct drain_t {
    job_group_t *borrowed_group;
    _Atomic uint64_t ran;
} drain_t;
static_assert(sizeof(drain_t) == 16);

static void job_drain_leaf(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    atomic_fetch_add(&((drain_t *)borrowed_argument)->ran, 1u);
}

static void job_drain_root(job_worker_t *worker, void *borrowed_argument) {
    drain_t *const drain = borrowed_argument;
    atomic_fetch_add(&drain->ran, 1u);
    for (uint32_t i = 0; i < 3u; i++) {
        job_spawn(worker, drain->borrowed_group, job_drain_leaf, drain);
    }
}

TEST(deinit_runs_all_that_is_queued_and_what_those_jobs_spawn) {
    enum : uint32_t { ROOTS = 2000u, CYCLES = 10u };
    for (uint32_t cycle = 0; cycle < CYCLES; cycle++) {
        job_system_t system;
        REQUIRE(system_start(&system));
        job_group_t group;
        job_group_init(&group);
        drain_t drain = {.borrowed_group = &group, .ran = 0};
        for (uint32_t i = 0; i < ROOTS; i++) {
            job_submit(&system, &group, job_drain_root, &drain);
        }
        job_deinit(&system);
        CHECK_EQ_U64(atomic_load(&drain.ran), (uint64_t)ROOTS * 4u);
        job_group_deinit(&group); /* aborts if the count did not reach zero */
    }
}

TEST(starting_and_stopping_leaves_nothing_behind) {
    const uint32_t threads_before = threads_alive_settled(1u);
    CHECK_EQ_U64(threads_before, 1u);
    heap_counters_t counters;
    for (uint32_t cycle = 0; cycle < 20u; cycle++) {
        job_system_t system;
        REQUIRE(system_start(&system));
        heap_counters_read(&counters);
        CHECK_EQ_U64(counters.live_bytes, JOB_REGION_BYTES);
        CHECK_EQ_U64(counters.live_regions, 1u);
        CHECK_EQ_U64(threads_alive_settled(threads_before + MC_THREADS_WORKERS), threads_before + MC_THREADS_WORKERS);
        job_deinit(&system);
        CHECK(system.owned_region.owned_base == nullptr);
        job_deinit(&system); /* an empty system does nothing */
        CHECK_EQ_U64(threads_alive_settled(threads_before), threads_before);
        heap_counters_read(&counters);
        CHECK_EQ_U64(counters.live_bytes, 0u);
        CHECK_EQ_U64(counters.live_regions, 0u);
    }
    CHECK(heap_exit_check());
}

TEST(init_reports_an_exhausted_budget_and_starts_nothing) {
    heap_region_t whole;
    REQUIRE(heap_reserve(HEAP_BUDGET_BYTES, &whole));
    const uint32_t threads_before = threads_alive_settled(1u);
    job_system_t system;
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    CHECK(!job_init(&system, &topology));
    CHECK_EQ_U64(threads_alive_settled(threads_before), threads_before);
    CHECK(system.owned_region.owned_base == nullptr);
    job_deinit(&system);
    heap_release(&whole);
    heap_counters_t counters;
    heap_counters_read(&counters);
    CHECK_EQ_U64(counters.live_bytes, 0u);
}

/* Run only by the test below, in a new process, since glibc keeps the stacks of finished threads and would reuse them, and
 * a thread that needs no new stack cannot fail here. Leaves room for the region and a few stacks and no more. */
TEST(child_start_fails_partway) {
    if (getenv("MC_JOB_TEST_CHILD") == nullptr) {
        return;
    }
    FILE *const statm = fopen("/proc/self/statm", "r");
    REQUIRE(statm != nullptr);
    uint64_t pages = 0;
    const int32_t fields = fscanf(statm, "%" SCNu64, &pages);
    (void)fclose(statm);
    REQUIRE(fields == 1);
    const uint64_t stack_room = 5u * (THREAD_STACK_BYTES + 4096u);
    const uint64_t limit = pages * 4096u + JOB_REGION_BYTES + HEAP_REGION_ALIGNMENT + stack_room;
    const struct rlimit address_space = {.rlim_cur = limit, .rlim_max = limit};
    REQUIRE(setrlimit(RLIMIT_AS, &address_space) == 0);

    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    job_system_t system;
    CHECK(!job_init(&system, &topology));
    CHECK(system.owned_region.owned_base == nullptr);
    CHECK_EQ_U64(threads_alive_settled(1u), 1u);
    heap_counters_t counters;
    heap_counters_read(&counters);
    CHECK_EQ_U64(counters.live_bytes, 0u);
}

/* ASan's own bookkeeping needs mappings of its own, which this limit would refuse, and it dies of that. */
#ifndef __SANITIZE_ADDRESS__
TEST(a_worker_failing_to_start_stops_the_ones_already_running) {
    REQUIRE(setenv("MC_JOB_TEST_CHILD", "1", 1) == 0);
    REQUIRE(setenv("TEST_FILTER", "child_start_fails_partway", 1) == 0);
    char self[] = "/proc/self/exe";
    char *const argv[] = {self, nullptr};
    test_process_t child;
    test_process_run(t, argv, 60000u, &child);
    REQUIRE(unsetenv("MC_JOB_TEST_CHILD") == 0);
    REQUIRE(unsetenv("TEST_FILTER") == 0);
    CHECK(child.started != 0);
    CHECK_EQ_I64(child.exit_code, 0);
    CHECK_CONTAINS(child.output, "cannot start worker-");
    /* Not the first, or this would not show any cleanup of running workers. */
    CHECK(strstr(child.output, "cannot start worker-00") == nullptr);
}
#endif

/* ---- Misuse aborts ----------------------------------------------------------------------------------------------- */

typedef void (*child_body_t)(void);

/* Runs body in a child with its stdout and stderr captured. No core file: an abort here is the expected result. */
static bool child_run(test_context_t *t, child_body_t body, char **out_output, int32_t *out_signal) {
    enum : uint64_t { OUTPUT_BYTES = 65536u };
    int32_t fds[2];
    if (pipe(fds) != 0) {
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        const struct rlimit no_core = {.rlim_cur = 0, .rlim_max = 0};
        (void)setrlimit(RLIMIT_CORE, &no_core);
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        (void)alarm(120u); /* a bug in a body must not outlive the run */
        body();
        _exit(0);
    }
    close(fds[1]);
    char *const text = test_alloc(t, OUTPUT_BYTES);
    uint64_t length = 0;
    while (length + 1u < OUTPUT_BYTES) {
        const int64_t count = read(fds[0], text + length, OUTPUT_BYTES - 1u - length);
        if (count == 0 || (count < 0 && errno != EINTR)) {
            break;
        }
        if (count > 0) {
            length += (uint64_t)count;
        }
    }
    close(fds[0]);
    text[length] = '\0';
    int32_t status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    *out_output = text;
    *out_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return true;
}

typedef struct misuse_t {
    job_group_t *borrowed_group;
    gate_t gate;
} misuse_t;
static_assert(sizeof(misuse_t) == 32);

static void job_misuse_wait(job_worker_t *worker, void *borrowed_argument) {
    misuse_t *const misuse = borrowed_argument;
    job_group_wait(worker->borrowed_system, misuse->borrowed_group);
}

static void body_wait_on_a_worker(void) {
    job_system_t system;
    if (!system_start(&system)) {
        _exit(10);
    }
    job_group_t group;
    job_group_init(&group);
    misuse_t misuse;
    memset(&misuse, 0, sizeof(misuse));
    misuse.borrowed_group = &group;
    job_submit(&system, &group, job_misuse_wait, &misuse);
    job_deinit(&system); /* never reached if the job aborts first, and runs it if not */
}

static void body_group_deinit_with_a_job_left(void) {
    job_group_t group;
    job_group_init(&group);
    atomic_store(&group.state, 1u); /* as if a job were still running */
    job_group_deinit(&group);
}

static void body_two_waiters(void) {
    job_system_t system;
    if (!system_start(&system)) {
        _exit(10);
    }
    job_group_t group;
    job_group_init(&group);
    atomic_store(&group.state, (1u << 31) | 1u); /* as if a job were running and someone else waiting on it */
    job_group_wait(&system, &group);
}

static void job_chain(job_worker_t *worker, void *borrowed_argument) {
    misuse_t *const misuse = borrowed_argument;
    job_spawn(worker, misuse->borrowed_group, job_chain, misuse);
}

/* Holds a worker that took it from a queue. Run inline it has to return, or the spawner would never get any further. */
static void job_chain_filler(job_worker_t *worker, void *borrowed_argument) {
    gate_t *const gate = borrowed_argument;
    if (worker->inline_depth > 1u) {
        return;
    }
    atomic_fetch_add(&gate->blocked, 1u);
    gate_wait_open(gate);
}

/* Holds every other worker on the gate, so nothing can leave a queue, then fills them, then spawns a job that spawns itself.
 * Each of those can only run inline, deeper every time. */
static void job_chain_parent(job_worker_t *worker, void *borrowed_argument) {
    misuse_t *const misuse = borrowed_argument;
    while (atomic_load(&misuse->gate.blocked) < MC_THREADS_WORKERS - 1u) {
        job_spawn(worker, misuse->borrowed_group, job_chain_filler, &misuse->gate);
        (void)sched_yield();
    }
    for (uint64_t i = 0; i < JOB_QUEUE_CAPACITY + JOB_INJECTOR_CAPACITY; i++) {
        job_spawn(worker, misuse->borrowed_group, job_chain_filler, &misuse->gate);
    }
    job_spawn(worker, misuse->borrowed_group, job_chain, misuse);
}

static void body_endless_inline_spawning(void) {
    job_system_t system;
    if (!system_start(&system)) {
        _exit(10);
    }
    job_group_t group;
    job_group_init(&group);
    misuse_t misuse;
    memset(&misuse, 0, sizeof(misuse));
    misuse.borrowed_group = &group;
    job_submit(&system, &group, job_chain_parent, &misuse);
    job_group_wait(&system, &group);
}

typedef struct misuse_case_t {
    child_body_t body;
    const char *expected;
} misuse_case_t;
static_assert(sizeof(misuse_case_t) == 16);

TEST(misuse_aborts_and_says_why) {
    static const misuse_case_t cases[] = {
        {body_wait_on_a_worker, "pthread_equal"},
        {body_group_deinit_with_a_job_left, "job_group_deinit"},
        {body_two_waiters, "flagged || (state & JOB_GROUP_WAITER) == 0"},
        {body_endless_inline_spawning, "already running on this stack"},
    };
    for (uint64_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *output = nullptr;
        int32_t signal_number = 0;
        REQUIRE(child_run(t, cases[i].body, &output, &signal_number));
        CHECK_EQ_I64(signal_number, SIGABRT);
        CHECK_CONTAINS(output, cases[i].expected);
    }
}

TEST_MAIN()
