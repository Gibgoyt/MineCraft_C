# 004-ahmed-bohemoth-climate-system — phase 11: Tuning and cost

**Status:** not started   (updated 2026-11-04)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then sections 4 to 8 and 10 to 12 of the master plan, `plans/004-ahmed-bohemoth-climate-system.md`. The master plan is the source of every number here. Where this file and the master plan disagree, the master plan wins, and the disagreement goes in **Issues found**.

## Goal

Every constant the climate system uses, from sections 4 to 8 of the master plan, lives in exactly one place in the code, with a name and a unit, and a test pins its value. Each one is listed in the table below, with its value, unit, section, what it tunes and where it lives. Where Ahmed changed a value by playing, the table shows the plan's value, the new value and why. Every bench of plan 004 has been run again on a release build, and its result is written next to its target, with misses stated plainly. Four measurements have been made over 8 seeds, including Ahmed's `2198175988018515243`, and recorded: climate class shares, hurricanes a season per warm coast, thunderstorm days a year by class, and how well the outlook's chances match what happened.

You can tell it is done without the todo list: the **Results** tables below have a number in every cell, and `run_climate_constants` passes.

## Where this fits

The vision file says the climate is worked out once, from the seed, and every later plan reads it: vegetation reads temperature and rain normals, settlements read where it is pleasant to live, the economy reads seasons, harvests and floods, and the player gets tasks from floods (vision file, Part 1). **Those plans will be built on these numbers.** If the climate's constants are scattered, unnamed or untested, the next plan cannot change one safely, and cannot tell whether a strange economy comes from the economy or from a rain rate nobody checked. So this phase makes the climate's numbers a single, visible, measured set, before anything is built on it.

It also proves the cost. The vision is an infinite world that keeps running smoothly with settlements and caravans added on top later. The climate must leave room for that: chunk generation must not wait on it, the sim's tick must stay inside its budget, and the frame rate must not drop. This phase measures each.

What this phase must **not** do:

- No new features, layers, weather kinds or settings. Only names, tests, measurements and the value changes Ahmed asks for.
- No change to the master plan file. It stays verbatim (`plans/README.md` rule 1). A changed value is recorded here, in the table and in **Issues found**.
- No change to terrain, to make the climate numbers look better. Terrain is not this plan's (vision file, rule 3).
- No vegetation, geology, settlement or economy work, even where a measurement shows they would help (a desert class where the biome is forest, for example: master plan 2.6 says that mismatch is expected).
- No tuning by an agent alone. An agent measures and proposes. **Ahmed decides** every value change.

## Ahmed's words

- "please drive a probability thing! but do not drive the actual physics! just create general code please that will sort of mimic it"
- "A single year is 365 days, altitude, distance from the ocean or any considerably large water bodies must somehow affect this hard-coded at generation time so we don't waste runtime compute"
- "physics must not be simulated to generate a hurricane but they should spawn! areas need hurricane and monsoon season please"
- "3 different ocean currents/temperatures would be nice"
- On the hurricane count (master plan Q5): start at about 7 a season off a warm coast, "tune by playing".

## Background, explained

### What a constant is, and why it has one home

A **constant** is a number written into the program that does not change while it runs: the cooling with height, 0.012 °C a block, or the hurricane spawn rate, 0.02 a day. The master plan has well over a hundred of them.

If a number is written in two places, someone will one day change one and not the other, and the two halves of the program will disagree without any error. So the repo's rule (vision file, rule 5) is: **every constant has one home**. In C that is an `enum` or a `#define` in one header, with a comment giving its unit, and every use refers to that name. A **literal** is a number written straight into code, like `0.012f32`. After this phase, the only literals allowed in `run/src/calendar/` and `run/src/climate/` are 0, 1, 2, -1, one half, and the parts of a formula that are pure maths, such as $\pi$ or the 3 and 2 of a smoothstep: anything else is a named constant.

A **knot table** is a constant too: a short list of (latitude, value) points a smooth curve passes through (master plan 6.6). It lives in one `static const` array in one file, with its unit in a comment.

### Units

Every constant states its unit, because a number without a unit cannot be checked. This plan's units:

| Unit | Meaning |
|---|---|
| block | 1 metre. Distances and heights |
| blocks a second | 1 metre a second. Wind |
| °C | Temperature |
| hPa | Hectopascal, air pressure. Sea level averages 1013 |
| mm | Millimetres of water: rain, snow as water, soil water, runoff |
| day | One game day, 24000 ticks, 20 minutes of play (`run/include/game/index.h:34`) |
| tick | 1/20 of a second (`config/machine.yaml`, `tick_hz: 20`) |
| degrees | Latitude and angles |
| per square per day | A spawn rate $\lambda$: the average number of new systems in one spawn square in one day |
| ratio | No unit: a factor or a share |

### Tuning, and why it is measured

**Tuning** is changing a constant so the game feels right: for example, fewer hurricanes because seven a season is too many to enjoy. Ahmed tunes by playing. But a feeling from one world can mislead: one seed may have a coast that happens to catch every storm. So every tuning change is checked by **measuring** over 8 seeds, and the numbers are recorded before and after.

### Averages over seeds, and how sure they are

A **seed** is the number a world grows from. Different seeds give different coasts, mountains and storms. A **share** is a count divided by a total: if 120 000 of 400 000 land cells are Desert, the Desert share is $120\,000 / 400\,000 = 30\%$. Over 8 seeds the share reported is the mean of the 8, with the smallest and largest, so a strange seed shows.

Counts of rare events, like hurricanes, wobble from year to year. For events that happen at random at a steady rate (which is how the plan spawns them: Poisson spawning, master plan section 1), the count in a period has a **standard deviation** of about the square root of its mean. So:

$$\text{70 hurricanes in 10 seasons} \Rightarrow 7.0 \pm \frac{\sqrt{70}}{10} = 7.0 \pm 0.84 \text{ a season.}$$

That is why the measurements run 10 years: one season's 7 could easily be 4 or 10 by chance.

**Worked example: hurricanes a season per warm coast, from the plan.** A warm-current coast at 15° north, with $s_{\text{hur}}$ near 1 for about 120 days. The rate is $\lambda = 0.02\,s_{\text{hur}}$ per 16 km square per day (7.2), so one square gets $0.02 \times 120 = 2.4$ a season, and a coast with three squares of sea in front of it gets $3 \times 2.4 = 7.2$. The measurement checks what the code really gives. If it gives 3, something is wrong in the code, not in the plan, and that is an issue for phase 06.

### Calibration of chances

The outlook gives chances: "70% chance of rain on day 2". A forecaster is **well calibrated** if, of all the times it said 70%, it rained about 70% of the time. To measure it, every forecast the outlook makes over 10 years is put in a **bin** by its chance: 0 to 10%, 10 to 20%, and so on. For each bin, count how often the event really happened.

**Worked example.** Over 10 years at 200 places, the outlook said between 60% and 80% for "rain on day 2" 5000 times, with a mean of 70.2%. It rained on 3410 of those days: $3410 / 5000 = 68.2\%$. The plan's test (phase 07) wants events given 70% to happen between 60% and 80% of the time: this passes. If it had happened 85% of the time, the outlook would be **under-confident** (it should have said higher). At 55%, **over-confident**. How sure is 68.2%? For a share $f$ of $n$ trials the standard error is

$$\sqrt{\frac{f(1-f)}{n}} = \sqrt{\frac{0.682 \times 0.318}{5000}} = 0.0066 = 0.66\%,$$

so 68.2% is good to about 1.3% either way (two standard errors). A bin with only 20 forecasts would be good to only about 20%, so bins with fewer than 100 forecasts are reported but not judged.

### What a bench measures

A **bench** is a program under `bench/` that times one thing, built with the game but not run by ctest (`cmake/targets.cmake:13`). It runs the thing many times on a **release** build (debug code is not optimised, so its times mean nothing: `bench/terrain/README.md`), with nothing else using the CPU, which is why every bench runs through `tools/test-or-build.sh`. It prints the time against its target. A miss prints `MISSED` and does not fail the run (`bench/world/README.md`): the targets are goals to measure, and a failing command would hide the rest of the table.

Times are given as the **median** (half the runs were faster) and the **p99** (99% of runs were faster), and the **worst**. The median says what is usual, and the p99 and worst say whether there are hitches a player would feel.

This machine's CPU heat changes the numbers: plan 001 phase 17 found two targets met from a cool CPU and missed straight after other heavy runs (`plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-17-performance-hardening.md`, issues). So each bench is run **twice**: first from a cool CPU (nothing heavy for 5 minutes before), then straight after the others. Both numbers are recorded.

### Determinism, again

The plan holds every climate output to the same bytes on 1 worker or 16 (master plan section 10). A tuning change can break that, if it adds a sum whose order depends on which worker ran first. So every tuning change is followed by the determinism tests of the phase that owns the constant.

## Design

### Where constants live

One header per part, the one that uses the constant. If two parts use it, it goes in the lower one (the one that is worked out first, master plan section 3), and the higher one includes that header.

| Part (master plan section 1) | Header | Sections |
|---|---|---|
| Calendar | `run/include/calendar/index.h` | 4 |
| Climate: latitude and sun | the header phase 02 made under `run/include/climate/` | 5 |
| Climate: cells, currents, pressure, wind, temperature, rain, seasons | the headers phases 03 to 05 made under `run/include/climate/` | 6 |
| Weather systems | `run/include/climate/systems/index.h` | 7.2, 7.3 |
| Weather | `run/include/climate/weather/index.h` | 7.4, 7.5 |
| Outlook | `run/include/climate/outlook/index.h` | 8 |

Each constant keeps the form its phase gave it. Integer constants go in an `enum : uint32_t` (or `int32_t`). Floating constants are either `static const float32_t NAME = 0.012f32;` in the header, as phases 03 and 04 write them, or `#define NAME 0.012f32`, as `run/include/game/index.h:35` writes `GAME_DAY_SECONDS`. Do not convert one form to the other just for this phase. Each has a comment: what it is, its unit, and the master plan section. For example:

```c
/* run/include/climate/<part>/index.h: the form every constant takes */
static const float32_t CLIMATE_LAPSE_C_PER_BLOCK = 0.012f32; /* °C lost per block of height: twice the real rate, as decided. 6.6 */
```

### The constant table

Every constant in sections 4 to 8 of the master plan. **Name** is the name the phase that owns it gave, marked "(phase NN)", where that phase's file names it; the others are this file's proposals. Either way, check the code: if the code uses another name, keep the code's, and write the real name and the header it lives in, in the **Home** column. Phases 03, 04 and 07 also have constants of their own beyond the master plan's ("this phase" or "this file" in their tables, such as `CLIMATE_SEA_AREA_MIN_CELLS`, `CLIMATE_CURRENT_WEAK`, `CLIMATE_HEMISPHERE_BLEND_DEG`, `CLIMATE_RAIN_NOISE_GAIN`, `CLIMATE_FOG_CALM_WIND`, `CLIMATE_FLOOD_BANKFULL`): add each as a row in its section when filling the Home column, so this table has every climate number. A constant the code turns out not to need (because a phase built it another way) stays in the table with "not used" and why, so nothing silently disappears.

The **Tuned** column starts empty. When Ahmed changes a value, write the new value there, and the date and reason in **Issues found**.

#### Calendar (section 4)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CALENDAR_TICKS_PER_DAY` | 24000 | ticks | 0, 4 | Length of a day. Existing: `GAME_DAY_SECONDS` 1200 s at 20 Hz, `run/include/game/index.h:34`. Use that, do not repeat it | | |
| `CALENDAR_DAYS_PER_YEAR` | 365 | days | 4 | Length of a year | | |
| `CALENDAR_DAYS_PER_WEEK` | 7 | days | 4 | How often the census runs | | |
| `CALENDAR_FIRST_DAY` ($D_0$) | 79 | days | 4 | A new world's first day: the spring equinox (Q2) | | |
| `CALENDAR_DAY_OFFSET_TICKS` ($t_0$) | set in phase 01 | ticks | 4 | Lines up day boundaries with 06:00 on the clock. Not given as a number in the master plan: record phase 01's value | | |
| `CALENDAR_CENSUS_HOUR` | 6 | hours on the clock | 2.2, 4.2 | When the census fires: 06:00 everywhere | | |
| `CLIMATE_WEEK_CATCHUP_FULL` (phase 07) | 52 | weeks | 4.2 | Skipped weeks worked out in full; past this, fast mode keeps only snow and soil water | | |
| `CALENDAR_SPRING_DAY` | 79 | day of year | 4.1, 5.2 | Northern spring start, and the equinox | | |
| `CALENDAR_SUMMER_DAY` | 170 | day of year | 4.1, 5.2 | Northern summer start; the sun furthest north | | |
| `CALENDAR_AUTUMN_DAY` | 262 | day of year | 4.1, 5.2 | Northern autumn start; the sun crosses the equator | | |
| `CALENDAR_WINTER_DAY` | 353 | day of year | 4.1, 5.2 | Northern winter start; the sun furthest south | | |
| `CALENDAR_SEASON_PARTS` | 3 | ratio | 4.1 | Early, mid and late: each season in equal thirds (phase 10) | | |

#### Latitude and the sun (section 5)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_TROPIC_DEG` | 23.5 | degrees | 5.1, 5.2 | Latitude of the tropics, and the sun's yearly swing | | |
| `CLIMATE_TROPIC_Y` | 10000 | blocks | 5.1 | Distance from the equator to the tropic: sets the whole latitude scale | | |
| `CLIMATE_POLE_Y` ($Y$) | 38298 | blocks | 5.1 | Equator to pole, $10\,000 \times 90 / 23.5$. Worked out from the two above, not typed | | |
| `CLIMATE_LATITUDE_PERIOD_Y` | 153191 | blocks | 5.1 | $4Y$: the pattern repeats. Worked out, not typed | | |
| `CLIMATE_POLAR_CIRCLE_DEG` | 66.5 | degrees | 4.1, 5.2 | $90 - 23.5$: polar night and midnight sun beyond it. Worked out | | |
| `CLIMATE_DECLINATION_DEG` | 23.5 | degrees | 5.2 | Amplitude of $\delta(d)$. The same as the tropic: one constant, used for both | | |
| `CLIMATE_EQUINOX_DAY` | 79 | day of year | 5.2 | Phase of $\delta(d)$. The same as `CALENDAR_SPRING_DAY` | | |
| `CLIMATE_HOURS_PER_DAY` | 24 | hours | 5.2 | Day length scale, and the hour angle | | |

#### Height, land and sea (6.1, 6.2)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_CELL_BLOCKS` (phase 03) | 64 | blocks | 1, 6.2, 6.9 | Cell size: one climate sample, and the land mask's spacing | | |
| `CLIMATE_TILE_CELLS` (phase 03) | 16 | cells | 1 | Tile side: 1024 blocks | | |
| `CLIMATE_LAKE_LARGE_CELLS` (phase 03) | 2 km², 489 cells | km², as cells | 6.2 | A lake this big counts as water for moisture | | |
| `CLIMATE_MARGIN_CELLS` (phase 03) | 8192 blocks, 128 cells | blocks, as cells | 6.2 | Margin round a tile for the distance transform (8 km) | | |
| `CLIMATE_SEA_DISTANCE_MAX` (phase 03) | 8192 | blocks | 6.2, 6.9 | Sea distance cap | | |
| `CLIMATE_KAPPA_SCALE` (phase 03) | 1500 | blocks | 6.2 | Continentality $\kappa = 1 - e^{-d_s/1500}$: how fast the sea's influence fades inland | | |

#### Ocean currents (6.3)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_CURRENT_REACH` (phase 04) | 600 | blocks | 6.3 | Sea cells this close to land have a current; its strength fades over this offshore | | |
| `CLIMATE_CURRENT_SIDE` (phase 04) | 0.5 | ratio | 6.3 | $\lvert n_x \rvert$ above this makes a west or east coast | | |
| `CLIMATE_CURRENT_BANDS_DEG` (phase 04) | 15, 45, 65 | degrees | 6.3 | Band edges of the current table | | |
| `CLIMATE_CURRENT_WARM_C` (phase 04) | +4 | °C | 6.3 | $C$ for a warm current | | |
| `CLIMATE_CURRENT_COLD_C` (phase 04) | -5 | °C | 6.3 | $C$ for a cold current | | |
| `CLIMATE_CURRENT_LAND_SCALE` (phase 04) | 600 | blocks | 6.3 | $\Delta T_{\text{current}} = C\,e^{-d_s/600}$ on land | | |
| `CLIMATE_SEA_PEAK_DAY` (phase 04) | 215 | day of year | 6.3 | Warmest sea in the north, 45 days after the land | | |

#### Pressure (6.4)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `climate_pressure_knots` | 0°: 1008, 15°: 1012, 30°: 1020, 45°: 1013, 60°: 1000, 75°: 1010, 90°: 1020 | hPa by $\lvert\psi\rvert$ | 6.4 | Background pressure belts | | |
| `CLIMATE_HEAT_LOW_HPA_PER_C` (phase 04) | -0.6 | hPa per °C | 6.4 | Heat lows and cold highs: the monsoon's strength | | |

#### Wind (6.5)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_WIND_SHIFT` (phase 04) | 0.5 | ratio | 6.5 | Belts follow the sun half as far | | |
| `CLIMATE_WIND_LAG_DAYS` (phase 04) | 30 | days | 6.5 | ... and a month late | | |
| `CLIMATE_TRADE_DIR` (phase 04) | $(-1, -0.4)$ | unit-less $(u, v)$ | 6.5 | Trade winds' direction, $v$ times $\operatorname{sgn}\psi$ | | |
| `CLIMATE_TRADE_SPEED` (phase 04) | 6 | blocks a second | 6.5 | Trade winds' speed | | |
| `CLIMATE_WESTERLY_DIR` (phase 04) | $(+1, +0.3)$ | | 6.5 | Westerlies' direction | | |
| `CLIMATE_WESTERLY_SPEED` (phase 04) | 8 | blocks a second | 6.5 | Westerlies' speed | | |
| `CLIMATE_POLAR_DIR` (phase 04) | $(-1, -0.2)$ | | 6.5 | Polar easterlies' direction | | |
| `CLIMATE_POLAR_SPEED` (phase 04) | 5 | blocks a second | 6.5 | Polar easterlies' speed | | |
| `CLIMATE_BELT_EDGES_DEG` (phase 04) | 30, 60 | degrees | 6.5 | Where one belt gives way to the next | | |
| `CLIMATE_BELT_BLEND_DEG` (phase 04) | 6 | degrees | 6.5 | Width of the smoothstep blend between belts | | |
| `CLIMATE_DOLDRUMS_DEG` (phase 04) | 5 | degrees | 6.5 | Doldrums half-width | | |
| `CLIMATE_DOLDRUMS_SPEED` (phase 04) | 0.4 | ratio | 6.5 | Speed in the doldrums | | |
| `CLIMATE_HORSE_SPEED` (phase 04) | 0.5 | ratio | 6.5 | Speed near 30°. The plan gives no width: phase 04 chose `CLIMATE_HORSE_DEG` 2° of full calm and `CLIMATE_CALM_RAMP_DEG` 3°, which are pinned too | | |
| `CLIMATE_PRESSURE_WIND_K` ($k_p$, phase 04) | 900 | blocks² per hPa per second | 6.5 | Wind from pressure differences | | |
| `CLIMATE_PRESSURE_TURN_DEG` (phase 04) | 70 | degrees | 6.5 | Turn from straight downhill in pressure, clockwise north | | |
| `CLIMATE_ALTITUDE_SCALE` (phase 04) | 800 | blocks | 6.5 | Speed $\times (1 + z/800)$ | | |
| `CLIMATE_ALTITUDE_MAX` (phase 04) | 2.5 | ratio | 6.5 | ... at most | | |
| `CLIMATE_STEER` (phase 04) | 0.7 | ratio | 6.5 | How much steep ground turns the wind along it | | |

#### Temperature (6.6)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `climate_temperature_knots` ($\bar T_{\text{lat}}$) | 0°: 27, 15°: 26, 30°: 20, 45°: 11, 60°: 0, 75°: -14, 90°: -24 | °C by latitude | 6.6 | Yearly mean at sea level | | |
| `CLIMATE_LAPSE_C_PER_BLOCK` ($\Gamma$) | 0.012 | °C per block | 6.6 | Cooling with height, as decided | | |
| `CLIMATE_BASIN_C_PER_BLOCK` | 0.025 | °C per block | 2.1, 6.6 | Below-sea-level heat $B$, on land not under sea | | |
| `climate_swing_sea_knots` ($A_{\text{sea}}$) | 0°: 1, 30°: 3, 60°: 6, 90°: 10 | °C | 6.6 | Half the yearly swing over sea | | |
| `climate_swing_land_knots` ($A_{\text{land}}$) | 0°: 2, 30°: 9, 60°: 18, 90°: 22 | °C | 6.6 | Half the yearly swing deep inland | | |
| `CLIMATE_SUMMER_PEAK_DAY` | 170 | day of year | 6.6 | Phase of the temperature year before the lag | | |
| `CLIMATE_LAG_BASE_DAYS` | 25 | days | 6.6 | $\ell = 25 + 20(1 - \kappa)$ | | |
| `CLIMATE_LAG_SEA_DAYS` | 20 | days | 6.6 | ... the sea's extra lag | | |
| `CLIMATE_DAILY_RANGE_BASE_C` | 4 | °C | 6.6 | Daily range $(4 + 8\kappa)(1 - 0.6r)$ | | |
| `CLIMATE_DAILY_RANGE_INLAND_C` | 8 | °C | 6.6 | ... inland part | | |
| `CLIMATE_DAILY_RANGE_RAIN_CUT` | 0.6 | ratio | 6.6 | ... cut by the share of rainy days | | |

#### Rain (6.7)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `climate_rain_knots` ($P_{\text{belt}}$) | 0°: 2400, 10°: 1800, 20°: 700, 30°: 300, 40°: 800, 50°: 1000, 60°: 800, 75°: 350, 90°: 150 | mm a year by $\lvert\psi\rvert$ | 6.7 | Rain belts | | |
| `CLIMATE_UPWIND_STEPS` | 16 | steps | 6.7 | Upwind walk steps | | |
| `CLIMATE_UPWIND_STEP_BLOCKS` | 128 | blocks | 6.7 | Upwind step length (16 × 128 = 2 km) | | |
| `CLIMATE_UPWIND_MAX_BLOCKS` | 8192 | blocks | 6.7 | The walk goes on over the coarse land mask to 8 km | | |
| `CLIMATE_MOISTURE_FLOOR` | 0.15 | ratio | 6.7 | $M$ never below this | | |
| `CLIMATE_MOISTURE_SCALE_BLOCKS` | 3000 | blocks | 6.7 | $e^{-d_{\text{up}}/3000}$: how fast air dries over land | | |
| `CLIMATE_MOISTURE_WARM` | 1.25 | ratio | 6.7 | Air that left over a warm current | | |
| `CLIMATE_MOISTURE_COLD` | 0.4 | ratio | 6.7 | Within 800 blocks of a cold current | | |
| `CLIMATE_MOISTURE_COLD_BLOCKS` | 800 | blocks | 6.7 | ... that distance | | |
| `CLIMATE_OROGRAPHIC_SLOPE` | 8 | ratio | 6.7 | $1 + 8 s_w$: windward rain | | |
| `CLIMATE_OROGRAPHIC_SLOPE_BLOCKS` | 256 | blocks | 6.7 | $s_w$ measured over this | | |
| `CLIMATE_RAIN_SHADOW_BLOCKS` | 400 | blocks | 6.7 | $\max(0, h_{\text{up}} - z)/400$: lee drying | | |
| `CLIMATE_OROGRAPHIC_MIN`, `_MAX` | 0.15, 2.5 | ratio | 6.7 | Clamp of $O$ | | |
| `CLIMATE_SNOW_ALL_RAIN_C` (phase 07) | 1 | °C | 6.7 | All rain above this | | |
| `CLIMATE_SNOW_ALL_SNOW_C` (phase 07) | -2 | °C | 6.7 | All snow below this; share $(1 - T)/3$ between | | |

#### Storm seasons, cells and classes (6.8 to 6.10)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_HURRICANE_COAST_BLOCKS` | 3000 | blocks | 6.8 | Coasts this close to sea share its hurricane season | | |
| `CLIMATE_HURRICANE_SEA_C` | 26.5 | °C | 6.8 | Sea temperature hurricanes need | | |
| `CLIMATE_HURRICANE_RAMP_C` | 2 | °C | 6.8 | $s_{\text{hur}}$ rises from 0 to 1 over this | | |
| `CLIMATE_HURRICANE_LAT_MIN_DEG`, `_MAX_DEG` | 5, 25 | degrees | 6.8 | Where hurricanes can form | | |
| `CLIMATE_HURRICANE_SEASON_MIN` | 0.25 | ratio | 4.1, 6.8 | "Hurricane season" while $s_{\text{hur}}$ is above this | | |
| `CLIMATE_MONSOON_RAIN_RATIO` | 3 | ratio | 6.8 | Wet half's rain over dry half's, for a monsoon | | |
| `CLIMATE_FIT_DAYS` (phase 04) | 12 | days | 6.9 | Days rain and wind are worked out on, to fit the harmonics | | |
| `CLIMATE_HARMONICS` | 2 | waves | 6.9 | Waves per year in the fit | | |
| `CLIMATE_WARP_BLOCKS` (phase 03) | 32 | blocks | 6.9 | Warp before the bilinear blend: how much borders wobble | | |
| `CLIMATE_HURRICANE_DAY_SCALE` | 4 | days a step | 6.9 | `hurricane_days` stores day / 4 in a byte | | |
| `CLIMATE_DRY_THRESHOLD_PER_C` | 20 | mm per °C | 6.10 | $P_{\text{th}} = 20\,\bar T + 140$ | | |
| `CLIMATE_DRY_THRESHOLD_BASE` | 140 | mm | 6.10 | ... base | | |
| `CLIMATE_CLASS_TUNDRA_C` | 10 | °C | 6.10 | Tundra below this warmest month; Ice below 0 | | |
| `CLIMATE_CLASS_TROPICAL_C` | 18 | °C | 6.10 | Coldest month for Rainforest, Monsoon, Savanna | | |
| `CLIMATE_CLASS_RAINFOREST_MM` | 60 | mm | 6.10 | Driest month for Rainforest | | |
| `CLIMATE_CLASS_TEMPERATE_C` | -3 | °C | 6.10 | Coldest month: above is Mediterranean or Temperate | | |
| `CLIMATE_CLASS_CONTINENTAL_C` | 22 | °C | 6.10 | Warmest month: Continental at or above, Boreal below | | |

#### Weather systems (7.2, 7.3)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_HURRICANE_SQUARE_BLOCKS` | 16384 | blocks | 7.2 | Spawn square (16 km) | | |
| `CLIMATE_HURRICANE_RATE` | 0.02 | per square per day, times $s_{\text{hur}}$ | 7.2 | **How many hurricanes.** Q5: tune by playing | | |
| `CLIMATE_HURRICANE_RADIUS_BLOCKS` | 1500 to 4000 | blocks | 7.2 | Size | | |
| `CLIMATE_HURRICANE_EYE_BLOCKS` | 80 to 150 | blocks | 7.2 | Eye radius | | |
| `CLIMATE_HURRICANE_HPA` | -30 to -90 | hPa | 7.2 | Pressure dip, so strength | | |
| `CLIMATE_HURRICANE_LIFE_DAYS` | 4 to 10 | days | 7.2 | Life | | |
| `CLIMATE_HURRICANE_LAND_DECAY` | 0.5 | ratio per day | 7.2 | Weakening over land | | |
| `CLIMATE_HURRICANE_DECAY_SEA_C` | 26 | °C | 7.2 | Weakens over sea below this | | |
| `CLIMATE_LIFE_GROW`, `_FADE` | 1/3, 1/3 | share of life | 7.2 | Life curve | | |
| `CLIMATE_LOW_SQUARE_BLOCKS` | 16384 | blocks | 7.2 | Spawn square | | |
| `CLIMATE_LOW_LAT_MIN_DEG`, `_MAX_DEG` | 35, 65 | degrees of $\lvert\psi\rvert$ | 7.2 | Where lows form | | |
| `CLIMATE_LOW_RATE_WINTER`, `_SUMMER` | 0.08, 0.04 | per square per day | 7.2 | How many lows | | |
| `CLIMATE_LOW_RADIUS_BLOCKS` | 3000 to 7000 | blocks | 7.2 | Size | | |
| `CLIMATE_LOW_HPA` | -10 to -30 | hPa | 7.2 | Depth | | |
| `CLIMATE_LOW_LIFE_DAYS` | 3 to 6 | days | 7.2 | Life | | |
| `CLIMATE_LOW_SPEED_BLOCKS_PER_DAY` | 6000 | blocks a day | 7.2 | Eastward speed | | |
| `CLIMATE_FRONT_LENGTH_BLOCKS` | 4000 to 8000 | blocks | 7.2 | Cold front length | | |
| `CLIMATE_FRONT_CELL_FACTOR` | 5 | ratio | 7.2 | Cells on a front, against the usual rate (the formula's $1 + 4 f_{\text{front}}$) | | |
| `CLIMATE_HIGH_SQUARE_BLOCKS` | 24576 | blocks | 7.2 | Spawn square (24 km) | | |
| `CLIMATE_HIGH_LAT_MIN_DEG`, `_MAX_DEG` | 20, 45 | degrees of $\lvert\psi\rvert$ | 7.2 | Where highs form (and continental interiors in winter) | | |
| `CLIMATE_HIGH_RATE` | 0.03 | per square per day | 7.2 | How many highs | | |
| `CLIMATE_HIGH_RADIUS_BLOCKS` | 6000 to 12000 | blocks | 7.2 | Size | | |
| `CLIMATE_HIGH_HPA` | +8 to +20 | hPa | 7.2 | Strength | | |
| `CLIMATE_HIGH_LIFE_DAYS` | 6 to 14 | days | 7.2 | Life | | |
| `CLIMATE_HIGH_SPEED_BLOCKS_PER_DAY` | 2000 | blocks a day | 7.2 | Eastward speed | | |
| `CLIMATE_HIGH_RAIN_FACTOR` | 0.2 | ratio | 7.2, 7.3, 7.2.1 | Rain under a high, and cells under one ($0.2^{f_{\text{high}}}$) | | |
| `CLIMATE_CELL_SQUARE_BLOCKS` | 1024 | blocks | 7.2 | Thunderstorm cell spawn square (1 km) | | |
| `CLIMATE_CELL_RADIUS_BLOCKS` | 150 to 400 | blocks | 7.2 | Cell size | | |
| `CLIMATE_CELL_HPA` | -1 to -3 | hPa | 7.2 | Cell pressure dip | | |
| `CLIMATE_CELL_LIFE_HOURS` | 1 to 4 | clock hours | 7.2 | Cell life | | |
| `CLIMATE_CELL_RATE` | 3 | per km² per day | 7.2.1 | **How many thunderstorms** | | |
| `CLIMATE_CELL_TEMP_C` | 18 | °C | 7.2.1 | Cells need a day warmer than this | | |
| `CLIMATE_CELL_TEMP_RAMP_C` | 10 | °C | 7.2.1 | ... and reach full rate 10 above it | | |
| `CLIMATE_CELL_MONSOON_FACTOR` | 2 | ratio | 7.2.1 | $1 + 2 f_{\text{monsoon}}$ | | |
| `CLIMATE_CELL_FRONT_FACTOR` | 4 | ratio | 7.2.1 | $1 + 4 f_{\text{front}}$ | | |
| `CLIMATE_CELL_START_HOURS` | 13 to 19 | clock hours | 7.2.1 | Cells start in the afternoon | | |
| `CLIMATE_TRACK_STEP_DAYS` | 0.25 | days | 7.3 | Track step | | |
| `CLIMATE_HURRICANE_SPEED_BLOCKS_PER_DAY` | 3500 | blocks a day | 7.3 | With the trades | | |
| `CLIMATE_HURRICANE_TURN_DEG` | 20 to 28 | degrees of $\lvert\psi\rvert$ | 7.3 | Where it turns poleward | | |
| `CLIMATE_HURRICANE_TURN_DAYS` | 2 | days | 7.3 | How long the turn takes | | |
| `CLIMATE_HURRICANE_LATE_SPEED_BLOCKS_PER_DAY` | 6000 | blocks a day | 7.3 | With the westerlies, after turning | | |
| `CLIMATE_SYSTEM_REACH_BLOCKS` | 35000 | blocks | 7.3 | Longest track: how far back to look for systems | | |
| `CLIMATE_SYSTEM_LIFE_MAX_DAYS` | 14 | days | 7.3 | Longest life: how long back to look | | |
| `CLIMATE_WIND_PROFILE_EXPONENT` | 0.6 | ratio | 7.3 | $v \propto (r_m / r)^{0.6}$ outside $r_m$ | | |
| `CLIMATE_HURRICANE_VMAX` | 33 at -30 hPa, 70 at -90 hPa | blocks a second | 7.3 | Hurricane wind from its dip | | |
| `CLIMATE_SYSTEM_RM_SHARE` | 0.5 | share of $R_i$ | 7.3 | Lows' and highs' strongest wind radius | | |
| `CLIMATE_SYSTEM_VMAX_PER_HPA` | 0.4 | blocks a second per hPa | 7.3 | Lows' and highs' strongest wind | | |
| `CLIMATE_HURRICANE_RAIN_MM_H` | 40 | mm an hour | 7.3 | Eyewall rain | | |
| `CLIMATE_LOW_RAIN_MM_H` | 1 to 4 | mm an hour | 7.3 | Rain shield | | |
| `CLIMATE_CELL_RAIN_MM_H` | 20 to 60 | mm an hour | 7.3 | Cell rain, for under an hour at a point | | |

#### Daily weather and the weekly state (7.4, 7.5)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_SPELL_BLOCKS` (phase 07) | 2500 | blocks | 7.4 | Size of day-to-day temperature patches | | |
| `CLIMATE_SPELL_DAYS` (phase 07) | 10 | days | 7.4 | Length of spells | | |
| `CLIMATE_SIGMA_T_COAST` (phase 07) | 2 | °C | 7.4 | $\sigma_T = 2 + 3\kappa$ | | |
| `CLIMATE_SIGMA_T_INLAND_EXTRA` (phase 07) | 3 | °C | 7.4 | ... inland part | | |
| `CLIMATE_HIGH_ANOMALY_SIGMAS` (phase 07) | 0.5 | share of $\sigma_T$ | 7.4 | Highs: heat waves in summer, cold snaps in winter | | |
| `CLIMATE_CLOUD_RANGE_CUT` (phase 07) | 0.6 | ratio | 7.4 | Clouds of lows and hurricanes cut the daily range | | |
| `CLIMATE_SYSTEM_RAIN_SHARE_MID` (phase 07) | 0.5 | ratio | 7.4 | $f_{\text{sys}}$ in the mid-latitudes | | |
| `CLIMATE_SYSTEM_RAIN_SHARE_TROPICS` (phase 07) | 0.3 | ratio | 7.4 | $f_{\text{sys}}$ in the tropics | | |
| `CLIMATE_RAIN_CHECK_TOLERANCE` | 0.15 | ratio | 7.4 | Long-run rain within 15% of normal (a test bound) | | |
| `CLIMATE_DAY_RAIN_MM` (phase 07) | 1 | mm | 7.4 | A day of rain or snow | | |
| `CLIMATE_STORM_DEPTH_HPA` (phase 07) | 20 | hPa | 7.4 | A low this deep is a Storm | | |
| `CLIMATE_STORM_WIND` (phase 07) | 15 | blocks a second | 7.4 | Wind this strong is a Storm | | |
| `CLIMATE_HEAT_WAVE_SIGMAS` (phase 07) | 1.5 | share of $\sigma_T$ | 7.4 | Heat wave | | |
| `CLIMATE_DUST_WIND` (phase 07) | 12 | blocks a second | 7.4 | Dust storm wind | | |
| `CLIMATE_DUST_WEEK_RAIN_MM` (phase 07) | 2 | mm in the week | 7.4 | Dust storm dryness | | |
| `CLIMATE_MELT_MM_PER_DEGREE_DAY` (phase 07) | 4 | mm per °C per day | 7.5 | Snowmelt | | |
| `CLIMATE_EVAPORATION_MM_PER_DEGREE_DAY` (phase 07) | 0.2 | mm per °C per day | 7.5 | Evaporation | | |
| `CLIMATE_SOIL_MM_MAX` (phase 07) | 150 | mm | 7.5 | Soil water held before runoff | | |
| `CLIMATE_ICE_GROW_CM_PER_DEGREE_DAY` (phase 07) | 1.5 | cm per °C per day | 7.5 | Ice growth on still water | | |
| `CLIMATE_ICE_MELT_CM_PER_DEGREE_DAY` (phase 07) | 2 | cm per °C per day | 7.5 | Ice melt | | |
| `CLIMATE_FLOOD_BANKS_FULL` | 1 | ratio | 7.5 | Flood index at which water is over the banks | | |

#### The outlook (section 8)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_OUTLOOK_DAYS` (phase 07) | 7 | days | 8 | How far ahead | | |
| `CLIMATE_OUTLOOK_SIGMA_START` (phase 07) | 200 | blocks | 8 | Track error now, $\sigma(0)$ | | |
| `CLIMATE_OUTLOOK_SIGMA_PER_DAY` (phase 07) | 400 | blocks a day | 8 | Track error growth | | |
| `CLIMATE_OUTLOOK_RAIN_MM` (phase 07) | 6 | mm | 8 | Background rain chance $1 - e^{-\bar P(d)/6}$ | | |
| `CLIMATE_OUTLOOK_SPREAD_SIGMAS` (phase 07) | 1.5 | share of $\sigma_T$ | 8 | Spread on the expected high and low | | |
| `CLIMATE_OUTLOOK_CALIBRATION_LOW`, `_HIGH` | 0.6, 0.8 | ratio | 10 (phase 07) | Events given 70% must happen this often (a test bound) | | |

#### From phases 08 to 10, not in sections 4 to 8

For completeness, so every climate number is in this file:

| Name | Value | Unit | Where | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `GAME_COVER_ICE_ON_CM` (phase 08) | 5 | cm | Master plan phase 08 | Ice blocks placed at the census | | |
| `GAME_COVER_SNOW_ON_MM` (phase 08) | 20 | mm of water | Master plan phase 08 | Snow layer placed at the census | | |
| `PLAYER_WIND_SHARE`, `PLAYER_WIND_THRESHOLD` (phase 08) | 0.04, 10 | drift per block a second; blocks a second | Phase 08 | How hard storms push (Q4: gently) | | |
| `CLIMATE_WARNING_THRESHOLD` | 0.5 | ratio | 9.1, phase 10 | Storm warning, strictly over | | |
| `CLIMATE_WARNING_TICKS` | 200 | ticks | Phase 10 | How often the warning is worked out | | |
| `UI_ATLAS_NOW_REDRAW_MS` | 500 | ms | 9.3, phase 09 | "Now" layers drawn again twice a second | | |
| `UI_ATLAS_SHADE_EXAGGERATION` | 1 | ratio | Phase 09 | Hill shading strength | | |

### `run_climate_constants`: a test that pins them

A NEW ctest, `tests/run/climate/constants/index.c`. For every named constant in the tables, one check that it equals the value in the **Tuned** column if there is one, else the plan's value. For knot tables, every knot. For derived constants (`CLIMATE_POLE_Y`, `CLIMATE_LATITUDE_PERIOD_Y`, `CLIMATE_POLAR_CIRCLE_DEG`), a check that they equal the formula of their sources, so changing the tropic distance moves them all.

Why: a value cannot then change by accident. A deliberate change means changing the constant, this test and this file's table together, which is exactly the record this phase wants. The test's README says so.

It also does a **literal sweep**: it reads every `index.c` under `run/src/calendar/` and `run/src/climate/` as text (through `file_read_whole`, `run/include/file/index.h:62`) and fails on any number literal that is not on a short allowlist: 0, 1, 2, -1 and 0.5; the 3 and 2 of a smoothstep, $3t^2 - 2t^3$; 360 and 180 for degrees; and forms of $\pi$. Array sizes must use a named constant. Each failure names the file and line. The allowlist lives in the test with a comment per entry.

### `bench_climate_survey`: the four measurements

A NEW bench, `bench/climate/survey/index.c` (the program `bench_climate_survey`, beside phase 07's `bench/climate/census/`), release build, no display, no card. For each seed it surveys a **strip** of the world: $x$ from 0 to 16 384 blocks, and $y$ over a whole latitude cycle from the south pole to the north pole, $-38\,298$ to $+38\,298$ blocks. That is $16.4 \times 76.6 = 1255$ km², about 1255 climate tiles, with every latitude band and its coasts. It counts as "generated" every column of that strip, through the climate functions alone (no chunks), so it measures the climate, not what a player happened to explore.

**The 8 seeds.** If 003 phase 8 stage 1 recorded its 8 seeds, use those. Otherwise: `2198175988018515243` (Ahmed's), `5569068473609503046` (`TERRAIN_SEED_DEFAULT`, `0x4D494E4543524146`, `run/include/terrain/index.h:23`), and 1, 2, 3, 4, 5, 6. Write the list used in **Results**.

**1. Climate class shares.** For each seed, the land cells of the strip by class (6.10), as a share of land cells. Report the mean, smallest and largest over the 8 seeds, per class. Also the share of land cells whose biome (`run/src/terrain/biome/`) disagrees with the class's broad kind (desert biome in a non-dry class, and the reverse), for the vegetation plan (master plan 2.6). No target: Ahmed judges by eye against the Atlas.

**2. Hurricanes a season per warm coast.** A **warm coast square** is a 16 km hurricane spawn square with at least one sea cell flagged with a warm current and hurricane season. For 10 years, count hurricanes that spawn in each such square. Report per square per season, and per **coast** (a run of neighbouring warm coast squares along one shore), mean over squares and seeds, with smallest and largest. Compare with the plan's 2.4 per square and about 7 per three-square coast (7.2). Also report how many of them make landfall (enter a land cell while above tropical-storm strength).

**3. Thunderstorm days a year by class.** For 200 land cells per class per seed (fewer if the class has fewer, said so), picked by hash, count days in 10 years whose day kind is Thunderstorm (7.4), divided by 10. Report mean, smallest and largest per class. No target; the plan expects many in Monsoon and Rainforest and almost none in Desert, Ice and Tundra.

**4. Outlook calibration.** At 200 places per seed, picked by hash among land cells, for every census over 10 years (520 weeks), take the outlook (section 8) for each of the 7 days and each event kind (rain, thunderstorms, storms, each named system reaching here). Then work out what really happened, by asking the weather for those days (the bench may read the future; the outlook may not). Bin the chances in tenths. For each bin report: count, mean chance, share that happened, and two standard errors. The plan's bound (phase 07): the 60 to 80% bin happens between 60% and 80% of the time. Report each event kind separately and all together.

The bench prints each table as Markdown, ready to paste into **Results**, and writes a TSV with `--record <file>` like `bench_world` does. It uses all 16 workers, and gives each seed and each measurement to whole jobs, so the numbers are the same bytes on 1 worker or 16. A test of that: `bench_climate_survey --seeds 2 --years 1 --workers 1` and `--workers 16` print the same.

Expected time: tiles 1255 per seed at 100 ms each on one worker is about 8 s on 16 workers; 10 years of weather at the sample cells and places is the larger part. If one seed takes over 10 minutes, the bench says so and the strip width or the years are halved by a flag, and that is recorded.

### The benches and their targets

Every bench plan 004 asked for, plus the two existing ones the climate can slow down. **Bench** names the program as the phase that made it named it: check that phase's file and write the real name here.

| What | Bench | Target | Master plan |
|---|---|---|---|
| One climate tile on one worker | `bench_climate` (`bench/climate/`, phase 03; re-run in phase 04) | under 100 ms | Phase 03, 12 |
| The systems near a point | made in phase 06: write its name here | under 2 ms | 7.3, phase 06 |
| The census for 100 km² of generated land | `bench_climate_census` (`bench/climate/census/`, phase 07; phase 07 runs it on the profile preset) | under 250 ms on 16 workers | Phase 07 |
| Frame rate at render distance 32, before and after weather | `bench_world` (`bench/world/`), profile preset, rows from phase 08 | before and after recorded; the plan 001 target is 120 fps at 1080p | Phase 08, 12 |
| Opening the Atlas over 100 km² | `bench_atlas` (phase 09) | under 100 ms to the first picture | Phase 09 |
| A full Atlas picture, worst layer, 1920 by 1080 | `bench_atlas` | under 60 ms on 16 workers | Phase 09 |
| The Atlas's main-thread frame | `bench_atlas` | under 2 ms | Phase 09 |
| The storm warning at a point | `run_climate_warning` on release (phase 10), or a row in phase 06's bench | under 2 ms | Phase 10 |
| Terrain with climate tiles in use | `bench_terrain` (`bench/terrain/`) | at least 1000 columns a second on 16 workers, as before plan 004 | Plan 001, 12 (chunks wait on tiles) |
| Sim tick at 100 mobs, with climate | `bench_mobs` and `bench_world` `night/walk-mobs` | at most 5 ms | Plan 001 |
| Memory per km² of climate cells | `bench_climate_survey` prints it | 32 KiB a km² (6.9) | 6.9 |

Every bench runs on the release preset, except `bench_world`, which runs on profile, as its README says.

### Results

Filled in by this phase. A blank cell is not done.

**Benches,** each from a cool CPU and straight after the others:

| What | Target | Cool | Hot | Met? |
|---|---|---|---|---|
| Climate tile, 1 worker | 100 ms | | | |
| Systems near a point | 2 ms | | | |
| Census, 100 km², 16 workers | 250 ms | | | |
| Frame rate, RD 32, before weather (`bench_world`, slowest row) | 120 fps | | | |
| Frame rate, RD 32, after weather | 120 fps | | | |
| Atlas first picture, 100 km² | 100 ms | | | |
| Atlas full picture, worst layer | 60 ms | | | |
| Atlas main frame | 2 ms | | | |
| Storm warning | 2 ms | | | |
| Terrain, 16 workers, slowest place | 1000 columns/s | | | |
| Sim tick p99, 100 mobs | 5 ms | | | |
| Climate memory | 32 KiB/km² | | | |

**Seeds used:** (list)

**Climate class shares,** % of land:

| Class | Mean | Smallest | Largest | Biome disagrees, % |
|---|---|---|---|---|
| Ice | | | | |
| Tundra | | | | |
| Desert | | | | |
| Steppe | | | | |
| Rainforest | | | | |
| Monsoon | | | | |
| Savanna | | | | |
| Mediterranean | | | | |
| Temperate | | | | |
| Continental | | | | |
| Boreal | | | | |

**Hurricanes:**

| Measure | Plan | Mean | Smallest | Largest |
|---|---|---|---|---|
| Per warm coast square, a season | 2.4 | | | |
| Per warm coast, a season | about 7 (three squares) | | | |
| Making landfall, a season per coast | — | | | |

**Thunderstorm days a year:** one row per class, as the class table.

**Outlook calibration:** one row per tenth, 0 to 100%, with count, mean chance, happened, two standard errors, for all events together; then the 60 to 80% row for each event kind.

**Long-run rain against normals** (the phase 06 test, re-run on 8 seeds over 10 years): the largest error of any cell, and the share of cells within 15%.

## Owned paths

Edit only these, and this file:

- `tests/run/climate/constants/` (NEW: ctest `run_climate_constants`)
- `bench/climate/survey/` (NEW: `bench_climate_survey`)
- The headers and sources under `run/include/calendar/`, `run/src/calendar/`, `run/include/climate/`, `run/src/climate/`, and `run/include/ui/atlas/`, `run/src/ui/atlas/`: **only** to turn literals into named constants, move a constant to its one home, and change a value Ahmed has decided. No other change
- The tests of those parts, only where a value Ahmed changed moves an expected number or a golden picture, each with a line saying why
- `bench/world/history.tsv`: new lines only, with `--record`
- Each bench's `README.md`: its new results section, as `bench/terrain/README.md` keeps them
- `plans/REPO_STRUCTURE.md`: the new test and bench

## Steps

1. **Read.** The vision file; master plan sections 4 to 8, 10 to 12; every earlier phase file of plan 004, for the names they gave constants, benches and tests, and for every issue they recorded. 001 phase 17's file, for how a target table with misses is written.

2. **Fill the Home column.** For each row of the constant table, find the constant in the code (grep for its value and its plan symbol), and write its real name and header. If it is a literal, note its file and line. If it is in two places, note both. If the code does not use it, write "not used" and why. Commit this table before changing any code, so the audit is on record.

3. **Give every constant one home.** In the headers named in Design, turn each literal into a named constant with its unit and section, and make every use refer to it. Remove duplicates. Values do not change in this step. After each part, run that part's tests and its determinism test: every pinned digest must hold, since no value changed. If one moves, a value did change by mistake: find it.

4. **`run_climate_constants`.** `tests/run/climate/constants/index.c`, `README.md`: one check per constant, the derived checks, and the literal sweep. It must pass on the code as step 3 left it.

5. **`bench_climate_survey`.** `bench/climate/survey/index.c`, `README.md`: the strip, the 8 seeds, the four measurements, the Markdown tables, `--record`, `--seeds`, `--years`, `--workers`. Check 1 worker and 16 print the same on a small run.

6. **Measure before tuning.** Release build. Run `bench_climate_survey` on all 8 seeds, 10 years. Paste its tables into **Results**, marked "before tuning", with the date.

7. **Run every bench, before tuning.** Each bench in the bench table, cool and hot. Record in **Results**, marked "before tuning". `bench_world` with `--record bench/world/history.tsv --label "<commit>: plan 004 phase 11, before tuning"`.

8. **Show Ahmed.** Write a short summary for Ahmed in this file: what the measurements show against the plan, plainly, with every miss. Propose changes only where a number is far from the plan's expectation (for example, hurricanes a coast under 3 or over 12 a season) or a bench missed, each with what it would change. Ahmed plays (the Gate) and decides.

9. **Tune, one constant at a time.** For each value Ahmed decides: change the constant, the **Tuned** column, `run_climate_constants`, and any test or golden whose expected number follows from it, each with a line saying why. Run that part's tests and determinism test. Re-run the measurement it affects. Write an entry in **Issues found**: the plan's value, the new value, Ahmed's reason and date, and the measurement before and after.

10. **Measure after tuning.** Run `bench_climate_survey` and every bench again. Record in **Results**, marked "after tuning". If nothing was tuned, say so and skip this step.

11. **Documents.** `README.md` for the new test and bench. Each bench's README gains its results. `plans/REPO_STRUCTURE.md`: `tests/run/climate/constants/` and `bench/climate/survey/`.

12. **Verify and hand over.** Run **Verification**. Tick each todo as it lands. Write every miss in plain words at the top of **Results**, as 001 phase 17 did.

## Todo

- [ ] Read the vision file, master plan 4 to 8 and 10 to 12, and every earlier plan 004 phase file
- [ ] Fill the Home column of the constant table from the code, and commit it before changing code
- [ ] Every constant in sections 4 to 8 lives in one place each, named, with its unit
- [ ] `tests/run/climate/constants/`: every constant pinned, derived constants checked, literal sweep, passing
- [ ] `bench/climate/survey/`: the strip, 8 seeds, four measurements, same output on 1 and 16 workers
- [ ] Re-measure, over 8 seeds: climate class shares, hurricanes a season per warm coast, thunderstorm days a year by class, and outlook calibration. Record them
- [ ] Re-run every bench: climate tile, systems, census, Atlas, frame rate, plus terrain and the sim tick, cool and hot. Record them
- [ ] Summary for Ahmed, with every miss, and proposals only where a number is far off
- [ ] Each value Ahmed tuned: constant, table, test and Issues found entry, with before and after
- [ ] Measurements and benches after tuning, recorded
- [ ] READMEs and `plans/REPO_STRUCTURE.md` updated
- [ ] Gate: Ahmed plays, reviews the tables, and says the numbers are right

## Tests

- `run_climate_constants` (NEW):
  - `every_constant_has_the_value_its_table_gives`: one check per row, with the name in the message. Fails naming the constant, the value found and the value expected.
  - `derived_constants_follow_their_sources`: `CLIMATE_POLE_Y == round(CLIMATE_TROPIC_Y * 90 / CLIMATE_TROPIC_DEG)`, the period is four times that, the polar circle is $90 - 23.5$. Fails naming which.
  - `knot_tables_have_every_knot_in_order`: each table's latitudes rise, and each value matches. Fails naming the table and knot.
  - `no_unnamed_number_in_the_climate_code`: the literal sweep. Fails with file, line and the literal.
- Every test of phases 01 to 10 still passes after step 3, with every pinned digest and golden unchanged. Fails if any moved: a value changed by mistake.
- After step 9, every test still passes, with each moved expected number or golden explained by a line in the commit and in **Issues found**.
- `bench_climate_survey --seeds 2 --years 1` gives the same output with `--workers 1` and `--workers 16`. Run by hand, recorded in **Results**; it is a bench, not a ctest.

## Verification

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_climate_constants|standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3400 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 7200 build/release/bin/bench_climate_survey --record bench/climate/survey/history.tsv
    tools/test-or-build.sh timeout 600 build/release/bin/bench_climate_survey --seeds 2 --years 1 --workers 1
    tools/test-or-build.sh timeout 600 build/release/bin/bench_climate_survey --seeds 2 --years 1 --workers 16
    tools/test-or-build.sh timeout 300 build/release/bin/bench_atlas
    tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 300 build/release/bin/bench_mobs
    tools/test-or-build.sh cmake --preset profile
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world --record bench/world/history.tsv --label "<commit>: plan 004 phase 11"
    pgrep -a minecraft_c    # must print nothing

Also the climate benches of phases 03, 06 and 07, each through the wrapper:

    tools/test-or-build.sh timeout 300 build/release/bin/bench_climate
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_climate_census

and phase 06's systems bench, by the name in its file.

`timeout 7200` on the full survey is the wrapper's room for 8 seeds over 10 years. If it is not enough, run it a seed at a time with `--seeds`, and say so.

## Gate

Ahmed reads **Results** and the summary in step 8. Then he plays, with a release build:

    ./build/release/bin/minecraft_c --windowed

1. In a new world on seed `2198175988018515243`, and one other, play through a year (fast-forward by sleeping). Watch the seasons, the snow come and go, and a hurricane season on a warm coast with the Atlas (`M`, `7`).
2. Is the number of hurricanes right? Too many thunderstorms, too few? Is it too hot or too cold anywhere? Do the deserts and rainforests on the Atlas's climate class layer (`M`, `8`) look like sensible places?
3. Say which numbers to change, if any. The agent changes them one at a time, measures again, and records it.
4. When the numbers feel right, say so. That ticks the gate.

## Risks and open questions

| # | Question or risk | Default |
|---|---|---|
| 1 | The master plan gives no number for $t_0$ or the width of the horse latitudes. | Record what phases 01 and 04 chose, in the table, and pin it |
| 2 | Hurricane counts may be far from the plan's 7 a coast because of how coasts come out of the terrain, not because of the rate. | Report per square too, which does not depend on coast shape. Tune `CLIMATE_HURRICANE_RATE` only on Ahmed's word (Q5) |
| 3 | Tuning a rate changes the long-run rain (7.4's 15% test). | After any spawn-rate change, re-run that test. Master plan 7.4: spawn rates are tuned, not normals |
| 4 | Some benches may miss on a hot CPU, as plan 001's did. | Record cool and hot; a hot miss is stated plainly, not hidden. Ahmed decides whether it matters |
| 5 | The full survey may take hours. | Flags for fewer seeds, years or a narrower strip; record exactly what was run |
| 6 | The literal sweep may flag harmless numbers. | Each allowed literal is on the allowlist with a comment. The list stays short |
| 7 | A tuned value moves golden pictures of phases 05 to 10. | Make them again, look at each, and give each a line saying why |
| 8 | 8 seeds may not be enough for rare classes such as Ice in a strip. | The strip covers both poles, so Ice and Tundra appear on every seed. A class with under 200 sample cells is reported with its count |

## Commits

Appended as they land:

## Issues found

None.
