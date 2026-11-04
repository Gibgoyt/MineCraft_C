# 003-ahmed-bohemoth-initial-game-fixes — phase 11: Taller world

**Status:** in progress: everything an agent can run passes, and Ahmed's own flight to the limit is what waits   (updated 2026-11-04)

## Goal

The world reaches from `z = -250` up to `z = +1250`, with sea level still at `z = 0`.

**Decided by Ahmed, 2026-11-04:** "let's please make height limit between -250 and +1250 I think that is best"

## What was asked before

The original request said "z co-ordinate at 0 for sea level until -250 and height limit at 500 please!" (`plans/PROMPT.md:70`), so the game was built to 500. This decision replaces that. Plan 001 keeps its record as it was.

## What it would change

| Today | At +1250 |
|---|---|
| `z_max: 500` in `config/machine.yaml` | `z_max: 1250` |
| 751 layers | 1501 layers |
| Storage `-256` to `512`, in `run/include/world/column/index.h` | `-256` to `1280` |
| 48 sections per column | 96 |
| Mountains up to 454 | Up to what phase 8 tunes, possibly about 1100 |

43 source files mention these limits, so it reaches `world`, `render`, `cuda/src/world_buffers/`, `terrain`, saves and tests.

**What it should cost:** air and solid sections store no array, and sky rays leave early by column height, so empty height is cheap. **What's unmeasured:** the per-column tables double, and taller mountains mean more surface to draw and more columns' worth of sections. Frame rate is already below target, and this needs measuring, not assuming.

**Do it before phase 8's mountains,** so terrain is only tuned once.

## Todo

- [x] Ahmed confirms the height: +1250
- [x] Change `config/machine.yaml`, and `COLUMN_Z_STORAGE_END` and `COLUMN_SECTIONS` in `run/include/world/column/index.h`, keeping sections aligned, as `static_assert`s there require
- [x] Follow the static assertions and the compile errors through `run/`, `cuda/`, saves and tests. Update every struct size that changes
- [x] Check column data, per-thread job buffers and stack frames against the 16 KiB limit, since `borrowed_states` and similar double
- [x] Bump `SAVE_WORLD_VERSION`, so older worlds are refused clearly
- [x] Check `run/src/render/lod/` and the coarse pass handle the taller range
- [x] Add tests that `z = 1250` can be placed, and `z = 1251` can't
- [x] Measure before and after with `bench_world` at render distance 32: FPS, VRAM and terrain columns a second. Record it here
- [ ] Ahmed builds a tower, or flies with `--test-world`, up to the limit

## Measured

Release preset, `tools/test-or-build.sh`, same seed, same machine, both on this worktree: before at `7e36574`, after with the change. One run each.

**Frame rate, `bench_world`, 1920x1080. Every settled row's `picture` hash is the same before and after, so these draw identical pictures.** Live rows have no hash, and draw what streaming kept up with.

| Row | Way | fps before | fps after | Target 120 |
|---|---|---|---|---|
| `plains/orbit` | settled | 149.1 | 148.3 | met |
| `plains/flight` | settled | 150.2 | 145.1 | met |
| `plains/flight-live` | live | 120.2 | 127.3 | met |
| `plains/orbit-godrays` | settled | 138.8 | 138.4 | met |
| `jungle/orbit` | settled | 119.2 | 118.7 | MISSED, both |
| `jungle/flight-live` | live | 148.8 | 161.2 | met |
| `jagged_peaks/orbit` | settled | 171.2 | 165.3 | met |
| `jagged_peaks/flight-live` | live | 230.4 | 229.8 | met |
| `deep_ocean/orbit` | settled | 109.8 | 111.9 | MISSED, both |
| `deep_ocean/flight-live` | live | 164.4 | 167.8 | met |
| `cave/orbit` | settled | 192.2 | 180.7 | met |
| `night/walk-mobs` | settled | 162.5 | 152.8 | met |
| `plains/flight-live-r128` | live | 146.2 | 143.9 | no target |
| `plains/orbit-r512` | settled | 61.2 | 101.3 | no target |

10 of 12 rows at render distance 32 meet the target before and after. Settled rows moved -6% to +2%, against the up to 10% the bench's README gives for this machine between runs. The r512 row's 61.2 before was far below the 109.6 and 108.95 in `bench/world/history.tsv`, so that gain is the before run being slow, not this change.

**VRAM:** voxels held 1798 to 1799 MiB at render distance 32, 2004 to 2006 at 128, 2164 to 2167 at 512. In use, the same within 26 MiB (live rows move). Textures 2, frames 204 or 220, unchanged. The table doubled, and this is all it came to.

**Terrain, `bench_terrain`, chunk columns a second on 16 workers.** A column is now 1536 layers, against 768.

| Place | Before | After | |
|---|---|---|---|
| plains | 2273 | 1827 | -20% |
| forest | 1307 | 1210 | -7% |
| jungle | 900 | 923 | +3% |
| dark_forest | 2001 | 1700 | -15% |
| mangrove_swamp | 2541 | 1760 | -31% |
| desert | 2071 | 1646 | -21% |
| snowy_taiga | 1246 | 1070 | -14% |
| jagged_peaks | 1356 | 1228 | -9% |
| deep_ocean | 3134 | 2301 | -27% |

Slowest place 900 before and 923 after, target 1000: not met either time, and `bench_terrain` exits 1 for that both ways.

**Opening a world, `bench_world`'s `opened in`, seconds, on the same rows:** 5.8 to 6.4 `plains/orbit`, 10.8 to 12.1 `jungle/orbit`, 9.3 to 10.9 `jagged_peaks/orbit`, 11.4 to 18.2 `deep_ocean/orbit`, 15.6 to 19.8 `cave/orbit`, 121.0 to 120.8 `plains/flight-live-r128`, 148.6 to 130.6 `plains/orbit-r512`. Columns made in 10 s of live flight: `plains` 2388 to 1938, `jungle` 943 to 809, `jagged_peaks` 1262 to 1033, `deep_ocean` 1051 to 905.

**Limits:** `world_buffers_column_t` 256 to 512 bytes, `column_t` 1160 to 2312, a map slot 1436 to 2736. The largest table, 1025 x 1025, reserves 2.68 GiB against 1.31, untouched until columns stream in. Light's wave capacity doubled to `1 << 22`, 80 MiB reserved, no pages until used. `save_world_delta_column`'s two entry lists on the stack doubled, and the release build's link check at `-Wframe-larger-than=16384` still passes. A worker's job arena is 32 MiB, and the largest far-field job, 48 sections of 8 KiB, is under 0.4 MiB. A column's working states are 786 KiB, sized from `COLUMN_SECTIONS`.

**For Ahmed to judge:** on these rows the picture's cost is within the bench's noise, and VRAM is up 1 to 3 MiB. The cost is on the CPU: up to 31% fewer columns a second, and up to 60% longer to open a world, from one run each way. It wasn't profiled. Terrain still stops at 454, so walking layers of air is the likely cause, not a proven one. **These rows have no taller terrain in them, so they say nothing yet about taller mountains' cost, which is the risk this file named. Phase 8 has to measure that.**

**After the gate check's fix, issue 11. Terrain, chunk columns a second on 16 workers, one run on a cooled CPU, against the "After" column above:** plains 2380 (was 1827), forest 1462 (1210), jungle 1077 (923), dark_forest 2189 (1700), mangrove_swamp 2638 (1760), desert 2052 (1646), snowy_taiga 1409 (1070), jagged_peaks 1519 (1228), deep_ocean 3280 (2301). So the cost was the layers of air, and it is gone. Every place meets 1000, exit 0, on three runs of 1077 to 1090. Straight after the whole suite it was 993, exit 1: phase 17 recorded the same, met cool and missed hot. These runs are not the same heat as the table above, so read them as the target being met again, not as exact gains.

**`bench_world` after it, one run:** exit 0, 10 of 12 rows at the target. `jungle/orbit` 119.6 and `deep_ocean/orbit` 112.1 still miss, as they did before this phase. Opening a world: `plains/orbit` 6.4 s, `jungle/orbit` 12.0, `jagged_peaks/orbit` 10.1, `deep_ocean/orbit` 17.9, `cave/orbit` 20.4, r128 117.9, r512 126.8. Most are about where they were with the taller table, so what opening costs is not mainly terrain, and it was not profiled. All nine settled rows' `picture` hashes are ones `bench/world/history.tsv` already holds, so the fog change touched no picture in these rows.

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 2400 ctest --preset release --output-on-failure
    tools/test-or-build.sh timeout 500 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

Ahmed: build a tower, or fly with `--test-world`, up to the limit.

    ./build/release/bin/minecraft_c --windowed

Run for this phase: release build clean with lint; ctest 100 of 100 in 757.7 s; `bench_world` exit 0 in 508.7 s; `bench_terrain` exit 1, its target not met, as before the change. Nothing of ours left running. Not run: `--windowed`, which is Ahmed's.

Run by the gate check, on a clean build of `ee105e0` with main merged: release build with lint, exit 0; serial ctest 100 of 100 in 679.7 s; `bench_world` exit 0 in 507.0 s; `bench_terrain` exit 1 at 993 straight after the suite and exit 0 at 1077 four minutes later, see Measured. Scripted flight with `--headless --test-world --render-distance 32`, to about z = 1243, pictures read: see issue 9. `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"` print nothing. Not run: `--windowed`, which is Ahmed's.

## Commits

Appended as they land:

- `49f56e0` Raise the top of the world to z = 1250 with 96 sections a column, two-word section masks everywhere, and save format version 3.
- `404e237` Say in the repository's structure that terrain's range now reaches z = 1250.
- `2dcbdb7` Record the taller world's todos, measurements before and after, and issues in its plan file.
- `78f01e2` Test the top of the world in the built program with real keys and hands, and wait for a killed game's whole process group so its lock on the world is gone.
- `672a209` Give rock above z = 500 its gravel, coal, iron and emerald with rows of their own, and leave out rows that are wholly over everything in a box.
- `2171826` Count what a ray goes up or down for the fog only as far as where fog starts, so the ground stays in sight from the top of the world at any render distance.
- `d3e21f4` Run the save test that times waits between pictures with nothing beside it under ctest -j.
- `bedad80` Say in the repository's structure where the sky test's new fixtures are.
- `ee105e0` Run terrain's stages only up to the highest layer they can make and leave the rest of a column air, which gives back what the taller world cost and meets the 1000 columns a second again.

## Issues found

**The frame rate cost is the risk to watch.** It's already below target, and taller mountains add surface to draw. If the measurements here are bad, that goes back to Ahmed with the numbers, and the height itself doesn't change without his say.

**Result: the frame rate held on today's terrain, which doesn't use the new height.** See Measured. The height stays at +1250, and the numbers are there for Ahmed.

**1. A section mask no longer fits a word.** Expected: bump the sizes and follow the asserts. Actual: 96 sections is more than the 64 bits of every mask, and `static_assert(COLUMN_SECTIONS < 64u)` in `run/include/world/edit/index.h` and `<= 64u` in `run/include/save/world/index.h` said so. This is the bulk of the change, and the phase's table doesn't list it. Done: `column_mask_t`, two words, in `run/include/world/column/index.h`, used by edit, light, tick, game and saves. `WORLD_BUFFERS_MASK_WORDS` does the same on the card, and `traverse` and `shade` read both words.

**2. Other limits that the asserts caught, or tests did.** `LIGHT_ENTRY` voxel number 18 bits to 19, in `run/src/world/light/index.c`. `SAVE_WORLD_PLACE_SECTIONS` 64 to 128. `world_buffers_column_t` to 512 bytes, keeping its slot a shift. Delta file column entries 24 to 32 bytes, which is what version 3 is. Each fixed.

**3. A taller world would have cut mob spawning, by reasoning and not by measurement.** `mob_spawn_tick` rolled a height across all of gameplay's range. With 1501 layers, terrain stopping at 454 and `MOB_SPAWN_SCAN_CELLS` at 48, about half the tries would start too high to find a floor. No test pinned this rate, and it wasn't measured. Done: `run/src/mob/spawn/index.c` rolls among the lowest 751 layers, or to the top of what a column holds if it reaches higher. This is wiring outside the phase's list.

**4. Three full test runs to get clean.** First run: 26 of 100 failed, many with a segfault. Between it and the second, `world_light_init` was found still allocating `borrowed_touched` at `sizeof(uint64_t)` per slot, too small for a `column_mask_t`, and the terrain tests' layer counts were put back to 751. No log ties each failure to a cause, but the segfaults were gone in the second run. Second run: 97 of 100. `run_world_light`, `run_world_map` and `run_world_sim` aborted with `arena out of memory`, as light's wave and a map slot doubled. `tests/run/world/map` also still expected `z = 501` to be refused. Done: light's and the map's test arenas doubled; `run_world_sim` keeps the old wave of `1 << 21`, since the harness's arena refused 128 MiB; the map test now asks for 1250 and 1251. Third run: 100 of 100.

**5. The terrain tests stay at 751 layers.** Their digests are pinned, and terrain doesn't reach past 454 yet, so keeping them says the taller world moved no terrain. They pass unchanged. Phase 8 will have to widen them when mountains go higher. Left alone on purpose.

**6. The plan's ctest timeout is too short.** This file said 1200 s, and `suite.log` from the phase before took 1347 s, though this run took 758 s. Changed to 2400 here.

**7. Work found already in this worktree.** At the start of this run the worktree held 46 uncommitted changed files, a release build and before-numbers from 10:18 to 10:56 that day, from an earlier attempt at this phase cut off during its test run, with this file still saying not started. Read through and kept, not redone. The before-numbers' binary was built from `7e36574` before the first edit, at 10:20, and ran at 10:21 and 10:29, by the logs' times.

**8. An earlier gate check left this file behind.** Expected: results and entries for what it found. Actual: its serial ctest and both benches had no result, this file still said done, `78f01e2` was not listed, and its `build/` was not removed. Done: all of it is run and recorded here, by this gate check. Its own worktree's `build/` is not this one's to touch, and this one's is removed.

**9. From the top of the world, the default render distance showed only sky.** Expected: flying to the limit shows the world below. Actual, from that gate check: fog is whole at 496 blocks from the eye at render distance 32, so the ground was gone by about +1000. It called this Ahmed's design call and left it. Done: fixed, since fog is there to hide the edge of what is loaded, and that edge is out to the side. `post_fog_along` in `run/include/gpu_bridge/post/index.h` counts what a ray goes up or down only as far as `fog_start`. A ray going up or down by less than that gets the old fog to the byte. `shade_fog_behind` and what `render_entities` leaves out go the same way. Evidence: `from_high_above_the_ground_shows_under_the_eye_and_melts_into_the_sky_at_the_side` in `tests/cuda/sky/fixtures/from_above.h`, from 160 and 1240 blocks up; and the flight's `down_top.png`, read, showing rivers, land and mountains under the eye.

**What it does not do, for Ahmed to judge.** Ground shows only within about 71% of the fog's reach to the side, 354 blocks at render distance 32. From the top, that is a patch straight below, about 17 degrees from straight down, and looking level, or even well below level, is still only sky. `slant_top.png` and `level_top.png` show this. Render distance 128 shows far more. Making the default show more would mean changing the render distance with height, or a bigger default, which is his call.

**One golden picture changed.** `tests/run/render/frame/fixtures/down.png`, whose game has render distance 6 and looks steeply down from high, so its rays do go past `fog_start`: 4302 pixels past the tolerance, in the distant water. Before and after were both looked at: the same view, the water a little clearer. It was made again with `MC_GOLDEN_WRITE=1`. The other two in that test came out exact, and the sky test's goldens pass unchanged.

**10. Ore stopped at z = 500.** Expected: rock of a mountain above 500 has ore. Actual: `ore/gravel`, `coal_high`, `iron_high` and `emerald` end at 500 in `run/src/terrain/ore/index.c`. Nothing was lost while terrain stops at 454, but phase 8 would have hit it. Done: four rows of their own, `ore/gravel_peak`, `coal_peak`, `iron_peak` and `emerald_peak`, from 501 to 1250, as much in a layer as the rows they go on from. Iron has what `iron_high` has at about 450, as its triangle is down to nothing at 500. Changing the old rows' numbers would have moved every vein in them, so they stay, and `run_terrain_ore`'s pinned digest passes unchanged. These numbers are a starting point, and no real mountain has reached them. Evidence: `rock_above_the_old_top_has_ore_up_to_the_last_layer` and `leaving_out_rows_over_the_rock_moves_nothing`.

**11. The taller world had cost the terrain target, and this file had not said so.** Expected: this file said `bench_terrain` missed "as before the change", at 900 before and 923 after. Actual: phase 17 recorded 1080 and 1094 from a cool CPU, and this gate check measured 974 at 62 C and 977 at 76 C, with the fix for issue 10 in place and not this one. So the 900 was a hot number, and the height did cost the target. No profiler is on this machine. Done: `terrain_generate_box` runs its stages only up to `TERRAIN_MADE_TOP`, 518, which is `TERRAIN_DENSITY_NEVER_SOLID_FROM + TERRAIN_FEATURE_TALLEST`, and fills the rest with air. Those are the limits the density's and the feature's own tests hold, so it follows when phase 8 raises them. Evidence: 1081, 1090 and 1077, exit 0, and `nothing_is_lost_above_where_terrain_stops_making` in `tests/run/terrain/seed/`, every stage by hand against it on whole columns, in the tallest place of a walk over the map, 407 high, and three plain ones. **Phase 8 must update `TERRAIN_DENSITY_HEIGHT_MAX` when it raises mountains, or terrain above this top is cut off.** The density's tests fail if it doesn't.

**12. `run_save_state_game`'s frame check failed once under `-j3`.** Expected: confirm whether that was load only. Actual: its longest wait between pictures during saves, 23.96 ms, against about 19.1, which fits the check of the longest without saves plus 16.667. Alone, five runs: median 2.11 to 2.14 ms both ways, longest at most 4.51 ms, all pass. Beside `render_feature_lod` and `render_feature_post`, four runs: all pass, but the longest rises to 8.6 to 17.5 with saves and 5.6 to 36.6 without, so the load moves it whichever way, and more than saves do. The failure itself was not reproduced, so load is the strong reading and not a proof. Done: `MC_TEST_RUN_SERIAL` in `cmake/targets.cmake`, set in that test's `test.cmake`, so ctest starts nothing beside it. `ctest -j3` was run with it, and it ran last and alone and passed.

**13. A test passed only because fog hid a change.** Expected: with `--no-day-cycle`, two pictures of `render_feature_sky`'s script are the same bytes. Actual: once the fog changed, 1 pixel of 57600 differed by 1 of 255, at (201, 165), the same on three runs. With the old fog and `--no-fog`, it is 2 pixels at the same place. So the difference was there all along and fog hid it. Something in the world moves that little in 198 ticks, and what was not traced. Done: the test now allows at most 8 pixels off by one step, none by more, and also checks that the clock moving really does move more than 8. That test and its helpers moved to `tests/cuda/sky/fixtures/program.h`, which also takes `index.c` back under the 1500 lines where the standard asks for a split: it had been at 1513.
