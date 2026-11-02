/* See run/src/math/vec2/README.md. */
#ifndef MC_RUN_MATH_VEC2_INDEX_H
#define MC_RUN_MATH_VEC2_INDEX_H

#include "types/index.h"

#include <stddef.h>

typedef struct vec2_t {
    float32_t x;
    float32_t y;
} vec2_t;
static_assert(sizeof(vec2_t) == 8);
static_assert(alignof(vec2_t) == 4);
static_assert(offsetof(vec2_t, x) == 0);
static_assert(offsetof(vec2_t, y) == 4);

vec2_t vec2_make(float32_t x, float32_t y);
vec2_t vec2_add(vec2_t a, vec2_t b);
vec2_t vec2_sub(vec2_t a, vec2_t b);
/* Component by component. */
vec2_t vec2_mul(vec2_t a, vec2_t b);
vec2_t vec2_scale(vec2_t v, float32_t factor);
vec2_t vec2_neg(vec2_t v);
float32_t vec2_dot(vec2_t a, vec2_t b);
float32_t vec2_length_squared(vec2_t v);
float32_t vec2_length(vec2_t v);
/* The zero vector if v has no usable length: zero, too small or too large for its square to fit, or not finite. */
vec2_t vec2_normalize(vec2_t v);
vec2_t vec2_lerp(vec2_t a, vec2_t b, float32_t t);
vec2_t vec2_min(vec2_t a, vec2_t b);
vec2_t vec2_max(vec2_t a, vec2_t b);

#endif
