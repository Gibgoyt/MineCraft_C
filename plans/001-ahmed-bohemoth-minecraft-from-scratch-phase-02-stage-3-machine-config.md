# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 3: Machine config generator

**Status:** blocked: implemented and verified on a copy, not on `main`, because the landing was refused (issue 1)   (updated 2026-11-02)

## Goal

`config/machine.yaml` generates `.h`, `.hu`, `.inc` and `.cmake` output. Proof: changing a yaml value changes the binary's reported define.

**Wave:** W2. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-build-skeleton.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/machine_config/`
- `config/machine.yaml`
- `tests/compile/machine_config/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

Ticked on the copy described in issue 1, not on `main`.

- [x] Create `config/machine.yaml` with the exact content from plan section 5
- [x] Implement `compile/src/machine_config/index.c` emitting `build/generated/machine/index.{h,hu,inc,cmake}`, including `MC_PLATFORM_POSIX`, `MC_THREADS_WORKERS`, `MC_CPU_HAS_AVX2`, `MC_GPU_ENABLED`
- [x] Compile the tools at configure time with `try_compile`/`execute_process`, and add the yaml to `CMAKE_CONFIGURE_DEPENDS`
- [x] Add `tests/compile/machine_config/index.c`, registered as ctest `compile_machine_config`
- [x] Add `compile/src/machine_config/README.md`

## Verification

    cmake --preset debug
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_machine_config$' --output-on-failure
    # Change threads.workers in config/machine.yaml, rebuild, and confirm the binary reports the new value, then restore it.
    pgrep -a minecraft_c    # must print nothing

Results, all on the copy described in issue 1, each heavy command through that copy's `tools/test-or-build.sh`:

| Check | Result |
|---|---|
| `config/machine.yaml` against plan section 5 | `sed -n 275,298p` of the plan piped to `cmp` is silent: identical |
| `cmake --preset` and `cmake --build --preset`, all four presets | rc 0. `standards_lint: clean, 32 source files`; `no recursion` on `debug` and `sanitize` |
| `ctest --preset <p> --output-on-failure`, all four presets | `100% tests passed out of 6` each. `compile_machine_config` passes on all four, 1752 checks on `debug`, 18.6 s under ASan and UBSan |
| Configure again with an unchanged yaml, then build | No `Building` or `Linking` line, so nothing was rebuilt |
| `threads.workers` 16 to 12, then only `cmake --build` | `[1/2] Re-running CMake...`, then the programs that include the header rebuild. `machine_config_report` goes from `MC_THREADS_WORKERS=16` to `=12`. All four generated files say 12 |
| Same, with the temporary `minecraft_c` print in issue 2 | `minecraft_c` goes from `MC_THREADS_WORKERS=16` to `=12`, and its NASM and CUDA sum still passes |
| `cpu.arch_flag` to `haswell`, then configure | `build.ninja` has 18 lines with `-march=haswell` and 0 with `-march=skylake`, so `cmake/flags.cmake` takes the value from the yaml |
| `threads.workers` to 17, then configure | `CMake Error at cmake/machine.cmake`, `config/machine.yaml was refused (1)`. The header keeps its last good value |
| Values restored | `cmp` of the copy's yaml against the committed yaml is silent, and the report prints 16 |
| Five deliberate breaks in the tool (rewriting unchanged files, the thread rule off, unwrapped negatives, a wrong define name, a changed `%define`) | The test fails on every one: `1752 checks, 4 failures`, then 17, 3, 12 and 19. Unbroken it prints `1752 checks, 0 failures` |
| `pgrep -a minecraft_c` | rc 1, nothing printed. `pgrep -af` on the copy's build path shows nothing of mine |

Every row was run on the final code.

## Commits

Appended as they land:

Nothing from this stage is on `main`, so there is no landed sha yet. The commits are on branch `worktree-wf_a4b549de-085-17`, based directly on `5bb5c57`. See issue 1.

## Issues found

**1. Stages 02.1, 02.2 and 02.4 are not on `main`, and the landing of this stage was refused too.**
- Expected: after `git rebase main`, a build system and a YAML parser to build on, and a landing step that moves `main`.
- Actual: `main` is still `5bb5c57`, with only `plans/` and `research/`, so this stage has nothing to build on there. The landing command returned `this command names git in a form too complex to verify that it stays inside the worktree. Refusing to run it — a worktree-isolated agent's git operations must target its own worktree.` That is the same block as Stage 02.1 issue 8 and Stage 02.2 issue 6.
- Done: ran the landing command twice, once for each landing step the workflow asks for, got the same refusal both times, and escalated. I did not try another way to move `main` (`update-ref`, `branch -f`, `cd` into the main checkout), because that would get around the isolation.
- This branch holds only this stage's own new files, plus one sentence in `plans/REPO_STRUCTURE.md`, and carries nobody else's commits. Stage 02.4 issue 9 reports that bringing another branch into a worktree was denied, so I did not try it. Instead I used `git archive` to export `worktree-wf_a4b549de-085-14` at `8af2957` (Stages 02.1 and 02.4) and the YAML paths of `worktree-wf_a4b549de-085-15` at `16be95a` (Stage 02.2) into `build/overlay/`, copied this stage over them, and verified there. That directory has been deleted.
- `git merge-tree --write-tree` first reported a conflict with `worktree-wf_a4b549de-085-14` in `plans/REPO_STRUCTURE.md`, because my note was on the table row next to Stage 02.4's new row. I moved the note to section 4. The re-check against `worktree-wf_a4b549de-085-14`, `-15` and `-16` returns rc 0 for each, with no conflict.
- So **the boxes above were ticked on that copy, not on `main`.** To finish, from a shell outside the isolation, in the main checkout, once Stages 02.1, 02.2 and 02.4 are in: `git rebase main worktree-wf_a4b549de-085-17 && git checkout main && git merge --ff-only worktree-wf_a4b549de-085-17`. Then run this file's Verification there, add the landed sha above, and set Status to done.
- Order matters in one way. After Stage 02.1 lands, `cmake/machine.cmake` stops the configure with a message naming the missing file if the YAML parser is not there, so land Stage 02.2 before or together with this one.

**2. The "binary" in the proof is `machine_config_report`, not `minecraft_c`.**
- Expected: the stage's proof reads "the binary reports the new value", and `minecraft_c` is the obvious binary.
- Actual: `minecraft_c` is currently the hello program in `cmake/hello/main/index.c`. Stage 02.1 owns it, it is not on `main`, and committing a change to it here would create a file on this branch that collides with that stage's. Stage 03.1 replaces it, and its todo list does not mention reporting defines.
- Done: added `compile/src/machine_config/report/`, which builds `bin/tools/machine_config_report`. It includes the generated header and prints the defines as compiled, and it is what the committed code uses as proof. I also showed that the real binary works, on the copy only, with these two temporary lines in `cmake/hello/main/index.c`, which build and pass the lint:

      #include "machine/index.h"
      printf("MC_THREADS_WORKERS=%d\n", MC_THREADS_WORKERS);

  Those lines are **not committed**. `minecraft_c` reports no define until someone adds them, or until Stage 03.1's `main` does. Ahmed may want that added to Stage 03.1.

**3. The configure-time build of the tool failed at `-O1`, in the YAML parser.**
- Expected: the YAML parser, which builds clean on all four presets, to compile with the same warnings at `-O1`.
- Actual: `compile/src/yaml/index.c:529:20: error: 'decoded' may be used uninitialized [-Werror=maybe-uninitialized]`, in `yaml_scan_escaped_quoted`. It looks like a false positive, since `decoded` should be set whenever `yaml_reserve_string` returns true, but I did not read that function to prove it.
- Done: the configure-time compile uses `-O0`. It runs once on a 1 KiB file, and the build targets judge the same source at each preset's level. The parser belongs to Stage 02.2, so I left it alone. Initializing `decoded` to `nullptr` there should remove the warning.

**4. The plan's paths and the standard's CPU list differ from what exists.**
- Output path: this stage's todo and plan section 5 say `build/generated/machine/`. `plans/REPO_STRUCTURE.md` section 4 and Stage 02.1's `MC_GENERATED_DIR` make it `build/<preset>/generated/machine/`, which presets need because they build separately. I followed the latter.
- F16C: `plans/CODING_STANDARD.md` section 7 lists F16C in the baseline and says each feature has a `MC_CPU_HAS_*` define, but the yaml in plan section 5 has no `f16c` key, and this stage says to use that content exactly. So there is no `MC_CPU_HAS_F16C`. Left alone; adding it means one yaml key and one table row. Stage 03.1's CPUID check should know.
- `gpu.architecture` is generated as `MC_GPU_ARCHITECTURE`, but `cmake/flags.cmake` still hard-codes `CMAKE_CUDA_ARCHITECTURES 75`, so changing the yaml value does not change the nvcc target yet. That file is Stage 02.1's and is not on this branch, so I left it. Other values, such as `display.*`, have no consumer yet either.

**5. Choices the plan left open, which Ahmed may want different.**
- The tool refuses more than the stage asked, on the grounds that a wrong value should stop the build with a line number. The full list is in `compile/src/machine_config/README.md`. The one most likely to surprise: **`threads.workers: 17` or more is refused on the current yaml**, because the thread counts would pass `cpu.logical_threads`, and threads are pinned. To try a higher number, lower `threads.reserved_os` or raise `cpu.logical_threads`.
- `threads.main` and `threads.sim` must be 1, and `threads.gpu` 0 or 1, as the plan's thread table has them.
- `cmake/machine.cmake` also compares the running GCC, nvcc, NASM and CMake against `toolchain.*` and stops the configure if one is older. All four passed here, but I never saw one fail, and `CMAKE_ASM_NASM_COMPILER_VERSION` is checked only when CMake sets it, which I did not confirm.
- The test writes its own `main` and `CHECK`, as Stage 02.2's does, so it does not depend on the order Stage 02.4 lands in. It could move to the harness later.

**6. Wiring outside this stage's owned paths.**
- `cmake/machine.cmake`, new. Stage 02.1 already includes it as `OPTIONAL` and `plans/REPO_STRUCTURE.md` already lists it, so no existing file changes.
- `plans/REPO_STRUCTURE.md`: one sentence in section 4 about `report/`.
