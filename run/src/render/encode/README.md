# run/src/render/encode

**Purpose:** turns a section's voxels into the three-level `uint64_t` bitmask tree the card walks: section mask, brick masks, then material and light for occupied voxels only.
**Tier:** Arena, and it allocates nothing: the caller gives it where to write.
**Owns:** nothing.
**Lifetime:** Job.
**Threads:** `worker`, through `render_encode_job_run`. `render_encode_section` touches only what it is given, so any thread may call it.
**Public functions** (`run/include/render/encode/index.h`):

- `render_encode_section`: one section's voxels to bytes; no blocks means no bytes.
- `render_encode_block_counts`, `render_encode_octant_voxel`, `render_encode_section_octant`, `render_encode_job_octant_set`: plan 009 phase 03: a section reduced by two each way to its octant of the level above, by the half rule and the grass-top rule (below), and a job that makes it beside the bytes.
- `render_encode_job_begin`, `render_encode_job_run`, `render_encode_job_done`: the same as a job over one section of a snapshot view, and how the gpu thread sees it finish.

**May be called by:** `render/upload`, `render/lod`, `render`, and tests.
**Depends on:** `gpu_bridge/world_buffers`, whose header defines the form; `world/edit`, for snapshot views; `job`; `simd/bitmask`; `tables/blocks`, for which blocks count towards a coarser voxel.

## What is a block

State 0 is air, with no entry. Every other state is occupied, water and glass among them: a ray has to stop at those too.

Light is stored only for occupied voxels, as the plan says. See Stage 08.2 issue 3 for what that means for shading.

## What it costs

Time follows the blocks, not the 4096 voxels. `asm/src/bitmask` turns the states into a bit per voxel, and from there only set bits are visited, twice: once to sort them into bricks, and once to read each one's material and light in brick order.

Bytes are `24 + 10 * bricks + 3 * voxels`, rounded up to 8. The most is 12952. A section of one block and one light all through takes 32, and nothing else is compressed. The header's `uniform_mask` (plan 009 phase 05) has a bit for each full brick whose 64 voxels are one material, whatever their light, which the water walk crosses in one step; a uniform section has all 64. The header grew from 16 bytes for it, so each of those sizes is 8 more than it was. Stage 08.2 issue 5 has the numbers from before that form existed, and Stage 08.4 issue 2 says why it does.
