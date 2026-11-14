# 007-ahmed-bohemoth-realistic-landforms — phase 6: Tuning, version and the final check

**Status:** tuning done (2026-11-14); the version, old worlds, surveys and the full final check are the joint step with plan 006 phase 05, not done here   (updated 2026-11-14)

Read first: `plans/007-ahmed-bohemoth-realistic-landforms-phase-00-vision-and-plan-files.md`, then the master `plans/007-ahmed-bohemoth-realistic-landforms.md` (sections 2.4, 4, 6, 9, 12), then every earlier phase file's Issues found and gate notes. Then `plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-05-version-old-worlds-and-final-check.md`, because by default this phase runs **together** with it (master Q2). Every number here comes from the master unless it says otherwise.

## Goal

**Ahmed has tuned the landforms in his own game and with `bench_landform`.** Every value he changed is recorded here with the reason: plateau size and tiers, escarpment steepness, range spacing, ruggedness mix, knob count, basin count.

**He has decided the cooling with height** (master 2.4, Q6). If it changed, `CLIMATE_LAPSE_C_PER_BLOCK` and every pin it moves are updated, with the reason.

**The generator version is closed:**

- **By default,** this phase runs as one joint final check with plan 006 phase 05. `TERRAIN_GENERATOR_VERSION` is **2**, and row 2 of `FEA_DIGEST_OF_GENERATOR` names both plans and is marked closed.
- **If plan 006 phase 05 already shipped version 2,** this phase makes it **3**.

**Old worlds** are listed with sizes and deleted once, unless Ahmed names one to keep.

**The full checks pass:**

- the debug and release suites, and a release build;
- 1 against 16 workers on the final pins;
- `bench_terrain`, `bench_lod`, `bench_climate` and `bench_atlas` inside their budgets.

**`bench_landform`'s "after" table** stands beside phase 01's "before" in this file, with master section 4's targets.

**Ahmed's walk** (coast, escarpment, plateau, koppie, range, summit, basin) is recorded in his words.

## Where this fits

**The vision.** Plan 007 makes the landform layer. This phase is where Ahmed tunes it and where the land is frozen as a version, so saves can trust it.

**What reads it:**

| Reader | What |
|---|---|
| `run/src/save/world/` | `TERRAIN_GENERATOR_VERSION` in each manifest (`:505`, compared at `:550`) |
| `tests/run/terrain/feature/` | `FEA_DIGEST_OF_GENERATOR[TERRAIN_GENERATOR_VERSION]` |
| Plan 006 phase 05 | The same bump, done once |
| Later plans (rock layers, settlements) | The final landform numbers and the tool |

**What this phase must NOT build:**

- No new landform. A change of shape found necessary here goes back into the phase that owns it as an Issue found, then is done and recorded here as a tuning value.
- No migration of old saves (master section 9; plan 006 section 7).
- No edit to plan 006's or plan 004's files. Joint results go here, and plan 006's own phase file records its side.
- No push.

## Ahmed's words

- "please make terrain generation much much more realistic!!"
- "I did notice that climate drops as I am climbing, maybe increase climate drop a little bit? not sure what the current temp vs altitude rate is at"
- Plan 006's decision, 2026-11-12, on old worlds: "**Delete them.** The generator version goes to 2"

---

## Background, explained

### Tuning by playing, with numbers kept

Every value tuned is one constant or one knot. A tuning round:

1. Ahmed names what feels wrong ("the plateau is too small", "the face is too gentle").
2. The agent changes one value.
3. `bench_landform` runs on the 8 seeds.
4. The numbers and pictures are recorded here.
5. Ahmed looks again: `/tp` to the printed place, `/fly`, F3, the Atlas.

Nothing is tuned against a single seed. Section 4's targets are checked after each round. A value that breaks one is recorded with which.

### The lapse rate question

Today it is 0.012 °C a block, 12 °C per 1000 blocks (`run/include/climate/heat/index.h:12`). That is almost twice Earth's 6.5 °C per km, chosen "Fast" on 2026-11-04.

| Choice | 650 plateau at the equator | Summit 1100 | Snowline |
|---|---|---|---|
| **0.012** | 19.2 °C | 13.8 °C | today |
| 0.015 | 17.3 °C | 10.5 °C | × 0.80 |
| 0.020 | 14.0 °C | 5.0 °C | × 0.60 |

**What changes with it:**

- every climate digest;
- plan 004 phase 11's constants table, whose "after" goes here;
- every snowline (plan 006 phase 04's tests, which use the climate's `snow_balance_z`);
- the HUD's temperature at the feet: `T_HUD = T_now - 0.012 (z_feet - z_ground)` in `run/src/game/corner/`, which reads the constant.

### Why one bump

A save holds only edits over generated land. A world saved on version-1 land and opened on new land would stitch edits onto a different landscape. So the version is raised, and old saves are refused with the existing message. Two plans changing land in the same weeks should raise it once, so Ahmed loses his test worlds once.

---

## Design

### The joint final check (default)

If plans 006 and 007 have both finished their land phases, this phase and plan 006 phase 05 are run **by one agent in one sitting**:

1. **Version.** `TERRAIN_GENERATOR_VERSION` = 2 (already opened by the first land change, master section 9). Row 2's comment becomes: "Plan 006 phases 02 to 04 (biomes, snow, ice, sand and trees from the climate) and plan 007 phases 03 to 05 (plateaus, escarpments, ranges, slope profiles, knobs, dry basins). Closed 2026-MM-DD."
2. **Pins.** Every terrain, climate, Atlas and render pin is checked against its last recorded reason. A pin that moved without one is an Issue found, and the work stops until it is explained.
3. **Old worlds.** `saves/test_1` to `test_8` (plan 006 phase 05's table), plus any made while the version was open, are listed fresh with `du -sb`, `du -sh` and the last changed date in this file. They are deleted unless Ahmed named one. `saves/` is ignored by git, so no tracked file changes.
4. **Suites, benches, 1 against 16.** As Verification.
5. **Surveys.** Plan 004 phase 11's biome-against-class survey (plan 006's) and `bench_landform`'s table, both on the final land.

### Alone (if plan 006 phase 05 already shipped version 2)

- `TERRAIN_GENERATOR_VERSION` = 3, with row 3 naming plan 007.
- Version-2 worlds are listed for Ahmed.
- The rest is as above.

### The before and after table

Filled by this phase from `bench_landform` on the 8 seeds:

| Measure (master section 4) | Before (phase 01) | After | Target | Met |
|---|---|---|---|---|
| Flat / gentle / moderate / steep / very steep / cliff / sheer | 1.0–1.2 / 8.8–10.0 / 21.7–23.0 / 24.8–25.4 / 24.4–25.8 / 15.1–16.1 / 1.4–1.6 | 16.8–22.2 / 33.9–37.6 / 18.2–20.7 / 10.7–13.2 / 7.1–9.2 / 3.4–5.1 / 0.6–1.1 (Ahmed's seed 23.0 / 36.8 / 18.7 / 10.8 / 6.8 / 3.3 / 0.6) | 30–40 / 25–35 / 15–20 / 7–12 / 3–6 / 1.5–3 / 0.3–1 % | Moderate, steep and sheer in or at their bands; flat, very steep and cliff not (Issue 6) |
| The same from real blocks | 1.1–1.8 / 6.5–9.5 / 17.0–22.0 / 26.4–28.3 / 22.4–26.2 / 13.5–17.7 / 2.5–4.3 | 8.9–11.1 / 27.0–31.6 / 28.9–31.0 / 15.3–18.0 / 7.5–10.0 / 3.3–5.4 / 0.6–1.5; 8.8–12.6 points from the profile | (difference under 5) | No (Issue 6: whole blocks) |
| Of ground steeper than 1:2, high up or on faces | 10.9–11.5% | 18.5–21.5% | ≥ 70% | No |
| Top decile over bottom 3, median slope | 2.45–2.62 | 1.52–1.89 | ≥ 5 | No |
| Concavity ratio, median | 1.39–1.44 (all peaks) | 2.23–2.67 on range peaks (297–433 a map; Issue 3) | ≥ 3 | No, closer |
| Plateau pieces ≥ 25 km²; largest | 0 | 0–5, 2 or more on 4 of 8; largest 15.6–88.9 km², 5.6–17.6 km across (on the ground less its knobs, Issue 2) | ≥ 2; ≥ 80 km², ≥ 8 km | On 4 of 8; 80 km² on 1 |
| Escarpment length; facing the sea | 0 | 706–1722 km; facing not measured (phase 03: 99.6%) | ≥ 30 km; ≥ 75% | Yes |
| Longest range at ≥ 500 | 3.9–6.0 km | 10.4–36.5 km, 20 or more on 4 of 8 | ≥ 20 km on 6 of 8 | No (5 of 8 before) |
| Knobs on a base ≥ 250 | 12.6–15.2% | 18.8–43.4%, 25 or more on 3 of 8 | ≥ 25% | No (8 of 8 before) |
| Dry basins; floors | 0 | 1.75 a map (1, 1, 0, 0, 2, 3, 4, 3); −50 to −100 | 1–3; −40 to −100 | Yes |
| Sea | 25.7–27.4% | 24.4–29.9% | 25–30% | Yes (24.4 and 24.5 on two) |
| Top | 1166–1181 | 1123–1135 | 1100–1190 | Yes |
| Floating rock per km² | 1072–1383 | 707–1690 | ≤ 1729 | Yes, all 8 |
| `bench_terrain` slowest | 843–944 (bench_landform's tall place) | jungle 975 and 959 | ≥ 1000 | No, by 2.5–4% |
| `bench_lod` per column | | worst 31.4 ms (shift 2) | ≤ 40 ms | Yes |
| `bench_climate` cold tile | | not run here (the climate is unchanged) | < 100 ms | For the joint check |

---

## Owned paths

This phase may edit these, plus this file:

- Any constant or knot in `run/src/terrain/landform/` and `run/src/terrain/density/` (tuning only; each recorded)
- `run/include/terrain/index.h` (the version), `tests/run/terrain/feature/index.c` (the row), every pin with its reason
- `run/include/climate/heat/index.h` (the lapse constant, only if Ahmed picks another) and the pins and constants table it moves
- `bench/*/README.md` (results)
- `saves/test_*` (deleting, after listing)

## Steps

1. **Read** every earlier phase's Issues found and gate notes; list what Ahmed asked to change. *Why: tuning starts from his words, not from scratch.*
2. **Tuning rounds,** one value at a time, `bench_landform` after each, recorded. *Why: "much much more realistic" is his eye's call.*
3. **The lapse decision** (Q6). If changed: the constant, the pins, the constants table, plan 006's snowline tests re-run. *Why: he asked, and it changes the snowline everywhere.*
4. **The version closed** (joint or alone), the row's comment written.
5. **Every pin checked** against its recorded reason.
6. **Old worlds listed, then deleted** (or kept as named).
7. **Suites, benches, 1 against 16;** release build.
8. **The before and after table;** pictures of the final land looked at and described.
9. **Ahmed's walk** and his notes.
10. **Status lines:** every phase file of plan 007 updated, and this plan's master status line updated in a new commit (the master's text otherwise left as approved, per `plans/README.md` rule 1).

## Todo

- [x] Every earlier phase's open requests from Ahmed listed here
- [x] Tuning rounds done, each value and its `bench_landform` numbers recorded
- [ ] Lapse decision recorded; if changed, the constant, the pins, plan 004 phase 11's constants "after" (here), and plan 006's snowline tests re-run
- [ ] `TERRAIN_GENERATOR_VERSION` closed at 2 (joint) or 3 (alone); `FEA_DIGEST_OF_GENERATOR` row comment naming the plans
- [ ] Every pin checked against its recorded reason; none moved without one
- [ ] Old worlds listed with sizes here, then deleted (or those Ahmed named kept)
- [ ] The full debug suite, the full release suite, a release build; `standards_lint` and `clang_format`
- [ ] 1 against 16 workers the same bytes on every final pin
- [ ] `bench_terrain` ≥ 1000, `bench_lod` ≤ 40 ms, `bench_climate` < 100 ms, `bench_atlas` in budget
- [ ] The before and after table filled; every target met or its miss explained and accepted by Ahmed
- [ ] Ahmed's walk recorded in his words
- [ ] Status lines of plan 007's files updated

## Tests

No new test. The suites, with every pin's last reason, are the test.

- **`FEA_DIGEST_OF_GENERATOR`'s row-count assert** (`tests/run/terrain/feature/index.c:61`) holds at the closed version.
- **`run_save_world`** refuses a save of the old version with the existing message, and the world list shows it as made by an older generator.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 3600 ctest --preset release --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_lod
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate
    tools/test-or-build.sh timeout 300 build/release/bin/bench_atlas
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 2198175988018515243 12636146071196537873 5569068473609503046 1
    tools/test-or-build.sh timeout 1800 build/release/bin/bench_landform /tmp/mc_c_landform 42 20261104 9999999999999 18446744073709551557
    du -sb saves/test_* ; du -sh saves/test_*
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed's walk in a new world on his seed, using plan 006's `/tp`, `/fly`, `/time` and F3, with the Atlas's contours:

1. a coastal plain;
2. the escarpment's face, climbed on foot;
3. the plateau's top, and inland across it;
4. a koppie on the plateau;
5. a range standing on the plateau, foot to summit on foot;
6. the view from the summit;
7. a dry basin.

At each stop he reads F3's temperature. He says whether the terrain is "much much more realistic", and what is still wrong. His words go here verbatim.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Plan 006 phase 05 runs first and alone | This phase bumps to 3 and lists version-2 worlds (Design, "Alone") |
| Tuning breaks a target already met | Section 4 rechecked after each round; a trade-off is Ahmed's to accept, recorded |
| The lapse change cascades into plan 006's snowline tests and plan 004's census numbers | Done only on Ahmed's word; every moved number recorded with its reason |
| A pin moved with no recorded reason | Stop; find the cause; Issue found; then continue |
| Ahmed keeps a world made while the version was open | It is refused like any old-version world; he was told (master section 9) |

## Commits

Appended as they land:

- `c083a614` WIP plan 007 phase 06 tuning: flatter lowland (continental rise to a fifth, coast 0.12 inland, softer valleys, hills 0 + 10, knobs from 0.7), rarer ranges (flank from 0.60, range cell 12288, range top 1080), plateaus read three quarters of broad continentalness blended at 2048, basin floors 100 x max(0.4, G); bench_landform reads plateaus on the ground less its knobs and the concavity on range peaks only
- `430cdcc6` Re-pin the terrain and climate digests on plan 007 phase 06's tuned land, each with its reason and old value; ...
- `25e43d2d` Fix the tests plan 007 phase 06's land broke, by reading the land: ...; the eight render pictures and the Atlas's 52 goldens made again and looked at, its plain-terrain hashes re-pinned; clang-format
- the commit that records this file's results and `bench_landform`'s README

## Issues found

**This file's tuning part was done alone** (2026-11-14), on main at c95ede89 with plan 007 phases 01 to 05 and plan 006 phases 01 to 04. The version, the old worlds, the surveys and the full final check are the joint 006-05/007-06 step, done after this by another agent: `TERRAIN_GENERATOR_VERSION` stays **1** here and row 1 of `FEA_DIGEST_OF_GENERATOR` moves with a line saying why. Open questions took their defaults (master Q1 to Q3 as phases 03 to 05; master section 4's targets as written). **Worlds made from this branch's land are test worlds.**

1. **What Ahmed and the earlier phases left open** (todo 1):
   - Ahmed: "please make terrain generation much much more realistic!!"; plateaus with knobs on them ("pimples", phase 04); the lapse question (master Q6, not this part's: the constant is unchanged).
   - Phase 03: rivers 78.5% joined against 90% (Issue 7); sea pockets inside plateaus (Issue 10); the render pool on tall ground (Issue 17).
   - Phase 04 Issue 7: master section 4's slope rows need the range belt narrower or rarer, the valley sinking softened, and the concavity measured on range peaks; Issue 8: the plateau rule and knobs on plateaus conflict (0 pieces of 25 km^2); Issue 9: floating rock over its limit on the two plateau-heavy seeds, `bench_terrain` 3.2% under at the jungle.
   - Phase 05 Issue 17: every basin floor at -99 or -100 (target -40 to -100).

2. **The decision on plateau pieces** (Ahmed has not answered; the default taken): **the knobs stay, and the plateau rule reads the ground less its knobs.** `bench_landform` takes the profile's new `knob` value (what a knob adds to the column: `terrain_density_profile_t.knob`, `terrain_density_profile_arrays_t.knob`, an output only, no land change) off the height before the 512-block squares and the 250 (`bench/landform/measure/README.md`). Why: Ahmed asked for knobs on plateau tops, and a knob of up to 90 breaks "relief at most 80 within 512 blocks" on every top, so the rule as written measured the knobs, not the plateau; the plateau under them is what master 3.4 means. The self-test's ground has no knob field and reads as before (all its checks hold).

3. **The concavity measure counted plateau tops as peaks; fixed.** The concavity and W50/H are now of **range peaks only**: at least 500 over the lowest in their 3 km, not on plateau land, no knob (`range_peaks` in the CSV; `peaks` still counts all). On phase 05's land this alone moved the median from 1.12–1.21 to 1.09–1.47 (300 to 600 range peaks a map against 4700 to 5100 peaks).

4. **The tuning loop.** A temporary release build of `bench_landform` read the tried values from the environment (never committed), 64 km at step 32, one window, on the 8 seeds; 45 s a try. Each line min to max over the 8: flat / gentle / moderate / steep / very steep / cliff / sheer % of land (profile, whole map). "Base" is phase 05's land. `cont` scales the land's rise of the continental spline (above 3), `val` the valley spline's two lower knots, `hill` min + extra, `knob` the knob threshold, `flank` the flank's zero knot (the rest rescaled), `cell` the range field's, `b2` the plateau reading broad continentalness blended at 2048 (as the basin gate does), `mix` the share of broad at the column in that, `coast` a shift of the continental spline's inner knots, `face` the plateau spline's face widths, `warp` the edge warp.

   | Try | Histogram | Other |
   |---|---|---|
   | Base (phase 05) | 2.1–2.7 / 16.3–19.3 / 30.5–32.8 / 20.4–21.6 / 14.7–16.1 / 8.3–9.8 / 1.8–3.6 | concavity 1.23–1.31 (all peaks); 0 plateaus of 25 km^2 |
   | flank 0.55 / 0.65 | flat +0.1, gentle +0.4 / +1.0, steep −0.5 / −0.9 | the belt is not what keeps the lowland off flat |
   | val 0.6, 0.85 | 2.1–2.8 / 17.1–20.2 / 32.7–35.1 / 21.5–22.8 / 13.2–14.7 / 5.6–7.2 / 1.6–3.4 | escarpment 246–654 km, largest plateau 2.6–8.9 km^2 |
   | Only `base` (no hills, knobs, valley, ranges, plateaus) | 8.8–11.0 / 42.2–44.7 / 41.3–44.1 / 3.8–4.6 / 0.3 / 0 / 0 | **the continental spline over `c`'s 48-block octaves is the lowland's roughness** |
   | Only `base`, cont 0.3 | 41.2–44.7 / 53.3–56.6 / 1.2–1.3 / 0.5–0.6 / 0.2 / 0 / 0 | |
   | cont 0.5 | 3.8–4.8 / 24.0–27.7 / 28.8–30.3 / 18.2–19.4 / 12.6–13.7 / 5.3–6.8 / 1.6–3.4 | sea 25.6–28.1 |
   | cont 0.3 | 5.2–6.5 / 26.8–30.4 / 27.6–28.8 / 17.5–18.7 / 11.1–12.2 / 4.6–6.2 / 1.6–3.4 | top 1052–1077 |
   | cont 0.3, val | 5.3–6.7 / 28.2–32.1 / 29.0–30.4 / 16.2–17.5 / 9.3–10.6 / 4.3–5.9 / 1.6–3.4 | |
   | cont 0.3, val, hill 3 + 15 | 8.0–9.9 / 31.7–35.9 / 24.2–25.5 / 14.8–16.2 / 8.9–10.2 / 4.2–5.7 / 1.6–3.3 | |
   | + flank 0.65, knob 0.65 | 9.2–11.6 / 34.2–39.2 / 22.1–23.6 / 12.7–14.6 / 8.0–9.6 / 4.2–5.7 / 1.6–3.4 | |
   | the same, no hills | 14.6–18.6 / 33.0–37.0 / 18.5–20.1 / 12.2–14.1 / 7.8–9.4 / 4.1–5.6 / 1.6–3.3 | |
   | cont 0.3, val, hill 3 + 15, knob 0.65, cell 16384, flank 0.70 | 9.3–12.0 / 34.7–40.4 / 22.2–23.8 / 12.3–14.4 / 7.3–9.2 / 3.5–5.4 / 1.5–3.3 | the longest "range" did not move: it is plateau tops at 650 |
   | the same with cell 12288, flank 0.60 | 8.9–11.5 / 34.0–39.3 / 22.6–24.4 / 12.9–14.8 / 7.7–9.5 / 3.8–5.5 / 1.5–3.3 | |
   | that land with no ranges / no plateaus / no knobs | sheer 1.4–3.2 / **0.2–0.3** / 1.6–3.3 | **sheer is the plateau faces** (phase 04's finding) |
   | knob 0.75 | steep 13.0–14.6 | fewer knobs, but `t` climbs faster: steeper sides |
   | Plateaus alone, face ×1.5 / ×2, warp 350 / 0 | sheer 1.3–3.1, 1.7–3.9 (×2 squeezed the next segments), 1.4–3.1, 1.4–3.1 | neither the face's width nor the warp sets the sheer |
   | **Plateaus alone, b2** | 29.1–36.8 / 49.6–53.6 / 5.2–10.1 / 2.7–5.8 / 1.6–3.5 / 0.7–1.8 / **0.1–0.2** | **`b`'s 768-block octave in `L` is the sheer**; but sea 17.9–23.6: the smooth coast gate lifts the sea's pockets |
   | cont 0.3, val, hill 3 + 15, knob 0.65, b2 | 8.9–11.0 / 36.3–39.6 / 24.3–26.4 / 13.6–15.3 / 7.8–8.9 / 3.1–3.9 / 0.3–0.4 | concavity 2.34–2.90; plateaus of 25 km^2 0–5; sea 18.3–24.2 |
   | + gate −0.5 to 0.4 | sea 20.1–25.0 | longest range 20 km on 1 of 8 |
   | mix 0.3 / 0.5 | sheer 0.6–1.3 / 1.0–2.2 | sea 21.0–25.6 / 22.7–26.5 |
   | b2, coast 0.04 / 0.08, basin floors 40 + 60 G | sea 19.7–25.9 / 21.1–27.7 | floors −65 to −100 |
   | cont 0.2, hill 0 + 10, knob 0.7, b2, coast 0.08 | 16.1–20.1 / 35.0–37.4 / 19.5–21.8 / 11.8–13.6 / 7.6–8.9 / 3.4–4.2 / 0.3–0.4 | |
   | + flank 0.60, cell 12288 | 17.7–22.7 / 36.0–38.8 / 18.0–20.8 / 10.5–12.8 / 6.7–8.4 / 2.9–3.9 / 0.2–0.3 | sea 20.6–27.0 |
   | + coast 0.12, massif lows 0.5 / 0.7 | 17.0–22.0 / 35.4–38.1 / 17.9–20.7 / 11.0–13.2 / 7.4–8.9 / 3.4–4.2 / 0.3–0.4 | longest range no better (5 of 8), steeper: dropped |
   | **+ coast 0.12, mix 0.25 (kept)** | 16.6–21.9 / 34.5–38.2 / 18.2–20.7 / 10.6–13.0 / 7.0–9.1 / 3.4–5.0 / 0.6–1.1 | sea 24.4–30.0; plateaus of 25 km^2 on every seed but one |

5. **The values kept** (every one in `run/src/terrain/density/` or `landform/`, each with its reason in the code and READMEs):

   | Value | Was | Now | Why |
   |---|---|---|---|
   | `continental` knots | land 3 → 100 over `c` −0.45 to 1; coast at about −0.46 | land 3 → 22.4 (a fifth of the rise); every inner knot +0.12, coast at about −0.34 | the rise over `c`'s fine octaves made every lowland moderate; the shift gives back the sea the plateaus lift |
   | `hill_land`, `hill_sea` | −0.45, −0.2; −0.58, −0.51 | −0.33, −0.08; −0.46, −0.39 | moved with the coast, so a beach still has no hills or knobs |
   | `valley` | −1: 0.3, −0.6: 0.65 | −1: 0.6, −0.6: 0.85 | the valley sinking was a main source of moderate and steep lowland (phase 04) |
   | Hills | 6 + 24 hill_relief | 0 + 10 hill_relief | flat lowland |
   | Knob threshold | 0.55 | 0.70 | about a tenth of the land, not a fifth; heights 20 to 90 as before |
   | `flank` | from nearness 0.40 | from 0.60, the rest rescaled | rarer ranges ... |
   | `density/range` cell | 8192 | 12288 | ... as wide in blocks, further apart |
   | `DENSITY_RANGE_TOP` | 1000 | 1080 | the summits stay at 1100 to 1190 with the land's rise 78 lower; sum 22.4 + 1080 + 40 + 10 = 1152.4 under 1190 |
   | Plateau's broad continentalness | `b` at the column | 0.75 `b2` + 0.25 `b` (`DENSITY_PLATEAU_BROAD_SHARE`) | sheer 1.8–3.6 → 0.6–1.1%, with the sea kept |
   | Basin floor | −100 | `-100 max(0.4, G)` (`DENSITY_BASIN_FLOOR_LEAST`) | floors spread over −50 to −100 |

   **Changed from the plan (shape, not only a constant):** the plateau's input and the basin floor's depth are one-line formula changes, owned by phases 03 and 05; they are recorded here, as this phase may not edit those files. Phase 03 tried a `b` weight and gates and found the faces steeper; the blend was not tried there.

6. **`bench_landform` on the kept values** (release, 64 km at 16, 64 windows, the strip; two runs, Ahmed's seed `11107447384583428913` with the first, exit 0 each; a CPU bench on a shared machine, no card clock applies). Min to max over the 8 seeds, Ahmed's beside, in the before-and-after table above (the "After" column). Per seed, in the order 2198175988018515243, 12636146071196537873, 5569068473609503046, 1, 42, 20261104, 9999999999999, 18446744073709551557: longest range at 500 or more 19.6, 19.8, 26.7, 27.0, 36.5, 20.8, 14.9, 10.4 km; plateau pieces of 25 km^2 3, 1, 5, 2, 3, 1, 1, 0 (largest 88.9, 28.1, 51.3, 41.6, 56.5, 40.1, 48.6, 15.6 km^2); sea 27.4, 29.9, 24.5, 28.6, 24.4, 29.9, 29.8, 28.5%; basins 1, 1, 0, 0, 2, 3, 4, 3 with floors −93, −55, -, -, −95 to −97, −65 to −90, −51 to −100, −90 to −100.
   - **Not met, and why:**
     - **Flat 16.8–22.2% (target 30–40), gentle 33.9–37.6 (25–35).** Moderate (18.2–20.7), steep (10.7–13.2) are in or at their bands; very steep 7.1–9.2 (3–6) and cliff 3.4–5.1 (1.5–3) over; sheer 0.6–1.1 (0.3–1) met but on one seed. The rest of "flat" is the knobs (4 points with them gone), the range belts and the hills; taking more would leave a lowland with nothing on it, and the steep classes are the ranges' tops, knobs' sides and the plateau faces, which are what Ahmed asked for.
     - **Real blocks against the profile: 8.8–12.6 points apart (target under 5).** The real blocks read flat 8.9–11.1%, gentle 27–32: a level of whole blocks over 8 blocks is a gradient of 0.125, so blocks can be flat only where they are exactly level, and the 3D noise of softness 4 moves the ground a block here and there. The profile's lowland is now flat enough for that difference to dominate.
     - **Steep ground high up 18.5–21.5% (target 70); top decile over bottom 3 1.5–1.9 (5).** Both read relative height in a 2048 square, whose top deciles are plateau tops (gentle) and whose steep ground is mostly plateau faces and knob sides at mid relief. Better than phase 05 (0.88–1.00 on the ratio) but far off; a measure that leaves plateau land out, as the concavity now does, would say more, and is for whoever takes this up.
     - **Concavity of range peaks 2.2–2.7 (target 3).** Ranges alone measured 2.1–2.5; the sharp curve `m^3` would need more of the alpine `G`.
     - **Longest range at 500 or more: 20 km on 4 of 8 (target 6; 5 before).** Two seeds at 19.6 and 19.8. The measure is plateau tops at 650 joined, more than ranges (with plateaus taken away it is 3 to 4 km), and the coast moved inland takes land from them.
     - **Knobs on a base of 250 or more: 18.8–43.4%, 25 or more on 3 of 8 (8 of 8 before).** Fewer knobs in all (4200 to 4800 a map against 10000), and fewer plateau tops at the coast.
     - **Plateau pieces of 25 km^2: 2 or more on 4 of 8 seeds** (0 on all before); the largest at least 80 km^2 and 8 km across on 1 seed (88.9, 17.6 km), 8 km across on 7.
   - **Met:** sea 24.4–29.9% (24.4 and 24.5 on two); top 1123–1135; basins 1.75 a map, floors −50 to −100; escarpment 706–1722 km (by the plateau rule on the knobless ground; the edges are counted on every tier now, so it is many times phase 04's 30–156).

7. **Benches**, release, shared machine (load 1.3 to 1.5; CPU benches, no card clock applies):
   - **`bench_terrain`:** slowest **jungle 975 and 959** (two runs; `/tp 14720 -5248`), then mangrove_swamp 1120 and 1130; jagged_peaks 1172, plateau_top 1258 and 1320, every other place 1814 or more. **Missed by 2.5 to 4%**, at a jungle (the cost is in its trees; the profile is 0.1% of a box), as in phase 04 (968). Phase 05's 1077 was at another jungle place, which this land moved.
   - **`bench_lod`:** means 17.0, 24.0, 12.7, 11.3, 11.2, 11.4 ms at shifts 1 to 6, worst 22.9 and 31.4 at shifts 1 and 2: the 40 ms budget met (phase 04's worst 46.5 at shift 2).
   - **Floating rock:** `bench_world_map` in full, two runs of 4, exit 0, 0 columns differing: pieces per km^2 of land 1551, 993, 995, 1246, 1690, 975, 1265, 707 (seeds in the order above) against phase 01's 1072 to 1383 and its limit +25%, 1729: **met on all 8** (phase 04: 931 to 2098, over on 2). The plateau faces are softer for the 3D noise where they are gentler, and the lowland has less of it.
   - **Rivers** (`run_terrain_hydrology`'s 4 maps): **88.7% joined** to the sea (8822 of 9941 columns), against phase 03's 78.5% and the 90% asked: the lowland's lower rise brings the sea and lakes nearer.
   - **`run_climate_warning`'s 20 ms**: 20.1 ms alone (main: 21.5 ms since phase 05's climate mask). This tuning does not touch the climate's ground; recorded, not fixed.

8. **Pins moved, each with this phase's line and its old value in the comment beside it:** `CELL_DIGEST_PIN` 0xE27D71453ED7D029, `SYS_DIGEST_PIN` 0x31B4C8C8455FDF56, `CAVE_DIGEST_PIN` 0xAA8CEDD5EF20F007, `BIO_DIGEST_PIN` 0x45E0B6464532298D, `BIO_COLUMNS_PIN` 0x001261CC1FC31977, `DENSITY_DIGEST_PIN` 0x02FC2ECAA373D0A1, `HYDRO_DIGEST_PIN` 0x826E69AC6913B5FA, `HYDRO_COLUMNS_PIN` 0x3E260C48629B4EE2, `HYDRO_FLOOD_PIN` 0xB8DE1D30846759D5, `ORE_DIGEST_PIN` 0x2868FA18E56A5E8C, `FEA_FOREST_PIN` 0x0A2981C595D9ACA2, `FEA_DIGEST_OF_GENERATOR[1]` 0xBF67826538D85EA0 (from 0x8E24FEE1F26EEAF2), the Atlas's 52 goldens and its two plain-terrain hashes 0x9E1B261418078E24 and 0xA533B1D63AD51BFB. Each is its test's 1-against-16-workers digest (same bytes on 1 and 16). `TERRAIN_GENERATOR_VERSION` stays 1.

9. **Other tests changed, with reasons** (each in its comment):
   - `run_terrain_density`: the README's formula typed out again with the new constants and the plateau's blended input; the corner's top 1152.4 (was 1170); land at 150 from 20% (24.7% measured); ranges: at least one piece of 1 km^2 a seed and 3 on 6 of 8 (one seed's high ground is one piece 16 km long); the plateau-top test reads the plateau's input as the profile does.
   - `run_terrain_hydrology`: rivers more than 0.7% of the maps (0.95%), the shallow ocean at least 6% (6.78%: more of the sea's pockets are lakes, 3.72%, and the shelf deeper).
   - `run_terrain_biome`: mushroom fields within 256 blocks of the open sea at least 70% (72.0 to 82.9%); a biome's place for the tests is a column on dry ground over the sea (forest, taiga and old-growth spruce taiga were found on lake columns), and the top check looks 15 by 15 chunks round it, every third chunk.
   - `run_climate_systems_rain`: at most 2 cells of 128 outside, none past 25% (two hurricane cells at 15 degrees, 15.4 and 22.6% off).
   - `run_game_command`: the far jumps go onto the ground (`/tp ~0 ~30000`), as kept at the height they left from the third went into rock under a plateau, was put out into a flooded cave and died; the tuning words are checked at the block the corner read, within one of the feet (the last jump sinks through the sea); the mid-jump save goes to the first dry land east of 3000, −2000 from the start, read from the hydrology (that place is now a lake).
   - `run_player_survival`: the tired run is on lowland under 120 (the nearest dry gentle run was a plateau top at 655, where the body stopped after 21 blocks and spent nothing), searched to 8 km (none within 4: the lowland is riddled with lakes); it starts at −6840, −7352.
   - **Pictures made again, each looked at against the old:** `render_feature_lod`'s `real_far.png` (a coast broken into islands and lakes, plateaus, and jagged range peaks on the skyline, where the old showed one peak over a coast) and `real_near.png` (snow-tipped taiga crowns from above, was jungle crowns); `render_feature_under_water`'s `land.png` (a terraced grass slope) and `sea.png`, `sea_no_air.png` (blue sea, the bed's hills gone from view); `run_render_frame`'s `start.png` (a dark taiga floor with trunks, was jungle crowns), `north_low.png` (a terraced slope of grass and dirt between trunks) and `down.png` (crowns in fog from above). The start of seed 7's world moved from jungle to taiga, as the land under it did.
   - `run_terrain_landform_measure` (`bench_landform --self-test`) passes unchanged: its ground has no knob field.

10. **Verification** (debug, rebased onto main at c95ede89): the whole suite at `-j 2`, 177 tests; the land's failures are the ones above, each fixed and rerun alone and passing; `run_climate_warning` fails alone as on main (Issue 7); `run_ui_atlas`'s lock-wait timing failed in the suite and passed alone. **After all the fixes, the whole suite again at `-j 2`: 175 of 177 pass**; `run_ui_atlas` (its lock wait, 2 ms) passed alone after, and `run_climate_warning` fails as on main. `standards_lint` and `clang_format` pass. Release: `bench_landform`, `bench_terrain`, `bench_lod` and `bench_world_map` built and run as above, then the release build deleted for the disk; the release suite is the joint check's. Nothing left running (`pgrep -af "$PWD/build"` empty).

**/tp lines for Ahmed** (his seed `11107447384583428913`, new world, `/fly`, F3 and the Atlas's contours):

    /tp -25264 -15408     the tallest peak (z 1133): a typical range, gentle feet, steep top, summit rock
    /tp -9280 16          the largest plateau piece (z 653, 40.6 km^2, 14 km across): knobs stand on its top
    /tp 26512 -12064      a dry basin's floor (z -97), at the foot of a range; the other basin is shallower (about -51)
    /tp 2288 -1024        gentle lowland (z 15), the plains place

**For the joint final check:** the tuned values are in; `bench_landform`'s after table is above; the lowland is now low (3 to 22 over the sea before hills, knobs, plateaus and ranges) and full of lakes and the sea's pockets, which Ahmed should look at; `bench_terrain` misses by 2.5 to 4% at the jungle; `run_climate_warning`'s 20 ms fails on main too.

### The joint final check (with plan 006 phase 05), 2026-11-14

Done by one agent on main at f5a3f85c, after this file's tuning part, as master Q2's default. Plan 006 phase 05's file holds the shared parts in full (the pins against their reasons, the version, the old worlds, the survey); this file records plan 007's side.

11. **`run_climate_warning`'s 20 ms, brought back under.** The slowest of 100 warnings was 20.1 to 21.5 ms alone in debug (30.2 ms, mean 20.7, in the whole suite at `-j 2`) since phase 05 made each column of the climate's mask dearer (the dry basin's inland gate samples broad continentalness at four nodes for a single column, and phase 06's plateau reads it too). The cause of the many columns: outside the climate's keep square no mask page is kept, so each read is one column of the ground, and the outlook's unborn sums start a typical track from the same square's centre on every day of the week, and check the open sea there, so they read the same cells day after day. Fixed in `run/src/climate/systems/` with no number changed: a build's mask keeps those columns too, 1024 of them direct-mapped by 5 low bits of the cell on each axis (`CLIMATE_SYSTEMS_MASK_POINTS`, in the build's arena, 20 KiB beside the pages' 64; the struct grows from 600 to 616 bytes), and the outlook reads the hurricane rate's open sea through the build's mask (`climate_systems_hurricane_rate_mask`, the same number as `climate_systems_hurricane_rate`). Alone, load 5.9 to 6.6: **slowest 15.0 and 15.7 ms, mean 11.5 and 12.1** (was 20.1 to 21.5); the census's outlook input 314 to 317 ms (was 432 in the suite). `run_climate_systems`, `_systems_rain`, `_weather`, `_layer`, `_cell`, `_constants` (the two bit counts named) and the rest of `run_climate_*`, `run_game_census` and `run_ui_hud` pass on their pins: the heights are the same bits, as the ground is a pure function.
