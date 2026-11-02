# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 1: CMake, mutex and hello binary

**Status:** done: verified on all four presets, most recently by the phase's third gate check; Stage 01.1's follow-ups are confirmed (issue 10)   (updated 2026-11-02)

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

- [x] Write `tools/test-or-build.sh`, a flock mutex around heavy commands
- [x] Write `.claude/rules/test-or-build-mutex.md`
- [x] Write root `CMakeLists.txt` with `project(minecraft_c LANGUAGES C ASM_NASM CUDA)` using the Stage 01.1 results
- [x] Write `CMakePresets.json` with configure, build and test presets `debug`, `sanitize`, `release`, `profile`
- [x] Write `cmake/` modules for flags and targets, using the plan's C flags, with object libraries `mc_asm`, `mc_cuda`, `mc_run` linking into `minecraft_c`
- [x] Add `.gitignore` for `build/`
- [x] Add `.gitignore` for `.claude/worktrees/`, and `.clang-format` (workflow task)
- [x] Discover modules by path with `GLOB` and `CONFIGURE_DEPENDS` (workflow task)

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

Result, from a deleted `build/` on 2026-11-02 (GCC 16.2.1, nvcc 13.4.92, NASM 3.02, CMake 4.4.3). The configure commands were run through the wrapper as well, see issue 7:

- All four configures and all four builds return 0.
- On each of the four presets, `minecraft_c` prints `minecraft_c hello: nasm=524800 cuda=524800 expected=524800` and exits 0. `main` returns non-zero unless both sums match, so exit 0 means the kernel ran.
- `ctest --preset <each>`: `100% tests passed out of 1` (`minecraft_c_hello`).
- `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"` print nothing, rc 1.

Further checks beyond the list above, all run in the worktree with throwaway files that were then removed:

| Claim | Evidence |
|---|---|
| Exact C flags | `build.ninja`: `-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion -Wshadow -Wvla -Wpadded -Werror -march=skylake -mtune=skylake -fno-tree-vectorize -fno-tree-slp-vectorize`, plus `-O3 -flto=auto` in release |
| Stage 01.1 link result | `NEEDED`: `libm.so.6 libc.so.6 ld-linux-x86-64.so.2` on debug, release and profile; `libasan.so.8 libm.so.6 libubsan.so.1 libc.so.6` on sanitize |
| No C++ runtime at run time | `LD_DEBUG=files` on release loads `libc libm libdl libpthread librt libcuda.so.1 libnvcuextend.so`; neither `libstdc++` nor `libgcc_s` |
| No C++ runtime symbols | `nm -u` on the CUDA object has no `cxa`, `gxx` or `Unwind` symbol |
| Device code is `sm_75` | `cuobjdump --list-elf`: `minecraft_c.1.sm_75.cubin` |
| The link check bites | `cmake/check_link.cmake` returns 1 on a program linked with `-lstdc++`, on one linked with `-z execstack`, and on the sanitize binary judged as debug |
| Padding fails the build | A struct with trailing padding in a probe module: `error: padding struct size to alignment boundary with 7 bytes [-Werror=padded]`, build rc 1 |
| nvcc warnings fail the build | A `__host__ __device__` function calling a host function: `error #20011-D`, build rc 1 |
| Discovery | Adding one module per language, a library and a tool under `compile/`, `tests/harness/`, and tests under `run`, `asm` and `compile`, with no `cmake` re-run and no list edited: built, and all 5 ctests pass on debug, sanitize and release. A `fixtures/bad/index.c` holding invalid C was not compiled. `test.cmake` set the name, arguments, timeout and an extra `add_test` |
| Real `main` replaces hello | With a probe `run/src/main/index.c`: `minecraft_c` prints from it, `minecraft_c_hello` leaves ctest, the tests still link, and `build.ninja` has no `cmake/hello` once the other two languages have modules. Removing the probe restores hello |
| Mutex exit status | No arguments 64; `true` 0; `false` 1; `exit 42` gives 42; a missing command 69; arguments with spaces and the working directory arrive unchanged |
| Mutex excludes across directories | A holds the lock for 3 s in one directory; B, started from `/tmp`, began 1.7 ms after A ended and printed `test-or-build: waiting for /tmp/mc_c_build.lock` |
| A leftover process cannot keep the lock | After a command left a background `sleep`, `flock -n /tmp/mc_c_build.lock true` succeeded at once |
| `.gitignore` | `git check-ignore build/debug .claude/worktrees/x` prints both |
| `.clang-format` | `clang-format --dry-run --Werror` on the two C files and the `.cu` returns 0 |

**Not tested:** other machines, and a real parallel build by two agents beyond the lock probe above. `-fsanitize` covers C only: nvcc's host code and NASM are not instrumented.

Second attempt, 2026-11-02, on branch `worktree-wf_a4b549de-085-12` after fast-forwarding onto the first attempt's commits: from a deleted `build/`, all four configures and builds return 0, `minecraft_c` prints `nasm=524800 cuda=524800 expected=524800` with rc 0 on all four, `ctest` is `100% tests passed out of 1` on all four, `NEEDED` is unchanged on release and sanitize, and `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"` both return 1. This run also closes the NASM gap: a probe `asm/src/nasm_warn_probe/index.asm` with a label alone on a line gave `error: label 'orphan_label' alone on a line without a colon might be in error`, build rc 1. Removing the probe restored a passing build with no re-run of `cmake` by hand.

**Notes for later stages.**

- Stage 02.3: include `cmake/machine.cmake` is optional and already wired, and it should set `MC_CPU_ARCH_FLAG`. Until then `cmake/flags.cmake` falls back to `skylake`. Generated files belong in `MC_GENERATED_DIR`, which is `build/<preset>/generated/`, as `plans/REPO_STRUCTURE.md` says; the master plan and the Stage 02.3 file say `build/generated/`.
- Stage 02.4: add the lint with `add_dependencies(mc_prebuild <lint target>)`. Tests named `standards_lint` and `standards_lint_fixtures` do not match the path rule, so use `MC_TEST_NAME` or `add_test` in a `test.cmake`. Put bad-code fixtures under a `fixtures/` directory, or discovery will compile them. Leave `cmake/hello/gpu/` out of the `cudaMalloc` check, or scan only `run/`, `asm/`, `cuda/` and `compile/`.
- A tool's own `main` file cannot be linked into its test, because a test has its own `main`. Put logic to test in a `main`-free directory under `compile/src/`, or have the test run the tool: `set(MC_TEST_ARGS $<TARGET_FILE:mc_tool_x>)`.

## Commits

Appended as they land:

On the integration branch `worktree-wf_a4b549de-085-20`; Ahmed fast-forwards `main` at the end.

- `004761b` Build, presets, discovery, mutex and hello
- `996f273` Verification and refused landing
- `2ac15d2` Re-verification and second refused landing
- `291b50c` First gate check: `cmake/flags.cmake`, `cmake/hello/`
- `8b5bd21` Second gate check: `cmake/`

## Issues found

**1. Stage 01.1's findings are not on `main`.**
- Expected: the task says to apply the findings "recorded in Stage 01.1", so they should be in this checkout after `git rebase main`.
- Actual: `main` is still `5bb5c57`. The stage file there reads "not started", and `plans/CODING_STANDARD.md` still says *(Stage 01.1)*. The results exist only on the unlanded branch `worktree-wf_a4b549de-085-10`, whose agent could not land it (its issue 8).
- Done: read that stage file and the `/tmp/mc_c_spikes/toolchain_link/` project directly, without changing them, and applied them: static cudart, `LINKER_LANGUAGE C`, cleared implicit CUDA link variables after `project()`, `-Xcompiler=-fno-exceptions,-fno-rtti,-fno-threadsafe-statics`, and the build-time `readelf` check Stage 01.1 issue 5 asked for. This branch does not include Phase 01, so landing it does not carry that work along. They touch no common file except possibly `plans/`, on different lines.

**2. This stage's owned paths have no room for the hello program's sources.**
- Expected: the stage needs C, NASM and CUDA sources to produce a hello binary.
- Actual: `run/`, `asm/` and `cuda/` are owned by later stages, and Stage 03.1 owns `run/src/main/`. An object library with no sources is also a CMake error, and later stages need `mc_asm` and `mc_cuda` to exist before they add a module.
- Done: put the program in `cmake/hello/`, which this stage owns, with `cmake/targets.cmake` switching it off by path. Nobody has to remove it by hand. Listed in `plans/REPO_STRUCTURE.md`.

**3. The first version of the test link kept the game's `main`, twice over.**
- Expected: `$<FILTER:$<TARGET_OBJECTS:mc_run>,EXCLUDE,mc_run\.dir/.../main/index\.c\.o$>` to remove `main` from a test's objects.
- Actual: `multiple definition of 'main'`, build rc 1. Found only because the discovery probe built a real test. Two separate causes. CMake rewrites backslashes in a source list to slashes, so `\.` became `/.`. And `$<TARGET_OBJECTS>` evaluates to `mc_run.dir/./cmake/hello/main/index.c.o` with a `./` that the Ninja file does not show, which a scratch project with `file(GENERATE)` revealed.
- Done: fixed with `[.]` and an optional `([.]/)?`. The probe then passed on debug, sanitize and release.

**4. CMake passes `-std=c2x`, not `-std=c23`.**
- Expected: `CMAKE_C_STANDARD 23` to give the standard's `-std=c23`.
- Actual: `build.ninja` had `-std=c2x`, the old spelling of the same mode.
- Done: set `CMAKE_C23_STANDARD_COMPILE_OPTION` to `-std=c23`, and confirmed it in `build.ninja`.

**5. CUDA under ASan needs an option, or it fails with a misleading error.**
- Expected: nothing special needed in the sanitize preset.
- Actual: the option is required, and I set it before testing without it. With `ASAN_OPTIONS=protect_shadow_gap=1` forced, `minecraft_c` prints `free sum: out of memory` and `the CUDA kernel failed`, rc 1.
- Done: `cmake/sanitize/index.c` sets `protect_shadow_gap=0` through `__asan_default_options`, linked into every sanitize program that links CUDA. Listed in `plans/REPO_STRUCTURE.md`.

**6. Small additions to the flags in the plan and the standard.**
- `-flto=auto` where they say `-flto`: the same LTO, with parallel link jobs.
- The link repeats `-O3`, `-march`, `-mtune` and the `-fno-tree-*` flags, so that LTO code generation sees them.
- `-D_GNU_SOURCE=1` for C and CUDA, because `-std=c23` hides POSIX and Linux declarations the standard allows.
- `--Werror=all-warnings` for nvcc, `-w+all -Werror` for NASM, `-Wl,--fatal-warnings -Wl,-z,noexecstack` for the link.
- `profile` is `-O3 -g -flto=auto -fno-omit-frame-pointer` with `MC_PROFILE=1`, and `sanitize` is `-O1 -g3 -fsanitize=address,undefined -fno-sanitize-recover=all`, neither of which the plan spelled out.
- Host code in `.cu` files does not get `-fno-tree-vectorize`. The flag list in the standard is for C. That host code is thin, and this was left as it is.

**7. The Verification list shows `cmake --preset` unwrapped; I wrapped it.**
- The stage file lists `cmake --preset <name>` unwrapped, but the configure step compiles with nvcc, and the workflow task says every heavy command goes through the wrapper once it exists. Results are the same. The wrapped form is what `.claude/rules/test-or-build-mutex.md` tells agents to use.

**8. The landing command is refused, so this work is not on `main`.**
- Expected: the workflow's landing step to fast-forward `main`.
- Actual: `flock /tmp/mc_c_land.lock sh -c 'git rebase main && git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only '"$(git rev-parse --abbrev-ref HEAD)"` returns `this command names git in a form too complex to verify that it stays inside the worktree. Refusing to run it — a worktree-isolated agent's git operations must target its own worktree.` A plain read with `git -C` on the main checkout was refused earlier in this session too: `this command redirects git to the shared checkout via -C`. `main` stays at `5bb5c57`. This is the same block as Stage 01.1 issue 8.
- Second attempt, 2026-11-02: the same command, run once more unchanged from `worktree-wf_a4b549de-085-12`, returned the same refusal, and `main` is still `5bb5c57`. Retrying from an isolated worktree cannot succeed, so this needs the merge below run from outside it, or the guard changed by whoever owns it. No other way of moving `main` was tried.
- Done: escalated. I did not try another way to move `main` (`update-ref`, `branch -f`, `cd` into the main checkout), because that would bypass the isolation rule. The branch is rebased on `main`, and `git merge-tree --write-tree worktree-wf_a4b549de-085-10 HEAD` reports no conflict with the unlanded Phase 01 branch. To finish, from outside the isolation, in the main checkout: `git merge --ff-only worktree-wf_a4b549de-085-12`, which contains all of `worktree-wf_a4b549de-085-11`. If `main` has moved, rebase this branch first. Then record the landed sha above and set Status to done.
- Until then, **every later stage that rebases on `main` will not have `tools/test-or-build.sh` or any CMake file.** Those stages depend on this landing.

**9. Gate check, 2026-11-02: three changes to this stage's files.** Evidence is in the phase file's gate check table.
- `cmake/flags.cmake`: the frame limit and warnings did not apply at the LTO link, where `release` and `profile` generate code. The link now gets `-Wframe-larger-than=` and `-Werror`. Stage 02.2 issue 5 had asked for this, and the Stage 02.4 file on `worktree-wf_a4b549de-085-16`, its issue 9, measured the fix.
- `cmake/flags.cmake`: `CMAKE_CUDA_ARCHITECTURES` comes from `gpu.architecture` once Stage 02.3 is in, and stays 75 until then.
- `cmake/hello/main/index.c`: reports `MC_THREADS_WORKERS` once the generated header exists. Before this, the program had no way to meet Stage 02.3's proof.
- Status of the landing: see the phase file's issue 1, which gives the commands that supersede the one at the end of issue 8 above.

**9. Second gate check, 2026-11-02: changes in this stage's paths.**
- `cmake/flags.cmake`: new function `mc_callgraph_compile_options`, so that `cmake/standards_lint.cmake` can ask for call graph objects under `release` and `profile` while flags are still set in this one file. See Stage 02.4 issue 14.
- `cmake/machine.cmake`: the configure-time compile moved from `-O0` to `-O1`. See the phase file's issue 12.
- `cmake/check_format.cmake`, new, for ctest `clang_format`. See the phase file's issue 9. Added to `plans/REPO_STRUCTURE.md`.
- All four presets were rerun from a deleted `build/`; the table is in the phase file.

**10. 2026-11-02: Stage 01.1's follow-ups for this stage are done.**
- Expected: Stage 01.1 issue 5, a `readelf -d` check that fails the build if `libstdc++` appears, and its issues 4 and 5 and section 9 applied.
- Actual, on this clean `debug` build: `cmake/check_link.cmake` runs after the link on `minecraft_c`. With `set(CMAKE_CUDA_IMPLICIT_LINK_LIBRARIES "")` removed from `cmake/flags.cmake`, the build returned rc 1: `check_link: ... needs libstdc++.so.6. Allowed: libc.so.6;libm.so.6;ld-linux-x86-64.so.2`, `ninja: build stopped`. The line was then restored. `build.ninja` links `-lcudart_static -lm -lcudadevrt -lcudart_static -lrt -lpthread -ldl`, and `nm -u` on the CUDA object has no `cxa`, `gxx` or `Unwind` symbol.
- Done: nothing to change.

**11. 2026-11-02: the landing process changed, so the "not on `main`" text above is history.**
- Expected: this stage to land on `main` through the workflow's landing command (issue 8).
- Actual: every landing from a worktree was refused, and `main` is still `5bb5c57`. The refusals are recorded above as they happened, and are left as written. The Commits section used to say the work was unlanded, on branches `worktree-wf_a4b549de-085-12` and later `worktree-wf_a4b549de-085-20`.
- Done: the process changed. Agent branches now merge into `worktree-wf_a4b549de-085-20`, which stands in for `main`, and Ahmed fast-forwards `main` at the end. The landing commands written above are no longer anyone's to run. The Commits section now lists the shas on the integration branch.
