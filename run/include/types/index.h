/* The single typedef site: plans/CODING_STANDARD.md section 2. This is the only file where the lint accepts a
 * floating-point base type, and it is compiled by GCC and by nvcc, so any literal in it would have to use F32 or F64.
 *
 * CUDA differs because of Stage 01.1, measured on nvcc 13.4.92: _Float32 in device code is a hard error, and an f32
 * literal in device arithmetic crashes the compiler. The two mappings are the same on the wire.
 *
 * libc_double_t exists only for the cast at a varargs call. GCC treats _Float64 as a distinct type, so -Wformat rejects
 * it for %f (Stage 01.1 issue 3, Stage 01.3 issue 1). Never declare a variable, field or parameter of this type. */
#ifndef MC_RUN_TYPES_INDEX_H
#define MC_RUN_TYPES_INDEX_H

#include <stdint.h>

#ifdef __CUDACC__
typedef float float32_t;
typedef double float64_t;
#define F32(x) x##F
#define F64(x) x
#else
typedef _Float32 float32_t;
typedef _Float64 float64_t;
#define F32(x) x##f32
#define F64(x) x##f64
#endif

typedef double libc_double_t;

static_assert(sizeof(float32_t) == 4);
static_assert(sizeof(float64_t) == 8);
static_assert(alignof(float32_t) == 4);
static_assert(alignof(float64_t) == 8);
static_assert(sizeof(libc_double_t) == 8);

#endif
