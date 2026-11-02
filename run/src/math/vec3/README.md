# run/src/math/vec3

**Purpose:** three-component float32 vectors: positions, directions and colors. x east, y north, z up, right-handed.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `vec3_make`, `vec3_splat`: build one.
- `vec3_add`, `vec3_sub`, `vec3_mul`, `vec3_scale`, `vec3_neg`, `vec3_cross`: arithmetic; `vec3_mul` is by component.
- `vec3_dot`, `vec3_length_squared`, `vec3_length`, `vec3_distance`, `vec3_normalize`: measures; normalizing no usable length
  gives zero.
- `vec3_lerp`, `vec3_min`, `vec3_max`, `vec3_abs`, `vec3_floor`: blends, bounds, rounding.

**May be called by:** anything in `run/` from the math layer up. `math/vec4`, `math/ivec3`, `math/mat4`, `math/quat`,
`math/aabb` and `math/ray` are built on it.
**Depends on:** `math/index.h` for `math_lerp`, `types`, glibc's `sqrtf`, `fabsf`, `floorf`.
