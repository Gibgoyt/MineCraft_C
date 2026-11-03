# 001-ahmed-bohemoth-minecraft-from-scratch — phase 17: Performance hardening

**Status:** done   (updated 2026-11-03)

## Goal

The game is profiled and tuned, and the plan's target table is filled in with measured numbers, with misses stated plainly.

**Wave:** W10.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-1-profiler.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-2-benchmark-worlds.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-stage-3-cost-pass.md`

## Todo

- [x] Stage 17.1: Profiler
- [x] Stage 17.2: Benchmark worlds
- [x] Stage 17.3: Top-cost pass
- [x] Phase gate passed: the table is filled in with measured numbers, and its misses are stated plainly below. Checked again by the gate check, which corrected the frame rate row

## Verification

**Gate:** Section 2 target table filled in with measured numbers, misses stated plainly.

    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world
    pgrep -a minecraft_c    # must print nothing

## The plan's section 2 targets, as measured

2026-11-03, on this machine, at 1920 by 1080, with the desktop on the same card, under the build lock, with no display: real KMS is Ahmed's to run. The evidence and every before and after are in the stage 3 file, and the gate check's own runs are in its issue 6. **These are misses: 120 fps with all effects on, on `deep_ocean/orbit` by 11% and `jungle/orbit` by 8%; 120 fps with god rays off on `deep_ocean/orbit` by 1 to 3%, and on the game's own threads on `jungle/orbit` too; and the sim tick and terrain once a hot CPU is held down.**

A settled row's fps is what the card's clock allows. It leaves out `past the card`, which is the CPU's, and that is why `--live` is lower. A live row's is the pictures the game's own threads drew, against the clock.

| Metric | Target | Measured | |
|---|---|---|---|
| Frame rate at 1080p, render distance 32, all effects | 120 FPS (8.33 ms) | **All effects, god rays on with `--god-rays`:** 9 of the 11 views. **`deep_ocean/orbit` 107.4 fps, 9.31 ms; `jungle/orbit` 110.2, 9.07**; next `jungle/flight-live` 125.4. **God rays off, as the game has them by default:** 11 of the 12 rows. `deep_ocean/orbit` 115.8 to 117.0, next `jungle/orbit` 122.5 to 123.5. **The same orbits on the game's own threads, god rays off:** `deep_ocean/orbit` 109.1, `jungle/orbit` 112.2, `jagged_peaks/orbit` 173.3. Before this phase's pass, god rays off: 5 of 12, slowest 34 to 37 | **MISSED**: by 11% at worst, and by 1 to 3% with god rays off |
| Render distance setting | 2 to 512 chunks | 2 runs. 128: 148.8 to 152.2 fps live. 512: 107.2 to 109.6 fps | met |
| Terrain | ≥ 1000 columns/s on 16 workers | Slowest place, `jungle`: **1080 and 1094** from a cool CPU, release preset. **707 and 992** straight after other runs, profile preset | met cool, **MISSED hot** |
| Sim tick at 100 mobs | ≤ 5 ms of the 50 ms budget | Whole game, p99: **3.0 to 4.0 ms** in 8 runs of 9 in a row, and **9.0** in the 9th, with the CPU held down; 8.9 and 9.8 in full runs. Mobs alone, `bench_mobs`: 1.72 to 1.99 at worst from cool, and **7.31** straight after other runs | met cool, **MISSED held down** |
| Allocations during play | 0 calls to the OS heap in hot paths | 0 in all 14 rows | met |
| VRAM | ≤ 6 GiB voxels, 256 MiB textures, 256 MiB frame buffers | 2164 MiB voxels at most, 2 MiB textures, 220 MiB frames | met |

## Commits

Appended as they land:

## Issues found

See each stage's. Two reach past their stage: **the machine's own CPU heat decides whether two of these targets are met**, Stage 17.3 issue 1; and **this table first said 11 of 12 rows met "all effects", with no god rays on 11 of them**, Stage 17.3 issue 6.
