/* See README.md. */
#include "math/vec3/index.h"

#include "math/index.h"

#include <math.h>

vec3_t vec3_make(float32_t x, float32_t y, float32_t z) {
    return (vec3_t){.x = x, .y = y, .z = z};
}

vec3_t vec3_splat(float32_t value) {
    return (vec3_t){.x = value, .y = value, .z = value};
}

vec3_t vec3_add(vec3_t a, vec3_t b) {
    return (vec3_t){.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

vec3_t vec3_sub(vec3_t a, vec3_t b) {
    return (vec3_t){.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

vec3_t vec3_mul(vec3_t a, vec3_t b) {
    return (vec3_t){.x = a.x * b.x, .y = a.y * b.y, .z = a.z * b.z};
}

vec3_t vec3_scale(vec3_t v, float32_t factor) {
    return (vec3_t){.x = v.x * factor, .y = v.y * factor, .z = v.z * factor};
}

vec3_t vec3_neg(vec3_t v) {
    return (vec3_t){.x = -v.x, .y = -v.y, .z = -v.z};
}

float32_t vec3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
    return (vec3_t){
        .x = a.y * b.z - a.z * b.y,
        .y = a.z * b.x - a.x * b.z,
        .z = a.x * b.y - a.y * b.x,
    };
}

float32_t vec3_length_squared(vec3_t v) {
    return vec3_dot(v, v);
}

float32_t vec3_length(vec3_t v) {
    return sqrtf(vec3_length_squared(v));
}

float32_t vec3_distance(vec3_t a, vec3_t b) {
    return vec3_length(vec3_sub(a, b));
}

vec3_t vec3_normalize(vec3_t v) {
    const float32_t length_squared = vec3_length_squared(v);
    if (!(length_squared > 0.0f32 && length_squared < INFINITY)) {
        return vec3_splat(0.0f32);
    }
    return vec3_scale(v, 1.0f32 / sqrtf(length_squared));
}

vec3_t vec3_lerp(vec3_t a, vec3_t b, float32_t t) {
    return (vec3_t){.x = math_lerp(a.x, b.x, t), .y = math_lerp(a.y, b.y, t), .z = math_lerp(a.z, b.z, t)};
}

vec3_t vec3_min(vec3_t a, vec3_t b) {
    return (vec3_t){.x = a.x < b.x ? a.x : b.x, .y = a.y < b.y ? a.y : b.y, .z = a.z < b.z ? a.z : b.z};
}

vec3_t vec3_max(vec3_t a, vec3_t b) {
    return (vec3_t){.x = a.x > b.x ? a.x : b.x, .y = a.y > b.y ? a.y : b.y, .z = a.z > b.z ? a.z : b.z};
}

vec3_t vec3_abs(vec3_t v) {
    return (vec3_t){.x = fabsf(v.x), .y = fabsf(v.y), .z = fabsf(v.z)};
}

vec3_t vec3_floor(vec3_t v) {
    return (vec3_t){.x = floorf(v.x), .y = floorf(v.y), .z = floorf(v.z)};
}
