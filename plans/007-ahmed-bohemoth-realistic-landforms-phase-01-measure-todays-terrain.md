# 007-ahmed-bohemoth-realistic-landforms — phase 1: Measure today's terrain

**Status:** done, waiting for Ahmed's gate   (updated 2026-11-12)

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

- [x] `bench/landform/` builds as `bench_landform`, with the command line above
- [x] Self-test on synthetic ground passes and runs in ctest as `run_terrain_landform_measure` (`tests/run/terrain/landform/measure/`; the plan's name `run_bench_landform_self_check` cannot be, Issues found 11)
- [x] Profile sweep, 64 by 64 km at step 16, on 16 workers; fine slope at step 4 in 64 tiles
- [x] Slope histogram (7 classes) and slope by relative-height decile
- [x] Peaks with concavity ratio and $W_{50}/H$
- [x] Plateau pieces, escarpment length, range pieces, knobs by base height
- [x] 64 real-block windows and the histogram difference
- [x] Pictures: relief with contours, slope classes, plateaus, true-scale profiles, the equator-to-pole strip, windows
- [x] `/tp X Y` lines for the places listed above
- [x] The profile's share of `bench_terrain`, and the tallest place's columns a second
- [x] Run on the 8 seeds; the "Today" table below filled in; every picture looked at and described below
- [x] `bench/landform/README.md` with the results; `plans/REPO_STRUCTURE.md` rows
- [x] Every `run_terrain_*` digest unchanged (nothing in `run/` changed)

## Today (filled in by this phase)

On plan 003's 8 seeds (2198175988018515243, 12636146071196537873, 5569068473609503046, 1, 42, 20261104, 9999999999999, 18446744073709551557), 64 by 64 km each.

| Measure (master section 4) | Min to max over 8 seeds | Ahmed's seed |
|---|---|---|
| Flat / gentle / moderate / steep / very steep / cliff / sheer, % of land (profile, the 64 windows at 4 blocks) | 0.8–1.3 / 7.8–11.6 / 20.7–26.7 / 24.6–26.8 / 21.9–26.7 / 11.0–15.6 / 0.8–1.9 | 1.2 / 10.0 / 23.8 / 25.0 / 23.4 / 15.2 / 1.5 |
| The same over the whole map (profile at its step 16, 8 blocks across) | 1.0–1.2 / 8.8–10.0 / 21.7–23.0 / 24.8–25.4 / 24.4–25.8 / 15.1–16.1 / 1.4–1.6 | 1.2 / 9.9 / 22.8 / 25.1 / 24.6 / 15.1 / 1.4 |
| The same from real blocks in the 64 windows | 1.1–1.8 / 6.5–9.5 / 17.0–22.0 / 26.4–28.3 / 22.4–26.2 / 13.5–17.7 / 2.5–4.3 | 1.5 / 8.2 / 19.4 / 26.9 / 24.0 / 16.6 / 3.5 |
| Of ground steeper than 1:2, share in the top 30% of relief or on a face (no face exists) | 10.9–11.5% | 11.7% |
| Top decile median slope over bottom 3 deciles | 2.45–2.62 | 2.57 |
| Median slope by decile, 0 to 9 | 0.39–0.43, 0.56–0.60, 0.78–0.83, 0.99–1.09, 0.99–1.22, 1.13–1.37, 1.17–1.50, 1.30–1.70, **1.65–1.94, 1.61–1.70** | 0.40, 0.56, 0.77, 0.98, 1.08, 1.22, 1.27, 1.45, **1.76, 1.59** |
| Concavity ratio, median (peaks found) | 1.39–1.44 (4678–5007 peaks) | 1.39 (4613) |
| $W_{50}/H$, median | 0.97–1.06 | 1.04 |
| Plateau pieces ≥ 25 km²; largest km² and across | 0 on every seed: no plateau land at all | 0 |
| Escarpment length | 0 | 0 |
| Longest range piece at ≥ 500, km; elongation | 3.9–6.0 km (1.6–4.7 km²); 2.7–3.3 (421–499 pieces) | 6.8 km (4.0 km²); 3.4 (422 pieces) |
| Knobs by base: < 150 / 150–250 / ≥ 250 | 18316–21832 / 932–1082 / 3241–3656 (12.6–15.2% at ≥ 250) | 18722 / 989 / 3275 (14.2%) |
| Sea; reaching 150, 300, 500, 750, 1000; top (profile, share of the map) | 25.7–27.4%; 19.9–21.7, 11.6–12.5, 4.58–5.23, 1.09–1.41, 0.16–0.25%; 1166–1181 | 27.6%; 21.2, 12.1, 4.95, 1.28, 0.20%; 1170 |
| Floating rock per km² (from `bench_world_map`, same seeds) | 1072–1383 (full `bench_world_map`, two runs of 4, exit 0, 0 columns differing) | not run |
| Profile against blocks, largest class difference | 3.7–4.7 points (under 5 on every seed) | 4.4 |
| Profile share of `bench_terrain`; µs a lattice column | 0.08–0.10% of `terrain_generate_box` on one worker; 0.32–0.37 µs | 0.09%; 0.37 µs |
| Tallest place against plains, columns a second (16 workers) | 843–944 against 1158–2092 | 829 against 1354 (a second run: 919 against 1490) |

The seeds took 28.4 to 30.1 s each, all outputs included (profile sweep about 4.1 s, the measures 1.1 s, the windows of real blocks about 22 s), so `--step 16` stays the default. The CPU timings are wall clock on a shared machine with other agents' builds queued behind the lock; no card was used, so no card clock applies.

### The `/tp` lines on Ahmed's seed

    largest plateau piece          none
    tallest peak                   /tp -24512 10528   (z 1170)
    longest range piece            /tp -25536 9248    (z 525)
    steepest 1 km window           /tp 20496 -20464   (76% of its land steeper than 1:1)
    a knob on a base >= 250        /tp -27120 8048    (z 1153)
    lowest dry land below 0        /tp -3788 3808     (z -18)
    a plains place, for the cost   /tp -32752 15600   (z 57)

Every seed's lines are in the program's output, after its table.

### The pictures, looked at

On Ahmed's seed `11107447384583428913` and on `2198175988018515243`, in `/tmp/mc_c_landform/`:

- **`relief_64km.png`.** The whole 64 km is one texture repeated: sea in blotches a few km across, green lowland, and short orange-to-grey ranges a few km long, curving, scattered evenly with no larger order. The 50-block contours on the lowland wander in tight loops, every few pixels: the lowland is never level. Ranges show their contours packed evenly from foot to top, then a small grey summit, the "ice cream cone" in plan: a 1 to 3 km blob with even rings.
- **`slope_64km.png`.** Almost no pale "flat" anywhere. Lowland is yellow (moderate, 5:1 to 2:1) mixed with orange (steep); every range is dark red (1:1 to 1:5) **from its foot up**, not only near the top. The two seeds' maps cannot be told apart in kind: **the 1:1 look is everywhere, not where Ahmed happened to look.**
- **`plateaus_64km.png`.** No yellow plateau at all and no red escarpment, on any seed. The tan range pieces (500 and over) are small blobs and short worms, 1 to 6 km.
- **`profiles.png`.** Through the tallest peak, along x and y: a 4 km cross-section rises from the sea at about 1:1, with bumps all the way up, to a pointed top; the y section is a single steep-sided mass about 2 km across at 1000. The plains section (no plateau to cut) shows the lowland as 20 to 40 blocks of continuous bumps, not flat. The longest range section is a chain of separate humps with deep saddles between, not a continuous ridge.
- **`strip.png`.** Equator to pole looks the same at every latitude: the same blotchy sea, bumpy lowland and short ranges. The snowline (plan 006's fallback, plan 004's formula at kappa 0.5) first meets the ground at 59° N on Ahmed's seed and 63° N on the other; south of that even the summits are under it, and near the pole all land is above it.
- **`windows.png`.** The real blocks at 4 blocks a pixel: every land window is orange and red with yellow between; the few pale patches are beaches and river floors. Range windows are dark red stripes. They read as the profile's map does, a little steeper (more sheer), which is what the 3D noise adds.

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

- `98d80288` Add bench_landform, which measures and draws the shape of a seed's land as plan 007 section 4 defines it, with its self-test in ctest, and record today's land on plan 003's 8 seeds and Ahmed's.
- (the commit that adds this line, after `98d80288`) Build bench_landform on main's terrain_generate_box_fallback, draw the strip's snowline from plan 006's fallback climate, and record plan 007 phase 01's verification.

### Verification, 2026-11-12

- `cmake --build --preset debug` and `--preset release`: exit 0, `standards_lint: clean`.
- `ctest -R '^run_terrain_landform_measure$'`: passed (5.1 to 6.1 s). `^(standards_lint|clang_format)$`: both passed. Run again after the rebase onto `24011684`: all three passed.
- `ctest -R '^run_terrain_.*$'`: 11 of 11 passed, before the rebase (biome, cave, cave_walk, density, feature, feature_own, fields, hydrology, landform_measure, ore, seed). Nothing under `run/` changed in this phase, so no digest could move.
- `bench_landform` on the 8 seeds in two runs of 4, and Ahmed's: exit 0, 28 to 30 s a seed; after the rebase, Ahmed's, `2198175988018515243` and the default seed again, the same numbers.
- `bench_world_map` on the 8 seeds, in full (not `--map-only`, for the floating rock): two runs of 4, exit 0, 0 columns differing.
- `pgrep -af "$PWD/build"` printed nothing at the end; no Xvfb was started.

## Issues found

1. **The answer to "have I not explored enough": the 1:1 look is everywhere.** All 8 seeds and Ahmed's give the same histogram within a few points: about 1% of land flat, 9% gentle, 23% moderate, 25% steep, 24% very steep (1:1 to 1:2), 15% cliff (1:2 to 1:5) and 1.5% sheer. Against master section 4's targets that is 30 times too little flat land and about 6 times too much cliff, and the lowland itself is mostly 5:1 to 1:1, from the hills term (±46 on a cell of 192) and the needles. The maps of different seeds and different latitudes cannot be told apart in kind.
2. **Concavity is 1.39 to 1.44, and slope peaks below the top.** The decile medians rise from 0.4 at the foot to 1.65–1.94 in decile 8, then **fall** in decile 9 (1.61–1.70): the rounded summit master 0 point 3 predicted from the S-curve splines. Top over bottom 3 is 2.5 (target ≥ 5); only 11% of ground steeper than 1:2 is in the top 30% of its relief (target ≥ 70%, master expected about 30%). Concavity is a little above the predicted 1 because a ray's foot is gentle lowland: the slope's foot is set by the hills, not by the range.
3. **No plateau land exists on any seed**, so plateau pieces, escarpment length and the plateau cross-section are all 0, as the master predicted. The profiles picture draws a plains place in that panel instead, and says so.
4. **Range pieces at 500 or more are 3.9 to 6.8 km long** (the master expected under 10), elongation about 3: a short worm, 421 to 499 pieces per map.
5. **Knobs on a base of 250 or more are 12.6 to 15.2% of knobs, not about 0** as the master predicted. The printed `/tp` knob of each seed stands within 20 blocks of its tallest peak (1142 to 1173 high): these are bumps of the crest and jagged terms on range flanks and summits, not koppies on high flat ground. Phase 04 should read the "≥ 25% of knobs on a base ≥ 250" target with that in mind, or add "and on ground whose relief in 512 is under 150" to the definition.
6. **Profile against blocks: 3.7 to 4.7 points, under the 5-point line on every seed.** Real blocks have fewer gentle and moderate columns and 2 to 3 points more sheer (2.5–4.3% against 0.8–1.9%): the 3D noise and rivers' banks add the steepest ground. Phase 04's softness design should note that sheer is already doubled by the blocks.
7. **"Lowest dry land below 0" is not "none".** Every seed has real land columns below 0 in its windows, 10 to 41 blocks down, which the master did not expect before phase 05. Not looked into here (no land change in this phase): they are probably cave mouths or carved river beds open to the sky with no water over their top block. Phase 05 should look at one (`/tp -3788 3808` on Ahmed's seed, z -18) before it adds basins, as its water rule must not meet these.
8. **The profile is 0.08 to 0.10% of a box's cost** on one worker, 0.32 to 0.37 µs a lattice column: master section 3.3's worry that macro fields would eat `bench_terrain`'s margin through the profile is unfounded, as the profile is not where a box's time goes. The 3.3 lattice is still cheap insurance but the "+15%" budget has room by a factor of 100.
9. **The tallest place is already under 1000 columns a second**: 829 to 944 on 16 workers at each seed's tallest peak, against 1158 to 2092 on plains, from this program. `bench_terrain`'s own places (1134 at the slowest, `snowy_taiga`) are not this tall. So phase 03's 650 plateau and any taller column start below the target before any new term; phase 03 must add a tall place to `bench_terrain` and measure it.
10. **Changed from the plan: the plateau rule and its self-test numbers.** Read with the 512 square centred on each column, "local relief within 512 blocks is at most 80" cuts 256 blocks off every plateau's rim. Plateau land is instead a column at 250 or over inside **some** 512 square of relief at most 80 (`bench/landform/measure/README.md`). On the self-test's 1:2 edge that takes 40 more blocks each side, so the check wants 27.04 ± 1 km², 5.20 ± 0.032 km across and 20.8 ± 0.5 km of escarpment, not 26.2, 5.12 and 20.5. It measured 27.08, 5.204 and 20.80.
11. **Changed from the plan: the test's name.** ctest names a test by its directory, so `tests/run/terrain/landform/measure/` is `run_terrain_landform_measure`, not `run_bench_landform_self_check`. It also falls under `^run_terrain_.*$`. Later phases' Verification should use the new name.
12. **Changed from the plan: the program is three files.** One file passed 2000 lines. `bench/landform/measure/` and `bench/landform/draw/` each hold an `index.h`, and `cmake/targets.cmake` now compiles a `bench/` directory with an `index.h` into the program above it instead of making it a program (6 lines, recorded in `plans/REPO_STRUCTURE.md`).
13. **Smaller choices of this file's own**, all in the READMEs: rays step one grid step (16 on the map, 4 on the self-test), not always 16; peaks and knobs must be strictly above their four neighbours (else a flat top's every column ties); a ray-less peak needs 8 of its 16 rays; the strip's snowline is plan 006's `terrain_climate_fallback_cell` (it landed on main while this phase was built; the first run used plan 004's formula directly, the warmest day's mean at 0 °C, and the pictures were redrawn with the fallback); "steepest 1 km window" is the window with the largest share of real land steeper than 1:1; "lowest dry land below 0" is read from the real blocks of the windows only.
14. **Rebased onto main at `24011684`** (plan 006 phase 02 had landed): `terrain_generate_box` now takes a climate sample, so the cost rows call `terrain_generate_box_fallback`, which plan 006 says gives the same bytes as no stage reads the sample yet. The default seed and Ahmed's re-run on the rebased tree gave the same numbers, byte for byte, in every CSV row but the times; columns a second there were 856 and 841 at the tallest place, 1387 and 2050 on plains, inside the first runs' spread.
15. **Self-test results** (release): cone 100.0% of the flank at g 1.00 ± 0.02, concavity 1.013, W50/H 1.000; the p = 2.5 peak concavity 3.141, top decile over the bottom 3 7.98; the plateau 1 piece, 27.08 km², 5.204 km, 20.80 km; the plane 100.00% moderate; 20 knobs, 10 under 150 and 10 at 250 or over; 2998 bytes of table and CSV the same twice on 16 workers and once on 1.
