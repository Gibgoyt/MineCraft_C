# 006-ahmed-bohemoth-biomes-follow-the-climate — phase 2: The climate sample and deterministic maths

**Status:** not started   (updated 2026-11-12)

Read first: `plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-00-vision-and-plan-files.md`, then the master `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md` (sections 0 "What is in the way", 2.1, 2.2, 2.3, 4 and 9). Every number here comes from the master. Where this file adds a number the master does not give, it says so and why.

## Goal

Terrain can read the climate without including `climate`. A new plain-data type, `terrain_climate_sample_t` (6 by 6 cells of 24 bytes, 888 bytes in all), lives in the terrain row. `game` fills it from `climate_cell_get` once per column job, after the column gate; the far field and mob spawn fill theirs; anywhere there is no climate, `terrain_climate_fallback(seed, x, y)` fills it from latitude and height alone with the same maths. Every number in it that can decide a block is worked out with a new libm-free `run/src/math/det/` (`det_exp`, `det_sin`, `det_cos`, `det_asin`, `det_atan2`, each within 1e-12 of libm and pinned), and stored as integers (centi-°C, mm, blocks). The column gate's read reach is 112 blocks, and a new world's spawn search runs after the climate is prepared. `terrain_generate_box`, the far-field sampler and `terrain_biome_column_at` take the sample, **but no biome rule reads it yet**, so every terrain digest holds. Filling a sample takes under 20 µs, and `terrain_climate_at` under 0.2 µs.

## Where this fits

**The vision.** Ahmed: "at world generation time please run through our climate mode first and then decide biome systems!!" Phase 02 is the plumbing that makes "climate first" possible: the climate's numbers reach the terrain's column jobs, deterministically, without a loop and without breaking the include ranks.

**What later phases read from it:**

| Reader | What it reads |
|---|---|
| Phase 03, biomes | `t_warm_c(z)`, `t_cold_c(z)`, yearly rain, driest month, monsoon flag, class, to pick the family |
| Phase 04, snow, ice, sand, trees | `snow_line_z`, `t_cold_c(z)` at the water's height, yearly rain |
| Phase 04, the far field | The same sample from its own sampler, tiles or fallback |
| Phase 05 | The version-2 pins, which include the `det_*` climate digests re-pinned here |
| Phase 01's F3 lines | Switch from `climate_normals_at` to `terrain_climate_at`, so the screen shows exactly what terrain sees, and `(fallback)` in test worlds |

**What this phase must NOT build:**

- **No biome, surface or feature rule change.** Every `run_terrain_*` pin holds. If one moves, it is a bug in this phase.
- No climate change apart from moving the sampled fields to `det_*` (master 2.3). The climate's inputs stay. Each climate digest that moves is re-pinned with a written reason.
- No change to the include ranks (`climate` 8, `terrain` 9, `compile/src/standards_lint/rules_include/index.c:62-73`). The sample is a seam.
- No new blocks.

## Ahmed's words

- "at world generation time please run through our climate mode first and then decide biome systems!!"
- "biome creation needs to properly follow ... align biome with climate"
- "EXTREMELY THOROUGHLY EXAMINE EVERYTHING NEEDED FOR THIS PLAN WHILE WE ARE STILL WRITING PLAN FILES! SO THAT WE CAN HAVE A SOLID IMPLEMENTATION!!"

"Climate first" is the spawn search moving after `climate_prepare_area`, and every column job filling its sample after the gate. "Solid" is the deterministic maths and the fallback: every caller that makes terrain gets a defined climate, and the same bytes on 1 or 16 workers.

---

## Background, explained

### Include ranks, and why a seam

`compile/src/standards_lint/rules_include/index.c:62-73` gives each system a row; **a lower row (a bigger number) may never include a higher one**. `climate` is 8 and `terrain` is 9, so terrain may not include `climate`: the climate reads terrain (`terrain_height_estimate`), and the reverse would be a loop. `math` is 11, so both may include `math/det`.

A **seam** is a plain-data type in the lower row that the higher row fills. `run/include/terrain/climate_sample/index.h` defines numbers only; `game` (row 2) includes both and copies climate cells into it. Terrain never knows the climate exists; it only gets a `const terrain_climate_sample_t *`.

### Why not just read the climate per column

Master section 0: a `climate_cell_get` costs about 4 µs and two `seq_cst` read-modify-writes per cell (the reader count of plan 004 phase 03's cache), and a column touches 11 to 19 biome tiles. `bench_terrain` is at 1134 columns a second against a target of 1000. Per-read cost would sink it. So each job copies the 36 cells it needs once, and then reads plain memory.

### The sample's size

A column is 16 by 16 blocks. Features reach 16 more blocks into neighbour chunks; the warp moves reads up to 32 blocks; the bilinear blend needs the next cell centre. From the column's west edge: $16 + 32 + 64$ (one cell for the blend) $= 112$ blocks west, and as many east past its 16 blocks: $112 + 16 + 112 = 240$ blocks, under $4 \times 64 = 256$. Cell alignment can add one more cell, and the margin adds one: **6 by 6 cells, 384 blocks**, "with room to spare" (master 2.1). Bytes: $16 + 36 \times 24 + 8 = 888$.

### Read reach 96 to 112

The column gate asks `climate_area_ready` for the column plus `CLIMATE_READ_REACH_BLOCKS` (`run/include/climate/cell/index.h:22`, 96 = one cell's blend 64 + the warp 32, asserted at `:49`), through `climate_tile_low` / `climate_tile_high` (`run/src/climate/index.c:210-215`). Features read 16 blocks further (a neighbour chunk), so a feature on the column's edge could read a cell whose tile the gate did not check. **112 = 96 + 16.** The assert becomes `CLIMATE_CELL_BLOCKS + CLIMATE_WARP_BLOCKS + TERRAIN_FEATURE_CHUNK` (or a named constant equal to 16 with a comment).

### Deterministic maths, and why libm is not allowed

`run/src/terrain/ore/README.md:87`: "nothing that decides a block may lean on how some libm rounds" (and `run/src/terrain/fields/README.md:68` for the same reason). glibc's `exp`, `sin`, `cos` and `asin` are correctly rounded on most inputs, but not promised so across versions or machines. A temperature 22.0000000001 against 21.9999999999 decides Continental against Boreal, and then which biome, which block. So:

- `det_*` are range-reduced polynomials with fixed coefficients, in `float64_t`, no libm, no `-ffast-math` dependence (they are written with explicit operation order; the build's flags already forbid contraction across statements, and the test pins the result bits).
- **Example, `det_exp(x)`:** $x = k \ln 2 + r$, $|r| \le \ln 2 / 2$, $k = \text{round}(x / \ln 2)$; $e^r$ by a degree-13 polynomial (Horner, fixed coefficients); multiply by $2^k$ by building the exponent bits. Error against libm under 1e-12 relative over $[-700, 700]$.
- The cell fields terrain reads are worked out with `det_*` in the climate's own parts (`heat`, `rain`, `season`, `harmonic`, `cell`), so the climate and terrain agree exactly, and the F3 readout and the blocks agree.
- **Integers at the seam:** `t_mean_cc = (int32_t)round_half_even(t_mean * 100)`. Terrain compares integers, so a last-bit difference cannot flip a block.

### The fallback climate

Plan 004 section 6.6's temperature with $\kappa = 0.5$ and no current, rain from the 6.7 latitude belts with moisture $M = 0.6$ and orography $O = 1$, then the class and snowline from those (master 2.2). It is a pure function of `(seed, x, y)` and defined everywhere, so tests, benches, test worlds, the far field past the keep square and the old spawn search all work. **Worked example to fill in during step 4:** at y = 0 (equator) the fallback's yearly mean at sea level equals plan 004's 27 °C within the κ = 0.5 term, and its `snow_line_z` is `TERRAIN_CLIMATE_NO_SNOW_LINE` (the master's 9999 "never lasts the summer").

### Lapse at the column's own height

`terrain_climate_at(sample, x, y, z)` takes z, and applies plan 004's 12 °C per 1000 blocks (0.012 °C a block) to the sea-level values. **Example:** `t_mean_cc` 2700 at sea level, z = 1250: $2700 - 1250 \times 1.2 = 1200$ centi-°C, 12 °C (master section 0 point 4). In integers: `t_cc(z) = t_mean_cc - (z * 12) / 10`, with the rounding written down.

---

## Design

### Directories and files

| Path | What | Tier |
|---|---|---|
| `run/src/math/det/`, `run/include/math/det/` (new) | `det_exp`, `det_sin`, `det_cos`, `det_asin`, `det_atan2`, float64, no libm | Pure functions |
| `run/src/terrain/climate_sample/`, `run/include/terrain/climate_sample/` (new) | The two types (master 2.1, sizes 24 and 888 asserted), `terrain_climate_at`, `terrain_climate_fallback`, the warp | Borrower: reads a sample, owns nothing |
| `tests/run/math/det/`, `tests/run/terrain/climate_sample/` (new) | Tests below | |
| `bench/terrain/` | Two new timings: filling a sample, `terrain_climate_at` | |

### The types (master 2.1, verbatim layout)

`terrain_climate_cell_t`: `t_mean_cc` (0), `t_swing_cc` (4), `rain_mm_year` (8), `snow_line_z` (12), `dry_month_mm` (16), `climate_class` (20), `flags` (21: sea, monsoon, warm or cold current coast), `_pad0[2]` (22); 24 bytes. `terrain_climate_sample_t`: `origin_x`, `origin_y` (0, 8), `cells[6][6]` (16, 864 bytes), `from_climate` (880), `_pad0[7]`; 888 bytes. `TERRAIN_CLIMATE_SIDE = 6`, `TERRAIN_CLIMATE_NO_SNOW_LINE` a named sentinel above the world top.

### `terrain_climate_at`

Result: `t_mean_c(z)`, `t_warm_c(z)`, `t_cold_c(z)` (warm = mean + |swing|, cold = mean - |swing|, with the swing's hemisphere sign), yearly rain, driest month, the snowline, the class of the nearest cell, flags. Bilinear between cell centres after a warp of up to 32 blocks, the same warp shape plan 004 phase 03 uses (but through `det_*` and the terrain's own noise), integer arithmetic throughout.

### The driest month (master 2.1, last paragraph)

One new value per cell: the driest month's rain. **Default:** worked out when the sample is filled, from `rain_mm_week[5]` (the cell's rain harmonics: $P_0$, $P_{1c}$, $P_{1s}$, $P_{2c}$, $P_{2s}$), by evaluating 12 month means with `det_cos`/`det_sin` and taking the smallest. This uses no cell byte. If the bench shows it costs too much, it moves into one of the cell's 12 spare bytes (`_pad1[12]` at 116, `run/include/climate/cell/index.h:86`) as mm in a `uint16_t`.

### Who fills it

- **Column jobs:** `game_column_job_run` (`run/src/game/index.c`) fills its sample from `climate_cell_get` after the gate in `game_columns_request` has said the tiles are ready; 36 copies, once per job.
- **The far field:** `render_lod_sample_terrain` (`run/src/render/lod/index.c:180-199`) fills one per level column: tiles where ready, fallback past them.
- **Mob spawn:** `mob/spawn/index.c:178` reads biomes through `terrain_biome_column_at`; it gets a small per-step sample at the player.
- **Spawn search:** `game_spawn_find` (`run/src/game/index.c:665`) moves after `climate_prepare_area` in `game_open`, so a new world's first columns use real climate. Test worlds and benches keep the fallback, labelled in their output.

### Signatures that change

`terrain_generate_box` (`run/include/terrain/index.h:92`), the far-field sampler, and `terrain_biome_column_at` (`run/include/terrain/biome/index.h:77`) take a `const terrain_climate_sample_t *`. Nothing inside reads it yet.

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/math/det/`, `tests/run/math/det/` (new)
- `run/{src,include}/terrain/climate_sample/`, `tests/run/terrain/climate_sample/` (new)
- `bench/terrain/`: the two timings

**Wiring outside them, each as small as possible:**

- `run/src/climate/{heat,rain,season,harmonic,cell}/`: the sampled fields through `det_*`
- `run/include/climate/cell/index.h`: `CLIMATE_READ_REACH_BLOCKS` 96 to 112 and its assert; the spare byte only if the default above is dropped
- `tests/run/climate/*`: re-pinned digests, each with its reason; `tests/run/climate/constants/` (`run_climate_constants`) as plan 004 phase 11's table says
- `run/{src,include}/terrain/index.{c,h}`, `run/{src,include}/terrain/biome/`: the new parameter only
- `run/src/game/index.c`: fill in `game_column_job_run`; move the spawn search in `game_open`
- `run/src/render/lod/index.c`, `run/src/mob/spawn/index.c`: fill their samples
- Every terrain test and bench caller: pass a fallback sample
- `compile/src/standards_lint/rules_include/index.c`: nothing (the seam needs no rank change); listed so an agent tempted to change it stops
- `plans/REPO_STRUCTURE.md`: rows for `math/det`, `terrain/climate_sample`, their tests

## Steps

1. **List every libm call on the path to the sampled fields** (`grep` over `run/src/climate/{heat,rain,season,harmonic,cell}/`; `kappa`'s $1 - e^{-d_s/1500}$ is one). *Why: the list is the contract for step 3, and anything missed leaves a libm dependency deciding blocks.*
2. **`run/src/math/det/`** with its tests against libm to 1e-12 over each range, and a digest pin over 65 536 fixed inputs. *Why: pinned first, before anything depends on it.*
3. **Move the sampled cell fields to `det_*`** in the climate's parts. Rebuild; every climate digest that moves is re-pinned in its test with a comment giving this phase and the reason; `run_climate_constants` updated. *Why: master 2.3, "climate digests change once, each with a recorded reason".*
4. **`terrain/climate_sample/`:** the types and asserts, `terrain_climate_at`, `terrain_climate_fallback`. Tests alone on hand-built samples. *Why: integer maths and the blend are tested before any caller.*
5. **Thread the parameter** through `terrain_generate_box`, the far-field sampler and `terrain_biome_column_at`; every caller passes a fallback sample. Run every `run_terrain_*`: all pins must hold. *Why: proves the plumbing moves no block.*
6. **`game` fills the sample** in `game_column_job_run`; the far field and mob spawn fill theirs. *Why: real climate where it exists.*
7. **Read reach 96 to 112** and the spawn search after `climate_prepare_area`. *Why: features on a column's edge read a ready cell; a new world starts on real climate.*
8. **Switch the F3 tuning lines** to `terrain_climate_at`, with `(climate)` / `(fallback)`.
9. **Benches** in `bench/terrain/`: fill under 20 µs, `terrain_climate_at` under 0.2 µs; `bench_terrain` before and after, recorded.
10. **Compare at three places** with plan 004's worked examples (Gate), READMEs, `plans/REPO_STRUCTURE.md`. Tick as each lands.

## Todo

From the master (section 9, phase 02):

- [ ] `run/src/math/det/` with tests against libm and a pin
- [ ] The climate cell fields that terrain reads move to `det_*`; climate digests are re-pinned with reasons
- [ ] The driest month's rain per cell (a spare byte, or from harmonics)
- [ ] `terrain/climate_sample/`: the types, `terrain_climate_at`, and `terrain_climate_fallback`
- [ ] `game` fills the sample in column jobs; the far field and mob spawn fill theirs
- [ ] The read reach goes from 96 to 112; the spawn search moves after the climate is prepared
- [ ] `terrain_generate_box`, the far field sampler and `terrain_biome_column_at` take the sample. **The biome rules don't change yet,** so every terrain digest holds
- [ ] Benches: filling a sample, and `terrain_climate_at`

Expanded:

- [ ] The libm inventory of step 1 written into this file
- [ ] `static_assert(sizeof(terrain_climate_cell_t) == 24)` and `sizeof(terrain_climate_sample_t) == 888`, with `offsetof` asserts
- [ ] `climate_tile_low` / `climate_tile_high` (`run/src/climate/index.c:210-215`) read the new reach; the gate test updated
- [ ] F3 lines read `terrain_climate_at`, show `(climate)` or `(fallback)`
- [ ] Every `run_terrain_*` pin unchanged
- [ ] `bench_terrain` at or above 1000 columns a second at its slowest place (1134 before), both numbers recorded here
- [ ] `standards_lint` (no new include across ranks) and `clang_format` pass
- [ ] The whole debug suite passes; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `det_functions_match_libm` (`run_math_det`) | Each `det_*` against libm on 1 000 000 points over its range | Within 1e-12 (relative for exp, absolute for the rest); `det_sin(0) == 0`, `det_cos(0) == 1` exactly | A coefficient or range reduction is wrong |
| `det_functions_are_pinned` | FNV-1a over the bits of 65 536 fixed inputs per function | Digest equals the pin, debug and release | Flags or compiler change a bit |
| `the_sample_sizes_are_the_master_s` | Asserts | 24 and 888 bytes | Layout drift |
| `the_blend_is_exact_with_the_warp_off` (`run_terrain_climate_sample`) | Hand-built cells, warp hook 0 | The bilinear worked example of plan 004 phase 03 in integers | Blend order or fraction wrong |
| `the_lapse_is_twelve_degrees_a_thousand_blocks` | `t_mean_cc` 2700, z 0 / 1250 | 2700 / 1200 | Wrong z or sign |
| `the_fallback_is_pure_and_defined_everywhere` | 100 000 points incl. poles, huge \|x\| | Same bytes twice; no NaN; class valid | A hidden state or a domain error |
| `the_fallback_matches_the_climate_at_kappa_half` | Where a real cell has κ ≈ 0.5 and no current | Temperatures within 0.5 °C | Formula drift between climate and fallback |
| `the_equator_has_no_snow_line` | Fallback and climate at y 0, z up to 1250 | `TERRAIN_CLIMATE_NO_SNOW_LINE` | Snowline maths wrong |
| `one_and_sixteen_workers_fill_the_same_samples` | Fill 4096 column samples on 1 and 16 workers | `memcmp` equal | Order dependence |
| `every_terrain_pin_holds` | The existing `run_terrain_*` suite | Unchanged | The plumbing changed a block |
| `a_feature_on_the_column_edge_reads_a_ready_cell` | Gate with reach 112 | Every cell `terrain_climate_at` reads for the column ±16 is ready | Reach still 96 |
| Bench: fill a sample | 36 copies | Under 20 µs | |
| Bench: `terrain_climate_at` | One call | Under 0.2 µs | |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_math_det$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_climate_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^(run_math_det|run_terrain_climate_sample)$' --output-on-failure
    tools/test-or-build.sh timeout 600 build/release/bin/bench_terrain
    pgrep -af "$PWD/build"    # must print nothing

The release run must print the same `det_*` and climate digests as debug.

## Gate

None for Ahmed (master section 9). The agent shows, in this file, that the F3 values match plan 004's worked examples at three places: the equator (yearly about 27 °C at sea level, no snowline), 45° N, and a high-latitude place, each with the values from `terrain_climate_at` next to plan 004's.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Terrain throughput falls below 1000 columns a second | A snapshot per job, integer reads; measured before and after (master section 6) |
| `det_*` moves climate digests and plan 004's pinned constants | Each move recorded with its reason; `run_climate_constants` updated with the table in plan 004 phase 11 (master section 6) |
| Fallback and climate disagree near the keep square's edge | The fallback matches the climate at κ 0.5; checked later on a long `/tp` flight (master section 6) |
| Moving the spawn search after `climate_prepare_area` changes where new worlds spawn | Expected for new worlds; existing worlds keep their saved spawn. Recorded |
| The driest month from harmonics costs too much in the fill | Move it to a `uint16_t` in `_pad1` (default stays harmonics) |
| Q4: `/tp` in test worlds | Yes, with the fallback (master Q4) |

## Commits

Appended as they land:

## Issues found

None.
