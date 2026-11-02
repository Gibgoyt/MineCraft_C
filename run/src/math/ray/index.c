/* See README.md. */
#include "math/ray/index.h"

ray_t ray_make(vec3_t origin, vec3_t direction) {
    return (ray_t){.origin = origin, .direction = direction};
}

ray_t ray_from_points(vec3_t from, vec3_t to) {
    return (ray_t){.origin = from, .direction = vec3_normalize(vec3_sub(to, from))};
}

vec3_t ray_at(ray_t ray, float32_t t) {
    return vec3_add(ray.origin, vec3_scale(ray.direction, t));
}

bool ray_intersect_aabb(ray_t ray, aabb_t box, float32_t t_min, float32_t t_max, ray_aabb_hit_t *out) {
    const float32_t origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float32_t direction[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float32_t low[3] = {box.min.x, box.min.y, box.min.z};
    const float32_t high[3] = {box.max.x, box.max.y, box.max.z};
    ray_aabb_hit_t hit = {.t_enter = t_min, .t_exit = t_max, .axis = RAY_AXIS_NONE, .normal_sign = 0, ._pad0 = {0, 0}};
    for (uint8_t axis = 0; axis < 3u; axis++) {
        if (direction[axis] == 0.0f32) {
            /* Parallel to this slab, and dividing would give 0 / 0 for an origin on its wall. Written so that NaN misses. */
            if (!(origin[axis] >= low[axis] && origin[axis] <= high[axis])) {
                return false;
            }
            continue;
        }
        const bool positive = direction[axis] > 0.0f32;
        const float32_t t_to_low = (low[axis] - origin[axis]) / direction[axis];
        const float32_t t_to_high = (high[axis] - origin[axis]) / direction[axis];
        const float32_t t_in = positive ? t_to_low : t_to_high;
        const float32_t t_out = positive ? t_to_high : t_to_low;
        /* Also false for NaN, which would slip past both comparisons after it. */
        if (!(t_in <= t_out)) {
            return false;
        }
        if (t_in > hit.t_enter) {
            hit.t_enter = t_in;
            hit.axis = axis;
            hit.normal_sign = positive ? -1 : 1;
        }
        if (t_out < hit.t_exit) {
            hit.t_exit = t_out;
        }
    }
    if (!(hit.t_enter <= hit.t_exit)) {
        return false;
    }
    *out = hit;
    return true;
}

bool ray_from_ndc(const mat4_t *inverse_view_projection, float32_t ndc_x, float32_t ndc_y, ray_t *out) {
    vec3_t on_near;
    vec3_t on_far;
    if (!mat4_project_point(inverse_view_projection, vec3_make(ndc_x, ndc_y, 0.0f32), &on_near) ||
        !mat4_project_point(inverse_view_projection, vec3_make(ndc_x, ndc_y, 1.0f32), &on_far)) {
        return false;
    }
    *out = ray_from_points(on_near, on_far);
    return true;
}
