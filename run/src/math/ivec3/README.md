# run/src/math/ivec3

**Purpose:** three-component int32 vectors: block and section coordinates, with the rounding that negative coordinates need.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `ivec3_make`, `ivec3_equal`: build and compare.
- `ivec3_add`, `ivec3_sub`, `ivec3_scale`, `ivec3_neg`, `ivec3_min`, `ivec3_max`: arithmetic and bounds.
- `ivec3_manhattan`: sum of the distances on each axis, in 64 bits so that it cannot overflow.
- `ivec3_floor_div`, `ivec3_floor_mod`: block to section, and block within section, rounding toward negative infinity. C's `/`
  and `%` round toward zero, which puts block -1 in the wrong section.
- `ivec3_from_vec3_floor`, `ivec3_to_vec3`: to and from float32. A position floors to the block it is in. A value that does not
  fit asserts in debug and sanitize.

**May be called by:** anything in `run/` from the math layer up.
**Depends on:** sibling `math/vec3`, `assert`, `types`, glibc's `floorf`.
