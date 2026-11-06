# tests/run/ui/census

**Purpose:** ctest `run_ui_census`. Tests plan 004 phase 07's census screen with a report (`run/src/ui/screen_census/`), with no card and no world: golden pictures at 640 by 480 and 1280 by 720 of a report with every event, four hurricanes named, 8 named systems ("Tropical storm" shortened) and the file's failure, and of the working screen (progress bar, Continue greyed), held byte for byte to `fixtures/`; the layout by hand at 320 by 240, 640 by 480 and 960 by 720 (scales 1, 2 and 3): the screen centred, Continue pressed at its middle and not a unit above it; Continue and Esc doing nothing until the file is written or has failed; the wind's compass point; no heap reserved in 200 frames.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/ui/` and its parts, `texture`, `heap` (the counters), `display/headless/png` (the golden pictures). No graphics card.

Golden pictures are written with `MC_GOLDEN_WRITE=1` and looked at before being kept.
