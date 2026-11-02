# run/src/math/vec4

**Purpose:** four-component float32 vectors: homogeneous points and colors with alpha. Aligned to 16.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `vec4_make`, `vec4_from_vec3`, `vec4_to_vec3`: build one, or convert; `vec4_to_vec3` drops w without dividing.
- `vec4_add`, `vec4_sub`, `vec4_mul`, `vec4_scale`, `vec4_neg`: arithmetic; `vec4_mul` is by component.
- `vec4_dot`, `vec4_length_squared`, `vec4_length`, `vec4_normalize`: measures; normalizing no usable length gives zero.
- `vec4_lerp`: a blend.

**May be called by:** anything in `run/` from the math layer up. `math/mat4` is built on it.
**Depends on:** sibling `math/vec3`, `math/index.h` for `math_lerp`, `types`, glibc's `sqrtf`.
