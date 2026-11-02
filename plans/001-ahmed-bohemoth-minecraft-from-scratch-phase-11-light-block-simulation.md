# 001-ahmed-bohemoth-minecraft-from-scratch — phase 11: Light and block simulation

**Status:** in progress: every check an agent can run passes, and what is left is Ahmed's look at lit terrain   (updated 2026-11-02)

## Goal

Sky and block light update correctly on edits, and fluids, falling blocks and random ticks work.

**Wave:** W7.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-stage-1-light.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-11-stage-2-block-simulation.md`

## Todo

- [ ] Stage 11.1: Light. Verified but for Ahmed's look at the pictures: its issue 5
- [x] Stage 11.2: Block simulation
- [x] Phase gate passed

## Verification

**Gate:** Incremental light matches full recompute on random edits.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_(light|sim)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

Stage 11.1: `11d03f8`, `bacfaed`. Stage 11.2: `7092cf2`. The gate check:

- `1622c2f` Test light against the running simulation, pin the simulation's thresholds, and fix a format breach the merges left in main.
- `9cf0102` Pin how far fluids look for a way down, the edge of a sea, the limit on fires and stale wakes with tests that fail when each is changed.
- `81783f8` Have each opaque block hold what its faces see, as the shader was promised, so that the world is not black, and take the whole log before any light pass.
- `6f0f052` Look only at the outer layers of opaque sections in a new column, and make the frame test's pictures again with light and the water stage both in.

## Gate check, 2026-11-02

From a clean build with the integration branch merged: `run_world_light` 24 tests and `run_world_sim` 45 tests pass on `debug` and `sanitize`. All 71 ctests pass on `debug`, in 621 s. `standards_lint` and `clang_format` pass. The incremental light matches a separate full recompute on random edits, on the seeds in the test and on others, and matches a rebuild after the simulation has run on real terrain. No process of this worktree was left, by `pgrep -af "$PWD/build"`.

## Issues found

1. **The gate passed and the phase was not done: the world it lit was black.** The gate asks that incremental light match a full recompute, and both matched, with the same wrong rule: solid voxels at 0, where Stage 09.2 had asked for what their faces see. Only `run_render_frame`'s golden pictures showed it. Fixed: Stage 11.1 issue 6, with its evidence. **The gate is narrower than its goal:** nothing in this phase's Verification runs `run_render_frame`, so the gate would still pass with this bug back in, though `an_opaque_block_holds_what_its_faces_see_and_passes_none_of_it_on` now fails if it is.
2. **Tests that held nothing to the README's numbers.** Stage 11.2 issue 8. No bug behind them.
3. **Left open, for Ahmed to weigh:** generated jungle and dark oak trees lose about 0.4% of their leaves, Stage 11.2 issue 10; and grass under some things turns to dirt, not traced, Stage 11.2 issue 11.
