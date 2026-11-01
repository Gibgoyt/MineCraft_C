# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 3: Water and glass

**Status:** not started   (updated 2026-11-01)

## Goal

Water and glass transparency, reflection and depth tint exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/shade/` transparency
- `tests/cuda/water/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Continue rays through water and glass
- [ ] Add simple water reflection and depth tint
- [ ] Add a setting, a recorded cost and a golden image; register test `render_feature_water`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_water$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
