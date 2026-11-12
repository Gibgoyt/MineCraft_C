# run/src/climate/rain

**Purpose:** rain, master plan 6.7 (plan 004 phase 05). `P = P_belt(ψ) · M · O` on each of the 12 fit days, fitted to an average and two yearly waves into `rain_mm_week` (mm a week; a day's normal is `P(d)/7`, the year's `P0 × 365/7`). `P_belt` is the rain-belt spline at the wind latitude `ψ = φ - 0.5 δ(d - 30)`, a rate in mm a year. `M = max(0.15, e^(-d_up/3000))` times 0.4 within 800 blocks of a cold current, else 1.25 if the air left over a warm current, else 1, in that order. `O = clamp(1 + 8 s_w - max(0, h_up - z)/400, 0.15, 2.5)`. `d_up` comes from the **upwind walk**: from the cell's centre, 128-block steps against the day's unit wind; steps 1 to 16 read the window's kinds (water is sea or a large lake, and a sea's current comes from `climate_air_window_current`), steps 17 to 64 read heights only, as the coarse land mask has them (water is height ≤ 0, any size, no current); `d_up = 128 (k_w - 1)` at the first water, 8192 with none. `h_up` is the highest ground before the water, the cell's own ground at the least; `s_w` is the rise into the wind over the last 256 blocks, from the window's heights read bilinearly, the sea as 0. A wind under 0.001 blocks a second has no direction: then `d_up` is the cell's sea distance. Sea and large-lake cells walk nowhere: `d_up = 0` with no current and `O = 1`.
**Tier:** Arena by depth; allocates nothing (the 12 days of a cell are a local array).
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; pure. A tile's cells are done south row first, x fastest, the days in order, so the bytes are the same on any worker.
**Public functions:**

- `climate_rain_walk`, `climate_rain_moisture`, `climate_rain_orographic`, `climate_rain_belt_mm_year`: the pieces, each with its worked example in the phase 05 file.
- `climate_rain_wind`, `climate_rain_psi`: the day's wind (the stored waves) and wind latitude, or the test hooks' fixed wind and frozen seasons.
- `climate_rain_day_mm_week`: one cell's rain on one day, with the walk it took.
- `climate_rain_build_tile`: a tile's `rain_mm_week`.
- `climate_test_hooks_t`: defined here, the lowest part that reads it; the Owner holds it and `climate_test_hooks_set` sets it. Tests only: a fixed wind, `P_belt` fixed, `δ = 0` in `ψ`, `O = 1`. All zeros in every real run.

Every step of a tile cell's walk is inside its window: the cell is 8192 blocks and half a cell from the edge, and step 64 is 8192 away. So the walk never reads the mask's table, and a cell's rain is a function of its window alone.

**May be called by:** its parent `run/src/climate/` (the tile job) and `season/` (the wind and the hooks).
**Depends on:** climate/cell, climate/normals (the belt spline), climate/air (the wind latitude, a window sea cell's current), climate/harmonic (the waves and the fit), climate/sea (the kinds), `assert`, `types`, `math/det` (the moisture's e^x since plan 006 phase 02), libm for exact functions only.
