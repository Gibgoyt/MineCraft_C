# run/src/math/vec2

**Purpose:** two-component float32 vectors, for screen and texture positions.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `vec2_make`: builds one.
- `vec2_add`, `vec2_sub`, `vec2_mul`, `vec2_scale`, `vec2_neg`: arithmetic; `vec2_mul` is by component.
- `vec2_dot`, `vec2_length_squared`, `vec2_length`, `vec2_normalize`: measures; normalizing no usable length gives zero.
- `vec2_lerp`, `vec2_min`, `vec2_max`: blends and bounds.

**May be called by:** anything in `run/` from the math layer up.
**Depends on:** `math/index.h` for `math_lerp`, `types`, glibc's `sqrtf`.
