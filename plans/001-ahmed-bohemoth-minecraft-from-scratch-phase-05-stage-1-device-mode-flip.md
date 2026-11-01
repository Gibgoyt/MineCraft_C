# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5, stage 1: Device, mode and flip

**Status:** not started   (updated 2026-11-01)

## Goal

Raw-ioctl card open, connector and mode pick, dumb buffers and page flip exist. Proof: Ahmed sees a test pattern on a VT.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/display/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/display/device/`
- `run/{src,include}/display/mode/`
- `run/{src,include}/display/flip/`
- `tests/run/display/mode/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `display` Owner tier: `run/src/display/index.c` with paired `display_init` and `display_deinit`, owning the regions and arenas its parts borrow
- [ ] Open the card from `machine.yaml` using raw ioctls and kernel UAPI headers only, in `run/src/display/device/index.c`
- [ ] Pick connector and mode in `run/src/display/mode/index.c`, honoring `connector: auto`
- [ ] Create dumb buffers and page flip in `run/src/display/flip/index.c`, using the Stage 01.2 results
- [ ] Add a `--test-pattern` mode
- [ ] Fail fast with a clear message when run from inside X
- [ ] Test mode picking with fixture data in `tests/run/display/mode/index.c`, registered as `run_display_mode`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_display_mode$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 30 build/debug/bin/minecraft_c --test-pattern

## Commits

Appended as they land:

## Issues found

None.
