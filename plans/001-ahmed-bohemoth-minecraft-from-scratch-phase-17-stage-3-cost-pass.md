# 001-ahmed-bohemoth-minecraft-from-scratch — phase 17, stage 3: Top-cost pass

**Status:** done   (updated 2026-11-03)

## Goal

A pass has been made over the top costs: memory layout, ray coherence, tile order, thread placement and upload budget.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-performance-hardening.md`.

**Owned paths** (edit only these, plus this file). The stage named these from the profile and the rows below, before editing:

- `cuda/src/shade/`, `cuda/include/shade/`, `run/include/gpu_bridge/shade/`: shade was the top cost of every slow row
- `cuda/include/traverse/`, `cuda/src/traverse/`, `run/include/gpu_bridge/traverse/`, `cuda/src/world_buffers/`, `run/include/gpu_bridge/world_buffers/`: what every ray of shade runs
- `bench/world/`: `--features-off`, `=<name>`, and the results

Wiring outside those, each as small as it could be: `run/src/render/upload/` and `run/src/render/frame/` pass the tops on, `run/src/game/index.c` sets `SHADE_FEATURE_SUN_APART`, `run/include/gpu_bridge/post/index.h` for one byte of `post_temporal_t`, `cuda/src/lod/index.cu` for one initializer, `tests/cuda/traverse/fixtures/world.h`, `tests/run/render/upload/`, `tests/run/render/frame/` and a printf's width in `tests/run/game/`.

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Rank costs from the profiler, and record them here
- [x] Work through memory layout, ray coherence, tile order, thread placement and upload budget
- [x] Record before and after numbers for every change
- [x] Record measured numbers for each row of the plan's section 2 target table in this file, with misses stated plainly

## The target table

Measured 2026-11-03 on this machine, at 1920 by 1080, with the desktop on the same card, under the build lock. **Two targets are missed: 120 fps on one row of 12, by 0.21 ms, and the tick at 100 mobs once the CPU is held down by its heat (issue 1).** The picture of every row is what it was before this stage to within 1 in 255 on 0.001% of channels or fewer, of the 6 rows compared.

| Metric | Target | Measured | |
|---|---|---|---|
| Frame rate at 1080p, render distance 32, all effects | 120 FPS (8.33 ms) | 11 of 12 rows of `bench_world`. The slowest, `deep_ocean/orbit`, **117.0 fps, 8.54 ms**. Next `jungle/orbit` 123.5, `plains/flight-live` 129.4. With god rays, the one effect off by default, 143.6. Before this stage: 5 of 12, and the slowest 34 to 37 | **MISSED** on one row, by 2.5% |
| Render distance setting | 2 to 512 chunks | 2: runs, a one-line check below. 128: 152.2 fps live. 512: 109.6 fps, was 66 to 75 | met; no frame target at those |
| Terrain | ≥ 1000 columns/s on 16 workers | Slowest place, `jungle`: **1080** on the release preset from a cool CPU. **707** on the profile preset straight after other runs | met from cool; **MISSED when hot**: see issue 1 |
| Sim tick at 100 mobs | ≤ 5 ms of the 50 ms budget | `bench_world`, `night/walk-mobs`, whole game, p99: **3.0 to 4.0 ms** in 8 of 9 runs in a row, and **9.0 ms** in the 9th, when the CPU was held down. The recorded full run: 8.9. `bench_mobs`, the mobs alone: p99 1.72 at worst | met, and **MISSED when the CPU is held down** |
| Allocations during play | 0 calls to the OS heap in hot paths | 0 in all 14 rows, flights and the walk included | met |
| VRAM | ≤ 6 GiB voxels, 256 MiB textures, 256 MiB frame buffers | Voxels held 1798 MiB, 482 to 829 in use, 2004 at r128, 2164 at r512. Textures 2 MiB. Frames 204 MiB, 220 at r128 and r512 (each up 16 MiB this stage for AO's list) | met |

Also asked for by Stage 17.2, and not a target: the far field still takes 105 to 118 s to fill when a world opens at render distance 128 or 512. Not looked into further: the game does not wait for it, as only `bench_world`'s settling does. Readback is a flat 1.28 ms of every picture, and cannot be made smaller: 8.3 MB in that time is 6.5 GB/s, which is what the card's link gives, as `nvidia-smi` says it is 8 lanes wide, not the 16 the plan assumed.

## The costs, ranked

From `bench_world` with effects taken away, `--features-off`, in ms of the shade pass, on the integration branch before anything here. The profiler's table gave the order of the passes and the mains: `shade` first by far on every slow row, then `readback`, `post`, `coarse`, `primary`. Since it times a kernel as a whole, what is inside shade had to come from this.

| Off | `plains/orbit` | `jungle/orbit` | `deep_ocean/orbit` |
|---|---|---|---|
| Nothing: all effects | 7.60 | 23.86 | 9.67 |
| AO | 4.68 | 8.66 | 9.33 |
| Shadows | 4.40 | 13.60 | 7.49 |
| AO and shadows | 1.87 | 2.80 | 7.23 |
| Alpha | 7.14 | 23.93 | 9.74 |
| Transparency | 7.04 | 23.94 | 4.07 |
| Reflection | 7.38 | 24.32 | 6.68 |
| Tint (walks all the way down) | 7.88 | 24.66 | 44.14 |
| TAA | 8.26 | 25.13 | 10.13 |

So: **AO, then the sun ray, then water.** In `jungle/orbit` AO's rays for pixels with nothing to go on were most of that: taking its turn rays away, by hand, left 20.65 of 22.63 at that point, and no AO at all 8.66.

## Before and after, for every change

Each line is three runs of `--only`, in order, on top of the one above: fps and shade pass, in ms. The picture hash stayed the same on every line unless it says so. These are with whatever the CPU was doing at that moment, which moves none of these numbers: they are the card's.

| Change | `plains/orbit` | `jungle/orbit` | `deep_ocean/orbit` |
|---|---|---|---|
| Integration branch | 91.8, 7.60 | 37.0, 23.86 | 77.3, 9.67 |
| **Ray coherence:** no branch on which axis a step takes | 95.4, 7.19 | 38.8, 22.63 | 77.6, 9.61 |
| **Memory layout:** `top_limit`: a rising ray above every column's top misses at once | 103.9, 6.37 | 39.3, 22.32 | 88.8, 8.02 |
| **Memory layout:** tables for `powf` of a texel's byte and of a light level, holding the card's own results | 110.8, 5.77 | 43.5, 19.85 | 89.3, 7.93 |
| **Ray coherence:** AO in a kernel of its own (0.001% of channels off by 1) | 146.4, 3.58 | 81.4, 9.18 | 92.6, 7.56 |
| **Ray coherence:** a last surface's sun ray in a kernel of its own | 154.0, 3.23 | 106.3, 6.29 | 90.8, 7.76 |
| **Ray coherence:** the first surface of water asks in a kernel of its own | 150.9, 3.36 | 104.9, 6.41 | 99.6, 6.79 |
| **Ray coherence:** fresh AO rays run as a list, not where they are on the picture (the shade numbers from the next run, with one line more, as the first run printed no passes) | 148.8, 3.47 | 115.7, 5.55 | 95.3, 7.27 |
| **Memory layout:** `region_top`: a ray over a whole square of 8 by 8 columns crosses it in a step, on the same cache line as the column | 150.6, 3.37 | 124.7, 4.89 | 100.7, 6.70 |
| **Ray coherence:** water deeper than a ray looks is finished in that first kernel, and only it keeps what it read of the tree | 150.7, 3.37 | 124.8, 4.89 | 117.0, 5.32 |
| Full run, recorded: the target table's numbers | 150.7, 3.37 | 123.5, 4.97 | 117.0, 5.30 |

The kernel count is the main thing here: **`jungle/orbit` went from 37 to 124 fps with the same picture, nearly all of it from running the rays of one kind together.** That a warp's threads which reach the same code at different times run it in turn is how the card is documented to work. That this is what cost so much is my reading of these numbers, as nothing here can see inside a warp. The kernel that kept the most, `shade_kernel`, went from 152 registers a thread to 146, as `cuobjdump -elf` says, so it is not register pressure that was saved. A kernel for plain pixels with few registers, below, gained nothing, which agrees.

**Tried, and taken out, each measured the same way:**

| Tried | `plains/orbit` | `jungle/orbit` | `deep_ocean/orbit` | Against |
|---|---|---|---|---|
| `__noinline__` on the sun and AO rays. Hoped: less code | 72.6, 10.48 | 30.8, 29.32 | 58.2, 13.89 | 95.4, 38.8, 77.6 |
| No `log2f` in the mip, no sky for a pixel with no fog, and no `powf` in post's, with the water cursor in `shade_kernel` | 104.7, 6.30 | 39.1, 22.44 | 80.6, 9.16 | 110.8, 43.5, 89.3 |
| The same, without the cursor. Gained nothing, so all of it went | 111.3, 5.72 | 43.9, 19.64 | 89.3, 7.94 | 110.8, 43.5, 89.3 |
| Two fresh AO rays and not four. Faster, and **the picture moved**, by up to 137 in 255 in `jagged_peaks/orbit` | 153.5 | 111.0 | 93.6 | 150.9, 104.9, 99.6 |
| A coarse answer for 16 by 16 pixels: costs more, not less, 0.70 and not 0.64 | 148.4, 3.36 | 102.8, 6.48 | 93.8, 6.96 | 150.9, 104.9, 99.6 |
| No coarse pass. Primary rays double, for 0.5 to 0.65 ms saved | 138.2 | 97.0 | 88.0 | 150.9, 104.9, 99.6 |
| The fresh AO list's length asked before launching | 148.3, 3.47 | 115.1, 5.55 | 95.0, 7.27 | 148.8, 115.7, 95.3 |
| `cudaFuncCachePreferL1` | 150.6, 3.37 | 124.5, 4.91 | 116.7, 5.34 | 150.7, 124.8, 117.0 |
| A kernel for pixels with only a texel to read | 150.6, 3.38 | 121.6, 5.10 | 117.4, 5.29 | 150.7, 124.8, 117.0 |
| A jump through a section all of water. Same picture, slower | 150.7, 3.37 | 124.7, 4.89 | 110.9, 5.79 | 150.7, 124.8, 117.0 |
| `post_sky`'s `powf` by squaring. 0.03% of channels off by 1, no faster | 150.6 | 125.1 | 117.3 | 150.7, 124.8, 117.0 |
| A table of where each byte starts for the tone map, in place of 3 `powf` a pixel. Same picture, no faster: post stayed 0.80 | 150.3, 3.39 | 123.1, 4.99 | 116.7, 5.34 | 150.7, 124.8, 117.0 |

**Tile order.** Blocks of 8 by 8, which are warps of 8 by 4, stay. Blocks of 4 by 16, or warps of 4 by 8: 139.0, 115.4, 111.9 fps. Blocks of 16 by 8, or warps of 16 by 2: 150.4, 120.6, 117.3. Against 150.7, 124.8, 117.0. The hits and pixels stay row by row: a warp's 32 pixels at 16 bytes each are 8 cache lines of 64 bytes whichever way they are laid out, so there is nothing to win.

**Upload budget.** 4 MiB a frame stays. A live flight sends about 27 MiB/s, a fifth of a MiB per picture, so the budget stops a frame only a few times in 10 s. At 2 MiB a frame: `plains/flight-live` 148.9 fps, 1180 columns, 6 stops, longest gap 17.9 ms. `jungle/flight-live` 153.3, 996, 2, 17.9. At 4 MiB, one run later: 144.7, 1209, 1, 17.9, and 149.8, 996, 3, 17.8. That is no difference that a run does not make by itself. The longest gap without a new picture, about 18 ms, went down from 26 to 43 ms.

**Thread placement.** It stays as it was, as what helped one target cost another. Moved so that nothing shares the sim's core, by leaving CPU 11 free and not 19, turn about over 3 rounds in the same minutes:

| | Old order, CPU 19 free | CPU 11 free |
|---|---|---|
| `jungle/flight-live`, tick p99 | 5.42, 5.17, 3.64 | 3.64, 3.76, 3.65 |
| `plains/flight-live`, tick p99 | 2.98, 3.12, 3.06 | 2.05, 2.82, 2.01 |
| `jungle/flight-live`, columns | 1010, 1014, 2411 | 1138, 1131, 1124 |
| `bench_terrain`, release, slowest place, from cool | **1080** | **977: would miss the target** |

So the tick is better in a live flight, where that was not the target's number, and the target's own row did not move: `night/walk-mobs` 8.87 and 9.15. Terrain lost its one core. **This is a choice Ahmed may want to make the other way.**

**Waiting for the card.** `cudaDeviceScheduleSpin` and `cudaDeviceScheduleBlockingSync`, to see if the wait was what held the CPU's clock down: no effect on the two groups in issue 1, and a blocking wait made the tick a little worse. Taken out.

## Verification

    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world
    tools/test-or-build.sh timeout 600 ctest --preset debug --output-on-failure
    pgrep -a minecraft_c    # must print nothing

What was run, and what it said: see "Checked" below.

## Checked

On the final code:

- `cmake --build --preset profile`: built, the last line read.
- `build/profile/bin/bench_world`: exit status 0, 496.6 s, under its 600 s limit. 11 of 12 rows met, `deep_ocean/orbit` 116.2 fps, `jungle/orbit` 122.7, and every picture hash the same as in the recorded run before it. The numbers in the table are from that recorded run, `--record bench/world/history.tsv`, 449.6 s, of the same code but for `post_temporal_t.sun_apart`, issue 3. The card's numbers of the two runs are within 1%.
- `ctest --preset debug --output-on-failure`: 96 of 96 passed, 1245.9 s, `standards_lint` and `clang_format` among them.
- `pgrep -a minecraft_c`: prints nothing.
- `build/profile/bin/minecraft_c --headless --test-world --ticks 60 --width 640 --height 360 --render-distance 2`: exit status 0, and 0 bytes live at exit.
- `build/release/bin/bench_terrain` from 60 C, with this thread order: exit status 0, slowest 1080. `build/profile/bin/bench_mobs`: exit status 0.
- Pictures of 6 rows, against those of the integration branch built apart, with `--shots`, last taken on the code of the recorded run: `cave/orbit` and `deep_ocean/orbit` the same to the bit, `plains/orbit`, `jungle/orbit`, `night/walk-mobs` and `jagged_peaks/orbit` off by 1 in at most 0.001% of channels, and by no more. No picture was looked at by eye in this stage, as that many channels off by 1 would not show.
- New in the tests: `tests/cuda/traverse/` now runs all its worlds and frames with `top_limit` and `region_top` set, against a reference that knows of neither; `a_square_of_columns_has_one_top_and_the_world_one_limit` in `run_render_upload`; the tables held to the functions in `run_render_frame`.
- The card: under 4 GiB, as `bench_world` keeps to 3584 MiB by its own budget and this stage adds 32 MiB of frame blocks. Not sampled again with `nvidia-smi`.

Not run: `tsan` and `sanitize`, and anything under KMS, which is Ahmed's to run. `release` was built only for `bench_terrain`.

## Commits

Appended as they land:

COMMITS_HERE

## Issues found

**1. Stage 17.2's two groups of CPU numbers are the CPU's heat, or the firmware's answer to it.** Expected: the same `tick` and `past the card` for the same pictures. Actual, 9 runs of `night/walk-mobs` in a row, started after 200 s idle:

| Runs | Tick mean | Tick p99 | Past the card | Package before, and at most | Throttle count, in that run |
|---|---|---|---|---|---|
| 1 to 8 | 0.62 to 0.71 | 2.97 to 4.05 | 1.71 to 1.97 | 61 to 73 C, up to 97 to 100 C | +22, rising to +596 |
| 9 | 1.90 | 8.99 | 5.99 | 73 C, up to 90 C | **+0** |

Sampled during `jungle/orbit` over 10 runs: `scaling_cur_freq` on CPU 0 averaged 1649 to 2883 MHz in the fast runs and **800 MHz in every slow run**, with the card's numbers the same in both, and spin waiting (where that core was busy for 197 of 200 jiffies) did not change that. Once down it stays down for some minutes: 8 slow runs in a row in one sample. An 8 MiB `memcpy` alone, cool, is 0.27 ms, and the game's `present`, which is that, was 0.75 and then 1.76. `thermal_throttle` counts show the package throttling, with `x86_pkg_temp` at up to 100 C. That is what I saw. That firmware then holds the clock low is my reading: the uncore's clock is not readable without root, and RAPL was not looked at. Nothing here changes a system setting, as this is Ahmed's machine.

Done: nothing in the code can fix a CPU that is too hot; the waits were tried, above. **The sim tick target is met from a cool CPU and missed from a held-down one, and so is the terrain target. Ahmed may want to look at the CPU's cooling or its limits.** `bench/world/README.md` says which group a number is in. A full run of 8 minutes ends up in the slow group by its later rows, which is why its `night/walk-mobs` says 8.9 and not 3.5.

**2. The first run of `lut` measured the old binary.** Expected: a new number. Actual: the build had failed on `error #20091-D: a __constant__ variable "SHADE_LEVEL_LIGHT" cannot be directly read in a host function`, and my `grep` hid that, so that row was the last build again, to the digit of its picture. Fixed: `__constant__` only in the card's pass, by `__CUDA_ARCH__`. From there every build's own last line was looked at.

**3. Holding AO's work in the hits block changes what a test reads.** Expected: all tests pass with sun rays apart when AO is off too. Actual: `render_feature_shapes`, `the_coarse_pass_never_starts_a_ray_past_a_box`: `different == 0u: 11011 != 0`, as it reads a frame's hits afterwards to hold them against a frame with no coarse pass, and with this they are the last surfaces, with a miss where a pixel has none. Fixed: `SHADE_FEATURE_SUN_APART`, which only `game` passes to `render_frame_features_set`, so that frames in tests that ask for the bare effects leave hits alone. It goes on as `post_temporal_t.sun_apart` and not in `scene.features`, since 4 tests hold that to `SHADE_FEATURES_EVERY` (`131071 != 65535`, when I first put it there). AO on still writes them, and no test with AO on reads them. So: **after a frame with AO on, or from `game`, `owned_hits` is not the primary hits.** Nothing in the game reads them after shade.

**4. A real capped ray in `shade_kernel`.** `TRAVERSE_INFO_CAPPED` in a hit now also means `shade_water_kernel` finished this pixel, so a primary ray that truly ran out of steps, with transparency on, would keep whatever was in its pixel from before. Left alone: it is never seen, as `tests/cuda/traverse/` holds it to 0 over every ray of its worlds, and the cap only trips on a bug. If it were seen, that is what it would look like.

**5. `run/src/game/index.c` stays near its limit.** 1764 lines, with no line added by this stage, as Stage 17.1 issue 4 says. `cuda/src/shade/index.cu` went from 1008 to 1449 lines, 51 short of the standard's point of splitting. The next kernel in it should split it.

**Limits to know about, by design:**

- The tables `SHADE_SRGB_BYTE`, `SHADE_LEVEL_LIGHT` are this card's `powf`, printed exactly. On another card a picture may round otherwise by 1 in a few channels, and nothing else.
- With AO on, a pixel's light is added in three kernels and so in another order, which is where the off-by-ones come from. Golden pictures in `tests/cuda/post/` have held at their own tolerances.
- `bench_mobs`, `bench_terrain` here ran on the profile and release presets as said in the table. `tsan` and `sanitize` were not run on this stage's change, as it adds no thread to the host and no host memory.
- `--features-off` numbers from before this stage include the inline sun ray, so they do not add up to what the kernels now cost.
