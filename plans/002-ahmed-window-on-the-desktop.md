# 002-ahmed-window-on-the-desktop

**Status:** done   (updated 2026-11-04)

## Goal

The game runs in a window on Ahmed's XFCE desktop, started from a terminal with the same command as before.

## Why this changes plan 001

Plan 001 chose KMS from a text console, on Ahmed's answer of 2026-11-01, and refused to run under X. On 2026-11-04 Ahmed asked to run it on the current XFCE GUI. That plan's record stays as it was. KMS stays too, for when there is no desktop: `DISPLAY` decides.

Plan 001 named raw X11 as the known alternative to KMS. The rules hold: no libX11 or xcb, our own protocol code, and no TCP. The one exception written into `plans/CODING_STANDARD.md` section 1 is the local Unix socket that X is reached by, in one file.

## Design

- `run/src/display/x11/wire`: the protocol as bytes. No I/O, and unit tested.
- `run/src/display/x11`: the connection, the window, MIT-SHM frames, and input.
- A window needs `DISPLAY` set **and** stdin on a terminal. `tests/harness` gives every program it runs `/dev/null`, so nothing `ctest` runs can ever open a window on a desktop, and the tests that it refuses under X still hold.
- `input` is fed by the window and never opens `/dev/input`, which would read what is typed at other windows.

## Todo

- [x] Protocol layer, with unit tests in `tests/run/display/x11`
- [x] Connection, window, shared-memory frames, pointer and keyboard
- [x] Wire into `display`, `input`, `main`; allow the socket in `standards_lint` for one file only
- [x] Checked on a private `Xvfb`, with no window on the desktop: test pattern, then the real game, driven by `xdotool`
- [x] Ahmed runs it on XFCE

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 ctest --preset release -R '^(run_display_x11|run_display_mode|run_display_vt_guard|standards_lint|clang_format)$' --output-on-failure

Ahmed, from a terminal on the desktop:

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

- `faaf936` Run the game in a window on an X desktop such as XFCE, through our own X11 protocol code over the local Unix socket with frames in shared memory.

## Issues found

All agent-runnable checks passed on 2026-11-04: `ctest --preset release`, 100 of 100. On a private `Xvfb`, with the real `xfwm4`: full screen at the screen's size, pointer held while focused and let go when focus went to another window, held again on return, and the close button ending the run with nothing leaked. A block mined by real clicks was gone in the next picture. None of it ran on the live desktop, which is the box still open.

Known limits, in `run/src/display/x11/README.md`: pointer acceleration applies, no vsync, one window size per run. Alt+Tab itself was not pressed in these tests: `xdotool windowactivate` moved the focus, since `xfwm4` on `Xvfb` had no key settings to read.

### Ahmed's first run on the live XFCE desktop, 2026-11-04

`--windowed`, three world sessions. It worked: 129 input events, 12 blocks mined, a world saved and reloaded from where it was left, Quit Game, `heap at exit: 27 reserved, 27 released, 0 bytes live`. Two things his log shows, neither fixed yet:

- **Two worlds got one seed.** Expected: a world made with no seed is new each time. Actual: `test_1` and `test_2` both report seed 2198175988018515243, in one run of the program. Evidence: `run/src/main/index.c` works out `blank_seed` once, before `game_session_init`, and his log lines from `save/world/index.c:621`. Left alone: he asked for a commit, and it is his to prioritize. Starting the program again gives a new seed.
- **64 FPS in play, against frames of about 9 ms.** Expected: about 110 pictures a second from a 9 ms frame. Actual: `1529 new pictures on the screen in 23.9 s: 64.1 FPS` with `total mean 8.875 ms`; the second session, with `shade` at 3 ms, gave 106.4. Cause not found. Left alone, for the same reason. The window's own loop ran at 119.9 fps throughout, so it is not the X11 present holding it back.
