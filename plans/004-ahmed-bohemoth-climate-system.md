# 004-ahmed-bohemoth-climate-system — master plan

**Status:** approved on 2026-11-06   (updated 2026-11-06)

On approval, `plans/README.md` applies: this file is copied verbatim, a setup agent creates one phase file per phase in section 10, and no code is written until Ahmed says go. **Nothing is pushed** (Ahmed, 2026-11-04: "No please don't push!").

## 0. Context

### Ahmed's words, 2026-11-02 to 2026-11-04

- "huge mountains, but sort of plateus in between! Like how mount everest has base-camp"
- "create winds please! And a map that i hit "M" to bring up map, it shows all generated chunks, and it shows winds, ocean current, etc... and I can filter temperature"
- "places available below sea level, similar to the dead sea, and these areas might be very hot"
- "an equator system assuming spawn near x,y 0,y that is the equator ... every 10km is on 23.5 degrees"
- "A single year is 365 days, altitude, distance from the ocean or any considerably large water bodies must somehow affect this hard-coded at generation time so we don't waste runtime compute"
- "3 different ocean currents/temperatures would be nice"
- "regions need to blend into each other"
- "every 7 days, when the sunrises, the entire world is paused, pause screen pops up everything is re-computed"
- "the user must be able to change filter to see wind current, ocean current, pressure systems like cyclones and anti-cyclones please, as well as rain pressure cells etc... to view incoming thunderstorms"
- "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please"
- "please drive a probability thing! but do not drive the actual physics! just create general code please that will sort of mimic it"
- "for now we are only creating a climate system and not yet a geological/rock system please!"
- "Vegetation systems not yet built into the plan please!"

### Decisions Ahmed made, 2026-11-04

| Question | Decision |
|---|---|
| Latitude scale | **The tropic line (23.5°) is 10 km from the equator.** A pole is 38.3 km from the equator |
| Directions | Positive y is north, positive x is east. (10, 10) is in the north-east quadrant |
| Year | **365 days.** The census every 7 days |
| Cooling with height | **Fast:** 12 °C per 1000 blocks, twice the real rate |
| Sun | Day length and the sun's path follow latitude and season. The sun moves **±23.5°** through the year. Polar night and midnight sun. Mobs spawn by real darkness |
| Atlas | **The game keeps running** while it's open. Only generated land is drawn. **Ungenerated land is grey** |
| Atlas layers | Wind, ocean currents, **pressure systems (cyclones and anticyclones)**, **rain cells and incoming thunderstorms** |
| Storms | **Spawned, not simulated.** Hurricanes and monsoons have seasons |
| Outlook | **Probabilities**, made by general code that mimics weather, not by physics |
| Geology | **Not in this plan.** No landforms, plateaus, massifs or basins. The climate reads whatever ground the terrain makes |
| Vegetation | **Not in this plan.** No autumn colours, and biomes are not changed to follow climate yet |

### What exists today (read from the repo, 2026-11-04)

| Fact | Where | What it means here |
|---|---|---|
| x is east, y is north, z is up, sea at `z = 0` | `run/include/render/frame/index.h:51` | Fits Ahmed's directions as they are |
| A day is 24000 ticks at 20 Hz | `run/include/game/index.h:34` | The calendar counts these |
| `world_ticks` is saved | `run/include/save/state/index.h:82` | Day, week and year come from it |
| `game_t.paused` exists, main to sim | `run/include/game/index.h:142` | The weekly pause uses it |
| Six noise "climate fields" | `run/src/terrain/climate/` | They are noise, not climate. Biomes keep reading them in this plan |
| Height comes from splines over those fields | `run/src/terrain/density/index.c:31-47` | The climate reads height through a new estimate function, and changes nothing in the terrain |
| No map tool yet | 003 phase 8 stage 1 plans `bench/world_map/` | This plan's pictures reuse that tool |
| Snow is a whole block | 001 phase 10 issue 8 | Thin snow needs a new block, which needs 003 phase 1 |

### Where this plan sits beside plan 003

- **Needs first:** 003 phase 10 (coordinates), 003 phase 11 (height to +1250), 003 phase 8 stage 1 (the headless map tool).
- **Changes no terrain.** The climate reads the ground and never changes what generates. So no world has to be thrown away for it, and no generator version bump is needed. The one exception is phase 09, which puts snow and ice on loaded ground, and that's state saved like any block edit.
- **Plateaus and Dead Sea basins** are for a later geology plan. The climate is ready for them: anywhere the ground is below sea level and not under sea, it gets the basin heat in 6.6.

---

## 1. Names

| Thing | Name | Code |
|---|---|---|
| The whole system | **Climate system** | — |
| Days, weeks, years, seasons, the weekly pause | **Calendar** | `run/{src,include}/calendar/` |
| Latitude, sun, sea distance, currents, pressure belts, wind, temperature, rain | **Climate** | `run/{src,include}/climate/` |
| Cyclones, anticyclones, hurricanes, thunderstorm cells | **Weather systems** | `run/{src,include}/climate/systems/` |
| The week's weather, snow, soil water, runoff | **Weather** | `run/{src,include}/climate/weather/` |
| Chances for the coming week | **Outlook** | `run/{src,include}/climate/outlook/` |
| The weekly re-calculation and its screen | **Census** | `run/src/ui/screen_census/` |
| The map on `M` | **Atlas** | `run/src/ui/atlas/` |

**One rename (Q1 in section 11):** `run/src/terrain/climate/` holds six noise fields, not climate. Phase 01 renames it to `run/src/terrain/fields/`. Only names change, and every pinned digest must hold.

Terms used below:

- **Pure function:** a function whose answer depends only on what it's given. `climate_at(seed, x, y, day)` always gives the same answer, whether or not anyone has been there. This is what makes an infinite world possible: nothing is simulated ahead of time.
- **Cache:** an answer kept after it's worked out once, so it isn't worked out again.
- **Cell:** a 64 by 64 block square. Climate is worked out once per cell, not per block.
- **Tile:** 16 by 16 cells, 1024 by 1024 blocks. Cells are worked out a tile at a time.
- **Bilinear blend:** a value between four cell centres, as a weighted average of the four, weighted by closeness. This is what makes regions blend.
- **Hash:** a function that turns numbers into a random-looking number, always the same for the same input. It's how a pure function decides "does a storm start here today?" with no list kept anywhere.
- **Poisson spawning:** deciding how many things start in a place and time from an average rate. A rate of 0.1 a day means about one every ten days, at random, with no pattern.
- **Deterministic:** the same seed gives the same bytes on 1 worker or 16. The repo's tests already hold terrain to this, and the climate is held to it too.

---

## 2. Where I disagree, and what I changed

Each changes a rule from what was asked, and each is for Ahmed to overrule.

### 2.1 Being below sea level barely heats anything on its own

At even the doubled cooling rate, 150 blocks below sea is only 1.8 °C warmer. The real Dead Sea is hot mostly because it sits in a dry belt near 31° N, enclosed by high ground.

**Changed:** land below sea level that isn't under sea gets an extra 2.5 °C per 100 blocks of depth, for the trapped air (6.6). The terrain makes no such land today. A later geology plan will.

### 2.2 "Sunrise" doesn't exist everywhere

The sun follows latitude now, so in polar winter it never rises. A pause "at sunrise" would never fire there.

**Changed:** the census fires at **06:00 on the world clock**, which is sunrise on the equator. It's the same moment everywhere.

### 2.3 A deterministic world has a perfect forecast

Storms are spawned by hash from the seed, so the future is already decided. Showing it exactly would make weather pointless to prepare for.

**Changed:** the outlook (section 8) works out chances. It adds growing uncertainty to known storms' tracks, and the average rate for storms that haven't formed yet. It never reads tomorrow's actual weather.

### 2.4 The Atlas could show climate beyond generated land, and won't

Climate is a pure function, so it's known everywhere. Ahmed decided the Atlas only draws generated land, with everything else grey. Storms crossing grey land are drawn as an outline only, so an incoming hurricane is still visible before it reaches explored land.

### 2.5 Storms spawn everywhere, but are only worked out where they matter

A hurricane can be born 40 km away and reach the player days later. To know what's near the player today, the game has to know every storm that started within reach in the last two weeks. **The way this stays cheap:** a storm's track is steered by the wind belts, which depend only on latitude, plus a land-or-sea check on the coarse land mask. So tracking a storm across unexplored land never needs a climate tile there. Section 7.3 has the cost.

### 2.6 Biomes stay as they are

Ahmed has said vegetation isn't part of this plan. So deserts don't move to where the climate says it's dry. The Atlas shows the climate class, so the mismatch is visible and can be fixed in the vegetation plan.

---

## 3. The order everything is worked out in

Strictly top to bottom. Nothing lower may be read by anything higher, so nothing is circular.

```
seed
 └─ terrain (exists, unchanged) → height estimate h(x, y), any point, no chunk      phase 03
     └─ latitude φ(y), sun δ(day)                                                    phase 02
         └─ land mask, sea distance, coast direction, continentality                 phase 03
             └─ ocean currents, pressure belts, heat lows and highs (monsoon)        phase 04
                 └─ wind by season                                                   phase 04
                     └─ temperature and rain normals, storm seasons                  phase 05
                         └─ weather systems: spawn, track, pressure, rain            phase 06
                             └─ daily weather, weekly state, census                  phase 07
                                 └─ outlook (chances)                                phase 07
                                     └─ weather in the world, Atlas, HUD             phases 08-10
```

**Normals** are the averages for each day of the year: what a place is usually like. **Weather** is what actually happens: normals plus storms plus noise. Normals are worked out once per cell and kept. Weather is worked out for the day being asked about.

---

## 4. Calendar

$$D = D_0 + \left\lfloor \frac{\text{world\_ticks} + t_0}{24000} \right\rfloor, \qquad \text{year} = \left\lfloor \frac{D}{365} \right\rfloor + 1, \qquad d = D \bmod 365, \qquad \text{week} = \left\lfloor \frac{D}{7} \right\rfloor$$

- $D$ is the day count. $d = 0 \ldots 364$ is the day of the year, shown as day $d + 1$.
- $t_0$ lines up day boundaries with 06:00 on the clock.
- $D_0 = 79$, so a new world starts on the spring equinox (Q2).

**Why weeks count from day 0 and not from each new year:** $365 = 52 \times 7 + 1$, so weeks counted within a year would leave a one-day week at the end. Counted from the world's first day, every week is 7 days.

**Worked example.** A world 1000 days old: $D = 79 + 1000 = 1079$. Year $= \lfloor 1079/365 \rfloor + 1 = 3$. Day of year $= 1079 - 730 = 349$, shown as day 350. Week $= \lfloor 1079/7 \rfloor = 154$.

### 4.1 Seasons, labelled by place

| Where | Label |
|---|---|
| North of the tropic | Spring from day 79, summer from 170, autumn from 262, winter from 353, each split into early, mid and late |
| South of the tropic | The same, half a year apart |
| Between the tropics | Wet season or dry season, from whether this cell's rain is above its yearly mean today |
| Past the polar circle, in polar night or midnight sun | Polar night, or midnight sun |
| Anywhere, as a second label | **Hurricane season** or **Monsoon** when 6.8 says so |

For example, "Mid summer · Hurricane season", or "Wet season · Monsoon".

### 4.2 The census, every 7 days at 06:00

1. Each tick, the sim checks whether `week` has passed `last_census_week`, which is saved.
2. If so, it sets `paused` and the census screen opens.
3. Workers recompute the weekly state (7.5) for every cell whose land has been generated. If several weeks were skipped (sleeping, a time jump, a long absence), it runs them one at a time, in order. Past 52 skipped weeks, the rest run in a fast mode that only keeps snow and soil water.
4. The screen shows the climate report and the outlook, then a **Continue** button.

**Why it can't drift or double-fire:** the trigger is "the week number went up", not "it's exactly 06:00 this tick". Sleeping or jumping time can't skip it, and it can't run twice for one week.

---

## 5. Latitude and the sun

### 5.1 Latitude

The tropic line (23.5°) is at $y = 10{,}000$. Latitude rises evenly with y, so a pole is at:

$$Y = 10\,000 \times \frac{90}{23.5} = 38\,298 \text{ blocks}$$

Latitude is a triangle wave, rising evenly to the pole, then falling evenly back:

$$\varphi(y) = \frac{180°}{\pi}\,\arcsin\!\left(\sin\frac{\pi y}{2Y}\right)$$

It repeats every $4Y = 153{,}191$ blocks: equator, north pole, equator, south pole, equator.

| Line | Latitude | y |
|---|---|---|
| Equator | 0° | 0, then 76,596 |
| Tropic | 23.5° | 10,000 |
| Polar circle | 66.5° | 28,298 |
| North pole | 90° | 38,298 |
| South pole | -90° | -38,298, and 114,894 |
| The whole pattern repeats | — | every 153,191 |

**Worked example.** At $y = 19{,}149$, halfway to the pole: $\sin(\pi \times 19149 / 76596) = \sin(\pi/4) = 0.707$, and $\arcsin(0.707) = \pi/4$, so $\varphi = 45°$ north. At $y = -19{,}149$, it's 45° south.

**Scale:** walking at about 4.3 blocks a second, equator to tropic is about 40 minutes, and equator to pole about 2.5 hours. x has no effect on latitude, and the world is infinite east and west.

### 5.2 The sun

The sun's **declination** $\delta$ is the latitude where it's straight overhead at noon. It moves between 23.5° north and 23.5° south through the year:

$$\delta(d) = 23.5° \, \sin\!\left(\frac{2\pi\,(d - 79)}{365}\right)$$

Day 79 is the spring equinox. The sun is furthest north on day 170, crosses the equator again on day 262, and is furthest south on day 353.

**Day length** comes from the sunrise hour angle $h_0$:

$$\cos h_0 = \operatorname{clamp}\big(-\tan\varphi\,\tan\delta,\; -1,\; 1\big), \qquad \text{day length} = 24\,\frac{h_0}{\pi} \text{ hours}$$

**Worked example, 60° north.** At midsummer $\delta = 23.5°$: $-\tan 60° \tan 23.5° = -1.732 \times 0.435 = -0.753$. $h_0 = \arccos(-0.753) = 2.43$ rad, so the day lasts $24 \times 2.43 / \pi = 18.6$ hours. At midwinter the sign flips: $h_0 = 0.72$ rad, 5.5 hours. Past 66.5°, the clamp gives 0 or 24 hours: polar night and midnight sun.

**The sun's height** above the horizon, at clock hour $t$, with hour angle $H = 2\pi(t - 12)/24$:

$$\sin e = \sin\varphi \sin\delta + \cos\varphi \cos\delta \cos H$$

The renderer uses the player's own latitude for the whole view. The sun's compass direction comes from the same angles, and is in phase 02's file.

**What follows from this:** nights are long in winter and toward the poles. Beds work at night and mobs spawn in darkness, so both follow automatically. In polar winter, hostile mobs can spawn all day.

---

## 6. Climate normals

### 6.1 The height estimate

`terrain_height_estimate(terrain, x, y)` is a new public pure function: the ground height the terrain's 2D splines give, with no 3D density and no chunk. The climate reads it once per 64-block sample. A test holds that it agrees with the real ground to within the density's softness. Nothing in the terrain changes.

### 6.2 Land mask, sea distance, continentality

The **land mask** is whether $h(x, y) > 0$, sampled every 64 blocks. Each sample is worked out once and kept in a sparse table keyed by its coordinates. A **sparse table** stores only the entries that exist, so an infinite world costs only what's been asked for.

**Sea, not lakes:** a flood fill on the coarse grid starts from open ocean and spreads through neighbouring cells below sea level. Water it never reaches is a lake. Lakes over 2 km² count as water for moisture (6.7), but not for currents, and never as "sea".

For each tile, a **distance transform** finds every cell's distance to the nearest sea, and the direction to it. It works over the tile plus an 8 km margin, so seas just outside count. Distances are capped at 8192. A distance transform is a two-pass sweep over a grid that gives every cell its distance to the nearest marked cell, without searching outward from each one.

**Continentality** $\kappa$ is how far from the sea's influence a place is:

$$\kappa = 1 - e^{-d_s / 1500}$$

$d_s$ is the distance to the sea. On the coast $\kappa = 0$. At 1500 blocks inland $\kappa = 0.63$, and at 4500, $0.95$.

### 6.3 Ocean currents: three kinds

Real oceans have cold currents on the west coasts of continents in the subtropics (Benguela off Namibia, Humboldt off Peru), warm currents on the east coasts (Agulhas off Durban, the Gulf Stream), and the reverse toward the poles.

For each sea cell within 600 blocks of land, $\vec n$ is the unit vector pointing to the nearest land. If $n_x > 0.5$, land is to the east, so this is **a continent's west coast**. If $n_x < -0.5$, it's **a continent's east coast**.

| Latitude | West coast | East coast | Elsewhere |
|---|---|---|---|
| 0° to 15° | Warm | Warm | Warm, weak |
| 15° to 45° | **Cold** | **Warm** | Neutral |
| 45° to 65° | **Warm** (a drift, like north-west Europe) | **Cold** (like Labrador) | Neutral |
| 65° to 90° | Cold | Cold | Cold |

The three kinds are **warm**, **cold** and **neutral**. Strength fades over 600 blocks offshore. On the Atlas, a current flows along the coast, at right angles to $\vec n$: cold ones toward the equator, warm ones toward the pole.

**On land**, each cell takes the current of its nearest sea, which the distance transform already found:

$$\Delta T_{\text{current}} = C\,e^{-d_s/600}, \qquad C = +4 \text{ (warm)},\; -5 \text{ (cold)},\; 0 \text{ (neutral)}$$

**Sea temperature,** which decides where hurricanes can form (7.2):

$$T_{\text{sea}} = \bar T_{\text{lat}}(\varphi) + C + A_{\text{sea}}(\varphi)\, s_h \cos\!\left(\frac{2\pi(d - 215)}{365}\right)$$

The sea is warmest about 45 days after the land, on day 215 in the north. The terms are as in 6.6.

### 6.4 Pressure belts

Real air pressure at sea level forms belts: low at the equator, high near 30°, low near 60°, high at the poles. Wind blows from high to low and is turned by the Earth's spin, which is why the wind belts in 6.5 exist.

Background pressure in hPa, by wind latitude $\lvert\psi\rvert$ (6.5): 0°: 1008, 15°: 1012, 30°: 1020, 45°: 1013, 60°: 1000, 75°: 1010, 90°: 1020.

**Heat lows and cold highs.** A continent heats up in summer, its air rises, and pressure over it falls. In winter it cools and pressure rises. That's what drives monsoons. For each land cell:

$$\Delta p_{\text{heat}} = -0.6\,\kappa\,\big(T_{\text{season}} - \bar T\big) \text{ hPa}$$

$T_{\text{season}} - \bar T$ is how far today's normal temperature is above the yearly mean (6.6).

**Worked example.** A continental interior with $\kappa = 0.9$ and a summer swing of +12 °C: $\Delta p = -0.6 \times 0.9 \times 12 = -6.5$ hPa. That's a heat low. In winter it's +6.5 hPa, a cold high. Over the sea, $\kappa = 0$, so there's no change.

Total pressure at a place and moment:

$$p = p_{\text{belt}}(\psi) + \Delta p_{\text{heat}} + \sum_i \Delta p_i$$

The last term is the weather systems near it (7.3).

### 6.5 Winds

Wind belts follow the sun north and south with the seasons, half as far and a month late:

$$\psi = \varphi - 0.5\,\delta(d - 30)$$

$\psi$ is the "wind latitude". Directions are unit vectors $(u, v)$, with $u$ east and $v$ north. Speeds are in blocks a second, which is metres a second.

| $\lvert\psi\rvert$ | Belt | Direction $(u, v)$ | Speed |
|---|---|---|---|
| 0° to 30° | Trade winds, from the east toward the equator | $(-1,\ -0.4\,\operatorname{sgn}\psi)$ | 6 |
| 30° to 60° | Westerlies | $(+1,\ +0.3\,\operatorname{sgn}\psi)$ | 8 |
| 60° to 90° | Polar easterlies | $(-1,\ -0.2\,\operatorname{sgn}\psi)$ | 5 |

$\operatorname{sgn}\psi$ is $+1$ north and $-1$ south. Belts blend over 6° with a smoothstep curve, $3t^2 - 2t^3$. Speed drops to 40% within 5° of the equator (the doldrums) and to 50% near 30° (the calm "horse latitudes").

**Wind from pressure differences.** Pressure from heat lows (6.4) and from weather systems (7.3) adds its own wind. Air moves from high to low pressure, turned by the Earth's spin, so it ends up nearly along the lines of equal pressure. In the north, it turns 70° clockwise from straight downhill in pressure. In the south, 70° anticlockwise:

$$\vec w_p = k_p\,R_{\mp 70°}\big(-\nabla \Delta p\big), \qquad k_p = 900 \text{ blocks}^2 \text{ per hPa per second}$$

$\nabla \Delta p$ is the pressure gradient, in hPa per block, and $R_\theta$ turns a vector by angle $\theta$.

**Why this gives the right spin:** north of the equator, air around a low turns anticlockwise and around a high clockwise. South of it, the reverse.

**Worked example: a monsoon.** A heat low of -6.5 hPa sits 3000 blocks inland from a south-facing coast at 20° north. The gradient toward the coast is about $6.5 / 3000 = 0.0022$ hPa per block. $\lvert\vec w_p\rvert = 900 \times 0.0022 = 2$ blocks a second, blowing in from the sea. That's enough to turn the dry season's offshore trade wind (about $6 \times 0.4 = 2.4$ north to south) into a wet onshore wind for the summer.

**The ground changes it:**

- **Altitude:** speed $\times \min(2.5,\ 1 + z/800)$. Peaks are windy.
- **Steering:** where the ground rises steeply across the wind, the wind turns along it. With slope gradient $\vec g$ and steepness $s = \min(1, \lvert\vec g\rvert)$: $\vec w' = \vec w - 0.7\,s\,(\vec w \cdot \hat g)\,\hat g$.

The yearly cycle of wind is stored per cell as a yearly average plus a once-a-year wave (6.9). Weather systems and day-to-day changes are added on top, when asked for (section 7).

### 6.6 Temperature

The normal daily mean temperature at the surface:

$$T = \bar T_{\text{lat}}(\varphi) + \Delta T_{\text{current}} - \Gamma z + B + A\,s_h\cos\!\left(\frac{2\pi(d - 170 - \ell)}{365}\right)$$

| Term | Meaning | Value |
|---|---|---|
| $\bar T_{\text{lat}}$ | Yearly mean at sea level, by latitude | Knots: 0°: 27, 15°: 26, 30°: 20, 45°: 11, 60°: 0, 75°: -14, 90°: -24 |
| $\Gamma$ | Cooling with height | **0.012 °C per block**, as decided |
| $z$ | Ground height, or the player's height for the HUD | — |
| $B$ | Below-sea-level heat (2.1) | $0.025 \max(0, -z)$, on land not under sea |
| $A$ | Half the yearly swing | $A_{\text{sea}} + \kappa\,(A_{\text{land}} - A_{\text{sea}})$ |
| $A_{\text{sea}}$, $A_{\text{land}}$ | By latitude | Sea: 0°: 1, 30°: 3, 60°: 6, 90°: 10. Land: 0°: 2, 30°: 9, 60°: 18, 90°: 22 |
| $s_h$ | Hemisphere | $+1$ north, $-1$ south |
| $\ell$ | Lag in days, since the sea warms slowly | $25 + 20\,(1 - \kappa)$ |

**Knots** are fixed points a curve passes through, and between them it's a smooth curve, using the same spline code `terrain/density` already has.

**Worked example 1: a beach on the equator.** $z = 3$, $\kappa = 0.05$, no current. $A = 1 + 0.05 \times 1 = 1.05$. $T = 27 - 0.04 \pm 1.05$, so about 26 to 28 °C all year.

**Worked example 2: a peak at 45° north, 1000 high, 1200 blocks inland, in January ($d = 15$).** $\kappa = 1 - e^{-0.8} = 0.55$. At 45°, $A_{\text{sea}} = 4.5$ and $A_{\text{land}} = 13.5$, so $A = 4.5 + 0.55 \times 9 = 9.5$. The lag is $\ell = 25 + 9 = 34$, and the cosine is $\cos(2\pi(15 - 204)/365) = \cos(-3.25) = -0.99$.

$$T = 11 + 0 - 12 + 0 + 9.5 \times (-0.99) = -10.4 \text{ °C}$$

In July it's $11 - 12 + 9.4 = 8.4$ °C: snowy all winter, bare in summer.

**Worked example 3: land 150 below sea level,** at 28° north ($y = 11{,}915$), 4000 blocks inland, in mid-July ($d = 196$). For when a geology plan makes such a place. $\bar T_{\text{lat}}(28°) = 20.8$, $\kappa = 0.93$, $A = 2.8 + 0.93 \times 5.7 = 8.1$, $\ell = 26$, and the cosine is 1.

$$T = 20.8 - 0.012 \times (-150) + 0.025 \times 150 + 8.1 = 20.8 + 1.8 + 3.75 + 8.1 = 34.5 \text{ °C}$$

That's the daily mean. The afternoon high adds half the daily range, $(4 + 8\kappa)(1 - 0.6\,r)$, with $r$ the share of rainy days. With $r = 0$ the range is 11.4, so the afternoon reaches about 40 °C.

### 6.7 Rain

Rain starts from belts by wind latitude: wet at the equator, dry near 30°, wet again in the mid-latitudes, dry at the poles. The belts follow the sun, so the tropics get a wet and a dry season on their own.

Yearly rain at sea level, in mm, by $\lvert\psi\rvert$: 0°: 2400, 10°: 1800, 20°: 700, 30°: 300, 40°: 800, 50°: 1000, 60°: 800, 75°: 350, 90°: 150.

$$P = P_{\text{belt}}(\psi)\cdot M \cdot O$$

**Moisture $M$:** air dries as it crosses land. From each cell, walk **upwind** against this season's wind, in 16 steps of 128 blocks, counting $d_{\text{up}}$, how far the air has come over land since it left the sea or a large lake. If it doesn't reach water within 2 km, the walk goes on over the coarse land mask, up to 8 km.

$$M = \max\!\big(0.15,\ e^{-d_{\text{up}}/3000}\big) \times \begin{cases} 1.25 & \text{if the air left over a warm current} \\ 0.4 & \text{within 800 blocks of a cold current} \\ 1 & \text{otherwise} \end{cases}$$

**Orographic factor $O$:** air rising up a slope drops its rain on the windward side, and leaves dry air behind the ridge, in its **rain shadow**.

$$O = \operatorname{clamp}\!\left(1 + 8\,s_w - \frac{\max(0,\ h_{\text{up}} - z)}{400},\ 0.15,\ 2.5\right)$$

$s_w$ is how fast the ground rises into the wind over the last 256 blocks (0 if it falls), and $h_{\text{up}}$ is the highest ground on the upwind walk.

**Rain or snow:** the share falling as snow is $\operatorname{clamp}\big((1 - T)/3,\ 0,\ 1\big)$. All rain above 1 °C, all snow below -2 °C.

**Worked example: Ahmed's South Africa sketch, 25° south, trade winds from the east.** On whatever ground the terrain makes with a coast, an escarpment and a plateau behind it:

- **East coast, warm current:** $P_{\text{belt}}(25°) = 500$, $M = 1.25$, $O = 1$. **625 mm.** Green.
- **Escarpment face,** rising 250 over 830 blocks into the wind: $s_w = 0.3$, so $O = 3.4$, clamped to 2.5. $M = e^{-0.33} \times 1.25 = 0.90$. **1125 mm.** The wettest place.
- **Plateau behind it,** at 500, 3000 blocks inland, behind a 750 crest: $M = e^{-1} = 0.37$, $O = 1 - 250/400 = 0.375$. **69 mm.** Semi-desert.
- **West coast, cold current:** the air has crossed the whole continent, so $M = 0.15 \times 0.4 = 0.06$. **About 30 mm,** plus fog. A Namib.

### 6.8 Storm seasons and monsoons

These are worked out per cell from the normals. They label the season (4.1), and set how often storms spawn (7.2).

**Hurricane season,** for a sea cell or a coast within 3 km of one:

$$s_{\text{hur}}(d) = \operatorname{clamp}\!\left(\frac{T_{\text{sea}}(d) - 26.5}{2},\ 0,\ 1\right) \cdot \mathbb{1}\big[\,5° \le \lvert\varphi\rvert \le 25°\,\big]$$

$\mathbb{1}[\cdot]$ is 1 when the condition holds and 0 otherwise. It's "hurricane season" while $s_{\text{hur}} > 0.25$. Hurricanes need sea water above about 26.5 °C, and they don't form within 5° of the equator, where the Earth's spin is too weak to turn them.

**Worked example.** A sea cell at 15° north, with a warm current ($C = +4$). $\bar T_{\text{lat}} = 26$, $A_{\text{sea}}(15°) = 2$. On day 215, $T_{\text{sea}} = 26 + 4 + 2 = 32$, so $s_{\text{hur}} = 1$, the peak. On day 32, $T_{\text{sea}} = 26 + 4 - 2 = 28$, so $s_{\text{hur}} = 0.75$. Over a warm current, the season is long. Without the current, day 32 gives $T_{\text{sea}} = 24$, and $s_{\text{hur}} = 0$: out of season.

**Monsoon,** for a land cell: the season's wind (6.5) blows from the sea, $\vec w \cdot \vec n_{\text{sea}} < 0$ where $\vec n_{\text{sea}}$ points toward the sea, while it blows from the land at the opposite time of year, and rain in the wet half of the year is over 3 times rain in the dry half. That's when the label says **Monsoon**.

### 6.9 What a cell stores

Everything in this section is worked out once per cell, when its tile is first needed, and kept. It's never saved: it comes back from the seed. The yearly cycles of rain and wind are stored as **harmonics**, an average plus waves over the year. Two waves are enough for one or two wet seasons. To get them, phase 05 works out rain and wind on 12 days spread through the year and fits the waves to them.

$$P(d) = P_0 + P_{1c}\cos\theta + P_{1s}\sin\theta + P_{2c}\cos 2\theta + P_{2s}\sin 2\theta, \qquad \theta = \frac{2\pi d}{365}$$

```c
/* One 64 by 64 block cell's climate normals: worked out once from the seed, never saved, never changed by play. */
typedef struct climate_cell_t {
    float32_t lat_deg;          /* 0: φ at the cell's centre */
    float32_t z_ground;         /* 4: mean of terrain_height_estimate over the cell */
    float32_t sea_distance;     /* 8: blocks to the nearest connected sea, at most 8192 */
    float32_t kappa;            /* 12: continentality, 0 on the coast to 1 deep inland */
    float32_t sea_direction[2]; /* 16: unit vector toward that sea */
    float32_t t_mean;           /* 24: °C, the yearly mean */
    float32_t t_swing;          /* 28: °C, A in 6.6, signed by hemisphere */
    float32_t t_lag_days;       /* 32: ℓ in 6.6 */
    float32_t rain_mm_week[5];  /* 36: P0, P1c, P1s, P2c, P2s */
    float32_t wind[6];          /* 56: u0, u1c, u1s, v0, v1c, v1s, blocks a second */
    float32_t pressure[3];      /* 80: Δp_heat as p0, p1c, p1s, hPa */
    float32_t snow_balance_z;   /* 92: height above which snow outlasts summer, from the normals */
    uint8_t current;            /* 96: CLIMATE_CURRENT_NONE, _WARM or _COLD, of the nearest sea */
    uint8_t current_strength;   /* 97: 0 to 255 */
    uint8_t climate_class;      /* 98: CLIMATE_CLASS_*, 6.10 */
    uint8_t flags;              /* 99: CLIMATE_CELL_SEA, _LAKE, _MONSOON, _HURRICANE_COAST */
    uint8_t hurricane_days[2];  /* 100: first and last day of hurricane season, 255 if none */
    uint8_t _pad0[26];          /* 102: room to grow without moving a save or a test */
} climate_cell_t;
static_assert(sizeof(climate_cell_t) == 128);
```

`hurricane_days` stores day numbers as a quarter of the real day (0 to 91 means day 0 to 364), so each fits in a byte. The phase file has the exact packing.

Between cell centres, every value is read with a bilinear blend. The point it's read at is first moved by a small warp noise, up to 32 blocks, so borders between regions wobble instead of following the 64-block grid. That's what makes regions blend.

**Memory:** 128 bytes × 256 cells = 32 KiB a tile, so 32 KiB per km², and 8 MiB for 16 by 16 km.

### 6.10 Climate classes

For the Atlas only in this plan (2.6). Each cell gets a class from its warmest and coldest month means ($T_{\text{max}}$, $T_{\text{min}}$), yearly rain $P$, and its driest month. The rules are simplified from the Köppen system, which geographers use to sort climates.

Dryness threshold: $P_{\text{th}} = 20\,\bar T + 140$ mm.

| Class | Rule |
|---|---|
| Ice | $T_{\text{max}} < 0$ |
| Tundra | $0 \le T_{\text{max}} < 10$ |
| Desert | $P < P_{\text{th}}/2$ |
| Steppe | $P < P_{\text{th}}$ |
| Rainforest | $T_{\text{min}} \ge 18$, driest month at least 60 mm |
| Monsoon | $T_{\text{min}} \ge 18$, the monsoon flag (6.8) |
| Savanna | $T_{\text{min}} \ge 18$, a dry season |
| Mediterranean | $T_{\text{min}} > -3$, dry summer |
| Temperate | $T_{\text{min}} > -3$, rain all year |
| Continental | $T_{\text{min}} \le -3$, $T_{\text{max}} \ge 22$ |
| Boreal | $T_{\text{min}} \le -3$, $T_{\text{max}} < 22$ |

The first rule that matches wins.

---

## 7. Weather

### 7.1 How weather is made, without simulating physics

Weather is **spawned, not simulated**. Three things are added up:

1. **Normals** (section 6): what this place is usually like today.
2. **Weather systems** (7.2 to 7.4): cyclones, anticyclones, hurricanes and thunderstorm cells. Each is spawned by hash with a rate that depends on place and season, moves along a track set by simple rules, and carries its own pressure, wind, rain and life. Nothing about air or water is simulated.
3. **Background noise** (7.4): smooth noise in space and time for the rest of the day-to-day change.

All three are pure functions of the seed and the time. Nothing about weather needs saving, except the state the census keeps (7.5).

### 7.2 Weather systems: kinds and spawn rates

Every **spawn square** (its size is per kind, below) has, for each day, a hash-decided count of new systems. The average rate $\lambda$ (spawns per square per day) depends on the place and the season. The hash also decides each new system's start time, its exact start point within the square, its size, strength and life, all within the ranges in the table.

| Kind | Spawn square | Where | Rate $\lambda$ a day | Radius | Pressure | Life | Moves |
|---|---|---|---|---|---|---|---|
| **Hurricane** (tropical cyclone) | 16 km | Sea, $s_{\text{hur}} > 0$ | $0.02\,s_{\text{hur}}$ | 1.5 to 4 km, eye 80 to 150 blocks | -30 to -90 hPa | 4 to 10 days | With the trades, then turns poleward and east (7.3) |
| **Low** (mid-latitude cyclone) | 16 km | $35° \le \lvert\psi\rvert \le 65°$ | 0.08 in winter, 0.04 in summer | 3 to 7 km | -10 to -30 hPa | 3 to 6 days | East with the westerlies, about 6 km a day |
| **High** (anticyclone) | 24 km | $20° \le \lvert\psi\rvert \le 45°$, and continental interiors in winter | 0.03 | 6 to 12 km | +8 to +20 hPa | 6 to 14 days | Slowly east, about 2 km a day |
| **Thunderstorm cell** | 1 km | Hot, wet afternoons, fronts, monsoon, the ITCZ | 7.2.1 | 150 to 400 blocks | -1 to -3 hPa | 1 to 4 clock hours | With the day's wind |

**Hurricanes** also weaken over land, by half each game day, and over sea below 26 °C. Their strength follows a life curve: they grow for the first third of their life, and fade over the last third.

**Lows** carry a **cold front**: a line 4 to 8 km long trailing from the centre toward the equator, where thunderstorm cells spawn at 5 times the usual rate. They spawn twice as often in winter, which is when real mid-latitude storms are strongest.

**Highs** bring clear, calm weather: they cut rain under them to 20% of normal, and make heat waves in summer and cold snaps in winter more likely (7.4).

**Hurricane names** are made from syllables by hash, so each world has its own: "Hurricane Tavela", "Tropical storm Orin".

**Worked example: how many hurricanes.** A warm-current coast at 15° north, with $s_{\text{hur}}$ near 1 for about 120 days. Each 16 km square of sea there gets $0.02 \times 120 \approx 2.4$ hurricanes a season. A coast with three such squares of sea in front of it sees about 7 a season form offshore. Many curve away without reaching it. The rate is one constant, tuned by playing.

#### 7.2.1 Thunderstorm cells

For each 1 km square and each day:

$$\lambda_{\text{cell}} = 3\cdot\operatorname{clamp}\!\left(\frac{T_{\text{day}} - 18}{10}, 0, 1\right)\cdot\frac{\bar P(d)}{\bar P_{\text{max}}}\cdot(1 + 2\,f_{\text{monsoon}})\cdot(1 + 4\,f_{\text{front}})\cdot(0.2)^{f_{\text{high}}}$$

- $T_{\text{day}}$ is today's temperature, and $\bar P(d) / \bar P_{\text{max}}$ is today's normal rain as a share of the wettest day of this cell's year.
- $f_{\text{monsoon}}$ is 1 in monsoon season, $f_{\text{front}}$ is 1 on a cold front, and $f_{\text{high}}$ is 1 under a high.
- Start times lean to the afternoon, 13:00 to 19:00 on the clock, except on fronts, where they follow the front.

**Worked example.** A monsoon afternoon at 30 °C, in the wettest week of the year, not under a high and not on a front: $\lambda = 3 \times 1 \times 1 \times 3 \times 1 \times 1 = 9$ cells a day in each km². Most days, the player sees several storms build. On a dry winter day at 15 °C, $\lambda = 0$.

Cells only need working out where someone looks: near the player and in the Atlas view.

### 7.3 Tracks, pressure and wind of a system

**Tracks.** A system moves by simple steering rules, in steps of a quarter of a day:

- Hurricanes: with the trade winds, at about 3.5 km a day. Once they reach $\lvert\psi\rvert$ of 20° to 28° (the hash picks where), they turn poleward over 2 days, then go east with the westerlies at about 6 km a day.
- Lows and highs: east with the westerlies, at their own speeds, with a slow drift poleward (lows) or equatorward (highs).
- Thunderstorm cells: with today's wind at the place they started.

Steering only reads latitude and the coarse land mask, so a track can be followed over land nobody has generated, without working out any climate tile there.

**Cost:** to know every system near the player now, the game looks at every spawn square within the longest track length (a hurricane: about 35 km), over the longest life (14 days). That's about $5 \times 5$ squares of 16 km for hurricanes and lows, $4 \times 4$ of 24 km for highs, for 14 days: under 1000 hashes, and a few dozen tracks. It's recomputed once a game day, and positions in between are interpolated. A test holds it under 2 ms.

**Pressure.** Each system adds a pressure dip or bump shaped like a bell:

$$\Delta p_i(r) = A_i(t)\,e^{-r^2 / R_i^2}$$

$r$ is the distance from its centre, $R_i$ its radius, and $A_i(t)$ its strength at that point in its life.

**Wind.** Around a system, the wind circles it, with this speed profile, where $r_m$ is the radius of strongest wind:

$$v(r) = \begin{cases} v_{\max}\,\dfrac{r}{r_m} & r < r_m \\[1ex] v_{\max}\left(\dfrac{r_m}{r}\right)^{0.6} & r \ge r_m \end{cases}$$

- For hurricanes, $r_m$ is the eye's edge, and $v_{\max}$ comes from the pressure dip: 33 blocks a second at -30 hPa, up to 70 at -90 hPa. Inside the eye, it's nearly calm.
- For lows and highs, $r_m = 0.5\,R_i$, and $v_{\max} = 0.4\,\lvert A_i \rvert$.
- It circles anticlockwise around lows in the north and clockwise in the south, and the reverse around highs. It's added to the belt wind.

**Rain.** Each system adds rain in its footprint:

| Kind | Rain |
|---|---|
| Hurricane | Up to 40 mm an hour in the eyewall, falling off with $r$, and spiral bands from a noise on angle and distance |
| Low | A wide shield of 1 to 4 mm an hour ahead of the centre, and heavy showers on the cold front |
| High | Cuts all rain under it to 20% |
| Thunderstorm cell | 20 to 60 mm an hour, for under an hour at any one point, with lightning and gusts |

### 7.4 Daily weather and the background

The rest of the day-to-day change comes from smooth noise in space and time:

$$T'_{\text{day}} = \sigma_T\,N_1\!\left(\frac{x}{2500}, \frac{y}{2500}, \frac{D}{10}\right) + T'_{\text{systems}}, \qquad \sigma_T = 2 + 3\kappa$$

$N_1$ is one of the repo's own 3D noises, seeded for this and scaled to roughly -1 to 1. Using time as the third axis gives **spells**: a cold day is likely followed by another, because the noise changes smoothly. Nothing is stored to get that.

$T'_{\text{systems}}$: a high adds $+0.5\,\sigma_T$ in summer and $-0.5\,\sigma_T$ in winter at its centre. A low or a hurricane adds clouds, which cut the daily range by 60%.

**Calibrating the rain.** Background rain is today's normal $\times\, (1 - f_{\text{sys}}) \times$ a noise factor. Systems bring the rest. $f_{\text{sys}}$ is about 0.5 in the mid-latitudes and 0.3 in the tropics. A test runs 10 years on 8 seeds and holds each cell's long-run rain to within 15% of its normal. If it's off, the spawn rates are what gets tuned, not the normals.

**Each day's kind,** for the census report and the HUD:

| Kind | When |
|---|---|
| Clear | Most dry days, and under highs |
| Cloudy | Some dry days in wet weeks, near lows |
| Rain | Over 1 mm, above 1 °C |
| Snow | Over 1 mm, below -2 °C. Sleet in between |
| Thunderstorm | A cell passed |
| Storm | A low over 20 hPa deep, or wind over 15 |
| Hurricane | Within a hurricane's radius |
| Fog | Calm mornings on a cold-current coast, or humid valleys |
| Heat wave | $T'_{\text{day}} > 1.5\,\sigma_T$ |
| Dust storm | Wind over 12, under 2 mm of rain in the week, on desert ground |

### 7.5 The weekly state

Snow, soil water and runoff depend on every week before, so they're **state**, kept per cell and saved. This is what the census recomputes each week.

```c
/* One cell's weather state, for every cell with generated land in it, saved with the world. */
typedef struct climate_week_t {
    float32_t t_mean;        /* 0: °C, last week */
    float32_t t_min;         /* 4 */
    float32_t t_max;         /* 8 */
    float32_t rain_mm;       /* 12: rain and snow together, as water */
    float32_t snow_mm;       /* 16: snow on the ground, as water */
    float32_t soil_mm;       /* 20: water in the soil, 0 to CLIMATE_SOIL_MM_MAX (150) */
    float32_t runoff_mm;     /* 24: what ran off last week */
    float32_t wind[2];       /* 28: last week's prevailing u and v */
    float32_t ice_cm;        /* 36: on still water */
    float32_t flood;         /* 40: runoff from upstream over channel size, 1 = banks full */
    uint8_t day_kind[7];     /* 44: CLIMATE_DAY_* for each day */
    uint8_t _pad0[13];       /* 51 */
} climate_week_t;
static_assert(sizeof(climate_week_t) == 64);
```

The weekly update, with $T_w$ the week's mean temperature, $P_w$ its rain and snow as water, and $f_s$ its snow share:

$$M_w = 7 \cdot 4 \cdot \max(0, T_w) \quad \text{(snowmelt, mm: 4 mm per degree per day)}$$

$$S_{w+1} = \max\big(0,\ S_w + f_s P_w - M_w\big)$$

$$E_w = 7 \cdot 0.2 \cdot \max(0, T_w) \quad \text{(evaporation, mm)}$$

$$W' = W_w + (1 - f_s) P_w + \min(M_w,\ S_w + f_s P_w) - E_w, \qquad W_{w+1} = \operatorname{clamp}(W', 0, 150), \qquad R_w = \max(0, W' - 150)$$

**Worked example: spring melt.** A cell has 120 mm of snow and 140 mm of soil water. The week averages 3 °C with 15 mm of rain. Melt: $7 \times 4 \times 3 = 84$ mm, leaving 36 mm of snow. Evaporation: $7 \times 0.2 \times 3 = 4.2$ mm. Soil: $140 + 15 + 84 - 4.2 = 234.8$, so the soil holds 150 and **84.8 mm runs off.**

**Ice** on still water grows by 1.5 cm for each degree below 0, for each day, and melts at 2 cm for each degree above 0.

**Flood index:** runoff flows downhill on the coarse grid, each cell passing its water to its lowest neighbour, so each cell's total is everything that flows through it. That's **flow accumulation**. It works over the height estimate, so land not yet generated still drains into land that is. A cell's flood index is that week's total over a channel size set by its long-run total. Over 1 means water over the banks.

A hurricane's week adds its rain here like any other, so a hurricane making landfall shows up as floods downstream.

---

## 8. The outlook: chances, not certainty

The outlook is general code that mimics forecasting. **It never reads the actual future.** It uses only what a forecaster could know: today's systems, the normals and the spawn rates.

For each of the next 7 days, at a place:

**1. Known systems.** Each system alive now has a forecast track: its real steering rules, with an error that grows with time. The error is drawn by hash from the seed, the system and the census week, so it's the same if the screen is reopened:

$$\sigma(t) = 200 + 400\,t \text{ blocks}, \qquad t \text{ in days ahead}$$

The chance that a system's footprint covers this place on day $t$:

$$p_i(t) = \exp\!\left(-\frac{d_i(t)^2}{2\big(\sigma(t)^2 + R_i^2\big)}\right)$$

$d_i(t)$ is the distance from this place to the system's forecast centre.

**2. Systems not born yet.** From the spawn rates: the chance at least one new system of a kind reaches this place by day $t$ is $1 - e^{-\Lambda(t)}$, where $\Lambda(t)$ is the sum of the rates over every spawn square and day from which a system could reach here by then.

**3. Combined.** For each kind of event, chances are combined as "at least one of them":

$$p(t) = 1 - \prod_i \big(1 - p_i(t)\big)\cdot e^{-\Lambda(t)}$$

**4. Background rain** has a chance from the normal: $1 - e^{-\bar P(d)/6}$ for a day's normal rain $\bar P(d)$ in mm.

**Worked example.** Hurricane Tavela is 6 km east of the player now, heading west at 3.5 km a day, with a radius of 2.5 km. In 2 days its forecast centre is $6 - 7 = -1$ km, so 1 km past. $\sigma(2) = 1000$. $p = \exp(-1000^2 / (2 \times (1000^2 + 2500^2))) = \exp(-0.069) = 93\%$. The census screen says: **Hurricane Tavela: 93% chance of reaching you on day 2.**

**What the screen shows:** for each day, a chance of rain, of thunderstorms, of storms, and of each named system reaching here, plus the expected high and low with a spread of $\pm 1.5\,\sigma_T$.

---

## 9. What the player sees

### 9.1 HUD

Top corner: **"Year 2 · Day 214 · Late summer · Hurricane season"**. Below it, the temperature where the player stands, a wind arrow and its speed, with a setting to turn that line off. When a forecast gives over 50% for a hurricane or storm within the next day: **"Storm warning: Hurricane Tavela"**. The settlement name goes above it all, once settlements exist.

### 9.2 Census screen

- **Where you are, last week:** high, low, rain or snow, snow on the ground, prevailing wind
- **Events within render distance:** frost, heat wave, thunderstorms, storms, hurricanes, floods, first snow, ice on lakes
- **The outlook** (section 8)
- **Continue**

### 9.3 The Atlas, on `M`

The game keeps running while it's open.

| Part | What |
|---|---|
| Ground | Every generated chunk. **Ungenerated land is grey** |
| Data | When a column is generated, its top block's colour and height are written to an Atlas tile: 512 by 512 blocks, 1 pixel a block, 4 bytes a pixel, 1 MiB. Zoomed-out levels are halved copies. Saved with the world |
| Normals layers, with a day slider | Temperature, rain, wind, ocean currents, pressure belts and heat lows, climate class, hurricane season, monsoon areas, snow balance, latitude lines (equator, tropics, polar circles) |
| Now layers | **Pressure** (lines every 4 hPa, H and L marks), **systems** (hurricanes with names, tracks behind them and forecast cones ahead, lows with cold fronts, highs), **rain radar** (rain rate now), **thunderstorm cells** (with lightning marks), wind now, temperature now |
| This week's layers | Snow, soil water, flood index, last week's rain |
| Other | Terrain with hill shading, height lines every 100 |
| Arrows | Wind and currents as arrows on a grid, length by speed. Currents red for warm, blue for cold, grey for neutral |
| Systems over grey land | Outlines only, so incoming storms are visible before they reach explored land (2.4) |
| Reading | The cell under the cursor: latitude, height, temperature, rain, wind, pressure, current, class |
| Keys | `M` opens and closes, wheel zooms, drag pans, `1` to `9` choose layers, `[` and `]` move the day, `P` centres on the player, `Esc` closes |

The Atlas draws into the UI layer, on the CPU. Each picture is drawn once per change of layer, zoom, pan or day, and reused between frames. "Now" layers are redrawn twice a second while open, since storms move.

The headless map tool from 003 phase 8 stage 1 draws every layer to PNG with the same code. Every phase's gate uses those pictures, so the maths can be checked before the Atlas exists.

---

## 10. Phases

Each phase ends somewhere worth stopping (`plans/README.md` rule 2), and becomes one phase file with the template's Goal, Todo, Verification, Commits and Issues found. Every heavy command goes through `tools/test-or-build.sh`. Every phase that adds climate output pins digests, each with a line saying why, and proves 1 worker and 16 give the same bytes.

### Phase 00: Plan files

- [ ] On approval, a setup agent copies this file verbatim and creates phase files 01 to 11 from the template, all unticked

### Phase 01: Calendar and the weekly pause

**Goal:** the HUD shows year, day and season. Every 7 days at 06:00 the world pauses, an empty census screen opens, and Continue resumes.

- [ ] Rename `run/src/terrain/climate/` to `run/src/terrain/fields/`. Every pinned digest holds. `plans/REPO_STRUCTURE.md` updated
- [ ] `run/{src,include}/calendar/`: day, year, day of year, week and the date part of the season label, from `world_ticks`, $D_0$ and $t_0$
- [ ] Save `last_census_week` and $D_0$. Bump `SAVE_WORLD_VERSION`
- [ ] The trigger in the sim's tick (4.2)
- [ ] `run/src/ui/screen_census/`: the shell, with Continue
- [ ] The HUD line, with a setting
- [ ] Tests: the census fires exactly once a week through plain ticks, sleeping through a night, and a 100-day jump (once per missed week, in order), and never twice after a save and load mid-week
- [ ] Tests: day 364 rolls to day 0 of the next year, and week numbers carry across years

**Gate:** Ahmed plays 8 days and sees one pause, on day 7 at 06:00.

### Phase 02: Latitude and the sun

**Goal:** latitude, declination and day length exist, and the sun's path in the sky follows the player's latitude and the season.

- [ ] `climate/`: $\varphi(y)$, $\delta(d)$, $h_0$, day length, the sun's height and direction
- [ ] Tests: $\varphi(0) = 0$, $\varphi(10000) = 23.5$, $\varphi(38298) = 90$, $\varphi(-38298) = -90$, $\varphi(y + 153191) = \varphi(y)$; 24-hour days past 66.5° at midsummer; 12-hour days everywhere at the equinox
- [ ] Feed the sun's direction to `render/frame` in place of today's fixed path. Night for beds and mob spawning follows it
- [ ] Latitude in the F3 overlay

**Gate:** Ahmed flies north past the polar circle in summer and sees the sun stay up.

### Phase 03: Height estimate and climate cells

**Goal:** any cell's ground height, sea distance and direction, sea or lake, and continentality, worked out once per tile, kept, and drawn.

- [ ] `terrain_height_estimate(x, y)`, public, with a test that it agrees with real ground to within the density's softness over 10,000 columns
- [ ] `climate/`: the coarse land-mask table, the flood fill from open ocean, the distance transform per tile with its 8 km margin, $\kappa$
- [ ] The tile cache: a sparse hash table of tiles, filled by background jobs ahead of chunk generation, nearest the player first. Precomputed over render distance plus 2 km when a world is created, behind a progress bar
- [ ] Map layers: sea distance, continentality, sea and lakes, latitude lines
- [ ] Tests: distance against a brute-force search on synthetic coasts; a tile's cells are the same whichever neighbours were worked out first; 1 and 16 workers give the same bytes
- [ ] Bench: one tile on one worker, target under 100 ms

### Phase 04: Currents, pressure and winds

**Goal:** every sea cell near land has its current, and every cell has its pressure and wind for any day. All on the map.

- [ ] Currents by 6.3, with strength and flow direction
- [ ] Pressure belts and heat lows (6.4)
- [ ] Wind belts, doldrums and horse latitudes, wind from pressure, altitude, steering by slopes (6.5)
- [ ] Wind and pressure stored as averages plus a yearly wave, fitted from 12 days
- [ ] Map layers: currents, pressure, wind, each with the day slider
- [ ] Tests: on a synthetic continent at 30° south, the west coast is cold and the east coast warm, and at 50° the reverse; trades blow from the east between 0° and 30°; a heat low makes an onshore summer wind on a synthetic coast at 20°; steering never makes wind faster

### Phase 05: Temperature, rain and storm seasons

**Goal:** every cell knows its normal temperature and rain for any day, its hurricane season and whether it has a monsoon. All on the map.

- [ ] Temperature (6.6), with all knots in one table
- [ ] Moisture by the upwind walk, orographic factor and rain belts (6.7), for 12 days, fitted to harmonics
- [ ] Sea temperature, hurricane season and the monsoon flag (6.8)
- [ ] Snow balance height, and climate classes (6.10)
- [ ] Map layers: temperature (day, mean, warmest, coldest), rain, hurricane season, monsoon, climate class
- [ ] Tests: the worked examples of 6.6 to 6.8 on synthetic ground, within 10%; temperature falls with height; a ridge's lee is drier than its windward side; south runs six months from north
- [ ] Ahmed's South Africa test: a synthetic continent with his sketch's profile at 25° south gives a wet east coast, a wettest escarpment, a dry plateau and a desert west coast

**Gate:** Ahmed reviews the maps over a year, with the slider.

### Phase 06: Weather systems

**Goal:** hurricanes, lows, highs and thunderstorm cells spawn by their rates, follow their tracks, and add pressure, wind and rain. All on the map, moving.

- [ ] `climate/systems/`: spawn by hash per square and day (7.2), tracks (7.3), life curves, names
- [ ] Pressure, wind and rain from each system (7.3)
- [ ] The daily list of systems near a point, and interpolation between days
- [ ] Map layers: pressure now with H and L, systems with tracks, rain radar, thunderstorm cells
- [ ] Tests: hurricanes only spawn in season and never within 5° of the equator; they turn poleward and weaken over land; winds circle lows anticlockwise in the north and clockwise in the south; a system's track is the same whichever day it's first asked for; no cells spawn under a high's centre in 1000 days
- [ ] Test: over 10 years on 8 seeds, each cell's rain is within 15% of its normal (7.4)
- [ ] Bench: the systems near a point, under 2 ms

**Gate:** Ahmed watches a hurricane season go by on the map.

### Phase 07: Daily weather, the weekly state, the census and the outlook

**Goal:** each day has its weather, the census works out each week's state for every generated cell and saves it, and the screen shows the report and the outlook.

- [ ] Daily weather and day kinds (7.4)
- [ ] The weekly update (7.5), ice, and flow accumulation for the flood index
- [ ] Save the weekly state in tiles. A world reopened mid-week has the same state
- [ ] The outlook (section 8), and the census screen's report
- [ ] Tests: snow builds through winter and melts in spring at 45°; a cell at 75° keeps snow all year; a hurricane landfall gives a flood index over 1 downstream; the outlook's chances are well calibrated (events given 70% happen between 60% and 80% of the time, over 10 years); replaying the same weeks gives the same bytes
- [ ] Bench: the census for 100 km² of generated land, under 250 ms on 16 workers

### Phase 08: Weather in the world

**Goal:** the player sees and feels the weather, near them: rain, snow, thunderstorms with lightning, hurricane winds, fog, ice and snow cover.

- [ ] Rain and snow falling, in the CUDA renderer, by the rain rate where the player is
- [ ] Lightning flashes in thunderstorm cells. Fog and sky colour by weather
- [ ] Wind pushes the player in storms and hurricanes, by a share of its speed
- [ ] At each census, in loaded chunks only: ice on still water over 5 cm, snow on the ground over 20 mm, and both removed when they melt. A thin snow layer block is needed, so this waits for 003 phase 1
- [ ] Bench: the frame-rate cost, at render distance 32, before and after

**Gate:** Ahmed plays through a winter and a hurricane season.

### Phase 09: The Atlas

**Goal:** `M` opens the map of everything generated, with every layer, the day slider, systems moving and the cursor reading.

- [ ] Atlas tiles written as columns generate, saved, with zoomed-out levels
- [ ] `run/src/ui/atlas/`, with the keys in 9.3
- [ ] Every layer from the headless tool, drawn by the same code. Ungenerated land grey, systems over it as outlines
- [ ] Golden pictures of each layer at two zooms, in `tests/run/ui/atlas/`
- [ ] Bench: opening the Atlas over 100 km² of generated land, under 100 ms to the first picture

**Gate:** Ahmed uses it to watch a storm come in and meets it.

### Phase 10: HUD, warnings and settings

**Goal:** the HUD's climate line and storm warnings, with settings.

- [ ] The climate line (9.1): temperature at the player's height, wind arrow and speed
- [ ] Storm warnings from the outlook
- [ ] Settings rows, kept like `show_coordinates`
- [ ] Golden HUD pictures, on and off

### Phase 11: Tuning and cost

**Goal:** the numbers Ahmed tuned by playing are recorded, and everything meets its budget.

- [ ] Re-measure, over 8 seeds: climate class shares, hurricanes a season per warm coast, thunderstorm days a year by class, and outlook calibration. Record them
- [ ] Re-run every bench: climate tile, systems, census, Atlas, frame rate
- [ ] Every constant in sections 4 to 8 lives in one place each, named, with its unit

---

## 11. Still open for Ahmed

Each has a default, and none blocks approval.

| # | Question | Default if not answered |
|---|---|---|
| Q1 | Rename `terrain/climate/` to `terrain/fields/`, so there aren't two "climate" modules? | Yes |
| Q2 | New worlds start on the spring equinox, day 80? | Yes |
| Q3 | Pole-to-pole, the latitude pattern repeats every 153 km. You said "38 km is an entire world": that's equator to one pole. Is that what you meant? | Yes, 38 km equator to pole |
| Q4 | Can storms and hurricanes push the player (phase 08)? | Yes, gently |
| Q5 | About 7 hurricanes a season forming off a warm coast, and many curving away. Too many, or right? | Start there, tune by playing |

## 12. Risks

| Risk | Why | What's done about it |
|---|---|---|
| Chunk generation waits on climate tiles | A tile is worked out before its chunks | Tiles are worked out ahead of the player, nearest first, and a bench holds a tile under 100 ms |
| Same bytes from 1 and 16 workers | Parallel sums can add in different orders | Each cell and each system is worked out by one worker, in a fixed order. Tests in every phase |
| Rain doesn't match its normals | Systems and background are added separately | The 10-year test in phase 06. Spawn rates are tuned, not normals |
| Frame rate | Weather visuals add work in a renderer already below target (003 note B) | Phase 08 measures before and after, and Ahmed decides |
| New blocks | Thin snow needs a new block id, which moves every id after it | Phase 08's snow cover waits for 003 phase 1 |
| Biomes don't match the climate | Vegetation isn't in this plan (2.6) | The Atlas shows climate class, so the mismatch is visible until the vegetation plan |

## 13. Sources

- Hytale zones and biomes, for ideas only: [The Spike, Hytale zones and biomes](https://www.thespike.gg/hytale/beginner-guides/hytale-zones-and-biomes). `https://hytale.fandom.com/wiki/Biomes` was asked for and could not be fetched. No Hytale files, numbers or art are used, as decided on 2026-11-04.
- The physical ideas (cooling with height, the sun's declination, day length, pressure and wind belts, monsoons, cold and warm coastal currents, rain shadows, hurricane water temperature, Köppen classes, snowmelt by degree-days) are standard geography, written here in our own form.

## 14. Not in this plan

- **Geology and landforms:** plateaus, escarpments, Everest-style massifs, below-sea-level basins (a later plan; the climate is ready for them, 2.1)
- **Vegetation:** biomes following climate, autumn colours, crops
- Economy, settlements, nations, trade, reputation
- Volcanoes
- Multiplayer 
