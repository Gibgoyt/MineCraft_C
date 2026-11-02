# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 5: Caves and ores

**Status:** done   (updated 2026-11-02)

## Goal

Caves (cheese, spaghetti, noodle) and ore tables by depth exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/cave/`
- `run/{src,include}/terrain/ore/`
- `tests/run/terrain/cave/`
- `tests/run/terrain/ore/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement cheese, spaghetti and noodle caves in `run/src/terrain/cave/index.c`
- [x] Implement ore tables by depth in `run/src/terrain/ore/index.c`
- [x] Register tests `run_terrain_cave` and `run_terrain_ore`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_cave$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_ore$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Results on 2026-11-02: the build, `run_terrain_cave` (17 tests, about 26 million checks), `run_terrain_ore` (16 tests, about 7 million checks), `standards_lint` and `clang_format` passed, and `pgrep` printed nothing. The whole debug suite: 52 of 53. The one failure, `run_types`, is from before this stage: see issue 1.

Also run:

- `run_terrain_cave` and `run_terrain_ore` on `release` and `sanitize` passed, with no sanitizer report. Each printed the same digest on all three presets: `0x60C530C4CB0A81CC` and `0xC76ED6257560A88D`. `profile` was not built. Changing the tile from 32 blocks to 16 (issue 3) did not move the digest, as it must not.
- 18 mutations, not committed. 17 failed between 1 and 5 tests: the floor lowered, the sea's seal removed, its roof thinned, cheese's roof gone, the cheese threshold, noodles too wide, cells rejected too eagerly, a level's tube fields not worked out, carving water, the deepslate band's share, the world-limit check on a vein, a vein's reach too short, a chunk left out, the wrong form of an ore, ore replacing gravel, emerald anywhere, and one table number. The 18th, blending `y` before `x`, failed none: a bilinear blend is the same sum in either order, up to rounding, and no voxel in the tests changes sign. So the order in the README is not held by a test.

What the tests measured, on `debug`, as the numbers do not depend on the preset:

| Rock | Cheese | Spaghetti | Noodle | Any cave |
|---|---|---|---|---|
| Below `z = -125` | 5.98% | 1.08% | 0.56% | 7.49% |
| `-125` to `-30` | 5.53% | 1.10% | 0.54% | 7.05% |
| From `-30` | 5.47% | 1.27% | 0.47% | 7.12% |

- Mean thinnest way across, in blocks: cheese 17.4, spaghetti 4.6, noodle 2.2.
- In a box of 128 by 128 by 96: 99.5% of spaghetti and 65.4% of noodle in pieces at least 24 blocks long, the longest 128 and 67.
- Caves from `z = -241` to `z = 367`, over 4 seeds and 256 chunks.
- 80 patches by the sea, on 4 seeds: 1239321 water voxels, 1654904 voxels cut, 1620551 of them below the sea's surface, **0 touching water**.
- Over 256 places: no cheese or noodle without 5 blocks of rock above it, and 1067 voxels where spaghetti meets the sky above the sea: about 0.4% of columns.

All stone, over 256 chunks of 751 layers, to show the table by itself:

| Ore | A chunk | Deepslate | `z` from | to | Mean |
|---|---|---|---|---|---|
| Coal | 1858 | 0.0% | -124 | 500 | 218 |
| Copper | 349 | 10.5% | -185 | 98 | -44 |
| Iron | 2023 | 4.2% | -249 | 500 | 216 |
| Gold | 84 | 80.6% | -249 | -60 | -160 |
| Lapis | 91 | 52.4% | -249 | 3 | -123 |
| Diamond | 32 | 96.4% | -249 | -93 | -196 |
| Emerald | 7.7, as most of those chunks are not high | 5.1% | -154 | 500 | 171 |

Coal veins measured 18.5 voxels against a size of 17. On real ground with its caves cut, on one patch of low land by the sea (36 chunks, so coal is low and there is no emerald), a chunk holds: coal 4.9, copper 55.1, iron 116.5, gold 63.7, lapis 52.8, diamond 24.2.

On `release`, 36 chunks of 751 layers: caves alone 12.7 ms on 1 worker and 2.4 ms on 16; ground, caves and ore together 26.1 ms on 1 worker and 4.9 ms on 16. That is 0.73 ms a chunk on one worker for those three, not the phase gate's benchmark.

No visual check was made here. Nothing draws this ground yet, and how it looks is for the phase gate's biome tour.

## Commits

Appended as they land:

- `2378b77` Add cheese, spaghetti and noodle caves and the ore tables by depth, tested to keep the sea out, to lay each ore at its depths, and to give the same bytes from 1 worker and 16.

## Wiring outside the owned paths

Each the smallest I could make:

- `run/include/terrain/index.h`, `run/src/terrain/index.c`, `run/src/terrain/README.md`: `terrain_init` starts the caves and the ores, and `terrain_t` holds a pointer to each. It had no padding left, so it grew from 128 bytes to 192, with 48 bytes of written-out padding.
- `run/include/main/index.h`: that moved `sizeof(app_t)` from 3840 to 3904.
- `tests/run/terrain/climate/index.c`, `tests/run/terrain/seed/index.c`: three checks count the fields in a new bank, and now add the caves' 9.
- `plans/REPO_STRUCTURE.md`: the `cave ore` row, and two test rows.

The running program now makes these fields at startup, on `TERRAIN_SEED_DEFAULT`. Nothing fills a real world from them yet outside tests.

## Decisions later stages need

- **The order is density, then caves, then ore:** `terrain_density_fill_box`, `terrain_cave_carve_box`, `terrain_ore_fill_box`, each on one array. `tests/run/terrain/ore/` runs exactly that on 16 workers. Ore replaces only rock, so a cave stays open and shows ore in its wall.
- **For hydrology (10.4):** caves below the sea are air, with rock round them. To fill one with water or lava, ask `terrain_cave_mask_box` and use the air it says was cut, or turn the air into something else after carving. A cave only ever carves what is not air, water, lava or bedrock. `TERRAIN_CAVE_FLOOR_TOP` (-242) is the lowest a cave can reach.
- **For surface rules (10.6):** a cave carves anything that is not air, water, lava or bedrock, so it will cut dirt or sand if they are laid first. Spaghetti does reach the sky, about 0.4% of columns. Ore leaves dirt, gravel and sand alone. Stone turns to deepslate in `terrain_ore_fill_box` by depth: nothing else should.
- **For biomes:** `ore/emerald` asks only that a chunk's middle column be at least 120 high. If "mountain" should mean a biome, that condition is what moves.
- **Padding:** `terrain_t` has 48 bytes left, room for 6 more pointers. If Stage 10.4 merged a pointer too, whichever merges second has one conflict, in that struct and in `sizeof(app_t)`.
- **The table's source:** its numbers are what I remember of a familiar game's published ore distributions, not checked against a source, as this stage looked nothing up. No game file or code was read. The tests hold the table to what its README says, not to that game.
- **Bank budget:** 19 of `TERRAIN_NOISE_FIELDS_MAX`'s 64 fields are used.

## Issues found

1. **`run_types` fails on the integration branch, and did before this stage.**
   - **Expected:** the whole debug suite to pass.
   - **Actual:** `tests/run/types/index.c:914` and `:922` expect `heap at exit: 6 reserved, 6 released` and `leak at exit: 6 regions`, and the program says 7. Stage 10.3's issue 1 found the same on the branch before it, and this stage reserves no region.
   - **Done:** left alone and reported, as that stage did. It is outside this stage's paths.

2. **A table from what I remember of a familiar game's published numbers gave far too many emeralds.**
   - **Expected:** about as many emeralds as diamonds, under a mountain.
   - **Actual:** with the remembered number, doubled, 200 veins tried in a chunk, 8 high chunks held 1287 emerald voxels in just the middle quarter of each, about 640 a chunk on all stone. Our veins of 3 really come to about 3.
   - **Done:** 16 tried in a chunk. The same 8 chunks then hold 116, about 58 a chunk on all stone, and about half that under real ground. The README says so at the row. Whether that is well paced is for playing it.

3. **`release` refused the first cave code, and only `release`.**
   - **Expected:** a build that passes on `debug` to pass on `release`.
   - **Actual:** `run/src/terrain/cave/index.c:601: error: the frame size of 18400 bytes is larger than 16384 bytes`, at link, as `-flto` inlined each level's work into `cave_tile`. `debug` and `sanitize` had passed.
   - **Done:** a tile went from 32 blocks a side to 16, the size of a chunk, which took each array on the stack from 81 values to 25. Tiles are only how a box is cut up, so the digests did not move. Now `release` builds, and every preset agrees.

4. **The first limit on four one-octave fields was a guess, and was off.**
   - **Expected:** 0.44 to be about twice each field's standard deviation.
   - **Actual:** `raw_noise_measures_what_the_limits_were_chosen_for` printed 0.27 for each, so 1.63 of them, and 10.6% of values past it. It passed, as its bounds are 1.5 to 2.5, but it did not meet what the README says.
   - **Done:** changed to 0.54, and cheese's from 0.39 to 0.40, before the digests were pinned.

5. **A shell command was refused while writing this stage.**
   - **Expected:** one Bash command could write a helper script and run it.
   - **Actual:** the worktree guard refused it: "too complex to verify that it stays inside the worktree".
   - **Done:** did as the message said: plain single commands. Nothing was bypassed.
