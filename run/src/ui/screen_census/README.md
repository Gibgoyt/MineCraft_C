# run/src/ui/screen_census

**Purpose:** the census screen. Plan 004 phase 01 built it: when the week number goes up at 06:00 on the world clock, the world stops and this comes up over its last picture. Plan 004 phase 07 fills it with the census's report: where you are last week, events within render distance, the outlook, a progress bar while the census works, and **Continue**. It also defines that report, `ui_census_report_t`, because `ui` may not include `game`, which fills it.
**Tier:** Arena by depth, and it allocates nothing: labels are stack locals, and the widgets draw on the frame they are given.
**Owns:** nothing. Its `ui_screen_census_t` is a field of `ui_t`, 16 bytes: the clock, `D0`, the week count and the hemisphere. The date is worked out from the clock as it is drawn; the report is passed to each frame.
**Lifetime:** From `ui_screen_census_open` until Continue.
**Threads:** `main`.
**Public functions** (`run/include/ui/screen_census/index.h`):

- `ui_screen_census_open`: for a census that stopped the world at this clock, after this many weeks.
- `ui_screen_census_frame`: one frame, given the report (or nullptr), which says whether the person has pressed Continue or Esc.
- `ui_screen_census_wind_from`: the compass point a wind blows from, for the report's wind.

**May be called by:** `run/src/game/session/`, and `run/src/ui/`, whose header holds its state.
**Depends on:** `calendar` for the date and the season, `climate/outlook` and `climate/weather/week` (the report's parts), `climate/systems` (the kinds' names), `file` (a write's failure, said in words), `ui/widget`, `ui/compose`, `assert`, libm.

## How it works

The same 320 by 240 layout as `run/src/ui/screen_settings/`, at any scale, over the world's last picture shaded as Settings shades it. Text is the 5 by 7 font at 6 units a letter and 9 a line, so a line holds 52 letters. With a report:

| Units from the top | What |
|---|---|
| 3 | `Census - Week 154 - Year 3 - Day 350` |
| 14 | **Where you are, last week:** `High 21.3 C  Low 8.1 C  Rain 12 mm` (`Rain or snow` in a week with snow days), then `Snow on the ground 0 mm  Wind SW 4.2 blocks/s` |
| 43 | **Events within render distance:** two columns of `Floods in 12 places` and the like, or `A quiet week`; then the hurricanes' names |
| 99 | **Outlook, chances in % (highs and lows +-4.5 C):** a table, days 1 to 7: rain, thunder and storm chances, and `high/low` in °C |
| 155 | The named systems with at least half a percent on some day, two columns of four: `Hurricane Tavela 93% d2` (the day of its highest chance; "Tropical storm" shortened to `Trop. storm`) |
| 185 to 210 | While the census works: `Working out the weather...`, `Week 3 of 14 - 41 of 100 tiles` and a bar of its own across the screen. After: why the file could not be saved, or that the world is too big for more tiles, or that the saved weather was refused, or how many weeks were worked out |
| 212 | Continue, greyed until the file is written or has failed |

The report's sections are drawn only once the census is done, so nothing half worked out is shown. Esc continues as the button does once it is enabled, so the world can never be left stopped: the census always ends written or failed. Without a report (a world with no climate census), phase 01's rows: `Census`, `Week 12`, the date line and clock, how many weeks, and Continue at once. It changes nothing in the game: `run/src/game/session/` unpauses the world on `UI_SCREEN_CENSUS_CONTINUE`.
