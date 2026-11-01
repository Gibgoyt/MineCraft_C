# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4, stage 2: Copy, blend and bitmask kernels

**Status:** not started   (updated 2026-11-01)

## Goal

NASM conventions are in use, with streaming copy, alpha blend, popcount and mask kernels and C prototypes.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-math-simd.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/simd/index.*` and `README.md` (the Owner tier)
- `asm/{src,include}/copy/`
- `asm/{src,include}/blend/`
- `asm/{src,include}/bitmask/`
- `run/{src,include}/simd/*`
- `tests/asm/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `simd` Owner tier: `run/src/simd/index.c` with paired `simd_init` and `simd_deinit`, owning the regions and arenas its parts borrow
- [ ] Create the shared NASM conventions in `asm/include/` per `plans/CODING_STANDARD.md`
- [ ] Implement `asm/src/copy/index.asm`, `asm/src/blend/index.asm`, `asm/src/bitmask/index.asm`
- [ ] Declare C prototypes in `run/include/simd/<name>/index.h`
- [ ] Add tests against scalar C references under `tests/asm/`, registered with names starting `asm_`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^asm_' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
