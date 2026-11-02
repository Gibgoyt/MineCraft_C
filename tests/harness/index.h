/* The test harness. A whole test program:
 *
 *     #include "harness/index.h"
 *
 *     TEST(adds_two_numbers) {
 *         CHECK_EQ_U64(1u + 1u, 2u);
 *     }
 *
 *     TEST_MAIN()
 *
 * Put it at tests/<path>/index.c and CMake registers it with ctest by itself. See README.md. */
#ifndef MC_TESTS_HARNESS_INDEX_H
#define MC_TESTS_HARNESS_INDEX_H

#include <stdint.h>

enum : uint64_t {
    TEST_ARENA_BYTES = 1ull << 30, /* reserved, not used: only touched pages cost anything */
};
enum { TEST_MAX_CASES = 1024 };

typedef struct test_context_t {
    const char *name;     /* the running test */
    uint8_t *owned_arena; /* mapped once by test_main, reset before each test */
    char **borrowed_args; /* the program's arguments after its name: MC_TEST_ARGS in test.cmake */
    uint64_t arena_used;
    uint64_t check_count;
    uint64_t failure_count; /* in the running test */
    int32_t arg_count;
    uint8_t _pad0[4];
} test_context_t;
static_assert(sizeof(test_context_t) == 56);

typedef void (*test_function_t)(test_context_t *t);

/* Runs the registered tests. If filter is not nullptr, only tests whose name contains it run. The TEST_FILTER
 * environment variable narrows it further. Returns the exit status: 0 only if a test ran and none failed. */
int32_t test_main(int32_t argc, char **argv, const char *filter);

void test_register(const char *name, test_function_t function);

/* Zeroed memory that lasts until the running test ends. Running out fails the run. */
void *test_alloc(test_context_t *t, uint64_t bytes);

/* The result of test_process_run. */
typedef struct test_process_t {
    char *output; /* stdout and stderr together, NUL terminated, in the test arena */
    uint64_t output_length;
    int32_t exit_code; /* 128 + signal if a signal ended it */
    uint8_t timed_out; /* it ran past timeout_ms and was killed */
    uint8_t started;   /* it could be exec'd at all */
    uint8_t _pad0[2];
} test_process_t;
static_assert(sizeof(test_process_t) == 24);

/* Runs argv[0] with stdin from /dev/null and captures its output. argv ends in nullptr. The whole process group is
 * killed at the deadline, so a test cannot leave anything running. */
void test_process_run(test_context_t *t, char *const *argv, uint32_t timeout_ms, test_process_t *out);

/* Used by the macros below. Each returns whether the check held. */
bool test_check(test_context_t *t, bool ok, const char *file, int32_t line, const char *expression);
bool test_check_u64(test_context_t *t, uint64_t left, uint64_t right, const char *file, int32_t line,
                    const char *left_text, const char *right_text);
bool test_check_i64(test_context_t *t, int64_t left, int64_t right, const char *file, int32_t line,
                    const char *left_text, const char *right_text);
bool test_check_str(test_context_t *t, const char *left, const char *right, const char *file, int32_t line,
                    const char *left_text, const char *right_text);
bool test_check_contains(test_context_t *t, const char *text, const char *needle, const char *file, int32_t line,
                         const char *text_name);
bool test_check_mem(test_context_t *t, const void *left, const void *right, uint64_t bytes, const char *file,
                    int32_t line, const char *left_text, const char *right_text);

/* Defines a test and registers it before main runs. The body sees the context as t. */
#define TEST(test_name)                                                                                                \
    static void test_body_##test_name(test_context_t *t);                                                              \
    [[gnu::constructor]] static void test_add_##test_name(void) {                                                      \
        test_register(#test_name, test_body_##test_name);                                                              \
    }                                                                                                                  \
    static void test_body_##test_name([[maybe_unused]] test_context_t *t)

/* The usual main. A test that needs its own calls test_main itself. */
#define TEST_MAIN()                                                                                                    \
    int32_t main(int32_t argc, char **argv) {                                                                          \
        return test_main(argc, argv, nullptr);                                                                         \
    }

/* CHECK records a failure and keeps going. REQUIRE records it and ends this test, for when the rest would crash. */
#define CHECK(condition) test_check(t, (condition), __FILE__, __LINE__, #condition)
#define REQUIRE(condition)                                                                                             \
    do {                                                                                                               \
        if (!test_check(t, (condition), __FILE__, __LINE__, #condition)) {                                             \
            return;                                                                                                    \
        }                                                                                                              \
    } while (false)
#define CHECK_EQ_U64(left, right) test_check_u64(t, (left), (right), __FILE__, __LINE__, #left, #right)
#define CHECK_EQ_I64(left, right) test_check_i64(t, (left), (right), __FILE__, __LINE__, #left, #right)
#define CHECK_STR_EQ(left, right) test_check_str(t, (left), (right), __FILE__, __LINE__, #left, #right)
#define CHECK_CONTAINS(text, needle) test_check_contains(t, (text), (needle), __FILE__, __LINE__, #text)
#define CHECK_MEM_EQ(left, right, bytes) test_check_mem(t, (left), (right), (bytes), __FILE__, __LINE__, #left, #right)
/* For float32_t and float64_t alike, which is why it is a macro and why this header names no floating-point type. */
#define CHECK_NEAR(left, right, tolerance)                                                                             \
    do {                                                                                                               \
        typeof((left) - (right)) test_near_gap = (left) - (right);                                                     \
        test_near_gap = test_near_gap < 0 ? -test_near_gap : test_near_gap;                                            \
        test_check(t, test_near_gap <= (tolerance), __FILE__, __LINE__, #left " is near " #right);                     \
    } while (false)

#endif
