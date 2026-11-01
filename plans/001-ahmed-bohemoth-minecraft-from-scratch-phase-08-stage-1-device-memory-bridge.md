# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 1: Device memory and C bridge

**Status:** not started   (updated 2026-11-01)

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

- [ ] Create the `gpu_bridge` Owner tier: `run/src/gpu_bridge/index.c` with paired `gpu_bridge_init` and `gpu_bridge_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement `cuda/src/device_memory/index.cu`, the only place calling `cudaMalloc`/`cudaFree`
- [ ] Write the `extern "C"` API in `run/include/gpu_bridge/*/index.h`, included from `cuda/include/*/index.hu`
- [ ] Add `static_assert`s for shared structs, compiled on both sides
- [ ] Handle and report CUDA errors
- [ ] Register test `run_gpu_bridge`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_gpu_bridge$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
