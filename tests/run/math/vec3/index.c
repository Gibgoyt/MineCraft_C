/* ctest run_math_vec3. See README.md. */
#include "harness/index.h"

#include "math/vec3/index.h"

#include <math.h>

static bool same(vec3_t a, float32_t x, float32_t y, float32_t z) {
    return a.x == x && a.y == y && a.z == z;
}

/* A fixed sequence, so that a failure repeats. */
static float32_t next_value(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return (float32_t)(int32_t)(*state % 2001u) * 0.01f32 - 10.0f32;
}

static vec3_t next_vec3(uint64_t *state) {
    const float32_t x = next_value(state);
    const float32_t y = next_value(state);
    const float32_t z = next_value(state);
    return vec3_make(x, y, z);
}

TEST(arithmetic) {
    const vec3_t a = vec3_make(1.0f32, 2.0f32, 3.0f32);
    const vec3_t b = vec3_make(4.0f32, -5.0f32, 6.0f32);
    CHECK(same(a, 1.0f32, 2.0f32, 3.0f32));
    CHECK(same(vec3_splat(7.0f32), 7.0f32, 7.0f32, 7.0f32));
    CHECK(same(vec3_add(a, b), 5.0f32, -3.0f32, 9.0f32));
    CHECK(same(vec3_sub(a, b), -3.0f32, 7.0f32, -3.0f32));
    CHECK(same(vec3_mul(a, b), 4.0f32, -10.0f32, 18.0f32));
    CHECK(same(vec3_scale(a, 2.0f32), 2.0f32, 4.0f32, 6.0f32));
    CHECK(same(vec3_neg(a), -1.0f32, -2.0f32, -3.0f32));
}

TEST(measures) {
    const vec3_t a = vec3_make(2.0f32, 3.0f32, 6.0f32);
    CHECK(vec3_dot(a, vec3_make(1.0f32, -1.0f32, 2.0f32)) == 11.0f32);
    CHECK(vec3_length_squared(a) == 49.0f32);
    CHECK(vec3_length(a) == 7.0f32);
    CHECK(vec3_distance(vec3_add(a, vec3_make(5.0f32, 5.0f32, 5.0f32)), vec3_splat(5.0f32)) == 7.0f32);
    const vec3_t unit = vec3_normalize(a);
    CHECK_NEAR(unit.x, 2.0f32 / 7.0f32, 1.0e-6f32);
    CHECK_NEAR(unit.y, 3.0f32 / 7.0f32, 1.0e-6f32);
    CHECK_NEAR(unit.z, 6.0f32 / 7.0f32, 1.0e-6f32);
}

TEST(cross_is_right_handed_with_x_east_y_north_z_up) {
    const vec3_t x = vec3_make(1.0f32, 0.0f32, 0.0f32);
    const vec3_t y = vec3_make(0.0f32, 1.0f32, 0.0f32);
    const vec3_t z = vec3_make(0.0f32, 0.0f32, 1.0f32);
    CHECK(same(vec3_cross(x, y), 0.0f32, 0.0f32, 1.0f32));
    CHECK(same(vec3_cross(y, z), 1.0f32, 0.0f32, 0.0f32));
    CHECK(same(vec3_cross(z, x), 0.0f32, 1.0f32, 0.0f32));
    CHECK(same(vec3_cross(y, x), 0.0f32, 0.0f32, -1.0f32));
    CHECK(same(vec3_cross(vec3_make(1.0f32, 2.0f32, 3.0f32), vec3_make(4.0f32, 5.0f32, 6.0f32)), -3.0f32, 6.0f32,
               -3.0f32));
}

TEST(cross_identities_on_random_inputs) {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    for (uint32_t i = 0; i < 10000u; i++) {
        const vec3_t a = next_vec3(&state);
        const vec3_t b = next_vec3(&state);
        const vec3_t c = vec3_cross(a, b);
        const float32_t scale = vec3_length(a) * vec3_length(b) + 1.0f32;
        CHECK_NEAR(vec3_dot(c, a) / (scale * scale), 0.0f32, 1.0e-5f32);
        CHECK_NEAR(vec3_dot(c, b) / (scale * scale), 0.0f32, 1.0e-5f32);
        /* Lagrange's identity: |a x b|^2 = |a|^2 |b|^2 - (a . b)^2. */
        const float32_t dot = vec3_dot(a, b);
        const float32_t lagrange = vec3_length_squared(a) * vec3_length_squared(b) - dot * dot;
        CHECK_NEAR(vec3_length_squared(c) / (scale * scale), lagrange / (scale * scale), 1.0e-4f32);
    }
}

TEST(normalize_gives_unit_length_on_random_inputs) {
    uint64_t state = 0x123456789ABCDEFull;
    for (uint32_t i = 0; i < 10000u; i++) {
        const vec3_t a = next_vec3(&state);
        if (vec3_length_squared(a) > 0.0f32) {
            CHECK_NEAR(vec3_length(vec3_normalize(a)), 1.0f32, 1.0e-5f32);
        }
    }
}

TEST(normalizing_no_usable_length_gives_zero_and_never_a_nan) {
    CHECK(same(vec3_normalize(vec3_splat(0.0f32)), 0.0f32, 0.0f32, 0.0f32));
    CHECK(same(vec3_normalize(vec3_splat(1.0e-30f32)), 0.0f32, 0.0f32, 0.0f32));
    CHECK(same(vec3_normalize(vec3_splat(1.0e30f32)), 0.0f32, 0.0f32, 0.0f32));
    CHECK(same(vec3_normalize(vec3_make(1.0f32, NAN, 0.0f32)), 0.0f32, 0.0f32, 0.0f32));
    CHECK(same(vec3_normalize(vec3_make(1.0f32, 0.0f32, INFINITY)), 0.0f32, 0.0f32, 0.0f32));
}

TEST(blends_bounds_and_rounding) {
    const vec3_t a = vec3_make(1.0f32, -2.0f32, 0.0f32);
    const vec3_t b = vec3_make(3.0f32, -6.0f32, 8.0f32);
    CHECK(same(vec3_lerp(a, b, 0.0f32), 1.0f32, -2.0f32, 0.0f32));
    CHECK(same(vec3_lerp(a, b, 1.0f32), 3.0f32, -6.0f32, 8.0f32));
    CHECK(same(vec3_lerp(a, b, 0.5f32), 2.0f32, -4.0f32, 4.0f32));
    CHECK(same(vec3_min(a, b), 1.0f32, -6.0f32, 0.0f32));
    CHECK(same(vec3_max(a, b), 3.0f32, -2.0f32, 8.0f32));
    CHECK(same(vec3_abs(vec3_make(-1.5f32, 2.5f32, -0.0f32)), 1.5f32, 2.5f32, 0.0f32));
    CHECK(same(vec3_floor(vec3_make(-0.5f32, 2.5f32, -3.0f32)), -1.0f32, 2.0f32, -3.0f32));
}

/* x * x is exactly 1 + 2^-11 + 2^-24. In float32_t that is a tie, which rounds to 1 + 2^-11, so the dot product below is
 * exactly 0 when multiplied and added as two steps, as the standard requires for the same world on every build. A fused
 * multiply-add keeps the 2^-24. volatile so that the compiler cannot work it out. */
TEST(a_dot_product_is_not_fused_into_an_fma) {
    volatile float32_t x = 0x1.001p0f32;
    volatile float32_t c = 0x1.002p0f32;
    const float32_t result = vec3_dot(vec3_make(x, 1.0f32, 0.0f32), vec3_make(x, -c, 0.0f32));
    CHECK(result == 0.0f32);
}

TEST_MAIN()
