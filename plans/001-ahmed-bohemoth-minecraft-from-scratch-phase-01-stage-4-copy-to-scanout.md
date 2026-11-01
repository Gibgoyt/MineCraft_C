# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 4: Copy to scanout spike

**Status:** not started   (updated 2026-11-01)

## Goal

We know the cost of readback plus the dumb-buffer write. Pass: total of at most 2 ms.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/copy_scanout/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Time a 1080p readback from device memory to pinned host memory in `/tmp/mc_c_spikes/copy_scanout/`
- [ ] Time the copy from host memory into a mapped dumb buffer
- [ ] Record both times and the total in this file

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    # Readback timing runs headless. The dumb-buffer part: Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 30 /tmp/mc_c_spikes/copy_scanout/copy_scanout
    pgrep -a copy_scanout    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
