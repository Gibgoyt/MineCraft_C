# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10: Terrain, all Overworld terrains

**Status:** in progress   (updated 2026-11-02). Every check an agent can run has passed. Waiting only on Ahmed's visual review of the biome tour, the last part of the gate, and his answers to issue 8.

## Goal

Every Overworld biome and terrain type generates deterministically from a seed inside `z = -250 ... +500`. Structures such as villages and strongholds are a later plan.

**Wave:** W6.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-1-seed-noise.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-2-climate.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-3-density.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-4-water-features.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-5-caves-ores.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-6-biomes-surface.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-stage-7-features.md`

## Todo

- [x] Stage 10.1: Seed and noise integration
- [x] Stage 10.2: Climate fields
- [x] Stage 10.3: 3D density
- [x] Stage 10.4: Rivers, oceans and aquifers
- [x] Stage 10.5: Caves and ores
- [x] Stage 10.6: Biomes and surface rules
- [x] Stage 10.7: Terrain features
- [x] Gate, agent part: 1 and 16 workers give byte-identical columns (`whole_columns_of_every_biome_are_the_same_from_1_and_16_workers_and_sound`)
- [x] Gate, agent part: a biome-coverage test finds every biome (`every_biome_turns_up_in_every_world_searched`)
- [x] Gate, agent part: bench recorded (`bench/terrain/README.md`), at least 1000 chunk columns a second on 16 workers in every place
- [ ] Gate, Ahmed: visual review of the biome tour. The command is below
- [ ] Phase gate passed

## Verification

**Gate:** The same seed gives byte-identical columns with 1 and 16 workers; a biome-coverage test finds every biome; bench recorded; Ahmed visually reviews a biome tour.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    pgrep -a minecraft_c    # must print nothing

**The biome tour, for Ahmed.** It needs no display and no card:

    tools/test-or-build.sh cmake --build --preset release --target mc_bench_terrain_tour
    tools/test-or-build.sh timeout 600 build/release/bin/bench_terrain_tour /tmp/mc_tour

Open `/tmp/mc_tour/all.png`, then `/tmp/mc_tour/<biome>.png`. The tour's colours are its own, and it is not the game's renderer. To see the same ground in the game, on a text console, `./build/release/bin/minecraft_c --test-world`.

Results of the gate check on 2026-11-02, on a clean build of this branch, which holds the integration branch:

- `debug`: the build passed, and the whole suite passed, 61 of 61, `run_terrain_*`, `standards_lint`, `clang_format` and `compile_game_tables` among them.
- `release`: the build passed. `bench_terrain` exited 0: see issue 1 for the numbers. `run_terrain_*` printed the same digests as `debug`.
- `sanitize`: the build, `run_terrain_*`, `run_render_frame` and `run_game` passed, with no sanitizer report.
- `profile`: the build passed, the first time any stage of this phase built it. No test was run on it.
- `bench_terrain_tour` on `release`: exit status 0, 53 of 53 biomes drawn.
- `minecraft_c --headless --test-world` on `release`, at render distances 16 and 32: exit status 0, 7811 and 3209 columns made from real terrain, no warning that the pool was full, and `heap at exit: 11 reserved, 11 released, 0 bytes live`. I looked at the screenshots to see that the program draws this ground, with hills, savanna trees, a badlands peak, shores and sea. That is not the visual review, which is Ahmed's.
- One mutation of my own, not committed: `terrain_generate_box` changing a voxel by how many times it had been called. Both worker tests and the new test failed, and the new test on the bytes and on the world's limits.
- `pgrep -af "$PWD/build"` printed nothing after each run.

Not agent-runnable: `--test-world` through KMS, and the visual review.

## Commits

Appended as they land:

- `ef00f7d` Stream the real terrain into the game instead of the test world, size the section pool for it, measure the bench in the plan's chunk columns on 16 workers, and speed up a walk's surface and caves until the target is really met.

## Wiring outside this phase's paths

- `run/src/game/`, `run/include/game/index.h`: a column job holds `terrain_t` and a byte per voxel, and calls `terrain_generate_box`. `run/{src,include}/game/test_world/` is gone, and with it its row in `plans/REPO_STRUCTURE.md`.
- `run/include/render/index.h`: `RENDER_POOL_BYTES_PER_COLUMN`, issue 2.
- `tests/run/render/frame/`: its tests and golden pictures now are of real terrain.

## Issues found

1. **The target of 1000 columns a second was not met, and Stage 10.7 said it was.**
   - **Expected:** `bench_terrain` to show the plan's target met, as Stage 10.7's results said, with 22,725 columns a second on 1 worker and 193,174 on 16 in the slowest place.
   - **Actual:** that bench counted a column of 1 by 1 blocks, and the plan's column is a chunk's: "Chunk = 16x16 column", as in `run/src/world/column/`. It also held the 1-worker number against a target set for 16 workers. In the plan's unit, `jungle` made 89 on 1 worker and 755 on 16, under the target.
   - **Done:** fixed in `ef00f7d`. The bench now makes the very box the game asks for, 16 by 16 by 768, and gates on 16 workers. A chunk's walk asked for the surface's noise and biomes once per slice, and for the caves twice per slice. It now reads them once (`terrain_surface_tile_read`, `terrain_surface_fill_tile_box`) and cuts with the mask it has (`terrain_cave_cut_with_mask`). Each new path is tested against the old: `a_tile_read_once_lays_what_the_fill_lays`, `cutting_with_a_mask_is_what_carving_gives`. All the pinned digests held, so no world moved. Now, on `release`: `jungle`, the slowest, 125 on 1 worker and 1051 on 16, and an earlier run 127 and 1096. All places are in `bench/terrain/README.md`. The margin is 5% to 10%, so little is left for later work on a column.

2. **Nothing in the game used this phase's work.**
   - **Expected:** the phase's goal, that the world is generated from the seed.
   - **Actual:** `run/src/game/test_world/` filled columns with Stage 10.3's density and a surface of its own, so rivers, caves, ores, biomes and features were run only by tests, the bench and the tour. Stages 10.6 and 10.7 each recorded that nothing called them, for someone later.
   - **Done:** fixed in `ef00f7d`: `game` makes every column with `terrain_generate_box`, and `test_world` is deleted, as `plans/REPO_STRUCTURE.md` had said it would go with Stage 10.6. `the_columns_the_game_streams_are_what_terrain_generates` in `tests/run/render/frame/` holds that voxel for voxel. Real ground needs far more than the pool allowed: measured on 100 columns scattered over 1280 blocks, a mean of 216 KiB a column, 20.2 sections, at about 10.7 KiB each. `RENDER_POOL_BYTES_PER_COLUMN` went from 6 sections of 8192 bytes to 24 of 16384. A flight at render distance 32 held 726.9 MiB of slabs for 3209 columns, so the plan's "~100 MiB at R=32" is too low by about 7 times, and against the 6 GiB voxel budget R=128 will not fit as the plan guessed. For Stage 17.3. The golden pictures in `tests/run/render/frame/fixtures/` were made again.

3. **The gate's own test did not show what the gate says.**
   - **Expected:** a test that the same seed gives byte-identical columns with 1 and 16 workers.
   - **Actual:** `one_and_sixteen_workers_give_the_same_bytes_in_any_order`, the only one through features, covers 25 chunks round one jungle, 751 layers of the 768 the game asks for, and nothing shows 16 workers really took part: a fast first worker could have taken all of it. `one_and_sixteen_workers_give_the_same_bytes` in the biome test has a barrier and a worker mask, and stops before features.
   - **Done:** added `whole_columns_of_every_biome_are_the_same_from_1_and_16_workers_and_sound`: all 768 layers, a chunk of each of the 50 surface biomes, with a barrier and exactly 16 workers shown in the mask, also in the other order, and no heap call during it. Over those bytes it checks that all outside the world is air, bedrock lies at `z = -250` and nowhere else, and no fluid lies on or beside air, or water beside lava, inside a chunk: 179292 fluid voxels, 0 of those. It found no bug in the terrain.

4. **`tests/run/terrain/biome/index.c` was over the coding standard's split line, which the lint does not check.**
   - **Expected:** section 5: split at 1500 lines.
   - **Actual:** 1518 lines, after `ef00f7d` added a test to it. `standards_lint` passed, as it holds only the limit of 2000.
   - **Done:** moved `a_tile_read_once_lays_what_the_fill_lays` to `tests/run/terrain/feature/`, whose code is what walks a chunk in slices. Now 1476 and 1468 lines. The lint is not this phase's, so it still does not check the 1500.

5. **A comment in a test claimed what it did not test.**
   - **Expected:** `cutting_with_a_mask_is_what_carving_gives` to cover a mask over the bedrock, as its comment said.
   - **Actual:** caves stop at `TERRAIN_CAVE_FLOOR_TOP`, -242, so no real mask reaches the bedrock, and the check for it was not exercised.
   - **Done:** corrected the comment, and added `a_mask_cuts_only_rock_and_says_what_it_cut`, which holds each kind of block to it by hand.

6. **`run/src/terrain/README.md`, and Stages 10.6 and 10.7, still said nothing calls this.**
   - **Done:** the README now says who does. The two stage files keep what they said, with a note on each as to what closed it, as does 10.7's withdrawn bench number. Stages 10.1 to 10.5 say "nothing fills a real world yet" of their own time: issue 2 is the correction for all of them.

7. **No hollow code found.** Looked for, on every part of `run/src/terrain/`, `bench/terrain/` and `tests/run/terrain/`: `TODO` and the like, tests with no check in them, public functions with no caller, heap or arena use outside `_init`, structs with no size assert, README fields missing, and the kernel really being called (`terrain/noise` calls `noise3_batch8`). The one hit, "for now" in `run/src/terrain/density/README.md`, describes what the density leaves for hydrology. All 8 tree kinds, and every feature a biome's file asks for, are grown by `feature_grow` and were counted in the world. Pumpkin, in no biome's file, is Stage 10.7's issue 6.

8. **Open, for Ahmed: biomes that hold less than their names say.** Each is real code doing what its README says, and each is written in that stage's file. None is a gate check, so none blocks the gate, but the tour will show them.
   - `sunflower_plains` has no sunflowers, and `bamboo_jungle` no bamboo: no such blocks in `config/blocks/`. The pixels are in `textures/blocks/`. Both need a non-cube shape.
   - `deep_dark` has no sculk: its floors are cobbled deepslate and tuff. Pixels exist, and it is a plain cube.
   - Snow is whole `snow_block`, not a thin layer. Powder snow has pixels and no block.
   - `mushroom_fields` is the oddest stretches of low coast, not islands far out at sea: the density's `continental` spline gives no land there (Stage 10.6's issue 3).
   - `eroded_badlands` has no hoodoos: that needs the density to change.
   - Plants draw as whole cubes in the game until Stage 09.1, which has not started.
   - `birch_leaves` come from a pack texture that may look wrong: Stage 10.7's issue 9.

   **Why I did not add the blocks:** block ids are given in alphabetical order, so a new block moves the id of every block after it, and with them every pinned digest in `tests/run/terrain/`. `config/blocks/` is outside this phase's paths, other stages are being worked on in parallel with pins of their own, and two of the three need a shape from a stage not begun. Raising mushroom islands or hoodoos changes every world. So each is for Ahmed to ask for, before Stage 16.1 saves worlds.

9. **Shell commands were refused during this check.**
   - **Expected:** compound Bash commands, one with a value in a shell variable and one with a heredoc and a script, to run.
   - **Actual:** the worktree guard refused three as "too complex to verify that it stays inside the worktree".
   - **Done:** did as the messages said: plain commands, and scripts written with the Write tool. Nothing was bypassed.
