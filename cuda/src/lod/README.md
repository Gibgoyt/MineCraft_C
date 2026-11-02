# cuda/src/lod

**Purpose:** rays past full detail: from the finest coarse grid out, each pixel's ray that went on past what the world's blocks show.
**Tier:** a CUDA area. It holds no memory. Blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns. `traverse_wait` waits.
**Threads:** the `gpu` thread.
**Public functions** (`run/include/gpu_bridge/lod/index.h`, for C and CUDA):

- `lod_frame`: one `lod_hit_t` per pixel of a camera, into what `cuda/src/shade/` reads.
- `lod_rays`: the same trace over a list of rays, for a test.

**May be called by:** `cuda/src/frame/`, and tests.
**Depends on:** `cuda/include/traverse/` for the ray loop and `traverse_view_t`, and `gpu_bridge/shade` for `shade_texture_t`.

## How a ray goes

A level is a world of the format `run/src/render/encode/` writes, with a voxel `1 << shift` blocks each way. So a ray in a level is the world's ray scaled down: the same direction, and an eye at `eye / size`, worked out from the whole block and the fraction so that it stays as exact as ever, and `traverse_trace` does the rest. Its `t` is in that level's voxels, and times `size` is blocks.

`traverse_trace`'s window is only a place to clip and a corner to count slots from, so a level's view is given the square rays are let into, `shown_*`, and not the table's. A level only shows what is built.

A ray starts at the finest level that has anything to show and goes on, each from where it left the last one's square. Each is traced with `resume` set, which takes only what it enters from there on. A coarser voxel can hold what a finer one said was air, and a ray never comes to in the middle of a block.

## Where the far field stops and full detail starts

There is no ring to fit against. A hit on a coarse voxel at a place in a column that full detail holds is passed by, and the ray goes on from where it leaves that column; a far column full detail does not hold is drawn. So a round radius of loaded columns against a square level, a column not yet streamed in, and the first second of a world all come out right with nothing to keep in step. It is a rare case for a ray that met nothing in full detail: `LOD_SKIPS_MAX` is what a level allows it.

The far field is asked only about rays that can still get there: what hit nothing in primary rays, and what hit a face with something to see through, so that a window looks out on the hills. It is under the primary pass's clock, as the rest of those same rays. `cuda/src/shade/` looks at `lod_hit_t` only where a ray of its own really leaves the world, so water and glass work out what they let through, and fog comes from `t` as for anything else.

## The sun

One ray for each far surface that faces it, from just outside the surface, through the coarse grids alone: level by level as a ray from the eye, with nothing to pass by, since a level holds all the ground, under full detail as well. So a hill's shadow on the far ground is the hill's. What is near is not shadowed by far hills, as sun rays in shade stop at full detail's edge, and nothing here shadows what is near.

## What is simple about it

A far surface is its face's mean colour and has no texture: a voxel is many blocks, and `mean_rgba` is what `compile/*/texture_vector` made for this. Water out there is what near water comes to when deep: the sky reflected for how square a ray meets it, the texture's mean as far as that covers, and what deep water gives back, with no ray to its bottom. It has no sides, as a side is only where a level rounds the surface, and a ray that came in through near water sees only more water. A far surface takes no AO and no reflections, and has no shape but a cube.
