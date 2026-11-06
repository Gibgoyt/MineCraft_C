# run/src/climate

**Purpose:** the climate of plan 004, worked out strictly top to bottom (master plan section 3). For now its first number: the latitude of a place, from its y alone (master plan 5.1, and the phase 02 file). Its part `sun/` gives where the sun is. Later phases add the land mask, sea distance, currents, belts, wind, temperature and rain here.
**Tier:** Owner by depth. It reserves nothing yet: plan 004 phase 03 adds the climate tile cache, which it will own.
**Owns:** nothing.
**Lifetime:** Process. There is no state, so it has no `_init` and no `_deinit`, like `run/src/calendar/`.
**Threads:** any. Every function is pure: the answer depends only on the arguments, and the same arguments give the same bits on every thread (the repo has no `-ffast-math`, and C23 contracts no floating-point expression).
**Public functions:**

- `climate_latitude_deg`: `phi(y) = (180 / pi) asin(sin(pi y / 2Y))`, in degrees, positive north, with `Y = CLIMATE_POLE_Y = 10000 * 90 / 23.5 = 38297.87` blocks. A triangle wave: 0 at y = 0, 23.5 at the tropic (10,000), 66.5 at the polar circle (28,298), 90 at the pole (38,298), 0 again at 76,596, and -90 at -38,298. y is first brought into one period, `CLIMATE_LATITUDE_PERIOD = 4Y`, so a y out at the world's edge keeps its digits. x plays no part.

**May be called by:** `main`, `game`, `mob` and `player` (`compile/src/standards_lint/rules_include/index.c` ranks it 8, under `render`), and later climate parts. `render` is never given it: it is handed a sun direction as three floats.
**Depends on:** `types`, glibc's libm.

Positive y is north and positive x is east (Ahmed, 2026-11-04). The period is 153,191.49 blocks, not a whole number, so the tropic stays at exactly 10,000: shifting y by the whole 153,191 moves latitude by about 0.001 degrees (phase 02 file, Issues found 2).
