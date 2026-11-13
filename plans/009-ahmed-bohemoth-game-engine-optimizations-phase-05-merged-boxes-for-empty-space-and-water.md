# 009-ahmed-bohemoth-game-engine-optimizations — phase 5: Merged boxes for empty space and water

**Status:** partly done: changes 1 to 3 tried (1 and the cheap form of 2 dropped, 3 kept); 4 and 5 not tried   (updated 2026-11-13)

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

- [x] 2³ coalescing; `cuda_traverse` exact; Results row (dropped: 3 % slower at his spot)
- [x] `uniform_mask` in the header (24 bytes) with every assert; encode; water walk uses it (the loop already crosses a uniform section whole); `render_feature_water` exact; Results row
- [ ] Per-brick distance written and read; neighbour re-encode on edit; exact; Results rows for primary, sun and AO steps (only its cheap form, a 2 x 2 x 2 cube of empty bricks as one step, was tried: dropped, 5 % slower on `deep_ocean/orbit`)
- [ ] Coarse pass on the pyramid; the exact-trace test passes; Results row
- [ ] Mirroring tried; kept only if at least 3 %
- [x] Each change kept or dropped by its numbers; switches removed; dropped code deleted (for the three tried)
- [x] Every golden picture and settled hash unchanged from phase 04 (`render_feature_*` pass; the settled hashes of his spot and `deep_ocean/orbit` the same with every change; see Results for `jungle/orbit` and `plains/orbit`)
- [x] `cuda/src/traverse/README.md` "What a ray skips" and `run/include/gpu_bridge/world_buffers/index.h`'s format comment updated with each kept change

## Results (filled in by this phase)

Measured on the profile build at 4c147bd9 (phase 04's last commit), 1920x1080, one `bench_world` or `bench_world_slope` row per run through the
wrapper, the builds' binaries saved and run interleaved. The card is power-capped and its clock wandered from 146 to 1590 MHz from run to run,
so a time is compared only with one at the same clock: his spot ran at 148 MHz in most runs, and `deep_ocean/orbit` at 300 MHz. His spot is
`ahmed_slope/night-64` (render distance 64); the orbits are at 32.

| Change | His spot at 148 MHz: Mcycles; primary / shade (AO) ms | `jungle/orbit` | `deep_ocean/orbit` at 300 MHz: card mean ms | `plains/orbit` | Kept? |
|---|---|---|---|---|---|
| Phase 04 baseline | 7.63, 7.66, 7.78; 17.2 to 17.5 / 23.9 to 24.3 (9.1 to 9.2) | 45.9, 78.7 | 101.1, 98.6 | 90.3, 49.7 | — |
| + coalescing | 7.85, 7.88, 7.89; 17.3 / 25.2 to 25.4 (10.0 to 10.1) | 27.3, 93.0 | 99.3, 99.3 | 50.6, 55.8 | **no** |
| + uniform bricks (on coalescing) | 7.87, 7.88; the same as coalescing alone: no water in his view to speak of (water 0.96 ms both) | 80.4 | **90.0, 89.9** | 50.9 | **yes** |
| + an empty 2 x 2 x 2 cube of bricks as one step (per-brick distance's cheap form) | not at a matching clock | — | 94.5 | — | **no** |
| + per-brick distance | not tried | | | | |
| + coarse on pyramid | not tried | | | | |
| + mirroring | not tried | | | | |

- **Coalescing** (plan step 1): `cuda_traverse`'s new `every_skip_finds_what_the_plain_loop_finds` held every hit's voxel, face, value and `t` to a plain copy of the loop (`traverse_world_t.plain`) bit for bit, with no hit differing, over the random world (-18 % steps), a checkerboard of single voxels (-24 %), Stage 01.3's terrain and lattice frames with and without the coarse pass (-26 % and -12 %), and slopes of 10 and 30 degrees from 1 to 2 blocks up (-20 % and -23 %, `skips_take_fewer_steps`). It left a 2 x 2 x 2 cube where a walk voxel by voxel would, not where `floorf` says, which is what made it exact at ties. But the card took 3 % more cycles at his spot, all of it in shade, most in AO (+0.9 ms) and the far field (+0.3 ms): the play kernels' registers did not change, so it is the work per step. Over the 3 % the plan allows, so it is gone, with the plain loop and both tests.
- **Uniform bricks** (step 3): the header is 24 bytes, `uniform_mask` one bit per full brick all one material (light aside: a lake is lit unevenly, and the walk asks only the material). `shade_water_through` crosses such a brick, or a section all one block, in one step, to the voxel of it a walk cell by cell would be in last and with the `t` it entered that voxel, so it leaves exactly where and when that walk would. `deep_ocean/orbit` 9 % faster at the same clock (primary 32.0 to 27.0 ms: the far field's own copy of the walk; shade 55.0 to 50.7), with its settled hash the same (A2B40714A47F8A28), and `render_feature_water`'s goldens the same to the bit. Every section is 8 bytes bigger and a uniform one 32 bytes, not 24; the bench's voxels in use did not move to the MiB on any orbit row (369, 1072 and 595 MiB).
- **A cube of empty bricks** as one step of 8 blocks, left where going brick by brick would leave it: exact on every scene above (-24 % steps on the random world, -23 % on the lattice), but 5 % slower than uniform bricks alone on `deep_ocean/orbit`. A distance per brick is the same idea with bigger boxes, bytes on the card and re-encoding on edits; with this result, and coalescing's, the step count is not what this card pays for here, and it was not built.
- **Hashes.** His spot's (3BE1DE0E27FDF83C) and `deep_ocean/orbit`'s are the same with every change. `jungle/orbit` and `plains/orbit` settle on a different picture from run to run with the phase 04 binary alone (plains 1C27CA02A70A76C4, then 8D2AB666C26E1602), so theirs say nothing either way; the times of those two rows are as noisy, for the same reason, and are given only as measured.
- **The coarse pass on the pyramid and mirroring** were not tried in this phase's time.

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

- `fc80bd50`: coalescing and uniform bricks tried, behind switches, with the exactness tests.
- `bcae4ad3`: coalescing and its plain loop dropped; uniform bricks kept with no switch.
- This file's Results.

**Verification**, after rebasing onto main at 150986d6: the debug build; `ctest -R '^(cuda_traverse|run_render_encode|run_render_upload|run_render_lod|run_gpu_bridge|render_feature_.*|run_render_frame|standards_lint|clang_format)$'`, 21 of 21 passing; the profile build. The measurements in Results were on phase 04's commit, before the rebase, which brought only terrain.

## Issues found

1. **`bench_world_slope --steps` fails at his spot**, with the phase 04 binary as well: "counting steps failed" after the measured frames, so the per-band step counts of phase 01 could not be had there, and steps are from `cuda_traverse` instead. Not looked into here.
2. **The card's clock is not steady enough for the orbit rows.** See Results.
