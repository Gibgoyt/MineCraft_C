/* ctest run_math_vec2. See README.md. */
#include "harness/index.h"

#include "math/vec2/index.h"

#include <math.h>

static bool same(vec2_t a, float32_t x, float32_t y) {
    return a.x == x && a.y == y;
}

TEST(arithmetic) {
    const vec2_t a = vec2_make(1.0f32, 2.0f32);
    const vec2_t b = vec2_make(3.0f32, -5.0f32);
    CHECK(same(a, 1.0f32, 2.0f32));
    CHECK(same(vec2_add(a, b), 4.0f32, -3.0f32));
    CHECK(same(vec2_sub(a, b), -2.0f32, 7.0f32));
    CHECK(same(vec2_mul(a, b), 3.0f32, -10.0f32));
    CHECK(same(vec2_scale(a, 3.0f32), 3.0f32, 6.0f32));
    CHECK(same(vec2_neg(a), -1.0f32, -2.0f32));
}

TEST(measures) {
    const vec2_t a = vec2_make(3.0f32, 4.0f32);
    CHECK(vec2_dot(a, vec2_make(2.0f32, -1.0f32)) == 2.0f32);
    CHECK(vec2_length_squared(a) == 25.0f32);
    CHECK(vec2_length(a) == 5.0f32);
    const vec2_t unit = vec2_normalize(a);
    CHECK_NEAR(unit.x, 0.6f32, 1.0e-6f32);
    CHECK_NEAR(unit.y, 0.8f32, 1.0e-6f32);
}

TEST(normalizing_no_usable_length_gives_zero_and_never_a_nan) {
    CHECK(same(vec2_normalize(vec2_make(0.0f32, 0.0f32)), 0.0f32, 0.0f32));
    CHECK(same(vec2_normalize(vec2_make(1.0e-30f32, 0.0f32)), 0.0f32, 0.0f32));
    CHECK(same(vec2_normalize(vec2_make(1.0e30f32, 0.0f32)), 0.0f32, 0.0f32));
    CHECK(same(vec2_normalize(vec2_make(NAN, 1.0f32)), 0.0f32, 0.0f32));
    CHECK(same(vec2_normalize(vec2_make(INFINITY, 1.0f32)), 0.0f32, 0.0f32));
}

TEST(blends_and_bounds) {
    const vec2_t a = vec2_make(1.0f32, -2.0f32);
    const vec2_t b = vec2_make(3.0f32, -6.0f32);
    CHECK(same(vec2_lerp(a, b, 0.0f32), 1.0f32, -2.0f32));
    CHECK(same(vec2_lerp(a, b, 1.0f32), 3.0f32, -6.0f32));
    CHECK(same(vec2_lerp(a, b, 0.5f32), 2.0f32, -4.0f32));
    CHECK(same(vec2_min(a, b), 1.0f32, -6.0f32));
    CHECK(same(vec2_max(a, b), 3.0f32, -2.0f32));
}

TEST_MAIN()
