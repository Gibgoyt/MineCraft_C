# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5, stage 4: Headless backend

**Status:** not started   (updated 2026-11-01)

## Goal

A no-display backend and PNG screenshot writer (stored deflate) exist, so agents can inspect output.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/display/headless/`
- `tests/run/display/headless/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement the no-display backend in `run/src/display/headless/index.c`
- [ ] Write PNG screenshots using stored deflate
- [ ] Test that the output PNG is valid in `tests/run/display/headless/index.c`, registered as `run_display_headless`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_display_headless$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
