# 003-ahmed-bohemoth-initial-game-fixes — phase 11: Taller world

**Status:** done, except Ahmed's own flight to the limit   (updated 2026-11-04)

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

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 2400 ctest --preset release --output-on-failure
    tools/test-or-build.sh timeout 500 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

Ahmed: build a tower, or fly with `--test-world`, up to the limit.

    ./build/release/bin/minecraft_c --windowed

Run for this phase: release build clean with lint; ctest 100 of 100 in 757.7 s; `bench_world` exit 0 in 508.7 s; `bench_terrain` exit 1, its target not met, as before the change. Nothing of ours left running. Not run: `--windowed`, which is Ahmed's.

## Commits

Appended as they land:

- `49f56e0` Raise the top of the world to z = 1250 with 96 sections a column, two-word section masks everywhere, and save format version 3.
- `404e237` Say in the repository's structure that terrain's range now reaches z = 1250.

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
