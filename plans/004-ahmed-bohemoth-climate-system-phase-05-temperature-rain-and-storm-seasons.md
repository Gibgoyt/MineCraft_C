# 004-ahmed-bohemoth-climate-system — phase 5: Temperature, rain and storm seasons

**Status:** in progress: built and verified by the agent; only Ahmed's gate is left   (updated 2026-11-06)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then sections 3, 6.3, 6.6 to 6.10 and 7.2 of `plans/004-ahmed-bohemoth-climate-system.md`, then the phase 03 and phase 04 files of this plan, which this phase builds on. Every number here comes from the master plan. Where this file adds a number the master plan does not give, it says "this phase" and gives the reason.

## Goal

For any seed, every climate cell knows its **normals**: its yearly mean temperature, its normal temperature on any day of the year at any height, its normal rain on any day (stored as an average plus two yearly waves), what share of it falls as snow, the height above which snow outlasts the summer, its climate class, whether it has a hurricane season and on which days, and whether it has a monsoon. A pure function `climate_normals_at` returns all of these at any block and day, blended between cells. The master plan's worked examples come out within 10%: an equator beach at 26 to 28 °C all year, a 45° N peak at -10.4 °C in January and 8.4 °C in July, land 150 blocks below sea level at 34.5 °C, Ahmed's South Africa sketch at 625, 1125 and 69 mm with a desert west coast (30 mm with the master plan's $O = 1$, 4.5 mm by its own shadow formula: Issues found 1), and a warm-current sea at 15° N in full hurricane season on day 215. Map pictures of temperature (on a day, mean, warmest, coldest), rain, hurricane season, monsoon, climate class and snow balance can be drawn for any day of the year. The same seed gives the same bytes on 1 worker or 16, and one tile still builds in under 100 ms on one worker.

## Where this fits

**The vision.** Ahmed's game is a stack worked out strictly top to bottom: seed, terrain, climate, then (in later plans) geology, vegetation, settlements, the economy and the player's tasks. "The land decides; nothing is hand-placed" (vision file, Part 1). Phases 03 and 04 gave every cell its height, its distance to the sea, its ocean current and its wind. This phase turns those into **what the place is like**: hot or cold, wet or dry, and when.

This is the phase the later plans read most. From the vision file's table "Why the climate system comes first":

| Later reader | What it will read from this phase |
|---|---|
| Vegetation (a later plan) | Temperature and rain normals and the climate class, to choose biomes and where crops grow. "Biomes don't match the climate" is a known gap until then (master plan 2.6) |
| Settlements (a later plan) | "Where it's pleasant to live: mild temperatures, water, few floods" |
| Economy (the plan after this one) | "A mountain pass closes when it's below 0 °C": that's this phase's temperature at the pass's height. "Harvests follow rain": this phase's rain normals. "A river route closes when it freezes": phase 07's ice, driven by this phase's temperature |
| Nations | "Seasons ... somehow needs to affect that nation's strengths and weaknesses": seasons are this phase's yearly waves |
| The player | Hurricane season and monsoon labels on the HUD (master plan 4.1) |

**What later phases of plan 004 read from it:**

| Reader | What it reads |
|---|---|
| Phase 06, weather systems | Hurricane season $s_{\text{hur}}$ for the hurricane spawn rate, today's normal temperature and rain for thunderstorm cells, the monsoon flag for $f_{\text{monsoon}}$ |
| Phase 07, daily weather and the census | Normal daily temperature at any height, normal weekly rain, the snow share, the climate class (dust storms need "desert ground") |
| Phase 08, weather in the world | Rain or snow, by the snow share |
| Phase 09, the Atlas | Every layer this phase draws, with the day slider |
| Phase 10, the HUD | The temperature where the player stands, at the player's height, and the season labels |

**What this phase must NOT build:**

- No weather systems, no day-to-day weather, no noise on top of the normals. Those are phases 06 and 07. This phase is the **average** year only.
- No change to biomes, no vegetation, no autumn colours (master plan 2.6, vision Part 2).
- No geology: no basins, plateaus or escarpments are made. The basin heat term $B$ is built and tested on the formula, ready for when a geology plan makes land below sea level (master plan 2.1).
- No saving. Normals come back from the seed (vision rule 1).
- No change to anything the terrain generates. Not one block.
- No physics of the atmosphere. Every number is a formula from latitude, height, distance from the sea, current and wind.

## Ahmed's words

- "A single year is 365 days, altitude, distance from the ocean or any considerably large water bodies must somehow affect this hard-coded at generation time so we don't waste runtime compute"
- "places available below sea level, similar to the dead sea, and these areas might be very hot"
- "an equator system assuming spawn near x,y 0,y that is the equator ... every 10km is on 23.5 degrees"
- "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please"
- "regions need to blend into each other"
- "and I can filter temperature" (the map)
- "Vegetation systems not yet built into the plan please!"

"Altitude" is the cooling term $\Gamma z$, which Ahmed set to 12 °C per 1000 blocks. "Distance from the ocean" is continentality $\kappa$ in the swing and the lag. "Hard-coded at generation time" is the cell: worked out once per tile, kept, never simulated. "Hurricane and monsoon season" are this phase's season flags, which phase 06's spawn rates and the HUD read. "Very hot below sea level" is the basin term $B$.

---

## Background, explained

Each idea this phase uses, in the order the code meets it, with a worked example. Maths is written in LaTeX.

### Normals and weather

A **normal** is the average for a place on a given day of the year: what it is usually like. **Weather** is what actually happens on one day: normals plus storms plus noise (phases 06 and 07). This phase builds only normals.

Example: a normal temperature of 8.4 °C for day 196 at some peak means that over many years, that peak's day 196 averages 8.4 °C. Phase 07 will make one particular day 196 colder or warmer than that.

### Pure function

A **pure function** gives an answer that depends only on what it's given. `climate_normals_at(climate, x, y, z, day)` always gives the same answer for the same seed, block and day, whether anyone has been there or not. So nothing is simulated ahead of time, and the world can be infinite.

### Knots and splines

A **knot** is a fixed point a curve must pass through: for example "at 45° latitude, the yearly mean temperature at sea level is 11 °C". A **spline** is a smooth curve through the knots. Between two knots it bends gently, and with the slopes this repo uses (Fritsch and Carlson's, in `run/src/terrain/density/index.c:117-147`) it never goes above or below its two neighbouring knots.

Phase 04 put the temperature knots in `run/src/climate/normals/` and made the repo's spline code reachable through `terrain_density_spline_build`. This phase adds the rain-belt knots there.

**Worked example.** $\bar T_{\text{lat}}$ has knots at 15° (26 °C) and 30° (20 °C). A straight line would give 22 °C at 25°. The spline gives **22.50 °C**, because the curve is still bending from the flat part near the equator. At 28° the straight line gives 20.8 °C and the spline **20.99 °C**. The master plan's worked examples used straight lines; the code uses the spline (phase 04, Issues found 3). This phase's tests check against the spline's values, and against the master plan's within 10%.

### Latitude, and the hemisphere sign

Latitude $\varphi$ comes from phase 02 (master plan 5.1): $\varphi = 0°$ at $y = 0$, $23.5°$ at $y = 10\,000$, $90°$ at $y = 38\,298$. It repeats every $153\,191$ blocks as a triangle wave.

The **hemisphere sign** $s_h$ is $+1$ north of the equator and $-1$ south of it. Seasons are opposite in the south, so every yearly wave is multiplied by $s_h$. Multiplying a cosine by $-1$ is the same as shifting it by half a year:

$$-\cos\!\left(\frac{2\pi(d - d_0)}{365}\right) = \cos\!\left(\frac{2\pi(d - d_0 - 182.5)}{365}\right)$$

Example: if the northern peak is day 204, the southern peak is day $204 + 182.5 = 386.5$, which is day $21.5$ of the next year. So "the south runs six months from the north", which is one of this phase's tests.

At exactly $\varphi = 0$, phase 04 uses $s_h = +1$. No cell centre is exactly on $y = 0$ (centres are at $64k + 32$), so this never matters for cells.

### The temperature formula, term by term

Master plan 6.6:

$$T = \bar T_{\text{lat}}(\varphi) + \Delta T_{\text{current}} - \Gamma z + B + A\,s_h\cos\!\left(\frac{2\pi(d - 170 - \ell)}{365}\right)$$

- $\bar T_{\text{lat}}(\varphi)$: the yearly mean at sea level, by latitude, from the knots 0°: 27, 15°: 26, 30°: 20, 45°: 11, 60°: 0, 75°: -14, 90°: -24 (°C), read at $\lvert\varphi\rvert$.
- $\Delta T_{\text{current}}$: phase 04 stores it per cell as $C \times \text{current\_strength} / 255$, which is $C\,e^{-d_s/600}$ with $C = +4$ (warm), $-5$ (cold), $0$ (neutral).
- $\Gamma z$: **cooling with height.** $\Gamma = 0.012$ °C per block, Ahmed's decision (twice the real rate). $z$ is the ground height, or the player's height for the HUD.
- $B$: **below-sea-level heat** (master plan 2.1): $B = 0.025 \max(0, -z)$ on land not under sea.
- $A$: **half the yearly swing.** $A = A_{\text{sea}} + \kappa\,(A_{\text{land}} - A_{\text{sea}})$, with knots for $A_{\text{sea}}$ (0°: 1, 30°: 3, 60°: 6, 90°: 10) and $A_{\text{land}}$ (0°: 2, 30°: 9, 60°: 18, 90°: 22). Phase 04 already stores $A\,s_h$ as `t_swing`.
- $\ell$: **lag in days.** The sea warms slowly, so coasts peak later. $\ell = 25 + 20(1 - \kappa)$. Phase 04 stores it as `t_lag_days`.
- $d$: day of the year, 0 to 364, fractions allowed. Day 170 is the solstice, so the warmest day is $170 + \ell$ in the north.

**Continentality** $\kappa = 1 - e^{-d_s/1500}$ (phase 03), with $d_s$ the distance to the sea. On a coast $\kappa = 0$: a small swing and a long lag. Deep inland $\kappa \to 1$: a big swing and a short lag.

**Worked example 1: a beach on the equator.** $z = 3$, $\kappa = 0.05$, no current, $\varphi = 0$. $\bar T_{\text{lat}} = 27$. $A = 1 + 0.05 \times (2 - 1) = 1.05$. $\Gamma z = 0.036$.

$$T = 27 - 0.036 \pm 1.05 = 25.91 \text{ to } 28.01 \text{ °C}$$

All year. The master plan wrote $-0.04$ for $\Gamma z$, which is the same number rounded.

**Worked example 2: a peak at 45° N, 1000 high, 1200 blocks inland.** $\kappa = 1 - e^{-1200/1500} = 1 - e^{-0.8} = 0.5507$. With the spline, $A_{\text{sea}}(45°) = 4.371$ and $A_{\text{land}}(45°) = 13.792$, so $A = 4.371 + 0.5507 \times 9.421 = 9.559$. $\ell = 25 + 20 \times 0.4493 = 33.99$.

In January, $d = 15$: the cosine is $\cos(2\pi(15 - 170 - 33.99)/365) = \cos(-3.2534) = -0.9938$.

$$T = 11 + 0 - 0.012 \times 1000 + 0 + 9.559 \times (-0.9938) = 11 - 12 - 9.500 = -10.50 \text{ °C}$$

On day 196, mid-July: the cosine is $0.9906$, and $T = -1 + 9.469 = 8.47$ °C. The master plan says $-10.4$ and $8.4$ with straight-line knots ($A = 9.5$). Both are within 2%.

**Worked example 3: land 150 below sea level at 28° N.** $y = 11\,915$, so $\varphi = 28.000°$. 4000 blocks inland: $\kappa = 1 - e^{-2.667} = 0.9305$. Mid-July, $d = 196$. Spline: $\bar T_{\text{lat}}(28°) = 20.99$, $A_{\text{sea}} = 2.843$, $A_{\text{land}} = 8.483$, so $A = 2.843 + 0.9305 \times 5.640 = 8.091$. $\ell = 25 + 20 \times 0.0695 = 26.39$. The cosine is $\cos(2\pi(196 - 196.39)/365) = 0.99998$.

$$T = 20.99 - 0.012 \times (-150) + 0.025 \times 150 + 8.091 = 20.99 + 1.80 + 3.75 + 8.09 = 34.63 \text{ °C}$$

The master plan says 34.5 with straight lines. That's the **daily mean**.

### The daily range

The afternoon high is the daily mean plus half the **daily range** (master plan 6.6):

$$\text{range} = (4 + 8\kappa)(1 - 0.6\,r)$$

$r$ is the share of rainy days. Clouds keep nights warm and days cool, so rainy places have small ranges. This phase works out $r$ from the normal rain of the day, with the outlook's formula from master plan section 8: the chance of a wet day is $1 - e^{-\bar P_{\text{day}}/6}$ with $\bar P_{\text{day}}$ in mm. (This phase: the master plan gives $r$ but not how to get it from normals; this is the one formula in the plan that turns normal rain into a chance of a wet day.)

**Worked example.** Example 3's place is dry: $r = 0$, range $= 4 + 8 \times 0.9305 = 11.44$, half of it 5.72. The afternoon reaches $34.63 + 5.72 = 40.35$ °C. The master plan says "about 40". At a place with 6 mm of normal rain a day: $r = 1 - e^{-1} = 0.632$, and the range shrinks by $0.6 \times 0.632 = 38\%$.

### Rain belts, and the wind latitude

Rain starts from belts that follow the sun (master plan 6.7): wet at the equator, dry near 30°, wet again in the mid-latitudes, dry at the poles. The belts are read at the **wind latitude** $\psi$ from phase 04:

$$\psi = \varphi - 0.5\,\delta(d - 30), \qquad \delta(d) = 23.5°\sin\!\left(\frac{2\pi(d - 79)}{365}\right)$$

Knots, yearly rain at sea level in mm, by $\lvert\psi\rvert$: 0°: 2400, 10°: 1800, 20°: 700, 30°: 300, 40°: 800, 50°: 1000, 60°: 800, 75°: 350, 90°: 150.

**This is a rate.** $P_{\text{belt}}(\psi)$ is "mm a year if every day were like today". Today's normal rain is $P_{\text{belt}}(\psi(d)) \cdot M \cdot O / 365$ mm, and the year's total is the average of $P_{\text{belt}}(\psi(d)) \cdot M \cdot O$ over the 365 days.

**Worked example: the tropics get a wet and a dry season on their own.** At $\varphi = -25°$ (25° south): on day 15, $\delta(-15) = 23.5 \sin(2\pi \times (-94)/365) = -23.47$, so $\psi = -25 + 11.74 = -13.26°$, and $P_{\text{belt}}(13.26°) = 1454$ mm a year (spline). On day 197, $\delta(167) = +23.46$, $\psi = -36.73°$, $P_{\text{belt}} = 634$. So the southern summer is wet and the winter much drier. The average over the year is **721 mm**, not $P_{\text{belt}}(25°) = 427$. See Issues found 1.

### The upwind walk: how far the air has come over land

Air picks up water over the sea and loses it over land. To know how dry the air is at a place, the code **walks upwind**: it starts at the cell's centre and steps against the wind, looking for water. In programming this is called **ray marching**: stepping along a straight line in fixed steps and testing each point.

- The direction is the cell's own wind on that day, from phase 04's stored wave, made a unit vector $\hat w$. Step $k$ is at $\vec c - 128\,k\,\hat w$ blocks.
- **Steps 1 to 16** (up to 2048 blocks) read phase 03's window: the cell containing the point, its kind (sea, large lake, small lake, land) and, through phase 04, the current of a sea cell. Water here is sea or a large lake.
- **Steps 17 to 64** (up to 8192 blocks) go on "over the coarse land mask" (master plan 6.7): they read only the mask's height at the cell containing the point, and water is height $\le 0$. The mask knows nothing of currents or lake sizes, so water met here has no current and any size counts. The mask's heights are the same numbers as the window's (phase 03 builds the window from mask pages), so the only difference is what is known about the water.
- $d_{\text{up}} = 128\,(k_w - 1)$, where $k_w$ is the first step that lands on water. The first step on water gives $d_{\text{up}} = 0$: the air left the sea less than 128 blocks back. If no water is met by step 64, $d_{\text{up}} = 8192$. A sea or large-lake cell itself has $d_{\text{up}} = 0$.

**Worked example.** A cell 1000 blocks inland from a straight coast, with the wind blowing straight from the sea. Steps 1 to 7 (128 to 896 blocks back) are land. Step 8 (1024 back) is sea. $d_{\text{up}} = 128 \times 7 = 896$. If the coast were exactly at 1024, $d_{\text{up}}$ would be 896, slightly less than the true 1000. The 128-block step is the master plan's; the error is at most one step.

### Moisture $M$

$$M = \max\!\big(0.15,\ e^{-d_{\text{up}}/3000}\big) \times \begin{cases} 0.4 & \text{if the cell is within 800 blocks of a cold current} \\ 1.25 & \text{else, if the air left over a warm current} \\ 1 & \text{otherwise} \end{cases}$$

- "Within 800 blocks of a cold current": the cell's nearest sea (phase 04's `current` of the cell) is cold, and the cell's `sea_distance` is at most 800.
- "The air left over a warm current": the walk met water within its first 16 steps, and that window cell has a warm current, from phase 04's current-of-a-window-cell function. Water met at steps 17 to 64, over the mask, has no current information and counts as neutral. That is also what makes the master plan's plateau example come out: its air came 3000 blocks from a warm sea, past step 16, and 6.7 gives it $M = e^{-1}$ with no 1.25 (Issues found 1).
- The cases are taken in this order, so a place both near a cold current and fed from a warm one gets 0.4. (This phase: the master plan lists cases without saying which wins.)

**Worked examples** from the South Africa sketch: a coast with the wind from a warm sea: $d_{\text{up}} = 0$, $M = 1 \times 1.25 = 1.25$. The escarpment, 1000 blocks of land upwind: $M = e^{-1000/3000} \times 1.25 = 0.7165 \times 1.25 = 0.8957$. The west coast after crossing the whole continent: $e^{-8192/3000} = 0.065$, floored to 0.15, times 0.4 for the cold current: $M = 0.06$.

### The orographic factor $O$ and the rain shadow

Air pushed up a slope cools and drops its rain on the **windward** side. Behind the ridge, in its **rain shadow**, the air is dry.

$$O = \operatorname{clamp}\!\left(1 + 8\,s_w - \frac{\max(0,\ h_{\text{up}} - z)}{400},\ 0.15,\ 2.5\right)$$

- $s_w$: how fast the ground rises into the wind over the last 256 blocks: $s_w = \max\big(0,\ (z_{\text{ground}} - h(\vec c - 256\,\hat w))/256\big)$, with $h$ read bilinearly from the window's heights (sea counted as 0).
- $h_{\text{up}}$: the highest ground at any step of the upwind walk, up to the step where water was met (or step 64).
- $z$: the cell's `z_ground`.
- $\operatorname{clamp}(v, a, b)$ means $v$, but no less than $a$ and no more than $b$.

**Worked examples.** The escarpment face rises 250 blocks over 830: $s_w = (250/830 \times 256)/256 = 0.301$, so $O = 1 + 2.41 = 3.41$, clamped to 2.5. The plateau at 500, behind a 750-block crest: $s_w = 0$ (flat or falling), $h_{\text{up}} - z = 250$, $O = 1 - 250/400 = 0.375$.

### Rain at a cell, on one day

$$P = P_{\text{belt}}(\psi) \cdot M \cdot O$$

**Worked example: Ahmed's South Africa sketch**, 25° south, trade winds from the east, with $P_{\text{belt}}$ taken as 500 as the master plan does:

| Place | $M$ | $O$ | $P$, mm a year |
|---|---|---|---|
| East coast, warm current | 1.25 | 1 | $500 \times 1.25 = 625$ |
| Escarpment face, 1000 blocks of land upwind | 0.8957 | 2.5 | $500 \times 0.8957 \times 2.5 = 1120$ (master plan: 1125, rounding $M$ to 0.90) |
| Plateau behind it | $e^{-1} = 0.3679$ | 0.375 | $500 \times 0.3679 \times 0.375 = 69.0$ |
| West coast, cold current | 0.06 | 1 | $500 \times 0.06 = 30$ |

**The west coast, by the formula.** The master plan takes $O = 1$ there. But the air reaching the west coast has just come down off the 500-block plateau, so $h_{\text{up}} - z \approx 500 - 17 = 483$, and $O = 1 - 483/400 = -0.21$, clamped to 0.15. By the formula, the west coast gets $500 \times 0.06 \times 0.15 = 4.5$ mm: still a desert, still the driest of the four, but not 30 mm. See Issues found 1.

With the spline, $P_{\text{belt}}(25°) = 426.7$, and every number scales by $426.7/500 = 0.853$: 533, 955, 58.9, 25.6. Over a real year, with the belts moving, the factor is $721/500 = 1.44$ at the east coast, and the winds change too: on day 197 the wind latitude is $-36.7°$, in the westerlies, so the **west** coast faces the wind in winter. That gives the south-west a winter-rain climate, like the real Cape. This is why the test runs the sketch twice: once with the seasons frozen, to check the master plan's numbers, and once over a real year, which is printed. See Issues found 1.

### Harmonics: storing a year in five numbers

Storing 365 days of rain for every cell would cost too much. Instead, rain is stored as **harmonics**: an average plus waves that repeat once and twice a year (master plan 6.9):

$$P(d) = P_0 + P_{1c}\cos\theta + P_{1s}\sin\theta + P_{2c}\cos 2\theta + P_{2s}\sin 2\theta, \qquad \theta = \frac{2\pi d}{365}$$

The once-a-year wave gives one wet season. The twice-a-year wave lets a place have two wet seasons, as places near the equator do when the sun passes overhead twice.

**Least squares** chooses the five numbers that make the curve as close as possible to the true values on 12 sample days $d_k = 365k/12$, $k = 0 \ldots 11$, in the sense of the smallest sum of squared misses. Phase 04 showed that for 12 evenly spaced days the answer needs no matrix solving, because the waves are **orthogonal** (their products sum to zero). For two waves:

$$P_0 = \frac{1}{12}\sum_k f_k,\quad P_{1c} = \frac{1}{6}\sum_k f_k\cos\theta_k,\quad P_{1s} = \frac{1}{6}\sum_k f_k\sin\theta_k,\quad P_{2c} = \frac{1}{6}\sum_k f_k\cos 2\theta_k,\quad P_{2s} = \frac{1}{6}\sum_k f_k\sin 2\theta_k$$

with $\theta_k = 2\pi k/12 = k \times 30°$. This holds because $\sum_k \cos^2 2\theta_k = \sum_k \sin^2 2\theta_k = 6$ and every cross sum is 0 for waves 1 and 2 on 12 points.

**Worked example.** Weekly rain on the 12 days, in mm: $f = (40, 55, 62, 50, 35, 30, 38, 52, 60, 48, 34, 31)$. Then $P_0 = 535/12 = 44.58$, $P_{1c} = 0.994$, $P_{1s} = 0.933$, $P_{2c} = -5.250$, $P_{2s} = 14.289$. The fitted curve gives $(40.3, 55.7, 60.9, 50.8, 35.1, 29.2, 38.3, 53.0, 58.3, 48.9, 34.5, 30.0)$ on the 12 days: within 1.8 mm of every value. Two wet seasons (days 30 to 60 and 210 to 240) are kept.

A fitted curve can dip a little below 0 in a desert. Every reader clamps $P(d)$ at 0.

**Units:** `rain_mm_week` holds mm a week. A day's normal is $P(d)/7$. The year's total is $P_0 \times 365/7$.

### Snow share

The share of precipitation that falls as snow (master plan 6.7):

$$f_s(T) = \operatorname{clamp}\!\left(\frac{1 - T}{3},\ 0,\ 1\right)$$

$T = 2$: 0. $T = 1$: 0, all rain. $T = 0$: $1/3$. $T = -1$: $2/3$. $T = -2$: 1, all snow.

### Sea temperature and hurricane season

Phase 04 built the sea temperature (master plan 6.3), as `climate_sea_temperature`:

$$T_{\text{sea}} = \bar T_{\text{lat}}(\varphi) + C_{\text{eff}} + A_{\text{sea}}(\varphi)\, s_h \cos\!\left(\frac{2\pi(d - 215)}{365}\right)$$

with $C_{\text{eff}} = C \times \text{strength}/255$. Hurricanes need water above about 26.5 °C, and don't form within 5° of the equator, where the Earth's spin is too weak to turn them (master plan 6.8):

$$s_{\text{hur}}(d) = \operatorname{clamp}\!\left(\frac{T_{\text{sea}}(d) - 26.5}{2},\ 0,\ 1\right) \cdot \mathbb{1}\big[\,5° \le \lvert\varphi\rvert \le 25°\,\big]$$

$\mathbb{1}[\cdot]$ is 1 when the condition holds and 0 otherwise. It's **hurricane season** while $s_{\text{hur}} > 0.25$.

**Worked example.** A sea cell at 15° N, with a full warm current ($C_{\text{eff}} = +4$). Spline: $\bar T_{\text{lat}}(15°) = 26$, $A_{\text{sea}}(15°) = 1.95$. Day 215: $T_{\text{sea}} = 26 + 4 + 1.95 = 31.95$, so $s_{\text{hur}} = 1$, the peak. Day 32 is the coldest (215 − 182.5 = 32.5): $T_{\text{sea}} = 26 + 4 - 1.95 = 28.05$, $s_{\text{hur}} = 0.775$. The master plan, with straight lines, says 32, 28 and 0.75. Without the current, day 32 gives 24.05 and $s_{\text{hur}} = 0$: out of season. Without the current the season (above 0.25) runs from day 156 to day 274, **119 days**. With the warm current it never ends: see Issues found 2.

### Storing the season's days in a byte

The cell stores the first and last day of hurricane season in `hurricane_days[2]`, one byte each. A byte holds 0 to 255, so a day is stored as a quarter: $b = \lfloor d/4 \rfloor$, 0 to 91 for days 0 to 364 (master plan 6.9). 255 means "no season".

**Worked example.** Season from day 156 to day 274: stored as $\lfloor 156/4 \rfloor = 39$ and $\lfloor 274/4 \rfloor = 68$. Reading it back, day $d$ is in season when $39 \le \lfloor d/4 \rfloor \le 68$, which is days 156 to 275: at most 3 days of error at each end, from the packing. In the south, a season around new year (say day 338 to day 91) is stored as first $84$, last $22$: **first greater than last means the season wraps** past day 364. All year is stored as $\{0, 91\}$.

### Monsoon

A **monsoon** is a seasonal reversal of wind: in summer the land heats, pressure over it falls (phase 04's heat low), and wet air blows in from the sea; in winter the reverse, and it's dry. The master plan's test (6.8), for a land cell with a sea in reach:

1. The season's wind blows **from the sea**: $\vec w \cdot \hat n_{\text{sea}} < 0$, where $\hat n_{\text{sea}}$ is the cell's `sea_direction` (pointing toward the sea). A wind pointing away from the sea comes from it.
2. At the opposite time of year, the wind blows **from the land**: $\vec w \cdot \hat n_{\text{sea}} > 0$.
3. Rain in the wet half of the year is over 3 times rain in the dry half.

On the 12 sample days: condition 1 and 2 hold if there is some $k$ with $\vec w_k \cdot \hat n < 0$ and $\vec w_{k+6} \cdot \hat n > 0$ (indices modulo 12). The wet half is the 6 consecutive sample days (going round the year) with the largest rain sum; the dry half is the other 6. If all three hold, the cell gets the flag `CLIMATE_CELL_MONSOON`.

**Monsoon today**, for the label and for phase 06's $f_{\text{monsoon}}$: the flag is set, the wind on day $d$ (from the stored wave) blows from the sea, and $P(d) > P_0$.

**Worked example.** A coast facing south ($\hat n = (0, -1)$). Summer wind $(-1.0, +1.5)$: $\vec w \cdot \hat n = -1.5 < 0$, from the sea. Six samples later, winter wind $(-4.0, -1.2)$: $+1.2 > 0$, from the land. Rain sums: wet half 180 mm, dry half 40 mm: $180 > 3 \times 40$. Monsoon. Phase 04's Issues found 1 shows that with the master plan's numbers this will be rare. This phase builds the test exactly as written.

### Snow balance height

`snow_balance_z` is the height above which snow outlasts the summer (master plan 6.9). Above it, glaciers would form; below it, the snow melts every year.

This phase finds it with a small **degree-day model**, the same one phase 07 uses for the weekly state (master plan 7.5), run on the normals:

- Week $w$ has mean temperature $T_w$ (the normal at day $7w + 3.5$, at height $z$) and precipitation $P_w$ (the normal weekly rain, clamped at 0).
- Snow on the ground: $S_{w+1} = \max\big(0,\ S_w + f_s(T_w)\,P_w - 7 \times 4 \times \max(0, T_w)\big)$. Snow melts at 4 mm of water per degree per day.
- Run two years from $S = 0$. If $S$ never reaches 0 in the second year, snow outlasts the summer at this height.

Higher is colder, so "outlasts" is true above some height and false below it. **Bisection** finds that height: start with a range known to contain it, test the middle, and keep the half where the answer changes. Each test halves the range. This phase searches $z$ from -250 to 2000 in 12 halvings, to within $2250/4096 = 0.55$ blocks. If snow outlasts the summer even at -250, the answer is -250; if not even at 2000, it's `CLIMATE_SNOW_BALANCE_NONE` (9999 blocks: "no glaciers here").

**Worked examples** (worked in this agent's scratch space with the spline knots and rain spread evenly over the year): 45° N, $\kappa = 0.5507$, no current, 900 mm a year: about **1484** blocks, above the world's top of 1250, so no permanent snow there. A coast at 60° ($\kappa = 0$, $\bar T = 0$, $A = 6$) with 800 mm: about **325**. A coast at 75° ($\bar T = -14$, $A = 7.93$) with 350 mm: snow outlasts the summer even at -250, so **-250**: everywhere there is glacier country.

### Monthly means of a wave

The climate class needs the warmest and coldest **month** means, not day values. A month here is $365/12 = 30.42$ days, centred on $d_m = 365(m + 0.5)/12$. The mean of a cosine over a month is the cosine at its centre times a fixed factor:

$$\frac{1}{L}\int_{d_m - L/2}^{d_m + L/2} \cos\!\left(\frac{2\pi d}{365}\right) dd = \cos\!\left(\frac{2\pi d_m}{365}\right)\cdot\frac{\sin(\pi/12)}{\pi/12} = 0.98862\,\cos\!\left(\frac{2\pi d_m}{365}\right)$$

For the twice-a-year wave the factor is $\frac{\sin(\pi/6)}{\pi/6} = 0.95493$. So a month's mean temperature is $\bar T + 0.98862\,A\,s_h\cos(\ldots)$ at its centre, and a month's rain uses $0.98862$ on the first wave and $0.95493$ on the second, times $365/12/7$ to turn mm a week into mm a month.

**Example:** the 45° N peak: $T_{\text{max}} = -1 + 0.98862 \times 9.559 = 8.45$ °C (warmest month), $T_{\text{min}} = -10.45$.

### Climate classes

A simplified **Köppen** system, which geographers use to sort climates (master plan 6.10). For each land cell: $T_{\text{max}}$, $T_{\text{min}}$ (warmest and coldest month means at `z_ground`), yearly rain $P$, the driest month, and the dryness threshold $P_{\text{th}} = 20\,\bar T + 140$ mm. The first rule that matches wins:

| Order | Class | Rule |
|---|---|---|
| 1 | Ice | $T_{\text{max}} < 0$ |
| 2 | Tundra | $0 \le T_{\text{max}} < 10$ |
| 3 | Desert | $P < P_{\text{th}}/2$ |
| 4 | Steppe | $P < P_{\text{th}}$ |
| 5 | Rainforest | $T_{\text{min}} \ge 18$, driest month at least 60 mm |
| 6 | Monsoon | $T_{\text{min}} \ge 18$, the monsoon flag |
| 7 | Savanna | $T_{\text{min}} \ge 18$, a dry season (driest month under 60 mm) |
| 8 | Mediterranean | $T_{\text{min}} > -3$, dry summer |
| 9 | Temperate | $T_{\text{min}} > -3$, rain all year |
| 10 | Continental | $T_{\text{min}} \le -3$, $T_{\text{max}} \ge 22$ |
| 11 | Boreal | $T_{\text{min}} \le -3$, $T_{\text{max}} < 22$ |

**"Dry summer"** (this phase, from the Köppen rule the master plan simplifies): the driest month of the local summer half has under 40 mm and under a third of the wettest month of the winter half. The summer half is the six months whose centres have $s_h\cos(2\pi(d_m - 170)/365) > 0$.

**Worked examples.** The 45° N peak: $T_{\text{max}} = 8.45$, so rule 2: **Tundra**. The South Africa plateau at 500 blocks, $\bar T = 22.5 - 6 = 16.5$: $P_{\text{th}} = 470$, and $69 < 235$: **Desert**. Sea cells get `CLIMATE_CLASS_NONE`.

### Bilinear blend with warp, again

Phase 03 reads a cell value at any point as a weighted average of the four nearest cell centres, at a point moved by up to 32 blocks of warp noise, so regions blend with wobbly borders. This phase blends `t_mean`, `t_swing`, `t_lag_days`, the five rain numbers and `snow_balance_z` that way. Categories (class, flags, `hurricane_days`) come from the nearest cell to the warped point, never a blend, as phase 04 does for the current's kind.

**Blending then evaluating** gives the same answer as evaluating then blending for anything linear in the stored numbers, such as the rain harmonics. The temperature wave is not linear in $\ell$ (it's inside the cosine), so the code blends the three temperature numbers and then evaluates. The difference between cells 64 blocks apart is a fraction of a degree, and the test checks it at cell centres, where there is no blending.

---

## Design

### Directories and files

New parts under `run/src/climate/` (phase 03 created the system, phase 04 added `normals`, `harmonic` and `air`). Each has exactly one `index.c`, a `README.md` (purpose, tier, owns, lifetime, threads, public functions, may be called by, depends on, per `plans/REPO_STRUCTURE.md` section 3), and a header `run/include/climate/<part>/index.h`:

| Directory | Depth, tier | Job |
|---|---|---|
| `run/src/climate/heat/` | 2, Arena (allocates nothing) | NEW. The temperature formula (6.6) as a pure function, the daily range, the snow share, and filling `t_mean` for a tile |
| `run/src/climate/rain/` | 2, Arena | NEW. The upwind walk, $M$, $O$, the rain belts on 12 days, and the two-wave fit into `rain_mm_week` for a tile. Scratch from the Job arena |
| `run/src/climate/season/` | 2, Arena (allocates nothing) | NEW. Hurricane season and `hurricane_days`, the hurricane-coast and monsoon flags, monsoon today, snow balance, climate classes |

Changes to earlier parts:

- `run/src/climate/normals/` (phase 04): adds the rain-belt knots, `NORMALS_P_BELT_LAT_DEG` and `NORMALS_P_BELT_MM`, and their spline, built in `climate_init` with the others.
- `run/src/climate/harmonic/` (phase 04): adds `climate_harmonic_fit2` and `climate_harmonic_at2`.
- `run/src/climate/air/` (phase 04): only makes the current-of-a-window-cell function public, if phase 04 left it `static`, as `climate_air_window_current`.
- `run/src/climate/cell/` (phase 03): after `climate_air_build_tile`, `climate_cell_build_tile` calls `climate_heat_build_tile`, `climate_rain_build_tile` and `climate_season_build_tile`, in that order.
- `run/src/climate/layer/` (phase 03): ten more layers.
- `run/src/climate/index.c`, `run/include/climate/index.h` (the Owner): `climate_normals_at`.

New tests (found by `cmake/targets.cmake` from their path, `tests/**/index.c` becomes ctest `<path with / as _>`):

| Directory | ctest |
|---|---|
| `tests/run/climate/heat/` | `run_climate_heat` |
| `tests/run/climate/rain/` | `run_climate_rain` |
| `tests/run/climate/season/` | `run_climate_season` |

Added to: `tests/run/climate/harmonic/` (`run_climate_harmonic`), `tests/run/climate/layer/` (`run_climate_layer`), `tests/run/climate/cell/` (`run_climate_cell`, the digest pin re-set with a line saying why), `tests/run/climate/fixtures/ground.h` (the South Africa continent and a ridge).

### Dependencies on earlier phases, by what they do

Phases 02 to 04 are not built yet. This file uses the names those phase files give. If the built code uses other names, use the built ones and write the mapping under Issues found.

| Needed | From | Name in its phase file |
|---|---|---|
| Latitude at a $y$ | Phase 02 | `climate_latitude_deg` (phase 03 uses this name) |
| Declination $\delta(d)$ | Phase 02 | whatever phase 02 names it |
| The cell, the window, the warped bilinear read | Phase 03 | `climate_cell_t`, `climate_window_t` (phase 04), `climate_sample_at` |
| A land-mask height at a block, built privately if missing | Phase 03 | the mask part; a point read is added in phase 06 if absent. This phase reads mask pages the way phase 03's window builder does |
| The spline builder | Phase 04 | `terrain_density_spline_build` |
| The knots and splines | Phase 04 | `run/src/climate/normals/` |
| `t_swing`, `t_lag_days`, `current`, `current_strength`, `wind[6]`, `pressure[3]` | Phase 04 | in `climate_cell_t` |
| A window cell's current | Phase 04 | step 1 of `climate_air_build_tile`; public as `climate_air_window_current` |
| Sea temperature | Phase 04 | `climate_sea_temperature(climate, cell, day)` |
| The 12-day fit of one wave | Phase 04 | `climate_harmonic_fit1`, `climate_harmonic_at1`, `CLIMATE_FIT_DAYS` |

### Constants

One home each (vision rule 5). Floats use the `f32` suffix, as `plans/CODING_STANDARD.md` section 2 requires in C-only headers.

In `run/include/climate/heat/index.h`:

```c
static const float32_t CLIMATE_LAPSE_C_PER_BLOCK = 0.012f32;        /* Γ, 6.6: Ahmed's "12 °C per 1000 blocks" */
static const float32_t CLIMATE_BASIN_C_PER_BLOCK = 0.025f32;        /* B, 6.6 and 2.1: below sea level, on land */
static const float32_t CLIMATE_SOLSTICE_DAY = 170.0f32;             /* 6.6: the cosine's zero lag */
static const float32_t CLIMATE_RANGE_COAST_C = 4.0f32;              /* 6.6: daily range on a coast */
static const float32_t CLIMATE_RANGE_INLAND_EXTRA_C = 8.0f32;       /* 6.6: + 8κ */
static const float32_t CLIMATE_RANGE_RAIN_CUT = 0.6f32;             /* 6.6: × (1 - 0.6 r) */
static const float32_t CLIMATE_WET_DAY_SCALE_MM = 6.0f32;           /* 8: r = 1 - e^{-P_day/6} */
static const float32_t CLIMATE_SNOW_ALL_RAIN_C = 1.0f32;            /* 6.7: all rain above */
static const float32_t CLIMATE_SNOW_ALL_SNOW_C = -2.0f32;           /* 6.7: all snow below */
```

In `run/include/climate/rain/index.h`:

```c
enum : uint32_t {
    CLIMATE_WALK_STEPS_WINDOW = 16u,   /* 6.7: 16 steps of 128 in the window */
    CLIMATE_WALK_STEPS_MAX = 64u,      /* 6.7: on over the mask to 8 km, 8192 / 128 */
};
static const float32_t CLIMATE_WALK_STEP_BLOCKS = 128.0f32;        /* 6.7 */
static const float32_t CLIMATE_MOISTURE_SCALE_BLOCKS = 3000.0f32;  /* 6.7: e^{-d_up/3000} */
static const float32_t CLIMATE_MOISTURE_MIN = 0.15f32;             /* 6.7 */
static const float32_t CLIMATE_MOISTURE_WARM = 1.25f32;            /* 6.7 */
static const float32_t CLIMATE_MOISTURE_COLD = 0.4f32;             /* 6.7 */
static const float32_t CLIMATE_MOISTURE_COLD_BLOCKS = 800.0f32;    /* 6.7 */
static const float32_t CLIMATE_OROGRAPHIC_GAIN = 8.0f32;           /* 6.7: 1 + 8 s_w */
static const float32_t CLIMATE_OROGRAPHIC_RISE_BLOCKS = 256.0f32;  /* 6.7: s_w over the last 256 */
static const float32_t CLIMATE_SHADOW_BLOCKS = 400.0f32;           /* 6.7: (h_up - z) / 400 */
static const float32_t CLIMATE_OROGRAPHIC_MIN = 0.15f32;           /* 6.7 */
static const float32_t CLIMATE_OROGRAPHIC_MAX = 2.5f32;            /* 6.7 */
static const float32_t CLIMATE_DAYS_A_WEEK = 7.0f32;
static const float32_t CLIMATE_DAYS_A_YEAR = 365.0f32;
```

In `run/include/climate/season/index.h`:

```c
static const float32_t CLIMATE_HURRICANE_SEA_C = 26.5f32;          /* 6.8 */
static const float32_t CLIMATE_HURRICANE_RAMP_C = 2.0f32;          /* 6.8: (T_sea - 26.5) / 2 */
static const float32_t CLIMATE_HURRICANE_LAT_MIN_DEG = 5.0f32;     /* 6.8 */
static const float32_t CLIMATE_HURRICANE_LAT_MAX_DEG = 25.0f32;    /* 6.8 */
static const float32_t CLIMATE_HURRICANE_SEASON_S = 0.25f32;       /* 6.8: "season while s_hur > 0.25" */
static const float32_t CLIMATE_HURRICANE_COAST_BLOCKS = 3000.0f32; /* 6.8: "a coast within 3 km" */
static const float32_t CLIMATE_MONSOON_RAIN_RATIO = 3.0f32;        /* 6.8 */
static const float32_t CLIMATE_MELT_MM_PER_DEGREE_DAY = 4.0f32;    /* 7.5; the one home: phase 07 includes this */
static const float32_t CLIMATE_SNOW_BALANCE_LOW = -250.0f32;       /* this phase: bisection range, the world's floor */
static const float32_t CLIMATE_SNOW_BALANCE_HIGH = 2000.0f32;      /* this phase: above 003 phase 11's +1250 */
static const float32_t CLIMATE_SNOW_BALANCE_NONE = 9999.0f32;      /* this phase: snow never outlasts summer */
enum : uint32_t {
    CLIMATE_SNOW_BALANCE_HALVINGS = 12u, /* this phase: 2250 / 4096 = 0.55 blocks */
    CLIMATE_SNOW_BALANCE_YEARS = 2u,     /* this phase: spin-up year, then the year tested */
};
static const float32_t CLIMATE_DRY_THRESHOLD_PER_C = 20.0f32;      /* 6.10: P_th = 20 T + 140 */
static const float32_t CLIMATE_DRY_THRESHOLD_BASE_MM = 140.0f32;   /* 6.10 */
static const float32_t CLIMATE_TROPICAL_MIN_C = 18.0f32;           /* 6.10 */
static const float32_t CLIMATE_TEMPERATE_MIN_C = -3.0f32;          /* 6.10 */
static const float32_t CLIMATE_CONTINENTAL_MAX_C = 22.0f32;        /* 6.10 */
static const float32_t CLIMATE_TUNDRA_MAX_C = 10.0f32;             /* 6.10 */
static const float32_t CLIMATE_WET_MONTH_MM = 60.0f32;             /* 6.10 */
static const float32_t CLIMATE_DRY_SUMMER_MM = 40.0f32;            /* this phase, Köppen's Cs */
static const float32_t CLIMATE_DRY_SUMMER_SHARE = 1.0f32 / 3.0f32; /* this phase, Köppen's Cs */
static const float32_t CLIMATE_MONTH_WAVE1 = 0.98862f32;           /* sin(π/12) / (π/12): a month's mean of wave 1 */
static const float32_t CLIMATE_MONTH_WAVE2 = 0.95493f32;           /* sin(π/6) / (π/6): of wave 2 */

enum : uint8_t {
    CLIMATE_CLASS_NONE = 0,          /* sea */
    CLIMATE_CLASS_ICE = 1,
    CLIMATE_CLASS_TUNDRA = 2,
    CLIMATE_CLASS_DESERT = 3,
    CLIMATE_CLASS_STEPPE = 4,
    CLIMATE_CLASS_RAINFOREST = 5,
    CLIMATE_CLASS_MONSOON = 6,
    CLIMATE_CLASS_SAVANNA = 7,
    CLIMATE_CLASS_MEDITERRANEAN = 8,
    CLIMATE_CLASS_TEMPERATE = 9,
    CLIMATE_CLASS_CONTINENTAL = 10,
    CLIMATE_CLASS_BOREAL = 11,
    CLIMATE_CLASS_COUNT = 12,
};
```

`CLIMATE_CELL_MONSOON`, `CLIMATE_CELL_HURRICANE_COAST` and `CLIMATE_DAY_NONE` (255) already exist in phase 03's `run/include/climate/cell/index.h`; this phase sets them.

Knots, in `run/src/climate/normals/index.c`, with the f64 suffix in code:

```c
static const float64_t NORMALS_P_BELT_LAT_DEG[] = {0, 10, 20, 30, 40, 50, 60, 75, 90};             /* 6.7, by |ψ| */
static const float64_t NORMALS_P_BELT_MM[] = {2400, 1800, 700, 300, 800, 1000, 800, 350, 150};     /* 6.7: mm a year */
```

Nine knots, inside `TERRAIN_DENSITY_SPLINE_KNOTS_MAX` (12, `run/include/terrain/density/index.h:18`).

### What the cell holds after this phase

`climate_cell_t` is phase 03's 128-byte struct, unchanged in layout. This phase fills:

| Field | Value |
|---|---|
| `t_mean` (24) | $\bar T_{\text{lat}}(\lvert\varphi\rvert) + C \times \text{current\_strength}/255$: the yearly mean **at sea level**, without height terms, so the read can apply any $z$. The yearly mean at the ground is `t_mean` $- \Gamma z_{\text{ground}} + B(z_{\text{ground}})$ |
| `rain_mm_week[5]` (36) | $P_0, P_{1c}, P_{1s}, P_{2c}, P_{2s}$, mm a week |
| `snow_balance_z` (92) | Blocks, from the bisection; `CLIMATE_SNOW_BALANCE_NONE` for sea cells |
| `climate_class` (98) | `CLIMATE_CLASS_*` |
| `flags` (99) | Adds `CLIMATE_CELL_MONSOON`, `CLIMATE_CELL_HURRICANE_COAST` |
| `hurricane_days[2]` (100) | Quarter days, first and last; `CLIMATE_DAY_NONE` (255) for none |

**Why `t_mean` is stored at sea level.** The read blends four cells. If each held its own ground's mean, blending across a cliff would mix heights, and a player at a different height would get the ground's temperature. Stored at sea level, the read adds $-\Gamma z + B(z)$ for the exact $z$ asked about.

### The pure formulas, in `climate/heat`

```c
/* 6.6, the normal daily mean temperature, °C. day from 0 to below 365, fractions allowed. land is false for sea and lakes:
 * then B = 0. A pure function of its arguments, for tests and for the read. */
float32_t climate_heat_formula(float32_t t_mean_sea_level, float32_t z, bool land, float32_t t_swing_signed,
                               float32_t lag_days, float32_t day);

/* The same from latitude, height, κ and the current's °C, working out t_mean, A and ℓ itself. For tests of 6.6's
 * worked examples, which give these inputs. */
float32_t climate_heat_from_place(const climate_t *climate, float32_t lat_deg, float32_t z, float32_t kappa,
                                  float32_t current_c, bool land, float32_t day);

/* 6.6: (4 + 8κ)(1 - 0.6 r), with r = 1 - e^{-P_day / 6} and P_day the day's normal rain in mm. °C. */
float32_t climate_heat_daily_range(float32_t kappa, float32_t rain_mm_day);

/* 6.7: clamp((1 - T) / 3, 0, 1). */
float32_t climate_heat_snow_share(float32_t t_c);

/* Fills t_mean of a tile's 256 cells, whose phase 03 and 04 fields are set. Pure. */
void climate_heat_build_tile(const climate_t *climate, climate_cell_t *cells);
```

$T = t_{\text{mean}} - \Gamma z + B + A s_h \cos(2\pi(d - 170 - \ell)/365)$, with $B = 0.025\max(0, -z)$ only when `land`. Worked in `float32_t`, with the cosine from `cosf` of `float32_t` arguments. The 2π is a named constant.

### Rain, in `climate/rain`

```c
/* The walk's answer for one cell and one wind. */
typedef struct climate_rain_walk_t {
    float32_t d_up_blocks;   /* 0: 128 (k_w - 1), 0 on water, 8192 if none met */
    float32_t h_up;          /* 4: highest ground met before water, blocks */
    float32_t s_w;           /* 8: rise into the wind over the last 256 blocks, 0 if falling */
    uint8_t water_current;   /* 12: CLIMATE_CURRENT_* of the water met, NONE if over the mask or none met */
    uint8_t met_water;       /* 13: 1 if water was met by step 64 */
    uint8_t _pad0[2];        /* 14 */
} climate_rain_walk_t;
static_assert(sizeof(climate_rain_walk_t) == 16);

/* Walks upwind from window cell (i, j) against the unit wind (wx, wy). Steps 1 to 16 read the window's kinds and currents;
 * steps 17 to 64 read heights only (the coarse land mask), so water there has no current. Pure. */
void climate_rain_walk(const climate_t *climate, const climate_window_t *window, uint32_t i, uint32_t j, float32_t wx,
                       float32_t wy, climate_rain_walk_t *out);

/* 6.7's M and O from a walk and the cell. */
float32_t climate_rain_moisture(const climate_rain_walk_t *walk, const climate_cell_t *cell);
float32_t climate_rain_orographic(const climate_rain_walk_t *walk, float32_t z_ground);

/* P_belt(|ψ|) in mm a year, from the spline. */
float32_t climate_rain_belt_mm_year(const climate_t *climate, float32_t psi_deg);

/* Fills rain_mm_week of a tile's 256 cells. Scratch (12 values a cell) from arena. Pure. */
void climate_rain_build_tile(const climate_t *climate, arena_t *arena, const climate_window_t *window,
                             climate_cell_t *cells);
```

**What `climate_rain_build_tile` does**, for each tile cell, south row first, x fastest:

1. For $k = 0 \ldots 11$, $d_k = 365k/12$:
   - a. The cell's wind on $d_k$ from phase 04's stored wave (`climate_harmonic_at1` on `wind[0..2]` and `wind[3..5]`). If its length is under $10^{-3}$ blocks a second, the walk has no direction: $d_{\text{up}}$ is the cell's own `sea_distance` (capped at 8192), $h_{\text{up}} = z_{\text{ground}}$, $s_w = 0$, and the water current is the cell's `current`. Otherwise $\hat w$ is the unit wind, and the walk goes against it.
   - b. $\psi_k = \varphi - 0.5\,\delta(d_k - 30)$ (phase 02's $\delta$).
   - c. $f_k = P_{\text{belt}}(\lvert\psi_k\rvert) \cdot M \cdot O \times 7/365$, mm a week. For sea and large-lake cells: $M$ with $d_{\text{up}} = 0$ (so 1, or 0.4 within 800 blocks of a cold current, which for a sea cell means its own current is cold), and $O = 1$.
2. `climate_harmonic_fit2` on the 12 $f_k$ into `rain_mm_week`.

**The walk, exactly:**

- Window cell $(i, j)$ has its centre at block $(c_x, c_y)$ (phase 04's formula). Step $k$ is at $(c_x - 128 k\,w_x,\ c_y - 128 k\,w_y)$.
- A step's point maps to the window cell containing it: $i' = \lfloor (p_x - x_0)/64 \rfloor$ with $x_0$ the window's west edge in blocks, the same for $j'$.
- **Steps 1 to 16:** read `window->kind[j' * 272 + i']`: water is `CLIMATE_SEA_KIND_SEA` or `CLIMATE_SEA_KIND_LAKE_LARGE`, and the height is `window->heights[...]`. These are always inside the window: a tile cell is at least 8192 blocks from the window's edge, and step 16 is 2048 away.
- **Steps 17 to 64:** read only a height: `window->heights[...]` while $0 \le i', j' < 272$ (always, in fact, since step 64 is 8192 away and the margin is 8192 plus at least half a cell), else phase 03's mask at that cell. Water is height $\le 0$, with no current. Reading the window's heights here is the mask's numbers without a second lookup; it is not reading the window's kinds.
- $h_{\text{up}}$ is the largest height met at steps $1 \ldots k_w - 1$, with $z_{\text{ground}}$ as its starting value, and sea counted as 0.
- $s_w$ reads the window's heights bilinearly at $\vec c - 256\,\hat w$.

**Cost.** Per tile: $256 \times 12$ walks of at most 64 steps, about 197,000 steps worst case, each a few array reads. Most walks meet water early.

### `climate/harmonic`, added

```c
/* The least-squares fit of P0 + P1c cos θ + P1s sin θ + P2c cos 2θ + P2s sin 2θ to values on the CLIMATE_FIT_DAYS days
 * d_k = 365 k / 12. Sums in float64_t, in k order. out = {P0, P1c, P1s, P2c, P2s}. */
void climate_harmonic_fit2(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[5]);
/* The two-wave curve on any day, from 0 to below 365, fractions allowed. Not clamped: callers clamp rain at 0. */
float32_t climate_harmonic_at2(const float32_t coefficients[5], float32_t day);
```

The 12 values of $\cos 2\theta_k$ and $\sin 2\theta_k$ are a second exact `static const float64_t` table, beside phase 04's: $\cos 60°k$ and $\sin 60°k$, so values $0, \pm 0.5, \pm\frac{\sqrt 3}{2}, \pm 1$.

### Seasons and classes, in `climate/season`

```c
/* 6.8's s_hur from latitude and sea temperature. 0 outside 5° to 25°. Pure. */
float32_t climate_season_hurricane_s(float32_t lat_deg, float32_t t_sea_c);

/* Whether day d is inside a stored season: first <= floor(d/4) <= last, or the wrapped form when first > last. False for
 * CLIMATE_DAY_NONE. */
bool climate_season_hurricane_today(const uint8_t hurricane_days[2], float32_t day);

/* Monsoon today: the flag, the wind on day d from the sea, and P(d) > P0. */
bool climate_season_monsoon_today(const climate_cell_t *cell, float32_t day);

/* The class from the five numbers, by 6.10's first match. Exposed for the table-driven test. */
uint8_t climate_season_classify(float32_t t_mean_ground, float32_t t_max_month, float32_t t_min_month,
                                float32_t rain_mm_year, float32_t driest_month_mm, bool dry_summer, bool monsoon);

/* Snow balance by bisection on the degree-day model, for a cell's temperature and rain numbers. Blocks. */
float32_t climate_season_snow_balance(const climate_cell_t *cell);

/* Fills hurricane_days, the two flags, snow_balance_z and climate_class of a tile's 256 cells. Pure. */
void climate_season_build_tile(const climate_t *climate, const climate_window_t *window, climate_cell_t *cells);
```

**What `climate_season_build_tile` does, per cell:**

1. **Hurricane days.** Which sea gives the season:
   - A sea cell: itself.
   - A land or lake cell with `sea_distance` $\le 3000$: its nearest sea cell, `window->sea_nearest` (phase 03), whose current comes from `climate_air_window_current` and whose latitude is at its own centre.
   - Otherwise: no season, `{255, 255}`, and stop.
2. For every day $d = 0 \ldots 364$ (whole days), $s_{\text{hur}}(d)$ with `climate_sea_temperature` on that sea. Find the days with $s > 0.25$. If none: `{255, 255}`. If all 365: `{0, 91}`. Otherwise take the **longest run** of consecutive in-season days, counting round the year (day 364 is followed by day 0), ties to the run that starts earliest; store its first and last day as quarter days. 365 evaluations of one cosine per cell is under 0.1 ms a tile.
3. **Hurricane coast flag:** a land or lake cell (not sea) that got a season in step 2.
4. **Monsoon flag** (land cells with a nonzero `sea_direction`): the three conditions in Background, on the 12 sample days: the wind from `wind[]`, the rain from `rain_mm_week` at the same days.
5. **Snow balance** (land and lake cells): `climate_season_snow_balance`. The bisection runs on the cell's own numbers at heights $z$, with $B(z)$ when $z < 0$ for land cells. Weekly temperatures use the stored `t_mean`, `t_swing`, `t_lag_days`; weekly rain is `climate_harmonic_at2` clamped at 0. 104 weeks a test, 12 tests: about 1250 cosines a cell, about 320,000 a tile. The bench measures it (Risks).
6. **Class** (land and lake cells; sea gets `NONE`): $\bar T$ at `z_ground`, the 12 monthly means of temperature and rain by the 0.98862 and 0.95493 factors, $T_{\text{max}}$, $T_{\text{min}}$, yearly rain $P_0 \times 365/7$, the driest month, dry summer, and the monsoon flag, then `climate_season_classify`.

### Reading the normals: the Owner's public function

In `run/include/climate/index.h`:

```c
/* A place's normals on a day: what it is usually like. */
typedef struct climate_normals_t {
    float32_t t_mean_c;          /* 0: yearly mean at z */
    float32_t t_day_c;           /* 4: the normal daily mean on day, at z */
    float32_t t_range_c;         /* 8: the normal daily range on day */
    float32_t t_high_c;          /* 12: t_day + range / 2 */
    float32_t t_low_c;           /* 16: t_day - range / 2 */
    float32_t t_max_month_c;     /* 20: warmest month mean at z */
    float32_t t_min_month_c;     /* 24: coldest month mean at z */
    float32_t rain_mm_day;       /* 28: P(day) / 7, clamped at 0 */
    float32_t rain_mm_year;      /* 32: P0 × 365 / 7 */
    float32_t rain_max_mm_day;   /* 36: the wettest day of this place's year, from 73 days 5 apart: phase 06 reads it */
    float32_t snow_share;        /* 40: f_s(t_day) */
    float32_t snow_balance_z;    /* 44: blocks, blended */
    float32_t sea_temperature_c; /* 48: for the nearest cell if it is sea, else NaN */
    float32_t hurricane_s;       /* 52: s_hur today, for the nearest cell if sea; 0 otherwise */
    uint8_t climate_class;       /* 56: the nearest cell's */
    uint8_t flags;               /* 57: the nearest cell's CLIMATE_CELL_* */
    uint8_t hurricane_season;    /* 58: 1 if today is inside the nearest cell's stored season */
    uint8_t monsoon_today;       /* 59: 1 if climate_season_monsoon_today holds for the nearest cell */
} climate_normals_t;
static_assert(sizeof(climate_normals_t) == 60);

/* The normals at a block and height on a day: blended between cells as phase 03 blends, categories from the nearest cell to
 * the warped point. z is the ground for maps, the player's height for the HUD. False, writing nothing, if a tile it needs is
 * not built. Any thread. day from 0 to below 365. */
bool climate_normals_at(const climate_t *climate, float64_t x, float64_t y, float32_t z, float32_t day,
                        climate_normals_t *out);
```

`land` for the $B$ term is "the nearest cell is neither `CLIMATE_CELL_SEA` nor `CLIMATE_CELL_LAKE`". A player in a mine 200 blocks below sea level under land gets $B = 5$ °C on top of $+2.4$ from $\Gamma$: deep mines are warm. That's what 6.6 says for "the player's height for the HUD", and phase 10 shows it.

### Layers

Added to `climate_layer_t` in `run/include/climate/layer/index.h`. Each uses `climate_layer_draw`'s `day` argument (the day slider), draws temperature at each cell's `z_ground`, and draws grey `0x808080` where a tile is not built, as phase 03's do.

| Layer | Drawn as |
|---|---|
| `CLIMATE_LAYER_TEMPERATURE_DAY` | The normal daily mean on `day`. Colour stops: -30 °C `0x3040A0`, -10 `0x60A0E0`, 0 `0xF0F0F0`, 15 `0xF0D060`, 30 `0xE06020`, 45 `0x801010`, linear in each channel between stops, clamped at the ends |
| `CLIMATE_LAYER_TEMPERATURE_MEAN` | The yearly mean, same colours |
| `CLIMATE_LAYER_TEMPERATURE_WARMEST` | The warmest month mean, same colours |
| `CLIMATE_LAYER_TEMPERATURE_COLDEST` | The coldest month mean, same colours |
| `CLIMATE_LAYER_RAIN_YEAR` | Yearly rain. Stops: 0 mm `0xC8A060`, 250 `0xE0D090`, 500 `0xA0C060`, 1000 `0x40A040`, 2000 `0x2070C0`, 3000 `0x203080` |
| `CLIMATE_LAYER_RAIN_DAY` | The day's normal rain times 365, same colours, so a day reads on the same scale as a year |
| `CLIMATE_LAYER_HURRICANE_SEASON` | Phase 03's water layer underneath. Sea cells with $s_{\text{hur}} > 0$ on `day`: from `0x603030` at $s = 0$ to `0xFF2020` at $s = 1$. Hurricane-coast land cells in season today: `0xFF9040` |
| `CLIMATE_LAYER_MONSOON` | Water layer underneath. Land with the monsoon flag: `0x208080`; monsoon today: `0x30E0E0` |
| `CLIMATE_LAYER_CLIMATE_CLASS` | Sea from phase 03's water colours. Ice `0xFFFFFF`, Tundra `0xB0B8C0`, Desert `0xF0D080`, Steppe `0xD0B060`, Rainforest `0x006020`, Monsoon `0x208040`, Savanna `0xA0C040`, Mediterranean `0xE0A040`, Temperate `0x60B060`, Continental `0x6080C0`, Boreal `0x305060` |
| `CLIMATE_LAYER_SNOW_BALANCE` | Land at or above its `snow_balance_z`: white `0xFFFFFF`. Below: grey from `0xA0A0A0` (within 50 blocks) to `0x404040` (2000 or more below). Sea: water layer |

### Threads, determinism and cost

Nothing new about threads: the work runs inside phase 03's tile job, one worker per tile, every loop south row first and x fastest, every sum in a fixed order. So the same bytes come from 1 worker or 16 (master plan 12). The rain scratch is $256 \times 12$ floats, 12 KiB, from the Job arena; nothing large goes on the stack (`-Wframe-larger-than`). The rain-belt spline lives in the climate's `arena_tables`, built once in `climate_init`, read-only after.

`climate_normals_at` allocates nothing and may be called from any thread, as phase 03's reads are.

### Edge cases

| Case | What happens |
|---|---|
| The south | Every yearly wave is multiplied by $s_h = -1$, so the year runs six months later. The rain belts move with $\psi$, which uses $\delta$, which is antisymmetric over half a year, so rain does too. Tested |
| Mirrored strips (phase 04's $e = -1$, past a pole) | Temperature and rain read only $\lvert\varphi\rvert$, $s_h = \operatorname{sgn}\varphi$, $\kappa$, the current and the wind. None of these needs $e$ here: phase 04's winds already carry it. Tested with phase 04's mirrored continent |
| A pole | $\lvert\varphi\rvert \le 90$; knots end at 90°, and the spline stays at the end's value beyond it |
| The equator | $s_h = +1$ at exactly 0 (phase 04's rule). Hurricanes: $s_{\text{hur}} = 0$ within 5° |
| No wind (length under $10^{-3}$) | The walk uses the cell's own sea distance, as in step 1a |
| A walk leaving the window | Reads phase 03's mask, built privately if not cached. The answer is a function of the cell alone, so two tiles never disagree about one cell |
| A desert with a fit dipping below 0 | Every reader clamps $P(d)$ at 0. The yearly total $P_0 \times 365/7$ is never negative, since $P_0$ is the mean of non-negative values |
| A season wrapping past day 364 | Stored with first greater than last; `climate_season_hurricane_today` handles it. Tested in the south |
| Land below sea level | Phase 03 marks every cell with ground at or below 0 as water (sea or lake), so $B$ never applies to a cell today. It applies to a **point** with $z < 0$ under a land cell (a mine), and to a geology plan's dry basins later. The formula is tested directly (Issues found 3) |
| Snow balance out of range | -250 if snow lasts even there, 9999 if it never lasts below 2000 |
| Negative coordinates | Nothing new: phase 03's floors and window indexing |

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/climate/heat/`, `run/{src,include}/climate/rain/`, `run/{src,include}/climate/season/` (NEW, each with `README.md` and one `index.*`)
- `run/{src,include}/climate/normals/` (the rain-belt knots), `run/{src,include}/climate/harmonic/` (the two-wave fit)
- `run/{src,include}/climate/cell/` (the three calls after `climate_air_build_tile`), `run/{src,include}/climate/layer/` (ten layers)
- `run/{src,include}/climate/index.{c,h}` and `run/src/climate/README.md` (`climate_normals_at`)
- `run/{src,include}/climate/air/`: only making the window-current function public, if it is `static`
- `tests/run/climate/heat/`, `tests/run/climate/rain/`, `tests/run/climate/season/` (NEW); `tests/run/climate/harmonic/`, `tests/run/climate/layer/` (added tests); `tests/run/climate/cell/index.c` (its digest pin, re-set with a line saying why); `tests/run/climate/fixtures/ground.h`
- `bench/climate/` (the new layers in `--pictures`, a `--days` list, the tile timing re-run)

**Wiring outside them:** `plans/REPO_STRUCTURE.md`, rows for `climate/heat`, `climate/rain`, `climate/season` and their tests. Nothing else. No terrain file changes in this phase.

## Steps

Build order. Each step builds and passes its own tests before the next starts.

1. **Read what phases 02 to 04 built.** Find latitude, declination, the cell and window structs, the mask read, `climate_sea_temperature`, the normals part, the harmonic part, and phase 04's window-current function. Write any name that differs from this file's under Issues found. *Why: every formula here reads them, and a second copy of any of them would break "one home".*
2. **Add the rain-belt knots** to `run/src/climate/normals/index.c` and build their spline in `climate_init`. Test in `run_climate_rain`: the spline passes through all nine knots exactly, and gives 426.7 at 25° (±0.1). *Why: the belts are the start of every rain number.*
3. **Add `climate_harmonic_fit2` and `climate_harmonic_at2`** in `run/src/climate/harmonic/index.c`, with the second exact table. Tests in `run_climate_harmonic`. *Why: the fit is small and exact, and is tested alone before it hides in a tile.*
4. **Create `run/src/climate/heat/`**: the formula, `climate_heat_from_place`, the daily range, the snow share, `climate_heat_build_tile`. With `tests/run/climate/heat/`: the three worked examples, cooling with height, the south's six months. *Why: temperature is the first normal, and everything after reads it.*
5. **Create `run/src/climate/rain/`**: the walk (window, then mask), $M$, $O$, the belt on 12 days, the fit, `climate_rain_build_tile`. Add the South Africa continent and a ridge to `tests/run/climate/fixtures/ground.h`, and a test hook (below) to freeze the seasons and to override $P_{\text{belt}}$. With `tests/run/climate/rain/`. *Why: rain is the hardest number here, and Ahmed's sketch is its test.*
6. **Create `run/src/climate/season/`**: $s_{\text{hur}}$, the longest-run season and its packing, the coast flag, the monsoon flag and monsoon today, the snow balance, the monthly means and the classes. With `tests/run/climate/season/`. *Why: phase 06's spawn rates and the HUD labels read these.*
7. **Call the three builders** from `climate_cell_build_tile` in `run/src/climate/cell/index.c`, after `climate_air_build_tile`. Re-run `run_climate_cell`: the 1-and-16-workers test must still pass, and the digest pin moves on purpose. Re-set it with a comment line saying "phase 05 filled t_mean, rain, snow balance, class, seasons". *Why: from here every tile carries its normals.*
8. **Add `climate_normals_at`** to the Owner, `run/src/climate/index.c`. Test in `run_climate_heat` that at cell centres it equals the formula on that cell's own numbers, and that with the warp off a point between four cells is their bilinear blend. *Why: this is what phases 06 to 10 call.*
9. **Add the ten layers** to `run/src/climate/layer/index.c`. Tests in `run_climate_layer`. *Why: Ahmed checks everything by looking (vision rule 4).*
10. **Extend `bench/climate/`**: `--pictures` draws the new layers, and `--days 15,46,76,...` draws the day-dependent ones for each listed day, named `<layer>_d<day>.png`. Re-run the tile timing on release and record it. *Why: the gate's pictures, and the 100 ms budget.*
11. **Update `plans/REPO_STRUCTURE.md`** and write each new `README.md`.
12. **Run the whole debug suite**, `standards_lint` and `clang_format`, then the release digest check.
13. **Draw the gate pictures**, show Ahmed, and tick the todo as each item lands.

**The test hook.** One struct, held by `climate_t` (phase 03) beside phase 03's warp-off hook, set only by tests through `climate_test_hooks_set` in `run/include/climate/index.h`, documented as for tests only, all off (zeros) in every real run:

```c
/* For tests only: each field at 0 means off. */
typedef struct climate_test_hooks_t {
    float32_t wind[2];        /* 0: if either is not 0, the rain walk and monsoon test use this wind on every day */
    float32_t belt_mm_year;   /* 8: if above 0, P_belt is this constant */
    uint8_t freeze_seasons;   /* 12: δ taken as 0 in ψ */
    uint8_t orographic_one;   /* 13: O forced to 1 */
    uint8_t _pad0[2];         /* 14 */
} climate_test_hooks_t;
static_assert(sizeof(climate_test_hooks_t) == 16);
void climate_test_hooks_set(climate_t *climate, const climate_test_hooks_t *hooks);
```

`climate_t` grows by 16 bytes from its padding, or its size assert is updated if none is left. *Why: the master plan's South Africa numbers assume fixed trade winds, a fixed belt of 500 and (for the west coast) no shadow, and the hooks are how the test states those assumptions instead of hiding them.*

## Todo

- [x] Phases 02 to 04's names found; differences written under Issues found (Issues found 7)
- [x] Rain-belt knots and spline in `run/src/climate/normals/`
- [x] `climate_harmonic_fit2` and `climate_harmonic_at2` in `run/src/climate/harmonic/`
- [x] `run/src/climate/heat/`: temperature (6.6) with all knots in one table, the daily range, the snow share, `t_mean` per tile
- [x] `run/src/climate/rain/`: the upwind walk (window, then mask to 8 km), moisture, orographic factor and rain belts (6.7), for 12 days, fitted to two harmonics
- [x] `run/src/climate/season/`: hurricane season and `hurricane_days` packing, hurricane-coast flag, monsoon flag and monsoon today (6.8)
- [x] Snow balance height, and climate classes (6.10)
- [x] The three builders called from `climate_cell_build_tile`; `run_climate_cell` pin re-set with a line saying why (called from the Owner's tile job, where phase 04's air builder is: Issues found 8; pin `0xB667CA61EBBB4C6F`)
- [x] `climate_normals_at` in the Owner (its struct is `climate_normals_at_t`: Issues found 7)
- [x] Test hooks: freeze the seasons, override the belt
- [x] Map layers: temperature (day, mean, warmest, coldest), rain (year, day), hurricane season, monsoon, climate class, snow balance
- [x] Tests: the worked examples of 6.6 to 6.8 on synthetic ground, within 10%; temperature falls with height; a ridge's lee is drier than its windward side; south runs six months from north
- [x] Ahmed's South Africa test: a synthetic continent with his sketch's profile at 25° south gives a wet east coast, a wettest escarpment, a dry plateau and a desert west coast
- [x] 1 and 16 workers give the same bytes; release prints the same digest as debug
- [x] Bench: one tile on one worker still under 100 ms, numbers recorded here and in `bench/climate/README.md`
- [x] A `README.md` in every new source directory, exactly one `index.*` in each
- [x] `plans/REPO_STRUCTURE.md` updated
- [x] The whole debug suite passes; `standards_lint` and `clang_format` pass
- [ ] Gate: Ahmed reviews the maps over a year, with the day list as the slider

## Tests

Names in the repo's sentence style. "Fails if" says what a wrong build looks like. Where a number comes from the master plan with straight-line knots, the test checks it within 10%, and checks the spline's own number tightly.

### `run_climate_harmonic` (added)

- **`two_pure_waves_come_back_exactly`.** $f_k = 5 + 2\cos\theta_k - 1\sin\theta_k + 3\cos 2\theta_k + 4\sin 2\theta_k$ gives $(5, 2, -1, 3, 4) \pm 10^{-5}$, and `climate_harmonic_at2` gives all 12 values back to $10^{-5}$. Fails if a sum uses the wrong table or a divisor is 12 instead of 6.
- **`the_worked_example_rain_fits_within_two_mm`.** $f = (40, 55, 62, 50, 35, 30, 38, 52, 60, 48, 34, 31)$ gives $P_0 = 44.583$, $P_{1c} = 0.994$, $P_{1s} = 0.933$, $P_{2c} = -5.250$, $P_{2s} = 14.289$ (each $\pm 10^{-3}$), and every fitted value is within 1.8 mm of its $f_k$.
- **`the_two_wave_fit_agrees_with_the_one_wave_fit_on_wave_one`.** For random $f$, `fit2`'s first three numbers equal `fit1`'s to $10^{-6}$. Fails if the orthogonality were broken.

### `run_climate_heat`

- **`the_equator_beach_is_26_to_28_all_year`.** `climate_heat_from_place` with $\varphi = 0$, $z = 3$, $\kappa = 0.05$, no current, land: every day of the year between 25.90 and 28.02, the coldest $25.914 \pm 0.01$, the warmest $28.014 \pm 0.01$. Fails if $A$ uses $\kappa$ the wrong way round (then $A = 1.95$, and the range is 25.0 to 29.0).
- **`the_45_north_peak_is_minus_10_4_in_january_and_8_4_in_july`.** $\varphi = 45$, $z = 1000$, $\kappa = 1 - e^{-0.8}$, no current, land. Day 15: $-10.50 \pm 0.02$, and within 10% of the master plan's $-10.4$. Day 196: $8.47 \pm 0.02$, within 10% of $8.4$. Fails if the lag is missing (day 15 would give $-10.87$, still close, so also:) the warmest day is $204 \pm 1$. Fails if $\ell$ is added with the wrong sign (warmest day 136).
- **`land_150_below_sea_level_at_28_north_is_34_5`.** $y = 11\,915$, $z = -150$, $\kappa = 1 - e^{-4000/1500}$, land, day 196: $34.63 \pm 0.05$, within 10% of 34.5; with the dry daily range, the high is $40.35 \pm 0.1$. The same with `land` false gives $30.88$ (no $B$). Fails if $B$ is missing or applies at sea.
- **`temperature_falls_twelve_degrees_per_thousand_blocks`.** For $z = 0, 250, 500, 1000$ on land: $T(z) - T(z + 1000) = 12.000 \pm 10^{-3}$. For $z = -200$ on land, $T(-200) - T(0) = 200 \times 0.037 = 7.40 \pm 10^{-3}$.
- **`the_south_runs_six_months_from_the_north`.** On a synthetic ground symmetric in $y$ (phase 04's mirrored-coast fixture, reflected), `climate_normals_at(x, y, z, d)` equals `climate_normals_at(x, -y, z, d + 182.5)` (days taken modulo 365) for temperature to $10^{-3}$ and rain to $10^{-3}$ mm, at 100 places and 12 days. Fails if $s_h$ is missing from a wave, or the rain belt ignores the sign of $\psi$.
- **`the_daily_range_is_the_worked_example`.** $\kappa = 0.9305$, no rain: 11.444. 6 mm a day: $11.444 \times (1 - 0.6 \times 0.6321) = 7.104 \pm 10^{-3}$.
- **`snow_share_follows_its_ramp`.** $T = 2, 1, 0, -1, -2, -3$ give $0, 0, 1/3, 2/3, 1, 1$.
- **`the_read_equals_the_formula_at_cell_centres`.** With the warp off (phase 03's hook), at 64 cell centres on a synthetic coast, `climate_normals_at` equals `climate_heat_formula` on that cell's own fields bit for bit, and between four cells it is their bilinear blend to $10^{-5}$.
- **`t_mean_is_stored_at_sea_level`.** A cell with `z_ground = 600` has `t_mean` equal to $\bar T_{\text{lat}} + C_{\text{eff}}$, and the read at $z = 600$ is 7.2 °C colder than at $z = 0$.

### `run_climate_rain`

- **`the_belt_spline_passes_through_its_knots`.** Exact at 0, 10, ..., 90. At 25°: $426.7 \pm 0.1$. At 13.26°: $1453.8 \pm 0.5$.
- **`the_walk_counts_land_since_the_water`.** Synthetic straight coast along $x = 0$, sea west, wind from the west: a cell centred at $x = 1000$ meets water at step 8, $d_{\text{up}} = 896$. A cell at $x = 32$: step 1, $d_{\text{up}} = 0$. Fails off by one step either way.
- **`the_walk_continues_past_the_window_over_the_mask`.** A synthetic world with no water within the window, and sea starting 9000 blocks upwind of a cell near the window's downwind edge: the walk meets it over the mask at step $\lceil 9000/128 \rceil$ or the next, and `water_current` is `NONE`. With the sea 9000 blocks further away: `met_water = 0`, $d_{\text{up}} = 8192$, $M = 0.15$.
- **`moisture_is_the_worked_examples`.** $d_{\text{up}} = 0$ over a warm current: 1.25. $d_{\text{up}} = 1000$, warm: $0.8957 \pm 10^{-4}$. $d_{\text{up}} = 8192$, within 800 of a cold current: $0.06 \pm 10^{-6}$. Cold wins over warm.
- **`orographic_factor_is_the_worked_examples`.** $s_w = 0.301$: 2.5 (clamped from 3.41). Flat, $h_{\text{up}} - z = 250$: 0.375. $h_{\text{up}} - z = 1000$: 0.15 (clamped).
- **`south_africa_with_the_plan_s_belt_gives_625_1125_69_and_30`.** The South Africa continent at 25° S (fixture below) with the hooks on (seasons frozen, wind fixed at $(-6, 0)$ from the east, $P_{\text{belt}} = 500$). First the inputs, so a wrong fixture fails loudly: east coast $d_{\text{up}} = 0$ with a warm current; escarpment $d_{\text{up}} = 1024$, warm, $s_w \ge 0.3$; plateau $d_{\text{up}} = 2944$, met over the mask (neutral), $h_{\text{up}} = 750$, $z = 500$, $s_w = 0$; west coast no water met, cold current within 800. Then yearly rain, each within 10% of the master plan: east coast $644 \pm 2$ (625; the 1-block coast gives $O = 1.031$), escarpment $1111 \pm 5$ (1125), plateau $70.3 \pm 0.5$ (69). West coast: $4.5 \pm 0.1$, **not** the master plan's 30, because the formula puts it in the plateau's rain shadow (Issues found 1); and with $O$ forced to 1 by a third hook value, $30.0 \pm 0.1$. And the ordering: escarpment > east coast > plateau > west coast, and the west coast and plateau both Desert by 6.10. Fails if any of $M$'s cases, the walk's step count, the window/mask split, or $O$'s shadow term is wrong; each moves one place out of range.
- **`south_africa_with_the_real_belt_frozen_scales_by_the_spline`.** Seasons frozen, wind fixed, no belt override: each of the four is $0.853 \pm 0.01$ times the previous test's value (549, 948, 60.0, 3.8). Fails if the belt is read at $\varphi$ instead of $\psi$ in some path.
- **`south_africa_over_a_real_year_is_printed`.** No hooks. Prints each place's yearly rain, its wettest and driest month, and the wind on days 15 and 197. Checks only that each is finite, at least 0, and that the escarpment is wetter than the plateau. Ahmed sees the printout at the gate.
- **`a_ridge_s_lee_is_drier_than_its_windward_side`.** Synthetic ridge 400 high running north-south at 15° N, trades from the east, seasons frozen: a cell 500 blocks east of the crest (windward) has at least 3 times the rain of one 500 blocks west (lee). Fails if $h_{\text{up}}$ or $s_w$ is read downwind.
- **`rain_is_never_negative_when_read`.** 10,000 random places and days on a real seed: `rain_mm_day` at least 0.

**The South Africa fixture,** in `tests/run/climate/fixtures/ground.h`. The height depends on $x$ only, the same for every $y$, and the test reads it around $y = -10\,656$ (the cell centre nearest 25° S, at 25.04°). Piecewise straight lines, in blocks:

| $x$ from | to | Height |
|---|---|---|
| $-\infty$ | 0 | $-50$: the west sea (a west coast at 25°: cold, by phase 04's table) |
| 0 | 1000 | from 1 up to 500 |
| 1000 | 10 000 | 500: the plateau |
| 10 000 | 10 400 | from 500 up to 750: the crest |
| 10 400 | 10 912 | from 750 down to 250 |
| 10 912 | 11 742 | from 250 down to 1: the escarpment face, 250 over 830 blocks |
| 11 742 | 11 968 | 1: the coastal strip |
| 11 968 | $+\infty$ | $-50$: the east sea (an east coast at 25°: warm) |

The four places are cell centres ($64k + 32$), so every step of the walk lands on a known cell, with the wind fixed from the east, $\hat w = (-1, 0)$, and the walk going toward $+x$:

| Place | Cell centre $x$ | First water | $d_{\text{up}}$ | Water's current | $h_{\text{up}}$, $z$ | $s_w$ |
|---|---|---|---|---|---|---|
| East coast | 11 936 | step 1 (12 064) | 0 | warm (window) | 1, 1 | $1/256$ |
| Escarpment | 10 912 | step 9 (12 064) | 1024 | warm (window) | about 255 (its own ground: everything upwind is lower), about 255 | $(255.4 - 173.2)/256 = 0.32$ |
| Plateau | 8 992 | step 24 (12 064) | 2944 | none: step 24 is over the mask | 750 (step 11 lands on 10 400 exactly), 500 | 0 |
| West coast | 32 | none by step 64 (8224) | 8192 | none (its own nearest sea is cold, 32 blocks away) | 500, 17.0 (the mean of 5.0, 13.0, 21.0, 28.9) | 0 |

The plateau's 2944 and the escarpment's 1024 are the nearest 128-block steps to the master plan's 3000 and 1000. North-south the fixture is uniform, so no edge is ever in reach.

**Test hooks for this test:** wind fixed at a given vector, seasons frozen ($\delta = 0$ in $\psi$), $P_{\text{belt}}$ overridden, and (for the one check against the master plan's west coast) $O$ forced to 1. See Steps for the hook.

### `run_climate_season`

- **`hurricane_season_at_15_north_over_a_warm_current`.** `climate_season_hurricane_s` with $\varphi = 15$ and $T_{\text{sea}}$ from `climate_sea_temperature` on a synthetic sea cell with $C_{\text{eff}} = +4$: day 215 gives $T_{\text{sea}} = 31.95 \pm 0.01$, $s = 1$. Day 32: $28.05 \pm 0.01$, $s = 0.775 \pm 0.005$ (master plan 0.75, within 10%). With no current, day 32: $24.05$, $s = 0$. Stored season with no current: `{39, 68}` (days 156 to 274). With the warm current: `{0, 91}`, all year (Issues found 2).
- **`no_hurricane_season_within_5_degrees_or_past_25`.** At $\varphi = 4.99$ and $25.01$, $s = 0$ for any $T_{\text{sea}}$ up to 40. At $5.00$ and $25.00$ it can be above 0.
- **`hurricane_days_pack_into_quarter_days`.** Day 364 stores 91. A southern sea at 15° S with no current: the season wraps, first greater than last, and `climate_season_hurricane_today` is true on days 0 and 350 and false on day 182. No season stores `{255, 255}` and is never "today".
- **`a_coast_within_3_km_of_a_hurricane_sea_is_flagged`.** Synthetic coast at 15° N: land at 2000 blocks from the sea has the flag and the sea's days; at 4000 it has neither.
- **`a_monsoon_coast_is_flagged_and_a_steady_one_is_not`.** On synthetic cells with set wind and rain numbers: the Background's worked example sets the flag; the same with winter wind also from the sea does not; the same with rain halves 180 and 70 does not ($180 < 210$).
- **`monsoon_today_is_the_wet_onshore_half`.** For the flagged cell: true on a summer day, false on a winter day.
- **`snow_balance_is_the_worked_examples`.** On synthetic cells with rain spread evenly over the year ($P_0$ only): 45° N, $\kappa = 0.5507$, 900 mm: $1484 \pm 16$. 60° coast, 800 mm: $325 \pm 16$. 75° coast, 350 mm: exactly $-250$. A cell at the equator: 9999. And the height falls with latitude between 45° and 75°. Fails if melt or the snow share is inverted (the height goes the wrong way with latitude).
- **`classes_follow_the_first_match`.** A table of 14 cases, one per class plus three order checks: $T_{\text{max}} = -1$ with 3000 mm is Ice, not Rainforest; $T_{\text{max}} = 8$ with 50 mm is Tundra, not Desert; $T_{\text{min}} = 20$ with $P$ under $P_{\text{th}}/2$ is Desert, not Savanna. Each case with its five numbers worked out in the test.
- **`the_45_north_peak_is_tundra_and_the_plateau_is_desert`.** Through the tile on synthetic ground: the peak's warmest month $8.45 \pm 0.05$, class Tundra; the South Africa plateau, Desert; sea cells, `NONE`.

### `run_climate_cell` (re-run, pin re-set)

- **`one_and_sixteen_workers_give_the_same_bytes`** (phase 03's test): still equal; the digest is re-set with a comment line saying phase 05 filled `t_mean`, rain, snow balance, class and seasons, and why that moves it.
- The release preset must print the same digest as debug: `-O3 -flto` must move no bit.

### `run_climate_layer` (added)

- **`each_new_layer_is_the_colour_the_readme_says`.** On synthetic cells with known values: 0 °C is `0xF0F0F0`, 15 °C `0xF0D060`; 1000 mm is `0x40A040`; Desert is `0xF0D080`; land above its snow balance is white.
- **`the_day_moves_the_day_layers_and_not_the_others`.** Drawing temperature-day on days 15 and 197 at 45° N differs; temperature-mean does not.
- **`unbuilt_tiles_are_grey`.** For every new layer.
- **`a_layer_picture_is_held_to_a_hash`.** Each new layer at 512 by 512 on the real seed, on days 15 and 197 for the day layers. The hashes are set only after Ahmed has looked at the pictures, as `run_ui_menu` does.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_climate_(harmonic|heat|rain|season)$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_climate_(cell|layer)$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^run_climate_cell$' --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate
    tools/test-or-build.sh timeout 600 build/release/bin/bench_climate --pictures /tmp/mc_c_climate 2198175988018515243 --days 15,46,76,107,137,168,198,229,259,290,320,351
    pgrep -a minecraft_c    # must print nothing

Every `run_terrain_.*` test must pass unchanged: this phase touches no terrain file. The release run of `run_climate_cell` must print the same digest as debug.

**The bench** (`bench/climate/index.c`), on release:

- **Cold tile, 1 worker:** as phase 03, now with phases 04 and 05 inside. **Exit 1 if any is over 100 ms.**
- Prints the share of each part: phase 03, air (phase 04), heat, rain (and its mask reads beyond the window), season (and the snow-balance bisection on its own).

## Gate

Ahmed reviews the maps over a year, with the day list as the slider (master plan phase 05 gate). The Atlas (phase 09) doesn't exist yet, so the slider is a folder of pictures, one per month.

**What he opens.** `/tmp/mc_c_climate/` after the last command above, for seed `2198175988018515243` and two others, 16 km by 16 km around spawn:

- `temperature_day_d015.png` to `temperature_day_d351.png`: twelve pictures, one a month. Stepping through them in any image viewer that walks a folder is the slider. Cold should spread from the poles in winter and pull back in summer; coasts should swing less than inland; mountains should be colder.
- `temperature_mean.png`, `temperature_warmest.png`, `temperature_coldest.png`.
- `rain_year.png` and `rain_day_d015.png` to `rain_day_d351.png`: wet tropics, dry belts near 30°, wet windward slopes, dry lee sides.
- `hurricane_season_d015.png` to `..._d351.png`: red seas between 5° and 25° in late summer, nothing near the equator.
- `monsoon.png`, `climate_class.png`, `snow_balance.png`.
- The synthetic South Africa continent's pictures (`--synthetic`), and the printout of `south_africa_over_a_real_year_is_printed`.

**The map tool.** If `bench/world_map/` (003 phase 8 stage 1) exists when this phase is built, the layers are added to it and drawn there with the same `climate_layer_draw`. On 2026-11-04 it does not exist, so `bench_climate --pictures` draws them, as phases 03 and 04 set out. The layer hashes in `run_climate_layer` are set only after Ahmed says yes.

## Risks and open questions

| Risk or question | Default |
|---|---|
| **The South Africa numbers hold only with the seasons frozen and $P_{\text{belt}} = 500$** (Issues found 1) | Test both ways, as above. Ahmed sees the real-year printout at the gate and decides whether the moving belt is what he wants |
| **Warm-current coasts at 15° have hurricane season all year** (Issues found 2) | Built as written. The picture shows it. Phase 06's counts are tuned by playing, as the master plan says (Q5) |
| **The tile may pass 100 ms** with the walk and the snow bisection added | Measure. If over: the bisection can start from the previous cell's answer with a narrower range (same result, fewer halvings, as long as the range still contains it; checked by falling back to the full range when it doesn't), and the walk can stop at the first water it meets (it already does). Record the numbers here |
| Snow balance from a model without storms may be too high or low against phase 07's real weekly snow | Phase 07's test "a cell at 75° keeps snow all year" checks it. If they disagree, the balance height is recomputed from phase 07's state at the census, which Ahmed decides |
| Two wet seasons need the second wave; places with sharper seasons lose detail | As the master plan chose (6.9): two waves. The fit's worst miss is printed by the harmonic test on real cells |
| A large lake inland stops the walk and resets moisture, as the master plan says, but a lake under 2 km² does not | As written. A lake over the mask (beyond the window) counts whatever its size, because the mask can't tell (Edge cases) |
| Monsoons will be rare with phase 04's numbers (phase 04, Issues found 1) | Built exactly; Ahmed's choice at phase 04's gate carries here |
| `t_mean` stored at sea level, where the master plan's comment says "the yearly mean" | Kept, for the reason in Design. The ground's mean is one line away, and `climate_normals_t.t_mean_c` gives it at any $z$ |

## Results

Run by the agent on 2026-11-06 in its worktree, every heavy command through `tools/test-or-build.sh`.

- `run_climate_harmonic`, `run_climate_heat`, `run_climate_rain`, `run_climate_season`, `run_climate_cell`, `run_climate_layer`, every other `run_climate*`, `standards_lint`, `clang_format`: all pass on debug.
- Worked examples, as the tests print them: equator beach 25.914 to 28.014 °C; 45° N peak -10.50 °C on day 15, 8.47 on day 196, warmest day 204; 28° N basin 34.63 °C, high 40.35, 30.88 without `B`; the south equal to the north half a year on at 1200 pairs; the read bit for bit equal to the formula at 64 cell centres. South Africa with the plan's assumptions: east coast 644.5, escarpment 1110.7, plateau 70.3, west coast 4.5 mm (30.00 with `O = 1`), plateau and west coast Desert; with the real belt 0.850 of each (the cell is at 25.04°); real year: east coast 646, escarpment 1101, plateau 116, west coast 141 mm (west coast wettest in month 7, its winter: Issues found 1's Cape). Ridge at 15° N: windward 454, lee 94.5 mm, 4.8 times. 15° N sea: 31.95 °C on day 215, 28.05 on day 32 (`s = 0.775`), season `{39, 68}` without the current, `{0, 91}` with it; 15° S wraps as `{84, 23}`. Snow balance: 45° N 1483.9, 60° coast 324.9, 75° coast -203.0 with `B` (-250 without, Issues found 9), equator 9999. 45° N peak (1184 blocks in, 45.05°): warmest month 8.386, Tundra. Year's mean of `P_belt` at 25° S: 721.1 mm.
- `run_climate_cell`: 1 and 16 workers the same bytes, digest `0xB667CA61EBBB4C6F`; the release run passes against the same pin.
- Release build links, `-Wframe-larger-than` included (nothing was added to `app_t`: the hooks sit in `climate_state_t`'s old padding, in the climate's region).
- `bench_climate` on release, exit 0: cold tile 29.5 to 30.8 ms on one worker (target 100), warm 7.91 ms. A tile's parts on one thread: window 24.25 ms, cells 3.63, air 0.10, heat 0.002, rain 0.51, season 1.78 (snow balance 1.07 of it). Phase 05 costs about 2.3 ms a tile.
- `bench_climate --pictures /tmp/mc_c_climate 2198175988018515243 --days 15,46,...,351`: 68 pictures; `--synthetic /tmp/mc_c_climate_synthetic --days 15,107,198,290`: 41. Looked at by the agent only (contact sheets): hot all over (the 16 km round spawn is 18.8° S to 18.8° N), wet seas near the equator, a step at the equator in the day's temperature (Issues found 10), hurricane seas red in the northern band on day 229 and none within 5°. Ahmed has not seen them.
- The whole debug suite (`ctest --preset debug`, 136 tests, every `run_terrain_*` among them unchanged): 100% passed, 1917.6 s.

## Commits

Appended as they land:

- `6f94c352` Add plan 004 phase 05's temperature, rain, hurricane seasons, monsoons, snow balance and climate classes to every climate cell, read them with climate_normals_at, draw ten new layers, and test them on synthetic ground and Ahmed's South Africa sketch.
- `e3f4a989` List the climate's heat, rain and season parts of plan 004 phase 05 and their tests in the repository structure.

## Issues found

Found while writing this file, by checking the master plan's arithmetic. None changes anything that exists. Each has a default, built until Ahmed decides.

1. **The South Africa example holds only for a fixed belt at 500 mm and fixed trade winds.**
   - **Expected** (master plan 6.7): $P_{\text{belt}}(25°) = 500$, giving 625, 1125, 69 and 30 mm a year.
   - **Actual:** (a) $P_{\text{belt}}$ is read at the wind latitude $\psi = \varphi - 0.5\,\delta(d - 30)$, which at 25° S moves between $-13.3°$ and $-36.7°$ through the year. The year's average of $P_{\text{belt}}$ there is **721 mm** with the spline (753 with straight lines), not 500. (b) 500 is the straight-line value; the spline that phase 04 chose (its Issues found 3) gives **426.7**, 14.7% lower, so phase 04's note that "phase 05's within 10% tests are not affected" is not true for rain. (c) On day 197 the wind latitude is $-36.7°$, in the westerlies, so the wind reverses for the southern winter and the west coast becomes windward.
   - **Evidence:** $\delta(167) = 23.5\sin(2\pi \times 88/365) = 23.46$; $\psi = -25 - 11.73 = -36.73$. The spline through the 6.7 knots with the repo's slope rule (`run/src/terrain/density/index.c:117-147`), worked out in this agent's scratch space: 426.7 at 25°, and a 365-day mean of 721.1.
   - **And the west coast's 30 mm uses $O = 1$,** but by 6.7's own $O$, air that has just come down 500 blocks off the plateau is in its rain shadow: $O = 1 - (500 - 17)/400 = -0.21$, clamped to 0.15, so $500 \times 0.06 \times 0.15 = 4.5$ mm. Still the driest place and still a desert (6.10), so Ahmed's sketch holds in kind.
   - **Also:** the plateau's 69 mm uses $M = e^{-1}$ with no warm-current 1.25, although its air came from the warm east sea. That only follows if water met beyond the window (step 17 on, over the mask) carries no current, which is how this file reads 6.7's "the walk goes on over the coarse land mask".
   - **Done:** the test runs the sketch with the seasons frozen, the wind fixed from the east and the belt at 500 (the master plan's own assumptions), where east coast, escarpment and plateau hold within 10%, and the west coast is checked both by the formula (4.5) and with $O$ forced to 1 (30). Then again with the spline (0.853 times each), and it prints the real year. Nothing in the formulas is changed. For Ahmed: the real-year model gives the south-west a winter-rain climate, like the real Cape, which may be welcome.

2. **"Near 1 for about 120 days" doesn't fit a warm-current coast.**
   - **Expected** (master plan 7.2's worked example, built on 6.8's): a warm-current coast at 15° N has $s_{\text{hur}}$ near 1 for about 120 days, so $0.02 \times 120 = 2.4$ hurricanes per square a season.
   - **Actual:** with $C = +4$, $T_{\text{sea}}$ never drops below 28.05 (day 32), so $s_{\text{hur}} \ge 0.775$ every day: the season never ends, and the sum of $s_{\text{hur}}$ over the year is **353**, which would give **7.1** per square a year. "About 120 days" is the **no-current** case: season from day 156 to 274 (119 days), sum of $s_{\text{hur}}$ **71.4**, which gives **1.43** per square. Also, 6.3's warm currents only reach 600 blocks offshore, and open sea at 15° to 45° is neutral, so most of a 16 km square is the no-current case anyway.
   - **Evidence:** 6.3 and 6.8's formulas, summed over 365 days in this agent's scratch space.
   - **Done:** nothing changed. This phase stores the season exactly; a warm-current cell at 15° gets `{0, 91}`. Phase 06's file carries the consequence for spawn counts. The master plan already says the rate is "one constant, tuned by playing" (Q5).

3. **The basin heat term can't fire on any cell today.**
   - **Expected** (master plan 2.1 and 6.6): "anywhere the ground is below sea level and not under sea, it gets the basin heat".
   - **Actual:** phase 03 marks every cell whose ground is at or below 0 as water (`water[i] = heights[i] <= 0`, phase 03 file, "What a tile job does", step 2), as the terrain fills all ground below 0 with water today. So no cell is "land below sea level", and $B$ applies only to points asked about with $z < 0$ under land cells (mines, for the HUD).
   - **Done:** the formula is built and tested directly (worked example 3). A geology plan that makes dry basins needs phase 03's water rule to change too; that's for that plan.

4. **Small arithmetic notes, all harmless.** 6.6 example 1 writes $\Gamma z$ as 0.04; it is 0.036. Example 2's $\cos(-3.25) = -0.99$ and $-10.4$ check out with straight lines. Example 3's 34.5 checks out with straight lines (34.63 with the spline). 6.7's escarpment: $500 \times 0.8957 \times 2.5 = 1120$, written as 1125 from rounding $M$ to 0.90. All within the tests' 10%.

5. **One home for the melt constant.** This phase's snow balance needs 7.5's 4 mm per degree per day, and phase 07's file lists `CLIMATE_MELT_MM_PER_DEGREE_DAY` among its own constants. This phase lands first, so the constant lives in `run/include/climate/season/index.h`, and phase 07 includes it instead of defining it again (vision rule 5).

6. **How the share of rainy days $r$ is found.** 6.6 gives the daily range with $r$, "the share of rainy days", but no way to get it from normals. This phase uses section 8's chance of a wet day, $1 - e^{-\bar P_{\text{day}}/6}$, the only formula in the plan that turns normal rain into a chance. Phase 07 may replace it with the real share from its weekly state for the census report; the normals keep this one.

Found while building, 2026-11-06:

7. **Names that differ from this file.**
   - **Expected:** `climate_normals_t` as the read's struct; `climate_heat_*`, `climate_rain_*` and `climate_season_build_tile` taking `const climate_t *`; `climate_rain_build_tile` taking an arena.
   - **Actual:** phase 04 already named its knot curves `climate_normals_t` (`run/include/climate/normals/index.h`), so the read's struct is `climate_normals_at_t`, 64 bytes, with a `z` field added (the height used; NaN in means the blended ground, which the layers use). The builders take `const climate_normals_t *` (the curves) and `const climate_test_hooks_t *`, as phase 04's `climate_air_build_tile` does, since parts below the Owner never see `climate_t`. The rain build needs no scratch (12 floats a cell, a local array), so takes no arena. `climate_heat_from_place` takes the curves too. The hooks type lives in `run/include/climate/rain/index.h`, the lowest part that reads it, and the Owner holds it in `climate_state_t` at 240, where 16 bytes of padding were (no size change). Declination is `climate_sun_declination_deg`, used through phase 04's `climate_air_wind_latitude`. The window-current function was `static air_sea_current`; it is now public as `climate_air_window_current(window, at, &strength)`, and the tile's own sea cells take their current from it, so there is one home (the same arithmetic in the same order; `run_climate_air` passes unchanged).
   - **Done:** used the built names; written here.

8. **The three builders are called from the Owner's tile job, not from `climate_cell_build_tile`.**
   - **Expected:** this file's step 7: `climate_cell_build_tile` calls them after `climate_air_build_tile`.
   - **Actual:** `climate_cell_build_tile` never called `climate_air_build_tile`; the Owner's `climate_tile_job` (`run/src/climate/index.c`) does, after the cell build. A sibling part calling three more would make `cell/` depend on everything after it.
   - **Done:** `climate_tile_job` calls heat, rain and season after air, in that order. `run_climate_cell`'s flag checks now mask phase 03's own flags (`CELL_KIND_FLAGS`), as phase 05 adds the monsoon and hurricane-coast flags beside them; that is the wiring in `tests/run/climate/cell/index.c` beside the pin.

9. **The 75° coast's snow balance is -203, not -250, once `B` is counted.**
   - **Expected:** "75° coast, 350 mm: exactly -250", and the design's "with `B(z)` when `z < 0` for land cells".
   - **Actual:** the two disagree. At -250 on land `B` adds 6.25 °C: the mean is -4.75 and the summer peaks at +3.2 °C, which melts 350 mm. With `B` the bisection gives -203.0; without it (a lake, `land` false) -250 exactly, which is the worked example's number.
   - **Evidence:** `run_climate_season` prints `75 coast -203.0`; `climate_season_snow_balance(&coast75, false) == -250`.
   - **Done:** kept the design's `B` for land; the test checks -203 ± 2 with `B` and exactly -250 without. Nothing in the world reaches it today (land below 0 is water, Issues found 3).

10. **The day's temperature steps at the equator.**
    - **Expected:** regions blend.
    - **Actual:** `t_swing` is `A s_h`, and `s_h` jumps from +1 to -1 at `φ = 0`; with `A` about 1 to 2 °C there, the daily mean steps by up to twice that across y = 0 on most days (the gate pictures show a line at the equator in `temperature_day_*`). It is the master plan's formula (6.6 with phase 04's `s_h = +1` at 0); the yearly mean, warmest and coldest month are smooth.
    - **Done:** nothing changed; for Ahmed at the gate. A smooth `s_h` (like phase 04's σ for the winds) would remove it if he wants.

11. **Two hand-worked numbers in the tests.**
    - `P_belt(13.26°)`: this file says 1453.8 ± 0.5; the repo's spline gives 1452.54 (0.09% apart, 25° is exactly 426.67 as written). The test checks 1452.54.
    - The walk past the window: this file puts the sea 9000 blocks upwind and expects it met at step ⌈9000/128⌉ = 71, which is past step 64, so it can never be met; and the window always holds the whole walk (a tile cell is 8192 blocks and half a cell from its edge), so "no water within the window" cannot hold with water met. The test puts the sea 7000 blocks upwind: met at step 55, past step 16, with no current; and 16,000 upwind: none met, `d_up = 8192`, `M = 0.15`.
    - The warmest month of the class rules is the month centred on the warmest day, `T̄ + 0.98862 |A|`, as this file's worked example (8.45) computes, not the warmest of the 12 calendar months (which gives 8.39 for the same peak); rain months are the 12 calendar months.
    - **Done:** as written in each test, with a comment saying why.

