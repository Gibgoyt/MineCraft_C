# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 2: Sky, shadows and stored light

**Status:** not started   (updated 2026-11-01)

## Goal

Sky, day cycle, fog, sun shadows and stored light exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/shade/`
- `cuda/{src,include}/post/`
- `tests/cuda/sky/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Add sky, day cycle and fog
- [ ] Add one sun shadow ray
- [ ] Use stored sky and block light in shading
- [ ] Add a setting, a recorded cost and a golden image for each effect; register test `render_feature_sky`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_sky$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
