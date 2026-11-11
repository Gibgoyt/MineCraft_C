# cuda/src/traverse

**Purpose:** the coarse depth pass and primary rays: hierarchical DDA through the three-level bitmask tree, and the table of boxes for what is not a whole cube.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns. `traverse_wait` waits.
**Threads:** the `gpu` thread, once there is one.
**Public functions** (`run/include/gpu_bridge/traverse/index.h`, for C and CUDA):

- `traverse_coarse`: per tile, a distance no ray of that tile can hit anything before, or `TRAVERSE_COARSE_SKY`.
- `traverse_primary`: one ray per pixel, with or without the coarse pass, into a `traverse_hit_t` per pixel.
- `traverse_rays`: the same loop over a list of rays.
- `traverse_wait`: waits for what was started, and reports a failure of any of it.
- `traverse_hit_face`, `traverse_hit_steps`: what a hit's `info` says.
- `traverse_tiles_across`, `traverse_coarse_bytes`, `traverse_hits_bytes`: sizes, for whoever holds the blocks.

Only for CUDA code (`cuda/include/traverse/index.hu`):

- `traverse_trace`: the ray loop, inline, because a device function cannot be called across files without device linking. `cuda/src/shade/` calls it for rays going on through holes, and sun rays there will too.
- `traverse_trace_stopping`, `traverse_goes_through`: the same loop for a ray that stops only on the materials a `traverse_stops_t` names, and goes through every other as air, a section all of one whole. `cuda/src/lod/` asks it whether anything opaque stands behind a see-through surface. Inlined with no stops, as `traverse_trace` is, the extra tests are not there at all.
- `traverse_boxes`, `traverse_shape_rows`: the shape table, on the card as constants, for the same reason in the header and not here.
- `traverse_view_from`: a `traverse_world_t` checked and made into what a kernel takes.

**May be called by:** `render`, the other `cuda/` areas.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/world_buffers` and `gpu_bridge/status`, and `cuda/include/device_memory/` for `device_memory_check` and `device_memory_pointer`.

## Why it is the way it is

- The eye is a cell and a fraction, and every plane is computed from the eye: `((float32_t)(plane - eye_cell) - eye_frac) * inverse`. So a ray is as exact at x = 16000000 as at the origin, and a test says so to the bit. Stage 01.3 computed from the world's corner, and could only do so because its world was small.
- Stage 01.3's three bugs stay fixed, each marked in the code: `(plane - origin) * inverse` and never `plane * inverse - origin * inverse` (its issue 2); a coordinate never moves against the ray (issue 3), which is also what bounds a ray's steps; and the hit's face comes from the voxel and not from the last step (issue 4). The last goes further than Stage 01.3 did: a ray enters a box at the latest of its near planes, so the face and `t` are worked out from the voxel alone.
- A section of one block and one light stores only those: Stage 01.3 issue 6 and Stage 08.4 issue 2.
- One block of threads is one 8 x 8 tile: Stage 01.3 measured it best or tied for best.
- There is no `%` in the loop for the toroidal table: the slot of the window's corner is worked out once per kernel.

## Shapes

A material's `traverse_shape_t` comes from the table `run/src/render/frame/` sends, `traverse_world_t.borrowed_shapes`. Nothing there, or a material past its end, or a value with no row, is a whole cube, and a cube takes the same few lines as ever: and with the setting off a picture is what it was before shapes, pixel for pixel: Stage 09.1 checked that against the old golden pictures.

A shape is up to four boxes inside its voxel, in 32nds of a block. So the voxel's bit in the tree stays set, the coarse pass stays right without knowing, and only the last step changes: `traverse_voxel_hit` cuts the ray against each box, from the eye as everything here is, and the nearest wins. If none is met the ray goes on, one voxel at a time, also through a section all of one block, which has no masks to skip by.

A ray with `resume` set is going on past a surface at `t_start`, and takes only what it enters after `t_start`. It need not know what it left or how far that goes. This relies on working a plane out the same way each time, whole number first, so that a cube's `t` here is the same bits as `traverse_hit_found`'s and is not after itself.

From inside a box a ray says `TRAVERSE_FACE_INSIDE` for a slab or stairs. For a plate or a torch it sees on past: someone flying through grass does not want the screen to go dark.

**A shape turns with its voxel's facing** (plan 005 phase 12). The table holds each shape as it faces north: stairs rising towards `+y`, a ladder or a vine on the `+y` side, a torch on a wall with its wall to `+y`. The facing is in the voxel, bits 12 and 13 of `traverse_hit_t.value` (`run/include/types/block_state/`), and `traverse_voxel_hit` turns each box about the middle of the voxel with `block_facing_box_turn` before it cuts the ray against it, so only a voxel that is not a cube pays for it, and a turned box is exactly as many 32nds from each wall as the table says. `TRAVERSE_VALUE_MATERIAL_MASK` is the block id alone, so the shape table and the textures are by block, and `TRAVERSE_VALUE_VOXEL_MASK` the id with its state. `cuda/src/shade/` reads a turned block's own faces: which of them the world sees on a side, and where on its picture, with `block_facing_face_to_local` and `block_facing_point_to_local`, so a bench's front stays its front and its top turns with it.

A torch on a wall is one shape of five boxes: the table holds boxes along the axes, so a stick leans by steps. `run/src/world/wall_torch/README.md` says why its colours go by height alone.

Plants are a `+` and not Minecraft's `X`: the table holds boxes along the axes, as the plan says, so the two plates are square to the world.

## What a ray skips

Besides the tree's own levels, three things let a ray cross empty space without asking each column it passes, none of which changes what it finds:

- **A column's top:** above it, the node is the column up to the top of the world.
- **A square's top:** `world_buffers_column_t.region_top`, in sections, one above the highest block of the `WORLD_BUFFERS_REGION_COLUMNS` square of columns on the world's own grid. Above it the node is the whole square. It is read with the column, on the same cache line. This is for a ray that goes a long way low over the ground: a reflection, a low sun, a primary ray at the horizon. It is ignored in a slot that holds another column, and 0 means not known.
- **The world's top:** `traverse_world_t.top_limit`, for a ray going up: at or above this, there is nothing left to meet, so it misses at once. The coarse pass uses it too. It comes from `world_buffers_t.top_limit`, which never comes down, as working that out would take a walk of the table: too high costs only steps.

The tests in `tests/cuda/traverse/` run with all three, against a reference that knows of none. `TRAVERSE_INFO`'s steps count what was done, so they are lower with these.

A step's three-way choice of axis is no branch: all three coordinates are worked out, and one kept. Rays of a warp that go different ways there would otherwise run their sides in turn, on every step.

## The coarse pass

The proof is in the comment on `traverse_coarse_tile`. In short: all of a tile's rays lie in a cone around its centre ray, so if the bounding box of a stretch of the centre ray, grown by the cone's width at the stretch's end, is empty, then no ray of the tile hits anything in that stretch. It tries a stretch of 16 blocks or a quarter of the distance so far by sections, then one of 16, then one of 4 voxel by voxel, and stops when none is clear. It backs off by a quarter block, so that the cell a ray starts in is not decided by rounding.

A tile whose rays have all left the window for good writes `TRAVERSE_COARSE_SKY`, and its rays cost one clip each.

The box hugs the stretch, and the short stretch looks at voxels. Both are needed for someone standing on the ground, and the measurements are in `plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-3-traverse.md`, issue 2. A tile looking down at a floor it is next to still says 0, which costs only its own work.

## The format

There is one format: the table and the slab pool in `run/include/gpu_bridge/world_buffers/index.h`, which `run/src/render/encode/` writes. Stage 08.3 first walked a format of its own, Stage 01.3's, and Stage 08.4 issue 1 is how the two became one. A column's height comes from its `top_z`, and a slot that is empty, or still holds the column that wrapped out of this place, reads as empty. `traverse_hit_t.value` is the voxel's material with its light above it.
