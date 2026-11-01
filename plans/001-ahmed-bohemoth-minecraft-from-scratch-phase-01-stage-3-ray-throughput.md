# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 3: Ray throughput spike

**Status:** done   (updated 2026-11-01)

## Goal

We know the headless cost of bitmask DDA on a synthetic R=32 world at 1080p. Pass: primary plus sun rays take at most 8.33 ms, or a fallback is recorded.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/ray_throughput/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Build a synthetic R=32 world in the plan's three-level `uint64_t` bitmask format, in `/tmp/mc_c_spikes/ray_throughput/`
- [x] Write a CUDA kernel casting primary and sun rays at 1920x1080
- [x] Measure frame time over several camera paths, and record the results in this file
- [x] If it misses 8.33 ms, record the measured numbers and the fallback for Ahmed to decide (it did not miss on terrain, so no fallback is chosen; the render-scale numbers and the stress-world misses are recorded below anyway)

## Results

**Verdict: pass.** On the terrain world, primary plus sun rays at 1920x1080 cost at most 4.15 ms at p99 and 4.47 ms for the slowest single frame, across all seven camera paths and both sun angles. That is about half of the 8.33 ms target. The fallback (render scale with TAA upsampling) is not needed for this budget and is not chosen. It stays available for later, because this spike covers only part of a real frame: see "What these numbers leave out".

Conditions: RTX 2080 Super, compute only, under Ahmed's live X desktop (274 MiB VRAM used by the desktop, 1-2 % utilization when idle). During the run nvidia-smi showed P2, 1905-1920 MHz core, 6801 MHz memory, 99 % utilization, about 140 W, 62-68 C. Times are host `CLOCK_MONOTONIC` around the launch plus `cudaDeviceSynchronize`, so launch overhead is included (an empty kernel costs 0.005 ms). There were 200 frames per path after 20 warm-up frames (100 at the lower resolutions). Other agents' builds were running on the CPU; `nvidia-smi --query-compute-apps` listed no other compute process when checked. Nothing else was pinned or isolated.

### Frame time at 1920x1080, primary + sun rays, terrain world

Sun at 50 degrees:

| Path | Mean ms | p50 | p95 | p99 | Max | About Mrays/s |
|---|---|---|---|---|---|---|
| walk_horizon | 0.87 | 0.85 | 1.42 | 1.80 | 1.92 | 3188 |
| flyover_high | 2.26 | 2.21 | 2.66 | 3.21 | 3.46 | 1228 |
| ridge_grazing | 1.73 | 1.84 | 2.45 | 2.58 | 2.94 | 1616 |
| spin_360 | 0.75 | 0.74 | 0.88 | 0.89 | 0.90 | 4568 |
| forest_floor | 0.70 | 0.71 | 0.92 | 1.11 | 1.20 | 4403 |
| canopy_skim | 1.73 | 1.91 | 2.23 | 2.25 | 2.26 | 1837 |
| cave_interior | 1.10 | 1.15 | 1.57 | 1.62 | 1.62 | 3720 |

Mrays/s is the mid-path ray count divided by the path's mean time, so it is approximate.

Sun at 15 degrees (long shadow rays), the worst case on terrain:

| Path | Mean ms | p50 | p95 | p99 | Max |
|---|---|---|---|---|---|
| walk_horizon | 1.04 | 0.93 | 1.98 | 2.92 | 3.01 |
| flyover_high | 2.37 | 2.32 | 2.84 | 2.90 | 2.91 |
| ridge_grazing | 2.36 | 2.31 | 3.85 | 4.13 | 4.15 |
| spin_360 | 0.85 | 0.82 | 1.06 | 1.08 | 1.10 |
| forest_floor | 0.80 | 0.86 | 1.04 | 1.05 | 1.06 |
| canopy_skim | 2.60 | 2.69 | 4.04 | 4.15 | 4.47 |
| cave_interior | 1.44 | 1.40 | 1.99 | 2.08 | 2.09 |

Primary rays alone: mean 0.52 to 1.06 ms, worst p99 1.21 ms.

### Ray statistics at mid-path, sun 50 degrees

| Path | Sun rays | Pixels hitting a block | Steps per primary | Steps per sun ray | Max primary | Max sun |
|---|---|---|---|---|---|---|
| walk_horizon | 709418 | 100.0 % | 13.6 | 10.6 | 81 | 56 |
| flyover_high | 698507 | 75.1 % | 29.8 | 12.4 | 169 | 155 |
| ridge_grazing | 727049 | 40.4 % | 35.0 | 13.7 | 190 | 121 |
| spin_360 | 1355274 | 98.3 % | 12.1 | 8.5 | 124 | 41 |
| forest_floor | 1029117 | 99.9 % | 8.6 | 6.3 | 165 | 45 |
| canopy_skim | 1105836 | 66.4 % | 28.3 | 13.4 | 243 | 66 |
| cave_interior | 2034982 | 100.0 % | 2.8 | 9.9 | 9 | 21 |

Each frame has 2073600 primary rays. A sun ray is cast only when the hit face points towards the sun.

### Render scale, for the record (mean ms / worst p99 over the seven paths, sun 50 degrees)

| Internal size | Mean range | Worst p99 |
|---|---|---|
| 1920x1080 | 0.70 - 2.26 | 3.21 |
| 1440x810 | 0.42 - 1.48 | 1.75 |
| 1280x720 | 0.34 - 1.24 | 1.52 |
| 960x540 | 0.22 - 0.84 | 1.41 |

### Stress world, where the target does break

`--lattice` replaces the terrain with single voxels every 9 blocks, so empty sections and bricks barely help: 51-147 steps per primary ray. No natural terrain looks like this, but a player could build it.

| Sun 50 degrees | Mean ms | p99 |
|---|---|---|
| forest_floor (the only pass) | 5.10 | 7.55 |
| walk_horizon | 9.48 | 12.07 |
| ridge_grazing | 10.30 | 12.84 |
| canopy_skim | 11.87 | 19.99 |
| spin_360 | 12.28 | 17.73 |
| flyover_high | 13.30 | 16.57 |
| cave_interior | 24.77 | 34.14 |

With the sun at 15 degrees the worst path is cave_interior at 35.46 ms mean, 55.56 ms p99. Primary rays alone stay at 3.28 to 8.26 ms mean. Throughput there is about 14 to 18 billion traversal steps per second (about 31 billion on flyover_high on terrain, where many steps skip whole nodes). Use the lower figure when estimating a future ray budget.

### Is the picture right?

Fast but wrong would mean nothing, so the kernel is checked against an exact voxel-by-voxel Amanatides-Woo trace in float64 on the dense grid, in C on the CPU. This covers 1/16 of the pixels of 3 poses per path, 2721600 rays per world, comparing hit or miss, hit voxel, hit face and shadow result:

| World | Hit or miss differs | Voxel differs | Face differs | Shadow differs |
|---|---|---|---|---|
| Terrain | 0 | 9 | 1 | 3 of 1133548 |
| Lattice | 10 | 8 | 8 | 2 of 503535 |

That is about 1 ray in 100000 to 200000, and the differences are consistent with float32 rounding on voxel corners, though I did not inspect each one. No terrain ray reached the step cap in the mid-path statistics pass (maximum 243 steps). I also looked at the pictures in `/tmp/mc_c_spikes/ray_throughput/out/`: hills, water, trees and sun shadows all look right. The 1/16 sample does not cover the mid-path poses used for those statistics.

### What was built

`/tmp/mc_c_spikes/ray_throughput/`: `CMakeLists.txt`, `shared.h`, `main.c` (C23, built with the standard's warning flags plus `-Wno-format`, see issue 1), `kernel.cu` (`sm_75`). Throwaway, as the plan says.

- **World:** 65x65 columns (1040x1040 blocks), 48 section slots per column, deterministic own noise: rolling hills, mountains up to z = 193, a sea, caves, ore blobs, porous-leaved trees. Built in 3 to 16 s depending on what else was running.
- **Format, as encoded:** column table (16 bytes: 48-bit section mask, first section, max height); section (16 bytes: 64-bit brick mask, first brick, or one uniform value); brick (64-bit voxel mask, plus a 4-byte reference); 4 bytes per occupied voxel (material and light) in mixed bricks only. Children are found with popcount.
- **Traversal:** one loop stepping through the largest empty node containing the current cell (column above its max height, empty section, empty brick, empty voxel), and re-reading masks only when the cell has left the cached column, section or brick.
- **Thread block shape:** 8x8 threads per block mapped to an 8x8 pixel tile is the best or tied for best of those tried. Flyover mean: 4x4 3.97 ms, 8x8 2.26 ms, 16x16 2.51 ms, 32x2 2.78 ms, 32x8 2.81 ms, 64x1 2.92 ms.

### What these numbers leave out

Do not read 4 ms as "the frame costs 4 ms". The spike has none of these: textures and mips (each hit reads only its voxel value and a small colour switch), non-cube shapes, water and glass continuing the ray (water is opaque here), AO, TAA, sky and fog, entities, the coarse depth pass, LOD, readback and copy (Stage 01.4), and world uploads running at the same time. The stored light is a stand-in. The coarse pass was not built because the target was already met. The world is static, and rays stop at its edge, so paths near the centre see about R=32 but those further out see less. The measurement ran under a nearly idle desktop; a busy compositor would take GPU time. Each path was measured for seconds, not minutes, so sustained thermal throttling is not covered.

So the budget left for everything else, on the same 8.33 ms, is about 4.2 ms on the worst terrain frame, and Stage 01.4 may take up to 2 ms of it. **If later effects overrun, render scale is the first lever:** 1440x810 cuts this part by about a third (worst p99 3.21 to 1.75 ms).

### For Stage 08.3 (traverse) and Phase 09

The three bugs under "Issues found" are the things to carry over. Without those three fixes the numbers look almost the same but the picture is wrong or a ray can loop until the cap. Also carry over: uniform-brick storage and the 8x8 block shape.

**Memory:** 72.5 MiB of world on the device at R=32 (columns 0.06, sections 1.31, brick masks 24.95, brick references 12.47, voxels 33.74). There are 85821 sections, about 20 per column, of which 27974 are uniform; 3270015 bricks, of which 3064562 are uniform; 8844778 stored voxels. Scaled by area this is about 1.1 GiB at R=128.

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    cmake --build /tmp/mc_c_spikes/ray_throughput/build
    timeout 120 /tmp/mc_c_spikes/ray_throughput/build/ray_throughput
    pgrep -a ray_throughput    # must print nothing

### Verification run

    cmake --build /tmp/mc_c_spikes/ray_throughput/build    # Built target ray_throughput
    timeout 120 /tmp/mc_c_spikes/ray_throughput/build/ray_throughput    # rc=0, total run 23.7 s; output kept in out/final_run.txt
    pgrep -a ray_throughput    # printed nothing, rc=1

## Commits

Appended as they land:

## Issues found

### 1. `float64_t` cannot be passed to `printf` under the standard's flags

- **Expected:** `plans/CODING_STANDARD.md` section 2 says `_Float64` works at libc boundaries, and section 9 sets `-Wall -Werror`.
- **Actual:** GCC 16.2 rejects it: `main.c:706:51: error: format '%f' expects argument of type 'double', but argument 7 has type 'float64_t' {aka '_Float64'} [-Werror=format=]`. `_Float64` is a distinct type to GCC, and casting to `double` is banned by the same standard.
- **Done:** worked around in the spike only, with `-Wno-format`. **Left open:** `CODING_STANDARD.md` is not owned by this stage. Stage 03.1 (`log`) needs a rule here: a single allowlisted conversion, or no floats through varargs.

### 2. Plane intersection lost precision, 23 % of pixels on one path had the wrong voxel

- **Expected:** the common GPU form `plane * inverse - origin * inverse`, which saves one subtraction, is accurate enough at world scale.
- **Actual:** the float64 reference reported 89787 wrong voxels of 388800 on `ridge_grazing` (79881 of them a neighbouring cell). For a ray with a nearly flat axis the inverse is large, so both products are huge and the difference loses up to tens of blocks of `t`.
- **Done:** fixed by using `(plane - origin) * inverse`. The wrong-voxel count on that path fell to 0-3 and the frame time did not measurably change. Stage 08.3 must use this form.

### 3. A near-tie of two planes could make a ray bounce between two cells until the step cap

- **Expected:** each step moves the ray forward.
- **Actual:** 2 pixels on `flyover_high` hit the 4096-step cap and were reported as misses. A host build of the same code showed the cycle at pixel (1501, 424): cells (659 515 391) and (659 516 390) alternating, `ty` = 143.568863 and `tz` = 143.568848. The plane reached second lay slightly behind the first, recomputing the other axis at that earlier `t` moved `y` back, and clamping to the node allowed it because the coarser node contained both cells.
- **Done:** fixed by never letting a coordinate move against the ray: the clamp's bound on the near side is the current cell, not the node's edge. This also bounds a ray at width + width + height steps. No ray reached the cap in the statistics passes afterwards (maximum 243 steps on terrain, 532 on the lattice).

### 4. The sun ray started from the wrong cell

- **Expected:** "the cell before the last step" is the air cell next to the hit voxel, as it is in a unit-step DDA.
- **Actual:** after fix 3, the shadow results disagreed with the reference on up to 847 of 143673 rays (`canopy_skim`). In a hierarchical DDA, the last step can leave a big empty node, so that cell can be far from the hit point. Fix 3 made this visible, because the start cell now bounds the ray.
- **Done:** fixed by starting from the hit voxel plus the face normal. Shadow disagreements fell to 3 of 1133548.

### 5. The reference check itself was wrong for a camera above the world box

- **Expected:** a mismatch means a kernel bug.
- **Actual:** the first lattice run reported about 150000 hit-or-miss differences per path with 0 voxel differences. The camera there is above the box top (z index 338.7, top 334). The kernel clips to the box and the reference stopped immediately.
- **Done:** the reference now does the same box clip. Differences fell to 0-3 per path. The kernel did not change, and the numbers above come from after this fix.

### 6. Plan estimate of VRAM has the right total for a different reason

- **Expected:** plan section 2 estimates about 100 MiB at R=32, assuming 6 sections per column at 4 KiB.
- **Actual:** 72.5 MiB, but with about 20 occupied sections per column, because the solid ground goes down to z = -256. The total holds only because 94 % of bricks are uniform and store no voxels. A rough estimate if those bricks stored every voxel: 3064562 x 64 x 4 bytes, about 750 MiB (assuming they are nearly full).
- **Done:** recorded here, since the master plan is not edited. **Stage 08.2 needs uniform bricks** (or an equivalent) for the estimate to hold, because the plan's format description does not mention them.
