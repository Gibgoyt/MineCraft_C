# 009-ahmed-bohemoth-game-engine-optimizations — phase 5: Merged boxes for empty space and water

**Status:** not started   (updated 2026-11-12)

Read first: the phase 00 file, then the master: sections 2.1, 3.4 (the 64-tree, Teardown and Chebyshev rows), 4.6 and 5. Then:

- `cuda/include/traverse/index.hu:305-455`: the loop, and how it picks `shift`;
- `cuda/src/traverse/README.md`: "What a ray skips", "The coarse pass";
- `cuda/include/shade/index.hu`: `shade_water_through`, `shade_cell_material_kept`;
- `run/include/gpu_bridge/world_buffers/index.h`: the section format;
- phase 01's step bands (steps inside occupied bricks, and water-walk steps) and phase 04's Results.

## Goal

Rays cross what they would otherwise step through cell by cell in fewer, larger steps, with **exactly the same hits**. This is Ahmed's "one shape instead of many", where it pays in a ray caster (master 2.1). Five changes are tried; each is kept only if it measures, and each kept one changes no hit. When this phase is done:

1. **2³ coalescing:** inside an occupied brick, an empty 2×2×2 cube is crossed in one step.
2. **Per-brick distance:** an empty brick knows its Chebyshev distance, in bricks, to the nearest occupied brick, and a ray crosses that many bricks in one step.
3. **Uniform bricks:** a brick full of one block is marked, and the water walk crosses it in one step, and a uniform section in one.
4. **The coarse pass on the pyramid:** far stretches test level-k occupancy, eight times fewer reads per level.
5. **Octant mirroring:** fewer branches per step, if it pays here.

For each, Results record its card time and steps at his spot, `jungle/orbit`, `deep_ocean/orbit` and `plains/orbit`, against phase 04, and whether it was kept.

**Every hit stays bitwise identical:** `cuda_traverse` compares every hit's voxel, face and `t` to a reference loop that knows no skipping, and every golden picture and settled hash from phase 04 is unchanged.

## Where this fits

**The vision.** Ahmed: "if there are is a 2x2x1 group of blocks, they should be rendered only as triangle lines of the larger rectangle". In a ray caster the cost is steps, so the larger rectangle that saves work is a larger *empty* or *uniform* cell for the ray to cross. Air above a slope merged into bigger steps; a lake of water merged into one box.

**What reads it:** phase 06, whose shadow rays start next to surfaces, where distances help most; phase 08.

**What this phase must NOT build:**

- No change to any hit, picture or level choice. These are exact.
- No unaligned, anisotropic boxes per direction. The 64-tree write-up measured 2 to 5 % for them; per-brick distance is the cheap form, tried first. Anisotropic distances are a follow-up only if per-brick distance pays more than 10 %.
- No change to what is on disk. The section format on the card changes (3); sections are encoded from the world on every load.

## Ahmed's words

- "if there are is a 2x2x1 group of blocks, they should be rendered only as triangle lines of the larger rectangle that it has created as a result of being one shape!!"
- "further optimization as we move away!!"

---

## Background, explained

### Where steps go to waste today

The loop picks the largest empty node holding the current cell (`index.hu:363-425`):

| Where the cell is | Node | Step size |
|---|---|---|
| Above the region's top | 8 × 8 columns, up to the sky | up to 128 blocks |
| Above the column's top | the column, up to the sky | 16 |
| In a section with no bricks there (`brick_mask` bit clear) | the 4³ brick | 4 |
| In an occupied brick, on an empty voxel | **one voxel** | **1** |
| In a uniform section of something it passes through (a stop ray) | the section | 16 |
| In a uniform section of a non-cube shape the ray missed | one voxel | 1 |

**The waste is the fourth row.** A ray skimming a slope, a ray just above a floor, and a shadow ray leaving a surface are all in occupied bricks most of their way near the ground, stepping 1. Phase 01's bands say how many such steps his view has.

### 2³ coalescing, worked

A brick's 64 voxel bits are at `(x & 3) | (y & 3) << 2 | (z & 3) << 4`. The 2×2×2 cube whose low corner is (0,0,0) covers bits 0, 1, 4, 5, 16, 17, 20 and 21: mask `0x0000000000330033`. The cube at (bx, by, bz) ∈ {0, 2}³ is that mask shifted left by `bx | by << 2 | bz << 4`.

So, on an empty voxel in an occupied brick: if `(brick_mask >> base) & 0x330033 == 0`, the node is the 2³ cube (`shift = 1`), not the voxel. That is one shift, one `and` and one compare, on values already in registers. The 64-tree write-up measured **21 % fewer cycles per primary ray** for the same idea on the same 4³ bricks.

### Per-brick Chebyshev distance, worked

For each brick of a section, store `d`: the number of brick rings around it that hold no voxel, capped at `distance_cap` (3 by default, so 2 bits would do; a byte keeps it simple). A ray in a brick with `d > 0` may move to anywhere inside the cube of (2d + 1)³ bricks centred on it without meeting anything. So the node becomes that cube, clipped to the section, or to neighbours that are known empty, because `d` was worked out across neighbours.

- **Cost:** 64 bytes a section, written by `render_encode_section` beside the masks. An edited section changes its own `d`s and its neighbours' up to `distance_cap` bricks away, so the up-to-26 neighbour sections are re-encoded only if a `d` actually changed.
- **Prize:** for primary rays the write-up found unaligned skipping worth only 2 to 5 %. For **shadow and AO rays**, which start on a surface and leave it, more of the path lies in near-surface bricks, so the prize may be larger. Phase 06's rays are counted separately. Deakin and Knackstedt (2020) call Chebyshev distance maps the fastest known empty-space skipping for volume rays; whether that holds for this tree is what this measures.

### Uniform bricks for water, worked

A lake is water voxels. The water walk (`shade_water_through`) steps cell by cell through it, reading the tree at each, and plan 005 phase 8 measured this walk as **6 to 10 ms of `deep_ocean/orbit`** at render distance 48 on the slow card. A 64-bit `uniform_mask` in the section header, one bit per brick that is full and all one block with one light, lets the walk cross such a brick in one step (4 blocks) and a uniform section in one (16 blocks, which `brick_count == 0` already says). That is the "one shape" for see-through runs.

The header grows from 16 to 24 bytes:

    world_buffers_section_t { uint64_t brick_mask; uint64_t uniform_mask; uint16_t voxel_count; ...; }

`WORLD_BUFFERS_SECTION_BYTES_MAX`, `_UNIFORM` and every static_assert move together. The host's copy of the format and the card's are the same header, so one change covers both.

### The coarse pass on the pyramid

`traverse_box_is_empty` (`cuda/src/traverse/index.cu:36-116`) walks columns, sections and bricks of level 0 for each stretch's box. Past d<sub>1</sub>, a ray uses level 1 anyway (phase 04), so the coarse pass may test level-k occupancy for stretches past d<sub>k</sub>. A box 8 blocks wide is one level-3 voxel instead of 512 level-0 ones.

Conservativeness is the one rule: a tile's distance must never pass something a ray of the tile would hit at the level it uses there. Since a ray at `t ≥ d_k` uses level k, testing level k over that stretch is exactly as conservative as the rays. The existing test, "compares against an exact trace of every pixel", holds it.

## Design

| Change | Where | Switch while measuring |
|---|---|---|
| 2³ coalescing | `cuda/include/traverse/index.hu`, the occupied-brick branch | `render/traverse` `coalesce_2` (bool) |
| Per-brick distance | `run/src/render/encode/` writes it; the format gains `distance[64]` after the brick words; the loop reads it in the empty-brick branch | `distance_cap` (0 = off) |
| Uniform bricks | Header `uniform_mask`; `render_encode_section`; `shade_water_through` and the loop's uniform branch | `uniform_bricks` (bool) |
| Coarse pass on the pyramid | `cuda/src/traverse/index.cu`, `traverse_coarse_tile` gets the bands and level views | `coarse_on_pyramid` (bool) |
| Octant mirroring | The loop: mirror coordinates into the positive octant once per ray | `mirror` (bool) |

The switches are tuning keys during this phase. At the end, a kept change's switch is removed (the code stays), and a dropped change's code is deleted, so no dead branch stays.

## Owned paths

| Path | Change |
|---|---|
| `cuda/include/traverse/index.hu` | Coalescing, distance step, uniform step, mirroring |
| `cuda/src/traverse/index.cu` | Coarse pass on the pyramid |
| `cuda/include/shade/index.hu`, `cuda/src/shade/` | The water walk's uniform step |
| `run/include/gpu_bridge/world_buffers/index.h`, `cuda/src/world_buffers/` | The header and format |
| `run/src/render/encode/`, `run/src/render/upload/` | Distances, uniform mask, neighbour re-encode |
| `compile/config/render/traverse/index.yaml` | Switches and `distance_cap` |
| `tests/cuda/traverse/`, `tests/run/render/encode/`, `tests/cuda/water/` | Exactness tests |

## Steps

1. **Coalescing first:** smallest, no format change, best published number. Exactness test, then measure.
2. **Uniform bricks:** format change, but a sure prize on water. Measure on `deep_ocean/orbit` and on a lake at his spot.
3. **Per-brick distance:** measure primary and, separately, sun and AO ray steps.
4. **The coarse pass on the pyramid.**
5. **Mirroring last;** dropped under 3 %.
6. **Remove switches and dead code;** record what stayed and why.

*Why this order:* cheapest and surest first, so a later experiment is measured on top of what is kept.

## Todo

- [ ] 2³ coalescing; `cuda_traverse` exact; Results row
- [ ] `uniform_mask` in the header (24 bytes) with every assert; encode; water walk and loop use it; `render_feature_water` exact; Results row
- [ ] Per-brick distance written and read; neighbour re-encode on edit; exact; Results rows for primary, sun and AO steps
- [ ] Coarse pass on the pyramid; the exact-trace test passes; Results row
- [ ] Mirroring tried; kept only if at least 3 %
- [ ] Each change kept or dropped by its numbers; switches removed; dropped code deleted
- [ ] Every golden picture and settled hash unchanged from phase 04
- [ ] `cuda/src/traverse/README.md` "What a ray skips" and `run/include/gpu_bridge/world_buffers/index.h`'s format comment updated with each kept change

## Results (filled in by this phase)

| Change | His spot (primary / shade ms, steps) | `jungle/orbit` | `deep_ocean/orbit` | `plains/orbit` | Kept? |
|---|---|---|---|---|---|
| Phase 04 baseline | | | | | — |
| + coalescing | | | | | |
| + uniform bricks | | | | | |
| + per-brick distance | | | | | |
| + coarse on pyramid | | | | | |
| + mirroring | | | | | |

## Tests

### `tests/cuda/traverse/` — ctest `cuda_traverse`

- **`every_skip_finds_what_the_plain_loop_finds`.** The existing reference (a loop that knows no skipping) against the real one, with each switch on and all on. Every hit's voxel, face, value and `t` bitwise equal, over:
  - the existing scenes;
  - a slope at 10° and 30° seen from 1 block above;
  - a checkerboard of single voxels, the worst case for coalescing.
- **`skips_take_fewer_steps`:** on the slope scene, mean steps fall with each kept change, and the numbers are printed.
- **`a_distance_never_passes_a_voxel`:** random sections with one voxel; every empty brick's `d` equals the brute-force Chebyshev distance, capped.

### `tests/run/render/encode/` — ctest `run_render_encode`

- `the_uniform_mask_marks_full_one_block_bricks`: a brick of water marked; a brick of water with one stone not; a full brick of two lights not.
- `an_edit_re_encodes_neighbours_only_when_a_distance_changes`.

### `tests/cuda/water/` — ctest `render_feature_water`

Golden pictures unchanged, bitwise, with uniform bricks on.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(cuda_traverse|run_render_encode|render_feature_water|render_feature_lod)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_.*|run_render_frame)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --steps /tmp/mc_c_009/p05/coalesce.tsv
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =deep_ocean/orbit --render-distance 64
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 phase 05: merged boxes"
    pgrep -af "$PWD/build"                                                       # must print nothing

## Gate

Pictures do not change, so the gate is numbers. Ahmed reads the Results table. If he wants, he plays his slope and a lake and says whether it feels faster. He says go for phase 06.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Coalescing adds a branch that costs more than it saves on some rows | Measured per row; dropped if any row at render distance 32 is over 3 % slower |
| The 24-byte header makes every section 8 bytes bigger | Uniform sections go from 24 to 32 bytes; VRAM recorded; within budget |
| Per-brick distance costs more to keep current under edits than it saves | Re-encode only on a changed `d`; dropped if it does not pay 3 % |
| A skip passes a voxel | The bitwise reference test; no skip lands without it |
| The coarse pass on the pyramid is not conservative at the band edge | Use the finer level within `band_dither` of an edge; the exact-trace test |

## Commits

Appended as they land:

## Issues found

None.
