# tests/run/ui/screens

**Purpose:** ctest `run_ui_screens`. Tests `run/src/ui/` with `screen_inventory`, `screen_crafting` as a table and as a woodworking bench, `screen_furnace` and `screen_chest`, and what `run/src/game/` does with them.
**Tier:** test.
**Owns:** the test arena, and the regions of what it starts, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the real-game test starts the game's own threads, and the built program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/ui/`, `run/src/game/`, `run/src/item/`, the built program, and a card for those last two.

What it holds to:

- Every screen's slots: how many, all inside the window, none on another, and that hit tests give each one back at three scales, its edges too.
- **Replays.** `fixtures/*.script` are real scripts, in the language of `run/src/display/headless/`. Each goes tick by tick through `game_input_from_script` and the calls `game_scene_tick` and `game_scene_draw` make, with the pointer moved by counts, as a mouse does. What they did to the items is then checked, and every `screenshot` line held to a PNG of that name in `fixtures/`, byte for byte.
- 20000 random clicks on each screen lose no item, make no bad stack and keep the inventory's books.
- The Coordinates box on the Settings screen: it changes that value alone, looks different, comes back the same, and leaves VSync and Effects... where a click finds them.
- A full inventory at closing, a click outside the window, a full ring, a view that is a copy, a picture left alone, no heap in 200 frames.
- On the real card, on one thread: E, Esc and the right button on each block; feet and hands stop; no placing on a chest, and a held button does not bring one back; a real chest filled by mouse and broken with its screen up.
- On the real threads: a click from main to the sim and back to what is drawn, with what it took printed.
- `minecraft_c --headless --test-world --walk` opening and closing by script, and showing it.

**The golden pictures.** They are 320 by 240, scale 1, on a made-up gradient. To make them again after a change to the look, then to look at every one before committing:

    MC_GOLDEN_WRITE=1 TEST_FILTER=replay tools/test-or-build.sh build/debug/bin/tests/run_ui_screens build/debug/bin/minecraft_c

From the repository's root, as the paths are relative to it.
