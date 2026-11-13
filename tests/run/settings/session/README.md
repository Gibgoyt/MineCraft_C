# tests/run/settings/session

**Purpose:** ctest `run_settings_session`. Tests `game_reconfigure`, `game_view_refresh` and what `run/src/game/session/` does with the Settings screen, on the real card with the real threads.
**Tier:** test.
**Owns:** the test arena, a directory of its own under `/tmp` for the test that wants one, removed at its end, and blocks on the card, well under 4 GiB.
**Lifetime:** One run.
**Threads:** main; the real `sim` and `gpu`, and real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/input/fixtures/`, `run/src/game/` and everything under it, `run/src/settings/`, `run/src/ui/`, and a card.

Compute only. No real display, console or input device is opened: pictures go to plain memory, and input comes from the fake evdev.

What it holds to:

- **Render distance in an open world:** in, out and past level 0 (held at 6 chunks by `block_radius_given`), counting columns against discs and a far field against its plan by the bands, a block placed before it all still there after. A world opened with room for more level 0 than it draws.
- **Everything else taking at once:** field of view, an effect and render scale, each coming back to the same pixels, a smaller picture brought up square by square, and the mouse.
- **With the real threads:** render distance, size and effects between two frames, with pictures going, and uncapped drawing more pictures than anyone takes.
- **A whole session,** on frames from `game_session_source` and input from fake devices: into a new world, Esc, Settings, the wheel on render distance and on render scale, the picture behind the screen drawn again in the pause, Done, the file, Esc, the same world at the same place with its time going on, and out. The display's own loop is what `tests/run/ui/menu/` and `tests/run/game/` hold to.
