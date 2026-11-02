/* See README.md. */
#include "math/ivec3/index.h"

#include "assert/index.h"

#include <math.h>

ivec3_t ivec3_make(int32_t x, int32_t y, int32_t z) {
    return (ivec3_t){.x = x, .y = y, .z = z};
}

ivec3_t ivec3_add(ivec3_t a, ivec3_t b) {
    return (ivec3_t){.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

ivec3_t ivec3_sub(ivec3_t a, ivec3_t b) {
    return (ivec3_t){.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

ivec3_t ivec3_scale(ivec3_t v, int32_t factor) {
    return (ivec3_t){.x = v.x * factor, .y = v.y * factor, .z = v.z * factor};
}

ivec3_t ivec3_neg(ivec3_t v) {
    return (ivec3_t){.x = -v.x, .y = -v.y, .z = -v.z};
}

bool ivec3_equal(ivec3_t a, ivec3_t b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

ivec3_t ivec3_min(ivec3_t a, ivec3_t b) {
    return (ivec3_t){.x = a.x < b.x ? a.x : b.x, .y = a.y < b.y ? a.y : b.y, .z = a.z < b.z ? a.z : b.z};
}

ivec3_t ivec3_max(ivec3_t a, ivec3_t b) {
    return (ivec3_t){.x = a.x > b.x ? a.x : b.x, .y = a.y > b.y ? a.y : b.y, .z = a.z > b.z ? a.z : b.z};
}

static int64_t ivec3_distance_on_axis(int32_t a, int32_t b) {
    const int64_t gap = (int64_t)a - (int64_t)b;
    return gap < 0 ? -gap : gap;
}

int64_t ivec3_manhattan(ivec3_t a, ivec3_t b) {
    return ivec3_distance_on_axis(a.x, b.x) + ivec3_distance_on_axis(a.y, b.y) + ivec3_distance_on_axis(a.z, b.z);
}

/* C's / rounds toward zero, so it comes out one too high when there is a remainder and the value is negative. */
static int32_t ivec3_floor_div_axis(int32_t value, int32_t divisor) {
    const int32_t quotient = value / divisor;
    return value % divisor < 0 ? quotient - 1 : quotient;
}

/* C's % takes the sign of the value. */
static int32_t ivec3_floor_mod_axis(int32_t value, int32_t divisor) {
    const int32_t remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

ivec3_t ivec3_floor_div(ivec3_t v, int32_t divisor) {
    ASSERT(divisor > 0);
    return (ivec3_t){
        .x = ivec3_floor_div_axis(v.x, divisor),
        .y = ivec3_floor_div_axis(v.y, divisor),
        .z = ivec3_floor_div_axis(v.z, divisor),
    };
}

ivec3_t ivec3_floor_mod(ivec3_t v, int32_t divisor) {
    ASSERT(divisor > 0);
    return (ivec3_t){
        .x = ivec3_floor_mod_axis(v.x, divisor),
        .y = ivec3_floor_mod_axis(v.y, divisor),
        .z = ivec3_floor_mod_axis(v.z, divisor),
    };
}

/* Converting a float that does not fit is undefined behaviour in C, hence the check. Both bounds are exact in float32_t,
 * and the comparison is false for NaN. */
static int32_t ivec3_floor_axis(float32_t value) {
    const float32_t floored = floorf(value);
    ASSERT(floored >= -0x1.0p31f32 && floored < 0x1.0p31f32);
    return (int32_t)floored;
}

ivec3_t ivec3_from_vec3_floor(vec3_t v) {
    return (ivec3_t){.x = ivec3_floor_axis(v.x), .y = ivec3_floor_axis(v.y), .z = ivec3_floor_axis(v.z)};
}

vec3_t ivec3_to_vec3(ivec3_t v) {
    return (vec3_t){.x = (float32_t)v.x, .y = (float32_t)v.y, .z = (float32_t)v.z};
}
