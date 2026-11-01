/* Tests the harness itself, including that it really reports failure: this program runs itself in a mode where its
 * sample_ tests fail on purpose, and reads what comes out. */
#include "harness/index.h"

#include <stdlib.h>
#include <string.h>

/* ---- Tests that fail on purpose. They run only under --samples. ----------------------------------------------- */

TEST(sample_failing_check) {
    CHECK(1 + 1 == 3);
    CHECK_EQ_U64(40u + 2u, 7u);
    CHECK_STR_EQ("left", "right");
}

TEST(sample_require_stops_the_test) {
    REQUIRE(false);
    CHECK_STR_EQ("this line", "must not run");
}

TEST(sample_passing) {
    CHECK(true);
}

/* ---- The harness's own tests. --------------------------------------------------------------------------------- */

static void self_run_samples(test_context_t *t, test_process_t *out, const char *self) {
    char *argv[3];
    argv[0] = test_alloc(t, strlen(self) + 1u);
    memcpy(argv[0], self, strlen(self) + 1u);
    argv[1] = test_alloc(t, sizeof("--samples"));
    memcpy(argv[1], "--samples", sizeof("--samples"));
    argv[2] = nullptr;
    test_process_run(t, argv, 30000u, out);
}

TEST(self_a_failing_check_fails_the_program) {
    REQUIRE(t->arg_count == 1);
    test_process_t process;
    self_run_samples(t, &process, t->borrowed_args[0]);
    CHECK_EQ_I64(process.exit_code, 1);
    CHECK(process.timed_out == 0);
    /* File and line, the expression, and both values. */
    CHECK_CONTAINS(process.output, "harness/index.c:11: sample_failing_check: CHECK failed: 1 + 1 == 3");
    CHECK_CONTAINS(process.output, "42 != 7");
    CHECK_CONTAINS(process.output, "\"left\" != \"right\"");
    CHECK_CONTAINS(process.output, "FAIL sample_failing_check (3 of 3 checks)");
    CHECK_CONTAINS(process.output, "ok   sample_passing");
    CHECK_CONTAINS(process.output, "harness: 3 tests, 2 failed");
    /* REQUIRE ended its test before the line after it. */
    CHECK_CONTAINS(process.output, "FAIL sample_require_stops_the_test (1 of 1 checks)");
    CHECK(strstr(process.output, "must not run") == nullptr);
}

/* The two tests below do the same thing, so whichever runs second proves that the first one's mess was cleaned up. */
static void self_check_arena_then_dirty_it(test_context_t *t) {
    uint8_t *first = test_alloc(t, 4096u);
    uint8_t *second = test_alloc(t, 64u);
    CHECK(second >= first + 4096);
    CHECK_EQ_U64((uint64_t)(uintptr_t)second % 16u, 0u);
    for (uint32_t i = 0; i < 4096u; i++) {
        CHECK_EQ_U64(first[i], 0u);
    }
    memset(first, 0xAB, 4096u);
    CHECK_EQ_U64(first[4095], 0xABu);
}

TEST(self_arena_is_zeroed_first_run) {
    self_check_arena_then_dirty_it(t);
}

TEST(self_arena_is_zeroed_second_run) {
    self_check_arena_then_dirty_it(t);
}

TEST(self_process_captures_output_and_exit_code) {
    char sh[] = "/bin/sh";
    char flag[] = "-c";
    char script[] = "echo to-stdout; echo to-stderr >&2; exit 42";
    char *const argv[] = {sh, flag, script, nullptr};
    test_process_t process;
    test_process_run(t, argv, 10000u, &process);
    CHECK_EQ_I64(process.exit_code, 42);
    CHECK(process.started != 0);
    CHECK_CONTAINS(process.output, "to-stdout");
    CHECK_CONTAINS(process.output, "to-stderr");
}

TEST(self_process_past_the_deadline_is_killed) {
    char sh[] = "/bin/sh";
    char flag[] = "-c";
    char script[] = "sleep 30 & wait";
    char *const argv[] = {sh, flag, script, nullptr};
    test_process_t process;
    test_process_run(t, argv, 200u, &process);
    CHECK(process.timed_out != 0);
    CHECK_EQ_I64(process.exit_code, 128 + 9);
}

TEST(self_process_that_cannot_start_is_reported) {
    char missing[] = "/nonexistent/program";
    char *const argv[] = {missing, nullptr};
    test_process_t process;
    test_process_run(t, argv, 10000u, &process);
    CHECK(process.started == 0);
    CHECK_EQ_I64(process.exit_code, 127);
}

TEST(self_near_compares_with_a_tolerance) {
    const int32_t a = 100;
    const int32_t b = 103;
    CHECK_NEAR(a, b, 3);
    CHECK_NEAR(b, a, 3);
}

int32_t main(int32_t argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--samples") == 0) {
        return test_main(argc, argv, "sample_");
    }
    /* The test re-runs this program, so it passes its own path along. */
    char *own_path[] = {argv[0], argv[0], nullptr};
    return test_main(2, own_path, "self_");
}
