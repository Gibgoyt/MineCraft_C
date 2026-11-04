# 002-ahmed-window-on-the-desktop

**Status:** in progress   (updated 2026-11-04)

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
- [ ] Ahmed runs it on XFCE

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 ctest --preset release -R '^(run_display_x11|run_display_mode|run_display_vt_guard|standards_lint|clang_format)$' --output-on-failure

Ahmed, from a terminal on the desktop:

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

None.
