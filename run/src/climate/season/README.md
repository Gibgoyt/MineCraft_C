# run/src/climate/season

**Purpose:** the labels worked out once from a cell's normals (plan 004 phase 05, master plan 6.8 to 6.10).

- **Hurricane season:** `s_hur(d) = clamp((T_sea(d) - 26.5)/2, 0, 1)` between 5° and 25° of latitude, with phase 04's sea temperature. A sea cell's season is its own; a land or lake cell within 3000 blocks of the sea takes its nearest sea cell's (that cell's current from `climate_air_window_current`, its latitude at its centre) and gets `CLIMATE_CELL_HURRICANE_COAST`. The season is the longest run of whole days with `s_hur > 0.25`, round the year, ties to the earliest start, stored in `hurricane_days` as quarter days (`floor(d/4)`, 0 to 91); 255 for none, `{0, 91}` for all year, first greater than last when it wraps past day 364.
- **Monsoon** (land with a sea direction): on the 12 fit days, a wind from the sea (`w · n < 0`) with the wind half a year later from the land, and the wettest 6 days in a row over 3 times the other 6: `CLIMATE_CELL_MONSOON`. Monsoon today: the flag, today's wind from the sea and `P(d) > P0`.
- **Snow balance:** the height above which snow outlasts the summer, by a degree-day model on the normals (master plan 7.5's 4 mm of water a degree a day): 52 weeks at days `7w + 3.5`, `S = max(0, S + f_s(T) P - 28 max(0, T))`, two years from no snow, and it outlasts if `S` never returns to 0 in the second. Bisection over -250 to 2000 in 12 halvings (0.55 blocks); -250 if it outlasts even there, 9999 (`CLIMATE_SNOW_BALANCE_NONE`) if not at 2000. The temperature wave and the rain of the 52 weeks are worked out once a cell, as they do not depend on the height.
- **Climate class** (6.10, simplified Köppen, first match wins): Ice, Tundra, Desert, Steppe, Rainforest, Monsoon, Savanna, Mediterranean, Temperate, Continental, Boreal; sea is `CLIMATE_CLASS_NONE`. The warmest and coldest month means are the month centred on the warmest and coldest day, `T̄ ± 0.98862 |A|`; the rain of the 12 calendar months uses 0.98862 on the first wave and 0.95493 on the second; "dry summer" is the driest month of the local summer half under 40 mm and under a third of the winter half's wettest.

**Tier:** Arena by depth; allocates nothing.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; pure.
**Public functions:** `climate_season_hurricane_s`, `climate_season_hurricane_days`, `climate_season_hurricane_today`, `climate_season_monsoon_flag`, `climate_season_monsoon_today`, `climate_season_classify`, `climate_season_months`, `climate_season_snow_outlasts`, `climate_season_snow_balance`, `climate_season_build_tile`. The constants, `CLIMATE_MELT_MM_PER_DEGREE_DAY` included (its one home, which phase 07 includes), and the `CLIMATE_CLASS_*` values are in the header.

**May be called by:** its parent `run/src/climate/` (the tile job, `climate_normals_at`), and `run/src/climate/layer/` through the Owner.
**Depends on:** climate/cell, climate/normals, climate/rain (the wind and the hooks), climate/heat (the formula's parts), climate/air (the sea temperature, a window sea cell's current), climate/harmonic, climate/sea, the Owner's header for latitude, `types`, libm.
