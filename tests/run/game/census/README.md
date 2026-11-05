# tests/run/game/census

**Purpose:** ctest `run_game_census`. Tests plan 004 phase 01's weekly census in a real game on the card: the trigger in `game_sim_tick`, the world clock in `game_t`, the census hook, the latch and the pause, the calendar in the save, the session's census screen, and the HUD's date line.
**Tier:** test.
**Owns:** nothing outside the test arena, and a directory of its own under `/tmp` for each test, removed at its end. Blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main, the real workers, and for the 100-day jump the real `sim` and `gpu` threads. The census hook writes the test's own record from whichever thread runs the sim; main reads it once the threads have stopped or the latch has said so.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/game/` and everything under it, `run/src/calendar/`, and a card.

Compute only: no display, console or input device is opened.

What it checks, each against the numbers in `plans/004-ahmed-bohemoth-climate-system-phase-01-calendar-and-weekly-pause.md`:

- **Plain ticks:** 8 clock days of ticks from a new world, all 192 000 of them, give exactly one census, week 12, at 06:00 on clock day 5 to within one tick.
- **Screens on:** the census stops the world in the tick it fires: that tick does not run, the latch gives week 12 once, no tick and no clock moves while paused, and after `game_pause_set(false)` ticks run and no second census comes.
- **The day cycle off:** the clock stands, so no census ever fires.
- **Sleeping:** from 23:00 on clock day 4, waking is 06:00 on clock day 5 exactly, and week 12 runs once.
- **A 100-day jump** through `game_loop_frame` on the real threads: weeks 12 to 25 in order, 14, none fast, one latch for week 25.
- **Saves:** a save and load mid-week never runs a week twice, and a save taken with the census screen up, before Continue, does not run it again.
- **The date line** in the real game: on, only the top right corner changes.
- **The session:** `fixtures/week.script` through `game_session_headless_scene`, as `--menu --headless` runs it: a new world through the menus, the census screen over the stopped world, and Continue back to play. Once the world is open the test puts the clock 40 ticks before 06:00 on clock day 5, as a console could, since five days of ticks would take minutes. `MC_CENSUS_PNG_DIR=<dir>` keeps its three pictures there to be looked at.
