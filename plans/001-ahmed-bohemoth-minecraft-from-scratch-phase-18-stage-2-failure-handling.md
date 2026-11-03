# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18, stage 2: Failure handling

**Status:** in progress: all code and `run_failure` done; the unplug and VT switch on real hardware are Ahmed's run   (updated 2026-11-03)

## Goal

Failure handling exists for GPU loss, device unplug, disk full, and VT switch during load.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-release-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/gpu_bridge/*`
- `run/{src,include}/input/*`
- `run/{src,include}/save/*`
- `run/{src,include}/display/vt_guard/`
- `tests/run/failure/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Handle GPU loss. A real fault on the real card under the real threads, and through the built program (exit 9). A hang is noticed by the gpu thread's silence; that one is simulated, as no way to hang a driver on a shared card is safe
- [ ] Handle device unplug (written; passes on the fake evdev and fake card; a real device pulled out is Ahmed's run, below)
- [x] Handle disk full. The kernel's own `ENOSPC`, on a small `tmpfs` in a user namespace, with no card and under a whole game
- [ ] Handle VT switch during load (written; passes on the fake kernel, with the signals real; a real VT switch is Ahmed's run, below)
- [x] Add tests where the failure can be simulated, registered as `run_failure`: `tests/run/failure/`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_failure$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/debug/bin/minecraft_c
    # Unplug a device, and switch VT during load.

## What each failure now does

The Before column is from reading the code as it stood, not from running it.

| Failure | Before | Now |
|---|---|---|
| Card faults | The run ended and the world saved, then every free failed in the log and `main` could report a leak (exit 4) | Blocks count as given up (`device_memory_t.lost`), no leak, exit 9, and the log says what happened and whether the world was saved |
| Card hangs, no error | The last picture for ever, and `game_loop_stop` would join for ever | `game_gpu_alive` sees 10 s of silence; the sim is joined, the world saved, the console back, `_exit(9)` with the stuck thread not joined and nothing it may hold released |
| Keyboard or mouse pulled out | Held keys let go, and the world went on with nobody at it | Also pauses a world in play, and says which device on the screen until it is back |
| Display goes | Run ends, exit 7, world saved | The same: now tested |
| Disk full in play | One line in a log nobody can read under KMS | A red band on the HUD and a line on the pause screen, until a save works again |
| Disk full on Quit to Title | The world closed, its last changes lost, and the program ended | The world stays open on the pause screen, which says why and offers Leave Unsaved. Quit again once there is room |
| VT switch during load | Unanswered until the world had opened, with the person looking at nothing | Answered inside the frame (`display_vt_guard_long_work`), opening goes on, nothing goes to the card while away, and what was typed there is thrown away |
| VT switch before the console is taken (`--test-world` load, the 3 s notice) | KD_GRAPHICS and a modeset over whatever VT was showing, holding master that VT's owner needed | Lets go of master and waits; takes over when our VT is back; a stop signal leaves everything as it was |

**Wiring outside the owned paths,** each the smallest that lets the failure be handled:

- `run/src/game/` and `run/src/game/session/`: the gpu watch, `game_open_alive_tell` in each long step, the session's handling of all of the above.
- `run/src/display/index.c`: the frame's end looks at the guard before flipping; `display_vt_guard_came_back_take` replaces a local flag.
- `run/src/main/`: `MAIN_EXIT_GPU_LOST`, `--pretend-gpu-lost`.
- `cuda/src/device_memory/`: `device_memory_fault_now`, and giving up blocks once lost.
- `run/src/ui/menu/` (Leave Unsaved, the message on the main and pause screens) and `run/src/ui/hud/` (`ui_hud_notice_draw`).
- `tests/run/display/mode/fixtures/fake_card.h`: `vt_release_stays`. `tests/run/settings/session/index.c`: the size of a struct holding `game_session_t`. `tests/run/display/vt_guard/index.c`: issue 8.

**Results.** See Commits for the sha. The whole debug suite again on the merged tree, with issue 8 fixed: 97 of 97 in 20 min 52 s. After issue 9's move of test code: `run_failure`, `standards_lint`, `clang_format` pass. `sanitize` preset: `run_failure` passes (27 s). `tsan` preset: see issue 10. `run_failure`: 13 tests, 0 failed, 22 s. Two mutations were checked to fail it, then undone: answering a switch only with a pet (`away_in_open`, `answered_in_open` fail) and flipping while away (`flips_while_away` fails). Real fault's words: `cudaErrorIllegalAddress: an illegal memory access was encountered`, and a write afterwards fails the same way. Xorg stayed up through every run (`pgrep`), and the kernel's log shows `NVRM: Xid 31` for this program's pid only.

## Commits

Appended as they land:

- `0d1b098` Failure handling: a lost or hung card saves the world and exits 9, a pulled device pauses play, a full disk is said on the screen and holds nobody, and a VT switch during a load is answered at once.
- `6019af7` Record the failure handling commit in its stage file.
- `abd0923` Move what the failure tests share into a fixture to keep the file under the split limit, and record the full suite and sanitizer results for failure handling.

## Issues found

1. **`game_session_run` handed a wrapped source's context to the session's discard.**
   - Expected: coming back from another VT calls `game_session_discard` with the session. Actual: it got whatever context the source had, so with a test's wrapper it read a `fail_run_t` as a session and died: SIGSEGV in `input_events_begin_frame`, from `game_session_discard` (gdb, on `a_vt_switch_while_...`).
   - `main` passes the session as both, so the program was never hit. `run_ui_menu` wraps too, and never switched VT.
   - Fixed: the session keeps the source and its context, and always gets its own. `game_session_t` is 136 bytes.

2. **A real fault leaves an `Xid 31` in the kernel's log, once for each of the three runs in `run_failure`.**
   - Expected and accepted: that is what a real fault is. Evidence: `journalctl -k`: `NVRM: Xid (PCI:0000:01:00): 31, pid=..., name=run_failure ... MMU Fault ... faulted @ 0x0_00001000`. Only this process's context goes.
   - `device_memory_fault_now` is on purpose at a small fixed address, so a line with `@ 0x0_00001000` is this test and not a real problem. Worth knowing when reading the log.

3. **The usage text went over what a string may hold.**
   - Expected: two more lines fit. Actual: `string length '4238' is greater than the length '4095'` (`-Werror=overlength-strings`). Fixed: they went in the second of the two strings.

4. **Left alone: the world's time goes on while another VT is showing.**
   - Expected, from "while away, it pets the watchdog and touches nothing": perhaps a stopped world. Actual: the display does nothing, but the sim thread keeps ticking, so a player left standing on another VT can die. `game_loop_discard` only throws input away. The inverse of this stage's device case.
   - Not this stage's list (only a switch during load is). A pause on going away would need a hook from `display_run_loop` that does not exist. For Ahmed to decide.

5. **Left alone: what closing logs after a lost card.** `render_frame_deinit` still logs `frame: unpinning host memory` three times and `frame: giving a clock event back`, each with CUDA 700, since those are not `device_memory`'s. Harmless, and the session's own line now comes after them, so it is what a person reads last.

6. **Limits, written down so nobody takes them for covered.**
   - A card that hangs while main itself is waiting on it, in `game_open` or in `game_reconfigure`'s join, is not seen by `game_gpu_alive`. The watchdog ends it after 8 s and the guardian gives the console back, with the world as of its last autosave.
   - A final save that takes more than 8 s on a stalled disk is the same: the alive calls are between steps. A kill is safe, as `run_save_state` shows, and loses that save.
   - A monitor turned off or pulled out ends the run if flips stop for a second (`no flip event within a second`), with the world saved and exit 7. It does not wait for the monitor to come back.

7. **Not run, because no agent may:** a real KMS run, a real device pulled out, a real VT switch. See below.

8. **`run_display_vt_guard` held to the old behaviour in one check.**
   - Expected: nothing else changes. Actual: the whole suite gave 96 of 97, with `the_keys_mean_nothing_while_another_vt_is_showing: CHECK failed: rig_count(rig, KDSETMODE, KD_TEXT) == 1u: 0 != 1`. That test takes the console with another VT showing, which is now a console not taken: no `KD_GRAPHICS`, so no `KD_TEXT` owed.
   - Fixed in the test, which also checks there was no `KD_GRAPHICS`. What it is for, that the keys do nothing there, still holds. `run_display_vt_guard` passes.

9. **`tests/run/failure/index.c` was 1626 lines.**
   - Expected: split at 1500 (`plans/CODING_STANDARD.md`, line 286). Actual: over it. `standards_lint` only enforces the 2000, so nothing said so.
   - Fixed: what the tests share went to `fixtures/rig.h` (496 lines), leaving `index.c` at 1138. One program still, and no test changed.
   - `run/src/game/index.c` is at 1889 and was already past 1500 before this stage (1756). Not split here, as it is outside this stage's paths beyond wiring.

10. **`run_failure` fails under the `tsan` preset, on what the test itself does.**
   - Expected: no report. Actual: `ctest --preset tsan -R '^run_failure$'` fails in 44 s with 10 `data race` reports from 6 pairs of places, and 2 refused mounts. No other check failed; each child failed only by TSan's exit status or by the mount.
   - Each report has a test's side on it: `fail_step_do` (`fixtures/rig.h:451`) reading the sim's `game_t.tick_count` from main, against `game_sim_tick` (`run/src/game/index.c:1394`); and `vt_coming_back_thread` and `vt_second_back_thread` (`index.c:907`, `:1091`, `:1093`) reading `guard.away`, `game.open`, `card.flips`, `kd_mode` and `alive_script`. None is between two places of the program's own.
   - The two disk tests did not run: `unshare: Invalid argument`, as TSan's own background thread makes every process threaded, and `unshare(CLONE_NEWUSER)` refuses that. So the disk full paths have had ASan and UBSan and not TSan.
   - Left alone: the tests read those to watch from outside, and the preset is run by hand (`plans/CODING_STANDARD.md`, line 451). Quieting them would mean making `tick_count` atomic for a test.

## For Ahmed

On a text console (Ctrl+Alt+F3, log in):

    ./build/debug/bin/minecraft_c

- Make a world on a seed that takes a while. While "Loading world..." shows, press Ctrl+Alt+F2. Expected: that VT appears at once. Come back: the loading screen or the world, and Esc pressed on F2 did not pause it.
- In a world, pull out the keyboard. Expected: pauses at once, and says "The keyboard has gone: plug one in". Plug it in: the words go, the pause stays, Esc resumes. The same with the mouse.
- `./build/debug/bin/minecraft_c --test-world`, and press Ctrl+Alt+F2 while it opens. Expected: F2 stays usable, nothing is drawn over it, and the picture comes when you go back.
