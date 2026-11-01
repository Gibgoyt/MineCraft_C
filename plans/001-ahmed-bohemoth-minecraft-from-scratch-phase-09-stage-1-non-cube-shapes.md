# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 1: Non-cube shapes

**Status:** not started   (updated 2026-11-01)

## Goal

Non-cube block shapes (slabs, stairs, plants, torches) and alpha exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/traverse/` shape table
- `cuda/{src,include}/shade/` alpha
- `tests/cuda/shapes/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Add a shape table of boxes to `cuda/src/traverse/index.cu`
- [ ] Add slabs, stairs, plants and torches
- [ ] Add alpha handling in `cuda/src/shade/index.cu`
- [ ] Add a setting, a recorded cost and a golden image; register test `render_feature_shapes`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_shapes$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
