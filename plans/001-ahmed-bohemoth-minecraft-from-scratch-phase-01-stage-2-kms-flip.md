# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 2: KMS flip spike

**Status:** not started   (updated 2026-11-01)

## Goal

We know whether dumb buffers, modeset and page-flip events work on `nvidia-drm`, with X on another VT and, if that fails, with X stopped by Ahmed. Pass: solid-color flips at 120 Hz under `timeout 10`, with the console restored.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/kms_flip/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Write a raw-ioctl spike in `/tmp/mc_c_spikes/kms_flip/` using only kernel UAPI headers: open `/dev/dri/card1`, pick a connector and mode, create dumb buffers, modeset, and flip solid colors
- [ ] Measure the flip interval from page-flip events, and restore the console on exit
- [ ] Give Ahmed the exact command to run with X on another VT
- [ ] If that fails, ask Ahmed to stop X and run it again; never stop X yourself
- [ ] Record results, errors and flip timings in this file

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 10 /tmp/mc_c_spikes/kms_flip/kms_flip
    pgrep -a kms_flip    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
