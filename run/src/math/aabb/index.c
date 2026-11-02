/* See README.md. */
#include "math/aabb/index.h"

aabb_t aabb_make(vec3_t min, vec3_t max) {
    return (aabb_t){.min = min, .max = max};
}

aabb_t aabb_from_corners(vec3_t a, vec3_t b) {
    return (aabb_t){.min = vec3_min(a, b), .max = vec3_max(a, b)};
}

aabb_t aabb_from_center(vec3_t center, vec3_t half_size) {
    return (aabb_t){.min = vec3_sub(center, half_size), .max = vec3_add(center, half_size)};
}

bool aabb_is_valid(aabb_t box) {
    return box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z;
}

vec3_t aabb_center(aabb_t box) {
    return vec3_scale(vec3_add(box.min, box.max), 0.5f32);
}

vec3_t aabb_half_size(aabb_t box) {
    return vec3_scale(vec3_sub(box.max, box.min), 0.5f32);
}

vec3_t aabb_size(aabb_t box) {
    return vec3_sub(box.max, box.min);
}

float32_t aabb_volume(aabb_t box) {
    const vec3_t size = aabb_size(box);
    return size.x * size.y * size.z;
}

bool aabb_contains_point(aabb_t box, vec3_t point) {
    return point.x >= box.min.x && point.x < box.max.x && point.y >= box.min.y && point.y < box.max.y &&
           point.z >= box.min.z && point.z < box.max.z;
}

bool aabb_overlaps(aabb_t a, aabb_t b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y && a.min.z < b.max.z &&
           b.min.z < a.max.z;
}

aabb_t aabb_translate(aabb_t box, vec3_t offset) {
    return (aabb_t){.min = vec3_add(box.min, offset), .max = vec3_add(box.max, offset)};
}

aabb_t aabb_expand(aabb_t box, vec3_t margin) {
    return (aabb_t){.min = vec3_sub(box.min, margin), .max = vec3_add(box.max, margin)};
}

aabb_t aabb_union(aabb_t a, aabb_t b) {
    return (aabb_t){.min = vec3_min(a.min, b.min), .max = vec3_max(a.max, b.max)};
}

aabb_t aabb_intersection(aabb_t a, aabb_t b) {
    return (aabb_t){.min = vec3_max(a.min, b.min), .max = vec3_min(a.max, b.max)};
}

vec3_t aabb_closest_point(aabb_t box, vec3_t point) {
    return vec3_min(vec3_max(point, box.min), box.max);
}
