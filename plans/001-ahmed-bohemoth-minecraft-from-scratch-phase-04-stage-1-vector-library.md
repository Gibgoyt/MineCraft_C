# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4, stage 1: Vector library

**Status:** not started   (updated 2026-11-01)

## Goal

vec2/3/4, ivec3, mat4, quat, aabb and ray exist, all scalar C.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-math-simd.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/math/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/math/*`
- `tests/run/math/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [ ] Create the `math` Owner tier: `run/src/math/index.c` with paired `math_init` and `math_deinit`, owning the regions and arenas its parts borrow
- [ ] Implement `run/{src,include}/math/vec2`, `vec3`, `vec4`, `ivec3`, `mat4`, `quat`, `aabb`, `ray`, each as its own `index.*`
- [ ] Keep this library scalar C, per the coding standard's NASM rule
- [ ] Add `static_assert`s on size and alignment for each type
- [ ] Add tests under `tests/run/math/`, registered with names starting `run_math_`
- [ ] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_math_' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
