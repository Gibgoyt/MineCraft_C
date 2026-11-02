/* See run/src/math/vec3/README.md. */
#ifndef MC_RUN_MATH_VEC3_INDEX_H
#define MC_RUN_MATH_VEC3_INDEX_H

#include "types/index.h"

#include <stddef.h>

/* 12 bytes with no padding, so an array of these has no holes. An aligned SIMD load of one is not possible: use vec4_t
 * or an array of each component for that. */
typedef struct vec3_t {
    float32_t x;
    float32_t y;
    float32_t z;
} vec3_t;
static_assert(sizeof(vec3_t) == 12);
static_assert(alignof(vec3_t) == 4);
static_assert(offsetof(vec3_t, x) == 0);
static_assert(offsetof(vec3_t, y) == 4);
static_assert(offsetof(vec3_t, z) == 8);

vec3_t vec3_make(float32_t x, float32_t y, float32_t z);
vec3_t vec3_splat(float32_t value);
vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_sub(vec3_t a, vec3_t b);
/* Component by component. */
vec3_t vec3_mul(vec3_t a, vec3_t b);
vec3_t vec3_scale(vec3_t v, float32_t factor);
vec3_t vec3_neg(vec3_t v);
float32_t vec3_dot(vec3_t a, vec3_t b);
/* Right-handed: x cross y is z. */
vec3_t vec3_cross(vec3_t a, vec3_t b);
float32_t vec3_length_squared(vec3_t v);
float32_t vec3_length(vec3_t v);
float32_t vec3_distance(vec3_t a, vec3_t b);
/* The zero vector if v has no usable length: zero, too small or too large for its square to fit, or not finite. */
vec3_t vec3_normalize(vec3_t v);
vec3_t vec3_lerp(vec3_t a, vec3_t b, float32_t t);
vec3_t vec3_min(vec3_t a, vec3_t b);
vec3_t vec3_max(vec3_t a, vec3_t b);
vec3_t vec3_abs(vec3_t v);
/* Toward negative infinity on each axis: -0.5 gives -1. */
vec3_t vec3_floor(vec3_t v);

#endif
