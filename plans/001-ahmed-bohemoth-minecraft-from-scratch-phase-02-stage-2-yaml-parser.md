# 001-ahmed-bohemoth-minecraft-from-scratch — phase 2, stage 2: YAML parser

**Status:** in progress   (updated 2026-11-02). The code is finished and verified. See issues 1 and 9 for what stands between it and done.

## Goal

Our own YAML subset parser exists, with line-numbered errors. Proof: parser tests and bad-input tests pass.

**Wave:** W2. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-02-build-skeleton.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/yaml/`
- `tests/compile/yaml/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement the parser in `compile/src/yaml/index.c` and `compile/include/yaml/index.h`, covering the subset `config/machine.yaml` uses, including inline maps
- [x] Report errors with line numbers
- [x] Add tests in `tests/compile/yaml/index.c`, registered as ctest `compile_yaml`, with valid and bad inputs. Registered by Stage 02.1's path discovery, so it needs no CMake edit; proven only on a copy of that stage's unlanded branch (issue 1)
- [x] Add `compile/src/yaml/README.md`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_yaml$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Results on 2026-11-02, run in the overlay described in issue 1:

- `cmake --build --preset debug`: clean, under the standard's flags including `-Wpadded -Werror`.
- `ctest -R '^compile_yaml$'`: passed on `debug`, `sanitize` (ASan and UBSan) and `release`. The test program prints `3278620 checks, 0 failures` on `debug` and `sanitize`.
- `pgrep -a minecraft_c`: printed nothing, rc 1.

**This has not yet been run in a checkout where `CMakeLists.txt` is on `main`.** Repeat the three commands above once Stage 02.1 has landed.

Results on 2026-11-02 from the second attempt, on the code with the fix in issue 8, in the same kind of overlay (Stage 02.1 at `2ac15d2`, since `main` was still `5bb5c57`):

- `cmake --build`: clean on `debug`, `sanitize`, `release` and `profile`, with no warning at any link.
- `ctest -R '^compile_yaml$'`: passed on all four presets. Run directly, the test program prints `3278213 checks, 0 failures` on `debug` and `sanitize`. The count differs from the first attempt's because of issue 8's new refusal and test cases.
- `pgrep -a minecraft_c`: printed nothing, rc 1.

## Commits

Appended as they land:

## Issues found

**1. `main` has no build system, so the stage's Verification could not run as written.**
- Expected: Stage 02.1 on `main`, giving `tools/test-or-build.sh`, the presets and test discovery after `git rebase main`.
- Actual: `main` is still `5bb5c57`, with no `CMakeLists.txt`. Stage 02.1 is verified on `worktree-wf_a4b549de-085-12` (`2ac15d2`), but its landing was refused (its issue 8).
- Done: exported that branch read-only with `git archive` into this worktree's `build/s021/`, copied my `compile/` and `tests/` over it, and ran the stage's commands there using its own wrapper, so the shared build lock was held. My branch stays based on `main` and carries none of Stage 02.1's commits. Its `cmake/targets.cmake` found `compile/src/yaml/index.c` as `mc_compile_lib` and `tests/compile/yaml/index.c` as ctest `compile_yaml` with no edit. Nothing in this stage touches a file Stage 02.1 owns, so landing order does not matter.

**2. Nothing in the standard says where a `compile/` library gets its memory, and `run/src/heap/` is off limits to it.**
- Expected: "one process arena" for compile tools, per `plans/CODING_STANDARD.md` section 4.
- Actual: `compile/` may not include `run/` other than `run/include/types/`, so `heap_reserve` and `arena_t` are out of reach, and no compile-side arena exists yet. This library also has to be usable from the configure-time tool build in Stage 02.3.
- Done: `yaml_parse` allocates nothing. The caller passes node and string storage (`yaml_storage_t`), and running out is an ordinary line-numbered error. Text length + 1 nodes and text length string bytes can never run out, and a test covers the densest shapes. The tool that owns the process arena provides the storage, and may simply use its text length to size it.

**3. `float64_t` does not exist yet, so no floating-point read was written.**
- Expected: `hardness: 0.5` in the dirt example to need a floating-point value.
- Actual: `run/include/types/index.h` belongs to Stage 03.1, which has not started, and the standard allows a floating-point base type only in that file. `strtod` would also bring locale dependence and rounding into generated tables.
- Done: `yaml_get_decimal` returns an exact `mantissa * 10^exponent`, so `0.5` is `5, -1`. Stage 07.1 can emit a suffixed literal from it with no rounding. This is a choice the plan did not spell out, and it is easy to add a `float64_t` reader beside it later if a tool really needs one.

**4. `tests/harness/` does not exist yet.**
- Expected: Stage 02.4 to provide the test framework.
- Actual: not landed, and its API is unknown.
- Done: `tests/compile/yaml/index.c` has a small `CHECK` macro of its own and its own `main`. **If Stage 02.4's harness defines `main`, this test will fail to link and must move to the harness.** `cmake/targets.cmake` links `tests/harness/index.c` into every test once it exists.

**5. Two mistakes of mine, found by the tests and the build.**
- A wrong expected line in the test: `yaml_seq_at(&d, test_at(&d, "h"), 1u)->line == 12u` failed on the first run. Counting the text by hand gave line 10, and the parser was right. I corrected the test.
- A test that kept going after a failed bound check. The first `release` build printed `warning: 'memcpy' writing between 2147483648 and 9223372036854775805 bytes into a region of size 8192` at the LTO link. `CHECK(length < sizeof(buffer))` only counts a failure, so the copy after it was reachable. GCC was right, and the warning did not fail the build because the link does not get `-Werror`. The test now returns at that point, and the warning is gone. **Compiler warnings at the LTO link do not fail the `release` build**, which is outside this stage's paths, so Stage 02.1's owner may want `-Werror` on the link as well.

**6. The landing command is refused, so this work is not on `main`.**
- Expected: the workflow's landing step to fast-forward `main`.
- Actual: `flock /tmp/mc_c_land.lock sh -c 'git rebase main && git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only '"$(git rev-parse --abbrev-ref HEAD)"` returned `this command names git in a form too complex to verify that it stays inside the worktree. Refusing to run it — a worktree-isolated agent's git operations must target its own worktree.` A plain read with `git -C` on the main checkout was refused as well: `this command redirects git to the shared checkout via -C`. `main` stays at `5bb5c57`. Stages 01.x and 02.1 report the same block.
- Done: tried once, and escalated. I did not try another way to move `main` (`update-ref`, `branch -f`, `cd` into the main checkout), because that would bypass the isolation rule. To finish, from outside the isolation, in the main checkout, after Stage 02.1 if it goes first: `git rebase main worktree-wf_a4b549de-085-13 && git checkout main && git merge --ff-only worktree-wf_a4b549de-085-13`. Then add the landed sha above and set Status to done.

**7. Cross-check against PyYAML, a one-off that is not part of the repo.**
- Why: my own tests only show that the parser agrees with what I believed YAML means.
- How: a throwaway driver dumped the parser's tree for 104,227 documents: 15 valid ones, plus every truncation, deletion, overwrite and insertion of 28 confusing bytes at every position. The trees were compared with `yaml.load(..., Loader=yaml.BaseLoader)` from PyYAML 6.0.3, run only as a local comparison. Nothing from it is linked or shipped.
- Result: this parser accepted 57,681 documents. PyYAML also accepted 53,325 of those, and 53,130 trees were identical.
- The 195 that differ all contain `?` or a tab. The samples I read were a plain scalar starting with `?` inside a flow collection, such as `{?os: linux}`. YAML 1.2 makes that the plain text `?os`, which is what this parser gives. PyYAML drops the `?`. I filtered on `?` and tabs and read samples rather than all 195, and with those filtered out there are 0 differences.
- The 4,356 that only this parser accepted: 3,036 contain a tab, and PyYAML is known to be strict about tabs inside plain scalars and after a colon. I did not classify those further. The other 1,320 are all `?` or `:` inside flow collections (`[1?, 2]`, `{ :main: 1 }`), plain text in YAML 1.2 and refused by PyYAML's 1.1 rules.
- 7,074 documents were refused here and accepted by PyYAML. That is the subset's job (duplicate keys, multi-line scalars and so on). I did not check them one by one, but a refusal cannot misread a value.

**8. A tab after a dash was counted as one column, which could silently misread a nested mapping.** Found while reviewing the first attempt's code on the second attempt.
- Expected: the README's promise that the parser refuses rather than guesses, and that indentation is spaces only.
- Actual: `-\ta: 1\n  b: 2\n` was accepted, because the tab took one column in `yaml_parse_block_line` and so `a` and `b` lined up. A tab has no defined width. With the new check disabled in the overlay, the new tests print `FAIL bad input 18: expected 1:3 'a tab may not separate', got ok=1 0:0 ''` and the same for input 19, then `3278625 checks, 2 failures`, rc 1. This confirms the old acceptance and that the tests catch it.
- Done: fixed. A tab between a dash and a value on the same line is now the error `a tab may not separate '-' from its value`, at the value's position. `-\t# c` with no value stays valid. Two bad inputs and one valid input were added to the test, and the README's refused list says so. I did not re-run the PyYAML comparison, so the counts in issue 7 describe the code before this fix, and its 3,036 tab documents remain unclassified.

**9. The landing command was refused again on the second attempt, so this work is still not on `main`.**
- Expected: the workflow's landing step to fast-forward `main`.
- Actual: the same command as issue 6, from branch `worktree-wf_a4b549de-085-15`, returned the same refusal: `this command names git in a form too complex to verify that it stays inside the worktree`. `main` is still `5bb5c57`, and the Stage 02.1 branch `worktree-wf_a4b549de-085-12` is unlanded too. This branch replaces `worktree-wf_a4b549de-085-13`: it holds that branch's two commits, cherry-picked, and the fix in issue 8.
- Done: tried once and escalated. As in issue 6, I did not look for another way to move `main`, because that would get around the isolation. To finish, from a shell outside the isolation, in the main checkout, after Stage 02.1 if it goes first: `git rebase main worktree-wf_a4b549de-085-15 && git checkout main && git merge --ff-only worktree-wf_a4b549de-085-15`. Then run the Verification commands there, add the landed sha under Commits, and set Status to done.
