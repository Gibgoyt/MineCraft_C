# 004-ahmed-bohemoth-climate-system — phase 1: Calendar and the weekly pause

**Status:** done, waiting on Ahmed's gate   (updated 2026-11-06)

Read `plans/004-ahmed-bohemoth-climate-system-phase-00-vision-and-plan-files.md` first, then section 4 of the master plan, `plans/004-ahmed-bohemoth-climate-system.md`. This file is the breakdown of the master plan's phase 01. Where the code made part of the master plan impossible as written, this file says so in **Issues found**, and the design below follows the default given there.

## Goal

The game knows the date. A world has a year, a day of the year, a week number and a season, all worked out from one saved world clock. The HUD's top-right corner shows a line such as `Year 1, Day 80, Early spring`, and a Settings box turns it off. Every time the week number goes up, at 06:00 on the world clock, the world stops: no tick runs, and a census screen opens over the last picture with the date, the week and a **Continue** button. Continue starts the world again. Sleeping, a jump in time and a save and load can neither skip a census nor make one fire twice. The census does no climate work yet: it calls one empty hook per week, in order, which phase 07 fills. `run/src/terrain/climate/` has been renamed `run/src/terrain/fields/`, and every pinned terrain digest is unchanged.

## Where this fits

The vision file's stack is strictly top to bottom: terrain, then climate, then vegetation, settlements, economy and the player. The calendar sits at the top of plan 004 because **everything below it is a function of the date**:

- **Phase 02** reads the day of the year for the sun's declination, $\delta(d)$.
- **Phases 04 to 06** read it for the season of every wind belt, heat low, temperature, rain and storm.
- **Phase 07** fills this phase's census hook: each week, for every generated cell, it works out snow, soil water, runoff, ice and floods, and saves them. The census screen then shows the report and the outlook.
- **Phase 10** adds the climate line and storm warnings under this phase's date line.

The vision file says why the week matters past plan 004. The **census is the economy's heartbeat**: the economy plan's tasks "are checked by the vitals at the next census, not by what was built", the flood index that destroys houses is worked out at the census, and seasons open and close trade routes and mountain passes. The settlements plan puts **the settlement's name above the date line** this phase draws. So the date line, the week number and the census trigger must be exact and dependable, even though nothing heavy runs in the census yet.

**This phase must NOT build:**

- Any climate number: no latitude (phase 02), no temperature, wind or rain, no weekly weather state (phase 07), no outlook (phase 07). The census hook is called and does nothing.
- The tropical "wet season / dry season" label (it needs rain normals, phase 05), the "polar night / midnight sun" label (it needs the sun, phase 02), and the "Hurricane season" and "Monsoon" second labels (phase 05).
- The settlement name, the climate line or storm warnings (later plans and phase 10).
- Any change to what the terrain generates. The rename changes names only.

## Ahmed's words

- "every 7 days, when the sunrises, the entire world is paused, pause screen pops up everything is re-computed"
- "A single year is 365 days, altitude, distance from the ocean or any considerably large water bodies must somehow affect this hard-coded at generation time so we don't waste runtime compute"
- Decision, 2026-11-04: "**365 days.** The census every 7 days"
- Master plan 2.2, accepted: the census fires at **06:00 on the world clock**, because in polar winter the sun never rises.
- Master plan Q1 (default yes): rename `terrain/climate/` to `terrain/fields/`, "so there aren't two 'climate' modules".
- Master plan Q2 (default yes): "New worlds start on the spring equinox, day 80".
- Vision file: "The HUD's top corner will show the settlement's name above the date line that plan 004 adds."

## Background, explained

Each idea this phase uses, in the order the design needs it.

### Ticks, frames and the three threads

A **tick** is one step of the simulated world. The game runs 20 ticks a second (`tick_hz: 20` in `config/machine.yaml`). A **frame** is one picture on the screen, as many a second as the display allows.

A **thread** is one line of work the processor runs at the same time as others. This game has three that matter here (`plans/REPO_STRUCTURE.md` section 12):

| Thread | What it does |
|---|---|
| `main` | Reads the keyboard and mouse, draws the HUD and menus, and **keeps the time of day** |
| `sim` | Runs the ticks: the player, mobs, items, blocks, saving |
| `gpu` | Drives the graphics card to draw each picture |

Two threads must never write the same memory at once. Values that one thread writes and another reads are **atomics** (C's `_Atomic`): a read always sees a whole value, never half of an old one and half of a new one. Each atomic says its **memory order**. *Relaxed* means only that one value is whole. *Release* on a write and *acquire* on the read mean "everything I wrote before this is visible to whoever sees this". This phase uses relaxed for the clock, since nothing else depends on it arriving with it, and release and acquire for the census signal.

### How the game keeps time today

A day is `GAME_DAY_SECONDS` = 1200 seconds, which is 24 000 ticks at 20 Hz (`run/include/game/index.h:34-35`). The time of day is a **fraction**: 0 is midnight, 0.25 is 06:00, 0.5 is noon, 0.75 is 18:00. A new world starts at `GAME_DAY_START` = 0.35, which is $0.35 \times 24 = 8.4$ hours, so 08:24.

The fraction is **main's**, in `game_t.day_fraction`. Main moves it on by the real seconds of each frame in `game_day_advance` (`run/src/game/index.c:342`). It hands it to the other threads as `day_units`, the fraction times $2^{32}$, so that it fits a 32-bit atomic:

$$\text{day\_units} = \lfloor f \times 2^{32} \rfloor, \qquad 06{:}00 \Rightarrow 0.25 \times 2^{32} = 2^{30} = 1\,073\,741\,824$$

When the player sleeps through a night, the sim raises `morning_latch`, and main sets the fraction to `PLAYER_SURVIVAL_MORNING` = 0.25, 06:00 (`run/src/game/index.c:343-344`). A **latch** is a flag one side sets and the other side clears once it has acted on it.

The tick counter `world_ticks` (in `game_t.tick_count`, saved in `save_state_time_t`) is **not** the clock. It counts ticks the sim ran, and the time of day moves separately. Sleeping moves the time of day by up to 9 hours and adds no ticks. Turning the "Day cycle" effect off stops the time of day while ticks go on. This is why this phase adds its own whole-day count (Issues found 2).

### The world clock

This phase adds one number main keeps beside the fraction: `clock_days`, the whole days since the midnight before the world's first morning. Together they are the **world clock**, in days:

$$c = \text{clock\_days} + f$$

A new world starts at $c = 0 + 0.35 = 0.35$. For the other threads, main packs both into one 64-bit atomic, `clock_units`: whole days in the top 32 bits and `day_units` in the bottom 32:

$$\text{clock\_units} = \text{clock\_days} \times 2^{32} + \text{day\_units}$$

**Worked example.** The clock 5 days and 6 hours in, $c = 5.25$: $5 \times 2^{32} + 2^{30} = 21\,474\,836\,480 + 1\,073\,741\,824 = 22\,548\,578\,304$.

How the clock moves:

- **Each frame** (or each tick with no threads): $c \leftarrow c + \text{seconds} / 1200$. When the fraction passes 1, `clock_days` goes up by one and the fraction wraps.
- **Waking from sleep**: the clock goes to the **next** 06:00. If the fraction is 0.25 or more, a day is added first. Sleeping from 23:00 ($c = 4.958$) gives $c = 5.25$. Sleeping from 01:00 ($c = 5.042$) gives $c = 5.25$ too. Either way the clock passes 06:00 exactly once.
- **`game_day_set`** (the console's and the tests' way to set the time) changes only the fraction, as it does today. A new **`game_clock_set`** sets both.
- **Paused**, or with the Day cycle effect off: the clock stands still, and so does the calendar.

### The calendar, from the clock

The master plan's formulas (section 4), with $D_0 = 79$:

$$D = D_0 + \left\lfloor c - 0.25 \right\rfloor, \qquad \text{year} = \left\lfloor \frac{D}{365} \right\rfloor + 1, \qquad d = D \bmod 365, \qquad \text{week} = \left\lfloor \frac{D}{7} \right\rfloor$$

- $\lfloor x \rfloor$ is **floor**: the largest whole number not above $x$. $\lfloor 4.9 \rfloor = 4$, $\lfloor -0.1 \rfloor = -1$.
- $a \bmod b$ is the **remainder** after dividing: $1079 \bmod 365 = 349$.
- $D$ is the **calendar day count**. The "$- 0.25$" makes each calendar day start at 06:00 on the clock, not at midnight. This is the master plan's $t_0$: written in ticks of a clock that starts at 0 when the world starts, $t_0 = 0.35 \times 24000 - 6000 = 2400$ ticks.
- $d = 0 \ldots 364$ is the **day of the year**, shown as $d + 1$, so 1 to 365.
- $D_0 = 79$ puts a new world on the spring equinox, shown as day 80 (Q2).

In integers, with no floating point, which is what the code does:

$$D = D_0 + \left\lfloor \frac{\text{clock\_units} - 2^{30}}{2^{32}} \right\rfloor$$

**Worked example 1: a new world.** $c = 0.35$: $D = 79 + \lfloor 0.10 \rfloor = 79$. Year $= \lfloor 79/365 \rfloor + 1 = 1$. $d = 79$, shown as **day 80**. Week $= \lfloor 79/7 \rfloor = 11$. The clock reads 08:24.

**Worked example 2: a world 1000 days old** (the master plan's). $c = 1000.35$: $D = 79 + \lfloor 1000.10 \rfloor = 1079$. Year $= \lfloor 1079/365 \rfloor + 1 = 2 + 1 = 3$. $d = 1079 - 730 = 349$, shown as **day 350**. Week $= \lfloor 1079/7 \rfloor = 154$ (since $154 \times 7 = 1078$).

**Worked example 3: a year rolling over.** $D = 364$: year 1, $d = 364$ (shown day 365), week 52. One calendar day later, $D = 365$: year 2, $d = 0$ (shown day 1), and week is still $\lfloor 365/7 \rfloor = 52$. The week does not restart with the year. $D = 371$ is week 53.

**Why weeks count from $D = 0$ and not from each new year:** $365 = 52 \times 7 + 1$, so weeks counted within a year would leave a one-day week at the end. Counted straight from $D$, every week is exactly 7 days, across any number of years.

### The census trigger

The sim keeps `last_census_week`, saved with the world. The rule, checked at the start of every tick:

$$\text{if } \text{week}(c) > \text{last\_census\_week}: \text{ run every week from } \text{last\_census\_week} + 1 \text{ to } \text{week}(c), \text{ in order, then set } \text{last\_census\_week} = \text{week}(c)$$

It never asks "is it exactly 06:00 this tick?". It asks "has the week number gone up?". So:

- A tick that lands just past 06:00 still sees it.
- Sleeping moves the clock to 06:00 exactly, and the next tick sees the new week.
- A big jump runs each missed week once, in order.
- Once run, a week is in `last_census_week`, which is saved, so a reload cannot run it again.

**Worked example 4: the first census of a new world.** Week 11 runs from $D = 77$ to $D = 83$. Week 12 starts at $D = 84$, which is $c = 84 - 79 + 0.25 = 5.25$: 06:00 on clock day 5, shown **Day 85**, 4 days 21 hours 36 minutes after the world started. After that, a census every 7 days: Day 92, Day 99, and so on. (See Issues found 3 for why this is not "day 7".)

**Worked example 5: a 100-day jump from a new world.** $c = 0.35 \rightarrow 100.35$, so $D = 79 \rightarrow 179$, and week $11 \rightarrow \lfloor 179/7 \rfloor = 25$. Weeks 12, 13, ..., 25 run: 14 of them, in that order, and one census screen opens.

**More than 52 missed weeks.** The master plan: "Past 52 skipped weeks, the rest run in a fast mode that only keeps snow and soil water." This phase works out which weeks run fast and passes a flag to the hook. The default (see Risks): the **newest 52 run in full, the older ones fast**, since the newest weeks decide the snow on the ground now. For a gap of 100 weeks: 48 fast, then 52 full.

### Seasons, by the day of the year

From master plan 4.1, north of the tropic:

| Season | From day $d$ | Length | Early | Mid | Late |
|---|---|---|---|---|---|
| Spring | 79 | 91 | 79 to 109 | 110 to 139 | 140 to 169 |
| Summer | 170 | 92 | 170 to 200 | 201 to 231 | 232 to 261 |
| Autumn | 262 | 91 | 262 to 292 | 293 to 322 | 323 to 352 |
| Winter | 353 | 91 | 353 to 18 | 19 to 48 | 49 to 78 |

The part is $\lfloor 3 \times \text{days into the season} / \text{length} \rfloor$: 0 early, 1 mid, 2 late. Winter wraps past the year's end: day 10 is $365 - 353 + 10 = 22$ days in, $\lfloor 66/91 \rfloor = 0$, early winter.

**South of the equator** the season is the opposite one on the same days: spring and autumn swap, and summer and winter swap. So day 262 starts spring in the south. This is exactly "half a year apart" with no rounding of 182.5 days.

**Worked example 6.** Day of the year $d = 349$ (shown day 350): autumn started at 262, so $349 - 262 = 87$ days in, $\lfloor 3 \times 87/91 \rfloor = \lfloor 2.87 \rfloor = 2$: **Late autumn** in the north, **Late spring** in the south.

This phase has no latitude. Until phase 02, "north" means $y \ge 0$ and "south" means $y < 0$, and the land between the tropics gets its hemisphere's temperate season. Phase 02 switches to the sign of the latitude, and phase 05 adds wet and dry seasons between the tropics.

### Structs, padding and `static_assert`

A **struct** is a group of named values stored one after another in memory, like a row of a table. Each value starts at an **offset**, a count of bytes from the start. The repo's rule (`plans/CODING_STANDARD.md` section 3) is that the compiler may never add hidden gap bytes ("padding"), so every gap is written out as a `uint8_t _padN[...]` array, and a **`static_assert`** checks the size and offsets when the code compiles. If a field is added in the wrong place, the build fails instead of the save format silently changing.

### The save file and its version

A world's state file (`<world>/state.<generation>`, `run/src/save/state/`) is a header, then **sections**: each a 16-byte head saying its id and length, then its bytes. The header has a **version** (`SAVE_STATE_VERSION`, now 1). A reader refuses a version it doesn't know, so an old program never misreads a new file. Each section is checked before anything in it is believed. A **checksum** (a number worked out from every byte, by a hash, so that any changed byte changes it) catches a damaged file.

The blocks have their own file with their own version, `SAVE_WORLD_VERSION` (`run/include/save/world/index.h:19`). The calendar is not blocks, so it goes in the state file (Issues found 1).

### A hook

A **hook** is a function pointer: a slot where another part of the program can put "call this function". This phase calls the slot once per census week and leaves it empty. Phase 07 puts its weekly climate work there. With nothing in it, nothing happens, and the tests put a recorder there to see which weeks were run.

### The rename

`run/src/terrain/climate/` holds six noise fields (continentalness, erosion, ridges, temperature, humidity, weirdness) that choose height and biome. They aren't climate in the sense this plan builds, and two modules called "climate" would confuse every later reader. Renaming is mechanical, **except for one trap**: each noise field's random numbers come from a **salt**, a hash of its name string, `"climate/continentalness"` and so on (`run/src/terrain/climate/index.c:90-98`, hashed by `terrain_rng_salt` in `run/src/terrain/noise/index.c:74`). Changing those strings would change every world ever generated. They stay exactly as they are.

## Design

### File layout

```
run/src/calendar/            NEW  Owner by depth, reserves nothing
├── README.md
└── index.c
run/include/calendar/
└── index.h                  NEW
run/src/ui/screen_census/    NEW  Arena tier by depth (depth 2), allocates nothing
├── README.md
└── index.c
run/include/ui/screen_census/
└── index.h                  NEW
tests/run/calendar/          NEW  ctest run_calendar
├── README.md
└── index.c
tests/run/game/census/       NEW  ctest run_game_census
├── README.md
├── index.c
└── fixtures/week.script     NEW  a headless session across one census
run/src/terrain/fields/      RENAMED from run/src/terrain/climate/ (git mv)
run/include/terrain/fields/  RENAMED from run/include/terrain/climate/
tests/run/terrain/fields/    RENAMED from tests/run/terrain/climate/; ctest run_terrain_fields
```

Each new source directory has exactly one `index.*` and a `README.md` stating purpose, tier, owns, lifetime, threads, public functions and who may call it, as `plans/REPO_STRUCTURE.md` section 3 asks. `calendar` is a new `run/` system, so it must be added to the dependency table in `compile/src/standards_lint/rules_include/index.c` (the lint's `unknown_system` rule refuses it otherwise) and to the diagram in `plans/REPO_STRUCTURE.md` section 11, at rank 11 beside `math` and `time`: it is plain arithmetic that anything may call.

### `run/include/calendar/index.h`

```c
/* See run/src/calendar/README.md. */
#ifndef MC_RUN_CALENDAR_INDEX_H
#define MC_RUN_CALENDAR_INDEX_H

#include "types/index.h"

#include <stdint.h>

enum : uint32_t {
    CALENDAR_DAYS_PER_YEAR = 365u,
    CALENDAR_DAYS_PER_WEEK = 7u,
    /* D0: the calendar day a new world starts on. 79 is the spring equinox, shown as day 80 (master plan Q2). */
    CALENDAR_DAY_ZERO = 79u,
    /* A census more than this many weeks late runs the older ones fast: master plan 4.2, and Risks in the phase 01 file. */
    CALENDAR_FULL_WEEKS_MAX = 52u,
    /* Where each season starts, north of the equator, as a day of the year. */
    CALENDAR_SPRING_FROM = 79u,
    CALENDAR_SUMMER_FROM = 170u,
    CALENDAR_AUTUMN_FROM = 262u,
    CALENDAR_WINTER_FROM = 353u,
    CALENDAR_LINE_BYTES = 64u, /* the date line, with its NUL */
};

enum : uint64_t {
    /* The clock: whole days in the top 32 bits, the time of day in 1/2^32 of a day below, as game_t.day_units has it. */
    CALENDAR_CLOCK_DAY = 1ull << 32,
    /* 06:00, where each calendar day starts: the master plan's t0. */
    CALENDAR_CLOCK_DAY_START = 1ull << 30,
};

/* The world clock: CALENDAR_CLOCK_DAY per day since the midnight before the world's first morning. */
typedef uint64_t calendar_clock_t;

/* What a clock reading comes to. */
typedef struct calendar_date_t {
    uint64_t day;         /* 0: D, calendar days since day 0 */
    uint64_t week;        /* 8: floor(D / 7) */
    uint32_t year;        /* 16: floor(D / 365) + 1 */
    uint16_t day_of_year; /* 20: d, 0 to 364; shown as d + 1 */
    uint8_t hour;         /* 22: on the clock, 0 to 23 */
    uint8_t minute;       /* 23: 0 to 59 */
} calendar_date_t;
static_assert(sizeof(calendar_date_t) == 24);

typedef enum calendar_season_t : uint8_t {
    CALENDAR_SEASON_SPRING = 0,
    CALENDAR_SEASON_SUMMER = 1,
    CALENDAR_SEASON_AUTUMN = 2,
    CALENDAR_SEASON_WINTER = 3,
} calendar_season_t;

typedef enum calendar_part_t : uint8_t {
    CALENDAR_PART_EARLY = 0,
    CALENDAR_PART_MID = 1,
    CALENDAR_PART_LATE = 2,
} calendar_part_t;

typedef struct calendar_season_label_t {
    uint8_t season; /* 0: calendar_season_t */
    uint8_t part;   /* 1: calendar_part_t */
} calendar_season_label_t;
static_assert(sizeof(calendar_season_label_t) == 2);

/* Which weeks a census late by some weeks runs. Week w runs fast if w < first_week + fast_weeks. */
typedef struct calendar_census_plan_t {
    uint64_t first_week; /* 0: last_census_week + 1 */
    uint64_t last_week;  /* 8: the week now */
    uint64_t count;      /* 16: last_week - first_week + 1, or 0 for no census */
    uint64_t fast_weeks; /* 24: the oldest this many run fast; count - CALENDAR_FULL_WEEKS_MAX if that is above 0 */
} calendar_census_plan_t;
static_assert(sizeof(calendar_census_plan_t) == 32);

/* The calendar day of a clock reading: day_zero + floor((clock - CALENDAR_CLOCK_DAY_START) / CALENDAR_CLOCK_DAY), and 0 if that
 * would be below 0. day_zero must be below CALENDAR_DAYS_PER_YEAR. */
uint64_t calendar_day_of(uint32_t day_zero, calendar_clock_t clock);

/* floor(calendar_day_of / 7). */
uint64_t calendar_week_of(uint32_t day_zero, calendar_clock_t clock);

/* All of it at once. The hour and minute are of the clock, not of the calendar day: 05:59 is still the day before. */
calendar_date_t calendar_date_of(uint32_t day_zero, calendar_clock_t clock);

/* The season by the table in master plan 4.1, for a place north (south false) or south of the equator. */
calendar_season_label_t calendar_season_temperate(uint16_t day_of_year, bool south);

/* "Early spring" ... "Late winter". Never nullptr. */
const char *calendar_season_name(calendar_season_label_t label);

/* "Year 3, Day 350, Late autumn" into out, always ended with a NUL. Returns its length. */
uint32_t calendar_date_line(const calendar_date_t *date, calendar_season_label_t label, char *out, uint64_t bytes);

/* Which weeks to run when the week is now week_now and the last census was last_census_week. count 0 if week_now is not above it. */
calendar_census_plan_t calendar_census_plan(uint64_t last_census_week, uint64_t week_now);

#endif
```

All functions are **pure**: the answer depends only on the arguments, nothing is stored, so any thread may call them. `calendar` has no `_init` and no `_deinit`, like `run/src/time/`. The only floating point is none: the clock is integers, so the same clock gives the same date on any machine.

`calendar_season_name` returns pointers into one `static const char *const` table of 12 strings. That table owns nothing (the strings are in the program's read-only data), so the One-Owner rule is not touched.

### The world clock in `game_t`

`game_t` must stay 1600 bytes (`run/include/game/index.h:200`), since `app_t` is asserted at 16000. Everything below goes into existing padding. Changed lines only:

```c
/* Phase 07's work for one census week, run on the sim thread, in order, once per week. fast is master plan 4.2's fast mode. */
typedef void game_census_week_fn(void *context, uint64_t week, bool fast);

typedef struct game_t {
    ...
    uint8_t save_failed_said; /* 877: the sim's */
    uint8_t census_screens;   /* 878: a census stops the world and waits for its screen; set before a world opens, and only read after */
    uint8_t _pad0[1];         /* 879 */
    ...
    /* A line of its own, written by main every frame. */
    float64_t day_fraction;          /* 896: main's: the time of day, from 0 to 1 */
    _Atomic uint32_t day_units;      /* 904: as today */
    _Atomic uint32_t morning_latch;  /* 908: as today */
    _Atomic uint32_t paused;         /* 912: main to sim, and the sim to itself as a census opens: no ticks while it is set; relaxed */
    uint32_t day_zero;               /* 916: D0 of this world; set as it opens, before any thread, and only read after */
    _Atomic uint64_t clock_units;    /* 920: main to gpu and sim: clock_days << 32 | day_units, a calendar_clock_t; relaxed */
    uint64_t clock_days;             /* 928: main's: whole days since the midnight before the world's first morning */
    _Atomic uint64_t census_latch;   /* 936: sim to main: the week whose screen should open, plus 1; 0 for none. release, acquire */
    uint8_t _pad1[16];               /* 944 */
    ...
    _Atomic int32_t hud_block[3];    /* 1248 */
    uint8_t _pad4[4];                /* 1260 */
    game_census_week_fn *census_week; /* 1264: nullptr for none; set before a world opens, and only read after */
    void *borrowed_census_context;   /* 1272: passed to census_week */
    ...
    save_t *borrowed_save;           /* 1448 */
    uint64_t last_census_week;       /* 1456: the sim's: the newest week a census has run for; saved */
    uint64_t census_runs;            /* 1464: the sim's: weeks run since the world opened, for tests and the log */
    ...
} game_t;
static_assert(sizeof(game_t) == 1600);
static_assert(offsetof(game_t, clock_units) == 920);
static_assert(offsetof(game_t, census_week) == 1264);
static_assert(offsetof(game_t, last_census_week) == 1456);
```

The days fit in 32 bits of `clock_units` for $2^{32}$ days, about 11.7 million years. `game_clock_set` asserts the days are below that.

**Who owns what:**

| Value | Written by | Read by |
|---|---|---|
| `day_fraction`, `clock_days` | main | main |
| `day_units`, `clock_units` | main, every frame, relaxed | gpu (picture), sim (beds, census) |
| `day_zero`, `census_screens`, `census_week` | main, before the threads start | everyone |
| `last_census_week`, `census_runs` | sim | sim, and `game_save_parts` on the sim or with the threads stopped |
| `paused` | main (pause, Continue), sim (a census opening) | sim, gpu |
| `census_latch` | sim, release | main, acquire, by exchange to 0 |

### Changes to the clock in `run/src/game/index.c`

- `game_day_set(game, f)`: as today, then publishes `clock_units = clock_days << 32 | day_units`. It never changes `clock_days`.
- `game_day_advance(game, seconds)`, the static function main calls each frame:
  - On `morning_latch`: if `day_fraction >= 0.25`, `clock_days++`. Then the fraction is 0.25. (`>=` and not `>` so that sleeping at exactly 06:00 moves a whole day. With today's fixed sun that can't happen, since 06:00 is never night. Phase 02's polar night needs it.)
  - With `SHADE_FEATURE_DAY_CYCLE` on: $f' = f + \text{seconds}/1200$; `clock_days += floor(f')`; $f = f' - \lfloor f' \rfloor$. Today's code wraps the fraction and loses the whole days; this keeps them.
- NEW `void game_clock_set(game_t *game, uint64_t clock_days, float64_t day_fraction)`: main's, any time. For tests now, and for phase 02's `--day-of-year`.
- `game_open`: before `game_save_parts`, sets `clock_days = 0`, `day_zero = CALENDAR_DAY_ZERO`, the fraction to `GAME_DAY_START`, `last_census_week = calendar_week_of(day_zero, clock_units)` (11 for a new world), `census_runs = 0`, `census_latch = 0`. If the save has a calendar, `save_state_apply` overwrites them (see below), and `clock_days`, `day_zero` and `last_census_week` are taken from it.

### The census in the sim's tick

In `run/src/game/tick/index.c`, NEW static `game_census_check(game)`, called first in `game_sim_tick`, after `arena_reset`:

```c
/* True if a census has just opened its screen, and this tick must not run. */
static bool game_census_check(game_t *game) {
    const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
    const uint64_t week = calendar_week_of(game->day_zero, clock);
    if (week <= game->last_census_week) {
        return false;
    }
    const calendar_census_plan_t plan = calendar_census_plan(game->last_census_week, week);
    for (uint64_t w = plan.first_week; w <= plan.last_week; w++) {
        if (game->census_week != nullptr) {
            game->census_week(game->borrowed_census_context, w, w < plan.first_week + plan.fast_weeks);
        }
        game->census_runs++;
    }
    game->last_census_week = week;
    LOG_INFO("calendar: census for week %" PRIu64 " (%" PRIu64 " weeks)", week, plan.count);
    if (game->census_screens == 0u) {
        return false; /* nobody to show it to: the world goes on */
    }
    atomic_store_explicit(&game->paused, 1u, memory_order_relaxed);
    atomic_store_explicit(&game->census_latch, week + 1u, memory_order_release);
    return true;
}
```

When it returns true, `game_sim_tick` ends its profiler span and returns **without** incrementing `tick_count`: that tick never happened. The sim thread's loop (`run/src/game/index.c:1185`) sees `paused` on its next pass and runs no more ticks, while streaming and autosaves go on as they do in a pause today. With no threads (`--headless`), the session stops calling the scene's tick (below). For a headless run with no session (tests that drive `game_headless_scene` directly), `game_scene_tick_timed` (`run/src/game/index.c:1047`) gets one new first line: while `paused` is set it returns true at once, before `game_day_advance`, so neither the clock nor the world moves, as in the threaded loop.

`last_census_week` moves on **before** the screen is answered. So an autosave while the screen is up already holds the new week, and the program being killed then cannot make that week run twice. Phase 07 must make its weekly state reach the same save (Risks).

NEW public functions in `run/include/game/index.h`, all `main`'s:

```c
/* From here on a census stops the world and waits: game_census_take says when. Without it a census runs and the world goes on.
 * Before game_open. */
void game_census_screens_set(game_t *game, bool screens);

/* What a census runs for each week: phase 07's. nullptr for nothing. Before game_open. */
void game_census_hook_set(game_t *game, game_census_week_fn *week, void *context);

/* True once for each census that has stopped the world, with the week it was for. Main, any time: an exchange, so never twice. */
bool game_census_take(game_t *game, uint64_t *out_week);

/* Main's clock as a date, and the date line for the HUD at the eye's place. */
calendar_date_t game_calendar_date(const game_t *game);
void game_calendar_line(const game_t *game, char *out, uint64_t bytes);
```

`game_calendar_date` and `game_calendar_line` go in `run/src/game/report/` beside `game_hud_block_read`, as what the HUD shows. The line's hemisphere is the sign of `hud_block[1]` when walking, or of the camera's y when flying.

### The save: a calendar section, and state version 2

In `run/include/save/state/index.h`:

```c
enum : uint32_t {
    SAVE_STATE_VERSION = 2u,          /* 2: a calendar section */
    SAVE_STATE_VERSION_READ_MIN = 1u, /* version 1 has no calendar, and opens with a new one */
    ...
};

typedef enum save_state_section_id_t : uint32_t {
    ...
    SAVE_STATE_SECTION_MINDS = 10,
    SAVE_STATE_SECTION_CALENDAR = 11, /* one save_state_calendar_t; in every version 2 file */
    SAVE_STATE_SECTION_COUNT = 11,
} save_state_section_id_t;

/* The world clock's whole days and the census, beside save_state_time_t's time of day. */
typedef struct save_state_calendar_t {
    uint64_t clock_days;       /* 0: game_t.clock_days */
    uint64_t last_census_week; /* 8 */
    uint32_t day_zero;         /* 16: D0 */
    uint8_t _pad0[4];          /* 20 */
} save_state_calendar_t;
static_assert(sizeof(save_state_calendar_t) == 24);
static_assert(alignof(save_state_calendar_t) == 8);
static_assert(offsetof(save_state_calendar_t, clock_days) == 0);
static_assert(offsetof(save_state_calendar_t, last_census_week) == 8);
static_assert(offsetof(save_state_calendar_t, day_zero) == 16);
static_assert(offsetof(save_state_calendar_t, _pad0) == 20);

typedef struct save_state_parts_t {
    ...
    save_state_time_t time;         /* 48 */
    save_state_calendar_t calendar; /* 72: always written, like time */
} save_state_parts_t;
static_assert(sizeof(save_state_parts_t) == 96);
static_assert(offsetof(save_state_parts_t, calendar) == 72);
```

`SAVE_STATE_IMAGE_BYTES` gains `+ sizeof(save_state_calendar_t)`. The section-count term grows by itself with `SAVE_STATE_SECTION_COUNT`.

In `run/src/save/state/index.c`:

- `save_state_capture` writes the calendar section right after the time section, with `_pad0` zero.
- `save_state_check` accepts a version from `SAVE_STATE_VERSION_READ_MIN` to `SAVE_STATE_VERSION`. A version 2 file must hold a calendar section, as every file must hold a time section. A version 1 file must not hold one.
- NEW `save_state_check_calendar`: exactly 24 bytes, `_pad0` all zero, `clock_days < 2^32`, `day_zero < 365`, and `last_census_week <= (day_zero + clock_days + 1) / 7`. A census can't have run for a week the saved clock hasn't reached (the $+1$ covers the fraction).
- `save_state_apply` copies the calendar into `parts->calendar` if the file has one, and leaves it as it was otherwise. `game_open` fills it with a new world's calendar first, so a **version 1 world opens on Year 1, Day 80, week 11**, with no census waiting.

`SAVE_WORLD_VERSION` does not change: no block format changes (Issues found 1).

`game_save_parts` fills `.calendar = {.clock_days = clock_units >> 32, .last_census_week = game->last_census_week, .day_zero = game->day_zero}`.

### The census screen shell

`run/include/ui/screen_census/index.h`:

```c
typedef enum ui_screen_census_action_t : uint8_t {
    UI_SCREEN_CENSUS_NONE = 0,
    UI_SCREEN_CENSUS_CONTINUE = 1, /* the button, or Esc */
} ui_screen_census_action_t;

/* The census screen: main's. Holds only what it was opened with. */
typedef struct ui_screen_census_t {
    ui_widget_t *borrowed_widget; /* 0 */
    calendar_date_t date;         /* 8: when the census ran */
    uint32_t weeks;               /* 32: how many weeks it ran, 1 unless some were missed */
    uint8_t south;                /* 36: which half of the world the season is named for */
    uint8_t _pad0[3];             /* 37 */
} ui_screen_census_t;
static_assert(sizeof(ui_screen_census_t) == 40);

void ui_screen_census_open(ui_screen_census_t *screen, ui_widget_t *widget, const calendar_date_t *date, uint32_t weeks,
                           bool south);

/* One frame between ui_frame_begin_over and ui_frame_end. escape is this frame's Esc press. */
ui_screen_census_action_t ui_screen_census_frame(ui_screen_census_t *screen, bool escape);
```

It goes into `ui_t` (`run/include/ui/index.h`) at offset 1104, out of `_pad0[48]`, leaving `_pad0[8]` at 1144, so `ui_t` stays 1152 bytes.

What it draws, in the same 240-unit-tall layout as `run/src/ui/screen_settings/` (scale from `ui_widget_scale_for`), over the shaded last picture like Settings over a world:

- Title row: `Census`
- Row 1: `Week 12` (the week from the latch)
- Row 2: the date line plus the clock: `Year 1, Day 85, Early spring, 06:00`
- Row 3, only if more than one week ran: `14 weeks were worked out`
- Row 8: a **Continue** button, the width of Settings' Done

Phase 07 replaces rows 1 to 7 with the report and the outlook.

### The session

In `run/include/game/session/index.h`, a new state: `GAME_SESSION_CENSUS = 4, /* a world open with its clock stopped, and the census screen over its last picture */`. In `run/src/game/session/index.c`:

- When a world opens, `game_census_screens_set(game, true)` is called, before `game_open`.
- **Under KMS** (`game_session_source`), in the `GAME_SESSION_PLAYING` branch, before `game_loop_input_frame`: if `game_census_take` gives a week, call `game_loop_input_clear`, `game_pause_set(game, true)` (it is already set; this also resets `last_frame_ns` so the pause's time is not owed afterwards), open the census screen with `game_calendar_date`, the plan's week count (latch week minus the week before) and the hemisphere, and set the state to `GAME_SESSION_CENSUS`.
- **Resume from the pause screen** (`UI_MENU_ACTION_RESUME`): check `game_census_take` first. If a census came in while the pause screen was up, go to the census screen instead of playing. This closes the one race: the sim firing in the same frame the player pressed Esc.
- **Under `--headless`** (`game_session_scene_tick`): the same check after `scene.tick` returns, so the next draw shows the census.
- In `GAME_SESSION_CENSUS`, the screen frame draws the census screen over `game_picture_held` the way `game_session_menu_draw` draws Settings over it. On `UI_SCREEN_CENSUS_CONTINUE`: `game_pause_set(game, false)`, state `GAME_SESSION_PLAYING`.
- Runs with no session (`--test-world`, and tests that drive `game_t` directly) never call `game_census_screens_set`, so a census runs, logs, and the world goes on.

### The HUD date line and its setting

- `settings_values_t` (`run/include/settings/persist/index.h:47`): `uint8_t show_date; /* 30: the date line in the top right corner, while playing */`, and `_pad0[1]` at 31. Default on. Kept in the file as `show_date: true`. A file without the line loads with it on and no warning, as `show_coordinates` does (`run/src/settings/persist/README.md:36`).
- `game_config_t` (`run/include/game/index.h:58`): `uint8_t show_date; /* 38 */`, `_pad1[1]` at 39. `game_session_config_from_settings` copies it. A config built by hand (tests) has it 0, so no existing picture changes.
- NEW `ui_hud_date_draw(ui_canvas_t *canvas, const ui_font_t *font, const char *text, bool below_notice)` in `run/src/ui/hud/`: the text on a dark panel at the **top right**, built like `ui_hud_coordinates_draw` (`run/src/ui/hud/index.c:214`) but measured from the right edge. Below a notice band when there is one.
- `game_hud_compose` (`run/src/game/index.c:1005`) draws it when walking and `config.show_date` is set, after the coordinates, and also while F3 is up, since the overlay is on the left.
- The Settings screen (`run/src/ui/screen_settings/index.c`) gets a **Date** checkbox at the left of **row 7**, `SETTINGS_DATE_WIDTH = 40` units wide, `SETTINGS_ID_DATE = 110u`. Row 6 is full (VSync 46, Coordinates 82, Effects... 64, of 200). Row 7 is where the "Could not keep them" message goes. While a message is up, row 7 shows the message and not the box. That message appears only after Done failed once, and a second Done leaves anyway. Phase 10's climate line box goes beside Date on row 7.
- Separator: commas, not the master plan's "·", which the font does not have (Issues found 5).

### The rename, exactly

Use `git mv` for the three directories, so history follows. Then, everywhere in the repo **except** `plans/` files other than `plans/REPO_STRUCTURE.md` (older plan files are records of what was done and stay as written):

| Old | New |
|---|---|
| `run/src/terrain/climate/`, `run/include/terrain/climate/`, `tests/run/terrain/climate/` | `.../terrain/fields/` |
| `#include "terrain/climate/index.h"` | `#include "terrain/fields/index.h"` |
| `MC_RUN_TERRAIN_CLIMATE_INDEX_H` | `MC_RUN_TERRAIN_FIELDS_INDEX_H` |
| `terrain_climate_t`, `_area_t`, `_arrays_t`, `_point_t` | `terrain_fields_t`, `_area_t`, `_arrays_t`, `_point_t` |
| `terrain_climate_init`, `_deinit`, `_area_columns`, `_sample_area`, `_sample_points`, `_at` | `terrain_fields_*` |
| `TERRAIN_CLIMATE_NOISE_FIELDS` | `TERRAIN_FIELDS_NOISE_FIELDS` |
| `borrowed_climate` in `terrain_t`, `terrain_biome_t`, `terrain_density_t`, `terrain_hydrology_t` | `borrowed_fields` |
| File-local `climate_*` and `CLIMATE_*` in the moved `index.c` and its test | `fields_*` and `FIELDS_*` |
| Parameters and locals named `climate` that hold a `terrain_climate_t *` | `fields` |
| ctest `run_terrain_climate` (named from its path by `cmake/targets.cmake:149`) | `run_terrain_fields` |
| The `climate` row of `plans/REPO_STRUCTURE.md` section 6 "World", and the `tests/run/terrain/climate/` row of section 10 | `fields` |
| Prose naming the module's path, in READMEs and `config/biomes/README.md:38` | the new path |

**Unchanged, on purpose:**

- The noise bank names `"climate/continentalness"`, `"climate/erosion"`, `"climate/temperature"`, `"climate/humidity"`, `"climate/weirdness"`, `"climate/warp_x"`, `"climate/warp_y"` (`run/src/terrain/climate/index.c:90-98`), and the test pins on them (`tests/run/terrain/climate/index.c:739-745`, `tests/run/terrain/seed/index.c:233,246`). They are hashed into salts, and changing a letter changes every world.
- The `climate:` key in `config/biomes/*/index.yaml` and its parser in `compile/src/game_tables/biomes/`. It is the biome table's own word for these ranges, and renaming it changes config files for no gain.
- The digest pin `0x1FC7F7D28F24CAA7` (`tests/run/terrain/climate/index.c:42`). Its constant may be renamed `FIELDS_DIGEST_PIN`. Its value must not change.

Files that reference the module today, from `grep -rln "terrain/climate\|terrain_climate\|TERRAIN_CLIMATE"`: `bench/terrain/index.c`, `bench/terrain/tour/index.c`, `bench/world/index.c`, `config/biomes/README.md`, `plans/REPO_STRUCTURE.md`, `run/include/terrain/{index.h,biome/index.h,climate/index.h,density/index.h,hydrology/index.h}`, `run/src/terrain/{index.c,README.md}`, `run/src/terrain/{biome,climate,density,hydrology}/{README.md,index.c}`, `run/src/terrain/{cave,feature,ore,surface}/index.c`, `tests/run/terrain/{biome,cave,climate,density,feature,hydrology,ore,seed}/index.c` and `tests/run/terrain/climate/README.md`. After the rename, the same grep, run outside `plans/`, must print nothing but `plans/REPO_STRUCTURE.md`'s history note if one is kept (none is needed).

### Edge cases

| Case | What happens |
|---|---|
| The clock set backwards (`game_day_set` to an earlier time) | The week can only fall; no census. The next one fires when the week passes `last_census_week` again |
| Day cycle effect off | The clock stands still, so no census ever fires. That is what "time stays where it was set" means |
| Paused, or on any screen | No tick, no census. The clock stands still because main doesn't run `game_loop_frame` |
| A census and Esc in the same frame | The pause screen opens; Resume goes to the census screen first |
| Killed with the census screen up | The week is already in `last_census_week`, so it doesn't run again after reload |
| A version 1 world | Opens on Year 1, Day 80, week 11. The log says `save: <world> has no calendar, and starts one` |
| `--test-world`, or a test with no session | The census runs and logs, the world goes on, nothing waits |
| More than 52 weeks missed | One pass, the older ones flagged fast, one screen |

## Owned paths

This phase may create or edit:

- `run/src/calendar/`, `run/include/calendar/` (NEW)
- `run/src/ui/screen_census/`, `run/include/ui/screen_census/` (NEW)
- `tests/run/calendar/`, `tests/run/game/census/` (NEW)
- `run/src/terrain/fields/`, `run/include/terrain/fields/`, `tests/run/terrain/fields/` (renamed)

Wiring outside them, kept to the lines this file names:

- **The rename's callers:** the files listed under "The rename, exactly".
- **Clock and census:** `run/include/game/index.h`, `run/src/game/index.c`, `run/src/game/tick/index.c`, `run/src/game/report/index.c`, `run/src/game/README.md`.
- **Session:** `run/include/game/session/index.h`, `run/src/game/session/index.c`.
- **Save:** `run/include/save/state/index.h`, `run/src/save/state/index.c`, `run/src/save/state/README.md`, `tests/run/save/state/index.c`. `tests/fuzz/save/index.c` only if it stops building.
- **UI:** `run/include/ui/index.h`, `run/src/ui/README.md`, `run/include/ui/hud/index.h`, `run/src/ui/hud/index.c`, `run/src/ui/hud/README.md`, `run/src/ui/screen_settings/index.c`, `run/src/ui/screen_settings/README.md`, `tests/run/ui/hud/` (one new golden), `tests/run/ui/screens/` (one new test, one new golden).
- **Settings:** `run/include/settings/persist/index.h`, `run/src/settings/persist/index.c`, `run/src/settings/persist/README.md`, `tests/run/settings/index.c`.
- **Lint and layout:** `compile/src/standards_lint/rules_include/index.c` (one row: `{"calendar", 11, 0}`), `plans/REPO_STRUCTURE.md` (sections 6, 10, 11 and 14).

## Steps

1. **Rename `terrain/climate` to `terrain/fields`, alone, first.** `git mv run/src/terrain/climate run/src/terrain/fields`, the same for `run/include/terrain/climate` and `tests/run/terrain/climate`. Apply the name table above. Re-run the configure step (CMake finds directories by a glob, and the old ctest name is cached): `tools/test-or-build.sh cmake --preset debug`. Build and run every terrain test and the lint. Check `run_terrain_fields` prints the digest `0x1FC7F7D28F24CAA7`, and that `grep -rn "terrain/climate\|terrain_climate\|TERRAIN_CLIMATE" --exclude-dir=plans --exclude-dir=build .` prints nothing. Commit this on its own, so a broken digest can only mean the rename. *Why first:* it touches many files and must be provably a pure rename before anything else moves.
2. **Create `run/src/calendar/` and `run/include/calendar/index.h`** with the header above, and its README. Write the functions in `index.c`. `calendar_day_of` works in `int64_t` for the subtraction so a clock before the first 06:00 gives day 0 and not a huge number. *Why:* everything else reads the date through these pure functions, and they can be tested with no card.
3. **Add `{"calendar", 11, 0}` to `include_systems`** in `compile/src/standards_lint/rules_include/index.c`, and `calendar` to the bottom row of the diagram in `plans/REPO_STRUCTURE.md` section 11 (beside `math`, `time`). *Why:* the lint's `unknown_system` rule refuses a `run/` system missing from that table.
4. **Write `tests/run/calendar/`** (ctest `run_calendar`) with the tests in the Tests section. Build and run it. *Why:* the date maths is proven before anything uses it.
5. **Add the world clock to `game_t`** at the offsets above, and change `game_day_set`, `game_day_advance`, `game_open`, and add `game_clock_set`. Run `run_game`, `run_player_survival` and `render_feature_sky`, which test the clock today: they must pass unchanged. *Why:* the census and the save both read the clock, so it must be right first.
6. **Save the calendar.** Add `save_state_calendar_t`, section 11, version 2 reading version 1, the check, capture and apply, as above. Fill `.calendar` in `game_save_parts`, and read it back in `game_open`. Update `a_file_is_these_bytes_by_hand` (`tests/run/save/state/index.c:305`) to the version 2 bytes: version 2, two sections, `sections_bytes` 80. Add the save tests. *Why:* `last_census_week` must survive a reload before the trigger exists, or the reload test can't be written.
7. **The trigger.** Add `game_census_check` at the start of `game_sim_tick`, the hook, the latch and `game_census_screens_set`, `game_census_hook_set` and `game_census_take`. *Why:* this is the heart of the phase, and with screens off it can be tested without any UI.
8. **Create `run/src/ui/screen_census/`** and add `census_screen` to `ui_t`. *Why:* the session needs something to open.
9. **The session.** Add `GAME_SESSION_CENSUS`, the latch checks under KMS, in Resume and under `--headless`, and Continue. *Why:* this is what makes the world actually stop for the player.
10. **The date line.** Add `show_date` to settings and config, the Date checkbox, `ui_hud_date_draw`, `game_calendar_line`, and the call in `game_hud_compose`. *Why:* the HUD is how Ahmed sees the calendar is right.
11. **Write `tests/run/game/census/`** (ctest `run_game_census`) and `fixtures/week.script`, and the new tests in `tests/run/settings/`, `tests/run/ui/hud/` and `tests/run/ui/screens/`. Make the two new golden pictures with `MC_GOLDEN_WRITE=1` (see those tests' READMEs), **look at each one**, then commit them. *Why:* each claim in the Goal gets a test that fails if it breaks.
12. **Documentation.** READMEs of every touched directory. `plans/REPO_STRUCTURE.md`: a `calendar` row (section 6, under a fitting heading such as "Gameplay"), `screen_census` in the `ui` row, the `tests/run/calendar/`, `tests/run/game/census/` and `tests/run/terrain/fields/` rows in section 10, and "The date and the census" in section 14's "Where is...?" table.
13. **Verification** as below, then tick this file's boxes, and write what was measured under Issues found or Commits.

## Todo

- [x] Rename `run/{src,include}/terrain/climate/` and `tests/run/terrain/climate/` to `.../terrain/fields/` with `git mv`, and every name in the table. Noise bank names unchanged
- [x] Every pinned terrain digest holds; `run_terrain_fields` prints `0x1FC7F7D28F24CAA7` (now `0xB97D2A98FDF4D13A`: Issues found 6); the grep for the old names outside `plans/` prints nothing
- [x] `plans/REPO_STRUCTURE.md` updated for the rename
- [x] `run/{src,include}/calendar/`: day, year, day of year, week, clock hour and minute, the season label's date part, the date line, the census plan, from the world clock, $D_0$ and $t_0$
- [x] `calendar` in the lint's system table and the dependency diagram
- [x] The world clock: `clock_days`, `clock_units`, `day_zero` in `game_t`; `game_day_advance` keeps whole days; waking goes to the next 06:00; `game_clock_set`
- [x] Save `last_census_week`, `clock_days` and $D_0$ in a new calendar section. Bump `SAVE_STATE_VERSION` to 2, reading version 1 with a new calendar (done as version 4, section 12, reading version 3: Issues found 10) (see Issues found 1 for why not `SAVE_WORLD_VERSION`)
- [x] The trigger in the sim's tick (master plan 4.2): one hook call per missed week, in order, older-than-52 flagged fast, the latch, the pause
- [x] `run/src/ui/screen_census/`: the shell, with the week, the date and time, and Continue
- [x] The session: `GAME_SESSION_CENSUS`, the latch under KMS, in Resume and under `--headless`, and Continue
- [x] The HUD date line in the top right, with a Date box in Settings, kept as `show_date`
- [x] Tests: the census fires exactly once a week through plain ticks, sleeping through a night, and a 100-day jump (once per missed week, in order), and never twice after a save and load mid-week
- [x] Tests: day 364 rolls to day 0 of the next year, and week numbers carry across years
- [x] Tests: the worked examples (day 1000 is year 3, day 350, week 154; a new world is day 80, week 11)
- [x] Two new golden pictures made, looked at, and committed
- [x] READMEs and `plans/REPO_STRUCTURE.md` for `calendar`, `screen_census` and the new tests

## Tests

All test names are sentences in snake case, as the repo writes them.

### `tests/run/calendar/` — ctest `run_calendar`, no card

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `a_new_world_starts_on_day_80_of_year_1_in_week_11_at_08_24` | `calendar_date_of(79, clock of 0.35)` | day 79, year 1, day_of_year 79, week 11, 08:24 | $D_0$ or $t_0$ is wrong, or the fraction is rounded the wrong way |
| `a_world_1000_days_old_is_year_3_day_350_week_154` | the master plan's worked example, at clock 1000.35 | D 1079, year 3, day_of_year 349, week 154 | any of the four formulas is wrong |
| `the_calendar_day_starts_at_06_00_and_not_at_midnight` | clock day 5 at 05:59:59.9 and at 06:00:00 exactly (units $5 \cdot 2^{32} + 2^{30}$) | D 83 then 84; the hour reads 5 then 6 | the "$- 0.25$" is missing or off by a unit |
| `day_364_rolls_to_day_0_of_the_next_year_and_weeks_carry_across` | D = 364, 365, 371, 729, 730 | years 1, 2, 2, 2, 3; days 364, 0, 6, 364, 0; weeks 52, 52, 53, 104, 104 | the week restarts with the year, or the year is off by one |
| `every_week_is_seven_days_across_ten_years` | every D from 0 to 3650: the week goes up by one exactly every 7th day | 521 changes | weeks counted within a year |
| `a_clock_before_the_first_morning_is_day_zero_and_not_a_huge_number` | day_zero 0, clock 0.1 | D 0 | an unsigned subtraction wraps |
| `seasons_start_on_days_79_170_262_and_353_with_three_parts_each` | every boundary in the season table, north and south | 79 early spring, 110 mid spring, 140 late spring, 170 early summer, 201, 232, 262, 293, 323, 353, 19, 49; south of 262 early spring, of 79 early autumn | a part boundary is off by a day |
| `day_350_is_late_autumn_in_the_north_and_late_spring_in_the_south` | worked example 6 | day_of_year 349 | the south is shifted instead of swapped |
| `the_date_line_reads_as_the_hud_shows_it` | `calendar_date_line` | `Year 3, Day 350, Late autumn`; `Year 1, Day 80, Early spring`; never past `bytes`, always ended | the line format changes, or overruns |
| `the_census_plan_runs_each_missed_week_once_in_order_and_the_oldest_past_52_fast` | `calendar_census_plan` | (11, 12): 12 to 12, count 1, fast 0. (11, 25): 12 to 25, count 14, fast 0. (11, 63): count 52, fast 0. (11, 111): count 100, fast 48, so weeks 12 to 59 fast and 60 to 111 full. (20, 15) and (20, 20): count 0 | weeks skipped, repeated, out of order, or the wrong end runs fast |
| `the_same_clock_gives_the_same_date_on_any_thread` | 16 workers each date 100 000 random clocks; compared with main's | all equal | anything here is not pure |

### `tests/run/game/census/` — ctest `run_game_census`, on the card, like `tests/run/game/`

A small world (render distance 2), opened for real. The recorder hook stores each week it is called with, and its fast flag, in the test's own array.

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `through_plain_ticks_the_census_fires_once_a_week_at_06_00` | Flying, screens off, 8 clock days of plain ticks through `game_headless_scene` from a new world (192 000 ticks; if that runs past 60 s, from an hour before each boundary instead, and say so in Issues found) | exactly one hook call, week 12, at clock 5.25 to within one tick (1/24 000 of a day); `census_runs` 1; `last_census_week` 12 | it fires on a fixed hour instead of the week going up, or twice |
| `with_screens_on_a_census_stops_the_world_and_continue_starts_it` | Screens on, clock set to 5 s before 06:00 on clock day 5, 200 ticks | the latch gives week 12 once and then nothing; `paused` set; `tick_count` grows by the ticks before 06:00 (100, within one, by where the 5 s lands on a tick) and by none after, however many more ticks the script sends; after `game_pause_set(false)`, ticks run again and no second census fires | the tick that fired still runs, or the latch fires twice |
| `sleeping_through_a_night_fires_the_census_once_on_waking` | Walking, with a bed as `tests/run/player/survival/` places one (`index.c:1371`), the clock at 23:00 on clock day 4 (`game_clock_set(game, 4, 0.9583)`), the player sleeps | on waking the clock is 5.25 exactly; one hook call, week 12; 100 ticks later still one | waking doesn't add the day, or adds two |
| `a_100_day_jump_runs_each_missed_week_once_in_order` | Real threads, `game_loop_frame` told 120 000 s have passed (100 days, as `tests/cuda/sky` tells it 120 s), screens on | hook calls for weeks 12, 13, ..., 25 in that order, 14 in all, none fast; one latch, week 25; `last_census_week` 25 | weeks skipped, repeated or out of order, or one screen per week |
| `a_save_and_load_mid_week_never_fires_the_same_week_twice` | Fire week 12; `game_save_finish`; close; reopen; tick to clock day 7 noon; then to clock day 12 06:00 | no call after reopening until week 13, then exactly one | `last_census_week` isn't saved or isn't read |
| `a_save_taken_with_the_census_screen_up_does_not_fire_it_again` | Fire with screens on, autosave while paused, reopen | no census on the first ticks after reopening | `last_census_week` moved only on Continue |
| `with_the_day_cycle_off_no_census_ever_fires` | `features_off = SHADE_FEATURE_DAY_CYCLE`, 10 000 ticks across where 06:00 would be | 0 calls | the calendar runs on ticks instead of the clock |
| `the_session_opens_the_census_screen_and_continue_resumes` | `fixtures/week.script` through `game_session_headless_scene` under `--menu --headless`: a new world, ticks across Day 85 06:00, a screenshot, Continue, more ticks | the screenshot differs from one a tick before by the screen; after Continue, ticks run and the next screenshot has no screen | the session never takes the latch, or Continue doesn't unpause |

### Elsewhere

| Where | Test | Checks |
|---|---|---|
| `tests/run/save/state/` | `a_file_is_these_bytes_by_hand` (updated) | The version 2 file byte by byte: version 2 at 48, `section_count` 2, the time section, then the calendar section's 16-byte head (24 bytes, id 11) and its 24 bytes |
| | `every_part_reloads_byte_for_byte_and_goes_on_the_same` (extended) | The calendar `{clock_days 1000, last_census_week 154, day_zero 79}` comes back exactly |
| | `a_version_1_file_still_opens_with_a_new_calendar` | A hand-made version 1 file, which is today's format, is believed, and `parts->calendar` is left as the caller filled it |
| | `a_calendar_that_cannot_be_true_is_refused` | Each refused: `clock_days` $2^{32}$, `day_zero` 365, `last_census_week` 200 with `clock_days` 1000 (most is $(79 + 1000 + 1)/7 = 154$), padding not zero, a version 2 file with no calendar section, and a version 1 file with one |
| `tests/run/settings/` | `show_date_is_kept_and_a_file_without_it_shows_the_date` | Round trip both ways; a file from before has it on. The written-file test (`index.c:146`) gains `show_date: true` |
| `tests/run/ui/screens/` | `the_date_box_changes_that_setting_and_nothing_else` | At 320 by 240, scale 1, row 7 starts 60 across and $24 + 7 \times 24 = 192$ down. A click at (64, 202) flips `show_date` only; VSync, Coordinates and Effects... (row 6) are untouched |
| | `the_census_screen_shows_the_week_and_the_date_and_continue_closes_it` | Golden `census.png` at 320 by 240; a click on Continue gives `UI_SCREEN_CENSUS_CONTINUE`, Esc too; nothing else does |
| `tests/run/ui/hud/` | `the_date_line_sits_in_the_top_right_and_moves_below_a_notice` | Golden `date_640x480.png`; no pixel left of the panel changes; with a notice the panel's top is one band lower, as for coordinates |
| | `the_date_line_follows_the_setting_in_the_real_game` | Real game, walking, `show_date` 1 then 0: the top-right corner differs, the rest of the picture is byte for byte the same |

## Verification

New directories need the configure step first:

    tools/test-or-build.sh cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_terrain_fields|run_terrain_seed|run_terrain_density|run_terrain_hydrology|run_terrain_cave|run_terrain_ore|run_terrain_biome|run_terrain_feature)$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_calendar|run_game_census|run_game|run_save_state|run_save_state_game|run_save_world|run_settings|run_settings_session|run_ui_hud|run_ui_screens|run_ui_menu|run_player_survival|run_playthrough|render_feature_sky)$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(standards_lint|standards_lint_fixtures|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

The whole suite, once, before the gate:

    tools/test-or-build.sh timeout 3400 ctest --preset debug --output-on-failure
    pgrep -a minecraft_c    # must print nothing

The rename must change no bytes. Check the terrain digests on release as Stage 10.2 did:

    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^(run_terrain_fields|run_terrain_density|run_terrain_hydrology|run_terrain_biome)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Gate

Ahmed, on a release build (`tools/test-or-build.sh cmake --preset release`, then `tools/test-or-build.sh cmake --build --preset release`):

    ./build/release/bin/minecraft_c --windowed

1. Create a new world. The top right says **`Year 1, Day 80, Early spring`**.
2. Play 8 in-game days. A day is 20 minutes, so this is about 2 hours 40 minutes; sleeping in a bed each night cuts it to about 1 hour 30 minutes.
3. You see **exactly one census**, at 06:00 on **Day 85**. The world freezes, the census screen says `Week 12` and `Year 1, Day 85, Early spring, 06:00`, and Continue resumes play. (The master plan's gate said "day 7". With the plan's week formula the first week of a new world is 5 days long: see Issues found 3.)
4. Quit to the title after Day 86 and reopen the world: no census comes until Day 92.
5. In Settings, untick **Date**: the line goes. Restart: it's still off.

A quick look without playing for hours: the headless test makes the census screen's picture in under a minute, and its PNGs can be opened:

    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_game_census$' --output-on-failure

## Risks and open questions

| Question | Default |
|---|---|
| Which weeks run fast past 52? The master plan says "past 52 skipped weeks, the rest". | The **oldest** run fast and the newest 52 in full, since the newest decide today's snow. One constant, `CALENDAR_FULL_WEEKS_MAX`, and one comparison to flip |
| One census screen per catch-up, or one per missed week? | **One**, saying how many weeks were worked out. 14 screens after a long sleep would be unbearable |
| Does Esc close the census screen? | **Yes**, like Continue, as Esc resumes from the pause screen. The world can't be left stuck |
| Phase 07's weekly state must reach the same save as `last_census_week`, or a crash between them loses a week's state | Phase 07's problem: it runs its weeks inside `game_census_check` on the sim thread, so the next autosave captures both. Noted here so phase 07 reads it |
| The Date box on row 7 hides when the save-failed message shows | Accepted: that message only appears after a failed Done, and the next Done leaves anyway |
| Old worlds restart their calendar at Year 1, Day 80 | Accepted: they are test worlds (as 003 phase 1 also says). No census storm: their `last_census_week` is the week they open on |
| The rename touches about 30 files | Done first, alone, in its own commit, proven by digests. Any digest change means a noise name was renamed by mistake |
| 003 phase 8 cites `run/src/terrain/climate/index.c:12` | 003 phase 8 runs before plan 004 (master plan "Needs first"), so it sees the old path. If it runs after, it reads `fields/` |
| The census trigger could slow every tick | One atomic load, one 64-bit divide and a compare per tick: nanoseconds |
| Ahmed's gate takes hours of play | The headless test above shows the screen in a minute. A `--day-of-year` flag comes in phase 02, for `--test-world` only |

## Commits

Appended as they land:

- `e6d90e1` Rename terrain/climate to terrain/fields.
- `8148786` REPO_STRUCTURE for the rename.
- `c16200d` Tick the rename in this file.
- `b2428d8` The calendar, `run_calendar`, the lint row.
- `47fbac9` The world clock, the census trigger, hook and latch, the save section, the census screen, the session, the HUD date line and `show_date`, and their tests.
- `8823b42` REPO_STRUCTURE for the calendar, the census screen and their tests.
- `b659ed1` The calendar save tests moved to their own ctest, `run_save_state_calendar` (Issues found 13).
- `260388c` REPO_STRUCTURE for `tests/run/save/state/calendar/`.
- `780b251` Todos ticked and issues 7 to 16 logged in this file.

Verification, 2026-11-06: debug, the eight terrain tests pass and `run_terrain_fields` prints `0xB97D2A98FDF4D13A`; the phase's list of 15 (with `run_save_state_calendar`) passes; the whole suite passes, 122 of 122 in 1784 s. The release build links with no warning, so `main()`'s frame is still under 16 KiB. On release the four terrain tests pass and `run_terrain_fields` prints `0xB97D2A98FDF4D13A` again. `pgrep -a minecraft_c` printed nothing after each run. The Gate is Ahmed's and has not been played.

## Issues found

Found while writing this file, 2026-11-04, before any code. Each is a conflict between the master plan and the code, or within the master plan, with what this file does about it.

**1. The calendar can't be saved by bumping `SAVE_WORLD_VERSION`.**

- **Expected (master plan phase 01):** "Save `last_census_week` and $D_0$. Bump `SAVE_WORLD_VERSION`."
- **Actual:** `SAVE_WORLD_VERSION` (`run/include/save/world/index.h:19`, value 2) versions the **blocks** manifest and delta files (`run/src/save/world/index.c:344,367`). Time lives in the **state** file: `save_state_time_t` (`run/include/save/state/index.h:82-88`), written by `save_state_out_time` (`run/src/save/state/index.c:180`) and versioned by `SAVE_STATE_VERSION` (`run/include/save/state/index.h:19`, value 1), checked at `run/src/save/state/index.c:779`.
- **Done in this file:** the calendar is a new state section (id 11), and `SAVE_STATE_VERSION` goes to 2. `SAVE_WORLD_VERSION` stays 2. Version 1 state files still open, with a new calendar, so no world is refused for this.

**2. `world_ticks` is not the world clock.**

- **Expected (master plan section 4):** $D = D_0 + \lfloor (\text{world\_ticks} + t_0)/24000 \rfloor$, with `world_ticks` from `save_state_time_t`.
- **Actual:** the time of day is main's, moved by each frame's real seconds (`run/src/game/index.c:342-349`), not by ticks. Sleeping sets it to 06:00 and adds no ticks (`run/src/game/index.c:343-344`, `run/src/game/tick/index.c:349-351`). With the Day cycle effect off the sun stops and ticks go on (`run/src/game/index.c:346`). `world_ticks` is `game->tick_count` (`run/src/game/index.c:118`). Using it would leave the date unmoved by a night's sleep, and put the census at a different moment from 06:00 on the sky.
- **Done in this file:** a whole-day count, `clock_days`, beside the time of day, saved in the calendar section. The formula is the master plan's with `world_ticks` replaced by the clock in ticks; every worked example gives the master plan's numbers (a world 1000 days old is still $D = 1079$, year 3, day 350, week 154).

**3. The first week of a new world is 5 days, so the gate's "day 7" can't hold with the plan's week formula.**

- **Expected (master plan):** week $= \lfloor D/7 \rfloor$, worked example week 154, and the gate "Ahmed plays 8 days and sees one pause, on day 7 at 06:00". Section 4 also says weeks are "counted from the world's first day".
- **Actual:** with $D_0 = 79$, a new world is in week 11 ($D = 77$ to $83$), and week 12 starts at $D = 84$: 06:00 on the world's sixth morning, shown Day 85. "Day 7 at 06:00" would need week $= \lfloor (D - D_0)/7 \rfloor$, which would make the worked example week 142, not 154.
- **Done in this file:** the formula and the worked example stand, since they are the plan's maths. The gate keeps "plays 8 days and sees one pause", now on Day 85. **For Ahmed:** if you'd rather the first census be exactly 7 days after a world starts, the change is one line, week $= \lfloor (D - D_0)/7 \rfloor$, and the worked example becomes 142.

**4. The master plan's HUD example names the wrong part of summer.**

- **Expected (master plan 9.1):** "Year 2 · Day 214 · Late summer".
- **Actual:** day 214 is $d = 213$, 43 days into a 92-day summer, and $\lfloor 3 \times 43/92 \rfloor = 1$: **mid** summer under 4.1's even split. Late summer starts on day 233 ($d = 232$).
- **Done in this file:** the even split of 4.1, which is the rule. The 9.1 line is read as an illustration.

**5. The font has no "·".**

- **Expected (master plan 4.1 and 9.1):** "Mid summer · Hurricane season".
- **Actual:** the font draws ASCII 32 to 126 only (`UI_FONT_FIRST = 32u`, `UI_FONT_GLYPHS = 96u`, `run/include/ui/font/index.h:17-18`), and shows anything else as `?`.
- **Done in this file:** commas: `Year 1, Day 80, Early spring`. Adding a glyph to the font texture is possible later, and is not this phase's.

Found while doing the work, 2026-11-06.

**6. The fields digest pin had already moved before the rename.**

- **Expected (this file, step 1):** `run_terrain_fields` prints `0x1FC7F7D28F24CAA7`.
- **Actual:** the pin on main before the rename was already `0xB97D2A98FDF4D13A` (`tests/run/terrain/climate/index.c:43` at `f663337`), changed by plan 003's world generation work after this file was written. After the rename, `run_terrain_fields` prints `digest 0xB97D2A98FDF4D13A` and passes, and the eight terrain tests pass on debug.
- **Done:** the pin stays `0xB97D2A98FDF4D13A`, the value from before the rename, which is what "unchanged" means. The constant is now `FIELDS_DIGEST_PIN`. The test's file-local `CLIMATE_FIELDS` became `FIELDS_COUNT`, not `FIELDS_FIELDS`, and a local array in `init_and_deinit_are_a_pair` became `noises`, since `fields` is now the name of the struct pointer beside it.


**7. 0.35 of a day is not exact in binary, so a new world's clock reads 08:23 rounded down.**

- **Expected (Tests, `a_new_world_starts_on_day_80_of_year_1_in_week_11_at_08_24`):** 08:24 at a clock of 0.35.
- **Actual:** `game_day_units_of` truncates: 0.35 as a `float64_t` is 0.34999999999999997..., times 2^32 is 1503238553.6, kept as ...553, and that is 503.9999 minutes, which rounds down to 08:23.
- **Done:** `calendar_date_of` rounds the hour and minute down, which keeps 05:59 the day before at any clock. `run_calendar` writes its clocks to the nearest unit, ...554, which is 08:24. The game's truncation is left as it was, so no existing picture, save or sky test moves. Nothing the player sees shows the time of a new world: the date line has no time, and the census screen shows 06:00, reached by a tick past 06:00 or by waking at exactly 2^30.

**8. `game_t` had changed since this file was written: other offsets, and `census_runs` is 32 bits.**

- **Expected (Design, "The world clock in `game_t`"):** `paused` then `day_zero` at 916, `clock_units` at 920, `census_week` at 1264, `last_census_week` at 1456, `census_runs` at 1464.
- **Actual:** `borrowed_felling` is at 920, `borrowed_sounds` at 928 and `borrowed_audio` at 1456 (plan 003). The padding left was `_pad0[2]` at 878, `_pad1[4]` at 916, `_pad8[24]` at 936, `_pad4[20]` at 1260, `_pad6[6]` at 1378 and `_pad7[8]` at 1464. `game_t` must stay 1600 bytes.
- **Done:** `census_screens` 878, `day_zero` 916, `clock_units` 936, `clock_days` 944, `census_latch` 952, `census_week` 1264, `borrowed_census_context` 1272, `census_runs` 1380 as a `uint32_t` (in main's line, written by the sim only on a census), `last_census_week` 1464. Each has a `static_assert`. `game_t` is still 1600 bytes and `app_t` 16064.

**9. `game_config_t` had no room for `show_date` at 38.**

- **Expected:** `show_date` at 38, `_pad1[1]` at 39.
- **Actual:** 38 is `god_rays`, and `_pad0[1]` at 39 was the only free byte. `game_config_t` is inside `game_t` and the session, so it cannot grow.
- **Done:** `show_date` at 39, and the struct has no padding left. Every `game_config_t` initialiser that named `._pad0 = {0}`, 52 of them in 32 files (`run/src/main/index.c`, and tests under `tests/run/`, `tests/cuda/` and `tests/soak/`), now names `.show_date = 0u`, so no existing picture changes. That is wiring outside the owned paths, one token per initialiser, found by the compiler.

**10. The state file was already at version 3, with section 11 taken.**

- **Expected:** `SAVE_STATE_VERSION` 1 to 2, `SAVE_STATE_SECTION_CALENDAR = 11`, read version 1.
- **Actual:** version 3 (2: names, 3: the woodworking bench's grid), section 11 is `SAVE_STATE_SECTION_NAMES`, and only exactly version 3 was read.
- **Done:** the calendar is section 12, `SAVE_STATE_VERSION` is 4, and `SAVE_STATE_VERSION_READ_MIN` is 3, the newest version without a calendar. A version 4 file must hold the calendar and a version 3 file must not. The translate step (`run/src/save/state/translate/`) passes section 12 through, as no number in it is a place in a table. The tests are named for version 3: `a_version_3_file_still_opens_with_a_new_calendar`. `tests/run/save/state/` and `tests/run/save/state/names/` each assert their rig's size, which grew by 24 with `save_state_parts_t` (wiring). `tests/fuzz/save/` still builds.

**11. `ui_t` had 16 bytes left, not 48, so the census screen holds the clock and not the date.**

- **Expected:** `ui_screen_census_t` of 40 bytes at 1104, out of `_pad0[48]`.
- **Actual:** `ui_t` is `settings_screen` at 1040 (96 bytes) and `_pad0[16]` at 1136. Growing `ui_t` grows `app_t`, which is on main's stack at the 16 KiB frame limit.
- **Done:** a 16-byte `ui_screen_census_t` at 1136: the clock, the week count, `D0` and the hemisphere. The date is worked out from the clock as it is drawn, and the widget is passed to `ui_screen_census_frame` instead of being borrowed. `ui_t` is still 1152 bytes.

**12. The Settings screen's row 7 is the sound volume's slider now.**

- **Expected:** a Date box at the left of row 7, where the "Could not keep them" message goes, hidden while the message is up; `SETTINGS_ID_DATE = 110u`.
- **Actual:** plan 003 phase 5 put the sound volume slider on row 7 with id 110, and the message now takes the title's place.
- **Done:** the Date box is the first 40 units of row 7 (`SETTINGS_DATE_WIDTH`), id 111, and the volume slider takes the 156 after a gap of 4. The box never hides. The test `the_volume_slider_changes_that_setting_and_nothing_else` now clicks the slider at 104 and 259 instead of 60 and 259 (`tests/run/ui/screens/`). The click at (64, 202) in `the_date_box_changes_that_setting_and_nothing_else` is the box, as this file said.

**13. The real-game date-line test is in `run_game_census`, and some test files are past 1500 lines.**

- **Expected:** `the_date_line_follows_the_setting_in_the_real_game` in `tests/run/ui/hud/`.
- **Actual:** `tests/run/ui/hud/index.c` was already 1603 lines on main, past the standard's 1500. The golden test added here takes it to 1673. `tests/run/settings/index.c` was 1495 and is 1530 with `show_date_is_kept_and_a_file_without_it_shows_the_date`. `tests/run/save/state/index.c` was 1481 and would have been 1645 with the calendar tests.
- **Done:** the real-game test is in `tests/run/game/census/`, which opens real worlds anyway, and compares each frame against the card's own picture under it, as two frames are not the same picture to the byte even with TAA and the day cycle off. The calendar save tests are their own ctest, `run_save_state_calendar` in `tests/run/save/state/calendar/`, so `tests/run/save/state/index.c` is 1497 lines. `tests/run/ui/hud/index.c` (1673) and `tests/run/settings/index.c` (1530) are left past 1500 and under the 2000 the lint enforces; splitting them is for whoever next works there.

**14. Main's clock stands still while paused.**

- **Expected (Design):** the clock stands still while paused "because main doesn't run `game_loop_frame`".
- **Actual:** a census pauses the world from the sim thread, and main may run a frame or two of `game_loop_frame` before it takes the latch.
- **Done:** `game_day_advance` applies a waking morning and then does nothing while `paused` is set. `game_pause_set`'s promise that a caller does not run frames while paused makes this change nothing for anything that was already there; `render_feature_sky`, `run_game` and `run_player_survival` pass unchanged.

**15. The plain-ticks test needs no shortcut.**

- **Expected:** if 192 000 ticks take more than 60 s, tick from an hour before each boundary instead.
- **Actual:** flying, the headless scene's 192 000 ticks take 0.2 s on debug.
- **Done:** all 192 000 ticks run.

**16. The session test sets the clock once the world is open.**

- **Expected:** `fixtures/week.script` with "ticks across Day 85 06:00".
- **Actual:** from a new world that is 117 600 ticks of a walking world.
- **Done:** the script opens a new world through the menus, and the test then puts the clock 40 ticks before 06:00 on clock day 5 with `game_clock_set`, as a console could. The census came on script tick 60, the screen shaded 921 273 of 1 228 800 bytes, and after Continue the world ticked on and the screen was gone. The pictures were looked at (`MC_CENSUS_PNG_DIR`).
