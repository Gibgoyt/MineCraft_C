/* ctest run_math_quat. See README.md. */
#include "harness/index.h"

#include "math/index.h"
#include "math/quat/index.h"

#include <math.h>

#define NEAR 1.0e-5f32

static float32_t next_value(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return (float32_t)(int32_t)(*state % 2001u) * 0.001f32 - 1.0f32;
}

static vec3_t next_vec3(uint64_t *state) {
    const float32_t x = next_value(state);
    const float32_t y = next_value(state);
    const float32_t z = next_value(state);
    return vec3_make(x, y, z);
}

static quat_t next_quat(uint64_t *state) {
    const vec3_t axis = next_vec3(state);
    const float32_t angle = next_value(state) * MATH_PI;
    return quat_from_axis_angle(axis, angle);
}

static bool vec3_near(vec3_t a, vec3_t b, float32_t tolerance) {
    return fabsf(a.x - b.x) <= tolerance && fabsf(a.y - b.y) <= tolerance && fabsf(a.z - b.z) <= tolerance;
}

static bool quat_near(quat_t a, quat_t b, float32_t tolerance) {
    return fabsf(a.x - b.x) <= tolerance && fabsf(a.y - b.y) <= tolerance && fabsf(a.z - b.z) <= tolerance &&
           fabsf(a.w - b.w) <= tolerance;
}

/* q and -q are one rotation. */
static bool same_rotation(quat_t a, quat_t b, float32_t tolerance) {
    return fabsf(fabsf(quat_dot(a, b)) - 1.0f32) <= tolerance;
}

static const vec3_t axis_x = {.x = 1.0f32, .y = 0.0f32, .z = 0.0f32};
static const vec3_t axis_y = {.x = 0.0f32, .y = 1.0f32, .z = 0.0f32};
static const vec3_t axis_z = {.x = 0.0f32, .y = 0.0f32, .z = 1.0f32};

TEST(identity_changes_nothing) {
    const quat_t identity = quat_identity();
    CHECK(quat_near(identity, quat_make(0.0f32, 0.0f32, 0.0f32, 1.0f32), 0.0f32));
    const vec3_t v = vec3_make(1.0f32, 2.0f32, 3.0f32);
    CHECK(vec3_near(quat_rotate(identity, v), v, 0.0f32));
    const quat_t q = quat_from_axis_angle(axis_z, 0.7f32);
    CHECK(quat_near(quat_multiply(identity, q), q, 0.0f32));
    CHECK(quat_near(quat_multiply(q, identity), q, 0.0f32));
}

TEST(axis_angle_turns_counterclockwise_about_each_axis) {
    const float32_t quarter = MATH_PI * 0.5f32;
    CHECK(vec3_near(quat_rotate(quat_from_axis_angle(axis_z, quarter), axis_x), axis_y, NEAR));
    CHECK(vec3_near(quat_rotate(quat_from_axis_angle(axis_x, quarter), axis_y), axis_z, NEAR));
    CHECK(vec3_near(quat_rotate(quat_from_axis_angle(axis_y, quarter), axis_z), axis_x, NEAR));
    /* The axis need not be unit length. */
    CHECK(
        vec3_near(quat_rotate(quat_from_axis_angle(vec3_make(0.0f32, 0.0f32, 9.0f32), quarter), axis_x), axis_y, NEAR));
    /* Half a turn is the values cos(90) and sin(90). */
    CHECK(quat_near(quat_from_axis_angle(axis_x, MATH_PI), quat_make(1.0f32, 0.0f32, 0.0f32, 0.0f32), NEAR));
}

TEST(no_axis_or_no_length_gives_the_identity) {
    CHECK(quat_near(quat_from_axis_angle(vec3_splat(0.0f32), 1.0f32), quat_identity(), 0.0f32));
    CHECK(quat_near(quat_normalize(quat_make(0.0f32, 0.0f32, 0.0f32, 0.0f32)), quat_identity(), 0.0f32));
    CHECK(quat_near(quat_normalize(quat_make(NAN, 0.0f32, 0.0f32, 1.0f32)), quat_identity(), 0.0f32));
    CHECK(quat_near(quat_normalize(quat_make(1.0e30f32, 0.0f32, 0.0f32, 1.0f32)), quat_identity(), 0.0f32));
}

TEST(normalize) {
    const quat_t q = quat_normalize(quat_make(1.0f32, 1.0f32, 1.0f32, 1.0f32));
    CHECK(quat_near(q, quat_make(0.5f32, 0.5f32, 0.5f32, 0.5f32), NEAR));
    CHECK_NEAR(quat_dot(q, q), 1.0f32, NEAR);
}

TEST(multiply_applies_the_right_hand_rotation_first) {
    const quat_t about_z = quat_from_axis_angle(axis_z, MATH_PI * 0.5f32);
    const quat_t about_x = quat_from_axis_angle(axis_x, MATH_PI * 0.5f32);
    /* z first takes x to y, and then x takes y to z. */
    CHECK(vec3_near(quat_rotate(quat_multiply(about_x, about_z), axis_x), axis_z, NEAR));
    /* x first leaves x alone, and then z takes it to y. */
    CHECK(vec3_near(quat_rotate(quat_multiply(about_z, about_x), axis_x), axis_y, NEAR));
}

TEST(conjugate_undoes_a_rotation) {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    for (uint32_t i = 0; i < 2000u; i++) {
        const quat_t q = next_quat(&state);
        CHECK(quat_near(quat_multiply(q, quat_conjugate(q)), quat_identity(), NEAR));
        const vec3_t v = next_vec3(&state);
        CHECK(vec3_near(quat_rotate(quat_conjugate(q), quat_rotate(q, v)), v, NEAR));
    }
}

TEST(rotate_and_to_mat4_agree_with_mat4_rotation) {
    uint64_t state = 0x123456789ABCDEFull;
    for (uint32_t i = 0; i < 5000u; i++) {
        const vec3_t axis = next_vec3(&state);
        const float32_t angle = next_value(&state) * MATH_PI;
        const quat_t q = quat_from_axis_angle(axis, angle);
        mat4_t reference;
        mat4_t converted;
        mat4_rotation(axis, angle, &reference);
        quat_to_mat4(q, &converted);
        for (uint32_t j = 0; j < 16u; j++) {
            CHECK_NEAR(converted.m[j], reference.m[j], NEAR);
        }
        const vec3_t v = next_vec3(&state);
        CHECK(vec3_near(quat_rotate(q, v), mat4_transform_direction(&reference, v), NEAR));
    }
}

TEST(multiply_agrees_with_matrix_multiply) {
    uint64_t state = 0xDEADBEEFCAFEF00Dull;
    for (uint32_t i = 0; i < 2000u; i++) {
        const quat_t a = next_quat(&state);
        const quat_t b = next_quat(&state);
        mat4_t matrix_a;
        mat4_t matrix_b;
        mat4_t matrix_product;
        mat4_t converted;
        quat_to_mat4(a, &matrix_a);
        quat_to_mat4(b, &matrix_b);
        mat4_multiply(&matrix_a, &matrix_b, &matrix_product);
        quat_to_mat4(quat_multiply(a, b), &converted);
        for (uint32_t j = 0; j < 16u; j++) {
            CHECK_NEAR(converted.m[j], matrix_product.m[j], 1.0e-4f32);
        }
    }
}

TEST(from_mat4_round_trips_on_random_rotations) {
    uint64_t state = 0x0123ABCD4567EF89ull;
    for (uint32_t i = 0; i < 5000u; i++) {
        const quat_t q = next_quat(&state);
        mat4_t m;
        quat_to_mat4(q, &m);
        CHECK(same_rotation(quat_from_mat4(&m), q, 1.0e-4f32));
    }
}

/* Half a turn about each axis makes the trace negative and makes that axis's component the largest, which is what picks
 * each of the four branches. The small rotation has a positive trace. Each is compared by its effect as well. */
TEST(from_mat4_on_each_branch) {
    const quat_t cases[6] = {
        quat_from_axis_angle(axis_x, 3.0f32),
        quat_from_axis_angle(axis_y, 3.0f32),
        quat_from_axis_angle(axis_z, 3.0f32),
        quat_from_axis_angle(vec3_make(1.0f32, 0.01f32, 0.02f32), MATH_PI),
        quat_from_axis_angle(vec3_make(0.02f32, 1.0f32, -0.01f32), -MATH_PI),
        quat_from_axis_angle(vec3_make(1.0f32, -2.0f32, 3.0f32), 0.1f32),
    };
    const vec3_t v = vec3_make(0.3f32, -0.5f32, 0.7f32);
    for (uint32_t i = 0; i < 6u; i++) {
        mat4_t m;
        quat_to_mat4(cases[i], &m);
        const quat_t back = quat_from_mat4(&m);
        CHECK(same_rotation(back, cases[i], 1.0e-5f32));
        CHECK(vec3_near(quat_rotate(back, v), quat_rotate(cases[i], v), 1.0e-5f32));
    }
}

TEST(slerp_ends_are_exact_enough_and_unit_length) {
    uint64_t state = 0x5555AAAA5555AAAAull;
    for (uint32_t i = 0; i < 2000u; i++) {
        const quat_t a = next_quat(&state);
        const quat_t b = next_quat(&state);
        CHECK(quat_near(quat_slerp(a, b, 0.0f32), a, 1.0e-4f32));
        CHECK(same_rotation(quat_slerp(a, b, 1.0f32), b, 1.0e-4f32));
        const quat_t middle = quat_slerp(a, b, next_value(&state) * 0.5f32 + 0.5f32);
        CHECK_NEAR(quat_dot(middle, middle), 1.0f32, 1.0e-4f32);
    }
}

TEST(slerp_moves_at_constant_speed) {
    /* A blend of two rotations about one axis is just a blend of the two angles, which a plain lerp of the components
     * misses: at 0.25 between 0 and 120 degrees, this gives 30 degrees, and the straight line about 28. */
    const quat_t a = quat_from_axis_angle(axis_z, 0.0f32);
    const quat_t b = quat_from_axis_angle(axis_z, math_radians(120.0f32));
    for (uint32_t i = 0; i <= 8u; i++) {
        const float32_t blend = (float32_t)i / 8.0f32;
        const quat_t expected = quat_from_axis_angle(axis_z, math_radians(120.0f32) * blend);
        CHECK(quat_near(quat_slerp(a, b, blend), expected, NEAR));
    }
}

TEST(slerp_takes_the_shorter_way_round) {
    const quat_t a = quat_from_axis_angle(axis_z, math_radians(10.0f32));
    const quat_t b = quat_from_axis_angle(axis_z, math_radians(350.0f32));
    /* 350 degrees is 10 the other way, so half way between is 0, not 180. */
    CHECK(same_rotation(quat_slerp(a, b, 0.5f32), quat_identity(), NEAR));
}

TEST(slerp_between_nearly_equal_rotations_stays_sound) {
    const quat_t a = quat_from_axis_angle(axis_y, 0.5f32);
    /* Close enough for the straight blend, and then each side of where it takes over, and exactly equal. */
    const float32_t gaps[5] = {0.0f32, 1.0e-6f32, 1.0e-3f32, 0.0632f32, 0.0633f32};
    for (uint32_t i = 0; i < 5u; i++) {
        const quat_t b = quat_from_axis_angle(axis_y, 0.5f32 + gaps[i]);
        const quat_t middle = quat_slerp(a, b, 0.5f32);
        const quat_t expected = quat_from_axis_angle(axis_y, 0.5f32 + gaps[i] * 0.5f32);
        CHECK(quat_near(middle, expected, NEAR));
    }
}

TEST_MAIN()
