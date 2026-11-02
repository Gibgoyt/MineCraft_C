/* Must compile under nvcc, host and device. F32 and F64 in device arithmetic, which is where an f32 literal crashes the
 * compiler (Stage 01.1 issue 2), and the sized types in a kernel signature, where _Float32 is an error (issue 1). */
#include "types/index.h"

extern "C" __global__ void fixture_kernel(float32_t *out_single, float64_t *out_twin, float32_t scale) {
    out_single[0] = (F32(1.5) + scale) * F32(0.5);
    out_twin[0] = (F64(2.25) + (float64_t)scale) * F64(0.5);
}
