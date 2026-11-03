/* ctest fuzz_planted. See README.md. Shows that the fuzzer finds what is there: without this, a fuzz that finds nothing proves nothing. */
#include "fuzz/support/index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct planted_context_t {
    fuzz_buffer_t buffer; /* 0 */
} planted_context_t;
static_assert(sizeof(planted_context_t) == 24);

/* Each byte right is one branch further in. Trying blindly, these eight bytes would be one chance in 2^48 after the first two. */
static bool planted_opened(const uint8_t *input, uint64_t length, uint8_t last) {
    if (length < 8u || input[0] != 'P') {
        return false;
    }
    if (input[1] != 'L') {
        return false;
    }
    if (input[2] != 'A') {
        return false;
    }
    if (input[3] != 'N') {
        return false;
    }
    if (input[4] != 'T') {
        return false;
    }
    if (input[5] != 'E') {
        return false;
    }
    if (input[6] != 'D') {
        return false;
    }
    return input[7] == last;
}

static void planted_execute_read(void *context, const uint8_t *input, uint64_t length) {
    planted_context_t *const planted = context;
    const uint8_t *const held = fuzz_buffer_hold(&planted->buffer, input, length);
    if (planted_opened(held, length, '!')) {
        /* One byte past the end: the smallest thing there is to find. */
        (void)printf("%c\n", held[length]);
    }
}

static void planted_execute_hang(void *context, const uint8_t *input, uint64_t length) {
    (void)context;
    if (length >= 2u && input[0] == 'H') {
        if (input[1] == 'A') {
            for (;;) {
                (void)usleep(1000u);
            }
        }
    }
}

static void planted_execute_oracle(void *context, const uint8_t *input, uint64_t length) {
    (void)context;
    if (length >= 2u && input[0] == 'O') {
        if (input[1] == 'K') {
            fuzz_fail("planted: the oracle said no");
        }
    }
}

static void planted_seeds(test_context_t *t, void *context, fuzz_corpus_t *corpus) {
    (void)t;
    (void)context;
    fuzz_seed_add(corpus, "PLAIN TEXT", 10u);
}

/* The program as a fuzz target, when its own check runs it. */
static void planted_fuzz(test_context_t *t, void (*execute)(void *, const uint8_t *, uint64_t), const char *name) {
    planted_context_t *const planted = test_alloc(t, sizeof(*planted));
    planted->buffer = fuzz_buffer_make(t, 4096u);
    const fuzz_target_t target = {.name = name,
                                  .borrowed_context = planted,
                                  .borrowed_tokens = nullptr,
                                  .input_capacity = 4096u,
                                  .seeds = planted_seeds,
                                  .fix = nullptr,
                                  .execute = execute,
                                  .borrowed_count_names = nullptr,
                                  .token_count = 0u,
                                  .count_name_count = 0u,
                                  .kind_count = 0u,
                                  ._pad0 = {0}};
    fuzz_run(t, &target);
}

TEST(read_past_the_end) {
    if (getenv("MC_FUZZ_PLANTED") != nullptr && strcmp(getenv("MC_FUZZ_PLANTED"), "read") == 0) {
        planted_fuzz(t, planted_execute_read, "planted_read");
    }
}

TEST(hang) {
    if (getenv("MC_FUZZ_PLANTED") != nullptr && strcmp(getenv("MC_FUZZ_PLANTED"), "hang") == 0) {
        planted_fuzz(t, planted_execute_hang, "planted_hang");
    }
}

TEST(oracle) {
    if (getenv("MC_FUZZ_PLANTED") != nullptr && strcmp(getenv("MC_FUZZ_PLANTED"), "oracle") == 0) {
        planted_fuzz(t, planted_execute_oracle, "planted_oracle");
    }
}

TEST(what_is_planted_is_found) {
    /* The run that fuzzes is one of the tests above, and this is what looks at it. It runs only from the top, by name. */
    if (getenv("MC_FUZZ_PLANTED") != nullptr) {
        return;
    }
    char *const crash_dir = test_alloc(t, 64u);
    (void)snprintf(crash_dir, 64u, "/tmp/mc_fuzz_planted_XXXXXX");
    REQUIRE(mkdtemp(crash_dir) != nullptr);
    char *const self = test_alloc(t, 1024u);
    const int64_t self_bytes = readlink("/proc/self/exe", self, 1023u);
    REQUIRE(self_bytes > 0);

    static const char *const kinds[] = {"read", "hang", "oracle"};
    static const char *const expected[] = {"planted_execute_read", "past the hang limit", "the oracle said no"};
    for (uint32_t i = 0u; i < 3u; i++) {
        char *const variable = test_alloc(t, 64u);
        (void)snprintf(variable, 64u, "MC_FUZZ_PLANTED=%s", kinds[i]);
        char *const argv[] = {(char *)"/usr/bin/env",
                              variable,
                              self,
                              (char *)"--seconds",
                              (char *)"120",
                              (char *)"--hang-seconds",
                              (char *)"3",
                              (char *)"--crash-dir",
                              crash_dir,
                              nullptr};
        test_process_t process;
        test_process_run(t, argv, 180000u, &process);
        REQUIRE(process.started != 0u);
        CHECK(process.timed_out == 0u);
        /* It says the program failed, and why, and where the input is. */
        CHECK(process.exit_code != 0);
        CHECK_CONTAINS(process.output, expected[i]);
        CHECK_CONTAINS(process.output, "to run it again");
        const char *const told = strstr(process.output, "--replay ");
        REQUIRE(told != nullptr);
        char *const kept = test_alloc(t, 256u);
        (void)snprintf(kept, 256u, "%s", told + strlen("--replay "));
        kept[strcspn(kept, "\n")] = '\0';
        /* The same input alone does the same. A hang is left out: it would hang. */
        if (i != 1u) {
            char *const again[] = {(char *)"/usr/bin/env", variable, self, (char *)"--replay", kept, nullptr};
            test_process_run(t, again, 60000u, &process);
            CHECK(process.exit_code != 0);
            CHECK_CONTAINS(process.output, expected[i]);
        }
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", crash_dir, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

TEST_MAIN()
