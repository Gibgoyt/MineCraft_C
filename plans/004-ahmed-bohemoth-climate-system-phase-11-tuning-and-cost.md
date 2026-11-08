# 004-ahmed-bohemoth-climate-system — phase 11: Tuning and cost

**Status:** in progress: built, measured and recorded before tuning (Results), every bench run again from a cool machine, and every climate number in the table and pinned (second pass); every agent-runnable check run, and run again by the gate checker in a clean worktree (Issues found 20). Left for Ahmed: the gate, Issues found 1 and 8 (phase 06's rates; no hurricanes on real seeds), and any value he decides to change, after which the measurements are run again   (updated 2026-11-08)

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
| `CALENDAR_TICKS_PER_DAY` | 24000 | ticks | 0, 4 | Length of a day. Existing: `GAME_DAY_SECONDS` 1200 s at 20 Hz, `run/include/game/index.h:34`. Use that, do not repeat it | Not a constant of its own: `GAME_DAY_SECONDS` 1200 (`run/include/game/index.h`) times `MC_GAME_TICK_HZ` 20 (`config/machine.yaml`). The climate keeps a copy it may see, `CLIMATE_REAL_SECONDS_A_DAY` 1200, run/include/climate/systems/index.h, pinned equal | |
| `CALENDAR_DAYS_PER_YEAR` | 365 | days | 4 | Length of a year | `CALENDAR_DAYS_PER_YEAR`, run/include/calendar/index.h. Every other part's year (`CLIMATE_YEAR_DAYS`, `CLIMATE_SYSTEMS_YEAR_DAYS`, `CLIMATE_HEAT_YEAR_DAYS`, `CLIMATE_DAYS_A_YEAR`, `CLIMATE_SEASON_DAYS`) is now made from it | |
| `CALENDAR_DAYS_PER_WEEK` | 7 | days | 4 | How often the census runs | `CALENDAR_DAYS_PER_WEEK`, run/include/calendar/index.h; `CLIMATE_WEEK_DAYS` and `CLIMATE_DAYS_A_WEEK` made from it | |
| `CALENDAR_FIRST_DAY` ($D_0$) | 79 | days | 4 | A new world's first day: the spring equinox (Q2) | `CALENDAR_DAY_ZERO`, run/include/calendar/index.h | |
| `CALENDAR_DAY_OFFSET_TICKS` ($t_0$) | set in phase 01 | ticks | 4 | Lines up day boundaries with 06:00 on the clock. Not given as a number in the master plan: record phase 01's value | `CALENDAR_CLOCK_DAY_START`, run/include/calendar/index.h: 2^30 in clock units, a quarter of `CALENDAR_CLOCK_DAY` (2^32 a day), so 06:00 = 6000 ticks. Made from `CALENDAR_CENSUS_HOUR` since phase 11 | |
| `CALENDAR_CENSUS_HOUR` | 6 | hours on the clock | 2.2, 4.2 | When the census fires: 06:00 everywhere | `CALENDAR_CENSUS_HOUR`, run/include/calendar/index.h (new, phase 11); `CLIMATE_DAY_START_HOUR` (run/include/climate/systems/index.h) made from it | |
| `CLIMATE_WEEK_CATCHUP_FULL` (phase 07) | 52 | weeks | 4.2 | Skipped weeks worked out in full; past this, fast mode keeps only snow and soil water | `CALENDAR_FULL_WEEKS_MAX`, run/include/calendar/index.h | |
| `CALENDAR_SPRING_DAY` | 79 | day of year | 4.1, 5.2 | Northern spring start, and the equinox | `CALENDAR_SPRING_FROM`, run/include/calendar/index.h | |
| `CALENDAR_SUMMER_DAY` | 170 | day of year | 4.1, 5.2 | Northern summer start; the sun furthest north | `CALENDAR_SUMMER_FROM`, run/include/calendar/index.h | |
| `CALENDAR_AUTUMN_DAY` | 262 | day of year | 4.1, 5.2 | Northern autumn start; the sun crosses the equator | `CALENDAR_AUTUMN_FROM`, run/include/calendar/index.h | |
| `CALENDAR_WINTER_DAY` | 353 | day of year | 4.1, 5.2 | Northern winter start; the sun furthest south | `CALENDAR_WINTER_FROM`, run/include/calendar/index.h | |
| `CALENDAR_SEASON_PARTS` | 3 | ratio | 4.1 | Early, mid and late: each season in equal thirds (phase 10) | `CALENDAR_SEASON_PARTS`, run/include/calendar/index.h (was a literal 3 in `run/src/calendar/index.c` and `run/src/climate/label/index.c`) | |
| `CALENDAR_NOON_HOUR` (phase 01) | 12 | hours on the clock | 4 | Noon, for the sun and the day's high | `CALENDAR_NOON_HOUR`, run/include/calendar/index.h; `CALENDAR_NOON_INTO_DAY_F64` and `_F32` (0.25 of a day after 06:00) made from it | |
| `CALENDAR_SEASONS` (phase 01) | 4 | seasons | 4.1 | Seasons in a year | `CALENDAR_SEASONS`, run/include/calendar/index.h; with `CALENDAR_SOUTH_SEASON_SHIFT` 2 (the south two seasons on) | |

#### Latitude and the sun (section 5)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_TROPIC_DEG` | 23.5 | degrees | 5.1, 5.2 | Latitude of the tropics, and the sun's yearly swing | `CLIMATE_TILT_DEG`, run/include/climate/index.h | |
| `CLIMATE_TROPIC_Y` | 10000 | blocks | 5.1 | Distance from the equator to the tropic: sets the whole latitude scale | `CLIMATE_TROPIC_Y`, run/include/climate/index.h | |
| `CLIMATE_POLE_Y` ($Y$) | 38298 | blocks | 5.1 | Equator to pole, $10\,000 \times 90 / 23.5$. Worked out from the two above, not typed | `CLIMATE_POLE_Y`, run/include/climate/index.h: `CLIMATE_TROPIC_Y * 90 / CLIMATE_TILT_DEG`, 38 297.87 | |
| `CLIMATE_LATITUDE_PERIOD_Y` | 153191 | blocks | 5.1 | $4Y$: the pattern repeats. Worked out, not typed | `CLIMATE_LATITUDE_PERIOD`, run/include/climate/index.h: `4 CLIMATE_POLE_Y` | |
| `CLIMATE_POLAR_CIRCLE_DEG` | 66.5 | degrees | 4.1, 5.2 | $90 - 23.5$: polar night and midnight sun beyond it. Worked out | `CLIMATE_POLAR_CIRCLE_DEG`, run/include/climate/index.h (new, phase 11: was `90 - CLIMATE_TILT_DEG` in `label/` and 66.5 typed in `layer/`) | |
| `CLIMATE_DECLINATION_DEG` | 23.5 | degrees | 5.2 | Amplitude of $\delta(d)$. The same as the tropic: one constant, used for both | `CLIMATE_TILT_DEG`, run/include/climate/index.h: the same constant as the tropic | |
| `CLIMATE_EQUINOX_DAY` | 79 | day of year | 5.2 | Phase of $\delta(d)$. The same as `CALENDAR_SPRING_DAY` | `CLIMATE_SUN_EQUINOX_DAY`, run/include/climate/sun/index.h: made from `CALENDAR_SPRING_FROM` since phase 11 (was 79 typed again) | |
| `CLIMATE_HOURS_PER_DAY` | 24 | hours | 5.2 | Day length scale, and the hour angle | `CALENDAR_HOURS_PER_DAY`, run/include/calendar/index.h (new, phase 11: was 24 typed in `sun/`, `label/`, `weather/`, `systems/` and `layer/`); `CLIMATE_HOURS_A_DAY` (run/include/climate/weather/index.h) made from it | |
| `CLIMATE_POLE_DEG` (phase 02) | 90 | degrees | 5.1 | Latitude of a pole; the polar circle is this minus the tilt | `CLIMATE_POLE_DEG`, run/include/climate/index.h | |
| `CLIMATE_SUN_NOON_HOUR` (phase 02) | 12 | hours | 5.2 | The sun's hour angle is 0 here | `CLIMATE_SUN_NOON_HOUR`, run/include/climate/sun/index.h | |

#### Height, land and sea (6.1, 6.2)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_CELL_BLOCKS` (phase 03) | 64 | blocks | 1, 6.2, 6.9 | Cell size: one climate sample, and the land mask's spacing | `CLIMATE_CELL_BLOCKS`, run/include/climate/cell/index.h | |
| `CLIMATE_TILE_CELLS` (phase 03) | 16 | cells | 1 | Tile side: 1024 blocks | `CLIMATE_TILE_CELLS`, run/include/climate/cell/index.h | |
| `CLIMATE_LAKE_LARGE_CELLS` (phase 03) | 2 km², 489 cells | km², as cells | 6.2 | A lake this big counts as water for moisture | `CLIMATE_LAKE_LARGE_CELLS`, run/include/climate/sea/index.h | |
| `CLIMATE_MARGIN_CELLS` (phase 03) | 8192 blocks, 128 cells | blocks, as cells | 6.2 | Margin round a tile for the distance transform (8 km) | `CLIMATE_MARGIN_CELLS`, run/include/climate/cell/index.h | |
| `CLIMATE_SEA_DISTANCE_MAX` (phase 03) | 8192 | blocks | 6.2, 6.9 | Sea distance cap | `CLIMATE_SEA_DISTANCE_MAX`, run/include/climate/cell/index.h (a `#define` since phase 11, so `layer/`'s scale table uses it instead of 8192 typed again) | |
| `CLIMATE_KAPPA_SCALE` (phase 03) | 1500 | blocks | 6.2 | Continentality $\kappa = 1 - e^{-d_s/1500}$: how fast the sea's influence fades inland | `CLIMATE_KAPPA_SCALE`, run/include/climate/cell/index.h | |
| `CLIMATE_SEA_AREA_MIN_CELLS` (phase 03) | 8192 | cells (33.6 km²) | 6.2 | Water this big is sea even wholly inside the window | `CLIMATE_SEA_AREA_MIN_CELLS`, run/include/climate/sea/index.h | |
| `CLIMATE_SEA_FLOOR_BLOCKS` (phase 03) | -30 | blocks | 6.2 | Water this deep somewhere makes its whole body sea | `CLIMATE_SEA_FLOOR_BLOCKS`, run/include/climate/cell/index.h | |
| `CLIMATE_GROUND_SAMPLES_SIDE` (phase 03) | 4 | samples a side | 6.1 | z_ground is the mean of 4 by 4 samples, 16 blocks apart | `CLIMATE_GROUND_SAMPLES_SIDE`, with `CLIMATE_GROUND_SAMPLE_STEP` 16, run/include/climate/cell/index.h | |

#### Ocean currents (6.3)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_CURRENT_REACH` (phase 04) | 600 | blocks | 6.3 | Sea cells this close to land have a current; its strength fades over this offshore | `CLIMATE_CURRENT_REACH`, run/include/climate/air/index.h | |
| `CLIMATE_CURRENT_SIDE` (phase 04) | 0.5 | ratio | 6.3 | $\lvert n_x \rvert$ above this makes a west or east coast | `CLIMATE_CURRENT_SIDE`, run/include/climate/air/index.h | |
| `CLIMATE_CURRENT_BANDS_DEG` (phase 04) | 15, 45, 65 | degrees | 6.3 | Band edges of the current table | `CLIMATE_CURRENT_BANDS_DEG[3]`, run/include/climate/air/index.h | |
| `CLIMATE_CURRENT_WARM_C` (phase 04) | +4 | °C | 6.3 | $C$ for a warm current | `CLIMATE_CURRENT_WARM_C`, run/include/climate/air/index.h | |
| `CLIMATE_CURRENT_COLD_C` (phase 04) | -5 | °C | 6.3 | $C$ for a cold current | `CLIMATE_CURRENT_COLD_C`, run/include/climate/air/index.h | |
| `CLIMATE_CURRENT_LAND_SCALE` (phase 04) | 600 | blocks | 6.3 | $\Delta T_{\text{current}} = C\,e^{-d_s/600}$ on land | `CLIMATE_CURRENT_LAND_SCALE`, run/include/climate/air/index.h | |
| `CLIMATE_SEA_PEAK_DAY` (phase 04) | 215 | day of year | 6.3 | Warmest sea in the north, 45 days after the land | `CLIMATE_SEA_PEAK_DAY`, run/include/climate/air/index.h | |
| `CLIMATE_CURRENT_WEAK` (phase 04) | 0.5 | ratio | 6.3 | "Warm, weak": half strength | `CLIMATE_CURRENT_WEAK`, run/include/climate/air/index.h | |
| `CLIMATE_CURRENT_FULL_WITHIN` (phase 04) | 32 | blocks | 6.3 | A sea cell this near land has its current at full strength | `CLIMATE_CURRENT_FULL_WITHIN`, run/include/climate/air/index.h; `CLIMATE_CURRENT_STRENGTH_MAX` 255 (run/include/climate/cell/index.h) stores full strength in a byte | |

#### Pressure (6.4)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `climate_pressure_knots` | 0°: 1008, 15°: 1012, 30°: 1020, 45°: 1013, 60°: 1000, 75°: 1010, 90°: 1020 | hPa by $\lvert\psi\rvert$ | 6.4 | Background pressure belts | `CLIMATE_PRESSURE_KNOTS` at `CLIMATE_KNOTS_LAT_DEG`, run/include/climate/normals/index.h (moved from `run/src/climate/normals/index.c` and named in capitals, phase 11) | |
| `CLIMATE_HEAT_LOW_HPA_PER_C` (phase 04) | -0.6 | hPa per °C | 6.4 | Heat lows and cold highs: the monsoon's strength | `CLIMATE_HEAT_LOW_HPA_PER_C`, run/include/climate/air/index.h | |

#### Wind (6.5)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_WIND_SHIFT` (phase 04) | 0.5 | ratio | 6.5 | Belts follow the sun half as far | `CLIMATE_WIND_SHIFT`, run/include/climate/air/index.h | |
| `CLIMATE_WIND_LAG_DAYS` (phase 04) | 30 | days | 6.5 | ... and a month late | `CLIMATE_WIND_LAG_DAYS`, run/include/climate/air/index.h | |
| `CLIMATE_TRADE_DIR` (phase 04) | $(-1, -0.4)$ | unit-less $(u, v)$ | 6.5 | Trade winds' direction, $v$ times $\operatorname{sgn}\psi$ | `CLIMATE_TRADE_DIR[2]`, run/include/climate/air/index.h (normalised where used) | |
| `CLIMATE_TRADE_SPEED` (phase 04) | 6 | blocks a second | 6.5 | Trade winds' speed | `CLIMATE_TRADE_SPEED`, run/include/climate/air/index.h | |
| `CLIMATE_WESTERLY_DIR` (phase 04) | $(+1, +0.3)$ | | 6.5 | Westerlies' direction | `CLIMATE_WESTERLY_DIR[2]`, run/include/climate/air/index.h | |
| `CLIMATE_WESTERLY_SPEED` (phase 04) | 8 | blocks a second | 6.5 | Westerlies' speed | `CLIMATE_WESTERLY_SPEED`, run/include/climate/air/index.h | |
| `CLIMATE_POLAR_DIR` (phase 04) | $(-1, -0.2)$ | | 6.5 | Polar easterlies' direction | `CLIMATE_POLAR_DIR[2]`, run/include/climate/air/index.h | |
| `CLIMATE_POLAR_SPEED` (phase 04) | 5 | blocks a second | 6.5 | Polar easterlies' speed | `CLIMATE_POLAR_SPEED`, run/include/climate/air/index.h | |
| `CLIMATE_BELT_EDGES_DEG` (phase 04) | 30, 60 | degrees | 6.5 | Where one belt gives way to the next | `CLIMATE_BELT_EDGES_DEG[2]`, run/include/climate/air/index.h | |
| `CLIMATE_BELT_BLEND_DEG` (phase 04) | 6 | degrees | 6.5 | Width of the smoothstep blend between belts | `CLIMATE_BELT_BLEND_DEG`, run/include/climate/air/index.h | |
| `CLIMATE_DOLDRUMS_DEG` (phase 04) | 5 | degrees | 6.5 | Doldrums half-width | `CLIMATE_DOLDRUMS_DEG`, run/include/climate/air/index.h | |
| `CLIMATE_DOLDRUMS_SPEED` (phase 04) | 0.4 | ratio | 6.5 | Speed in the doldrums | `CLIMATE_DOLDRUMS_SPEED`, run/include/climate/air/index.h | |
| `CLIMATE_HORSE_SPEED` (phase 04) | 0.5 | ratio | 6.5 | Speed near 30°. The plan gives no width: phase 04 chose `CLIMATE_HORSE_DEG` 2° of full calm and `CLIMATE_CALM_RAMP_DEG` 3°, which are pinned too | `CLIMATE_HORSE_SPEED`, `CLIMATE_HORSE_DEG` 2, `CLIMATE_CALM_RAMP_DEG` 3, run/include/climate/air/index.h | |
| `CLIMATE_PRESSURE_WIND_K` ($k_p$, phase 04) | 900 | blocks² per hPa per second | 6.5 | Wind from pressure differences | `CLIMATE_PRESSURE_WIND_K`, run/include/climate/air/index.h | |
| `CLIMATE_PRESSURE_TURN_DEG` (phase 04) | 70 | degrees | 6.5 | Turn from straight downhill in pressure, clockwise north | `CLIMATE_PRESSURE_TURN_DEG`, run/include/climate/air/index.h | |
| `CLIMATE_ALTITUDE_SCALE` (phase 04) | 800 | blocks | 6.5 | Speed $\times (1 + z/800)$ | `CLIMATE_ALTITUDE_SCALE`, run/include/climate/air/index.h | |
| `CLIMATE_ALTITUDE_MAX` (phase 04) | 2.5 | ratio | 6.5 | ... at most | `CLIMATE_ALTITUDE_MAX`, run/include/climate/air/index.h | |
| `CLIMATE_STEER` (phase 04) | 0.7 | ratio | 6.5 | How much steep ground turns the wind along it | `CLIMATE_STEER`, run/include/climate/air/index.h | |
| `CLIMATE_HEMISPHERE_BLEND_DEG` (phase 04) | 3 | degrees | 6.5 | The blend across the equator, $\sigma(\psi)$ over ±3° | `CLIMATE_HEMISPHERE_BLEND_DEG`, run/include/climate/air/index.h | |

#### Temperature (6.6)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `climate_temperature_knots` ($\bar T_{\text{lat}}$) | 0°: 27, 15°: 26, 30°: 20, 45°: 11, 60°: 0, 75°: -14, 90°: -24 | °C by latitude | 6.6 | Yearly mean at sea level | `CLIMATE_TEMPERATURE_KNOTS` at `CLIMATE_KNOTS_LAT_DEG`, run/include/climate/normals/index.h (moved from the source, phase 11) | |
| `CLIMATE_LAPSE_C_PER_BLOCK` ($\Gamma$) | 0.012 | °C per block | 6.6 | Cooling with height, as decided | `CLIMATE_LAPSE_C_PER_BLOCK`, run/include/climate/heat/index.h | |
| `CLIMATE_BASIN_C_PER_BLOCK` | 0.025 | °C per block | 2.1, 6.6 | Below-sea-level heat $B$, on land not under sea | `CLIMATE_BASIN_C_PER_BLOCK`, run/include/climate/heat/index.h | |
| `climate_swing_sea_knots` ($A_{\text{sea}}$) | 0°: 1, 30°: 3, 60°: 6, 90°: 10 | °C | 6.6 | Half the yearly swing over sea | `CLIMATE_SWING_SEA_KNOTS` at `CLIMATE_SWING_KNOTS_LAT_DEG`, run/include/climate/normals/index.h (moved, phase 11) | |
| `climate_swing_land_knots` ($A_{\text{land}}$) | 0°: 2, 30°: 9, 60°: 18, 90°: 22 | °C | 6.6 | Half the yearly swing deep inland | `CLIMATE_SWING_LAND_KNOTS` at `CLIMATE_SWING_KNOTS_LAT_DEG`, run/include/climate/normals/index.h (moved, phase 11) | |
| `CLIMATE_SUMMER_PEAK_DAY` | 170 | day of year | 6.6 | Phase of the temperature year before the lag | `CLIMATE_SOLSTICE_DAY` (run/include/climate/heat/index.h) and `CLIMATE_SUMMER_DAY` (run/include/climate/air/index.h): both typed 170 before phase 11, now both made from `CALENDAR_SUMMER_FROM` | |
| `CLIMATE_LAG_BASE_DAYS` | 25 | days | 6.6 | $\ell = 25 + 20(1 - \kappa)$ | `CLIMATE_LAG_BASE_DAYS`, run/include/climate/air/index.h | |
| `CLIMATE_LAG_SEA_DAYS` | 20 | days | 6.6 | ... the sea's extra lag | `CLIMATE_LAG_SEA_DAYS`, run/include/climate/air/index.h | |
| `CLIMATE_DAILY_RANGE_BASE_C` | 4 | °C | 6.6 | Daily range $(4 + 8\kappa)(1 - 0.6r)$ | `CLIMATE_RANGE_COAST_C`, run/include/climate/heat/index.h | |
| `CLIMATE_DAILY_RANGE_INLAND_C` | 8 | °C | 6.6 | ... inland part | `CLIMATE_RANGE_INLAND_EXTRA_C`, run/include/climate/heat/index.h | |
| `CLIMATE_DAILY_RANGE_RAIN_CUT` | 0.6 | ratio | 6.6 | ... cut by the share of rainy days | `CLIMATE_RANGE_RAIN_CUT`, run/include/climate/heat/index.h; the share of rainy days uses `CLIMATE_WET_DAY_SCALE_MM` 6, the same 6 as the outlook's (`CLIMATE_OUTLOOK_RAIN_MM` is now that constant) | |

#### Rain (6.7)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `climate_rain_knots` ($P_{\text{belt}}$) | 0°: 2400, 10°: 1800, 20°: 700, 30°: 300, 40°: 800, 50°: 1000, 60°: 800, 75°: 350, 90°: 150 | mm a year by $\lvert\psi\rvert$ | 6.7 | Rain belts | `CLIMATE_RAIN_KNOTS` at `CLIMATE_RAIN_KNOTS_LAT_DEG`, run/include/climate/normals/index.h (moved, phase 11) | |
| `CLIMATE_UPWIND_STEPS` | 16 | steps | 6.7 | Upwind walk steps | `CLIMATE_WALK_STEPS_WINDOW`, run/include/climate/rain/index.h | |
| `CLIMATE_UPWIND_STEP_BLOCKS` | 128 | blocks | 6.7 | Upwind step length (16 × 128 = 2 km) | `CLIMATE_WALK_STEP_BLOCKS`, run/include/climate/rain/index.h | |
| `CLIMATE_UPWIND_MAX_BLOCKS` | 8192 | blocks | 6.7 | The walk goes on over the coarse land mask to 8 km | `CLIMATE_WALK_NONE_BLOCKS` 8192 and `CLIMATE_WALK_STEPS_MAX` 64 (8192 / 128), run/include/climate/rain/index.h | |
| `CLIMATE_MOISTURE_FLOOR` | 0.15 | ratio | 6.7 | $M$ never below this | `CLIMATE_MOISTURE_MIN`, run/include/climate/rain/index.h | |
| `CLIMATE_MOISTURE_SCALE_BLOCKS` | 3000 | blocks | 6.7 | $e^{-d_{\text{up}}/3000}$: how fast air dries over land | `CLIMATE_MOISTURE_SCALE_BLOCKS`, run/include/climate/rain/index.h | |
| `CLIMATE_MOISTURE_WARM` | 1.25 | ratio | 6.7 | Air that left over a warm current | `CLIMATE_MOISTURE_WARM`, run/include/climate/rain/index.h | |
| `CLIMATE_MOISTURE_COLD` | 0.4 | ratio | 6.7 | Within 800 blocks of a cold current | `CLIMATE_MOISTURE_COLD`, run/include/climate/rain/index.h | |
| `CLIMATE_MOISTURE_COLD_BLOCKS` | 800 | blocks | 6.7 | ... that distance | `CLIMATE_MOISTURE_COLD_BLOCKS`, run/include/climate/rain/index.h | |
| `CLIMATE_OROGRAPHIC_SLOPE` | 8 | ratio | 6.7 | $1 + 8 s_w$: windward rain | `CLIMATE_OROGRAPHIC_GAIN`, run/include/climate/rain/index.h | |
| `CLIMATE_OROGRAPHIC_SLOPE_BLOCKS` | 256 | blocks | 6.7 | $s_w$ measured over this | `CLIMATE_OROGRAPHIC_RISE_BLOCKS`, run/include/climate/rain/index.h | |
| `CLIMATE_RAIN_SHADOW_BLOCKS` | 400 | blocks | 6.7 | $\max(0, h_{\text{up}} - z)/400$: lee drying | `CLIMATE_SHADOW_BLOCKS`, run/include/climate/rain/index.h | |
| `CLIMATE_OROGRAPHIC_MIN`, `_MAX` | 0.15, 2.5 | ratio | 6.7 | Clamp of $O$ | `CLIMATE_OROGRAPHIC_MIN`, `CLIMATE_OROGRAPHIC_MAX`, run/include/climate/rain/index.h | |
| `CLIMATE_SNOW_ALL_RAIN_C` (phase 07) | 1 | °C | 6.7 | All rain above this | `CLIMATE_SNOW_ALL_RAIN_C`, run/include/climate/heat/index.h | |
| `CLIMATE_SNOW_ALL_SNOW_C` (phase 07) | -2 | °C | 6.7 | All snow below this; share $(1 - T)/3$ between | `CLIMATE_SNOW_ALL_SNOW_C`, run/include/climate/heat/index.h | |
| `CLIMATE_WALK_CALM` (phase 05) | 0.001 | blocks a second | 6.7 | Below this a wind has no direction for the upwind walk | `CLIMATE_WALK_CALM`, run/include/climate/rain/index.h | |

#### Storm seasons, cells and classes (6.8 to 6.10)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_HURRICANE_COAST_BLOCKS` | 3000 | blocks | 6.8 | Coasts this close to sea share its hurricane season | `CLIMATE_HURRICANE_COAST_BLOCKS`, run/include/climate/season/index.h | |
| `CLIMATE_HURRICANE_SEA_C` | 26.5 | °C | 6.8 | Sea temperature hurricanes need | `CLIMATE_HURRICANE_SEA_C`, run/include/climate/season/index.h | |
| `CLIMATE_HURRICANE_RAMP_C` | 2 | °C | 6.8 | $s_{\text{hur}}$ rises from 0 to 1 over this | `CLIMATE_HURRICANE_RAMP_C`, run/include/climate/season/index.h | |
| `CLIMATE_HURRICANE_LAT_MIN_DEG`, `_MAX_DEG` | 5, 25 | degrees | 6.8 | Where hurricanes can form | `CLIMATE_HURRICANE_LAT_MIN_DEG`, `CLIMATE_HURRICANE_LAT_MAX_DEG`, run/include/climate/season/index.h | |
| `CLIMATE_HURRICANE_SEASON_MIN` | 0.25 | ratio | 4.1, 6.8 | "Hurricane season" while $s_{\text{hur}}$ is above this | `CLIMATE_HURRICANE_SEASON_S`, run/include/climate/season/index.h | |
| `CLIMATE_MONSOON_RAIN_RATIO` | 3 | ratio | 6.8 | Wet half's rain over dry half's, for a monsoon | `CLIMATE_MONSOON_RAIN_RATIO`, run/include/climate/season/index.h | |
| `CLIMATE_FIT_DAYS` (phase 04) | 12 | days | 6.9 | Days rain and wind are worked out on, to fit the harmonics | `CLIMATE_FIT_DAYS`, run/include/climate/harmonic/index.h | |
| `CLIMATE_HARMONICS` | 2 | waves | 6.9 | Waves per year in the fit | `CLIMATE_HARMONIC2_COEFFICIENTS` 5 (a mean and 2 waves) and `CLIMATE_HARMONIC1_COEFFICIENTS` 3 (wind and pressure: one wave), run/include/climate/harmonic/index.h (new names, phase 11: were 3, 5 and 6 typed) | |
| `CLIMATE_WARP_BLOCKS` (phase 03) | 32 | blocks | 6.9 | Warp before the bilinear blend: how much borders wobble | `CLIMATE_WARP_BLOCKS`, run/include/climate/cell/index.h; with `CLIMATE_WARP_FREQUENCY`, `CLIMATE_WARP_LIMIT` 0.35 and `CLIMATE_WARP_CUBIC` 1.5 (the last two moved from `run/src/climate/index.c`) | |
| `CLIMATE_HURRICANE_DAY_SCALE` | 4 | days a step | 6.9 | `hurricane_days` stores day / 4 in a byte | `CLIMATE_HURRICANE_DAY_SCALE`, run/include/climate/season/index.h (new, phase 11: was 4 typed three times in `season/`) | |
| `CLIMATE_DRY_THRESHOLD_PER_C` | 20 | mm per °C | 6.10 | $P_{\text{th}} = 20\,\bar T + 140$ | `CLIMATE_DRY_THRESHOLD_PER_C`, run/include/climate/season/index.h | |
| `CLIMATE_DRY_THRESHOLD_BASE` | 140 | mm | 6.10 | ... base | `CLIMATE_DRY_THRESHOLD_BASE_MM`, run/include/climate/season/index.h | |
| `CLIMATE_CLASS_TUNDRA_C` | 10 | °C | 6.10 | Tundra below this warmest month; Ice below 0 | `CLIMATE_TUNDRA_MAX_C`, run/include/climate/season/index.h | |
| `CLIMATE_CLASS_TROPICAL_C` | 18 | °C | 6.10 | Coldest month for Rainforest, Monsoon, Savanna | `CLIMATE_TROPICAL_MIN_C`, run/include/climate/season/index.h | |
| `CLIMATE_CLASS_RAINFOREST_MM` | 60 | mm | 6.10 | Driest month for Rainforest | `CLIMATE_WET_MONTH_MM`, run/include/climate/season/index.h; with phase 05's `CLIMATE_DRY_SUMMER_MM` 40 and `CLIMATE_DRY_SUMMER_SHARE` 1/3 | |
| `CLIMATE_CLASS_TEMPERATE_C` | -3 | °C | 6.10 | Coldest month: above is Mediterranean or Temperate | `CLIMATE_TEMPERATE_MIN_C`, run/include/climate/season/index.h | |
| `CLIMATE_CLASS_CONTINENTAL_C` | 22 | °C | 6.10 | Warmest month: Continental at or above, Boreal below | `CLIMATE_CONTINENTAL_MAX_C`, run/include/climate/season/index.h | |
| `CLIMATE_MONTH_WAVE1`, `_WAVE2` (phase 05) | 0.98862, 0.95493 | ratio | 6.10 | A month's mean of each harmonic wave, $\sin(x)/x$ at π/12 and π/6 | `CLIMATE_MONTH_WAVE1`, `CLIMATE_MONTH_WAVE2`, run/include/climate/season/index.h | |
| `CLIMATE_SNOW_BALANCE_LOW`, `_HIGH`, `_NONE` (phase 05) | -250, 2000, 9999 | blocks | 6.8 | The snow balance's search range, and its answer where snow never lasts the summer | `CLIMATE_SNOW_BALANCE_LOW`, `_HIGH`, `_NONE`, with `CLIMATE_SNOW_BALANCE_HALVINGS` 12 and `_YEARS` 2, run/include/climate/season/index.h | |

#### Weather systems (7.2, 7.3)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_HURRICANE_SQUARE_BLOCKS` | 16384 | blocks | 7.2 | Spawn square (16 km) | `CLIMATE_SQUARE_HURRICANE`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_RATE` | 0.02 | per square per day, times $s_{\text{hur}}$ | 7.2 | **How many hurricanes.** Q5: tune by playing | `CLIMATE_HURRICANE_RATE`, run/include/climate/systems/index.h | 0.034: by phase 06's agent, not Ahmed (Issues found 1) |
| `CLIMATE_HURRICANE_RADIUS_BLOCKS` | 1500 to 4000 | blocks | 7.2 | Size | `CLIMATE_HURRICANE_RADIUS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_EYE_BLOCKS` | 80 to 150 | blocks | 7.2 | Eye radius | `CLIMATE_HURRICANE_EYE[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_HPA` | -30 to -90 | hPa | 7.2 | Pressure dip, so strength | `CLIMATE_HURRICANE_HPA[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_LIFE_DAYS` | 4 to 10 | days | 7.2 | Life | `CLIMATE_HURRICANE_LIFE_DAYS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_LAND_DECAY` | 0.5 | ratio per day | 7.2 | Weakening over land | `CLIMATE_HURRICANE_HALF_LIFE_DAYS` 1, run/include/climate/systems/index.h: a half-life of one day, which is 0.5 a day | |
| `CLIMATE_HURRICANE_DECAY_SEA_C` | 26 | °C | 7.2 | Weakens over sea below this | `CLIMATE_HURRICANE_COOL_SEA_C`, run/include/climate/systems/index.h | |
| `CLIMATE_LIFE_GROW`, `_FADE` | 1/3, 1/3 | share of life | 7.2 | Life curve | `CLIMATE_LIFE_GROW_SHARE`, `CLIMATE_LIFE_FADE_SHARE`, `CLIMATE_LIFE_THIRDS` 3, run/include/climate/systems/index.h (new, phase 11: were `SYSTEMS_THIRD` and 3 typed in `systems/`) | |
| `CLIMATE_LOW_SQUARE_BLOCKS` | 16384 | blocks | 7.2 | Spawn square | `CLIMATE_SQUARE_LOW`, run/include/climate/systems/index.h | |
| `CLIMATE_LOW_LAT_MIN_DEG`, `_MAX_DEG` | 35, 65 | degrees of $\lvert\psi\rvert$ | 7.2 | Where lows form | `CLIMATE_LOW_PSI_MIN_DEG`, `CLIMATE_LOW_PSI_MAX_DEG`, run/include/climate/systems/index.h | |
| `CLIMATE_LOW_RATE_WINTER`, `_SUMMER` | 0.08, 0.04 | per square per day | 7.2 | How many lows | `CLIMATE_LOW_RATE_WINTER`, `CLIMATE_LOW_RATE_SUMMER`, run/include/climate/systems/index.h | 0.18, 0.09: by phase 06's agent (Issues found 1) |
| `CLIMATE_LOW_RADIUS_BLOCKS` | 3000 to 7000 | blocks | 7.2 | Size | `CLIMATE_LOW_RADIUS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_LOW_HPA` | -10 to -30 | hPa | 7.2 | Depth | `CLIMATE_LOW_HPA[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_LOW_LIFE_DAYS` | 3 to 6 | days | 7.2 | Life | `CLIMATE_LOW_LIFE_DAYS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_LOW_SPEED_BLOCKS_PER_DAY` | 6000 | blocks a day | 7.2 | Eastward speed | `CLIMATE_LOW_SPEED[2]` 5000 to 7000 ("about 6 km a day"), run/include/climate/systems/index.h; drift `CLIMATE_LOW_POLEWARD` | |
| `CLIMATE_FRONT_LENGTH_BLOCKS` | 4000 to 8000 | blocks | 7.2 | Cold front length | `CLIMATE_LOW_FRONT_BLOCKS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_FRONT_CELL_FACTOR` | 5 | ratio | 7.2 | Cells on a front, against the usual rate (the formula's $1 + 4 f_{\text{front}}$) | `CLIMATE_THUNDER_FRONT` 4, run/include/climate/systems/index.h: the 4 of 1 + 4 f_front | |
| `CLIMATE_HIGH_SQUARE_BLOCKS` | 24576 | blocks | 7.2 | Spawn square (24 km) | `CLIMATE_SQUARE_HIGH`, run/include/climate/systems/index.h | |
| `CLIMATE_HIGH_LAT_MIN_DEG`, `_MAX_DEG` | 20, 45 | degrees of $\lvert\psi\rvert$ | 7.2 | Where highs form (and continental interiors in winter) | `CLIMATE_HIGH_PSI_MIN_DEG`, `CLIMATE_HIGH_PSI_MAX_DEG`, run/include/climate/systems/index.h; interiors: `CLIMATE_HIGH_INTERIOR_PSI_MAX_DEG`, `CLIMATE_HIGH_INTERIOR_BLOCKS` | |
| `CLIMATE_HIGH_RATE` | 0.03 | per square per day | 7.2 | How many highs | `CLIMATE_HIGH_RATE`, run/include/climate/systems/index.h | |
| `CLIMATE_HIGH_RADIUS_BLOCKS` | 6000 to 12000 | blocks | 7.2 | Size | `CLIMATE_HIGH_RADIUS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HIGH_HPA` | +8 to +20 | hPa | 7.2 | Strength | `CLIMATE_HIGH_HPA[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HIGH_LIFE_DAYS` | 6 to 14 | days | 7.2 | Life | `CLIMATE_HIGH_LIFE_DAYS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HIGH_SPEED_BLOCKS_PER_DAY` | 2000 | blocks a day | 7.2 | Eastward speed | `CLIMATE_HIGH_SPEED[2]` 1500 to 2500, run/include/climate/systems/index.h; drift `CLIMATE_HIGH_EQUATORWARD` | |
| `CLIMATE_HIGH_RAIN_FACTOR` | 0.2 | ratio | 7.2, 7.3, 7.2.1 | Rain under a high, and cells under one ($0.2^{f_{\text{high}}}$) | `CLIMATE_HIGH_RAIN_SHARE`, run/include/climate/systems/index.h; `CLIMATE_THUNDER_HIGH` is now that constant (both were 0.2 typed) | |
| `CLIMATE_CELL_SQUARE_BLOCKS` | 1024 | blocks | 7.2 | Thunderstorm cell spawn square (1 km) | `CLIMATE_SQUARE_THUNDER`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_RADIUS_BLOCKS` | 150 to 400 | blocks | 7.2 | Cell size | `CLIMATE_THUNDER_RADIUS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_HPA` | -1 to -3 | hPa | 7.2 | Cell pressure dip | `CLIMATE_THUNDER_HPA[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_LIFE_HOURS` | 1 to 4 | clock hours | 7.2 | Cell life | `CLIMATE_THUNDER_LIFE_HOURS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_RATE` | 3 | per km² per day | 7.2.1 | **How many thunderstorms** | `CLIMATE_THUNDER_RATE` 3, times `CLIMATE_THUNDER_RATE_GAIN`, run/include/climate/systems/index.h | gain 0.22: by phase 06's agent (Issues found 1) |
| `CLIMATE_CELL_TEMP_C` | 18 | °C | 7.2.1 | Cells need a day warmer than this | `CLIMATE_THUNDER_HOT_C`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_TEMP_RAMP_C` | 10 | °C | 7.2.1 | ... and reach full rate 10 above it | `CLIMATE_THUNDER_HOT_RANGE_C`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_MONSOON_FACTOR` | 2 | ratio | 7.2.1 | $1 + 2 f_{\text{monsoon}}$ | `CLIMATE_THUNDER_MONSOON`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_FRONT_FACTOR` | 4 | ratio | 7.2.1 | $1 + 4 f_{\text{front}}$ | `CLIMATE_THUNDER_FRONT`, run/include/climate/systems/index.h | |
| `CLIMATE_CELL_START_HOURS` | 13 to 19 | clock hours | 7.2.1 | Cells start in the afternoon | `CLIMATE_THUNDER_START_HOURS[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_TRACK_STEP_DAYS` | 0.25 | days | 7.3 | Track step | `CLIMATE_TRACK_STEP_DAYS`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_SPEED_BLOCKS_PER_DAY` | 3500 | blocks a day | 7.3 | With the trades | `CLIMATE_HURRICANE_SPEED_WEST`, run/include/climate/systems/index.h; times `CLIMATE_SPEED_SCALE[2]` | |
| `CLIMATE_HURRICANE_TURN_DEG` | 20 to 28 | degrees of $\lvert\psi\rvert$ | 7.3 | Where it turns poleward | `CLIMATE_HURRICANE_TURN_PSI_DEG[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_TURN_DAYS` | 2 | days | 7.3 | How long the turn takes | `CLIMATE_HURRICANE_TURN_DAYS`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_LATE_SPEED_BLOCKS_PER_DAY` | 6000 | blocks a day | 7.3 | With the westerlies, after turning | `CLIMATE_HURRICANE_SPEED_EAST`, run/include/climate/systems/index.h | |
| `CLIMATE_SYSTEM_REACH_BLOCKS` | 35000 | blocks | 7.3 | Longest track: how far back to look for systems | Not a constant: `climate_systems_reach(kind)` works it out from the life, speed and radius constants (a hurricane: 77 140 blocks with its drift and 3 R, more than the plan's 35 000; phase 06 Issues found 2) | |
| `CLIMATE_SYSTEM_LIFE_MAX_DAYS` | 14 | days | 7.3 | Longest life: how long back to look | Not a constant: `CLIMATE_HIGH_LIFE_DAYS[1]` 14, the longest life, read by `systems_life_max`; `CLIMATE_TRACK_SAMPLES_MAX` 57 follows from it | |
| `CLIMATE_WIND_PROFILE_EXPONENT` | 0.6 | ratio | 7.3 | $v \propto (r_m / r)^{0.6}$ outside $r_m$ | `CLIMATE_WIND_PROFILE_POWER`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_VMAX` | 33 at -30 hPa, 70 at -90 hPa | blocks a second | 7.3 | Hurricane wind from its dip | `CLIMATE_HURRICANE_VMAX_AT_30`, `CLIMATE_HURRICANE_VMAX_AT_90`, run/include/climate/systems/index.h; tropical storm under `CLIMATE_HURRICANE_STORM_VMAX` 33 | |
| `CLIMATE_SYSTEM_RM_SHARE` | 0.5 | share of $R_i$ | 7.3 | Lows' and highs' strongest wind radius | `CLIMATE_SYSTEM_CORE_SHARE`, run/include/climate/systems/index.h | |
| `CLIMATE_SYSTEM_VMAX_PER_HPA` | 0.4 | blocks a second per hPa | 7.3 | Lows' and highs' strongest wind | `CLIMATE_SYSTEM_VMAX_PER_HPA`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_RAIN_MM_H` | 40 | mm an hour | 7.3 | Eyewall rain | `CLIMATE_HURRICANE_EYEWALL_MM_HOUR` at `CLIMATE_HURRICANE_EYEWALL_HPA` 90, run/include/climate/systems/index.h | |
| `CLIMATE_LOW_RAIN_MM_H` | 1 to 4 | mm an hour | 7.3 | Rain shield | `CLIMATE_LOW_SHIELD_MM_HOUR[2]`, run/include/climate/systems/index.h; the front's `CLIMATE_FRONT_MM_HOUR` 3 and `CLIMATE_FRONT_HALF_WIDTH` 400 | |
| `CLIMATE_CELL_RAIN_MM_H` | 20 to 60 | mm an hour | 7.3 | Cell rain, for under an hour at a point | `CLIMATE_THUNDER_MM_HOUR[2]`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS` (phase 06) | 2048 | blocks | 7.2 | **Where a hurricane may spawn: water this far all round.** Why the survey found none (Issues found 8) | `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_POLEWARD_WEST`, `_EAST` (phase 06) | 0.15, 0.3 | share of speed | 7.3 | A hurricane's poleward drift before and after it turns | run/include/climate/systems/index.h; with `CLIMATE_SPEED_SCALE` 0.85 to 1.15, its own speed | |
| `CLIMATE_LOW_POLEWARD`, `CLIMATE_HIGH_EQUATORWARD` (phase 06) | 0.08, 0.1 | share of speed | 7.2 | Lows' and highs' drift | run/include/climate/systems/index.h | 0.08 from 0.15: by phase 06's agent (Issues found 1) |
| `CLIMATE_DRIFT_POLE_STOP_DEG`, `CLIMATE_DRIFT_EQUATOR_STOP_DEG` (phase 06) | 85, 5 | degrees | 7.3 | No poleward drift past 85°, no equatorward drift inside 5° | run/include/climate/systems/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_LOW_PEAK_DAY` (phase 06) | 200 | day of year | 7.2 | The low rate is lowest here in the north | `CLIMATE_LOW_PEAK_DAY`, run/include/climate/systems/index.h | |
| `CLIMATE_SYSTEM_REACH_RADII`, `CLIMATE_SYSTEM_WIND_TAPER_RADII` (phase 06) | 3, 2 | radii | 7.3 | Nothing past 3 R; the wind tapers to 0 from 2 R | run/include/climate/systems/index.h | |
| `CLIMATE_SYSTEM_DEAD_HPA` (phase 06) | 5 | hPa | 7.2 | A big system weaker than this ends | `CLIMATE_SYSTEM_DEAD_HPA`, run/include/climate/systems/index.h | |
| `CLIMATE_HURRICANE_BAND_ARMS`, `_BAND_WIND` (phase 06) | 3, 2.5 | arms; ratio | 7.3 | A hurricane's spiral rain bands: how many, how tightly wound | run/include/climate/systems/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_LOW_SHIELD_MEAN`, `_AHEAD`, `_WIDTH` (phase 06) | 2.5, 0.5, 0.6 | ratio; share of R | 7.3 | A low's rain shield: the front's showers against it, how far ahead it sits, how wide | run/include/climate/systems/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_FRONT_TURN_X`, `_Y` (phase 06) | -0.5, 0.8660254 | unit vector | 7.2 | A cold front points equatorward, turned 30° west | run/include/climate/systems/index.h (pinned since phase 11's second pass), and checked to be a unit vector | |
| `CLIMATE_SYSTEM_RAIN_SHARE_POLAR`, `_POLAR_DEG` (phase 06) | 0.4; 60 to 70 | ratio; degrees | 7.4 | f_sys toward the poles, blended between those latitudes | run/include/climate/systems/index.h (`_POLAR_DEG` pinned since phase 11's second pass) | part of the f_sys blend phase 06's agent tuned (Issues found 1) |
| `CLIMATE_SYSTEM_RAIN_SEASON_MARGIN_DEG` (phase 06) | 5 | degrees | 7.4 | Lows bring rain this far outside their band | run/include/climate/systems/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_SYSTEM_RAIN_WET_MM_DAY` (phase 06) | 5 | mm a day | 7.3 | Storm rain scales with the normal over this | run/include/climate/systems/index.h | |
| `CLIMATE_THUNDER_HOT_FLOOR`, `_RAIN_SCALE_BASE`, `_SPEED_MAX` (phase 06) | 0.2, 1/3, 15 | ratio; ratio; blocks a second | 7.2.1 | A cell's rain scale: its floor and base; and the cells' fastest drift | run/include/climate/systems/index.h | the base 1/3 was set by phase 06's agent (phase 06, Issues found 12) |
| `CLIMATE_THUNDER_BIG_MARGIN` (phase 06) | 1250 | blocks | 7.2.1 | How much further out big systems are found than cells, so a cell sees the high or front it started under | run/include/climate/systems/index.h (pinned since phase 11's second pass) | |

#### Daily weather and the weekly state (7.4, 7.5)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_SPELL_BLOCKS` (phase 07) | 2500 | blocks | 7.4 | Size of day-to-day temperature patches | `CLIMATE_SPELL_BLOCKS`, run/include/climate/weather/index.h | |
| `CLIMATE_SPELL_DAYS` (phase 07) | 10 | days | 7.4 | Length of spells | `CLIMATE_SPELL_DAYS`, run/include/climate/weather/index.h | |
| `CLIMATE_SIGMA_T_COAST` (phase 07) | 2 | °C | 7.4 | $\sigma_T = 2 + 3\kappa$ | `CLIMATE_SIGMA_T_COAST`, run/include/climate/weather/index.h | |
| `CLIMATE_SIGMA_T_INLAND_EXTRA` (phase 07) | 3 | °C | 7.4 | ... inland part | `CLIMATE_SIGMA_T_INLAND_EXTRA`, run/include/climate/weather/index.h | |
| `CLIMATE_HIGH_ANOMALY_SIGMAS` (phase 07) | 0.5 | share of $\sigma_T$ | 7.4 | Highs: heat waves in summer, cold snaps in winter | `CLIMATE_HIGH_ANOMALY_SIGMAS`, run/include/climate/weather/index.h | |
| `CLIMATE_CLOUD_RANGE_CUT` (phase 07) | 0.6 | ratio | 7.4 | Clouds of lows and hurricanes cut the daily range | `CLIMATE_CLOUD_RANGE_CUT`, run/include/climate/weather/index.h | |
| `CLIMATE_SYSTEM_RAIN_SHARE_MID` (phase 07) | 0.5 | ratio | 7.4 | $f_{\text{sys}}$ in the mid-latitudes | `CLIMATE_SYSTEM_RAIN_SHARE_MID`, run/include/climate/systems/index.h (phase 06 owns it; the blend: `CLIMATE_SYSTEM_RAIN_SHARE_BLEND_DEG`, `_POLAR`, `_POLAR_DEG`) | |
| `CLIMATE_SYSTEM_RAIN_SHARE_TROPICS` (phase 07) | 0.3 | ratio | 7.4 | $f_{\text{sys}}$ in the tropics | `CLIMATE_SYSTEM_RAIN_SHARE_TROPICS`, run/include/climate/systems/index.h | |
| `CLIMATE_RAIN_CHECK_TOLERANCE` | 0.15 | ratio | 7.4 | Long-run rain within 15% of normal (a test bound) | `RAIN_TOLERANCE`, `tests/run/climate/systems/rain/index.c`: a test's bound, in the test; `SURVEY_RAIN_TOLERANCE` in `bench/climate/survey/`. Not pinned: it is no game number | |
| `CLIMATE_DAY_RAIN_MM` (phase 07) | 1 | mm | 7.4 | A day of rain or snow | `CLIMATE_DAY_RAIN_MM`, run/include/climate/weather/week/index.h | |
| `CLIMATE_STORM_DEPTH_HPA` (phase 07) | 20 | hPa | 7.4 | A low this deep is a Storm | `CLIMATE_STORM_DEPTH_HPA`, run/include/climate/weather/week/index.h | |
| `CLIMATE_STORM_WIND` (phase 07) | 15 | blocks a second | 7.4 | Wind this strong is a Storm | `CLIMATE_STORM_WIND`, run/include/climate/weather/week/index.h | |
| `CLIMATE_HEAT_WAVE_SIGMAS` (phase 07) | 1.5 | share of $\sigma_T$ | 7.4 | Heat wave | `CLIMATE_HEAT_WAVE_SIGMAS`, run/include/climate/weather/week/index.h | |
| `CLIMATE_DUST_WIND` (phase 07) | 12 | blocks a second | 7.4 | Dust storm wind | `CLIMATE_DUST_WIND`, run/include/climate/weather/week/index.h | |
| `CLIMATE_DUST_WEEK_RAIN_MM` (phase 07) | 2 | mm in the week | 7.4 | Dust storm dryness | `CLIMATE_DUST_WEEK_RAIN_MM`, run/include/climate/weather/week/index.h | |
| `CLIMATE_MELT_MM_PER_DEGREE_DAY` (phase 07) | 4 | mm per °C per day | 7.5 | Snowmelt | `CLIMATE_MELT_MM_PER_DEGREE_DAY`, run/include/climate/season/index.h (phase 05 needed it first, for the snow balance) | |
| `CLIMATE_EVAPORATION_MM_PER_DEGREE_DAY` (phase 07) | 0.2 | mm per °C per day | 7.5 | Evaporation | `CLIMATE_EVAPORATION_MM_PER_DEGREE_DAY`, run/include/climate/weather/week/index.h | |
| `CLIMATE_SOIL_MM_MAX` (phase 07) | 150 | mm | 7.5 | Soil water held before runoff | `CLIMATE_SOIL_MM_MAX`, run/include/climate/weather/week/index.h | |
| `CLIMATE_ICE_GROW_CM_PER_DEGREE_DAY` (phase 07) | 1.5 | cm per °C per day | 7.5 | Ice growth on still water | `CLIMATE_ICE_GROW_CM_PER_DEGREE_DAY`, run/include/climate/weather/week/index.h | |
| `CLIMATE_ICE_MELT_CM_PER_DEGREE_DAY` (phase 07) | 2 | cm per °C per day | 7.5 | Ice melt | `CLIMATE_ICE_MELT_CM_PER_DEGREE_DAY`, run/include/climate/weather/week/index.h | |
| `CLIMATE_FLOOD_BANKS_FULL` | 1 | ratio | 7.5 | Flood index at which water is over the banks | Not a constant: the flood index is runoff over channel size, so 1 is banks full by definition. The channel size is phase 07's `CLIMATE_FLOOD_BANKFULL` 3 (a channel holds 3 times its usual week) and `CLIMATE_FLOOD_CHANNEL_MIN` 20, run/include/climate/weather/flow/index.h | |
| `CLIMATE_RAIN_NOISE_GAIN` (phase 07) | 0.9 | ratio | 7.4 | Day-to-day rain amount, $g = \max(0, 1 + 0.9 N_2)$ | `CLIMATE_RAIN_NOISE_GAIN`, run/include/climate/weather/index.h | |
| `CLIMATE_RAIN_AMOUNT_BLOCKS`, `_DAYS`; `CLIMATE_RAIN_HOURS_BLOCKS`, `_HOURS` (phase 07) | 2000, 3; 1500, 6 | blocks; days; blocks; hours | 7.4 | Size and length of the rain amount and rain hour noises $N_2$, $N_3$ | run/include/climate/weather/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_SPELL_CLAMP` and the raw spreads (phase 07) | 3; 0.2013, 0.2008, 0.2015 | spreads; ratio | 7.4 | Each noise clamped to ±3; the measured raw spread of $N_1$, $N_2$, $N_3$ | `CLIMATE_SPELL_CLAMP`, `CLIMATE_SPELL_RAW_SD`, `CLIMATE_RAIN_AMOUNT_RAW_SD`, `CLIMATE_RAIN_HOURS_RAW_SD`, run/include/climate/weather/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_FOG_CALM_WIND`, `_COAST_BLOCKS`, `_VALLEY_BLOCKS`, `_VALLEY_SOIL_MM` (phase 07) | 3, 2000, 30, 100 | blocks a second; blocks; blocks; mm | 7.4 | A Fog day: calm at 06:00, and near a cold-current sea or in a wet valley this deep | run/include/climate/weather/week/index.h (the last two pinned since phase 11's second pass) | |
| `CLIMATE_CLOUDY_WEEK_RAIN_MM`, `CLIMATE_CLOUDY_LOW_RADII` (phase 07) | 10, 1.5 | mm in the week; radii | 7.4 | A Cloudy day: a wet week's normal, or near a low | run/include/climate/weather/week/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_WEEK_SPINUP_YEARS` (phase 07) | 2 | years | 7.5 | Years of weekly updates from normals alone that make a cell's first state | `CLIMATE_WEEK_SPINUP_YEARS`, `CLIMATE_WEEK_SPINUP_WEEKS` 104, run/include/climate/weather/index.h (pinned since phase 11's second pass) | |
| `CLIMATE_WEATHER_SHARE_BLOCKS` (phase 07) | 8192 | blocks | 7.4 | The widest box of places one day list serves | run/include/climate/weather/index.h (pinned since phase 11's second pass) | |
| Phase 08's sky: `CLIMATE_CLOUD_UNDER_HIGH`, `_CLEAR`, `_CLOUDY`, `_STORM`, `_PER_MM_HOUR`; `CLIMATE_FOG_MORNING_HOURS`; `CLIMATE_DUST_FULL_WIND` | 0.05, 0.2, 0.6, 0.75, 0.25; 4; 20 | cloud share; hours; blocks a second | 7.4, phase 08 | How cloudy each hour looks, how long morning fog lasts, when dust is full | run/include/climate/weather/index.h | |

#### The outlook (section 8)

| Name | Value | Unit | Section | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `CLIMATE_OUTLOOK_DAYS` (phase 07) | 7 | days | 8 | How far ahead | `CLIMATE_OUTLOOK_DAYS`, run/include/climate/outlook/index.h | |
| `CLIMATE_OUTLOOK_SIGMA_START` (phase 07) | 200 | blocks | 8 | Track error now, $\sigma(0)$ | `CLIMATE_OUTLOOK_SIGMA_START`, run/include/climate/outlook/index.h | |
| `CLIMATE_OUTLOOK_SIGMA_PER_DAY` (phase 07) | 400 | blocks a day | 8 | Track error growth | `CLIMATE_OUTLOOK_SIGMA_PER_DAY`, run/include/climate/outlook/index.h | |
| `CLIMATE_OUTLOOK_RAIN_MM` (phase 07) | 6 | mm | 8 | Background rain chance $1 - e^{-\bar P(d)/6}$ | `CLIMATE_OUTLOOK_RAIN_MM`, run/include/climate/outlook/index.h: since phase 11 a name for `CLIMATE_WET_DAY_SCALE_MM` (run/include/climate/heat/index.h), the one 6 | |
| `CLIMATE_OUTLOOK_SPREAD_SIGMAS` (phase 07) | 1.5 | share of $\sigma_T$ | 8 | Spread on the expected high and low | `CLIMATE_OUTLOOK_SPREAD_SIGMAS`, run/include/climate/outlook/index.h | |
| `CLIMATE_OUTLOOK_CALIBRATION_LOW`, `_HIGH` | 0.6, 0.8 | ratio | 10 (phase 07) | Events given 70% must happen this often (a test bound) | Literals 0.60 and 0.80 in `tests/run/climate/outlook/index.c`, and `SURVEY_BIN_LOW`, `SURVEY_BIN_HIGH` in `bench/climate/survey/`: a test's bound, not a game number. Not pinned | |
| `CLIMATE_OUTLOOK_REACH` (phase 07) | 50000 | blocks | 8 | Any system that can reach here in 7 days starts within this | `CLIMATE_OUTLOOK_REACH`, run/include/climate/outlook/index.h | |
| `CLIMATE_OUTLOOK_CELL_RADIUS`, `_CELL_HOURS` (phase 07) | 275, 2.5 | blocks; hours | 8 | A thunderstorm cell's mean size and life in the forecast: the knob for the over-forecast thunderstorms (Results) | run/include/climate/outlook/index.h | |
| `CLIMATE_OUTLOOK_DEEP_LOW_SHARE` (phase 07) | 0.5 | ratio | 8 | Share of lows deeper than -20 hPa, for the storm chance | run/include/climate/outlook/index.h | |
| `CLIMATE_OUTLOOK_FOOTPRINT` (phase 07) | master | choice | 8 | The forecast footprint: master plan 8's $e^{-d^2/2(\sigma^2+R^2)}$, not the exact disk | run/include/climate/outlook/index.h (pinned since phase 11's second pass) | |

#### From phases 08 to 10, not in sections 4 to 8

For completeness, so every climate number is in this file:

| Name | Value | Unit | Where | What it tunes | Home | Tuned |
|---|---|---|---|---|---|---|
| `GAME_COVER_ICE_ON_CM` (phase 08) | 5 | cm | Master plan phase 08 | Ice blocks placed at the census | `GAME_COVER_ICE_ON_CM`, `run/include/game/cover/index.h`; the census's event uses `CLIMATE_ICE_EVENT_CM` 5, run/include/climate/weather/week/index.h | |
| `GAME_COVER_SNOW_ON_MM` (phase 08) | 20 | mm of water | Master plan phase 08 | Snow layer placed at the census | `GAME_COVER_SNOW_ON_MM`, `run/include/game/cover/index.h` | |
| `PLAYER_WIND_SHARE`, `PLAYER_WIND_THRESHOLD` (phase 08) | 0.04, 10 | drift per block a second; blocks a second | Phase 08 | How hard storms push (Q4: gently) | `PLAYER_WIND_SHARE`, `PLAYER_WIND_THRESHOLD`, `run/include/player/movement/index.h` | |
| `CLIMATE_WARNING_THRESHOLD` | 0.5 | ratio | 9.1, phase 10 | Storm warning, strictly over | `CLIMATE_WARNING_THRESHOLD`, `run/include/climate/warning/index.h` | |
| `CLIMATE_WARNING_TICKS` | 200 | ticks | Phase 10 | How often the warning is worked out | `CLIMATE_WARNING_TICKS`, `run/include/climate/warning/index.h` | |
| `UI_ATLAS_NOW_REDRAW_MS` | 500 | ms | 9.3, phase 09 | "Now" layers drawn again twice a second | `UI_ATLAS_NOW_REDRAW_MS`, `run/include/ui/atlas/index.h` | |
| `UI_ATLAS_SHADE_EXAGGERATION` | 1 | ratio | Phase 09 | Hill shading strength | `UI_ATLAS_SHADE_EXAGGERATION`, `run/include/ui/atlas/draw/index.h` | |
| `CLIMATE_WARNING_STEPS`, `_STEP_DAYS` | 5, 0.25 | steps; days | Phase 10 | The warning looks now and every quarter day to a day ahead | run/include/climate/warning/index.h (pinned since phase 11's second pass) | |
| The Atlas's scales: `CLIMATE_LAYER_PRESSURE_LOW_HPA`, `_MID_HPA`, `_HIGH_HPA`, `_LINE_HPA`; `_WIND_TOP`; `_LAND_TOP`; `_SNOW_NEAR_BLOCKS`, `_FAR_BLOCKS`; the `_TEMPERATURE_C`, `_RAIN_MM` and `_RADAR_MM_HOUR` stops | 990, 1013, 1030, 4; 15; 454; 50, 2000; -30 to 45 °C; 0 to 3000 mm; 0.1 to 50 mm an hour | hPa; blocks a second; blocks; °C; mm | 9.3, phases 05 and 09 | What each Atlas colour means | run/include/climate/layer/index.h (pinned since phase 11's second pass) | |

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

Filled in by this phase, **before tuning** (2026-11-07 and 08). Nothing was tuned: Ahmed has not played the gate, so steps 9 and 10 did not happen and there is no "after tuning".

**The misses, plainly:**

- **No hurricane formed in any warm coast square, on any of the 8 seeds, in 10 years.** The plan expects 2.4 a square and about 7 a coast a season. The cause is phase 06's open-sea rule (2048 blocks of water all round), which the terrain's seas almost never meet (Issues found 8; phase 06, Issues found 14 and 21).
- **The outlook over-forecasts thunderstorms.** Of thunderstorm chances of 60 to 80% (mean 68.7%), only 31.4% came true, in 24 880 forecasts. Every kind together is within the plan's bound (74.4%), as rain dominates the count.
- **Rain against normals:** 93.7% of 16 000 cells are within 15% of their normal over 10 years; the worst is 59% off. Phase 06's test (8 boxes) held every cell within 15%; spread over every class, 6.3% are not.
- **No Continental land at all** on any seed (no place with a coldest month at or below -3 °C and a warmest at or above 22 °C).
- **The storm warning takes 4.4 ms on average, 6.5 ms at its slowest, against 2 ms** (it runs on a worker, never in the tick: Issues found 9).
- **Frame rate at render distance 32: 0 of 16 rows at 120 fps in the first run (slowest 12.1 fps), 3 of 16 in the second pass's run from a cool machine** (`plains/orbit` 85.1, `deep_ocean/orbit` 41.8, the slowest clear row). The card slows to about a third under steady load (phase 08's notes), and did again before the second run's last rows; the climate itself costs the CPU, not the card.
- **The first pass had no run from a cool CPU** (Issues found 11): "first" and "second" below are two runs in a row. The second pass ran every bench again from a cool machine (the table after this one, Issues found 18).

**Benches,** release unless it says profile, through the build lock, two runs in a row:

| What | Target | First | Second | Met? |
|---|---|---|---|---|
| Climate tile, 1 worker (`bench_climate`), slowest cold | 100 ms | 30.09 ms | 29.77 ms | met |
| Systems near a point (`bench_climate --systems`), warm median (slowest) | 2 ms | 0.340 ms (0.907) | 0.341 ms (0.910) | met |
| Census, 100 km², 16 workers (`bench_climate_census`, profile), median of 5 | 250 ms | 218.6 ms | 214.8 ms | met |
| Frame rate, RD 32, before weather (`bench_world`, profile, slowest row) | 120 fps | 12.1 fps (`deep_ocean/orbit`) | one run (716 s) | missed, card slowed |
| Frame rate, RD 32, after weather (`plains/orbit` clear 21.3; rain, snow, thunder, fog) | 120 fps | 20.5, 20.2, 20.4, 21.3 fps | one run | missed, card slowed |
| Atlas first picture, 100 km² (`bench_atlas`), median (slowest) | 100 ms | 16.6 ms (18.1) | 16.8 ms (26.1) | met |
| Atlas full picture, worst layer, median (slowest) | 60 ms | 36.5 ms (39.5), `monsoon` | 44.1 ms (56.6), `temperature_day` | met |
| Atlas main frame, median (slowest) | 2 ms | 0.575 ms (1.028) | 0.713 ms (0.913) | met |
| Storm warning (`run_climate_warning` on release), slowest of 100 (mean) | 2 ms | 6.50 ms (4.40) | 6.04 ms (4.24) | **missed** |
| Terrain, 16 workers, slowest place (`bench_terrain`) | 1000 columns/s | 1097 (`snowy_taiga`) | 1122 | met |
| Sim tick p99, 100 mobs (`bench_mobs`, scattered) | 5 ms | 1.078 ms | 1.095 ms | met |
| Sim tick p99, `night/walk-mobs` (`bench_world`, profile) | 5 ms | 2.064 ms | one run | met |
| Climate memory (`bench_climate_survey`) | 32 KiB/km² | 30.52 KiB/km² of cells, 0.95 of land mask | the same (computed) | met |

**Benches from a cool machine** (second pass, 2026-11-08, Issues found 18): release unless it says profile, through the build lock, after 5 minutes with nothing heavy (load average 1.2 at the first, 0.9 at the census), on the code of `ffc94238`, one run each, exit status 0 each:

| What | Target | Cool | Met? |
|---|---|---|---|
| Climate tile, 1 worker, slowest cold | 100 ms | 30.28 ms (warm 7.56) | met |
| Systems near a point, warm median (slowest) | 2 ms | 0.337 ms (0.908) | met |
| Census, 100 km², 16 workers (profile), median of 5 | 250 ms | 183.8 ms (first census 1018.2) | met |
| Frame rate, RD 32 (`bench_world`, profile), clear rows | 120 fps | 3 of 16 met; `plains/orbit` 85.1, `plains/flight` 102.8, `jungle/orbit` 53.5, `deep_ocean/orbit` 41.8, `cave/orbit` 89.8 | missed |
| Frame rate, RD 32, weather rows (rain, snow, thunder, fog) | 120 fps | 25.4, 24.4, 23.9, 24.8 fps, on a card slowed again (every pass 3.4 to 3.7 times the clear row's, weather or not) | missed; weather's own cost not readable here |
| Atlas first picture, median (slowest) | 100 ms | 16.1 ms (17.0) | met |
| Atlas worst layer by median (slowest single picture) | 60 ms | 35.6 ms `temperature_coldest` (59.2 ms, `temperature_now`) | met |
| Atlas main frame, median (slowest) | 2 ms | 0.680 ms (1.022) | met |
| Storm warning (`run_climate_warning`, release), slowest of 100 (mean) | 2 ms | 6.32 ms (4.40) | **missed** |
| Terrain, 16 workers, slowest place | 1000 columns/s | 1134 (`snowy_taiga`) | met |
| Sim tick p99, 100 mobs scattered (`bench_mobs`) | 5 ms | 1.080 ms | met |
| Sim tick p99, `night/walk-mobs` (profile) | 5 ms | 1.974 ms | met |

From cool, only the census and the frame rate moved: the census 15% faster, the frame rate's early rows four times faster as the card had not slowed yet. The storm warning misses the same from cool.

**The survey,** `tools/test-or-build.sh timeout 7200 build/release/bin/bench_climate_survey --record bench/climate/survey/history.tsv`, exit status 0, 6435 s (774 to 816 s a seed), 0 tiles unbuilt and 0 days or censuses that failed. Its tables as it printed them:

**Seeds used:** `2198175988018515243`, `12636146071196537873`, `5569068473609503046`, `1`, `42`, `20261104`, `9999999999999`, `18446744073709551557`

Land cells a seed: 231381, 225129, 228868, 226655, 228366, 223073, 229418, 226884. Cells whose tile was not built: 0. Days or censuses that could not be worked out: 0.

**Climate class shares,** % of land, over the seeds:

| Class | Mean | Smallest | Largest | Biome disagrees, % |
|---|---|---|---|---|
| Ice | 27.66 | 26.40 | 28.44 | 17.6 |
| Tundra | 12.60 | 12.10 | 13.35 | 16.4 |
| Desert | 13.10 | 11.90 | 15.70 | 81.0 |
| Steppe | 10.54 | 10.13 | 10.92 | 80.9 |
| Rainforest | 5.37 | 4.98 | 6.20 | 16.3 |
| Monsoon | 2.62 | 2.41 | 2.85 | 16.9 |
| Savanna | 9.10 | 8.11 | 9.68 | 16.8 |
| Mediterranean | 6.06 | 5.45 | 6.48 | 21.5 |
| Temperate | 11.11 | 10.20 | 11.90 | 17.9 |
| Continental | 0.00 | 0.00 | 0.00 | 0.0 |
| Boreal | 1.83 | 1.48 | 2.17 | 19.9 |

Biome disagrees: a desert or badlands biome in a class that is not Desert or Steppe, or another biome in Desert or Steppe; pooled over the seeds.

**Hurricanes,** 16 warm coast squares and 8 coasts over the seeds (16 squares reach the hurricane band):

| Measure | Plan | Mean | Smallest | Largest |
|---|---|---|---|---|
| Per warm coast square, a season | 2.4 | 0.00 | 0.00 | 0.00 |
| Per warm coast, a season | about 7 (three squares) | 0.00 | 0.00 | 0.00 |
| Making landfall, a season per coast | - | 0.00 | 0.00 | 0.00 |

**Thunderstorm days a year,** at up to 200 land cells per class per seed, picked by hash:

| Class | Cells | Mean | Smallest | Largest |
|---|---|---|---|---|
| Ice | 1600 | 0.00 | 0.00 | 0.00 |
| Tundra | 1600 | 0.01 | 0.00 | 11.60 |
| Desert | 1600 | 21.24 | 0.00 | 104.40 |
| Steppe | 1600 | 20.58 | 0.00 | 83.50 |
| Rainforest | 1600 | 52.25 | 5.80 | 122.90 |
| Monsoon | 1600 | 40.24 | 1.50 | 110.10 |
| Savanna | 1600 | 44.35 | 1.30 | 106.70 |
| Mediterranean | 1600 | 3.68 | 0.00 | 57.50 |
| Temperate | 1600 | 9.14 | 0.00 | 88.80 |
| Continental | 0 (fewer on some seeds) | 0.00 | 0.00 | 0.00 |
| Boreal | 1600 | 0.01 | 0.00 | 2.00 |

**Outlook calibration,** 200 places per seed, every census of 10 years, the 7 days ahead, every kind together:

| Chance | Count | Mean chance | Happened | Two standard errors | |
|---|---|---|---|---|---|
| 0 to 10% | 60432498 | 0.4% | 0.2% | 0.0% |  |
| 10 to 20% | 2103655 | 14.7% | 12.1% | 0.0% |  |
| 20 to 30% | 1484094 | 24.4% | 25.4% | 0.1% |  |
| 30 to 40% | 763843 | 34.7% | 40.7% | 0.1% |  |
| 40 to 50% | 561925 | 44.9% | 50.1% | 0.1% |  |
| 50 to 60% | 454684 | 54.8% | 58.4% | 0.1% |  |
| 60 to 70% | 385351 | 64.9% | 70.4% | 0.1% |  |
| 70 to 80% | 312804 | 74.7% | 79.4% | 0.1% |  |
| 80 to 90% | 257702 | 85.1% | 86.3% | 0.1% |  |
| 90 to 100% | 206515 | 94.1% | 90.0% | 0.1% |  |

The 60 to 80% bin, by kind (the plan's bound: it happens 60 to 80% of the time):

| Kind | Count | Mean chance | Happened | Two standard errors | |
|---|---|---|---|---|---|
| rain | 527130 | 69.2% | 76.8% | 0.1% |  |
| thunderstorms | 24880 | 68.7% | 31.4% | 0.6% |  |
| storms | 14867 | 69.5% | 70.6% | 0.7% |  |
| hurricanes | 4 | 69.1% | 75.0% | 43.3% | not judged (under 100) |
| named systems | 131274 | 69.5% | 73.5% | 0.2% |  |
| all together | 698155 | 69.3% | 74.4% | 0.1% |  |

All kinds, 60 to 80%: 74.4% happened, within the bound.

**Long-run rain against normals,** at the thunderstorm cells, 10 years: the largest error 59.3%, and 14986 of 16000 cells (93.7%) within 15%.

**Climate memory:** 128 bytes a cell, 256 cells a tile of 1.0486 km^2: 30.52 KiB a km^2 of cells (target 32), and 0.95 KiB a km^2 of land mask.

**1 worker against 16** (by hand, as the file asks): `bench_climate_survey --seeds 2 --years 1 --width 4096 --places 20 --per-class 20` with `--workers 1` (152 s) and `--workers 16` (21 s): **the same bytes**, sha256 `f65a8c25a390ede67a97c414e7e614e91856a792ed43dc60195cdd7c5b2f4cec` both. The narrower strip and fewer places are because 1 worker with the defaults takes about 45 minutes (Issues found 13). The margin checks of Issues found 13 printed the same bytes too.

**Summary for Ahmed** (step 8). The climate's numbers now have one home each and a test that pins them; nothing was tuned. Against the plan:

- **Classes:** about 28% of land is Ice and 13% Tundra, because the strip runs pole to pole and latitude rises evenly with y; Desert 13%, Steppe 11%, Temperate 11%, Savanna 9%, Mediterranean 6%, Rainforest 5%, Monsoon 3%, Boreal 2%, Continental 0%. The 8 seeds agree within 1 to 4 points. Continental never happens: no land has both a coldest month at or below -3 °C and a warmest at or above 22 °C. Not looked into further; the inland swing knots or the -3 and 22 thresholds are where it would be decided. Most Desert and Steppe land has no desert biome (81%), as master plan 2.6 expects.
- **Hurricanes:** none. Each seed's strip has 2 warm coast squares (one each side of the equator), and none spawned a hurricane in 10 years, because none of their sea has 2048 blocks of water all round. **This is far from the plan's 2.4 a square, so a proposal:** lower `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS` (for example to 512) or test only the spawn point; then measure again with this bench. It would move phase 06's pinned digests. Ahmed's call (Q5).
- **Thunderstorms:** many in Rainforest (52 days a year), Savanna (44), Monsoon (40), and in Desert and Steppe (21) where they are hot; almost none in Ice, Tundra and Boreal, as the plan expects. Hot deserts with 21 thunderstorm days are more than real deserts get; if that feels wrong in play, the thunder rate's wetness term is the knob, not the normals.
- **Outlook:** well calibrated for rain, storms and named systems (60 to 80% came true 76.8%, 70.6% and 73.5%), but **thunderstorm chances are about twice too high** (68.7% forecast, 31.4% happened). A proposal, only if Ahmed wants: lower the outlook's cell footprint (`CLIMATE_OUTLOOK_CELL_RADIUS` 275 or `CLIMATE_OUTLOOK_CELL_HOURS` 2.5), which changes only the forecast, not the weather.
- **Rain:** 93.7% of cells within 15% of their normal; the worst 59%. No proposal: master plan 7.4 tunes spawn rates, and phase 06's rates already were.
- **Cost:** every climate bench meets its target except the storm warning (4.4 ms against 2, off the sim thread). Terrain and the sim tick keep their budgets. The frame rate misses because the card was slowed during the run; phase 08's weather costs 0 to 0.75 ms a frame on top (fog nothing, rain 0.5, snow and thunder 0.75; phase 08, Issues found 17). From a cool machine 3 of 16 rows met 120 fps.

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

- [ ] Read the vision file, master plan 4 to 8 and 10 to 12, and every earlier plan 004 phase file. Done in part: the master plan in full, this file, and the parts of phases 01 to 10 that name constants, benches and tests. The second pass also read the title of every issue in every earlier phase file, read in full each one that mentions phase 11 or a bench miss, and checked every constant name in phases 01 to 10 against the code and this test by script (Issues found 16). Still not every earlier phase file end to end, so not ticked
- [x] Fill the Home column of the constant table from the code, and commit it before changing code (`c025dc6b`, before the code commit `37299396`; the working tree was edited first: Issues found 2)
- [x] Every constant in sections 4 to 8 lives in one place each, named, with its unit (Issues found 6 and 7; the sweep finds 0 unnamed numbers, 256 before), and the phases' own in the table (Issues found 16)
- [x] `tests/run/climate/constants/`: every constant pinned, derived constants checked, literal sweep, passing (13 tests, 441 checks since the second pass)
- [x] `bench/climate/survey/`: the strip, 8 seeds, four measurements, same output on 1 and 16 workers (on a narrower strip: Results)
- [x] Re-measure, over 8 seeds: climate class shares, hurricanes a season per warm coast, thunderstorm days a year by class, and outlook calibration. Record them
- [x] Re-run every bench: climate tile, systems, census, Atlas, frame rate, plus terrain and the sim tick, cool and hot. Record them (first pass twice in a row, not cool, Issues found 11; second pass once each from a cool machine, Issues found 18)
- [x] Summary for Ahmed, with every miss, and proposals only where a number is far off
- [ ] Each value Ahmed tuned: constant, table, test and Issues found entry, with before and after. Ahmed has tuned nothing yet
- [ ] Measurements and benches after tuning, recorded. Nothing tuned, so nothing to run yet
- [x] READMEs and `plans/REPO_STRUCTURE.md` updated
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
    tools/test-or-build.sh timeout 600 build/release/bin/bench_climate_survey --seeds 2 --years 1 --width 4096 --places 20 --per-class 20 --workers 1
    tools/test-or-build.sh timeout 600 build/release/bin/bench_climate_survey --seeds 2 --years 1 --width 4096 --places 20 --per-class 20 --workers 16
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

- `c025dc6b` Fill the Home column of plan 004 phase 11's constant table from the code, with the values phase 06 tuned in the Tuned column.
- `37299396` Give every number in the calendar and climate code a name in one home, with the knot tables and the shared year, week and hour made from the calendar's, and pin them all with the new run_climate_constants test and its literal sweep.
- `d5c52706` Add bench_climate_survey, plan 004 phase 11's four measurements over 8 seeds on a pole-to-pole strip, the same bytes on 1 worker or 16.
- `8fadcbd0` Say in the repository structure where run_climate_constants and bench_climate_survey are and what they do.
- `ad79538e` Record plan 004 phase 11's bench runs in each bench's README and history: the survey over 8 seeds, the climate tile, systems, census, Atlas, terrain, mobs and the world's frame rate.
- `61f7becc` Record plan 004 phase 11's results before tuning, the summary for Ahmed with every miss, its issues, commits and verification in its phase file.
- `7e9eb18e` List the last commit of plan 004 phase 11 in its phase file.
- `6a3916a7` Pin the climate's remaining tuning numbers in run_climate_constants (second pass).
- `ffc94238` Say in the outlook's header that the master plan's footprint is the one in force (second pass).
- `6c3d5656` Record plan 004 phase 11's benches run again from a cool machine in each bench's README and the world's history (second pass).
- `858a5543` Record plan 004 phase 11's second pass in its phase file.
- `cf4e060c` Record the gate check of plan 004 phase 11: the 8-seed survey and the world bench run again on 73cd23cb.
- `445ee226` Record plan 004 phase 11's gate check in its phase file, with the Verification's 1-worker and 16-worker lines given the narrow strip that can finish.

## Issues found

1. **Phase 06 changed five values without Ahmed.** Expected (this file, "What this phase must not do"): no value changes unless Ahmed decides. Actual: phase 06's agent tuned `CLIMATE_HURRICANE_RATE` 0.02 to 0.034, `CLIMATE_LOW_RATE_WINTER` and `_SUMMER` 0.08 and 0.04 to 0.18 and 0.09, `CLIMATE_THUNDER_RATE_GAIN` 1 to 0.22, `CLIMATE_LOW_POLEWARD` 0.15 to 0.08 and the f_sys blend 20° to 35° to 25° to 50° (phase 06 file, Issues found 12 and 14, commit `1e4f94d7`), to meet master plan 7.4's 15% rain test. Done: the values the code holds are the ones pinned, and the table's **Tuned** column says "by phase 06's agent" for each. Nothing changed here. **For Ahmed:** keep them (the rain test holds with them) or say which to put back; a change to any rate means re-running `run_climate_systems_rain`.

2. **The code was edited before the Home column was committed.** Expected (step 2): the audit committed before any code changes. Actual: the audit was done from the code as merged (`b3236a90`), and the literal work began in the working tree before the table was written into this file. Done: the table went in first in history (`c025dc6b`), and the code after it (`37299396`). The Home column describes the code after phase 11 (it names the new homes); where a constant moved, the column says from where.

3. **Names differ from the table's proposals.** Expected (Design): keep the code's names. Done: kept, and written in **Home** (for example `CLIMATE_TILT_DEG` for `CLIMATE_TROPIC_DEG`, `CLIMATE_WALK_STEPS_WINDOW` for `CLIMATE_UPWIND_STEPS`). One exception: the table's knot tables were lower case (`climate_pressure_knots`); a constant is upper case by the coding standard, section 6, so they are `CLIMATE_PRESSURE_KNOTS`, `CLIMATE_TEMPERATURE_KNOTS`, `CLIMATE_SWING_SEA_KNOTS`, `CLIMATE_SWING_LAND_KNOTS` and `CLIMATE_RAIN_KNOTS`, moved from `run/src/climate/normals/index.c` into its header so the test can read every knot. The curves are built from the same numbers: every climate digest held (12 below).

4. **Five rows are not constants in the code.** `CALENDAR_TICKS_PER_DAY` is `GAME_DAY_SECONDS × MC_GAME_TICK_HZ` (the climate keeps `CLIMATE_REAL_SECONDS_A_DAY`, pinned equal, as it cannot include `game/`); `CLIMATE_SYSTEM_REACH_BLOCKS` and `CLIMATE_SYSTEM_LIFE_MAX_DAYS` are worked out by `climate_systems_reach` and `systems_life_max` from the life, speed and radius constants; `CLIMATE_FLOOD_BANKS_FULL` is 1 by the flood index's definition; `CLIMATE_RAIN_CHECK_TOLERANCE` and `CLIMATE_OUTLOOK_CALIBRATION_LOW`, `_HIGH` are test bounds in their tests. Each says so in **Home**; the test pins what each comes from.

5. **The literal sweep's allowlist is longer than the file's.** Expected (Design): 0, 1, 2, -1, 0.5, the smoothstep's 3 and 2, 180 and 360, π. Actual: that list fails on the coding standard's own numbers and on every named definition. Done: three more entries, each with its reason in the test: the numbers of named definitions outside function bodies (a file's `static const` tables and scalars, enum members, `#define`s: each is the named constant the rule asks for), array subscripts (`w[3]`; an array's *size* is still checked), and `static_assert` lines and `_padN` members (the coding standard requires their numbers). The sweep tries itself on a planted sample (4 found of 4). On the code before this phase it finds **256** numbers with no name; after, 0.

6. **Eight `static const` floats became `#define`s.** Expected (Design): do not convert one form to the other just for this phase. Actual: `climate_layer_scale`'s static tables typed 8192, 990, 1013, 1030, 15, 454, 50 and 2000 again, because a C static initializer cannot read a `static const`. Done: `CLIMATE_SEA_DISTANCE_MAX`, `CLIMATE_LAYER_PRESSURE_LOW_HPA`, `_MID_HPA`, `_HIGH_HPA`, `CLIMATE_LAYER_WIND_TOP`, `CLIMATE_LAYER_LAND_TOP`, `CLIMATE_LAYER_SNOW_NEAR_BLOCKS` and `_FAR_BLOCKS` are macros, so the tables are built from them. Same values.

7. **Duplicates removed.** The year was typed 365 in five parts (`CLIMATE_YEAR_DAYS`, `CLIMATE_SYSTEMS_YEAR_DAYS`, `CLIMATE_HEAT_YEAR_DAYS`, `CLIMATE_DAYS_A_YEAR`, `CLIMATE_SEASON_DAYS`), the week twice, 24 hours in five files, 170 twice (`CLIMATE_SOLSTICE_DAY`, `CLIMATE_SUMMER_DAY`), 79 in `sun/`, 6 mm in the heat and the outlook, 0.2 for a high's cut and for cells under a high, 23.5 and 66.5 in the latitude lines, 3 × 400 for a front's reach. Each is now made from one home (the calendar's for days and hours; `CLIMATE_POLAR_CIRCLE_DEG`, new, for 66.5), and `run_climate_constants` checks they agree. New names for code mechanics (alignment, corners, a byte's 255, a key's 32 bits, the hash constants, the Atlas's pixel bytes, cloud amounts of phase 08) are in the headers of the parts that use them.

8. **Hurricanes: none in any warm coast square of the survey.** See Results and the summary: the plan's 2.4 a square a season is not reached because hurricanes only spawn on open sea (`CLIMATE_HURRICANE_OPEN_SEA_BLOCKS`, 2048 blocks of water all round), which the terrain's patchy seas almost never give. Phase 06's Issues found 14 and 21 found it near spawn; the survey confirms it over 8 seeds. Not changed: Ahmed's (Q5).

9. **The storm warning misses its 2 ms on release.** Expected (master plan, phase 10): 2 ms. Actual: slowest 6.5 and 6.0 ms, mean 4.4 and 4.2 ms, of 100 warnings (`run_climate_warning` on release, twice). It runs on a worker, never in the sim tick (phase 10, Issues found 13), so no tick waits for it, and the sim tick at 100 mobs meets its 5 ms. Left for Ahmed: whether the 2 ms matters for a job off the sim thread; phase 10's file names the cost (the known list and the unborn lows' `exp`s).

10. **Memory: 30.5 KiB a km², not 32.** The plan's 32 KiB counts a 1024-block tile as a km²; it is 1.0486 km². Met either way.

11. **No run was from a truly cool CPU.** Expected (Background): the first run after 5 minutes with nothing heavy. Actual: every heavy command shares one machine-wide lock with other agents' worktrees, and this phase's own builds came straight before; the release benches started at load average 2.3, falling from 9.7. Done: each bench was run twice in a row and both are recorded; the columns say "first" and "second".

12. **No digest or golden moved.** Expected (step 3): every pinned digest holds, as no value changed. Actual: `run_calendar`, every `run_climate_*`, `run_game_census`, `run_game_weather`, `run_save_climate`, `run_ui_atlas`, `run_ui_atlas_game`, `run_ui_census`, `run_ui_hud`, `run_ui_hud_corner` and `render_feature_weather` pass on debug with the code after the change (the knot tables' move included), and the whole suite in Verification.

13. **The survey's own choices.** A thunderstorm cell's reach in a day is 4200 blocks, so phase C builds 5120 blocks round each band; `--margin 9216` printed the same bytes on 2 seeds over 1 year (40 places, 40 cells a class), and phase A's 1-tile margin printed the same bytes as 9216 on Ahmed's seed with the defaults over 1 year. The 1-worker and 16-worker check ran on a 4096-block strip with 20 places and 20 cells a class: with the defaults, 1 worker would take about 45 minutes, past the file's 600 s. A seed takes about 13 minutes with the defaults, over the file's 10 minutes; nothing was halved, as the whole run fits 7200 s (Results).

14. **Biomes disagree with the classes, as expected.** Most Desert and Steppe land does not have a desert biome, and some desert biome sits in wet classes (Results). Master plan 2.6: the vegetation plan's. Nothing done.

15. **Verification, run 2026-11-07 and 08, every command through `tools/test-or-build.sh`.** Debug configure and build: clean. `run_climate_constants`, `standards_lint`, `clang_format`: 3 of 3. The whole debug suite (`ctest --preset debug`, 3400 s): **151 of 151 pass** in 2268 s, on the code of `ad79538e`. Release configure and build (`-O3 -flto`, main's 16 KiB frame check): clean, before the survey and again at the end; nothing here touches `app_t`. Profile configure and build: clean. The survey, its 1-worker and 16-worker runs, `bench_climate`, `--systems`, `bench_atlas`, `bench_terrain`, `bench_mobs`, `run_climate_warning` on release, `bench_climate_census` and `bench_world --record` on profile: as in Results (the warning and the frame rate miss). `pgrep -af "$PWD/build"` and `pgrep -a minecraft_c` print nothing afterwards. Not run by the agent: Ahmed's gate (`./build/release/bin/minecraft_c --windowed`).

16. **The table and the test missed some of the phases' own numbers (second pass).** Expected (Design): every phase-own constant a row in its section, "so this table has every climate number", and pinned. Actual: a script listed every `CLIMATE_` and `CALENDAR_` name in phases 01 to 10 and every value constant in the climate headers against the test: 41 named numbers had no pin, and 49 names the test pins had no row; the unpinned ones change what the game does (among them `CLIMATE_HURRICANE_OPEN_SEA_BLOCKS`, the knob behind Issues found 8; the drift stops, spiral bands, rain shield and front turn of phase 06; the rain noises' sizes, the fog and cloudy-day rules and the spin-up of phase 07; the outlook's footprint choice; the warning's steps; the Atlas's colour scales). Done: a row each in its section, marked "(pinned since phase 11's second pass)" where the pin is new, and the pins in `run_climate_constants` (`6a3916a7`; 441 checks, was 369), with the calendar's float copies, `CLIMATE_HARMONIC_HALF_DAYS`, the spin-up weeks and the warning's one day checked against their sources. Left unpinned on purpose, as mechanics, not climate numbers: kinds and flags, buffer and table sizes, colours, pixel sizes, `CLIMATE_DIRECTION_EPSILON`, `CLIMATE_SUN_POLE_COS`, `CLIMATE_UNIFORM_SCALE` (2^-53), `CLIMATE_SEASON_DRIEST_START_MM` (a sentinel) and `CLIMATE_THUNDER_QUERY_MAX` (a list bound). No value changed.

17. **Phase 10's note for this phase: a stale comment.** Phase 10, Issues found 31: `run/include/climate/outlook/index.h` said the exact footprint is in force while the code uses the master plan's. Done (`ffc94238`): the comment now says the master plan's form is in force, as phase 07's calibration passed with it (phase 07, Issues found 14); `run_climate_constants` pins the choice. Comment only.

18. **Every bench again from a cool machine (second pass, 2026-11-08).** Expected (Background): each bench cool and hot. Actual: the machine was idle (load average 0.2 before this pass's builds); after the release and profile builds, 5 minutes with nothing heavy (load average 1.4), then every bench once through the lock. Results: the table "Benches from a cool machine". Every met target is still met; the census is 15% faster cool (183.8 against 218.6 ms); the storm warning still misses (6.3 ms slowest, 4.4 mean, against 2). `bench_world`: 3 of 16 rows met 120 fps against 0 in the first run, because its early rows ran before the card slowed; `nvidia-smi` mid-run read 1575 of 2100 MHz with the power-cap and thermal slowdown reasons, and the weather rows, last, have every pass 3.4 to 3.7 times the clear row's, so they cannot show weather's cost (phase 08's like-with-like runs stand: rain +0.49 ms, snow +0.75, thunder +0.71, fog 0). The summary said weather costs "1 to 3 ms a frame"; phase 08's numbers are 0 to 0.75 ms, and the summary now says so. Recorded in each bench's README and as `ffc94238: plan 004 phase 11, second pass, cool machine` in `bench/world/history.tsv`. The survey was not run again: no climate number changed, and every climate test passes on the same digests.

19. **Verification of the second pass, 2026-11-08, every command through `tools/test-or-build.sh`.** Main was already in; the first pass's branch `worktree-wf_6f8a0c68-47a-58` merged as a fast-forward. Debug configure and build: clean, no warnings. `run_climate_constants`, `run_climate_outlook`, `standards_lint`, `clang_format`: 4 of 4 after the last change. The whole debug suite: **151 of 151 pass** in 2329 s. Release configure and build (`-O3 -flto`, main's 16 KiB frame check): clean, on `ffc94238`; nothing here touches `app_t`. Profile configure and build: clean. Every bench once from a cool machine (Issues found 18). Not run again: the 8-seed survey and its 1-worker and 16-worker check, as no climate number or code path changed (test pins and a comment only). `pgrep -af "$PWD/build"` and `pgrep -a minecraft_c` print nothing afterwards. Not run by the agent: Ahmed's gate.

20. **Gate check, 2026-11-08** (a separate agent, worktree `wf_6f8a0c68-47a-61`, on `73cd23cb`, main merged, clean `build/`; every command through `tools/test-or-build.sh`).
    - **Builds:** debug, release (`-O3 -flto`, main's 16 KiB frame check) and profile configure and build clean.
    - **Tests:** `run_climate_constants`, `standards_lint`, `clang_format` 3 of 3; `run_climate_constants` 13 tests, 441 checks. The whole debug suite **151 of 151** in 2187 s.
    - **The sweep is real:** with `CLIMATE_LAPSE_C_PER_BLOCK * z` changed to `0.012f32 * z` in a copy of `run/src/climate/heat/`, the sweep named `heat/index.c:11: 0.012f32` and the test failed (1 of 13).
    - **Worked examples through the real code** (a small program linked to the debug objects): 6.6 example 2, the 45° peak, -10.50 °C in January and 8.56 in July (plan -10.4 and 8.4); example 3, 150 below sea level at 28°, 34.62 °C (plan 34.5) and a daily range of 11.44 (plan 11.4); example 1, the equator beach 25.96 to 27.96 (plan 26 to 28); `CLIMATE_POLE_Y` 38 297.87, latitude 23.5° at y = 10 000 and 90° at the pole; s_hur 1 at 28.5 °C, 0.5 at 27.5, 0 at 3° and 30°; `climate_cell_t` 128 bytes; the calibration example's two standard errors 1.32%. `P_belt(25°)` is 426.7, not the plan's 500: phase 05's known spline note (its Issues found 1).
    - **1 worker against 16:** the narrow run printed the same bytes, sha256 `f65a8c25…4cec`, **the same as recorded** in Results (156 s and 21 s).
    - **The full survey again** (`--record`): exit 0, 6450 s, every seed's land cells and every table the same as Results, so the 8-seed numbers are confirmed, including 0 hurricanes and the thunderstorm over-forecast.
    - **Benches:** tile 30.28 ms; systems 0.348 ms median (0.927 slowest); census 205.1 ms (profile); Atlas first picture 16.8 ms, worst layer by median 36.5 ms (`hurricane_season`), but one `temperature_now` picture at 60.6 ms, just over 60 (the target is judged on the median, as before); terrain 1058 columns/s; mobs p99 1.095 ms; `night/walk-mobs` tick p99 2.25 ms; storm warning slowest 6.44 ms, mean 4.52 (**missed**, as Issues found 9); `bench_world` 1 of 16 rows at 120 fps (`jagged_peaks/orbit` 126.5), the card slowed and the machine shared, as Issues found 18. Recorded as `73cd23cb: plan 004 phase 11, gate check` in `bench/world/history.tsv` (`cf4e060c`).
    - **Climate class layer drawn and looked at** (`bench_climate --pictures … 2198175988018515243 --layer climate_class`): rainforest and savanna round the equator, desert and steppe towards ±19°, and the patchy seas that explain Issues found 8.
    - **Defect fixed:** Verification ran the 1-worker and 16-worker check at the default strip under `timeout 600`, which cannot finish (about 45 minutes, Issues found 13). The two lines now give the narrow strip that was really run and recorded.
    - **Not changed, for Ahmed:** no hurricanes on any seed (Issues found 8, phase 06's open-sea rule against master plan 7.2's "Sea, s_hur > 0"), phase 06's rates (Issues found 1), the storm warning's 2 ms, the frame rate, and the gate.
    - **Afterwards:** `pgrep -af "$PWD/build"` and `pgrep -a minecraft_c` print nothing; no display server was started.
