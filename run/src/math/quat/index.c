/* See README.md. */
#include "math/quat/index.h"

#include "math/index.h"

#include <math.h>

/* Near 1, acosf loses precision: at this cosine, one rounding step in the cosine moves the angle by about 2e-6 radians.
 * A straight blend is off by about 1e-6 radians there, and by less above it, so above it the blend is the better of the two. */
#define QUAT_SLERP_LINEAR_ABOVE 0.9995f32

quat_t quat_identity(void) {
    return (quat_t){.x = 0.0f32, .y = 0.0f32, .z = 0.0f32, .w = 1.0f32};
}

quat_t quat_make(float32_t x, float32_t y, float32_t z, float32_t w) {
    return (quat_t){.x = x, .y = y, .z = z, .w = w};
}

quat_t quat_from_axis_angle(vec3_t axis, float32_t radians) {
    const vec3_t unit = vec3_normalize(axis);
    if (unit.x == 0.0f32 && unit.y == 0.0f32 && unit.z == 0.0f32) {
        return quat_identity();
    }
    const float32_t half = radians * 0.5f32;
    const float32_t s = sinf(half);
    return (quat_t){.x = unit.x * s, .y = unit.y * s, .z = unit.z * s, .w = cosf(half)};
}

/* Shepperd's method: divide by whichever of the four components is largest, since dividing by a small one loses the
 * precision. Entry r,c of the matrix is s[c * 4 + r]. */
quat_t quat_from_mat4(const mat4_t *m) {
    const float32_t *s = m->m;
    const float32_t m00 = s[0];
    const float32_t m11 = s[5];
    const float32_t m22 = s[10];
    const float32_t trace = m00 + m11 + m22;
    quat_t q;
    if (trace > 0.0f32) {
        const float32_t four_w = sqrtf(1.0f32 + trace) * 2.0f32;
        q = (quat_t){.x = (s[6] - s[9]) / four_w,
                     .y = (s[8] - s[2]) / four_w,
                     .z = (s[1] - s[4]) / four_w,
                     .w = 0.25f32 * four_w};
    } else if (m00 >= m11 && m00 >= m22) {
        const float32_t four_x = sqrtf(1.0f32 + m00 - m11 - m22) * 2.0f32;
        q = (quat_t){.x = 0.25f32 * four_x,
                     .y = (s[4] + s[1]) / four_x,
                     .z = (s[8] + s[2]) / four_x,
                     .w = (s[6] - s[9]) / four_x};
    } else if (m11 >= m22) {
        const float32_t four_y = sqrtf(1.0f32 + m11 - m00 - m22) * 2.0f32;
        q = (quat_t){.x = (s[4] + s[1]) / four_y,
                     .y = 0.25f32 * four_y,
                     .z = (s[9] + s[6]) / four_y,
                     .w = (s[8] - s[2]) / four_y};
    } else {
        const float32_t four_z = sqrtf(1.0f32 + m22 - m00 - m11) * 2.0f32;
        q = (quat_t){.x = (s[8] + s[2]) / four_z,
                     .y = (s[9] + s[6]) / four_z,
                     .z = 0.25f32 * four_z,
                     .w = (s[1] - s[4]) / four_z};
    }
    return quat_normalize(q);
}

quat_t quat_multiply(quat_t a, quat_t b) {
    return (quat_t){
        .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

quat_t quat_conjugate(quat_t q) {
    return (quat_t){.x = -q.x, .y = -q.y, .z = -q.z, .w = q.w};
}

float32_t quat_dot(quat_t a, quat_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

quat_t quat_normalize(quat_t q) {
    const float32_t length_squared = quat_dot(q, q);
    if (!(length_squared > 0.0f32 && length_squared < INFINITY)) {
        return quat_identity();
    }
    const float32_t scale = 1.0f32 / sqrtf(length_squared);
    return (quat_t){.x = q.x * scale, .y = q.y * scale, .z = q.z * scale, .w = q.w * scale};
}

/* v + 2 u x (u x v + w v), with u the vector part: q v q* multiplied out, at about half the cost of doing that. */
vec3_t quat_rotate(quat_t q, vec3_t v) {
    const vec3_t u = {.x = q.x, .y = q.y, .z = q.z};
    const vec3_t inner = vec3_add(vec3_cross(u, v), vec3_scale(v, q.w));
    return vec3_add(v, vec3_scale(vec3_cross(u, inner), 2.0f32));
}

void quat_to_mat4(quat_t q, mat4_t *out) {
    mat4_identity(out);
    const float32_t xx = q.x * q.x;
    const float32_t yy = q.y * q.y;
    const float32_t zz = q.z * q.z;
    const float32_t xy = q.x * q.y;
    const float32_t xz = q.x * q.z;
    const float32_t yz = q.y * q.z;
    const float32_t wx = q.w * q.x;
    const float32_t wy = q.w * q.y;
    const float32_t wz = q.w * q.z;
    out->m[0] = 1.0f32 - 2.0f32 * (yy + zz);
    out->m[1] = 2.0f32 * (xy + wz);
    out->m[2] = 2.0f32 * (xz - wy);
    out->m[4] = 2.0f32 * (xy - wz);
    out->m[5] = 1.0f32 - 2.0f32 * (xx + zz);
    out->m[6] = 2.0f32 * (yz + wx);
    out->m[8] = 2.0f32 * (xz + wy);
    out->m[9] = 2.0f32 * (yz - wx);
    out->m[10] = 1.0f32 - 2.0f32 * (xx + yy);
}

quat_t quat_slerp(quat_t a, quat_t b, float32_t t) {
    float32_t cosine = quat_dot(a, b);
    quat_t end = b;
    if (cosine < 0.0f32) {
        /* q and -q are the same rotation, and the negative one is the shorter way round. */
        end = (quat_t){.x = -b.x, .y = -b.y, .z = -b.z, .w = -b.w};
        cosine = -cosine;
    }
    float32_t from_a = 1.0f32 - t;
    float32_t from_end = t;
    if (cosine < QUAT_SLERP_LINEAR_ABOVE) {
        const float32_t angle = acosf(cosine);
        const float32_t inverse_sine = 1.0f32 / sinf(angle);
        from_a = sinf((1.0f32 - t) * angle) * inverse_sine;
        from_end = sinf(t * angle) * inverse_sine;
    }
    return quat_normalize((quat_t){
        .x = a.x * from_a + end.x * from_end,
        .y = a.y * from_a + end.y * from_end,
        .z = a.z * from_a + end.z * from_end,
        .w = a.w * from_a + end.w * from_end,
    });
}
