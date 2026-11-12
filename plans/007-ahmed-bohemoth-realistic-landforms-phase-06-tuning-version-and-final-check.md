# 007-ahmed-bohemoth-realistic-landforms — phase 6: Tuning, version and the final check

**Status:** not started   (updated 2026-11-12)

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
| Flat / gentle / moderate / steep / very steep / cliff / sheer | | | 30–40 / 25–35 / 15–20 / 7–12 / 3–6 / 1.5–3 / 0.3–1 % | |
| Of ground steeper than 1:2, high up or on faces | | | ≥ 70% | |
| Top decile over bottom 3, median slope | | | ≥ 5 | |
| Concavity ratio, median | | | ≥ 3 | |
| Plateau pieces ≥ 25 km²; largest | | | ≥ 2; ≥ 80 km², ≥ 8 km | |
| Escarpment length; facing the sea | | | ≥ 30 km; ≥ 75% | |
| Longest range at ≥ 500 | | | ≥ 20 km on 6 of 8 | |
| Knobs on a base ≥ 250 | | | ≥ 25% | |
| Dry basins; floors | | | 1–3; -40 to -100 | |
| Sea | | | 25–30% | |
| Top | | | 1100–1190 | |
| Floating rock per km² | | | ≤ before + 25% | |
| `bench_terrain` slowest | | | ≥ 1000 | |
| `bench_lod` per column | | | ≤ 40 ms | |
| `bench_climate` cold tile | | | < 100 ms | |

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

- [ ] Every earlier phase's open requests from Ahmed listed here
- [ ] Tuning rounds done, each value and its `bench_landform` numbers recorded
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

## Issues found

None.
