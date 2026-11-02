# 001-ahmed-bohemoth-minecraft-from-scratch — phase 7, stage 3: World map and edits

**Status:** done   (updated 2026-11-02)

## Goal

A toroidal column map, streaming by distance, edit log, dirty flags and snapshots exist.

**Wave:** W5. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-07-world-data.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/world/map/`
- `run/{src,include}/world/edit/`
- `tests/run/world/map/`
- `tests/run/world/edit/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement the toroidal column map and distance-based streaming in `run/src/world/map/index.c`
- [x] Implement the edit log and dirty flags in `run/src/world/edit/index.c`
- [x] Implement snapshots for publishing state to the renderer
- [x] Add random-edit tests, registered as `run_world_map` and `run_world_edit`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_map$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_edit$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Wiring outside the owned paths

Each the smallest I could make:

- `run/{src,include}/world/index.*`, `run/src/world/README.md`: `world_open`, `world_close` and `world_open_bytes`, with `map`, `edit` and `snapshot` in `world_t`, which goes from 320 to 1088 bytes. The column region now also holds their records: 1320 bytes a slot, not 1160, plus 37.1 MiB. `world_init` refuses a limit of no columns itself, since the region is no longer 0 bytes for it.
- `run/include/main/index.h`: the `app_t` size assert, 2560 to 3328. Any stage merged alongside that changes `app_t` moves this number too.
- `tests/run/world/column/index.c`: the size assert of `column_test_t`, 1536 to 2304, which holds a `world_t`.
- `plans/REPO_STRUCTURE.md`: the two test rows.

Nothing calls `world_open` in the running game until `game` does (Stage 08.4). So `minecraft_c` only reserves more address space, and only tests exercise this stage's code.

## Measured

`snapshot_cost_on_this_machine`, one run on a shared machine, so a rough figure. Nothing asserts it. Each is an average of 50 publishes.

| Table radius | Columns | `world_open`, release | Publish, 1 section: debug / release | Publish, 8 sections: debug / release |
|---|---|---|---|---|
| 32 | 3209 | 0.07 ms | 0.048 / 0.020 ms | 0.157 / 0.036 ms |
| 128 | 51433 | 0.42 ms | 0.550 / 0.239 ms | 0.650 / 0.264 ms |

**Not measured:** radius 512. A publish clears and writes its table whole, so it should grow with the table, but no number was taken. See issue 4.

## Issues found

All 2026-11-02, by this stage.

1. **Files were already in this worktree when I started.** Expected: an empty stage, since `git status` on the merged integration branch should be clean. Actual: untracked `run/{src,include}/world/{map,edit}/index.*`, all written within about 50 ms at 07:04:06, with no READMEs, no tests and no snapshot code. Evidence: `stat`, and no process had its cwd here but my own shell. I could not find what wrote them, and an earlier attempt at this stage is my guess, not something I established. Done: since they are in my owned paths, I took them as a draft, and read every line before keeping any. Kept with changes, and every behaviour below is tested as if I had written it. Faults in the draft: `world_edit_dirty_restore` was declared in the header and defined as a different `_restore_for`, with no consumer; one `-Wconversion` error; `world_map_init` did not check its center as `world_map_set_center` does; `world_edit_column_revision` and `_top_z` took any slot.
2. **A plain triple buffer would lose sections.** Expected, from the plan: a snapshot is the newest state, and a skipped one does not matter. Actual: true of a table, not of sections, because a view holds only what was dirty since the last publish, and taking its flags means the skipped view's sections are never sent. Done: a view replaced unread gives its sections' flags back, so a section can arrive late and newer but cannot be lost. `a_view_nobody_read_gives_its_sections_back` and both mirror tests hold this. The `unread sections lost` mutant is caught.
3. **`top_z` can be too high, on purpose.** It is never below the true top. A section in direct form has no counts, so it does not know it has gone back to all air until `section_compact`. For sky rays that costs a little speed and is never wrong. The random test checks both halves: never below the truth, and above it only for a direct section.
4. **A publish costs in proportion to the table, not to what changed.** Measured above. At the default render distance it is nothing. Left as it is, since a smaller table comes from asking `world_open` for a smaller radius. If radius 512 turns out to matter, Stage 17.3 is the place to make this incremental, per view.
5. **My test numbers were wrong, caught by the tests' own guards.**
    - `changed > 100000` failed at about 82000: only 5 of the 16 chunks those coordinates reach are loaded. Lowered to 50000 with that written beside it.
    - The random walk first made 129 commits against 1989 out of date, so it was barely testing a live commit: requests piled up faster than they returned, and returns were picked evenly from a heap of stale ones. Fixed the test's mix to favour recent requests. Now 1022 commits, 817 out of date, 916 unloads. No product code changed for either.
6. **Tests checked by breaking things on purpose.** A one-off script, not committed, each run built and tested through the lock. 74 changes to `map`, `edit` and the Owner. First run: 65 caught; 4 did not compile and 1 matched twice, which were my own bad mutants and were rewritten; 4 survived:
    - `unload not a change`: a real gap in the tests. An unload with nothing else changed must still reach the renderer. Added `a_column_unloaded_with_nothing_else_changed_is_published`.
    - `restore on a gone column`: a gap. The state check saves a pointless queue entry, which nothing checked. `a_column_that_goes_takes_its_flags_with_it` now does.
    - `leaving flag stays`: dead code, as `world_map_stream_take` resets it. Removed from `world_map_unload`.
    - `deinit does not close`: dead code, as nothing in a world outlives its regions. Removed `world_close` from `world_deinit`.

    Second run, of the 5 rewritten and the 2 test gaps: 7 of 7 caught. Final count: 72 caught, 2 removed as dead. The mutants are hand-picked, so this is good evidence and not a proof.
7. **A failure that was not real, caught before it was believed.** After the mutation pass, all of `ctest --preset debug` failed `run_world_edit`, while sanitize and release passed. Cause: a compound command with the rebuild in it had been refused (see 9), so debug still held the last mutant's objects. After a rebuild it passed, and `ctest --repeat until-fail:25` on it passed, which also speaks against flakiness in the thread test, though it cannot rule that out.
8. **Races, beyond the Verification list.** The sanitize preset has no thread checker. So I built `run_world_edit` by hand with `-fsanitize=thread`, from the module's sources, in `build/tsan_edit/`, not added to CMake. The real reader-thread test: 0 reports. As a check on the checker, a copy with both `memory_order_acq_rel` exchanges weakened to relaxed: 10 data-race reports, and that test still passed, which shows the test alone would not have seen it. This is one machine, and x86's strong ordering hides some faults from a run, though not from the checker's model. **Not added to ctest**, so nothing guards this from here on.
9. **Beyond the Verification list, also run, on the final code.** All of `ctest --preset debug`, 47 of 47, including `clang_format` and `run_types`. `run_world_map`, `run_world_edit` and `run_world_column` in `sanitize`, and the first two in `release`, with `minecraft_c` also building in release. One `minecraft_c --headless --ticks 2` in debug, on a build taken just before the last comment and dead-code changes: 6 reserved, 6 released, 0 live. Only compute on the CPU: no GPU, display or input device was touched. The reader test does pin one thread to the `gpu` role's CPU for about a second, by `thread_start`.
10. **Budget.** The column region grows from 1.14 GiB to 1.33 GiB of reservation. Stage 07.2's issue 5 said a later system reserving over about 8.8 GiB would fail: it is now about 8.67 GiB. `static_assert` in `run/include/world/index.h` and `the_default_limits_hold_a_table_of_the_largest_render_distance` keep the world's own share inside the budget.
11. **Left for later stages.** Nothing here has been used by a real consumer. `render/upload` (Stage 08.2) is the first reader of a view, and may find it wants something else in one. Terrain on workers must still bring results to the map's thread: `world_map_stream_commit`, like the section pool, is one thread at a time.
12. **Process.** The worktree isolation check refused several compound shell commands, one of them for a stray argument of mine. I re-ran them as plain separate commands in this worktree, and did scripted edits with Python files in the scratchpad. All builds and tests in this stage went through `tools/test-or-build.sh`. `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"` print nothing.
