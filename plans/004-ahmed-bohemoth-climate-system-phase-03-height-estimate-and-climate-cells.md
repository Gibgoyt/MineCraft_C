# 004-ahmed-bohemoth-climate-system — phase 3: Height estimate and climate cells

**Status:** in progress: built and agent-verified; Ahmed's gate (the pictures) and the live progress bar are left   (updated 2026-11-06)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then sections 3, 6.1, 6.2 and 6.9 of `plans/004-ahmed-bohemoth-climate-system.md`. Every number here comes from the master plan. Where this file adds a number the master plan does not give, it says so and gives the reason.

## Goal

For any seed, any place in the world can be asked for its climate cell, and gets the same answer on every run and on any number of workers. A cell is a 64 by 64 block square. It knows its latitude, its mean ground height, whether it is sea, a large lake or land, how far it is to the nearest connected sea and in which direction, how far a sea cell is from land and in which direction, and its continentality $\kappa$. Cells are worked out a tile (1024 by 1024 blocks) at a time by background jobs, nearest the player first, kept in a cache, and never saved. A new world works out the tiles around spawn behind a progress bar before play starts. Between cell centres, values are read with a warped bilinear blend, so regions blend. `terrain_height_estimate` exists as a public pure function, and every terrain digest the repo pins is unchanged. Four map layers exist (sea distance, continentality, sea and lakes, latitude lines) and are drawn to PNG. One tile on one worker takes under 100 ms.

## Where this fits

**The vision.** Ahmed's game is a stack worked out strictly top to bottom: seed, terrain, climate, then (in later plans) geology, vegetation, settlements, the economy and the player's tasks. The rule the whole stack rests on is "the land decides; nothing is hand-placed" (vision file, Part 1). This phase is the bottom of the climate layer. It is the first place where the climate looks at the land.

**What later phases of plan 004 read from it:**

| Reader | What it reads |
|---|---|
| Phase 04, currents | `land_distance` and `land_direction` of sea cells (which coast side, the strength fade over 600 blocks). The nearest sea of each land cell |
| Phase 04, pressure and wind | $\kappa$ (heat lows), `z_ground` (windier peaks), the coarse heights (slope steering) |
| Phase 05, temperature | $\kappa$ (yearly swing and lag), `z_ground` (cooling with height), latitude |
| Phase 05, rain | The coarse land mask, for the upwind walk that goes on "over the coarse land mask, up to 8 km". Large lakes, which count as water for moisture |
| Phase 06, storms | The coarse land mask, so a storm can be tracked over land nobody has generated (master plan 2.5, 7.3) |
| Phase 07, floods | The height estimate, for flow accumulation over land not yet generated |
| Phase 09, the Atlas | The same layer code this phase starts, in `run/src/climate/layer/` |

**What later plans read from it.** The vision's table: settlements want "mild temperatures, water, few floods", so distance to the sea and to large lakes is one of their inputs. The economy wants "a sea lane is cheap along a favourable current", and currents (phase 04) need this phase's coast directions. Vegetation chooses biomes by temperature and rain, which phase 05 builds on $\kappa$. **Geology** (a later plan) will change the ground: plateaus, escarpments, basins. That is why the climate reads the ground only through `terrain_height_estimate`. When geology changes the terrain's splines, the estimate follows, and every climate cell follows with it, with no change to plan 004 (vision file, Part 1: "When it lands, climate is simply recomputed from the new ground").

**What this phase must NOT build:**

- No change to what the terrain generates. Not one block. Every pinned terrain digest must hold (master plan 0, "Changes no terrain").
- No temperature, rain, wind, currents or pressure. Those are phases 04 and 05. The cell's fields for them stay zero (and `hurricane_days` stays "none").
- No biome change, no vegetation, no geology, no landforms (master plan 14).
- No saving. Cells come back from the seed (master plan 6.9, vision rule 1).
- No Atlas screen. Only the layer drawing code and pictures written by a tool. The Atlas is phase 09.

## Ahmed's words

- "A single year is 365 days, altitude, distance from the ocean or any considerably large water bodies must somehow affect this hard-coded at generation time so we don't waste runtime compute"
- "regions need to blend into each other"
- "an equator system assuming spawn near x,y 0,y that is the equator ... every 10km is on 23.5 degrees"
- "for now we are only creating a climate system and not yet a geological/rock system please!"
- From the vision: "Any generated map from any seed somehow needs all of these economics in place"

"Hard-coded at generation time" is answered by the cache: a cell is worked out once from the seed and kept. "Considerably large water bodies" is answered by the flood fill: connected sea, plus lakes over 2 km². "Blend into each other" is answered by the warped bilinear read.

---

## Background, explained

Each idea this phase uses, in the order the code meets it. Each has a worked example with real numbers.

### Struct

A **struct** is a block of memory holding several named values side by side, at fixed byte offsets. In this repo every struct's size is checked when the program is compiled (`static_assert`), and the compiler is never allowed to insert hidden gap bytes: any gap is written out as a field named `_pad0`, `_pad1`. So anyone reading a struct knows exactly where each value is.

Example: `climate_cell_t` (below) is 128 bytes. `kappa` is at byte 12. A tile has 256 cells, so a tile's cells are $256 \times 128 = 32\,768$ bytes, 32 KiB, one after another. Cell number 17 of a tile starts at byte $17 \times 128 = 2176$.

### Pure function

A **pure function** gives an answer that depends only on its inputs: no hidden state, no "what ran before". `terrain_height_estimate(terrain, x, y)` gives the same height for the same seed and the same `x, y`, whether it is the first call of the program or the millionth, and on whichever thread. This is what lets the world be infinite: nothing has to be worked out ahead of time, and any piece can be worked out in any order.

Example: worker 3 asks for the height at $(5000, -12000)$ at 10:00 and worker 11 asks at 10:05. Both get exactly the same 32-bit float, bit for bit.

### Spline

A **spline** is a smooth curve through a list of fixed points called **knots**. The terrain turns noise values into heights through splines (`run/src/terrain/density/index.c:31-70`). The repo's splines use Fritsch and Carlson's slopes, which never let the curve go above or below its knots between two knots.

Example from the real code: `DENSITY_KNOTS_CONTINENTAL` has knots $(-0.1, -10)$ and $(-0.04, 3)$. A continentalness of $-0.07$ lies between them, so the base height lies between $-10$ and $3$ blocks.

### The height estimate

The terrain decides a column's ground in two layers (`run/src/terrain/density/README.md`):

1. A **profile**: from 2D noise and splines, a `height` (where the ground would be with no 3D noise) and a `softness` (how far 3D noise can move the ground either way). Softness is 4 to 30 blocks (`TERRAIN_DENSITY_SOFTNESS_MIN`, `_MAX`).
2. **3D density**: worked out on a lattice every 4 blocks across and every 8 blocks up, and blended between lattice points. A voxel is stone where density is above 0.

The **height estimate** is layer 1 only, blended between the four lattice columns around the point exactly as layer 2 blends them: first along x, then along y. It needs no 3D noise and no chunk, so it costs a small part of a real column.

**Worked example.** The point $(1, 3)$ lies in the lattice square with corners at $(0,0)$, $(4,0)$, $(0,4)$, $(4,4)$. Say the four profile heights are $h_{00} = 40$, $h_{40} = 44$, $h_{04} = 48$, $h_{44} = 60$. The fractions across the square are $t_x = 1/4 = 0.25$ and $t_y = 3/4 = 0.75$.

$$\text{south} = 40 + 0.25\,(44 - 40) = 41, \qquad \text{north} = 48 + 0.25\,(60 - 48) = 51$$

$$h = 41 + 0.75\,(51 - 41) = 48.5 \text{ blocks}$$

**How far the real ground can be from it.** Read from `density_plane` and `density_fade` (`run/src/terrain/density/index.c:319-360`): at each lattice corner the density is $\frac{h_i - z}{s_i}$ plus noise of size at most 1 times a fade. When $\frac{h_i - z}{s_i} > 1$ that corner's value is surely above 0, and when it is below $-1$ it is surely below 0. Between lattice levels 8 blocks apart, values are blended along z, which can move the crossing by up to one level. So the top stone voxel $z_{\text{top}}$ of a column always obeys:

$$\min_i (h_i - s_i) - 8 \;\le\; z_{\text{top}} \;\le\; \max_i (h_i + s_i) + 8$$

The header already uses the same 8 blocks: `TERRAIN_DENSITY_NEVER_SOLID_FROM = HEIGHT_MAX + SOFTNESS_MAX + CELL_Z` (`run/include/terrain/density/index.h:33-34`). The agreement test in this phase checks this bound on every column, and also measures how often the ground is within the softness alone.

### Latitude

Phase 02 builds latitude $\varphi(y)$, a triangle wave that repeats every $153\,191$ blocks (master plan 5.1). This phase only calls it, at each cell's centre. Example: a cell whose centre is at $y = 19\,168$ has $\varphi = \frac{180}{\pi} \arcsin\big(\sin\frac{\pi \times 19168}{76596}\big) = 45.04°$.

### Cell and tile coordinates, including negative ones

A **cell** is a 64 by 64 block square. A **tile** is 16 by 16 cells, 1024 by 1024 blocks (master plan 1). For a block coordinate $x$:

$$\text{cell}_x = \lfloor x / 64 \rfloor, \qquad \text{tile}_x = \lfloor x / 1024 \rfloor, \qquad \text{cell in tile} = \text{cell}_x \bmod 16$$

The floor matters for negative numbers. In C, `x / 64` rounds toward zero, which is wrong for negatives. This repo already uses arithmetic right shift for floor (`x >> DENSITY_CELL_XY_SHIFT`, `run/src/terrain/density/index.c:435`), and GCC defines `>>` on a negative signed number as arithmetic. So the code writes `x >> 6`, `x >> 10` and `(x >> 6) & 15`.

**Worked example.** $x = -1$: $-1 \gg 6 = -1$, so cell $-1$, which covers blocks $-64$ to $-1$. $-1 \gg 10 = -1$, so tile $-1$, covering $-1024$ to $-1$. $(-1) \,\&\, 15 = 15$, the last cell of that tile. The cell's centre is $64 \times (-1) + 32 = -32$. With plain division, `-1 / 64 = 0` would have put it in cell 0, on the wrong side of the origin.

### The land mask, a sparse table, and its pages

The **land mask** says, for one point per cell (the cell's centre), whether the estimated ground is above sea level: land if $h > 0$ (`MC_WORLD_Z_SEA` is 0). The height itself is kept, not just the yes or no, because phase 04 needs slopes from it and phase 07 needs it for water flowing downhill.

A **sparse table** stores only the entries that have been asked for. An infinite world can't have an array for every place, but it only ever asks about a finite area near the player, so a sparse table costs only what has been used. Here the entries are grouped into **mask pages**: one page holds the 256 centre heights of one tile, as `float32_t`, 1024 bytes.

Example: a page for tile $(2, -3)$ holds the heights at $x = 2048 + 32 + 64 i$, $y = -3072 + 32 + 64 j$ for $i, j = 0 \ldots 15$.

### Hash table, and the toroidal table used here

A **hash table** finds an entry from its key without searching everything: a **hash function** turns the key into a slot number, and the entry lives in that slot. When two keys want the same slot, that is a **collision**, and the table needs a rule for it.

This phase uses the simplest kind, the one `run/src/world/map/` already uses for columns (`run_world_map` tests "the toroidal table"): the hash of tile $(t_x, t_y)$ is its coordinates modulo the table's side $N$:

$$\text{slot} = (t_y \bmod N) \cdot N + (t_x \bmod N)$$

With $N$ a power of two, the modulo is a bit mask that also works for negative numbers in two's complement: `t & (N - 1)`. It is called **toroidal** because the table wraps around like the surface of a ring: going off the east edge comes back on the west. One slot holds one tile at a time. **The collision rule is replacement:** two tiles that share a slot are at least $N$ tiles apart, so when the player is near one of them, the other is far away and can be dropped. It will be worked out again from the seed if anyone comes back.

**Worked example,** with $N = 64$: tile $(-1, 70)$ goes to slot $(70 \,\&\, 63) \cdot 64 + ((-1) \,\&\, 63) = 6 \cdot 64 + 63 = 447$. Tile $(63, 6)$ also goes to slot 447. They are 64 tiles (64 km) apart in each axis, so only one of them can be near the player.

### Cache

A **cache** keeps an answer after it has been worked out once, so it isn't worked out again. The climate tile cache keeps each tile's 256 cells. It is safe to drop any entry at any time, because the answer is a pure function of the seed: dropping it only costs time, never correctness.

### Job system

`run/src/job/` runs small pieces of work, called **jobs**, on 16 pinned **worker** threads (`MC_THREADS_WORKERS`). A thread outside the workers queues a job with `job_try_submit` or `job_submit`, and waits for a **group** of jobs with `job_group_wait`. Each worker has its own **Job arena**: 32 MiB of scratch memory (`JOB_ARENA_BYTES`) that is emptied after every job. Its rules (`run/src/job/README.md`): a job may not wait for other jobs, may not wait on a lock another job holds, and everything it takes from the arena is gone when it returns.

So here: one tile is one job. A job builds its tile's cells in its arena's scratch memory and writes the result into the cache. It never waits for anything.

### Atomics and the reader count

Several threads touch the cache at once: the sim thread decides which tiles to build, workers build them, and other threads read cells. An **atomic** variable is one that threads can read and write without tearing it, with a stated **memory order** that says what other memory the read or write makes visible. **Release** on a write and **acquire** on the matching read mean: everything written before the release is visible after the acquire. **Sequentially consistent** (`seq_cst`) is the strongest order: all such operations happen in one order every thread agrees on.

Each cache slot has a `state` and a `readers` count. A reader adds 1 to `readers`, then checks `state`. Something wanting to replace the tile first changes `state` to `EVICTING`, then checks `readers`. Both use `seq_cst`. **Why this is safe:** with `seq_cst`, the two threads can't both miss each other's write. Either the replacer sees `readers > 0` and backs off, or the reader sees `EVICTING` and backs off. This is the same argument as Dekker's algorithm for two threads.

Example timeline: reader R does `readers: 0 → 1`, then loads `state` and sees `READY`, so reads the cells. Replacer W then does `state: READY → EVICTING`, loads `readers` and sees 1, so puts `state` back to `READY` and tries again later. R finishes and does `readers: 1 → 0`.

### Flood fill and connected components

A **flood fill** starts at one grid point and spreads to each neighbour that matches a rule, then to theirs, until nothing more matches. Here the rule is "below sea level", and neighbours are the four sharing an edge (north, south, east, west), called **4-connected**. All the points one fill reaches form a **connected component**. Recursion is banned in this repo (`plans/CODING_STANDARD.md` section 4), so the fill keeps a **queue** of points still to visit, in arena memory.

**Worked example,** on a 6 by 6 grid, `#` land and `.` water:

```
y=5  . . # # # #
y=4  . . # . . #
y=3  . # # . . #
y=2  . # # # # #
y=1  . . # # . #
y=0  . . . # . .
     x=0 1 2 3 4 5
```

Scanning rows from $y = 0$, the first water is $(0,0)$. Its fill reaches all of column 0, $(1,0)$, $(2,0)$, $(1,1)$, $(0,4)$, $(1,4)$, $(1,5)$ and so on: component A, 12 points, touching the grid's edge. The next unvisited water is $(4,0)$: its fill reaches $(4,1)$ and $(5,0)$, component B, 3 points, touching the edge. The last is $(3,3)$: it reaches $(4,3)$, $(3,4)$, $(4,4)$, component C, 4 points, not touching the edge. With 4-connectivity, $(4,1)$ does **not** connect to $(3,3)$ through the diagonal: that is why 4 and not 8 neighbours, so water does not leak through a corner where two land cells meet.

### Which water is sea: the rule for an infinite world

The master plan says the fill "starts from open ocean". In a finite world, open ocean is whatever touches the map's edge. An infinite world has no edge, and a fill can't run forever. So each tile decides inside a **window**: the tile plus 8 km of margin on every side (the margin the master plan already gives the distance transform). The window is $16 + 2 \times 128 = 272$ cells a side, 17 tiles, 17.4 km.

**The rule:** a water component in the window is **sea** if it touches the window's edge, or if its area inside the window is at least `CLIMATE_SEA_AREA_MIN_CELLS = 8192` cells ($8192 \times 64^2 = 33.6$ km²). Any other water component is a **lake**. A lake of at least `CLIMATE_LAKE_LARGE_CELLS = 489` cells ($489 \times 4096 = 2.003$ km²) is a **large lake**, which counts as water for moisture in phase 05 and is flagged `CLIMATE_CELL_LAKE`. Smaller lakes are treated as land for every climate purpose.

**Why this rule:**

- A component that touches the window's edge reaches at least 8 km from the tile. In this terrain, ground below 0 needs continentalness below about $-0.07$ (`DENSITY_KNOTS_CONTINENTAL`, `run/src/terrain/density/index.c:31-34`), and continentalness is a noise whose largest wave is 2048 blocks (`CLIMATE_SHAPE_CONTINENTALNESS`, `run/src/terrain/climate/index.c:12`). Low ground that runs on for 8 km is a sea, not a pond.
- A body wholly inside the window but over 33.6 km², about 6 by 6 km, is an inland sea. Treating it as sea is closer to what Ahmed means by "considerably large water bodies" than calling it a lake.
- The rule depends only on the window, which depends only on the tile. So a tile's cells are the same whatever else has been worked out, which the master plan's test asks for.

**Example.** In a window, a component touches the west edge with 3000 cells: sea. A component in the middle with 600 cells: a large lake ($600 \times 4096 = 2.46$ km²). One with 100 cells: a small lake, treated as land.

### Distance transform

A **distance transform** takes a grid where some points are marked (here: sea) and gives every point its distance to the nearest marked point, without searching outward from each point one at a time. This phase uses the exact method of Felzenszwalb and Huttenlocher, which is two sweeps: first along every row, then along every column.

**Pass 1, along a row.** For each point $q$ in a row, find the squared distance along the row to the nearest marked point: $d_1(q) = \min_p \big((q - p)^2 + f(p)\big)$, where $f(p) = 0$ at a marked point and $\infty$ elsewhere. Each marked point $p$ makes a **parabola** $(q - p)^2 + f(p)$, and the answer is the **lower envelope**: the lowest parabola at each $q$. The algorithm walks the row once, keeping the list of parabolas that are lowest somewhere, and the points $z$ where one takes over from the next. Two parabolas from $p_1 < p_2$ cross at

$$s = \frac{\big(f(p_2) + p_2^2\big) - \big(f(p_1) + p_1^2\big)}{2\,p_2 - 2\,p_1}$$

**Worked example, pass 1.** A row of 8 points with sea at $p = 2$ and $p = 6$. The two parabolas $(q-2)^2$ and $(q-6)^2$ cross at $s = \frac{36 - 4}{12 - 4} = 4$. So points 0 to 4 take $(q-2)^2$ and points 5 to 7 take $(q-6)^2$ (at $q = 4$ both give 4; the earlier one is kept, so ties always go to the lower index):

| $q$ | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| $d_1(q)$ | 4 | 1 | 0 | 1 | 4 | 1 | 0 | 1 |
| nearest $p$ | 2 | 2 | 2 | 2 | 2 | 6 | 6 | 6 |

**Pass 2, along a column.** The same walk again, but now $f$ is pass 1's result in that column: $D(x, y) = \min_{y'} \big((y - y')^2 + d_1(x, y')\big)$. That is the exact squared Euclidean distance, because $(x - x_s)^2 + (y - y_s)^2$ splits into a row part and a column part. Pass 1 also records, per point, the column of its nearest marked point; pass 2 records the row. The nearest marked point of $(x, y)$ is then $(\text{col}_1[y^*][x],\, y^*)$, where $y^*$ is the row pass 2 chose.

All squared distances are whole numbers of cells squared, so they are exact in `uint32_t`. Only the crossing points $s$ are fractions, kept in `float64_t`.

**From cells to blocks.** The master plan wants "every cell's distance to the nearest sea". Cell centres are 64 blocks apart and the coast lies somewhere between a land centre and a sea centre, so this phase takes the coast as half a cell in from the sea centre:

$$d_s = \min\!\big(8192,\ \max(0,\ 64\sqrt{D} - 32)\big) \text{ blocks}$$

**Worked example.** A land cell whose nearest sea cell is 3 cells east and 4 cells north: $D = 3^2 + 4^2 = 25$, $d_s = 64 \times 5 - 32 = 288$ blocks. The direction to that sea is $(3, 4)/5 = (0.6, 0.8)$. A land cell next to the sea, $D = 1$: $d_s = 32$. A cell with no sea in the window: $D = \infty$, $d_s = 8192$, direction $(0, 0)$.

**The same transform the other way.** Run once more with the land cells marked. That gives every sea cell its distance and direction to the nearest land, `land_distance` and `land_direction`. Phase 04 needs exactly this for currents: $\vec n$, "the unit vector pointing to the nearest land", and the 600-block fade offshore.

**Why the margin is 8 km and the cap is 8192.** A tile's outermost cell centre is 32 blocks inside the tile. The window reaches $128 \times 64 = 8192$ blocks past the tile's edge, so any sea within 8192 blocks of any of the tile's cells is inside the window. Beyond that, the distance is capped anyway.

### Continentality

**Continentality** $\kappa$ (kappa) measures how far a place is from the sea's calming influence. The master plan's formula (6.2):

$$\kappa = 1 - e^{-d_s / 1500}$$

$e^{-u}$ is the exponential decay: it is 1 at $u = 0$ and falls toward 0, by a factor of $e \approx 2.718$ for each 1 added to $u$. So $\kappa$ is 0 on the coast and climbs toward 1 inland.

**Worked examples:**

| $d_s$ (blocks) | $d_s / 1500$ | $e^{-d_s/1500}$ | $\kappa$ |
|---|---|---|---|
| 0 (sea) | 0 | 1 | 0 |
| 32 (next to the sea) | 0.0213 | 0.9789 | 0.0211 |
| 288 | 0.192 | 0.8253 | 0.1747 |
| 1500 | 1 | 0.3679 | 0.632 |
| 4500 | 3 | 0.0498 | 0.950 |
| 8192 (the cap) | 5.461 | 0.00425 | 0.99575 |

Large lakes do not lower $\kappa$: $d_s$ is to the connected sea only, as the master plan says ("never as sea"). Lakes enter the climate through moisture in phase 05.

### Bilinear blend

A **bilinear blend** finds a value between four grid points as a weighted average, weighted by closeness: first blend along x on the south pair and on the north pair, then blend those two along y. It is what makes cell values flow smoothly instead of jumping at cell borders.

**Worked example.** Cells $(0,0)$, $(1,0)$, $(0,1)$, $(1,1)$ have centres at $x, y \in \{32, 96\}$ and $\kappa$ values 0.10, 0.30, 0.20 and 0.60. At the point $(48, 80)$: $t_x = (48 - 32)/64 = 0.25$ and $t_y = (80 - 32)/64 = 0.75$.

$$\text{south} = 0.10 + 0.25\,(0.30 - 0.10) = 0.15, \quad \text{north} = 0.20 + 0.25\,(0.60 - 0.20) = 0.30$$

$$\kappa = 0.15 + 0.75\,(0.30 - 0.15) = 0.2625$$

### Domain warp

A **domain warp** moves the point a value is read at by a smooth noise before reading it. Without it, the borders between regions would follow the 64-block grid in straight lines. With it, they wobble. The master plan (6.9) asks for "a small warp noise, up to 32 blocks".

Here: two 2D noise fields $W_x$ and $W_y$, each spread to $-1 \ldots 1$ by the repo's own spread function (`density_spread`, `run/src/terrain/density/index.c:239-244`), and the read point becomes $(x + 32\,W_x,\ y + 32\,W_y)$.

**Worked example.** At block $(1000, 2000)$ the two warp fields give $W_x = 0.5$ and $W_y = -0.25$. The cells are read at $(1016, 1992)$. A border that would have been at $x = 1024$ is met 16 blocks sooner here, and somewhere else it might be met 20 blocks later.

**Latitude is not blended or warped.** It is worked out exactly from the read point's own $y$. Blending it would be wrong at the poles, where latitude turns back: two cells either side of a pole both have about $89.6°$, and a blend between them never reaches 90.

### Background precompute and a progress bar

A tile takes tens of milliseconds. A new world needs about 49 of them around spawn before play. That is worked out on all 16 workers while the loading screen shows how many are done, so the player sees progress instead of a frozen screen.

---

## Design

### Directories and files

`run/src/climate/` is a new **system** at depth 1, an **Owner** (it reserves memory). Phase 02 creates it for latitude and the sun. **If phase 02 has not created it, this phase does,** and adds `climate` to the dependency table in `compile/src/standards_lint/rules_include/index.c` and to `plans/REPO_STRUCTURE.md` section 11 (without that, the lint fails with `unknown_system`: `compile/src/standards_lint/rules_tree/index.c:237-243`).

**Its rank in the lint table: 8**, between `gpu_bridge` (7) and `terrain` (9). It reads `terrain` and `job`, which are below it, and `render` (6), `ui` (3) and `game` (2) may read it.

New directories this phase adds, each with exactly one `index.*` and a `README.md` stating purpose, tier, what it owns, lifetime, threads, public functions, callers and dependencies (`plans/REPO_STRUCTURE.md` section 3):

| Directory | Depth, tier | Job |
|---|---|---|
| `run/src/climate/` (extended) | 1, Owner | `climate_init`, `climate_deinit`, `climate_open`, `climate_close`: the region, the warp fields, the scheduler, the public reads |
| `run/src/climate/mask/` | 2, Arena | Builds a mask page from the ground; assembles a tile's 272 by 272 window from pages, building missing ones |
| `run/src/climate/sea/` | 2, Arena | The flood fill and the distance transform, on a grid the caller gives. Knows nothing of tiles or terrain, so tests feed it synthetic grids |
| `run/src/climate/cell/` | 2, Arena | `climate_cell_t`, building one tile's 256 cells from a window, and the warped bilinear read |
| `run/src/climate/cache/` | 2, Arena | The two toroidal tables, slot states, the reader count, claim and publish |
| `run/src/climate/layer/` | 2, Arena | Map layers to XRGB8888 pixels. The Atlas (phase 09) and the map tools draw with this same code |

Headers mirror them under `run/include/climate/<part>/index.h`. Tests:

| Directory | ctest |
|---|---|
| `tests/run/climate/sea/` | `run_climate_sea` |
| `tests/run/climate/mask/` | `run_climate_mask` |
| `tests/run/climate/cell/` | `run_climate_cell` |
| `tests/run/climate/cache/` | `run_climate_cache` |
| `tests/run/climate/layer/` | `run_climate_layer` |
| `tests/run/climate/fixtures/` | Not a test (discovery skips `fixtures/`): `ground.h`, synthetic grounds shared by the climate tests here and in phase 04 |

Bench: `bench/climate/` (`index.c`, `README.md`, results history in the README), built as `build/<preset>/bin/bench_climate` by the existing discovery (`plans/REPO_STRUCTURE.md` section 10).

### The height estimate, in `terrain`

Nothing that exists changes. Three functions are added.

In `run/include/terrain/density/index.h` and `run/src/terrain/density/index.c`:

```c
/* The profile at count columns anywhere, in blocks, through the same batch the box uses: the very bits a box's lattice
 * gets at a whole-number column. Allocates nothing. Must be finite. */
void terrain_density_profile_points(const terrain_density_t *density, const float64_t *xs, const float64_t *ys,
                                    uint64_t count, float32_t *heights, float32_t *softness);
```

It loops over `density_profile_batch` (`run/src/terrain/density/index.c:247`) in pieces of `DENSITY_BATCH_COLUMNS` (128). It is a wrapper only, so no existing number moves.

In `run/include/terrain/index.h` and `run/src/terrain/index.c` (the Owner, today 88 lines):

```c
/* The ground the terrain's 2D splines give at a column, with no 3D density and no chunk: the profile at the four lattice
 * columns round it (TERRAIN_DENSITY_CELL_XY apart), blended along x, then y, as terrain_density's boxes blend them. A pure
 * function of the terrain and the column. The climate reads the ground through this and nothing else. */
terrain_density_profile_t terrain_height_estimate_profile(const terrain_t *terrain, int32_t x, int32_t y);
float32_t terrain_height_estimate(const terrain_t *terrain, int32_t x, int32_t y);  /* .height of the above */

/* The same at count_x by count_y columns step blocks apart, x changing fastest. origin_x, origin_y and step must be whole
 * multiples of TERRAIN_DENSITY_CELL_XY (asserted): every column is then a lattice column, the blend's fractions are 0, and
 * each value is bit for bit the single-column answer. Allocates nothing: works through the stack in pieces of 128. */
void terrain_height_estimate_area(const terrain_t *terrain, int32_t origin_x, int32_t origin_y, int32_t step,
                                  uint32_t count_x, uint32_t count_y, float32_t *heights);
```

**How `terrain_height_estimate_profile` works, step by step:**

1. `cx = x >> 2`, `cy = y >> 2` (floor, `DENSITY_CELL_XY_SHIFT`), `t_x = (float32_t)(x & 3) * 0.25f32`, `t_y = (float32_t)(y & 3) * 0.25f32`.
2. Four lattice columns $(4cx, 4cy)$, $(4cx+4, 4cy)$, $(4cx, 4cy+4)$, $(4cx+4, 4cy+4)$, in that order, through `terrain_density_profile_points` with `count = 4`.
3. Blend height and softness each as `density_write_voxels` does (`run/src/terrain/density/index.c:442-444`): `near = s0 + t_x * (s1 - s0)`, `far = n0 + t_x * (n1 - n0)`, `value = near + t_y * (far - near)`, all in `float32_t`.

At a lattice column, $t_x = t_y = 0$, and $a + 0 \times (b - a) = a$ exactly, so the area function can call the points function directly on lattice columns and get the same bits. The climate only ever asks at lattice columns: cell centres are at $64k + 32$ and the ground samples (below) at $64k + 8 + 16j$, all multiples of 4.

The master plan writes it two ways, `terrain_height_estimate(terrain, x, y)` in 6.1 and `terrain_height_estimate(x, y)` in phase 03's todo. A pure function of the seed needs the terrain, so the three-argument form is the one built.

### Ground as an input, so tests can use synthetic continents

The climate never calls the terrain directly. It calls a **ground function** it was given at `climate_open`:

```c
/* Heights in blocks at count_x by count_y columns, step apart, x fastest. Must be a pure function of its arguments. */
typedef void (*climate_ground_fn_t)(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                    uint32_t count_x, uint32_t count_y, float32_t *heights);
```

`climate_open` passes a small function that calls `terrain_height_estimate_area` with the terrain as context. Tests pass synthetic grounds from `tests/run/climate/fixtures/ground.h` (a straight coast, a round island, a lake inside land, a channel one cell wide, a diagonal-only gap; phase 04 adds continents at given latitudes). That keeps one code path for real and synthetic worlds.

### Constants

In `run/include/climate/cell/index.h` unless said otherwise. Each with its unit and where it comes from.

```c
enum : int32_t {
    CLIMATE_CELL_BLOCKS = 64,           /* blocks on a cell's side: master plan 1 */
    CLIMATE_CELL_SHIFT = 6,
    CLIMATE_TILE_CELLS = 16,            /* cells on a tile's side: master plan 1 */
    CLIMATE_TILE_BLOCKS = 1024,
    CLIMATE_TILE_SHIFT = 10,
    CLIMATE_MARGIN_CELLS = 128,         /* the 8 km margin, 8192 / 64: master plan 6.2 */
    CLIMATE_WINDOW_CELLS = 272,         /* 16 + 2 * 128 on a side */
    CLIMATE_WINDOW_TILES = 17,          /* pages on a side of the window */
    CLIMATE_WARP_BLOCKS = 32,           /* the warp's reach: master plan 6.9 */
    CLIMATE_READ_REACH_BLOCKS = 96,     /* a read's reach past its point: one cell's blend plus the warp, 64 + 32 */
    CLIMATE_GROUND_SAMPLES_SIDE = 4,    /* this phase: z_ground is the mean of 4 by 4 samples, 16 blocks apart */
};
static_assert(CLIMATE_CELL_BLOCKS == 1 << CLIMATE_CELL_SHIFT);
static_assert(CLIMATE_TILE_BLOCKS == CLIMATE_CELL_BLOCKS * CLIMATE_TILE_CELLS);
static_assert(CLIMATE_WINDOW_CELLS == CLIMATE_TILE_CELLS + 2 * CLIMATE_MARGIN_CELLS);
static_assert(CLIMATE_WINDOW_CELLS == CLIMATE_WINDOW_TILES * CLIMATE_TILE_CELLS);

enum : uint32_t {
    CLIMATE_SEA_AREA_MIN_CELLS = 8192u, /* this phase: water this big is sea even inside the window, 33.6 km² */
    CLIMATE_LAKE_LARGE_CELLS = 489u,    /* 2 km² in cells, rounded up: master plan 6.2's "lakes over 2 km²" */
};
static const float32_t CLIMATE_SEA_DISTANCE_MAX = 8192.0f32;  /* blocks: master plan 6.2 */
static const float32_t CLIMATE_KAPPA_SCALE = 1500.0f32;       /* blocks: master plan 6.2 */
static const float32_t CLIMATE_WARP_FREQUENCY = 1.0f32 / 256.0f32; /* this phase: cycles per block, first octave */
```

**Why 4 by 4 ground samples, 16 blocks apart.** `z_ground` is "the mean of terrain_height_estimate over the cell". The cell is 64 blocks. The terrain's 2D features are hills with a 192-block first wave and jagged detail with a 32-block wave and at most 34 blocks of height (`DENSITY_SHAPE_HILLS`, `DENSITY_SHAPE_JAGGED`, `DENSITY_JAGGED_BLOCKS`). Sixteen samples average the jagged detail out well, and cost 4096 estimates a tile instead of 16,384 for 8 by 8. The bench tells whether that's needed; the constant can rise if the budget allows. The sample columns are at offsets 8, 24, 40, 56 inside the cell, all multiples of 4.

**Warp fields.** Two fields named `"climate_cells/warp_x"` and `"climate_cells/warp_y"` (not `"climate/..."`: the terrain's own six fields already use names starting `climate/`, and a field comes from its name and the seed, so a name clash would give the same noise twice). Shape: 2D, `frequency_x = frequency_y = 1/256`, lacunarity 2, persistence 0.5, 2 octaves. Spread with the limit 0.35, the same as `DENSITY_LIMIT_DETAIL`, as the repo's other fields do. They live in the climate's own `terrain_noise_bank_t`, seeded with the world's seed, holding 2 fields, in the climate's region.

### `climate_cell_t`

Exactly the master plan's struct (6.9), with two fields added inside its `_pad0[26]`, which the master plan calls "room to grow". Phase 04 needs them, and they come out of this phase's second distance transform, so they are filled here.

```c
/* One 64 by 64 block cell's climate normals: worked out once from the seed, never saved, never changed by play. */
typedef struct climate_cell_t {
    float32_t lat_deg;           /* 0: φ at the cell's centre */
    float32_t z_ground;          /* 4: mean of terrain_height_estimate over the cell */
    float32_t sea_distance;      /* 8: blocks to the nearest connected sea, at most 8192; 0 in the sea */
    float32_t kappa;             /* 12: continentality, 0 on the coast to 1 deep inland */
    float32_t sea_direction[2];  /* 16: unit vector toward that sea; (0, 0) in the sea or with none in reach */
    float32_t t_mean;            /* 24: °C, the yearly mean (phase 05) */
    float32_t t_swing;           /* 28: °C, A in 6.6, signed by hemisphere (phase 04) */
    float32_t t_lag_days;        /* 32: ℓ in 6.6 (phase 04) */
    float32_t rain_mm_week[5];   /* 36: P0, P1c, P1s, P2c, P2s (phase 05) */
    float32_t wind[6];           /* 56: u0, u1c, u1s, v0, v1c, v1s, blocks a second (phase 04) */
    float32_t pressure[3];       /* 80: Δp_heat as p0, p1c, p1s, hPa (phase 04) */
    float32_t snow_balance_z;    /* 92: height above which snow outlasts summer (phase 05) */
    uint8_t current;             /* 96: CLIMATE_CURRENT_*, of the nearest sea (phase 04) */
    uint8_t current_strength;    /* 97: 0 to 255 (phase 04) */
    uint8_t climate_class;       /* 98: CLIMATE_CLASS_* (phase 05) */
    uint8_t flags;               /* 99: CLIMATE_CELL_SEA, _LAKE here; _MONSOON, _HURRICANE_COAST in phase 05 */
    uint8_t hurricane_days[2];   /* 100: first and last day of hurricane season, 255 if none (phase 05) */
    uint8_t _pad0[2];            /* 102 */
    float32_t land_distance;     /* 104: this phase: blocks to the nearest land, at most 8192; 0 on land */
    float32_t land_direction[2]; /* 108: this phase: unit vector toward that land; (0, 0) on land or with none */
    uint8_t _pad1[12];           /* 116: still room to grow */
} climate_cell_t;
static_assert(sizeof(climate_cell_t) == 128);
static_assert(alignof(climate_cell_t) == 4);
static_assert(offsetof(climate_cell_t, kappa) == 12);
static_assert(offsetof(climate_cell_t, wind) == 56);
static_assert(offsetof(climate_cell_t, current) == 96);
static_assert(offsetof(climate_cell_t, hurricane_days) == 100);
static_assert(offsetof(climate_cell_t, land_distance) == 104);
static_assert(offsetof(climate_cell_t, land_direction) == 108);
/* ... one offsetof line for every field, as the master plan gives them */

enum : uint8_t {
    CLIMATE_CELL_SEA = 1u << 0,
    CLIMATE_CELL_LAKE = 1u << 1,              /* a lake of at least CLIMATE_LAKE_LARGE_CELLS */
    CLIMATE_CELL_MONSOON = 1u << 2,           /* phase 05 */
    CLIMATE_CELL_HURRICANE_COAST = 1u << 3,   /* phase 05 */
    CLIMATE_DAY_NONE = 255u,                  /* hurricane_days with no season */
};
```

The struct is written to and compared as bytes (tests `memcmp` cells, and pin a digest over them), so the builder `memset`s each tile's cells to 0 before filling them. Padding is always 0.

`land_distance` and `land_direction` are not in the master plan's struct. They are the "unit vector pointing to the nearest land" and "within 600 blocks of land" of 6.3, which phase 04 needs for every sea cell, and they cost nothing extra here. They use 12 of the 26 spare bytes, and `land_distance` sits at 104 so it is 4-byte aligned.

### The cache

```c
typedef enum climate_slot_state_t : uint32_t {
    CLIMATE_SLOT_EMPTY = 0,
    CLIMATE_SLOT_CLAIMED = 1,   /* someone is building it; key is set */
    CLIMATE_SLOT_READY = 2,     /* its data is complete and matches key */
    CLIMATE_SLOT_EVICTING = 3,  /* being taken for another key */
} climate_slot_state_t;

/* One slot of a toroidal table. Its own cache line: workers and the sim write different slots at once. */
typedef struct climate_slot_t {
    alignas(MC_CPU_CACHE_LINE_BYTES) _Atomic uint64_t key; /* 0: (uint32_t)tile_x | (uint64_t)(uint32_t)tile_y << 32 */
    _Atomic uint32_t state;                                /* 8: climate_slot_state_t */
    _Atomic uint32_t readers;                              /* 12: reads in progress */
    uint8_t _pad0[MC_CPU_CACHE_LINE_BYTES - 16];           /* 16 */
} climate_slot_t;
static_assert(sizeof(climate_slot_t) == MC_CPU_CACHE_LINE_BYTES);

enum : uint32_t {
    CLIMATE_TILE_SIDE = 64u,                                        /* the cells table: 64 km square */
    CLIMATE_TILE_SLOTS = CLIMATE_TILE_SIDE * CLIMATE_TILE_SIDE,     /* 4096 */
    CLIMATE_MASK_SIDE = 128u,                                       /* the mask table: 128 km square */
    CLIMATE_MASK_SLOTS = CLIMATE_MASK_SIDE * CLIMATE_MASK_SIDE,     /* 16384 */
    CLIMATE_JOBS_IN_FLIGHT_MAX = 4u,                                /* tile jobs the sim keeps queued, leaving 12 workers for columns */
};
```

**Where data lives.** Slot $i$ of the cells table owns cells `borrowed_cells + i * 256`, forever. Slot $i$ of the mask table owns heights `borrowed_mask_heights + i * 256`. Nothing is ever allocated after `climate_init`.

**Memory budget,** all in one `heap_region_t` reserved by `climate_init`:

| Part | Bytes |
|---|---|
| Cells, 4096 tiles × 256 × 128 | 128 MiB |
| Mask heights, 16384 pages × 256 × 4 | 16 MiB |
| Cell slots, 4096 × 64 | 256 KiB |
| Mask slots, 16384 × 64 | 1 MiB |
| Warp fields and bank, through an arena at the region's start | under 4 KiB |
| **Region**, rounded up to `HEAP_REGION_ALIGNMENT` | about 146 MiB of address space |

Only touched pages use real memory (`run/src/heap/` maps with `MADV_HUGEPAGE`). Around the player, tiles in one row of the table are consecutive slots, so 7 tiles of a row touch one 2 MiB huge page: about 16 MiB real for the 49 tiles around spawn, and about 2 huge pages of mask. Against `memory.process_budget_mib: 16384` (`config/machine.yaml:22`) that's under 1%.

**Why 64 and 128.** Tiles the game keeps are within render distance plus 2 km of the player: at the largest render distance, 512 chunks, that's $512 \times 16 + 2048 = 10\,240$ blocks, 10 tiles each way, 21 across, well inside 64. A tile's window reaches 8 tiles further, so mask pages in use reach 18 tiles each way, inside 128. Phase 06 reads the mask out to about 35 km for storm tracks, which also fits in 128.

**The read protocol** (any thread):

1. `atomic_fetch_add(&slot->readers, 1, seq_cst)`.
2. `state = atomic_load(&slot->state, seq_cst)`. If not `READY`, go to 5 with "missing".
3. `key = atomic_load(&slot->key, relaxed)`. If not the wanted key, go to 5 with "missing". (The acquire of `state` in step 2, through `seq_cst`, makes the key and data written before the publisher's release visible.)
4. Copy what's needed out of the slot's data.
5. `atomic_fetch_sub(&slot->readers, 1, release)`.

**Claiming a slot for key $K$** (the sim for tiles, any worker for mask pages):

1. Load `state` (acquire) and `key`.
2. If `READY` and key is $K$: nothing to do.
3. If `CLAIMED` (by someone else): for a tile, nothing to do (it's being built). For a mask page, the worker builds its own copy in its arena and uses that, without waiting. Both copies are the same bytes, since a page is a pure function.
4. If `EMPTY`: compare-exchange `EMPTY → CLAIMED` (seq_cst). On success, store key (relaxed), build, then store `READY` (release).
5. If `READY` with another key, or `CLAIMED`/`READY` left over: only if $K$ is inside the **keep square** (below). Compare-exchange `READY → EVICTING` (seq_cst), then load `readers` (seq_cst). If 0: store key, `EVICTING → CLAIMED`, build, publish `READY`. If not 0: store `READY` back and try in a later step. The old tile is necessarily outside the keep square, because two keys sharing a slot differ by a multiple of the table's side in some axis.
6. If $K$ is outside the keep square: build into scratch and don't cache it. That's for rare far reads (a layer drawn far away, a storm track).

**The keep square** is the table side centred on the player's tile: for cells, $|t_x - p_x| < 32$ and $|t_y - p_y| < 32$; for mask pages, half of 128. The sim stores the player's tile in `keep_tile_x`, `keep_tile_y` (relaxed atomics) each step.

### The Owner: `climate_t`

If phase 02 has already defined a `climate_t`, these fields are added to it and its `static_assert`s are updated. If not, this is the struct:

```c
/* One world's climate cells. One, under app_t. Written by the sim (scheduling), workers (tiles, pages) and readers (the
 * reader counts), so the atomics have a line of their own. */
typedef struct climate_t {
    arena_t arena_tables;                         /* 0: over the region's start: the warp bank's fields; World lifetime */
    job_group_t group;                            /* 64: tile jobs in flight */
    heap_region_t owned_region;                   /* 128: everything below */
    terrain_noise_bank_t warp_bank;               /* 144 */
    const void *borrowed_ground_context;          /* 168: the terrain_t, or a test's synthetic ground */
    climate_ground_fn_t ground;                   /* 176: nullptr while closed */
    const terrain_noise_field_t *borrowed_warp_x; /* 184 */
    const terrain_noise_field_t *borrowed_warp_y; /* 192 */
    climate_slot_t *borrowed_tile_slots;          /* 200: CLIMATE_TILE_SLOTS, in owned_region */
    climate_slot_t *borrowed_mask_slots;          /* 208: CLIMATE_MASK_SLOTS */
    climate_cell_t *borrowed_cells;               /* 216: CLIMATE_TILE_SLOTS * 256 */
    float32_t *borrowed_mask_heights;             /* 224: CLIMATE_MASK_SLOTS * 256 */
    uint64_t seed;                                /* 232 */
    uint8_t open;                                 /* 240 */
    uint8_t _pad0[15];                            /* 241 */
    /* A line of its own: the sim writes the keep square, workers count down. */
    _Atomic int32_t keep_tile_x;                  /* 256 */
    _Atomic int32_t keep_tile_y;                  /* 260 */
    _Atomic uint32_t jobs_in_flight;              /* 264 */
    uint8_t _pad1[MC_CPU_CACHE_LINE_BYTES - 12];  /* 268 */
} climate_t;
static_assert(sizeof(climate_t) == 320);
static_assert(alignof(climate_t) == 64);
```

### Public functions

In `run/include/climate/index.h` (Owner):

```c
/* Process lifetime: reserves the region, all of it untouched. False, having logged why, if the heap says no. */
[[nodiscard]] bool climate_init(climate_t *climate);
void climate_deinit(climate_t *climate);

/* World lifetime: binds a terrain (the ground is terrain_height_estimate_area), seeds the warp bank, empties both tables.
 * No job of a previous world may be running. */
[[nodiscard]] bool climate_open(climate_t *climate, const terrain_t *terrain);
/* The same on any ground, for tests and the bench. */
[[nodiscard]] bool climate_open_ground(climate_t *climate, uint64_t seed, climate_ground_fn_t ground,
                                       const void *borrowed_context);
/* Waits for its jobs (not on a worker), then forgets the world. Safe on a closed climate. */
void climate_close(climate_t *climate, const job_system_t *system);

/* The sim, once a step: moves the keep square to the player and queues up to CLIMATE_JOBS_IN_FLIGHT_MAX tiles not yet
 * built within radius_blocks (plus the read reach), nearest first. Never waits. */
void climate_schedule(climate_t *climate, job_system_t *system, int32_t player_x, int32_t player_y,
                      uint32_t radius_blocks);

/* Whether every tile a read anywhere in this block rectangle (both ends included) touches is built. Any thread. */
bool climate_area_ready(const climate_t *climate, int32_t x_min, int32_t y_min, int32_t x_max, int32_t y_max);

/* Any thread but a worker: builds every tile a read in the rectangle touches, on all workers, and waits. Mask pages first,
 * one job each, then tiles, so no two jobs build one page. progress (may be nullptr) is told done and total after each
 * job. Moves the keep square to the rectangle's middle. */
void climate_prepare_area(climate_t *climate, job_system_t *system, int32_t x_min, int32_t y_min, int32_t x_max,
                          int32_t y_max, void (*progress)(void *context, uint32_t done, uint32_t total),
                          void *progress_context);

/* The cell holding a block, copied out. False if its tile is not built: nothing is written. Any thread. */
bool climate_cell_get(const climate_t *climate, int32_t x, int32_t y, climate_cell_t *out);

/* What this phase's fields blend to at a block: warped, bilinear, latitude exact. False if a tile it needs is not built. */
typedef struct climate_sample_t {
    float32_t lat_deg;          /* 0: exact, from y */
    float32_t z_ground;         /* 4 */
    float32_t sea_distance;     /* 8 */
    float32_t kappa;            /* 12 */
    float32_t sea_direction[2]; /* 16: blended, then made unit again; (0, 0) if it blends to nearly nothing */
    float32_t land_distance;    /* 24 */
    uint8_t flags;              /* 28: the nearest cell's, to the warped point */
    uint8_t _pad0[3];           /* 29 */
} climate_sample_t;
static_assert(sizeof(climate_sample_t) == 32);
bool climate_sample_at(const climate_t *climate, float64_t x, float64_t y, climate_sample_t *out);
```

In `run/include/climate/sea/index.h` (pure, any thread, for tests on synthetic grids):

```c
/* Labels the 4-connected water components of a side by side grid (water[i] != 0), in row-major order of their first
 * cell, and says which are sea by CLIMATE_SEA_AREA_MIN_CELLS and the edge. Out: kind[i] = CLIMATE_SEA_KIND_LAND, _SEA,
 * _LAKE_LARGE or _LAKE_SMALL. queue holds side * side entries; labels side * side. */
void climate_sea_classify(const uint8_t *water, uint32_t side, uint32_t *labels, uint32_t *queue, uint8_t *kind);

/* Exact squared distance, in cells², from every cell to the nearest marked one, and that one's index. CLIMATE_SEA_FAR where
 * none is marked. scratch holds 3 * side + 1 uint32_t and side + 1 float64_t per the README. */
void climate_sea_distance(const uint8_t *marked, uint32_t side, uint32_t *distance2, uint32_t *nearest,
                          climate_sea_scratch_t *scratch);
```

In `run/include/climate/cell/index.h`:

```c
/* Every cell of tile (tile_x, tile_y), into cells (256, south row first, x fastest), from its window. Pure: the same
 * bytes whatever else exists. arena is the worker's Job arena; about 2.3 MiB is taken from it. */
void climate_cell_build_tile(const climate_t *climate, arena_t *arena, int32_t tile_x, int32_t tile_y,
                             const float32_t *window_heights, climate_cell_t *cells);
```

In `run/include/climate/layer/index.h`:

```c
typedef enum climate_layer_t : uint32_t {
    CLIMATE_LAYER_SEA_DISTANCE = 0,
    CLIMATE_LAYER_CONTINENTALITY = 1,
    CLIMATE_LAYER_WATER = 2,       /* sea, large lakes, small lakes, land shaded by z_ground */
    CLIMATE_LAYER_LATITUDE = 3,    /* drawn over another layer */
    CLIMATE_LAYER_COUNT_PHASE_03 = 4,
} climate_layer_t;

/* width by height XRGB8888 pixels, rows pitch bytes apart, pixel (0, 0) at the north-west corner, blocks_per_pixel apart,
 * each pixel the sample at its centre. Grey (0x808080) where a tile is not built. day is for later layers. */
void climate_layer_draw(const climate_t *climate, climate_layer_t layer, int32_t west_x, int32_t north_y,
                        int32_t blocks_per_pixel, uint32_t width, uint32_t height, uint32_t day, uint8_t *pixels,
                        uint32_t pitch);
```

**Layer colours.** Sea distance: black at 0 to white at 8192, linear. Continentality: $\kappa = 0$ is `0x2060C0` (blue), $\kappa = 1$ is `0xA06030` (brown), linear in each channel. Water: sea `0x103070`, large lake `0x3080E0`, small lake `0x70B0F0`, land grey by `z_ground` from `0x406040` at 0 to `0xF0F0F0` at 454. Latitude: a 1-pixel line where $\varphi$ crosses 0° (red `0xFF2020`), ±23.5° (orange `0xFF9020`), ±66.5° (cyan `0x20E0FF`), drawn over whichever layer is under it. Choosing the line's row: pixel rows whose $y$ range contains a $y$ where $\varphi$ equals the value, for every repeat of the 153,191-block pattern in view.

### What a tile job does, in order

`climate_tile_job` (in `run/src/climate/index.c`), on a worker, for tile $(t_x, t_y)$ whose slot the sim has claimed:

1. **Window.** Take `CLIMATE_WINDOW_CELLS²` floats (289 KiB) from the Job arena. For each of the 17 by 17 pages $(t_x - 8 \ldots t_x + 8,\ t_y - 8 \ldots t_y + 8)$, in row-major order: read it from the mask table under the read protocol and copy it into place; if missing, claim and build it (step 1a), or build a private copy if someone else holds the claim. Window cell $(i, j)$ is at index `j * 272 + i`, row 0 at the south.
   - 1a. **Building a page:** the ground function at `origin = tile * 1024 + 32`, `step = 64`, 16 by 16. 256 estimates.
2. **Water.** `water[i] = heights[i] <= 0`.
3. **Classify.** `climate_sea_classify`: labels, area per label, edge contact, kind per cell.
4. **Distance to sea.** `climate_sea_distance` with `marked = (kind == SEA)`.
5. **Distance to land.** `climate_sea_distance` with `marked` = land or small lake (small lakes count as land, as the sea rule says).
6. **Ground samples.** The ground function at `origin = tile * 1024 + 8`, `step = 16`, 64 by 64: 4096 heights.
7. **Cells.** For each cell, `iy` from 0 to 15 and `ix` from 0 to 15 in that order, window cell $(128 + ix, 128 + iy)$:
   - `lat_deg` = phase 02's latitude at $y = t_y \cdot 1024 + 64\,iy + 32$.
   - `z_ground` = the 16 samples of step 6 for this cell, summed in a fixed order (row by row) in `float32_t`, divided by 16.
   - If sea: `flags = CLIMATE_CELL_SEA`, `sea_distance = 0`, `kappa = 0`, `sea_direction = (0, 0)`. `land_distance` and `land_direction` from step 5.
   - Else: `sea_distance` and `sea_direction` from step 4, `kappa` $= 1 - e^{-d_s/1500}$ (`expf`). If land or a small lake: `land_distance = 0`, `land_direction = (0, 0)`. A large lake is neither: it gets both distances from steps 4 and 5, and `flags |= CLIMATE_CELL_LAKE`.
   - `hurricane_days = {255, 255}`. Everything else stays 0 from the `memset`.
8. **Publish.** Store `READY` with release. Decrement `jobs_in_flight` (release).

Each tile is built by exactly one job, in a fixed order, so floating-point sums always add in the same order: the same bytes on 1 worker or 16 (master plan 12).

**Job arena use:** heights 289 KiB, water 72 KiB, labels and queue 578 KiB, kind 72 KiB, two distance and two nearest arrays 1156 KiB, distance-transform scratch 8 KiB, ground samples 16 KiB: about 2.2 MiB of the 32 MiB. Nothing large goes on the stack (`-Wframe-larger-than`).

### Threads and ownership

| Thread | What it does here |
|---|---|
| main | `climate_init` and `climate_deinit` in `app_init` and `app_deinit`. `climate_open`, `climate_prepare_area` and `climate_close` while a world opens and closes, with no game threads running |
| sim | `climate_schedule` each step. The column gate in `game_columns_request` |
| workers | Tile jobs. Mask pages inside them |
| anyone | `climate_cell_get`, `climate_sample_at`, `climate_area_ready`, `climate_layer_draw` |

`climate_t` lives in `app_t`, next to `terrain_t` (`run/include/main/index.h:31-58`, `static_assert(sizeof(app_t) == 16000)`, which changes and is updated). `game_t` borrows it as `borrowed_climate`, set before a world opens like `borrowed_mob`.

### Scheduling and the column gate

**Radius.** The master plan: "render distance plus 2 km". So `radius_blocks = render_distance * 16 + 2048`, and tiles are wanted wherever a read within that radius could reach: radius plus `CLIMATE_READ_REACH_BLOCKS` (96).

**Order.** Wanted tiles not `READY` and not `CLAIMED` are sorted by the squared distance from the player's block to the tile's centre, ties by $t_y$ then $t_x$, and the first ones are claimed and submitted until `jobs_in_flight` reaches 4. At render distance 32 that's at most $\lceil (2 \times 2656) / 1024 \rceil + 1 = 7$ tiles a side, 49 candidates, so sorting them each step is cheap. If `job_try_submit` fails (the shared queue is full), the claim is undone (`CLAIMED → EMPTY`) and tried next step.

**Gate.** "Chunk generation waiting for its tile" (master plan, phase 03 todo). A job may not wait (`run/src/job/README.md`), so the waiting happens before submitting. In `game_columns_request` (`run/src/game/index.c:794-818`): after `world_map_stream_take` gives a request, if `game->borrowed_climate != nullptr` and `climate_area_ready` is false for the column's 16 by 16 blocks, the request is cancelled with `world_map_stream_cancel` and the loop returns. The map offers the same nearest column again next step. Tiles are worked out nearest the player first, so the nearest column's tiles are the first to come.

Nothing in the terrain reads the climate, so the gate changes no block. It changes only when a column arrives. With no climate (`borrowed_climate == nullptr`, as every existing test leaves it), nothing changes at all.

### Precompute at world creation

In `game_open` (`run/src/game/index.c:441`), after the spawn place is known (`game_spawn_find`, line 484) and before the threads start: if `borrowed_climate` is set, `climate_prepare_area` over spawn $\pm$ (`render_distance * 16 + 2048`). Progress goes to two new `game_t` fields, `open_progress_done` and `open_progress_total` (`uint32_t`, from `_pad1[44]` at offset 916 on main's line), and each step calls `game_open_alive_tell` (`run/src/game/index.c:435`), which the session already uses to draw the loading screen. `ui_menu_loading_frame` (`run/src/ui/menu/index.c:388`) draws a third line, "Working out the climate: 37%", from a new `uint8_t loading_percent` in `ui_menu_t`'s `_pad0[5]` (at offset 299). The session's alive callback (`run/src/game/session/index.c:499`) copies the percent across before drawing.

Phases 01 and 02 may have used some of that padding. Re-read both structs, take free bytes, and keep every `static_assert`.

At render distance 32: $\lceil 2 \times (2560 + 96) / 1024 \rceil = 6$, so 6 or 7 tiles a side, at most 49 tiles and $(7 + 16)^2 = 529$ mask pages.

### Edge cases

| Case | What happens |
|---|---|
| Negative coordinates | Floors by `>>` everywhere, slots by `& (side - 1)`. Tested at $x, y = -1, -1024, -1025$, and tiles straddling 0 |
| A read near a tile's border | The four cells may be in up to four tiles. `climate_sample_at` reads each under the protocol. If any is missing, it returns false and writes nothing |
| Tile borders and the sea rule | Two neighbouring tiles decide sea or lake in their own windows. They can disagree only about a component that reaches one window's edge and not the other's, and is under 33.6 km². That would show as a step in `sea_distance` at the tile border. The 8 km margin makes it rare. The layer pictures show it if it happens (Risks) |
| Latitude repeat and poles | Latitude comes from phase 02's function at the exact $y$. Nothing else in this phase depends on latitude, and the tables have no edge at the poles or the repeat |
| All sea | Every component touches the edge: all sea, all distances 0, $\kappa = 0$, `land_distance` 8192 |
| No sea in the window | `sea_distance = 8192`, `sea_direction = (0, 0)`, $\kappa = 0.99575$ |
| Coordinates near $\pm 2^{31}$ | Tiles go to $\pm 2^{21}$. Window pages $\pm 8$ stay in range. Block coordinates are formed in `int64_t` before narrowing, as `terrain_density_profile_area` does (`run/src/terrain/density/index.c:285-287`) |
| Ground below sea level and not sea | The terrain fills all ground below 0 with water today (`density_block_of`, `run/src/terrain/density/index.c:364-375`), so this is a lake cell. Phase 05's basin heat (6.6) waits for a geology plan |
| A world closed while tiles build | `climate_close` waits on `group` first |

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/climate/` and every new part below it: `mask/`, `sea/`, `cell/`, `cache/`, `layer/`
- `run/{src,include}/terrain/density/index.{c,h}`: only `terrain_density_profile_points`, added
- `run/{src,include}/terrain/index.{c,h}`, `run/src/terrain/README.md`: only the three estimate functions, added
- `run/src/terrain/density/README.md`: a line for the new function
- `tests/run/climate/` (all of it, with `fixtures/`)
- `tests/run/terrain/density/index.c`: new tests only; no existing test or pin changes
- `bench/climate/`

**Wiring outside them, each kept as small as possible:**

- `run/include/main/index.h`, `run/src/main/index.c`: `climate_t climate` in `app_t`, its `static_assert`, `climate_init` after `terrain_init` and `climate_deinit` before `terrain_deinit`, on every failure path of `app_init` (lines 482-587 call `terrain_deinit` on each)
- `run/include/game/index.h`, `run/src/game/index.c`: `borrowed_climate`, `open_progress_done`, `open_progress_total` from padding; `climate_schedule` in the sim step; the gate in `game_columns_request`; `climate_prepare_area` in `game_open`
- `run/src/game/session/index.c`: `climate_close` and `climate_open` around `terrain_init` in `game_session_world_open` (line 153); the percent in the alive callback
- `run/include/ui/menu/index.h`, `run/src/ui/menu/index.c`: `loading_percent` and the third line
- `compile/src/standards_lint/rules_include/index.c`: `{"climate", 8, 0}`, if phase 02 didn't add it
- `plans/REPO_STRUCTURE.md`: rows for `climate` and its parts (section 6), the tests (section 10), `bench/climate`, and section 11

## Steps

Build order. Each step builds and passes its own tests before the next starts.

1. **Read phase 02's result.** Find its latitude function, its declination function, and whether `run/src/climate/` and `climate_t` exist and where. This file calls latitude `climate_latitude_deg`; use whatever phase 02 named it. Check `compile/src/standards_lint/rules_include/index.c` has `climate`. *Why: everything below calls latitude, and two `climate_t`s would not build.*
2. **Add `terrain_density_profile_points`** in `run/src/terrain/density/index.c`, a loop over `density_profile_batch`. *Why: the estimate needs single columns anywhere, through the very code the boxes use, so its bits match.*
3. **Add `terrain_height_estimate_profile`, `terrain_height_estimate` and `terrain_height_estimate_area`** in `run/src/terrain/index.c`, with their header lines and README lines. *Why: master plan 6.1. The climate reads ground through this and nothing else.*
4. **Add the two estimate tests** to `tests/run/terrain/density/index.c`, and run every `run_terrain_.*` test: every pin must hold. *Why: proves the estimate agrees with the real ground, and that nothing in the terrain moved.*
5. **Create `run/src/climate/sea/`:** `climate_sea_classify` (BFS from an arena queue, 4-connected, row-major discovery) and `climate_sea_distance` (Felzenszwalb and Huttenlocher, rows then columns, recording nearest indices, ties to the lower index). With `tests/run/climate/sea/` and `tests/run/climate/fixtures/ground.h`. *Why: the two algorithms are the risky maths, and are tested alone on synthetic grids before anything uses them.*
6. **Create `run/src/climate/cache/`:** slots, the read protocol, claim, publish, evict, the keep square. With `tests/run/climate/cache/`, including real threads. *Why: the concurrency is the other risky part, and is tested alone.*
7. **Create `run/src/climate/mask/`:** a page from the ground function; a window from pages, building or privately copying missing ones. With `tests/run/climate/mask/`.
8. **Create `run/src/climate/cell/`:** `climate_cell_t` with its asserts and flags; `climate_cell_build_tile` (job steps 2 to 7); the warped bilinear read used by `climate_sample_at`. *Why: this is where the master plan's numbers turn into cells.*
9. **Extend the Owner `run/src/climate/index.c`:** `climate_init`, `climate_deinit`, `climate_open`, `climate_open_ground`, `climate_close`, the tile job, `climate_schedule`, `climate_area_ready`, `climate_prepare_area`, `climate_cell_get`, `climate_sample_at`. With `tests/run/climate/cell/` (it tests through the Owner).
10. **Create `run/src/climate/layer/`** with the four layers, and `tests/run/climate/layer/`.
11. **Create `bench/climate/`:** the tile timings and `--pictures <directory> <seed>`, which writes the four layers as PNGs (below, Gate).
12. **Wire it in:** `app_t`, `app_init`, `app_deinit`; the session; `game_t`, the scheduler, the gate, the precompute; the loading line. Run the whole debug suite. *Why: last, so every part is proven before the game depends on it.*
13. **Update `plans/REPO_STRUCTURE.md`** and every new README.
14. **Run the bench on release**, record its numbers in `bench/climate/README.md` and here.
15. **Draw the pictures** for the gate, and tick the todo as each item lands.

## Todo

- [x] Phase 02's names found; `climate` in the lint table at rank 8; `climate_t` shape settled (Issues 1, 4)
- [x] `terrain_density_profile_points` in `run/src/terrain/density/`
- [x] `terrain_height_estimate_profile`, `terrain_height_estimate`, `terrain_height_estimate_area` in `run/src/terrain/`, public, with a test that it agrees with real ground to within the density's softness over 10,000 columns (share 1.0000, worst 0.91 blocks past the softness; hard bound held on all 10,000)
- [x] Every `run_terrain_.*` pin unchanged (all 10 `run_terrain_*` tests passed in the full debug run)
- [x] `run/src/climate/sea/`: the flood fill from open ocean (the window rule), and the distance transform, both ways
- [x] `run/src/climate/cache/`: the two toroidal tables, the reader count, claim, publish and evict
- [x] `run/src/climate/mask/`: the coarse land-mask pages in a sparse table, and the window
- [x] `run/src/climate/cell/`: `climate_cell_t` (128 bytes), the tile build with its 8 km margin and 8192 cap, $\kappa$, and the warped bilinear blend
- [x] The tile cache: background jobs ahead of chunk generation, nearest the player first; chunk generation waits for its tile (`climate_schedule` in `game_sim_step`, the gate in `game_columns_request`; tested by `a_column_is_not_asked_for_before_its_tiles_are_built` and by the `--menu` tests of the suite, which now run with it)
- [ ] Precomputed over render distance plus 2 km when a world is created, behind a progress bar (the precompute is done, in `game_open`; the bar is not seen live: Issue 9)
- [x] Map layers: sea distance, continentality, sea and lakes, latitude lines, in `run/src/climate/layer/`
- [x] Tests: distance against a brute-force search on synthetic coasts; a tile's cells are the same whichever neighbours were worked out first; 1 and 16 workers give the same bytes
- [x] Bench: one tile on one worker, target under 100 ms, in `bench/climate/` (slowest cold tile 30.7 ms, release)
- [x] A `README.md` in every new source directory, exactly one `index.*` in each (`standards_lint` clean)
- [x] `plans/REPO_STRUCTURE.md` updated
- [x] The whole debug suite passes; `standards_lint` and `clang_format` pass (full run: 129 of 130; `run_types` failed on the region count (Issue 18), was fixed and rerun; after the sea-rule change, the climate tests and the ten `--menu`-driven tests were rerun and pass)
- [ ] Gate pictures drawn and shown to Ahmed (drawn: `/tmp/mc_c_climate/` and `/tmp/mc_c_climate_<seed>/`; not yet shown)

## Tests

Names in the repo's sentence style. "Fails if" says what a wrong implementation looks like.

### `run_terrain_density` (two new tests)

- **`the_height_estimate_agrees_with_the_real_ground_within_the_softness`.** 10,000 columns: 4 seeds from `DENSITY_SEEDS` (`0x1234567890ABCDEF`, `1`, `2`, `0x4D494E4543524146`), 50 by 50 columns each, 157 blocks apart (odd, so most columns are between lattice points), centred on 0. For each: `terrain_density_fill_box` on a 1 by 1 by 751 box from `MC_WORLD_Z_MIN`; $z_{\text{top}}$ is the highest `BLOCK_ID_STONE`. The four lattice corners' profiles give $h_i$, $s_i$. **Hard check, every column:** $\min_i(h_i - s_i) - 8 \le z_{\text{top}} \le \max_i(h_i + s_i) + 8$ (the bound in Background). **Share check:** at least 95% of columns have $|z_{\text{top}} + 1 - h_{\text{est}}| \le s_{\text{est}} + 2$. Prints the share, the worst excess and where. Fails if the blend's order or fractions are wrong (the share drops), or if the estimate reads the wrong lattice column for negative coordinates (the hard check fails west and south of 0).
- **`the_height_estimate_is_the_profile_on_the_lattice`.** 4096 lattice columns (multiples of 4, including negative ones): `terrain_height_estimate` equals `terrain_density_profile_area`'s height bit for bit, and `terrain_height_estimate_area` equals both. Fails if the area version blends where it shouldn't, or the single version takes a different batch path.

### `run_climate_sea`

- **`components_are_labelled_as_the_worked_example_says`.** The 6 by 6 grid from Background: three components, A with 12 cells touching the edge, B with 3 touching, C with 4 not touching; labels in row-major order of first cell. Fails on 8-connectivity (B and C merge) or a different label order.
- **`water_touching_the_window_edge_is_sea`** and **`a_big_enclosed_water_is_sea_and_a_small_one_a_lake`.** On a 272 grid: a band along the west edge: sea. An enclosed square of $91 \times 91 = 8281$ cells: sea (over 8192). One of $90 \times 90 = 8100$: a large lake. One of $22 \times 22 = 484$: a small lake (under 489). One of $23 \times 22 = 506$: large. Fails if the thresholds are off by one or compare the wrong way.
- **`a_channel_one_cell_wide_joins_a_lake_to_the_sea_and_a_diagonal_does_not`.** Fails on 8-connectivity.
- **`distance_matches_a_brute_force_search_on_synthetic_coasts`.** For each of: a straight north-south coast, a diagonal coast, a round island of radius 40 cells, an L-shaped bay, 200 random sea cells, a checkerboard, all sea, no sea. On 64 by 64 grids, brute force over every pair; on the full 272 grid, brute force over the list of marked cells (the random and island cases). Every `distance2` equal exactly. Every `nearest` is at exactly that squared distance. Fails on any envelope mistake, which shows as a wrong distance in some row.
- **`the_worked_example_row_is_exact`.** The 8-cell row with marks at 2 and 6 gives `4 1 0 1 4 1 0 1` and nearest `2 2 2 2 2 6 6 6` (the tie at 4 goes to 2).
- **`no_marked_cell_gives_far_everywhere`.** All `CLIMATE_SEA_FAR`, no crash, no read past the grid (guard words around every array).

### `run_climate_mask`

- **`a_page_holds_the_ground_at_each_cell_centre`.** With a synthetic ground $h(x, y) = x - 3y$: page $(2, -3)$'s cell $(i, j)$ equals $2048 + 32 + 64i - 3(-3072 + 32 + 64j)$ exactly.
- **`a_page_from_the_real_terrain_is_the_height_estimate`.** Every value equals `terrain_height_estimate` at the centre, bit for bit.
- **`a_window_is_its_seventeen_by_seventeen_pages_in_place`.** Window cell $(i, j)$ equals the ground at $x = (t_x - 8) \cdot 1024 + 64i + 32$.
- **`a_page_claimed_by_another_is_copied_privately_and_matches`.** Claim a page by hand, build the window: the window has the right values and the table still shows the claim.

### `run_climate_cache`

- **`slots_follow_the_coordinates_modulo_the_side`.** Tile $(-1, 70)$ goes to slot 447; $(63, 6)$ too; $(0, 0)$ to slot 0; $(-64, -64)$ to slot 0.
- **`a_tile_outside_the_keep_square_is_never_cached`** and **`a_far_tile_is_replaced_by_a_near_one_in_its_slot`.**
- **`a_reader_holding_a_slot_stops_its_eviction`.** Hold a read; an eviction attempt fails and leaves `READY` and the data; release; it succeeds.
- **`readers_and_evictors_on_real_threads_never_see_torn_tiles`.** 8 reader threads and 1 replacing thread swap two tiles in one slot 100,000 times; each tile's cells are filled with a pattern from its key; every successful read sees one key's pattern whole. Run also under the `tsan` preset by hand. Fails if any memory order is too weak.

### `run_climate_cell`

- **`kappa_and_distances_are_the_worked_examples`.** Synthetic ground in cell indices around tile $(0, 0)$: sea ($h = -50$) where cell $x \le 0$ and cell $y \le -1$, land ($h = 20$) everywhere else. The sea reaches the window's west and south edges, so it is sea. The land cell $(3, 3)$ has its nearest sea centre at $(0, -1)$: 3 cells west and 4 south, $D = 25$ (every other sea cell gives $(3 - x)^2 + (3 - y)^2 > 25$), so $d_s = 288$, direction $(-0.6, -0.8)$, $\kappa = 0.1747 \pm 10^{-4}$. The land cell $(0, 0)$ is next to the sea: $D = 1$, $d_s = 32$, $\kappa = 0.0211$. A ground with no sea at all: 8192 and $0.99575$. Fails if the half cell is missed (it gives 320 and 0.1921).
- **`a_straight_coast_gives_distance_rising_by_sixty_four_a_cell`.** Coast along $x = 0$, sea west: land cells' $d_s$ are 32, 96, 160, ... and directions $(-1, 0)$; sea cells' `land_distance` is the same mirrored with direction $(+1, 0)$.
- **`flags_mark_sea_large_lakes_and_nothing_else`.** A synthetic world with a sea, a 3 km² lake and a 1 km² lake.
- **`z_ground_is_the_mean_of_sixteen_samples`.** Ground $h = x$: a cell from 0 to 63 has samples at 8, 24, 40, 56, so `z_ground` = 32 exactly.
- **`latitude_is_phase_two_s_at_each_cell_centre`.** And `climate_sample_at` gives the exact latitude at any $y$, never a blend: at $y = 38\,298$ (the pole) it's $90.0$, though no cell centre is there.
- **`the_blend_is_the_worked_example_with_the_warp_turned_off`.** A test hook sets the warp to 0: $\kappa$ at $(48, 80)$ from cells 0.10, 0.30, 0.20, 0.60 is $0.2625 \pm 10^{-6}$.
- **`the_warp_moves_reads_by_at_most_thirty_two_blocks`.** 100,000 points: the warped point is within 32 blocks in each axis, and somewhere over 20 (so it is really on).
- **`a_tile_s_cells_are_the_same_whichever_neighbours_were_worked_out_first`.** Real seed: tile $(0, 0)$ alone in a fresh climate; then a fresh climate builds all 8 neighbours, then the 17 by 17 window's pages in reverse order, then $(0, 0)$; then a third with its window's pages prebuilt by hand. `memcmp` of the 256 cells: equal. Fails if anything leaks from cache state into a tile.
- **`one_and_sixteen_workers_give_the_same_bytes`.** `climate_prepare_area` over 8 by 8 tiles around 0 with 1 worker (`job_init_count`), and again with 16, each in a fresh climate. `memcmp` of all 16,384 cells: equal. Then a **pin**: an FNV-1a digest over those cells for seed `0x1234567890ABCDEF`, as `run_terrain_density` pins its blocks (`tests/run/terrain/density/index.c:52-62`), with a comment saying any change to the estimate, the sea rule, the transform, the ground samples or $\kappa$ moves it on purpose.
- **`negative_coordinates_find_the_right_cell`.** $x = -1$ is cell 15 of tile $-1$; `climate_cell_get(-1, -1)` returns the cell centred at $(-32, -32)$.
- **`a_read_is_false_until_its_tiles_are_built`.** And writes nothing.

### `run_climate_layer`

- **`each_layer_is_the_colour_the_readme_says`.** On a synthetic straight coast: known pixels by hand (sea distance 0 is black, 8192 white; $\kappa = 0$ is `0x2060C0`).
- **`latitude_lines_land_on_the_rows_of_their_latitudes`.** At 16 blocks a pixel over $y = -40\,000 \ldots 40\,000$: rows containing $y = 0$, $\pm 10\,000$, $\pm 28\,298$ coloured, nothing else.
- **`unbuilt_tiles_are_grey`.**
- **`a_layer_picture_is_held_to_a_hash`.** Each layer at 512 by 512 pixels on the real seed, hashed; the hashes are set after Ahmed has looked at the pictures, as `run_ui_menu` does ("pictures held to hashes set after looking at them", `plans/REPO_STRUCTURE.md` section 10).

### The game wiring

The existing suite must pass unchanged: no existing test sets a climate, so the gate is off in all of them. One new test in `tests/run/climate/cell/`: **`a_column_is_not_asked_for_before_its_tiles_are_built`**, driving `climate_area_ready` and a `world_map_t` the way `game_columns_request` does.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_climate_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset tsan
    tools/test-or-build.sh timeout 600 ctest --preset tsan -R '^run_climate_cache$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^run_climate_cell$' --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate --pictures /tmp/mc_c_climate 2198175988018515243
    pgrep -a minecraft_c    # must print nothing

The release run of `run_climate_cell` must print the same digest as debug: `-O3 -flto` must move no bit, as plan 001 checked for terrain.

**The bench** (`bench/climate/index.c`), with its README stating these:

- **Cold tile, 1 worker:** a fresh climate each time, so the tile builds all 289 pages of its window. 9 tiles in different places, best of 3 each. **Exit 1 if any is over 100 ms.**
- **Warm tile, 1 worker:** the window's pages already built, as when walking. Reported.
- **Spawn precompute, 16 workers:** `climate_prepare_area` over render distance 32 plus 2 km. Reported, with the tile count.
- Prints ms per tile, estimates per second, and the share of time in the ground function.

## Gate

Ahmed looks at pictures of the four layers for seed `2198175988018515243` and two others, 16 km by 16 km around spawn, and says whether they look right: seas where the terrain has seas, lakes marked inland, distance and continentality rising smoothly away from coasts with no steps at tile borders, and the latitude lines where they should be.

**The map tool.** The master plan wants every phase's pictures from the headless map tool of 003 phase 8 stage 1, `bench/world_map/`. **On 2026-11-04 it does not exist** (`ls bench/world_map` fails). So:

1. If `bench/world_map/` exists when this phase is built, add the four layers to it through `climate_layer_draw`, and use it for the gate, with its own command (003 phase 8's verification shows `build/release/bin/bench_world_map /tmp/mc_c_maps 2198175988018515243`).
2. If not, `bench_climate --pictures <directory> <seed>` writes `sea_distance.png`, `continentality.png`, `water.png` and `latitude.png` (latitude drawn over water), 1024 by 1024 pixels at 16 blocks a pixel, with `display_png_encode` (`run/include/display/headless/png/index.h`) and the same `climate_layer_draw`. It is not a second map tool: it draws no terrain, only climate layers. When `bench/world_map/` lands, it calls the same layer code.

Either way, Ahmed opens the PNGs from `/tmp/mc_c_climate/` (or the map tool's directory). The layer hashes in `run_climate_layer` are set only after he says yes.

## Risks and open questions

| Risk or question | Default |
|---|---|
| **The cold tile may miss 100 ms.** It needs about 74,000 mask estimates plus 4,096 ground samples. The cost of one estimate isn't measured yet | Measure first. If over: precompute builds pages before tiles (already planned), and the sim also schedules page jobs ahead. Record the numbers here. If still over, ground samples can drop to 2 by 2, which Ahmed decides |
| Tile-border steps in `sea_distance` where two windows disagree on sea or lake | Look for them in the pictures. If seen, add a second condition to the edge rule: a component touching the window's edge counts as sea only if it also has at least 489 cells inside the window |
| The column gate slows streaming at the start and when flying fast, and nothing in the terrain reads the climate yet | Keep it: phases 07 and 09 assume every generated column has its cells. The precompute covers spawn, and at walking speed tiles are 2 km ahead (about 8 minutes). If flying outruns them, the gate shows as late columns, and Ahmed decides whether to raise `CLIMATE_JOBS_IN_FLIGHT_MAX` |
| `run/src/terrain/hydrology/` has its own idea of a lake (low ground with continentalness over $-0.04$, `run/src/terrain/hydrology/index.c:176-177`), and the climate's is by connection. They will not always agree | Both stay. The climate's is what the climate means by "large water bodies". The Atlas may later show both |
| Phase 02 names may differ from this file's | Use phase 02's, and note the names here |
| Phase 01 renames `run/src/terrain/climate/` to `run/src/terrain/fields/`, so `terrain_climate_area_t` may have a new name | This phase's terrain functions take plain integers, so they don't depend on that type's name |
| The added `land_distance` and `land_direction` | Inside the master plan's spare bytes. If Ahmed prefers them elsewhere, phase 04 can recompute them from the window instead |

## Results, 2026-11-06

Verification as run, each through `tools/test-or-build.sh`:

- Debug build: clean. `run_terrain_*`: all pass (the agreement test: 10,000 columns, 0 outside the bound, share 1.0000). `run_climate_*`: all 7 pass. `standards_lint` and `clang_format`: pass.
- Full debug suite (1752 s): 129 of 130; `run_types` (Issue 18) fixed and passes. Not rerun whole after Issue 17's change; what was rerun after it: all `run_climate*`, `run_ui_menu`, `run_save_world`, `run_save_state_game`, `run_game_census`, `run_game_sun`, `run_settings`, `run_settings_session`, `run_playthrough`, `run_failure`, `run_types`: all pass.
- tsan: `run_climate_cache` and `run_climate_cell` pass with no report (Issue 19).
- Release build, LTO link with `-Wframe-larger-than=16384`: clean. `run_climate_cell` release: digest `0x61708389BBF3789B`, the same as debug.
- `bench_climate` release: exit 0; cold tile 28.8 to 30.7 ms, warm 5.65 ms, spawn precompute 23.8 ms on 16 workers (`bench/climate/README.md`).
- `bench_climate --pictures`: `/tmp/mc_c_climate/` (seed 2198175988018515243), `/tmp/mc_c_climate_1311768467294899695/`, `/tmp/mc_c_climate_5569200986553504070/`.
- `pgrep -af "$PWD/build"`: nothing of this worktree left running.

## Commits

Appended as they land:

- `360b711` Add the height estimate and the climate cells of plan 004 phase 03: land mask pages, sea and lakes by window, distance transforms, continentality, the tile cache and its jobs, map layers, the bench and the game wiring.
- `af67e59` Make a water body sea when it reaches the sea floor rather than the window edge, so climate tiles agree at their borders, and record the climate bench.
- `1de7fc0` List the climate cells of plan 004 phase 03, their tests and bench in the repository structure.
- `e61f5c9` Record plan 004 phase 03 in its phase file: todos, results and the issues found.

## Issues found

All 2026-11-06, while building the phase.

1. **Phase 02's names.** Latitude is `climate_latitude_deg(float64_t y)`, as this file says. `run/src/climate/` existed with `sun/`; no `climate_t` existed; `{"climate", 8, 0}` was already in the lint table. Nothing to change.
2. **The agreement test's box was too short.** Expected: a 1 by 1 by 751 box from `MC_WORLD_Z_MIN` holds the top stone. Actual: 751 layers reach only z = 500, and since 003 phase 11 the ground reaches 1190. Done: the box runs to `TERRAIN_DENSITY_NEVER_SOLID_FROM` (1475 layers). Result: 10,000 columns, 0 outside the hard bound, share within softness + 2 = 1.0000, worst 0.91 past the softness at (-1413, -2355).
3. **The 6 by 6 worked example's component A has 11 cells, not 12.** Counting the file's own picture: column 0 (6) plus (1,0), (2,0), (1,1), (1,4), (1,5). B 3 and C 4 are right; 11 + 3 + 4 = 18 water cells. The test checks 11.
4. **`climate_t` (320 bytes) cannot sit in `app_t`.** main's frame was at the 16 KiB limit. Done: `climate_t` in `app_t` is a 64-byte handle (the region and a pointer); the plan's struct is `climate_state_t` at the region's start. `app_t` went from 16064 to 16128 bytes, and main's frame then measured 16448 at -O0 (`-Wframe-larger-than=16384` failed). Done: main's body after argument parsing moved to `[[gnu::noinline]] main_with_app`; `app_t` is still declared in `main`. The limit is unchanged.
5. **`game_t` padding.** `_pad1[44]` at 916 named here was used by phases 01 and 02. Done: `open_percent` (one `uint8_t`, 0 to 100, in place of done and total) at 879 from `_pad0[1]`, and `borrowed_climate` at 1080 from `_pad2[8]`. `sizeof(game_t)` stays 1600. `ui_menu_t.loading_percent` is at 299 as planned.
6. **Small lakes get a flag.** The water layer needs to tell small lakes from land, and the cell had no way to. Done: `CLIMATE_CELL_LAKE_SMALL = 1 << 4`, maps only; small lakes stay land for every climate purpose. The test is `flags_mark_sea_large_lakes_and_small_lakes_and_nothing_else`.
7. **Distance-transform scratch** is `5 * side + 1` words, not `3 * side + 1`: the sweep's input, output, arg-min, parabolas and a copy of one column of the row pass's nearest x. In `run/src/climate/sea/README.md`.
8. **`bench/world_map/` exists now**, so the gate's first option was open. Not taken: the Verification section's command is `bench_climate --pictures`, which this phase built. Adding the four layers to `bench_world_map` through `climate_layer_draw` is left for phase 09 or a follow-up.
9. **The progress bar is not seen while it fills.** Expected: the loading screen shows "Working out the climate: N%". Actual: under KMS (and in a window) the loading screen is drawn once before the frame that opens the world, and nothing presents a frame from inside `game_open`, so the line is never on screen. What is built: `game_open` calls `climate_prepare_area` with a progress callback that sets `game_t.open_percent` and calls `game_open_alive_tell`; the session's alive callback copies it into `ui_menu_t.loading_percent`; `ui_menu_loading_frame` draws the line when the percent is above 0; the log records the time. Presenting a frame from inside `game_open` needs display work outside this phase. The todo box is left unticked.
10. **Who closes the climate.** The session opens it over the terrain, and `game_close` closes it after waiting for the jobs the sim queued, as it does for column jobs. The session also closes it before `terrain_deinit` and on each failed open, so `climate_deinit` (which asserts it is closed) is safe on every path.
11. **Added `climate_prepare_tile`**, which is not in the plan's list: one tile in one job, its window's pages built inside that job. The bench's cold and warm tiles and several tests need it.
12. **Radius source.** The sim reads `world_map_t.view_radius` (the chunks the map streams, so the chunks the gate holds back) × 16 + 2048, not `game_config_t.render_distance`, because `config` is main's to change.
13. **Climate only for menu worlds.** `game_climate_set` is called only for `--menu` runs. Test worlds (`--test-world`, the headless pictures) run as before, so their pinned pictures cannot move.
14. **The cache race test starved its evictor.** First run: 224 s. Eight readers back to back nearly always hold the slot, and by design the protocol gives an evictor no turn (it tries again in a later step). Done: readers pause about 1024 loads between reads, as real readers do. Now 0.63 s: 100,000 swaps, 1,228,254 whole reads, 0 torn.
15. **The gate test drives no `world_map_t`.** `game_columns_request` is static. The test drives `climate_area_ready` and `climate_schedule` the way that function does: the player's tile is queued first, at most 4 are in flight, and the column's gate opens. The real gate runs in every `--menu` test of the suite.
16. **With the planned window rule, almost no water was sea.** Seed `0x1234567890ABCDEF`, 8 by 8 tiles round 0: 59 sea cells and 3361 large-lake cells of 16,384. The 16 km pictures for seeds 2198175988018515243, 1311768467294899695 and 5569200986553504070 gave 0.0%, 0.4% and 0.0% sea, and 22.2%, 18.8% and 22.1% large lake. This terrain's seas are enclosed basins of a few km to about 25 km², under 33.6 km², so only a body that happened to reach a window's edge was sea.
17. **The window rule left steps at tile borders, and the plan's fallback would not remove them.** Evidence: `continentality.png` for seed 2198175988018515243 showed κ stepping along straight lines on tile borders. One tile's window reached a body's far side and called it sea; the next tile's window did not, and called the same body a lake (the Risks table predicted this). The planned fallback (an edge-touching body also needs 489 cells) does not change what the second tile sees. **Done:** a body is sea if any of its cells is at or below `CLIMATE_SEA_FLOOR_BLOCKS = -30` (the terrain's sea bed falls below -30 at continentalness about -0.57), or it has 8192 cells. Touching the window's edge no longer counts. Depth is a fact about one cell, so tiles disagree only about a shallow arm more than 8 km from its body's deep part. Result on the same three seeds: 28.8%, 26.6% and 30.4% sea, 0.0% large lake, 0.8% small lake, and no steps in the pictures. A new test holds it: over 8 by 8 tiles, no two neighbouring cells differ by more than 64 blocks in sea distance (0 found, debug and release). That check was not run against the old rule; the old pictures are the evidence for the steps. The digest pin moved from `0xE2FA8EB862075544` to `0x61708389BBF3789B`. **For Ahmed:** this changes the plan's sea rule, so it is his to overrule. A side effect: on real terrain no lake reaches 2 km² without also reaching -30, so `CLIMATE_CELL_LAKE` does not happen on these seeds (it is tested on synthetic ground).
18. **`run_types` pins the number of heap regions.** It expected "14 reserved" and "leak at exit: 14 regions"; the climate's region makes 15. Updated in `tests/run/types/index.c`, with a comment saying why.
19. **tsan was built for the two climate test targets only**, not the whole `tsan` preset, to save time and disk. `run_climate_cache` (37.8 s, 100,000 swaps, 0 torn) and `run_climate_cell` (4.8 s) both pass under tsan with no report.
