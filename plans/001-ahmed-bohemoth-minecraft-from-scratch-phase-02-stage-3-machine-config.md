# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 3: Machine config generator

**Status:** not started   (updated 2026-11-01)

## Goal

`config/machine.yaml` generates `.h`, `.hu`, `.inc` and `.cmake` output. Proof: changing a yaml value changes the binary's reported define.

**Wave:** W2. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-build-skeleton.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/machine_config/`
- `config/machine.yaml`
- `tests/compile/machine_config/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create `config/machine.yaml` with the exact content from plan section 5
- [ ] Implement `compile/src/machine_config/index.c` emitting `build/generated/machine/index.{h,hu,inc,cmake}`, including `MC_PLATFORM_POSIX`, `MC_THREADS_WORKERS`, `MC_CPU_HAS_AVX2`, `MC_GPU_ENABLED`
- [ ] Compile the tools at configure time with `try_compile`/`execute_process`, and add the yaml to `CMAKE_CONFIGURE_DEPENDS`
- [ ] Add `tests/compile/machine_config/index.c`, registered as ctest `compile_machine_config`
- [ ] Add `compile/src/machine_config/README.md`

## Verification

    cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_machine_config$' --output-on-failure
    # Change threads.workers in config/machine.yaml, rebuild, and confirm the binary reports the new value, then restore it.
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
