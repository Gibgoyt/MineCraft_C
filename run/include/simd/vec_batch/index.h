/* Prototypes of asm/src/vec_batch/index.asm. See its README.md. Scalar references: simd/reference/index.h.
 *
 * Each result is exactly what the scalar function in math/mat4/index.h gives for that vector. out and in must be the same
 * memory or not overlap at all, and nothing past count is touched. m must be aligned to 32 and in and out to their own
 * types, as their types say. */
#ifndef MC_RUN_SIMD_VEC_BATCH_INDEX_H
#define MC_RUN_SIMD_VEC_BATCH_INDEX_H

#include "math/mat4/index.h"
#include "math/vec3/index.h"
#include "math/vec4/index.h"

#include <stdint.h>

/* out[i] = mat4_transform_point(m, in[i]). */
void vec_batch_transform_points(vec3_t *out, const vec3_t *in, uint64_t count, const mat4_t *m);

/* out[i] = mat4_transform_direction(m, in[i]). */
void vec_batch_transform_directions(vec3_t *out, const vec3_t *in, uint64_t count, const mat4_t *m);

/* out[i] = mat4_mul_vec4(m, in[i]). */
void vec_batch_mul_vec4(vec4_t *out, const vec4_t *in, uint64_t count, const mat4_t *m);

#endif
