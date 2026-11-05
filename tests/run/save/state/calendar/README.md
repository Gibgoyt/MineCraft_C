# tests/run/save/state/calendar

**Purpose:** ctest `run_save_state_calendar`. Tests the calendar section of `run/src/save/state/` (plan 004 phase 01, state version 4): the calendar `{clock_days 1000, last_census_week 154, day_zero 79}` reloads exactly; a version 3 file, made by taking the section out of a real file, is believed, leaves the caller's new calendar as it was, and saves again as version 4 with that calendar; and calendars that cannot be true are refused: `clock_days` of 2^32, `day_zero` 365, a `last_census_week` past what the clock has reached, padding not zero, a section too short, a version 4 file with no calendar, a version 3 file with one, and a version older than any read.
**Tier:** test.
**Owns:** nothing outside what each test starts and stops, and a directory under `/tmp` for each, removed at its end. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/save/`, `run/src/calendar/`, and the stores in `run/src/player/`, `run/src/item/`, `run/src/entity/` and `run/src/mob/`.

## Why it is its own ctest

These tests were in `tests/run/save/state/`, whose `index.c` they would have taken past the coding standard's 1500 lines. This file has its own small rig, as `tests/run/save/state/names/` has, with only what the calendar tests read: the time, the calendar and a stack in the bag.
