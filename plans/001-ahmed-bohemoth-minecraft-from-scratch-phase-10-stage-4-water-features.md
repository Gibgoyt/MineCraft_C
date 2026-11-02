# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 4: Rivers, oceans and aquifers

**Status:** done   (updated 2026-11-02)

## Goal

Rivers, oceans, beaches, aquifers and the lava level exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/hydrology/`
- `tests/run/terrain/hydrology/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement rivers, oceans and beaches in `run/src/terrain/hydrology/index.c`
- [x] Implement aquifers and the lava level
- [x] Register test `run_terrain_hydrology`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_hydrology$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Results on 2026-11-02: the build, `run_terrain_hydrology` (19 tests, 130088512 checks) and `standards_lint` passed, and `pgrep` printed nothing. The whole debug suite: 51 of 52, `clang_format` among those passing. The one failure, `run_types`, was failing before this stage: see issue 1.

Also run:

- `run_terrain_hydrology` on `release`: passed, with the same three pinned digests as `debug`, so `-O3 -flto` moves no bit. `run_terrain_hydrology`, `_density`, `_climate` and `_seed` on `sanitize`: all passed, with no sanitizer report. `profile` was not built.
- 17 mutations, not committed: the fill's shortcuts wrong at either end, the layers over a box ignored, the aquifer barrier removed and made too thin, each plug against the sea, the air and the lava removed, the lava level off by one, a neighbouring column's band ignored, cut stone left dry under the sea, the bed too shallow, a site moved, the level step changed, and the flood and lava thresholds each moved, the lava one twice. 15 of them failed at least one test when first run. The other two are issue 3: they survived the first version of the test, and fail since it was fixed.

What the test measured over 4 seeds, 262144 columns each, every 4 blocks over 2048 blocks a side:

| Land | Ocean | Deep ocean | River | Lake | Beach | Stony shore |
|---|---|---|---|---|---|---|
| 33.83% | 16.23% | 42.22% | 3.29% | 0.66% | 3.68% | 0.09% |

- **Rivers:** 95.4% of river columns join the sea, or leave the map along water. Mean width where a row crosses one: 31.9 blocks. A crossing at a slant reads wider than the river is.
- **Coast:** of 10416 edges between sea and anything else on the map, 6 go straight onto land with no shore between.
- **Blocks:** over 10766336 voxels at places of every kind: 71939 cut by rivers, 19473 sand, 4313 sandstone, 8790 gravel, 1837 clay, and none of the sand over anything open.
- **Flood:** opening 44789760 voxels from `z = -249` to `110`: 71.2% air, 17.9% water, 6.0% lava, 4.8% stone. 215238 of the lava voxels lie in aquifers above the lava level, and 227562 water voxels stand above the sea. Not one fluid voxel lies beside air or over air, or water beside lava.
- **By hand:** 7218 aquifer voxels recomputed from the README, looking through 125 cells where the code looks through 27, gave the same block every time.

36 chunks of 751 layers filled in 4.1 ms on 1 worker on `release`, against 2.9 ms for the density alone on the same ground. That is both stages together, and not the phase gate's benchmark.

No visual check was made here. Nothing draws this ground yet, and how the rivers and shores look is for the phase gate's biome tour. Widths and shares above are numbers only, and the river constants may want changing once they are seen.

## Commits

Appended as they land:

- `9109df9` Add rivers along weirdness's zero line, oceans, beaches, aquifers that leave nothing able to flow and the lava level at z = -230, tested voxel by voxel and to give the same bytes from 1 worker and 16.

## Wiring outside the owned paths

Each the smallest I could make:

- `run/include/terrain/index.h`, `run/src/terrain/index.c`, `run/src/terrain/README.md`: `terrain_init` starts the hydrology and `terrain_t` holds a pointer to it. Stage 10.3 had left no padding, so `terrain_t` grew from 128 to 192 bytes, with 56 of padding for the next parts.
- `run/include/main/index.h`: the size assert on `app_t`, from 3840 to 3904.
- `tests/run/terrain/climate/index.c`, `tests/run/terrain/seed/index.c`: three checks counted the fields in a new bank. They now add `TERRAIN_HYDROLOGY_NOISE_FIELDS`, as Stage 10.3 warned.
- `plans/REPO_STRUCTURE.md`: the `hydrology` row, the test row, and a row in the "where is it" table.

The running program now adds these fields at startup, on `TERRAIN_SEED_DEFAULT`. Nothing fills a real world from them yet outside tests.

## Decisions later stages need

- **`terrain_hydrology_fill_box` is now the box to start from,** not `terrain_density_fill_box`: rivers, water, sand, sandstone, gravel and clay are in it. It costs more than the density's, because it reads the lattice again.
- **For caves (10.5):** open the ground after the fill, set `opened[i]` on what you opened, and call `terrain_hydrology_flood_box`. It gives air, water, lava or stone. Stone is deliberate: it is what stops two bodies of fluid meeting. Do not pass it the fill's own open voxels. The lava level is `TERRAIN_HYDROLOGY_LAVA_TOP`, `-230`, and an opened voxel at or below it is always lava.
- **For biomes (10.6):** `terrain_hydrology_columns_area` gives each column's `kind`: land, ocean, deep ocean, river, lake, beach, stony shore. Sand and the rest are only on what this stage is sure of, and everything else is left as stone. `ground` is the height to use, as `height` does not know about rivers.
- **For features (10.7):** a lake kind already exists where low ground lies below the sea far from the coast. Water stands at `z = 0` in all of them, and there is no water above the sea on the surface yet.
- **The bank:** 15 of `TERRAIN_NOISE_FIELDS_MAX`'s 64 fields are used. `terrain_t` has 56 bytes of padding left, so 7 more pointers before it grows again.

## Issues found

1. **`run_types` fails on the integration branch, and did before this stage.**
   - **Expected:** the whole debug suite to pass.
   - **Actual:** `tests/run/types/index.c:914` and `:922` expect `heap at exit: 6 reserved, 6 released` and `leak at exit: 6 regions`, and the program reports 7. This stage reserves no region. Stage 10.3's issue 1 recorded the same two checks and the same 7 before this work.
   - **Done:** left alone and reported. The test is outside this stage's paths.

2. **`terrain_t` could not take another pointer.**
   - **Expected:** to add a field, as earlier stages had.
   - **Actual:** Stage 10.3 used the last of its padding and said so. `terrain_t` is `alignas(64)`, so it had to grow by 64.
   - **Done:** grew it, and `app_t` with it. See Wiring. If Stage 10.5 grew it on another branch, the merge of the two will conflict there and in `app_t`'s assert: keep both pointers, and let them share the padding.

3. **The first test was too weak on the aquifer's numbers.**
   - **Expected:** moving `HYDROLOGY_FLOOD_WET_FROM` from 0.3 to 0.31, or `HYDROLOGY_LAVA_FROM` from 0.4 to 0.41, to fail something.
   - **Actual:** both passed. The invariants hold for any numbers, and the pin covers one box of 48 by 48, which holds few cells near a threshold.
   - **Done:** added `an_aquifer_voxel_is_worked_out_as_the_readme_says`, which types the aquifer out again and looks through 125 cells. Both mutations now fail it. It also proves the README's claim that 27 cells are enough, on the voxels it samples.

4. **Limit on `hydrology/lava` first guessed at 0.48.**
   - **Expected:** about twice its standard deviation, as the README says of every field.
   - **Actual:** `raw_noise_measures_what_the_limits_were_chosen_for` printed 1.78 of them. It passed, as its bounds are 1.5 to 2.5.
   - **Done:** changed to 0.54, which measures 2.00, before the digests were pinned.

5. **Shell commands were refused while writing this stage.**
   - **Expected:** one Bash command could chain `python3` heredocs, file writes and a build, and `git -C .` would be accepted.
   - **Actual:** the worktree guard refused two: "too complex to verify that it stays inside the worktree", and "points git at a directory computed at runtime".
   - **Done:** did as the messages said: plain single commands, and edits through scripts in the scratchpad. Nothing was bypassed.

6. **The scratchpad is shared with another agent.**
   - **Expected:** it to be this session's alone.
   - **Actual:** the Stage 10.5 agent wrote its own `mutate.py` over mine there, with `root` set to its own worktree. Mine had already finished and restored its file. My final runs, with all three pinned digests the same on `debug` and `release`, show nothing of mine was left mutated.
   - **Done:** used other file names from then on. Noted for whoever runs parallel agents: a common script name in a shared scratchpad can run against the wrong tree.
