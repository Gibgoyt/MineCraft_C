# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5, stage 4: Headless backend

**Status:** done   (updated 2026-11-02)

## Goal

A no-display backend and PNG screenshot writer (stored deflate) exist, so agents can inspect output.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/display/headless/`
- `tests/run/display/headless/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement the no-display backend in `run/src/display/headless/index.c`
- [x] Write PNG screenshots using stored deflate (`run/src/display/headless/png/`)
- [x] Test that the output PNG is valid in `tests/run/display/headless/index.c`, registered as `run_display_headless`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each
- [x] Added by the orchestrator: scripted input. `minecraft_c --headless --script <path> --screenshot-dir <dir>` replays timed key and mouse events and writes PNGs at chosen ticks (`run/src/display/headless/script/`). Documented in `run/src/display/headless/README.md`

**Wiring outside the owned paths**, kept to what the module needs to be called:

- `run/{src,include}/display/index.*`: `display_offscreen_t` with `display_offscreen_init` and `_deinit`, since the Arena-tier backend may not reserve memory itself, and `display_offscreen_config_from_machine`. `display_t` is untouched.
- `run/{src,include}/main/index.*`: the flags, `app_t.offscreen`, and `MAIN_EXIT_HEADLESS` = 8.
- `run/src/display/README.md`, `run/src/main/README.md`, `plans/REPO_STRUCTURE.md`: the new parts.

**Results, 2026-11-02:** the whole debug suite passed, 32 of 32. `run_display_headless` ran 32 tests and 828203 checks, and also passed under the sanitize preset with no sanitizer report. Three files from the built program also passed Python's `zlib` with every chunk CRC checked, and `file` read them as PNG. `moved.png` was opened and looked at: the `w` lamp lit, tick lamps for 5, a red cursor moved by (60, -40). 1080p: 16 ticks and 3 screenshots of 6.2 MB each in 0.17 s on the debug build.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_display_headless$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `751761b` Add the no-display backend, with a stored-deflate PNG writer and a script of timed key and mouse events that takes screenshots at chosen ticks.

## Issues found

1. **The test could not include the decoder by relative path.**
   - **Expected:** `tests/run/display/headless/index.c` could reach `compile/include/png/index.h` as `../compile/include/png/index.h`.
   - **Actual:** the build stopped at `tests/run/display/headless/index.c:4: [include_relative] ... include from an include root, never by a relative path`, and `mc_add_test` has no setting for another include root.
   - **Done:** `tests/run/display/headless/test.cmake` makes an interface library that carries the include root and the `mc_compile_lib` objects, and sets it as `MC_TEST_LIBS`. `cmake/targets.cmake` did not need changing.
2. **That the program opens no device is shown by reading the code, not by watching it.**
   - **Expected:** a syscall trace of a `--headless` run with no `/dev/dri` or `/dev/input` open in it.
   - **Actual:** `which strace` finds nothing on this machine, and installing software is out of scope.
   - **Done:** left that way. `grep -nE 'dri|/dev/|open\('` over the four `run/src/display/headless` sources finds only a comment; files go through `run/src/file/`; `main_run_headless` does not call `display_init`. A trace is a reasonable check for Ahmed, in `needs_ahmed` of this stage's report.
3. **Scripted input does not pass through `input` yet, and drives only the demo scene.**
   - **Expected:** a script drives real gameplay through the real input path.
   - **Actual:** neither exists. No branch had `run/include/input` when this stage ran (`git log --all -- run/include/input` printed nothing; Stage 05.3 had not landed), and `game` is Stage 08.4.
   - **Done:** events carry evdev's own `KEY_*` and `BTN_*` numbers, and `move` and `wheel` carry what `REL_X`, `REL_Y` and `REL_WHEEL` would, so nothing has to be translated. `display_headless_scene_t` is where `game` plugs in, and `main_run_headless` passes the demo scene until it does. **Stage 08.4 owes the swap.** The README says so.
4. **Merging will touch shared structs.** `app_t` and `main_options_t` grew, and Stages 05.2 and 05.3 may add to `app_t` too. A merge conflict in `run/include/main/index.h` is to be expected, and its `static_assert` says if the sizes come out wrong.
