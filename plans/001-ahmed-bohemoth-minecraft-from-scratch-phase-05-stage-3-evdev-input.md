# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5, stage 3: Evdev input

**Status:** not started   (updated 2026-11-01)

## Goal

evdev discovery by capability, raw mouse and keyboard, and hot-plug exist. Proof: replay tests, plus Ahmed live.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/input/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/input/*`
- `tests/run/input/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `input` Owner tier: `run/src/input/index.c` with paired `input_init` and `input_deinit`, owning the regions and arenas its parts borrow
- [ ] Discover devices under `/dev/input/` by capability in `run/src/input/discovery/index.c`
- [ ] Read raw keyboard and mouse events in `run/src/input/events/index.c`
- [ ] Handle hot-plug
- [ ] Add event-replay tests in `tests/run/input/`, registered as `run_input`
- [ ] Add the cursor square to `--test-pattern`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_input$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 60 build/debug/bin/minecraft_c --test-pattern

## Commits

Appended as they land:

## Issues found

None.
