# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9, stage 6: LOD clipmaps

**Status:** not started   (updated 2026-11-01)

## Goal

LOD clipmaps reach R=512, with worker-built coarse grids.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-renderer-features.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/lod/`
- `run/{src,include}/render/lod/`
- `tests/cuda/lod/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Build coarse grids on workers in `run/src/render/lod/index.c`
- [ ] Trace clipmaps beyond the full-detail radius in `cuda/src/lod/index.cu`
- [ ] Add a setting, a recorded cost at R=512 and a golden image; register test `render_feature_lod`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^render_feature_lod$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
