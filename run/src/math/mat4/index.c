/* See README.md. */
#include "math/mat4/index.h"

#include "assert/index.h"
#include "math/index.h"

#include <math.h>
#include <string.h>

void mat4_identity(mat4_t *out) {
    memset(out, 0, sizeof(*out));
    out->m[0] = 1.0f32;
    out->m[5] = 1.0f32;
    out->m[10] = 1.0f32;
    out->m[15] = 1.0f32;
}

void mat4_multiply(const mat4_t *a, const mat4_t *b, mat4_t *out) {
    mat4_t result;
    for (uint32_t column = 0; column < 4u; column++) {
        const float32_t *b_column = &b->m[column * 4u];
        for (uint32_t row = 0; row < 4u; row++) {
            result.m[column * 4u + row] = a->m[row] * b_column[0] + a->m[4u + row] * b_column[1] +
                                          a->m[8u + row] * b_column[2] + a->m[12u + row] * b_column[3];
        }
    }
    *out = result;
}

void mat4_transpose(const mat4_t *m, mat4_t *out) {
    mat4_t result;
    for (uint32_t column = 0; column < 4u; column++) {
        for (uint32_t row = 0; row < 4u; row++) {
            result.m[column * 4u + row] = m->m[row * 4u + column];
        }
    }
    *out = result;
}

/* The six 2x2 minors of the top two rows and of the bottom two. The determinant and the inverse are both built from these,
 * which costs far less than expanding each cofactor on its own. The source is column-major, so entry r,c is s[c * 4 + r]. */
typedef struct mat4_minors_t {
    float32_t top[6];
    float32_t bottom[6];
} mat4_minors_t;
static_assert(sizeof(mat4_minors_t) == 48);

static void mat4_minors(const float32_t *s, mat4_minors_t *out) {
    out->top[0] = s[0] * s[5] - s[4] * s[1];
    out->top[1] = s[0] * s[9] - s[8] * s[1];
    out->top[2] = s[0] * s[13] - s[12] * s[1];
    out->top[3] = s[4] * s[9] - s[8] * s[5];
    out->top[4] = s[4] * s[13] - s[12] * s[5];
    out->top[5] = s[8] * s[13] - s[12] * s[9];
    out->bottom[0] = s[2] * s[7] - s[6] * s[3];
    out->bottom[1] = s[2] * s[11] - s[10] * s[3];
    out->bottom[2] = s[2] * s[15] - s[14] * s[3];
    out->bottom[3] = s[6] * s[11] - s[10] * s[7];
    out->bottom[4] = s[6] * s[15] - s[14] * s[7];
    out->bottom[5] = s[10] * s[15] - s[14] * s[11];
}

/* Laplace expansion along the top two rows. */
static float32_t mat4_determinant_of_minors(const mat4_minors_t *minors) {
    const float32_t *a = minors->top;
    const float32_t *b = minors->bottom;
    return a[0] * b[5] - a[1] * b[4] + a[2] * b[3] + a[3] * b[2] - a[4] * b[1] + a[5] * b[0];
}

float32_t mat4_determinant(const mat4_t *m) {
    mat4_minors_t minors;
    mat4_minors(m->m, &minors);
    return mat4_determinant_of_minors(&minors);
}

bool mat4_inverse(const mat4_t *m, mat4_t *out) {
    mat4_minors_t minors;
    mat4_minors(m->m, &minors);
    const float32_t determinant = mat4_determinant_of_minors(&minors);
    if (determinant == 0.0f32 || !isfinite(determinant)) {
        return false;
    }
    const float32_t *s = m->m;
    const float32_t *a = minors.top;
    const float32_t *b = minors.bottom;
    const float32_t scale = 1.0f32 / determinant;
    /* The adjugate, in column-major order: each entry is the signed cofactor of the mirrored position. */
    const mat4_t result = {.m = {
                               (s[5] * b[5] - s[9] * b[4] + s[13] * b[3]) * scale,
                               (-s[1] * b[5] + s[9] * b[2] - s[13] * b[1]) * scale,
                               (s[1] * b[4] - s[5] * b[2] + s[13] * b[0]) * scale,
                               (-s[1] * b[3] + s[5] * b[1] - s[9] * b[0]) * scale,

                               (-s[4] * b[5] + s[8] * b[4] - s[12] * b[3]) * scale,
                               (s[0] * b[5] - s[8] * b[2] + s[12] * b[1]) * scale,
                               (-s[0] * b[4] + s[4] * b[2] - s[12] * b[0]) * scale,
                               (s[0] * b[3] - s[4] * b[1] + s[8] * b[0]) * scale,

                               (s[7] * a[5] - s[11] * a[4] + s[15] * a[3]) * scale,
                               (-s[3] * a[5] + s[11] * a[2] - s[15] * a[1]) * scale,
                               (s[3] * a[4] - s[7] * a[2] + s[15] * a[0]) * scale,
                               (-s[3] * a[3] + s[7] * a[1] - s[11] * a[0]) * scale,

                               (-s[6] * a[5] + s[10] * a[4] - s[14] * a[3]) * scale,
                               (s[2] * a[5] - s[10] * a[2] + s[14] * a[1]) * scale,
                               (-s[2] * a[4] + s[6] * a[2] - s[14] * a[0]) * scale,
                               (s[2] * a[3] - s[6] * a[1] + s[10] * a[0]) * scale,
                           }};
    *out = result;
    return true;
}

vec4_t mat4_mul_vec4(const mat4_t *m, vec4_t v) {
    const float32_t *s = m->m;
    return (vec4_t){
        .x = s[0] * v.x + s[4] * v.y + s[8] * v.z + s[12] * v.w,
        .y = s[1] * v.x + s[5] * v.y + s[9] * v.z + s[13] * v.w,
        .z = s[2] * v.x + s[6] * v.y + s[10] * v.z + s[14] * v.w,
        .w = s[3] * v.x + s[7] * v.y + s[11] * v.z + s[15] * v.w,
    };
}

vec3_t mat4_transform_point(const mat4_t *m, vec3_t point) {
    const float32_t *s = m->m;
    return (vec3_t){
        .x = s[0] * point.x + s[4] * point.y + s[8] * point.z + s[12],
        .y = s[1] * point.x + s[5] * point.y + s[9] * point.z + s[13],
        .z = s[2] * point.x + s[6] * point.y + s[10] * point.z + s[14],
    };
}

vec3_t mat4_transform_direction(const mat4_t *m, vec3_t direction) {
    const float32_t *s = m->m;
    return (vec3_t){
        .x = s[0] * direction.x + s[4] * direction.y + s[8] * direction.z,
        .y = s[1] * direction.x + s[5] * direction.y + s[9] * direction.z,
        .z = s[2] * direction.x + s[6] * direction.y + s[10] * direction.z,
    };
}

bool mat4_project_point(const mat4_t *m, vec3_t point, vec3_t *out) {
    const vec4_t clip = mat4_mul_vec4(m, vec4_from_vec3(point, 1.0f32));
    if (clip.w == 0.0f32) {
        return false;
    }
    *out = vec3_scale(vec4_to_vec3(clip), 1.0f32 / clip.w);
    return true;
}

void mat4_translation(vec3_t offset, mat4_t *out) {
    mat4_identity(out);
    out->m[12] = offset.x;
    out->m[13] = offset.y;
    out->m[14] = offset.z;
}

void mat4_scaling(vec3_t factors, mat4_t *out) {
    mat4_identity(out);
    out->m[0] = factors.x;
    out->m[5] = factors.y;
    out->m[10] = factors.z;
}

/* Rodrigues' formula, written out as a matrix. */
void mat4_rotation(vec3_t axis, float32_t radians, mat4_t *out) {
    mat4_identity(out);
    const vec3_t u = vec3_normalize(axis);
    if (u.x == 0.0f32 && u.y == 0.0f32 && u.z == 0.0f32) {
        return;
    }
    const float32_t c = cosf(radians);
    const float32_t s = sinf(radians);
    const float32_t k = 1.0f32 - c;
    out->m[0] = c + u.x * u.x * k;
    out->m[1] = u.y * u.x * k + u.z * s;
    out->m[2] = u.z * u.x * k - u.y * s;
    out->m[4] = u.x * u.y * k - u.z * s;
    out->m[5] = c + u.y * u.y * k;
    out->m[6] = u.z * u.y * k + u.x * s;
    out->m[8] = u.x * u.z * k + u.y * s;
    out->m[9] = u.y * u.z * k - u.x * s;
    out->m[10] = c + u.z * u.z * k;
}

bool mat4_look_at(vec3_t eye, vec3_t target, vec3_t up, mat4_t *out) {
    mat4_identity(out);
    const vec3_t forward = vec3_normalize(vec3_sub(target, eye));
    const vec3_t right = vec3_normalize(vec3_cross(forward, up));
    if (vec3_length_squared(forward) == 0.0f32 || vec3_length_squared(right) == 0.0f32) {
        return false;
    }
    const vec3_t camera_up = vec3_cross(right, forward);
    out->m[0] = right.x;
    out->m[4] = right.y;
    out->m[8] = right.z;
    out->m[12] = -vec3_dot(right, eye);
    out->m[1] = camera_up.x;
    out->m[5] = camera_up.y;
    out->m[9] = camera_up.z;
    out->m[13] = -vec3_dot(camera_up, eye);
    out->m[2] = -forward.x;
    out->m[6] = -forward.y;
    out->m[10] = -forward.z;
    out->m[14] = vec3_dot(forward, eye);
    return true;
}

void mat4_perspective(float32_t fov_y_radians, float32_t aspect, float32_t near_plane, float32_t far_plane,
                      mat4_t *out) {
    ASSERT(fov_y_radians > 0.0f32 && fov_y_radians < MATH_PI);
    ASSERT(aspect > 0.0f32);
    ASSERT(near_plane > 0.0f32 && near_plane < far_plane);
    memset(out, 0, sizeof(*out));
    const float32_t focal = 1.0f32 / tanf(fov_y_radians * 0.5f32);
    const float32_t depth_scale = far_plane / (near_plane - far_plane);
    out->m[0] = focal / aspect;
    out->m[5] = focal;
    out->m[10] = depth_scale;
    out->m[11] = -1.0f32;
    out->m[14] = near_plane * depth_scale;
}
