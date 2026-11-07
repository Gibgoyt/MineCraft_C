# tests/run/ui/atlas/game

**Purpose:** ctest `run_ui_atlas_game`. Tests the Atlas (plan 004 phase 09) in real games: `run/src/game/atlas/` and the
wiring in `run/src/game/` and `run/src/game/input/`, through the session's headless scene (a world made through the
menus, as `--menu --headless` runs it), through `game_loop_source` under fake input, and in the built program. Split from
`tests/run/ui/atlas/` because these need the card and that file was at the standard's line for splitting.
**Tier:** test.
**Owns:** the test arena; a game's, ui's, texture's and job system's regions; blocks on the card well under 1 GiB;
temporary directories under `/tmp/mc_ui_atlas_game_*`, all given back by the end of each test.
**Lifetime:** One run.
**Threads:** main and the real workers; the loop test starts the real sim and gpu threads; the built program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/input/fixtures/` (the fake evdev), `run/src/game/` and everything under it,
`compile/src/png/` to read the program's screenshots, the built program, and a card.

Compute only. No real display, console or input device is opened, and the program runs `--headless --no-sound`.

What it holds to:

- **The game keeps running:** in a played world, walking, with W held for the 100 ticks the Atlas is up: the game's ticks
  and world/tick's rise by one a tick, the world is never paused and the session stays playing, columns stream in, and
  the body does not move. Closed, the same W walks it more than a block.
- **Esc:** in a played world, Esc with the Atlas up closes it and the world plays on; a second Esc brings up the pause
  screen. In a test world under real input, Esc with the Atlas up closes it and `game_loop_source` goes on; a second
  ends the run.
- **Tiles kept:** a played world walked until over 50 columns have generated, closed (which saves its tiles), and the
  Atlas opened again on `<world>/atlas` with nothing streamed: every pixel of 40 by 40 columns round the start is as it
  was, and no file is refused.
- **The built program:** `--headless --no-sound --test-world --walk` with a script that presses `m`, waits 40 ticks and
  presses `escape`: the two Atlas screenshots differ from the world in over 90% of pixels, the explored land is there
  with grey round it, the last is the world again, the body ran all 90 ticks, and it ends with no leak.
  `MC_ATLAS_PNG_DIR=1` keeps the screenshots and prints where they are.
