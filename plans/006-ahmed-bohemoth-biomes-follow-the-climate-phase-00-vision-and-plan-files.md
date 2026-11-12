# 006-ahmed-bohemoth-biomes-follow-the-climate — phase 0: The vision, and the plan files

**Status:** done   (updated 2026-11-12)

**Read this file before any other file of plan 006,** then the master `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md`. Every phase file assumes you know what is here. Plan 004's vision file (`plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md`) describes the whole game; this file only says where plan 006 sits in it.

## Goal

Two things are true when this phase is done:

1. Every agent working on plan 006 can say in a few sentences what is wrong today (snow at 24 °C on the equator), why (biomes never read the climate), what plan 006 changes (biome, surface and feature stages follow the climate) and what it must not change (height, density, hydrology, caves, ores).
2. The plan files exist: the master copied byte for byte, and phase files 01 to 05, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

---

## Part 1: Where plan 006 sits

### The layer order (master section 1)

Plan 004 phase 00 put the game's layers in this order: seed → terrain → climate → geology / vegetation → settlements → economy → player, worked out strictly top to bottom, each layer reading only the layers above it. Plan 004 deferred vegetation ("Vegetation systems not yet built into the plan please!"). **Plan 006 is that deferred vegetation layer.**

```
seed
 └─ fields (c, e, ridges, broad; T and H no longer used by biomes)
     └─ density profile and height estimate         (unchanged)
         ├─ hydrology: rivers, beaches, sea bed      (unchanged)
         └─ climate cells: t_mean, swing, rain, class, snowline   (unchanged inputs)
             └─ biome = climate family, then landform and variety noise   ← new
                 └─ surface: top, filler, snowline, sand               ← new rules
                     └─ features: trees, plants, snow and ice by climate   ← new rules
                         └─ cover (plan 004 phase 08): seasonal snow and ice   (unchanged)
```

**There is no loop.** The climate reads the ground only through `terrain_height_estimate*` (the 2D splines over continentalness, erosion, ridges and broad). Height and density never read temperature, humidity or biome. So height → climate → biome and surface is acyclic, as long as biomes never feed back into height.

### The rule plan 006 lifts

Plan 004 phase 00, rule 3: "**Nothing in the terrain changes.** The climate reads the ground through `terrain_height_estimate`, and never changes what generates."

Plan 006 **lifts rule 3 for the biome, surface and feature stages only.**

| Stage | Plan 006 |
|---|---|
| Biome (`run/src/terrain/biome/`) | Changes: family from the climate first (phase 03) |
| Surface (`run/src/terrain/surface/`) | Changes: snowline, sand (phase 04) |
| Features (`run/src/terrain/feature/`) | Changes: snow, ice, tree rates by climate (phase 04) |
| Height and density | **Unchanged** |
| Hydrology, rivers, beaches, sea bed | **Unchanged** |
| Caves | **Unchanged** |
| Ores | **Unchanged** |
| The climate's inputs and outputs | Unchanged, apart from phase 02's move of the sampled fields to deterministic maths |
| The cover (seasonal snow and ice) | Unchanged |

Because the land changes, `TERRAIN_GENERATOR_VERSION` goes from 1 to 2 and the old worlds are deleted (phase 05).

### The "before" numbers

Plan 004 phase 11 measured the mismatch over 8 seeds, pole to pole (`plans/004-ahmed-bohemoth-climate-system-phase-11-tuning-and-cost.md`, "The survey"), as "biome disagrees, % of land": a desert or badlands biome in a class that is not Desert or Steppe, or another biome in Desert or Steppe.

| Class | Biome disagrees, % |
|---|---|
| Desert | **81.0** |
| Steppe | **80.9** |
| Mediterranean | 21.5 |
| Boreal | 19.9 |
| Temperate | 17.9 |
| Ice | 17.6 |
| Monsoon | 16.9 |
| Savanna | 16.8 |
| Tundra | 16.4 |
| Rainforest | 16.3 |
| Continental | 0.0 (the class never occurs) |

So: Desert 81.0%, Steppe 80.9%, every other class 16% to 21%, Continental 0%. Plan 006's target (master section 5) is **under 5% in every class**, on a stricter measure (biome not a member of the climate class's family); phase 03 prints both, and phase 05 writes the "after" next to these.

### Ahmed's decisions, 2026-11-12

| Question | Decision | Phase |
|---|---|---|
| Old worlds (`saves/test_1` to `test_8`) | **Delete them.** The generator version goes to 2 | 05 |
| Fast travel | `/tp` with absolute, `~` relative and `lat`; **a `/fly` toggle**; and **an F3 tuning readout** | 01 |
| Where the climate sits | **"run through our climate model first and then decide biome systems"**: climate decides what kind of land, and noise only adds variety inside it | 02, 03 |

And his words that started it: "Snow just spawns absolutely freiking randomly. Look at ~/Pictures/1.png please! temperature is 24 deg C but there is snow!" (x -68, y 149, z 443, 0.35° N, day 91, HUD 24.1 °C, snow blocks).

---

## Part 2: Rules every phase follows

1. **Pure functions of the seed and the climate sample.** Every biome, surface and feature decision is a pure function of the seed, the position and the `terrain_climate_sample_t` it is given (master 2.1). Nothing new is saved except the teleport's `arriving` flag and fly mode (phase 01).
2. **Deterministic.** The same seed gives the same bytes on 1 worker or 16, with the climate on, and with the same climate replayed. Every phase that changes output tests that.
3. **No libm in block decisions.** Anything that decides a block is worked out with `run/src/math/det/` and compared as integers (master 2.3; `run/src/terrain/ore/README.md:87`).
4. **Height, density, hydrology, caves and ores do not change.** Their digests hold through every phase. A change is an Issue found.
5. **Terrain never includes `climate`.** The include ranks stay (climate 8, terrain 9); the climate sample is a seam that `game` fills.
6. **Everything is drawn before it's trusted.** Phases 03 and 04 look at the pictures (`bench_biome_vs_class`, the equator-to-pole strip) and record what was seen.
7. **Every heavy command goes through `tools/test-or-build.sh`**, one command per call, `timeout` inside the wrapper (`.claude/rules/test-or-build-mutex.md`). After tests, `pgrep -af "$PWD/build"` prints nothing. Never start a server or the game and leave it running; never kill Ahmed's running game.
8. **Never push.**
9. **Commit as Ahmed, with `git commit -F -`, one-sentence messages, no AI trailers** (no `Co-Authored-By`, no anthropic or Claude lines). After each commit, `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` prints nothing.
10. **The repo's own rules:** `plans/README.md` (todos ticked as work lands, issues recorded when they arise), `plans/CODING_STANDARD.md`, `plans/REPO_STRUCTURE.md`.

---

## Part 3: The phases, and what each leaves behind

| Phase | File | What's true when it's done |
|---|---|---|
| 00 | `...-phase-00-vision-and-plan-files.md` | This file, the verbatim master, and phase files 01 to 05 exist |
| 01 | `...-phase-01-teleport-fly-and-tuning-readout.md` | `/tp`, `/fly`, `/time` and the F3 tuning lines work; three far jumps redraw within 15 s (the black-world bug fixed) |
| 02 | `...-phase-02-climate-sample-and-deterministic-maths.md` | Terrain gets a climate sample (or the fallback) in every caller, from libm-free maths; read reach 112; spawn search after the climate; no terrain digest moved |
| 03 | `...-phase-03-biomes-from-the-climate.md` | Biomes are chosen family first; every biome YAML names its families; the mismatch survey is under 5% in every class |
| 04 | `...-phase-04-snow-ice-sand-and-trees.md` | Snow only above the snowline or in Ice, ice by the coldest month, sand by family, trees by rain, near and far alike; no snow or ice at the equator |
| 05 | `...-phase-05-version-old-worlds-and-final-check.md` | Generator version 2 with its pins; old worlds listed and deleted; full suites; the survey's "after" recorded; Ahmed's gate notes |

The open questions Q1 to Q5 (master section 10) each have a default, and each is repeated in the phase it affects: Q1 snowy equatorial peaks (03, 04), Q2 keys and Q3 fly speed (01), Q4 `/tp` in test worlds (01, 02), Q5 keeping old worlds (05).

---

## Todo

- [x] Read `plans/README.md`, and plan 004's phase 00 and phase 03 files as the model for structure and depth
- [x] Check `006` is free: `ls plans/ | grep '^006'` printed nothing
- [x] Copy the master byte for byte from `~/.claude/plans/pasted-content-id-a5ab-ahmed-admin-mine-serialized-fiddle.md` to `plans/006-ahmed-bohemoth-biomes-follow-the-climate.md`; `cmp` printed nothing; committed alone
- [x] Create phase files 01 to 05 from the template, every box unticked, status `not started`, each with a "Read first" line linking back to this file and the master, one commit each
- [x] This file restates section 1's layer order, says 006 lifts plan 004's rule 3 for biome, surface and features only, and points to plan 004 phase 11's mismatch numbers as the "before"
- [x] Every phase file has Goal, Todo, Tests, Verification, Gate, Risks, Commits and Issues found: `grep -L "## Issues found" plans/006-*phase-*.md` prints nothing
- [x] Report the paths created

## Verification

No build, test or code changes in this phase.

    ls plans/006-*
    grep -L "## Issues found" plans/006-*phase-*.md    # prints nothing
    git status --short                                 # nothing uncommitted

## Commits

Appended as they land:

- `56534267` Copy the approved biomes-follow-the-climate master plan into plans/ as plan 006, byte for byte.
- `01bb5f76` Add the plan 006 phase 01 file for teleport, fly and the F3 tuning readout.
- `78eb7544` Add the plan 006 phase 02 file for the climate sample seam and deterministic maths.
- `31e81fb6` Add the plan 006 phase 03 file for choosing biomes from the climate family first.
- `8fcd5a51` Add the plan 006 phase 04 file for snow, ice, sand and tree rates that follow the climate.
- `af35fd9f` Add the plan 006 phase 05 file for generator version 2, deleting the old test worlds and the final check.

## Issues found

- 2026-11-12, plan setup, reading the code to fill the phase files. Places where the master and the code differ, recorded in the phase files as defaults rather than as changes to the master (which stays verbatim):
  - **The YAML key `climate:` is already taken** by today's noise boxes (`config/biomes/*/index.yaml`, `GZ_CLIMATE_FIELDS` in `compile/src/game_tables/biomes/index.c:48`). Phase 03's default: rename today's block to `landform:` and make `climate:` the new block.
  - **The master's `old_growth_taiga`** is two biomes in the repo, `old_growth_spruce_taiga` and `old_growth_pine_taiga`; its "sparse forest" has no biome of that name; and 53 biomes include oceans, rivers, beaches, three underground biomes and others the family table does not name. Phase 03 step 1 assigns them.
  - **Two more snow sources than the master names:** `high: { block: snow_block, from: 400 / 800 }` in `windswept_hills` and `stony_peaks`, and the far field's `render_lod_sample_freeze` (`run/src/render/lod/index.c:137`). Phase 04 covers them.
  - **`arriving` does not exist yet;** survival's existing wait flag is `placing` (`run/include/player/survival/index.h:112`), and `survival_stand_at_spawn` reads `survival->spawn`, not a target. Phase 01 adds `arriving` and a target-taking variant.
  - **Plan 004 phase 11's "disagrees" measure** counts only desert against non-desert; the master's 5% target is on family membership. Phase 03 prints both.
