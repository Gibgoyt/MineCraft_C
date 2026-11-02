# asm/src/vec_batch

**Purpose:** transforms whole arrays of vectors by one matrix, for what has many of them to move at once, such as mob
body parts.
**Tier:** Borrow in spirit: it uses the pointers and count it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It has no state.
**Public functions** (`run/include/simd/vec_batch/index.h`):

- `vec_batch_transform_points`: `mat4_transform_point` on each `vec3_t`.
- `vec_batch_transform_directions`: `mat4_transform_direction` on each `vec3_t`.
- `vec_batch_mul_vec4`: `mat4_mul_vec4` on each `vec4_t`.

**May be called by:** `entity`, `mob`, `render`, `run/src/simd/` for its self-check, tests and benchmarks.
**Depends on:** `asm/include/conventions/`, `asm/include/vec_batch/`, the generated `struct_offsets/index.inc`.

Each result is bit for bit what the scalar function in `run/src/math/mat4/` gives, in the same order of operations. So
this file uses no FMA, and code may use one or the other without a seam showing.
