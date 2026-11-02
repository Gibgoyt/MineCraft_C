/* See README.md. */
#include "math/vec2/index.h"

#include "math/index.h"

#include <math.h>

vec2_t vec2_make(float32_t x, float32_t y) {
    return (vec2_t){.x = x, .y = y};
}

vec2_t vec2_add(vec2_t a, vec2_t b) {
    return (vec2_t){.x = a.x + b.x, .y = a.y + b.y};
}

vec2_t vec2_sub(vec2_t a, vec2_t b) {
    return (vec2_t){.x = a.x - b.x, .y = a.y - b.y};
}

vec2_t vec2_mul(vec2_t a, vec2_t b) {
    return (vec2_t){.x = a.x * b.x, .y = a.y * b.y};
}

vec2_t vec2_scale(vec2_t v, float32_t factor) {
    return (vec2_t){.x = v.x * factor, .y = v.y * factor};
}

vec2_t vec2_neg(vec2_t v) {
    return (vec2_t){.x = -v.x, .y = -v.y};
}

float32_t vec2_dot(vec2_t a, vec2_t b) {
    return a.x * b.x + a.y * b.y;
}

float32_t vec2_length_squared(vec2_t v) {
    return vec2_dot(v, v);
}

float32_t vec2_length(vec2_t v) {
    return sqrtf(vec2_length_squared(v));
}

vec2_t vec2_normalize(vec2_t v) {
    const float32_t length_squared = vec2_length_squared(v);
    if (!(length_squared > 0.0f32 && length_squared < INFINITY)) {
        return (vec2_t){.x = 0.0f32, .y = 0.0f32};
    }
    return vec2_scale(v, 1.0f32 / sqrtf(length_squared));
}

vec2_t vec2_lerp(vec2_t a, vec2_t b, float32_t t) {
    return (vec2_t){.x = math_lerp(a.x, b.x, t), .y = math_lerp(a.y, b.y, t)};
}

vec2_t vec2_min(vec2_t a, vec2_t b) {
    return (vec2_t){.x = a.x < b.x ? a.x : b.x, .y = a.y < b.y ? a.y : b.y};
}

vec2_t vec2_max(vec2_t a, vec2_t b) {
    return (vec2_t){.x = a.x > b.x ? a.x : b.x, .y = a.y > b.y ? a.y : b.y};
}
