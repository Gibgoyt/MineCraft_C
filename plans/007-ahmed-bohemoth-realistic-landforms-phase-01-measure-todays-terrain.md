# 007-ahmed-bohemoth-realistic-landforms — phase 1: Measure today's terrain

**Status:** not started   (updated 2026-11-12)

Read first: `plans/007-ahmed-bohemoth-realistic-landforms-phase-00-vision-and-plan-files.md`, then the master `plans/007-ahmed-bohemoth-realistic-landforms.md`: section 0 ("Why the hills look like 1:1 cones"), 3.14 and 4. Every number here comes from the master. Where this file adds a number the master does not give, it says so and why.

## Goal

A headless program, `bench_landform`, measures any seed's land as the master's section 4 defines it, and draws it. It reports:

- the slope histogram, and slope by relative height;
- each peak's concavity ratio and width over relief;
- plateau pieces and escarpment length;
- range length, and where knobs stand;
- the height bands and the sea.

It works over 64 by 64 km from the 2D profile, and on 64 windows of 1 km² from real blocks. It also writes the pictures in master 3.14: relief with contours, slope classes, plateaus, true-scale profiles, and an equator-to-pole strip.

It has a self-test on synthetic ground: a 1:1 cone, a $p = 2.5$ peak and a 5 by 5 km plateau each give known numbers. That self-test runs in ctest.

It has been run on today's land for plan 003's 8 seeds. Its table fills the "Today" column of master section 4 in this file, and its pictures have been looked at and described. The profile's share of `bench_terrain`'s time, and the cost of the tallest place, are recorded.

**No land changes. Every digest holds.**

## Where this fits

**The vision.** Plan 007 is the landform layer (phase 00). Ahmed said "maybe I think 1:1 slopes are because I have not explored much of the world". The master argues from the code that some of it holds everywhere: no plateau term exists, and ranges are symmetric tents about 2 km wide. But how often each slope occurs, and where, is a measurement. So nothing changes until this phase has measured it, and every later phase is judged with the same tool.

**What reads it:**

| Reader | What |
|---|---|
| Phase 02 | The relief pictures use the same contour rule. Phase 02's Atlas lines are checked against them |
| Phase 03 | Plateau pieces, escarpment length, range length, sea share, height bands; the `/tp` lines for its gate |
| Phase 04 | Slope histogram, slope by relative height, concavity ratio, knobs by base height, floating rock |
| Phase 05 | Basin count and floors |
| Phase 06 | The "after" table beside this phase's "before" |
| Ahmed | The pictures and the `/tp` lines, with plan 006's `/tp` and `/fly` |

**What this phase must NOT build:**

- No change to `run/src/terrain/` or any land. Every `run_terrain_*` digest holds.
- No new terrain fields or splines. Those are phases 03 to 05.
- No Atlas change. That is phase 02.
- No climate change. The strip reads the climate's formula, or plan 006's fallback if it exists; it does not change them.

## Ahmed's words

- "please add a plan that varies the steepness of hills! I noticed all your hills are 1:1"
- "all your hills still are ice cream cones ... over a span of 2km x 2km there is a mountain range!"
- "maybe I think 1:1 slopes are because I have not explored much of the world"
- "3. slopes don't need to be 1:1, they can be 5:1 (e.g. 5 blocks forward for every 1 block up) and at the peaks go to 1:5"

---

## Background, explained

### Gradient, and run:rise

The gradient $g$ at a column is how fast the ground rises in its steepest direction: $g = \sqrt{(\partial h/\partial x)^2 + (\partial h/\partial y)^2}$.

- On the profile it is measured by central differences over a baseline of 8 blocks: $\partial h/\partial x \approx (h(x+4) - h(x-4))/8$, on the lattice every 4 blocks.
- On real blocks the same baseline is used on the top solid block's height (no trees or plants).

Ahmed's run:rise is $1/g : 1$. So 5:1 is $g = 0.2$, 1:1 is $g = 1$, and 1:5 is $g = 5$. The `sqrt` here is in a bench, not a block decision, so libm is fine.

**Example:** $h(x-4) = 100$, $h(x+4) = 108$, and the same in $y$. Then $\partial h/\partial x = 1$, $\partial h/\partial y = 1$, and $g = \sqrt 2 = 1.41$: a 1:1.41 slope, $\arctan 1.41 = 54.7°$.

### Local relief, and relative height

**Local relief** at a column is the highest minus the lowest ground within a square of 2048 blocks centred on it, at step 16. **Relative height** is $(h - \min)/(\max - \min)$, from 0 at the lowest to 1 at the highest.

"Slope by relative height" bins land columns by deciles of relative height, only where the local relief is at least 150 (mountain land), and reports the median gradient in each decile.

- A cone gives the same median in every decile.
- A concave mountain gives medians rising with the decile.

**Target** (master section 4): the top decile's median over the median of the bottom 3 deciles is at least 5.

### Peaks, concavity, and width over relief

A **peak** is a column whose height is the highest within 512 blocks (at step 16) and whose relief is at least 150. Its relief is its height minus the lowest ground within 3 km.

From the peak, 16 rays are walked outwards every 16 blocks. Each ray stops where the ground first stops falling for 64 blocks, or at 3 km. On each ray we find the distances $r_{25}, r_{50}, r_{75}, r_{100}$ at which the ground has fallen 25, 50, 75 and 100% of that ray's drop. Their medians over the 16 rays are the peak's.

- **Concavity ratio** = mean slope of the top half over mean slope of the bottom half $= \dfrac{0.5H / r_{50}}{0.5H / (r_{100} - r_{50})} = \dfrac{r_{100} - r_{50}}{r_{50}}$.
  - A 1:1 cone: $r_{50} = 0.5 r_{100}$, so the ratio is **1.00**.
  - $h = H(1 - r/R)^{2.5}$: $r_{50} = R(1 - 0.5^{0.4}) = 0.2421R$, so the ratio is $0.7579 / 0.2421 =$ **3.13**.
- **Width over relief** $W_{50}/H = 2 r_{50} / H$. A 1:1 cone gives 1.00. Reported only, with no target.

### Plateau pieces and escarpments

**Plateau land** is a column at 250 or over whose local relief within 512 blocks is at most 80. Pieces are 4-connected at step 16, as `bench_world_map` joins its high pieces. Each piece gets its area in km² and how far across it is (its longest axis-aligned extent, as `bench_world_map` measures "across").

**An escarpment column** is plateau land within 16 blocks of a non-plateau column, where the ground within 1500 blocks outwards drops at least 300. Escarpment length is the count of such columns times 16, in km.

Today, the master predicts 0 for both.

### Ranges and knobs

- **A range piece** is connected dry land at 500 or over. For each piece: its area, its longest extent, and its elongation (extent over $\sqrt{\text{area}}$).
- **A knob** is a local maximum within 128 blocks whose prominence is 15 to 150, where prominence is height minus the highest saddle to higher ground within 512 blocks, found by a flood. Each knob is binned by its base height (the saddle): below 150, 150 to 250, 250 and over.

Today, the master predicts that almost no knob has a base of 250 or over.

### Why both the profile and real blocks

- **The profile** is the 2D height, before the 3D noise, rivers and caves. It is fast: 64 by 64 km at step 16 is 16.8 million profile evaluations, done in batches of 128 on 16 workers.
- **Real blocks** (`terrain_generate_box`, as `bench_world_map` reads them) are what Ahmed walks on, including the overhangs that softness allows and rivers' cuts. They are slow, so they are used on 64 windows of 1 km² chosen evenly.

The histogram is printed both ways. If they disagree by more than 5 points in any class, that is recorded, since it says the 3D noise or rivers shape the slopes more than the plan assumes.

---

## Design

### Directories and files

- `bench/landform/index.c` and `README.md`: one program, `bench_landform`. If it would pass 1500 lines, its measures split into sub-directories (`plans/CODING_STANDARD.md`: split at 1500, hard limit 2000), as `bench/world_map/atlas/` did.
- `tests/run/terrain/landform/measure/test.cmake` and `README.md`: runs `bench_landform --self-test`, as `tests/run/terrain/cave/walk/test.cmake` runs `bench_cave_walk`.

### Command line

    bench_landform [--self-test] [--size-km N] [--step N] [--strip] [--windows N] <directory> [seed...]

Defaults:

- `--size-km 64`, `--step 16`, `--windows 64`;
- the strip on;
- the default seed `TERRAIN_SEED_DEFAULT`.

Exit status: 0 if it ran, 1 if a picture could not be written or the self-test failed, 2 if it could not start, as `bench/world_map/README.md` says of itself.

### Outputs

As master 3.14:

- `<seed>_relief_64km.png`
- `<seed>_slope_64km.png`
- `<seed>_plateaus_64km.png`
- `<seed>_profiles.png`
- `<seed>_strip.png`
- `<seed>_windows.png`
- `<seed>.csv`

Colours are this program's and hand-typed, as `bench_world_map` says of its own.

**Slope classes and their colours:**

| Class | $g$ | Run:rise | Colour |
|---|---|---|---|
| Flat | < 0.05 | flatter than 20:1 | `0xE8F0D8` |
| Gentle | 0.05 to 0.2 | 20:1 to 5:1 | `0xA8D890` |
| Moderate | 0.2 to 0.5 | 5:1 to 2:1 | `0xF0E070` |
| Steep | 0.5 to 1 | 2:1 to 1:1 | `0xF0A040` |
| Very steep | 1 to 2 | 1:1 to 1:2 | `0xE06030` |
| Cliff | 2 to 5 | 1:2 to 1:5 | `0xB02020` |
| Sheer | > 5 | steeper than 1:5 | `0x500010` |

**Contours** on the relief picture: every 50 blocks thin, every 500 thick, by the same rule phase 02 puts in the Atlas (a line where the height's band changes between a pixel and its east or south neighbour). The rule is written once, in this bench, and phase 02 copies its numbers, not its code: the Atlas is in `run/`, and a bench may not be linked into the game.

**Profiles** are drawn at true scale, one block across per block up, so a 1:1 slope is drawn at 45°. Each has a grid every 100 blocks and labels in the font.

**The strip:**

- x from -2048 to 2048, y from 0 to 38,298 (`CLIMATE_POLE_Y`), a column every 8 blocks: 512 by 4788 pixels.
- Shaded relief with contours.
- Latitude ticks every 10°.
- The snowline drawn as a dashed line where the ground passes it. It comes from plan 006's `terrain_climate_fallback` if that exists when this phase is built, else from plan 004's temperature formula at κ 0.5 with no current. The picture says which.

### The table

Printed per seed, then min to max over the seeds. **Every row of master section 4**, plus:

- the profile-against-blocks histogram difference;
- peaks found;
- the median $W_{50}/H$;
- the number of knobs;
- the time taken.

**Then the `/tp` lines**, each with a label:

- `largest plateau piece` (its middle);
- `tallest peak`;
- `longest range piece` (its middle);
- `steepest 1 km window`;
- `a knob on a base >= 250` (or "none");
- `lowest dry land below 0` (or "none").

Each is printed as `/tp X Y`, ready for plan 006's command box.

### The cost measurements

Two numbers for phases 03 and 04's budgets:

1. **The profile's share of `bench_terrain`.**
   - Time `terrain_density_profile_points` on 36 chunk columns' lattices (each 5 by 5, as a box asks), against `terrain_generate_box` on the same 36.
   - Both from this program, single-threaded, best of 3.
   - It prints the share and the profile's microseconds a lattice column.
   - It is not a change to `bench_terrain`.
2. **The tallest place's cost.**
   - `terrain_generate_box` columns a second, 16 workers, at the `tallest peak`, beside a plains place, from this program.
   - So phase 03 knows what a 650 plateau column might cost before it builds one.

---

## Owned paths

This phase may edit these, plus this file:

- `bench/landform/` (new)
- `tests/run/terrain/landform/measure/` (new)

**Wiring outside them, as small as possible:**

- The CMake lines that register a bench and a test, following `bench/world_map/` and `tests/run/terrain/cave/walk/`
- `plans/REPO_STRUCTURE.md`: rows for `bench/landform/` and its test

## Steps

1. **Read `bench/world_map/index.c`** for how it samples columns on 16 workers, reads real tops, joins pieces and writes PNGs (`display_png_encode`). *Why: the same patterns, so the numbers are comparable with plan 003's table.*
2. **The self-test first:** synthetic heightfields through the same measuring code: a 1:1 cone, the $p = 2.5$ peak, a 5 by 5 km plateau at 400 with a 1:2 edge, and a plane with $g = 0.2$. *Why: a measuring tool that is wrong would mislead every later phase.*
3. **The profile sweep** over 64 km at step 16, the histogram at step 4 in 64 tiles of 1 km² (fine slope from the lattice), and relative heights. *Why: the bulk of the table.*
4. **Peaks, plateau pieces, escarpments, range pieces, knobs.** *Why: the shape measures that answer "cones" and "pimples".*
5. **Real-block windows** (64 of 1 km²) and the histogram both ways. *Why: what Ahmed walks on.*
6. **Pictures:** relief with contours, slope, plateaus, profiles, strip, windows. *Why: Ahmed tunes by eye.*
7. **The cost measurements.** *Why: phases 03 and 04 spend from that margin.*
8. **Run on 8 seeds; fill the "Today" table below; look at every picture and describe it in this file.**
9. **README and `plans/REPO_STRUCTURE.md`.** Tick each todo as it lands.

## Todo

- [ ] `bench/landform/` builds as `bench_landform`, with the command line above
- [ ] Self-test on synthetic ground passes and runs in ctest as `run_bench_landform_self_check` (`tests/run/terrain/landform/measure/`)
- [ ] Profile sweep, 64 by 64 km at step 16, on 16 workers; fine slope at step 4 in 64 tiles
- [ ] Slope histogram (7 classes) and slope by relative-height decile
- [ ] Peaks with concavity ratio and $W_{50}/H$
- [ ] Plateau pieces, escarpment length, range pieces, knobs by base height
- [ ] 64 real-block windows and the histogram difference
- [ ] Pictures: relief with contours, slope classes, plateaus, true-scale profiles, the equator-to-pole strip, windows
- [ ] `/tp X Y` lines for the places listed above
- [ ] The profile's share of `bench_terrain`, and the tallest place's columns a second
- [ ] Run on the 8 seeds; the "Today" table below filled in; every picture looked at and described below
- [ ] `bench/landform/README.md` with the results; `plans/REPO_STRUCTURE.md` rows
- [ ] Every `run_terrain_*` digest unchanged (nothing in `run/` changed)

## Today (filled in by this phase)

On plan 003's 8 seeds (2198175988018515243, 12636146071196537873, 5569068473609503046, 1, 42, 20261104, 9999999999999, 18446744073709551557), 64 by 64 km each.

| Measure (master section 4) | Min to max over 8 seeds | Ahmed's seed |
|---|---|---|
| Flat / gentle / moderate / steep / very steep / cliff / sheer | | |
| Of ground steeper than 1:2, share in the top 30% of relief or on a face | | |
| Top decile median slope over bottom 3 deciles | | |
| Concavity ratio, median (peaks found) | | |
| $W_{50}/H$, median | | |
| Plateau pieces ≥ 25 km²; largest km² and across | | |
| Escarpment length | | |
| Longest range piece at ≥ 500, km; elongation | | |
| Knobs by base: < 150 / 150–250 / ≥ 250 | | |
| Sea; reaching 150, 300, 500, 750, 1000; top | | |
| Floating rock per km² (from `bench_world_map`, same seeds) | | |
| Profile against blocks, largest class difference | | |
| Profile share of `bench_terrain`; µs a lattice column | | |
| Tallest place against plains, columns a second | | |

## Tests

### `run_bench_landform_self_check`

Runs `bench_landform --self-test`, which builds each synthetic heightfield in memory and runs the real measuring code on it, at step 4. Each check prints what it got.

- **`a_one_to_one_cone_is_all_steep_and_has_concavity_one`.** A cone of height 600, radius 600, apex at the middle of a 4 by 4 km flat field at 0.
  - At least 95% of the cone's columns, leaving out 16 blocks round the apex and the rim, measure $g = 1.00 \pm 0.02$. Its exact $g$ is 1.0, which is the boundary between two classes, so the check is on $g$ and not on a class.
  - Concavity ratio 1.00 ± 0.05; $W_{50}/H$ 1.00 ± 0.05.
  - Fails if the gradient or the rays are wrong.
- **`a_power_two_and_a_half_peak_has_concavity_three`.** $h = 800(1 - r/1500)^{2.5}$ for $r < 1500$. Concavity ratio 3.13 ± 0.2. The slope-by-decile ratio (top over the bottom 3) is at least 5.
- **`a_five_by_five_km_plateau_is_one_piece`.** A square at 400, 5120 blocks a side, with a 1:2 edge, on a field at 20.
  - One plateau piece, 26.2 ± 1 km² ($5.12^2$).
  - Across 5.12 ± 0.032 km.
  - Escarpment length 20.5 ± 0.5 km.
- **`a_plane_of_five_to_one_is_all_moderate`.** $h = 0.2x + 0.1y$: $g = 0.2236$, in the moderate class, 100%.
- **`knobs_are_binned_by_their_base`.** Ten domes of prominence 40 on the plateau above and ten on the field: 10 at base ≥ 250 and 10 below 150.
- **`the_same_input_gives_the_same_table`.** Two runs give the same table and CSV bytes; 1 and 16 workers give the same bytes.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_bench_landform_self_check$' --output-on-failure
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 2198175988018515243 12636146071196537873 5569068473609503046 1
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 42 20261104 9999999999999 18446744073709551557
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map --map-only /tmp/mc_c_maps 2198175988018515243
    pgrep -af "$PWD/build"    # must print nothing

The seeds run in two groups of 4, so each call stays inside its timeout, as plan 003 phase 8 did with `bench_world_map`. If one seed takes more than 7 minutes, the default `--step` goes to 32 for the 64 km sweep, and that is recorded here.

## Gate

Ahmed looks at, for his seed and one other:

- `relief_64km.png`;
- `slope_64km.png`;
- `profiles.png`;
- `strip.png`;
- the table.

He says:

1. Does the slope map and the profile show the "1:1 cones" he saw? Is it everywhere, or where he happened to look?
2. Which scale for plateaus: master Q1 A, B or C?
3. Are master section 4's targets right, or does he move any (Q3)?

He may also `/tp` to the printed places (plan 006 phase 01) and compare with the pictures. His answers are recorded below and in the phase files they affect.

## Risks and open questions

| Risk or question | Default |
|---|---|
| A 64 km sweep at step 16 is 16.8 million profile evaluations; at about 1 µs each on 16 workers that is about 1 s, but real blocks for 64 windows cost more (about 18 s a seed for a 16 km `--map-only` map in `bench_world_map`) | Measured. If a seed takes over 7 minutes, use `--step 32` and fewer windows; record it |
| "Peak", "plateau" and "knob" depend on window sizes chosen here (512, 3 km, 128) | They are this file's choices, printed with the table; the self-test fixes their meaning on synthetic ground. Ahmed may change them at the gate |
| Plan 006's fallback climate may not exist yet for the strip's snowline | Use plan 004's formula at κ 0.5 and say so on the picture |
| The profile and real blocks disagree on slopes (softness, rivers) | Both printed; a difference over 5 points in any class is an Issue found, and phase 04's softness design is checked against it |
| The table's measures are new and nothing checks them against the world but the self-test | The self-test covers each measure on a known shape; the pictures show the same pieces outlined, so a wrong piece is visible |

## Commits

Appended as they land:

## Issues found

None.
