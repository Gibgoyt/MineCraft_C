/* ctest run_thread. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "machine/index.h"
#include "thread/index.h"
#include "thread/ring/index.h"

#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- A made-up machine ------------------------------------------------------------------------------------------- */

/* Writes <root>/cpu<i>/topology/thread_siblings_list for each entry, with nullptr meaning an offline CPU. */
static const char *fake_sysfs(test_context_t *t, const char *name, const char *const *sibling_lists, uint32_t count) {
    char *root = test_alloc(t, FILE_PATH_BYTES);
    if (t->arg_count != 1) {
        test_check(t, false, __FILE__, __LINE__, "the scratch directory is the one argument");
        return "/nonexistent/no_scratch_directory";
    }
    snprintf(root, FILE_PATH_BYTES, "%s/%s", t->borrowed_args[0], name);
    char *path = test_alloc(t, FILE_PATH_BYTES);
    for (uint32_t cpu = 0; cpu < count; cpu++) {
        snprintf(path, FILE_PATH_BYTES, "%s/cpu%" PRIu32 "/topology", root, cpu);
        char *list_path = test_alloc(t, FILE_PATH_BYTES);
        snprintf(list_path, FILE_PATH_BYTES, "%s/thread_siblings_list", path);
        if (sibling_lists[cpu] == nullptr) {
            (void)file_remove(list_path); /* from an earlier run, if the test has changed since */
            continue;
        }
        test_check(t, file_make_dirs(path) == FILE_OK, __FILE__, __LINE__, "made a fake topology directory");
        test_check(t, file_write_atomic(list_path, sibling_lists[cpu], strlen(sibling_lists[cpu])) == FILE_OK, __FILE__,
                   __LINE__, "wrote a fake sibling list");
    }
    return root;
}

static thread_cpu_mask_t mask_of_all(void) {
    thread_cpu_mask_t mask;
    memset(&mask, 0xFF, sizeof(mask));
    return mask;
}

static void mask_remove(thread_cpu_mask_t *mask, uint32_t cpu) {
    mask->bits[cpu / 64u] &= ~(1ull << (cpu % 64u));
}

static void check_order(test_context_t *t, const thread_topology_t *topology, const uint16_t *expected,
                        uint32_t count) {
    CHECK_EQ_U64(topology->cpu_count, count);
    for (uint32_t i = 0; i < count && i < topology->cpu_count; i++) {
        CHECK_EQ_U64(topology->cpu_order[i], expected[i]);
    }
}

/* The layout of the i9-10900K in config/machine.yaml, as the kernel writes it. */
static const char *const comet_lake[] = {"0,10\n", "1,11\n", "2,12\n", "3,13\n", "4,14\n", "5,15\n", "6,16\n",
                                         "7,17\n", "8,18\n", "9,19\n", "0,10\n", "1,11\n", "2,12\n", "3,13\n",
                                         "4,14\n", "5,15\n", "6,16\n", "7,17\n", "8,18\n", "9,19\n"};

TEST(comet_lake_gets_physical_cores_first) {
    const char *root = fake_sysfs(t, "comet", comet_lake, 20u);
    const thread_cpu_mask_t all = mask_of_all();
    thread_topology_t topology;
    REQUIRE(thread_topology_read_from(&topology, root, &all, true));
    static const uint16_t expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    check_order(t, &topology, expected, 20u);
    CHECK_EQ_U64(topology.physical_count, 10u);
}

/* Siblings numbered side by side, as on many other machines. Rising order would be the wrong answer here. */
TEST(adjacent_siblings_are_taken_apart) {
    static const char *const lists[] = {"0-1\n", "0-1\n", "2-3\n", "2-3\n", "4-5\n", "4-5\n"};
    const char *root = fake_sysfs(t, "adjacent", lists, 6u);
    const thread_cpu_mask_t all = mask_of_all();
    thread_topology_t topology;
    REQUIRE(thread_topology_read_from(&topology, root, &all, true));
    static const uint16_t expected[] = {0, 2, 4, 1, 3, 5};
    check_order(t, &topology, expected, 6u);
    CHECK_EQ_U64(topology.physical_count, 3u);
}

TEST(pin_physical_first_off_is_rising_order) {
    static const char *const lists[] = {"0-1\n", "0-1\n", "2-3\n", "2-3\n"};
    const char *root = fake_sysfs(t, "rising", lists, 4u);
    const thread_cpu_mask_t all = mask_of_all();
    thread_topology_t topology;
    REQUIRE(thread_topology_read_from(&topology, root, &all, false));
    static const uint16_t expected[] = {0, 1, 2, 3};
    check_order(t, &topology, expected, 4u);
    CHECK_EQ_U64(topology.physical_count, 2u);
}

TEST(no_hyperthreading_is_every_cpu_once) {
    static const char *const lists[] = {"0\n", "1\n", "2\n"};
    const char *root = fake_sysfs(t, "single", lists, 3u);
    const thread_cpu_mask_t all = mask_of_all();
    thread_topology_t topology;
    REQUIRE(thread_topology_read_from(&topology, root, &all, true));
    static const uint16_t expected[] = {0, 1, 2};
    check_order(t, &topology, expected, 3u);
    CHECK_EQ_U64(topology.physical_count, 3u);
}

TEST(an_offline_cpu_is_skipped) {
    static const char *const lists[] = {"0\n", nullptr, "2\n", nullptr, "4\n"};
    const char *root = fake_sysfs(t, "offline", lists, 5u);
    const thread_cpu_mask_t all = mask_of_all();
    thread_topology_t topology;
    REQUIRE(thread_topology_read_from(&topology, root, &all, true));
    static const uint16_t expected[] = {0, 2, 4};
    check_order(t, &topology, expected, 3u);
}

/* Under taskset: a core whose first CPU is forbidden still gets a place among the physical ones, through its other. */
TEST(the_affinity_mask_is_respected) {
    const char *root = fake_sysfs(t, "comet", comet_lake, 20u);
    thread_cpu_mask_t allowed = {.bits = {0}};
    allowed.bits[0] = (1ull << 1u) | (1ull << 11u) | (1ull << 12u) | (1ull << 5u);
    thread_topology_t topology;
    REQUIRE(thread_topology_read_from(&topology, root, &allowed, true));
    /* Cores: 1 and 11 share one; 12 stands for its core because 2 is out; 5 has its own. */
    static const uint16_t expected[] = {1, 5, 12, 11};
    check_order(t, &topology, expected, 4u);
    CHECK_EQ_U64(topology.physical_count, 3u);

    thread_cpu_mask_t most = mask_of_all();
    mask_remove(&most, 0u);
    REQUIRE(thread_topology_read_from(&topology, root, &most, true));
    CHECK_EQ_U64(topology.cpu_order[0], 1u);
    CHECK_EQ_U64(topology.cpu_count, 19u);
    CHECK_EQ_U64(topology.physical_count, 10u);
}

TEST(list_formats_the_kernel_writes) {
    static const char *const lists[] = {"0-1,4\n", "0-1,4\n", "2\n", "3", "0-1,4\n"};
    const char *root = fake_sysfs(t, "formats", lists, 5u);
    const thread_cpu_mask_t all = mask_of_all();
    thread_topology_t topology;
    REQUIRE(thread_topology_read_from(&topology, root, &all, true));
    static const uint16_t expected[] = {0, 2, 3, 1, 4};
    check_order(t, &topology, expected, 5u);
    CHECK_EQ_U64(topology.physical_count, 3u);
}

/* Each of these is /sys making no sense, and has to be an error and not a guess. */
TEST(nonsense_is_refused) {
    static const char *const cases[] = {
        "",            /* empty */
        "\n",          /* nothing on the line */
        "x\n",         /* not a number */
        "0,\n",        /* a list that stops */
        "0-\n",        /* a range that stops */
        "1\n",         /* cpu 0 is not in its own list */
        "1-0\n",       /* backwards */
        "0 1\n",       /* wrong separator */
        "0\n\n",       /* more than one line */
        "99999999\n",  /* past any real CPU number */
        "0-99999999\n" /* a range past it */
    };
    const thread_cpu_mask_t all = mask_of_all();
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char name[32];
        snprintf(name, sizeof(name), "nonsense_%" PRIu32, i);
        const char *const lists[] = {cases[i]};
        const char *root = fake_sysfs(t, name, lists, 1u);
        thread_topology_t topology;
        CHECK(!thread_topology_read_from(&topology, root, &all, true));
    }
}

TEST(no_cpu_at_all_is_refused) {
    const thread_cpu_mask_t all = mask_of_all();
    thread_topology_t topology;
    CHECK(!thread_topology_read_from(&topology, "/nonexistent/sysfs/cpu", &all, true));

    const char *root = fake_sysfs(t, "comet", comet_lake, 20u);
    const thread_cpu_mask_t none = {.bits = {0}};
    CHECK(!thread_topology_read_from(&topology, root, &none, true));
}

/* ---- This machine, against what /proc/cpuinfo says --------------------------------------------------------------- */

/* /proc/cpuinfo is a different kernel interface, so a mistake reading /sys cannot also be made here. */
typedef struct cpuinfo_t {
    int32_t core_of[THREAD_MAX_CPUS]; /* physical id * 65536 + core id */
    uint32_t logical;
    uint32_t physical;
} cpuinfo_t;
static_assert(sizeof(cpuinfo_t) == 4u * THREAD_MAX_CPUS + 8u);

static int64_t cpuinfo_field(const char *block, const char *key) {
    const char *found = strstr(block, key);
    if (found == nullptr) {
        return -1;
    }
    const char *colon = strchr(found, ':');
    return colon == nullptr ? -1 : strtol(colon + 1, nullptr, 10);
}

static bool cpuinfo_read(test_context_t *t, cpuinfo_t *out) {
    enum : uint64_t { TEXT_BYTES = 1u << 22u };
    char *text = test_alloc(t, TEXT_BYTES);
    uint64_t length = 0;
    if (file_read_whole("/proc/cpuinfo", text, TEXT_BYTES - 1u, &length) != FILE_OK) {
        return false;
    }
    text[length] = '\0';
    memset(out, 0, sizeof(*out));
    char *cursor = text;
    while (*cursor != '\0') {
        char *end = strstr(cursor, "\n\n");
        if (end != nullptr) {
            *end = '\0';
        }
        const int64_t processor = cpuinfo_field(cursor, "processor");
        const int64_t package = cpuinfo_field(cursor, "physical id");
        const int64_t core = cpuinfo_field(cursor, "core id");
        if (processor < 0 || processor >= THREAD_MAX_CPUS || package < 0 || core < 0) {
            return false;
        }
        const int32_t identity = (int32_t)(package * 65536 + core);
        bool seen = false;
        for (uint32_t i = 0; i < THREAD_MAX_CPUS; i++) {
            seen = seen || (out->core_of[i] == identity + 1);
        }
        out->core_of[processor] = identity + 1; /* 0 stays "not a CPU" */
        out->logical++;
        out->physical += seen ? 0u : 1u;
        if (end == nullptr) {
            break;
        }
        cursor = end + 2;
    }
    return out->logical > 0u;
}

static bool this_process_has_every_cpu(uint32_t logical) {
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    return sched_getaffinity(0, sizeof(affinity), &affinity) == 0 && (uint32_t)CPU_COUNT(&affinity) == logical;
}

TEST(this_machine_is_read_correctly) {
    cpuinfo_t *info = test_alloc(t, sizeof(cpuinfo_t));
    REQUIRE(cpuinfo_read(t, info));
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    if (!this_process_has_every_cpu(info->logical)) {
        return; /* under taskset, and the comparison below would not mean anything */
    }
    CHECK_EQ_U64(topology.cpu_count, info->logical);
    CHECK_EQ_U64(topology.physical_count, info->physical);

    /* config/machine.yaml says what this machine is, and a wrong number there would misplace every thread. */
    CHECK_EQ_U64(topology.cpu_count, MC_CPU_LOGICAL_THREADS);
    CHECK_EQ_U64(topology.physical_count, MC_CPU_PHYSICAL_CORES);

    /* Physical cores first, which is the whole point: no core appears twice until every core has appeared once. */
    REQUIRE(topology.physical_count <= topology.cpu_count);
    uint64_t repeats = 0;
    for (uint32_t i = 0; i < topology.physical_count; i++) {
        for (uint32_t j = 0; j < i; j++) {
            repeats += info->core_of[topology.cpu_order[i]] == info->core_of[topology.cpu_order[j]] ? 1u : 0u;
        }
    }
    CHECK_EQ_U64(repeats, 0u);
    /* And each CPU appears exactly once. */
    uint32_t seen[THREAD_MAX_CPUS] = {0};
    for (uint32_t i = 0; i < topology.cpu_count; i++) {
        seen[topology.cpu_order[i]]++;
    }
    for (uint32_t cpu = 0; cpu < THREAD_MAX_CPUS; cpu++) {
        CHECK(seen[cpu] <= 1u);
        CHECK_EQ_U64(seen[cpu], info->core_of[cpu] != 0 ? 1u : 0u);
    }
}

/* ---- Places in the order ----------------------------------------------------------------------------------------- */

TEST(roles_take_their_places_from_machine_yaml) {
    thread_topology_t topology = {.cpu_count = 256u, .physical_count = 128u, .cpu_order = {0}};
    for (uint32_t i = 0; i < 256u; i++) {
        topology.cpu_order[i] = (uint16_t)(1000u + i); /* so a place cannot be mistaken for a CPU number */
    }
    uint32_t place = 0;
    CHECK_EQ_U64(thread_cpu_for(&topology, THREAD_ROLE_MAIN, 0u), 1000u + place++);
    CHECK_EQ_U64(thread_cpu_for(&topology, THREAD_ROLE_SIM, 0u), 1000u + place++);
    if (MC_THREADS_GPU != 0) {
        CHECK_EQ_U64(thread_cpu_for(&topology, THREAD_ROLE_GPU, 0u), 1000u + place++);
    }
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        CHECK_EQ_U64(thread_cpu_for(&topology, THREAD_ROLE_WORKER, i), 1000u + place++);
    }
    /* What is left over is left for the OS. */
    CHECK(place + MC_THREADS_RESERVED_OS <= MC_CPU_LOGICAL_THREADS);
}

TEST(too_few_cpus_wrap_round) {
    thread_topology_t topology = {.cpu_count = 2u, .physical_count = 2u, .cpu_order = {0}};
    topology.cpu_order[0] = 5u;
    topology.cpu_order[1] = 7u;
    const uint32_t first_worker_place = MC_THREADS_MAIN + MC_THREADS_SIM + MC_THREADS_GPU;
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        CHECK_EQ_U64(thread_cpu_for(&topology, THREAD_ROLE_WORKER, i), (first_worker_place + i) % 2u == 0u ? 5u : 7u);
    }
}

/* An index past a role's count is a bug, and has to stop the program there. A child, because it ends in abort. */
TEST(an_index_past_the_role_asserts) {
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        /* The assert's message would look like a failure in the ctest log, and it is the expected result. */
        file_t silent;
        if (file_open(&silent, "/dev/null", FILE_MODE_WRITE) != FILE_OK || dup2(silent.fd, STDERR_FILENO) < 0) {
            _exit(100);
        }
        thread_topology_t topology = {.cpu_count = 1u, .physical_count = 1u, .cpu_order = {0}};
        (void)thread_cpu_for(&topology, THREAD_ROLE_WORKER, MC_THREADS_WORKERS);
        _exit(0);
    }
    int32_t wait_status = 0;
    REQUIRE(waitpid(child, &wait_status, 0) == child);
    CHECK(WIFSIGNALED(wait_status) && WTERMSIG(wait_status) == SIGABRT);
}

/* ---- Real threads on real CPUs ----------------------------------------------------------------------------------- */

typedef struct pin_report_t {
    uint32_t cpu_seen;
    uint32_t cpus_allowed;
    uint64_t stack_bytes;
    uint32_t cpu_allowed; /* the one CPU in its mask, if there is one */
    uint8_t ran;
    uint8_t _pad0[3];
    char name[THREAD_NAME_BYTES];
} pin_report_t;
static_assert(sizeof(pin_report_t) == 40);

static void pin_report_thread(void *raw) {
    pin_report_t *report = raw;
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
        report->cpus_allowed = (uint32_t)CPU_COUNT(&affinity);
        for (uint32_t cpu = 0; cpu < THREAD_MAX_CPUS; cpu++) {
            if (CPU_ISSET(cpu, &affinity)) {
                report->cpu_allowed = cpu;
            }
        }
    }
    const int32_t current = sched_getcpu();
    report->cpu_seen = current >= 0 ? (uint32_t)current : UINT32_MAX;
    (void)pthread_getname_np(pthread_self(), report->name, sizeof(report->name));
    pthread_attr_t attributes;
    if (pthread_getattr_np(pthread_self(), &attributes) == 0) {
        uint64_t stack_bytes = 0;
        void *stack_base = nullptr;
        if (pthread_attr_getstack(&attributes, &stack_base, &stack_bytes) == 0) {
            report->stack_bytes = stack_bytes;
        }
        (void)pthread_attr_destroy(&attributes);
    }
    report->ran = 1;
}

/* All at once, and all kept: a worker's CPU may only be its own. */
TEST(every_role_runs_where_it_was_placed) {
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));

    enum { COUNT = MC_THREADS_SIM + MC_THREADS_GPU + MC_THREADS_WORKERS };
    static_assert(COUNT <= 64);
    thread_t threads[COUNT];
    pin_report_t reports[COUNT];
    memset(reports, 0, sizeof(reports));
    uint32_t started = 0;
    bool all_started = true;
    for (uint32_t i = 0; i < MC_THREADS_SIM; i++) {
        all_started = all_started && thread_start(&threads[started], &topology, THREAD_ROLE_SIM, i, pin_report_thread,
                                                  &reports[started]);
        started += all_started ? 1u : 0u;
    }
    for (uint32_t i = 0; all_started && i < MC_THREADS_GPU; i++) {
        all_started =
            thread_start(&threads[started], &topology, THREAD_ROLE_GPU, i, pin_report_thread, &reports[started]);
        started += all_started ? 1u : 0u;
    }
    for (uint32_t i = 0; all_started && i < MC_THREADS_WORKERS; i++) {
        all_started =
            thread_start(&threads[started], &topology, THREAD_ROLE_WORKER, i, pin_report_thread, &reports[started]);
        started += all_started ? 1u : 0u;
    }
    for (uint32_t i = 0; i < started; i++) {
        thread_join(&threads[i]);
    }
    CHECK(all_started);
    CHECK_EQ_U64(started, COUNT);

    for (uint32_t i = 0; i < started; i++) {
        CHECK_EQ_U64(reports[i].ran, 1u);
        CHECK_EQ_U64(reports[i].cpus_allowed, 1u);
        CHECK_EQ_U64(reports[i].cpu_allowed, threads[i].cpu);
        CHECK_EQ_U64(reports[i].cpu_seen, threads[i].cpu);
        CHECK_STR_EQ(reports[i].name, threads[i].name);
        /* glibc may round up, and takes its guard page and TLS out of it. */
        CHECK(reports[i].stack_bytes >= THREAD_STACK_BYTES / 2u);
        CHECK(reports[i].stack_bytes <= THREAD_STACK_BYTES * 2u);
    }
    if (topology.cpu_count >= (uint32_t)COUNT + MC_THREADS_MAIN) {
        for (uint32_t i = 0; i < started; i++) {
            for (uint32_t j = 0; j < i; j++) {
                CHECK(threads[i].cpu != threads[j].cpu);
            }
            CHECK(threads[i].cpu != thread_cpu_for(&topology, THREAD_ROLE_MAIN, 0u));
        }
    }
}

TEST(threads_are_named_after_their_role) {
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    thread_t thread;
    pin_report_t report;
    memset(&report, 0, sizeof(report));
    REQUIRE(thread_start(&thread, &topology, THREAD_ROLE_WORKER, 3u, pin_report_thread, &report));
    thread_join(&thread);
    CHECK_STR_EQ(report.name, "worker-03");
    REQUIRE(thread_start(&thread, &topology, THREAD_ROLE_SIM, 0u, pin_report_thread, &report));
    thread_join(&thread);
    CHECK_STR_EQ(report.name, "sim");
}

/* A thread can start another on a CPU it is not on itself, which lets main pin itself before starting the rest. */
typedef struct pin_self_t {
    const thread_topology_t *borrowed_topology; /* read before anything was pinned, which is the rule */
    pin_report_t reports[2];
} pin_self_t;
static_assert(sizeof(pin_self_t) == 88);

static void pin_self_thread(void *raw) {
    pin_self_t *self = raw;
    if (!thread_pin_self(thread_cpu_for(self->borrowed_topology, THREAD_ROLE_MAIN, 0u))) {
        return;
    }
    pin_report_thread(&self->reports[0]);
    thread_t other;
    if (thread_start(&other, self->borrowed_topology, THREAD_ROLE_SIM, 0u, pin_report_thread, &self->reports[1])) {
        thread_join(&other);
    }
}

TEST(a_pinned_thread_can_start_one_elsewhere) {
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    if (topology.cpu_count < 2u) {
        return;
    }
    /* On a thread started for it, because the test's own main thread must stay free to go anywhere. It starts on a
     * worker's CPU, then moves to main's, as the real main thread will. */
    pin_self_t self;
    memset(&self, 0, sizeof(self));
    self.borrowed_topology = &topology;
    thread_t thread;
    REQUIRE(thread_start(&thread, &topology, THREAD_ROLE_WORKER, 0u, pin_self_thread, &self));
    thread_join(&thread);
    CHECK_EQ_U64(self.reports[0].ran, 1u);
    CHECK_EQ_U64(self.reports[0].cpus_allowed, 1u);
    CHECK_EQ_U64(self.reports[0].cpu_seen, thread_cpu_for(&topology, THREAD_ROLE_MAIN, 0u));
    CHECK_EQ_U64(self.reports[1].ran, 1u);
    CHECK_EQ_U64(self.reports[1].cpus_allowed, 1u);
    CHECK_EQ_U64(self.reports[1].cpu_seen, thread_cpu_for(&topology, THREAD_ROLE_SIM, 0u));
}

/* The warning path's other half: told the truth about too few CPUs, the threads still run, sharing. */
TEST(threads_share_when_there_are_too_few_cpus) {
    thread_topology_t real;
    REQUIRE(thread_topology_read(&real));
    thread_topology_t topology = {.cpu_count = 1u, .physical_count = 1u, .cpu_order = {real.cpu_order[0]}};
    thread_t threads[3];
    pin_report_t reports[3];
    memset(reports, 0, sizeof(reports));
    for (uint32_t i = 0; i < 3u; i++) {
        REQUIRE(thread_start(&threads[i], &topology, THREAD_ROLE_WORKER, i, pin_report_thread, &reports[i]));
    }
    for (uint32_t i = 0; i < 3u; i++) {
        thread_join(&threads[i]);
        CHECK_EQ_U64(reports[i].cpu_seen, real.cpu_order[0]);
    }
}

TEST(a_cpu_that_does_not_exist_is_a_clean_failure) {
    thread_topology_t real;
    REQUIRE(thread_topology_read(&real));
    if (real.cpu_count > 250u) {
        return;
    }
    thread_topology_t topology = {.cpu_count = 1u, .physical_count = 1u, .cpu_order = {255u}};
    thread_t thread;
    pin_report_t report;
    memset(&report, 0, sizeof(report));
    CHECK(!thread_start(&thread, &topology, THREAD_ROLE_SIM, 0u, pin_report_thread, &report));
    thread_join(&thread); /* does nothing, and must not hang or crash */
    CHECK_EQ_U64(report.ran, 0u);
    CHECK(!thread_pin_self(255u));
}

TEST(join_twice_does_nothing_the_second_time) {
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    thread_t thread;
    pin_report_t report;
    memset(&report, 0, sizeof(report));
    REQUIRE(thread_start(&thread, &topology, THREAD_ROLE_SIM, 0u, pin_report_thread, &report));
    thread_join(&thread);
    thread_join(&thread);
    CHECK_EQ_U64(report.ran, 1u);
}

/* ---- Ring, single-threaded --------------------------------------------------------------------------------------- */

TEST(ring_sizes_are_validated) {
    CHECK_EQ_U64(ring_storage_bytes(8u, 4u), 32u);
    CHECK_EQ_U64(ring_storage_bytes(1u, 1u), 1u);
    CHECK_EQ_U64(ring_storage_bytes(0u, 4u), 0u);
    CHECK_EQ_U64(ring_storage_bytes(7u, 4u), 0u);
    CHECK_EQ_U64(ring_storage_bytes(8u, 0u), 0u);
    CHECK_EQ_U64(ring_storage_bytes(1ull << 63u, 2u), 0u);
    CHECK_EQ_U64(ring_storage_bytes(1ull << 62u, 4u), 0u);

    uint32_t storage[8];
    ring_t ring;
    CHECK(ring_init(&ring, storage, sizeof(storage), 8u, sizeof(uint32_t)));
    ring_deinit(&ring);
    CHECK(!ring_init(&ring, storage, sizeof(storage) - 1u, 8u, sizeof(uint32_t)));
    CHECK(!ring_init(&ring, storage, sizeof(storage), 7u, sizeof(uint32_t)));
    CHECK(!ring_init(&ring, storage, sizeof(storage), 0u, sizeof(uint32_t)));
    CHECK(!ring_init(&ring, storage, sizeof(storage), 8u, 0u));
    CHECK(!ring_init(&ring, nullptr, sizeof(storage), 8u, sizeof(uint32_t)));
}

TEST(ring_is_first_in_first_out_and_stops_when_full_or_empty) {
    uint32_t storage[4];
    ring_t ring;
    REQUIRE(ring_init(&ring, storage, sizeof(storage), 4u, sizeof(uint32_t)));
    uint32_t value = 0;
    CHECK(!ring_try_pop(&ring, &value));
    CHECK_EQ_U64(ring_count(&ring), 0u);

    /* Many times round, so that head and tail wrap the storage at every offset. */
    uint32_t next_in = 1;
    uint32_t next_out = 1;
    for (uint32_t round = 0; round < 100u; round++) {
        for (uint32_t i = 0; i < 4u; i++) {
            CHECK(ring_try_push(&ring, &next_in));
            next_in++;
        }
        CHECK_EQ_U64(ring_count(&ring), 4u);
        CHECK(!ring_try_push(&ring, &next_in));
        CHECK_EQ_U64(ring_count(&ring), 4u);
        /* Take a different number each round, so the wrap point moves about. */
        const uint32_t take = 1u + round % 4u;
        for (uint32_t i = 0; i < take; i++) {
            CHECK(ring_try_pop(&ring, &value));
            CHECK_EQ_U64(value, next_out);
            next_out++;
        }
        for (uint32_t i = next_in - next_out; i > 0u; i--) {
            CHECK(ring_try_pop(&ring, &value));
            CHECK_EQ_U64(value, next_out);
            next_out++;
        }
        CHECK(!ring_try_pop(&ring, &value));
    }
    ring_deinit(&ring);
}

TEST(ring_of_one_works) {
    uint64_t storage = 0;
    ring_t ring;
    REQUIRE(ring_init(&ring, &storage, sizeof(storage), 1u, sizeof(uint64_t)));
    for (uint64_t i = 0; i < 10u; i++) {
        CHECK(ring_try_push(&ring, &i));
        const uint64_t blocked = 99u;
        CHECK(!ring_try_push(&ring, &blocked));
        uint64_t out = 0;
        CHECK(ring_try_pop(&ring, &out));
        CHECK_EQ_U64(out, i);
        CHECK(!ring_try_pop(&ring, &out));
    }
}

/* Elements of odd sizes, in storage with no particular alignment. */
TEST(ring_carries_elements_of_any_size) {
    static const uint64_t sizes[] = {1u, 3u, 8u, 13u, 64u, 200u};
    for (uint32_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        const uint64_t bytes = sizes[s];
        const uint64_t capacity = 8u;
        uint8_t *storage = test_alloc(t, capacity * bytes + 1u);
        uint8_t *in = test_alloc(t, bytes);
        uint8_t *out = test_alloc(t, bytes);
        ring_t ring;
        REQUIRE(ring_init(&ring, storage + 1, capacity * bytes, capacity, bytes));
        uint8_t written = 0;
        uint8_t expected = 0;
        for (uint32_t i = 0; i < 100u; i++) {
            memset(in, written, bytes);
            in[bytes - 1u] = (uint8_t)~written;
            CHECK(ring_try_push(&ring, in));
            written++;
            if (i % 3u != 2u) {
                CHECK(ring_try_pop(&ring, out));
                CHECK_EQ_U64(out[0], bytes == 1u ? (uint8_t)~expected : expected);
                CHECK_EQ_U64(out[bytes - 1u], (uint8_t)~expected);
                expected++;
            } else if (ring_count(&ring) == capacity) {
                CHECK(ring_try_pop(&ring, out));
                expected++;
            }
        }
        /* Nothing was written outside what it was given. */
        CHECK_EQ_U64(storage[0], 0u);
    }
}

TEST(ring_batches_cross_the_wrap_and_stop_at_what_fits) {
    uint32_t storage[8];
    ring_t ring;
    REQUIRE(ring_init(&ring, storage, sizeof(storage), 8u, sizeof(uint32_t)));
    uint32_t batch[12];
    uint32_t out[12];
    uint32_t next_in = 0;
    uint32_t next_out = 0;
    /* Lots of sizes, so a batch crosses the end of storage at every point. */
    for (uint32_t round = 0; round < 500u; round++) {
        const uint64_t want_in = (round * 5u + 1u) % 12u;
        for (uint32_t i = 0; i < want_in; i++) {
            batch[i] = next_in + i;
        }
        const uint64_t free_slots = 8u - ring_count(&ring);
        const uint64_t pushed = ring_push_many(&ring, batch, want_in);
        CHECK_EQ_U64(pushed, want_in < free_slots ? want_in : free_slots);
        next_in += (uint32_t)pushed;

        const uint64_t want_out = (round * 3u) % 12u;
        const uint64_t held = ring_count(&ring);
        const uint64_t popped = ring_pop_many(&ring, out, want_out);
        CHECK_EQ_U64(popped, want_out < held ? want_out : held);
        for (uint32_t i = 0; i < popped; i++) {
            CHECK_EQ_U64(out[i], next_out);
            next_out++;
        }
    }
    CHECK_EQ_U64(next_in - next_out, ring_count(&ring));
    CHECK_EQ_U64(ring_push_many(&ring, batch, 0u), 0u);
    CHECK_EQ_U64(ring_pop_many(&ring, out, 0u), 0u);
}

/* ---- Ring, across two threads ------------------------------------------------------------------------------------ */

/* An element big enough that a torn copy can happen, if the ordering were wrong. */
typedef struct stress_item_t {
    uint64_t sequence;
    uint64_t check;
    uint64_t words[6];
} stress_item_t;
static_assert(sizeof(stress_item_t) == 64);

static stress_item_t stress_item_make(uint64_t sequence) {
    stress_item_t item = {.sequence = sequence, .check = ~sequence, .words = {0}};
    for (uint32_t i = 0; i < 6u; i++) {
        item.words[i] = sequence * (i + 3u);
    }
    return item;
}

static bool stress_item_is_valid(const stress_item_t *item, uint64_t sequence) {
    const stress_item_t expected = stress_item_make(sequence);
    return memcmp(item, &expected, sizeof(expected)) == 0;
}

typedef struct stress_side_t {
    ring_t *borrowed_ring;
    uint64_t total;
    uint64_t batch; /* 1 means the single-element calls */
    uint64_t bad;   /* out of order, lost, duplicated, or torn */
    uint64_t done;
} stress_side_t;
static_assert(sizeof(stress_side_t) == 40);

static void stress_producer(void *raw) {
    stress_side_t *side = raw;
    stress_item_t batch[32];
    uint64_t sequence = 0;
    while (sequence < side->total) {
        if (side->batch == 1u) {
            const stress_item_t item = stress_item_make(sequence);
            if (ring_try_push(side->borrowed_ring, &item)) {
                sequence++;
            } else {
                (void)sched_yield();
            }
        } else {
            const uint64_t left = side->total - sequence;
            const uint64_t want = left < side->batch ? left : side->batch;
            for (uint64_t i = 0; i < want; i++) {
                batch[i] = stress_item_make(sequence + i);
            }
            uint64_t pushed = 0;
            while (pushed < want) {
                const uint64_t count = ring_push_many(side->borrowed_ring, batch + pushed, want - pushed);
                pushed += count;
                if (count == 0u) {
                    (void)sched_yield();
                }
            }
            sequence += want;
        }
    }
    side->done = sequence;
}

static void stress_consumer(void *raw) {
    stress_side_t *side = raw;
    stress_item_t batch[32];
    uint64_t sequence = 0;
    while (sequence < side->total) {
        const uint64_t want = side->batch < side->total - sequence ? side->batch : side->total - sequence;
        const uint64_t count = side->batch == 1u ? (ring_try_pop(side->borrowed_ring, batch) ? 1u : 0u)
                                                 : ring_pop_many(side->borrowed_ring, batch, want);
        for (uint64_t i = 0; i < count; i++) {
            side->bad += stress_item_is_valid(&batch[i], sequence + i) ? 0u : 1u;
        }
        sequence += count;
        if (count == 0u) {
            (void)sched_yield();
        }
    }
    side->done = sequence;
}

/* Worker places 0 and 1, which are different physical cores on this machine, so the elements really cross between
 * caches. A small ring, so the two sides keep catching each other and every full and empty path is taken. */
static void run_stress(test_context_t *t, uint64_t total, uint64_t producer_batch, uint64_t consumer_batch) {
    enum : uint64_t { CAPACITY = 64u };
    stress_item_t *storage = test_alloc(t, CAPACITY * sizeof(stress_item_t));
    ring_t ring;
    REQUIRE(ring_init(&ring, storage, CAPACITY * sizeof(stress_item_t), CAPACITY, sizeof(stress_item_t)));
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));

    stress_side_t producer = {.borrowed_ring = &ring, .total = total, .batch = producer_batch, .bad = 0, .done = 0};
    stress_side_t consumer = {.borrowed_ring = &ring, .total = total, .batch = consumer_batch, .bad = 0, .done = 0};
    thread_t threads[2];
    REQUIRE(thread_start(&threads[0], &topology, THREAD_ROLE_WORKER, 0u, stress_producer, &producer));
    if (!thread_start(&threads[1], &topology, THREAD_ROLE_WORKER, 1u, stress_consumer, &consumer)) {
        /* The producer would wait on a full ring for ever, so end the program and not just this test. */
        test_check(t, false, __FILE__, __LINE__, "started the consumer");
        _exit(1);
    }
    thread_join(&threads[0]);
    thread_join(&threads[1]);

    CHECK_EQ_U64(producer.done, total);
    CHECK_EQ_U64(consumer.done, total);
    CHECK_EQ_U64(consumer.bad, 0u);
    CHECK_EQ_U64(ring_count(&ring), 0u);
    uint8_t leftover[sizeof(stress_item_t)];
    CHECK(!ring_try_pop(&ring, leftover));
}

TEST(ring_loses_and_tears_nothing_between_two_threads) {
    run_stress(t, 3000000u, 1u, 1u);
}

TEST(ring_batches_lose_and_tear_nothing_between_two_threads) {
    run_stress(t, 3000000u, 32u, 7u);
    run_stress(t, 3000000u, 5u, 32u);
}

/* A pointer as the element: a handoff_, where the receiver takes over what it points at. */
typedef struct handoff_side_t {
    ring_t *borrowed_ring;
    uint64_t *borrowed_blocks;
    uint64_t count;
    uint64_t bad;
} handoff_side_t;
static_assert(sizeof(handoff_side_t) == 32);

static void handoff_producer(void *raw) {
    handoff_side_t *side = raw;
    for (uint64_t i = 0; i < side->count; i++) {
        /* Written before the push, and read only by the consumer after its pop. */
        uint64_t *handoff_block = &side->borrowed_blocks[i];
        *handoff_block = i * 3u + 1u;
        while (!ring_try_push(side->borrowed_ring, &handoff_block)) {
            (void)sched_yield();
        }
    }
}

static void handoff_consumer(void *raw) {
    handoff_side_t *side = raw;
    for (uint64_t i = 0; i < side->count; i++) {
        uint64_t *handoff_block = nullptr;
        while (!ring_try_pop(side->borrowed_ring, &handoff_block)) {
            (void)sched_yield();
        }
        side->bad += handoff_block == &side->borrowed_blocks[i] && *handoff_block == i * 3u + 1u ? 0u : 1u;
    }
}

TEST(ring_hands_pointers_over) {
    enum : uint64_t { COUNT = 200000u, CAPACITY = 16u };
    uint64_t **storage = test_alloc(t, CAPACITY * sizeof(uint64_t *));
    uint64_t *blocks = test_alloc(t, COUNT * sizeof(uint64_t));
    ring_t ring;
    REQUIRE(ring_init(&ring, storage, CAPACITY * sizeof(uint64_t *), CAPACITY, sizeof(uint64_t *)));
    thread_topology_t topology;
    REQUIRE(thread_topology_read(&topology));
    handoff_side_t side = {.borrowed_ring = &ring, .borrowed_blocks = blocks, .count = COUNT, .bad = 0};
    thread_t threads[2];
    REQUIRE(thread_start(&threads[0], &topology, THREAD_ROLE_WORKER, 0u, handoff_producer, &side));
    if (!thread_start(&threads[1], &topology, THREAD_ROLE_WORKER, 1u, handoff_consumer, &side)) {
        test_check(t, false, __FILE__, __LINE__, "started the consumer");
        _exit(1);
    }
    thread_join(&threads[0]);
    thread_join(&threads[1]);
    CHECK_EQ_U64(side.bad, 0u);
}

TEST_MAIN()
