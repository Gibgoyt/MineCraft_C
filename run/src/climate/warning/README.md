# run/src/climate/warning

**Purpose:** the HUD's storm warning, master plan 9.1: "when a forecast gives over 50% for a hurricane or storm within the next day". Plan 004 phase 10. The chances are the outlook's own (`climate/outlook/`, phase 07): this part adds no weather maths, only the five moments and the rule.
**Tier:** Arena by depth (2). It allocates only from the arena it is given (in the game, the Job arena of the worker it runs on), and nothing for the rule itself.
**Owns:** nothing.
**Lifetime:** Process. No state.
**Threads:** any. Every function is pure in the climate's seed and its arguments; `climate_warning_at` reads climate tiles under the cache's read protocol and mask pages as `climate/systems/` builds them.
**Public functions:**

- `climate_warning_decide`: the phase 10 file's rule, from plain numbers. 1: the known hurricane (tropical storms included) with the largest `q`, ties to the lower id, if `q > 0.5`: `CLIMATE_WARNING_NAMED`. 2: else if `1 - prod(1 - q_i) e^(-dLambda)` over the hurricanes is over 0.5: `CLIMATE_WARNING_HURRICANE_LIKELY`. 3: else the same over storm lows: `CLIMATE_WARNING_STORM`. 4: else none. Strictly over `CLIMATE_WARNING_THRESHOLD`: exactly 0.5 does not warn. The combination is `climate_outlook_at_least_one`.
- `climate_warning_steps`: one system's chance at `t0, t0 + 1/4, ..., t0 + 1` days after the census, through `climate_outlook_system_chance`, and whether it is a storm (at least `CLIMATE_STORM_DEPTH_HPA`, 20 hPa, deep, 7.4) at each.
- `climate_warning_at`: the whole thing at a place and moment `t`. The forecast is the one the census at 06:00 on the first day of `t`'s week made (`climate_warning_census_t`), so `t0 = t - census_t`, 0 to 7 days. The known systems are `climate_outlook_known_list`'s; a hurricane's `q` is its best over the five moments, a low's its best over the moments it is a storm; highs never warn, and thunderstorm cells are never asked for. `dLambda = Lambda(t0 + 1) - Lambda(t0)` from `climate_outlook_unborn`, held at 0 or above (the outlook's Lambda is of systems present at an instant, so it can fall); for lows it is taken at the outlook's share of lows that become storms, `CLIMATE_OUTLOOK_DEEP_LOW_SHARE`, as the outlook's own day storm chance takes it. A named warning carries the system's name seed and whether it is a hurricane or still a tropical storm now (`CLIMATE_HURRICANE_STORM_VMAX`), so whoever shows it needs no climate: `climate_system_name` makes the name. False, with no warning, if the place's tile is not built.
- `climate_warning_census_t`: `7 floor(floor(t) / 7)`.

Worked example (phase 10 file): Hurricane Tavela 6 km east at the census, heading west at 3.5 km a day, radius 2500, no forecast error: 5.7%, 12.6%, 24.4%, 41.6% and 62.3% at the five moments, so `q = 62.3%` and "Storm warning: Hurricane Tavela". With the start 6483 blocks east the fifth moment is 51% and it warns; at 6571, 49% and it does not.

**May be called by:** `game` (`game/corner`: the sim submits it to a worker every `CLIMATE_WARNING_TICKS` and after each census, as one call takes milliseconds), tests, the bench.
**Depends on:** `run/src/climate/` (cells), `climate/outlook` (the per-system chance, the known list, the unborn rate sums, `climate_outlook_at_least_one`, `CLIMATE_OUTLOOK_FOOTPRINT`), `climate/systems` (system state, kinds, `CLIMATE_HURRICANE_STORM_VMAX`), `climate/weather/week` (`CLIMATE_STORM_DEPTH_HPA`), `run/src/calendar/`, `arena`, `types`, glibc's libm.
