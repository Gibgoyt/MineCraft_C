/* See run/src/math/ivec3/README.md. */
#ifndef MC_RUN_MATH_IVEC3_INDEX_H
#define MC_RUN_MATH_IVEC3_INDEX_H

#include "math/vec3/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* Block and section coordinates. Overflowing int32_t is the caller's bug, as with any int32_t. */
typedef struct ivec3_t {
    int32_t x;
    int32_t y;
    int32_t z;
} ivec3_t;
static_assert(sizeof(ivec3_t) == 12);
static_assert(alignof(ivec3_t) == 4);
static_assert(offsetof(ivec3_t, x) == 0);
static_assert(offsetof(ivec3_t, y) == 4);
static_assert(offsetof(ivec3_t, z) == 8);

ivec3_t ivec3_make(int32_t x, int32_t y, int32_t z);
ivec3_t ivec3_add(ivec3_t a, ivec3_t b);
ivec3_t ivec3_sub(ivec3_t a, ivec3_t b);
ivec3_t ivec3_scale(ivec3_t v, int32_t factor);
ivec3_t ivec3_neg(ivec3_t v);
bool ivec3_equal(ivec3_t a, ivec3_t b);
ivec3_t ivec3_min(ivec3_t a, ivec3_t b);
ivec3_t ivec3_max(ivec3_t a, ivec3_t b);
/* 64 bits, since three int32_t magnitudes do not fit in 32. */
int64_t ivec3_manhattan(ivec3_t a, ivec3_t b);
/* Which section holds a block: rounds toward negative infinity on each axis, unlike C's /, so block -1 in sections of
 * 16 is in section -1. divisor must be above zero. */
ivec3_t ivec3_floor_div(ivec3_t v, int32_t divisor);
/* Where in that section: 0 to divisor - 1 on each axis, unlike C's %. divisor must be above zero. Always
 * ivec3_floor_div(v, d) * d + ivec3_floor_mod(v, d) == v. */
ivec3_t ivec3_floor_mod(ivec3_t v, int32_t divisor);
/* The block a position is in: rounds toward negative infinity. Each floored component must fit in int32_t, and not be
 * NaN. */
ivec3_t ivec3_from_vec3_floor(vec3_t v);
/* Large values round to the nearest float32_t: above 2^24 not every int32_t has one. */
vec3_t ivec3_to_vec3(ivec3_t v);

#endif
