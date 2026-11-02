/* See run/src/math/README.md. */
#ifndef MC_RUN_MATH_INDEX_H
#define MC_RUN_MATH_INDEX_H

#include "types/index.h"

#include <stdint.h>

#define MATH_PI F32(3.14159265358979323846)

/* The Owner of the vector library. Its parts allocate nothing, so this holds no memory: see the README. */
typedef struct math_t {
    uint8_t ready; /* math_init succeeded and math_deinit has not run */
    uint8_t _pad0[7];
} math_t;
static_assert(sizeof(math_t) == 8);
static_assert(alignof(math_t) == 1);

/* Checks that this thread's floating-point environment is the one "same seed, same world" needs: round to nearest,
 * denormals neither flushed nor read as zero. Says what is wrong and returns false if not. Call it before starting any
 * thread: a new thread inherits the environment of the one that started it, so this one check covers them all. */
bool math_init(math_t *self);
/* Safe on a zeroed math_t. */
void math_deinit(math_t *self);

float32_t math_radians(float32_t degrees);

/* A NaN value stays NaN. Needs low <= high. */
float32_t math_clamp(float32_t value, float32_t low, float32_t high);

/* Exact at both ends: t = 0 gives a and t = 1 gives b, which a + (b - a) * t does not promise. Not limited to 0..1. */
float32_t math_lerp(float32_t a, float32_t b, float32_t t);

#endif
