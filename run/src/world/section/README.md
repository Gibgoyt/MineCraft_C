# run/src/world/section

**Purpose:** stores one 16x16x16 cube of `uint16_t` block states and `uint8_t` light, as small as its contents allow, with no array at all for a uniform one. A state is a whole voxel: the block id in its low 12 bits and the block's own state (facing, half, open) in the 4 above, as `run/include/types/block_state/` lays them out.
**Tier:** Arena (depth 2): takes memory from an `arena_t *` that `run/src/world/` gives its pool, and never frees: released blocks wait in the pool for the next of their size.
**Owns:** nothing. A section holds its arrays from the pool, and they lie in its parent's arena.
**Lifetime:** World.
**Threads:** a section has one writer and no readers during a write, since a write may move its block. Any number of readers without a writer. A pool is one thread at a time, so a section's writer must also hold the pool. There is one pool, so the plan for terrain (Phase 10) is that workers build flat arrays in their job arenas and the pool's thread calls `section_blocks_import`.
**Public functions** (`run/include/world/section/index.h`):

- `section_pool_init`, `section_pool_deinit`, `section_pool_reset`: the pool, and forgetting it after its arena is reset.
- `section_voxel_index`: `x`, `y`, `z` to the one index every array uses.
- `section_light_pack`, `section_light_sky`, `section_light_block`: sky and block light in one byte.
- `section_init`, `section_deinit`: a uniform section, and giving everything back.
- `section_block_get`, `section_light_get`: one voxel. Inline, since nothing is read more.
- `section_block_set`, `section_light_set`: one voxel. False if the pool has no room, with nothing changed.
- `section_block_fill`, `section_light_fill`: every voxel the same, returning the array.
- `section_blocks_import`, `section_blocks_export`, `section_lights_import`, `section_lights_export`: all voxels, flat.
- `section_compact`: looks for what writes cannot see. Writes call it every 4096.
- `section_palette`, `section_palette_capacity`: read the palette, for encoding.
- `section_pool_bytes`: what a section holds in the pool.

**May be called by:** `run/src/world/`, `world/column`, `world/map`, and `save/world`.
**Depends on:** `arena`, `assert`, `machine`.

## The forms

Blocks and light change form separately, by themselves.

| Blocks | Bits per voxel | Palette | Block in the pool, bytes |
|---|---|---|---|
| Uniform | 0 | the one state, in `section_t` | none |
| Up to 2 states | 1 | 2 | 576 |
| Up to 4 states | 2 | 4 | 1088 |
| Up to 16 states | 4 | 16 | 2112 |
| Up to 256 states | 8 | 256 | 5120 |
| More | 16, the state itself | none | 8192 |
| Light: uniform | 0 | the one value, in `section_t` | none |
| Light: varied | 8 | none | 4096 |

A palette block is the packed indices, then the palette, then a `uint16_t` count of voxels per slot, on a cache line. Indices never straddle a `uint64_t`, since each width divides 64.

A write that needs another form makes it. The counts are what let a palette see its own states die out: at one state it becomes uniform and returns its block at once, and it narrows when half its room would still hold what is left. So after growing to `n` bits there must be many writes before a repack, and the repack's cost of 4096 voxels is not paid on every edit near a limit.

A direct array and a light array have no counts. Instead, every 4096 writes that changed something, `section_compact` looks at all of it, so a write pays for about one read, and a light pass can call it as it ends. A direct array returns to a palette only at 128 states or fewer, half of the largest.

## Measured memory per section

From ctest `run_world_section`, test `memory_per_section_is_as_the_readme_says`, which fails if a number moves. Each section is also 24 bytes in its column. "Of flat" is against 2 bytes of block and 1 of light per voxel, 12288 bytes.

| Section | Pool bytes | With struct | Of flat |
|---|---|---|---|
| All air, or all stone, lit evenly | 0 | 24 | 0% |
| Air and one block, lit evenly | 576 | 600 | 4% |
| 4 states, lit evenly | 1088 | 1112 | 9% |
| 16 states, lit evenly | 2112 | 2136 | 17% |
| All air, with varied light | 4096 | 4120 | 33% |
| 4 states, with varied light | 5184 | 5208 | 42% |
| 16 states, with varied light | 6208 | 6232 | 50% |
| 256 states, with varied light | 9216 | 9240 | 75% |
| 4096 states, with varied light | 12288 | 12312 | 100% |

## What it does not do

A free block serves only its own size, so a world that used many 1-bit blocks and now wants 4-bit ones takes new memory while the old wait. `section_pool_t`'s `carved_bytes` against `live_bytes` shows how much. `world_reset` clears it, and nothing has yet needed more.

A state's meaning is not known here. Block 0 is air, as `run/src/world/column/` asserts against the generated tables, so that zero bytes are an empty section.

## The state beside the id (plan 005 phase 12)

Nothing here changed for it: a voxel was already a `uint16_t`, and the block table never had more than 4096 ids, so the top 4 bits were always 0. They now hold the block's state. A palette tells two facings of one block apart as it tells two blocks apart, so a section costs more only when it holds a block in more than one state: a staircase turning a corner is two palette entries where it was one. The table of memory above did not move, and neither did `bench_world`'s: see that phase's file.
