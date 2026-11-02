/* ctest run_arena. See README.md. */
#include "harness/index.h"

#include "arena/index.h"
#include "heap/index.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#endif

enum : uint64_t {
    MIB = 1ull << 20,
    CHILD_OUTPUT_BYTES = 1u << 16,
};

/* ---- Helpers ----------------------------------------------------------------------------------------------------- */

/* What a real Owner holds: the region and the arena over it. */
typedef struct arena_fixture_t {
    arena_t arena_test;
    heap_region_t owned_region;
    uint8_t _pad0[48];
} arena_fixture_t;
static_assert(sizeof(arena_fixture_t) == 128);

static bool fixture_init(arena_fixture_t *fixture, uint64_t bytes) {
    if (!heap_reserve(bytes, &fixture->owned_region)) {
        return false;
    }
    arena_init(&fixture->arena_test, "test", fixture->owned_region.owned_base, fixture->owned_region.bytes);
    return true;
}

static void fixture_deinit(arena_fixture_t *fixture) {
    arena_deinit(&fixture->arena_test);
    heap_release(&fixture->owned_region);
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

/* ---- Allocating -------------------------------------------------------------------------------------------------- */

TEST(allocations_come_in_order_without_overlap) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    uint8_t *const base = fixture.owned_region.owned_base;

    uint8_t *const first = arena_alloc(arena, 100u, 1u);
    CHECK(first == base);
    CHECK_EQ_U64(arena->used, 100u);
    uint8_t *const second = arena_alloc(arena, 50u, 1u);
    CHECK(second == base + 100);
    CHECK_EQ_U64(arena->used, 150u);
    memset(first, 0x11, 100u);
    memset(second, 0x22, 50u);
    CHECK_EQ_U64(first[99], 0x11u);
    CHECK_EQ_U64(second[0], 0x22u);
    CHECK_EQ_U64(arena->alloc_count, 2u);
    CHECK_EQ_U64(arena->peak, 150u);

    fixture_deinit(&fixture);
}

TEST(alignment_pads_only_as_far_as_it_must) {
    arena_fixture_t fixture;
    REQUIRE(
        fixture_init(&fixture, 4u * MIB)); /* the loop below reaches past 2 MiB: each step's 1 byte costs a boundary */
    arena_t *const arena = &fixture.arena_test;
    uint8_t *const base = fixture.owned_region.owned_base;

    CHECK(arena_alloc(arena, 3u, 1u) == base);
    CHECK(arena_alloc(arena, 8u, 8u) == base + 8);
    CHECK_EQ_U64(arena->used, 16u);
    CHECK(arena_alloc(arena, 1u, 8u) == base + 16); /* already aligned: no gap */
    CHECK(arena_alloc(arena, 1u, 4096u) == base + 4096);
    for (uint64_t alignment = 1u; alignment <= 1u << 20; alignment <<= 1u) {
        const void *const block = arena_alloc(arena, 1u, alignment);
        CHECK_EQ_U64((uintptr_t)block & (alignment - 1u), 0u);
    }
    fixture_deinit(&fixture);
}

/* A slice of a region can start anywhere, and alignment is of the address and not of the offset. */
TEST(alignment_is_of_the_address_when_the_memory_is_a_slice) {
    heap_region_t region;
    REQUIRE(heap_reserve(2u * MIB, &region));
    arena_t slice;
    arena_init(&slice, "slice", region.owned_base + 3, 1000u);
    uint8_t *const block = arena_alloc(&slice, 8u, 64u);
    CHECK(block == region.owned_base + 64);
    CHECK_EQ_U64(slice.used, 69u);
    arena_deinit(&slice);
    heap_release(&region);
}

TEST(one_region_splits_between_arenas_that_do_not_overlap) {
    heap_region_t region;
    REQUIRE(heap_reserve(4u * MIB, &region));
    arena_t left;
    arena_t right;
    arena_init(&left, "left", region.owned_base, 2u * MIB);
    arena_init(&right, "right", region.owned_base + 2u * MIB, 2u * MIB);

    uint8_t *const in_left = arena_alloc(&left, 2u * MIB, 1u);
    uint8_t *const in_right = arena_alloc(&right, 2u * MIB, 1u);
    memset(in_left, 1, 2u * MIB);
    memset(in_right, 2, 2u * MIB);
    CHECK_EQ_U64(in_left[2u * MIB - 1u], 1u);
    CHECK_EQ_U64(in_right[0], 2u);
    arena_deinit(&right);
    arena_deinit(&left);
    heap_release(&region);
}

TEST(zero_bytes_gives_an_aligned_pointer_and_moves_only_for_the_alignment) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    CHECK(arena_alloc(arena, 0u, 1u) == fixture.owned_region.owned_base);
    CHECK_EQ_U64(arena->used, 0u);
    arena_alloc(arena, 1u, 1u);
    const void *const empty = arena_alloc(arena, 0u, 64u);
    CHECK(empty == fixture.owned_region.owned_base + 64);
    CHECK_EQ_U64(arena->used, 64u);
    fixture_deinit(&fixture);
}

TEST(zeroed_really_clears_memory_used_before_a_reset) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    memset(arena_alloc(arena, 4096u, 1u), 0xFF, 4096u);
    arena_reset(arena);
    const uint8_t *const block = arena_alloc_zeroed(arena, 4096u, 1u);
    bool all_zero = true;
    for (uint32_t i = 0; i < 4096u; i++) {
        all_zero = all_zero && block[i] == 0;
    }
    CHECK(all_zero);
    fixture_deinit(&fixture);
}

TEST(array_multiplies_and_aligns) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    arena_alloc(arena, 1u, 1u);
    const uint64_t *const items = arena_alloc_array(arena, 10u, sizeof(uint64_t), alignof(uint64_t));
    CHECK(items == (const void *)(fixture.owned_region.owned_base + 8));
    CHECK_EQ_U64(arena->used, 88u);
    CHECK(arena_alloc_array(arena, 0u, 8u, 8u) != nullptr);
    CHECK(arena_alloc_array(arena, 8u, 0u, 8u) != nullptr);
    CHECK_EQ_U64(arena->used, 88u);
    fixture_deinit(&fixture);
}

/* ---- Running out ------------------------------------------------------------------------------------------------- */

TEST(an_arena_fills_to_exactly_its_capacity) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;

    void *block = nullptr;
    CHECK(arena_alloc_try(arena, 2u * MIB - 1u, 1u, &block));
    CHECK(block != nullptr);
    CHECK(arena_alloc_try(arena, 1u, 1u, &block));
    CHECK_EQ_U64(arena->used, 2u * MIB);
    CHECK_EQ_U64(arena->failed_count, 0u);

    block = fixture.owned_region.owned_base;
    CHECK(!arena_alloc_try(arena, 1u, 1u, &block));
    CHECK(block == nullptr);
    CHECK_EQ_U64(arena->used, 2u * MIB);
    CHECK_EQ_U64(arena->failed_count, 1u);
    CHECK_EQ_U64(arena->alloc_count, 2u);
    fixture_deinit(&fixture);
}

/* What fits by size but not after its alignment gap must be refused, and must take nothing. */
TEST(the_alignment_gap_counts_against_what_is_left) {
    heap_region_t region;
    REQUIRE(heap_reserve(2u * MIB, &region));
    arena_t arena;
    arena_init(&arena, "small", region.owned_base, 128u);
    arena_alloc(&arena, 1u, 1u);
    void *block = nullptr;
    CHECK(!arena_alloc_try(&arena, 65u, 64u, &block));
    CHECK_EQ_U64(arena.used, 1u);
    CHECK(arena_alloc_try(&arena, 64u, 64u, &block));
    CHECK(block == region.owned_base + 64);
    CHECK_EQ_U64(arena.used, 128u);
    CHECK(arena_alloc_try(&arena, 0u, 64u, &block)); /* one past the end is aligned, and holds nothing */
    CHECK(!arena_alloc_try(&arena, 0u, 256u, &block));
    arena_deinit(&arena);
    heap_release(&region);
}

TEST(huge_sizes_do_not_wrap_around) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    arena_alloc(arena, 16u, 1u);
    static const uint64_t sizes[] = {UINT64_MAX, UINT64_MAX - 15u, UINT64_MAX - 4096u, 1ull << 63, 2u * MIB};
    for (uint64_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        void *block = nullptr;
        CHECK(!arena_alloc_try(arena, sizes[i], 4096u, &block));
        CHECK(block == nullptr);
        CHECK_EQ_U64(arena->used, 16u);
    }
    fixture_deinit(&fixture);
}

TEST(a_failed_try_on_an_empty_handle_is_only_a_failure) {
    arena_t empty;
    memset(&empty, 0, sizeof(empty));
    void *block = nullptr;
    CHECK(!arena_alloc_try(&empty, 1u, 1u, &block));
    CHECK_EQ_U64(empty.failed_count, 1u);
    arena_deinit(&empty);
}

static void body_exhaust(void) {
    arena_fixture_t fixture;
    if (!fixture_init(&fixture, 2u * MIB)) {
        _exit(10);
    }
    arena_alloc(&fixture.arena_test, 2u * MIB, 1u);
    arena_alloc(&fixture.arena_test, 7u, 1u);
    _exit(0);
}

static void body_array_overflow(void) {
    arena_fixture_t fixture;
    if (!fixture_init(&fixture, 2u * MIB)) {
        _exit(10);
    }
    /* Multiplied without a check this would be 8 bytes, which fits. */
    arena_alloc_array(&fixture.arena_test, (UINT64_MAX / 8u) + 2u, 8u, 8u);
    _exit(0);
}

static void body_alignment_zero(void) {
    arena_fixture_t fixture;
    if (!fixture_init(&fixture, 2u * MIB)) {
        _exit(10);
    }
    arena_alloc(&fixture.arena_test, 1u, 0u);
    _exit(0);
}

static void body_alignment_not_a_power_of_two(void) {
    arena_fixture_t fixture;
    if (!fixture_init(&fixture, 2u * MIB)) {
        _exit(10);
    }
    arena_alloc(&fixture.arena_test, 1u, 12u);
    _exit(0);
}

static void body_init_without_memory(void) {
    arena_t arena;
    arena_init(&arena, "none", nullptr, 16u);
    _exit(0);
}

TEST(running_out_aborts_naming_the_arena_and_the_numbers) {
    child_result_t result;
    REQUIRE(child_run(t, body_exhaust, &result));
    CHECK_EQ_I64(result.signal, SIGABRT);
    CHECK_CONTAINS(result.output, "arena 'test' is out of memory");
    CHECK_CONTAINS(result.output, "asked for 7 bytes");
    CHECK_CONTAINS(result.output, "2097152 of 2097152 used");
}

TEST(an_array_size_past_64_bits_aborts_and_does_not_wrap) {
    child_result_t result;
    REQUIRE(child_run(t, body_array_overflow, &result));
    CHECK_EQ_I64(result.signal, SIGABRT);
    CHECK_CONTAINS(result.output, "does not fit in 64 bits");
}

TEST(bad_arguments_abort) {
    static const child_body_t bodies[] = {body_alignment_zero, body_alignment_not_a_power_of_two,
                                          body_init_without_memory};
    for (uint64_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
        child_result_t result;
        REQUIRE(child_run(t, bodies[i], &result));
        CHECK_EQ_I64(result.signal, SIGABRT);
        CHECK_CONTAINS(result.output, "assertion failed");
    }
}

/* ---- Reset ------------------------------------------------------------------------------------------------------- */

TEST(reset_starts_over_at_the_same_address_and_keeps_the_peak) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    const void *const first = arena_alloc(arena, 1000u, 8u);
    arena_alloc(arena, 500u, 8u);
    const uint64_t peak = arena->peak;
    CHECK_EQ_U64(peak, 1500u);

    arena_reset(arena);
    CHECK_EQ_U64(arena->used, 0u);
    CHECK_EQ_U64(arena->peak, peak);
    CHECK_EQ_U64(arena->reset_count, 1u);
    CHECK(arena_alloc(arena, 8u, 8u) == first);
    CHECK_EQ_U64(arena->used, 8u);
    CHECK_EQ_U64(arena->peak, peak);

    /* A reset gives all of it back: the whole capacity fits after being full. */
    arena_reset(arena);
    arena_alloc(arena, 2u * MIB, 1u);
    arena_reset(arena);
    CHECK(arena_alloc(arena, 2u * MIB, 1u) == first);
    fixture_deinit(&fixture);
}

/* The rule from plans/CODING_STANDARD.md section 4: no heap calls in hot paths. A hot path's allocations are these. */
TEST(allocating_and_resetting_never_touch_the_heap) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    heap_counters_t before;
    heap_counters_read(&before);
    for (uint32_t frame = 0; frame < 1000u; frame++) {
        for (uint32_t i = 0; i < 100u; i++) {
            *(uint8_t *)arena_alloc(&fixture.arena_test, 64u, 64u) = (uint8_t)i;
        }
        arena_reset(&fixture.arena_test);
    }
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);
    CHECK_EQ_U64(fixture.arena_test.alloc_count, 100000u);
    CHECK_EQ_U64(fixture.arena_test.reset_count, 1000u);
    fixture_deinit(&fixture);
}

/* ---- Lifetime ---------------------------------------------------------------------------------------------------- */

TEST(deinit_empties_the_handle_and_the_memory_may_be_used_again) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    arena_alloc(arena, 8u, 8u);
    arena_deinit(arena);
    CHECK(arena->borrowed_base == nullptr);
    CHECK_EQ_U64(arena->capacity, 0u);
    CHECK_EQ_U64(arena->used, 0u);
    arena_deinit(arena); /* does nothing */

    /* Deinit hands the memory back whole, so its holder may touch any of it. */
    memset(fixture.owned_region.owned_base, 1, fixture.owned_region.bytes);
    arena_init(arena, "again", fixture.owned_region.owned_base, fixture.owned_region.bytes);
    CHECK(arena_alloc(arena, 8u, 8u) == fixture.owned_region.owned_base);
    fixture_deinit(&fixture);
    CHECK(fixture.owned_region.owned_base == nullptr);
    CHECK(heap_exit_check());
}

/* ---- Sanitize preset only ---------------------------------------------------------------------------------------- */

#ifdef __SANITIZE_ADDRESS__

TEST(asan_sees_what_is_handed_out_and_nothing_else) {
    arena_fixture_t fixture;
    REQUIRE(fixture_init(&fixture, 2u * MIB));
    arena_t *const arena = &fixture.arena_test;
    uint8_t *const base = fixture.owned_region.owned_base;
    CHECK(__asan_address_is_poisoned(base) != 0);
    CHECK(__asan_address_is_poisoned(base + 2u * MIB - 1u) != 0);

    uint8_t *const block = arena_alloc(arena, 13u, 16u);
    CHECK(__asan_address_is_poisoned(block) == 0);
    CHECK(__asan_address_is_poisoned(block + 12) == 0);
    CHECK(__asan_address_is_poisoned(block + 13) != 0);
    uint8_t *const second = arena_alloc(arena, 16u, 64u);
    CHECK(__asan_address_is_poisoned(block + 13) != 0); /* the alignment gap */
    CHECK(__asan_address_is_poisoned(second - 1) != 0);
    CHECK(__asan_address_is_poisoned(second) == 0);

    arena_reset(arena);
    CHECK(__asan_address_is_poisoned(block) != 0);
    CHECK(__asan_address_is_poisoned(second) != 0);

    arena_deinit(arena);
    CHECK(__asan_address_is_poisoned(base) == 0);
    CHECK(__asan_address_is_poisoned(base + 2u * MIB - 1u) == 0);
    heap_release(&fixture.owned_region);
}

static void body_use_after_reset(void) {
    arena_fixture_t fixture;
    if (!fixture_init(&fixture, 2u * MIB)) {
        _exit(10);
    }
    volatile uint8_t *const block = arena_alloc(&fixture.arena_test, 8u, 8u);
    block[0] = 1;
    arena_reset(&fixture.arena_test);
    block[0] = 2;
    _exit(0);
}

static void body_write_past_the_last_block(void) {
    arena_fixture_t fixture;
    if (!fixture_init(&fixture, 2u * MIB)) {
        _exit(10);
    }
    volatile uint8_t *const block = arena_alloc(&fixture.arena_test, 8u, 8u);
    block[8] = 1;
    _exit(0);
}

TEST(asan_stops_a_stray_write) {
    static const child_body_t bodies[] = {body_use_after_reset, body_write_past_the_last_block};
    for (uint64_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
        child_result_t result;
        REQUIRE(child_run(t, bodies[i], &result));
        CHECK_CONTAINS(result.output, "use-after-poison");
        CHECK(result.exit_code != 0);
    }
}

#endif

TEST_MAIN()
