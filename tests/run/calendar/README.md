# tests/run/calendar

**Purpose:** ctest `run_calendar`. Tests `run/src/calendar/`: a new world's date, the master plan's world 1000 days old, the calendar day starting at 06:00, years rolling over while weeks carry on, every week 7 days across ten years, a clock before the first morning, every season and part boundary north and south, the date line and its bounds, the census plan's weeks and fast flags, the continuous year position the sun reads (plan 004 phase 02), and the same dates from 16 threads as from one.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and 16 short-lived threads it joins for the purity test. They only write their own slice of an array, and main checks.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/calendar/`. No graphics card.

The clocks are written to the nearest 1/2^32 of a day, so 0.35 of a day reads 08:24 and not 08:23. See Issues found 7 in `plans/004-ahmed-bohemoth-climate-system-phase-01-calendar-and-weekly-pause.md`.
