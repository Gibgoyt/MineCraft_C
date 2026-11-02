# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 3: Ray traversal

**Status:** done, with the tree format still to be agreed with Stage 08.2: issue 1   (updated 2026-11-02)

## Goal

The coarse pass and primary rays exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-cuda-renderer-core.md`.

**Owned paths** (edit only these, plus this file):

- `cuda/{src,include}/traverse/`
- `tests/cuda/traverse/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement the low-resolution conservative coarse depth pass in `cuda/src/traverse/index.cu`
- [x] Implement primary rays with hierarchical DDA through the bitmask tree
- [x] Test known hits against a CPU reference, registered as `cuda_traverse`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each. One exception, the same as Stage 08.1 issue 3: `run/src/gpu_bridge/traverse/` holds a README and no `index.*`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^cuda_traverse$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Run on 2026-11-02 on the RTX 2080 Super, compute only, with X up: the build is clean, `cuda_traverse` passes 21 tests and 1485 checks in about 21 s, `standards_lint` and `clang_format` pass, and `pgrep -a minecraft_c` prints nothing. `cuda_traverse` also passes on the `release` and `sanitize` presets. The whole debug suite is 49 of 50: `run_types` fails, and not because of this stage: issue 5.

## Results

**Terrain needs no fallback.** Coarse plus primary at 1920x1080 is at most 1.05 ms at p99, against 1.83 ms for primary alone in the same run.

**The lattice misses 8.33 ms on traversal alone, on one path:** `cave_interior`, p99 8.76 and 9.45 ms in two runs. The other six paths pass. So Stage 01.3's fallback applies to that world only, and it is ready: issue 3.

Release preset, 200 frames per path after 20 warm-up frames, host clock around the launches and `cudaDeviceSynchronize`. The worlds, paths and field of view are Stage 01.3's, at R = 32. Other agents' builds may have been running on the CPU, and the card was not checked to be otherwise idle during the runs. First run, in milliseconds:

| Terrain | Primary alone mean | p99 | Coarse alone mean | Both mean | p99 | Max | Steps per ray, without | with |
|---|---|---|---|---|---|---|---|---|
| walk_horizon | 0.78 | 1.53 | 0.083 | 0.57 | 1.04 | 1.22 | 13.6 | 12.1 |
| flyover_high | 1.21 | 1.47 | 0.171 | 0.81 | 0.88 | 0.88 | 32.3 | 7.9 |
| ridge_grazing | 1.36 | 1.80 | 0.151 | 0.68 | 0.93 | 0.95 | 54.8 | 6.5 |
| spin_360 | 0.56 | 0.68 | 0.032 | 0.47 | 0.57 | 0.58 | 12.1 | 8.8 |
| forest_floor | 0.58 | 0.87 | 0.045 | 0.47 | 0.57 | 0.59 | 8.6 | 8.6 |
| canopy_skim | 1.17 | 1.46 | 0.207 | 0.76 | 0.92 | 0.93 | 33.0 | 8.0 |
| cave_interior | 0.68 | 0.99 | 0.079 | 0.40 | 0.53 | 0.54 | 2.8 | 2.4 |

| Lattice | Primary alone mean | p99 | Coarse alone mean | Both mean | p99 | Max | Steps per ray, without | with |
|---|---|---|---|---|---|---|---|---|
| walk_horizon | 6.21 | 7.89 | 0.269 | 5.56 | 6.50 | 7.08 | 102.2 | 67.8 |
| flyover_high | 6.61 | 8.00 | 0.396 | 6.28 | 7.67 | 8.12 | 107.3 | 77.7 |
| ridge_grazing | 5.64 | 7.79 | 0.303 | 5.15 | 7.14 | 7.16 | 80.3 | 54.7 |
| spin_360 | 5.99 | 6.45 | 0.325 | 5.44 | 5.96 | 6.35 | 105.1 | 70.4 |
| forest_floor | 4.00 | 5.23 | 0.196 | 3.50 | 4.58 | 4.59 | 70.2 | 42.7 |
| canopy_skim | 5.57 | 7.60 | 0.270 | 5.04 | 6.88 | 6.96 | 93.1 | 61.7 |
| cave_interior | 8.93 | 9.67 | 0.426 | 8.18 | 8.76 | 8.89 | 149.1 | 117.6 |

Coarse plus primary over the seven paths. Means are from the first run, and p99 from the first and second:

| Size | Terrain mean | Terrain worst p99 | Lattice mean | Lattice worst p99 |
|---|---|---|---|---|
| 1920x1080 | 0.40 - 0.81 | 1.04, 1.05 | 3.50 - 8.18 | 8.76, 9.45 |
| 1440x810 | 0.27 - 0.57 | 0.82, 0.85 | 2.24 - 5.10 | 5.49, 5.48 |
| 1280x720 | 0.23 - 0.51 | 0.67, 0.68 | 1.86 - 4.20 | 4.47, 4.53 |
| 960x540 | 0.15 - 0.39 | 0.62, 0.69 | 1.21 - 2.70 | 2.91, 3.45 |

The coarse pass more than pays for itself on mean time on all 14 rows, most on `ridge_grazing` on terrain (1.36 to 0.68 ms). These numbers leave out sun rays, which belong to Stage 09.2. In Stage 01.3 they took the lattice's `cave_interior` to about 25 ms, so that world will need far more than 1440x810 once they are on.

**Is the answer right?** Against an exact voxel-by-voxel trace in `float64_t` on the dense array, at 1280x720, every 4th pixel each way, 3 poses on each of 7 paths, 1209600 rays per world, with the coarse pass:

| World | Hit or miss differs | Voxel differs | Face differs | `t` differs | Value differs | Not explained by rounding | Hits before the coarse distance |
|---|---|---|---|---|---|---|---|
| Terrain | 0 | 2 | 0 | 0 | 0 | 0 | 0 |
| Lattice | 2 | 1 | 0 | 0 | 0 | 0 | 0 |

- Random rays through a world holding every kind of node: 350000 rays, no difference at all. A further 50000 exact diagonals differ on 5955, none unexplained: issue 4.
- Random cameras from 1x1 to 320x180, very narrow to very wide, every pixel: 1304500 rays, none hitting before its tile's distance, and 19369 of 21140 tiles start past 0.
- No ray reached the step cap in any of these.
- The checks can fail: a reference a hundredth of a block off leaves 22300 of 57600 rays unexplained on terrain and 10772 on the lattice, a forged coarse distance is caught, and changing a brick mask on the card changes the answer.

## Wiring outside the owned paths

- `run/include/gpu_bridge/traverse/index.h` and `run/src/gpu_bridge/traverse/README.md`: the boundary the standard requires for a CUDA area. The README is there for the same reason as Stage 08.1 issue 3.
- `run/src/gpu_bridge/README.md`: a line in its table of parts.
- `plans/REPO_STRUCTURE.md`: the part and the test.

Nothing in the game calls this yet. Stage 08.4's frame will.

## Commits

Appended as they land:

## Issues found

**1. Stage 08.2 has not landed, so the tree format is defined here.**
- Expected: `cuda/src/world_buffers/` and the encoder to exist, since the phase lists them first.
- Actual: `git log --all -- cuda/src/world_buffers run/src/render` printed nothing when this stage ran.
- Done: `run/include/gpu_bridge/traverse/index.h` defines the tree as Stage 01.3 measured it, and the test has its own encoder written from a dense array. One deliberate change from Stage 01.3: a column's `top` is one above its highest occupied height and 0 when empty, because `max_zi` cannot say empty. **Not tested against the real encoder, since there is none. Whichever of 08.2 and this stage merges second must make the two agree.** The only code that reads the layout is `traverse_trace` and `traverse_box_is_empty`.

**2. The coarse pass's first version was correct and did nothing on the ground.**
- Expected: a conservative march saves steps.
- Actual: the first version, a cube around a point on the centre ray, passed every correctness check and started no tile past 0 on five of the seven terrain paths, with identical step counts on them: an eye 2.7 above the ground has the ground inside any cube around it.
- Done: the box now bounds the swept stretch of the ray. That took `ridge_grazing` from 2974945 to 1886636 steps (sums over the test's frames), but three paths still started no tile past 0, because a brick holding the eye usually holds the floor too. So the short stretch now looks at voxels, which took `walk_horizon` from 2673431 to 2038341 and `ridge_grazing` to 1031808. A test now requires the pass to skip a share of tiles and to save steps on a flight, because a pass saying 0 everywhere is correct and worthless.

**3. The lattice misses 8.33 ms on one path, on traversal alone.**
- Expected from Stage 01.3: primary alone 3.28 to 8.26 ms mean, with the miss coming from sun rays.
- Actual: `cave_interior` with the coarse pass has mean 8.18 and 8.30 ms, and p99 8.76 and 9.45 ms. Primary alone is 8.93 and 9.03 ms mean, somewhat above Stage 01.3's 8.26. That difference was not traced. Stage 01.3 published no p99 for primary alone on this world.
- Done: Stage 01.3's fallback is a lower internal size, and `traverse_camera_t` carries that size. 1440x810 has a worst p99 of 5.49 ms, and a test checks 1920x1080, 1440x810, 1280x720 and 960x540 against the reference. Choosing a size at run time belongs to Stage 08.4 and upsampling to Stage 09.4, and neither exists yet, so nothing switches size by itself today. No natural terrain looks like this world, and Stage 01.3 left the decision to Ahmed, so it is escalated to him and not decided here.

**4. Exact diagonals skip a corner.**
- Expected: every ray agrees with the reference to within rounding.
- Actual: of 50000 rays along (±1, ±1, ±1) from whole-number corners, 5955 name another voxel or miss, 165 of them hit or miss. They pass exactly through voxel edges and corners. At such a tie the hierarchical step moves two axes at once, while the unit-step reference visits one neighbour first.
- Done: left alone, because such a voxel is touched only on an edge or corner. These rays are counted apart so that a real difference cannot hide among them, and the classifier explains all of them.

**5. `run_types` fails on the integration branch, and not from this stage.**
- Expected: `heap at exit: 6 reserved, 6 released, 0 bytes live`.
- Actual: `tests/run/types/index.c:914` and `:922` fail against `heap at exit: 7 reserved, 7 released, 0 bytes live` and the matching leak message. Merge `65a9bc4` kept both the terrain and `gpu_bridge` fields in `app_t`, and each stage had counted only itself, so the program now reserves 7. This stage reserves nothing on the heap.
- Done: left alone, as outside this stage's paths. The test's two numbers need to become 7. Escalated to the orchestrator in this stage's result.

**6. Two command forms were refused by the worktree isolation.**
- Expected: `tools/test-or-build.sh sh -c '...ctest...'`, and a Python script holding `git` in its text, to run.
- Actual: both refused, as text it could not show stayed inside the worktree.
- Done: every result here came from plain commands, and this file was written with the edit tools. Nothing was routed around the limit.
