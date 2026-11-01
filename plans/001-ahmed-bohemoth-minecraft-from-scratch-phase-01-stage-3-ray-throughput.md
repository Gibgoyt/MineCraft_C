# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 3: Ray throughput spike

**Status:** not started   (updated 2026-11-01)

## Goal

We know the headless cost of bitmask DDA on a synthetic R=32 world at 1080p. Pass: primary plus sun rays take at most 8.33 ms, or a fallback is recorded.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/ray_throughput/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Build a synthetic R=32 world in the plan's three-level `uint64_t` bitmask format, in `/tmp/mc_c_spikes/ray_throughput/`
- [ ] Write a CUDA kernel casting primary and sun rays at 1920x1080
- [ ] Measure frame time over several camera paths, and record the results in this file
- [ ] If it misses 8.33 ms, record the measured numbers and the fallback for Ahmed to decide

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    cmake --build /tmp/mc_c_spikes/ray_throughput/build
    timeout 120 /tmp/mc_c_spikes/ray_throughput/build/ray_throughput
    pgrep -a ray_throughput    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
