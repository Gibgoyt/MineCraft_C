# run/src/calendar

**Purpose:** the date. From the world clock (`calendar_clock_t`: whole days in the top 32 bits, the time of day in 1/2^32 of a day below) it works out the calendar day `D`, the year, the day of the year, the week, the clock's hour and minute, the temperate season label, the HUD's date line, and which weeks a late census runs. Plan 004 section 4 and its phase 01 file.
**Tier:** Owner by depth. It reserves nothing.
**Owns:** nothing. The twelve season names are read-only strings in the program's data.
**Lifetime:** Process. There is no state, so it has no `_init` and no `_deinit`, like `run/src/time/`.
**Threads:** any. Every function is pure: the answer depends only on the arguments.
**Public functions:**

- `calendar_day_of`, `calendar_week_of`: `D = D0 + floor((clock - 06:00) / day)`, and `floor(D / 7)`. A clock before the first 06:00 of a world with `D0 = 0` is day 0, not a wrapped number.
- `calendar_date_of`: all of it at once, with the clock's hour and minute, rounded down.
- `calendar_season_temperate`, `calendar_season_name`: the season and its early, mid or late part by plan 004's table 4.1, north or south of the equator.
- `calendar_date_line`: `Year 3, Day 350, Late autumn`, never past the bytes it is given.
- `calendar_census_plan`: the weeks from the last census + 1 to now, and how many of the oldest run fast (all but the newest `CALENDAR_FULL_WEEKS_MAX`).

**May be called by:** everything in `run/` from this layer up. It sits on the bottom row of `plans/REPO_STRUCTURE.md` section 11, beside `math` and `time`.
**Depends on:** `assert`, `types`, glibc (`snprintf`).

## The formulas

    D           = D0 + floor((clock - 2^30) / 2^32)      the calendar day starts at 06:00 on the clock
    year        = floor(D / 365) + 1
    day of year = D mod 365                               shown as + 1, so 1 to 365
    week        = floor(D / 7)                            counted from D = 0, never restarted by a new year

`D0 = 79` (`CALENDAR_DAY_ZERO`) puts a new world on the spring equinox, day 80. A new world's clock is 0.35 of a day, so it is week 11, and the first census is at the start of week 12, `D = 84`: 06:00 on the world's sixth morning, shown Day 85.

Weeks run straight from `D = 0` because `365 = 52 * 7 + 1`: counted within each year, the last week would be one day long.

The south's season is the north's opposite on the same day (spring and autumn swap, summer and winter swap), so there is no rounding of half a year. Until plan 004 phase 02 brings latitude, the caller chooses the hemisphere.
