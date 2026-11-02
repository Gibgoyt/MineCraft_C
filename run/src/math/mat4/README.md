# run/src/math/mat4

**Purpose:** 4x4 float32 matrices: column-major, column vector on the right. Aligned to 32. Always passed by pointer, since 64
bytes is over the by-value limit in `plans/CODING_STANDARD.md` section 6.
**Tier:** Arena by depth. It allocates nothing. Results go to the caller's `out`.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `mat4_identity`, `mat4_multiply`, `mat4_transpose`: the basics. `out` may alias an input.
- `mat4_determinant`, `mat4_inverse`: inverse says false when there is none.
- `mat4_mul_vec4`, `mat4_transform_point`, `mat4_transform_direction`, `mat4_project_point`: applying one. Only the last divides
  by w, and it says false when w is zero.
- `mat4_translation`, `mat4_scaling`, `mat4_rotation`: building transforms.
- `mat4_look_at`, `mat4_perspective`: the camera. View space looks down -z, and clip depth runs 0 at near to 1 at far.

**May be called by:** anything in `run/` from the math layer up. `math/quat` and `math/ray` are built on it.
**Depends on:** siblings `math/vec3` and `math/vec4`, `math/index.h` for `MATH_PI`, `assert`, `types`, glibc's `sinf`, `cosf`,
`tanf`.
