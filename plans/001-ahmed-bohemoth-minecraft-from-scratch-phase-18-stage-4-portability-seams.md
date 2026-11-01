# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18, stage 4: Portability seams

**Status:** not started   (updated 2026-11-01)

## Goal

Portability seams are documented for later platforms, and a CPU-only render backend is recorded as future work.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-release-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `plans/REPO_STRUCTURE.md` portability section

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Document each platform seam: `config/machine.yaml`, `run/src/display`, `run/src/input`, `run/src/gpu_bridge`
- [ ] Record the CPU-only render backend as future work

## Verification

    grep -n 'CPU-only' plans/REPO_STRUCTURE.md

## Commits

Appended as they land:

## Issues found

None.
