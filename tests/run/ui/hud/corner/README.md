# tests/run/ui/hud/corner

**Purpose:** ctest `run_ui_hud_corner`. Tests plan 004 phase 10's top right corner: `ui_hud_corner_draw` and its neighbours in `run/src/ui/hud/`, and `run/src/game/corner/` (the sim's words, main's text) in a real game. It is its own program because `tests/run/ui/hud/index.c` is past the coding standard's 1500 lines; the phase 10 file calls these tests additions to `run_ui_hud`.
**Tier:** test.
**Owns:** the test arena, a `ui_t`'s and a `texture_t`'s regions, and in the real games everything a run has, a climate over seed 2198175988018515243 included, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the real game's workers build its climate tiles, the columns and the storm warning's job.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/ui/`, `run/src/game/` and its part `game/corner`, `run/src/climate/` and its parts, `run/src/display/headless/png/`, `compile/src/png/` to read the golden pictures, and a card for the real games.

What it holds to:

- **The words by hand:** the trade wind of master plan 6.5 is "6.0 m/s from E"; each of the 8 points from the middle of its 45 degrees, and 21.8 degrees on is the next; calm under 0.5; -0.04 is "0.0" and never "-0.0"; a named warning is "Storm warning: Hurricane <name>" or "Tropical storm <name>" from the name seed alone, and the other two kinds their own words.
- **Where it sits,** at 320 by 240, 640 by 480 and 1920 by 1080: each panel's right edge a margin from the canvas's, as wide as its text and two margins (and the arrow's space and square on the climate line), a line and two margins tall, the panels touching with no gap, the warning on red, nothing past the canvas.
- **An empty line takes no room,** so a lone climate line is at the top, and slot 0, set to "Testville", changes not one pixel in plan 004.
- **Below a notice** the first panel starts where the band ends. At 320 by 240 the date line reaches the coordinates' panel and F3's, so the corner goes a margin below whichever it is; at 640 by 480 it is clear of both and stays at the top. The two never share a pixel.
- **The arrow as worked by hand:** facing north with the trade wind, (-0.928, 0.371); facing east with a wind blowing east, straight up; its tip left of and below its tail within a pixel of that times its length at both corner scales, tip and tail drawn white.
- **Golden pictures,** at 640 by 480 over tests/run/ui/hud's background and HUD: `corner_on_640x480.png` (the phase 10 file's date line, "31.2 °C  wind 6.1 m/s from SW" and its arrow, the coordinates), `corner_warning_640x480.png` (with "Storm warning: Hurricane Tavela"), `corner_320x240.png` (all three, below the coordinates), `corner_debug_640x480.png` (F3 up). Off is no new file: the corner with only phase 01's line is `date_640x480.png` byte for byte, and with nothing, under the coordinates, `coordinates_640x480.png`.
- **The real game, one thread:** the first picture has the words before any tick (the opening publishes them); at day 250, at a cell 15 degrees north whose phase 05 season holds, after 20 ticks the date line says "Day 250" and "Hurricane season", the temperature is the test's own `T_now(ground) - 0.012 (z - z_ground)` from phase 07's and phase 03's functions to 0.05, 500 blocks up it is 6.0 colder to a tenth, and turning round turns the arrow over.
- **The settings:** with the warning off, the climate line off, or both, only the corner changes; back on, the same pixels; the date off too, nothing in the corner.
- **No heap** in 200 ticks and frames of the real game with all three lines up.

**Golden pictures:** made again, after looking at them, with

    MC_GOLDEN_WRITE=1 tools/test-or-build.sh ctest --preset debug -R '^run_ui_hud_corner$'

which also rewrites `date_640x480.png` and `coordinates_640x480.png` in `tests/run/ui/hud/fixtures/`: those must come out unchanged (`git diff` says so), as they are tests/run/ui/hud's too.
