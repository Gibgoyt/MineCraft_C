/* ctest run_math_ivec3. See README.md. */
#include "harness/index.h"

#include "math/ivec3/index.h"

#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static bool same(ivec3_t a, int32_t x, int32_t y, int32_t z) {
    return a.x == x && a.y == y && a.z == z;
}

TEST(arithmetic_and_comparison) {
    const ivec3_t a = ivec3_make(1, -2, 3);
    const ivec3_t b = ivec3_make(-4, 5, 6);
    CHECK(same(a, 1, -2, 3));
    CHECK(same(ivec3_add(a, b), -3, 3, 9));
    CHECK(same(ivec3_sub(a, b), 5, -7, -3));
    CHECK(same(ivec3_scale(a, -3), -3, 6, -9));
    CHECK(same(ivec3_neg(a), -1, 2, -3));
    CHECK(same(ivec3_min(a, b), -4, -2, 3));
    CHECK(same(ivec3_max(a, b), 1, 5, 6));
    CHECK(ivec3_equal(a, ivec3_make(1, -2, 3)));
    CHECK(!ivec3_equal(a, ivec3_make(0, -2, 3)));
    CHECK(!ivec3_equal(a, ivec3_make(1, 0, 3)));
    CHECK(!ivec3_equal(a, ivec3_make(1, -2, 0)));
}

TEST(manhattan_cannot_overflow) {
    CHECK_EQ_I64(ivec3_manhattan(ivec3_make(1, -2, 3), ivec3_make(-4, 5, 6)), 5 + 7 + 3);
    CHECK_EQ_I64(ivec3_manhattan(ivec3_make(0, 0, 0), ivec3_make(0, 0, 0)), 0);
    const ivec3_t low = ivec3_make(INT32_MIN, INT32_MIN, INT32_MIN);
    const ivec3_t high = ivec3_make(INT32_MAX, INT32_MAX, INT32_MAX);
    CHECK_EQ_I64(ivec3_manhattan(low, high), 3ll * 4294967295ll);
}

TEST(floor_div_and_mod_on_the_cases_that_matter) {
    /* Block -1 is the last block of section -1, and block 0 is the first of section 0. */
    CHECK(same(ivec3_floor_div(ivec3_make(-1, 0, 15), 16), -1, 0, 0));
    CHECK(same(ivec3_floor_mod(ivec3_make(-1, 0, 15), 16), 15, 0, 15));
    CHECK(same(ivec3_floor_div(ivec3_make(-16, -17, 16), 16), -1, -2, 1));
    CHECK(same(ivec3_floor_mod(ivec3_make(-16, -17, 16), 16), 0, 15, 0));
    CHECK(same(ivec3_floor_div(ivec3_make(-250, 500, 0), 16), -16, 31, 0));
    CHECK(same(ivec3_floor_mod(ivec3_make(-250, 500, 0), 16), 6, 4, 0));
    CHECK(same(ivec3_floor_div(ivec3_make(-7, 7, 0), 1), -7, 7, 0));
    CHECK(same(ivec3_floor_mod(ivec3_make(-7, 7, 0), 1), 0, 0, 0));
}

/* An independent reference: repeated stepping in 64 bits, with no / or % to get wrong in the same way. */
TEST(floor_div_and_mod_match_a_stepping_reference) {
    const int32_t divisors[5] = {1, 2, 3, 16, 17};
    for (uint32_t d = 0; d < 5u; d++) {
        const int32_t divisor = divisors[d];
        int64_t quotient = -1000;
        int64_t section_start = quotient * divisor;
        for (int32_t value = -1000 * divisor; value < 1000 * divisor; value++) {
            if ((int64_t)value >= section_start + divisor) {
                section_start += divisor;
                quotient++;
            }
            const ivec3_t v = ivec3_make(value, value, value);
            const ivec3_t q = ivec3_floor_div(v, divisor);
            const ivec3_t r = ivec3_floor_mod(v, divisor);
            CHECK_EQ_I64(q.x, quotient);
            CHECK_EQ_I64(q.y, quotient);
            CHECK_EQ_I64(q.z, quotient);
            CHECK_EQ_I64(r.x, (int64_t)value - section_start);
            CHECK_EQ_I64(r.y, (int64_t)value - section_start);
            CHECK_EQ_I64(r.z, (int64_t)value - section_start);
        }
    }
}

TEST(floor_div_and_mod_at_the_limits_of_int32) {
    const ivec3_t v = ivec3_make(INT32_MIN, INT32_MAX, -1);
    const ivec3_t q = ivec3_floor_div(v, 16);
    CHECK_EQ_I64(q.x, -134217728);
    CHECK_EQ_I64(q.y, 134217727);
    CHECK_EQ_I64(q.z, -1);
    const ivec3_t r = ivec3_floor_mod(v, 16);
    CHECK_EQ_I64(r.x, 0);
    CHECK_EQ_I64(r.y, 15);
    CHECK_EQ_I64(r.z, 15);
    CHECK(same(ivec3_floor_div(ivec3_make(INT32_MIN, INT32_MAX, 0), INT32_MAX), -2, 1, 0));
    CHECK(same(ivec3_floor_mod(ivec3_make(INT32_MIN, INT32_MAX, 0), INT32_MAX), INT32_MAX - 1, 0, 0));
}

TEST(a_position_floors_to_the_block_it_is_in) {
    CHECK(same(ivec3_from_vec3_floor(vec3_make(0.5f32, -0.5f32, 2.0f32)), 0, -1, 2));
    CHECK(same(ivec3_from_vec3_floor(vec3_make(-1.0f32, -1.0001f32, 0.9999f32)), -1, -2, 0));
    CHECK(same(ivec3_from_vec3_floor(vec3_make(-0.0f32, 0.0f32, 499.5f32)), 0, 0, 499));
    /* The largest and smallest values that fit. */
    CHECK(same(ivec3_from_vec3_floor(vec3_make(-0x1.0p31f32, 0x1.fffffep30f32, 0.0f32)), INT32_MIN, 2147483520, 0));
}

TEST(converts_to_float) {
    const vec3_t v = ivec3_to_vec3(ivec3_make(-250, 500, 0));
    CHECK(v.x == -250.0f32 && v.y == 500.0f32 && v.z == 0.0f32);
}

/* Assertions exist only without NDEBUG, so in release and profile there is nothing to test here. */
#ifndef NDEBUG
typedef void (*ivec3_bad_call_t)(void);

/* Runs what is given in a child and returns whether it died of SIGABRT. Nothing in the child returns here. */
static bool dies_of_abort(ivec3_bad_call_t call) {
    const pid_t child = fork();
    if (child == 0) {
        /* The assertion's message would read like a failure of this run. */
        close(STDERR_FILENO);
        call();
        _exit(0);
    }
    int32_t status = 0;
    waitpid(child, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void divide_by_zero(void) {
    (void)ivec3_floor_div(ivec3_make(1, 2, 3), 0);
}

static void mod_by_negative(void) {
    (void)ivec3_floor_mod(ivec3_make(1, 2, 3), -16);
}

static void floor_a_nan(void) {
    (void)ivec3_from_vec3_floor(vec3_make(NAN, 0.0f32, 0.0f32));
}

static void floor_past_int32(void) {
    (void)ivec3_from_vec3_floor(vec3_make(0.0f32, 0x1.0p31f32, 0.0f32));
}

TEST(misuse_asserts) {
    CHECK(dies_of_abort(divide_by_zero));
    CHECK(dies_of_abort(mod_by_negative));
    CHECK(dies_of_abort(floor_a_nan));
    CHECK(dies_of_abort(floor_past_int32));
}
#endif

TEST_MAIN()
