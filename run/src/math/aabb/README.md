# run/src/math/aabb

**Purpose:** axis-aligned boxes, for collision and entity bounds. Swept collision is not here: that is `run/src/physics/`.
**Tier:** Arena by depth. It allocates nothing.
**Owns:** nothing.
**Lifetime:** Values only.
**Threads:** any. No shared state.
**Public functions:**

- `aabb_make`, `aabb_from_corners`, `aabb_from_center`: build one.
- `aabb_is_valid`, `aabb_center`, `aabb_half_size`, `aabb_size`, `aabb_volume`: measures.
- `aabb_contains_point`: min included, max not, so a point on a shared wall is in one box only.
- `aabb_overlaps`: shared volume only, so boxes that merely touch do not overlap.
- `aabb_translate`, `aabb_expand`, `aabb_union`, `aabb_intersection`: new boxes. An intersection may be invalid.
- `aabb_closest_point`: the nearest point in or on the box.

**May be called by:** anything in `run/` from the math layer up. `math/ray` is built on it.
**Depends on:** sibling `math/vec3`, `types`.
