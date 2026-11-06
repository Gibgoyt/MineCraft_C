# 004-ahmed-bohemoth-climate-system — phase 4: Currents, pressure and winds

**Status:** in progress: built and agent-verified (full debug suite, release, bench, pictures); only Ahmed's gate is left (the pictures, the layer hashes and the monsoon question)   (updated 2026-11-06)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then sections 3, 6.3, 6.4, 6.5 and 6.9 of `plans/004-ahmed-bohemoth-climate-system.md`, then the phase 03 file, `plans/004-ahmed-bohemoth-climate-system-phase-03-height-estimate-and-climate-cells.md`, whose cells and window this phase fills in. Every number here comes from the master plan. Where this file adds a number the master plan does not give, it says so and gives the reason.

## Goal

Every climate cell has its ocean current, its air pressure and its wind for any day of the year, worked out once when its tile is built, from the seed, and never saved. A sea cell within 600 blocks of land knows whether its current is warm, cold or neutral, how strong it is, and which way it flows. A land cell knows the current of its nearest sea and how much it still feels it. Every cell stores its heat-low pressure and its wind as a yearly average plus one yearly wave, fitted from 12 days. Reading at any block and any day gives a wind vector in blocks a second and a pressure in hPa, blended between cells. The sea's temperature for any day can be asked for. Three map layers exist, currents, pressure and wind, each drawn for any day. On synthetic continents, the coasts get the currents the master plan's table says, the trade winds blow from the east, winds circle a heat low the right way in each hemisphere, and slopes never make wind faster. One and sixteen workers give the same bytes, and a tile still builds in under 100 ms on one worker.

## Where this fits

**The vision.** The game is a stack worked out strictly top to bottom (vision file, Part 1), and "the land decides; nothing is hand-placed". This phase is the second rung of the climate: it turns the land's shape (phase 03's sea distances and coasts) into the movement of water and air. Ahmed asked for it by name: winds, ocean currents, three kinds of current, and an Atlas that shows wind, currents and pressure.

**What later phases of plan 004 read from it:**

| Reader | What it reads |
|---|---|
| Phase 05, temperature | `current` and `current_strength` for $\Delta T_{\text{current}}$ (6.6). `t_swing` and `t_lag_days`, which this phase works out first because heat lows need them |
| Phase 05, rain | This season's wind, for the upwind walk (6.7). Whether air left over a warm current or near a cold one, for $M$ |
| Phase 05, storm seasons | The sea temperature (6.3), for hurricane season (6.8). The season's wind against the coast, for the monsoon flag |
| Phase 06, weather systems | The wind belts, which steer storm tracks (7.3). The pressure field the systems' bells are added to |
| Phase 07, outlook | The same steering, for forecast tracks |
| Phase 09, the Atlas | The three layers drawn here, with the day slider |

**What later plans read from it.** The vision's table says the economy plan needs "a sea lane is cheap along a favourable current", and trade needs "huge seasonal trade routes that just make sense": sailing ships following currents and prevailing winds. So the current's **flow direction** and the **wind for any day** must be real numbers a later plan can read, not just colours on a map. Settlements read climate for "where it's pleasant to live", which phase 05 builds from this phase's currents. A later geology plan changes the ground. Nothing here reads anything but phase 03's cells, so currents and winds follow new coasts and mountains with no change.

**What this phase must NOT build:**

- No physics. Ahmed: "physics must not be simulated", "do not drive the actual physics! just create general code please that will sort of mimic it". Winds come from a table of belts plus a simple turn of the pressure gradient. Currents come from a table by latitude and coast side. Nothing flows, nothing is integrated over time.
- No weather systems (cyclones, anticyclones, hurricanes, thunderstorm cells). That is phase 06. This phase's pressure is the belts and the seasonal heat lows only.
- No temperature normals, rain, storm seasons or climate classes. That is phase 05. This phase fills only `t_swing` and `t_lag_days`, which its heat lows need.
- No change to the terrain, no geology, no vegetation, no biome change (master plan 14).
- No saving. No Atlas screen (phase 09).

## Ahmed's words

- "create winds please! And a map that i hit "M" to bring up map, it shows all generated chunks, and it shows winds, ocean current, etc..."
- "3 different ocean currents/temperatures would be nice"
- "the user must be able to change filter to see wind current, ocean current, pressure systems like cyclones and anti-cyclones please"
- "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please"
- "please drive a probability thing! but do not drive the actual physics! just create general code please that will sort of mimic it"
- "A single year is 365 days, altitude, distance from the ocean or any considerably large water bodies must somehow affect this hard-coded at generation time so we don't waste runtime compute"
- From the vision: "huge seasonal trade routes that just make sense"

The three currents are 6.3's warm, cold and neutral. "Hard-coded at generation time" is the stored yearly wave: worked out once per cell, read with a few multiplications. Monsoon season is phase 05's label, and it depends on this phase's heat lows turning the wind (see Issues found).

---

## Background, explained

Each idea this phase uses, with a worked example. Phase 03's file explains struct, pure function, spline, cell, tile, cache, job system, distance transform, $\kappa$, bilinear blend and domain warp; they are not repeated here.

### Vectors, unit vectors and the dot product

A **vector** here is two numbers $(u, v)$: $u$ is the part pointing east (+x), $v$ the part pointing north (+y). Wind is a vector in blocks a second. Its **speed** is its length, $\lvert \vec w \rvert = \sqrt{u^2 + v^2}$. A **unit vector** has length 1 and only says a direction. Any vector divided by its length is a unit vector.

**Example.** The master plan's trade-wind direction $(-1, -0.4)$ has length $\sqrt{1 + 0.16} = 1.0770$. As a unit vector it is $(-0.9285, -0.3714)$: mostly west, a little south. At 6 blocks a second the wind is $(-5.571, -2.228)$.

The **dot product** of two vectors is $\vec a \cdot \vec b = a_u b_u + a_v b_v$. With $\hat b$ a unit vector, $\vec a \cdot \hat b$ is how much of $\vec a$ points along $\hat b$. Example: wind $(-5.571, -2.228)$ against "inland" $\hat n = (0, 1)$ is $-2.228$: the wind blows 2.228 blocks a second toward the sea, which is **offshore**.

### Which coast is which: $n_x$

Phase 03 stores, for every sea cell, `land_direction`: the unit vector $\vec n$ from that sea cell toward the nearest land. If $n_x > 0.5$, land is mostly to the east, so this sea lies off **the west coast of a continent**. If $n_x < -0.5$, land is mostly to the west: **the east coast**. Otherwise the coast faces north or south.

**Example.** A sea cell whose nearest land is 3 cells east and 1 cell north: $\vec n = (3, 1)/\sqrt{10} = (0.949, 0.316)$. $n_x = 0.949 > 0.5$: a west coast. A cell whose nearest land is due north, $\vec n = (0, 1)$: neither.

### Why real currents are warm or cold

Water near a continent's subtropical west coast comes from the poles and wells up from the deep, so it is cold: the Benguela off Namibia, the Humboldt off Peru. On subtropical east coasts, water comes from the equator, so it is warm: the Agulhas off Durban, the Gulf Stream. Further toward the poles the pattern turns around: a warm drift reaches north-west Europe, and a cold current runs down Labrador. The master plan's table (6.3) is this, written as rules on latitude and coast side.

### The latitude in this world is a mirror

Phase 02's latitude is a triangle wave (master plan 5.1). Inside $-38\,298 \le y \le 38\,298$ it is simply

$$\varphi = 90° \times \frac{y}{38\,298}$$

because $\arcsin(\sin a) = a$ there. Example: $y = 21\,277$ gives $\varphi = 50.00°$. Past the north pole, from $y = 38\,298$ to $114\,894$, latitude falls again: the strip from the pole to the next equator is a **mirror image** of the strip below it, reflected in $y$. At $y = 55\,319$ latitude is again $50°$ north, because $55\,319 = 76\,596 - 21\,277$.

A mirror reverses direction along $y$. So in a mirrored strip, "toward the equator" is $+y$, not $-y$, and a turn that is clockwise in the real strip looks anticlockwise in world coordinates. This phase writes that as one number per place:

$$e(y) = \operatorname{sgn}\!\left(\cos\frac{\pi y}{2Y}\right), \qquad Y = 38\,298$$

$e = +1$ where latitude rises with $y$ (around $y = 0$), and $e = -1$ in the mirrored strips (around $y = 76\,596$). **Example:** $y = 20\,000$: $\cos(\pi \times 20000 / 76596) = \cos(0.8203) = 0.682$, so $e = +1$. $y = 60\,000$: $\cos(2.461) = -0.776$, $e = -1$.

**Poleward,** as a world direction, is then the unit vector $(0,\ e \cdot \operatorname{sgn}\varphi)$, and equatorward is its opposite. At $y = 20\,000$ ($\varphi = 47°$ N, $e = +1$) poleward is $(0, +1)$, north. At $y = 60\,000$ ($\varphi = 49.8°$ N, $e = -1$) poleward is $(0, -1)$: walking $-y$ there takes you toward the pole at $y = 38\,298$.

The master plan's tables are written for the real strip. This phase applies them in every strip by multiplying their north-south parts by $e$. The Issues found section says why.

### Splines through knots, again

The master plan gives pressure, mean temperature and yearly swing as **knots** (6.4, 6.6), and says "between them it's a smooth curve, using the same spline code `terrain/density` already has". That code (`density_spline_build`, `terrain_density_spline_at`, `run/src/terrain/density/index.c:120-174`) uses Fritsch and Carlson's slopes, so the curve goes through every knot and never above or below its two neighbours.

Its values between knots are not the straight-line values. Worked out with the repo's slope rule:

| Curve | At | Straight line | Spline |
|---|---|---|---|
| $p_{\text{belt}}$ | 37.5° | 1016.5 hPa | **1017.64 hPa** |
| $p_{\text{belt}}$ | 20° | 1014.67 | **1014.86** |
| $A_{\text{sea}}$ | 15° | 2.00 °C | **1.95 °C** |
| $A_{\text{land}}$ | 15° | 5.50 | **5.39** |
| $A_{\text{sea}}$ | 45° | 4.50 | **4.37** |
| $A_{\text{land}}$ | 45° | 13.50 | **13.79** |
| $\bar T_{\text{lat}}$ | 28° | 20.80 | **20.99** |

The master plan's worked examples used straight lines, so they differ from what the code will give by up to 3%. This phase's tests use the spline's own values. See Issues found.

### Air pressure and its belts

**Air pressure** is the weight of the air above a place, in **hectopascals** (hPa). At sea level it is about 1013 hPa. Real pressure forms belts by latitude: low at the equator (rising hot air), high near 30° (sinking air: the deserts), low near 60°, high at the poles. The master plan's knots, by the wind latitude $\lvert\psi\rvert$ (next section): 0°: 1008, 15°: 1012, 30°: 1020, 45°: 1013, 60°: 1000, 75°: 1010, 90°: 1020.

**Example.** At $\lvert\psi\rvert = 33.27°$ (45° N in mid-July, below): the spline gives $p_{\text{belt}} = 1019.49$ hPa.

### Wind latitude: the belts follow the sun

The wind belts move north and south with the seasons, half as far as the sun and a month late (master plan 6.5):

$$\psi = \varphi - 0.5\,\delta(d - 30)$$

$\delta$ is phase 02's declination, $23.5° \sin(2\pi(d - 79)/365)$.

**Worked example.** $\varphi = 20°$ N. In mid-July, $d = 197$: $\delta(167) = 23.5 \sin(2\pi \times 88/365) = 23.46°$, so $\psi = 20 - 11.73 = 8.27°$. In mid-January, $d = 15$: $\delta(-15) = -23.47°$, so $\psi = 20 + 11.74 = 31.74°$. The same place sits in the trade winds in summer and at the edge of the westerlies in winter.

$\psi$ can pass 90° near a pole ($\varphi = 85°$ in mid-January gives $96.7°$). Every table is read at $\min(\lvert\psi\rvert, 90°)$.

### Smoothstep and blending belts

**Smoothstep** is the curve $S(t) = 3t^2 - 2t^3$ for $t$ from 0 to 1 (and 0 below, 1 above). It goes from 0 to 1 with zero slope at both ends, so a blend made with it has no corner.

**Example.** $S(0.25) = 3(0.0625) - 2(0.015625) = 0.15625$. $S(0.5) = 0.5$.

The master plan: "belts blend over 6°". So the trade winds give way to the westerlies from $\lvert\psi\rvert = 27°$ to $33°$, with weight $b_1 = S\big((\lvert\psi\rvert - 27)/6\big)$, and the westerlies give way to the polar easterlies from $57°$ to $63°$, with $b_2 = S\big((\lvert\psi\rvert - 57)/6\big)$.

### The belt wind, step by step

With unit directions from the master plan's table (normalised, see Issues found):

| Belt | Direction in the real north | Unit | Speed |
|---|---|---|---|
| Trades | $(-1, -0.4)$ | $(-0.9285, -0.3714)$ | 6 |
| Westerlies | $(+1, +0.3)$ | $(+0.9578, +0.2873)$ | 8 |
| Polar easterlies | $(-1, -0.2)$ | $(-0.9806, -0.1961)$ | 5 |

The north-south part is multiplied by a hemisphere sign. The master plan uses $\operatorname{sgn}\psi$, which jumps from $-1$ to $+1$ at the equator. This phase smooths it over $\pm 3°$, $\sigma(\psi) = 2\,S\big((\psi + 3)/6\big) - 1$, so the wind has no jump at the equator (the doldrums, where it is weak anyway). Then it is multiplied by $e$ for the mirrored strips.

**Calms.** The doldrums: speed is 40% within 5° of the equator. The horse latitudes: 50% near 30°. This phase gives each a 3° ramp so neither has a corner:

$$f_{\text{dol}} = 0.4 + 0.6\,S\!\left(\frac{\lvert\psi\rvert - 5}{3}\right), \qquad f_{\text{horse}} = 0.5 + 0.5\,S\!\left(\frac{\big\lvert\lvert\psi\rvert - 30\big\rvert - 2}{3}\right)$$

So $f_{\text{dol}} = 0.4$ up to 5°, 1 from 8°. $f_{\text{horse}} = 0.5$ from 28° to 32°, 1 below 25° and above 35°.

**The belt wind:**

$$\vec w_{\text{belt}} = f_{\text{dol}}\,f_{\text{horse}} \Big[(1 - b_2)\big((1 - b_1)\,6\,\hat t + b_1\,8\,\hat w\big) + b_2\,5\,\hat p\Big]$$

where $\hat t$, $\hat w$, $\hat p$ are the unit directions with their $v$ multiplied by $e\,\sigma(\psi)$.

**Worked examples** (real strip, $e = +1$):

| $\psi$ | $b_1$ | $b_2$ | $f_{\text{dol}} f_{\text{horse}}$ | $\vec w_{\text{belt}}$ |
|---|---|---|---|---|
| 15° | 0 | 0 | 1 | $(-5.571, -2.228)$: trade from the north-east |
| −15° | 0 | 0 | 1 | $(-5.571, +2.228)$: trade from the south-east |
| 4° | 0 | 0 | 0.4 | $(-2.228, -0.891)$: speed 2.4, the doldrums |
| 30° | 0.5 | 0 | 0.5 | $0.5 \times \big(0.5(-5.571) + 0.5(7.663),\ 0.5(-2.228) + 0.5(2.299)\big) = (0.523, 0.018)$ |
| 45° | 1 | 0 | 1 | $(7.663, 2.299)$: westerly from the south-west |
| 75° | 1 | 1 | 1 | $(-4.903, -0.981)$: polar easterly |

### Heat lows and cold highs

A continent heats up in summer, its air rises and pressure over it falls: a **heat low**. In winter it cools and pressure rises: a **cold high**. The master plan (6.4):

$$\Delta p_{\text{heat}} = -0.6\,\kappa\,\big(T_{\text{season}} - \bar T\big) \text{ hPa}$$

$T_{\text{season}} - \bar T$ is how far today's normal temperature is above the yearly mean. From 6.6, that is the swing term alone (the other terms do not change with the day):

$$T_{\text{season}} - \bar T = A\,s_h \cos\!\left(\frac{2\pi(d - 170 - \ell)}{365}\right), \quad A = A_{\text{sea}} + \kappa(A_{\text{land}} - A_{\text{sea}}), \quad \ell = 25 + 20(1 - \kappa)$$

$s_h = \operatorname{sgn}\varphi$ is the hemisphere ($+1$ north, $-1$ south).

**Worked example from the master plan.** $\kappa = 0.9$, a summer swing of +12 °C: $\Delta p = -0.6 \times 0.9 \times 12 = -6.48$ hPa, the master plan's $-6.5$. A heat low. In winter, $+6.48$: a cold high. At sea, $\kappa = 0$ and $\Delta p = 0$.

**Worked example in full.** A cell 3450 blocks from the sea at 45° N. $\kappa = 1 - e^{-3450/1500} = 1 - e^{-2.3} = 0.8997$. $A = 4.371 + 0.8997 \times (13.792 - 4.371) = 12.848$ °C. $\ell = 25 + 20 \times 0.1003 = 27.01$ days. On day 197: $\cos(2\pi(197 - 197.01)/365) = 1.0000$, so

$$\Delta p_{\text{heat}} = -0.6 \times 0.8997 \times 12.848 \times 1 = -6.936 \text{ hPa}$$

### Pressure gradient

The **pressure gradient** $\nabla \Delta p$ says how fast pressure changes per block, east and north: $\big(\partial \Delta p/\partial x,\ \partial \Delta p/\partial y\big)$, in hPa per block. It points toward rising pressure, so $-\nabla \Delta p$ points "downhill", toward low pressure. On the cell grid it is a **central difference**: the value one cell east minus the value one cell west, over the 128 blocks between them.

**Example.** $\Delta p$ is $-3.0$ hPa one cell north and $-2.0$ one cell south, and the same east and west: $\nabla \Delta p = (0,\ (-3.0 - (-2.0))/128) = (0, -0.0078)$ hPa per block. $-\nabla \Delta p = (0, +0.0078)$: downhill is north.

### Turning a vector: the rotation matrix

Air sliding downhill in pressure is turned by the Earth's spin, so it ends up nearly along the lines of equal pressure. The master plan: 70° clockwise from straight downhill in the north, 70° anticlockwise in the south.

A **rotation matrix** turns a vector by an angle $\theta$, anticlockwise for positive $\theta$:

$$R_\theta = \begin{pmatrix} \cos\theta & -\sin\theta \\ \sin\theta & \cos\theta \end{pmatrix}, \qquad R_\theta \begin{pmatrix} u \\ v \end{pmatrix} = \begin{pmatrix} u\cos\theta - v\sin\theta \\ u\sin\theta + v\cos\theta \end{pmatrix}$$

Clockwise by 70° is $\theta = -70°$. With $\cos 70° = 0.3420$ and $\sin 70° = 0.9397$:

$$R_{-70°} = \begin{pmatrix} 0.3420 & 0.9397 \\ -0.9397 & 0.3420 \end{pmatrix}, \qquad R_{+70°} = \begin{pmatrix} 0.3420 & -0.9397 \\ 0.9397 & 0.3420 \end{pmatrix}$$

**Checking the spin with a vector.** A low sits at the origin, and we stand 1000 blocks east of it, at $(1000, 0)$. Downhill points at the low: $-\nabla \Delta p \propto (-1, 0)$, west.

- **North:** $R_{-70°}(-1, 0) = (-0.3420,\ 0.9397)$. The wind blows mostly north, a little toward the low. North on the east side of a centre means the air goes round **anticlockwise**. That is right for a low in the north.
- **South:** $R_{+70°}(-1, 0) = (-0.3420,\ -0.9397)$. Mostly south on the east side: **clockwise**. Right for a low in the south.
- **A high,** north: downhill points away from it, $(+1, 0)$. $R_{-70°}(1, 0) = (0.3420, -0.9397)$: south on the east side, clockwise. Right.

The **spin** of a wind $\vec w$ at position $\vec r$ from a centre is the sign of $r_u w_v - r_v w_u$: positive is anticlockwise. Here $1000 \times 0.9397 - 0 = +939.7$: anticlockwise.

**In a mirrored strip** the world's $y$ is reversed, which turns clockwise into anticlockwise as seen in world coordinates. So the turn used is

$$\theta = -70° \times \operatorname{sgn}\varphi \times e$$

and a low in the mirrored north turns clockwise in world coordinates, which is exactly the mirror image of the real north.

**Wind from pressure** (master plan 6.5):

$$\vec w_p = k_p\,R_\theta\big(-\nabla \Delta p\big), \qquad k_p = 900 \text{ blocks}^2 \text{ per hPa per second}$$

**Example.** The gradient above, $-\nabla \Delta p = (0, 0.0078)$, in the real north: $R_{-70°}(0, 0.0078) = (0.9397 \times 0.0078,\ 0.3420 \times 0.0078) = (0.00733, 0.00267)$, times 900: $\vec w_p = (6.60, 2.40)$ blocks a second.

### The ground changes the wind

**Altitude** (master plan 6.5): speed $\times \min(2.5,\ 1 + z/800)$, with $z$ the cell's ground height, and 0 over the sea. Peaks are windy.

| $z$ (blocks) | 0 | 400 | 1000 | 1200 | 1500 |
|---|---|---|---|---|---|
| Factor | 1.00 | 1.50 | 2.25 | 2.50 | 2.50 (the cap) |

**Steering** (master plan 6.5): where the ground rises steeply across the wind, the wind turns along it. With slope gradient $\vec g$ (blocks of rise per block across, from phase 03's heights) and steepness $s = \min(1, \lvert \vec g \rvert)$, with $\hat g = \vec g / \lvert\vec g\rvert$:

$$\vec w' = \vec w - 0.7\,s\,(\vec w \cdot \hat g)\,\hat g$$

This removes part of the wind that blows straight up or down the slope, and leaves the part along the slope.

**Worked example.** Wind $(8, 0)$, east. Ground rising to the north-east at 0.8 blocks per block: $\vec g = 0.8(0.7071, 0.7071)$, $s = 0.8$, $\hat g = (0.7071, 0.7071)$. $\vec w \cdot \hat g = 5.657$. $0.7 \times 0.8 \times 5.657 = 3.168$. $\vec w' = (8, 0) - 3.168(0.7071, 0.7071) = (5.760, -2.240)$: turned 21.3° to the south, along the slope, at 6.18 blocks a second.

**Why steering never makes wind faster.** Write $a = \vec w \cdot \hat g$ and $k = 0.7s$, with $0 \le k \le 0.7$. Then

$$\lvert\vec w'\rvert^2 = \lvert\vec w\rvert^2 - 2ka^2 + k^2a^2 = \lvert\vec w\rvert^2 - a^2 k(2 - k) \le \lvert\vec w\rvert^2$$

since $k(2 - k) \ge 0$ for $0 \le k \le 2$. In the example: $64 - 32.0 \times 0.56 \times 1.44 = 38.2$, and $\sqrt{38.2} = 6.18$.

**Order:** belt wind plus pressure wind, then steering, then altitude. Steering works on direction; altitude only scales.

### A monsoon, and what this model really gives at 20° north

A **monsoon** is a wind that blows from the sea in summer, bringing heavy rain, and from the land in winter. The master plan's worked example (6.5) says a heat low 3000 blocks inland at 20° N makes a 2 blocks-a-second wind "blowing in from the sea", enough to beat the trade wind's 2.4 offshore.

Worked out with the formulas as written, on a straight coast at $y = 8510$ (20° N), sea to the south and land to the north, in mid-July:

1. **The heat low is weaker than the example assumes.** At 20°, $A_{\text{sea}} = 2.27$ and $A_{\text{land}} = 6.54$. 3000 blocks inland, $\kappa = 0.865$, $A = 5.96$, and $\Delta p = -0.6 \times 0.865 \times 5.96 \approx -3.1$ hPa, not $-6.5$.
2. **The steepest gradient** is about 700 blocks inland, where $\kappa$ rises fastest: about $0.0014$ hPa per block. $\lvert\vec w_p\rvert = 900 \times 0.0014 \approx 1.26$.
3. **The turn takes most of it along the coast.** Downhill is north, $(0, 1)$. $R_{-70°}(0, 1) = (0.9397, 0.3420)$. So $\vec w_p \approx (1.18, 0.43)$: only $0.43$ blows onshore. The master plan's example used the whole 2 as onshore and left out the turn.
4. **The belt wind** at $\psi = 8.27°$ is the full trade, $(-5.571, -2.228)$.
5. **The total** is about $(-4.39, -1.80)$: still offshore.
6. **In winter**, $\psi = 31.74°$ is in the horse latitudes, the belt is $(3.075, 0.891)$, the cold high's wind is $(-1.18, -0.43)$, and the total, about $(1.89, 0.46)$, is onshore.

So with the master plan's numbers, the heat low pushes the right way, but at 20° N the summer wind stays offshore and the winter wind is onshore: the reverse of a monsoon. This phase builds the formulas exactly as written, tests what does hold, and puts the choice to Ahmed in Issues found.

### Fitting a yearly wave by least squares

Wind and pressure change through the year. Storing 365 days for every cell would cost too much, so each is stored as a yearly average plus one yearly wave (master plan 6.9):

$$f(d) \approx a_0 + a_1\cos\theta + b_1\sin\theta, \qquad \theta = \frac{2\pi d}{365}$$

To find $a_0$, $a_1$, $b_1$, this phase works out the true value on 12 days spread evenly through the year, $d_k = 365k/12$ for $k = 0 \ldots 11$, so $\theta_k = 2\pi k/12 = k \times 30°$. Then it picks the three numbers that make the wave as close as possible to the 12 values, in the sense of **least squares**: the smallest sum of squared misses,

$$E = \sum_{k=0}^{11} \big(a_0 + a_1\cos\theta_k + b_1\sin\theta_k - f_k\big)^2$$

At the smallest $E$, its slope along each of the three numbers is zero. Setting $\partial E/\partial a_0 = \partial E/\partial a_1 = \partial E/\partial b_1 = 0$ gives three **normal equations**:

$$\begin{pmatrix} \sum 1 & \sum\cos\theta_k & \sum\sin\theta_k \\ \sum\cos\theta_k & \sum\cos^2\theta_k & \sum\cos\theta_k\sin\theta_k \\ \sum\sin\theta_k & \sum\sin\theta_k\cos\theta_k & \sum\sin^2\theta_k \end{pmatrix} \begin{pmatrix} a_0 \\ a_1 \\ b_1 \end{pmatrix} = \begin{pmatrix} \sum f_k \\ \sum f_k\cos\theta_k \\ \sum f_k\sin\theta_k \end{pmatrix}$$

For 12 evenly spaced angles the sums are exact: $\sum 1 = 12$, $\sum\cos\theta_k = \sum\sin\theta_k = 0$, $\sum\cos\theta_k\sin\theta_k = 0$, and $\sum\cos^2\theta_k = \sum\sin^2\theta_k = 6$. (Each cosine has a partner half a turn away with the opposite sign; and $\cos^2 = (1 + \cos 2\theta)/2$, whose $\cos 2\theta$ part also sums to zero.) The matrix is diagonal, so the solution is just:

$$a_0 = \frac{1}{12}\sum_k f_k, \qquad a_1 = \frac{1}{6}\sum_k f_k\cos\theta_k, \qquad b_1 = \frac{1}{6}\sum_k f_k\sin\theta_k$$

**Worked example.** A value that is 1 for the first half of the year and 0 for the second: $f = (1,1,1,1,1,1,0,0,0,0,0,0)$.

- $a_0 = 6/12 = 0.5$
- $a_1 = \frac{1}{6}(\cos 0° + \cos 30° + \cos 60° + \cos 90° + \cos 120° + \cos 150°) = \frac{1}{6}(1 + 0.866 + 0.5 + 0 - 0.5 - 0.866) = 0.1667$
- $b_1 = \frac{1}{6}(0 + 0.5 + 0.866 + 1 + 0.866 + 0.5) = \frac{3.732}{6} = 0.6220$

**A pure wave comes back exactly.** The heat low is itself one yearly wave: $\Delta p(d) = a\cos(\theta - \theta_0)$ with $a = -0.6\kappa A s_h$ and $\theta_0 = 2\pi(170 + \ell)/365$. Since $\cos(\theta - \theta_0) = \cos\theta\cos\theta_0 + \sin\theta\sin\theta_0$, the fit gives $p_0 = 0$, $p_{1c} = a\cos\theta_0$, $p_{1s} = a\sin\theta_0$ exactly (up to rounding). For the 45° N cell above: $a = -6.936$, $\theta_0 = 2\pi \times 197.01/365 = 3.3913$ rad, $\cos\theta_0 = -0.9689$, $\sin\theta_0 = -0.2471$, so $p_{1c} = 6.721$, $p_{1s} = 1.714$. Check on day 197 ($\theta = 3.3912$): $6.721 \times (-0.9689) + 1.714 \times (-0.2471) = -6.936$.

**The wind is not a pure wave.** A place that switches from trades to westerlies through the year has a wind shaped more like a step. One wave smooths it. Example: at 20° on open sea, the true $u$ goes from $-5.57$ to $+3.09$ through the year; the fitted wave goes from about $-7$ to $-0.13$. The average is right, and the season of strongest east wind is right, but the short westerly spell is lost. That is the cost the master plan chose (one wave for wind, two for rain). The test reports the largest miss, so Ahmed can see it.

---

## Design

### Files

New parts under `run/src/climate/` (phase 03 created the system), each with exactly one `index.*`, a `README.md` (purpose, tier, owns, lifetime, threads, public functions, callers, dependencies), and a header under `run/include/climate/<part>/index.h`:

| Directory | Depth, tier | Job |
|---|---|---|
| `run/src/climate/normals/` | 2, Arena | **The one home of every knot table** (master plan rule 5): $p_{\text{belt}}$ (6.4), $\bar T_{\text{lat}}$, $A_{\text{sea}}$, $A_{\text{land}}$ (6.6). Phase 05 adds its tables here. Built once at `climate_init` into splines in the climate's arena |
| `run/src/climate/harmonic/` | 2, Arena (allocates nothing) | The 12-day least-squares fit, and evaluating a stored wave. Phase 05 adds the two-wave form for rain |
| `run/src/climate/air/` | 2, Arena (allocates nothing) | Currents, heat lows, belt winds, pressure wind, steering, altitude, for one tile from its window; and reading air at a point and day |

Changes to phase 03's parts:

- `run/src/climate/cell/`: the window's arrays, today locals of `climate_cell_build_tile`, become one struct, `climate_window_t` (below), so `air` can read them. No number changes. After phase 03's cell loop, `climate_cell_build_tile` calls `climate_air_build_tile`.
- `run/src/climate/layer/`: three more layers.
- `run/src/climate/index.c`: `climate_air_at`, and building the normals in `climate_init`.

New tests:

| Directory | ctest |
|---|---|
| `tests/run/climate/normals/` | `run_climate_normals` |
| `tests/run/climate/harmonic/` | `run_climate_harmonic` |
| `tests/run/climate/air/` | `run_climate_air` |

`tests/run/climate/fixtures/ground.h` gains the synthetic continents and coasts below.

**One addition to the terrain:** `run/include/terrain/density/index.h` gains

```c
/* Builds a spline through count knots, xs rising, with the same slopes terrain_density's own splines have. For anything
 * that wants that curve on its own knots, such as the climate's tables. */
void terrain_density_spline_build(terrain_density_spline_t *spline, const float64_t *xs, const float64_t *ys,
                                  uint32_t count);
```

a public wrapper round the static `density_spline_build` (`run/src/terrain/density/index.c:120`). No terrain number changes. The master plan says the climate's knots use "the same spline code `terrain/density` already has", and this is the smallest way to reach it.

### Constants

In `run/include/climate/air/index.h` unless said. Each named, with its unit and source.

```c
/* Currents, master plan 6.3 */
static const float32_t CLIMATE_CURRENT_REACH = 600.0f32;         /* blocks offshore a current is felt */
static const float32_t CLIMATE_CURRENT_FULL_WITHIN = 32.0f32;    /* this phase: a sea cell next to land, half a cell, has full strength */
static const float32_t CLIMATE_CURRENT_LAND_SCALE = 600.0f32;    /* blocks: e^{-d_s/600} on land */
static const float32_t CLIMATE_CURRENT_SIDE = 0.5f32;            /* |n_x| over this picks a west or east coast */
static const float32_t CLIMATE_CURRENT_WARM_C = 4.0f32;          /* °C */
static const float32_t CLIMATE_CURRENT_COLD_C = -5.0f32;         /* °C */
static const float32_t CLIMATE_CURRENT_WEAK = 0.5f32;            /* this phase: "warm, weak" is half strength */
static const float32_t CLIMATE_CURRENT_BANDS_DEG[3] = {15.0f32, 45.0f32, 65.0f32};
static const float32_t CLIMATE_SEA_PEAK_DAY = 215.0f32;          /* day the sea is warmest in the north */

/* Pressure, master plan 6.4 */
static const float32_t CLIMATE_HEAT_LOW_HPA_PER_C = -0.6f32;     /* hPa per °C of seasonal swing, times κ */

/* Winds, master plan 6.5 */
static const float32_t CLIMATE_WIND_LAG_DAYS = 30.0f32;          /* ψ uses δ(d - 30) */
static const float32_t CLIMATE_WIND_SHIFT = 0.5f32;              /* ψ moves half as far as the sun */
static const float32_t CLIMATE_TRADE_SPEED = 6.0f32;             /* blocks a second */
static const float32_t CLIMATE_WESTERLY_SPEED = 8.0f32;
static const float32_t CLIMATE_POLAR_SPEED = 5.0f32;
static const float32_t CLIMATE_TRADE_DIR[2] = {-1.0f32, -0.4f32};    /* normalised at use */
static const float32_t CLIMATE_WESTERLY_DIR[2] = {1.0f32, 0.3f32};
static const float32_t CLIMATE_POLAR_DIR[2] = {-1.0f32, -0.2f32};
static const float32_t CLIMATE_BELT_EDGES_DEG[2] = {30.0f32, 60.0f32};
static const float32_t CLIMATE_BELT_BLEND_DEG = 6.0f32;
static const float32_t CLIMATE_HEMISPHERE_BLEND_DEG = 3.0f32;    /* this phase: σ(ψ) over ±3° */
static const float32_t CLIMATE_DOLDRUMS_DEG = 5.0f32;
static const float32_t CLIMATE_DOLDRUMS_SPEED = 0.4f32;
static const float32_t CLIMATE_HORSE_DEG = 2.0f32;               /* this phase: full calm within 2° of 30° */
static const float32_t CLIMATE_HORSE_SPEED = 0.5f32;
static const float32_t CLIMATE_CALM_RAMP_DEG = 3.0f32;           /* this phase */
static const float32_t CLIMATE_PRESSURE_TURN_DEG = 70.0f32;
static const float32_t CLIMATE_PRESSURE_WIND_K = 900.0f32;       /* blocks² per hPa per second */
static const float32_t CLIMATE_ALTITUDE_SCALE = 800.0f32;        /* blocks */
static const float32_t CLIMATE_ALTITUDE_MAX = 2.5f32;
static const float32_t CLIMATE_STEER = 0.7f32;

/* The fit, master plan 6.9 */
enum : uint32_t { CLIMATE_FIT_DAYS = 12u };
```

Knots, in `run/src/climate/normals/index.c`, each table with a comment naming its master plan section:

```c
static const float64_t NORMALS_LAT_DEG[] = {0, 15, 30, 45, 60, 75, 90};           /* written with the f64 suffix in code */
static const float64_t NORMALS_P_BELT_HPA[] = {1008, 1012, 1020, 1013, 1000, 1010, 1020};  /* 6.4, by |ψ| */
static const float64_t NORMALS_T_MEAN_C[] = {27, 26, 20, 11, 0, -14, -24};                 /* 6.6, by |φ| */
static const float64_t NORMALS_SWING_LAT_DEG[] = {0, 30, 60, 90};
static const float64_t NORMALS_A_SEA_C[] = {1, 3, 6, 10};                                  /* 6.6 */
static const float64_t NORMALS_A_LAND_C[] = {2, 9, 18, 22};                                /* 6.6 */
```

### The window, from phase 03

```c
/* One tile's window: the tile plus CLIMATE_MARGIN_CELLS on every side, CLIMATE_WINDOW_CELLS a side, south row first. All in
 * the building worker's Job arena, gone when the job ends. Built by climate_cell_build_tile, read by climate_air_build_tile. */
typedef struct climate_window_t {
    const float32_t *heights;       /* 0: ground at each cell centre, blocks */
    const uint8_t *kind;            /* 8: CLIMATE_SEA_KIND_* */
    const uint32_t *sea_distance2;  /* 16: cells², to the nearest sea */
    const uint32_t *sea_nearest;    /* 24: window index of that sea cell */
    const uint32_t *land_distance2; /* 32: cells², to the nearest land or small lake */
    const uint32_t *land_nearest;   /* 40 */
    int32_t tile_x;                 /* 48 */
    int32_t tile_y;                 /* 52 */
} climate_window_t;
static_assert(sizeof(climate_window_t) == 56);
```

Window cell $(i, j)$ has its centre at $x = (t_x - 8) \cdot 1024 + 64i + 32$, $y = (t_y - 8) \cdot 1024 + 64j + 32$. The tile's own cells are $i, j = 128 \ldots 143$.

### Current kinds

```c
enum : uint8_t {
    CLIMATE_CURRENT_NONE = 0,  /* neutral, the master plan's third kind */
    CLIMATE_CURRENT_WARM = 1,
    CLIMATE_CURRENT_COLD = 2,
};
```

### What `climate_air_build_tile` does

```c
/* Fills current, current_strength, t_swing, t_lag_days, wind[6] and pressure[3] of a tile's 256 cells, whose phase 03 fields
 * are already set. Pure: the same bytes from the same window. Its scratch, about 50 KiB, comes from arena (the worker's
 * Job arena); nothing large goes on the stack. */
void climate_air_build_tile(const climate_t *climate, arena_t *arena, const climate_window_t *window,
                            climate_cell_t *cells);
```

Step by step, for the tile's cells and a ring of one cell round them (18 by 18, window $i, j = 127 \ldots 144$), which the gradients need. Every loop runs south row first, x fastest, so every sum adds in one order.

**1. The current of a sea window cell** (a function, used for the tile's sea cells and for the nearest sea of each land cell): with $\vec n$ the unit vector from this cell's centre to its `land_nearest` cell's centre, and $\lvert\varphi\rvert$ at this cell's centre:

| $\lvert\varphi\rvert$ | $n_x > 0.5$ (west coast) | $n_x < -0.5$ (east coast) | Otherwise |
|---|---|---|---|
| below 15° | Warm | Warm | Warm, weak |
| 15° to below 45° | Cold | Warm | None |
| 45° to below 65° | Warm | Cold | None |
| 65° and up | Cold | Cold | Cold |

A band's lower edge belongs to it: exactly 15° is the second row. $n_x$ uses only $x$, which is never mirrored, so west and east coasts are the same in every strip.

**2. A sea cell's strength,** with $d_l$ its `land_distance` in blocks (phase 03's formula, $64\sqrt{D} - 32$):

$$\text{fade} = \operatorname{clamp}\!\left(\frac{600 - d_l}{600 - 32},\ 0,\ 1\right), \qquad \text{current\_strength} = \operatorname{round}\big(255 \times \text{fade} \times w\big)$$

with $w = 0.5$ for "warm, weak" and 1 otherwise. A sea cell next to land ($d_l = 32$) has full strength, and it reaches 0 at 600 blocks, so "strength fades over 600 blocks offshore" with no step at 600. Past 600, `current = NONE`, strength 0. A large lake cell has no current: lakes count "not for currents" (6.2).

**3. A land cell's current** is its nearest sea's (`sea_nearest`, whose kind step 1 works out, whatever that sea cell's own distance from land), with strength $\operatorname{round}(255\,e^{-d_s/600})$ times $w$. So phase 05 gets $\Delta T_{\text{current}} = C \times \text{current\_strength}/255$, which is $C\,e^{-d_s/600}$ as 6.3 says, to within the byte's rounding. With no sea in reach: `NONE`, 0.

**4. Swing and lag** (6.6, needed by 6.4): $A = A_{\text{sea}}(\lvert\varphi\rvert) + \kappa\,(A_{\text{land}} - A_{\text{sea}})$, `t_swing` $= A\,s_h$, `t_lag_days` $= 25 + 20(1 - \kappa)$. For ring cells $\kappa$ comes from the window's `sea_distance2`, the same formula as phase 03.

**5. The 12 days.** For $k = 0 \ldots 11$, $d_k = 365k/12$ (a fraction of a day; every formula takes a `float32_t` day):

- a. $\Delta p_k$ for each of the 18 by 18 cells: $-0.6\,\kappa\,A\,s_h\cos\big(2\pi(d_k - 170 - \ell)/365\big)$. Zero for sea.
- b. For each tile cell, the gradient by central differences: $g_x = (\Delta p_{i+1} - \Delta p_{i-1})/128$, $g_y = (\Delta p_{j+1} - \Delta p_{j-1})/128$, hPa per block.
- c. $\vec w_p = 900\,R_\theta(-g_x, -g_y)$ with $\theta = -70° \cdot \operatorname{sgn}\varphi \cdot e$. On the equator ($\varphi = 0$ exactly) there is no turn and $\vec w_p = 0$, as the Earth's spin does nothing there.
- d. $\psi_k = \varphi - 0.5\,\delta(d_k - 30)$ and $\vec w_{\text{belt}}(\psi_k)$ as in Background.
- e. $\vec w = \vec w_{\text{belt}} + \vec w_p$.
- f. Steering with the slope from the window's heights, with the sea flat at 0: $\vec g = \big(h^+_{i+1} - h^+_{i-1},\ h^+_{j+1} - h^+_{j-1}\big)/128$, $h^+ = \max(0, h)$. If $\lvert\vec g\rvert < 10^{-6}$, skip.
- g. Altitude: times $\min\big(2.5,\ 1 + \max(0, z_{\text{ground}})/800\big)$.
- h. Keep $u_k$, $v_k$, $\Delta p_k$.

**6. The fit.** `climate_harmonic_fit1` on $u_k$, on $v_k$ and on $\Delta p_k$: `wind = {u0, u1c, u1s, v0, v1c, v1s}`, `pressure = {p0, p1c, p1s}`.

The belt pressure $p_{\text{belt}}(\psi)$ is not stored: it depends only on $y$ and the day, so it is worked out when read.

### `climate_harmonic`

```c
/* The least-squares fit of a0 + a1 cos θ + b1 sin θ to values on the CLIMATE_FIT_DAYS days d_k = 365 k / 12, θ = 2π d / 365.
 * Sums in float64_t, in k order. out = {a0, a1, b1}. */
void climate_harmonic_fit1(const float32_t values[CLIMATE_FIT_DAYS], float32_t out[3]);
/* The wave on any day, d from 0 to below 365, fractions allowed. */
float32_t climate_harmonic_at1(const float32_t coefficients[3], float32_t day);
```

The 12 cosines and sines are a `static const float64_t` table (exact values: $0, \pm 0.5, \pm\frac{\sqrt 3}{2}, \pm 1$), so no `cos` is called per cell.

### Reading air

```c
/* Air at a block and a day, blended between cells as phase 03 blends (the same warp, the same four cells). */
typedef struct climate_air_t {
    float32_t wind[2];           /* 0: u east, v north, blocks a second, from the stored waves */
    float32_t pressure_hpa;      /* 8: p_belt + Δp_heat, no weather systems (phase 06 adds them) */
    float32_t pressure_belt_hpa; /* 12: at this exact y and day */
    float32_t pressure_heat_hpa; /* 16: from the stored wave */
    float32_t current_c;         /* 20: C × strength / 255, blended: what phase 05 adds to temperature */
    uint8_t current;             /* 24: the nearest cell's kind, to the warped point */
    uint8_t current_strength;    /* 25: the same cell's */
    uint8_t _pad0[2];            /* 26 */
} climate_air_t;
static_assert(sizeof(climate_air_t) == 28);

/* False, writing nothing, if a tile it needs is not built. Any thread. day from 0 to below 365. */
bool climate_air_at(const climate_t *climate, float64_t x, float64_t y, float32_t day, climate_air_t *out);

/* 6.3: T_sea = T̄_lat(φ) + C × strength / 255 + A_sea(φ) s_h cos(2π (d - 215) / 365), °C. For a sea cell. */
float32_t climate_sea_temperature(const climate_t *climate, const climate_cell_t *cell, float32_t day);

/* A current's flow direction for the Atlas, a unit vector along the coast: cold toward the equator, warm toward the pole.
 * (0, 0) for NONE. */
void climate_current_flow(const climate_cell_t *cell, float32_t y_centre, float32_t out[2]);
```

**Blending what is blended.** The six wind coefficients, the three pressure coefficients and $C \times \text{strength}/255$ are blended bilinearly; then the wave is evaluated. Evaluating and blending commute because both are linear, so blending coefficients is the same as blending the day's values, and cheaper. The current's kind is a category, so it comes from the nearest cell, never a blend.

**Flow direction** (6.3): the two directions along the coast are $\vec t_\pm = \pm(-n_y, n_x)$. Poleward is $(0, e\,\operatorname{sgn}\varphi)$. A warm current takes the $\vec t$ whose dot product with poleward is positive, a cold one the other. If the dot product is 0 (a coast facing due north or south), warm takes the eastward one and cold the westward, by rule. For a land cell, $\vec n$ is its nearest sea's.

**Example.** A sea cell at 30° S ($y = -12\,766$, $e = +1$, poleward $(0, -1)$) off a west coast with $\vec n = (1, 0)$: the current is cold. $\vec t_\pm = \pm(0, 1)$. Poleward·$(0, 1) = -1 < 0$, so cold takes $(0, 1)$: it flows north, toward the equator, like the Benguela.

### Layers

Added to `climate_layer_t` (phase 03, `run/include/climate/layer/index.h`):

| Layer | Drawn as |
|---|---|
| `CLIMATE_LAYER_CURRENTS` | Phase 03's water layer underneath. An arrow every 32 pixels on sea cells with a current: red `0xE03030` warm, blue `0x3060E0` cold, grey `0x909090` neutral. Length $4 + 12 \times \text{strength}/255$ pixels along the flow direction. Land unchanged |
| `CLIMATE_LAYER_PRESSURE` | Colour from 990 hPa (`0x3050C0`) through 1013 (`0xF0F0F0`) to 1030 (`0xC04030`), linear in each half. A 1-pixel dark line where total pressure crosses a multiple of 4 hPa (the master plan's "lines every 4 hPa", 9.3) |
| `CLIMATE_LAYER_WIND` | Speed as colour, 0 (`0x202040`) to 15 blocks a second (`0xF0E040`). An arrow every 32 pixels, length $2 \times$ speed pixels, capped at 30, pointing downwind |

Each uses `climate_layer_draw`'s `day` argument, which is the day slider. Arrows are drawn with an integer line walk (Bresenham's line algorithm: step one pixel along the longer axis each time, and one along the shorter axis whenever the error built up passes half a pixel), clipped to the picture, with no allocation. Pixel $(0, 0)$ is the north-west corner, so a vector's $v$ is drawn upward.

### Threads, memory and cost

Nothing new about threads: the work runs inside phase 03's tile job, on one worker per tile, in a fixed order, so the same bytes come from 1 worker or 16. The scratch is 18 by 18 cells × 12 days × 3 floats, about 46 KiB, from the Job arena, plus 18 by 18 $\kappa$, $A$, $\ell$. Cost per tile: 256 cells × 12 days of a few dozen multiplications, and 324 × 12 cosines: well under 1 ms. The normals' splines live in the climate's `arena_tables` (phase 03), built once in `climate_init`, read-only after.

### Edge cases

| Case | What happens |
|---|---|
| A mirrored strip ($e = -1$) | North-south parts of belt winds, the turn's sense and "poleward" all flip. Coast side ($n_x$) does not. Tested |
| A pole ($\lvert\varphi\rvert$ near 90°) | $\lvert\psi\rvert$ clamped to 90°. Polar easterlies. $e$ changes sign at the pole itself, and $\varphi$'s sign does not, so $v$ changes sign there: the polar wind blows away from the pole on both sides, as air flowing out of a polar high does |
| The equator ($\varphi = 0$) | $s_h = 0$ would mean no swing; this phase uses $s_h = +1$ at exactly 0 so the cell gets a swing (only a cell centred exactly on $y = 0$ has $\varphi = 0$, and phase 03's centres are at $64k + 32$, so none does). No pressure turn: $\vec w_p = 0$ when $\operatorname{sgn}\varphi = 0$ |
| A band edge in latitude | Kinds change in a step at 15°, 45°, 65°. Temperature (phase 05) blends $C \times \text{strength}$ between cells, so the step in kind becomes a 64-block slope in temperature. The current arrows change colour at the line, which is what a real map shows |
| A sea cell with no land within 8192 | `land_nearest` has none: `NONE`, 0, no flow |
| A land cell with no sea within 8192 | `NONE`, 0. $\kappa = 0.99575$ still gives it a heat low |
| A large lake | No current. Its $\kappa$ is from the sea, as phase 03 sets it, so a big inland lake does not cancel a heat low. Phase 05's moisture is where lakes count |
| The 18 by 18 ring at the tile edge | From the window, which reaches 8 km past the tile, so a gradient at the edge uses the true neighbour, and two tiles agree at their border |
| Negative coordinates | Nothing new: phase 03's floors and window indexing |

## Owned paths

This phase may edit these, plus this file:

- `run/{src,include}/climate/normals/`, `run/{src,include}/climate/harmonic/`, `run/{src,include}/climate/air/` (new)
- `run/{src,include}/climate/cell/` (the window struct, the call to `air`), `run/{src,include}/climate/layer/` (three layers), `run/{src,include}/climate/index.{c,h}` and `run/src/climate/README.md` (`climate_air_at`, building the normals)
- `tests/run/climate/normals/`, `tests/run/climate/harmonic/`, `tests/run/climate/air/` (new); `tests/run/climate/fixtures/ground.h`; `tests/run/climate/cell/index.c` (its digest pin, re-set with a line saying why)
- `bench/climate/` (the new layers in `--pictures`, the tile timing re-run)

**Wiring outside them, kept minimal:**

- `run/{src,include}/terrain/density/index.{c,h}`, `run/src/terrain/density/README.md`: only `terrain_density_spline_build`, a public wrapper. Every terrain pin must hold
- `plans/REPO_STRUCTURE.md`: rows for `climate/normals`, `climate/harmonic`, `climate/air` and their tests

## Steps

Build order. Each step builds and passes its tests before the next.

1. **Read phase 03 as built.** Check the names of its window arrays, `climate_sample_at`'s warp and blend helpers, and phase 02's latitude and declination functions. Use those names. *Why: this phase plugs into both.*
2. **Add `terrain_density_spline_build`** to `run/src/terrain/density/`. Run `run_terrain_density`: its pin must hold. *Why: the master plan's knots use the terrain's spline code, and its builder is static today.*
3. **Create `run/src/climate/normals/`** with the four knot tables and their splines, built in `climate_init` into `arena_tables`. Tests `run_climate_normals`. *Why: one home for every knot, and phase 05 adds to it.*
4. **Create `run/src/climate/harmonic/`** with the fit and the evaluation, and `run_climate_harmonic`. *Why: the maths is small and exact, and it is tested alone before it hides inside a tile.*
5. **Refactor phase 03's window into `climate_window_t`** in `run/src/climate/cell/`. Run `run_climate_.*`: phase 03's digest must not move yet. *Why: proves the refactor changed nothing before new numbers arrive.*
6. **Create `run/src/climate/air/`** in this order, each with its tests in `run_climate_air`: the mirror sign $e$ and poleward; the belt wind (smoothstep, $\sigma$, calms); the rotation; steering and altitude; currents and strength; swing and lag; heat-low pressure and its gradient; the 12-day loop and the fit; `climate_air_build_tile`. *Why: each piece has its own worked example to be checked against before the next uses it.*
7. **Call `climate_air_build_tile`** at the end of `climate_cell_build_tile`. Phase 03's digest pin now moves: re-set it, with a comment saying "phase 04 fills currents, swing, lag, wind and pressure".
8. **Add `climate_air_at`, `climate_sea_temperature` and `climate_current_flow`** to the Owner.
9. **Add the three layers** to `run/src/climate/layer/`, with tests.
10. **Add the synthetic continents to `tests/run/climate/fixtures/ground.h`**, and the continent tests.
11. **Extend `bench/climate/`:** the new layers in `--pictures` on days 15, 105, 196 and 288, and `--synthetic`, which draws the two continents. Re-run the tile timing.
12. **Update `plans/REPO_STRUCTURE.md`** and every new README.
13. **Run everything** (Verification), record the bench and the fit's largest miss here, tick the todo as each item lands.

## Todo

- [x] Phase 02 and 03 names found and used
- [x] `terrain_density_spline_build`, public; every terrain pin unchanged
- [x] `run/src/climate/normals/`: $p_{\text{belt}}$, $\bar T_{\text{lat}}$, $A_{\text{sea}}$, $A_{\text{land}}$ in one home
- [x] `run/src/climate/harmonic/`: the 12-day least-squares fit
- [x] Phase 03's window as `climate_window_t`, with no number moved
- [x] Currents by 6.3, with strength and flow direction, in `run/src/climate/air/`
- [x] Pressure belts and heat lows (6.4)
- [x] Wind belts, doldrums and horse latitudes, wind from pressure, altitude, steering by slopes (6.5)
- [x] Wind and pressure stored as averages plus a yearly wave, fitted from 12 days
- [x] `climate_air_at`, `climate_sea_temperature`, `climate_current_flow`
- [x] Map layers: currents, pressure, wind, each with the day slider
- [x] Tests: on a synthetic continent at 30° south, the west coast is cold and the east coast warm, and at 50° the reverse; trades blow from the east between 0° and 30°; a heat low makes an onshore summer wind on a synthetic coast at 20° (see Issues found); steering never makes wind faster
- [x] 1 and 16 workers give the same bytes; the digest re-pinned with its reason
- [x] Bench: one tile on one worker still under 100 ms
- [x] A `README.md` in every new source directory, exactly one `index.*` in each
- [x] `plans/REPO_STRUCTURE.md` updated
- [x] The whole debug suite passes; `standards_lint` and `clang_format` pass
- [ ] Gate pictures drawn and shown to Ahmed, and the monsoon question answered

## Tests

### The synthetic grounds, in `tests/run/climate/fixtures/ground.h`

Each is a pure function of $(x, y)$, given as `climate_ground_fn_t` to `climate_open_ground` (phase 03).

- **Continent at 30° S:** land ($h = 30$) for $-2048 \le x < 2048$ and $-14\,766 \le y < -10\,766$ (latitudes $-34.70°$ to $-25.30°$, all in the 15° to 45° band); sea ($h = -60$) elsewhere. 4 km square, centred on 30° S ($y = -12\,766$).
- **Continent at 50° N:** the same shape for $19\,277 \le y < 23\,277$ ($45.30°$ to $54.70°$, all in the 45° to 65° band), centred on 50° N ($y = 21\,277$).
- **Mirrored continent at 50° N:** the same for $53\,319 \le y < 57\,319$, centred on $y = 55\,319$, which is 50° N in the mirrored strip.
- **Continent at 8° N and at 70° N** (the outer bands): $1404 \le y < 5404$ and $27\,787 \le y < 31\,787$.
- **Coast at 20° N:** land for $y \ge 8512$ (cell line nearest 20° N), sea below, all $x$. Land height 20.
- **Islands for spin:** a round island of radius 4000 blocks centred at $(0, 19\,149)$ (45° N), at $(0, -19\,149)$ (45° S), and at $(0, 57\,447)$ (45° N mirrored).
- **Open sea:** $h = -100$ everywhere.
- **A ridge:** land, $h = 0.8 \times (x - 10\,000)$ for $x$ from 10,000 to 10,500, flat elsewhere, for steering on real cells.

### `run_climate_normals`

- **`the_knots_are_the_master_plan_s_and_the_curves_pass_through_them`.** $p_{\text{belt}}$ at 0, 15, 30, 45, 60, 75, 90 is 1008, 1012, 1020, 1013, 1000, 1010, 1020 exactly; the same for $\bar T_{\text{lat}}$, $A_{\text{sea}}$, $A_{\text{land}}$. Fails on a mistyped knot.
- **`between_knots_the_curves_are_the_terrain_s_spline`.** $p_{\text{belt}}(37.5) = 1017.6375$, $p_{\text{belt}}(20) = 1014.8642$, $A_{\text{sea}}(15) = 1.9500$, $A_{\text{land}}(45) = 13.7921$, $\bar T_{\text{lat}}(28) = 20.9862$, each $\pm 10^{-4}$. Fails if a straight line or another spline is used.
- **`a_value_past_ninety_is_the_value_at_ninety`.** $p_{\text{belt}}(96.7) = 1020$.

### `run_climate_harmonic`

- **`a_half_year_step_fits_to_the_worked_example`.** $(1,1,1,1,1,1,0,0,0,0,0,0)$ gives $(0.5000, 0.1667, 0.6220) \pm 10^{-4}$.
- **`a_pure_wave_comes_back_exactly`.** $f_k = 3 + 2\cos\theta_k - 5\sin\theta_k$ gives $(3, 2, -5) \pm 10^{-5}$, and `climate_harmonic_at1` reproduces all 12 values to $10^{-5}$.
- **`a_constant_has_no_wave`.** $(c, 0, 0)$.
- **`the_heat_low_wave_is_the_worked_example`.** The 45° N cell, $\kappa = 0.8997$: $p_0 = 0 \pm 10^{-4}$, $p_{1c} = 6.721 \pm 0.002$, $p_{1s} = 1.714 \pm 0.002$, and day 197 evaluates to $-6.936 \pm 0.002$.

### `run_climate_air`

- **`the_mirror_sign_is_plus_near_the_equator_and_minus_past_the_pole`.** $e(20\,000) = +1$, $e(60\,000) = -1$, $e(-20\,000) = +1$, $e(-60\,000) = -1$. Poleward at $y = 60\,000$ is $(0, -1)$.
- **`the_belt_wind_is_the_worked_examples`.** The six rows of the Background table, each $\pm 10^{-3}$.
- **`trades_blow_from_the_east_between_0_and_30_degrees`.** (a) The belt wind's $u < 0$ for every $\psi$ from $-26.9°$ to $26.9°$ in steps of $0.1°$ (the blend into the westerlies starts at 27°). (b) On open sea, every cell with $\lvert\varphi\rvert \le 15°$ has $u < 0$ on all 12 fit days, from the stored wave (at $\lvert\varphi\rvert \le 15°$, $\psi$ stays inside $\pm 26.75°$ all year). (c) Every open-sea cell with $\lvert\varphi\rvert \le 22°$ has a yearly mean $u_0 < 0$. Prints $u_0$ at 1°, 10°, 20° and 25° (expect about $-4.4$, $-4.3$, $-3.6$, $-1.0$). Fails if a sign or the hemisphere smoothing is wrong.
- **`westerlies_blow_from_the_west_and_polar_winds_from_the_east`.** Belt $u > 0$ for $\lvert\psi\rvert$ from 35° to 55°, $u < 0$ from 63° to 90°.
- **`the_doldrums_and_horse_latitudes_slow_the_wind`.** Speed at $\psi = 4°$ is $2.400 \pm 10^{-3}$ (0.4 × 6); $f_{\text{horse}}(30) = 0.5$, $f_{\text{horse}}(28) = 0.5$, $f_{\text{horse}}(36) = 1$; $f_{\text{dol}}(8) = 1$.
- **`the_belt_wind_has_no_jumps`.** From $\psi = -90°$ to $90°$ in steps of $0.01°$, no component changes by more than $0.1$ blocks a second between steps (the steepest smooth change, in the blend into the polar easterlies near 60°, is about $0.031$ per $0.01°$). Fails on a hard sign or a hard belt edge.
- **`the_rotation_turns_downhill_seventy_degrees_clockwise_in_the_north`.** $R_{-70°}(-1, 0) = (-0.3420, 0.9397)$, $R_{+70°}(-1, 0) = (-0.3420, -0.9397)$, $R_{-70°}(0, 0.0078) \times 900 = (6.597, 2.401)$, each $\pm 10^{-3}$. At $y = 60\,000$, $\varphi > 0$, the turn is $+70°$.
- **`winds_circle_a_summer_heat_low_anticlockwise_in_the_north_and_clockwise_in_the_south`.** On the three spin islands on day 197 (northern summer) and day 15 (southern summer), using only the pressure wind (belt subtracted): for every land cell between 1000 and 3500 blocks from the centre, the spin $r_u w_v - r_v w_u$ is positive (anticlockwise) for the real 45° N island in its summer, negative for the 45° S island in its summer, and negative in world coordinates for the mirrored 45° N island. In each one's winter (the cold high) the signs reverse. Fails if the turn ignores the hemisphere or the mirror.
- **`steering_never_makes_wind_faster`.** 1,000,000 random winds (each component $-30$ to $30$) and slopes ($\lvert\vec g\rvert$ 0 to 3, any direction), from `terrain_rng`: $\lvert\vec w'\rvert \le \lvert\vec w\rvert (1 + 10^{-6})$. The worked example $(8, 0)$, $\vec g = 0.8(0.7071, 0.7071)$ gives $(5.760, -2.240) \pm 10^{-3}$. A wind along the slope ($\vec w \perp \vec g$) is unchanged. A flat cell ($\vec g = 0$) is unchanged. On the ridge ground, every cell's stored wind on every fit day is no faster than the same cell's wind with steering turned off by a test hook, times its altitude factor.
- **`altitude_makes_peaks_windier_up_to_two_and_a_half_times`.** Factors 1, 1.5, 2.25, 2.5, 2.5 at $z$ = 0, 400, 1000, 1200, 1500; a sea cell 1.
- **`on_a_continent_at_30_south_the_west_coast_is_cold_and_the_east_coast_warm`.** Sea cells within 600 blocks of land with $n_x > 0.5$ (west of the continent): all `COLD`. With $n_x < -0.5$: all `WARM`. North and south coasts ($\lvert n_x\rvert \le 0.5$): all `NONE`. At least 30 cells of each counted. Cold flows north (toward the equator), warm flows south.
- **`on_a_continent_at_50_north_the_west_coast_is_warm_and_the_east_coast_cold`.** The reverse kinds; warm flows north, cold flows south. And on the **mirrored** 50° N continent: the same kinds, but warm flows $-y$ and cold $+y$ (still toward the pole and the equator).
- **`the_outer_bands_are_warm_near_the_equator_and_cold_near_the_poles`.** At 8° N: west and east coasts `WARM` at full strength, north and south coasts `WARM` at half. At 70° N: every coast `COLD`.
- **`a_current_fades_over_600_blocks_offshore`.** Off the 30° S west coast: strength 255 at $d_l = 32$, $\operatorname{round}(255 \times 504/568) = 226$ at 96, $\operatorname{round}(255 \times 120/568) = 54$ at 480, `NONE` and 0 at 608.
- **`land_takes_the_current_of_its_nearest_sea`.** Inside the 30° S continent, a cell 288 blocks from the west coast (and further from every other coast): `COLD`, strength $\operatorname{round}(255\,e^{-0.48}) = 158$; next to the coast, $d_s = 32$: 242. `current_c` from `climate_air_at` there is $-5 \times 158/255 = -3.10 \pm 0.02$.
- **`the_sea_temperature_is_the_formula`.** A sea cell at 15° N with a full-strength warm current: day 215 gives $26 + 4 + 1.950 = 31.95 \pm 0.01$ °C; day 32 gives $28.05 \pm 0.01$; the same cell with no current on day 32, $24.05 \pm 0.01$. (The master plan's 32, 28 and 24 used $A_{\text{sea}}(15) = 2$; Issues found.) A cell at 15° S on day 32 gives the northern day 215's swing, $+1.95$.
- **`the_heat_low_is_the_worked_examples`.** $\kappa = 0.9$, swing 12: $-6.48$. The 45° N cell at 3450 blocks: $-6.936 \pm 0.002$ on day 197, and $+6.936$ half a year later. A sea cell: 0 on every day.
- **`a_heat_low_turns_the_summer_wind_onshore_on_a_coast_at_20_north`.** On the 20° N coast, land cells from 192 to 2000 blocks inland: (a) the pressure wind's onshore part (its $v$) is positive on day 197 and negative on day 15; (b) at the cell nearest 704 blocks inland on day 197 it is $0.43 \pm 0.05$ blocks a second; (c) the stored wind's onshore part on day 197 is larger than the belt wind's alone by exactly the pressure wind's (after steering, which is off on flat land). It **prints** the summer and winter total onshore parts at 704 blocks (expect about $-1.80$ and $+0.46$) and does not fail on them, because with the master plan's constants the summer total stays offshore. See Issues found: when Ahmed decides, (d) "the summer total is onshore" is added as a hard check.
- **`stored_pressure_reproduces_the_twelve_days`.** For every cell of a real-seed tile, the stored $\Delta p$ wave evaluated at each $d_k$ equals the $\Delta p_k$ the build worked out, $\pm 10^{-4}$ hPa (it is a pure wave). For wind, prints the largest miss over the tile.
- **`air_is_read_as_the_cells_blend`.** With the warp off (test hook): `climate_air_at` at a point between four cells equals the bilinear blend of their coefficients evaluated at the day, and `pressure_belt_hpa` is the spline at that exact $y$'s $\psi$.
- **`one_and_sixteen_workers_give_the_same_bytes`** (in `run_climate_cell`, phase 03's test, re-pinned): 8 by 8 tiles around 0, 1 worker against 16, `memcmp` of every cell; the new digest pinned with the comment "phase 04 fills currents, swing, lag, wind and pressure". Fails if any sum's order depends on the worker.
- **`a_tile_s_cells_are_the_same_whichever_neighbours_were_worked_out_first`** (phase 03's test): still passes, now covering the new fields.

### `run_climate_layer` (added)

- **`current_arrows_are_red_blue_and_grey_and_point_along_the_flow`.** On the 30° S continent at 8 blocks a pixel: an arrow off the west coast is blue and its head is north of its tail; off the east coast red, head south.
- **`pressure_lines_fall_on_multiples_of_four`.** On a synthetic field with pressure rising linearly in $y$: lines at the rows where it crosses 1008, 1012, 1016, 1020.
- **`wind_arrows_point_downwind`.** On open sea at 15° N: arrows point west-south-west.
- **`each_new_layer_is_held_to_a_hash`**, set after Ahmed has looked.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_terrain_density$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_climate_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^run_climate_.*$' --output-on-failure
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate --pictures /tmp/mc_c_climate 2198175988018515243
    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate --synthetic /tmp/mc_c_climate_synthetic
    pgrep -a minecraft_c    # must print nothing

The release run must print the same cell digest as debug. `bench_climate` exits 1 if a cold tile is over 100 ms on one worker, as in phase 03; the README records the before and after.

## Gate

Ahmed looks at, for seed `2198175988018515243` and two others, 16 km by 16 km around spawn:

- the **currents** layer: red on the east coasts and blue on the west coasts between about 15° and 45°, the reverse further toward the poles, arrows running along coasts;
- the **pressure** layer on days 15, 105, 196 and 288: the belts, and lows over big land in summer and highs in winter;
- the **wind** layer on the same days: trade winds from the east in the tropics, westerlies further out, calm bands near the equator and 30°, faster winds on peaks;

and the two synthetic continents (`--synthetic`), which show the textbook pattern without the noise of real coasts. If `bench/world_map/` (003 phase 8 stage 1) exists by then, the layers are added to it and drawn there; it did not exist on 2026-11-04 (`ls bench/world_map` fails), so `bench_climate --pictures` draws them with the same `climate_layer_draw`, as phase 03 set out. Ahmed also answers the monsoon question in Issues found. The layer hashes are set only after he says yes.

## Risks and open questions

| Risk or question | Default |
|---|---|
| **Monsoons at 20° N don't come out of the master plan's numbers** (Background, and Issues found 1) | Build as written; the test checks what holds and prints the rest; Ahmed chooses at the gate |
| One yearly wave smooths a wind that switches belts (at 20°: true $u$ from $-5.6$ to $+3.1$, fitted $-7.0$ to $-0.1$) | As the master plan chose. The test prints the largest miss. If phase 05's rain or monsoon flag suffers, store a second wave in the 12 spare bytes left in `climate_cell_t` (two more for $u$ and $v$ need 16 bytes, so that would mean dropping `land_direction` to recompute it, which Ahmed decides) |
| Current kinds change in a step at a band line | Kept, as the table says. Phase 05's temperature blends it |
| Pressure wind on coasts depends on the steepest part of $\kappa$, near the coast, so it's strongest within about a km of the sea | As the formulas give. The pictures show it |
| `t_swing` and `t_lag_days` are 6.6's (phase 05's) terms, worked out here first | Phase 05 reads them and must not compute them a second way |

## Results, 2026-11-06

Everything below was run in this worktree through `tools/test-or-build.sh`.

| Check | Result |
|---|---|
| `run_terrain_density` after the wrapper | Passed: its pin holds |
| `run_climate_cell` after the window refactor, before the air call | Passed with the old pin `0x61708389BBF3789B`: no number moved |
| `run_climate_cell` with the air | Digest `0x4950918E191FF004`, re-pinned with the reason; 1 worker and 16 the same bytes; the release build prints the same digest |
| `run_climate_.*`, debug and release | 9 of 9 pass in each |
| `standards_lint`, `clang_format` | Pass |
| Full debug suite | 133 of 133 pass (1764 s) |
| Release build (`-O3 -flto`, main's frame limit) | Builds and links clean; `app_t` did not change (`climate_t` stays 64 bytes) |
| `bench_climate` (release) | Cold tile, 1 worker: 26.7 to 27.6 ms over 9 places (target 100; phase 03 recorded 28.8 to 30.7); warm tile 5.34 ms; spawn precompute 24.3 ms on 16 workers. Exit 0. The air costs too little to see next to the ground function (90.6% of a tile) |
| `bench_climate --pictures /tmp/mc_c_climate 2198175988018515243` | Phase 03's four and `currents.png`, `pressure_day{015,105,196,288}.png`, `wind_day{015,105,196,288}.png`. Exit 0 |
| `bench_climate --synthetic /tmp/mc_c_climate_synthetic` | `south30_*` and `north50_*`: currents, and pressure and wind on the four days. Exit 0. Looked at by the agent: blue arrows north along the 30° S west coast, red south along its east coast, grey crosses on its neutral north and south coasts; a heat low over the 50° N continent on day 196 |
| `pgrep -a minecraft_c`, `pgrep -af $PWD/build` | Nothing |

Numbers the tests print, for Ahmed:

- Open sea, stored yearly mean `u0`: -4.37 at 1°, -4.29 at 10°, -3.64 at 20°, -0.99 at 25° (the plan expected about -4.4, -4.3, -3.6, -1.0).
- Largest change of the belt wind between 0.01° steps of ψ: 0.0314 blocks a second (limit 0.1).
- Spin islands: every land cell 1 to 3.5 km from the centre turns the right way: 8634 of 8634 at 45° N and at 45° S, 8626 of 8626 on the mirrored island, in summer and in winter.
- Steering: at most 3.8e-6 blocks a second added over 1,000,000 random winds (rounding); on the ridge 1920 of 6144 cell days slowed, none faster.
- 20° N coast: the pressure wind's onshore part 672 and 736 blocks inland on day 197: 0.464 and 0.469 (the plan's 0.43 ± 0.05); day 15: -0.464 and -0.469. The stored total onshore part at 672 blocks: **summer -2.54, winter -0.04** (the plan estimated -1.80 and +0.46). See Issues found 10.
- Real tile (0, 0) of seed `0x1234567890ABCDEF`: the heat-low wave reproduces its 12 days to 6e-8 hPa; the wind wave's largest miss on a fit day is **4.98 blocks a second** (one wave smoothing a belt change, as the Risks table says).
- Layer hashes on seed 2198175988018515243, 256 by 256 at 16 blocks a pixel round (0, 0), the same in debug and release, printed and not pinned until Ahmed has looked: currents `0xAC3FAB076FA5F7B6`; pressure on days 15, 105, 196, 288: `0x7DA7F01C0FDEF985`, `0xC57981BF50840875`, `0x0578CFB18C1C37EE`, `0xE723D99B428F8176`; wind: `0x5E90DAB5164B3792`, `0x69E2EB2713DCD9ED`, `0x09EE2604946E84B7`, `0x424676382ABD6462`.

**Second pass, 2026-11-06** (a fresh agent, branch `worktree-wf_6f8a0c68-47a-21`, merged main and the first pass with no change to the code; re-ran the Verification section through `tools/test-or-build.sh`):

| Check | Result |
|---|---|
| Debug build; `run_terrain_density`, `run_climate_*`, `standards_lint`, `clang_format` | 12 of 12 pass |
| Full debug suite | 133 of 133 pass (1896 s) |
| Release build (`-O3 -flto`, frame limit) | Builds and links clean |
| `run_climate_*`, release | 9 of 9 pass; cell digest `0x4950918E191FF004`, the same as debug |
| `bench_climate` (release) | Exit 0; slowest cold tile 28.63 ms (target 100), warm 5.53 ms, spawn precompute 23.5 ms |
| `--pictures` seed 2198175988018515243, `--synthetic` | Exit 0; all PNGs written. The agent looked at `north50_currents.png`: red arrows north on the west coast, blue south on the east |
| Gate's two other seeds | `--pictures /tmp/mc_c_climate_seed2 1` and `--pictures /tmp/mc_c_climate_seed3 1311768467294899695`, both exit 0, ready for Ahmed |

**Wiring outside the owned paths:** none beyond the plan's list. `run/{src,include}/terrain/density/` gained only `terrain_density_spline_build` and its README lines; `tests/run/climate/cell/README.md`, `tests/run/climate/layer/README.md` and `bench/climate/README.md` describe the new pin, tests and pictures.

## Commits

Appended as they land:

- `70d3420` Add the climate's knot tables and the 12-day yearly-wave fit for plan 004 phase 04, make the terrain's spline builder public for them, and hand the tile window out of the cell build as one struct with no number moved.
- `48afeff` Add plan 004 phase 04's currents, heat lows and winds to every climate cell, read them with climate_air_at, draw the currents, pressure and wind layers, and test them on synthetic continents, islands, a coast and a ridge.
- `e59520e` Describe plan 004 phase 04's currents, pressure and winds in the climate's READMEs, its tests' and the climate bench's.
- `55c8148` List the climate's normals, harmonic and air parts of plan 004 phase 04 and their tests in the repository structure.
- `97db1df` Record plan 004 phase 04 in its phase file: todos, results, the bench and the issues found.
- `da455ec` List the phase file commit of plan 004 phase 04 in its phase file.
- `4cd0c0f` Record the second verification pass of plan 004 phase 04 in its phase file.

## Issues found

Found while writing this file, from the master plan's own numbers. None of these changes anything that exists. Each is for Ahmed to decide; until then the defaults below are built.

1. **The master plan's monsoon example does not hold with its own formulas.**
   - **Expected** (master plan 6.5, worked example): a heat low 3000 blocks inland at 20° N gives a 2 blocks-a-second wind "blowing in from the sea", enough to turn the trade wind's 2.4 offshore into an onshore summer wind. Phase 04's todo has the test "a heat low makes an onshore summer wind on a synthetic coast at 20°".
   - **Actual,** worked out in Background, "A monsoon": at 20° the heat low is about $-3.1$ hPa, not $-6.5$ (that number was for $A = 12$, a 45° interior); the steepest gradient is about 0.0014 hPa per block, so $\lvert\vec w_p\rvert \approx 1.26$; and the 70° turn leaves only $\cos 70° = 34\%$ of it onshore, 0.43. The example used the whole wind as onshore. The summer total is about $(-4.39, -1.80)$, still offshore. In winter the belt is already in the horse latitudes ($\psi = 31.7°$) and the total is about $(1.89, 0.46)$, onshore: the reverse of a monsoon.
   - **Evidence:** master plan sections 6.4, 6.5 and 6.6; the arithmetic above, step by step.
   - **Consequence:** phase 05's monsoon flag ("the season's wind blows from the sea while it blows from the land at the opposite time of year") will rarely be set, and hurricane-free monsoon seasons, which Ahmed asked for ("areas need hurricane and monsoon season please"), will be rare.
   - **What's done:** the formulas are built exactly as written. The test checks the parts that hold (the pressure wind pushes onshore in summer and offshore in winter, by the computed amount) and prints the totals. **Options for Ahmed:** (a) let the wind belts follow the sun all the way, $\psi = \varphi - \delta(d - 30)$, so the equatorial calm and the southern trades reach 20° N in summer; (b) a smaller turn for heat-low winds, as real surface winds over land are turned 30° to 45° by friction, not 70°; (c) a larger $k_p$; (d) leave it. Default: (d), until he decides at this phase's gate.

2. **"Unit vectors" that aren't.** Master plan 6.5 calls the belt directions "unit vectors $(u, v)$" but gives $(-1, -0.4)$, length 1.077, and its monsoon example uses $6 \times 0.4 = 2.4$ for the trade's north-south part, as if not normalised. **Default:** normalised, as the text says, so the speed column is the speed: the trade's north-south part is 2.228, not 2.4. Ahmed may prefer the raw vectors, which make winds 4% to 8% faster than their stated speed.

3. **Straight-line numbers in the worked examples, splines in the code.** Master plan 6.6 says knots are joined "using the same spline code `terrain/density` already has", but its worked examples interpolate in straight lines: $A_{\text{sea}}(15°) = 2$ (spline: 1.95), $A_{\text{sea}}(45°) = 4.5$ (4.37), $A_{\text{land}}(45°) = 13.5$ (13.79), $\bar T_{\text{lat}}(28°) = 20.8$ (20.99). So 6.8's sea temperatures 32, 28 and 24 come out 31.95, 28.05 and 24.05. **Default:** the spline, as 6.6 says; this phase's tests use the spline's values; phase 05's "within 10%" tests are not affected.

4. **The master plan's tables are written for one hemisphere strip, and its latitude repeats as a mirror.** $\varphi(y)$ (5.1) rises to the pole at $y = 38\,298$ and falls after it, so the strip from the pole to the next equator is a mirror image in $y$. Read literally, "$v$ north" with $-0.4\,\operatorname{sgn}\psi$ would make the trades in a mirrored strip blow toward the pole, and "70° clockwise in the north" would make its lows spin the other way from a mirror image, with a jump at the pole. **Default:** apply every table in the strip's own sense, through $e(y) = \operatorname{sgn}\cos(\pi y/2Y)$: north-south parts and the turn's sense flip in mirrored strips, and "toward the pole" and "toward the equator" mean what they say. West and east coasts don't change, since $x$ is never mirrored. Tested with the mirrored continent and island.

Found while building, 2026-11-06:

5. **The normals have their own arena, not `arena_tables`.**
   - **Expected:** the curves built once in `climate_init` into the climate's `arena_tables`.
   - **Actual:** `climate_open_ground` resets `arena_tables` for every world (`run/src/climate/index.c`), so curves built at init would be wiped by the first open.
   - **What was done:** `CLIMATE_STATE_BYTES` grew from 4096 to 8192; a new `arena_normals` (4 KiB at `CLIMATE_NORMALS_OFFSET`, Process lifetime) holds them, built once in `climate_init`. `climate_state_t` grew from 384 to 448 bytes and keeps `borrowed_normals` in old padding. `climate_t` in `app_t` is unchanged. There is room for 13 curves; phase 05 adds its tables there.
6. **A land cell's flow direction uses `-sea_direction`.**
   - **Expected** (Design, "Flow direction"): a land cell's `n` is its nearest sea cell's.
   - **Actual:** that vector is not stored in a land cell, and `climate_current_flow` takes only the cell.
   - **What was done:** a land cell's coast is taken as the way back from its sea, `-sea_direction`. On a straight coast the two are the same (tested: land behind the 30° S west coast flows north); at a corner they can differ by the corner's angle. The kind and strength are exact, from the nearest sea cell in the window. If a later plan needs the exact vector on land, 8 of the cell's 12 spare bytes would hold it.
7. **600 blocks offshore can be past a band line.**
   - **Expected:** on the 50° N and 70° N continents, every sea cell within 600 blocks with `n_x > 0.5` has the band's kind.
   - **Actual:** those continents reach 45.3° and 65.3°, so sea cells 600 blocks south of them sit at 43.9° and 63.9°, in the next band, and take its kind, as the master plan's table (by the sea cell's own latitude) says.
   - **What was done:** the continent tests count only sea cells whose own latitude is in the band (`air_coasts` in `tests/run/climate/air/index.c`). Every counted cell passes, with at least 30 per coast side.
8. **`climate_air_build_tile` takes the normals, and the Owner calls it.**
   - **Expected:** `climate_air_build_tile(const climate_t *, ...)`, called at the end of `climate_cell_build_tile`.
   - **Actual:** the cell part knows nothing of the normals.
   - **What was done:** it takes `const climate_normals_t *` and an optional `climate_air_trace_t *` (the tests' hook, with `steer_off`). The Owner's tile job calls it right after `climate_cell_build_tile`, on the window that function hands out. The tile job is the only caller of both, so the effect is the same, and tests can build a tile straight from a window.
9. **The spin test uses fit days.** The trace holds only the 12 fit days, so the spin islands use day 212.9 (k = 7) for the northern summer and day 0 for the southern: the fit days nearest the plan's 197 and 15, inside each heat low's peak (170 + ℓ, ℓ from 25 to 45). The 20° N coast test asks days 197 and 15 exactly, working the pressure wind out from the stored pressure waves of each cell's neighbours, which are pure waves.
10. **The monsoon totals are further offshore than the plan's estimate.**
    - **Expected** (Background, "A monsoon"): a total onshore part about 704 blocks inland of -1.80 in summer and +0.46 in winter.
    - **Actual:** -2.54 and -0.04 from the stored waves, while the pressure wind itself (0.464) matches the plan's 0.43. The difference is the one-wave fit: at 20° N the true belt wind steps between the trades and the horse latitudes through the year, and one wave smooths the winter's onshore spell away (the wind's largest miss on a real tile is 4.98 blocks a second); and altitude multiplies by 1.025 on land 20 blocks up.
    - **What was done:** printed, not checked, as the plan says. It strengthens Issue 1: with the master plan's numbers and one wave, the 20° N coast gets no onshore season at all. Ahmed's options in Issue 1 stand; a second wave for wind (Risks table) would bring the winter onshore spell back.
11. **Neutral currents carry strength 0, and the layer marks them with a grey cross.** NONE has no flow, so the plan's grey arrow could not point anywhere. A sea cell within 600 blocks of land whose current is neutral gets a 3-pixel grey cross instead. A neutral cell's `current_strength` is 0, so its `current_c` is 0 either way.
