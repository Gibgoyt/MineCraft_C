# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 5: Entity boxes and outlines

**Status:** not started   (updated 2026-11-01)

## Goal

Entity boxes and per-tile lists, block outline and breaking overlay exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/entity_rays/`
- `run/{src,include}/render/entities/`
- `tests/cuda/entity_rays/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement per-tile entity lists in `run/src/render/entities/index.c`
- [ ] Intersect entity boxes in `cuda/src/entity_rays/index.cu`
- [ ] Add the block outline and breaking overlay
- [ ] Add a setting, a recorded cost and a golden image; register test `render_feature_entities`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_entities$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
