# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1: Feasibility spikes

**Status:** not started   (updated 2026-11-01)

## Goal

The risks that could overturn the plan have been measured, and every result is recorded in these stage files. Spike code was throwaway under `/tmp/`; only the results stay.

**Wave:** W1.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-1-toolchain-link.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-2-kms-flip.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-3-ray-throughput.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-4-copy-to-scanout.md`

## Todo

- [ ] Stage 01.1: Toolchain link spike
- [ ] Stage 01.2: KMS flip spike
- [ ] Stage 01.3: Ray throughput spike
- [ ] Stage 01.4: Copy to scanout spike
- [ ] Phase gate passed

## Verification

**Gate:** Stages 01.1 to 01.4 have recorded results. Failure routes, each decided by Ahmed: 01.2 fails means X11 raw protocol is the only known alternative; 01.3 misses means internal render scale with TAA upsampling, or a lower FPS target.

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    ls /tmp/mc_c_spikes/    # spike code stays here, out of the repo
    pgrep -a 'spike|kms_flip|ray_throughput|copy_scanout'    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
