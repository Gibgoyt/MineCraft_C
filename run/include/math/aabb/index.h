/* See run/src/math/aabb/README.md. */
#ifndef MC_RUN_MATH_AABB_INDEX_H
#define MC_RUN_MATH_AABB_INDEX_H

#include "math/vec3/index.h"
#include "types/index.h"

#include <stddef.h>

/* An axis-aligned box. It is valid when min <= max on every axis, and every function below needs a valid box, except
 * aabb_is_valid and what aabb_intersection returns. */
typedef struct aabb_t {
    vec3_t min;
    vec3_t max;
} aabb_t;
static_assert(sizeof(aabb_t) == 24);
static_assert(alignof(aabb_t) == 4);
static_assert(offsetof(aabb_t, min) == 0);
static_assert(offsetof(aabb_t, max) == 12);

aabb_t aabb_make(vec3_t min, vec3_t max);
/* The box with these two opposite corners, in any order. */
aabb_t aabb_from_corners(vec3_t a, vec3_t b);
aabb_t aabb_from_center(vec3_t center, vec3_t half_size);
bool aabb_is_valid(aabb_t box);
vec3_t aabb_center(aabb_t box);
vec3_t aabb_half_size(aabb_t box);
vec3_t aabb_size(aabb_t box);
float32_t aabb_volume(aabb_t box);
/* min <= point < max on every axis, so a point on the wall between two touching boxes is in exactly one of them. */
bool aabb_contains_point(aabb_t box, vec3_t point);
/* Whether the boxes share any volume. Two that only touch do not, so a player standing on a block is not inside it. */
bool aabb_overlaps(aabb_t a, aabb_t b);
aabb_t aabb_translate(aabb_t box, vec3_t offset);
/* Grows each side by margin on that axis, so each axis of the box's size grows by twice that. A negative margin may
 * leave the box invalid. */
aabb_t aabb_expand(aabb_t box, vec3_t margin);
/* The smallest box holding both. */
aabb_t aabb_union(aabb_t a, aabb_t b);
/* The box both share, which is invalid if they do not meet. Check with aabb_is_valid. */
aabb_t aabb_intersection(aabb_t a, aabb_t b);
/* The nearest point to it in or on the box. */
vec3_t aabb_closest_point(aabb_t box, vec3_t point);

#endif
