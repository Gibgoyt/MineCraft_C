/* ctest run_heap. See README.md. */
#include "harness/index.h"

#include "heap/index.h"
#include "log/index.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

enum : uint64_t {
    MIB = 1ull << 20,
    PAGE_BYTES = 4096u,
    CHILD_OUTPUT_BYTES = 1u << 16,
};
enum { THREAD_COUNT = 8, THREAD_ROUNDS = 100 };

/* ---- Helpers ----------------------------------------------------------------------------------------------------- */

static uint64_t counters_live_regions(void) {
    heap_counters_t counters;
    heap_counters_read(&counters);
    return counters.live_regions;
}

static uint64_t counters_live_bytes(void) {
    heap_counters_t counters;
    heap_counters_read(&counters);
    return counters.live_bytes;
}

/* Sends log lines to a nonblocking pipe for the length of a test, so expected errors land where they can be checked and
 * do not read as a failure in the ctest output. A test's lines fit in a pipe's buffer. */
typedef struct log_capture_t {
    int32_t fds[2];
    int32_t previous_fd;
    uint8_t _pad0[4];
} log_capture_t;
static_assert(sizeof(log_capture_t) == 16);

static bool log_capture_begin(log_capture_t *capture) {
    if (pipe2(capture->fds, O_NONBLOCK) != 0) {
        return false;
    }
    capture->previous_fd = log_set_fd(capture->fds[1]);
    memset(capture->_pad0, 0, sizeof(capture->_pad0));
    return true;
}

static char *log_capture_end(test_context_t *t, log_capture_t *capture) {
    log_set_fd(capture->previous_fd);
    close(capture->fds[1]);
    char *text = test_alloc(t, CHILD_OUTPUT_BYTES);
    const int64_t count = read(capture->fds[0], text, CHILD_OUTPUT_BYTES - 1u);
    close(capture->fds[0]);
    text[count > 0 ? (uint64_t)count : 0u] = '\0';
    return text;
}

typedef struct child_result_t {
    char *output;
    int32_t exit_code; /* -1 if a signal ended it */
    int32_t signal;    /* 0 unless a signal ended it */
} child_result_t;
static_assert(sizeof(child_result_t) == 16);

typedef void (*child_body_t)(void);

/* Runs body in a child with its stdout and stderr captured. No core file: an abort here is the expected result. */
static bool child_run(test_context_t *t, child_body_t body, child_result_t *out) {
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
        setrlimit(RLIMIT_CORE, &no_core);
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        body();
        _exit(0);
    }
    close(fds[1]);
    char *text = test_alloc(t, CHILD_OUTPUT_BYTES);
    uint64_t length = 0;
    while (length + 1u < CHILD_OUTPUT_BYTES) {
        const int64_t count = read(fds[0], text + length, CHILD_OUTPUT_BYTES - 1u - length);
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
    out->output = text;
    out->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    out->signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return true;
}

/* What the kernel says of the mapping holding address, read from /proc/self/smaps. Adjacent regions can merge into one
 * mapping, which does not matter to any of the facts here. */
typedef struct smaps_entry_t {
    uint64_t start;
    uint64_t end;
    uint64_t anon_huge_kib;
    uint8_t found;
    uint8_t has_huge_advice; /* VmFlags has "hg" */
    uint8_t thp_eligible;    /* THPeligible: 1 */
    uint8_t _pad0[5];
} smaps_entry_t;
static_assert(sizeof(smaps_entry_t) == 32);

static bool smaps_find(const void *address, smaps_entry_t *out) {
    *out = (smaps_entry_t){
        .start = 0, .end = 0, .anon_huge_kib = 0, .found = 0, .has_huge_advice = 0, .thp_eligible = 0, ._pad0 = {0}};
    FILE *file = fopen("/proc/self/smaps", "r");
    if (file == nullptr) {
        return false;
    }
    const uint64_t wanted = (uint64_t)(uintptr_t)address;
    char line[512];
    bool inside = false;
    while (fgets(line, sizeof(line), file) != nullptr) {
        uint64_t start = 0;
        uint64_t end = 0;
        uint64_t kib = 0;
        int32_t flag = 0;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " ", &start, &end) == 2) {
            inside = wanted >= start && wanted < end;
            if (inside) {
                out->start = start;
                out->end = end;
                out->found = 1;
            }
        } else if (inside && sscanf(line, "AnonHugePages: %" SCNu64, &kib) == 1) {
            out->anon_huge_kib = kib;
        } else if (inside && sscanf(line, "THPeligible: %" SCNd32, &flag) == 1) {
            out->thp_eligible = flag == 1 ? 1 : 0;
        } else if (inside && strncmp(line, "VmFlags:", 8u) == 0) {
            out->has_huge_advice = strstr(line, " hg") != nullptr ? 1 : 0;
        }
    }
    fclose(file);
    return out->found != 0;
}

static uint64_t process_mapped_pages(void) {
    FILE *file = fopen("/proc/self/statm", "r");
    if (file == nullptr) {
        return 0;
    }
    uint64_t pages = 0;
    if (fscanf(file, "%" SCNu64, &pages) != 1) {
        pages = 0;
    }
    fclose(file);
    return pages;
}

/* ---- Reserving --------------------------------------------------------------------------------------------------- */

/* The sizes below are written for 2 MiB. If machine.yaml changes it, this says so at once. */
static_assert(HEAP_REGION_ALIGNMENT == 2097152u);
static_assert(HEAP_BUDGET_BYTES == (uint64_t)MC_MEMORY_PROCESS_BUDGET_MIB * 1048576u);

TEST(reserve_aligns_rounds_up_and_gives_zeroed_writable_memory) {
    static const uint64_t asked[] = {1u, PAGE_BYTES, 2u * MIB - 1u, 2u * MIB, 2u * MIB + 1u, 5u * MIB, 64u * MIB};
    static const uint64_t expected[] = {2u * MIB, 2u * MIB, 2u * MIB, 2u * MIB, 4u * MIB, 6u * MIB, 64u * MIB};
    for (uint64_t i = 0; i < sizeof(asked) / sizeof(asked[0]); i++) {
        heap_region_t region;
        REQUIRE(heap_reserve(asked[i], &region));
        CHECK_EQ_U64(region.bytes, expected[i]);
        CHECK_EQ_U64((uintptr_t)region.owned_base % (2u * MIB), 0u);
        volatile uint8_t *const bytes = region.owned_base;
        CHECK_EQ_U64(bytes[0], 0u);
        CHECK_EQ_U64(bytes[region.bytes - 1u], 0u);
        bytes[0] = 0xAB;
        bytes[region.bytes - 1u] = 0xCD;
        CHECK_EQ_U64(bytes[0], 0xABu);
        CHECK_EQ_U64(bytes[region.bytes - 1u], 0xCDu);
        heap_release(&region);
    }
}

/* The slack mapped to get the alignment must all be given back, or each reservation leaks address space up to 2 MiB. */
TEST(reserve_maps_exactly_the_region_and_release_unmaps_it) {
    const uint64_t pages_before = process_mapped_pages();
    REQUIRE(pages_before != 0);
    heap_region_t region;
    REQUIRE(heap_reserve(6u * MIB, &region));
    CHECK_EQ_U64(process_mapped_pages(), pages_before + 6u * MIB / PAGE_BYTES);
    uint8_t *const base = region.owned_base;
    heap_release(&region);
    CHECK_EQ_U64(process_mapped_pages(), pages_before);

    uint8_t residency = 0;
    CHECK_EQ_I64(mincore(base, 1u, &residency), -1);
    CHECK_EQ_I64(errno, ENOMEM);
}

static void *thread_hold_stack(void *argument) {
    pthread_barrier_t *const barrier = argument;
    pthread_barrier_wait(barrier); /* the stack is mapped and the thread fully started */
    pthread_barrier_wait(barrier); /* the test is finished with it */
    return nullptr;
}

/* The kernel maps downward, so a new mapping usually ends where the one above it starts. Above a heap region that is a
 * 2 MiB boundary, nothing sticks out at the top, and the test above never runs the tail trim. Above a thread stack of an
 * odd length, which is how it goes in the real game, both ends stick out. The stack is too big for any small hole, and
 * only address space. */
TEST(both_ends_are_given_back_below_a_mapping_that_ends_off_the_boundary) {
    pthread_attr_t attributes;
    REQUIRE(pthread_attr_init(&attributes) == 0);
    REQUIRE(pthread_attr_setstacksize(&attributes, 64u * MIB + 3u * PAGE_BYTES) == 0);
    pthread_barrier_t barrier;
    REQUIRE(pthread_barrier_init(&barrier, nullptr, 2u) == 0);
    pthread_t thread;
    REQUIRE(pthread_create(&thread, &attributes, thread_hold_stack, &barrier) == 0);
    pthread_barrier_wait(&barrier);

    const uint64_t pages_before = process_mapped_pages();
    heap_region_t region;
    const bool reserved = heap_reserve(6u * MIB, &region);
    CHECK(reserved);
    if (reserved) {
        CHECK_EQ_U64((uintptr_t)region.owned_base % (2u * MIB), 0u);
        CHECK_EQ_U64(process_mapped_pages(), pages_before + 6u * MIB / PAGE_BYTES);
        heap_release(&region);
        CHECK_EQ_U64(process_mapped_pages(), pages_before);
    }

    pthread_barrier_wait(&barrier);
    pthread_join(thread, nullptr);
    pthread_barrier_destroy(&barrier);
    pthread_attr_destroy(&attributes);
}

TEST(the_kernel_shows_the_huge_page_advice_and_gives_huge_pages) {
    heap_region_t region;
    REQUIRE(heap_reserve(16u * MIB, &region));
    smaps_entry_t entry;
    REQUIRE(smaps_find(region.owned_base, &entry));
    CHECK(entry.has_huge_advice != 0);
    heap_counters_t counters;
    heap_counters_read(&counters);
    CHECK_EQ_U64(counters.huge_advice_failures, 0u);

    /* With advice given, a fault on a whole aligned 2 MiB takes a huge page if the kernel can find one and THP is on for
     * this process at all. The mapping starts on a boundary, so what comes back is not a matter of luck in placement. */
    memset(region.owned_base, 1, region.bytes);
    REQUIRE(smaps_find(region.owned_base, &entry));
    if (entry.thp_eligible != 0 && prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0) == 0) {
        CHECK(entry.anon_huge_kib >= 2048u);
    } else {
        fprintf(stderr, "note: THP is off here, so only the advice flag was checked\n");
    }
    heap_release(&region);
}

/* ---- Counters ---------------------------------------------------------------------------------------------------- */

TEST(counters_follow_reserve_and_release) {
    heap_counters_t start;
    heap_counters_read(&start);
    CHECK_EQ_U64(start.live_bytes, 0u);
    CHECK_EQ_U64(start.live_regions, 0u);
    CHECK(heap_exit_check());

    heap_region_t first;
    heap_region_t second;
    REQUIRE(heap_reserve(3u * MIB, &first));
    REQUIRE(heap_reserve(2u * MIB, &second));
    heap_counters_t both;
    heap_counters_read(&both);
    CHECK_EQ_U64(both.live_bytes, 6u * MIB);
    CHECK_EQ_U64(both.live_regions, 2u);
    CHECK_EQ_U64(both.reserve_calls, start.reserve_calls + 2u);
    CHECK_EQ_U64(both.release_calls, start.release_calls);
    CHECK(both.peak_bytes >= 6u * MIB);

    heap_release(&first);
    heap_counters_t one;
    heap_counters_read(&one);
    CHECK_EQ_U64(one.live_bytes, 2u * MIB);
    CHECK_EQ_U64(one.live_regions, 1u);
    CHECK_EQ_U64(one.release_calls, start.release_calls + 1u);
    CHECK_EQ_U64(one.peak_bytes, both.peak_bytes);

    heap_release(&second);
    CHECK_EQ_U64(counters_live_bytes(), 0u);
    CHECK_EQ_U64(counters_live_regions(), 0u);
    CHECK(heap_exit_check());
}

TEST(release_empties_the_handle_and_an_empty_handle_does_nothing) {
    heap_region_t region;
    REQUIRE(heap_reserve(2u * MIB, &region));
    heap_release(&region);
    CHECK(region.owned_base == nullptr);
    CHECK_EQ_U64(region.bytes, 0u);

    heap_counters_t before;
    heap_counters_read(&before);
    heap_release(&region);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(after.live_bytes, 0u);
}

TEST(exit_check_reports_a_leak) {
    heap_region_t region;
    REQUIRE(heap_reserve(4u * MIB, &region));
    log_capture_t capture;
    REQUIRE(log_capture_begin(&capture));
    const bool clean = heap_exit_check();
    const char *const text = log_capture_end(t, &capture);
    CHECK(!clean);
    CHECK_CONTAINS(text, "leak at exit");
    CHECK_CONTAINS(text, "1 regions");
    CHECK_CONTAINS(text, "4194304 bytes");
    heap_release(&region);
    CHECK(heap_exit_check());
}

/* ---- Refusals ---------------------------------------------------------------------------------------------------- */

TEST(bad_sizes_are_refused_with_the_handle_empty_and_nothing_counted_live) {
    static const uint64_t sizes[] = {0u, (uint64_t)HEAP_BUDGET_BYTES + 1u, 1ull << 47, UINT64_MAX, UINT64_MAX - MIB};
    for (uint64_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        heap_counters_t before;
        heap_counters_read(&before);
        heap_region_t region = {.owned_base = (uint8_t *)1, .bytes = 1u};
        log_capture_t capture;
        REQUIRE(log_capture_begin(&capture));
        const bool reserved = heap_reserve(sizes[i], &region);
        const char *const text = log_capture_end(t, &capture);
        CHECK(!reserved);
        CHECK(region.owned_base == nullptr);
        CHECK_EQ_U64(region.bytes, 0u);
        CHECK_CONTAINS(text, "cannot reserve");
        heap_counters_t after;
        heap_counters_read(&after);
        CHECK_EQ_U64(after.failed_reserves, before.failed_reserves + 1u);
        CHECK_EQ_U64(after.reserve_calls, before.reserve_calls + 1u);
        CHECK_EQ_U64(after.live_bytes, 0u);
        CHECK_EQ_U64(after.live_regions, 0u);
    }
}

/* Untouched, so the whole budget costs address space only, however large machine.yaml makes it. */
TEST(the_budget_is_on_what_is_live_and_comes_back_on_release) {
    heap_region_t whole;
    REQUIRE(heap_reserve(HEAP_BUDGET_BYTES, &whole));
    CHECK_EQ_U64(whole.bytes, HEAP_BUDGET_BYTES);

    heap_counters_t before;
    heap_counters_read(&before);
    heap_region_t extra;
    log_capture_t capture;
    REQUIRE(log_capture_begin(&capture));
    const bool reserved = heap_reserve(1u, &extra);
    const char *const text = log_capture_end(t, &capture);
    CHECK(!reserved);
    CHECK(extra.owned_base == nullptr);
    CHECK_CONTAINS(text, "over the budget");
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_bytes, HEAP_BUDGET_BYTES);
    CHECK_EQ_U64(after.live_regions, 1u);
    CHECK_EQ_U64(after.failed_reserves, before.failed_reserves + 1u);

    heap_release(&whole);
    REQUIRE(heap_reserve(1u, &extra));
    heap_release(&extra);
}

/* An address space limit refuses the mapping whatever the overcommit setting. The room left is less than the reservation
 * and more than none, because under ASan the sanitizer's own runtime still has to map memory, and dies if it cannot. */
static void body_reserve_with_mmap_refused(void) {
    const uint64_t mapped = process_mapped_pages() * PAGE_BYTES;
    if (mapped == 0) {
        _exit(9);
    }
    const struct rlimit tight = {.rlim_cur = mapped + 8u * MIB, .rlim_max = RLIM_INFINITY};
    if (setrlimit(RLIMIT_AS, &tight) != 0) {
        _exit(10);
    }
    heap_region_t region;
    if (heap_reserve(64u * MIB, &region)) {
        _exit(11);
    }
    if (region.owned_base != nullptr || region.bytes != 0) {
        _exit(12);
    }
    heap_counters_t counters;
    heap_counters_read(&counters);
    if (counters.live_bytes != 0 || counters.live_regions != 0 || counters.failed_reserves == 0) {
        _exit(13); /* the failed map must hand back the budget it claimed */
    }
    _exit(0);
}

TEST(a_real_mmap_failure_returns_false_and_gives_the_budget_back) {
    child_result_t result;
    REQUIRE(child_run(t, body_reserve_with_mmap_refused, &result));
    CHECK_EQ_I64(result.signal, 0);
    CHECK_EQ_I64(result.exit_code, 0);
    CHECK_CONTAINS(result.output, "mmap of");
    CHECK_CONTAINS(result.output, "Cannot allocate memory");
}

/* ---- Misuse aborts ----------------------------------------------------------------------------------------------- */

static void body_release_twice_through_a_stale_copy(void) {
    heap_region_t region;
    if (!heap_reserve(2u * MIB, &region)) {
        _exit(10);
    }
    heap_region_t stale = region;
    heap_release(&region);
    heap_release(&stale);
    _exit(0);
}

static void body_release_misaligned(void) {
    heap_region_t region;
    if (!heap_reserve(4u * MIB, &region)) {
        _exit(10);
    }
    region.owned_base += PAGE_BYTES;
    heap_release(&region);
    _exit(0);
}

static void body_release_bad_size(void) {
    heap_region_t region;
    if (!heap_reserve(4u * MIB, &region)) {
        _exit(10);
    }
    region.bytes = 3u * MIB;
    heap_release(&region);
    _exit(0);
}

static void body_release_size_without_base(void) {
    heap_region_t region = {.owned_base = nullptr, .bytes = 2u * MIB};
    heap_release(&region);
    _exit(0);
}

typedef struct misuse_case_t {
    child_body_t body;
    const char *expected;
} misuse_case_t;
static_assert(sizeof(misuse_case_t) == 16);

TEST(misuse_of_release_aborts_and_says_why) {
    static const misuse_case_t cases[] = {
        {body_release_twice_through_a_stale_copy, "heap_page_is_mapped"},
        {body_release_misaligned, "uintptr_t"},
        {body_release_bad_size, "bytes != 0"},
        {body_release_size_without_base, "bytes == 0"},
    };
    for (uint64_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        child_result_t result;
        REQUIRE(child_run(t, cases[i].body, &result));
        CHECK_EQ_I64(result.signal, SIGABRT);
        CHECK_CONTAINS(result.output, "assertion failed");
        CHECK_CONTAINS(result.output, "heap_release");
        CHECK_CONTAINS(result.output, cases[i].expected);
    }
}

/* ---- Threads ----------------------------------------------------------------------------------------------------- */

typedef struct thread_work_t {
    uint64_t failures;
    uint8_t mark;
    uint8_t _pad0[7];
} thread_work_t;
static_assert(sizeof(thread_work_t) == 16);

/* Marks each region it gets and reads the marks back later: two threads handed overlapping memory would show here. */
static void *thread_reserve_and_release(void *argument) {
    thread_work_t *const work = argument;
    for (uint32_t round = 0; round < THREAD_ROUNDS; round++) {
        heap_region_t first;
        heap_region_t second;
        if (!heap_reserve(2u * MIB, &first) || !heap_reserve(4u * MIB, &second)) {
            work->failures++;
            return nullptr;
        }
        first.owned_base[first.bytes - 1u] = work->mark;
        second.owned_base[0] = work->mark;
        second.owned_base[second.bytes - 1u] = work->mark;
        if (((uintptr_t)first.owned_base | (uintptr_t)second.owned_base) % (2u * MIB) != 0) {
            work->failures++;
        }
        sched_yield();
        if (first.owned_base[first.bytes - 1u] != work->mark || second.owned_base[0] != work->mark ||
            second.owned_base[second.bytes - 1u] != work->mark) {
            work->failures++;
        }
        heap_release(&first);
        heap_release(&second);
    }
    return nullptr;
}

TEST(counters_stay_exact_with_threads_racing) {
    heap_counters_t before;
    heap_counters_read(&before);
    pthread_t threads[THREAD_COUNT];
    thread_work_t work[THREAD_COUNT];
    for (uint32_t i = 0; i < THREAD_COUNT; i++) {
        work[i] = (thread_work_t){.failures = 0, .mark = (uint8_t)(i + 1u), ._pad0 = {0}};
        REQUIRE(pthread_create(&threads[i], nullptr, thread_reserve_and_release, &work[i]) == 0);
    }
    for (uint32_t i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], nullptr);
        CHECK_EQ_U64(work[i].failures, 0u);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    const uint64_t each = 2u * THREAD_COUNT * THREAD_ROUNDS;
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls + each);
    CHECK_EQ_U64(after.release_calls, before.release_calls + each);
    CHECK_EQ_U64(after.live_bytes, 0u);
    CHECK_EQ_U64(after.live_regions, 0u);
    CHECK(after.peak_bytes <= HEAP_BUDGET_BYTES);
    CHECK(heap_exit_check());
}

TEST_MAIN()
