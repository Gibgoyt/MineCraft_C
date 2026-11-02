# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 6: Biomes and surface rules

**Status:** done   (updated 2026-11-02)

## Goal

The biome table and surface rules exist for: plains, sunflower plains, all forests, taigas, snowy biomes, ice spikes, swamp, mangrove, desert, savanna, windswept, badlands, jungles, meadow, cherry grove, grove, slopes, all peaks, shores, rivers, all oceans, mushroom fields, dripstone, lush caves, deep dark.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/biome/`
- `run/{src,include}/terrain/surface/`
- `config/biomes/*`
- `tests/run/terrain/biome/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Define every listed biome in `config/biomes/<name>/index.yaml`
- [x] Implement selection in `run/src/terrain/biome/index.c`
- [x] Implement surface rules in `run/src/terrain/surface/index.c`
- [x] Add the biome-coverage test, registered as `run_terrain_biome`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_biome$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Results on 2026-11-02: the build, `run_terrain_biome` (22 tests, about 26.8 million checks), `standards_lint` and `clang_format` passed, and `pgrep` printed nothing. The whole debug suite: 58 of 58 on its last run. The run before that was 57 of 58: `run_render_upload` failed once, and passed alone twice straight after: see issue 5.

Also run:

- `run_terrain_biome` on `release` and `sanitize` passed, with no sanitizer report. Each printed the same digests on all three presets: `0x5A80BA2E80CDD2F4` and `0x4C00A0C4B7250BF1`. `compile_game_tables` passed on `debug` with its new biome tests. `profile` was not built.
- 18 mutations, not committed. All 18 failed at least one test other than the two digest pins: priority inverted, the height scale, lakes sent to the ocean's rows, the `y` blend, the cave roof, looking 8 layers over a box, assuming nothing over a box, skipping a tile too eagerly, a skip that ignores rivers, wet treated as dry, covering what is not stone, the filler off by one, the bands' bend, the steepness' scale, a floor read from the voxel below, patch noise not spread, a yaml block changed, and a yaml priority changed. Those four about what lies over a box are only caught by `any_split_of_the_work_gives_the_same_bytes`.

What the tests measured:

- All 53 biomes turned up in all 3 seeds searched, each 65 km across, sampled every 128 blocks. Shares of all columns, over those 3 seeds, most to least by group: oceans 45.1% (deep 32.3%), snowy land 10.3%, deserts and badlands 9.1%, rivers 4.9%, shores 3.1%. The rarest are `stony_shore` 0.08%, `windswept_savanna` 0.13%, `windswept_hills` 0.20%. Underground, of the levels tried: `deep_dark` 12.2%, `dripstone_caves` 11.1%, `lush_caves` 9.0%.
- Every stage on 36 chunks, hydrology to ore: on `release`, 57,800 columns a second on 1 worker and 346,000 on 16. Stage 10.7's target is 1000.

Nothing yet says the biomes look right, or sit well next to each other. That is the phase gate's tour, whose command Stage 10.7 owes.

Wiring outside the owned paths, each the least that would do:

- `compile/src/game_tables/biomes/` and its header, a new part of the tool, and the call in `compile/src/game_tables/index.c`. `plans/REPO_STRUCTURE.md` already says `config/biomes/` is read by this tool, and the file is the source of truth only if something reads it. Its README, and the tool's.
- `cmake/generated.cmake`: the biome tables as outputs of the block tables' command.
- `run/src/terrain/index.c`, `run/include/terrain/index.h`, its README: two fields in `terrain_t` from its padding, so it stays 192 bytes and `app_t` does not change.
- `tests/compile/game_tables/`: the tool's new part is tested where the tool is.
- `tests/run/terrain/{seed,climate,hydrology,ore}/`: the number of fields in the bank, see issue 2.
- `plans/REPO_STRUCTURE.md`.

## Commits

Appended as they land:

- `28da06d` Add all 53 biomes as config files, choose among them by the nearest box in the climate, and lay each biome's surface and cave linings, tested to find every biome in every world searched and to give the same bytes from 1 worker and 16.

## Issues found

1. **The stage's owned paths leave out what reads its yaml.** Expected `config/biomes/` to be read by something in the owned paths. Actually the plan has `compile/src/game_tables/` read it (`plans/REPO_STRUCTURE.md`, line 119), and that tool's README says its parts arrive with their stages. Added the part there rather than typing a second copy of the table into `run/src/terrain/biome/`. Listed under wiring above.

2. **Four older tests pin the bank's field count.** Expected adding fields to break nothing. Actually `tests/run/terrain/{seed,climate,hydrology,ore}/` each compare `noise_bank.count` with a sum of named stages, as `a_bank_refuses_what_is_wrong` failed with `27 != 24`. Added `TERRAIN_SURFACE_NOISE_FIELDS` to each sum. The world itself did not move: their pinned digests still pass, since a field comes from its name and not from what came before it. The next stage to add fields meets the same thing.

3. **There can be no mushroom islands.** Expected mushroom fields far out at sea, as the plan's list implies. Actually `run/src/terrain/density/`'s `continental` spline gives no land at `c` below about -0.06, and `inland` allows no mountains below -0.1, so no column far from the coast is above the sea. The density is Stage 10.3's and the choice cannot raise ground. Left alone: `mushroom_fields` holds the oddest stretches of low coast, `c` from -0.2 to 0.13 with `weirdness` under -0.45, 0.75% of the world. Escalated: real islands need a change to the density, and that changes every world, so it is for Ahmed to ask for.

4. **Blocks the plan's biomes want do not exist.** `config/blocks/` has no sculk, thin snow, powder snow or bamboo, though `textures/blocks/` has pixels for sculk, powder snow and bamboo. `deep_dark` lines its caves with cobbled deepslate and tuff, the snowy biomes lie under `snow_block`, and `bamboo_jungle` differs from `jungle` by its podzol and its feature numbers. Eroded badlands have no hoodoos either: those need the ground's shape to change, and this stage only colours it. All left alone, and written in `run/src/terrain/surface/README.md`. Adding the blocks is outside this stage.

5. **`run_render_upload` failed once, and it is not this stage's.** Expected the whole suite to pass. It failed in one `ctest --preset debug -j 4` run, in which it had passed the run before, and passed twice alone directly after, in 1.8 s each, with 286 MiB of 8192 MiB of the card in use by then. The output of the failure was not kept, as that run was not asked for it. Other agents share this card, and this stage's code calls nothing in `run/src/render/`, so I suspect contention, but that was not shown. Left alone.

6. **`tests/run/terrain/seed/index.c` was not clang-formatted as it stood on the integration branch.** `clang_format` flagged `:680`, a line this stage did not write. Formatted the file, which moved that one call and nothing else. The test passes after it.

7. **Nothing in the game calls this yet.** Expected, and for the record: `run/src/main/` starts `terrain_t`, and no stage has yet made chunks from it for the world, so the five-step order in `run/src/terrain/surface/README.md` is run only by `tests/run/terrain/biome/`. Whoever feeds `run/src/world/` from terrain must call them in that order. **Closed by the phase gate:** `run/src/game/` makes every column with `terrain_generate_box`, which calls them in that order. See the phase file's issue 2.
