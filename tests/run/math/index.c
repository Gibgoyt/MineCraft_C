/* ctest run_math_owner. See README.md. */
#include "harness/index.h"

#include "math/index.h"
#include "types/index.h"

#include <fenv.h>
#include <pthread.h>

/* MXCSR's flush-to-zero and denormals-are-zero bits. */
enum : uint32_t {
    MXCSR_FTZ = 0x8000u,
    MXCSR_DAZ = 0x0040u,
};

static void set_mxcsr_bits(uint32_t bits) {
    fenv_t environment;
    fegetenv(&environment);
    environment.__mxcsr |= bits;
    fesetenv(&environment);
}

TEST(init_succeeds_in_the_normal_environment_and_deinit_clears_it) {
    math_t math;
    CHECK(math_init(&math));
    CHECK_EQ_U64(math.ready, 1u);
    math_deinit(&math);
    CHECK_EQ_U64(math.ready, 0u);
}

TEST(deinit_is_safe_on_a_zeroed_math) {
    math_t math = {.ready = 0, ._pad0 = {0}};
    math_deinit(&math);
    CHECK_EQ_U64(math.ready, 0u);
}

TEST(init_refuses_every_other_rounding_mode) {
    const int32_t modes[3] = {FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    for (uint32_t i = 0; i < 3u; i++) {
        fenv_t saved;
        fegetenv(&saved);
        fesetround(modes[i]);
        math_t math;
        const bool ok = math_init(&math);
        fesetenv(&saved);
        CHECK(!ok);
        CHECK_EQ_U64(math.ready, 0u);
    }
}

TEST(init_refuses_flush_to_zero) {
    fenv_t saved;
    fegetenv(&saved);
    set_mxcsr_bits(MXCSR_FTZ);
    math_t math;
    const bool ok = math_init(&math);
    fesetenv(&saved);
    CHECK(!ok);
}

TEST(init_refuses_denormals_are_zero) {
    fenv_t saved;
    fegetenv(&saved);
    set_mxcsr_bits(MXCSR_DAZ);
    math_t math;
    const bool ok = math_init(&math);
    fesetenv(&saved);
    CHECK(!ok);
}

TEST(init_works_again_once_the_environment_is_restored) {
    fenv_t saved;
    fegetenv(&saved);
    set_mxcsr_bits(MXCSR_FTZ | MXCSR_DAZ);
    fesetround(FE_UPWARD);
    fesetenv(&saved);
    math_t math;
    CHECK(math_init(&math));
}

static void *read_rounding_mode(void *raw) {
    *(int32_t *)raw = fegetround();
    return nullptr;
}

/* main checks once, before job_init, and relies on this for every worker. */
TEST(a_new_thread_inherits_the_environment_of_the_thread_that_started_it) {
    fenv_t saved;
    fegetenv(&saved);
    fesetround(FE_UPWARD);
    int32_t seen = FE_TONEAREST;
    pthread_t thread;
    const int32_t created = pthread_create(&thread, nullptr, read_rounding_mode, &seen);
    if (created == 0) {
        pthread_join(thread, nullptr);
    }
    fesetenv(&saved);
    REQUIRE(created == 0);
    CHECK_EQ_I64(seen, FE_UPWARD);
}

TEST(radians) {
    CHECK_NEAR(math_radians(180.0f32), MATH_PI, 1.0e-6f32);
    CHECK_NEAR(math_radians(90.0f32), MATH_PI * 0.5f32, 1.0e-6f32);
    CHECK_NEAR(math_radians(-45.0f32), MATH_PI * -0.25f32, 1.0e-6f32);
    CHECK(math_radians(0.0f32) == 0.0f32);
}

TEST(clamp) {
    CHECK(math_clamp(-1.0f32, 0.0f32, 1.0f32) == 0.0f32);
    CHECK(math_clamp(2.0f32, 0.0f32, 1.0f32) == 1.0f32);
    CHECK(math_clamp(0.5f32, 0.0f32, 1.0f32) == 0.5f32);
    CHECK(math_clamp(3.0f32, 3.0f32, 3.0f32) == 3.0f32);
}

TEST(lerp_is_exact_at_both_ends_even_where_a_shorter_form_is_not) {
    /* a + (b - a) * 1 gives 0 here, since 1e8 absorbs the small value. */
    const float32_t a = 1.0e8f32;
    const float32_t b = 0.125f32;
    CHECK(math_lerp(a, b, 0.0f32) == a);
    CHECK(math_lerp(a, b, 1.0f32) == b);
    CHECK(math_lerp(2.0f32, 4.0f32, 0.5f32) == 3.0f32);
    CHECK(math_lerp(2.0f32, 4.0f32, 2.0f32) == 6.0f32);
    CHECK(math_lerp(2.0f32, 4.0f32, -1.0f32) == 0.0f32);
}

TEST_MAIN()
