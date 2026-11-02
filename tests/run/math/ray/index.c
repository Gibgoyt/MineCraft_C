/* ctest run_math_ray. See README.md. */
#include "harness/index.h"

#include "math/index.h"
#include "math/ray/index.h"

#include <math.h>
#include <string.h>

#define NEAR 1.0e-5f32
#define T_FAR 1000.0f32

static aabb_t test_box(void) {
    return aabb_make(vec3_make(1.0f32, 1.0f32, 1.0f32), vec3_make(2.0f32, 2.0f32, 2.0f32));
}

static float32_t next_value(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return (float32_t)(int32_t)(*state % 2001u) * 0.005f32 - 5.0f32;
}

static vec3_t next_vec3(uint64_t *state) {
    const float32_t x = next_value(state);
    const float32_t y = next_value(state);
    const float32_t z = next_value(state);
    return vec3_make(x, y, z);
}

TEST(points_along_a_ray) {
    const ray_t ray = ray_make(vec3_make(1.0f32, 2.0f32, 3.0f32), vec3_make(1.0f32, 0.0f32, -2.0f32));
    const vec3_t at = ray_at(ray, 2.0f32);
    CHECK(at.x == 3.0f32 && at.y == 2.0f32 && at.z == -1.0f32);
    const vec3_t start = ray_at(ray, 0.0f32);
    CHECK(start.x == 1.0f32 && start.y == 2.0f32 && start.z == 3.0f32);
}

TEST(from_points_has_a_unit_direction_so_t_is_a_distance) {
    const ray_t ray = ray_from_points(vec3_make(1.0f32, 1.0f32, 1.0f32), vec3_make(4.0f32, 5.0f32, 1.0f32));
    CHECK_NEAR(ray.direction.x, 0.6f32, NEAR);
    CHECK_NEAR(ray.direction.y, 0.8f32, NEAR);
    CHECK(ray.direction.z == 0.0f32);
    const vec3_t at = ray_at(ray, 5.0f32);
    CHECK_NEAR(at.x, 4.0f32, NEAR);
    CHECK_NEAR(at.y, 5.0f32, NEAR);
    const ray_t none = ray_from_points(vec3_splat(1.0f32), vec3_splat(1.0f32));
    CHECK(vec3_length_squared(none.direction) == 0.0f32);
}

typedef struct face_case_t {
    vec3_t origin;
    vec3_t direction;
    float32_t t_enter;
    float32_t t_exit;
    int32_t normal_sign;
    uint32_t axis;
} face_case_t;
static_assert(sizeof(face_case_t) == 40);

TEST(comes_in_through_each_of_the_six_faces) {
    const face_case_t cases[6] = {
        {{-1.0f32, 1.5f32, 1.5f32}, {1.0f32, 0.0f32, 0.0f32}, 2.0f32, 3.0f32, -1, 0u},
        {{5.0f32, 1.5f32, 1.5f32}, {-1.0f32, 0.0f32, 0.0f32}, 3.0f32, 4.0f32, 1, 0u},
        {{1.5f32, -1.0f32, 1.5f32}, {0.0f32, 2.0f32, 0.0f32}, 1.0f32, 1.5f32, -1, 1u},
        {{1.5f32, 4.0f32, 1.5f32}, {0.0f32, -2.0f32, 0.0f32}, 1.0f32, 1.5f32, 1, 1u},
        {{1.5f32, 1.5f32, 0.0f32}, {0.0f32, 0.0f32, 1.0f32}, 1.0f32, 2.0f32, -1, 2u},
        {{1.5f32, 1.5f32, 3.0f32}, {0.0f32, 0.0f32, -1.0f32}, 1.0f32, 2.0f32, 1, 2u},
    };
    for (uint32_t i = 0; i < 6u; i++) {
        ray_aabb_hit_t hit;
        REQUIRE(ray_intersect_aabb(ray_make(cases[i].origin, cases[i].direction), test_box(), 0.0f32, T_FAR, &hit));
        CHECK(hit.t_enter == cases[i].t_enter);
        CHECK(hit.t_exit == cases[i].t_exit);
        CHECK_EQ_U64(hit.axis, cases[i].axis);
        CHECK_EQ_I64(hit.normal_sign, cases[i].normal_sign);
    }
}

TEST(misses) {
    ray_aabb_hit_t hit = {.t_enter = 99.0f32, .t_exit = 99.0f32, .axis = 9, .normal_sign = 9, ._pad0 = {0, 0}};
    const aabb_t box = test_box();
    /* Passing to one side, with a zero component and without. */
    CHECK(!ray_intersect_aabb(ray_make(vec3_make(-1.0f32, 3.0f32, 1.5f32), vec3_make(1.0f32, 0.0f32, 0.0f32)), box,
                              0.0f32, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(vec3_make(-1.0f32, 3.0f32, 1.5f32), vec3_make(1.0f32, 0.1f32, 0.0f32)), box,
                              0.0f32, T_FAR, &hit));
    /* A diagonal that passes the corner. */
    CHECK(!ray_intersect_aabb(ray_make(vec3_make(0.0f32, 2.5f32, 1.5f32), vec3_make(1.0f32, 1.0f32, 0.0f32)), box,
                              0.0f32, T_FAR, &hit));
    /* Pointing away: the line hits, and the ray does not. */
    CHECK(!ray_intersect_aabb(ray_make(vec3_make(5.0f32, 1.5f32, 1.5f32), vec3_make(1.0f32, 0.0f32, 0.0f32)), box,
                              0.0f32, T_FAR, &hit));
    CHECK(hit.t_enter == 99.0f32 && hit.axis == 9u);
}

TEST(a_ray_starting_inside_came_through_no_face) {
    ray_aabb_hit_t hit;
    REQUIRE(ray_intersect_aabb(ray_make(vec3_splat(1.5f32), vec3_make(1.0f32, 0.0f32, 0.0f32)), test_box(), 0.0f32,
                               T_FAR, &hit));
    CHECK(hit.t_enter == 0.0f32);
    CHECK(hit.t_exit == 0.5f32);
    CHECK_EQ_U64(hit.axis, RAY_AXIS_NONE);
    CHECK_EQ_I64(hit.normal_sign, 0);
}

TEST(the_t_range_limits_the_hit) {
    const ray_t ray = ray_make(vec3_make(-1.0f32, 1.5f32, 1.5f32), vec3_make(1.0f32, 0.0f32, 0.0f32));
    const aabb_t box = test_box(); /* in from t 2 to t 3 */
    ray_aabb_hit_t hit;
    CHECK(!ray_intersect_aabb(ray, box, 0.0f32, 1.5f32, &hit));
    CHECK(!ray_intersect_aabb(ray, box, 3.5f32, T_FAR, &hit));
    REQUIRE(ray_intersect_aabb(ray, box, 0.0f32, 2.5f32, &hit));
    CHECK(hit.t_enter == 2.0f32 && hit.t_exit == 2.5f32);
    CHECK_EQ_U64(hit.axis, 0u);
    /* t_min past the way in: it starts within the range, so through no face. */
    REQUIRE(ray_intersect_aabb(ray, box, 2.5f32, T_FAR, &hit));
    CHECK(hit.t_enter == 2.5f32 && hit.t_exit == 3.0f32);
    CHECK_EQ_U64(hit.axis, RAY_AXIS_NONE);
    /* A range with nothing in it. */
    CHECK(!ray_intersect_aabb(ray, box, 2.8f32, 2.2f32, &hit));
}

TEST(a_ray_along_a_wall_or_through_a_corner_counts_as_a_hit) {
    ray_aabb_hit_t hit;
    const aabb_t box = test_box();
    /* In the plane of a wall, parallel to it. The test divides nothing on that axis: dividing would be 0 / 0. */
    REQUIRE(ray_intersect_aabb(ray_make(vec3_make(0.0f32, 1.0f32, 1.5f32), vec3_make(1.0f32, 0.0f32, 0.0f32)), box,
                               0.0f32, T_FAR, &hit));
    CHECK(hit.t_enter == 1.0f32 && hit.t_exit == 2.0f32);
    REQUIRE(ray_intersect_aabb(ray_make(vec3_make(0.0f32, 2.0f32, 1.5f32), vec3_make(1.0f32, 0.0f32, 0.0f32)), box,
                               0.0f32, T_FAR, &hit));
    CHECK(hit.t_enter == 1.0f32 && hit.t_exit == 2.0f32);
    /* Touching just the corner at (1, 2): in and out at the same t. */
    REQUIRE(ray_intersect_aabb(ray_make(vec3_make(0.0f32, 1.0f32, 1.5f32), vec3_make(1.0f32, 1.0f32, 0.0f32)), box,
                               0.0f32, T_FAR, &hit));
    CHECK(hit.t_enter == 1.0f32 && hit.t_exit == 1.0f32);
}

TEST(a_zero_direction_hits_only_from_inside) {
    ray_aabb_hit_t hit;
    const aabb_t box = test_box();
    REQUIRE(ray_intersect_aabb(ray_make(vec3_splat(1.5f32), vec3_splat(0.0f32)), box, 0.25f32, 0.75f32, &hit));
    CHECK(hit.t_enter == 0.25f32 && hit.t_exit == 0.75f32);
    CHECK_EQ_U64(hit.axis, RAY_AXIS_NONE);
    CHECK(!ray_intersect_aabb(ray_make(vec3_splat(5.0f32), vec3_splat(0.0f32)), box, 0.0f32, T_FAR, &hit));
}

TEST(a_nan_anywhere_is_a_miss) {
    ray_aabb_hit_t hit;
    const aabb_t box = test_box();
    const vec3_t good_origin = vec3_make(-1.0f32, 1.5f32, 1.5f32);
    const vec3_t good_direction = vec3_make(1.0f32, 0.0f32, 0.0f32);
    CHECK(!ray_intersect_aabb(ray_make(vec3_make(NAN, 1.5f32, 1.5f32), good_direction), box, 0.0f32, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(vec3_make(-1.0f32, NAN, 1.5f32), good_direction), box, 0.0f32, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(vec3_make(-1.0f32, 1.5f32, NAN), good_direction), box, 0.0f32, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(good_origin, vec3_make(NAN, 0.0f32, 0.0f32)), box, 0.0f32, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(good_origin, vec3_make(1.0f32, NAN, 0.0f32)), box, 0.0f32, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(good_origin, vec3_make(1.0f32, 0.0f32, NAN)), box, 0.0f32, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(good_origin, good_direction), box, NAN, T_FAR, &hit));
    CHECK(!ray_intersect_aabb(ray_make(good_origin, good_direction), box, 0.0f32, NAN, &hit));
}

/* An independent reference: walk along the ray in small steps and ask aabb_contains_point. It can only miss a hit shorter
 * than a step, so a difference counts only when the walk found something and the test did not, or when the test's own
 * interval, shrunk by a little to stay off the walls, is not inside the box. */
TEST(agrees_with_walking_along_random_rays) {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    const aabb_t box = aabb_make(vec3_make(-1.0f32, -2.0f32, -0.5f32), vec3_make(1.5f32, 0.5f32, 2.0f32));
    const float32_t step = 0.02f32;
    uint32_t hits = 0;
    uint32_t misses = 0;
    for (uint32_t i = 0; i < 3000u; i++) {
        const ray_t ray = ray_make(next_vec3(&state), vec3_normalize(next_vec3(&state)));
        bool walk_hit = false;
        for (uint32_t s = 0; s < 750u && !walk_hit; s++) {
            walk_hit = aabb_contains_point(box, ray_at(ray, (float32_t)s * step));
        }
        ray_aabb_hit_t hit;
        const bool slab_hit = ray_intersect_aabb(ray, box, 0.0f32, T_FAR, &hit);
        if (walk_hit) {
            CHECK(slab_hit);
        }
        if (slab_hit) {
            hits++;
            const float32_t margin = (hit.t_exit - hit.t_enter) * 0.001f32;
            /* A sliver across a corner is shorter than rounding can resolve, so there is nothing to look at inside it. */
            if (hit.t_exit - hit.t_enter > 1.0e-3f32) {
                CHECK(aabb_contains_point(box, ray_at(ray, hit.t_enter + margin)));
                CHECK(aabb_contains_point(box, ray_at(ray, hit.t_exit - margin)));
            }
            /* Just outside each end is outside the box, unless it started inside. */
            if (hit.axis != RAY_AXIS_NONE) {
                CHECK(!aabb_contains_point(box, ray_at(ray, hit.t_enter - 0.01f32)));
            }
            CHECK(!aabb_contains_point(box, ray_at(ray, hit.t_exit + 0.01f32)));
        } else {
            misses++;
        }
    }
    CHECK(hits > 100u);
    CHECK(misses > 100u);
}

TEST(from_ndc_through_a_real_camera) {
    const vec3_t eye = vec3_make(10.0f32, 20.0f32, 70.0f32);
    const vec3_t target = vec3_make(20.0f32, 20.0f32, 70.0f32);
    mat4_t view;
    mat4_t projection;
    mat4_t view_projection;
    mat4_t inverse;
    REQUIRE(mat4_look_at(eye, target, vec3_make(0.0f32, 0.0f32, 1.0f32), &view));
    mat4_perspective(math_radians(90.0f32), 2.0f32, 0.5f32, 500.0f32, &projection);
    mat4_multiply(&projection, &view, &view_projection);
    REQUIRE(mat4_inverse(&view_projection, &inverse));

    /* The center of the screen looks east, starting on the near plane. */
    ray_t ray;
    REQUIRE(ray_from_ndc(&inverse, 0.0f32, 0.0f32, &ray));
    CHECK_NEAR(ray.direction.x, 1.0f32, 1.0e-3f32);
    CHECK_NEAR(ray.direction.y, 0.0f32, 1.0e-3f32);
    CHECK_NEAR(ray.direction.z, 0.0f32, 1.0e-3f32);
    CHECK_NEAR(ray.origin.x, 10.5f32, 1.0e-2f32);
    CHECK_NEAR(ray.origin.y, 20.0f32, 1.0e-2f32);
    CHECK_NEAR(ray.origin.z, 70.0f32, 1.0e-2f32);

    /* The top edge is 45 degrees up. */
    REQUIRE(ray_from_ndc(&inverse, 0.0f32, 1.0f32, &ray));
    CHECK_NEAR(ray.direction.x, 0.70710678f32, 1.0e-3f32);
    CHECK_NEAR(ray.direction.y, 0.0f32, 1.0e-3f32);
    CHECK_NEAR(ray.direction.z, 0.70710678f32, 1.0e-3f32);

    /* The right edge, with aspect 2, is 2 across for 1 ahead, and looking east, right is south. */
    REQUIRE(ray_from_ndc(&inverse, 1.0f32, 0.0f32, &ray));
    const float32_t root_5 = sqrtf(5.0f32);
    CHECK_NEAR(ray.direction.x, 1.0f32 / root_5, 1.0e-3f32);
    CHECK_NEAR(ray.direction.y, -2.0f32 / root_5, 1.0e-3f32);
    CHECK_NEAR(ray.direction.z, 0.0f32, 1.0e-3f32);

    /* The ray through where a world point projects passes through that point. */
    const vec3_t point = vec3_make(30.0f32, 25.0f32, 73.0f32);
    vec3_t ndc;
    REQUIRE(mat4_project_point(&view_projection, point, &ndc));
    REQUIRE(ray_from_ndc(&inverse, ndc.x, ndc.y, &ray));
    const float32_t distance = vec3_distance(ray.origin, point);
    CHECK(vec3_distance(ray_at(ray, distance), point) < 1.0e-2f32);
}

TEST(from_ndc_says_when_it_cannot) {
    mat4_t no_w;
    memset(&no_w, 0, sizeof(no_w));
    ray_t ray = ray_make(vec3_splat(9.0f32), vec3_splat(9.0f32));
    CHECK(!ray_from_ndc(&no_w, 0.0f32, 0.0f32, &ray));
    CHECK(ray.origin.x == 9.0f32);
}

TEST_MAIN()
