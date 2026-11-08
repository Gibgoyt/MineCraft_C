# 004-ahmed-bohemoth-climate-system — phase 6: Weather systems

**Status:** in progress: built and verified on 2026-11-06; Issues found 21 (no hurricanes on real seeds) fixed by the final check on 2026-11-08 (Issues found 22). Only Ahmed's gate is left (watching a season in the frames), and his word on the hurricane rate, which is his to tune by playing (Q5)   (updated 2026-11-08)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then sections 2.3, 2.5, 7.1 to 7.4 of `plans/004-ahmed-bohemoth-climate-system.md`, then the phase 03, 04 and 05 files of this plan, which this phase builds on. Every number here comes from the master plan. Where this file adds a number the master plan does not give, it says "this phase" and gives the reason.

## Goal

For any seed, any place and any moment, the game can list every **weather system** near that place: hurricanes, mid-latitude lows with their cold fronts, highs, and thunderstorm cells. Each one is **spawned by a hash** at a rate that depends on place and season (master plan 7.2), moves along a **track** made by simple steering rules (7.3), grows and fades over its life, and adds pressure, wind and rain around it. Nothing about air or water is simulated. The list is a pure function of the seed, the place and the day: it is the same on any thread, on 1 worker or 16, whichever day it was first asked for. Hurricanes only form in their season, never within 5° of the equator, turn poleward and then east, and weaken over land. Winds circle lows anticlockwise in the north and clockwise in the south. Hurricanes have names made from syllables. Over 10 years on 8 seeds, each test cell's rain (background plus systems) is within 15% of its normal. Map pictures of pressure now (with H and L), systems with their tracks, rain radar and thunderstorm cells can be drawn for any moment, and a folder of frames shows a whole hurricane season going by. The list of big systems near a point takes under 2 ms.

## Where this fits

**The vision.** Ahmed's game is a stack worked out strictly top to bottom: seed, terrain, climate, then (in later plans) geology, vegetation, settlements, the economy and the player's tasks. "The land decides; nothing is hand-placed" (vision file, Part 1). Phase 05 made the **average** year. This phase makes the **events** that turn an average into a history: the hurricane that hit one coast town and missed the next, the winter storm that closed a pass, the week of thunderstorms that flooded a valley.

Ahmed asked for this directly: "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please". The vision file says why it matters later:

| Later reader | What it will read from this phase |
|---|---|
| Phase 07, daily weather and the census | Each day's systems near each generated cell: their rain (into soil water, runoff and the **flood index**), their wind (storm days), their pressure (storm days, "a low over 20 hPa deep"). The outlook's known systems and spawn rates |
| Phase 08, weather in the world | Rain rate now where the player stands, lightning in thunderstorm cells, hurricane winds pushing the player |
| Phase 09, the Atlas | "Pressure systems like cyclones and anti-cyclones ... rain pressure cells etc... to view incoming thunderstorms" (Ahmed). The systems drawn over grey land as outlines, so an incoming hurricane is visible before it reaches explored land (master plan 2.4) |
| Phase 10, the HUD | "Storm warning: Hurricane Tavela" |
| The economy (the plan after this one) | Floods destroy houses: the vision's scorecard counts "property destroyed against property standing", and phase 07's flood index, fed by this phase's rain, is what destroys it. Hurricane seasons make coastal towns riskier; sea lanes close in storms |
| Settlements (a later plan) | "Where it's pleasant to live: ... few floods": the long-run storm record of a place |
| The player (a later plan) | Tasks like "keep the rains from ruining our homes" |

**What this phase must NOT build:**

- **No physics.** No fluid simulation, no air parcels, no pressure solver, no sea surface heat budget. A hurricane is a record with a start point, a size, a strength, a life and a track rule (Ahmed: "please drive a probability thing! but do not drive the actual physics!").
- No daily background weather (temperature spells, background rain noise), no day kinds, no weekly state, no census, no outlook. Those are phase 07. This phase's rain test uses the background rain's **expected value** only.
- Nothing the player sees in the world: no falling rain, no lightning flashes, no wind pushing. That's phase 08.
- No Atlas screen, no HUD. Only layer drawing code and pictures written by a tool.
- No game wiring. Nothing in `run/src/game/` calls this phase yet. Phases 07 to 10 call it.
- No vegetation, no biome change, no geology (vision Part 2). No saving: systems come back from the seed (vision rule 1).

## Ahmed's words

- "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please"
- "please drive a probability thing! but do not drive the actual physics! just create general code please that will sort of mimic it"
- "the user must be able to change filter to see wind current, ocean current, pressure systems like cyclones and anti-cyclones please, as well as rain pressure cells etc... to view incoming thunderstorms"
- "create winds please! And a map that i hit "M" to bring up map, it shows all generated chunks, and it shows winds, ocean current, etc..."
- From the vision: "Any generated map from any seed somehow needs all of these economics in place"

"Spawn" is the hash per spawn square and day. "A probability thing" is the Poisson rate. "General code that will sort of mimic it" is the steering rules, life curves and bell-shaped pressure, which look like weather without computing it. "Pressure systems like cyclones and anti-cyclones" and "rain pressure cells" are this phase's four kinds, and the map layers draw them.

---

## Background, explained

First the real weather, briefly, then exactly how the game fakes each part. Then every programming idea this phase uses, with a worked example. Maths is in LaTeX.

### Game time and real time

A game day is 24,000 ticks at 20 ticks a second (`run/include/game/index.h:34`), so **1 game day = 1200 real seconds** (20 minutes), and **1 clock hour = 50 real seconds**. Wind speeds in this plan are in **blocks a real second** (phase 04: what the player feels). Storm speeds are in **blocks a game day**. To convert:

$$1 \text{ block/s} = 1200 \text{ blocks a game day}$$

Example: a low moving "about 6 km a day" moves 6000 blocks a game day, which is $6000 / 1200 = 5$ blocks a real second: a bit slower than walking (4.3). A thunderstorm cell carried by a 6 blocks/s wind for 2.5 clock hours moves $6 \times 2.5 \times 50 = 750$ blocks.

**Time in this phase** is one `float64_t` count of days, $t = D + f$, where $D$ is phase 01's day count and $f$ is the fraction of the day since its start. Phase 01 starts days at 06:00 on the clock (master plan 4), so clock hour $h$ is $f = ((h - 6) \bmod 24)/24$. Example: 13:00 is $f = 7/24 = 0.2917$; 19:00 is $f = 13/24 = 0.5417$. The day of the year for normals is $d = D \bmod 365$.

### Real weather systems, and how the game fakes each

**Air pressure** is the weight of the air above a place, in hectopascals (hPa); about 1013 at sea level. Wind blows from high pressure toward low. But the Earth spins, and that turns moving air sideways: to the right in the northern hemisphere, to the left in the southern. This is the **Coriolis effect**. It is zero at the equator and strongest at the poles. Because of it, air doesn't flow straight into a low: it spirals round it. **Around a low, air turns anticlockwise in the north and clockwise in the south. Around a high, the reverse.**

**A hurricane** (also typhoon, tropical cyclone) is a low that forms over warm tropical sea, above about 26.5 °C. Warm wet air rises, its water condenses and releases heat, which drives more rising: a heat engine fed by the sea. It needs the Coriolis effect to start spinning, so it never forms within about 5° of the equator. It has a calm **eye** tens of km wide (here 80 to 150 blocks), a ring of the strongest wind and heaviest rain round it (the **eyewall**), and **spiral rain bands** further out. It drifts west with the trade winds and slightly poleward, then often **recurves**: turns poleward and then east into the westerlies. Over land or cold water its heat supply is cut and it weakens within a day or two.

**The game's hurricane:** a pressure dip shaped like a bell, -30 to -90 hPa deep, 1.5 to 4 km wide; a wind profile that rises to a peak at the eye's edge and falls away; rain heaviest at the eyewall with spiral bands; a track made of two rules (west and slightly poleward, then turn and go east); strength that grows, holds and fades over 4 to 10 days, and halves each day over land or cool sea. It spawns at a rate set by phase 05's hurricane season.

**A mid-latitude low** (extratropical cyclone) forms where cold polar air meets warm subtropical air, between about 35° and 65°. It's bigger and weaker than a hurricane, moves east with the westerlies, and carries **fronts**: boundaries between air masses. Its **cold front** trails from the centre toward the equator; cold air pushes under warm air there and lifts it fast, making a line of heavy showers and thunderstorms. Ahead of the low, warm air rides up over cold air slowly, making a wide shield of steady rain. They are strongest and most frequent in winter, when the temperature difference is largest.

**The game's low:** a bell 10 to 30 hPa deep, 3 to 7 km wide, moving east at about 6 km a day with a slow poleward drift; a rain shield of 1 to 4 mm an hour ahead of the centre; a cold front, a straight line 4 to 8 km long trailing equatorward, where thunderstorm cells spawn 5 times as often. Twice as many spawn in winter as in summer.

**A high** (anticyclone) is sinking air. Sinking air warms and dries, so highs bring clear calm weather: heat waves in summer, cold snaps in winter. They're big, slow, and sit for days, mostly near 30° (the subtropical highs) and over continents in winter.

**The game's high:** a bump 8 to 20 hPa high, 6 to 12 km wide, drifting east at about 2 km a day and slowly toward the equator, for 6 to 14 days. Under it, rain is cut to 20% and thunderstorm cells to 20%.

**A thunderstorm cell** is a single tower of rising air, a few km across in reality, that lives an hour or two: it builds in the afternoon heat, rains hard for half an hour, and dies. They're common on hot wet afternoons, along cold fronts, in monsoons, and in the **ITCZ** (the intertropical convergence zone, the band of rising air near the equator where the trade winds meet; in this plan it's the wet equatorial rain belt that phase 05 already follows with $\psi$).

**The game's cell:** a small bell of -1 to -3 hPa, 150 to 400 blocks wide, raining 20 to 60 mm an hour at its middle, for 1 to 4 clock hours, carried in a straight line by the day's wind where it started. They spawn per 1 km square per day at the rate in 7.2.1.

**How the game fakes the spin.** It never computes the Coriolis force. Each system's wind is written directly as a circle round its centre, with the turning sense chosen by the rule above: the sign is $s_h$ (north or south) times phase 04's $e$ (which flips in mirrored strips past a pole), and flipped again for highs.

### Hash

A **hash** turns numbers into a random-looking number, always the same for the same input. The repo's is `terrain_rng_hash3(seed, x, y, z)` (`run/include/terrain/rng/index.h:28-30`): a pure function, so a worker anywhere, at any time, gets the same answer. This phase uses it as "the dice for this spawn square on this day": `terrain_rng_hash3(kind_seed, square_x, square_y, day)`. No list of storms is kept anywhere; the hash is asked again whenever the answer is needed.

Each kind has its own seed, `terrain_rng_derive(world_seed, terrain_rng_salt("climate_systems/hurricane"))` and so on, so a hurricane square and a low square with the same numbers get unrelated dice. (The names start `climate_systems/`, not `climate/`, so they can never equal a terrain noise field's name, which also go through `terrain_rng_salt`.)

### A stream of random numbers from one hash

Each new system needs about a dozen random numbers: its start time, start point, size, strength, life and so on. `terrain_rng_start(seed)` starts a **stream** (SplitMix64, `run/src/terrain/rng/`) and `terrain_rng_next_f64` gives uniform numbers in $[0, 1)$ one after another. The stream for spawn $i$ of a square-day is started from `terrain_rng_derive(h, i + 1)`, where $h$ is the square-day's hash. **Every system always draws the same numbers in the same order**, even ones it doesn't use, and never uses `terrain_rng_next_below` (which sometimes takes more than one draw). So adding a field later changes no existing storm, as long as it's drawn after the others.

A uniform number $u$ becomes a value in a range by $v = \text{lo} + u\,(\text{hi} - \text{lo})$. Example: a hurricane's life is 4 to 10 days; $u = 0.25$ gives $4 + 0.25 \times 6 = 5.5$ days.

### Poisson spawning

Storms start at random, with no pattern, at some average rate $\lambda$ per square per day. The number $n$ that start in one square on one day then follows the **Poisson distribution**:

$$P(n = k) = \frac{\lambda^k e^{-\lambda}}{k!}$$

Example, $\lambda = 0.02$: $P(0) = e^{-0.02} = 0.98020$, $P(1) = 0.02 \times 0.98020 = 0.019604$, $P(2) = 0.000196$. So on 98% of days nothing starts, and two in one day happens about once in 5000 days.

### Turning a uniform number into a count: the inverse CDF

The **CDF** (cumulative distribution function) $F(k) = P(n \le k)$ adds the probabilities up. To draw a count from one uniform number $u$, find the smallest $k$ with $u < F(k)$. That's the **inverse CDF method**. In code:

```
k = 0; p = exp(-λ); F = p
while u >= F and k < cap:  k = k + 1;  p = p * λ / k;  F = F + p
```

**Worked example, $\lambda = 0.02$:** $F(0) = 0.98020$, $F(1) = 0.99980$. A hash giving $u = 0.5$ gives 0. $u = 0.985$ gives 1. $u = 0.9999$ gives 2.

**Worked example, $\lambda = 9$** (7.2.1's monsoon afternoon): $F(8) = 0.4557$, $F(9) = 0.5874$. $u = 0.5$ gives **9**.

$u$ is the hash's top 53 bits times $2^{-53}$, exactly as `terrain_rng_next_f64` makes it (`run/src/terrain/rng/index.c:95-97`), so it's exact and the same on any machine. The cap (`CLIMATE_SPAWNS_MAX`, below) stops a corrupted $\lambda$ from looping forever; at the rates here it is never reached ($P(n > 8)$ at $\lambda = 0.08$ is about $10^{-14}$).

### Rates that change across a square: thinning

The rate depends on place: a hurricane square 16 km tall spans 38° of latitude, sea and land. So the rate at each start point is different. **Thinning** handles this exactly: draw candidates at the largest possible rate $\lambda_{\max}$, give each a start point and time, and **keep** it with probability $\lambda(\text{point}, \text{time}) / \lambda_{\max}$. The kept ones are exactly a Poisson process with the varying rate. Each candidate's "keep" decision uses one more uniform number from its stream: keep if $u_{\text{keep}} < \lambda/\lambda_{\max}$.

**Worked example.** Hurricanes have $\lambda_{\max} = 0.02$ per square per day and $\lambda = 0.02\,s_{\text{hur}}$. A candidate at a point where $s_{\text{hur}} = 0.725$ is kept if $u_{\text{keep}} < 0.725$. Over a whole square, the kept rate is $0.02$ times the square's average $s_{\text{hur}}$. A square entirely in full season gets 0.02 a day, as 7.2's table says.

### Track integration

A system's **track** is where its centre is over its life. It's made by **integration**: start at the start point, and repeatedly move by velocity × time step. This is **Euler's method**:

$$\vec p_{k+1} = \vec p_k + \vec v(\vec p_k, t_k)\,\Delta t, \qquad \Delta t = 0.25 \text{ day}$$

The velocity comes from the steering rules (below), which read only latitude, the season and phase 03's coarse land mask. Each point $\vec p_k$ is a **track sample**, kept in the system's record.

**Worked example.** A low at $(0, 19\,149)$ (45° N) moving east at 6000 blocks a day with a poleward drift of 15%: $\vec v = (6000, 900)$ blocks a day. After one step: $(1500, 19\,374)$. After four steps (one day): $(6000, 20\,049)$, about 47.1° N.

### Linear interpolation between samples

Between two samples, the position at time $t$ is a weighted average:

$$\vec p(t) = \vec p_k + \frac{t - t_k}{\Delta t}\,(\vec p_{k+1} - \vec p_k)$$

Example: samples $(0, 0)$ at $t = 10.0$ and $(1500, 225)$ at $t = 10.25$; at $t = 10.1$ the centre is $(600, 90)$. This is what master plan 7.3 means by "recomputed once a game day, and positions in between are interpolated": the list for a day holds the samples, and any moment in the day is read from them with no new integration.

### Smoothstep and the life curve

$S(x) = 3x^2 - 2x^3$ for $0 \le x \le 1$ (0 below, 1 above) rises smoothly from 0 to 1 with no corners. A system's **life curve** $g(\tau)$, with $\tau$ the share of its life gone from 0 to 1 (7.2: "they grow for the first third of their life, and fade over the last third"):

$$g(\tau) = \begin{cases} S(3\tau) & \tau < \tfrac13 \\ 1 & \tfrac13 \le \tau \le \tfrac23 \\ S\big(3(1 - \tau)\big) & \tau > \tfrac23 \end{cases}$$

Example: $\tau = 1/6$ gives $S(0.5) = 0.5$: half grown. The average of $g$ over a life is exactly $2/3$.

### A bell of pressure

Master plan 7.3: each system adds

$$\Delta p_i(r) = A_i(t)\,e^{-r^2/R_i^2}$$

$r$ is the distance from the centre, $R_i$ the radius, $A_i(t)$ the strength now (negative for lows). This is a **Gaussian bell**. Example: a hurricane with $A = -60$ hPa and $R = 2500$: at the centre $-60$; at $r = 1250$, $-60\,e^{-0.25} = -46.73$; at $r = 2500$, $-60/e = -22.07$; at $3R = 7500$, $-60\,e^{-9} = -0.0074$.

### The wind round a system

Master plan 7.3's speed profile, with $r_m$ the radius of strongest wind:

$$v(r) = \begin{cases} v_{\max}\,r / r_m & r < r_m \\ v_{\max}\,(r_m/r)^{0.6} & r \ge r_m \end{cases}$$

Inside $r_m$ it grows like a spinning disc; outside it falls off slowly. Example: a hurricane with $v_{\max} = 51.5$ and an eye of $r_m = 100$: at $r = 50$, 25.75 (the eye is calm near its middle); at $r = 100$, 51.5; at $r = 1000$, $51.5 \times 0.1^{0.6} = 12.94$ blocks a second.

**Direction.** The unit vector from the centre to the point is $\hat r = (\Delta x, \Delta y)/r$. Turning it 90° anticlockwise gives $(-\hat r_y, \hat r_x)$: the direction of anticlockwise circling. The wind is

$$\vec w = \sigma\,v(r)\,(-\hat r_y,\ \hat r_x), \qquad \sigma = s_h \cdot e \cdot \begin{cases} +1 & \text{low, hurricane} \\ -1 & \text{high} \end{cases}$$

Example: a low at 45° N ($s_h = e = +1$), a point due east of the centre: $\hat r = (1, 0)$, so the wind is $(0, +v)$: north. Air east of a northern low blows north, which is anticlockwise. In the south ($s_h = -1$) it blows south: clockwise.

### Stored strength and wind speed

For hurricanes, $v_{\max}$ comes from the pressure dip (7.3): 33 blocks a second at -30 hPa, 70 at -90. Between them it's a straight line; below 30 hPa (while growing or dying) it falls in a straight line to 0:

$$v_{\max} = \begin{cases} 33\,\lvert A\rvert/30 & \lvert A\rvert < 30 \\ 33 + 37\,(\lvert A\rvert - 30)/60 & \lvert A\rvert \ge 30 \end{cases}$$

Example: $A = -60$ gives $33 + 18.5 = 51.5$. A hurricane under 33 blocks a second is called a **tropical storm** (7.2: "Tropical storm Orin").

For lows and highs (7.3): $r_m = 0.5\,R$ and $v_{\max} = 0.4\,\lvert A\rvert$. Example: a low of -20 hPa: 8 blocks a second at half its radius.

### Names from syllables

7.2: "Hurricane names are made from syllables by hash, so each world has its own". A table of 24 short syllables. A system's 32-bit `name_seed` (from its stream) picks:

- bit 0: 2 syllables (0) or 3 (1);
- bits 1 to 5, 6 to 10, 11 to 15: three 5-bit numbers, each taken modulo 24, the syllables.

The first letter is made upper case. If 3 syllables would make more than 8 letters, the third is dropped, so "Tropical storm " plus the name fits in 24 bytes with its terminating zero (phase 07 keeps names in `char name[24]`).

**Worked example.** The table starts `ta ve la o rin ka ...`. `name_seed = 4161`: bit 0 is 1, so 3 syllables. $(4161 \gg 1) \mathbin{\&} 31 = 0$: "ta". $(4161 \gg 6) \mathbin{\&} 31 = 1$: "ve". $(4161 \gg 11) \mathbin{\&} 31 = 2$: "la". **Tavela.** `name_seed = 262`: bit 0 is 0, 2 syllables, $3$ and $4$: "o" and "rin": **Orin**. Those are the master plan's two examples.

### Looking up the systems near a point

To know what's near a place today, the game must find every system that started within reach, on any day it could still be alive (master plan 2.5). For each kind:

1. **Reach** $L$: the furthest a system can travel in its longest life, plus 3 radii (beyond $3R$ its pressure is under 0.02% of its strength and its wind and rain are cut to zero).
2. **Squares:** every spawn square whose area comes within $L$ plus the asked radius of the point.
3. **Days:** from $D - \lceil \text{longest life} \rceil$ to $D$.
4. For each square and day: the count by the inverse CDF, then each candidate's numbers, the thinning, the track, and a check that its footprint reaches the asked area at some moment of day $D$.

The cost is mostly the counts, which are one hash and a compare each (most come out 0).

---

## Design

### Directories and files

`run/src/climate/systems/` is the master plan's own name for this (section 1). Each directory has exactly one `index.c`, a `README.md` (purpose, tier, owns, lifetime, threads, public functions, may be called by, depends on, per `plans/REPO_STRUCTURE.md` section 3), and a header at `run/include/climate/<path>/index.h`:

| Directory | Depth, tier | Job |
|---|---|---|
| `run/src/climate/systems/` | 2, Arena | NEW. Kinds, constants, rates, the hash and Poisson counts, thinning, tracks, the day list. Allocates the list and private mask pages from the arena it is given |
| `run/src/climate/systems/field/` | 3, Borrow | NEW. Pressure, wind and rain at a point and moment, from a day list. Allocates nothing |
| `run/src/climate/systems/name/` | 3, Borrow | NEW. The syllable table and the name builder. Allocates nothing |

Changes to earlier parts:

- `run/src/climate/mask/` (phase 03): adds `climate_mask_height_at`, a point read, if phase 03 didn't make one.
- `run/src/climate/layer/` (phase 03): five "now" layers and `climate_layer_draw_now`.
- `run/src/climate/index.c`, `run/include/climate/index.h` (the Owner): `climate_systems_hooks_set` for tests.

New tests:

| Directory | ctest |
|---|---|
| `tests/run/climate/systems/` | `run_climate_systems` |
| `tests/run/climate/systems/rain/` | `run_climate_systems_rain`, with a `test.cmake` setting `MC_TEST_TIMEOUT 3600` (the 10-year test; `cmake/targets.cmake:140-145` documents the variable) |

Added to: `tests/run/climate/layer/` (`run_climate_layer`), `tests/run/climate/fixtures/ground.h` (an island and an all-sea ground for tracks). Bench: `bench/climate/` gains `--systems` and `--season`.

### Dependencies on earlier phases, by what they do

Phases 01 to 05 are not built yet. This file uses the names their phase files give. If the built code uses other names, use the built ones and write the mapping under Issues found.

| Needed | From | Name in its phase file |
|---|---|---|
| Day count $D$, the 06:00 day start | Phase 01 | the calendar |
| Latitude, declination | Phase 02 | `climate_latitude_deg`, phase 02's $\delta$ |
| The coarse land mask, read at a point, built privately if not cached | Phase 03 | the mask part; `climate_mask_height_at` added here if absent |
| The mirror sign $e(y)$ | Phase 04 | phase 04's Background, "The latitude in this world is a mirror" |
| $\bar T_{\text{lat}}$ and $A_{\text{sea}}$ splines | Phase 04 | `run/src/climate/normals/` |
| Wind and pressure at a point and day | Phase 04 | `climate_air_at` |
| Normal temperature, normal rain and its wettest day, monsoon today | Phase 05 | `climate_normals_at` (`t_day_c`, `rain_mm_day`, `rain_max_mm_day`, `monsoon_today`) |
| $s_{\text{hur}}$ from latitude and sea temperature | Phase 05 | `climate_season_hurricane_s` |

### Kinds and constants

In `run/include/climate/systems/index.h`. One home each (vision rule 5); floats use the `f32` or `f64` suffix as `plans/CODING_STANDARD.md` section 2 requires.

```c
enum : uint8_t {
    CLIMATE_SYSTEM_HURRICANE = 0,
    CLIMATE_SYSTEM_LOW = 1,
    CLIMATE_SYSTEM_HIGH = 2,
    CLIMATE_SYSTEM_THUNDER = 3,     /* thunderstorm cells: kept in their own array, climate_thunder_t */
    CLIMATE_SYSTEM_KINDS = 4,
};
enum : uint32_t {                   /* bits for the kinds argument of climate_systems_day_build */
    CLIMATE_SYSTEMS_BIG = (1u << CLIMATE_SYSTEM_HURRICANE) | (1u << CLIMATE_SYSTEM_LOW) | (1u << CLIMATE_SYSTEM_HIGH),
    CLIMATE_SYSTEMS_ALL = CLIMATE_SYSTEMS_BIG | (1u << CLIMATE_SYSTEM_THUNDER),
};

/* Time. */
static const float64_t CLIMATE_TRACK_STEP_DAYS = 0.25f64;          /* 7.3: steps of a quarter of a day */
static const float32_t CLIMATE_REAL_SECONDS_A_DAY = 1200.0f32;     /* 24000 ticks at 20 Hz: run/include/game/index.h:34 */
enum : uint32_t {
    CLIMATE_TRACK_SAMPLES_MAX = 57u,  /* 14 days × 4 + 1: the longest life, a high's */
    CLIMATE_SPAWNS_MAX = 8u,          /* this phase: the inverse CDF's cap per square-day, big systems */
    CLIMATE_THUNDER_SPAWNS_MAX = 64u, /* this phase: the cap for cells, λ up to 45 before tuning */
    CLIMATE_SYSTEMS_LIST_MAX = 128u,  /* this phase: big systems in one day list */
    CLIMATE_THUNDER_LIST_MAX = 8192u, /* this phase: cells in one day list */
};
static const float32_t CLIMATE_THUNDER_QUERY_MAX = 8192.0f32;     /* this phase: blocks, the largest thunder_radius */

/* Spawn squares, blocks: 7.2. */
static const float64_t CLIMATE_SQUARE_HURRICANE = 16384.0f64;      /* 16 km */
static const float64_t CLIMATE_SQUARE_LOW = 16384.0f64;            /* 16 km */
static const float64_t CLIMATE_SQUARE_HIGH = 24576.0f64;           /* 24 km */
static const float64_t CLIMATE_SQUARE_THUNDER = 1024.0f64;         /* 1 km */

/* Rates, spawns per square per day: 7.2 and 7.2.1. The ones the 10-year test tunes (7.4) are marked. */
static const float32_t CLIMATE_HURRICANE_RATE = 0.02f32;           /* × s_hur; tuned */
static const float32_t CLIMATE_LOW_RATE_WINTER = 0.08f32;          /* tuned */
static const float32_t CLIMATE_LOW_RATE_SUMMER = 0.04f32;          /* tuned */
static const float32_t CLIMATE_HIGH_RATE = 0.03f32;                /* tuned */
static const float32_t CLIMATE_THUNDER_RATE = 3.0f32;              /* the leading 3 of 7.2.1 */
static const float32_t CLIMATE_THUNDER_RATE_GAIN = 1.0f32;         /* this phase: the one knob 7.4 tunes for cells */
static const float32_t CLIMATE_THUNDER_HOT_C = 18.0f32;            /* 7.2.1: (T - 18) / 10 */
static const float32_t CLIMATE_THUNDER_HOT_RANGE_C = 10.0f32;
static const float32_t CLIMATE_THUNDER_MONSOON = 2.0f32;           /* 7.2.1: 1 + 2 f_monsoon */
static const float32_t CLIMATE_THUNDER_FRONT = 4.0f32;             /* 7.2.1: 1 + 4 f_front */
static const float32_t CLIMATE_THUNDER_HIGH = 0.2f32;              /* 7.2.1: 0.2^{f_high} */

/* Where: 7.2. */
static const float32_t CLIMATE_LOW_PSI_MIN_DEG = 35.0f32;
static const float32_t CLIMATE_LOW_PSI_MAX_DEG = 65.0f32;
static const float32_t CLIMATE_HIGH_PSI_MIN_DEG = 20.0f32;
static const float32_t CLIMATE_HIGH_PSI_MAX_DEG = 45.0f32;
static const float32_t CLIMATE_HIGH_INTERIOR_PSI_MAX_DEG = 70.0f32; /* this phase: winter interior highs up to here */
static const float32_t CLIMATE_HIGH_INTERIOR_BLOCKS = 3000.0f32;   /* this phase: land this far all round is "interior" */
static const float32_t CLIMATE_HURRICANE_OPEN_SEA_BLOCKS = 2048.0f32; /* this phase: water this far all round is "sea" */

/* Sizes, strengths and lives: 7.2. Each a uniform range. */
static const float32_t CLIMATE_HURRICANE_RADIUS[2] = {1500.0f32, 4000.0f32};
static const float32_t CLIMATE_HURRICANE_EYE[2] = {80.0f32, 150.0f32};
static const float32_t CLIMATE_HURRICANE_HPA[2] = {-30.0f32, -90.0f32};
static const float32_t CLIMATE_HURRICANE_LIFE_DAYS[2] = {4.0f32, 10.0f32};
static const float32_t CLIMATE_LOW_RADIUS[2] = {3000.0f32, 7000.0f32};
static const float32_t CLIMATE_LOW_HPA[2] = {-10.0f32, -30.0f32};
static const float32_t CLIMATE_LOW_LIFE_DAYS[2] = {3.0f32, 6.0f32};
static const float32_t CLIMATE_LOW_FRONT_BLOCKS[2] = {4000.0f32, 8000.0f32};
static const float32_t CLIMATE_HIGH_RADIUS[2] = {6000.0f32, 12000.0f32};
static const float32_t CLIMATE_HIGH_HPA[2] = {8.0f32, 20.0f32};
static const float32_t CLIMATE_HIGH_LIFE_DAYS[2] = {6.0f32, 14.0f32};
static const float32_t CLIMATE_THUNDER_RADIUS[2] = {150.0f32, 400.0f32};
static const float32_t CLIMATE_THUNDER_HPA[2] = {-1.0f32, -3.0f32};
static const float32_t CLIMATE_THUNDER_LIFE_HOURS[2] = {1.0f32, 4.0f32};
static const float32_t CLIMATE_THUNDER_MM_HOUR[2] = {20.0f32, 60.0f32};
static const float32_t CLIMATE_THUNDER_START_HOURS[2] = {13.0f32, 19.0f32}; /* clock hours, off fronts */

/* Motion, blocks a game day: 7.3. */
static const float32_t CLIMATE_HURRICANE_SPEED_WEST = 3500.0f32;
static const float32_t CLIMATE_HURRICANE_SPEED_EAST = 6000.0f32;
static const float32_t CLIMATE_HURRICANE_POLEWARD_WEST = 0.15f32;  /* this phase: drift while going west, Issues found 3 */
static const float32_t CLIMATE_HURRICANE_POLEWARD_EAST = 0.3f32;   /* this phase: the westerlies' own 0.3, 6.5 */
static const float32_t CLIMATE_HURRICANE_TURN_PSI_DEG[2] = {20.0f32, 28.0f32};
static const float32_t CLIMATE_HURRICANE_TURN_DAYS = 2.0f32;
static const float32_t CLIMATE_LOW_SPEED[2] = {5000.0f32, 7000.0f32};    /* this phase: "about 6 km a day", ±1/6 */
static const float32_t CLIMATE_LOW_POLEWARD = 0.15f32;                   /* this phase: "a slow drift poleward" */
static const float32_t CLIMATE_HIGH_SPEED[2] = {1500.0f32, 2500.0f32};   /* this phase: "about 2 km a day", ±1/4 */
static const float32_t CLIMATE_HIGH_EQUATORWARD = 0.1f32;                /* this phase: "or equatorward (highs)" */
static const float32_t CLIMATE_SPEED_SCALE[2] = {0.85f32, 1.15f32};      /* this phase: a hurricane's own speed */
static const float32_t CLIMATE_THUNDER_SPEED_MAX = 15.0f32;              /* this phase: blocks a second, caps reach */
static const float32_t CLIMATE_DRIFT_POLE_STOP_DEG = 85.0f32;            /* this phase: no poleward drift past here */
static const float32_t CLIMATE_DRIFT_EQUATOR_STOP_DEG = 5.0f32;          /* this phase: no equatorward drift inside here */

/* Weakening: 7.2. */
static const float32_t CLIMATE_HURRICANE_HALF_LIFE_DAYS = 1.0f32;  /* halves each game day over land */
static const float32_t CLIMATE_HURRICANE_COOL_SEA_C = 26.0f32;     /* and over sea below this */
static const float32_t CLIMATE_SYSTEM_DEAD_HPA = 5.0f32;           /* this phase: under this, a big system ends */

/* Fields: 7.3. */
static const float32_t CLIMATE_SYSTEM_REACH_RADII = 3.0f32;        /* this phase: nothing past 3 R */
static const float32_t CLIMATE_SYSTEM_WIND_TAPER_RADII = 2.0f32;   /* this phase: wind tapers to 0 from 2 R to 3 R */
static const float32_t CLIMATE_HURRICANE_VMAX_AT_30 = 33.0f32;     /* blocks a second at -30 hPa */
static const float32_t CLIMATE_HURRICANE_VMAX_AT_90 = 70.0f32;     /* at -90 */
static const float32_t CLIMATE_HURRICANE_STORM_VMAX = 33.0f32;     /* under this it's a tropical storm */
static const float32_t CLIMATE_SYSTEM_VMAX_PER_HPA = 0.4f32;       /* lows and highs */
static const float32_t CLIMATE_SYSTEM_CORE_SHARE = 0.5f32;         /* lows and highs: r_m = 0.5 R */
static const float32_t CLIMATE_WIND_PROFILE_POWER = 0.6f32;

/* Rain: 7.3. */
static const float32_t CLIMATE_HURRICANE_EYEWALL_MM_HOUR = 40.0f32;  /* at -90 hPa; scaled by |A| / 90 */
static const float32_t CLIMATE_LOW_SHIELD_MM_HOUR[2] = {1.0f32, 4.0f32};
static const float32_t CLIMATE_LOW_SHIELD_AHEAD = 0.5f32;           /* this phase: shield centred 0.5 R ahead */
static const float32_t CLIMATE_LOW_SHIELD_WIDTH = 0.6f32;           /* this phase: shield's width, × R */
static const float32_t CLIMATE_FRONT_MM_HOUR = 3.0f32;              /* this phase: showers on the front line */
static const float32_t CLIMATE_FRONT_HALF_WIDTH = 400.0f32;         /* this phase: blocks either side of the line */
static const float32_t CLIMATE_HIGH_RAIN_SHARE = 0.2f32;            /* 7.2 and 7.3 */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_TROPICS = 0.3f32;  /* f_sys, 7.4 */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_MID = 0.5f32;      /* f_sys, 7.4 */
static const float32_t CLIMATE_SYSTEM_RAIN_SHARE_BLEND_DEG[2] = {20.0f32, 35.0f32}; /* this phase: smoothstep between */
```

### What a big system is

```c
/* One track sample: where the centre is, a quarter day apart. */
typedef struct climate_track_sample_t {
    float64_t x;         /* 0: blocks */
    float64_t y;         /* 8 */
    float32_t weaken;    /* 16: hurricanes: the product of weakening so far, 1 at birth; 1 for other kinds */
    uint8_t over_land;   /* 20: the mask's height > 0 here */
    uint8_t stage;       /* 21: hurricanes: 0 westward, 1 turning, 2 eastward; 0 otherwise */
    uint8_t _pad0[2];    /* 22 */
} climate_track_sample_t;
static_assert(sizeof(climate_track_sample_t) == 24);
static_assert(offsetof(climate_track_sample_t, weaken) == 16);
static_assert(offsetof(climate_track_sample_t, over_land) == 20);

/* A hurricane, low or high: everything about it, from its spawn to its death. Worked out from the seed; never saved. */
typedef struct climate_system_t {
    float64_t spawn_day;      /* 0: t = D + f when it formed */
    float64_t start_x;        /* 8: blocks */
    float64_t start_y;        /* 16 */
    uint64_t id;              /* 24: the stream's seed: stable, unique per kind, square, day and index */
    float32_t radius;         /* 32: R, blocks */
    float32_t core_radius;    /* 36: r_m: the eye's edge for hurricanes, 0.5 R otherwise */
    float32_t peak_hpa;       /* 40: A at full strength, signed */
    float32_t life_days;      /* 44: as drawn; the track may end sooner if it weakens to death */
    float32_t speed;          /* 48: blocks a day: lows' and highs' own speed; hurricanes' scale 0.85 to 1.15 */
    float32_t turn_psi_deg;   /* 52: hurricanes: |ψ| where the recurve starts; 0 otherwise */
    float32_t front_length;   /* 56: lows: blocks; 0 otherwise */
    float32_t shield_mm_hour; /* 60: lows: the rain shield at full strength; 0 otherwise */
    uint32_t name_seed;       /* 64 */
    int32_t square_x;         /* 68 */
    int32_t square_y;         /* 72 */
    uint8_t kind;             /* 76: CLIMATE_SYSTEM_* */
    uint8_t spawn_index;      /* 77: which of its square-day's candidates */
    uint8_t sample_count;     /* 78: samples[0 .. sample_count - 1] are its whole track */
    uint8_t _pad0[1];         /* 79 */
    climate_track_sample_t samples[CLIMATE_TRACK_SAMPLES_MAX]; /* 80 */
} climate_system_t;
static_assert(sizeof(climate_system_t) == 80 + 57 * 24);   /* 1448 */
static_assert(alignof(climate_system_t) == 8);
static_assert(offsetof(climate_system_t, id) == 24);
static_assert(offsetof(climate_system_t, name_seed) == 64);
static_assert(offsetof(climate_system_t, kind) == 76);
static_assert(offsetof(climate_system_t, samples) == 80);
```

Sample $k$ is at time `spawn_day` $+ 0.25k$. The system is alive from `spawn_day` to `spawn_day` $+ 0.25\,(\text{sample\_count} - 1)$. The whole track is kept, not just the day's piece, because the Atlas draws "tracks behind them" (master plan 9.3) and phase 07's outlook needs the system's present state.

```c
/* A thunderstorm cell: a straight line at a constant velocity, so no samples. */
typedef struct climate_thunder_t {
    float64_t spawn_day;      /* 0 */
    float64_t start_x;        /* 8 */
    float64_t start_y;        /* 16 */
    uint64_t id;              /* 24 */
    float32_t velocity[2];    /* 32: blocks a game day: the day's wind at the start, capped at 15 blocks a second */
    float32_t radius;         /* 40 */
    float32_t peak_mm_hour;   /* 44 */
    float32_t peak_hpa;       /* 48 */
    float32_t life_days;      /* 52: 1 to 4 clock hours, over 24 */
    uint8_t on_front;         /* 56 */
    uint8_t _pad0[7];         /* 57 */
} climate_thunder_t;
static_assert(sizeof(climate_thunder_t) == 64);
static_assert(alignof(climate_thunder_t) == 8);

/* Everything near a place on one day. Built once a day by whoever needs it, in their arena, and read all day. */
typedef struct climate_systems_day_t {
    climate_system_t *borrowed_systems;   /* 0: system_capacity of them, in the builder's arena */
    climate_thunder_t *borrowed_thunder;  /* 8 */
    float64_t centre_x;                   /* 16: blocks */
    float64_t centre_y;                   /* 24 */
    int64_t day;                          /* 32: D */
    float32_t radius;                     /* 40: big systems whose 3 R footprint comes within this, at any moment of D */
    float32_t thunder_radius;             /* 44: the same for cells; 0 if cells were not asked for */
    uint32_t system_count;                /* 48 */
    uint32_t system_capacity;             /* 52 */
    uint32_t thunder_count;               /* 56 */
    uint32_t thunder_capacity;            /* 60 */
    uint8_t thunder_complete;             /* 64: 1 if every tile cells needed was built (cells read normals) */
    uint8_t overflowed;                   /* 65: 1 if a capacity was reached: a bug, tests hold it at 0 */
    uint8_t _pad0[6];                     /* 66 */
} climate_systems_day_t;
static_assert(sizeof(climate_systems_day_t) == 72);

/* A big system at a moment. */
typedef struct climate_system_state_t {
    float64_t x;             /* 0 */
    float64_t y;             /* 8 */
    float32_t pressure_hpa;  /* 16: A(t), signed */
    float32_t wind_max;      /* 20: v_max, blocks a second */
    float32_t heading[2];    /* 24: unit direction of motion */
    float32_t life_share;    /* 32: τ */
    uint8_t alive;           /* 36 */
    uint8_t over_land;       /* 37 */
    uint8_t _pad0[2];        /* 38 */
} climate_system_state_t;
static_assert(sizeof(climate_system_state_t) == 40);
```

The list order is fixed: big systems sorted by kind, then `spawn_day`, then `square_y`, `square_x`, `spawn_index`; cells by `spawn_day`, then their square, then index. Sums over the list (pressure, wind, rain) add in that order, so they're the same bytes however the list was found.

### Public functions

In `run/include/climate/systems/index.h`:

```c
/* Every system of the asked kinds near (x, y) at any moment of day D: big systems whose 3 R footprint comes within radius,
 * cells within thunder_radius (at most CLIMATE_THUNDER_QUERY_MAX, 8192 blocks). Memory for the list and for any mask page
 * it must build privately comes from arena. A pure function of the climate's seed and its arguments, with one exception:
 * cells read phase 05's normals, so if a tile they need is not built, those squares are skipped and thunder_complete is 0.
 * False only if arena is too small. Any thread. */
[[nodiscard]] bool climate_systems_day_build(const climate_t *climate, arena_t *arena, float64_t x, float64_t y,
                                             float32_t radius, float32_t thunder_radius, int64_t day, uint32_t kinds,
                                             climate_systems_day_t *out);

/* A big system at time t (any time; alive is 0 outside its life). Interpolates between samples. */
void climate_system_state_at(const climate_system_t *system, float64_t t, climate_system_state_t *out);

/* 7.2's rates, exposed for tests, the outlook (phase 07) and the bench. Spawns per square per day, at a point and time. */
float32_t climate_systems_hurricane_rate(const climate_t *climate, arena_t *arena, float64_t x, float64_t y, float64_t t);
float32_t climate_systems_low_rate(const climate_t *climate, float64_t y, float64_t t);
float32_t climate_systems_high_rate(const climate_t *climate, arena_t *arena, float64_t x, float64_t y, float64_t t);
/* 7.2.1 at a 1 km square's centre on day D, before thinning by fronts and highs. Writes false to *ready if its tile is not
 * built. */
float32_t climate_systems_thunder_rate(const climate_t *climate, int32_t square_x, int32_t square_y, int64_t day,
                                       bool *ready);

/* The longest distance a system of this kind can reach from its spawn square, blocks: track plus 3 R. */
float64_t climate_systems_reach(uint8_t kind);
```

In `run/include/climate/systems/field/index.h`:

```c
/* Sum of every system's bell at (x, y) at time t, hPa. Phase 04's belt and heat-low pressure are not included. */
float32_t climate_systems_pressure_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* Sum of every system's circling wind, blocks a second, u east and v north. */
void climate_systems_wind_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t, float32_t out[2]);
/* Rain from hurricanes, lows, fronts and cells, mm an hour, before the high's cut. */
float32_t climate_systems_rain_at(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* 0.2 under any high's radius, else 1: what background and system rain are multiplied by (7.2, 7.3). */
float32_t climate_systems_rain_factor(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* Whether (x, y) is on a low's cold front at t, and under a high: 7.2.1's f_front and f_high. */
bool climate_systems_on_front(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
bool climate_systems_under_high(const climate_systems_day_t *day, float64_t x, float64_t y, float64_t t);
/* 7.4's f_sys at a latitude. */
float32_t climate_systems_rain_share(float32_t lat_deg);
```

In `run/include/climate/systems/name/index.h`:

```c
/* The bare name from a seed: 2 or 3 syllables, at most 8 letters, first letter upper case, into out (at least 9 bytes). */
void climate_system_name(uint32_t name_seed, char out[9]);
/* "Hurricane Tavela" or "Tropical storm Tavela", by v_max at t. out holds 24 bytes. */
void climate_system_title(const climate_system_t *system, float64_t t, char out[24]);
```

In `run/include/climate/mask/index.h` (phase 03's part), if absent:

```c
/* The coarse land mask's height at the 64-block cell holding (x, y): from the cached page under the read protocol, or by
 * claiming and building it inside the keep square, or by building a private copy in arena outside it. The same number
 * whichever way. Any thread. */
float32_t climate_mask_height_at(const climate_t *climate, arena_t *arena, float64_t x, float64_t y);
```

For tests, in `run/include/climate/index.h`:

```c
/* For tests only: each field at 0 means off. */
typedef struct climate_systems_hooks_t {
    float32_t hurricane_s;           /* 0: if above 0, s_hur is this between hurricane_first and hurricane_last, else 0 */
    float32_t hurricane_current_c;   /* 4: added to the track-time sea temperature */
    uint16_t hurricane_first;        /* 8: days of the year */
    uint16_t hurricane_last;         /* 10 */
    uint8_t big_off;                 /* 12: bit per kind: no spawns of that kind */
    uint8_t _pad0[3];                /* 13 */
} climate_systems_hooks_t;
static_assert(sizeof(climate_systems_hooks_t) == 16);
void climate_systems_hooks_set(climate_t *climate, const climate_systems_hooks_t *hooks);
```

### Spawning: the hash scheme

For each kind $k$ with square side $Q_k$:

1. `kind_seed = terrain_rng_derive(world_seed, terrain_rng_salt(name_k))`, with `name_k` one of `"climate_systems/hurricane"`, `"climate_systems/low"`, `"climate_systems/high"`, `"climate_systems/thunder"`. Worked out once in `climate_open`, kept in `climate_t` (4 × 8 bytes from its padding).
2. Square of a block: $s_x = \lfloor x / Q_k \rfloor$, $s_y = \lfloor y / Q_k \rfloor$, in `float64_t` then to `int32_t` (`floor`, so $x = -1$ is square $-1$).
3. The square-day's hash: $h = $ `terrain_rng_hash3(kind_seed, s_x, s_y, (int32_t)D)`. $D$ fits: `int32_t` holds 5.8 million years of days.
4. The count: $u = (h \gg 11) \times 2^{-53}$, then the inverse CDF with $\lambda_{\max}$ (a constant per kind for big systems; per square-day for cells, below), capped at `CLIMATE_SPAWNS_MAX` or `CLIMATE_THUNDER_SPAWNS_MAX`.
5. For each candidate $i = 0 \ldots n - 1$: `id = terrain_rng_derive(h, i + 1)`, `stream = terrain_rng_start(id)`. Draws, always all of them, in this order, each `terrain_rng_next_f64` unless said:

| # | Draw | Becomes |
|---|---|---|
| 1 | $u_t$ | Start time $f$ in the day: $u_t$ for big systems; for cells off fronts $(13 - 6 + 6u_t)/24$, on fronts $u_t$ |
| 2 | $u_x$ | Start $x = (s_x + u_x)\,Q_k$ |
| 3 | $u_y$ | Start $y = (s_y + u_y)\,Q_k$ |
| 4 | $u_{\text{keep}}$ | Thinning: kept if $u_{\text{keep}} < \lambda(\text{start}, t)/\lambda_{\max}$ |
| 5 | $u_R$ | Radius from its range |
| 6 | $u_c$ | Hurricanes: eye radius. Others: unused |
| 7 | $u_A$ | Peak pressure from its range |
| 8 | $u_L$ | Life from its range |
| 9 | $u_v$ | Lows, highs: speed from range. Hurricanes: speed scale. Cells: peak rain |
| 10 | $u_\psi$ | Hurricanes: turn latitude. Lows: front length. Others: unused |
| 11 | $u_s$ | Lows: shield rain. Others: unused |
| 12 | `terrain_rng_next_u32` | `name_seed` |

A candidate not kept still makes all 12 draws, so its neighbours' numbers never depend on whether it was kept.

### The rates at a point, by kind

| Kind | $\lambda_{\max}$ | $\lambda(\text{point}, t)$ |
|---|---|---|
| Hurricane | 0.02 | $0.02\,s_{\text{hur}}$, with $s_{\text{hur}}$ from phase 05's `climate_season_hurricane_s` at the point's $\varphi$ and the **track-time sea temperature** $T_{\text{sea},0}$ (below); 0 unless the point is open sea: the mask's height $\le 0$ at the point and at the 4 points `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS` (2048) north, south, east and west |
| Low | 0.08 | $0.06 - 0.02\,s_h\cos\big(2\pi(d - 200)/365\big)$ while $35° \le \lvert\psi\rvert \le 65°$, else 0. In the north this is 0.08 on day 17 and 0.04 on day 200 |
| High | 0.03 | 0.03 while $20° \le \lvert\psi\rvert \le 45°$; also 0.03 in a **continental interior in winter**: local winter ($s_h\cos(2\pi(d - 200)/365) < 0$), $\lvert\psi\rvert \le 70°$, and the mask's height $> 0$ at the point and at 8 points 3000 blocks away (the 4 axes and 4 diagonals). Else 0 |
| Thunder | per square-day, below | — |

$\psi = \varphi - 0.5\,\delta(d - 30)$ (phase 04), at the start point and start time's day of the year.

**The track-time sea temperature.** Phase 04's $T_{\text{sea}}$ includes the current $C$, which lives in a climate cell. Big systems must spawn and move anywhere without a climate tile (master plan 2.5, 7.3). So spawning and weakening use

$$T_{\text{sea},0} = \bar T_{\text{lat}}(\lvert\varphi\rvert) + A_{\text{sea}}(\lvert\varphi\rvert)\,s_h\cos\!\left(\frac{2\pi(d - 215)}{365}\right) + C_{\text{hook}}$$

with no current (and $C_{\text{hook}} = 0$ outside tests). At 15° N on day 215 that's $26 + 1.95 = 27.95$ °C, $s_{\text{hur}} = 0.725$. Warm currents still lengthen the **labelled** season (phase 05's `hurricane_days`), but not the spawn rate. See Issues found 1.

**Worked example: how many hurricanes.** Master plan 7.2: $s_{\text{hur}}$ near 1 for about 120 days gives $0.02 \times 120 = 2.4$ per square a season. With the hook forcing $s_{\text{hur}} = 1$ for 120 days, the machinery must give exactly that (a test). With the real $T_{\text{sea},0}$ at 15° N, $\sum_d s_{\text{hur}} = 71.4$, so a square of open sea all at 15° would get $0.02 \times 71.4 = 1.43$ a year. A real 16 km square spans 38.5° of latitude, so the square from $y = 0$ to $16\,384$ (0° to 38.5° N), all sea, averages $0.58$ a year.

### Thunderstorm cells: rate and spawning

For each 1 km square $(s_x, s_y)$ and day $D$, at the square's centre, with phase 05's normals at the centre's ground (master plan 7.2.1):

$$\lambda_{\text{base}} = G\cdot 3\cdot\operatorname{clamp}\!\left(\frac{T_{\text{day}} - 18}{10}, 0, 1\right)\cdot\frac{\bar P(d)}{\bar P_{\max}}\cdot(1 + 2\,f_{\text{monsoon}})$$

- $G$ is `CLIMATE_THUNDER_RATE_GAIN`, 1 until the 10-year test tunes it (Issues found 4).
- $T_{\text{day}}$ is **phase 05's normal daily mean** `t_day_c`. Phase 07 adds the day's background anomaly by changing this one input, in the one function `climate_systems_thunder_rate`, and nothing else here (the master plan's order, section 3: daily weather sits below systems, so phase 07 owns the anomaly).
- $\bar P(d)/\bar P_{\max}$ is `rain_mm_day / rain_max_mm_day` from `climate_normals_at`, 0 if the maximum is 0.
- $f_{\text{monsoon}}$ is `monsoon_today`.

The two factors that change within the day and across the square are applied by **splitting and thinning**, so the rate is exact everywhere:

- $(1 + 4 f_{\text{front}}) = 1 + 4 f_{\text{front}}$: two separate spawn processes on the same square-day. **Afternoon cells**, $\lambda_{\max} = \lambda_{\text{base}}$, starting 13:00 to 19:00, all kept by this factor. **Front cells**, $\lambda_{\max} = 4\lambda_{\text{base}}$, starting at any hour, kept only if their start point is on a cold front at their start time. The two use different hashes: `terrain_rng_hash3(thunder_seed, s_x, s_y, D)` and the same with `terrain_rng_derive(thunder_seed, 1)`.
- $(0.2)^{f_{\text{high}}}$: every cell of either process is kept with probability 0.2 if its start point is under a high at its start time.

**Worked example.** A monsoon afternoon at 30 °C in the wettest week, not on a front, not under a high, $G = 1$: $\lambda_{\text{base}} = 3 \times 1 \times 1 \times 3 = 9$, the master plan's number. On a front: afternoon 9 plus front cells at up to 36, so 45 in all. Under a high: $9 \times 0.2 = 1.8$. On a dry winter day at 15 °C: 0.

**A cell's motion.** Its velocity is phase 04's wind (`climate_air_at`) at its start point at noon of its start day, converted to blocks a game day ($\times 1200$), with speed capped at 15 blocks a second. Its position at $t$ is start $+$ velocity $\times\,(t - t_{\text{spawn}})$. Its strength follows the life curve over its 1 to 4 clock hours.

**Reach.** 4 clock hours at 15 blocks a second is $15 \times 200 = 3000$ blocks, plus $3 \times 400$: **4200 blocks**. Cells starting on day $D-1$ can be alive on day $D$ only if they started on a front late in the day, so days $D-1$ and $D$ are scanned.

### Tracks: the steering rules

All in `float64_t`. Each step reads the current sample's position $\vec p_k$ and time $t_k = t_{\text{spawn}} + 0.25k$. Write $\varphi$, $s_h$, $e$ and $\psi$ for that place and day, and **poleward** $\hat P = (0,\ e\,s_h)$ (phase 04: in a mirrored strip, poleward is $-y$ north of the equator).

**Hurricanes**, with speed scale $c$ (0.85 to 1.15) and turn latitude $\psi_{\text{turn}}$ (20° to 28°):

- **Stage 0, westward:** $\vec v = c \cdot 3500 \cdot \big((-1, 0) + 0.15\,\hat P\big)$. When $\lvert\psi\rvert \ge \psi_{\text{turn}}$, stage 1 starts, at that step's time $t_{\text{turn}}$.
- **Stage 1, turning, for 2 days:** with $q = S\big((t_k - t_{\text{turn}})/2\big)$, the heading angle in the local frame goes from west-poleward to east-poleward through due poleward: $a = (1 - q)\,a_W + q\,a_E$ with $a_W = \pi - \arctan 0.15 = 171.47°$ and $a_E = \arctan 0.3 = 16.70°$, speed $c\,(3500 + 2500\,q)$, and $\vec v = \text{speed}\cdot(\cos a,\ \sin a\cdot e\,s_h)$. At $q = 0.5$ the heading is $94.1°$: nearly due poleward.
- **Stage 2, eastward:** $\vec v = c \cdot 6000 \cdot \big((1, 0) + 0.3\,\hat P\big)$.
- **Weakening:** at each sample, over land (mask height $> 0$ at $\vec p_k$) or over sea with $T_{\text{sea},0} < 26$, the next sample's `weaken` is this one's times $0.5^{0.25} = 0.8409$: halving per day. Otherwise unchanged.
- **Death:** the track ends at the first sample where $\lvert A\rvert = \lvert A_{\text{peak}}\rvert\,g(\tau)\,\text{weaken} < 5$ hPa after the first third of life, or when $\tau$ reaches 1.

**Lows:** $\vec v = \text{speed}\cdot\big((1, 0) + 0.15\,\hat P\big)$. **Highs:** $\vec v = \text{speed}\cdot\big((1, 0) - 0.1\,\hat P\big)$. Their strength is $A_{\text{peak}}\,g(\tau)$, and their track ends at $\tau = 1$.

**Drift stops.** The poleward part is 0 where $\lvert\varphi\rvert > 85°$, and the equatorward part is 0 where $\lvert\varphi\rvert < 5°$. So no track reaches a pole line (where $e$ flips and "poleward" would reverse every step) and no high reaches the equator.

**Why a hurricane never comes within 5° of the equator.** It spawns only where $s_{\text{hur}} > 0$, which needs $\lvert\varphi\rvert \ge 5°$ (6.8). Every stage's velocity has a poleward part of at least $0.15 \times 3500 \times 0.85 = 446$ blocks a day (stage 1's $\sin a$ stays above $\sin 16.7° > 0$), so $\lvert\varphi\rvert$ only grows. A test holds every sample of every track to it.

**Worked example.** A hurricane spawns at $(0, 6383)$ (15° N) on day 200 with $c = 1$ and $\psi_{\text{turn}} = 22°$. Then $\psi = 15 - 0.5 \times \delta(170) = 15 - 11.75 = 3.25°$. Moving $(-3500, 525)$ a day, $\varphi$ grows by $525/425.53 = 1.234°$ a day and $\psi$ a little less as the season turns, so it reaches $\lvert\psi\rvert = 22°$ in about 15 days: longer than its life. Most hurricanes spawned at 15° in mid-season never recurve; ones spawned near 20°, or late in the season when $\delta$ is smaller, do. The pictures show the mix (Risks).

### How a system is looked up near a point

`climate_systems_day_build` for kind $k$, point $(x, y)$, radius $r_q$, day $D$:

1. **Reach** $L_k$ (`climate_systems_reach`): the longest life times the fastest speed including drift, plus 3 times the largest radius:

| Kind | Longest life | Fastest, blocks a day | Track | $+3R_{\max}$ | $L_k$ | Squares scanned at $r_q = 0$ | Days scanned |
|---|---|---|---|---|---|---|---|
| Hurricane | 10 | $6900\sqrt{1 + 0.09} = 7204$ | 72.0 km | 12 km | 84.0 km | up to $12 \times 12$ | $D - 10 \ldots D$ |
| Low | 6 | $7000\sqrt{1 + 0.0225} = 7078$ | 42.5 km | 21 km | 63.5 km | up to $9 \times 9$ | $D - 6 \ldots D$ |
| High | 14 | $2500\sqrt{1.01} = 2512$ | 35.2 km | 36 km | 71.2 km | up to $7 \times 7$ | $D - 14 \ldots D$ |
| Thunder | 1/6 | $15 \times 1200 = 18\,000$ | 3.0 km | 1.2 km | 4.2 km | up to $10 \times 10$ | $D - 1 \ldots D$ |

2. **Squares:** every $(s_x, s_y)$ whose square $[s_x Q_k, (s_x + 1) Q_k) \times [s_y Q_k, (s_y + 1) Q_k)$ has a point within $L_k + r_q$ of $(x, y)$: for $s_y$ from $\lfloor (y - L_k - r_q)/Q_k \rfloor$ to $\lfloor (y + L_k + r_q)/Q_k \rfloor$, the same for $s_x$, skipping squares whose nearest point is further. Scanned south row first, x fastest.
3. **Days:** oldest first.
4. **Each candidate:** draws, thinning, track (big systems: integrate from spawn to death, at most 57 samples; cells: analytic), then keep it if at some moment of $[D, D + 1)$ the distance from its centre to $(x, y)$ is at most $r_q + 3R$. For big systems that's checked at every sample in the day and both samples bracketing it, with the segment's closest approach; for cells, the closest approach of a straight segment.
5. **Cells come after big systems**, because front and high thinning read the big systems' list. So when cells are asked for, big systems are always found too, with $r_q$ at least the cells' radius plus 4.2 km.
6. Sort as in "the list order", into the arena.

**Cost** (Issues found 2 compares the master plan's estimate). At $r_q = 0$: about $144 \times 11 + 81 \times 7 + 49 \times 15 = 2886$ square-day counts for big systems, each one hash, an `exp`-free compare against a precomputed $e^{-\lambda_{\max}}$, and usually nothing else. Candidates: about $1584 \times 0.02 + 567 \times 0.08 + 735 \times 0.03 = 99$; each costs a few rate checks (hurricanes: 5 mask reads) and, if kept, a track of up to 57 steps (hurricanes: one mask read a step). Mask pages in the keep square (128 km, phase 03) are cached after their first build, so a warm call reads them; a cold call may build a few dozen private pages. The bench measures both.

### Per-day recompute and interpolation

Whoever needs systems builds **one list per day per area** and reads it all day: phase 07's census (a list per tile per day), phase 08's sim (one around the player, rebuilt when $D$ changes), the Atlas (one for its view and day). Reading any moment of day $D$ interpolates between samples, as in Background. Because the list is a pure function, rebuilding it mid-day, or on another thread, gives the same bytes. Building the list for day $D + 1$ re-integrates each system from its spawn, so every system's samples are identical in both lists: **a track is the same whichever day it's first asked for**.

Phase 06 itself wires no list into the game. It provides the function, the fields, the layers and the bench.

### Fields: pressure, wind, rain

All in `run/src/climate/systems/field/`, read-only over a day list, any thread, no allocation. For each big system alive at $t$, $\vec c$ its centre from `climate_system_state_at`, $r = \lvert(x, y) - \vec c\rvert$, and nothing added past $3R$.

- **Pressure:** $A(t)\,e^{-r^2/R^2}$. Cells: the same with their own $A$, $R$ and life curve.
- **Wind:** 7.3's profile, times a taper $1$ for $r \le 2R$, $S\big((3R - r)/R\big)$ from $2R$ to $3R$; direction $\sigma\,(-\hat r_y, \hat r_x)$ with $\sigma = s_h\,e$ at the system's centre, negated for highs. At $r < 1$ block, 0. Cells add no wind here (gusts are phase 08).
- **Rain**, mm an hour:
  - **Hurricane:** $I = 40\,\lvert A\rvert/90$. Inside the eye, $I\,(r/r_m)^2$: nearly dry at the centre. Outside, $I\,(r_m/r)^{0.5}$ times the band factor $b = 0.5 + 0.5\cos\big(3\theta - 2.5\,\sigma\ln(r/r_m) + 2\pi\,n\big)$, where $\theta$ is the angle from the centre and $n$ is the system's `id` scaled to $[0, 1)$: three spiral arms that wind inward in the system's own turning sense. (The master plan says "from a noise on angle and distance"; a cosine spiral is that noise's simplest deterministic form, this phase's choice.) Past $R$, faded by the taper.
  - **Low:** the shield, $I_s\,g(\tau)\,e^{-\lvert(x,y) - \vec a\rvert^2/(0.6R)^2}$ with $\vec a = \vec c + 0.5R\,\hat h$ ahead of the centre along its heading $\hat h$. Plus the front: $3\,g(\tau)\,e^{-d_f^2/400^2}$ with $d_f$ the distance to the front segment.
  - **Thunder cell:** $I_{\text{peak}}\,g(\tau)\,\max(0,\ 1 - r^2/R^2)$.
- **The front** of a low: the segment from $\vec c$ to $\vec c + L_f\,\hat f$, with $\hat f$ the equatorward direction turned 30° toward the west: $\hat f = (-0.5,\ -0.866\,e\,s_h)$. "On the front" means within 400 blocks of the segment.
- **The high's cut:** $0.2$ if $r < R$ of any alive high, else 1 (matching phase 07's "under a high's footprint ($r < R$)").
- **$f_{\text{sys}}$:** $0.3 + 0.2\,S\big((\lvert\varphi\rvert - 20)/15\big)$: 0.3 in the tropics, 0.5 from 35°.

**Worked examples** (tests): a hurricane of $A = -60$, $R = 2500$, $r_m = 100$ at full strength: pressure $-46.73$ at 1250 blocks; wind 51.5 at 100 blocks and 12.94 at 1000; eyewall rain $40 \times 60/90 = 26.7$ mm an hour at $r_m$ times its band factor.

### Rain calibration (7.4)

Master plan 7.4: background rain is the normal times $(1 - f_{\text{sys}})$ times a noise factor; systems bring the rest. Phase 07 builds the noise. This phase holds the **expected** total:

$$\text{rain over 10 years} = \sum_{\text{hours}} \Big[\tfrac{\bar P(d)}{7 \cdot 24}\,(1 - f_{\text{sys}}) + R_{\text{sys}}(x, y, t)\Big]\cdot F_{\text{high}}(x, y, t)$$

and requires it to be within 15% of $\sum_{\text{hours}} \bar P(d)/(7 \cdot 24)$, the normal. If it's off, **spawn rates are tuned, not normals** (7.4): `CLIMATE_THUNDER_RATE_GAIN` first (cells dominate the tropics), then the low and high rates. Each tuned value is recorded under Commits and in this file with the before and after measurement.

### Layers

Added to `climate_layer_t` (phase 03), drawn by a new function in `run/include/climate/layer/index.h`:

```c
/* A "now" layer at time t from a day list built for floor(t), centred where the picture is. Grey where a tile is not built,
 * with systems over grey drawn as outlines only, so storms show before they reach explored land (master plan 2.4). */
void climate_layer_draw_now(const climate_t *climate, const climate_systems_day_t *day, climate_layer_t layer,
                            float64_t t, int32_t west_x, int32_t north_y, int32_t blocks_per_pixel, uint32_t width,
                            uint32_t height, uint8_t *pixels, uint32_t pitch);
```

| Layer | Drawn as |
|---|---|
| `CLIMATE_LAYER_PRESSURE_NOW` | Phase 04's pressure colours of belt + heat low + systems, a dark line every 4 hPa, and a 5 by 7 pixel **H** (`0x2040C0`) or **L** (`0xC02020`) at each big system's centre, from a bitmap in the layer code (the climate cannot use `ui/font`: `ui` ranks above it) |
| `CLIMATE_LAYER_SYSTEMS` | Phase 03's water layer underneath. Each big system: a circle at $R$ (hurricanes red `0xFF3030`, lows orange `0xFF9020`, highs blue `0x3080FF`), a filled dot at $r_m$ for hurricanes, the track behind it as a white `0xFFFFFF` polyline from its spawn to now, and a low's front as a blue `0x2050FF` line with a tooth every 16 pixels. Over grey land: the circle and track only |
| `CLIMATE_LAYER_RAIN_RADAR` | Rain now: the background's expected rate plus systems, times the high's cut, mm an hour: 0.1 `0x80C0FF`, 1 `0x40A040`, 4 `0xE0E040`, 10 `0xF08020`, 25 `0xE02020`, 50 `0xC020C0`; under 0.1 transparent over phase 03's water layer |
| `CLIMATE_LAYER_THUNDER` | Each alive cell a disc at $R$ in `0xC0C0FF`, with a yellow `0xFFE020` 3-segment zigzag (the lightning mark) at its centre |
| `CLIMATE_LAYER_WIND_NOW` | Phase 04's wind layer, with each system's circling wind added |

Names are not drawn in pictures (no font); the bench writes them beside the frames in `systems.txt`, and the Atlas (phase 09) draws them with `ui/font`.

### Threads, memory and determinism

| Thread | What it does here |
|---|---|
| Any | `climate_systems_day_build`, with its own arena: a worker's Job arena, a test arena, the sim's tick arena. Reads the climate (read-only after `climate_open`) and the mask cache (phase 03's protocol) |
| Any | Field functions, `climate_system_state_at`, names, layers: read-only, no allocation |

**Memory per list:** big systems $128 \times 1448 = 185$ KiB, cells $8192 \times 64 = 512$ KiB, private mask pages $256 \times 4 = 1$ KiB each. The builder takes $(\text{capacity})$ up front and the list's count tells what was used.

**Determinism:** every number comes from the hash streams and fixed-order `float64_t` arithmetic; the scan order is fixed; the list is sorted by a total order on keys that don't depend on the scan; field sums add in list order. Mask heights are pure (phase 03), cached or not. So 1 worker or 16, cached or cold, the list's bytes are the same. The release preset must give the same digest as debug (libm's `cos`, `sin`, `exp`, `log` are called the same way on every preset).

### Edge cases

| Case | What happens |
|---|---|
| Negative coordinates | Squares by `floor`, so $x = -1$ is square $-1$ and $x = -16\,384$ is square $-1$, $-16\,385$ is $-2$. Tested |
| Equator crossing | Forbidden for hurricanes by construction (poleward part always positive) and tested. Highs' equatorward drift stops inside 5°. Lows never get near (they spawn at 35° to 65°) |
| Tracks reaching a pole line | Poleward drift stops past 85°, so none does. At 85° a low is 2128 blocks from the pole |
| Latitude repeat, mirrored strips | Everything uses $\lvert\varphi\rvert$, $s_h$ and $e$ at the system's place. A mirrored strip's hurricanes go "poleward" toward the pole at $y = 38\,298$, so $-y$ there. Tested with spin directions at $y = 57\,447$ (45° N, $e = -1$) |
| A system spawned in one strip that drifts into the next | Can't: the only strip boundaries are the equator and the poles, and both are guarded |
| Land never generated | Big systems read only the coarse mask (phase 03's height estimate), which exists everywhere. Cells need tiles; their list says so with `thunder_complete` |
| A hurricane born over a big lake | The open-sea test (water 2048 blocks round on all four sides) rejects lakes under about 4 km across; bigger lakes count as sea for spawning |
| A list asked at a huge radius | Capacities cap it; `overflowed` is set and tests hold it at 0 for every radius phases 07 to 09 use (up to 40 km for big systems, 8 km for cells) |
| Time before the world's first day | $D$ may be any `int64_t`; days before 0 still hash (the hash takes `int32_t`, and $D$ near 0 is far from its limit) |

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/climate/systems/`, `run/{src,include}/climate/systems/field/`, `run/{src,include}/climate/systems/name/` (NEW, each with `README.md` and one `index.*`)
- `run/{src,include}/climate/mask/`: only `climate_mask_height_at`, if absent
- `run/{src,include}/climate/layer/`: the five layers and `climate_layer_draw_now`
- `run/{src,include}/climate/index.{c,h}` and `run/src/climate/README.md`: the four kind seeds in `climate_t`, `climate_systems_hooks_set`
- `tests/run/climate/systems/`, `tests/run/climate/systems/rain/` (NEW, the latter with `test.cmake`); `tests/run/climate/layer/` (added tests); `tests/run/climate/fixtures/ground.h`
- `bench/climate/` (`--systems`, `--season`)

**Wiring outside them:** `plans/REPO_STRUCTURE.md`, rows for `climate/systems`, `climate/systems/field`, `climate/systems/name` and the two tests. No game file, no terrain file.

## Steps

Build order. Each step builds and passes its own tests before the next starts.

1. **Read what phases 01 to 05 built.** Find the day count, latitude, declination, the mirror sign, the mask's reads, `climate_air_at`, the normals splines, `climate_normals_at` and `climate_season_hurricane_s`. Write any name that differs under Issues found. *Why: this phase reads all of them, and must not compute any a second way.*
2. **Add `climate_mask_height_at`** to `run/src/climate/mask/` if phase 03 has no point read. Test in `run_climate_systems`: it equals the window's height at the same cell, cached or not, on a synthetic ground. *Why: tracks and spawn checks read the mask at any point, with no tile.*
3. **Create `run/src/climate/systems/` with the kinds, constants and structs**, their asserts, and the README. Add the four kind seeds to `climate_t` and set them in `climate_open`. *Why: everything else uses them.*
4. **Write the counts:** $u$ from the hash, the inverse CDF with its cap, the precomputed $e^{-\lambda_{\max}}$, and the 12-draw stream. Test `poisson_counts_follow_their_rate`. *Why: this is the "probability thing" Ahmed asked for, and it must be exactly Poisson before anything rests on it.*
5. **Write the three big-system rates and thinning:** $T_{\text{sea},0}$, the open-sea and interior checks, the seasonal low rate, the hooks. Tests: the hurricane count with the hook, in-season only, never within 5°. *Why: 7.2's table.*
6. **Write the tracks:** the three hurricane stages, lows, highs, drift stops, weakening, death, the life curve, `climate_system_state_at`. Tests: turning, weakening, the pole, the same track from any day. *Why: 7.3's steering.*
7. **Create `run/src/climate/systems/name/`**: the syllable table and both functions. Test the names. *Why: 7.2's names, and phase 07 and 10 show them.*
8. **Write `climate_systems_day_build` for big systems**: reach, squares, days, the in-day check, the sort. Tests: list order, 1 and 16 workers, overflow never set. *Why: this is what phases 07 to 09 call.*
9. **Create `run/src/climate/systems/field/`**: pressure, wind, rain, the front, the high's cut, $f_{\text{sys}}$. Tests: the bell, the wind profile, the spin directions in all four strip-hemisphere cases. *Why: 7.3's pressure, wind and rain.*
10. **Add thunderstorm cells** to the builder: the rate from normals, the two processes, front and high thinning, motion, reach. Tests: the rate's worked examples, start hours, the high's 20%. *Why: 7.2.1.*
11. **Add the five "now" layers** and `climate_layer_draw_now`. Tests in `run_climate_layer`. *Why: Ahmed checks by looking.*
12. **Extend `bench/climate/`**: `--systems` times the big-system list warm and cold and the cells separately; `--season <dir> <seed> <first_day> <last_day> <step_days>` writes frames of `pressure_now`, `systems`, `rain_radar` and `thunder` with a `systems.txt` of names, spawn days, peaks and tracks. *Why: the 2 ms budget, and the gate.*
13. **Write `run_climate_systems_rain`**, run it on release, and **tune** the marked rates until every test cell is within 15%. Record each change and its measurements here and under Commits. *Why: 7.4. Expect `CLIMATE_THUNDER_RATE_GAIN` to come down by about 10 times (Issues found 4).*
14. **Update `plans/REPO_STRUCTURE.md`** and every new `README.md`.
15. **Run the whole debug suite**, `standards_lint`, `clang_format`, and the release digest check.
16. **Draw the gate frames**, show Ahmed, and tick the todo as each item lands.

## Todo

- [x] Phases 01 to 05's names found; differences written under Issues found
- [x] `climate_mask_height_at` in `run/src/climate/mask/`, if absent: absent, and added in the Owner instead (`mask/` cannot see `climate_t`), with `climate_mask_page_read`, `climate_mask_page_kept` and `climate_mask_point` (Issues found 9, 17)
- [x] `run/src/climate/systems/`: kinds, constants, `climate_system_t` (1448 bytes), `climate_thunder_t` (64), `climate_systems_day_t` (72), asserts, kind seeds in `climate_t`
- [x] Spawn by hash per square and day (7.2): inverse-CDF Poisson counts, the 12-draw stream, thinning by place and season
- [x] Rates: hurricanes by $s_{\text{hur}}$ on open sea, lows by season in 35° to 65°, highs in 20° to 45° and winter interiors
- [x] Tracks (7.3): hurricanes west, turn, east; lows and highs east with drift; drift stops; weakening over land and cool sea; life curves; death
- [x] Names from syllables, in `run/src/climate/systems/name/`
- [x] The daily list of systems near a point, sorted, and interpolation between samples within the day
- [x] Pressure, wind and rain from each system (7.3), fronts, the high's cut, $f_{\text{sys}}$, in `run/src/climate/systems/field/`
- [x] Thunderstorm cells (7.2.1): rate from normals, afternoon and front processes, high thinning, straight-line motion
- [x] Map layers: pressure now with H and L, systems with tracks, rain radar, thunderstorm cells, wind now; systems over grey as outlines
- [x] Tests: hurricanes only spawn in season and never within 5° of the equator; they turn poleward and weaken over land; winds circle lows anticlockwise in the north and clockwise in the south; a system's track is the same whichever day it's first asked for; cells under a high's centre over 1000 days (Issues found 5)
- [x] Test: over 10 years on 8 seeds, each cell's rain is within 15% of its normal (7.4); rates tuned and recorded
- [x] Bench: the systems near a point, under 2 ms
- [x] 1 and 16 workers give the same bytes; release prints the same digest as debug
- [x] A `README.md` in every new source directory, exactly one `index.*` in each
- [x] `plans/REPO_STRUCTURE.md` updated
- [x] The whole debug suite passes; `standards_lint` and `clang_format` pass
- [ ] Gate: Ahmed watches a hurricane season go by in the frames

## Tests

Names in the repo's sentence style. "Fails if" says what a wrong build looks like. Tests on synthetic ground open the climate with phase 03's `climate_open_ground`.

### `run_climate_systems`

- **`poisson_counts_follow_their_rate`.** The inverse CDF by hand: $\lambda = 0.02$ gives 0 at $u = 0.5$, 1 at $u = 0.985$, 2 at $u = 0.9999$; $\lambda = 9$ gives 9 at $u = 0.5$. Then $10^6$ square-days of real hashes at $\lambda = 0.02$, $0.08$ and $9$: the mean within 1% of $\lambda$, the variance within 2% of $\lambda$ (a Poisson's variance equals its mean), and the share of zeros within 0.1 points of $e^{-\lambda}$. Fails if $u$ is built from the wrong bits (the shares drift) or the loop compares the wrong way.
- **`the_machinery_gives_the_plan_s_2_4_hurricanes_a_season`.** Hook: $s_{\text{hur}} = 1$ from day 150 to day 269 (120 days), 0 otherwise; all-sea ground. 2000 squares for one season: mean kept spawns per square $2.40 \pm 0.15$ (the standard error is $\sqrt{2.4/2000} = 0.035$). Prints the real-world count for the same squares with the hook off. Fails if thinning or the rate is off by any factor.
- **`hurricane_season_sea_is_32_with_a_warm_current_and_27_95_without`.** $T_{\text{sea},0}$ at 15° N on day 215: $27.95 \pm 0.01$ and $s_{\text{hur}} = 0.725 \pm 0.005$. With the hook's current at $+4$: $31.95 \pm 0.01$, $s_{\text{hur}} = 1$, and equal to phase 05's `climate_sea_temperature` on a sea cell with a full warm current to $10^{-4}$. Fails if the spawn rate reads the wrong spline or the wrong day.
- **`hurricanes_only_spawn_in_season_and_never_within_5_degrees`.** All-sea ground, 20 years, a band of squares from $y = -32\,768$ to $32\,768$, 8 squares wide. Every kept hurricane: $s_{\text{hur}} > 0$ at its start; $5 \le \lvert\varphi\rvert \le 25$ at its start; and **every sample of every track** has $\lvert\varphi\rvert \ge 5$. On day 32 at 15° N with no current ($s_{\text{hur}} = 0$) none start in squares wholly between 10° and 20°. Fails if a track's velocity ever points equatorward.
- **`hurricanes_turn_poleward_and_then_east`.** All-sea ground, every hurricane over 20 years that reaches stage 2: in stage 0, $u < 0$ and the poleward part $> 0$ at every step; one day after $t_{\text{turn}}$, the heading is within 15° of due poleward; two days after, $u > 0$. In the south and in a mirrored strip, "poleward" in world terms is $-y$ and $-y$ respectively ($y = -19\,149$; $y = 57\,447$). Fails if $e$ or $s_h$ is dropped from $\hat P$.
- **`hurricanes_weaken_by_half_a_day_over_land`.** Ground: sea, with a land square 40 km across. A hurricane forced (hook) to spawn so it crosses the land in the flat third of its life: `weaken` after 4 samples on land is $0.5 \pm 10^{-6}$ times its value before; over sea at 15° in season it stays. Also over sea below 26 °C (late in the season at 25°): halves the same way. Fails if the factor is per sample instead of per day.
- **`winds_circle_lows_anticlockwise_in_the_north_and_clockwise_in_the_south`.** For a low of $A = -20$, $R = 5000$ placed by hand, and a high of $+14$, at $y = 19\,149$ (45° N), $y = -19\,149$ (45° S) and $y = 57\,447$ (45° N in a mirrored strip, $e = -1$): the wind at the four points $r_m$ east, north, west and south of the centre. North, low: east point blows $+y$, north point $-x$ (anticlockwise; the 2D cross product $\hat r \times \vec w > 0$ at all four). South, low: all four $< 0$. High: the reverse of each. Mirrored north: the world-frame sense is the reverse of the real north (phase 04 Issues found 4's default). Speed at $r_m$: $8.000 \pm 10^{-4}$ for the low, $5.600 \pm 10^{-4}$ for the high. Fails if the sign misses $s_h$, $e$ or the high's flip; each failure breaks exactly one case.
- **`the_hurricane_wind_and_pressure_are_the_worked_examples`.** $A = -60$, $R = 2500$, $r_m = 100$, full strength: $v_{\max} = 51.5$; wind 25.75 at 50, 51.5 at 100, $12.94 \pm 0.01$ at 1000; pressure $-46.73 \pm 0.01$ at 1250, $-22.07 \pm 0.01$ at 2500; nothing past 7500. At $A = -90$: $v_{\max} = 70$. At $A = -15$: $16.5$, titled "Tropical storm".
- **`a_system_s_track_is_the_same_whichever_day_it_s_first_asked_for`.** Fresh climate A builds the list for day 1005 only. Fresh climate B builds days 1000 to 1005 in order. For every big system in A's list, B's day-1005 list has the same `id`, and `memcmp` of the two `climate_system_t` is 0. Then the same with B going backwards. Fails if anything carries over between days or depends on what was asked first.
- **`one_and_sixteen_workers_give_the_same_lists`.** 64 places on a real seed, days 0 to 99: lists built on 1 worker in order and on 16 workers as jobs. `memcmp` equal. Then a **pin**: an FNV-1a digest of all the lists for seed `0x1234567890ABCDEF`, as `run_terrain_density` pins its blocks, with a comment saying any change to a rate, a draw's order, a steering rule or a constant here moves it on purpose.
- **`cells_under_a_high_s_centre_are_a_fifth_of_the_rate_over_1000_days`.** Hooks: a high placed by hand, sitting still at a place where $\lambda_{\text{base}} = 9$ (synthetic normals), for 1000 days. Cells started within $0.3R$ of its centre, per km² per day: $1.8 \pm 0.1$; the same place with the high removed: $9.0 \pm 0.2$. The master plan's todo asks for "no cells ... under a high's centre in 1000 days"; see Issues found 5 for why the formula gives 20%, and the option that makes it 0.
- **`thunderstorm_rate_is_the_worked_example`.** At a synthetic cell with $T_{\text{day}} = 30$, $\bar P = \bar P_{\max}$, monsoon today, $G = 1$: $\lambda_{\text{base}} = 9.000$. Measured over $10^5$ square-days: afternoon cells $9.00 \pm 0.05$ a day, front cells on a fixed front $36.0 \pm 0.2$, under a fixed high $1.80 \pm 0.02$. $T_{\text{day}} = 15$: exactly 0 cells.
- **`cells_start_in_the_afternoon_unless_on_a_front`.** Every afternoon cell starts between 13:00 and 19:00 ($f \in [7/24, 13/24]$); front cells spread over the whole day (each 4-hour bin within 20% of a sixth).
- **`cells_move_with_the_day_s_wind_and_never_faster_than_15`.** Velocity equals phase 04's wind at the start at noon times 1200, capped at $18\,000$ blocks a day.
- **`a_cell_s_speed_is_capped_at_15_blocks_a_second`** (added, Issues found 19). `climate_systems_thunder_velocity` at winds of 10, 15, 20 and 40 blocks a second and 0: the wind times 1200 under the cap, 18,000 blocks a day with its direction kept over it.
- **`names_are_made_from_syllables`.** Seed 4161 gives "Tavela", 262 gives "Orin". Over $10^6$ seeds no name is over 8 letters, every name starts upper case, and the longest title fits in 24 bytes with its zero.
- **`lows_carry_a_front_toward_the_equator`.** A low at 45° N: the front's far end is south-west of the centre; at 45° S, north-west; the "on front" test is true 300 blocks from the line and false at 500.
- **`no_track_reaches_a_pole_and_no_high_reaches_the_equator`.** 20 years of all kinds on all-sea and all-land grounds, every sample: $\lvert\varphi\rvert \le 85° + 0.25 \times 7204/425.5$ (one step past the stop), highs $\ge 5° - $ one step.
- **`negative_coordinates_find_their_squares`.** $x = -1$ and $x = -16\,384$ are hurricane square $-1$; $-16\,385$ is $-2$; a list at $(-20\,000, -20\,000)$ equals one built from its squares by hand.
- **`lists_never_overflow`.** 1000 random places and days on 4 seeds, big systems at 40 km and cells at 8 km: `overflowed` is 0, and the largest counts are printed.
- **`the_mask_point_read_matches_the_window`.** Cached, claimed and private reads give the same height as phase 03's window at 1000 cells.

### `run_climate_systems_rain`

- **`ten_years_of_rain_are_within_15_percent_of_normal_on_8_seeds`.** For 8 seeds, each on its real ground with a test box of 4 by 4 km (16 tiles built), centred at a different latitude per seed: 0°, 10°, 15°, 25°, 35°, 45°, 55°, 65° ($y = 0$, 4255, 6383, 10\,638, 14\,894, 19\,149, 23\,404, 27\,660; the nearest land box to each if one exists within 20 km, else the sea there). In each box, 16 test cells. For each day of 10 years, one list for the box (big systems at 2 km beyond the box, cells at the box), and 96 samples a day (every 15 minutes) of the expected total rate at each test cell. Each cell's 10-year total within 15% of its normal. Prints, per seed and cell: normal, total, the share from each kind, the number of events of each kind, and an estimate of the sampling noise ($\sqrt{n}/n$ of the cell events). Fails, as built from the master plan's rates, in the tropics (Issues found 4); step 13 tunes until it passes. `test.cmake` sets `MC_TEST_TIMEOUT 3600`.

### `run_climate_layer` (added)

- **`each_now_layer_is_the_colour_the_readme_says`.** Known pixels on a synthetic day list: an H at a high's centre, an L at a low's, a red circle at a hurricane's $R$, 25 mm an hour in `0xE02020`.
- **`systems_over_grey_land_are_outlines`.** A hurricane over unbuilt tiles: grey pixels except its circle and track.
- **`a_now_layer_picture_is_held_to_a_hash`.** Each at 512 by 512 on the real seed at two moments; hashes set only after Ahmed has looked.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_climate_systems$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_climate_layer$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_climate_.*$' -E '^run_climate_systems_rain$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -E '^run_climate_systems_rain$'
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 3700 ctest --preset release -R '^run_climate_systems_rain$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^run_climate_systems$' --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate --systems
    tools/test-or-build.sh timeout 900 build/release/bin/bench_climate --season /tmp/mc_c_season 2198175988018515243 150 300 0.5
    pgrep -a minecraft_c    # must print nothing

The 10-year test runs on release, where it takes minutes rather than an hour; it is excluded from the debug suite run only for time, and still registered there. The release `run_climate_systems` must print the same digest as debug.

**The bench** (`bench/climate/index.c`, `--systems`), on release, with its README stating these:

- **Big systems near a point, warm:** mask pages already cached. 100 places, days, best of 3 each. **Exit 1 if the median is over 2 ms** (master plan 7.3).
- **Big systems, cold:** a fresh climate each time. Reported.
- **Cells near the player** at 2 km: reported.
- Prints counts hashed, candidates, kept, track steps and mask reads per call.

### Results, 2026-11-06

Every command above was run through the build lock in the worktree, apart from Ahmed's gate:

| Command | Result |
|---|---|
| `cmake --build --preset debug` | built, `standards_lint` clean |
| `ctest --preset debug -R '^run_climate_systems$'` | passed, 18 tests |
| `ctest --preset debug -R '^run_climate_layer$'` | passed, 15 tests (3 new) |
| `ctest --preset debug -R '^run_climate_.*$' -E '^run_climate_systems_rain$'` | 13 of 13 passed |
| `ctest --preset debug -R '^(standards_lint|clang_format)$'` | both passed |
| `ctest --preset debug -E '^run_climate_systems_rain$'` (the whole suite) | 137 of 137 passed, 2017 s |
| `cmake --build --preset release` | built and linked (`-flto`, the frame limit at link): `climate_t` is unchanged at 64 bytes, so main's frame is not touched |
| `ctest --preset release -R '^run_climate_systems_rain$'` | passed: 0 of 128 cells outside 15%, worst 0.140; 5 sites ran 30 years under the noise rule; 17 s |
| `ctest --preset release -R '^run_climate_systems$'` | passed; digest `0x69B94D8AD6C147C9`, the same as debug's and pinned |
| `bench_climate --systems` | exit 0: warm median **0.357 ms** (slowest 0.967), target 2 ms; cold median 33.8 ms; with cells at 2 km 23.8 ms, 9.8 cells a list. Per call 2191 square-days hashed, 135.6 candidates, 25.4 kept, 587 track steps, 599 mask reads |
| `bench_climate --season /tmp/mc_c_season 2198175988018515243 150 300 0.5` | exit 0: 1204 frames and `systems.txt` in 203 s; view round (-67232, 7446); on average 10.8 lows, 2.0 highs and 465 cells near the view a day, **no hurricanes** (Issues found 14). Folder deleted after checking (3.6 GB in `/tmp`'s memory) |
| `pgrep -a minecraft_c` | nothing |

Measured along the way (`run_climate_systems`, release): the inverse CDF over 10^6 hashes, mean 0.01991, 0.08006 and 9.004 at λ 0.02, 0.08 and 9; the forced 120-day season 4.125 hurricanes a square at 0.034 (2.361 at 0.02); the real all-sea count 1.02 a square a year; 15° N on day 215, 27.950 °C and `s_hur` 0.7250, with +4, 31.950 and 1; 20 years on 32 squares of sea, 321 hurricanes, lowest sample 5.04°; hurricane worked example 12.936 at 1000 blocks, -46.728 and -22.073 hPa; a hurricane over land 0.5000001 after 4 samples; afternoon cells 8.9985 a square-day at λ 9, 1.8003 under a high, 36.016 on a front; within 0.3 R of a high's centre 1.794 per km² a day against 8.969 without; the largest lists 17 big systems at 40 km and 567 cells at 8 km; 6400 lists in 4.2 s on 1 worker and 0.69 s on 16.


### Results rerun, 2026-11-06 (second agent, worktree `wf_6f8a0c68-47a-30`, after the cap test of Issues found 19)

| Command | Result |
|---|---|
| `cmake --build --preset debug` | built, `standards_lint` clean |
| `ctest --preset debug -R '^run_climate_systems$'` | passed, 19 tests |
| `ctest --preset debug -R '^run_climate_.*$' -E '^run_climate_systems_rain$'` | 13 of 13 passed (`run_climate_layer` among them) |
| `ctest --preset debug -R '^(standards_lint|clang_format)$'` | both passed |
| `ctest --preset debug -E '^run_climate_systems_rain$'` | 137 of 137 passed, 1902 s |
| `cmake --build --preset release` | built and linked, frame check included |
| `ctest --preset release -R '^run_climate_systems_rain$'` | passed: 0 of 128 cells outside 15%, worst 0.140, 17 s |
| `ctest --preset release -R '^run_climate_systems$'` | passed, 19 tests; digest `0x69B94D8AD6C147C9`, the pin, unchanged |
| `bench_climate --systems` | warm median 0.363 ms (target 2 ms, met), cold 34.5 ms, cells at 2 km 23.2 ms |
| `bench_climate --season /tmp/mc_c_season 2198175988018515243 150 300 0.5` | exit 0, 301 moments in 203 s, 0 hurricanes (Issues found 14); folder deleted |
| `pgrep -a minecraft_c` | nothing |


### Gate check, 2026-11-06 (skeptical checker, worktree `wf_6f8a0c68-47a-32`, main merged at `ca8ead0d`)

Every command above rerun in a clean build of this worktree, through the build lock:

| Command | Result |
|---|---|
| `cmake --build --preset debug` | built, `standards_lint` clean, no recursion |
| `ctest --preset debug -R '^run_climate_.*$' -E '^run_climate_systems_rain$'` | 13 of 13 passed (`run_climate_systems` 19 tests, `run_climate_layer` among them) |
| `ctest --preset debug -R '^(standards_lint|clang_format)$'` | both passed |
| `ctest --preset debug -E '^run_climate_systems_rain$'` | 137 of 137 passed, 1924 s |
| `cmake --build --preset release` | built and linked (LTO, frame limit), `standards_lint` clean |
| `ctest --preset release -R '^run_climate_systems_rain$'` | passed: 0 of 128 cells outside 15%, worst 0.140, 0 failed days |
| `ctest --preset release -R '^run_climate_systems$'` | passed; digest `0x69B94D8AD6C147C9`, equal to debug's and the pin |
| `bench_climate --systems` | exit 0: warm median 0.354 ms (target 2 ms), cold 33.0 ms, cells at 2 km 24.3 ms |
| `bench_climate --season /tmp/mc_c_season 2198175988018515243 150 300 0.5` | exit 0, 301 moments, 1205 files, 3.6 GB, 204 s; 0 hurricanes; folder deleted |
| `pgrep -a minecraft_c` | nothing |

Worked examples run through the real code by a separate program linked against the debug objects (not a test): hurricane $A = -60$, $R = 2500$, $r_m = 100$ gives $v_{\max} = 51.5$, wind 25.75 at 50, 51.5 at 100, 12.936 at 1000; pressure -46.728 at 1250, -22.073 at 2500, 0 at 7600; eyewall peak 26.667 mm an hour; $v_{\max}(-90) = 70$, $v_{\max}(-15) = 16.5$, "Hurricane Tavela". A low of -20 at 45° N blows $(0, +8.000)$ at $r_m$ east, at 45° S $(0, -8.000)$. $\psi(15°, 200) = 3.2501$ (the plan's 3.25). The Tracks worked example, from $(0, 6383)$ on day 200 with $c = 1$ and $\psi_{\text{turn}} = 22$ on all-sea ground: $(-3500, +525)$ a day exactly, $\varphi$ up 1.234° a day, $\psi$ 13.2° by day 208, so it never recurves, as the plan says; it weakens from day 206.75 (sea below 26 °C at 23° N) and dies at sample 35. $T_{\text{sea},0}$ at 15° N on day 215: 27.950. Thunder base at 30 °C, wet, monsoon: 1.98 = 9 × 0.22. Reaches 84.0, 63.1, 71.2 and 4.2 km. All match.

Frames read (day 220 of the gate run): the systems layer shows a low's orange circle with its white track from the west and its blue front running south-west; the pressure layer shows the belts with 4 hPa lines and a red L at the low; the radar shows a low's shield and the background; the thunder layer shows cell discs. No stubs, TODOs or empty tests found in the phase's code or tests; every public function has a caller. No defect needed fixing. What is left is Ahmed's gate and Issues found 21.

## Gate

**Ahmed watches a hurricane season go by** (master plan phase 06 gate). The Atlas doesn't exist yet, so the season is a folder of frames.

`bench_climate --season /tmp/mc_c_season 2198175988018515243 150 300 0.5` picks the 64 by 64 km view with the most sea between 10° and 25° N within 100 km of spawn, and writes, every half day from day 150 to day 300 of the world's first year: `systems_d150.0.png`, `pressure_now_d150.0.png`, `rain_radar_d150.0.png`, `thunder_d150.0.png` (thunder only where tiles are built) at 64 blocks a pixel, and `systems.txt`: each hurricane's name, spawn day and place, peak, and the day it died or left the view. Stepping through `systems_d*.png` in any image viewer that walks a folder plays the season. Ahmed looks for: hurricanes forming only over warm sea in late summer, moving west and slightly poleward, some turning and heading east, fading over land; lows marching east further north with their fronts; highs sitting still and clearing the radar; thunderstorm cells popping up in the afternoons. He reads `systems.txt` for names, and says whether the count feels right (master plan Q5). The layer hashes are set only after he says yes.

## Risks and open questions

| Risk or question | Default |
|---|---|
| **Fewer hurricanes than the master plan's 2.4 per square a season** with the track-time sea temperature (Issues found 1) | Built as written; the machinery test proves 2.4 with the season forced; the bench prints the real count. If Ahmed wants more, `CLIMATE_HURRICANE_RATE` is the one constant (Q5) |
| **Most mid-season hurricanes spawned at 15° never recurve** within their life (worked example in Tracks) | As the master plan's numbers give. The frames show it. If Ahmed wants more recurving, the turn latitude range (20° to 28°) can drop, which he decides |
| **Cells will flood the tropics** at the master plan's rate (Issues found 4) | Step 13 tunes `CLIMATE_THUNDER_RATE_GAIN`; expected about 0.06 to 0.1 |
| **The 10-year test is noisy** where cells are rare and heavy | The test prints the sampling noise. If noise alone exceeds 5% at a cell, that cell runs 30 years instead of 10, and the change is recorded here |
| The cold list may be slow when many mask pages must be built privately far from the player | The bench reports it. Phase 07's census runs near generated land, where pages are cached. If cold calls matter, mask page jobs can be scheduled along known tracks, which Ahmed decides |
| Cells need built tiles, so lists far from the player have none | By design (master plan 7.2.1: "Cells only need working out where someone looks"). `thunder_complete` tells the caller |
| The high's rain cut is a hard edge at $R$ | Matches phase 07's wording. If the radar shows a visible ring, a smooth cut ($1 - 0.8\,e^{-r^2/R^2}$) is one line, which Ahmed decides at the gate |

## Commits

Appended as they land:

- `1e4f94d7` Add plan 004 phase 06's weather systems (code, tests, bench), with the tuned rates of Issues found 12 and 14: `CLIMATE_THUNDER_RATE_GAIN` 1 to 0.22, lows 0.08 and 0.04 to 0.18 and 0.09, lows' drift 0.15 to 0.08, hurricanes 0.02 to 0.034, `f_sys` 20° to 35° to 25° to 50° and 0.4 past 60° to 70°; measured before and after in Issues found 12's table (110 cells outside, then 0, worst 0.140).
- `749a25d5` List the weather systems in `plans/REPO_STRUCTURE.md`.
- `6298210e` Record the phase in this file: todos, results, the tuning and the issues found.
- `da6bfbd3` List the phase file commit in this file.
- `8b33af74` Move the cell speed cap into `climate_systems_thunder_velocity` and test it over 15 blocks a second (Issues found 19).
- `940f9efa` Record the cap test and the verification rerun in this file.
- `1b29be67` Record the gate check in this file: verification rerun, worked examples, Issues found 21.
- `97110031` (the final check) Let hurricanes form over sea at the point, at 0.3 a square-day, with their share of the rain in `f_sys` and the cells' gain 0.205, and move the tests that held the old rates (Issues found 22).

## Issues found

Found while writing this file, by checking the master plan's arithmetic against its own formulas and the other phase files. None changes anything that exists. Each has a default, built until Ahmed decides.

1. **Spawning can't read ocean currents, so the master plan's 2.4 hurricanes a season won't happen as written.**
   - **Expected** (master plan 7.2): a warm-current coast at 15° N, $s_{\text{hur}}$ near 1 for about 120 days, $0.02 \times 120 = 2.4$ per 16 km square a season.
   - **Actual:** (a) currents live in climate cells, and master plan 2.5 and 7.3 require systems to spawn and move "without working out any climate tile". So spawning uses the sea temperature without the current, $T_{\text{sea},0}$. (b) Even with the current, 6.3's currents fade within 600 blocks of land, a sliver of a 16 km square. (c) Phase 05's Issues found 2: with a full warm current the season never ends ($\sum s_{\text{hur}} = 353$, 7.1 a year), and without one it's 119 days ($\sum s_{\text{hur}} = 71.4$, 1.43 a year). (d) A 16 km square spans 38.5° of latitude, so the square from 0° to 38.5°, all sea, averages $0.58$ a year.
   - **Evidence:** master plan 2.5, 6.3, 6.8, 7.2, 7.3; the sums worked in this agent's scratch space with the repo's spline rule.
   - **Done:** spawning uses $T_{\text{sea},0}$. The test `the_machinery_gives_the_plan_s_2_4_hurricanes_a_season` forces the master plan's assumptions and checks 2.4 exactly; the real count is printed. **For Ahmed:** keep (default), or raise `CLIMATE_HURRICANE_RATE`; to reach 2.4 for a square at 15° in open sea it would be about 0.034 (master plan Q5, "tune by playing").

2. **The lookup costs more than "under 1000 hashes", but still fits 2 ms.**
   - **Expected** (7.3): "about $5 \times 5$ squares of 16 km for hurricanes and lows, $4 \times 4$ of 24 km for highs, for 14 days: under 1000 hashes".
   - **Actual:** $(25 + 25 + 16) \times 14 = 924$ is right for those squares, but the squares are too few: a hurricane can travel up to 72 km (stage 2 runs at 6 km a day, not 3.5, and the hash's speed scale goes to 1.15), and its pressure reaches 3 radii (12 km) past its centre. Honest reaches are 84, 63.5 and 71.2 km, so up to $12^2$, $9^2$ and $7^2$ squares, each scanned only over its own kind's longest life (10, 6, 14 days), about 2900 square-day hashes at a point.
   - **Done:** the honest reach is used, because a smaller one would make systems pop into existence at the edge. Each count is one hash and one compare, so 2900 is still a few microseconds; the bench holds the whole list to 2 ms.

3. **Hurricanes "with the trade winds" would be pushed onto the equator.**
   - **Expected** (7.3): hurricanes move "with the trade winds".
   - **Actual:** 6.5's trades blow $(-1, -0.4\,\operatorname{sgn}\psi)$: west **and toward the equator**. A hurricane following them would cross the 5° line and the equator, which 6.8 forbids and which real hurricanes never do (they drift west and slightly poleward).
   - **Done:** stage 0 moves west at 3500 blocks a day with a 15% poleward drift (this phase). The test holds every sample at $\lvert\varphi\rvert \ge 5°$.

4. **Thunderstorm cells at 7.2.1's rate would rain about 10 to 17 times the normal.**
   - **Expected** (7.2.1, 7.4): 9 cells a day per km² on a monsoon afternoon, and rain within 15% of normal.
   - **Actual:** one cell with a 20 to 60 mm an hour peak, a paraboloid footprint of 150 to 400 blocks, 1 to 4 clock hours and the life curve (mean 2/3) drops on average $40 \times \pi \times 80\,833/2 \times 2.5 \times 2/3 \text{ mm·blocks}^2 = 8.46$ mm·km². Nine a day per km² is **76 mm a day**. A monsoon cell's whole normal is under about 10 mm a day, and cells' share of it ($f_{\text{sys}} = 0.3$) about 3. So the gain needs to come down to roughly 0.05 to 0.1.
   - **Also:** phase 07's Issues found 4 reached the same conclusion with a cell moving $6 \times 54\,000$ blocks in 2.5 hours. That used **game** seconds; in real seconds (1200 a game day) the cell moves 750 blocks, so the footprint argument there overstates it, but the volume argument here stands.
   - **Evidence:** 7.2, 7.2.1, 7.3's rain table; `E[R^2] = (150^2 + 150 \cdot 400 + 400^2)/3 = 80\,833`; the life curve's mean worked numerically. Lows, by the same method at 45° N in winter, give about 1.4 mm a day against a system share of about 1.2: close, so only cells are badly off.
   - **Done:** `CLIMATE_THUNDER_RATE_GAIN` starts at 1 (the master plan's number), and step 13 tunes it with the 10-year test, as 7.4 says ("the spawn rates are what gets tuned, not the normals"). The leading 3 and the factors' shape stay.

5. **"No cells under a high's centre" contradicts 7.2.1's formula.**
   - **Expected:** phase 06's todo: "no cells spawn under a high's centre in 1000 days".
   - **Actual:** 7.2.1 multiplies the rate by $(0.2)^{f_{\text{high}}}$, which is 0.2 under a high, not 0. Over 1000 days at any place with thunder, cells will spawn under a high's centre.
   - **Done:** built as the formula says; the test checks the 20%. **For Ahmed:** keep 20% (default), or make it 0 within $0.3R$ of a high's centre (one line), which makes the todo's test hold literally.

6. **Two ways to make a system's wind.** 6.5 says weather systems' pressure "adds its own wind" through the turned pressure gradient ($k_p$), and 7.3 gives each system an explicit circling profile. Doing both would count the wind twice. **Done:** systems use 7.3's profile only; phase 04's $k_p$ wind stays for heat lows only, as phase 04 built it.

7. **One home for $f_{\text{sys}}$.** Phase 07's file lists `CLIMATE_SYSTEM_RAIN_SHARE_MID` and `_TROPICS` among its constants. This phase lands first and needs them for the rain test, so they live in `run/include/climate/systems/index.h`, and phase 07 includes them (vision rule 5). The master plan gives 0.5 and 0.3 "about"; this phase blends them by smoothstep between 20° and 35°.

8. **Arithmetic checked and correct:** 7.2's $0.02 \times 120 = 2.4$ and "about 7" for three squares; 7.2.1's $3 \times 1 \times 1 \times 3 \times 1 \times 1 = 9$; 7.3's 33 and 70 blocks a second at -30 and -90 hPa; 7.3's $(25 + 25 + 16) \times 14 = 924$ hashes for the squares it counted.

Found while building, 2026-11-06 (the implementing agent, worktree `wf_6f8a0c68-47a-29`):

9. **Names in the built code (step 1).** Expected: the names this file uses. Actual, and used as built: the day count $D$ is phase 01's calendar day (`calendar_day_of`, day 0 the world's first, which is day of the year 79), and the day of the year is $D \bmod 365$; latitude `climate_latitude_deg`; declination `climate_sun_declination_deg`; the mirror sign `climate_air_mirror_sign`; $\psi$ `climate_air_wind_latitude`; the track-time sea temperature is phase 04's `climate_air_sea_temperature` with the hooks' current as its current; `climate_season_hurricane_s` as named; phase 05's normals are `climate_normals_at_t` (phase 05's rename), read with `climate_normals_at`. Phase 03 had no point read, so `climate_mask_height_at` was added, **in the Owner** (`run/{src,include}/climate/index.*`), not in `mask/`: `mask/` is lower and cannot see `climate_t`, so its signature could not live there. With it: `climate_mask_page_read`, `climate_mask_page_kept`, `climate_mask_point`. The four kind seeds and the hooks did not fit `climate_state_t`'s 6 spare bytes, so the state grew from 448 to its limit of 512 bytes (`CLIMATE_TABLES_OFFSET`), with `climate_seed`, `climate_systems_seed` and `climate_systems_hooks` to read them. `climate_systems_mask_init` was renamed `climate_systems_mask_start`: `standards_lint` (`init_deinit_pair`) wants a `_deinit` beside any `_init`, and the cache has nothing to release.

10. **Winter interior highs spawned on the equator.** Expected (`no_track_reaches_a_pole_and_no_high_reaches_the_equator`): no high below 5°. Actual: 1701 high samples below the limit on all-land ground: the interior rule (`|ψ| ≤ 70°`, land all round, $s_h \cos(\ldots) < 0$) has no lower bound, and "winter" on the equator is just the sign of a cosine. Done: interior highs only from 20° (`CLIMATE_HIGH_PSI_MIN_DEG`) to 70°. The test passes; highest $|\varphi|$ of any track 85.5°.

11. **The 10-year test cannot pass with spawn rates alone: normals change up to 20 times within a 4 km box.** Expected (7.4): tune the rates until each cell is within 15%. Actual, run 1 with the master plan's rates (log kept in the agent's scratch space): 110 of 128 cells outside, ratios 0.8 to 83. Within one box the 10-year normals run from 465 mm to 33,423 mm (seed 2 at 15° N), because phase 05's orographic rain and rain shadows change on a cell's scale. A cell moves up to 3 km and a low is up to 7 km across, so the rain any rate gives lands nearly evenly over a box, while the normal it must match does not: no rate can put a 20-fold range inside a window of 0.15 to 0.45 of the normal. Done: **wetness**: the rain a system brings to a place is its raw rain times the place's normal on the day over 5 mm (`CLIMATE_SYSTEM_RAIN_WET_MM_DAY`, `climate_systems_wetness`), applied by whoever adds rain up (the radar, this test, phase 07). Storms rain hard where it is usually wet. The raw fields are unchanged. **For Ahmed:** this phase's choice; with it the long-run rain follows the normals at every scale.

12. **With wetness, 7.2.1's factors counted the wet season twice, and a constant $f_{\text{sys}}$ did not fit where no system forms.** Expected: the rates tuned and nothing else. Actual, by runs of the test (each 15 s on release; per-kind shares from its table fitted to find the next step):

    | Run | Change | Cells outside 15% |
    |---|---|---|
    | 1 | the master plan's rates | 110 |
    | 2 | wetness (Issues found 11) | 124 (fit: 29 at the best rates) |
    | 3 | each cell's rain times `1 / (P̄/P̄_max (1 + 2 f_monsoon))` | fit: 24 |
    | 4 | that scale also over heat (floor 0.2); gain 0.07; lows 0.2 and 0.1; lows' drift 0.08; $f_{\text{sys}}$ blended 25° to 50° | 29 |
    | 5 | each low's rain times the mean low rate over its birth rate; $f_{\text{sys}}$ down to 0.4 from 60° to 70° | 7 |
    | 6 | cells 3 times as many and a third as heavy (`CLIMATE_THUNDER_RAIN_SCALE_BASE`), gain 0.2; 30 years where noise is over 5% | 4 |
    | 7 | $f_{\text{sys}}$ only on days some system can rain there (`climate_systems_rain_share_today`); gain 0.22 | 1 |
    | 8 | lows 0.184 and 0.092 | 1 (1.150) |
    | 9 | lows 0.18 and 0.09 | **0, worst 0.140** |
    | final | hurricanes 0.034 (Issues found 14) | **0, worst 0.140** |

    Why each: 7.2.1 makes cells more often when hot, in the wet season and in the monsoon; with rain following the normal, the wet season is in the normal already, so each cell is made lighter by the same factors (`climate_systems_thunder_rain_scale`) and the rain cells bring follows the normal while how often they form is still 7.2.1's. Lows: twice as many in winter, each lighter in step, for the same reason. The test's sampling noise: at the master plan's gain a 4 km box at the equator saw only 168 cell passages in 10 years (noise 7.7%), so cells were made more and lighter, and the file's own Risks rule (30 years where the noise is over 5%) is in the test: 5 of 8 sites run 30 years. $f_{\text{sys}}$ (0.3, 0.5, "about" in 7.4): at 45° in summer $|\psi|$ is under 35° (no lows) and a cool cell has no cells, so a summer-wet cell there got half the systems' rain it needed; on such days the background now brings the whole normal. Tuned values, in the header and marked: `CLIMATE_THUNDER_RATE_GAIN` 1 to 0.22 (Issues found 4 expected 0.05 to 0.1 before the rain scale), `CLIMATE_LOW_RATE_WINTER` 0.08 to 0.18, `CLIMATE_LOW_RATE_SUMMER` 0.04 to 0.09, `CLIMATE_LOW_POLEWARD` 0.15 to 0.08, `CLIMATE_SYSTEM_RAIN_SHARE_BLEND_DEG` 20 to 35 to 25 to 50, new `CLIMATE_SYSTEM_RAIN_SHARE_POLAR` 0.4 from 60° to 70°; `CLIMATE_HIGH_RATE` unchanged at 0.03. The front's showers go with the low's shield (3 × shield / 2.5) so a low's season scale reaches its front. Normals unchanged. **For Ahmed:** these are this phase's choices on the master plan's model; the frequencies (7.2, 7.2.1) are as written, the amounts follow the normals.

13. **The test builds more than 16 tiles.** Expected: 16 tiles a box. Actual: cells that rain on a box start up to 4.2 km outside it, and a cell is only spawned where its square's tile is built, so with 16 tiles the edge cells lose rain and `thunder_complete` is 0. Done: the box plus 4.2 km and a square is built, 225 tiles a seed, 2 s for all 8 on release. `thunder_complete` is checked on every day.

14. **Hurricanes at 0.034 a square-day, and none near spawn on Ahmed's seed.** Expected (section 11, Q5, the default this run was given): about 7 hurricanes a season off a warm coast, tuned by playing. Actual: at 0.02 with the track-time sea, an open-sea square at 15° gets 1.43 a year (Issues found 1); 0.034 gives the master plan's 2.4, the figure behind "about 7". Set to 0.034; the machinery test now checks `120 × rate` (4.08; measured 4.125). The real all-sea count is 1.02 a square a year. **But** on seed 2198175988018515243, the gate's, the band from 10° to 25° N within 100 km of spawn is at best 32% sea on the coarse mask and **1% open sea** (water 2048 blocks round on all sides), so `bench_climate --season` sees no hurricane from day 150 to 300: its seas are inlets and lakes. The rain test's 8 boxes are all on land and saw none either, so hurricane rain is not exercised by the calibration. **For Ahmed:** keep 2048 blocks of open sea (default), or lower `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS`, or look at a seed with open ocean.

15. **Cells are ordered by spawn time, then id.** Expected: by spawn time, then square, then index. Actual: `climate_thunder_t` (64 bytes, as specified) has no square or index. `id` is the derived hash of the square-day and index, unique, and independent of the scan, so the order is total and the bytes the same however the list is found.

16. **Big systems for cells are found 1250 blocks further out.** Cells started on day D - 1 (at most 4 clock hours before D) are kept or thinned by the fronts and highs over their start; at 7204 blocks a day a big system moves up to 1201 blocks in 4 hours, so when cells are asked for, big systems are found out to `thunder_radius + 4200 + 1250` (`CLIMATE_THUNDER_BIG_MARGIN`). Without it a cell's thinning could depend on the radius asked.

17. **Outside the keep square, one column, not a private page.** The file says a private page (256 estimates) per mask read there. A hurricane's reach is 84 km and the mask's keep square is 64 km each way, so most of a list's far reads are outside it. `climate_mask_point` reads the one column from the ground; `tests/run/climate/mask/` already holds a real page to single estimates bit for bit, and `the_mask_point_read_matches_the_window` holds the point read to the page on 1000 cells, inside and outside the keep square. The warm list is 0.35 ms, the cold one 33 ms.

18. **Two test fixes, not product bugs.** `hurricanes_turn_poleward_and_then_east` first failed (164 of 191 a day into the turn 24° off poleward): the test used 0 for "no turn yet", and a hurricane may turn at its spawn; fixed in the test, after which the worst is under 5°. `hurricanes_only_spawn_in_season_and_never_within_5_degrees` counted a day-32 hurricane at 10° to 20° in the **south**, where day 32 is summer; the check is north only.

19. **The 15 blocks a second cap on cells is never reached by real winds.** Phase 04's winds stay under 15, so `cells_move_with_the_day_s_wind_and_never_faster_than_15` checks every cell is at most 18,000 blocks a day and equals the noon wind times 1200, but has no case over the cap. Done (second agent, worktree `wf_6f8a0c68-47a-30`): the cap moved, with the same arithmetic, into the pure `climate_systems_thunder_velocity`, which the cell builder calls; the new test `a_cell_s_speed_is_capped_at_15_blocks_a_second` gives it winds of 10, 15, 20 and 40 blocks a second and still air: under the cap the wind times 1200, over it 18,000 blocks a day with the direction kept. The pinned digest did not move.

20. **The gate's view spans 145° of latitude.** 64 km at 64 blocks a pixel, as the file says, is 1.7 times the equator-to-pole distance: the frames run from 59.5° S to 85.5° N (past the pole, mirrored). Stored PNGs make the 1204 frames 3.6 GB; the run took 207 s for 301 moments. The folder was deleted after checking, to spare `/tmp`'s memory; Ahmed makes it with the command under Gate.

21. **No hurricanes form near spawn on any seed tried, so there is nothing for Ahmed to see in the gate frames.** Found by the gate checker, worktree `wf_6f8a0c68-47a-32`. Expected (section 11, Q5): about 7 hurricanes a season, tuned by playing. Actual: `bench_climate --season` on seeds 2198175988018515243 (days 150 to 300), 1 and 42 (days 200 to 260): each view's band from 10° to 25° N is about 33% sea on the coarse mask and **0% open sea**, and 0 hurricanes formed. The terrain's water comes in patches a few km across, so the rule "water 2048 blocks away on all four sides" almost never holds. The machinery works: the forced-season test and the all-sea tests make hurricanes that move, turn and weaken as the plan says, and the worked track above does too. Done: nothing changed. Changing `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS` or the open-sea rule is Issues found 14's decision for Ahmed, and it would move the pinned digest. **For Ahmed:** as things stand, the game has almost no hurricanes on real seeds. Lower the open-sea distance (for example to 512), check only the spawn point, or keep it as it is.

22. **Hurricanes form on real seeds again: the open-sea rule dropped, the rate set to Q5's "about 7", the rain kept on its normals (the final check, 2026-11-08, worktree `wf_6f8a0c68-47a-63`).** Expected (master plan 7.2 and Q5, the default this work was told to keep): hurricanes spawn over "Sea, s_hur > 0", about 2.4 a season a warm coast square and about 7 a three-square coast. Actual: none on any of the 8 seeds in 10 years (phase 11's survey; Issues found 14 and 21), because this phase's rule wanted water 2048 blocks round on all four sides, which the master plan does not have.
    - **Measured first,** with a throwaway probe (not committed) on the release build: the expected hurricanes a season in a 16 km square of the 5 to 25 degree band, 24 squares on each side of the equator on each of plan 003's 8 seeds, at 0.034, by the open-sea distance: 0 (the point alone) 0.259, 256 blocks 0.059, 512 0.016, 1024 0.003, 1536 0.002, 2048 0.001. So 512, Issues found 21's suggestion, still gives almost none.
    - **Done:** `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS` is 0, and `systems_open_sea` then reads only the point (the master plan's "Sea"). `bench_climate_survey --years 4 --places 1 --per-class 1` at 0.034: 0.27 a warm square a season (0 to 0.75); so `CLIMATE_HURRICANE_RATE` is 0.3 (0.034 x 2.4 / 0.27), and the same survey gives **2.23 a warm square** (0.50 to 3.50), 4.47 a coast of the strip's two squares (2.5 to 6.0), 1.44 landfalls a coast. The rate stays the one constant Ahmed tunes by playing.
    - **The rain test then failed,** as the boxes' tropical rain had been tuned with no hurricanes: `run_climate_systems_rain` on release, 9 of 128 outside 15%, worst 0.212 (10 and 15 degrees north, hurricanes 7 to 12% of the total). Done, in 7.4's spirit (systems bring `f_sys`, the background the rest): in hurricane season `f_sys` is `CLIMATE_SYSTEM_RAIN_SHARE_HURRICANE` (0.25) times the day's `s_hur` more (`climate_systems_rain_share_today` takes `s_hur`; `climate/weather` keeps it in its day input, the radar and the test pass it): 2 outside, worst 0.171 (an equator box, 1.163, where no hurricane forms but their rain reaches); then `CLIMATE_THUNDER_RATE_GAIN` 0.22 to 0.205: **0 outside, worst 0.142**, as phase 06's own tuning left it (0.140). Normals unchanged.
    - **Pins moved on purpose:** the systems digest `0x69B94D8AD6C147C9` to `0x80775D7E2D42F80B`, the same on 1 worker and 16 (`one_and_sixteen_workers_give_the_same_lists`); `run_climate_constants` pins 0, 0.3, 0.205 and the new 0.25. The machinery test forces 120 days: 36.17 a square against 120 x 0.3.
    - **Phase 07's landfall test** forced the season all year; at 0.3 hurricanes came ashore every week and the week before was already in flood. Done: the hook forces only days 200 to 202, "before" is the census two weeks earlier, and the landfall week must be over 1 and ten times that (phase 07, Issues found).
    - **For Ahmed:** hurricanes now form near most warm coasts; on the gate's seed 2198175988018515243 run headless at day 230, the Atlas's systems layer zoomed out shows named tropical storms with tracks and cones (for example "Tropical storm Vedusa"). Too many or too few is the rate, `CLIMATE_HURRICANE_RATE`, by playing.
    - **Cost, release, through the lock:** `bench_climate --systems`: warm median **0.508 ms** (slowest 1.125; target 2, met), 443.3 candidates a call (135.6 before), cold median 35.9 ms; `bench_climate_census` 224.3 ms (target 250, met, on release this time). The survey on 1 worker and on 16 (`--seeds 1 --years 2 --places 20 --per-class 10 --width 4096`) printed the same bytes (sha256 `60c0d1d9...`).
