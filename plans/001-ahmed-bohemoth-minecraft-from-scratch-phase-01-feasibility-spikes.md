# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1: Feasibility spikes

**Status:** in progress: Stages 01.1 and 01.3 are done. Stage 01.2 and the dumb-buffer half of 01.4 await Ahmed's runs on a text console, so the gate stays open. All of this phase's records are on the integration branch `worktree-wf_a4b549de-085-20`   (updated 2026-11-02)

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

Second gate check result, 2026-11-02: the same two commands again behave as stated, every agent-runnable check below passes from clean, and **the gate still does not pass**, for the same two reasons.

| Stage | Second check | Box | Waiting for |
|---|---|---|---|
| 01.1 | Clean rebuild and all three matrices pass | Ticked | Landing |
| 01.2 | Strict build; 14 fake-card cases pass; source copy unchanged | Open | Ahmed's run |
| 01.3 | Rebuilt to the 01.1 rules; `VERDICT: PASS`, 4.10 ms; 12 of 12 disagreements explained by rounding; negative controls fail as they should | Ticked | Landing |
| 01.4 | Readback 1.263 ms; stand-in total 1.80 ms; 10 fake cases asserted; rebuild from the stage file works | Open | Ahmed's run; a miss is now likelier, as the driver's source puts the buffer in VRAM |

## Commits

Appended as they land:

On the integration branch `worktree-wf_a4b549de-085-20`; Ahmed fast-forwards `main` at the end.

- `59a5397` First gate check
- `9cb0362` Second gate check
- `92ec4a8` Statuses and notes for the integration branch process

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

**4. Second gate check: defects it was handed, and what became of each.**

| Defect | Outcome |
|---|---|
| Phase 01 not on `main` | **Open.** The landing command was refused a third time from this checker's worktree, see below |
| 01.2 nothing known about real hardware | **Open, narrowed.** Only Ahmed's run can answer it. The driver source was read, Stage 01.2's last issue |
| 01.4 real write unmeasured, target unjudged | **Open, narrowed.** Same. Source says VRAM, so the predicted miss is likelier, Stage 01.4 issue 9 |
| 01.4 outlier, cause unknown | **Open, narrowed.** Not recurred in 24 runs. Memory contention and throttle events ruled out by experiment; a low core clock fits best; why the clock would be low under load is unproven, Stage 01.4 issue 8 |
| 01.3 `-Wno-format` and g++ link | **Fixed**, Stage 01.3 issue 10 |
| 01.3 chosen limit, `--no-validate` | **Fixed**, Stage 01.3 issue 11 |

New defects it found and fixed: `build.sh` in 01.4 failed on the documented rebuild, and `test_shim/run.sh` in 01.4 asserted nothing (Stage 01.4 issue 10). New finding for later stages: sleeping before the copy costs 0.8 to 1.2 ms in the write, measured at about 900 to 1200 MHz (Stage 01.4 Results).

**Landing, third round.** Expected: step 5's command lands. Actual: `flock /tmp/mc_c_land.lock sh -c 'git rebase main && git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only '"$(git rev-parse --abbrev-ref HEAD)"` was refused: "this command names git in a form too complex to verify that it stays inside the worktree". Done: the attempt was made once, and no other route to move `main` was tried, for the reason in issue 1. **A checker told to fix this cannot, from inside the isolation. Either the workflow lands these commits from outside, or its agents run without worktree isolation.** Later phases need that, as they read `plans/CODING_STANDARD.md`.

**Limit on the sources in `/tmp`.** Only 01.2 and 01.4 keep their source in the repo. The Verification commands for 01.1 and 01.3 need `/tmp/mc_c_spikes/`, which is tmpfs, so they cannot be rerun after a reboot. Their results stay, as this phase intends. The `outlier/` experiments and `test_shim/` for 01.2 and 01.4 are in the same position.

**5. 2026-11-02: the landing process changed, so the "not on `main`" text above is history.**
- Expected: this stage to land on `main` through the workflow's landing command (issues 1 and 4, and "Landing, third round").
- Actual: every landing from a worktree was refused, and `main` is still `5bb5c57`. The refusals are recorded above as they happened, and are left as written. The Commits section used to say the work was unlanded, on branch `worktree-wf_a4b549de-085-10`.
- Done: the process changed. Agent branches now merge into `worktree-wf_a4b549de-085-20`, which stands in for `main`, and Ahmed fast-forwards `main` at the end. The landing commands written above are no longer anyone's to run. The Commits section now lists the shas on the integration branch.
- The "Landing" entries in the gate check tables above were true on their dates. For 01.1 and 01.3 nothing now waits on an agent. What still blocks this gate is unchanged: Ahmed's runs for 01.2 and 01.4, whose commands are in those stage files. Neither failure route has been triggered or ruled out.
- Issue 2's spike sources stay in the 01.2 and 01.4 stage files until his results are recorded.
