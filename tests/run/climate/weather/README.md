# tests/run/climate/weather

**Purpose:** ctest `run_climate_weather`. Tests `run/src/climate/weather/` and its part `week/` (plan 004 phase 07): the three noises' raw spread measured over 100,000 points and held to the pinned numbers within 3%; the weekly update with the master plan's spring melt (melt 84, evaporation 4.2, snow 36, soil 150, runoff 84.8); ice day by day (12 cm, and never below 0); 100,000 random weeks inside the soil bucket and conserving water to 0.01 mm; every row of the day-kind table and pairs that match two rows; a day the same bytes on a fresh climate and on one that has answered other questions first, in the other order; spells (the spell noise 1 day apart correlated over 0.9, 30 days apart under 0.3, over 1000 places and 10 years); heat waves (days over 1.5 σ between 2% and 12% over 1000 inland places and 10 years, and whole summer days at one place); the 24 hourly rates of `climate_weather_at` adding up to the day's rain within 0.001 mm; snow building over 50 mm in winter and gone from day 170 to 300 at 45° N, 600 high, 1200 blocks inland, three years running; snow lying all year at 75° N on the coast.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main, and the job workers that build climate tiles.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts, `terrain` (the hash streams), `job`, `thread`, `tests/run/climate/fixtures/` (synthetic grounds). No graphics card.
