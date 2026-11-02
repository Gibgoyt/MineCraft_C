/* See run/src/math/quat/README.md. */
#ifndef MC_RUN_MATH_QUAT_INDEX_H
#define MC_RUN_MATH_QUAT_INDEX_H

#include "math/mat4/index.h"
#include "math/vec3/index.h"
#include "types/index.h"

#include <stddef.h>

/* A rotation, as x i + y j + z k + w, with w last. Aligned to 16, like vec4_t, so a pointer to one must be too. */
typedef struct quat_t {
    alignas(16) float32_t x;
    float32_t y;
    float32_t z;
    float32_t w;
} quat_t;
static_assert(sizeof(quat_t) == 16);
static_assert(alignof(quat_t) == 16);
static_assert(offsetof(quat_t, x) == 0);
static_assert(offsetof(quat_t, y) == 4);
static_assert(offsetof(quat_t, z) == 8);
static_assert(offsetof(quat_t, w) == 12);

quat_t quat_identity(void);
quat_t quat_make(float32_t x, float32_t y, float32_t z, float32_t w);
/* Counterclockwise looking down the axis toward the origin, the same way as mat4_rotation. axis need not be unit length.
 * A zero axis gives the identity. */
quat_t quat_from_axis_angle(vec3_t axis, float32_t radians);
/* Takes the rotation part of m, which must be a pure rotation, and returns it as a unit quaternion. */
quat_t quat_from_mat4(const mat4_t *m);
/* a * b, so b is applied to a vector first. */
quat_t quat_multiply(quat_t a, quat_t b);
/* The inverse rotation, for a unit quaternion. */
quat_t quat_conjugate(quat_t q);
float32_t quat_dot(quat_t a, quat_t b);
/* The identity if q has no usable length. */
quat_t quat_normalize(quat_t q);
/* Rotates v. q must be unit length. */
vec3_t quat_rotate(quat_t q, vec3_t v);
/* q must be unit length. */
void quat_to_mat4(quat_t q, mat4_t *out);
/* Constant speed along the shorter way round, between unit quaternions. t = 0 gives a, and t = 1 gives b or, when that is
 * the shorter way, -b, which is the same rotation. Returns unit length. */
quat_t quat_slerp(quat_t a, quat_t b, float32_t t);

#endif
