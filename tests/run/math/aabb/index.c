/* ctest run_math_aabb. See README.md. */
#include "harness/index.h"

#include "math/aabb/index.h"

#include <math.h>

static bool same_vec3(vec3_t a, float32_t x, float32_t y, float32_t z) {
    return a.x == x && a.y == y && a.z == z;
}

static aabb_t unit_block(float32_t x, float32_t y, float32_t z) {
    return aabb_make(vec3_make(x, y, z), vec3_make(x + 1.0f32, y + 1.0f32, z + 1.0f32));
}

TEST(builds_and_measures) {
    const aabb_t box = aabb_make(vec3_make(1.0f32, 2.0f32, 3.0f32), vec3_make(3.0f32, 6.0f32, 9.0f32));
    CHECK(same_vec3(box.min, 1.0f32, 2.0f32, 3.0f32));
    CHECK(same_vec3(box.max, 3.0f32, 6.0f32, 9.0f32));
    CHECK(same_vec3(aabb_center(box), 2.0f32, 4.0f32, 6.0f32));
    CHECK(same_vec3(aabb_half_size(box), 1.0f32, 2.0f32, 3.0f32));
    CHECK(same_vec3(aabb_size(box), 2.0f32, 4.0f32, 6.0f32));
    CHECK(aabb_volume(box) == 48.0f32);

    const aabb_t centered = aabb_from_center(vec3_make(2.0f32, 4.0f32, 6.0f32), vec3_make(1.0f32, 2.0f32, 3.0f32));
    CHECK_MEM_EQ(&centered, &box, sizeof(aabb_t));
}

TEST(from_corners_sorts_each_axis_on_its_own) {
    const aabb_t box = aabb_from_corners(vec3_make(3.0f32, 2.0f32, 9.0f32), vec3_make(1.0f32, 6.0f32, 3.0f32));
    CHECK(same_vec3(box.min, 1.0f32, 2.0f32, 3.0f32));
    CHECK(same_vec3(box.max, 3.0f32, 6.0f32, 9.0f32));
}

TEST(validity_looks_at_every_axis) {
    CHECK(aabb_is_valid(unit_block(0.0f32, 0.0f32, 0.0f32)));
    CHECK(aabb_is_valid(aabb_make(vec3_splat(1.0f32), vec3_splat(1.0f32))));
    CHECK(!aabb_is_valid(aabb_make(vec3_make(2.0f32, 0.0f32, 0.0f32), vec3_make(1.0f32, 1.0f32, 1.0f32))));
    CHECK(!aabb_is_valid(aabb_make(vec3_make(0.0f32, 2.0f32, 0.0f32), vec3_make(1.0f32, 1.0f32, 1.0f32))));
    CHECK(!aabb_is_valid(aabb_make(vec3_make(0.0f32, 0.0f32, 2.0f32), vec3_make(1.0f32, 1.0f32, 1.0f32))));
    CHECK(!aabb_is_valid(aabb_make(vec3_make(NAN, 0.0f32, 0.0f32), vec3_make(1.0f32, 1.0f32, 1.0f32))));
}

TEST(a_point_on_a_shared_wall_is_in_exactly_one_box) {
    const aabb_t left = unit_block(0.0f32, 0.0f32, 0.0f32);
    const aabb_t right = unit_block(1.0f32, 0.0f32, 0.0f32);
    const vec3_t on_wall = vec3_make(1.0f32, 0.5f32, 0.5f32);
    CHECK(!aabb_contains_point(left, on_wall));
    CHECK(aabb_contains_point(right, on_wall));
    /* Min corner in, and each face of max out, on every axis. */
    CHECK(aabb_contains_point(left, vec3_splat(0.0f32)));
    CHECK(aabb_contains_point(left, vec3_splat(0.5f32)));
    CHECK(!aabb_contains_point(left, vec3_make(0.5f32, 1.0f32, 0.5f32)));
    CHECK(!aabb_contains_point(left, vec3_make(0.5f32, 0.5f32, 1.0f32)));
    CHECK(!aabb_contains_point(left, vec3_make(-0.1f32, 0.5f32, 0.5f32)));
    CHECK(!aabb_contains_point(left, vec3_make(0.5f32, -0.1f32, 0.5f32)));
    CHECK(!aabb_contains_point(left, vec3_make(0.5f32, 0.5f32, -0.1f32)));
    CHECK(!aabb_contains_point(left, vec3_make(NAN, 0.5f32, 0.5f32)));
}

TEST(boxes_that_only_touch_do_not_overlap) {
    const aabb_t block = unit_block(0.0f32, 0.0f32, 0.0f32);
    /* A player standing on it, and a neighbour on each axis, from each side. */
    CHECK(!aabb_overlaps(block, unit_block(0.0f32, 0.0f32, 1.0f32)));
    CHECK(!aabb_overlaps(unit_block(0.0f32, 0.0f32, 1.0f32), block));
    CHECK(!aabb_overlaps(block, unit_block(1.0f32, 0.0f32, 0.0f32)));
    CHECK(!aabb_overlaps(block, unit_block(-1.0f32, 0.0f32, 0.0f32)));
    CHECK(!aabb_overlaps(block, unit_block(0.0f32, 1.0f32, 0.0f32)));
    CHECK(!aabb_overlaps(block, unit_block(0.0f32, -1.0f32, 0.0f32)));
    CHECK(!aabb_overlaps(block, unit_block(0.0f32, 0.0f32, -1.0f32)));
}

TEST(boxes_sharing_volume_overlap) {
    const aabb_t block = unit_block(0.0f32, 0.0f32, 0.0f32);
    CHECK(aabb_overlaps(block, block));
    CHECK(aabb_overlaps(block, unit_block(0.5f32, 0.5f32, 0.5f32)));
    CHECK(aabb_overlaps(unit_block(0.5f32, 0.5f32, 0.5f32), block));
    /* One wholly inside the other. */
    const aabb_t inner = aabb_make(vec3_splat(0.25f32), vec3_splat(0.75f32));
    CHECK(aabb_overlaps(block, inner));
    CHECK(aabb_overlaps(inner, block));
    /* Apart on one axis only. */
    CHECK(!aabb_overlaps(block, unit_block(0.5f32, 0.5f32, 2.0f32)));
    CHECK(!aabb_overlaps(block, unit_block(0.5f32, 2.0f32, 0.5f32)));
    CHECK(!aabb_overlaps(block, unit_block(2.0f32, 0.5f32, 0.5f32)));
}

TEST(new_boxes) {
    const aabb_t block = unit_block(0.0f32, 0.0f32, 0.0f32);
    const aabb_t moved = aabb_translate(block, vec3_make(1.0f32, -2.0f32, 3.0f32));
    CHECK(same_vec3(moved.min, 1.0f32, -2.0f32, 3.0f32));
    CHECK(same_vec3(moved.max, 2.0f32, -1.0f32, 4.0f32));

    const aabb_t grown = aabb_expand(block, vec3_make(1.0f32, 2.0f32, 3.0f32));
    CHECK(same_vec3(grown.min, -1.0f32, -2.0f32, -3.0f32));
    CHECK(same_vec3(grown.max, 2.0f32, 3.0f32, 4.0f32));

    const aabb_t both = aabb_union(block, unit_block(3.0f32, -3.0f32, 0.5f32));
    CHECK(same_vec3(both.min, 0.0f32, -3.0f32, 0.0f32));
    CHECK(same_vec3(both.max, 4.0f32, 1.0f32, 1.5f32));

    const aabb_t shared = aabb_intersection(block, unit_block(0.5f32, -0.5f32, 0.0f32));
    CHECK(aabb_is_valid(shared));
    CHECK(same_vec3(shared.min, 0.5f32, 0.0f32, 0.0f32));
    CHECK(same_vec3(shared.max, 1.0f32, 0.5f32, 1.0f32));
    CHECK(!aabb_is_valid(aabb_intersection(block, unit_block(2.0f32, 0.0f32, 0.0f32))));
}

TEST(closest_point) {
    const aabb_t block = unit_block(0.0f32, 0.0f32, 0.0f32);
    CHECK(same_vec3(aabb_closest_point(block, vec3_make(0.5f32, 0.25f32, 0.75f32)), 0.5f32, 0.25f32, 0.75f32));
    CHECK(same_vec3(aabb_closest_point(block, vec3_make(-5.0f32, 0.5f32, 9.0f32)), 0.0f32, 0.5f32, 1.0f32));
    CHECK(same_vec3(aabb_closest_point(block, vec3_make(2.0f32, -1.0f32, 0.5f32)), 1.0f32, 0.0f32, 0.5f32));
}

TEST_MAIN()
