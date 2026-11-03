# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18, stage 4: Portability seams

**Status:** done   (updated 2026-11-04)

## Goal

Portability seams are documented for later platforms, and a CPU-only render backend is recorded as future work.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-release-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `plans/REPO_STRUCTURE.md` portability section

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Document each platform seam: `config/machine.yaml`, `run/src/display`, `run/src/input`, `run/src/gpu_bridge`
- [x] Record the CPU-only render backend as future work

## Verification

    grep -n 'CPU-only' plans/REPO_STRUCTURE.md

## Commits

Appended as they land:

## Issues found

**1. `gpu.enabled: false` does not give a game without a card.** Found 2026-11-04 writing section 15.5. Left alone: this stage documents, and the CPU-only backend is the future work that would change it.

- Expected, from the key's name: a build with no CUDA that still runs.
- Actual: `MC_GPU_ENABLED` is read in one place that matters, `run/src/main/index.c:440`, which skips `gpu_bridge_init`. `CMakeLists.txt:4` still names `CUDA` as a language, and `cmake/targets.cmake` still compiles `mc_cuda` and links `cudart_static`.
- Evidence: with only that key changed, on the debug preset, `--report` printed `MC_GPU_ENABLED=0` and exited 0, and `--headless --ticks 2` exited 0. `--headless --test-world --ticks 2` exited 5 with `gpu: allocating from a closed device_memory_t: invalid argument`, and `heap at exit: 15 reserved, 15 released, 0 bytes live`. The yaml was restored with `git checkout`.

**2. Four `machine.yaml` keys that sound like switches are not.** Found 2026-11-04. Left alone and recorded in section 15.1, because no second platform exists to switch to.

- `platform.os`, `gpu.api` and `display.backend` have no consumer. `machine_config` refuses any other word, measured with a copy of the yaml: `platform.os must be one of: linux`, `display.backend must be one of: kms`, `gpu.api must be one of: cuda`, each exit 1.
- `platform.posix` is only printed by `--report`.

**3. `threads.gpu: 0` passes the config check and looks fatal later.** Found 2026-11-04. Left alone. `machine_config` accepted a copy of the yaml with it (exit 0), and its comment in `compile/src/machine_config/index.c` says the gpu thread goes away on a build with no GPU. But `run/src/game/index.c:1777` always starts that thread, and `thread_cpu_for` has `ASSERT_ALWAYS(index < count)` (`run/src/thread/index.c`). This is from reading the code. A threaded game with that value was not run.
