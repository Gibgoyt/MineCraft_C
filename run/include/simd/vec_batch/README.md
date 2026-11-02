# run/include/simd/vec_batch

**Purpose:** C prototypes of the NASM functions in `asm/src/vec_batch/`.
**Tier:** none: a header with no source in `run/`.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any.
**Public functions:** `vec_batch_transform_points`, `vec_batch_transform_directions`, `vec_batch_mul_vec4`.
**May be called by:** `entity`, `mob`, `render`, `run/src/simd/`, tests and benchmarks.
**Depends on:** `asm/src/vec_batch/`, `math/mat4`, `math/vec3`, `math/vec4`.
