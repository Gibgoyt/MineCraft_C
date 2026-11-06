# tests/run/game/sun

**Purpose:** ctest `run_game_sun`. Tests plan 004 phase 02 through the whole chain on the real game: clock to latitude to picture, beds and mobs. The picture's sun (`render_frame_t.sun_given`) at the drawing eye's latitude on a midsummer midnight, at y = 8 and at y = 30,000 (70.5 degrees north, the sun up in the north); the midnight sky clearly brighter there than at the equator; a bed that will not work at 60 degrees north on a midsummer midnight and sleeps at 18:00 in midwinter; a zombie's try on an open stone floor too bright at noon on the equator and dark enough at noon at 80 degrees north in midwinter, with the sun height the tick gave the body; the HUD's season label saying `Midnight sun` and `Polar night`, and taking its hemisphere from the latitude's sign; and `--day-of-year` on the built program, taken with `--test-world` and refused without it or past 365.
**Tier:** test.
**Owns:** nothing outside the test arena. Blocks on the card for the length of a test, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main and the real workers, through `game_settle` and the headless scene's ticks; no sim or gpu thread. The program it runs is killed at its deadline with its whole process group.
**Public functions:** none; a test program.
**May be called by:** ctest, with the built `minecraft_c` as its argument (`test.cmake`).
**Depends on:** `tests/harness/`, `run/src/game/` and everything under it, `run/src/climate/`, `run/src/climate/sun/`, and a card.

Bodies are moved by setting their feet and waiting a few ticks: a body whose column is not loaded waits where it is, the eye follows it, and the map loads around it. Floors are put at z = 1000, over any land, so nothing stands between them and the sky. Compute only: no display, console, input device or sound server is opened.
