# 009-ahmed-bohemoth-game-engine-optimizations — phase 3: One pyramid of levels

**Status:** done but the gate (Ahmed reads the store size, the upload and VRAM numbers at render distance 64, and the edit latency)   (updated 2026-11-13)

Read first: the phase 00 file, then the master: sections 0 ("What Full detail means"), 1, 2.1, 4.3 and 4.4. Then:

- `run/src/render/lod/README.md`: the whole of it, the far field as it is;
- `run/src/render/encode/README.md` and `run/src/render/upload/README.md`: how a section reaches the card;
- `run/include/gpu_bridge/world_buffers/index.h`: the format;
- phase 01's step bands and full-detail sweep, which say how wide each level must be.

## Goal

The far field's levels and the blocks become one pyramid. Nothing yet draws from the new parts. When this phase is done:

- **A pyramid of levels 1 to K is planned by pixel error, not by full detail.** `render_lod_plan` takes the tuning file's `pixel_error`, the FOV, the picture height and the render distance. It returns every level from 1 up whose band (master 2.2) falls inside the render distance. Each level's radius is its band's far edge plus the margin. **Level 1 exists even under today's full detail**; nothing traces it yet.
- **Where level 0 is loaded, levels 1 and 2 are reduced from its blocks.** A level-k voxel is the 2×2×2 group of level k−1 voxels, by the half rule. Where level 0 is not loaded, levels are built from terrain samples as today.
- **An edit reaches level 1 and level 2 within 100 ms**, through the upload path that already carries it to level 0.
- **Every picture is unchanged:** golden pictures and settled `bench_world` hashes are the same bytes, because rays still use today's ring and far field.
- `bench/lod/` measures the reduction's worker time and bytes.

## Where this fits

**The vision.** Ahmed: "if there are is a 2x2x1 group of blocks, they should be rendered only as … the larger rectangle that it has created as a result of being one shape". In a ray caster that is a coarser voxel standing for a group of blocks (master 2.1). Phase 04 draws from these levels by distance; this phase makes sure they exist, nest, and stay true to edits, before any picture depends on them.

**What reads it:** phase 04 (rays walk the pyramid); phase 05 (the coarse pass tests coarse levels); phase 06 (shadow rays step coarse levels).

**What this phase must NOT build:**

- No change to any ray, kernel or picture.
- No removal of Full detail (phase 04).
- No change to how far columns are sampled from terrain. The far field's half rule, features shift and overlay stay.
- No card-side allocation. Slabs stay the host's (`world_buffers_t`, `run/include/gpu_bridge/world_buffers/index.h`).

## Ahmed's words

- ""Full detail" should never ever be a thing! if there are is a 2x2x1 group of blocks, they should be rendered only as triangle lines of the larger rectangle that it has created as a result of being one shape!!"

---

## Background, explained

### What a level is today

A far level is a whole `world_buffers` world whose voxel is 2<sup>shift</sup> blocks each way (`run/src/render/lod/README.md`, "A level is a world"):

- **Columns:** a level's column is 16 × 16 of its voxels, so a level-1 column covers 32 × 32 blocks, 2 × 2 chunks.
- **Height:** up to `RENDER_LOD_HEIGHT_BLOCKS` = 1536 blocks, which is 768 voxels at level 1, 48 sections.
- **How it is built:** one column of terrain is sampled at each voxel's middle, and the half rule (`render_lod_cells_from_column`, `run/src/render/lod/index.c:151`) keeps a voxel if at least half of what it covers in height is blocks.
- **What it skips:** caves and ores; features only up to `RENDER_LOD_FEATURES_SHIFT_MAX`.
- **Edits** come only through the save overlay, captured once a second.

`render_lod_plan` (`:61-84`) keeps a level only if its reach is past full detail, so at full detail 32 level 1 (reach 448) is never built.

### Why levels must nest

When phase 04 hands a ray from level k to level k+1 at a band edge, the coarser surface must be within ε<sub>k+1</sub> of the finer one, or the edge shows as a step. If level k+1 is made **from level k**, it nests by construction: every coarse voxel is a function of exactly the eight finer voxels inside it.

A terrain sample through the voxel's middle does not nest. It can miss a tree, or a mined hole, that the blocks hold. Where blocks are loaded, they are the truth, so near levels come from them.

### The reduction, worked

For a level-k voxel over the 8 voxels of level k−1 below it (2 × 2 × 2):

| Input (8 voxels) | Output |
|---|---|
| 8 stone | stone |
| 4 stone, 4 air | stone (half rule: at least half) |
| 3 stone, 5 air | air |
| 4 dirt below, 2 grass above, 2 air above | dirt? **No:** the most common counted material, ties to the higher voxel, so the 4 dirt win. A grass top seen from above would then be lost. So the rule is "the material of the highest counted voxel that has air above it, else the most common". That gives grass, as a coarse surface should show its top |
| 4 water, 4 air | water (water counts, as `render_lod_counts` counts `BLOCK_SHAPE_FLUID`) |
| Torches, plants, doors, stairs and slabs | Counted as `render_lod_counts` counts them: slabs and stairs count, cross and torch shapes do not |
| Light | The brightest stored light of the counted voxels (faces show the brighter side, `cuda/src/shade/README.md`) |

A uniform section reduces to a uniform section (24 bytes), and an empty one to nothing.

### Where the reduction runs: beside the encode

When a section changes, `run/src/render/upload/` takes a snapshot and a worker encodes it (`render_encode_section`, `run/src/render/encode/`). That worker holds all 4096 blocks of the section at that moment. Reducing them to the section's **octant** of its level-1 section (8 × 8 × 8 voxels of 2 blocks: one eighth of a level-1 section, 16³ of 2-block voxels = 32³ blocks = 2 × 2 × 2 level-0 sections) costs one pass over blocks the worker already has.

The gpu thread keeps an **octant store**:

- per level-0 section, its reduced octant: 512 materials and 512 lights, or a single value for a uniform octant;
- when any of a level-1 section's eight octants changes, the level-1 section is assembled from them, encoded with `render_encode_section` and sent through level 1's `world_buffers`;
- assembling a level-1 section also reduces it to its octant of level 2, so level 2 follows where it overlaps loaded blocks.

An edit thus reaches level 1 in the same upload round that takes it to level 0. The upload budget counts the bytes.

**Octant store size, worked.** At E = 2 and 1080p, level 0 reaches about 27 chunks (master 2.4): π × 27² ≈ 2290 columns. With about 10 non-empty sections each, that is 22900 octants, at most 1.5 KiB each, so at most **34 MiB of host memory**. Uniform octants (buried stone, open air) are 3 bytes. The store lives in `render`'s host arena, reserved for the table's largest radius.

### Level widths, worked

A level's radius in its own columns is ⌈band far edge / (16 s<sub>k</sub>)⌉ + margin:

| E | Level 1 band | Level 1 radius (columns of 32 blocks) | Side | Pool at 64 KiB a column |
|---|---|---|---|---|
| 2 | 386 – 771 | 25 + 2 = 27 | 55 | 189 MiB |
| 4 | 193 – 386 | 13 + 2 = 15 | 31 | 60 MiB |
| 8 | 96 – 193 | 7 + 2 = 9 | 19 | 23 MiB |

The level's square must reach its far edge, so these radii are larger than today's `RENDER_LOD_RADIUS_COLUMNS` = 16 cap allows at E = 2. The cap becomes the tuning file's `radius_columns_max` (raised to 32) and `world_buffers`' `TRAVERSE_COLUMNS_WIDE_MAX` (2047) is far above it. The pool budget is `gpu.budget.voxels_mib`, 6144 MiB; `bench_world` saw at most 2498 MiB used. Phase 03 records the real bytes.

## Design

### `run/src/render/lod/`

- **The plan:** `render_lod_plan(const render_lod_bands_t *bands, uint32_t render_distance_chunks)`. `render_lod_bands_t` holds d<sub>k</sub> for k = 1..K, worked out by a new pure function `render_lod_bands(pixel_error, near_guarantee, half_fov_tangent, height_pixels)` from master 4.4. The old signature stays until phase 04 removes full detail, as a wrapper that bands as today.
- **Sources per column:** a level column whose every block column is in the level-0 table and ready is built from octants; any other is built from terrain samples, as today. A column changes from one to the other when level 0's window moves; it is rebuilt, and the old one shows until the new one comes, as today's replan does.
- **The octant store and assembly:** a new part, `run/src/render/lod/octant/`, Arena tier, with its README. The gpu thread owns it. The upload's encode job writes the octant into job memory beside the encoded section, and the gpu thread copies it into the store when the job is collected.

### `run/src/render/encode/`

`render_encode_section_octant`: the reduction for one section into its octant, pure, beside `render_encode_section`. The same function reduces an assembled level-k section to its level-k+1 octant.

### Tuning (`compile/config/render/lod/index.yaml`)

- `pixel_error` (2.0 pixels);
- `near_guarantee` (16 blocks);
- `level_count` (6);
- `radius_columns_max` (32);
- `octant_levels` (2: how many levels above 0 are reduced from blocks).

## Owned paths

| Path | Change |
|---|---|
| `run/src/render/lod/`, `run/include/render/lod/index.h` | Bands, the plan by bands, sources per column |
| `run/src/render/lod/octant/` (new), `run/include/render/lod/octant/index.h` | The octant store and assembly |
| `run/src/render/encode/`, `run/include/render/encode/index.h` | `render_encode_section_octant` |
| `run/src/render/upload/` | Octants carried from the encode job |
| `run/src/render/` (`render_open_lod`, `render_replan_lod`) | Sizes for the wider level 1 and the store |
| `compile/config/render/lod/index.yaml` | The keys above |
| `tests/run/render/lod/`, `tests/run/render/encode/`, `bench/lod/` | Tests and measures |

## Steps

1. **`render_lod_bands`, pure, tested on paper first.** *Why:* every later phase reads it; FOV and resolution must move it exactly.
2. **`render_encode_section_octant` and its table test.** *Why:* the half rule and the grass-top rule are where a coarse level goes wrong.
3. **The octant store and assembly, with a test that an assembled level-1 section equals one reduced directly from its 32³ blocks.** *Why:* two ways to the same bytes is the proof.
4. **The plan by bands; level 1 built; sources per column.** Pictures checked unchanged after this step.
5. **Edits:** a block mined at 400 blocks changes level 1 within 100 ms, in a test on the game's threads (`run_render_distance_live` has the machinery).
6. **Measure:** `bench/lod/` gains octant rows (µs per section, bytes per level-1 column); `bench_world` full run, hashes unchanged; VRAM per level recorded.

## Todo

- [x] `render_lod_bands` with its paper test (FOV 70 and 30, 1080 and 540 rows, E 1 / 2 / 4 / 8, the 16-block floor)
- [x] `render_encode_section_octant` with its table test (every row of "The reduction, worked")
- [x] `run/src/render/lod/octant/` with README; assembled equals direct, byte for byte (at `run/src/render/octant/`: Issues found 1)
- [x] `render_lod_plan` by bands; level 1 planned and built; sources per column chosen by level 0's table
- [x] Tuning keys `pixel_error`, `near_guarantee`, `level_count`, `radius_columns_max`, `octant_levels`
- [x] An edit at 400 blocks reaches level 1 and level 2 within 100 ms (test)
- [x] Octant store bytes and upload bytes recorded; within the master's 1/7 more
- [x] Every golden picture and settled hash unchanged
- [x] VRAM per level at render distance 64, E = 2, recorded

## Tests

### `tests/run/render/lod/` — ctest `run_render_lod`, no card for the pure parts

- `bands_follow_the_pixel_error`:
  - E = 2, FOV 70, 1080 rows: d<sub>1</sub> = 385.6 ± 0.5, d<sub>2</sub> = 771.2 ± 0.5;
  - E = 8: d<sub>1</sub> = 96.4;
  - E = 16: d<sub>1</sub> = 48.2;
  - E = 64: d<sub>1</sub> = 16, the floor (12.05 would be under it).
- `bands_follow_the_fov_and_the_rows`: FOV 30 doubles-and-more d<sub>1</sub> (tan 35° / tan 15° = 2.61×); 540 rows halves it.
- `the_plan_keeps_every_level_inside_the_render_distance`: at 64 chunks and E = 2, levels 1 and 2 only; at 128, levels 1 to 3.
- `assembled_level_1_equals_direct_reduction`: random sections, several seeds; byte-identical encodings.

### `tests/run/render/encode/` — ctest `run_render_encode`

`the_octant_reduction_table`: each row of the table above, as an 8-voxel input and the expected voxel.

### On the game's threads — `run_render_distance_live` (extended)

`an_edit_reaches_level_1_within_100_ms`: mine at 400 blocks, read level 1's host table until the voxel changes; time ≤ 100 ms.

### Unchanged

`render_feature_lod`, `render_feature_*`, `run_render_frame`: every golden picture the same bytes.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_render_lod|run_render_encode|run_render_upload)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_.*|run_render_frame|run_render_distance_live)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_lod
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 phase 03: pyramid built, not drawn"
    pgrep -af "$PWD/build"                                                       # must print nothing

## Gate

Pictures are unchanged, so there is nothing new to see. Ahmed reads:

- the octant store's size;
- the upload and VRAM numbers at render distance 64;
- the edit latency.

He says go for phase 04.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Level 1 at E = 2 is wide (55 columns, 189 MiB) | Within the voxels budget; recorded. If VRAM is short, `radius_columns_max` caps it and the band past the cap uses level 2 |
| The grass-top rule makes coarse ground the wrong colour somewhere (snow on dirt, sand on stone) | Same rule as a surface seen from above; checked on the snowy-peak and desert bench shots in phase 04 |
| A column switching from terrain-built to octant-built pops | Old column shown until the new one arrives, as today; phase 04's dither hides the rest |
| The octant store races the encode jobs | The gpu thread is the only writer; jobs write only their own memory; as the far field's jobs do today |
| Terrain-built and octant-built voxels disagree at the boundary between them | Expected (sampled against exact); phase 04's error test measures it |

## Commits

Appended as they land:

- `023514d4` Build plan 009's pyramid of levels beside the far field, drawn by nothing yet: bands and the plan by pixel error, `render_encode_section_octant`, the octant store in `run/src/render/octant/` fed by the upload, levels 1 and 2 made from the blocks where loaded and from terrain elsewhere, an edit in both within 100 ms, the tuning keys, and their tests; every golden picture unchanged.
- this commit: bench_lod's octant rows in its README, `bench_world`'s history line `009 phase 03: pyramid built, not drawn`, and the measurements and verification here.

**Verification, as run (2026-11-13), on `main` at `b8b6d822` (unmoved):** `pgrep` for Ahmed's game printed nothing before every card run. `cmake --build --preset debug`: clean, lint clean. `run_render_lod`, `run_render_encode`, `run_render_upload`: pass. Every `render_feature_*` (11), `run_render_frame`, `run_render_distance_live` (both tests), `run_settings_session`, `run_types`, `cuda_lod`: pass, so every golden picture is the same bytes. `standards_lint`, `clang_format`: pass. `cmake --build --preset profile`: clean. `bench_lod`: exit 0, the table above. `bench_world --record ... --label "009 phase 03: pyramid built, not drawn"`: exit 0, 14 of 14 settled hashes as before. `bench_world_slope --only night-64`: `C28033CF2426D1B5`. `run_render_distance_live`'s first test reports, in this debug build only, `open_32` at 18.8 s and `rd_64_again` past its marks, as a debug build's terrain does (its README: deadlines are held only in an optimized build); it passed. `pgrep -af "$PWD/build"` printed nothing at the end.

## Issues found

### 1. Changed from the plan, and why

- **The octant store is `run/src/render/octant/`, not `run/src/render/lod/octant/`.** At depth 3 a directory is the Borrow tier, which `standards_lint` forbids to allocate (`tier_borrow_alloc`), and the store takes its tables and blocks from an arena. At depth 2, beside `render/lod` and `render/upload`, it is the Arena tier; both siblings name it in their READMEs, as `include_sibling` asks. Header `run/include/render/octant/index.h`.
- **The pyramid is a second `render_lod_t`, beside the far field, not the far field's own levels.** Pictures must not change, and today's far field (planned by full detail, terrain-sampled, passed by inside full detail) is what rays read: so its levels stay exactly as they are, and the pyramid is a separate set of levels that `render_lod_scene` never hands to a kernel. It is owned by `render` (`render_pyramid_t`, its own region, `render->owned_pyramid`), with the World lifetime of the map, not the frame's: a resize keeps it. The cost is that a shift both have (2 at render distance 64) is built twice where it is sampled from terrain; phase 04 replaces the far field with the pyramid and that goes.
- **`render_lod_plan_by_bands`, not a new signature of `render_lod_plan`.** C has no overloading; the old `render_lod_plan(full_detail, render_distance)` stays as it is for the far field until phase 04, and the pyramid uses the new name.
- **`radius_columns` stays 16 for the far field; `radius_columns_max` (32) is a new key for the pyramid alone.** Raising `radius_columns` would widen today's far field and change pictures.
- **Assembly on the gpu thread, column by column, not section by section.** When an octant changes, the whole level 1 column over it is put together from the store and sent through the far field's own `render_lod_column_send` (a column's sections are written whole there). bench_lod: 0.18 ms a column on the mean, 0.31 at worst, release; `octant_columns_per_frame` (8) caps a frame. An edit's column is one of them.
- **Where level 0 is loaded is the card's own table, at its generation.** A column of level 1 is made from octants when its four chunks are in the map's table, ready, at the generation the store holds, and complete. Missing sections are air, as on the card, and each that comes later marks the level 1 column again, so it converges as loading does. Above level 1 the source is the pyramid's level below, made from octants at the generation it was sent with; a level 1 column made again from terrain makes the level 2 column over it again.
- **The grass-top rule, made exact:** a kept voxel's block is the most common counted block of the top layer of its 2 x 2 x 2 group, or of the bottom layer if the top has none; a tie goes to the first in index order. Light is sky and block light each at their most over the counted voxels. Every row of the table is in `the_octant_reduction_table`.
- **Three budget keys more** (`compile/config/render/budget/index.yaml`): `octant_store_mib` (320: host memory for octant blocks), `octant_columns_per_frame` (8), `octant_pool_kib_per_column` (192: what a level 1 column made from the blocks gets of its level's pool, halved each level up). A first run with 96 MiB of store and the far field's 64 KiB a column ran out of both (65474 of 65536 blocks, and 1681 pyramid columns with no room): a level 1 column from the blocks holds the caves and ores terrain sampling skips, 132 KiB on the mean against 34. `render_lod_pyramid_pool_bytes` gives the columns over the blocks the larger share and the rest the far field's.
- **Its workers wait for the far field.** The pyramid sends no column to a worker while the far field rays read is not idle (`render_lod_jobs_hold`), so it cannot slow what is drawn; octant columns come regardless. `game_settle` waits for the pyramid too, so a bench row is measured with nothing of it still building.
- **The edit test runs at 1920 x 1080 and render distance 64.** At the file's 640 x 360 the bands are three times nearer (level 1 from 128 blocks, its square only 11 columns), so 400 blocks is past level 1; and level 2's band starts at 771 blocks at 1080 rows, so render distance 32 has no level 2. It puts a 4 x 4 x 4 cube of stone in the air rather than mining one block: one block cannot change a voxel of 2 by the half rule.
- **A change of field of view alone does not replan the pyramid** until the next change of the view (render distance, full detail, scale). Replanning it there would stop the threads and take memory, which `run_settings_session` holds a field-of-view change to never do. Phase 04 works the bands out every frame.
- **`render_encode_block_counts` reads the block's id** (`& BLOCK_STATE_ID_MASK`), where the far field's `render_lod_counts` read `block_defs[state]`: a voxel of the world holds stairs' facing above the id, and an unknown block (`run_render_frame`'s missing-texture test puts 60000) read past the table. An unknown id counts, as it is drawn as a cube. The far field's own samples never hold either, and every golden picture is the same.

### 2. For phase 04

- The pyramid's VRAM is the price: at Ahmed's settings about 464 MiB more of voxels while the far field also stands. When phase 04 drops the far field (2 levels, about 130 MiB) and full detail goes to 27 chunks, most of it comes back. The game's own budget (6144 MiB) holds it; the bench's 3584 does not at render distance 128 and 512, and neither does the reference mode at 64 (5639 MiB plus the pyramid), so phase 04's comparisons at 64 need the pyramid's pool counted or the reference without it.
- Level 1's octant columns hold caves and ores (four times the bytes of a terrain one). Phase 04's seam test will see octant-built and terrain-built columns disagree at level 0's edge, as the risk table says.
- The bands are worked out only when the view changes (Issues 1): phase 04 moves them to every frame.

### 3. Measured

- **The edit** (`an_edit_reaches_level_1_and_level_2_within_100_ms`, debug build, real threads): a cube of stone at 392, -8, 28, 400 blocks east of the eye, in level 1 **15.6 ms** after the edit and in level 2 **17.1 ms**. Level 1 had it at 12.4 ms in the run at render distance 32.
- **For the gate, at Ahmed's spot** (`bench_world_slope --only night-64`, profile, render distance 64, full detail 32, 1920 x 1080, E = 2, 70°; card at **148 MHz**, its floor): the picture is **`C28033CF2426D1B5`, phase 01's**, and **6.43 Mcycles** a picture (43.30 ms, p99 43.70) against phase 01's 6.46 at the same 148 MHz: the pyramid costs the frame nothing once settled. Bands: level 1 from 385.6 blocks, level 2 from 771.2.
  - **VRAM per level:** level 1, 27 columns each way (55 x 55), **354 MiB of pool, 195.8 MiB of slabs held** (39027 sections); level 2, 18 each way (37 x 37), **108 MiB of pool, 51.0 MiB held** (8727 sections). Tables 1.5 and 0.7 MiB. The bench's voxels went from 2776 MiB (phase 01) to **3240 MiB**.
  - **Octant store: 137.8 MiB of host memory** (91966 non-uniform octants live of the 218453 room, 0 refused), against the phase file's estimate of 34 MiB: real ground has 14 to 41 non-uniform sections a chunk with caves, not 10, and full detail 32 holds more columns than E = 2's 27 chunks would.
  - **Columns:** 2842 of level 1 and 445 of level 2 made from octants (2.37 s of the gpu thread in all, through the fill); 6738 pyramid columns built in all, 146.9 s of workers' time for the ones from terrain; none without room.
  - **Upload bytes:** 320.7 MiB sent from octants in that run. bench_lod: a level 1 column from the blocks is **12.7 %** of the bytes of the four chunks under it (132.2 KiB against 1043.5 on the mean of six places), level 2 an eighth of that again: **about 1/7 more than level 0 moves**, the master's figure. The worker's part, a section's octant, is 7.95 µs on top of its encode; putting a level 1 column together and encoding it is 201.5 µs on the gpu thread (profile), the level 2 octants after it 177.7 µs.
- **`bench_world` full run** (profile, `009 phase 03: pyramid built, not drawn`, card 1575 MHz and 84 °C at the start): **all 14 settled pictures the same hashes as `009 phase 01`**. Voxels at render distance 32 went from 2650 to 2901 MiB (pyramid level 1 there, 18 columns each way). At render distance 128 and 512 the bench's 3584 MiB budget refused the pyramid's pool, it logged so, and the rows ran without it, the same as before.
- **That world** (render distance 64, full detail 32, 1920 x 1080, E = 2, the edit test's): pyramid level 1 27 columns each way, **354 MiB of pool, 157.4 MiB of slabs held**; level 2 18 columns each way, **108 MiB of pool, 42.8 MiB held**; 2318 level 1 and 553 level 2 columns made from octants, 0 columns with no room. Octant store: **105.8 MiB** host (70090 blocks of 1536 bytes live, 0 refused).
