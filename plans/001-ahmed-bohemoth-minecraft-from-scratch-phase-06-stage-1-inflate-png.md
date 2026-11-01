# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6, stage 1: Inflate and PNG decoder

**Status:** not started   (updated 2026-11-01)

## Goal

Our own bounds-checked decoder exists. Packs are untrusted input.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-textures.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/inflate/`
- `compile/{src,include}/png/`
- `tests/compile/inflate/`
- `tests/compile/png/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement `compile/src/inflate/index.c` with bounds checks on every read and write
- [ ] Implement `compile/src/png/index.c`, rejecting malformed files with clear errors
- [ ] Add tests with valid and corrupt files, registered as `compile_inflate` and `compile_png`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_inflate$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_png$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
