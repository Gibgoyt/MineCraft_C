# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 1: CMake, mutex and hello binary

**Status:** not started   (updated 2026-11-01)

## Goal

Presets, the mutex wrapper and all three languages work, producing a hello binary. Proof: configure and build succeed on all presets.

**Wave:** W2. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-build-skeleton.md`.

**Owned paths** (edit only these, plus this file):

- `CMakeLists.txt`
- `CMakePresets.json`
- `cmake/`
- `tools/`
- `.gitignore`
- `.claude/rules/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Write `tools/test-or-build.sh`, a flock mutex around heavy commands
- [ ] Write `.claude/rules/test-or-build-mutex.md`
- [ ] Write root `CMakeLists.txt` with `project(minecraft_c LANGUAGES C ASM_NASM CUDA)` using the Stage 01.1 results
- [ ] Write `CMakePresets.json` with configure, build and test presets `debug`, `sanitize`, `release`, `profile`
- [ ] Write `cmake/` modules for flags and targets, using the plan's C flags, with object libraries `mc_asm`, `mc_cuda`, `mc_run` linking into `minecraft_c`
- [ ] Add `.gitignore` for `build/`

## Verification

Accepted exception (plan section 6): this stage creates the wrapper, so work before it exists is not wrapped.

    cmake --preset debug
    cmake --preset sanitize
    cmake --preset release
    cmake --preset profile
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh cmake --build --preset profile
    timeout 10 build/debug/bin/minecraft_c
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
