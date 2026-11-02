# run/src/render/encode

**Purpose:** turns a section's voxels into the three-level `uint64_t` bitmask tree the card walks: section mask, brick masks, then material and light for occupied voxels only.
**Tier:** Arena, and it allocates nothing: the caller gives it where to write.
**Owns:** nothing.
**Lifetime:** Job.
**Threads:** `worker`, through `render_encode_job_run`. `render_encode_section` touches only what it is given, so any thread may call it.
**Public functions** (`run/include/render/encode/index.h`):

- `render_encode_section`: one section's voxels to bytes; no blocks means no bytes.
- `render_encode_job_begin`, `render_encode_job_run`, `render_encode_job_done`: the same as a job over one section of a snapshot view, and how the gpu thread sees it finish.

**May be called by:** `render/upload`, and tests.
**Depends on:** `gpu_bridge/world_buffers`, whose header defines the form; `world/edit`, for snapshot views; `job`; `simd/bitmask`.

## What is a block

State 0 is air, with no entry. Every other state is occupied, water and glass among them: a ray has to stop at those too.

Light is stored only for occupied voxels, as the plan says. See Stage 08.2 issue 3 for what that means for shading.

## What it costs

Time follows the blocks, not the 4096 voxels. `asm/src/bitmask` turns the states into a bit per voxel, and from there only set bits are visited, twice: once to sort them into bricks, and once to read each one's material and light in brick order.

Bytes are `16 + 10 * bricks + 3 * voxels`, rounded up to 8. The most is 12944. A section of one block and one light all through takes 24, and nothing else is compressed. Stage 08.2 issue 5 has the numbers from before that form existed, and Stage 08.4 issue 2 says why it does.
