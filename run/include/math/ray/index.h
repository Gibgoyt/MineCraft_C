/* See run/src/math/ray/README.md. */
#ifndef MC_RUN_MATH_RAY_INDEX_H
#define MC_RUN_MATH_RAY_INDEX_H

#include "math/aabb/index.h"
#include "math/mat4/index.h"
#include "math/vec3/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

/* The points origin + t * direction. A t is in units of direction's length, so it is a distance only if direction has
 * unit length. Nothing here needs it to. */
typedef struct ray_t {
    vec3_t origin;
    vec3_t direction;
} ray_t;
static_assert(sizeof(ray_t) == 24);
static_assert(alignof(ray_t) == 4);
static_assert(offsetof(ray_t, origin) == 0);
static_assert(offsetof(ray_t, direction) == 12);

/* Means the ray starts inside the box or at t_min, so it came in through no face. */
enum : uint8_t { RAY_AXIS_NONE = 3 };

/* Where a ray meets a box. */
typedef struct ray_aabb_hit_t {
    float32_t t_enter;  /* first t in the box, and never below the t_min asked for */
    float32_t t_exit;   /* last t in the box, and never above the t_max asked for */
    uint8_t axis;       /* the axis of the face it came in through, 0 x, 1 y, 2 z, or RAY_AXIS_NONE */
    int8_t normal_sign; /* which side of that axis the face looks to, -1 or 1; 0 with RAY_AXIS_NONE */
    uint8_t _pad0[2];
} ray_aabb_hit_t;
static_assert(sizeof(ray_aabb_hit_t) == 12);
static_assert(alignof(ray_aabb_hit_t) == 4);
static_assert(offsetof(ray_aabb_hit_t, t_enter) == 0);
static_assert(offsetof(ray_aabb_hit_t, t_exit) == 4);
static_assert(offsetof(ray_aabb_hit_t, axis) == 8);
static_assert(offsetof(ray_aabb_hit_t, normal_sign) == 9);

ray_t ray_make(vec3_t origin, vec3_t direction);
/* From one point toward another, with a unit direction, so t is a distance. A zero direction if they are the same. */
ray_t ray_from_points(vec3_t from, vec3_t to);
vec3_t ray_at(ray_t ray, float32_t t);
/* The slab test. True if some t from t_min to t_max is in the box, including a ray that only grazes it, and out then says
 * where. False leaves out untouched. A NaN anywhere in the ray is a miss, and so is a ray with a zero direction that
 * starts outside the box. A ray with a zero direction starting inside hits, from t_min to t_max. */
bool ray_intersect_aabb(ray_t ray, aabb_t box, float32_t t_min, float32_t t_max, ray_aabb_hit_t *out);
/* The camera ray through a screen position. ndc_x and ndc_y run -1 to 1 with y up, and inverse_view_projection is the
 * inverse of projection * view, with mat4_perspective's depth of 0 at near and 1 at far. The ray starts on the near plane
 * and has a unit direction. False, with out untouched, if the matrix cannot give those two points. */
bool ray_from_ndc(const mat4_t *inverse_view_projection, float32_t ndc_x, float32_t ndc_y, ray_t *out);

#endif
