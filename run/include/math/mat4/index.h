/* See run/src/math/mat4/README.md. */
#ifndef MC_RUN_MATH_MAT4_INDEX_H
#define MC_RUN_MATH_MAT4_INDEX_H

#include "math/vec3/index.h"
#include "math/vec4/index.h"
#include "types/index.h"

#include <stddef.h>

/* Column-major: m[column * 4 + row], with a column vector on the right of the matrix, so a translation is in m[12],
 * m[13] and m[14]. Aligned to 32, so that NASM may load two columns at a time with vmovaps. A pointer to one must be
 * aligned to 32 as well. */
typedef struct mat4_t {
    alignas(32) float32_t m[16];
} mat4_t;
static_assert(sizeof(mat4_t) == 64);
static_assert(alignof(mat4_t) == 32);
static_assert(offsetof(mat4_t, m) == 0);

void mat4_identity(mat4_t *out);
/* out = a * b, so b is applied to a point first. out may be a or b. */
void mat4_multiply(const mat4_t *a, const mat4_t *b, mat4_t *out);
/* out may be m. */
void mat4_transpose(const mat4_t *m, mat4_t *out);
float32_t mat4_determinant(const mat4_t *m);
/* False, with out untouched, if m has no inverse: its determinant is exactly zero or not finite. out may be m. */
bool mat4_inverse(const mat4_t *m, mat4_t *out);

vec4_t mat4_mul_vec4(const mat4_t *m, vec4_t v);
/* As a point, w = 1, for a matrix whose bottom row is 0 0 0 1. No divide. */
vec3_t mat4_transform_point(const mat4_t *m, vec3_t point);
/* As a direction, w = 0: translation has no effect. */
vec3_t mat4_transform_direction(const mat4_t *m, vec3_t direction);
/* As a point, w = 1, then divided by the resulting w, for a projection. False, with out untouched, if that w is zero:
 * the point is on the plane through the camera, and has no image. */
bool mat4_project_point(const mat4_t *m, vec3_t point, vec3_t *out);

void mat4_translation(vec3_t offset, mat4_t *out);
void mat4_scaling(vec3_t factors, mat4_t *out);
/* Counterclockwise looking down the axis toward the origin, as is right-handed. axis need not be unit length. A zero
 * axis gives the identity. */
void mat4_rotation(vec3_t axis, float32_t radians, mat4_t *out);

/* World to view: the camera at eye looks down its own -z, its +x is to the right and its +y up. False, with the identity
 * in out, if eye equals target or up is parallel to the view. */
bool mat4_look_at(vec3_t eye, vec3_t target, vec3_t up, mat4_t *out);
/* View to clip, for a camera looking down -z. After the divide, x and y run -1 to 1, y up, and depth runs 0 at near to
 * 1 at far. Needs 0 < fov_y_radians < pi, aspect > 0, 0 < near_plane < far_plane. */
void mat4_perspective(float32_t fov_y_radians, float32_t aspect, float32_t near_plane, float32_t far_plane,
                      mat4_t *out);

#endif
