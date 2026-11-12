# 007-ahmed-bohemoth-realistic-landforms — phase 0: The vision, and the plan files

**Status:** done   (updated 2026-11-12)

**Read this file before any other file of plan 007,** then the master `plans/007-ahmed-bohemoth-realistic-landforms.md`. Every phase file assumes you know what is here.

- Plan 004's vision file (`plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md`) describes the whole game.
- Plan 006's phase 00 says where the vegetation layer sits.
- This file only says where plan 007 sits, and what it relies on.

## Goal

Two things are true when this phase is done:

1. **Every agent working on plan 007 can say in a few sentences:**
   - what is wrong today: hills and ranges are 1:1 "ice cream cones" and "pimples", a range is a 2 km tent, and there are no plateaus;
   - why: every height term is a symmetric bump of a zero-mean noise, steepest mid-flank, each sized to about the same slope, with nothing that holds the ground up (master section 0);
   - what plan 007 changes: height and density, with plateaus, escarpments, long ranges, slopes that steepen upwards, knobs on any ground, dry basins, and Atlas contour lines;
   - what it must not change: caves, ores, the climate's maths, the world's height range.
2. **The plan files exist:** the master, and phase files 01 to 06, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

---

## Part 1: Where plan 007 sits

### The layer order (master section 1)

Plan 004 phase 00 put the game's layers in this order: seed, terrain, climate, geology and vegetation, settlements, economy, player. Each layer reads only the layers above it.

- Plan 004 deferred geology: "Geology and landforms: plateaus, escarpments, Everest-style massifs, below-sea-level basins (a later plan; the climate is ready for them)".
- Plan 004's vision drew it beside climate, "because it changes the ground the climate reads. When it lands, climate is simply recomputed from the new ground".

**Plan 007 is that geology (landform) layer.**

```
seed
 └─ fields: c, b, e, r                                            (unchanged)
     └─ landform lattice, every 32 blocks: uplift, edge warp, basin, ruggedness   ← new
         └─ density profile: base + plateau + range·shape + knobs + hills + peaks   ← new formula
             ├─ the 3D density, softness                         (same rule, new heights)
             ├─ hydrology: rivers, beaches, sea, dry basins       ← basins only
             └─ height estimate = the profile, bit for bit        (follows by construction)
                 └─ climate cells: heights, water mask (+ dry basins), lapse   (no code change except the dry mask)
                     └─ biome (plan 006): climate family, then landform with height − lift
                         └─ surface, features, cover                       (plan 006's)
```

**There is no loop.** Height reads only noise fields and the landform lattice. The climate reads height only through `terrain_height_estimate*`, which is the profile itself (`run/src/terrain/index.c:132-185`). Biomes read the climate after. Nothing below feeds back into height.

### The rules plan 007 lifts

| Stage | Rule | Plan 007 |
|---|---|---|
| Height and density (`run/src/terrain/density/`, new `run/src/terrain/landform/`) | Plan 004 phase 00 rule 3, "Nothing in the terrain changes"; plan 006, "height, density unchanged" | **Changes:** phases 03 to 05 |
| Hydrology | Plan 006, "unchanged" | **Changes only for dry basins:** phase 05 |
| Biome | Plan 006 phase 03 changes it | **One change:** the height axis reads `ground - lift` (phase 03) |
| Caves, ores | Unchanged | **Unchanged.** Their digests move only because the ground under them moved |
| The climate | Unchanged | **Unchanged maths.** Heights follow by the estimate; the water rule reads the dry mask (phase 05); the lapse constant only if Ahmed picks one (phase 06) |
| The Atlas | Plan 004 phase 09 | Contours on the Terrain layer, the pixel gains cover depth (phase 02) |
| The world's height range, -250 to 1250, `TERRAIN_DENSITY_HEIGHT_MIN` -116 and `MAX` 1190 | Plan 003 phase 11 | **Unchanged.** The new formula is budgeted inside them (master 3.2) |

### The order with plan 006 (master section 0, Q2)

**Plan 006 phase 01 is being built now,** in another worktree. Plan 006's files are **not** edited by plan 007.

**Recommended order:**

1. 006 phase 01 (tools).
2. 006 phase 02 (climate sample).
3. 007 phases 01 and 02 (measuring and contours). These change no land, so they may run beside 006.
4. 007 phases 03 to 05 (the new land).
5. 006 phases 03 and 04 (biomes, tuned on the final land).
6. **One joint final check:** 006 phase 05 with 007 phase 06, making **one** generator bump to 2 and deleting old worlds once.

If 006 phase 05 ships first, 007 phase 06 bumps to 3.

**What plan 006 phase 03 needs to know.** Its biome "landform" height axis means **height above `lift`** (the plateau and basin terms) once 007 phase 03 lands. So a 650 plateau's grassland counts as low ground for the biome, while the climate still cools it by its absolute height. 007 phase 03 records this in its Issues found, and the joint final check confirms it.

---

## Part 2: The 006 additions plan 007 relies on

Ahmed asked for these in plan 006, to tune terrain and climate by eye, and **plan 006 phase 01 is building them now** (`plans/006-ahmed-bohemoth-biomes-follow-the-climate-phase-01-teleport-fly-and-tuning-readout.md`):

| Command | What it does | Used by plan 007 for |
|---|---|---|
| `/tp X Y Z`, `/tp X Y` | Teleport to a block; with no Z, stand on the ground there | Every `bench_landform` "`/tp X Y`" line: largest plateau, tallest peak, longest range, a knob on a plateau, a basin |
| `/tp ~DX ~DY ~DZ` (and `/tp ~ ~ ~300`) | Relative jumps | Stepping along an escarpment; going up to see a plateau from above |
| `/tp lat DEG [X]` | To a latitude | Checking plateaus and snowlines from the equator to the pole (phase 01's strip, in the game) |
| `/tp top` | Straight up to the highest ground of this column | Summits, plateau rims |
| `/fly` | No gravity, no fall damage | Flying along a range or an escarpment |
| `/speed 1-10` | Movement or fly speed | Covering a 10 km plateau quickly |
| `/time day N`, `/time hour H` | Day of year and clock | Seasonal snow on plateaus and summits |
| `/help` | Lists the commands | |
| F3 tuning readout | Biome, family, class; yearly, warm and cold temperatures at the feet's height; rain; snowline | Reading how much cooler a plateau or summit is (master 2.4, Q6); basin heat (phase 05) |

**One caveat, recorded on 2026-11-12.** The phase 01 file in `main` at the time of writing (`de0c0faa`) lists:

- `/tp` (absolute, `~` relative, `lat`, `top`);
- `/fly`, with speed on the mouse wheel from 10 to 1000 blocks a second;
- `/time` and `/help`;
- the F3 lines.

It does not name a `/speed 1-10` command. If phase 01 ships without one, plan 007 uses fly speed on the wheel instead. Nothing in plan 007 depends on `/speed` itself.

**Until those land,** plan 007's gates fall back on the coordinates `bench_landform` prints and plain walking. Phases 01 and 02 need no in-game command at all.

---

## Part 3: Rules every phase follows

1. **Pure functions of the seed.** Every landform value is a pure function of the seed and the position, on a world-aligned lattice, blended in a fixed order ($x$ then $y$), so no box, batch or thread can change a bit (master 3.3).
2. **Deterministic.** The same seed gives the same bytes on 1 worker or 16. Every phase that changes land tests that.
3. **No libm in block decisions.** Shapes are monotone splines. No `pow`, `exp`, `sqrt` or trig decides a block (master 3.12; `run/src/terrain/ore/README.md`). Benches and tools may use libm.
4. **The estimate stays the profile.** Every height term lives in `density_profile_batch`, so `terrain_height_estimate*` follows bit for bit and the climate follows with it (master 3.10).
5. **Inside the budget.** Heights stay within -116 to 1190 by hand-added limits and a test (master 3.2). The world top, the lava rule and the tree room do not move.
6. **Measured before and after.** Phase 01's `bench_landform` runs before any land changes, and after each land phase, on plan 003's 8 seeds. Every tuning try is recorded with its numbers.
7. **Every pin that moves is re-pinned with its reason,** naming the phase. A pin that moves without a reason is an Issue found, and the work stops until it is explained.
8. **Every heavy command goes through `tools/test-or-build.sh`,** one command per call, with `timeout` inside the wrapper (`.claude/rules/test-or-build-mutex.md`). After tests, `pgrep -af "$PWD/build"` prints nothing. Never start a server or the game and leave it running, and never touch Ahmed's running game.
9. **Never push.**
10. **Commit as Ahmed,** with `git commit -F -`, one-sentence messages, and no AI trailers (no `Co-Authored-By`, no Anthropic or Claude lines). After each commit, `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` prints nothing.
11. **The repo's own rules:** `plans/README.md` (todos ticked as work lands, issues recorded when they arise), `plans/CODING_STANDARD.md` (split at 1500 lines, hard limit 2000), `plans/REPO_STRUCTURE.md`.

---

## Part 4: The phases, and what each leaves behind

| Phase | File | What's true when it's done |
|---|---|---|
| 00 | `...-phase-00-vision-and-plan-files.md` | This file, the master and phase files 01 to 06 exist |
| 01 | `...-phase-01-measure-todays-terrain.md` | `bench_landform` measures and draws slopes, peaks, plateaus, ranges, knobs and an equator-to-pole strip; today's numbers recorded; Ahmed picked the scale (Q1) and targets (Q3) |
| 02 | `...-phase-02-atlas-contour-lines.md` | Contours on the Atlas Terrain layer by default, spaced by zoom, index lines, on the ground under trees; tile file version 2 reads version 1 |
| 03 | `...-phase-03-plateaus-escarpments-and-long-ranges.md` | Two-tier plateaus 6 to 16 km across with seaward concave escarpments and gentle back slopes; ranges 15 to 40 km long standing on them; `lift` for biomes; the version opened |
| 04 | `...-phase-04-slope-profiles-and-hills-on-any-ground.md` | Gentle feet and steep tops by region; summit cliffs; knobs (koppies) and gentler hills on any ground; the slope targets met |
| 05 | `...-phase-05-dry-basins-below-sea-level.md` | Dry basins to -100 with sealed rims; no water against their air; the climate counts them as land and basin heat fires |
| 06 | `...-phase-06-tuning-version-and-final-check.md` | Ahmed's tuning; the lapse decision; one version bump shared with plan 006; old worlds deleted once; full suites; before and after |

**Open questions** (master section 12) each have a default, and each is repeated in the phase it affects:

| Question | Phases |
|---|---|
| Q1 plateau scale | 01, 03 |
| Q2 order and version | 03, 06 |
| Q3 targets | 01, 03, 04 |
| Q4 basins | 05 |
| Q5 contours and labels | 02 |
| Q6 lapse | 06 |
| Q7 "600 km high" | 01, 03 |

---

## Ahmed's words, 2026-11-12

- "please add a plan that varies the steepness of hills! I noticed all your hills are 1:1"
- "all your hills still are ice cream cones ... over a span of 2km x 2km there is a mountain range!"
- "1. plateus of mountain ranges can span **MASSIVE AREAS!** for example I live in South Africa, there is an escarpment, but a really really high plateu 1000km wide and 600km high, we can have something similar, if a mountain goes up that does not necessarily mean it needs to come down really fast, you are creating pimples, but we want actual mountain ranges"
- "2. these things I refer to as "pimples" does not only need to spawn at altitude 0, it can also be on plateus etc...."
- "3. slopes don't need to be 1:1, they can be 5:1 (e.g. 5 blocks forward for every 1 block up) and at the peaks go to 1:5 please make terrain generation much much more realistic!!"
- "When bringing up the map, look at ~/Pictures/2.png, let's please show contour lines"
- "I did notice that climate drops as I am climbing, maybe increase climate drop a little bit? not sure what the current temp vs altitude rate is at"
- "maybe I think 1:1 slopes are because I have not explored much of the world"

---

## Todo

- [x] Read `plans/README.md`, plan 004's master and its phase 00 and phase 03 files, plan 004 phase 05's Issues found 3 (basins), plan 003's phase 08 and 11 files on mountains and height, and plan 006's master and phase files
- [x] Read the terrain height code (`run/src/terrain/fields/`, `density/`, `hydrology/`, the estimate in `run/src/terrain/index.c`), the biome height axis, the Atlas terrain layer and pixel, the climate's heat and water rule, and the benches (`bench/terrain`, `bench/world_map`, `bench/biome_map`, `bench/atlas`)
- [x] Looked at `~/Pictures/2.png`: the Atlas Terrain layer, no contour lines (they exist only on the second press of `1`, every 100, one pixel, on tree crowns)
- [x] Check `007` is free: `ls plans/ | grep '^007'` printed nothing before this plan
- [x] The master `plans/007-ahmed-bohemoth-realistic-landforms.md` written, status "draft for approval (2026-11-12)", committed alone
- [x] Phase files 01 to 06 written from the template with plan 004's richer parts, every box unticked, status `not started`, each with a "Read first" line linking back to this file and the master, one commit each
- [x] This file restates the layer order, the rules lifted, the order with plan 006, and the 006 additions plan 007 relies on
- [x] Every phase file has Goal, Todo, Tests, Verification, Gate, Risks, Commits and Issues found: `grep -L "## Issues found" plans/007-*phase-*.md` prints nothing
- [x] Plan 006's files left untouched

## Verification

No build, test or code changes in this phase.

    ls plans/007-*
    grep -L "## Issues found" plans/007-*phase-*.md    # prints nothing
    git status --short                                 # nothing uncommitted

## Commits

Appended as they land:

- `037e005f` Add the plan 007 master for realistic landforms: plateaus with escarpments, long ranges, slopes that steepen towards the peaks, knobs on any ground, dry basins and Atlas contour lines.
- `dd3c0744` Add the plan 007 phase 01 file for measuring today's slopes, peaks, plateaus and an equator-to-pole strip with a headless tool before any land changes.
- `1dfca759` Add the plan 007 phase 02 file for contour lines on the Atlas terrain layer, spaced by zoom, with index lines and the ground read under the trees.
- `3bfa37e4` Add the plan 007 phase 03 file for plateaus with seaward escarpments, long ranges standing on them, and the landform lattice that keeps them cheap.
- `f876c1a1` Correct the plan 007 master so the plateau's back edge is told by broad continentalness instead of gradients, with the landform lattice's real cost and a target for escarpments facing the sea.
- `e7ae025c` Add the plan 007 phase 04 file for slopes that are gentle at the foot and steep near the top, summit cliffs, and knobs and hills on any ground including plateaus.
- `c723ffad` Add the plan 007 phase 05 file for dry basins below sea level with sealed rims, so plan 004's basin heat finally has land to warm.
- `a0834581` Add the plan 007 phase 06 file for Ahmed's tuning round, the cooling-with-height decision, one generator bump shared with plan 006, and the final check.

## Issues found

- **2026-11-12, plan setup, reading the code to write the files.** Places where a first draft and the code differed, fixed before the phase files were committed, or recorded as defaults:
  - **The master's first draft put gradients on the landform lattice for the plateau's back edge.** Working out the cost for phase 03 showed that a gradient needs a ring of nodes round every box: about 3 times the nodes, about 16 octaves a lattice column, against a `bench_terrain` margin of 13%. **Corrected in `f876c1a1`:** the back edge is told by broad continentalness $b$ (low near the coast, high inland), which the profile batch already samples, so a chunk box needs only 4 nodes (about 1 to 2 octaves a lattice column). The true gradient test stays as the fallback, measured before it is chosen. A target for escarpments facing the sea (at least 75%) was added to master section 4 so the cheaper rule is checked.
  - **The master's worked slope table was first written for a power curve,** while $S_G$ is a blend of two curves. Corrected before the first commit: the table now gives $G = 0$, 0.5 and 1 separately, so the slope ratio between 90% and 10% of the envelope is 3.7, 9 and 81.
  - **The Atlas already has contour lines,** on the second Terrain layer (`terrain_lines`, `run/src/ui/atlas/layer/index.c:176-177`, `:361-368`), every 100 blocks, one pixel, over tree crowns. Ahmed's picture is the first layer, which has none. Phase 02 moves the lines onto the default layer rather than adding new ones.
  - **Biomes choose by absolute height** (`run/src/terrain/biome/index.c:68-74`; `stony_peaks` from 500), so a plateau at 650 would be bare peaks. This is not in Ahmed's words, but it decides whether plateaus look right. Phase 03 adds `lift` and the `ground - lift` axis, and plan 006 phase 03 is told (Part 1).
  - **Removing foothills** (60 blocks on creased ridges) is phase 04's own choice, not the master's. It is recorded there with the way back.
  - **`/speed 1-10`** is not in the phase 01 file in `main` at `de0c0faa` (Part 2). Plan 007 does not depend on it.
