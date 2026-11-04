# 003-ahmed-bohemoth-initial-game-fixes — phase 10: Coordinates on screen

**Status:** in progress: the code and its checks pass, but the gate's whole-suite run is unfinished (21 of 100 entries) and Ahmed's own look is still to come   (updated 2026-11-04)

## Goal

A setting shows your x, y and z on screen while you play, and is remembered.

**Ahmed's words:** "co-ordinates option also available in settings somewhere please"

## How it is today

Nothing shows your position. The F3 overlay (`run/src/ui/hud/index.c:222`) shows only FPS and timings. Your position appears only in the terminal, when you leave a world.

## Todo

- [x] Add `show_coordinates` to `settings_values_t` in `run/include/settings/persist/index.h`, using a spare byte in `_pad0`, and read and write it in `run/src/settings/persist/`
- [x] Add a checkbox in `run/src/ui/screen_settings/index.c`, beside VSync
- [x] Send the player's position from the sim to the HUD in `run/src/game/`, as the other HUD values are sent
- [x] Draw `x y z` in the top-left corner in `run/src/ui/hud/`, as whole blocks. z is up, with 0 at sea level, so the numbers match the world limits
- [x] Also add them to the F3 overlay, so they show whatever the setting says
- [x] Add tests in `tests/run/settings/` that the setting survives a save and load, and that a settings file without it still loads
- [x] Add a golden picture in `tests/run/ui/hud/`, on and off, and a test in `tests/run/ui/screens/` that the checkbox changes it

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_settings|run_ui_hud|run_ui_screens|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: turn it on in Settings, walk and dig down to see the numbers change, then restart and check it's still on.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

- `e6afbcd` Show the player's x, y and z in the top left corner and on the F3 overlay, with a Coordinates box in Settings that is kept and on by default.
- `dd34279` Say in the repo structure that the HUD and its overlay show coordinates.
- `7972ff4` Record the coordinates phase as built, with what was added, what was tested and the one rule broken on the way.
- `308c5b1` Test that a tick alone moves the coordinates and that a reopened world shows the saved block before any tick, and put the tick comment back on its function.
- `ca7ad78` Record the coordinates gate check, with what it proved, what it corrected and that the whole suite did not finish.
## Issues found

**On by default, for now.** It's useful while you're testing the world. It's one line to change.

**Needs no new blocks or items,** so it can be built any time. It's small, and helps judge phase 8's terrain, so I'd build it early.

**Ahmed's check is not done.** Nobody has looked at this on a real screen, walked or dug. A release build of `minecraft_c` links with no warnings (it was deleted with `build/`, so build it before running the line above: `tools/test-or-build.sh cmake --preset release`, then `tools/test-or-build.sh cmake --build --preset release`).

**What was built beyond the todo list, each on purpose:**

- Expected: F3 and the corner line to live side by side. Actual: both are panels at the top left, and two dark panels overlap. Done: the corner line is not drawn while F3 is up, as the overlay has them whatever the setting says. Test: `in_the_real_game_the_screen_is_the_hud_over_the_picture_and_the_picture_is_left_alone` checks a blank pixel of the panel is one panel dark.
- Expected: the overlay's 104-wide panel to hold another line. Actual: `x -30000000 y -30000000 z -250` would be cut off. Done: `UI_HUD_DEBUG_WIDTH` is now 176, and the height one line more. `debug_640x480.png` was made again and looked at. Test: `the_overlays_numbers_change_only_their_own_line_and_it_stays_on_its_panel` measures the longest text against the panel.
- Expected: a notice to be no concern. Actual: the band for "You died" or "Not saving" crosses the corner and would hide the numbers. Done: the panel goes just under the band. `game_hud_trouble_draw` now returns whether it drew. Test: `coordinates_stay_on_their_own_small_panel_and_move_below_a_notice`.
- Expected: a picture before the first tick to have a position. Actual: the words start at 0, and the first tick is up to 50 ms later. Done: `game_hud_position_publish` also runs as a world opens. (Corrected by the gate check below: a saved position is not late by a tick.)
- A flying run has no sim to ask, so `game_hud_block_read` gives the overlay the eye's own block.

**Row 6 of Settings had room for three, just.** VSync and Effects... had half each. Now VSync is 46, Coordinates 82, and Effects... the remaining 64, of 200 with two gaps of 4, looked at in a screenshot at scale 2. The click points in `title.script` and the settings test still land on the same things. No row moved.

**z needed no conversion here, for now.** `config/machine.yaml` has `z_sea: 0`, so `MC_WORLD_Z_SEA` subtracts nothing. It is still subtracted, so the numbers stay right if that ever changes. `z_max` is still 500 until phase 11.

**The test was not put only where the phase said.** The checkbox's click is tested in `tests/run/ui/screens/` as asked. Taking it on and off in a real world, with the file and a new program, needed the built-program helpers in `tests/run/settings/` (`fixtures/coordinates.script`), so it is there. Its log line "game: now drawing" stays absent, which shows the card is not touched. Pictures: 844 pixels in the corner and 0 past it differ between on and off; 0 and 0 between off and a new program.

**A rule of ours I broke, once.** Expected: every test run through `tools/test-or-build.sh`, under `timeout`. Actual: I ran `build/debug/bin/tests/run_ui_hud --help | head -2` bare, to see its options. The harness has no `--help`, so it started the tests, on the card, outside the lock, until `head` closed the pipe. Evidence: its first two `ok` lines came back, and `pgrep -af "$PWD/build"` printed nothing straight after. No window, sound or device was touched. Not repeated.

**Tests beyond the phase's list, all passed, on the debug build:** `run_settings_session`, `run_game`, `run_playthrough`, `run_profiler`, `run_render_frame`, `run_save_state_game`, `run_ui_font`, `run_ui_menu`, as they run the same HUD or settings. The other 87 ctest entries were not run here.

**Gate check, 2026-11-04.**

- Verification: a clean debug build of this worktree passed `run_settings`, `run_ui_hud`, `run_ui_screens`, `standards_lint` and `clang_format`, and `run_save_state_game` too, after the changes below.
- Played headless at 640x480, PNGs read: `x 24 y 24 z 72` on opening, `x 34 y 30 z 54` after walking, `x 31 y 37 z 52` after strafing, then z 46, 45, 43 while digging down. F3 showed the same `x 31 y 37 z 43`. Reopened in a new program, it was on and still `x 31 y 37 z 43`. Turned off at the title screen, the file said `show_coordinates: false`, a new world had no corner panel, and F3 still had them. Also read at 1280x720, on and with F3.
- Expected: a test that the numbers follow the body. Actual: none; every test's body stood still or called the publish by hand. Done: `run_ui_hud` now moves the body and runs a tick. `run_save_state_game` now saves three blocks away and reads the block before any tick. Evidence: with both publish calls removed, both tests failed (`shown[0]`: 0 != -5; `block[0] != before_tick`). Calls restored.
- Expected: the note above, that a saved position shows late by a tick, to be true. Actual: false. `game_open_parts` spawns at the saved feet before `game_hud_start` publishes (`run/src/game/index.c`). Done: note corrected, no code change, and the new test holds it.
- Expected: the comment on `game_sim_tick` to sit on it. Actual: adding `game_hud_position_publish` left it on that function (`run/src/game/tick/index.c`). Done: moved.
- Not done: the whole suite. It was stopped at entry 22 of 100, with 21 passed and none failed, because this agent was told to return. The other entries are unverified on this branch. Run `tools/test-or-build.sh timeout 3400 ctest --preset debug --output-on-failure`.
- Not seen live: the notice band with the panel beneath it. Only `coordinates_stay_on_their_own_small_panel_and_move_below_a_notice` covers it.
