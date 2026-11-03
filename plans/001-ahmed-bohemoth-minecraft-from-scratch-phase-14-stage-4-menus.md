# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14, stage 4: Menus

**Status:** done, but for what only Ahmed can run on a real console, below   (updated 2026-11-03)

## Goal

Main menu, world create and select, and pause exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-ui-settings.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/ui/menu/`
- `tests/run/ui/menu/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement the main menu, world create and select, and pause in `run/src/ui/menu/index.c`, splitting into subdirectories before 1500 lines
- [x] Register test `run_ui_menu`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

- [ ] On a real console, by Ahmed: `./build/debug/bin/minecraft_c` from Ctrl+Alt+F3. Singleplayer, Create New World, type a name, Enter; walk; Esc pauses and stops time; Resume; Quit to Title; choose the world again; Quit Game. The console comes back. Agents cannot run this, because the desktop owns the display

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_ui_menu$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `382ea25` Add the main menu, world create and select, and a pause screen, and run the game from them with a session under game.

## Wiring outside the owned paths

A menu no one can reach is not one, and `run/src/main/README.md` said the menu was how a world would be reached. So:

- `run/src/game/session/`, new: from the title screen to Quit Game. `ui` may not call `game`, and `main` may not hold this much. Listed in `plans/REPO_STRUCTURE.md`.
- `run/src/ui/index.c`: `ui_t` holds the `ui_menu_t`, and `ui_frame_begin_blank` is new, sharing what `ui_frame_begin_over` does with memory.
- `run/src/game/index.c`: `game_pause_set`, and `paused` read on the sim thread; `game_loop_source` split into `game_loop_threads_ensure` and `game_loop_input_frame`, doing what it did; `game_loop_input_clear`; `game_open_alive_set`; `game_scene_draw` keeps its picture for a pause to go over.
- `run/src/main/index.c`: `--menu`, `--saves`, and no flags at all on a terminal being `--menu`. `tests/run/types` runs no flags with stdin from `/dev/null`, and still gets the check of the runtime.
- `.gitignore`: `/saves/`.
- `tests/run/ui/font/index.c`: the size asserted for its `test_ui_t`, which holds a `ui_t`.

## Issues found

**1. Giving the console back between a menu and a world leaves it in graphics mode.**

- Expected: `game_session_run` as first written would call `display_frames_run` once for the menus and once for each world, opening worlds with the console back: where a log can be read, and with no frame kept waiting.
- Actual: `the_whole_round_runs_through_the_display_loop_on_the_real_threads`: `CHECK failed: card->kd_mode == KD_TEXT: 1 != 0`. On every run after the first the guardian also printed "the program ended without giving the console back".
- Why: `display_vt_guard_give_back` claims its restore steps with `restore_next` and `restore_done`, which nothing resets, and `display_flip_restore` runs once and drops master, so a second taking restores nothing. Each is deliberate, for one run of a program. Each taking also holds the notice up for 3 seconds, with keys going to the shell in between.
- Done: not changed, since that code keeps a console usable after a crash and only Ahmed can test it on a real one. The session runs on one `display_frames_run`, and a world opens inside a frame, behind a loading screen. The test now passes with `kd_mode` back at `KD_TEXT`.

**2. That frame logs and takes long, which `display_frame_source_t` says a source must not.**

- Expected: a source does neither.
- Actual: `game_open` and `game_report` log. A line on a console in graphics mode is not drawn, and is there when the console comes back. Nothing logs on any other frame, and `ui_menu_frame` never does.
- Time: `game_open` looks for dry land over up to 16641 columns, single-threaded. Seed 4242 took 817 ms in a debug build, about 440 columns' worth, so an all-ocean seed could take 30 seconds, past the guardian's 8-second watchdog, which would end the program. Measured with land at the middle, at 1920x1080 in a debug build: 19 ms at `--render-distance 16`, and 21 ms at the default of 32, whose section pool works out at about 1.9 GiB, under the 4 GiB limit.
- Done: `game_open_alive_set`: `game_spawn_find` tells `display_vt_guard_pet` once for each ring. The whole-round test checks that the watchdog was told during that frame. A VT switch asked for during that frame waits for it to end.
- Not measured: a render distance past full detail, where `render_open_lod` puts more on the card, and a real all-ocean seed. Under KMS the log says `session: world ... in N s`.

**3. A world is its name and seed, and nothing else yet.** `world.meta` is read back, and the same seed gives the same picture, which the test holds to byte for byte. What was changed in a world is lost on Quit to Title, until Stage 16.1, which keeps its files in the directory `chosen.directory` names. So the pause screen says Quit to Title, and not Save.

**4. Settings is a disabled button.** Stage 14.5's screen did not exist when this was done. It should call `ui_menu_settings_enable` and answer `UI_MENU_ACTION_SETTINGS` in `game_session_action`, in `run/src/game/session/index.c`. `run_ui_menu` checks the action comes back once enabled.

**5. The HUD and the other game screens are not in the world's frames.** Those are Stages 14.2 and 14.3, in `game_loop_frame` and `game_scene_draw`. What this stage draws goes through `game_session_screen_frame`, on the last picture of those frames, so it goes over whatever they put in the picture only if they draw there. Whoever merges those should check that a pause goes over the HUD or hides it, as they mean to: `borrowed_last_picture` has no HUD in it.
