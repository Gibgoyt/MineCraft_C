# tests/run/ui/menu

**Purpose:** ctest `run_ui_menu`. Tests `run/src/ui/menu/` with `catalog`, and `run/src/game/session/`.
**Tier:** test.
**Owns:** the test arena, a directory of its own under `/tmp` for each test that wants one, removed at its end, and for two tests blocks on the card, well under 1 GiB.
**Lifetime:** One run.
**Threads:** main; the built program, and the real `sim`, `gpu` and guardian in the last test.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/display/mode/fixtures/`, `tests/run/input/fixtures/`, `run/src/ui/`, `run/src/game/` and everything under it, the built program, and a card.

Compute only. No real display, console or input device is opened: the last test's kernels are fakes, and the one run of the program without `--headless` has stdin from `/dev/null`, so that `display_init` refuses it before anything else happens.

What it holds to:

- **What is typed:** seeds, numbers and words against FNV-1a's published vectors; names; directories that cannot leave `saves`; a `world.meta` cut at every length and wrong in each way.
- **On disk:** made, found, sorted, taken names, what is no world and what is a broken one, more than fit, and a file in the way. All by hand.
- **Each screen** on input frames made here, clicking where the layout puts things, by numbers worked out by hand: buttons, a list's scrolling and its rows, the fields, Enter and Esc, and what goes wrong said on the screen.
- **The pause screen** blends the shade once over the picture, by the reference blend, and leaves the picture alone.
- **No heap and no table memory** in 200 frames of each screen.
- **Every size,** from 320 by 240 to 3840 by 2160.
- **The built program,** by `fixtures/flow.script` under `--headless`: all the way round and out, with a world opened twice from one seed giving one picture, another seed another, and no leak. Then what the command line refuses.
- **All of `game_session_run`** through `display_frames_run` on fake kernels, with the real threads and a real guardian forked, on one taking of the console: a world opened, paused, left and opened again, 300 ms of pause with no ticks, and everything gone at the end.

**The pictures.** Test `every_screen_is_the_picture_that_was_looked_at` compares a hash of each screen with the `HASH_*` numbers, set after looking at them. To look again, whether after a change to the look or because the test fails:

    mkdir -p /tmp/menu_shots
    UI_MENU_PNG_DIR=/tmp/menu_shots build/debug/bin/tests/run_ui_menu build/debug/bin/minecraft_c

Run it with `tools/test-or-build.sh` and `timeout` in front. `UI_MENU_PNG_DIR` also keeps every size of every screen, and the pictures the built program took. If the screens are right, the run prints the new hashes.
