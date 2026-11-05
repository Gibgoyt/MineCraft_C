# run/src/ui/screen_census

**Purpose:** the census screen, plan 004 phase 01: when the week number goes up at 06:00 on the world clock, the world stops and this comes up over its last picture, with the week, the date and time, how many weeks were worked out if more than one, and a **Continue** button. Plan 004 phase 07 puts the climate report and the outlook in rows 1 to 7.
**Tier:** Arena by depth, and it allocates nothing: labels are stack locals, and the widgets draw on the frame they are given.
**Owns:** nothing. Its `ui_screen_census_t` is a field of `ui_t`, 16 bytes: the clock, `D0`, the week count and the hemisphere. The date is worked out from the clock as it is drawn.
**Lifetime:** From `ui_screen_census_open` until Continue.
**Threads:** `main`.
**Public functions** (`run/include/ui/screen_census/index.h`):

- `ui_screen_census_open`: for a census that stopped the world at this clock, after this many weeks.
- `ui_screen_census_frame`: one frame, which says whether the person has pressed Continue or Esc.

**May be called by:** `run/src/game/session/`, and `run/src/ui/`, whose header holds its state.
**Depends on:** `calendar` for the date and the season, `ui/widget`, `ui/compose`, `assert`.

## How it works

The same 320 by 240 layout as `run/src/ui/screen_settings/`, at any scale, over the world's last picture shaded as Settings shades it:

| Row | What |
|---|---|
| Title | `Census` |
| 1 | `Week 12` |
| 2 | `Year 1, Day 85, Early spring, 06:00`: the date line and the clock |
| 3 | `14 weeks were worked out`, only if more than one week ran |
| 8 | Continue, as wide as Settings' Done |

Esc continues as the button does, so the world can never be left stopped. It changes nothing in the game: `run/src/game/session/` unpauses the world on `UI_SCREEN_CENSUS_CONTINUE`.
