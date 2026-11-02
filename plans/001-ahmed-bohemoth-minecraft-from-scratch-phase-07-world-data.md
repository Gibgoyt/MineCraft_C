# 001-ahmed-bohemoth-minecraft-from-scratch — phase 7: World data

**Status:** done   (updated 2026-11-02)

## Goal

Blocks are defined in readable YAML, sections and columns store them compactly, and the world map streams by distance within the `z = -250 ... +500` range.

**Wave:** W5.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-stage-1-block-tables.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-stage-2-section-column.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-stage-3-map-edit.md`

## Todo

- [x] Stage 07.1: Block tables
- [x] Stage 07.2: Sections and columns
- [x] Stage 07.3: World map and edits
- [x] Phase gate passed

## Verification

**Gate:** Random-edit tests vs a brute-force array; `z=-251` and `z=501` rejected; memory per section measured.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(compile_game_tables|run_world)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Gate results

Run by the gate checker on 2026-11-02, on the integration branch merged in, from `rm -rf build`:

- `cmake --build --preset debug`: builds, with `standards_lint` and the recursion check in it.
- `ctest --preset debug -R '^(compile_game_tables|run_world)'`: 5 of 5.
- All of `ctest --preset debug`: 47 of 47, including `standards_lint`, `clang_format` and `run_types`.
- `sanitize`: `compile_game_tables`, `run_world_*` and `run_texture`, 6 of 6. `release`: `compile_game_tables` and `run_world_*`, 5 of 5, with `minecraft_c` building.
- `minecraft_c --headless --ticks 2`, under `timeout`: `heap at exit: 6 reserved, 6 released, 0 bytes live`.
- `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"`: nothing of mine.

**Random edits against a brute-force array:** `run_world_section`, `run_world_column`, `run_world_map` and `run_world_edit`. **`z=-251` and `z=501` rejected:** at `column`, `map` and `edit`. **Memory per section measured:** the table in the Stage 07.2 file, asserted by a test.

**Nothing here awaits Ahmed.** Mining feel, which Stage 07.1 issue 11 mentions, belongs to Stage 12.3. Nothing in `minecraft_c` opens a world until Stage 08.4: see Stage 07.3 issue 17.

## Commits

Appended as they land:

- `e32ebcb` Stage 07.1.
- `0ab4b2c` Stage 07.2.
- `2e2074a` Stage 07.3.
- `7cea60d` Check the Phase 7 gate: make north plus y as the plan's axes say, name borrowed pointers as the standard asks, test flags that overrule a shape and block textures against the blob, and stop a deleted texture staying in the blob.

## Issues found

All 2026-11-02, by the gate checker, which started out assuming that the stages were not finished. Details are in each stage file.

1. **Faces: north was `-y`, against the plan's axes.** The one real product bug. No test could see it. Fixed, and held by a test: Stage 07.1 issue 15.
2. **Ownership prefixes missing on arena pointers,** in `game_tables` and two test files. The lint does not check struct fields. Fixed: Stage 07.1 issue 16 and Stage 07.3 issue 14.
3. **Test gaps.** `solid: false` and the like, found by a surviving mutant, and no check of the block tables against the blob at run time. Both now tested: Stage 07.1 issues 17 and 20.
4. **Things the stages had left because they were out of paths.** A deleted texture staying in the blob, and the yaml header not saying an error must start zeroed. Both fixed: Stage 07.1 issues 18 and 19.
5. **Mutants of my own:** 21 hand-picked, across `game_tables`, `section`, `column`, `map`, `edit` and the Owner. 20 caught at first, and the 21st after issue 3's test. A test's list of failures needed care: one mutant crashed `run_world_column` as `SEGFAULT`, which my script's filter did not list, and the rerun showed it was caught there too.
6. **No stubs, TODOs or empty tests found** in this phase's paths, by reading them and by search. The stages' measured numbers and their process notes were not all re-measured: the publish timings in Stage 07.3 are one machine's, and nothing asserts them.
7. **Left open, not a defect of this phase:** no thread-checking preset in ctest (Stage 07.3 issue 15), and `yaml_storage_t`'s bare pointers (Stage 07.1 issue 16).
8. **Process.** The worktree isolation check refused compound shell commands. I re-ran them as plain ones, and did scripted edits with Python files in the scratchpad. One edit script stopped partway, and I restored the one file it had written with `git checkout --` before rerunning it. The temporary probe texture and the bad field in dirt's yaml were each removed by the script that made them, and `git status` confirmed it.
