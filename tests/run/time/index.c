/* ctest run_time. See README.md. */
#include "harness/index.h"

#include "time/index.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>

enum : int64_t {
    SLEEP_NS = 20 * TIME_NS_PER_MS,
    /* The long side, on a machine that may be running a build. */
    GENEROUS_NS = 5 * TIME_NS_PER_SECOND,
};

TEST(reads_the_same_clock_as_glibc) {
    for (uint32_t i = 0; i < 1000u; i++) {
        struct timespec before;
        struct timespec after;
        REQUIRE(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
        const int64_t now = time_now_ns();
        REQUIRE(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
        const int64_t before_ns = (int64_t)before.tv_sec * TIME_NS_PER_SECOND + (int64_t)before.tv_nsec;
        const int64_t after_ns = (int64_t)after.tv_sec * TIME_NS_PER_SECOND + (int64_t)after.tv_nsec;
        REQUIRE(before_ns <= now);
        REQUIRE(now <= after_ns);
    }
}

TEST(never_goes_backwards) {
    int64_t last = time_now_ns();
    uint64_t went_back = 0;
    bool moved = false;
    for (uint32_t i = 0; i < 2000000u; i++) {
        const int64_t now = time_now_ns();
        went_back += now < last ? 1u : 0u;
        moved = moved || now > last;
        last = now;
    }
    CHECK_EQ_U64(went_back, 0u);
    CHECK(moved);
}

/* One timeline across threads: a reading taken after seeing another thread's reading cannot be below it. */
typedef struct shared_timeline_t {
    _Atomic int64_t latest;
    _Atomic uint64_t went_back;
} shared_timeline_t;
static_assert(sizeof(shared_timeline_t) == 16);

static void *timeline_thread(void *raw) {
    shared_timeline_t *shared = raw;
    for (uint32_t i = 0; i < 500000u; i++) {
        const int64_t seen = atomic_load_explicit(&shared->latest, memory_order_acquire);
        const int64_t now = time_now_ns();
        if (now < seen) {
            atomic_fetch_add_explicit(&shared->went_back, 1u, memory_order_relaxed);
        }
        atomic_store_explicit(&shared->latest, now, memory_order_release);
    }
    return nullptr;
}

TEST(same_timeline_on_every_thread) {
    shared_timeline_t shared = {.latest = 0, .went_back = 0};
    pthread_t threads[4];
    for (uint32_t i = 0; i < 4u; i++) {
        REQUIRE(pthread_create(&threads[i], nullptr, timeline_thread, &shared) == 0);
    }
    for (uint32_t i = 0; i < 4u; i++) {
        REQUIRE(pthread_join(threads[i], nullptr) == 0);
    }
    CHECK_EQ_U64(atomic_load(&shared.went_back), 0u);
}

TEST(sleep_lasts_at_least_as_long_as_asked) {
    for (uint32_t i = 0; i < 5u; i++) {
        const int64_t start = time_now_ns();
        time_sleep_ns(SLEEP_NS);
        const int64_t elapsed = time_now_ns() - start;
        CHECK(elapsed >= SLEEP_NS);
        CHECK(elapsed < GENEROUS_NS);
    }
}

TEST(sleep_of_nothing_returns_at_once) {
    const int64_t start = time_now_ns();
    time_sleep_ns(0);
    time_sleep_ns(-TIME_NS_PER_SECOND);
    CHECK(time_now_ns() - start < GENEROUS_NS / 5);
}

TEST(sleep_until_reaches_the_deadline) {
    int64_t deadline = time_now_ns();
    for (uint32_t i = 0; i < 5u; i++) {
        deadline += SLEEP_NS;
        time_sleep_until_ns(deadline);
        CHECK(time_now_ns() >= deadline);
    }
}

TEST(sleep_until_a_past_deadline_returns_at_once) {
    const int64_t start = time_now_ns();
    time_sleep_until_ns(start - TIME_NS_PER_SECOND);
    time_sleep_until_ns(0);
    time_sleep_until_ns(-1);
    CHECK(time_now_ns() - start < GENEROUS_NS / 5);
}

/* ---- A signal during a sleep ------------------------------------------------------------------------------------- */

static _Atomic uint32_t signals_seen;

static void count_signal([[maybe_unused]] int32_t number) {
    atomic_fetch_add_explicit(&signals_seen, 1u, memory_order_relaxed);
}

typedef struct sleeper_t {
    int64_t asked_ns;
    int64_t elapsed_ns;
} sleeper_t;
static_assert(sizeof(sleeper_t) == 16);

static void *sleeper_thread(void *raw) {
    sleeper_t *sleeper = raw;
    const int64_t start = time_now_ns();
    time_sleep_ns(sleeper->asked_ns);
    sleeper->elapsed_ns = time_now_ns() - start;
    return nullptr;
}

TEST(sleep_is_not_cut_short_by_a_signal) {
    /* No SA_RESTART, so each one really does end the kernel's sleep with EINTR. */
    struct sigaction action = {.sa_handler = count_signal, .sa_flags = 0};
    REQUIRE(sigemptyset(&action.sa_mask) == 0);
    struct sigaction previous;
    REQUIRE(sigaction(SIGUSR1, &action, &previous) == 0);
    atomic_store(&signals_seen, 0u);

    sleeper_t sleeper = {.asked_ns = 200 * TIME_NS_PER_MS, .elapsed_ns = 0};
    pthread_t thread;
    REQUIRE(pthread_create(&thread, nullptr, sleeper_thread, &sleeper) == 0);
    for (uint32_t i = 0; i < 5u; i++) {
        time_sleep_ns(10 * TIME_NS_PER_MS);
        CHECK_EQ_I64(pthread_kill(thread, SIGUSR1), 0);
    }
    REQUIRE(pthread_join(thread, nullptr) == 0);
    REQUIRE(sigaction(SIGUSR1, &previous, nullptr) == 0);

    CHECK(atomic_load(&signals_seen) > 0u);
    CHECK(sleeper.elapsed_ns >= sleeper.asked_ns);
    CHECK(sleeper.elapsed_ns < GENEROUS_NS);
}

TEST(converts_durations) {
    CHECK_NEAR(time_ns_to_seconds(1500000000), 1.5f64, 1.0e-12f64);
    CHECK_NEAR(time_ns_to_ms(2500000), 2.5f64, 1.0e-12f64);
    CHECK_NEAR(time_ns_to_seconds(-TIME_NS_PER_SECOND), -1.0f64, 1.0e-12f64);
    CHECK_EQ_I64(TIME_NS_PER_SECOND, 1000 * TIME_NS_PER_MS);
}

TEST_MAIN()
