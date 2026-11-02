/* ctest run_math_mat4. See README.md. */
#include "harness/index.h"

#include "math/index.h"
#include "math/mat4/index.h"

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

static bool matrix_near(const mat4_t *a, const mat4_t *b, float32_t tolerance) {
    for (uint32_t i = 0; i < 16u; i++) {
        if (!(fabsf(a->m[i] - b->m[i]) <= tolerance)) {
            return false;
        }
    }
    return true;
}

static bool vec3_near(vec3_t a, vec3_t b, float32_t tolerance) {
    return fabsf(a.x - b.x) <= tolerance && fabsf(a.y - b.y) <= tolerance && fabsf(a.z - b.z) <= tolerance;
}

/* Each entry names its row and column, so the expected values below can be read off. Entry r,c holds 10r + c + 1. */
static void numbered_matrix(mat4_t *out) {
    for (uint32_t column = 0; column < 4u; column++) {
        for (uint32_t row = 0; row < 4u; row++) {
            out->m[column * 4u + row] = (float32_t)(row * 10u + column + 1u);
        }
    }
}

TEST(identity_and_storage_order) {
    mat4_t m;
    mat4_identity(&m);
    for (uint32_t i = 0; i < 16u; i++) {
        CHECK(m.m[i] == (i % 5u == 0u ? 1.0f32 : 0.0f32));
    }
    mat4_translation(vec3_make(7.0f32, 8.0f32, 9.0f32), &m);
    CHECK(m.m[12] == 7.0f32 && m.m[13] == 8.0f32 && m.m[14] == 9.0f32);
    numbered_matrix(&m);
    /* mat4_mul_vec4 with a basis vector reads out a column, which says nothing is stored the other way round. */
    const vec4_t column_1 = mat4_mul_vec4(&m, vec4_make(0.0f32, 1.0f32, 0.0f32, 0.0f32));
    CHECK(column_1.x == 2.0f32 && column_1.y == 12.0f32 && column_1.z == 22.0f32 && column_1.w == 32.0f32);
}

TEST(multiply_against_a_product_worked_by_hand) {
    mat4_t a;
    mat4_t b;
    numbered_matrix(&a);
    mat4_transpose(&a, &b);
    mat4_t product;
    mat4_multiply(&a, &b, &product);
    /* Entry r,c of a * a^T is the dot product of rows r and c of a. Row 0 is 1 2 3 4, row 1 is 11 12 13 14, row 3 is
     * 31 32 33 34. */
    CHECK(product.m[0] == 30.0f32);
    CHECK(product.m[1] == 130.0f32);
    CHECK(product.m[3] == 330.0f32);
    CHECK(product.m[4] == 130.0f32);
    CHECK(product.m[5] == 630.0f32);
    CHECK(product.m[12] == 330.0f32);
    CHECK(product.m[15] == 4230.0f32);

    /* The same with a matrix that is not symmetric in the result: a * a, whose entry 0,1 is row 0 of a dotted with column
     * 1 of a: 1 2 3 4 with 2 12 22 32. */
    mat4_multiply(&a, &a, &product);
    CHECK(product.m[4] == 2.0f32 + 24.0f32 + 66.0f32 + 128.0f32);
    /* Entry 1,0 is row 1 with column 0: 11 12 13 14 with 1 11 21 31. */
    CHECK(product.m[1] == 11.0f32 + 132.0f32 + 273.0f32 + 434.0f32);
}

TEST(multiply_applies_the_right_hand_matrix_first) {
    mat4_t translate;
    mat4_t rotate;
    mat4_translation(vec3_make(10.0f32, 0.0f32, 0.0f32), &translate);
    mat4_rotation(vec3_make(0.0f32, 0.0f32, 1.0f32), MATH_PI * 0.5f32, &rotate);
    mat4_t translate_after_rotate;
    mat4_multiply(&translate, &rotate, &translate_after_rotate);
    const vec3_t point = vec3_make(1.0f32, 0.0f32, 0.0f32);
    CHECK(vec3_near(mat4_transform_point(&translate_after_rotate, point), vec3_make(10.0f32, 1.0f32, 0.0f32), NEAR));
    mat4_t rotate_after_translate;
    mat4_multiply(&rotate, &translate, &rotate_after_translate);
    CHECK(vec3_near(mat4_transform_point(&rotate_after_translate, point), vec3_make(0.0f32, 11.0f32, 0.0f32), NEAR));
}

TEST(multiply_and_transpose_may_write_over_an_input) {
    mat4_t a;
    mat4_t b;
    mat4_t expected;
    numbered_matrix(&a);
    mat4_transpose(&a, &b);
    mat4_multiply(&a, &b, &expected);
    mat4_t aliased = a;
    mat4_multiply(&aliased, &b, &aliased);
    CHECK_MEM_EQ(&aliased, &expected, sizeof(mat4_t));
    aliased = b;
    mat4_multiply(&a, &aliased, &aliased);
    CHECK_MEM_EQ(&aliased, &expected, sizeof(mat4_t));
    mat4_t twice;
    mat4_transpose(&a, &twice);
    mat4_transpose(&twice, &twice);
    CHECK_MEM_EQ(&twice, &a, sizeof(mat4_t));
}

TEST(determinant_of_matrices_worked_by_hand) {
    mat4_t m;
    mat4_identity(&m);
    CHECK(mat4_determinant(&m) == 1.0f32);
    mat4_scaling(vec3_make(2.0f32, 3.0f32, 4.0f32), &m);
    CHECK(mat4_determinant(&m) == 24.0f32);
    numbered_matrix(&m); /* Its rows are in arithmetic progression, so it is singular. */
    CHECK(mat4_determinant(&m) == 0.0f32);
    /* Rows 2 0 0 1, 0 3 0 0, 0 0 5 0, 1 0 0 1: expanding along the middle two gives 3 * 5 * (2 * 1 - 1 * 1). */
    const mat4_t chosen = {.m = {2.0f32, 0.0f32, 0.0f32, 1.0f32, 0.0f32, 3.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 5.0f32,
                                 0.0f32, 1.0f32, 0.0f32, 0.0f32, 1.0f32}};
    CHECK(mat4_determinant(&chosen) == 15.0f32);
}

TEST(inverse_of_a_matrix_worked_by_hand) {
    const mat4_t m = {.m = {2.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 4.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 8.0f32,
                            0.0f32, 2.0f32, 4.0f32, 8.0f32, 1.0f32}};
    const mat4_t expected = {.m = {0.5f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 0.25f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32,
                                   0.125f32, 0.0f32, -1.0f32, -1.0f32, -1.0f32, 1.0f32}};
    mat4_t inverse;
    REQUIRE(mat4_inverse(&m, &inverse));
    CHECK(matrix_near(&inverse, &expected, NEAR));
}

/* Every entry of the adjugate gets exercised by a full matrix, which an affine transform is not: this has a real bottom
 * row. */
TEST(inverse_of_random_full_matrices) {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    mat4_t identity;
    mat4_identity(&identity);
    uint32_t tried = 0;
    for (uint32_t i = 0; i < 20000u; i++) {
        mat4_t m;
        for (uint32_t j = 0; j < 16u; j++) {
            m.m[j] = next_value(&state);
        }
        /* Close to singular, float32 cannot be held to any tolerance. */
        if (fabsf(mat4_determinant(&m)) < 0.05f32) {
            continue;
        }
        tried++;
        mat4_t inverse;
        REQUIRE(mat4_inverse(&m, &inverse));
        mat4_t left;
        mat4_t right;
        mat4_multiply(&inverse, &m, &left);
        mat4_multiply(&m, &inverse, &right);
        CHECK(matrix_near(&left, &identity, 1.0e-3f32));
        CHECK(matrix_near(&right, &identity, 1.0e-3f32));
    }
    CHECK(tried > 1000u);
}

TEST(inverse_of_random_transforms_undoes_them) {
    uint64_t state = 0x123456789ABCDEFull;
    for (uint32_t i = 0; i < 5000u; i++) {
        const vec3_t axis = next_vec3(&state);
        const float32_t angle = next_value(&state) * MATH_PI;
        const vec3_t offset = vec3_scale(next_vec3(&state), 100.0f32);
        const vec3_t factors = vec3_add(vec3_scale(vec3_abs(next_vec3(&state)), 2.0f32), vec3_splat(0.5f32));
        mat4_t translate;
        mat4_t rotate;
        mat4_t scale;
        mat4_t m;
        mat4_translation(offset, &translate);
        mat4_rotation(axis, angle, &rotate);
        mat4_scaling(factors, &scale);
        mat4_multiply(&rotate, &scale, &m);
        mat4_multiply(&translate, &m, &m);
        mat4_t inverse;
        REQUIRE(mat4_inverse(&m, &inverse));
        const vec3_t point = next_vec3(&state);
        const vec3_t back = mat4_transform_point(&inverse, mat4_transform_point(&m, point));
        CHECK(vec3_near(back, point, 1.0e-3f32));
    }
}

TEST(inverse_says_when_there_is_none_and_leaves_out_alone) {
    mat4_t singular;
    numbered_matrix(&singular);
    mat4_t out;
    mat4_identity(&out);
    const mat4_t before = out;
    CHECK(!mat4_inverse(&singular, &out));
    CHECK_MEM_EQ(&out, &before, sizeof(mat4_t));

    mat4_t flat;
    mat4_scaling(vec3_make(1.0f32, 0.0f32, 1.0f32), &flat);
    CHECK(!mat4_inverse(&flat, &out));
    CHECK_MEM_EQ(&out, &before, sizeof(mat4_t));

    mat4_t not_finite;
    mat4_identity(&not_finite);
    not_finite.m[5] = NAN;
    CHECK(!mat4_inverse(&not_finite, &out));
    not_finite.m[5] = INFINITY;
    CHECK(!mat4_inverse(&not_finite, &out));
    CHECK_MEM_EQ(&out, &before, sizeof(mat4_t));
}

TEST(inverse_may_write_over_its_input) {
    mat4_t m;
    mat4_scaling(vec3_make(2.0f32, 4.0f32, 8.0f32), &m);
    REQUIRE(mat4_inverse(&m, &m));
    CHECK(m.m[0] == 0.5f32 && m.m[5] == 0.25f32 && m.m[10] == 0.125f32);
}

TEST(points_and_directions) {
    mat4_t m;
    mat4_translation(vec3_make(1.0f32, 2.0f32, 3.0f32), &m);
    const vec3_t v = vec3_make(10.0f32, 20.0f32, 30.0f32);
    CHECK(vec3_near(mat4_transform_point(&m, v), vec3_make(11.0f32, 22.0f32, 33.0f32), 0.0f32));
    CHECK(vec3_near(mat4_transform_direction(&m, v), v, 0.0f32));
    mat4_scaling(vec3_make(2.0f32, 3.0f32, 4.0f32), &m);
    CHECK(vec3_near(mat4_transform_point(&m, v), vec3_make(20.0f32, 60.0f32, 120.0f32), 0.0f32));
    CHECK(vec3_near(mat4_transform_direction(&m, v), vec3_make(20.0f32, 60.0f32, 120.0f32), 0.0f32));
}

TEST(rotation_is_counterclockwise_about_each_axis) {
    mat4_t m;
    const vec3_t x = vec3_make(1.0f32, 0.0f32, 0.0f32);
    const vec3_t y = vec3_make(0.0f32, 1.0f32, 0.0f32);
    const vec3_t z = vec3_make(0.0f32, 0.0f32, 1.0f32);
    const float32_t quarter = MATH_PI * 0.5f32;
    mat4_rotation(z, quarter, &m);
    CHECK(vec3_near(mat4_transform_direction(&m, x), y, NEAR));
    mat4_rotation(x, quarter, &m);
    CHECK(vec3_near(mat4_transform_direction(&m, y), z, NEAR));
    mat4_rotation(y, quarter, &m);
    CHECK(vec3_near(mat4_transform_direction(&m, z), x, NEAR));
    /* The axis need not be unit length, and one on a diagonal cycles the three. */
    mat4_rotation(vec3_splat(5.0f32), MATH_PI * 2.0f32 / 3.0f32, &m);
    CHECK(vec3_near(mat4_transform_direction(&m, x), y, NEAR));
    CHECK(vec3_near(mat4_transform_direction(&m, y), z, NEAR));
    CHECK(vec3_near(mat4_transform_direction(&m, z), x, NEAR));
}

TEST(rotation_about_no_axis_is_the_identity) {
    mat4_t m;
    mat4_t identity;
    mat4_identity(&identity);
    mat4_rotation(vec3_splat(0.0f32), 1.0f32, &m);
    CHECK_MEM_EQ(&m, &identity, sizeof(mat4_t));
}

TEST(project_divides_by_w_and_says_when_it_cannot) {
    /* The bottom row is 0 0 1 0, so w comes from z. */
    const mat4_t m = {.m = {1.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 1.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32, 1.0f32,
                            1.0f32, 0.0f32, 0.0f32, 0.0f32, 0.0f32}};
    vec3_t out = vec3_make(9.0f32, 9.0f32, 9.0f32);
    CHECK(mat4_project_point(&m, vec3_make(2.0f32, 4.0f32, 2.0f32), &out));
    CHECK(vec3_near(out, vec3_make(1.0f32, 2.0f32, 1.0f32), 0.0f32));
    out = vec3_make(9.0f32, 9.0f32, 9.0f32);
    CHECK(!mat4_project_point(&m, vec3_make(2.0f32, 4.0f32, 0.0f32), &out));
    CHECK(vec3_near(out, vec3_make(9.0f32, 9.0f32, 9.0f32), 0.0f32));
}

TEST(look_at_with_the_world_axes) {
    /* Standing at the origin looking east, with z up: north is to the left, so east is the camera's -z, up its +y and
     * south its +x. */
    mat4_t view;
    REQUIRE(
        mat4_look_at(vec3_splat(0.0f32), vec3_make(1.0f32, 0.0f32, 0.0f32), vec3_make(0.0f32, 0.0f32, 1.0f32), &view));
    CHECK(vec3_near(mat4_transform_point(&view, vec3_make(5.0f32, 0.0f32, 0.0f32)), vec3_make(0.0f32, 0.0f32, -5.0f32),
                    NEAR));
    CHECK(vec3_near(mat4_transform_point(&view, vec3_make(0.0f32, 0.0f32, 2.0f32)), vec3_make(0.0f32, 2.0f32, 0.0f32),
                    NEAR));
    CHECK(vec3_near(mat4_transform_point(&view, vec3_make(0.0f32, -3.0f32, 0.0f32)), vec3_make(3.0f32, 0.0f32, 0.0f32),
                    NEAR));
}

TEST(look_at_moves_the_eye_to_the_origin) {
    const vec3_t eye = vec3_make(10.0f32, -4.0f32, 70.0f32);
    const vec3_t target = vec3_make(13.0f32, 0.0f32, 70.0f32);
    mat4_t view;
    REQUIRE(mat4_look_at(eye, target, vec3_make(0.0f32, 0.0f32, 1.0f32), &view));
    CHECK(vec3_near(mat4_transform_point(&view, eye), vec3_splat(0.0f32), 1.0e-4f32));
    /* The target is 5 away, straight ahead. */
    CHECK(vec3_near(mat4_transform_point(&view, target), vec3_make(0.0f32, 0.0f32, -5.0f32), 1.0e-4f32));
    /* A rotation and a translation, so a determinant of 1. */
    CHECK_NEAR(mat4_determinant(&view), 1.0f32, NEAR);
}

TEST(look_at_with_no_answer_says_so_and_gives_the_identity) {
    mat4_t identity;
    mat4_identity(&identity);
    mat4_t view;
    const vec3_t up = vec3_make(0.0f32, 0.0f32, 1.0f32);
    CHECK(!mat4_look_at(vec3_splat(3.0f32), vec3_splat(3.0f32), up, &view));
    CHECK_MEM_EQ(&view, &identity, sizeof(mat4_t));
    CHECK(!mat4_look_at(vec3_splat(0.0f32), vec3_make(0.0f32, 0.0f32, 5.0f32), up, &view));
    CHECK_MEM_EQ(&view, &identity, sizeof(mat4_t));
}

TEST(perspective_depth_runs_zero_at_near_to_one_at_far) {
    mat4_t projection;
    mat4_perspective(math_radians(90.0f32), 2.0f32, 0.5f32, 100.0f32, &projection);
    vec3_t ndc;
    REQUIRE(mat4_project_point(&projection, vec3_make(0.0f32, 0.0f32, -0.5f32), &ndc));
    CHECK_NEAR(ndc.z, 0.0f32, NEAR);
    REQUIRE(mat4_project_point(&projection, vec3_make(0.0f32, 0.0f32, -100.0f32), &ndc));
    CHECK_NEAR(ndc.z, 1.0f32, NEAR);
    /* Depth rises with distance. */
    vec3_t nearer;
    vec3_t farther;
    REQUIRE(mat4_project_point(&projection, vec3_make(0.0f32, 0.0f32, -2.0f32), &nearer));
    REQUIRE(mat4_project_point(&projection, vec3_make(0.0f32, 0.0f32, -3.0f32), &farther));
    CHECK(nearer.z < farther.z);
}

TEST(perspective_frames_the_field_of_view) {
    /* 90 degrees up and down: at distance d, the top edge of the view is d up. With aspect 2, the right edge is 2d over. */
    mat4_t projection;
    mat4_perspective(math_radians(90.0f32), 2.0f32, 0.5f32, 100.0f32, &projection);
    vec3_t ndc;
    REQUIRE(mat4_project_point(&projection, vec3_make(0.0f32, 4.0f32, -4.0f32), &ndc));
    CHECK_NEAR(ndc.x, 0.0f32, NEAR);
    CHECK_NEAR(ndc.y, 1.0f32, NEAR);
    REQUIRE(mat4_project_point(&projection, vec3_make(8.0f32, -2.0f32, -4.0f32), &ndc));
    CHECK_NEAR(ndc.x, 1.0f32, NEAR);
    CHECK_NEAR(ndc.y, -0.5f32, NEAR);
}

TEST_MAIN()
