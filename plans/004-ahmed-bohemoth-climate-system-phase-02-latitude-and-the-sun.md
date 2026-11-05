# 004-ahmed-bohemoth-climate-system — phase 2: Latitude and the sun

**Status:** not started   (updated 2026-11-04)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then section 5 of the master plan, `plans/004-ahmed-bohemoth-climate-system.md`. This phase needs phase 01 done: it reads phase 01's world clock (`game_t.clock_units`, `game_t.day_zero`) and calendar (`run/src/calendar/`).

## Goal

Every place in the world has a latitude, worked out from its y alone: 0° at y = 0, 23.5° north at y = 10,000, 90° north at y = 38,298, back to 0° at y = 76,596, and 90° south at y = −38,298. Every day of the year has a solar declination, from 23.5° north at midsummer to 23.5° south at midwinter. From the two, the game knows the length of the day and where the sun is in the sky, at any place and moment. The sun drawn on screen follows the eye's latitude and the season: it rises and sets in the right directions, is high in summer and low in winter, never sets past the polar circle at midsummer and never rises there at midwinter. Beds work and hostile mobs spawn by how far the sun really is below the horizon at the player's place, so nights are long in winter and near the poles, and in polar winter mobs can spawn at noon. The F3 overlay shows the latitude and the sun's height, and the HUD's season label says "Polar night" or "Midnight sun" where that is true. On the equator at the spring equinox, beds and mobs behave exactly as they do today.

## Where this fits

The vision file says the climate system exists so that every later layer can read it, and that the stack is worked out strictly top to bottom. Latitude is the first climate number and the one everything else in plan 004 hangs from. Master plan section 3 puts it directly under the terrain:

```
seed
 └─ terrain → height estimate h(x, y)              phase 03
     └─ latitude φ(y), sun δ(day)                   THIS PHASE
         └─ land mask, sea distance ...             phase 03
             └─ currents, pressure belts ...        phase 04
```

What reads this phase's functions later:

| Reader | What it takes |
|---|---|
| Phase 03 | `lat_deg` in every `climate_cell_t`, at the cell's centre |
| Phase 04 | Ocean currents by latitude band (6.3); pressure and wind belts by wind latitude $\psi = \varphi - 0.5\,\delta(d - 30)$ (6.4, 6.5) |
| Phase 05 | Temperature knots $\bar T_{\text{lat}}(\varphi)$ and swings $A(\varphi)$ (6.6); hurricanes only between 5° and 25° (6.8) |
| Phase 06 | Storm spawn regions and tracks steered by latitude alone (7.3) |
| Phase 09 | The Atlas's latitude lines: equator, tropics, polar circles |
| Vegetation plan | Day length and the sun's height, for what can grow where |
| Economy and settlements plans | Long winter nights and short summer ones shape work, harvests and where people choose to live; the climate they read is built on this latitude |

The vision's founding rule is "the land decides; nothing is hand-placed". Latitude is the most basic way the land differs from place to place: walk north and the world gets colder, the seasons get stronger and the nights longer, from the seed alone, with no per-map script.

**This phase must NOT build:**

- Any temperature, wind, rain, current or pressure (phases 04 and 05). Latitude is not yet used to change how cold anything is.
- Any change to what the terrain generates or to which biome is where. Biomes stay as they are (master plan 2.6). Snow does not move north.
- The tropics' wet and dry season labels (phase 05), and hurricane or monsoon labels (phase 05).
- A moon of its own. The moon stays opposite the sun, as it is today.
- The climate tile cache, cells or anything stored (phase 03). Everything here is a pure function.

## Ahmed's words

- "an equator system assuming spawn near x,y 0,y that is the equator ... every 10km is on 23.5 degrees"
- Decision, 2026-11-04: "**The tropic line (23.5°) is 10 km from the equator.** A pole is 38.3 km from the equator"
- Decision: "Positive y is north, positive x is east. (10, 10) is in the north-east quadrant"
- Decision: "Day length and the sun's path follow latitude and season. The sun moves **±23.5°** through the year. Polar night and midnight sun. Mobs spawn by real darkness"
- Master plan 5.2: "The renderer uses the player's own latitude for the whole view. The sun's compass direction comes from the same angles, and is in phase 02's file."
- Master plan 5.2: "nights are long in winter and toward the poles. Beds work at night and mobs spawn in darkness, so both follow automatically. In polar winter, hostile mobs can spawn all day."
- Master plan Q3 (default yes): "38 km equator to pole".

## Background, explained

### Degrees, radians and the trigonometric functions

An angle can be measured in **degrees** (a full turn is 360°) or **radians** (a full turn is $2\pi \approx 6.2832$). To convert: radians $=$ degrees $\times \pi / 180$. So $90° = \pi/2 = 1.5708$ rad, and $23.5° = 0.41015$ rad. The C library's `sin`, `cos`, `tan`, `asin`, `acos` and `atan2` all work in radians.

- $\sin\theta$ and $\cos\theta$: for a point on a circle of radius 1 at angle $\theta$ from the x axis, $\cos\theta$ is its x and $\sin\theta$ its y. $\sin 30° = 0.5$, $\cos 60° = 0.5$, $\sin 90° = 1$.
- $\tan\theta = \sin\theta / \cos\theta$. $\tan 45° = 1$, $\tan 60° = 1.732$, $\tan 23.5° = 0.4348$.
- $\arcsin$ (`asin`) and $\arccos$ (`acos`) are the inverses: $\arcsin(0.5) = 30°$. `asin` gives an angle from $-90°$ to $90°$, `acos` from $0°$ to $180°$.
- `atan2(a, b)` gives the angle whose sine is proportional to $a$ and cosine to $b$, from $-180°$ to $180°$, so it knows which quarter of the circle a direction is in. `atan2(1, 1)` $= 45°$, `atan2(1, -1)` $= 135°$.

### A unit vector

A **vector** here is three numbers $(x, y, z)$ giving a direction in the world: x east, y north, z up (`run/include/render/frame/index.h:51`). A **unit vector** has length 1: $x^2 + y^2 + z^2 = 1$. "Toward the sun" is a unit vector, and the renderer takes exactly that.

The **dot product** of two vectors is $a \cdot b = a_x b_x + a_y b_y + a_z b_z$. For unit vectors it is the cosine of the angle between them: 1 if they point the same way, 0 at right angles.

### Latitude as a triangle wave

On a real planet, latitude is the angle north or south of the equator. Here the world is flat and infinite, so latitude is a rule from y. Ahmed's rule: 23.5° at y = 10,000, rising evenly. So a pole (90°) is at

$$Y = 10\,000 \times \frac{90}{23.5} = 38\,297.87 \text{ blocks}$$

Past the pole, the latitude must come back down rather than go past 90°, so it's a **triangle wave**: up evenly from the equator to the north pole, down evenly through the equator to the south pole, and up again. The master plan writes it with one formula:

$$\varphi(y) = \frac{180°}{\pi}\,\arcsin\!\left(\sin\frac{\pi y}{2Y}\right)$$

Why this is a triangle: $\sin$ of an angle that grows evenly goes smoothly up and down, and $\arcsin$ undoes it exactly while the angle is between $-90°$ and $90°$, giving back the even growth. Past $90°$, $\arcsin$ can only answer in $-90°$ to $90°$, so it folds the angle back down. The result goes up and down in straight lines.

The pattern repeats every $4Y = 153\,191.49$ blocks:

| Line | φ | y |
|---|---|---|
| Equator | 0° | 0, 76,596, 153,191 |
| Tropic of the north | 23.5° N | 10,000 |
| Northern polar circle | 66.5° N | 28,298 |
| North pole | 90° N | 38,298 |
| South pole | 90° S | −38,298, 114,894 |

**Worked example 1** (the master plan's). $y = 19\,149$: $\pi \times 19\,149 / 76\,596 = \pi/4$. $\sin(\pi/4) = 0.7071$, and $\arcsin(0.7071) = \pi/4$, which is $45°$. So $\varphi = 45°$ N. At $y = -19\,149$, $\varphi = 45°$ S.

**Worked example 2.** $y = 30\,000$: $30\,000 / 38\,297.87 \times 90° = 70.5°$ N. Still on the way up, so no folding.

**Worked example 3.** $y = 100\,000$ is past the equator at 76,596, on the way down to the south pole: $\pi \times 100\,000 / 76\,596 = 4.1015$ rad $= 235°$. $\sin 235° = -0.8192$, and $\arcsin(-0.8192) = -55°$. So $\varphi = 55°$ S.

North is positive and south negative. x does not matter: the world is infinite east and west, and every x at one y has one latitude.

**Precision.** y can be up to 30 million (the world's edge). $\sin$ of a very large angle loses digits, so the code first brings y into one period: $y' = y - 4Y \lfloor (y + 2Y)/4Y \rfloor$, which lies in $[-2Y, 2Y)$, then uses the formula. In `float64_t` this is exact to far better than 0.001°. Since $4Y$ is not a whole number, $\varphi(y + 153\,191)$ differs from $\varphi(y)$ by about 0.001°, which nobody can see (Issues found 2).

### Declination: where the sun is overhead

A planet's axis is tilted, so through the year the sun is straight overhead at noon at different latitudes. That latitude is the **declination** $\delta$. Ahmed's decision: it moves between 23.5° north and 23.5° south.

$$\delta(d) = 23.5° \times \sin\!\left(\frac{2\pi\,(d - 79)}{365}\right)$$

$d$ is the day of the year, 0 to 364. The sine is 0 on day 79 (the spring equinox), 1 a quarter of a year later (day 170.25, midsummer in the north), 0 again on day 261.5 (the autumn equinox), and −1 on day 352.75 (midwinter).

| Day $d$ | Shown as | δ |
|---|---|---|
| 79 | Day 80 | 0.0° |
| 170 | Day 171 | 23.4998° |
| 262 | Day 263 | −0.20° |
| 353 | Day 354 | −23.4998° |

So that the sun moves smoothly and doesn't jump a little every 06:00, the code uses a **continuous** day of the year: the calendar day plus the fraction of it that has passed. With phase 01's clock $c$ (in days) and $D_0 = 79$:

$$d_c = (D_0 + c - 0.25) \bmod 365$$

A new world at 08:24 on its first day has $c = 0.35$, so $d_c = 79.10$ and $\delta = 0.04°$.

### The hour angle

The **hour angle** $H$ is how far the sun has turned from noon, as an angle: a day is a full turn of $2\pi$, so each hour is $15°$.

$$H = \frac{2\pi\,(t - 12)}{24}, \qquad t = 24 f \text{ hours on the clock}$$

At noon $H = 0$. At 06:00 $H = -90°$, at 18:00 $H = +90°$, at midnight $\pm 180°$. Negative is morning.

### The direction to the sun, derived

Stand at latitude $\varphi$, and use the game's own axes: east $\hat x$, north $\hat y$, up $\hat z$.

1. The planet's axis points at the celestial north pole. Seen from latitude $\varphi$, that point is due north, $\varphi$ above the horizon:
$$\vec P = (0,\ \cos\varphi,\ \sin\varphi)$$
2. The sun is $\delta$ away from the celestial equator, toward $\vec P$. So $\vec s \cdot \vec P = \sin\delta$.
3. Two more unit vectors finish a set of three at right angles to each other and to $\vec P$: east, $\vec E = (1, 0, 0)$, and the one in the north-up plane at right angles to $\vec P$, pointing up and toward the equator, $\vec M = (0,\ -\sin\varphi,\ \cos\varphi)$. Check: $\vec P \cdot \vec M = -\cos\varphi\sin\varphi + \sin\varphi\cos\varphi = 0$.
4. The sun goes round $\vec P$ once a day. At noon ($H = 0$) it's toward $\vec M$, and in the morning ($H < 0$) toward the east:
$$\vec s = \sin\delta\,\vec P + \cos\delta\,\big(\cos H\,\vec M - \sin H\,\vec E\big)$$
5. Written out, component by component:

$$\boxed{\;s_x = -\cos\delta\,\sin H, \qquad s_y = \sin\delta\cos\varphi - \cos\delta\cos H\sin\varphi, \qquad s_z = \sin\delta\sin\varphi + \cos\delta\cos H\cos\varphi\;}$$

$s_z$ is the master plan's formula for the sun's height: $\sin e = s_z$, where $e$ is the **elevation**, the angle above the horizon. It's a unit vector, since $\vec P$, $\vec M$ and $\vec E$ are, and are at right angles.

**The compass direction (azimuth)**, measured clockwise from north, so 90° is east, 180° south and 270° west:

$$A = \operatorname{atan2}(s_x,\ s_y) \quad \text{brought into } [0°, 360°)$$

$$e = \arcsin(s_z)$$

**Worked example 4: 45° N at the equinox, 09:00.** $\delta = 0$, $H = -45°$. $s_x = -\sin(-45°) = 0.7071$. $s_y = -\cos(-45°)\sin 45° = -0.5$. $s_z = \cos(-45°)\cos 45° = 0.5$. So $e = \arcsin(0.5) = 30°$ above the horizon, and $A = \operatorname{atan2}(0.7071, -0.5) = 125.3°$: south-east, as a morning sun should be.

**Worked example 5: 60° N, midsummer noon and midnight.** $\delta = 23.5°$. At noon $H = 0$, so $s_z = \sin\delta\sin\varphi + \cos\delta\cos\varphi = \cos(\varphi - \delta)$, and $e = 90° - (\varphi - \delta) = 90° - 36.5° = 53.5°$, due south. At midnight $H = 180°$, so $s_z = \sin\delta\sin\varphi - \cos\delta\cos\varphi = -\cos(\varphi + \delta)$, and $e = \varphi + \delta - 90° = -6.5°$: the sun is only 6.5° below the northern horizon.

**Worked example 6: 70° N at midsummer midnight.** $e = 70° + 23.5° - 90° = +3.5°$: the sun is up at midnight, due north. That's the **midnight sun**.

**Worked example 7: 80° N at midwinter noon.** $\delta = -23.5°$: $e = 90° - 80° - 23.5° = -13.5°$. The sun never comes up. That's **polar night**.

**The south.** With $\varphi < 0$, at noon $s_y = \sin(\delta - \varphi) > 0$: the sun is in the north, as it should be.

**Today's fixed sun is a special case.** `render_day_fill` (`run/src/render/frame/index.c:327-334`) uses $\vec s = (\cos a,\ -h\sin T,\ h\cos T)$ with $a = 2\pi(f - 0.25)$, $h = \sin a$ and $T = 0.35$ rad. Since $\cos a = -\sin H$ and $\sin a = \cos H$, that is exactly the boxed formula with $\delta = 0$ and $\varphi = 0.35$ rad $= 20.05°$: a world that was always at 20° N on the equinox. This phase keeps that function exactly as it is, for the tests and pictures that call it, and adds the general one beside it.

### Day length and the sunrise hour angle

Sunrise and sunset are where $s_z = 0$:

$$\sin\delta\sin\varphi + \cos\delta\cos\varphi\cos h_0 = 0 \quad\Longrightarrow\quad \cos h_0 = -\tan\varphi\,\tan\delta$$

$h_0$ is the hour angle of sunset ($-h_0$ is sunrise). The sun is up from $-h_0$ to $+h_0$, a turn of $2h_0$ out of $2\pi$:

$$\text{day length} = 24 \times \frac{2h_0}{2\pi} = 24\,\frac{h_0}{\pi} \text{ hours}$$

If $-\tan\varphi\tan\delta$ is below $-1$ the sun never sets ($h_0 = \pi$, 24 hours), and above $+1$ it never rises ($h_0 = 0$, 0 hours). The master plan clamps it to $[-1, 1]$.

**Worked example 8: 60° N** (the master plan's). Midsummer, $\delta = 23.5°$: $-\tan 60° \times \tan 23.5° = -1.7321 \times 0.4348 = -0.7531$. $h_0 = \arccos(-0.7531) = 2.4236$ rad. Day length $= 24 \times 2.4236 / \pi = 18.51$ hours. Midwinter, $\delta = -23.5°$: $+0.7531$, $h_0 = 0.7180$ rad, $5.49$ hours. The two add to exactly 24, as they must (Issues found 1: the master plan rounds the first to 18.6).

**Worked example 9: the equinox.** $\delta = 0$, so $\tan\delta = 0$, $\cos h_0 = 0$, $h_0 = \pi/2$: 12 hours, everywhere.

**Worked example 10: the polar circle.** At $\varphi = 66.5°$ and $\delta = 23.5°$: $\tan 66.5° \times \tan 23.5° = 2.2998 \times 0.4348 = 1.0000$. So the polar circle is exactly where midsummer has 24 hours of sun. North of it, the clamp gives 24 hours at midsummer and 0 at midwinter.

**At the poles exactly**, $\tan 90°$ is infinite. The code divides instead: $\cos h_0 = -\sin\varphi\sin\delta / (\cos\varphi\cos\delta)$, and when $\cos\varphi$ is below $10^{-12}$ it answers 24 hours if $\varphi$ and $\delta$ have the same sign, 0 if opposite, and 12 if $\delta = 0$.

### How dark is dark: beds and mobs today

Two rules in the game decide "is it night?", both from the time of day $f$ alone:

**Beds** (`run/src/player/survival/index.c:80-83`, constants at `run/include/player/survival/index.h:40-41`). A bed works when $f \ge 0.772583$ or $f < 0.227458$. Both are where $\cos H = \cos(2\pi(f - 0.5)) = -0.141418$, so the rule today is:

$$\text{night for beds} \iff \cos H < -0.141418$$

**Mobs** (`run/src/mob/spawn/index.c:24-29`). How much the sky's light drops:

$$\text{darken} = \left\lfloor 11 \times \big(1 - \operatorname{clamp}(2\cos H + 0.5,\ 0,\ 1)\big)\right\rfloor$$

A place open to the sky has sky light 15. A mob spawns if $15 - \text{darken}$ is at most a roll from 0 to the mob's max light (7 for a zombie: `tests/run/mob/spawn/index.c:331`). So on open ground mobs need darken at least 8.

**On the equator at the equinox**, $\varphi = 0$ and $\delta = 0$, so $s_z = \cos H$. The quantity both rules already use **is** the sine of the sun's elevation there. So the change is simple: give both rules $s_z$ at the player's own place in place of $\cos H$. On the equator at the equinox nothing changes, to the bit. Everywhere else they follow the real sun:

| Rule | Threshold on $s_z$ | As an elevation |
|---|---|---|
| A bed works | $s_z < -0.141418$ | Sun more than 8.13° below the horizon |
| Open-ground mobs (darken ≥ 8) | $2 s_z + 0.5 \le 3/11$, so $s_z \le -0.1136$ | Sun more than 6.5° below the horizon |
| Full darkness (darken 11) | $s_z \le -0.25$ | Sun more than 14.5° below |

**Worked example 11: 60° N, midsummer midnight.** The sun is at $-6.5°$ (example 5): $s_z = -0.1132$. Bed: $-0.1132 > -0.1414$, so **no bed**. Mobs: $2 \times (-0.1132) + 0.5 = 0.274$, darken $= \lfloor 11 \times 0.726 \rfloor = 7$, open sky light 8, above any roll: **no mobs on open ground**. A white night.

**Worked example 12: 80° N, midwinter noon.** $e = -13.5°$, $s_z = -0.2334$. Bed: works. Mobs: $2 \times (-0.2334) + 0.5 = 0.033$, darken $= \lfloor 11 \times 0.967 \rfloor = 10$, open sky light 5: a zombie spawns on rolls 5, 6 and 7. **Mobs at noon.**

North of $90° - 23.5° - 8.13° = 58.4°$, midsummer has no night a bed counts.

### Pure functions, and which thread computes the sun

A **pure function** is one whose answer depends only on what it's given, and which changes nothing. `climate_latitude_deg(y)` gives the same answer on any thread, any number of times. So no thread has to own the sun: each thread that needs it works it out from two numbers it can already read, phase 01's 64-bit clock `clock_units` (one atomic load) and a y it already has.

| Thread | Needs | From |
|---|---|---|
| `gpu` (or whoever draws) | The sun's direction for the picture | `clock_units` and the y of the camera that picture is drawn from |
| `sim` | $s_z$ for beds and mobs | `clock_units` and the y of the player's feet |
| `main` | Latitude and elevation for F3; polar label for the HUD | `clock_units` and the camera's y |

The libm functions are deterministic on one machine, and the repo bans `-ffast-math` (`plans/CODING_STANDARD.md` section 6), so the same clock and y give the same bits on every thread.

## Design

### File layout

```
run/src/climate/             NEW  Owner by depth; reserves nothing yet (phase 03 adds the tile cache)
├── README.md
└── index.c                  latitude
run/include/climate/
└── index.h                  NEW
run/src/climate/sun/         NEW  depth 2, allocates nothing
├── README.md
└── index.c                  declination, hour angle, day length, elevation, azimuth, direction
run/include/climate/sun/
└── index.h                  NEW
tests/run/climate/           NEW  ctest run_climate
├── README.md
└── index.c
tests/run/climate/sun/       NEW  ctest run_climate_sun
├── README.md
└── index.c
tests/run/game/sun/          NEW  ctest run_game_sun, on the card
├── README.md
└── index.c
```

The master plan names `run/{src,include}/climate/` for all of latitude, sun, sea distance, currents, belts, wind, temperature and rain. This phase starts it. `climate` is a new `run/` system, so `compile/src/standards_lint/rules_include/index.c` gets `{"climate", 8, 0}`: below `game` (2), `mob` and `player` (4) and `render` (6), which may call it, and above `terrain` (9) and `calendar` (11), which it will read. `plans/REPO_STRUCTURE.md` section 11's diagram gets a `climate` row between `render` and `terrain`.

### `run/include/climate/index.h`

```c
/* See run/src/climate/README.md. */
#ifndef MC_RUN_CLIMATE_INDEX_H
#define MC_RUN_CLIMATE_INDEX_H

#include "types/index.h"

/* Ahmed, 2026-11-04: the tropic, at 23.5 degrees, is 10 km from the equator, and latitude rises evenly with y. */
#define CLIMATE_TILT_DEG 23.5f64
#define CLIMATE_TROPIC_Y 10000.0f64
/* Y in master plan 5.1: blocks from the equator to a pole, 38297.87. */
#define CLIMATE_POLE_Y (CLIMATE_TROPIC_Y * 90.0f64 / CLIMATE_TILT_DEG)
/* Equator, north pole, equator, south pole, equator: 153191.49 blocks. */
#define CLIMATE_LATITUDE_PERIOD (4.0f64 * CLIMATE_POLE_Y)
#define CLIMATE_PI 3.14159265358979323846f64

/* phi(y) of master plan 5.1, in degrees: positive north, from -90 to 90, a triangle wave in y. Any finite y. Pure. */
float64_t climate_latitude_deg(float64_t y);

#endif
```

### `run/include/climate/sun/index.h`

```c
/* See run/src/climate/sun/README.md. */
#ifndef MC_RUN_CLIMATE_SUN_INDEX_H
#define MC_RUN_CLIMATE_SUN_INDEX_H

#include "calendar/index.h"
#include "types/index.h"

/* The spring equinox, as a day of the year: delta is 0 here and rising. */
#define CLIMATE_SUN_EQUINOX_DAY 79.0f64
/* Below this cos(phi) a place is a pole, and day length is 0, 12 or 24 hours by the signs alone. */
#define CLIMATE_SUN_POLE_COS 1.0e-12f64

/* Where the sun is, seen from one place at one moment. */
typedef struct climate_sun_t {
    float64_t direction[3];    /* 0: unit vector toward the sun: x east, y north, z up. direction[2] is sin(elevation) */
    float64_t declination_deg; /* 24: delta: the latitude where it is overhead at noon today */
    float64_t hour_angle;      /* 32: H, radians: 0 at noon, -pi/2 at 06:00 */
    float64_t elevation_deg;   /* 40: above the horizon; below 0 is night */
    float64_t azimuth_deg;     /* 48: clockwise from north (+y), 0 to 360: 90 is east */
    float64_t day_hours;       /* 56: sunrise to sunset today, 0 to 24 */
} climate_sun_t;
static_assert(sizeof(climate_sun_t) == 64);

/* delta(d) of master plan 5.2, for a day of the year that may have a fraction: 23.5 * sin(2 pi (d - 79) / 365). */
float64_t climate_sun_declination_deg(float64_t day_of_year);

/* h0 of master plan 5.2, radians from 0 (no sun) to pi (no night), with the clamp, and the poles by their signs. */
float64_t climate_sun_sunset_hour_angle(float64_t latitude_deg, float64_t declination_deg);

/* 24 h0 / pi. */
float64_t climate_sun_day_hours(float64_t latitude_deg, float64_t declination_deg);

/* All of it at a latitude, a day of the year with its fraction, and an hour on the clock (0 to 24). Pure. */
void climate_sun_at(float64_t latitude_deg, float64_t day_of_year, float64_t clock_hour, climate_sun_t *out);

/* The same from the world clock and a y: what the game's threads call. */
void climate_sun_of_clock(uint32_t day_zero, calendar_clock_t clock, float64_t y, climate_sun_t *out);

/* Only direction[2], which is all the bed and mob rules need: the same bits as climate_sun_of_clock gives there. */
float64_t climate_sun_height_of_clock(uint32_t day_zero, calendar_clock_t clock, float64_t y);

#endif
```

How each is worked out, in `float64_t` throughout:

- `climate_latitude_deg(y)`: $y' = y - P\lfloor (y + P/2)/P \rfloor$ with $P$ = `CLIMATE_LATITUDE_PERIOD`; then $\frac{180}{\pi}\arcsin\big(\sin(\pi y' / 2Y)\big)$, and the result clamped to $[-90, 90]$ against rounding.
- `climate_sun_declination_deg(d)`: $23.5 \sin(2\pi(d - 79)/365)$.
- `climate_sun_sunset_hour_angle(φ, δ)`: if $\cos\varphi <$ `CLIMATE_SUN_POLE_COS`, return $\pi$, $0$ or $\pi/2$ by the signs. Otherwise $c = -\sin\varphi\sin\delta/(\cos\varphi\cos\delta)$, clamped to $[-1, 1]$, and $\arccos c$.
- `climate_sun_at`: $H = 2\pi(t - 12)/24$, then the boxed $\vec s$, $e = \arcsin(s_z)$ with $s_z$ clamped to $[-1, 1]$ first, $A = \operatorname{atan2}(s_x, s_y)$ in degrees plus 360 if negative, and `day_hours`.
- `climate_sun_of_clock`: the day of the year from the NEW `calendar_year_position(day_zero, clock)` (below), $t = 24 \times$ (`clock` mod $2^{32}$) $/ 2^{32}$, and $\varphi$ = `climate_latitude_deg(y)`.

### One addition to `calendar`

```c
/* d_c: the day of the year with the fraction of the calendar day passed, from 0 to just under 365. 79.1 is 08:24 on a new
 * world's first morning. Pure. */
float64_t calendar_year_position(uint32_t day_zero, calendar_clock_t clock);
```

It is $\big((\text{day\_zero} \cdot 2^{32} + \text{clock} - 2^{30}) \bmod (365 \cdot 2^{32})\big) / 2^{32}$, done in integers before the one division, so the fraction is exact and the result never reaches 365.

### The renderer: a sun given from outside

`render_frame_t` (`run/include/render/frame/index.h:144`) stays 960 bytes. Out of `_pad1[48]` at 912:

```c
    frame_timings_t last_timings;   /* 848 */
    float32_t sun_given[3];         /* 912: with sun_from_outside: toward the sun, a unit vector, x east, y north, z up */
    uint8_t sun_from_outside;       /* 924: 1 after render_frame_sun_set, 0 after render_frame_time_set */
    uint8_t _pad1[35];              /* 925 */
} render_frame_t;
static_assert(sizeof(render_frame_t) == 960);
static_assert(offsetof(render_frame_t, sun_given) == 912);
```

In `run/src/render/frame/index.c`:

- NEW `void render_sky_fill(const float32_t sun_direction[3], float32_t height, post_settings_t *post, shade_sun_t *sun)`: the body of today's `render_day_fill` from line 335 onward, with `sun_direction` and `height` given. `height` decides the colours of day, twilight and night, exactly as the local `height` does today.
- `render_day_fill(day_fraction, post, sun)` keeps its first lines (330-333) word for word, then calls `render_sky_fill(sun_direction, height, post, sun)`. **Every picture drawn through it is the same to the bit.**
- NEW `void render_frame_sun_set(render_frame_t *frame, const float32_t direction[3])`: stores the direction, sets `sun_from_outside`, and calls `frame_settings_apply`. The colours' `height` is `direction[2]`, the sine of the real elevation.
- `render_frame_time_set` clears `sun_from_outside`, then as today.
- `frame_settings_apply` (line 389): with `SHADE_FEATURE_SKY`, `sun_from_outside ? render_sky_fill(frame->sun_given, frame->sun_given[2], ...) : render_day_fill(frame->day_fraction, ...)`.
- `render` gains no include of `climate`: it is handed three floats.

The moon stays opposite the sun (`run/src/render/frame/index.c:369-371`): in polar night the moon is up all day and lights the land dimly, as `direct_night` and `ambient_night` say.

### The game: the sun from the clock and the eye's latitude

In `run/src/game/index.c`, `game_day_apply` (line 352), called by whichever thread draws, takes the camera that picture is drawn from:

```c
/* On whichever thread draws: the sun of the clock main last said, at the latitude of the eye this picture is drawn from. */
static void game_day_apply(game_t *game, const render_camera_t *camera) {
    climate_sun_t sun;
    climate_sun_of_clock(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed),
                         camera->position[1], &sun);
    const float32_t direction[3] = {(float32_t)sun.direction[0], (float32_t)sun.direction[1],
                                    (float32_t)sun.direction[2]};
    render_frame_sun_set(&game->borrowed_render->frame, direction);
}
```

Call sites, each with a camera it already has: `game_open` (lines 521 and 548, `&game->camera`), `game_scene_draw_timed` (line 1106, `&game->camera`), `game_view_refresh` (line 1156, `&game->camera`), the gpu thread (line 1237, its local `camera`).

`render_frame_time_set` is no longer called by `game`. It stays for the tests and benches that set a time straight on a frame.

### Beds: `player_survival`

In `run/include/player/survival/index.h`:

- `player_survival_t.day_fraction` (offset 24, `float64_t`) becomes `sun_height`: "the sine of the sun's elevation at the body, as the sim last worked it out". Same offset, same size, so `player_survival_t` stays 192 bytes and the saved player record keeps its layout.
- `PLAYER_SURVIVAL_NIGHT_FROM` and `PLAYER_SURVIVAL_NIGHT_TO` go. In their place:

```c
/* A bed works while the sun is lower than this: sin of -8.13 degrees. It is cos(2 pi (0.772583 - 0.5)), so on the equator at the
 * equinox a bed works from 12542 to 23459 ticks after sunrise, as it did with a fixed sun. */
#define PLAYER_SURVIVAL_NIGHT_SUN (-0.14141751442824427f64)
```

- `bool player_survival_is_night(float64_t sun_height)`: `sun_height < PLAYER_SURVIVAL_NIGHT_SUN`.
- `void player_survival_sun_set(player_survival_t *survival, float64_t sun_height)` replaces `player_survival_day_set`.
- `player_survival_init` sets `sun_height = 1.0` (noon), where it set `PLAYER_SURVIVAL_MORNING`.
- `PLAYER_SURVIVAL_MORNING` stays: it's where the clock goes after sleeping.

In `run/src/game/tick/index.c`, `game_sim_tick` (line 500) sets it each tick from the body's own y:

```c
player_survival_sun_set(&game->borrowed_player->survival,
                        climate_sun_height_of_clock(game->day_zero,
                                                    atomic_load_explicit(&game->clock_units, memory_order_relaxed),
                                                    game->borrowed_player->movement.position[1]));
```

`run/src/save/state/index.c:518` checks `isfinite(health->sun_height)` in place of `day_fraction`. A file saved before this phase holds a day fraction there, which is finite and is replaced on the first tick. Nothing reads it before that tick, so no version bump is needed.

### Mobs: `mob_spawn`

In `run/include/mob/spawn/index.h`:

- `mob_spawn_view_t.day_fraction` (offset 24) becomes `float64_t sun_height; /* 24: sin of the sun's elevation at the player: 1 overhead, -1 straight down */`. Still 32 bytes.
- `uint32_t mob_spawn_sky_darken(float64_t sun_height)`: `lit = clamp(2 * sun_height + 0.5, 0, 1)`, then as today. The comment says it's the published curve with the sun's real height in place of the clock's.

`game_items_tick` (`run/src/game/tick/index.c:258`) passes `.sun_height = player->survival.sun_height`. A mob spawns at most `MOB_SPAWN_REACH` = 128 blocks away along y, which is 0.3° of latitude: the player's sun stands for all of them.

### Season label: hemisphere and the polar labels

In `run/src/game/report/index.c`, phase 01's `game_calendar_line`:

- The hemisphere is now the sign of `climate_latitude_deg(y)`, not of y. (Past y = 76,596 the north is behind you.)
- If the day length at the eye is 0: the season part reads **`Polar night`**. If it's 24: **`Midnight sun`**. Otherwise the temperate label as before. So: `Year 1, Day 171, Midnight sun`. This is master plan 4.1's third row. Phase 05 adds the tropics' wet and dry seasons, and the second label.

### F3: latitude and the sun's height

`ui_hud_debug_t` (`run/include/ui/hud/index.h:72`):

```c
typedef struct ui_hud_debug_t {
    float32_t fps;                          /* 0 */
    float32_t stage_ms[UI_HUD_STAGE_COUNT]; /* 4 */
    int32_t block[3];                       /* 40 */
    float32_t latitude_deg;                 /* 52: positive north */
    float32_t sun_elevation_deg;            /* 56: below 0 is night */
} ui_hud_debug_t;
static_assert(sizeof(ui_hud_debug_t) == 60);
```

`ui_hud_debug_draw` gets one more line under the coordinates: `lat 45.00 N  sun 30.0` (`S` for south, `N` at exactly 0, two decimals for latitude, one for the sun). `UI_HUD_DEBUG_LINES` (`run/src/ui/hud/index.c:20`) grows by one. The longest is `lat 90.00 N  sun -66.5`, 22 characters, inside `UI_HUD_DEBUG_WIDTH` = 176 font pixels. `game_hud_debug_read` (`run/src/game/report/index.c:57`) fills both from main's camera y and `clock_units`.

### A way to choose the day, for testing and the gate

A new world starts on the equinox, day 80, and reaching midsummer would take 90 game days. `run/src/main/index.c` gets `--day-of-year <1..365>`, only with `--test-world`, like `--time-of-day` (line 359). It calls phase 01's `game_clock_set(game, (d - 1 - 79 + 365) % 365, fraction)`, with the fraction from `--time-of-day` or `GAME_DAY_START`. `main_options_t` stays 104 bytes: `uint8_t has_day_of_year` and `uint16_t day_of_year` come out of `_pad0[3]`. A shown day of 171 at 00:00 is still calendar day 170 until 06:00 (phase 01's calendar day starts at 06:00), which the usage text says.

### Threads and ownership

Nothing new is owned and nothing is allocated. The only new shared state is `render_frame_t.sun_given`, written and read only by the thread that draws (as `day_fraction` is today). `player_survival_t.sun_height` is the sim's. Every other value is worked out on the spot from `clock_units` (main writes, others read, relaxed, phase 01) and a y the thread already owns.

### Edge cases

| Case | What happens |
|---|---|
| y past a pole | The latitude folds back: y = 50,000 is 62.5° N |
| Exactly at a pole | Day length by signs (24, 0 or 12 h); azimuth is whatever `atan2` gives, since every way is south |
| The equator at an equinox, noon | The sun is straight overhead: walls cast no shadow, and every top face is fully lit. Today's tilted path avoided that on purpose (`run/src/render/frame/index.c:328`); the real path has it for a few days a year near the equator |
| Day cycle off | The clock stops, so the sun's hour and season stop. Flying north still moves the sun, since latitude changes |
| `SHADE_FEATURE_SKY` off | `frame_day_plain` as today: sun straight up, no colours |
| Sleeping in polar night | The clock goes to 06:00, which may still be dark, so a bed works again at once: each sleep is a day |
| Midnight sun | A bed answers `PLAYER_SURVIVAL_BED_NOT_NIGHT` all day |
| Two threads, one moment | The same `clock_units` and y give the same bits on each |

## Owned paths

This phase may create or edit:

- `run/src/climate/`, `run/include/climate/` (NEW, including `sun/`)
- `tests/run/climate/`, `tests/run/climate/sun/`, `tests/run/game/sun/` (NEW)

Wiring outside them, kept to what this file names:

- **Calendar:** `run/include/calendar/index.h`, `run/src/calendar/index.c` and README (`calendar_year_position`), `tests/run/calendar/index.c` (its test).
- **Renderer:** `run/include/render/frame/index.h`, `run/src/render/frame/index.c`, `run/src/render/frame/README.md`.
- **Game:** `run/src/game/index.c` (`game_day_apply`), `run/src/game/tick/index.c` (sun height each tick, mob view), `run/src/game/report/index.c` (F3, season label), `run/src/game/README.md`.
- **Beds:** `run/include/player/survival/index.h`, `run/src/player/survival/index.c` and README; `run/src/save/state/index.c` (the one `isfinite` line).
- **Mobs:** `run/include/mob/spawn/index.h`, `run/src/mob/spawn/index.c` and README.
- **HUD:** `run/include/ui/hud/index.h`, `run/src/ui/hud/index.c` and README.
- **Flag:** `run/src/main/index.c`.
- **Callers of renamed fields:** `tests/run/mob/spawn/index.c`, `tests/run/player/survival/index.c`, `tests/fuzz/save/index.c:300`, `bench/mobs/index.c:236` (`.day_fraction = 0.0` becomes `.sun_height = -1.0`, which is midnight on both rules).
- **Pictures:** `tests/run/ui/hud/fixtures/debug_640x480.png`, and any golden picture drawn through `game_day_apply` (see Risks), each remade only after looking at it.
- **Lint and layout:** `compile/src/standards_lint/rules_include/index.c` (one row), `plans/REPO_STRUCTURE.md` (sections 6, 10, 11 and 14).

## Steps

1. **Create `run/src/climate/` and `run/include/climate/index.h`** with `climate_latitude_deg`, and the README (purpose, tier Owner by depth reserving nothing yet, threads any, public functions, may be called by `game`, `mob`, `player` and later climate parts). *Why:* every later step needs latitude, and it's the simplest pure function to prove first.
2. **Add `{"climate", 8, 0}`** to `include_systems` in `compile/src/standards_lint/rules_include/index.c`, and the `climate` row to the diagram in `plans/REPO_STRUCTURE.md` section 11. *Why:* the lint's `unknown_system` rule refuses a `run/` system not in that table, and rank 8 lets `game`, `mob` and `player` include it while it can read `terrain` and `calendar`.
3. **Write `tests/run/climate/`** (ctest `run_climate`). Build and run. *Why:* latitude is proven against the master plan's table before anything draws with it.
4. **Add `calendar_year_position`** to `run/src/calendar/` and a test for it in `tests/run/calendar/`. *Why:* the sun needs a smooth day of the year from phase 01's clock.
5. **Create `run/src/climate/sun/`** with the header above, the README, and the functions. *Why:* the sun's maths in one place, pure, so every thread can call it.
6. **Write `tests/run/climate/sun/`** (ctest `run_climate_sun`). Build and run. *Why:* every worked example in this file becomes a check before the picture depends on it.
7. **The renderer.** Split `render_day_fill` into itself plus `render_sky_fill`, add `sun_given` and `render_frame_sun_set`, and the branch in `frame_settings_apply`. Run `render_feature_sky`, `render_feature_post`, `render_feature_water`, `render_feature_shapes` and `render_feature_lod` before changing `game`: their golden pictures must all still match. *Why:* proves the split changed nothing before the game starts using the new path.
8. **The game.** Change `game_day_apply` to take the camera and set the sun from the clock and its y. Run the same tests again, and `run_render_frame`. Note every golden picture that no longer matches. Look at each one's new picture (written with `MC_GOLDEN_WRITE=1` as that test's README says), check the sun is where this file says, then commit the new pictures, each test's README saying why. *Why:* this is the moment the sky changes, so it's the moment to look.
9. **Beds.** Rename `day_fraction` to `sun_height` in `player_survival_t`, add `PLAYER_SURVIVAL_NIGHT_SUN`, change `player_survival_is_night`, `player_survival_sun_set`, the sim's call each tick and the save check. Update `tests/run/player/survival/`. *Why:* nights follow the sun.
10. **Mobs.** Rename the view's field, change `mob_spawn_sky_darken`, pass the sun height from the tick, update `tests/run/mob/spawn/`, `tests/fuzz/save/` and `bench/mobs/`. *Why:* mobs spawn by real darkness, Ahmed's decision.
11. **The F3 line and the season label.** Add the two fields, the line, `game_hud_debug_read`, and the hemisphere and polar labels in `game_calendar_line`. Remake `debug_640x480.png` after looking at it. *Why:* Ahmed can read his latitude while flying, and the HUD names polar night and midnight sun.
12. **`--day-of-year`.** Add the flag to `run/src/main/index.c`, refused without `--test-world`. *Why:* the gate needs midsummer without 90 days of play.
13. **Write `tests/run/game/sun/`** (ctest `run_game_sun`). *Why:* proves the whole chain, clock to latitude to picture, beds and mobs, on the real game.
14. **Documentation.** READMEs of every touched directory. `plans/REPO_STRUCTURE.md`: a `climate` system with its `sun` part in section 6, the three test rows in section 10, and "Latitude and where the sun is" in section 14.
15. **Verification** below, then tick this file's boxes and record what was measured.

## Todo

- [ ] `run/{src,include}/climate/`: $\varphi(y)$, with the period reduction, and its README
- [ ] `climate` in the lint's system table at rank 8, and in the dependency diagram
- [ ] `run/{src,include}/climate/sun/`: $\delta(d)$, $h_0$, day length, the sun's direction, elevation and azimuth, from a latitude or from the world clock and a y
- [ ] `calendar_year_position` in `run/src/calendar/`, with its test
- [ ] Tests: $\varphi(0) = 0$, $\varphi(10000) = 23.5$, $\varphi(38298) = 90$, $\varphi(-38298) = -90$, $\varphi(y + 153191) = \varphi(y)$; 24-hour days past 66.5° at midsummer; 12-hour days everywhere at the equinox
- [ ] Tests: every worked example in this file (45° at y = 19,149; 18.51 h and 5.49 h at 60° N; azimuth 125.3° and elevation 30° at 45° N, 09:00, equinox)
- [ ] `render_sky_fill` split out of `render_day_fill` with no change to any picture drawn through `render_day_fill`
- [ ] `render_frame_sun_set`, and `game_day_apply` feeding it the sun at the drawing camera's latitude, in place of today's fixed path
- [ ] Golden pictures drawn through the game: each looked at, remade, and its README saying why
- [ ] Night for beds follows the sun at the body: `sun_height`, `PLAYER_SURVIVAL_NIGHT_SUN`
- [ ] Mob spawning darkness follows the sun at the player: `mob_spawn_sky_darken(sun_height)`
- [ ] On the equator at the equinox, beds and mob darkness are what they were, for every time of day
- [ ] Latitude and the sun's elevation in the F3 overlay; `debug_640x480.png` remade after looking
- [ ] The season label uses the latitude's hemisphere, and says `Polar night` or `Midnight sun`
- [ ] `--day-of-year`, only with `--test-world`
- [ ] READMEs and `plans/REPO_STRUCTURE.md` for `climate`, `climate/sun` and the new tests

## Tests

### `tests/run/climate/` — ctest `run_climate`, no card

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `latitude_is_0_at_the_equator_23_5_at_the_tropic_and_90_at_each_pole` | the master plan's list | $\varphi(0) = 0$; $\varphi(10000) = 23.5$ within $10^{-9}$; $\varphi(28298) = 66.5$ within 0.001; $\varphi(38298) = 90$ and $\varphi(-38298) = -90$ within 0.001; $\varphi(76596) = 0$ within 0.001 | $Y$ is wrong, or north and south are swapped |
| `latitude_at_19149_is_45_north_and_at_minus_19149_is_45_south` | worked example 1 | 45.000 within 0.001 | the scale or the arcsin is wrong |
| `latitude_folds_back_past_a_pole` | worked examples 2 and 3 | $\varphi(30000) = 70.5$; $\varphi(50000) = 62.50$; $\varphi(100000) = -55.0$; all within 0.001 | the wave isn't a triangle |
| `latitude_repeats_every_period` | $\varphi(y + 153191) - \varphi(y)$ for 10,000 y spread from $-3 \times 10^7$ to $3 \times 10^7$, and with the exact `CLIMATE_LATITUDE_PERIOD` | within 0.002° for the whole number; within $10^{-6}$ for the exact period | the reduction loses digits far out |
| `latitude_rises_evenly_between_the_poles` | steps of 1 block from $-38297$ to $38297$ | each step $90/38297.87 = 0.0023500$° within $10^{-7}$ | it's a smooth wave, not a triangle |
| `latitude_does_not_care_about_x` | (by construction, it takes only y) the header has no x | — | — |
| `latitude_is_the_same_on_any_thread` | 16 workers, 100,000 random y each, against main | equal bits | anything here is not pure |

### `tests/run/climate/sun/` — ctest `run_climate_sun`, no card

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `declination_is_0_at_the_equinoxes_and_23_5_at_the_solstices` | $\delta(79) = 0$; $\delta(170.25) = 23.5$; $\delta(261.5) = 0$; $\delta(352.75) = -23.5$ | within $10^{-9}$ | the phase or the sign is wrong |
| `at_60_north_midsummer_is_18_51_hours_and_midwinter_5_49` | worked example 8 | $h_0 = 2.4236$ rad; 18.515 h and 5.485 h within 0.001; they add to 24 within $10^{-9}$ | the clamp, the sign or the factor 24 is wrong |
| `every_latitude_has_12_hour_days_at_the_equinox` | $\delta = 0$, $\varphi$ from $-90$ to $90$ in steps of 0.1, poles included | 12 h within $10^{-9}$ | a pole gives NaN |
| `past_66_5_midsummer_has_24_hours_and_midwinter_none` | $d = 170.25$, $\varphi$ from 66.6 to 90 north (24 h) and south (0 h); and at $\varphi = 66.5$, $\delta = 23.5$ exactly | 24 and 0 exactly; 24 within 0.001 at the circle itself ($\tan 66.5° \tan 23.5° = 1.0000$ to rounding) | the clamp is missing |
| `the_south_is_the_north_half_a_year_apart` | day length at $(\varphi, \delta)$ and $(-\varphi, -\delta)$ for 1000 random pairs | equal | a sign is lost |
| `the_sun_is_a_unit_vector_and_its_height_is_the_master_plans_formula` | 100,000 random $(\varphi, d, t)$ | $\lvert\vec s\rvert = 1$ within $10^{-12}$; $s_z = \sin\varphi\sin\delta + \cos\varphi\cos\delta\cos H$ within $10^{-12}$ | a component is wrong |
| `at_45_north_on_the_equinox_the_9_oclock_sun_is_30_degrees_up_in_the_south_east` | worked example 4 | $\vec s = (0.70711, -0.5, 0.5)$; $e = 30.000$; $A = 125.264$; all within 0.001 | east and west, or north and south, are swapped |
| `the_sun_rises_in_the_east_and_sets_in_the_west_at_the_equinox` | $\varphi = 0$, $\delta = 0$, 06:00 and 18:00 | $\vec s = (1, 0, 0)$ and $(-1, 0, 0)$ within $10^{-12}$ | the hour angle's sign is wrong |
| `at_noon_the_sun_is_south_in_the_north_and_north_in_the_south` | 12:00, $\varphi = \pm 45°$, $\delta = 0$ | $A = 180$ and $A = 0$ (or 360); $e = 45$ | the derivation's $\vec M$ is wrong |
| `the_midnight_sun_at_70_north_and_polar_night_at_80_north` | worked examples 5, 6 and 7 | 60° N midsummer noon 53.5°, midnight $-6.5$°; 70° N midsummer midnight $+3.5$°, azimuth 0; 80° N midwinter noon $-13.5$°; each within 0.01 | |
| `the_fixed_sun_of_today_is_this_sun_at_20_degrees_north_on_the_equinox` | $\varphi = 0.35$ rad, $\delta = 0$, every 0.001 of a day: against `render_day_fill`'s direction | within $10^{-6}$ (it is `float32_t`) | the derivation and the old path disagree |
| `from_the_clock_a_new_world_at_08_24_on_the_equator_has_the_sun_36_degrees_up_in_the_east` | `climate_sun_of_clock(79, clock 0.35, y 8)` | $d_c = 79.1$, $\delta = 0.0405$, $e = 36.0$ within 0.05, $A = 90$ within 0.1 | the clock or day of year is read wrongly |
| `the_sun_is_the_same_on_any_thread` | 16 workers, 100,000 random clocks and y | equal bits | anything is not pure |

### `tests/run/calendar/` — one new test

| Test | Checks | Numbers |
|---|---|---|
| `the_year_position_has_the_fraction_of_the_calendar_day_and_never_reaches_365` | `calendar_year_position(79, ...)` | clock 0.35: 79.10; clock 5.25: 84.0; clock 285.9999: 364.9999; clock 286.25: 0.0 |

### Existing tests changed

| Where | Test | Change |
|---|---|---|
| `tests/run/player/survival/` | `armor_falls_and_night_by_hand` (`index.c:166`) | The night list becomes sun heights: night for $-0.141418$, $-0.5$, $-1$; day for $-0.141417$, $0$, $0.5$, $1$. Plus: for each of today's fractions (`index.c:187-188`), $\cos(2\pi(f - 0.5))$ gives today's answer, so the equator at the equinox is unchanged |
| | The sleep in the real game (`index.c:1371`) | Passes as it is: it sleeps at 0.9, which at spawn ($\varphi \approx 0.02°$, $\delta \approx 0.04°$) is $s_z = -0.809$ |
| `tests/run/mob/spawn/` | `index.c:225-235` | `mob_spawn_sky_darken(1.0) == 0`, `(-1.0) == 11`, `(0.0) == 5`, `(0.5878) == 0` (08:24 on the equator); `ms_view` takes a sun height, `MS_NOON` 1.0 and `MS_MIDNIGHT` $-1.0$ |
| `tests/run/ui/hud/` | `the_overlays_numbers_change_only_their_own_line_and_it_stays_on_its_panel` (`index.c:549`) | Covers the new line: `lat -90.00 S  sun -66.5` fits; changing latitude changes only that line |
| | `golden_pictures_match` | `debug_640x480.png` remade, looked at |

### `tests/run/game/sun/` — ctest `run_game_sun`, on the card

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `the_picture_takes_the_sun_at_the_eyes_latitude` | Flying, clock at day 171 (`game_clock_set(game, 91, 0.0)`, midnight), eye at y = 8, then y = 30,000; `render_frame_t.sun_given` after a draw | at y = 8: $s_z = -0.917$ within 0.01 (midnight, $e = -66.5°$); at y = 30,000 ($70.5°$ N): $s_z = +0.070$ within 0.01, $s_y > 0$ (sun due north) | the eye's y isn't used, or the fixed path still draws |
| `the_sky_is_lit_at_midnight_past_the_polar_circle_in_summer` | The same two places: the mean brightness of the sky pixels | at 70.5° N brighter than at the equator by a clear margin (the test prints both) | the colours still follow the clock |
| `a_bed_does_not_work_at_60_north_on_a_midsummer_midnight` | Walking at y = 25,532 (60° N), day 171, 00:00, a bed placed as in `tests/run/player/survival/` | `bed_answer == PLAYER_SURVIVAL_BED_NOT_NIGHT`; at day 354 and 18:00 the same bed sleeps | beds still use the clock |
| `mobs_find_open_ground_dark_at_noon_at_80_north_in_midwinter` | `mob_spawn_check` on an open-sky stone floor at y = 34,042 (80° N), day 354, 12:00, roll 7 | `MOB_SPAWN_OK` (darken 10, light 5); at y = 8 the same noon gives `MOB_SPAWN_TOO_BRIGHT` | darkness still uses the clock |
| `the_season_label_says_midnight_sun_and_polar_night` | `game_calendar_line` at 70.5° N on day 171, and at 80° N on day 354 | ends `Midnight sun`; ends `Polar night`; at y = 8 day 171 `Early summer`; at y = $-19{,}149$ day 171 `Early winter` | the label ignores the sun or the latitude's sign |
| `the_day_of_year_flag_is_taken_with_test_world_and_refused_without` | The built program, `--headless --test-world --day-of-year 171 --ticks 2`, and `--headless --menu --day-of-year 171` | the first runs and logs the day; the second exits refusing it | the flag is missing or unguarded |

## Verification

New directories need the configure step first:

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_climate|run_climate_sun|run_calendar|run_game_sun|run_game_census)$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_survival|run_mob_spawn|run_ui_hud|run_render_frame|run_game|run_save_state|run_save_state_game)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_sky|render_feature_post|render_feature_water|render_feature_shapes|render_feature_lod|render_feature_entities)$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(standards_lint|standards_lint_fixtures|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

The whole suite, once, before the gate:

    tools/test-or-build.sh timeout 3400 ctest --preset debug --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Gate

Ahmed, on a release build (`tools/test-or-build.sh cmake --preset release`, then `tools/test-or-build.sh cmake --build --preset release`):

    ./build/release/bin/minecraft_c --windowed --test-world --day-of-year 171 --time-of-day 0.95

1. It's 22:48 on midsummer's day at the equator: dark. Press F3: `lat 0.00 N`, and a sun well below 0.
2. Fly north (W, looking along +y; the compass points come from the coordinates in the corner: y must grow). Scroll the wheel up about 13 steps and hold Ctrl for the fastest speed, about 1,800 blocks a second. Watch the F3 latitude climb: 23.50 at y = 10,000, 45.00 at 19,149.
3. As you pass about 52° N (y ≈ 22,100) the midnight sky stops being fully dark, since the sun is less than 14.5° below the horizon. Past about 58° N (y ≈ 24,850) a bed would no longer work. Past the polar circle, **66.5° N at y = 28,298**, the sun is above the northern horizon at midnight, and the HUD's season reads `Midnight sun`.
4. Keep going to the pole, 90° N at y = 38,298, and past it: the latitude falls again.
5. Optionally, the winter side: `./build/release/bin/minecraft_c --windowed --test-world --day-of-year 354 --time-of-day 0.5`, fly to 80° N (y ≈ 34,042): noon, and the sun never comes up.

## Risks and open questions

| Question | Default |
|---|---|
| Golden pictures change wherever the game draws with its own clock. Known: `tests/run/render/frame/fixtures/{start,north_low,down}.png`, since `game_open` sets the sun and `fr_draw` keeps it (`tests/run/render/frame/index.c:1162-1183`). Possibly some of `tests/cuda/lod/`, whose `fixtures/rest.h:240` runs `game_loop_frame` | Step 7 proves nothing changes through `render_day_fill`; step 8 lists what changed through `game`. At spawn (0.02° N) on the first morning the new sun is 36° up due east, against today's 33.5° up east-south-east, so shadows move a little south. Each picture is looked at before it's remade |
| At the equator near an equinox, noon has no shadows from walls, which today's tilt avoided on purpose (`run/src/render/frame/index.c:328`) | Accepted as the real sun Ahmed asked for. If it looks wrong in play, a small fixed tilt could be added to $\varphi$ for drawing only. Not done unless Ahmed asks |
| Sleeping in polar night wakes at 06:00, still dark | Accepted for now (phase 01 wakes to 06:00). Waking at the next sunrise instead would be one change to `game_day_advance`; for Ahmed |
| North of 58.4° N there is no bed night around midsummer | Accepted: it follows from the real sun and Ahmed's "Mobs spawn by real darkness", which the master plan extends to beds |
| The moon is always opposite the sun, so in polar night it's up all day | Accepted for this plan. A real moon is not in plan 004 |
| `player_survival_t.sun_height` reuses the saved bytes of `day_fraction` | Safe: every tick sets it before anything reads it, and `isfinite` holds for both. No `SAVE_STATE_VERSION` bump |
| Mobs far from the player use the player's sun | 128 blocks is 0.3° of latitude: invisible |
| Phase 03 may want latitude per cell in `float32_t` | `climate_cell_t.lat_deg` is `float32_t`; it is worked out here in `float64_t` and narrowed there |
| Ahmed's gate flight over ungenerated land | The sun doesn't need terrain. Ground may lag behind at 1,800 blocks a second; slow down near the polar circle to look |

## Commits

Appended as they land:

## Issues found

Found while writing this file, 2026-11-04, before any code.

**1. The master plan rounds 60° N midsummer to 18.6 hours; it is 18.5.**

- **Expected (master plan 5.2):** "$h_0 = \arccos(-0.753) = 2.43$ rad, so the day lasts $24 \times 2.43 / \pi = 18.6$ hours."
- **Actual:** $\arccos(-0.7531) = 2.4236$ rad, and $24 \times 2.4236/\pi = 18.515$ hours. Rounding $h_0$ to 2.43 first gives 18.56, which then rounds to 18.6. The midwinter 5.5 hours ($5.485$) is right, and the two must add to 24.
- **Done in this file:** the test holds 18.515 and 5.485, within 0.001. No change to the formula.

**2. The latitude period is not a whole number of blocks.**

- **Expected (master plan 5.1 and the phase 02 test):** "It repeats every $4Y = 153{,}191$ blocks", and "$\varphi(y + 153191) = \varphi(y)$".
- **Actual:** $4Y = 4 \times 10\,000 \times 90 / 23.5 = 153\,191.49$. Shifting by the whole number 153,191 moves latitude by about 0.0012°.
- **Done in this file:** $Y$ stays exactly $10\,000 \times 90/23.5$, so the tropic is at exactly 10,000 as Ahmed decided. The test checks the whole-number shift within 0.002° and the exact period within $10^{-6}$.

**3. The master plan's darkness rules are worded by time, and are kept by the sun's height.**

- **Expected (master plan 5.2):** "Beds work at night and mobs spawn in darkness, so both follow automatically."
- **Actual:** neither follows automatically. Both read the time of day: beds by two fixed fractions (`run/include/player/survival/index.h:40-41`, `run/src/player/survival/index.c:80-83`), mobs by $\cos(2\pi(f - 0.5))$ (`run/src/mob/spawn/index.c:24-29`). Fed today's clock, they would ignore latitude and season.
- **Done in this file:** both take the sine of the sun's elevation at the player, which equals the quantity they already use on the equator at the equinox, so nothing changes there and both follow the real sun elsewhere. This is the code change the master plan's "automatically" needs.

**4. Phase 01's hemisphere for the season label was y's sign.**

- **Expected (phase 01 file):** north is $y \ge 0$, until phase 02.
- **Actual:** past y = 76,596 the latitude is south while y is positive.
- **Done in this file:** the label's hemisphere becomes the sign of $\varphi(y)$, and the polar labels are added, as phase 01's file said phase 02 would.
