/* See README.md. */
#include "math/index.h"

#include "assert/index.h"
#include "log/index.h"

#include <fenv.h>
#include <string.h>

/* volatile so that this runs on the CPU, under the mode being checked, and is not worked out by the compiler. The first
 * product has a denormal result, which flush-to-zero makes 0. The second has a denormal operand, which denormals-are-zero
 * makes 0. */
static bool math_denormals_work(void) {
    volatile float32_t smallest_normal = 0x1.0p-126f32;
    volatile float32_t denormal = 0x1.0p-130f32;
    const float32_t made = smallest_normal * 0.5f32;
    const float32_t read = denormal * 1024.0f32;
    return made == 0x1.0p-127f32 && read == 0x1.0p-120f32;
}

bool math_init(math_t *self) {
    memset(self, 0, sizeof(*self));
    if (fegetround() != FE_TONEAREST) {
        LOG_ERROR("math: the rounding mode is not round to nearest, so the same seed would not give the same world");
        return false;
    }
    if (!math_denormals_work()) {
        LOG_ERROR("math: denormals are being flushed to zero, so the same seed would not give the same world");
        return false;
    }
    self->ready = 1;
    return true;
}

void math_deinit(math_t *self) {
    memset(self, 0, sizeof(*self));
}

float32_t math_radians(float32_t degrees) {
    return degrees * (MATH_PI / 180.0f32);
}

float32_t math_clamp(float32_t value, float32_t low, float32_t high) {
    ASSERT(low <= high);
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

float32_t math_lerp(float32_t a, float32_t b, float32_t t) {
    return a * (1.0f32 - t) + b * t;
}
