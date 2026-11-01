# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 4: AO, TAA and god rays

**Status:** not started   (updated 2026-11-01)

## Goal

AO with temporal reuse, TAA and optional god rays exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/post/`
- `cuda/{src,include}/shade/` AO
- `tests/cuda/post/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Add short AO rays at reduced rate with temporal reuse
- [ ] Add TAA in `cuda/src/post/index.cu`
- [ ] Add optional god rays
- [ ] Add a setting, a recorded cost and a golden image for each effect; register test `render_feature_post`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_post$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
