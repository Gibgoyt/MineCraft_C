# run/src/math/quat

**Purpose:** rotations as unit quaternions, for interpolating body parts and the camera between ticks. Aligned to 16.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `quat_identity`, `quat_make`, `quat_from_axis_angle`, `quat_from_mat4`: build one. A zero axis gives the identity.
- `quat_multiply`, `quat_conjugate`, `quat_dot`, `quat_normalize`: arithmetic. Normalizing no usable length gives the identity.
- `quat_rotate`, `quat_to_mat4`: applying one. Both need unit length.
- `quat_slerp`: constant speed along the shorter way round.

**May be called by:** anything in `run/` from the math layer up.
**Depends on:** siblings `math/mat4` and `math/vec3`, `types`, glibc's `sinf`, `cosf`, `acosf`, `sqrtf`.
