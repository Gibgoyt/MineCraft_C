/* See run/src/math/vec4/README.md. */
#ifndef MC_RUN_MATH_VEC4_INDEX_H
#define MC_RUN_MATH_VEC4_INDEX_H

#include "math/vec3/index.h"
#include "types/index.h"

#include <stddef.h>

/* Aligned to 16, so that NASM may load one with vmovaps. A pointer to one must therefore be aligned to 16 as well. */
typedef struct vec4_t {
    alignas(16) float32_t x;
    float32_t y;
    float32_t z;
    float32_t w;
} vec4_t;
static_assert(sizeof(vec4_t) == 16);
static_assert(alignof(vec4_t) == 16);
static_assert(offsetof(vec4_t, x) == 0);
static_assert(offsetof(vec4_t, y) == 4);
static_assert(offsetof(vec4_t, z) == 8);
static_assert(offsetof(vec4_t, w) == 12);

vec4_t vec4_make(float32_t x, float32_t y, float32_t z, float32_t w);
vec4_t vec4_from_vec3(vec3_t v, float32_t w);
/* Drops w without dividing by it. */
vec3_t vec4_to_vec3(vec4_t v);
vec4_t vec4_add(vec4_t a, vec4_t b);
vec4_t vec4_sub(vec4_t a, vec4_t b);
/* Component by component. */
vec4_t vec4_mul(vec4_t a, vec4_t b);
vec4_t vec4_scale(vec4_t v, float32_t factor);
vec4_t vec4_neg(vec4_t v);
float32_t vec4_dot(vec4_t a, vec4_t b);
float32_t vec4_length_squared(vec4_t v);
float32_t vec4_length(vec4_t v);
/* The zero vector if v has no usable length: zero, too small or too large for its square to fit, or not finite. */
vec4_t vec4_normalize(vec4_t v);
vec4_t vec4_lerp(vec4_t a, vec4_t b, float32_t t);

#endif
