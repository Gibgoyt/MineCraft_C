# run/src/climate/weather/week

**Purpose:** plan 004 phase 07's weekly update on plain numbers (master plan 7.5), and the phase 07 file's day-kind table, so both can be tested with the master plan's own numbers and no climate at all.

- **The update**, in this order: the 7 day kinds; `T_w` the mean of the 7 day means, `t_min` the lowest low and `t_max` the highest high; `P_w` the sum of the rain; `f_s P_w` the sum of the snow parts, passed as that sum so it is exact; ice day by day; then `M_w = 7 · 4 · max(0, T_w)`, `S' = max(0, S + f_s P_w − M_w)`, `E_w = 7 · 0.2 · max(0, T_w)`, `W' = W + (1 − f_s) P_w + min(M_w, S + f_s P_w) − E_w`, soil `clamp(W', 0, 150)` and runoff `max(0, W' − 150)` from `W'` before the clamp; the mean of the 12:00 winds. `flood` is left alone: flow accumulation sets it.
- **Ice** on still water, day by day: `I += 1.5 max(0, −T) − 2 max(0, T)` cm, never below 0.
- **Fast mode's** water-only week (`climate_week_update_water`): the same snow, soil and runoff from one week's mean temperature, precipitation and snow share; ice, flood and the day kinds stay as they were.
- **The snow share** is phase 05's `climate_heat_snow_share`, `clamp((1 − T) / 3, 0, 1)`: all rain above 1 °C, all snow below −2 °C.
- **The day kind:** the first row that matches, in the order hurricane, storm (a low over 20 hPa deep or the strongest hour over 15 blocks a second), thunderstorm, dust storm (wind over 12, under 2 mm in the 7 days ending today, desert), snow, sleet, rain (over 1 mm, by the day's mean: below −2, −2 to 1, above 1), heat wave (`T' > 1.5 σ_T`), fog (wind at 06:00 under 3, and a cold-current coast within 2000 blocks or a humid valley 30 blocks below its 8 neighbours' mean with soil over 100 mm), cloudy (this week's normal rain over 10 mm, or within 1.5 radii of a low at 12:00), clear.

Every sum is `float32_t`, added in one fixed order: the same inputs give the same bits on any thread.

**Tier:** Borrow by depth: allocates nothing.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; pure.
**Public functions:** `climate_week_day_kind`, `climate_week_update`, `climate_week_update_water`, `climate_week_ice`, `climate_week_evaporation_actual` (what 7.5's clamp at 0 let evaporate, for the conservation test).
**May be called by:** its parent `run/src/climate/weather/`, phases 08 to 11, the tests.
**Depends on:** `climate/season` (`CLIMATE_MELT_MM_PER_DEGREE_DAY`) and `climate/heat` (the snow share and its two temperatures), phase 05's one homes for them; `types`.
