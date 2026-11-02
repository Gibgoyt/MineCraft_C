# 001-ahmed-bohemoth-minecraft-from-scratch — phase 10, stage 3: 3D density

**Status:** done   (updated 2026-11-02)

## Goal

3D density with height splines rescaled to `z` in `[-250, 500]`, bedrock and sea level 0 exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-10-terrain.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/terrain/density/`
- `tests/run/terrain/density/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement density and height splines in `run/src/terrain/density/index.c`
- [x] Place bedrock at `z = -250`, and put water's top block at `z = 0`
- [x] Test the range limits, registered as `run_terrain_density`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_terrain_density$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Results on 2026-11-02: the build, `run_terrain_density` (19 tests, 20827542 checks) and `standards_lint` passed, and `pgrep` printed nothing. The whole debug suite: 50 of 51, `clang_format` among those passing. The one failure, `run_types`, is not from this stage: see issue 1.

Also run:

- `run_terrain_density`, `run_terrain_climate` and `run_terrain_seed` on the `release` and `sanitize` presets passed, with no sanitizer report. Density printed the same digest on all three, `0x9E92B14641827BCF`, so `-O3 -flto` moves no bit. `profile` was not built.
- Nine mutations, not committed: bedrock a layer up, water a block too high, the fast path's margin wrong, spline slopes that overshoot, the fade stopping too soon, an interpolation weight off by 1%, mountains too tall, a lattice column misplaced, and the check for `z` above the world removed. The first eight each failed between 2 and 6 tests. The ninth failed none, and cannot: nothing from 492 up is stone, so that voxel is air with or without the check. It stays as a guard that does not lean on the numbers.

What the test measured over 8 seeds, 2 million columns, from each column's `height`:

| Under the sea | Below -60 | 0 to 60 | From 150 | From 300 | Lowest | Highest |
|---|---|---|---|---|---|---|
| 47.9% | 32.9% | 34.5% | 4.13% | 0.527% | -116.0 | 446.1 |

Over 4 seeds and 9216 whole columns, the highest stone was 383 and the highest water 0, and nothing open lay below -117. 29 of 1024 columns round one mountain have stone over air; 0 of 8192 on plains do, whose steepest step is 2 blocks.

36 chunks of 751 layers filled in 2.9 ms on 1 worker on `release`. That is the blocks of this stage alone, on one patch of ground, and not the phase gate's benchmark.

No visual check was made here. Nothing draws this ground yet, and how it looks is for the phase gate's biome tour.

## Commits

Appended as they land:

- `30c4590` Add the 3D density, with height splines reaching from -116 to 454, bedrock at z = -250 and water's top block at z = 0, tested at the range limits and to give the same bytes from 1 worker and 16.

## Wiring outside the owned paths

Each the smallest I could make:

- `run/include/terrain/index.h`, `run/src/terrain/index.c`, `run/src/terrain/README.md`: `terrain_init` starts the density and `terrain_t` holds a pointer to it, out of its last 8 bytes of padding, so `terrain_t` stays 128 bytes and `app_t` does not move.
- `tests/run/terrain/climate/index.c`, `tests/run/terrain/seed/index.c`: three checks counted the fields in a new bank as the climate's 7. They now add the density's 3.
- `plans/REPO_STRUCTURE.md`: the `density` row, and the test row.

The running program now makes these fields and splines at startup, on `TERRAIN_SEED_DEFAULT`. Nothing fills a real world from them yet outside tests.

## Decisions later stages need

- **`terrain_t` has no padding left.** The next part that adds a pointer must grow it, by 64 bytes since it is `alignas(64)`, and with it `app_t`.
- **`terrain_density_fill_box` gives the first blocks:** bedrock, stone, water and air, as `uint16_t` block ids in section order. Stages 10.4 to 10.6 work on those.
- **Limits a later stage can rely on:** `TERRAIN_DENSITY_HEIGHT_MIN` and `_MAX` (-116, 454), `TERRAIN_DENSITY_ALWAYS_SOLID_TOP` (-155: stone from there down to the bedrock) and `TERRAIN_DENSITY_NEVER_SOLID_FROM` (492). So a tree on the highest peak has at least 8 layers to stay under `z = 500`, and must still clamp.
- **For hydrology (10.4):** water here is every open voxel up to `z = 0`, with no idea of being connected to a sea. 4 of 9216 columns had water below stone at `z = 0`, under a shore's overhang. `height` can also dip below 0 inland where hills meet low land, making ponds at sea level. Valleys, where `ridges` is low, sink towards the sea, ready for rivers.
- **Bank budget:** 10 of `TERRAIN_NOISE_FIELDS_MAX`'s 64 fields are used.
- **Ask for shape with `terrain_density_profile_area`** when blocks are not needed: it costs 26 octaves a column and no 3D noise.

## Issues found

1. **`run_types` fails on the integration branch, and did before this stage.**
   - **Expected:** the whole debug suite to pass.
   - **Actual:** `tests/run/types/index.c:914` and `:922` expect `heap at exit: 6 reserved, 6 released` and `leak at exit: 6 regions`, and the program reports 7. This stage reserves no region. Built and run on the integration branch's own tip, `15e8516`, with none of this stage's work: the same two checks fail with the same `7 reserved, 7 released`. The 7th looks like `owned_host` in `run/src/gpu_bridge/index.c`, from Stage 08.1's merge, which I did not confirm further.
   - **Done:** left alone and reported. The test is outside this stage's paths, and the fix is for whoever owns that merge: change the two 6s to 7s, once it is confirmed that 7 is the right count.

2. **`terrain_init` adding fields broke counts in `run_terrain_climate` and `run_terrain_seed`.**
   - **Expected:** nothing else to change for fields in the bank.
   - **Actual:** `init_and_deinit_are_a_pair`, `a_field_does_not_depend_on_what_was_added_before_it` and `a_bank_refuses_what_is_wrong` failed with `10 != 7` and `12 != 9`.
   - **Done:** they now add `TERRAIN_DENSITY_NOISE_FIELDS`. See Wiring. Each later stage that adds fields will hit the same three.

3. **The first limits on the three new fields were guesses, and were off.**
   - **Expected:** 0.39, 0.43 and 0.34 to be about twice each field's standard deviation.
   - **Actual:** `raw_noise_measures_what_the_limits_were_chosen_for` printed 2.30, 2.21 and 1.92 of them. It passed, as its bounds are 1.5 to 2.5, but it did not meet what the README says.
   - **Done:** changed to 0.34, 0.39 and 0.35, which measure 2.01, 2.01 and 1.98, before the digest was pinned.

4. **Shell commands were refused while writing this stage.**
   - **Expected:** one Bash command could chain edits, builds and tests, or end in a `grep` for trailers with an `echo` after it.
   - **Actual:** the worktree guard refused three: "too complex to verify that it stays inside the worktree".
   - **Done:** did as the message said: plain single commands. Nothing was bypassed.
