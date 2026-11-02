/* See README.md. */
#include "math/vec4/index.h"

#include "math/index.h"

#include <math.h>

vec4_t vec4_make(float32_t x, float32_t y, float32_t z, float32_t w) {
    return (vec4_t){.x = x, .y = y, .z = z, .w = w};
}

vec4_t vec4_from_vec3(vec3_t v, float32_t w) {
    return (vec4_t){.x = v.x, .y = v.y, .z = v.z, .w = w};
}

vec3_t vec4_to_vec3(vec4_t v) {
    return (vec3_t){.x = v.x, .y = v.y, .z = v.z};
}

vec4_t vec4_add(vec4_t a, vec4_t b) {
    return (vec4_t){.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z, .w = a.w + b.w};
}

vec4_t vec4_sub(vec4_t a, vec4_t b) {
    return (vec4_t){.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z, .w = a.w - b.w};
}

vec4_t vec4_mul(vec4_t a, vec4_t b) {
    return (vec4_t){.x = a.x * b.x, .y = a.y * b.y, .z = a.z * b.z, .w = a.w * b.w};
}

vec4_t vec4_scale(vec4_t v, float32_t factor) {
    return (vec4_t){.x = v.x * factor, .y = v.y * factor, .z = v.z * factor, .w = v.w * factor};
}

vec4_t vec4_neg(vec4_t v) {
    return (vec4_t){.x = -v.x, .y = -v.y, .z = -v.z, .w = -v.w};
}

float32_t vec4_dot(vec4_t a, vec4_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

float32_t vec4_length_squared(vec4_t v) {
    return vec4_dot(v, v);
}

float32_t vec4_length(vec4_t v) {
    return sqrtf(vec4_length_squared(v));
}

vec4_t vec4_normalize(vec4_t v) {
    const float32_t length_squared = vec4_length_squared(v);
    if (!(length_squared > 0.0f32 && length_squared < INFINITY)) {
        return (vec4_t){.x = 0.0f32, .y = 0.0f32, .z = 0.0f32, .w = 0.0f32};
    }
    return vec4_scale(v, 1.0f32 / sqrtf(length_squared));
}

vec4_t vec4_lerp(vec4_t a, vec4_t b, float32_t t) {
    return (vec4_t){
        .x = math_lerp(a.x, b.x, t),
        .y = math_lerp(a.y, b.y, t),
        .z = math_lerp(a.z, b.z, t),
        .w = math_lerp(a.w, b.w, t),
    };
}
