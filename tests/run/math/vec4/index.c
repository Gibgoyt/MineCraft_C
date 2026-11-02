/* ctest run_math_vec4. See README.md. */
#include "harness/index.h"

#include "math/vec4/index.h"

#include <math.h>

static bool same(vec4_t a, float32_t x, float32_t y, float32_t z, float32_t w) {
    return a.x == x && a.y == y && a.z == z && a.w == w;
}

TEST(arithmetic) {
    const vec4_t a = vec4_make(1.0f32, 2.0f32, 3.0f32, 4.0f32);
    const vec4_t b = vec4_make(5.0f32, -6.0f32, 7.0f32, -8.0f32);
    CHECK(same(a, 1.0f32, 2.0f32, 3.0f32, 4.0f32));
    CHECK(same(vec4_add(a, b), 6.0f32, -4.0f32, 10.0f32, -4.0f32));
    CHECK(same(vec4_sub(a, b), -4.0f32, 8.0f32, -4.0f32, 12.0f32));
    CHECK(same(vec4_mul(a, b), 5.0f32, -12.0f32, 21.0f32, -32.0f32));
    CHECK(same(vec4_scale(a, 2.0f32), 2.0f32, 4.0f32, 6.0f32, 8.0f32));
    CHECK(same(vec4_neg(a), -1.0f32, -2.0f32, -3.0f32, -4.0f32));
}

TEST(converts_to_and_from_vec3) {
    const vec4_t a = vec4_from_vec3(vec3_make(1.0f32, 2.0f32, 3.0f32), 9.0f32);
    CHECK(same(a, 1.0f32, 2.0f32, 3.0f32, 9.0f32));
    const vec3_t back = vec4_to_vec3(vec4_make(2.0f32, 4.0f32, 6.0f32, 2.0f32));
    CHECK(back.x == 2.0f32 && back.y == 4.0f32 && back.z == 6.0f32);
}

TEST(measures) {
    const vec4_t a = vec4_make(1.0f32, 1.0f32, 3.0f32, 5.0f32);
    CHECK(vec4_dot(a, vec4_make(1.0f32, 2.0f32, 3.0f32, 4.0f32)) == 32.0f32);
    CHECK(vec4_length_squared(a) == 36.0f32);
    CHECK(vec4_length(a) == 6.0f32);
    const vec4_t unit = vec4_normalize(a);
    CHECK_NEAR(unit.x, 1.0f32 / 6.0f32, 1.0e-6f32);
    CHECK_NEAR(unit.y, 1.0f32 / 6.0f32, 1.0e-6f32);
    CHECK_NEAR(unit.z, 0.5f32, 1.0e-6f32);
    CHECK_NEAR(unit.w, 5.0f32 / 6.0f32, 1.0e-6f32);
}

TEST(normalizing_no_usable_length_gives_zero_and_never_a_nan) {
    const float32_t zero = 0.0f32;
    CHECK(same(vec4_normalize(vec4_make(zero, zero, zero, zero)), zero, zero, zero, zero));
    CHECK(same(vec4_normalize(vec4_make(1.0e-30f32, zero, zero, zero)), zero, zero, zero, zero));
    CHECK(same(vec4_normalize(vec4_make(zero, 1.0e30f32, zero, zero)), zero, zero, zero, zero));
    CHECK(same(vec4_normalize(vec4_make(zero, zero, NAN, zero)), zero, zero, zero, zero));
    CHECK(same(vec4_normalize(vec4_make(zero, zero, zero, INFINITY)), zero, zero, zero, zero));
}

TEST(lerp) {
    const vec4_t a = vec4_make(0.0f32, 2.0f32, -2.0f32, 1.0f32);
    const vec4_t b = vec4_make(2.0f32, 4.0f32, 2.0f32, 1.0f32);
    CHECK(same(vec4_lerp(a, b, 0.0f32), 0.0f32, 2.0f32, -2.0f32, 1.0f32));
    CHECK(same(vec4_lerp(a, b, 1.0f32), 2.0f32, 4.0f32, 2.0f32, 1.0f32));
    CHECK(same(vec4_lerp(a, b, 0.5f32), 1.0f32, 3.0f32, 0.0f32, 1.0f32));
}

TEST_MAIN()
