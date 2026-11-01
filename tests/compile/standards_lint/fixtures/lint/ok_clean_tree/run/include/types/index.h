#ifdef __CUDACC__
typedef float float32_t;
typedef double float64_t;
#define F32(x) x##F
#else
typedef _Float32 float32_t;
typedef _Float64 float64_t;
#define F32(x) x##f32
#endif
typedef double libc_double_t;
#define TYPES_ONE F32(1.0)
