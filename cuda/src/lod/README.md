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
**Depends on:** `cuda/include/traverse/` for the ray loop and `traverse_view_t`, and `cuda/include/shade/` for `shade_texture_t` and what shade's own ray meets: `shade_surface_read`, `shade_water_through`, `shade_own_kind_behind`.

## How a ray goes

A level is a world of the format `run/src/render/encode/` writes, with a voxel `1 << shift` blocks each way. So a ray in a level is the world's ray scaled down: the same direction, and an eye at `eye / size`, worked out from the whole block and the fraction so that it stays as exact as ever, and `traverse_trace` does the rest. Its `t` is in that level's voxels, and times `size` is blocks.

`traverse_trace`'s window is only a place to clip and a corner to count slots from, so a level's view is given the square rays are let into, `shown_*`, and not the table's. A level only shows what is built.

A ray starts at the finest level that has anything to show and goes on, each from where it left the last one's square. Each is traced with `resume` set, which takes only what it enters from there on. A coarser voxel can hold what a finer one said was air, and a ray never comes to in the middle of a block.

## Where the far field stops and full detail starts

There is no ring to fit against. A hit on a coarse voxel at a place in a column that full detail holds is passed by, and the ray goes on from where it leaves that column; a far column full detail does not hold is drawn. So a round radius of loaded columns against a square level, a column not yet streamed in, and the first second of a world all come out right with nothing to keep in step. It is a rare case for a ray that met nothing in full detail: `LOD_SKIPS_MAX` is what a level allows it.

The far field is asked only about rays that can still get there: what hit nothing in primary rays, and what shade's own ray, going on past all it can see through, takes out of full detail, so that a window or a lake looks out on the hills. It is under the primary pass's clock, as the rest of those same rays. `cuda/src/shade/` looks at `lod_hit_t` only where a ray of its own really leaves the world, so water and glass work out what they let through, and fog comes from `t` as for anything else.

## Only where shade will look

A far ray starts at the eye, and passes by the columns full detail holds one at a time, up to `LOD_SKIPS_MAX` fresh traversals a level. For a ray that hit nothing that is cheap, as it meets little coarse ground there. For a ray past water, leaves or a plant it is not: it comes to near ground almost every time, and the far result goes unused. Sent for every such pixel, those rays cost 4 to 6 ms a frame on plains, 19 to 22 on leaves and 26 to 29 on water, at 1080p, at any render distance past full detail (plan 005 phase 8). So for a pixel whose first surface is not a miss, the far field asks in turn:

1. Does shade go on past it? The texel shade will read there, by `shade_surface_read` from `cuda/include/shade/`, the same lines: one that is opaque, and is not water, ends shade's ray. Most leaves and grass end here.
2. Does anything opaque, or water, stand behind it in full detail? One `traverse_trace_stopping` from that surface, which goes through every block but the stops: the blocks no face of which has anything to see through, and water. The renderer works them out once from the same texture table and sends them at the end of the face table (`frame_lod_stops` in `run/src/render/frame/`, `lod_light_t.stops_after_faces`). Almost every ray past leaves, glass or a plant stops here on near ground, and gets nothing. One that gets out gets its far ray.
3. If water stopped it, does shade's own ray get through? `lod_goes_out` follows it as `shade_kernel` does, surface by surface, a body of water walked by `shade_water_through` with `shade_water_kernel`'s cursor, to `SHADE_WATER_SEEN_BLOCKS` with tint on, up to `SHADE_ALPHA_LAYERS`. A sea seen from above is cut short there, and shade never looks past it. With no stops, every pixel shade sees past asks this.

Steps 1 and 2 are `lod_past_kernel`, and 3 and the far rays are `lod_frame_kernel`. Shade's ray can stop sooner than these say, never later, so every far pixel is the same as before, and a test holds far hills to showing through glass and water.

What was tried and dropped: a second trace on through water to its bed, so that a ray down into a lake would stop there. It cost more than step 3, as a ray towards the horizon crosses hundreds of voxels of water one at a time. And step 3 for every pixel past leaves, in place of step 2: it costs what shade's own walk through a canopy costs, about 7 ms on `jungle/orbit`.

## The sun

One ray for each far surface that faces it, from just outside the surface, through the coarse grids alone: level by level as a ray from the eye, with nothing to pass by, since a level holds all the ground, under full detail as well. So a hill's shadow on the far ground is the hill's. What is near is not shadowed by far hills, as sun rays in shade stop at full detail's edge, and nothing here shadows what is near.

## What is simple about it

A far surface is its face's mean colour and has no texture: a voxel is many blocks, and `mean_rgba` is what `compile/*/texture_vector` made for this. Water out there is what near water comes to when deep: the sky reflected for how square a ray meets it, the texture's mean as far as that covers, and what deep water gives back, with no ray to its bottom. It has no sides, as a side is only where a level rounds the surface, and a ray that came in through near water sees only more water. A far surface takes no AO and no reflections, and has no shape but a cube.
