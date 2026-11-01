# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 4: Standards lint and test harness

**Status:** in progress: the work is finished and verified; the only thing pending is the landing on `main` (issue 2)   (updated 2026-11-02)

## Goal

The lint and the test harness exist. Proof: each banned construct has a failing fixture.

**Wave:** W2. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-build-skeleton.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/standards_lint/`
- `tests/harness/`
- `tests/compile/standards_lint/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Check tiers by path depth, include direction from `plans/REPO_STRUCTURE.md` section 11, the `borrowed_` static allowlist, and `.hu`-only headers under `cuda/`
- [x] Detect recursion from GCC `-fcallgraph-info` output and set `-Wframe-larger-than=`, per `plans/CODING_STANDARD.md` section 10
- [x] Write the minimal test harness in `tests/harness/`
- [x] Implement `compile/src/standards_lint/index.c`, checking the banned list in `plans/CODING_STANDARD.md`, the 2000-line limit, `index.*` naming, and README presence
- [x] Run the lint before compile, so a breach fails the build
- [x] Add one failing fixture per banned construct under `tests/compile/standards_lint/`, including a 2001-line file, `int`, and a missing README
- [x] Add a padding fixture proving `-Werror=padded` rejects it
- [x] Register ctest `standards_lint` for the repo and `standards_lint_fixtures` for the fixtures
- [x] Add `compile/src/standards_lint/README.md`
- [x] Skip `build/` and `.claude/`, and lint every other directory with no list to edit (workflow task)
- [x] A ctest registration helper in `cmake/` that discovers `tests/**/index.c` (workflow task; see issue 7: Stage 02.1's `mc_add_test` already does this, so no second one was written)

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint_fixtures$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Result, 2026-11-02 (GCC 16.2.1, CMake 4.4.3):

- All three commands return 0 on `debug`. `standards_lint_fixtures` passes: 175 lint fixtures, 5 call graph fixtures and 6 compiler fixtures. All 138 rules in `standards_lint --list-rules` have a failing fixture, and the test fails if one does not. `standards_lint` reports `clean, 25 source files` and `no recursion`.
- `sanitize`, `release` and `profile` also configure, build, and pass all 4 ctests. `sanitize` runs the lint and the fixtures under ASan and UBSan.
- `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"` print nothing.

Further checks against the real build, with throwaway files that were then removed. A clean build followed:

| Claim | Evidence |
|---|---|
| A breach fails the build before the code compiles | `int world_probe;` in a new `run/src/world/index.c`: build rc 1 at `CMakeFiles/mc_standards_lint`, `[banned:int]`, and no `Building` line for that file |
| Every directory is linted with no list | `tools/nowhere/index.c`, in a directory no list knows: `[file_location]` and `[readme_missing]`, `ninja: build stopped` |
| 2001 lines fail | `wc -l` 2001: `[line_limit] 2001 lines; the limit is 2000`, `ninja: build stopped`. The fixture `ok_2000_lines` stays clean |
| Recursion across two real modules fails the build | `world_probe` and `terrain_probe` calling each other from two files: `[recursion] terrain_probe -> world_probe -> terrain_probe`, `ninja: build stopped` |
| Padding fails the build | `error: padding struct to align 'b' [-Werror=padded]`, `ninja: build stopped` |
| The frame limit fails the build on `debug` | A 20000-byte local: `error: the frame size of 20016 bytes is larger than 16384 bytes [-Werror=frame-larger-than=]` |
| Test discovery | `tests/compile/probe_discovery/index.c`, using only the harness, with no list edited: built, and `ctest -R '^compile_probe_discovery$'` passes |
| A harness failure really fails | `compile_standards_lint_harness` runs itself with tests that fail on purpose, and checks exit status 1, file and line, both values, and that `REQUIRE` stops its test |
| The fixtures can fail | On the first run `packed` failed, because of a real lint bug (issue 6) |

**Not verified:** the frame limit under `release` and `profile`; see issue 4. Other machines. How Stage 02.2 and 02.3 code fares against this lint, because neither had any code when this was written (issue 8).

**Notes for later stages.**

- Rule ids and where each lives are in `compile/src/standards_lint/README.md`. To see what the lint thinks: `build/debug/bin/tools/standards_lint --root .`
- Compile tools: `mmap` and `munmap` are allowed only at `compile/src/<tool>/index.c`; `malloc` is not allowed at all. A tool's logic in a part must be `main`-free, and takes an arena from its Owner.
- Tests: use `test_alloc` from `tests/harness/`, not `malloc`. `tests/harness/README.md` shows a whole test in five lines.
- A new `run/` system has to be added to the rank table in `compile/src/standards_lint/rules_include/index.c` and to `plans/REPO_STRUCTURE.md` section 11, or `[unknown_system]` fails the build.
- A new `borrowed_` static has to be listed in `borrowed_static_files` in `compile/src/standards_lint/catalog/index.c`.

## Commits

Appended as they land:

## Issues found

**1. Stage 02.1 is not on `main`, and this stage cannot exist without it.**
- Expected: after `git rebase main`, `tools/test-or-build.sh`, `CMakeLists.txt` and `cmake/` to be here.
- Actual: `main` was still `5bb5c57`, which has only `plans/` and `research/`. Stage 02.1's own issue 8 says its landing was refused and warns later stages of exactly this. Its work is on branch `worktree-wf_a4b549de-085-12` at `2ac15d2`.
- Done: fast-forwarded this branch onto `2ac15d2` with `git merge --ff-only`, which changes only this worktree. So **this branch carries Stage 02.1's three commits**, unchanged. It does not include any Phase 01 branch.

**2. The landing command is refused, so this work is not on `main`.** See the entry at the end of this section, written after the attempt.

**3. The standard names `-Wframe-larger-than=` but gives no number.**
- Expected: a value in `plans/CODING_STANDARD.md` or the master plan. `grep` found none, and no stage file sets a thread stack size.
- Done: chose 16384 bytes, set as `MC_FRAME_LIMIT_BYTES` in `cmake/flags.cmake`. The hello program's 4096-byte array fits, and every preset including ASan builds. It applies to C only: nvcc's host compile does not get it. Ahmed may want a different number once Stage 03.3 fixes the thread stack size. Changing it means changing that one line, plus the 20000-byte array in `fixtures/compiler/frame_too_large/` if it goes above that.

**4. With `-flto`, GCC writes no `.ci` file at compile time, so `release` and `profile` have no recursion check.**
- Expected: the check to run on every preset.
- Actual: `gcc -std=c23 -O3 -flto=auto -fcallgraph-info=su -c b.c -o lto/b.c.o` returns 0 and writes only `b.c.o`, where the same command without `-flto` also writes `b.c.ci`. So `MC_C_CALLGRAPH` is set only on `debug` and `sanitize`, and `mc_recursion_check` does not exist on the other two.
- Done: accepted, since all four presets build the same source and `debug` is the one every stage's verification runs. It could only miss recursion that exists solely under `#ifdef NDEBUG`.
- The frame limit has a similar gap that I could not close. Under `release` a probe with a 20000-byte local linked with no error: `[3/6] Linking C executable bin/minecraft_c`. That is inconclusive, because nothing called the function and LTO may have removed it. Compile time under LTO generates no code, so the check can fire only at the link, and the link flags do not carry it. `debug` and `sanitize` do enforce it. The fixtures compile with `-O0 -fno-lto` on every preset so they judge the flag and not the optimizer.

**5. The literal rule follows Stage 01.1's measurement, which is not on `main` either.**
- Expected: section 2 of the standard on `main` says an unsuffixed `1.0` is banned everywhere.
- Actual: the updated section 2 on unlanded branch `worktree-wf_a4b549de-085-10` says `.cu` and `.hu` must write `1.0F` or `1.0`, because an `f32` literal in device code crashes nvcc with `Internal Compiler Error`, and shared headers must write `F32(x)`.
- Done: implemented the measured version (`literal_cuda_suffix`, `literal_shared_header`), since the other would reject code that works and accept code that crashes the compiler. The same goes for `libc_double_t`, which the lint allows in the types header.

**6. Bugs the lint and its fixtures found during this stage.**
- The lint rejected its own code on its first real run: `compile/src/standards_lint/graph/index.c:13: [init_deinit_pair] lint_graph_init has no lint_graph_deinit`. It really had none, because it needs no teardown, so I renamed it `lint_graph_begin`.
- The fixture `packed` failed with three rules where it expected one: `[type_name] __attribute__: type names are lower_snake_case_t`. `typedef struct ... } __attribute__((packed)) world_t;` made the lint read the attribute as the type's name. Fixed in `rules_close_tagged_type` by skipping attributes.
- The first `struct lint_graph_edge_t { ... }` in a `.c` file, with the typedef in the header, would have broken `struct_no_typedef`. Moved both to the header before the first build.

**7. Wiring outside this stage's owned paths, all of it small.**
- `CMakeLists.txt`: one `include(cmake/standards_lint.cmake)`.
- `cmake/standards_lint.cmake`, new: runs the lint through `mc_prebuild`, gates the tools and tests on it, runs the recursion check, and registers ctest `standards_lint`. Added to `plans/REPO_STRUCTURE.md`.
- `cmake/flags.cmake`: `-Wframe-larger-than=` and `-fcallgraph-info=su`.
- `.gitignore`: `build/` became `/build/`. The old pattern matched a directory named `build` at any depth, which would have silently ignored the `ok_skipped_directories` fixture and any future `run/src/.../build/` module. `build/debug` at the root is still ignored.
- The workflow task asked for a ctest registration helper in `cmake/` that discovers `tests/**/index.c`. Stage 02.1 already wrote it: `mc_add_test` and the glob in `cmake/targets.cmake`. A second one would be a duplicate, so I wrote none, and proved the existing one with a probe test (see the table above).

**8. The lint is stricter than the banned list, and other in-flight stages will meet it.**
- Beyond section 10's table I implemented rules the standard states but that table does not list: `struct_no_sizeof_assert`, `bool_in_struct`, `struct_no_typedef`, `type_name`, `init_deinit_pair`, `goto_cleanup_only`, the eight README fields from `plans/REPO_STRUCTURE.md` section 3, a README tier that must match the path's depth, the NASM required form, and C++ words in `.cu` files.
- Stages 02.2 and 02.3 are running in parallel, with no commits when I looked. Their code may breach these when it first builds on top of this, and the build will say where. That is the lint working, but those stages have to allow for it.
- Choices the standard leaves open: `mmap` is allowed at `compile/src/<tool>/index.c`, because a compile tool may not include `run/` and so has no `heap_reserve`. `profiler`, which section 11's diagram omits, shares the `job` row. `cuda/` may include any `run/` header at or below `gpu_bridge`. `mc_compile_lib` and `mc_tool_standards_lint` compile before the lint runs, because the lint is built from them; a breach in them fails one step later.
- Limits are listed under "What it does not check" in `compile/src/standards_lint/README.md`: tokens and not types, no recursion through function pointers, and the CUDA recursion check is a token scan.
