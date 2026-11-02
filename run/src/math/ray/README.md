# run/src/math/ray

**Purpose:** rays, and where one meets a box, on the CPU. Walking a ray through the block grid is not here: that is
`run/src/physics/raycast/` and, on the GPU, `cuda/src/traverse/`.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `ray_make`, `ray_from_points`, `ray_from_ndc`: build one. The last gives the camera ray through a screen position.
- `ray_at`: the point at a given t.
- `ray_intersect_aabb`: the slab test, with the t range and the face it came in through. Handles an axis-parallel ray, one that
  starts inside and one that only grazes. A NaN is a miss.

**May be called by:** anything in `run/` from the math layer up.
**Depends on:** siblings `math/aabb`, `math/mat4` and `math/vec3`, `types`.
