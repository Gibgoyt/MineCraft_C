# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 1: Toolchain link spike

**Status:** not started   (updated 2026-11-01)

## Goal

We know whether CMake links C, NASM and CUDA through a `main.c`, whether `-fno-exceptions -fno-rtti -fno-threadsafe-statics` remove the C++ runtime symbols, and whether cudart should be static or shared. Pass: a binary runs a kernel and the exact link list is recorded for the coding standard.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/toolchain_link/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create a throwaway CMake project in `/tmp/mc_c_spikes/toolchain_link/` with languages C, ASM_NASM and CUDA
- [ ] Link `main.c`, one NASM `.o` and one `.cu` kernel built for `sm_75`, and run the kernel
- [ ] Run `nm -u` on the CUDA object with and without `-fno-exceptions -fno-rtti -fno-threadsafe-statics`, and record the symbols that remain
- [ ] Try static and shared cudart, and record what each needs at link and run time
- [ ] Try `_Float32` in device code, to settle the CUDA `float32_t` mapping
- [ ] Record the exact link list in this file for `plans/CODING_STANDARD.md`

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    cmake -S /tmp/mc_c_spikes/toolchain_link -B /tmp/mc_c_spikes/toolchain_link/build
    cmake --build /tmp/mc_c_spikes/toolchain_link/build
    timeout 60 /tmp/mc_c_spikes/toolchain_link/build/spike
    pgrep -a spike    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
