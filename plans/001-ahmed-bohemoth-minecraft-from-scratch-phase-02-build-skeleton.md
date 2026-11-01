# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2: Build skeleton

**Status:** blocked: nothing from this phase is on `main`, which is still `5bb5c57`. The gate passes on a tree built from the unlanded branches, but the gate is about `main`, and only a landing from outside the worktree isolation can put it there (issue 1)   (updated 2026-11-02)

## Goal

CMake builds all three languages on every preset, `config/machine.yaml` drives compile-time defines through our own YAML parser, and the standards lint fails the build on any breach.

**Wave:** W2.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-1-cmake-mutex-hello.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-2-yaml-parser.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-3-machine-config.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-stage-4-standards-lint-harness.md`

## Todo

Left unticked on purpose: a tick here would say this is on `main`, and it is not. What each stage has actually been shown to do is under Verification.

- [ ] Stage 02.1: CMake, mutex and hello binary
- [ ] Stage 02.2: YAML parser
- [ ] Stage 02.3: Machine config generator
- [ ] Stage 02.4: Standards lint and test harness
- [ ] Phase gate passed

## Verification

**Gate:** `ctest` is green; lint rejects a 2001-line file, `int`, padding, and a missing README.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug --output-on-failure

### Gate check, 2026-11-02

Run by the skeptical gate checker on branch `worktree-wf_a4b549de-085-19`. **On `main` the gate cannot run at all:** `main` has `plans/` and `research/` and no `CMakeLists.txt`.

So it ran on an overlay, a throwaway copy under this worktree's `build/` holding this branch (Stages 02.1 and 02.4 plus the fixes below), the YAML paths of `worktree-wf_a4b549de-085-15` at `16be95a`, and the machine paths of `worktree-wf_a4b549de-085-18` at `fd20b5c`. The tree that the landing commands in issue 1 will produce was compared with it; see the note under that issue.

| Check | Result |
|---|---|
| Clean `cmake --preset`, `cmake --build --preset`, `ctest --preset` on `debug`, `sanitize`, `release`, `profile`, all through the wrapper from a deleted preset directory | rc 0 on all three steps on all four. `100% tests passed out of 7` on each |
| `minecraft_c` on all four | rc 0, prints `MC_THREADS_WORKERS=16` and `nasm=524800 cuda=524800 expected=524800`. It ran a real kernel on the GPU; compute only, a few KiB |
| `NEEDED` | `libm libc ld-linux` on debug, release and profile; `libasan libm libubsan libc` on sanitize. No C++ runtime |
| `standards_lint` | `clean, 32 source files` on all four; `no recursion` on debug and sanitize |
| Gate: `int` in a real module | build rc 1 at `mc_standards_lint`: `run/src/world/index.c:1: [banned:int]`, `ninja: build stopped` |
| Gate: 2001 lines | build rc 1: `[line_limit] 2001 lines; the limit is 2000` |
| Gate: padding | build rc 1: `error: padding struct to align 'b' [-Werror=padded]` |
| Gate: missing README | build rc 1: `run/src/world/README.md: [readme_missing]` |
| Stage 02.3's proof, on the real binary | `threads.workers` 16 to 12, only `cmake --build`: `Re-running CMake`, then `minecraft_c` prints `MC_THREADS_WORKERS=12`. Restored, prints 16, `cmp` of the yaml silent |
| `gpu.architecture` reaches nvcc | `sm_80` gives `arch=compute_80` in `build.ninja`; `tu104` stops the configure with a line naming the key; `sm_75` gives `arch=compute_75` |
| `.inc` and `.hu` are usable, not just written | A probe `index.asm` with `%include "machine/index.inc"` and `%if MC_THREADS_WORKERS != 16` / `%error`, and a probe `index.cu` with `#include "machine/index.hu"` and `static_assert(MC_THREADS_WORKERS == 16)`: both compile and link. Probes removed |
| Frame limit on `release` and `profile` | A called function with a 20000-byte local a `read` writes to: `error: the frame size of 20016 bytes is larger than 16384 bytes`, build rc 1 on each. My first probe filled the array with `memset` and passed, because the optimizer removed the array. That was the probe's fault, and it is why the second one uses a syscall |
| The tests can fail | One break at a time: YAML error line off by one, duplicate keys accepted, wrong `#define` prefix, lint limit moved to 2001, banned check skipped for quoted includes, `pragma_other` off: each ctest returned 8 (the `#define` one also failed the build). The last break, the harness not counting failures, is issue 2 |
| Stubs and placeholders | `grep` for `TODO`, `FIXME`, `XXX`, `stub`, `not implemented`, `placeholder` outside `plans/`, `research/` and fixtures: nothing |
| Modules nothing calls | None found. The YAML parser is called by `machine_config`; the generated header by `machine_config_report` and now `minecraft_c`; the lint by the build; the harness by the lint's tests |
| No process left | `pgrep -a minecraft_c` rc 1 |

**Not run on the real `main`, because it has nothing to build.** **Not tested:** other machines. `release` and `profile` have no recursion check, as Stage 02.4 issue 4 says.

## Commits

Appended as they land:

None landed. Gate fixes are on branch `worktree-wf_a4b549de-085-19`.

## Issues found

**1. Nothing from this phase is on `main`, and the gate checker cannot put it there.**
- Expected: stages landed, so that the gate runs on `main` after `git rebase main`.
- Actual: `git rev-parse main` is `5bb5c57`. Every stage reports a refused landing. The workflow's landing command returns `this command names git in a form too complex to verify that it stays inside the worktree`, and a plain read with `git -C` on the main checkout is refused as well.
- Integration was refused too. In this worktree `git merge --ff-only worktree-wf_a4b549de-085-14` worked, which brought Stages 02.1 and 02.4. Then `git merge --no-ff worktree-wf_a4b549de-085-15` and the same for `-18` were each denied: `Permission for this action was denied by the Claude Code auto mode classifier. Reason: [Modify Shared Resources]`.
- Done: escalated. I did not retry, and did not bring those commits in another way (`cherry-pick`, copying their files in and committing) or move `main` by another route (`update-ref`, `branch -f`, `cd` into the main checkout), because each of those would get around the refusal rather than resolve it. I read the two branches with `git archive`, into the overlay only.
- **To finish, from a shell outside the isolation, in the main checkout:**

      git merge --ff-only worktree-wf_a4b549de-085-19
      git merge --no-edit worktree-wf_a4b549de-085-15
      git merge --no-edit worktree-wf_a4b549de-085-18
      clang-format -i tests/compile/machine_config/index.c
      git add tests/compile/machine_config/index.c
      git commit -m "Format the machine config test."
      tools/test-or-build.sh cmake --preset debug
      tools/test-or-build.sh cmake --build --preset debug
      tools/test-or-build.sh timeout 300 ctest --preset debug --output-on-failure

  Then tick this file and set Status to done, and add the landed shas under Commits here and in the stage files.
- **Do not merge `worktree-wf_a4b549de-085-16`.** Its one commit changes only the Stage 02.4 file and will conflict with this branch's version. What it recorded is closed by issue 9 in the Stage 02.4 file on this branch.
- The Stage 02.2 and 02.3 files on this branch still read `not started`, because their real versions are on their branches. Editing them here would make the merges above conflict. So what the gate found in those two stages is logged here, in issues 3 to 7, and they come in with the merge.

**2. The gate found the lint could be walked past, and the self-test of the harness could not fail.** The detail and fixes are in the Stage 02.4 file on this branch, issues 9 and 10. Its fixtures and tests pass, and 200 rules each have a failing fixture.

**3. Stage 02.3: the stage's proof named the binary, but `minecraft_c` reported nothing.** Its todo was ticked and its issue 2 said so openly.
- Expected: "changing a yaml value changes the binary's reported define".
- Actual: only `machine_config_report` printed it.
- Fixed in `cmake/hello/main/index.c`, which is on this branch: `minecraft_c` prints `MC_THREADS_WORKERS`. `__has_include` keeps it building if this branch lands alone, which I built and tested. See the table above for the 16 to 12 run.

**4. Stage 02.3: `gpu.architecture` changed the generated header and not the code.**
- Expected: `config/machine.yaml` to drive the build, per the phase goal.
- Actual: `cmake/flags.cmake` hard-coded `CMAKE_CUDA_ARCHITECTURES 75`. The stage's issue 4 said so and left it, because the file was on another branch.
- Fixed in `cmake/flags.cmake`, with the evidence in the table above.

**5. Stage 02.3: `tests/compile/machine_config/index.c` does not match `.clang-format`.** Left for the commands in issue 1.
- Expected: `plans/CODING_STANDARD.md` section 6 says clang-format runs from the repo's config.
- Actual: `clang-format --dry-run --Werror` reports 37 errors in that file, and none in any other C file of the phase. Nothing runs clang-format in the build, so nothing fails, and section 10's table lists no enforcement.
- Not fixed here, because the file is not on this branch. Formatting is the one edit in issue 1's commands.

**6. Stage 02.3: F16C, a conflict between the plan and the standard, for Ahmed.**
- `plans/CODING_STANDARD.md` section 7 lists F16C in the baseline and says each feature has an `MC_CPU_HAS_*` define. The yaml in plan section 5, which the stage must copy exactly, has no `f16c` key. So `MC_CPU_HAS_F16C` does not exist. The stage's issue 4 reports this too. Left alone, since the choice is Ahmed's: one yaml key and one table row, or one word out of the standard.

**7. Stages 02.2 and 02.3: small things left as they are.**
- `compile_yaml` and `compile_machine_config` have their own `main` and `CHECK` rather than `tests/harness/`. They work, and mutation shows they fail when they should (table above), so this is duplication and not a false pass.
- `yaml_scan_escaped_quoted` needs `-O0` at the configure-time compile because of a false `-Wmaybe-uninitialized` at `-O1`; Stage 02.3 issue 3 read the code and showed it false. Initializing `decoded` to `nullptr` would let that compile match the rest.
- `gpu.enabled`, `display.*`, `game.*` and `world.*` have no consumer yet. Their stages come later.
