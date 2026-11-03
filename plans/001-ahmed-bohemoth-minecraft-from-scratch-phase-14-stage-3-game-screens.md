# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14, stage 3: Game screens

**Status:** done   (updated 2026-11-03)

## Goal

Inventory, crafting, furnace and chest screens exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-ui-settings.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/ui/screen_inventory/`
- `run/{src,include}/ui/screen_crafting/`
- `run/{src,include}/ui/screen_furnace/`
- `run/{src,include}/ui/screen_chest/`
- `tests/run/ui/screens/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement each screen in its own directory
- [x] Add golden images and input replay, registered as `run_ui_screens`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_ui_screens$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    # E, then the mouse, then E or Esc. Mine, craft a table, place it, and right-click it.
    ./build/debug/bin/minecraft_c --test-world --walk

## Commits

Appended as they land:

- `cb0f922` Add the inventory, crafting, furnace and chest screens, opened by E and the right button, with clicks crossing from main to the sim and golden pictures from replayed scripts.

## Issues found

1. **The screens had to reach the running game, which the stage's owned paths do not cover.** Expected from the todo alone: four directories. Stage 13.2's issue 3 says that all of an inventory is the sim's and these must cross from `main`, and a screen no one can open would not be a screen. Done, as the smallest wiring found: `run/{src,include}/ui/index.*` (`ui_t` from 576 to 640 bytes, and the `ui_screen_*` calls that choose a screen by kind); `run/{src,include}/game/index.*` (`game_ui_set`, `game_screen_input`, `game_input_from_script`, a call each in a sim step, a tick, a body's death, both input paths and both draw paths; `game_t` stays 1152 bytes); `run/{src,include}/player/index.*` (`player_t.hands_held_off`, from padding, and `player_sneak_sent`); `run/{src,include}/main/index.*` (`game_ui_set` under `--walk`, `app_t` from 12864 to 12928 bytes, the usage text); `tests/run/ui/font/index.c` (`test_ui_t`'s size); the READMEs of `ui`, `game` and `main`; `plans/REPO_STRUCTURE.md`.
2. **What all four share lives in `screen_inventory`.** Expected: four peers. All four draw the same window and the player's rows and use one road between the threads, and the stage lists no directory for that. Done: it is in `screen_inventory`, since every other screen is the inventory plus something, and the other three include it, as its README allows.
3. **Will meet Stage 14.2 at the same lines.** That stage is being built at the same time from the same base, and will also grow `ui_t` and `app_t` and draw over the picture in `game_loop_frame` and `game_scene_draw`. Here, those two calls are one `game_picture_with_screen`, and with nothing up it does not copy. When merged, the sizes add up, and a HUD should draw under a screen, so it goes first within a single frame begin and end, and `ui_screen_over` will need splitting into its draw part. Left for the merge, as neither stage can see the other.
4. **A button held through a screen brought it straight back.** Expected: closing with the right button down leaves it closed. Actual, in the real-game test: `in_a_real_game_e_and_the_right_button...: CHECK failed: ui_screen_sim_kind(g->ui) == UI_SCREEN_NONE: 4 != 0`, for each of the three blocks. `main` sends no buttons while a screen is up, so the tick after closing saw the held button as a new press. Fixed in `game_screen_hands`: the button counts as held for as long as a screen is up.
5. **The window is the textures' own and has a few marks.** `textures/ui/panel` is 165 by 170, from the pack, with specks that the nine parts stretch near the top right. Seen in every golden picture. It is what `ui_widget_panel` draws for all of `ui`, and is not this stage's to change. Left alone.
6. **No player in the inventory window, and no recipe book or drag-to-spread.** No model of a player exists to draw until a later stage, so that part of the window is empty. A drag across slots is not in the plan's list, and clicks do all of it, one at a time.
7. **Not reachable by any test here: the real keyboard and mouse.** `game_loop_source`'s calls need KMS and a real device, neither of which may be opened here. The same `ui_screen_input` is proved with made-up polls across the real threads, and through a script in the built program. Left for Ahmed's own run: see Verification.
8. **`run_render_frame` failed here, on the check Stage 13.2 issue 6 already records.** Expected: pass. Actual: `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap` failed at `tests/run/render/frame/index.c:1349` (`game->columns_made > 100u`) and `:1355` (`picture` against `screen`) on 2 of 3 runs alone, and in the wider run below. That test sets neither `game_ui_set` nor `game_player_set`, so `game_has_screens` is false in it and no screen code acts. It was not rerun on a build without this stage, so the claim that it is the old failure rests on those two facts and the same checks failing. Left alone: not this stage's.

## Evidence

- `run_ui_screens`: 18 tests, 0 failed, on the debug and sanitize presets (ASan and UBSan quiet). On the real threads a click was on the screen 2.8 ms after it was sent, on one run.
- The golden pictures were set after looking at all seven, some at four times their size: slots, stacks, counts, the wear bar, tooltips, the pointer's stack, the gauges part way.
- Two mutations to show the tests can fail, both restored: a wear bar one pixel shorter failed `inventory_carrying.png` by 14 bytes, and one item too many in the books failed the random clicks, 1236 of their checks.
- The table arena holds 648384 bytes of its 1.5 MiB with all of `ui` loaded.
- `ctest --preset debug -j3 -E 'render_feature_(sky|lod|shapes|water|post)|^cuda'`: 73 of 74 passed, the other being issue 8. The tests that this leaves out were not run: they use no player or `ui`, and take minutes each on the card.

## Phase gate check, 2026-11-03

**9. `run_render_frame` (issue 8 above, and Stage 14.5 issue 6): the wait in the test, now fixed.**

- Reproduced at the gate: failed in both `ctest -j3` and `-j4` runs of the whole suite, and passed all 6 runs alone. The test took 400 identical frames as all having arrived, and `game_loop_frame` shows the same picture again when no new one has come, so a loaded machine ended the wait with columns still coming.
- Done: in `tests/run/render/frame/index.c`, only a new picture counts, and only with the sim's `stream_quiet` set. Then 3 of 3 passed alongside three heavy tests at `-j4`. Nothing in `game` or `ui` changed for this. That is good evidence for the reading in Stage 14.5 issue 6, but no failure was caught in the act before the change.

Issue 3 above, met at the merge: `game_hud_compose` draws the HUD, then the overlay, then a screen, within one frame. Seen in a screenshot of the built program, with F3 on and the inventory up.
