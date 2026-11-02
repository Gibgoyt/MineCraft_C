# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 1: Device memory and C bridge

**Status:** done   (updated 2026-11-02)

## Goal

The C ABI, device allocator and error handling exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-cuda-renderer-core.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/gpu_bridge/index.*` and `README.md` (the Owner tier)
- `cuda/{src,include}/device_memory/`
- `run/{src,include}/gpu_bridge/*`
- `tests/run/gpu_bridge/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `gpu_bridge` Owner tier: `run/src/gpu_bridge/index.c` with paired `gpu_bridge_init` and `gpu_bridge_deinit`, owning the regions and arenas its parts borrow
- [x] Implement `cuda/src/device_memory/index.cu`, the only place calling `cudaMalloc`/`cudaFree`
- [x] Write the `extern "C"` API in `run/include/gpu_bridge/*/index.h`, included from `cuda/include/*/index.hu`
- [x] Add `static_assert`s for shared structs, compiled on both sides
- [x] Handle and report CUDA errors
- [x] Register test `run_gpu_bridge`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each. One exception: issue 3

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_gpu_bridge$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Run on 2026-11-02, on the RTX 2080 Super, with X up: all pass, and `pgrep` prints nothing. Beyond those: the whole debug suite, 48 of 48; `run_gpu_bridge` on the `sanitize` and `release` presets, which pass. `readelf -d` on the release program still shows only `libm`, `libc` and `ld-linux`.

## Wiring outside the owned paths

- `run/src/main/index.c`, `run/include/main/index.h`, `run/src/main/README.md`: `gpu_bridge_t` in `app_t`, its init and deinit, and `gpu_bridge_exit_check` in the leak check at exit. Stage 08.4's `game` will borrow it.
- `tests/run/types/index.c`: the program now reserves 6 heap regions and not 5, which that test states exactly.
- `cmake/targets.cmake`, `compile/src/standards_lint/catalog/index.c`, `cmake/hello/`: `mc_cuda` has a real source now, so the stand-in, its fallback and its lint allowance are gone, as `cmake/hello/README.md` said they should be.
- `plans/REPO_STRUCTURE.md`: the parts and the test, and the stand-in's line removed.

## Commits

Appended as they land:

- `8e0a544` Add the C bridge to CUDA and the device allocator, with budgets per pool, checked handles, every CUDA error turned into a status, and a kernel self-check at start, and start it from main in place of the hello stand-in.

## Issues found

**1. `--headless` and the default run now need a card, by decision.** Expected from the wiring, recorded so that it is not a surprise. Before this stage those two ran with no GPU. With `gpu.enabled: true` they now exit with status 5 and `no CUDA device` on a machine CUDA cannot see a card on, which `the_program_says_why_and_exits_when_there_is_no_card` checks with `CUDA_VISIBLE_DEVICES=`. Tests that run the program (`run_types`, `run_display_headless`) inherit that. `--report` and `--test-pattern` do not start it. `minecraft_c` was not run in KMS mode at any point, and `--test-pattern` was not touched.

**2. A failure already reported came back as the next kernel's own.** Expected: a refused allocation leaves the next launch alone. Actual: on the release preset only, `a_kernel_changes_a_real_block` failed with `starting the self-check kernel: out of memory (CUDA 2, cudaErrorMemoryAllocation: out of memory)`, deterministically, in six runs of six, and debug and sanitize passed. Cause: `cudaGetLastError` returns the last error of any earlier call until something asks, and the pebibyte in `the_card_saying_no_is_reported_in_cuda_s_words` left one. The presets differed because the tests register from constructors, whose order differs under `-flto`, so on release that test ran first. So in the game, one failed `cudaMalloc` would have failed the next frame's launch, on any preset. Fixed: `device_memory_check` clears it as it records it. A regression check now does this in one test, regardless of order; it failed on debug before the fix (`after_failure == GPU_STATUS_OK: 4 != 0`) and passes after it, on all three presets.

**3. `run/src/gpu_bridge/device_memory/` holds a README and no `index.*`.** The standard says one `index.*` per source directory, and the boundary in section 8 says this part's source is under `cuda/`, so there is no C to put there. The directory exists because `standards_lint`'s `include_sibling` looks for `run/src/<part>/README.md` to allow one part's header including another's, and `device_memory/index.h` must include `status/index.h`. The lint accepts it. Stages 08.2 onward add areas the same way and will want the same: each area's header needs `gpu_status_t`.

**4. Measurement refused: whether CUDA's own threads inherit main's pin.** `gpu_bridge_init` runs before main pins itself, because a thread inherits its creator's CPU mask. I tried to confirm it by listing the threads' masks during one default run, from a script in the scratchpad that started `build/debug/bin/minecraft_c` and read its `/proc` entries. The permission classifier refused it (`Interfere With Workloads`), and I did not route around that. So how many threads CUDA starts, and their masks, is not measured here. The order is correct whatever the answer, and the comments are worded so as not to claim it. Left for Ahmed if he wants the number.

**5. Not exercised for real: a faulting kernel.** `GPU_STATUS_DEVICE_LOST` and `GPU_STATUS_NO_KERNEL_IMAGE` are read as a table against the numbers in `driver_types.h`, and were never produced by a card, because faulting a kernel on a card the desktop shares is not worth the risk. `cudaErrorNoDevice` and `cudaErrorMemoryAllocation` are real, on the real runtime.

**6. Measured.** `cudaMalloc` of 1 PiB fails at once with `cudaErrorMemoryAllocation` and no block. The card reports 7798 MiB total, 7392 MiB free with the desktop up, against a budget of 6656 MiB, so the budget fits with about 700 MiB to spare at this moment. Blocks of 1, 3, 100, 4096 and 1 MiB all came back on a 64-byte boundary. `run_gpu_bridge` takes about 0.6 s on debug, with its blocks never above 16 MiB.
