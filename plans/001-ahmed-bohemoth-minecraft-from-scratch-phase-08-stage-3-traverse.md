# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 3: Ray traversal

**Status:** not started   (updated 2026-11-01)

## Goal

The coarse pass and primary rays exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-cuda-renderer-core.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/traverse/`
- `tests/cuda/traverse/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Implement the low-resolution conservative coarse depth pass in `cuda/src/traverse/index.cu`
- [ ] Implement primary rays with hierarchical DDA through the bitmask tree
- [ ] Test known hits against a CPU reference, registered as `cuda_traverse`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^cuda_traverse$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
