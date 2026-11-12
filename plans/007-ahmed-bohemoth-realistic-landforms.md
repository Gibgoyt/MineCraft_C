# 007-ahmed-bohemoth-realistic-landforms — master plan

**Status:** draft for approval (2026-11-12). Plans only: on approval, `plans/README.md` applies. Phase files 00 to 06 exist beside this file (section 11), phase 00 done and the rest `not started`, and **no code is written until Ahmed says go.** Nothing is pushed.

## 0. Context

### Ahmed's words, 2026-11-12

- "please add a plan that varies the steepness of hills! I noticed all your hills are 1:1"
- "all your hills still are ice cream cones ... over a span of 2km x 2km there is a mountain range!"
- "1. plateus of mountain ranges can span **MASSIVE AREAS!** for example I live in South Africa, there is an escarpment, but a really really high plateu 1000km wide and 600km high, we can have something similar, if a mountain goes up that does not necessarily mean it needs to come down really fast, you are creating pimples, but we want actual mountain ranges"
- "2. these things I refer to as "pimples" does not only need to spawn at altitude 0, it can also be on plateus etc...."
- "3. slopes don't need to be 1:1, they can be 5:1 (e.g. 5 blocks forward for every 1 block up) and at the peaks go to 1:5 please make terrain generation much much more realistic!!"
- "When bringing up the map, look at ~/Pictures/2.png, let's please show contour lines"
- "I did notice that climate drops as I am climbing, maybe increase climate drop a little bit? not sure what the current temp vs altitude rate is at"
- "maybe I think 1:1 slopes are because I have not explored much of the world"

And from plan 004 (2026-11-02), never yet built:

- "huge mountains, but sort of plateus in between! Like how mount everest has base-camp"
- "places available below sea level, similar to the dead sea, and these areas might be very hot"

**Notation used everywhere in this plan.** Ahmed writes a slope as *run:rise*: "5:1" is 5 blocks forward for 1 up, "1:5" is 1 forward for 5 up. The plan also gives the **gradient** $g = \text{rise} / \text{run}$, the number the code works with. So 5:1 is $g = 0.2$ (11°), 1:1 is $g = 1$ (45°), and 1:5 is $g = 5$ (79°).

### What the picture shows

`~/Pictures/2.png` is the Atlas (`M`) on its **Terrain** layer, at x -116, y 163, z 348, 0.4° N, 24.1 °C, Rainforest. It shows each column's top colour with hill shading (`run/src/ui/atlas/layer/index.c:344-372`). A white range runs south-west to north-east through the middle, and dark green lowland lies west of it.

- **No contour lines.** The Terrain layer draws none. A second layer, `terrain_lines`, does draw them. You reach it by pressing `1` a second time (`run/src/ui/atlas/layer/index.c:173-177`). Its lines are every 100 blocks, one pixel wide, and drawn by halving the shade where the height crosses a multiple of 100 between a pixel and its east or south neighbour (`:361-368`). There is no thicker index line and no label. The height it reads is the top block's height, tree crowns included (`run/src/ui/atlas/tile/index.c:63-79`), so over forest the lines would follow the trees.
- **The white is snow at 24 °C on the equator.** That is plan 006's bug (biomes ignore the climate), and plan 006 phases 03 and 04 fix it. The "ice cream cone" look is half that white cap and half the shape. **This plan fixes the shape.**
- The picture gives no zoom. At the Atlas's default 2 blocks a pixel (`UI_ATLAS_ZOOM_DEFAULT`, `run/include/ui/atlas/layer/index.h:25`), the white range is about 2 km across, which fits Ahmed's "over a span of 2km x 2km there is a mountain range".

### Why the hills look like 1:1 cones (read from the code, 2026-11-12)

The ground's height is a sum of 2D terms in `terrain_density_profile_from` (`run/src/terrain/density/index.c:250-285`), with a 3D noise of ±`softness` on top:

    height = base + 1030·rise + 60·foothill + hill_blocks·hills + 14·body·jagged
    base = continental(c), times valley(r) on land          (-110 to 100)
    body = range_land(b) · belt(1 - |range|) · massif(m)     (0 to 1)
    rise = body · crest(1 - |crest|)                         (0 to 1)

All numbers below are from the code's constants. The slope estimates are worked from the field sizes and the README's measured spacings. Phase 01 measures the real values.

1. **No term holds the ground up after it rises.** Every term is a bump in a zero-mean noise field, so whatever goes up comes straight back down. There is no step, terrace or plateau term. The only term that keeps a level across kilometres is `continental(c)`, and it tops out at **100 blocks** (`:32-35`). This is exactly Ahmed's "if a mountain goes up that does not necessarily mean it needs to come down really fast".
2. **A range is a symmetric tent about 2 km wide.**
   - A range is where the `range` field (cell 4096, 3 octaves, persistence 0.45; `:126-132`) is 0. Its height comes from `belt(1 - |range|)`. The fold $1 - |v|$ is symmetric about the line and creased on it.
   - `belt` is non-zero only for nearness above 0.52, and full only above 0.94 (`:53-55`). Because the field is spread evenly, the share of the world in some belt is 0.48 (the README says "about 48%").
   - The README puts range lines **about 4 km apart** (`run/src/terrain/density/README.md`, "How a range is made").
   - If the field rises roughly linearly between lines $D$ apart, a belt is $0.48\,D \approx 1.9$ km wide. Its full-height crest is only $0.06\,D \approx 240$ blocks wide. Each flank climbs over about $0.42 \cdot D/2 \approx 840$ blocks.
   - **That is Ahmed's 2 km mountain range:** up to 1030 blocks of rise over about 840 blocks, so **about 1:1 on average.** The steepest piece of `belt` (nearness 0.82 to 0.94: 0.32 over 0.12, slope 2.67) gives $1030 \times 2.67 / 2000 \approx$ **1.4 rise per run** at full massif.
3. **The steepest part is mid-flank, and the summit is rounder.** Real mountains go the other way.
   - `belt`, `crest` and `massif` are cubic Hermite splines with Fritsch–Carlson slopes (`:174-201`). Each piece is an S-curve, steepest in its middle and flattening into its knots.
   - The noise spread $u(1.5 - 0.5u^2)$ (`run/src/terrain/fields/index.c:78-83`) does the same to every field: flat at both ends.
   - So slope **peaks halfway up and eases off near the top**. A real mountain is gentle at its foot and steep near its top: a concave profile. Here the top is a dome or a crease, and the foot is as steep as the middle.
4. **Every smaller term was sized to about the same steepness, so the land looks the same at every scale.** Each term's height over its cell ($A/L$):
   - **crest** (cell 896): half the range's height over about 180 blocks, about **1.6:1**;
   - **foothills**: 60 blocks on ridges (weirdness cell 512, folded, with a crease at the top), about 60 over 90 to 170 blocks, **1:2 to 1:3**;
   - **hills**: up to ±46 (`8 + 38·hill_relief`) on a field of cell 192, about 92 of relief over 100 to 200 blocks, **1:1 to 1:2**;
   - **jagged**: ±14 on a field of cell 32, about **1:1** needles on all range ground (`:100-155`).

   Bumps of one shape at every size, piled up, read as "pimples" to the eye.
5. **The small bumps only sit low.** These are "pimples" in Ahmed's sense:
   - `hill_blocks` is gated by `hill_land(c) · valley(r)`, and it sits on a base of at most 100.
   - `foothill` is multiplied by $(1 - \text{body})$, so it is never on a range.
   - Nothing high is wide enough to carry a knob, so knobs appear only near altitude 0 to 150. This is his point 2.
6. **Peaks are where lines cross.** A summit is where a crest line (cell 896) crosses a range line under a high `massif` (cell 2048). Two creased folds meeting make an isolated pyramid on a 2 km base: **"ice cream cones"**.
7. **What was measured before.** Plan 003 phase 8 stage 4 (`plans/003-ahmed-bohemoth-initial-game-fixes-phase-08-world-generation.md`, "After stage 4"):
   - 10.2 to 11.1% of land rises **3 or more for 1** over 8 blocks;
   - the largest piece of ground at 150 or over is **6.1 to 8.7 km across**;
   - dry land is 192 to 256 blocks from high ground.

   So steep ground exists, but it is scattered by noise (needles, crests, rivers' cuts) and not placed where real steep ground is: on upper slopes and escarpment faces. Its "Open for Ahmed" line already named the knobs: "how steep a range's sides are, up to about 3 for 1 where it nears a sea ... `DENSITY_RANGE_BLOCKS`, the cell of `DENSITY_SHAPE_RANGE` and the knots of `belt` are where to do it."

**About "I have not explored much of the world".** Points 1 to 6 hold everywhere, by construction. No place in the world has a plateau, because no term can make one. But how *often* each slope occurs, and where, is a measurement. So phase 01 measures before anything changes.

### What exists to build on

| Fact | Where | What this plan does with it |
|---|---|---|
| The height is plain 2D arithmetic on spread noise, per lattice column every 4 blocks | `density_profile_batch`, `run/src/terrain/density/index.c:339-408` | New terms go in the same batch, so the 3D density, hydrology, biomes and the estimate all see them |
| The climate reads the ground only through `terrain_height_estimate*`, which **is** the same profile, bit for bit | `run/src/terrain/index.c:132-185`; the callback at `run/src/climate/index.c:83` | Changing the profile moves every climate tile with it, and nothing in the climate's code changes for height |
| Monotone splines that never overshoot their knots, at most 12 knots | `density_spline_build`, `:174-201`; `TERRAIN_DENSITY_SPLINE_KNOTS_MAX`, `run/include/terrain/density/index.h:18` | Every new shape is a spline, so limits can be added up by hand and no libm is needed |
| Noise fields come from a name, so adding a field moves no other field | `run/src/terrain/noise/README.md` | New fields (uplift, basin, knobs, …) leave today's fields' values alone |
| 30 of 64 noise field slots used | `run/include/terrain/*/index.h` (`*_NOISE_FIELDS`), `TERRAIN_NOISE_FIELDS_MAX` | Room for about 8 more; plan 006 phase 03 adds 2 |
| Height limits -116 to 1190, softness 4 to 26, never stone from 1224 | `run/include/terrain/density/index.h:27-37` | The new formula is budgeted to stay inside them (3.2), so the world top, lava rules and tree room all hold |
| The Atlas already has a contour layer and marching squares | `run/src/ui/atlas/layer/index.c:361-368`; plan 004 phase 09, "Isobars and contours" | Phase 02 brings the lines onto the Terrain layer, adapts the spacing to the zoom, adds index lines, and reads the ground under trees |
| Headless map tools | `bench/world_map/` (16 km map, slices, a table with a `steep` row), `bench/world_map/atlas/`, `bench/biome_map/`, `bench/terrain/`, `bench/lod/` | Phase 01 adds `bench/landform/`, which measures slope, peaks, plateaus and an equator-to-pole strip |
| Fast travel and a tuning readout | Plan 006 phase 01 (being built now): `/tp`, `/fly`, `/time`, `/help`, F3 lines | Every gate of this plan is walked with them (phase 00, "006 additions") |
| The climate is ready for below-sea basins: 2.5 °C per 100 blocks | `CLIMATE_BASIN_C_PER_BLOCK`, `run/include/climate/heat/index.h:13`; `run/src/climate/heat/index.c:10-11` | Phase 05 makes the dry basins it waits for |

### What is in the way

| Obstacle | Where | What this plan does |
|---|---|---|
| Ground below `z = 0` that is not stone is **always water** | `density_block_of`, `run/src/terrain/density/index.c:505-516` | Phase 05 adds a per-column surface water level: 0 as today, or "none" inside a dry basin |
| The climate marks every cell with ground at or below 0 as water, so basin heat can never fire (plan 004 phase 05 Issues found 3) | `run/src/climate/cell/index.c:72-78` | Phase 05 gives the climate the terrain's dry mask through the same estimate seam |
| Hydrology's near-surface flood band can reach -154, and aquifer lava starts at -160 | `run/src/terrain/hydrology/README.md`, "What an opened voxel holds" | Basin floors stop at -100, so `TERRAIN_DENSITY_HEIGHT_MIN` stays -116 and the lava separation stands |
| Biomes choose by **absolute** height: peaks from 500, meadow 300 to 500, plains to 300 | `terrain_biome_distance`, `run/src/terrain/biome/index.c:68-74`; `config/biomes/{stony_peaks,meadow,plains}/index.yaml` | A plateau at 650 would come out as bare peaks. The profile gains a `lift` (the plateau and basin terms), and the biome height axis reads height **less the lift** (3.11). The climate keeps absolute height |
| `bench_terrain` is 1134 columns a second at its slowest place, against 1000 | `bench/terrain/README.md` | Macro fields go on a coarse 32-block lattice (3.3). Measured in phases 01, 03 and 04 |
| Taller ground costs more: caves are sampled to the top of the ground, and more sections are non-uniform | `plans/003-…-phase-08-world-generation.md` Issues found 23 and 46; `RENDER_POOL_BYTES_PER_COLUMN`, `run/include/render/index.h:31-38` | Phase 01 measures the cost of a tall place; phase 03 adds a plateau place to `bench_terrain` and checks the render pool |
| Saves hold edits over generated land, with no migration | `run/include/terrain/index.h:30-32`; `tests/run/terrain/feature/index.c:47-61` | Generator version policy, section 9 |
| Plan 006 is mid-flight and also changes land (biomes, snow), with its own version bump to 2 | Plan 006 master sections 7 and 9 | Order and one bump, section 9 |

### Where this plan sits beside plan 006

Plan 006 changes **what lies on** the ground: biomes, snow, sand and trees, all from the climate. Plan 006 keeps height, density and hydrology unchanged. **Plan 007 changes the ground itself.**

The order is still acyclic:

1. 007 changes the height splines.
2. The climate reads `terrain_height_estimate`, so climate tiles follow with no code change.
3. 006's biome, surface and feature work reads the climate after that.

**Recommended order** (Q2, section 12):

1. 006 phase 01: the tools (being built).
2. 006 phase 02: the climate sample.
3. 007 phases 01 and 02: measuring and contours, which change no land, and may run beside 006.
4. 007 phases 03 to 05: the new land.
5. 006 phases 03 and 04: biomes, tuned on the final land.
6. One joint final check: 006 phase 05 together with 007 phase 06, making **one** generator bump to 2 and deleting old worlds once.

## 1. The rule this plan changes

Plan 004 phase 00 put the layers in this order: seed, terrain, climate, geology and vegetation, settlements, economy, player. **Plan 007 is the geology (landform) layer.** It changes the ground the climate reads, as the vision file foresaw: "When it lands, climate is simply recomputed from the new ground."

**What it lifts:**

- Plan 004 phase 00 rule 3 ("Nothing in the terrain changes").
- Plan 006's "height, density, hydrology unchanged".

It lifts them for **height and density**, and for **hydrology only where dry basins need it.** Caves, ores and the climate's maths do not change.

```
seed
 └─ fields: c, b, e, r                                            (unchanged)
     └─ landform lattice, every 32 blocks: uplift, edge warp, basin, ruggedness   ← new (3.3)
         └─ density profile: base + plateau + range·shape + knobs + hills + peaks   ← new formula (3.2)
             ├─ the 3D density, softness                         (same rule, new heights)
             ├─ hydrology: rivers, beaches, sea, dry basins       ← basins only (3.8)
             └─ height estimate = the profile, bit for bit        (follows by construction)
                 └─ climate cells: heights, water mask (+ dry basins), lapse   (no code change except the dry mask)
                     └─ biome (plan 006): climate family, then landform with height − lift   ← 3.11
                         └─ surface, features, cover                       (plan 006's)
```

## 2. Where I disagree, or read Ahmed's words a particular way

Each changes what was asked, and each is for Ahmed to overrule (section 12).

### 2.1 "1000 km wide and 600 km high" has to be scaled

- **The world is small sideways.** Ahmed decided the equator-to-pole distance is 38,298 blocks (plan 004: "the tropic line (23.5°) is 10 km from the equator"). On Earth that is about 10,000 km, so **one block is about 261 m of real distance**. A 1,000 km plateau at that scale is **3.8 km**.
- **The world is short upwards.** It ends at 1250. Earth's highest mountain is 8.8 km. "600 km high" can't be meant literally. The Highveld is about 1.2 to 1.8 km high, and the Drakensberg rim is over 3 km. **I read it as "a plateau about as big again north to south" (600 km, scaled), or as a slip.** Ahmed to say.
- **Height by temperature.**
  - The lapse rate is 0.012 °C a block, against Earth's 6.5 °C per km. So **one block cools like about 0.54 m**, and 1000 blocks cool like 1.85 km.
  - The Highveld at 1,500 to 1,800 m cools by 9.8 to 11.7 °C, which here is **810 to 970 blocks**. A 650-block plateau cools 7.8 °C, like about 1,200 m.
- **Default:**
  - plateaus **6 to 16 km across**, with tops at **250 to 650**;
  - escarpments falling **300 to 600 blocks** over 0.3 to 1.5 km;
  - ranges running **tens of km** along their lines, some standing on the plateaus.

  That is option B in Q1: bigger than true scale, because a 4 km plateau would not feel "MASSIVE".
  - Walking across 10 km takes about 39 minutes at 4.3 blocks a second.
  - Flying across takes 10 s at 1000 blocks a second.

### 2.2 "1:5 at the peaks" means cliffs, and cliffs must stay rare

- $g = 5$ is 79°: a wall.
- Real high mountains have such faces near their summits and on escarpment rims, over a small share of the land.
- **Default:** about 0.3 to 1% of land steeper than 1:5, and 1.5 to 3% between 1:2 and 1:5, at least 70% of both in the top 30% of local relief or on an escarpment face (targets, section 4).

  Today, 10% of land is steeper than 1:3 but scattered (0.7).

### 2.3 The world top stays 1250

A plateau at 650 with ranges on it must share the same 1190-block budget the ranges have today. So **a range standing on a plateau rises less above it** (3.2): on a 650 plateau, summits reach about 1100.

Raising the world top is out of scope: plan 003 phase 11 sized the render pool, the section count and the save format to 1250.

### 2.4 Cooling with height: an open question, not a change

`CLIMATE_LAPSE_C_PER_BLOCK = 0.012` (`run/include/climate/heat/index.h:12`). That is 12 °C per 1000 blocks, already **almost twice Earth's 6.5 °C per km**, as Ahmed chose on 2026-11-04 ("Fast").

| Lapse, °C a block | Per 1000 blocks | 650 plateau at the equator (27 °C at sea) | Summit 1100 at the equator | Snowline height, against today |
|---|---|---|---|---|
| **0.012 (today, default)** | 12 | 19.2 °C | 13.8 °C | × 1.00 |
| 0.015 | 15 | 17.3 °C | 10.5 °C | × 0.80 |
| 0.020 | 20 | 14.0 °C | 5.0 °C | × 0.60 |

The snowline scales as $1/\Gamma$. It is where the warm-season temperature $T_0 - \Gamma z$ reaches a fixed threshold, so $z = (T_0 - T_{th}) / \Gamma$.

- **Any change moves every climate digest** (`tests/run/climate/*`), plan 004 phase 11's constants table, the snowline, and plan 006's snow tests.
- **Default: keep 0.012.** Ahmed tries the F3 readout (plan 006 phase 01) on a plateau and a summit first. If he picks another value, phase 06 changes the one constant and re-pins with the reason (Q6).

## 3. Design

All numbers in this section are **first guesses**, to be tuned against phase 01's tool and Ahmed's eye. Every knot table is written so that its limits add up by hand, as the density README does today.

### 3.1 Scale

| Thing | Real | Here (default) |
|---|---|---|
| Plateau width | Highveld, about 1000 km | 6 to 16 km (Q1 option B); 4 km at true scale |
| Plateau height | 1.2 to 1.8 km | Tier 1 about 300, tier 2 about 650 |
| Escarpment drop | 1 to 2 km over 10 to 50 km, steepest near the rim | 300 to 600 blocks over 300 to 1500 blocks, concave |
| Range length | Hundreds of km | 15 to 40 km along a line (range cell 8192) |
| Range width | Tens of km | 3 to 5 km foot to foot, 0.3 to 1 km of rugged crest |
| Koppies (Ahmed's "pimples") | 20 to 150 m, on the plateau | 20 to 90 blocks, on any ground |
| Dry basin | Dead Sea -430 m | 2 to 6 km across, floor -40 to -100 |

### 3.2 The new height formula, and its budget

With $c$, $b$, $e$ and $r$ as today; $U$, $W$, $B$ and $G$ new macro fields on the landform lattice (3.3); and $K$, $H$ and $J$ new or retuned fine fields:

$$h = \underbrace{\text{base}(c, r, e)}_{\le 100} + \underbrace{P_{\max}\,\text{plateau}(L)}_{\le 650} - \underbrace{\text{basin}}_{\le 200} + \underbrace{(1000 - P)\,S_G\big(m\big)}_{\text{range}} + \underbrace{\text{knob}}_{\le 90} + \underbrace{\text{hill}}_{\le 30} + \underbrace{\text{peak}}_{\le 40}$$

- `base` is today's `continental(c)`, with today's valley sinking and wetland rule.
- $P = P_{\max}\,\text{plateau}(L)$ is the plateau lift, 0 to 650 (3.4).
- $m \in [0, 1]$ is the range envelope (3.5). $S_G$ is the slope-shaping curve chosen by ruggedness (3.6). The range rises at most $1000 - P$ above the plateau.
- `knob` (3.7) is multiplied by $(1 - \text{body})$, so knobs and range never stack.
- `peak` (3.6) is non-zero only where $m > 0.6$.
- `basin` (3.8) is non-zero only where $P < 0.1\,P_{\max}$ and inland.
- **lift** $= P - \text{basin}$ is stored in the profile for biomes (3.11).

**Budget, the way the header's limits are written today.**

| Case | Sum | Limit |
|---|---|---|
| Top, on a range | $100 + (P + 1000 - P) + 30 + 40 = 1170$ | ≤ `TERRAIN_DENSITY_HEIGHT_MAX` 1190 ✓ |
| Top, on a plateau with a knob, no range | $100 + 650 + 90 + 30 = 870$ | ✓ |
| Bottom, at sea | $-110 - 6$ (`hill_sea`) $= -116$ | ≥ `TERRAIN_DENSITY_HEIGHT_MIN` -116 ✓ |
| Bottom, a basin | Floor clamped at -100: `basin` ≤ base + 100, and no hill on a basin floor | ✓ |

So `TERRAIN_DENSITY_HEIGHT_MAX`, `HEIGHT_MIN`, `NEVER_SOLID_FROM` (1224), `TERRAIN_MADE_TOP` and the hydrology lava rule are **unchanged**. A unit test adds every spline's extreme together, as `tests/run/terrain/density/` does today.

**Softness** stays $4 + 22 \cdot (\text{high} + \text{body} - \text{high} \cdot \text{body})$, but `high` gains $+0.5 \cdot \text{face}$ (3.4). The overhangs and arches the 3D noise makes then go to escarpment faces and ranges, not to plateau tops.

### 3.3 The landform lattice: macro fields every 32 blocks

The plateau, edge-warp, basin and ruggedness fields have cells of 2 to 24 km. Sampling them every 4 blocks would add about 11 octaves to every lattice column. That is about 33% more profile cost, with `bench_terrain`'s margin at 13%.

- **They are sampled on a world-aligned lattice every 32 blocks**, at $x = 32i$, $y = 32j$. A lattice column bilinearly blends its 4 surrounding nodes, $x$ first and then $y$, in float64. That is the same rule as the density's own lattice, so **a column's bits never depend on the box or batch it came in**.
- Within a batch, nodes are found once and shared. A 32-block density tile has at most 4 nodes; a scattered estimate call has up to 4 per point.
- **Cost:**
  - about 11 octaves for every node, which is $11/64 \approx 0.2$ octaves per 4-block lattice column inside a box;
  - up to 44 per point for scattered single-point estimates, which the climate's tile build absorbs (cold tile 30.7 ms against 100, plan 004 phase 03).
- **Fallback, if the lattice shows** (straight-line artefacts at 32-block scale, unlikely with cells of at least 2 km): sample directly, and pay the octaves.
- **Why the escarpment can be crisp anyway:**
  - the lattice blends the *smooth input* $L$;
  - the sharp step is applied per lattice column by the plateau spline;
  - a step 75 blocks wide over a field blended every 32 blocks keeps its shape.

### 3.4 Plateaus and escarpments

**Fields:**

| Field | Cell | Octaves | Use |
|---|---|---|---|
| `landform/uplift` $U$ | 24576 (Q1 B; 16384 for A-ish, 65536 for C) | 3, persistence 0.5 | Where the land is lifted |
| `landform/edge_x`, `landform/edge_y` $W$ | 2048 | 2 | Warp of $U$'s sample point, ±700 blocks: bays, spurs, amphitheatres along the rim |

**The plateau input:**

$$L = U(x + 700\,W_x,\ y + 700\,W_y) + 0.25\,(b - 0.1)$$

with $b$ broad continentalness. The $b$ term pulls plateaus inland and makes their seaward edge run roughly parallel to the coast. That gives **coastal plain, then escarpment, then plateau**, as in South Africa. $L$ is clamped to $[-1, 1]$.

**The plateau spline** (11 knots, two tiers; heights at $P_{\max} = 650$):

| $L$ | -1 | 0.00 | 0.10 | 0.16 | 0.19 | 0.21 | 0.40 | 0.48 | 0.53 | 0.56 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| plateau | 0 | 0 | 0.04 | 0.14 | 0.40 | 0.45 | 0.48 | 0.55 | 0.95 | 1.00 | 1.00 |
| blocks | 0 | 0 | 26 | 91 | 260 | 293 | 312 | 358 | 618 | 650 | 650 |

**Worked numbers.** Take $dL/dx \approx 4 \times 10^{-4}$ a block (uplift at cell 24576 plus the $b$ term; phase 01 measures it):

| Piece | $\Delta L$ | Width | Rise | Gradient | Run:rise |
|---|---|---|---|---|---|
| Pediment, 0.00 to 0.16 | 0.16 | 400 | 91 | 0.23 | 4.4:1 ✓ "5:1" |
| Tier 1 face, 0.16 to 0.19 | 0.03 | 75 | 169 | 2.25 | 1:2.25 |
| Tier 1 lip, 0.19 to 0.21 | 0.02 | 50 | 33 | 0.66 | rounding the rim |
| Tier 1 top, 0.21 to 0.40 | 0.19 | 475 | 19 | 0.04 | 25:1, a gentle dip inland |
| Tier 2 foot, 0.40 to 0.48 | 0.08 | 200 | 46 | 0.23 | 4.3:1 |
| Tier 2 face, 0.48 to 0.53 | 0.05 | 125 | 260 | 2.1 | 1:2 |
| Tier 2 top, 0.56 to 1 | 0.44 | ≥ 1100 each side | 0 | 0 | flat, with knobs and hills on it |

The spline never overshoots, so the face is concave: gentle at its foot and steepest just below the lip. That is the profile Ahmed described.

**Asymmetry: "it does not need to come down fast".** A blob of high $L$ has an edge all round. A real plateau has its escarpment on the seaward side and slopes away gently inland. So:

- On the landform lattice, each node also gets $\nabla L$ and $\nabla b$ by central differences of neighbouring nodes. These are pure functions of the node and its 4 neighbours.
- Where $\hat\nabla L \cdot \hat\nabla b < 0$, the land is falling *toward* the interior. That is the plateau's back edge. There $L$ is stretched before the spline: $L' = 0.53 + (L - 0.53) \cdot k$, with $k$ blending from 1 to 0.2. So that edge is about **5 times wider**: tier 2's back slope goes from the face's 1:2 ($g = 2.1$) to about 2.4:1 ($g = 0.42$), spread over about 625 blocks instead of 125.
- The blend is a smoothstep of the cosine, built from $+$, $-$, $\times$ and $\div$ only.

So walking inland off the rim, the ground stays high for kilometres and comes down slowly, if it comes down at all.

**Share of land.** Thresholds are tuned in phase 03 against these targets:

- tier 2 (≥ 550) on **8 to 12% of land**;
- tier 1 (250 to 400) on **12 to 18%**;
- **at least 2 plateau pieces of 25 km² or more on every 64 by 64 km map, the largest at least 80 km²** (section 4).

**Plateau top** gets knobs and hills (3.7), so it is not a table. Rivers stay off it: hydrology fades a river's depth from 45 to 120 high (`run/src/terrain/hydrology/README.md`), so rivers on plateaus stay out of scope.

**Everest's "base camp"** (plan 004) falls out of this: a range standing on a tier-2 plateau has a flat high floor at its foot.

### 3.5 Long ranges

The range line idea works: "a line where a smooth field is 0 has no ends". What is wrong is the cross-section and the scale.

| Change | Today | New (default) | Why |
|---|---|---|---|
| `range` cell | 4096 | **8192** | Lines about 8 km apart, curving less, so a range runs 15 to 40 km |
| Cross-section | `belt` S-curve on $1 - \|range\|$ from 0.52 | **`flank`**: nearness 0.40: 0, 0.55: 0.06, 0.70: 0.20, 0.82: 0.42, 0.90: 0.64, 0.95: 0.82, 0.985: 0.96, 1: 1 | A wider, almost straight envelope. The concavity comes from $S_G$ (3.6), so it is not baked in twice |
| `massif` cell | 2048 | **4096** | Long high stretches and real passes |
| `crest` | Cell 896, half height always | Cell 896, but its depth scales with $m$: $\text{crest\_share} = 1 - 0.5\,m\,(1 - \text{crest}(\cdot))$ | Side valleys cut the high part. The foothills stay smooth |
| Height | $1030 \cdot \text{rise}$ | $(1000 - P)\, S_G(m)$, $m = \text{body} \cdot \text{crest\_share}$ | Stands on the plateau, inside the budget |
| `jagged` (cell 32, 14 blocks everywhere on ranges) | On | **Replaced** by `peak` (3.6), only near summits | Needles on every foothill are part of the "pimples" |

**Belt width with $D = 8192$.**

- Nearness 0.40 is $|v| = 0.6$. So a belt runs $0.6 \cdot D/2 \approx 2.4$ km each side of the line, **4.9 km foot to foot**.
- The share of the world inside some belt is 0.6 before the land gate and `massif`, against 0.48 today. Phase 03 tunes the 0.40 knot to hold "reaching 500" near today's 4.8 to 6.1%.
- Both values are measured in phase 01.

### 3.6 Slope profiles: gentle feet, steep tops

**The idea.** Take a smooth envelope $m$ that rises steadily from 0 at a mountain's foot to 1 at its top. Shape it with a curve that is flat at 0 and steep at 1. Then the ground is gentle low down and steep high up, with no change to *where* the mountains are.

For $h = H\, m^p$:

$$\frac{dh}{dx} = H\, p\, m^{p-1} \frac{dm}{dx}$$

So the slope at $m = 0.9$ divided by the slope at $m = 0.1$ is $9^{p-1}$. **With $p = 2.5$ that is 27, which is Ahmed's 5:1 at the foot to 1:5 near the top** ($1/0.2 \cdot 5 = 25$).

**Splines, not `pow`.** libm's `pow` may not decide a block (`run/src/terrain/ore/README.md`; `run/src/terrain/fields/README.md`). So $S$ is a monotone spline through knots of $m^p$:

| $m$ | 0 | 0.2 | 0.4 | 0.6 | 0.8 | 0.9 | 1 |
|---|---|---|---|---|---|---|---|
| $S_{\text{soft}}$ ($p \approx 1.6$) | 0 | 0.076 | 0.231 | 0.442 | 0.700 | 0.845 | 1 |
| $S_{\text{sharp}}$ ($p \approx 3$) | 0 | 0.008 | 0.064 | 0.216 | 0.512 | 0.729 | 1 |

$S_G = (1 - G)\, S_{\text{soft}} + G\, S_{\text{sharp}}$, with $G \in [0, 1]$ from `landform/rugged`: cell 3072, 2 octaves, on the landform lattice, spread to 0 to 1. Some regions are rolling, and some are alpine.

**Worked slopes.** One flank: $H = 1000$, $m$ rising 0 to 1 over 2000 blocks ($dm/dx = 1/2000$). The gradient is $H \cdot S_G'(m) \cdot dm/dx$, with $S'$ from the curves the knots follow ($1.6\,m^{0.6}$ and $3\,m^2$):

| $m$ | Height, $G = 0.5$ | Gradient, $G = 0$ (soft) | Gradient, $G = 0.5$ | Gradient, $G = 1$ (sharp) |
|---|---|---|---|---|
| 0.1 | 13 | 0.20 (5:1) | 0.11 (9:1) | 0.015 (67:1) |
| 0.2 | 42 | 0.30 | 0.18 (5.5:1) | 0.06 |
| 0.4 | 148 | 0.46 | 0.35 (2.9:1) | 0.24 |
| 0.6 | 329 | 0.59 | 0.57 (1.8:1) | 0.54 |
| 0.8 | 606 | 0.70 | 0.83 (1.2:1) | 0.96 |
| 0.95 | 889 | 0.78 | 1.06 (1:1.06) | 1.35 (1:1.35) |

The slope at $m = 0.9$ over the slope at $m = 0.1$:

- **3.7** on soft ground ($G = 0$): rolling, like downs;
- **9** at $G = 0.5$;
- **81** on sharp ground ($G = 1$): alpine.

Regions differ, which is the variety Ahmed asked for. The envelope alone gives from 67:1 at a sharp foot to about 1:1.35 high up. **The last steps to 1:5** come from two places:

- **`peak`:** a ridged field, $J = 1 - |n|$ (`landform/peak`, cell 48, 2 octaves), times $40 \cdot \max(0, (m - 0.6)/0.4)^3$. Its gradient at $m = 1$ is about $40 \times 2 / 24 \approx 3.3$ along its creases, so arêtes and summit cliffs reach **1:3 to 1:6**. It is 0 below 60% of the envelope.
- **`crest`** cutting side valleys into the high part only (3.5).

**Target shape, measured** (section 4). For every peak, the mean slope over the top half of its relief divided by the mean slope over the bottom half is the **concavity ratio**.

- A 1:1 cone has 1.
- $h = H(1 - r/R)^p$ with $p = 2.5$ has $r_{50} = R(1 - 0.5^{0.4}) = 0.242R$, top half $0.5H / 0.242R$ against bottom half $0.5H / 0.758R$: a ratio of **3.1**.
- **Target: median at least 3, today expected about 1** (phase 01 measures).

### 3.7 Hills and knobs on any ground (Ahmed's point 2)

**Hills.** Rolling relief everywhere on land:

- **Size:** amplitude $6 + 24 \cdot \text{hill\_relief}(e) \cdot (0.5 + 0.5\,G)$, at most 30, against 46 today.
- **Cell:** 384, 3 octaves, against 192 today. So the typical hill slope halves, to **about 1:4 to 1:6**.
- **Gate:** they keep `hill_land(c) · valley(r)` (no hills on beaches; river floors stay level). They **lose** every tie to low height, so they ride on the plateau as on the lowland.

**Knobs** (koppies, inselbergs, buttes):

- **Field:** `relief/knobs` $K$, cell 320, 2 octaves, spread.
- **Rule:** a knob stands where $K > k_0 = 0.55$:

$$\text{knob} = A_K \cdot S_K\!\left(\frac{K - k_0}{1 - k_0}\right) \cdot (1 - \text{body}) \cdot \text{land}(c)$$

- **Height:** $A_K = 20 + 70\,\sigma$, with $\sigma$ from the knob field's second octave remapped, so 20 to 90 blocks.
- **Shape:** $S_K$ blends a dome (convex, $S = 1 - (1 - t)^2$) and a koppie (steep sides, flat top: knots 0: 0, 0.3: 0.15, 0.5: 0.75, 0.7: 0.95, 1: 1) by $G$.
- **Spacing:** about $0.225$ of ground has some knob. Knobs are 120 to 250 blocks across, so they lie hundreds of blocks apart, with flat ground between.
- **Slopes:** from about 6:1 for a low dome to about 1:1.5 for a koppie's side.
- **Not on a range or in the sea.** They are not tied to height, so they stand at 0 and at 650 alike.

### 3.8 Dry basins below sea level (plan 004's Dead Sea)

**Field.** `landform/basin` $B$: cell 8192, 2 octaves, on the landform lattice, spread.

**Depression:**

$$\text{basin} = D(B) \cdot \text{inland}(c) \cdot \left(1 - \frac{P}{0.1\,P_{\max}}\right)_{+}, \qquad D = (\text{base} + 100)\cdot S_B\!\left(\frac{B - 0.8}{0.2}\right)_+$$

So the floor reaches at most -100. $S_B$ is concave (a flat floor with steep sides), and the depression starts at $B = 0.8$, about 10% of the world before the gates.

- **Target:** 1 to 3 basins per 64 by 64 km map, each 2 to 6 km across, floors -40 to -100 (Q4).
- **The rim is guaranteed dry land.**
  - Where $0.72 \le B < 0.8$, the ground is raised to at least `softness + 3`, using $h = \max(h, s + 3)$ (no libm).
  - That ring is closed: it is a contour of a smooth field round a bounded region.
  - Because the density never leaves ground missing below $h - s$ (density README), no rim voxel at or below 0 is ever open.
  - The ring is therefore stone at sea level all round, and **no water can touch the basin's air**.
- **A column is a dry basin** where $B \ge 0.8$ and the gates are non-zero. Its open voxels at or below 0 are **air**, not water. This is a per-column *surface water level*: today always 0, and none in a basin. `density_block_of` and hydrology read it.
- **Caves and aquifers.**
  - Hydrology's near-surface flood rule ("water up to `z = 0`") takes the column's water level, so caves under a basin hold air.
  - Its existing wall rule 2 (a deeper voxel beside the sea's rule is stone where that rule says water) is extended to "beside a column whose rule differs". So a cave crossing from a rim column into a basin column meets stone, not a water face.
  - The hydrology test that opens whole boxes to check "nothing placed here can flow" runs on basin boxes too.
- **Hydrology's kind.** A new `TERRAIN_HYDROLOGY_KIND_BASIN`: dry land below 0. Biomes treat it as land. Plan 006's climate family then picks desert or steppe, as the basin is hot and, at 28° N, dry.
- **The climate.**
  - `terrain_height_estimate_area` gains a companion, or an optional out-array, giving each point's water level.
  - `climate_cell`'s rule becomes $\text{water} = h \le \text{level}$ (`run/src/climate/cell/index.c:72-78`).
  - Basin cells are land, and $B$'s 2.5 °C per 100 blocks fires.
  - **Worked:** a floor at -100 is $+2.5 + 1.2 = 3.7$ °C warmer than the sea-level mean, from basin heat plus negative lapse. At 28° N, plan 004's worked example 3 applies.
- Climate digests move only where basins exist. Each re-pin names this phase.

### 3.9 Cliffs and the 3D noise

- The surface is still a 2D heightfield, sampled every 4 blocks and blended. So a face of $g = 5$ is a drop of 20 blocks per lattice cell, which the blocks show as a wall.
- `softness` (4 to 26) lets the 3D `detail` noise (cell 64) bend that wall into overhangs. With `high + 0.5·face` in softness (3.2), faces get up to 26 and plateau tops stay near 4.
- **Watch:** floating rock per km² (`bench_world_map`'s `floating`, 581 to 730 after plan 003 stage 4, about 1100 to 1400 after caves). Allowed to rise at most 25%. Phase 04 records it.

### 3.10 The height estimate stays in sync and cheap

- Every new term lives in `density_profile_batch`, which `terrain_density_profile_points` and so `terrain_height_estimate*` call. The estimate is **the profile, bit for bit**, as `the_height_estimate_is_the_profile_on_the_lattice` already tests.
- The climate needs no code change for heights.
- Dry basins need one new read (3.8).
- Nothing 3D, and nothing reading the climate or biomes, enters the profile, so there is no loop.

### 3.11 Biomes read relief; the climate reads absolute height

- `terrain_density_profile_t` grows from 8 to 16 bytes: `height`, `softness`, `lift` (= $P - \text{basin}$), and padding.
- `terrain_biome_distance`'s height axis (`run/src/terrain/biome/index.c:68-74`) reads **`ground - lift`**. A 650 plateau's grassland at 660 then counts as 10: plains or savanna, not `stony_peaks`. A range on the plateau counts from the plateau's top.
- Surface rules that use absolute heights for rock bands (`high: { block: snow_block, from: 400 / 800 }`, plan 006 phase 00 Issues found) are plan 006 phase 04's to replace with the climate snowline.
- **The climate keeps absolute height:** plateau air is cooler (2.4). Plan 006's family choice reads the climate, so the Highveld comes out cooler and drier.
- **Plan 006 phase 03 is told this:** its "landform" height axis means height above the lift. This plan does not edit plan 006's files. The note goes in this plan's phase 03 and in the joint final check.

### 3.12 Determinism and no libm

- Every new term uses $+$, $-$, $\times$, $\div$, comparisons, `fabs`, and the existing spline and spread, in float64 and then float32 as today.
- No `pow`, `exp`, `sqrt` or trig decides a block. Gradients on the landform lattice are differences, and "normalised" dot products use the sign and a smoothstep of $\frac{a \cdot b}{|a|\,|b|}$, computed as $\frac{(a \cdot b)\,|a \cdot b|}{|a|^2 |b|^2}$ (squared cosine with sign). This needs no `sqrt`.
- **1 and 16 workers give the same bytes.** Every phase that changes land runs that check. Lattice nodes are pure functions, so no order can matter.

### 3.13 Atlas contour lines (phase 02)

Plan 004's master 9.3 promised "terrain with hill shading, height lines every 100". They exist only on the second Terrain layer, one pixel wide, over tree crowns.

**What changes:**

- **On by default on `terrain`.** The second press of `1` becomes `terrain_plain`, the same map without lines (Q5). The key `terrain_lines` is renamed. Goldens follow, with reasons.
- **Spacing by zoom** ($s$ blocks a pixel). On a 1:1 slope, lines must be at least 3 pixels apart:

| $s$ (zoom) | Minor line every | Index line (2 px, darker) every |
|---|---|---|
| ≤ 1 (zoom ≤ 0) | 25 | 250 |
| 1.4 to 4 (zoom 1 to 4) | 50 | 500 |
| 5.7 to 16 (zoom 5 to 8) | 100 | 500 |
| ≥ 22 (zoom ≥ 9) | 250 | 1000 |

This meets "thin every 50 or 100 blocks, thicker every 500" at the zooms Ahmed uses. The minor line halves the shade, as today; the index line multiplies it by 0.3 and is 2 pixels wide (the crossing pixel and its south-east neighbour).

- **Lines follow the ground, not the trees.**
  - The Atlas pixel has 16 bits of RGB565 and 16 bits of height (`run/include/ui/atlas/tile/index.h:11-38`). Height needs only 11 bits: -256 to 1279 is 1536 values.
  - So the pixel becomes RGB565 (16), height (11, offset from -256) and **cover depth** (5 bits: the top block's height above the first block that is not leaves, a log, a plant, snow layer or a torch, clamped to 31).
  - Contours and hill shading read `height - cover`. Colour still shows the canopy, so the map looks the same and the lines lie on the ground.
  - The tile file version goes 1 to 2. A version-1 file decodes with cover 0, so old worlds' maps still open; their lines follow trees until re-explored.
  - Mips average cover like height.
- **Labels: none by default** (Q5). The pointer readout already says the height under the pointer. An optional step draws the index height in the font every 400 pixels along index lines, if Ahmed wants it.
- **No lines over sea** (the pixel's height there is the water's top).
- **The map tools draw the same lines.** `bench_world_map_atlas` and phase 01's tool use `ui_atlas_draw_picture`.

### 3.14 Tools for tuning (phase 01, used by every later phase)

**`bench/landform/`** (`bench_landform`): headless, no card, on 16 workers, from the profile (fast) and from real blocks (exact) on sampled windows. Writes:

| Output | What |
|---|---|
| `<seed>_relief_64km.png` | 64 by 64 km round the middle, a column every 16 blocks (4096²): shaded relief with contours (index every 500) |
| `<seed>_slope_64km.png` | The same area, coloured by slope class: flat, 20:1, 5:1, 2:1, 1:1, 1:2, 1:5, steeper |
| `<seed>_plateaus_64km.png` | Plateau pieces outlined, escarpment edges in red, ranges' crest lines in brown |
| `<seed>_profiles.png` | True-scale (1:1 vertical) cross-sections: through the highest peak (2 directions), across the largest plateau's edge, along the longest range |
| `<seed>_strip.png` | An equator-to-pole strip, x from -2048 to 2048, y from 0 to 38,298, a column every 8 blocks: shaded relief, contours, and the snowline from the fallback climate (plan 006 2.2) where that exists, else today's climate formula at κ 0.5 |
| `<seed>_windows.png` | 64 windows of 1 km² chosen evenly, from real blocks (`terrain_generate_box` tops), for fine slope |
| `<seed>.csv` | Every number in section 4's table, per seed |

**The table it prints**, per seed and as min to max over the seeds:

- the slope histogram (classes in section 4), from the profile at 4 blocks and from the real windows;
- slope by relative height (deciles of local relief within 2 km);
- concavity ratio and the half-height radius over relief ($W_{50}/H$) of every peak with relief ≥ 150;
- plateau pieces, area and across;
- escarpment length;
- range pieces at 500 or over, length and elongation;
- knobs by base height;
- height bands as `bench_world_map`'s;
- sea share;
- **coordinates** of the largest plateau, the longest escarpment, the tallest peak, a knob on a plateau, and a basin, **printed as ready `/tp X Y` lines**.

It runs in phase 01 on today's land, and in phases 03 to 06 on the new.

**In the game:** plan 006 phase 01's commands (phase 00, "006 additions").

- `/tp` the printed coordinates.
- `/fly` along an escarpment.
- `/tp top` on a summit.
- F3 shows height, temperature and the snowline.
- With contours (phase 02), the Atlas shows the shape at a glance.

## 4. Targets

On 8 seeds (plan 003's eight), 64 by 64 km each, land only unless said. "Today" is measured in phase 01. The expectations given here are predictions from section 0.

| Measure | How | Today (expected) | Target |
|---|---|---|---|
| Flat, $g < 0.05$ (flatter than 20:1) | Profile, step 4 | Phase 01 | 30 to 40% |
| Gentle, 0.05 to 0.2 (20:1 to 5:1) | | | 25 to 35% |
| Moderate, 0.2 to 0.5 (5:1 to 2:1) | | | 15 to 20% |
| Steep, 0.5 to 1 (2:1 to 1:1) | | | 7 to 12% |
| Very steep, 1 to 2 | | | 3 to 6% |
| Cliff, 2 to 5 (1:2 to 1:5) | | | 1.5 to 3% |
| Sheer, > 5 | | | 0.3 to 1% |
| Of ground steeper than 1:2, the share in the top 30% of local relief or on an escarpment face | Deciles within 2 km | About 30% (noise is everywhere) | **≥ 70%** |
| Median slope in the top decile of mountain relief, over median slope in the bottom 3 deciles | Same | About 1 to 1.5 | **≥ 5** |
| Peak concavity ratio, median (3.6) | Peaks with relief ≥ 150 | About 1 | **≥ 3** |
| Plateau pieces ≥ 25 km² (≥ 250 high, local relief ≤ 80 in 512 blocks) | Connected | 0 | **≥ 2 per map; largest ≥ 80 km², ≥ 8 km across** |
| Escarpment length (a plateau edge dropping ≥ 300 within 1500 blocks) | Traced | 0 | ≥ 30 km per map |
| Longest range piece at ≥ 500 | Longest extent | Phase 01 (expected under 10 km) | ≥ 20 km on at least 6 of 8 seeds |
| Knobs (prominence 15 to 150) on a base ≥ 250 | Count | About 0 | ≥ 25% of knobs |
| Dry basins | Count, floor | 0 | 1 to 3 per map (averaged over 8), floors -40 to -100 |
| Sea | As `bench_world_map` | 24.9 to 29.1% | 25 to 30% (unchanged) |
| Reaching 500 / 1000 | Same | 4.84–6.13% / 0.17–0.29% | Plateau tier 2 adds about 8 to 12% at 550 to 650; above 750 stays near today's 1.2 to 1.6% |
| Top | Highest ground | 1151 to 1209 | 1100 to 1190 |
| Floating rock per km² | `bench_world_map` | Phase 01 | ≤ today + 25% |

These are first targets. Ahmed changes any of them at the phase 01 gate (Q3).

## 5. What does not change

- Caves, ores, their fields and their rules. Their digests move only because the ground under them moved; each re-pin says so.
- The climate's maths, apart from the dry-basin water rule (3.8) and the lapse constant if Ahmed picks one (2.4).
- The world's height range, -250 to 1250, `TERRAIN_DENSITY_HEIGHT_MIN` -116 and `MAX` 1190, the 1224 stone limit, the lava level and the aquifer rules.
- Today's fields `c`, `e`, `weirdness`, the warp, temperature and humidity: same names, same values.
- Rivers (except that they meet new ground) and beaches.
- Plan 006's biome, surface and feature rules, except the meaning of the biome height axis (3.11).
- The save format, except the generator version (section 9). The Atlas tile file goes to version 2, reading version 1 (3.13).

## 6. Costs and limits

| What | Budget | How it's held |
|---|---|---|
| Profile cost per lattice column | at most +15% | The landform lattice (3.3); `peak` only where $m > 0.6$; knobs skipped where their gate is 0; jagged's 2 octaves removed. Phase 01 measures the profile's share of `bench_terrain` |
| `bench_terrain` | ≥ 1000 columns a second at its slowest place (1134 today), **including a new plateau place** | Measured in phases 01, 03, 04 and 06 |
| `bench_lod`, the far field | 35 ms a column today, at most 40 (the same budget as plan 006) | Measured in phases 03 and 06 |
| `bench_climate` cold tile | < 100 ms (30.7 today) | Scattered estimates cost more (3.3). Measured in phase 03 |
| Render pool | Sections a column at the most crowded place ≤ 32 on average (49.5 measured among ranges, 25% headroom in `RENDER_POOL_BYTES_PER_COLUMN`) | Measured at a plateau and on a range in phase 03 |
| `bench_atlas` | Every layer < 60 ms; column top < 20 µs (12.8 today) | Cover depth costs one more test a voxel. Phase 02 |
| Determinism | 1 and 16 workers, same bytes | Every land phase |

## 7. Tests the plan relies on

- **Limits:** every spline's corners added up stay within -116 and 1190, and real columns of 8 seeds over 64 km never pass them (`tests/run/terrain/density/`).
- **The estimate is the profile:** the existing bit-for-bit test passes with the new formula, for heights and for `lift`.
- **Plateau shape:** on a synthetic $L$ (a test hook giving a straight ramp), the plateau spline gives the worked table in 3.4 within 1 block, and the back-edge stretch gives a back slope at least 4 times wider than the face.
- **Slope shaping:** $S_{\text{soft}}$ and $S_{\text{sharp}}$ at their knots exactly; monotone. On a synthetic linear envelope, the gradient at $m = 0.9$ over the one at $m = 0.1$ is:
  - 3 to 5 for $G = 0$;
  - 6 to 13 for $G = 0.5$;
  - at least 40 for $G = 1$.

  The spline's slopes near 0 differ a little from the power curve, hence the ranges.
- **Measured targets** (section 4) from `bench_landform` on 8 seeds, recorded in phases 03, 04 and 06.
- **Basins:** the rim ring is ≥ `softness + 3` everywhere; no open voxel at or below 0 in a rim column; opened basin boxes hold no fluid beside air; the climate marks basin cells land, and basin heat at a real basin equals the formula.
- **Contours:** on a synthetic plane $z = x/10$ at $s = 1$, minor lines sit exactly at the pixel columns where $\lfloor z/25 \rfloor$ changes; index lines are 2 pixels; under a synthetic tree the line follows ground, not crown; a version-1 tile decodes; goldens.
- **Determinism:** 1 against 16 workers on the new pins.
- **The tool's own self-test:** a synthetic 1:1 cone gives all its slope in the 1 to 2 class and a concavity ratio of 1.00 ± 0.05; a synthetic $p = 2.5$ peak gives 3.1 ± 0.2; a synthetic 5 by 5 km plateau gives one piece of 25 ± 1 km².

## 8. Risks

| Risk | What's done about it |
|---|---|
| `bench_terrain` falls under 1000, from more octaves and taller columns (caves to the ground's top) | The landform lattice; jagged removed; measure first (phase 01). If short, cache the profile per column job (hydrology recomputes the density's lattice today: "A lattice column costs the density's 30 to 35 octaves over again", `run/src/terrain/hydrology/README.md`). Recorded as an option, not done unless needed |
| Plateaus look like flat tables | Knobs, hills, ruggedness, the edge warp, the dip slope (3.4, 3.7); judged on pictures and by Ahmed's walk |
| The 32-block landform lattice shows as straight lines | Its fields are smooth over 2 km or more; checked on pictures. Fallback: direct sampling (3.3) |
| Too much high land squeezes the sea or the lowland | Sea is held to 25 to 30%; plateau shares are targets (section 4) |
| Spawn: `game_spawn_find` wants ground ≤ 120 (`run/src/game/spawn/index.c:27`) | Coastal plains remain; `run_game_spawn`'s 72 seeds must still pass |
| Rivers: plateaus cut rivers that used to reach the sea; the "90% joined" test (`rivers_hold_water_and_reach_the_sea_and_the_coast_has_beaches`) may drop | Measured in phase 03; plateaus are pulled inland by $b$, where rivers already fade (`reach` ends at $c = 0.15$) |
| Dry basins let water touch air somewhere | The rim rule, the wall rule, and the box test that opens whole boxes (3.8); phase 05 doesn't close until it passes on 8 seeds |
| Floating rock rises with steeper faces and more softness on faces | Watched, ≤ +25%, phase 04 |
| The render pool overflows at a plateau with ranges | Measured in phase 03; the near columns already get 96 sections |
| Plan 006 tunes biomes on the old land and must redo it | The recommended order (section 0) and one joint final check |
| The climate's tiles all move, so plan 004 phase 11's tuned numbers (survey, census) shift | Expected; the joint final check reruns plan 004 phase 11's survey with plan 006's |

## 9. Generator version and old worlds

- **The land changes in phases 03, 04 and 05**, so every terrain digest moves. Each re-pin names the phase and why, the same pattern as plan 006.
- **One bump, by default** (Q2). With the recommended order, the joint final check (006 phase 05 together with 007 phase 06) raises `TERRAIN_GENERATOR_VERSION` from 1 to **2**, once, and adds row 2 of `FEA_DIGEST_OF_GENERATOR` with both plans named.
- **If plan 006 phase 05 has already shipped version 2** when 007's land lands: 007 phase 06 raises it to **3** and lists the version-2 worlds for Ahmed to keep or delete.
- **Between the first land change and the bump**, worlds Ahmed makes from `main` would say "version 1" over changed land.
  - **Default:** phase 03's first land-changing commit also raises the version to the next number, and adds that row marked "open".
  - The final check closes it.
  - Each phase file tells Ahmed that worlds made while the version is open are test worlds.
  - Plan 006's files are not edited; if Ahmed agrees, whichever plan changes land first does this (Q2).
- **Old worlds:** as plan 006 decided, delete `saves/test_1` to `test_8` once, in the joint final check, after listing them with sizes. Ahmed may keep any he names.

## 10. Out of scope

- Raising the world top above 1250.
- Rivers on plateaus, waterfalls off escarpments, canyons, erosion simulation, or anything that needs neighbours' heights beyond the landform lattice.
- Volcanoes, glaciers, sea cliffs and coastal landforms beyond what plateaus give.
- Rock strata and geology as blocks (sandstone bands in an escarpment face): a later "rock layers" plan.
- New blocks and new biomes (plan 006's families pick from today's).
- Biome tint, vegetation changes: plan 006's.
- Settlements.

## 11. Phases

Each phase ends somewhere worth stopping, and has its own file with plan 004's rich structure:

- Goal, read first, where it fits, what it must NOT build, Ahmed's words;
- background explained, design, owned paths, steps with a *Why*;
- todo, tests with numbers, verification, gate, risks;
- Commits, and Issues found.

Every heavy command goes through `tools/test-or-build.sh`, one command per call, with `timeout` inside.

### Phase 00: The vision and the plan files — done

- [x] This master and phase files 01 to 06, every box of 01 to 06 unticked
- [x] Phase 00 restates section 1's layer order, the ordering with plan 006, and the "006 additions" this plan relies on

### Phase 01: Measure today's terrain

- [ ] `bench/landform/` with every output in 3.14 and its self-test (`run_bench_landform_self_check`)
- [ ] Run on 8 seeds over 64 by 64 km, plus the equator-to-pole strip; fill section 4's "Today" column in the phase file
- [ ] The profile's share of `bench_terrain` time, and the cost of the tallest place, recorded
- [ ] Ahmed's gate: he looks at the pictures and the table, and answers Q1, Q3

**Gate:** Ahmed sees today's slope map, profiles and strip, and says whether "1:1 everywhere" is what he saw; he picks the scale (Q1) and confirms or moves the targets (Q3).

### Phase 02: Atlas contour lines

- [ ] Pixel with cover depth; tile file version 2 that reads version 1
- [ ] Contours on `terrain` by default, zoom-adaptive spacing, index lines; `terrain_plain` as the second press
- [ ] Goldens, `bench_atlas`, the map tools
- [ ] Optional: index labels (Q5)

**Gate:** Ahmed presses `M` in his own world and sees contour lines on the Terrain layer, on the ground under the trees.

### Phase 03: Plateaus, escarpments and long ranges

- [ ] `run/src/terrain/landform/`: the landform lattice, its fields, $\nabla$ on nodes
- [ ] The plateau spline, the edge warp, the back-edge stretch; ranges at cell 8192 with `flank`, `massif` 4096, crest scaled by $m$; the range standing on $P$ inside the budget
- [ ] `lift` in the profile; the biome height axis reads `ground - lift`
- [ ] The version opened (section 9); every pin re-pinned with reasons; 1 against 16 workers
- [ ] `bench_terrain` with a plateau place, `bench_lod`, `bench_climate`, the render pool
- [ ] `bench_landform` on 8 seeds: plateau, escarpment and range targets

**Gate:** Ahmed `/tp`s to the printed largest plateau, flies along its escarpment, stands on the rim, looks inland, and reads the Atlas contours.

### Phase 04: Slope profiles, and hills on any ground

- [ ] $S_{\text{soft}}$, $S_{\text{sharp}}$ and ruggedness $G$; `peak` replacing `jagged`
- [ ] Hills at cell 384 and ≤ 30; knobs with dome or koppie shapes on any base
- [ ] Softness on faces; floating rock watched
- [ ] `bench_landform`: slope histogram, slope by relative height, concavity, knobs on plateaus, all in section 4's targets on 8 seeds

**Gate:** Ahmed walks up a range from its foot to its summit, with no fly, and says whether it goes from 5:1 to steep near the top; he finds a koppie on a plateau.

### Phase 05: Dry basins below sea level

- [ ] `landform/basin`, the rim rule, the per-column water level in density and hydrology, `TERRAIN_HYDROLOGY_KIND_BASIN`
- [ ] The flood and wall rules for caves under and beside basins; the whole-box fluid test on basin boxes
- [ ] The climate's water rule reads the dry mask; basin heat at a real basin
- [ ] `bench_landform`: basin count and floors on 8 seeds

**Gate:** Ahmed `/tp`s into a printed basin, stands at -80 in dry air, and reads a hotter F3 temperature.

### Phase 06: Tuning, version and the final check

- [ ] Ahmed's tuning round on all of it, with the tool and in the game; every number he changes recorded
- [ ] The lapse decision (Q6)
- [ ] The joint final check with plan 006 phase 05 (or alone): the version closed, old worlds listed and deleted, full debug and release suites, release build, 1 against 16 workers, benches, `bench_landform`'s "after" table beside phase 01's "before"

**Gate:** Ahmed's walk: coast, escarpment, plateau, koppie, range, summit, basin; his notes recorded.

## 12. Still open for Ahmed

Each has a default, and none blocks approval.

| # | Question | Default if not answered |
|---|---|---|
| Q1 | Plateau scale: **A** true latitude scale, about 4 km across (uplift cell 16384, smaller); **B** 6 to 16 km (cell 24576); **C** 20 to 40 km, a plateau spanning 8 to 15° of latitude (cell 65536) | **B** |
| Q2 | Order and version: 007's land before 006's biomes, with **one** bump to 2 in a joint final check, the version opened at the first land change | Yes, as section 0 and 9 say |
| Q3 | The targets of section 4 (slope classes, plateau sizes, concavity ≥ 3, cliffs ≥ 70% high up) | As written, until the phase 01 gate |
| Q4 | Dry basins: how many, how deep, and a salt lake at the bottom or bone dry? | 1 to 3 per 64 km map, -40 to -100, dry (a lake can come later) |
| Q5 | Contours: on `terrain` by default with `terrain_plain` second; labels on index lines? | On by default; no labels |
| Q6 | Cooling with height: 0.012 (today), 0.015 or 0.020 °C a block (2.4) | 0.012 until Ahmed has tried F3 on a plateau and a summit |
| Q7 | "600 km high": does it mean 600 km north to south (scaled), or something else? | Read as the plateau's depth, scaled with its width |

## Verification of this plan

    ls plans/007-*
    grep -L "## Issues found" plans/007-*phase-*.md   # prints nothing
    git status --short                               # nothing uncommitted
