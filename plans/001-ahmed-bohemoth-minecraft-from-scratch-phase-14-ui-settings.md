# 001-ahmed-bohemoth-minecraft-from-scratch — phase 14: UI and settings

**Status:** in progress   (updated 2026-11-03): the gate has passed in everything an agent can run. What awaits Ahmed: the run on a real console below.

## Goal

The HUD, all game screens, menus and settings work, including render distance in chunks.

**Wave:** W8.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-1-font-widgets.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-2-hud.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-3-game-screens.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-4-menus.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-14-stage-5-settings.md`

## Todo

- [x] Stage 14.1: Font and widgets
- [x] Stage 14.2: HUD
- [x] Stage 14.3: Game screens
- [x] Stage 14.4: Menus
- [x] Stage 14.5: Settings
- [x] Phase gate passed: headless in the built program, and on the real threads on fake devices
- [ ] Ahmed, on a real console (Ctrl+Alt+F3, log in): `./build/release/bin/minecraft_c`. Create a world, Esc, Settings, turn the render distance slider: it shows behind the screen. Done, Esc, the world goes on. Quit Game, start again: the setting is still there. Then VSync off, watching F3. Then E, F3, mining, and the rest listed in the stage files

## Verification

**Gate:** Changing render distance in the menu takes effect without restarting, and persists.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_ui|run_settings)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/debug/bin/minecraft_c

## Commits

Appended as they land:

- `f7940b7` Show a new field of view behind the Settings screen, hold menu flags to what settings can hold, split input out of game, and repair the format and region count the merges broke.
- `6554ef6` Merge the integration branch, with game_settle split out as well to keep game under the size for splitting.
- `afed598` Record the Phase 14 gate check, and let the sanitize preset build and run_render_frame wait for real pictures.

## Issues found

Gate check of 2026-11-03, from a clean build of the integration branch at `28f1566`, merged up to `ff8d27b` along the way. Each is written up in its stage file.

| Defect | Evidence | Done |
|---|---|---|
| A new field of view did not show behind the Settings screen until something else changed | A script with the wheel on that slider: the picture stayed at 70 | Fixed, Stage 14.5 issue 7 |
| `--menu --full-detail 99` and `--render-distance 1` got past every range, and the usage text said flags last "for the run" when Done keeps them | `main_settings_read` | Clamped and the text put right, Stage 14.5 issue 8 |
| `game/index.c` at 1610 lines, against the standard's 1500 and the stage's claim of being under it | `wc -l`; the lint stops only at 2000 | `game/input`, `game/settle`: 1495 lines, Stage 14.5 issue 9 |
| `run_types` failed: 13 regions pinned, 14 real | `tests/run/types/index.c:914` | Fixed, Stage 14.5 issue 10 |
| The sanitize preset did not build at all | `run/src/main/index.c:852`, a frame of 18112 bytes | Fixed, Stage 14.5 issue 11 |
| `clang_format` failed on 4 mob files after a merge | 7 violations | Formatted, at this gate |
| `run_render_frame` failed under load, as Stage 14.3 issue 8 and Stage 14.5 issue 6 record | Failed at `-j3` and `-j4`, passed alone | The test's wait fixed, Stage 14.3 issue 9 |
| Ended with 0 checks under ctest, silently | `render_distance_past_full_detail...` | Now says it did not run, and run by hand, Stage 14.5 issue 12 |

**Left open, and not this phase's.**

- **`run_mob_zombie` fails.** Expected: pass. Actual: `tests/run/mob/zombie/index.c:973: the_program_at_midnight_with_nobody_moving_gets_its_player_hit: CHECK failed: said[strlen("zombies struck ")] != '0'`; the log says `zombies struck 0 blows`, with the body hurt 4 times by something else. It fails the same on the integration branch at `ff8d27b` with none of this gate's changes, built and run on its own. Left for Phase 15's gate, since it is a question of what that test should now expect with the other mobs merged.
- **Real-time tests under load.** `run_player_movement`'s `the_real_threads_walk_a_real_world_at_the_speed_of_the_tick` failed once at `-j3` and passed 3 of 3 alone. `render_feature_sky` hit its time limit at `-j2` beside `render_feature_lod`, and passed alone in 172 s. Neither is this phase's code, and the fixed one above is the only such test changed here.

**Decisions left for Ahmed, each said in its stage file:** a pause hides the HUD and does not dim it (Stage 14.4). Flags given with `--menu` are kept if Settings is left with Done (Stage 14.5 issue 8). A column past the render distance still casts a shadow until the eye moves on (Stage 14.5 issue 2).

## Evidence

All on this branch, at `ff8d27b` merged, after a clean `rm -rf build/debug`:

- `ctest --preset debug -R '^(run_ui|run_settings)'`: 6 of 6. `standards_lint` and `clang_format` pass. On the sanitize preset the same 6 pass, with ASan and UBSan on.
- The gate, in the built program: `live.script`, 2 from 8 is 108 thousandths different, 4 from 8 is 21, 8 again 0, live 4 against a new program reading the kept file 0, live 2 against a new program at 2 0. Pictures looked at.
- A far field on a release build, by hand: `SETTINGS_FAR=1 ... run_settings`: 32 checks, 14 from 8 is 35, live 12 from a new program at 12 is 0.
- Mutations, both restored: `settings_save` not writing the file failed 4 tests of `run_settings`, and the field of view not being published failed 2 of `run_settings_session`.
- Standards review past the lint, over `ui`, `settings`, `game/session`, `game/view`: no `bool` in a struct, no owning `static`, no `goto`, no banned type, no allocation out of its tier, and no public function of `ui` or `settings` uncalled in `run/src`. No TODO or stub.
- The rest of the suite: 81 of 83 at `-j3` (out of 90, leaving out the long `render_feature_*`, `cuda_traverse` and `run_terrain_feature`), the two being those left open above. All of `render_feature_*` and `cuda_traverse` pass, `render_feature_sky` alone. `run_terrain_feature` passed at `-j4` on this code.
- `pgrep -af` on this worktree's build prints nothing.
