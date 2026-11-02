# cuda/src/traverse

**Purpose:** the coarse depth pass and primary rays: hierarchical DDA through the three-level bitmask tree.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns. `traverse_wait` waits.
**Threads:** the `gpu` thread, once there is one.
**Public functions** (`run/include/gpu_bridge/traverse/index.h`, for C and CUDA):

- `traverse_coarse`: per tile, a distance no ray of that tile can hit anything before, or `TRAVERSE_COARSE_SKY`.
- `traverse_primary`: one ray per pixel, with or without the coarse pass, into a `traverse_hit_t` per pixel.
- `traverse_rays`: the same loop over a list of rays.
- `traverse_wait`: waits for what was started, and reports a failure of any of it.

Only for CUDA code (`cuda/include/traverse/index.hu`):

- `traverse_trace`: the ray loop, inline, because a device function cannot be called across files without device linking. Sun rays in `cuda/src/shade/` will call this.

**May be called by:** `render`, the other `cuda/` areas.
**Depends on:** `gpu_bridge/device_memory` and `gpu_bridge/status`, and `cuda/include/device_memory/` for `device_memory_check` and `device_memory_pointer`.

## Why it is the way it is

- The eye is a cell and a fraction, and every plane is computed from the eye: `((float32_t)(plane - eye_cell) - eye_frac) * inverse`. So a ray is as exact at x = 16000000 as at the origin, and a test says so to the bit. Stage 01.3 computed from the world's corner, and could only do so because its world was small.
- Stage 01.3's three bugs stay fixed, each marked in the code: `(plane - origin) * inverse` and never `plane * inverse - origin * inverse` (its issue 2); a coordinate never moves against the ray (issue 3), which is also what bounds a ray's steps; and the hit's face comes from the voxel and not from the last step (issue 4). The last goes further than Stage 01.3 did: a ray enters a box at the latest of its near planes, so the face and `t` are worked out from the voxel alone.
- A uniform section, and a uniform brick, store only one value: Stage 01.3 issue 6.
- One block of threads is one 8 x 8 tile: Stage 01.3 measured it best or tied for best.
- There is no `%` in the loop for the toroidal table: the slot of the window's corner is worked out once per kernel.

## The coarse pass

The proof is in the comment on `traverse_coarse_tile`. In short: all of a tile's rays lie in a cone around its centre ray, so if the bounding box of a stretch of the centre ray, grown by the cone's width at the stretch's end, is empty, then no ray of the tile hits anything in that stretch. It tries a stretch of 16 blocks or a quarter of the distance so far by sections, then one of 16, then one of 4 voxel by voxel, and stops when none is clear. It backs off by a quarter block, so that the cell a ray starts in is not decided by rounding.

A tile whose rays have all left the window for good writes `TRAVERSE_COARSE_SKY`, and its rays cost one clip each.

The box hugs the stretch, and the short stretch looks at voxels. Both are needed for someone standing on the ground, and the measurements are in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-3-traverse.md`, issue 2. A tile looking down at a floor it is next to still says 0, which costs only its own work.

## The format

`traverse_column_t`, `traverse_section_t`, `brick_masks` and `brick_refs` are described in the header. They are Stage 01.3's format, as measured, with one change: a column's `top` is one above its highest occupied height, and 0 when empty, where Stage 01.3 stored the highest height itself, which cannot say empty. Stage 08.2's encoder is to write this: see issue 1 in the stage file.
