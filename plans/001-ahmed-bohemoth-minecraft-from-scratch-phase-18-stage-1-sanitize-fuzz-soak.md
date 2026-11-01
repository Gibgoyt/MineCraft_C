# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18, stage 1: Sanitizers, fuzz and soak

**Status:** not started   (updated 2026-11-01)

## Goal

Sanitizer and fuzz runs (YAML, PNG, saves) and a multi-hour soak are complete.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-release-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `tests/fuzz/`
- `tests/soak/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Add fuzz targets for YAML, PNG and saves under `tests/fuzz/`
- [ ] Run the sanitize preset across all tests
- [ ] Add a bounded headless soak under `tests/soak/`, and record results
- [ ] Log every defect found in this file

## Verification

    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 600 ctest --preset sanitize --output-on-failure
    tools/test-or-build.sh timeout 14400 build/sanitize/bin/soak
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
