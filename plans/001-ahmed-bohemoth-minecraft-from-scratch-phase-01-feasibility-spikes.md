# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1: Feasibility spikes

**Status:** in progress: Stages 01.1 and 01.3 are verified. Stage 01.2 and the dumb-buffer half of 01.4 await Ahmed's runs on a text console. None of this phase is on `main` yet, because agents cannot land it (see Issues)   (updated 2026-11-01)

## Goal

The risks that could overturn the plan have been measured, and every result is recorded in these stage files. Spike code was throwaway under `/tmp/`; only the results stay.

**Wave:** W1.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-1-toolchain-link.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-2-kms-flip.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-3-ray-throughput.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-4-copy-to-scanout.md`

## Todo

- [x] Stage 01.1: Toolchain link spike
- [ ] Stage 01.2: KMS flip spike
- [x] Stage 01.3: Ray throughput spike
- [ ] Stage 01.4: Copy to scanout spike
- [ ] Phase gate passed

## Verification

**Gate:** Stages 01.1 to 01.4 have recorded results. Failure routes, each decided by Ahmed: 01.2 fails means X11 raw protocol is the only known alternative; 01.3 misses means internal render scale with TAA upsampling, or a lower FPS target.

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    ls /tmp/mc_c_spikes/    # spike code stays here, out of the repo
    pgrep -a 'spike|kms_flip|ray_throughput|copy_scanout'    # must print nothing

Gate check result, 2026-11-01: both commands behave as stated (four directories listed, `pgrep` rc 1). **The gate does not pass:** 01.2 has no hardware result and 01.4 has no scanout write time, and only Ahmed can run those. Neither failure route has been triggered, and neither has been ruled out.

| Stage | Agent-runnable checks | Box | Waiting for |
|---|---|---|---|
| 01.1 | Pass, clean rebuild and all three matrices | Ticked | Landing |
| 01.2 | Strict build passes; 14 fake-card cases pass | Open | Ahmed's run |
| 01.3 | Pass, worst p99 4.24 ms against 8.33 ms | Ticked | Landing |
| 01.4 | Readback 1.26 ms; stand-in total about 1.8 ms; shim cases pass | Open | Ahmed's run; a miss is predicted if the buffer is in VRAM |

## Commits

Appended as they land:

Nothing from this stage is on `main`, so there is no landed sha yet. The commits are on branch `worktree-wf_a4b549de-085-9`, which holds all four Phase 01 stages rebased on `5bb5c57`, and the gate check. Landing needs `git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only worktree-wf_a4b549de-085-9` from outside the agent isolation; see the phase file. Record the landed shas here after that.

## Issues found

**1. Agents cannot land anything from a worktree, so `main` holds none of this phase.**
- Expected: all four stages landed; the gate checker checks them on `main`.
- Actual: `main` is still `5bb5c57`. Each stage's work sat only on its own branch (`worktree-wf_a4b549de-085-1` to `-8`). The landing command is refused for the gate checker too: `flock /tmp/mc_c_land.lock sh -c 'git rebase main && git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only ...'` gives "this command names git in a form too complex to verify that it stays inside the worktree". So does a read-only `git -C` on the main checkout.
- Done: left alone, because that refusal is a deliberate limit on this agent, and `update-ref` or a push would bypass it. The latest commits of branches `-4`, `-5`, `-6` and `-8` were cherry-picked in that order onto branch `worktree-wf_a4b549de-085-9`, with no conflicts, as they touch separate files. Branches `-1`, `-2`, `-3` and `-7` are earlier attempts at the same work. To land: `git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only worktree-wf_a4b549de-085-9` from outside the isolation. Until then, later phases do not see the updated `plans/CODING_STANDARD.md`, whose Stage 01.1 measurements they need.

**2. Spike sources are in the repo for two stages, against this file's "only the results stay".**
- Expected: spike code stays in `/tmp`.
- Actual: 01.2 and 01.4 keep sources in their stage files, because Ahmed's runs have to survive `/tmp` being cleared.
- Done: left in place. Delete those blocks once his results are recorded.

**3. Defects found by the gate check are logged in the stage files.** In short: 01.2 had never executed and had a false FAIL on a slow modeset; 01.3's exit code asserted nothing and its Verification could not build from clean; 01.4's rerun source was missing and its stated ranges were too narrow; 01.1 had only a banned cast. All are fixed, except that the 01.4 outlier remains unexplained.
