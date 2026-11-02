# 001-ahmed-bohemoth-minecraft-from-scratch — phase 4, stage 1: Vector library

**Status:** done   (updated 2026-11-02)

## Goal

vec2/3/4, ivec3, mat4, quat, aabb and ray exist, all scalar C.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-04-math-simd.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/math/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/math/*`
- `tests/run/math/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `math` Owner tier: `run/src/math/index.c` with paired `math_init` and `math_deinit`, owning the regions and arenas its parts borrow (it owns none: see issue 1)
- [x] Implement `run/{src,include}/math/vec2`, `vec3`, `vec4`, `ivec3`, `mat4`, `quat`, `aabb`, `ray`, each as its own `index.*`
- [x] Keep this library scalar C, per the coding standard's NASM rule
- [x] Add `static_assert`s on size and alignment for each type
- [x] Add tests under `tests/run/math/`, registered with names starting `run_math_`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

**Wiring outside the owned paths:** `math_t math` in `app_t` (`run/include/main/index.h`, taken from `_pad0`), `math_init` before
`job_init` and `math_deinit` after `job_deinit` (`run/src/main/index.c`), its entries in `run/src/main/README.md`, and two rows of
`plans/REPO_STRUCTURE.md`.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_math_' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `2a1f416` Add the scalar vector library under run/src/math with its tests, and check the floating-point environment from main before any worker starts.

## Results

Run on 2026-11-02, after the merge of the integration branch at `9a18bbe`:

- Debug: the build is clean, and all 23 ctests pass, including the 9 `run_math_*`, `standards_lint`, `clang_format` and `run_types`.
- Sanitize and release: `run_math_*` all pass. `run_types` passed in both before the last test was added.
- Release inlining: `nm build/release/bin/tests/run_math_vec3` lists no `vec3_*` or `math_*` symbol, so `-flto` inlined them all.
- Scalar: `objdump -d` of the release `run_math_ray` has no `addps`, `subps`, `mulps` or `divps`, in either encoding.
- Mutation: 44 hand-made bugs across all nine source files. 43 failed their test at once, and the other is issue 3. Each was a
  single sed on the source, such as a flipped cofactor sign, `<` for `<=` on a box edge, or a dropped NaN guard.

## Issues found

1. **The todo says the Owner owns "the regions and arenas its parts borrow", and there are none to own.**
   - Expected: an Owner with memory to hand down.
   - Actual: every part is arithmetic on values the caller holds. `plans/CODING_STANDARD.md` section 4 says any tier may allocate
     less than it is allowed, and names `run/src/math/vec3/` as depth 2 allocating nothing.
   - Done: `math_t` holds no memory. Rather than an empty pair, `math_init` does real work: it refuses a rounding mode other than
     nearest, and flush-to-zero or denormals-are-zero, any of which would break "same seed, same world". It runs before `job_init`
     because a new thread inherits the creating thread's environment, and `run_math_owner` tests that inheritance.
   - Left open: no test sees `minecraft_c` itself exit on a bad environment. There is no command line option to cause one, so
     `run_math_owner` covers `math_init`, and only review covers its call in `main`.
2. **My first test did not compile.**
   - Expected: `tests/run/math/quat/index.c` to build.
   - Actual: a local `float32_t t` hid the harness's `test_context_t *t`, so `CHECK` failed with "expected 'test_context_t *' but
     argument is of type 'float32_t'".
   - Done: renamed the local to `blend`.
3. **One mutant survived: nothing pinned what a ray starting exactly on a face reports.**
   - Expected: `t_in >= hit.t_enter` in place of `>` in `ray_intersect_aabb` to fail `run_math_ray`.
   - Actual: it passed. The header says a ray at t_min came through no face, and no test held it to that.
   - Done: added `a_ray_starting_on_a_face_came_through_no_face`. The same mutant now fails it.
4. **The FMA test needed proof that it can fail.**
   - Expected: `a_dot_product_is_not_fused_into_an_fma` to tell the two builds apart. `-march=skylake` has FMA, and only `-std=c23`
     keeps GCC from contracting, so losing that flag would silently change results.
   - Evidence: built by hand with `gcc -std=c23 -O2 -march=skylake -ffp-contract=fast`, it fails at `tests/run/math/vec3/index.c:116`;
     the same line without that flag exits 0. It also passes in the release preset, where `-flto` generates code at the link.
   - Done: nothing further; recorded so nobody weakens it.
