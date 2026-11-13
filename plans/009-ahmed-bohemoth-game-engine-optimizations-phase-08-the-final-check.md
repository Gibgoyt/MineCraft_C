# 009-ahmed-bohemoth-game-engine-optimizations — phase 8: The final check

**Status:** measured and checked; most targets missed (said plainly below); every gate is still Ahmed's   (updated 2026-11-14)

Read first: the phase 00 file, then the master: sections 7 (targets), 8 (tests), 9 (risks) and 12 (open questions). Then every earlier phase file's Results, Gate answers and Issues found, and `bench/world/README.md`, "Reading these numbers".

## Goal

Every target in the master's section 7 is measured like with like against phase 01's "Today", on Ahmed's card as it is, with its clock beside every number. Every suite passes. Ahmed has played and given his verdict. When this phase is done:

- **The targets table below is filled:** each row has today's value, the final value, the card's MHz, and met or missed, said plainly.
- **The full debug, sanitize and release suites pass,** with every heavy command through the mutex.
- **`bench/world/history.tsv`** has a final run at render distance 32 and at 64, labelled with the commit and the `MC_TUNE_HASH`.
- **The tuning YAML holds the values Ahmed chose** (E from phase 04, the internal scale from phase 07), committed.
- **Every README and plan document the plan touched says what the code now does:**
  - REPO_STRUCTURE: `compile/config/`, `compile/src/tuning/`, `run/src/render/lod/octant/`;
  - CODING_STANDARD: the tuning rule;
  - the renderer's READMEs: no "full detail";
  - the master's open questions: each answered or still open, with its default.
- **Ahmed's verdict** at his spot is recorded.

## Where this fits

**The vision.** Ahmed's words, all of them, checked:

| He said | Checked by |
|---|---|
| "Full detail" should never ever be a thing | `grep -rni "full.detail" run cuda compile bench` finds nothing; the Settings screen has five sliders |
| a 2x2x1 group … rendered … as the larger rectangle … one shape | The pyramid's reduction tests (phase 03); merged boxes' step counts (phase 05) |
| resolution drops from really close … further optimization as we move away | The bands at the chosen E; step bands at his spot |
| at all times 16 blocks in all directions VIEWABLE … rendered properly | The near-guarantee test, run once more here on every place, at every E he tried |
| research … unreal engine and godot | Master section 3 and its sources |
| config settings … read at compile time from a yaml file … ./compile/config/ | `compile/config/`, one edit plus one build (phase 02's gate, repeated) |

**What this phase must NOT build:** nothing new. A fix found here is a small commit named in Issues found. A larger one goes back to its phase, reopened.

## Ahmed's words

- "this way in the future we can very easily change numbers to get the sweet spot of exactly what we need!!"

---

## Targets (filled in by this phase)

**Read this first: the land moved.** Phase 01 measured his spot with the feet at z 2, on the land of its day, which looked like 5.png. Plan 007 phases 03 and 04 raised and reshaped that land. The bench now puts the feet on the ground at x -385, y 347, which is z 271 on plan 007 phase 04's land. From there the view is a close bush and a snowy ridge (`ahmed_slope/night-64`, picture `D9C4D51544904C5F`), not 5.png's. So "Today" and "Final" are **not like with like** for any row taken at his spot. The like-with-like comparison is on today's land, final against the reference and against level 0 held at 32 chunks, both run back to back at the same 148 MHz (Results). All final rows were taken with the card held at its floor, 147 to 149 MHz, on `main` at 5cba451b plus this branch.

| Measure | Today (phase 01) | Final | MHz | Target | Met? |
|---|---|---|---|---|---|
| Card time at his spot, capped, p99 over 240 settled frames | 46.61 ms (old land) | 46.62 ms, 6.79 Mcycles | 148 / 148 | ≤ 16.7 ms | **Missed.** At 148 MHz no picture of this view comes near 16.7 ms. 16.7 ms would need about 410 MHz at the final 6.79 Mcycles. |
| The same, mean | 43.69 ms (old land) | 45.69 ms | 148 / 148 | ≤ 14 ms | **Missed**, for the same reason. 14 ms is 485 MHz at 6.79 Mcycles. |
| Stretch: cool card, if measured | not measured | not measured | — | ≤ 8.33 ms | Not measured: the card was never cool in this run. |
| Near guarantee | n/a | 0 hits and 0 colours differ (`render_feature_lod_bands`: 6 eyes × E 2 and 8 × FOV 70 and 30) | — | byte-identical | **Met** for hits. Colours: see Issues found 3, the E 8 leaves. |
| Error share past 16 blocks at the chosen E | n/a | 2.26 % past E (2.39 % past 2 E after plan 007 phase 04) looking north; 0 to 2.7 % by eye | — | ≤ 1 % | **Missed**, by construction (phase 04, Issues 2). The test now holds 3 % past 2 E. |
| Mean primary steps a pixel at his spot | 9.06 primary; 19.9 with the far field and far_past (old land) | 15.22 primary; 19.7 with far 7.6 M and far_past 1.7 M | 148 | −40 % | **Missed**, and not comparable (other view). On today's land: 15.2 against the reference's 18.6 (−18 %). |
| Shadow-ray steps at his spot | 0.24 M at night, 22.9 M by day (old land) | 0.32 M at night, 1.3 M by day | 148 | −50 % | By day **−94 %**, but the new view has few faces to the sun: not like with like. Phase 06 measured the cone as saving no step on the old view (Issues 3 there). **Not met** as a like-with-like result. |
| Water walk on `deep_ocean/orbit` at 64 | 13.73 ms primary (plan 005 phase 8) | phase 05: −9 % at a like 300 MHz (uniform bricks); this run 112.95 ms card mean, clock not read | — | −50 % | **Missed** (−9 %). |
| `bench_world` rows at 32 | `history.tsv` | `009 final: rows at 32` | not read by `bench_world` (card at 300 MHz and 82 °C after the run) | none > 3 % slower | **Cannot say.** The land changed under every row (plans 006 and 007), so no picture matches an earlier line, and `bench_world` does not read the clock. Against `009 phase 04: rows at 32`, also at the floor: 9 rows faster, 9 slower, up to +47 % (`jungle/flight-live`) and +224 % (`deep_ocean/flight-live`, a live row whose world differs). |
| Settings screen | 6 sliders | 5 sliders, no "Full detail" | — | 5, no "Full detail" | **Met** (phase 04; `run_ui_screens` goldens pass). |
| VRAM at 64 | 2776 MiB voxels (full detail 32) | 2521 MiB voxels at E 2 | — | ≤ today | **Met** (−9 %). The reference, level 0 to 64, is 5639. |
| Fill-in at 64 | 7.66 s | All drawn at 3.95 and 4.21 s (`rd_64`) and 5.25 and 6.01 s (`rd_64_again`), release, 640 × 360, on plan 007 phase 04's land | — | no worse | **Met** for the whole fill. But the test's "half of it shows within 1 s" now fails on both (Issues found 7). At 1920 × 1080 the far field of a world opened at 64 is filled at 13.6 s (`render_feature_lod`'s edit test), which is a different measure. |
| Edit to every level | up to 1 s | 19.2 ms to level 1, 19.5 ms to level 2 (release, final run; 13.8 and 14.1 before the rebase) | — | ≤ 100 ms | **Met.** |
| Tuning edit to new numbers | n/a | Only the dependents rebuild (phase 02, issue 4) | — | only dependents rebuild | **Met**, measured by phase 02, not run again. |

**The next levers, for the misses.** These are in order of what the numbers say they are worth at his spot today:

1. **The card's clock.** The same picture is 3 to 4 times faster at 450 to 600 MHz than at the 148 MHz floor where every row here ran. Plan 008's question of the power cap is the largest single lever.
2. **The far field at this view.** It is 8.3 of primary's 18.6 ms. The reference (level 0 to 64, no far field) is **10 % cheaper** in cycles than E 2: 6.11 against 6.79 Mcycles, in a back-to-back pair at 148 MHz. That costs 5639 against 2521 MiB of voxels. At this view, E 2's far field buys VRAM, not time.
3. **Render scale 75.** It takes the ray passes to **63 %** of 100 %'s in a back-to-back pair at the same 148 MHz (25.49 against 40.18 ms; card total 30.60 against 45.69). That is near phase 07's 62 % target, which no earlier pair could show at a like clock. Q9's default stays 100 until Ahmed has looked.
4. **E.** Phase 04 measured E 4 at −13 % and E 8 at −14 % at 148 MHz, against E 2's −4 %.
5. **AO** is 9.2 ms of shade's 21.5, the largest single kernel. That belongs to plan 008, not here.

## Results (filled in by this phase)

### His spot on today's land (`bench_world_slope`, profile, 1920 × 1080, render distance 64, feet at z 271)

Each row ran alone in its own wrapper call, back to back, all at the card's floor. Every row is in `bench/world/slope/history.tsv`, labelled `final-*`.

| Row | Level 0 | Card ms (p99) | Mcycles at MHz | Primary (far) / shade (AO) / post | Voxels MiB | Steps a frame | Picture |
|---|---|---|---|---|---|---|---|
| `night-64`, E 2 | 27 chunks | 45.69 (46.62) | 6.79 at 148 | 18.58 (8.33) / 21.51 (9.16) / 4.24 | 2521 | primary 31.6 M (15.2 a pixel), far 7.6 M, far_past 1.7 M, sun 0.32 M, AO 3.8 M | `D9C4D51544904C5F` |
| `day-64`, E 2 | 27 | 44.85 (45.90) | 6.66 at 148 | 18.03 (7.77) / 21.31 (9.16) / 4.14 | 2521 | as at night; sun 1.3 M | `FA5774EA027C63C8` |
| `night-64`, level 0 held to 32 chunks (phase 01's full detail 32) | 32 | 45.73 (46.62) | 6.79 at 148 | 18.59 (8.35) / 21.53 / 4.24 | 3194 | the same to 0.1 M | `7E1082A76E6BE2CA` |
| `night-64`, reference (level 0 to 64, no far field) | 64 | 41.19 (42.17) | **6.11** at 148 | 12.19 (0) / 23.39 (10.25) / 4.24 | 5639 | primary 38.5 M (18.6 a pixel), sun 0.5 M, AO 4.3 M | `255791DD46769E7D` |
| `night-64`, render scale 75 | 21 | 30.60 (31.28) | 4.54 at 148 | 11.64 (5.47) / 13.78 (5.30) / 3.84 | 1770 | — | `E23D7F2AC6E9AE61` |

- Holding level 0 to 32 chunks changes nothing at this view. The bands, not level 0's radius, decide where each pixel's ray hands over.
- Primary's step bands at night (`--steps`): 7.4 M in 0-16 blocks, 6.3 M in 16-64, 6.6 M in 64-128, 6.4 M in 128-256, 4.9 M in 256-512, none past. Every far step is past 256 blocks: 5.8 M on level 1, 1.8 M on level 2. Sun rays at night are 99 % within 16 blocks of their surface.

### `bench_world` (profile, recorded with `tuning` = the build's `MC_TUNE_HASH`)

- **`009 final: rows at 32`**: exit 0. 0 of 16 rows at 120 fps, as the card was at its floor. Voxels 2309 MiB at 32 (2650 in phase 01).
- **`009 final: rows at 64`**: exit 0, 700 s. Voxels 2521 MiB at 64, 2724 at 128, 3033 at 512. `deep_ocean/orbit` 112.95 ms mean. `jagged_peaks/orbit` 28.81. `jungle/orbit` 87.84.
- Neither run reads the card's clock (`nvidia-smi` read 300 MHz and 82 °C straight after). The numbers are recorded, but they are not a comparison.

### `bench_lod` (release)

A level 1 column from octants is **12.7 %** of the bytes of the blocks under it, the same as phase 03 found. Assembling it takes 349 µs on the mean on the gpu thread (phase 03: 201.5 µs, on the old land), and up to 621 µs at (-6000, -500). Plan 007 phase 04's land holds more non-uniform sections: 1588 KiB of blocks a column on the mean, against phase 03's 1043.5. The worker column times are as before: 21.2 ms on the mean and 30.4 ms at worst for voxels of 2, against the 50 ms target.

## Todo

- [x] Ahmed's game is closed before any card work (`pgrep -af 'build/release/bin/minecraft_c'` prints nothing), or he is asked and waited for
- [ ] Debug suite, sanitize suite, release build: all pass. Debug 177/177 and the release build pass. The release suite is 176/177: `run_render_distance_live`'s `rd_64` checks fail on plan 007 phase 04's land (Issues found 7). Sanitize was not run (Issues found 8)
- [x] `bench_world` full run at 32 and at `--render-distance 64`, recorded with the commit and `MC_TUNE_HASH`
- [x] `ahmed_slope/night-64` and `day-64` with `--steps`; the targets table filled, each row met or missed said plainly. The spot is on new land, so the comparison is not like with like (Targets)
- [x] Near-guarantee test on every place at the chosen E, FOV 70 and 30, render scale 100 and 75. `render_feature_lod_bands` covers E 2 and 8 at FOV 70 and 30. Render scale 75 at E 2 is E 2.67 to the bands, which falls between the two (Issues found 3)
- [ ] `grep -rni "full.detail" run cuda compile bench` finds nothing. It finds 6 lines, each kept on purpose (Issues found 6)
- [x] REPO_STRUCTURE, CODING_STANDARD and every touched README read against the code (REPO_STRUCTURE's `render/lod` row corrected and its `render/octant` row added)
- [x] The master's open questions: each answered, or left with its default, listed in this file
- [ ] Chosen E and internal scale committed in `compile/config/`. Ahmed has chosen neither: E stays 2.0 in `compile/config/render/lod/index.yaml`, and the internal scale is the Settings value `render_scale_percent` (100), not a tuning key
- [ ] Ahmed plays his slope at night and by day, at render distance 64, and gives his verdict
- [x] Nothing left running: `pgrep -af "$PWD/build"` prints nothing (and no Xvfb). Also, `git ls-files '*.c' '*.h' '*.cu' '*.hu' | grep -v fixtures | xargs wc -l | awk '$1>2000 && $2!="total"'` prints nothing

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                                    # must print nothing
    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1800 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --preset sanitize
    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 3600 ctest --preset sanitize --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --record bench/world/history.tsv --label "009 final: rows at 32"
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --render-distance 64 --record bench/world/history.tsv --label "009 final: rows at 64"
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/night-64 --steps /tmp/mc_c_009/final/night.tsv --shots /tmp/mc_c_009/final
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --only =ahmed_slope/day-64 --steps /tmp/mc_c_009/final/day.tsv --shots /tmp/mc_c_009/final
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"                                                       # must print nothing

## Gate

Ahmed runs, himself:

    ./build/release/bin/minecraft_c --menu

He stands where 5.png was taken, at night, render distance 64, F3 up. He looks up the slope, turns, walks, mines a block 20 blocks away and one far off, and looks again by day. He says:

1. What F3's FPS and gpu total read, against 31.9 and 29.44 ms.
2. Whether anything within 16 blocks looks different from before. It must not.
3. Whether the far slope, the ridge and the shadows look right.
4. Whether changing a number in `compile/config/` and rebuilding was as easy as he wanted.
5. His verdict on plan 009.

His answers are recorded below.

## Risks and open questions

| Risk or question | Default |
|---|---|
| The card's clock differs between phase 01 and now | MHz beside every number; rows repeated; like with like only |
| A target is missed | Said plainly; the next lever named (E, internal scale, a phase 05 experiment revisited, or plan 008's work) |
| Plans 006 and 008 landed changes that move numbers | Phase 01's rows re-run on today's `main` with 009's tuning set to "off" where possible, and the comparison stated |

## Commits

Appended as they land:

- `3980713a` Say in REPO_STRUCTURE what run/src/render/lod/ now is, and add the row for run/src/render/octant/.
- `755b9ea2` Have run_render_distance_live mine a log with no log right behind it.
- `9e449082` Hold the R=512 cost test's 3 GiB of voxels without the far turn cache's fixed 96 MiB.
- `0701b816` Record the final rows at Ahmed's x and y on plan 007's land, in `bench/world/slope/history.tsv`.
- this branch, after it: bench_world's final rows at 32 and 64, in `bench/world/history.tsv`; and this file.
- Dropped in the rebase: my fix to `run_terrain_feature_own`'s 23 KiB frame in the release build. Plan 008 phase 07 landed the same fix first (`d0eb3162`).

## Issues found

### 1. Ahmed's gate list (all still his)

None of plan 009's gates has been answered. Every one waits for Ahmed, at his own terminal, with nothing else on the card:

    ./build/release/bin/minecraft_c --menu

1. **Phase 01, Q2, Q5, Q10:** read phase 01's table, heatmaps and step bands. Then say his seed and view (Q2), whether shadow rays are skipped under 2/255 (Q5), and whether the targets stand (Q10). Note that **his spot of 5.png no longer exists**: plan 007 put 270 to 300 blocks of land on it. He may want to name a new spot.
2. **Phase 02:** change `ao_turns` from 4 to 2 in `compile/config/render/shade/index.yaml`, run `tools/test-or-build.sh cmake --build --preset release`, see the new value and `MC_TUNE_HASH` in `./build/release/bin/minecraft_c --report`, then put it back.
3. **Phase 03:** read the store size (172 MiB of host memory at his spot today), the upload (1/7 more than level 0) and the VRAM at 64 (2521 MiB), and the edit latency (14 ms).
4. **Phase 04, choose E:** play at render distance 64 with `pixel_error` 2, 4 and 8 (`compile/config/render/lod/index.yaml`, one release build each). Look for a ring or band, ground that steps, and anything within 16 blocks that changed. E 4 measured −13 % and E 8 −14 % at 148 MHz. Write the chosen E to the YAML.
5. **Phase 05:** read the merged-boxes table: uniform water bricks kept (−9 % on `deep_ocean/orbit`), coalescing and the cheap per-brick distance dropped, the coarse pass on the pyramid and mirroring not tried. Then say go or stop.
6. **Phase 06:** look at shadows by day and by night. The far shadows on near ground are new. The step target was missed, as shadow rays were already short.
7. **Phase 07, Q9:** set render scale 75 in Settings, and look standing still and turning. It costs 63 % of 100 %'s ray passes at a like clock (Results). Say whether the default goes to 75.
8. **This phase:** at render distance 64, F3 up, at night and by day: FPS and gpu total, whether anything within 16 blocks differs from before, whether the far ground and the shadows look right, whether the tuning edit was easy, and his verdict on plan 009.

### 2. The master's open questions (section 12): none answered, each on its default

| # | Default in force |
|---|---|
| Q1 | `compile/config/` (built so) |
| Q2 | Seed `14148492376765118422`, `--look 8 34`. The feet are now put on the ground (z 271 on plan 007 phase 04's land), so this is no longer 5.png's view |
| Q3 | `render_distance_default` and `_max` stay in `config/machine.yaml` |
| Q4 | Capped at `block_radius_max_chunks` 48 |
| Q5 | Yes, at 2/255 (`shadow_skip_below` 0.0078). It fires only within 0.15° of the horizon (phase 06, issue 2) |
| Q6 | One E for all. The error share is over 1 %, which by this question's rule would call for per-kind E, but per-kind E is not built |
| Q7 | No software VRS in this plan |
| Q8 | No shadow cache in this plan |
| Q9 | 100 % until Ahmed has seen 75 |
| Q10 | The targets as written |
| Q11 | No runtime tuning override |

### 3. E 8 drew sky behind near leaves (plan 006 phase 04's question): looked into, not settled

In `render_feature_lod_bands`, at "north, over it", E 8, FOV 70, 21 pixels show this. The first surface is leaves 14.3 to 15.1 blocks off. The reference ends on ground 25 to 42 blocks behind them. In the banded picture shade's own ray goes past the leaves and ends on **sky**, and the far field holds **no surface** for those pixels (`lod_hit_t.material` is `LOD_NO_MATERIAL`, `t` is the sky's). At E 8 and 180 rows, d_1 is about 16 to 17 blocks, so that ground lies inside level 1's band (16 to 32 blocks), where it should be met. What was ruled out:

- **Columns not built.** Settling the far field again after a picture at E 8, before drawing (so that the bands of E 8 had decided which columns are "covered"), gave the same 21 pixels.
- **The turn cache.** The finding is older than plan 008 phase 06.

What is left is between `lod_past_kernel` (whether it asks for a far ray), `lod_goes_out` and `lod_trace` for a ray that starts at d_1 just past leaves. The next step is to print, for pixel (211, 27) at that eye, the ask, `t_from` and each level's `t_stop` inside `lod_frame_kernel_body`. That needs a CUDA rebuild and is not a small fix, so it was left.

The two pixels at E 2 in the same eye are another matter. There the reference has sky and the colours picture has a far surface at 307 blocks: that is a coarse level missing a thin far surface, not a bug. The test keeps its exception for these pixels (colour not held where the reference ends past 16).

### 4. Picture hashes of `jungle/orbit` vary from run to run: the far field

On today's code `jungle/orbit` at 32 gave a different settled picture in each of four runs: `46FCDE2BC31FDB5D`, `6FDEA2F71410C85D`, and, with the turn cache off (`--features-off 100000`), `E47976D93F082EA6` and `D3D4C248562EF90A`. **With the far field off as well** (`--features-off 108000`), two runs gave the same `FF8E286D9AFEE41C`. So the cause is in the far field, and it is not the turn cache. The likeliest cause, not proven, is that which level 1 and 2 columns are made from octants and which from terrain at the moment of each picture depends on worker timing. The two hold different bytes: octants have the caves and ores, terrain sampling does not. Issue 5 points the same way. Until this is fixed, a row whose picture shows the far field cannot be compared by hash.

### 5. The pyramid keeps remaking columns where the land is busy (plan 008 phase 07's finding): confirmed, not fixed

Plan 008 phase 07 found that at its spot (-80 134 418) level 1's pool filled (479 to 653 columns with no room), and that 11 564 columns and 3.7 GiB went out in a 90 s still row, keeping the gpu thread busy 2 to 13 ms a picture. Here, `bench_world_slope` at that spot (render distance 64, E 2, 1920 × 1080) had **0 columns with no room**, but **5109 level 1 and 2978 level 2 columns made from octants** for windows of 55 × 55 = 3025 and 37 × 37 = 1369 columns. That is 1.7 and 2.2 times each, and 1.9 GiB in a 34 s run. At Ahmed's x and y it was 3259 and 1305 (1.1 and 1.0 times). Level 1's pool there held 267 of 306 MiB.

Plan 007 phase 04's land holds more non-uniform sections: `bench_lod` measured 1588 KiB of blocks a column on the mean against 1044 on the old land. So `octant_pool_kib_per_column` (192) is now tight. A NO_ROOM column also goes back to WANTED whenever anything under it changes (`render_lod_level_column_changed`), and is tried again.

Two things are worth doing in their own phase:

- Size the pool for this land, by raising `octant_pool_kib_per_column` and measuring the VRAM.
- Stop a level 1 column made from terrain being made again from octants (`render_lod_terrain_sent`), and the level above it again with it, more than once per real change.

The row at that spot reads `NOT SOUND`: the eye at z 418 looking up 34° sees mostly sky.

### 6. "Full detail" left in the tree on purpose

`grep -rni "full.detail" run cuda compile bench` finds 6 lines, none of them a setting:

- the settings loader's handling of an old file's `full_detail:` line, and its log message (`run/src/settings/persist/index.c`), and its README;
- the `full_detail` column name in `bench/world/slope/history.tsv`'s header, kept so that phase 01's lines still read; the column now holds level 0's radius;
- two lines of `bench/world/slope/README.md` that say what was so before phase 04.

### 7. The release build and suite: three failures, all in tests, all fixed

The first release run after the rebase onto 5cba451b failed to build `run_terrain_feature_own` (a 23 168-byte frame under LTO; plan 008 phase 07 landed the same fix as `d0eb3162`), and 3 tests failed:

- **`run_render_distance_live`**: the mined log never left the crosshair's square (`log_32`, `log_64_busy`). The nearest lit log was in plan 006 phase 04's 2 × 2 jungle trunk, so mining it showed the same bark behind. The test now looks for a log with no log right behind it. In the debug build the deadline is not held, which is why the debug suite passed.
- **`render_feature_lod`**: the R=512 cost test (release only) held the voxels pool under 3 GiB. Plan 008 phase 06's 96 MiB turn cache took it to 3129 MiB (3033 without it). The test now holds the 3 GiB without the turn cache's block, and prints that block's size.
- **`run_terrain_landform_measure`**: failed only because the build had stopped before its program was linked.

**The final suites, after the rebase onto `d6f3ce12`** (plan 007 phase 04's land and plan 008 phase 07):

- **Debug: 177 of 177 pass.**
- **Release: 176 of 177 pass.** `run_render_distance_live` fails twice in a row, the second time run alone, on the same two checks. The mined logs now go at 0.02 s, so that fix holds.
  - `rd_64`: "by 1 s the picture had come 48 % (then 50 %) of the way to its last, not half". The whole change is only 1.2 % of the picture, and half of it comes at once. The rest comes from 2 to 3 s.
  - `rd_64_again`: "−108 %". The picture first moves **away** from its last (2.23 % differing, then 4.65 % at 0.5 to 1 s, then 1.34 % at 2 s) and settles by 5 s.

  Both passed in this branch's release run on `905dcf7f`, before plan 007 phase 04's land. The far field showing one picture and then another (terrain-made columns, then octant-made ones, Issues 4 and 5) fits "moves away, then back". That points at the same cause as Issues 4 and 5, which is not a small fix. The test's checks were not loosened. The whole fill still finishes by 4.2 s and 6.0 s, against the 15 s target.

### 8. Not run: the sanitize suite

The phase's Verification lists the sanitize preset. It was not run: `/home` had 11 to 19 GiB free while the debug, release and profile builds stood together, and the orchestrator asked for the debug and release suites. Plan 008 phase 07 ran tsan and fixed one link error (`98a7dc09`).

### 9. The disk

`/tmp`'s quota filled once during this phase (the shared scratchpad held 19 GiB) and lost one command's output. The orchestrator cleared it. `/home` is at 94 %.
