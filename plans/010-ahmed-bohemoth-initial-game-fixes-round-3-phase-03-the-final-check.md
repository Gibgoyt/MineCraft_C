# 010-ahmed-bohemoth-initial-game-fixes-round-3 — phase 3: The final check

**Status:** not started   (updated 2026-11-14)

Read first: `plans/010-ahmed-bohemoth-initial-game-fixes-round-3-phase-00-vision-and-plan-files.md`, then the master `plans/010-ahmed-bohemoth-initial-game-fixes-round-3.md` (sections 4, 5, 7, 9 and 10), then phases 01 and 02's Issues found.

## Goal

Plan 010 is closed:
- The full debug and release suites pass, apart from failures shown to fail on main itself, each named with its evidence.
- A release build is made.
- The version 3 pins are the same bytes on 1 worker and 16.
- `bench_terrain` is at or above 1000 columns a second at its slowest place, `bench_lod` at or under 40 ms a column, and the feller's find under 1 ms on the biggest tree, each recorded.
- The worlds in `saves/` are dealt with **exactly as Ahmed said for Q1**, and nothing else is deleted.
- Ahmed's gate notes are written here in his words.

## Where this fits

**The vision.** The two halves meet here: the land has its stumps (phase 01), and the feller cuts by layers and leaves them (phase 02). Ahmed checks both at once, in a new world.

**What this phase must NOT build:**
- No new behaviour. A fix found here is the smallest that makes a test pass, recorded in Issues found with its evidence.
- No change to any pin without a recorded reason.
- No deletion in `saves/` beyond what Ahmed names.
- Nothing of plan 011 (the farmer's workbench).

## Ahmed's words

- "So we are touching world generation to make trees have a tree stump as well as properly creating tree feller!"
- "Look at ~/Pictures/10.png in Hytale the tree only falls if i cut down an entire layer! and 11.png shows the tree stump, i can tell you that a little of that stump goes underground."

---

## Background, explained

### The old worlds

Phase 01 lists `saves/` with sizes, dates and each manifest's generator, and asks Q1. On 2026-11-14:
- nine generator 1 worlds (`test_1` to `test_8`, `test_8-2`), refused since version 2, waiting since plan 006 phase 05 Issues found 3;
- **`test_8-3`, generator 2, 58 MB**, which version 3 refuses.

**The order:**
1. List again, fresh, into Issues found.
2. Read Ahmed's answer as phase 01's Issues found records it, or ask the main thread for it.
3. Check no game has a save open (`pgrep -af minecraft_c`). If it is running, wait and say so; **never kill Ahmed's game**.
4. `rm -r --` exactly the named directories, from `/home/ahmed/Projects/Games/MineCraft_C/saves/`, and record what went.

`saves/` is ignored by git (`.gitignore`'s `/saves/`), so no tracked file changes. **If Ahmed has not answered, nothing is deleted** and the todo stays unticked with "waiting on Ahmed".

### 1 against 16 workers

As plan 006 phase 05: the same seed's chunks made by one worker and by sixteen give the same bytes, for the land `FEA_DIGEST_OF_GENERATOR[3]` pins and for every pin phase 01 moved.

## Owned paths

This file, and only the smallest fixes a failing test needs (in the paths phases 01 and 02 owned), each recorded. `saves/` as Q1 says.

## Steps

1. **Rebase onto main**, if main moved, and record it. *Why: the suites must run on what will merge.*
2. **The debug suite, whole.** A failure is run alone. If it passes alone, it is load (as `run_climate_warning`'s 20 ms and `run_ui_atlas`'s lock wait were in plans 006 and 007); record both runs. If it fails on main too, show main's run and leave it for its owner. *Why: a closed plan says what passes.*
3. **A release build and the release suite.** *Why: the find's 1 ms and the benches are release numbers.*
4. **1 against 16 workers** on the version 3 pins. *Why: determinism (master section 4).*
5. **Benches:** `bench_terrain` (every place, the slowest named), `bench_lod`, and the find's time from `run_player_felling`'s release run. *Why: master section 4's budgets.*
6. **The old worlds**, as above. *Why: Q1.*
7. **Ahmed's gate**, then his notes here in his words. *Why: he decides when it is right.*

## Todo

From the master (section 9, phase 03):

- [ ] Full debug and release suites; a release build; 1 against 16 workers on the new pins
- [ ] `bench_terrain`, `bench_lod`, the find's time
- [ ] The old worlds: delete exactly what Ahmed names, nothing else
- [ ] Ahmed's gate notes, in his words

Expanded:

- [ ] `saves/` listed again, fresh, with generators, before anything is deleted
- [ ] Each failing test run alone and against main, recorded
- [ ] `plans/010-*` phase files' todos and statuses current; this plan's master left as it is (verbatim)
- [ ] `pgrep -af "$PWD/build"` and `pgrep -a Xvfb` print nothing at the end

## Tests

| Test | Checks | Numbers |
|---|---|---|
| Debug suite | Everything | All pass, or each failure shown as load or as main's |
| Release suite | Everything | The same |
| 1 against 16 | Version 3 pins | Same bytes |
| `bench_terrain` | Slowest place | ≥ 1000 columns/s |
| `bench_lod` | Per column | ≤ 40 ms |
| The find | Biggest tree of every kind, 100 cuts, release | < 1 ms |

## Verification

    git log --oneline main..HEAD
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 3600 ctest --preset release --output-on-failure -j 2
    tools/test-or-build.sh timeout 600 build/release/bin/bench_terrain
    tools/test-or-build.sh timeout 600 build/release/bin/bench_lod
    tools/test-or-build.sh timeout 600 ctest --preset release -R '^run_player_felling$' --output-on-failure
    du -sb /home/ahmed/Projects/Games/MineCraft_C/saves/*    # the list, before deleting anything
    pgrep -af minecraft_c                                     # no game running before any rm
    pgrep -af "$PWD/build"                                    # must print nothing
    pgrep -a Xvfb                                             # must print nothing

## Gate

Ahmed opens a new world himself (with `/tp` and `/fly` from plan 006 to find trees quickly) and:
1. Cuts a thin oak through its first log over the ground (one log): it falls, and a flush stump with a root under it stays.
2. Cuts a 2 by 2 jungle tree's bottom layer: it stands after 1, 2 and 3 logs, and falls on the 4th.
3. Looks down at the stump: level with the grass, rings showing. Digs beside it and sees it go underground.
4. Cuts one log out of a wide trunk's side, part way up: the tree stays standing.
5. Cuts a log out of a wall he built, with leaves against it: nothing else goes.

His notes go here, in his words, with any change he asks for recorded as an Issue found or a new plan.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Ahmed's game is running with a world open | Do not delete; never kill it; wait and tell the main thread |
| Ahmed has not answered Q1 | Delete nothing; the todo stays open, "waiting on Ahmed" |
| A suite failure that is main's own | Shown on main, left for its owner, recorded |
| Q1 to Q6 (master section 10) | Their defaults, unless Ahmed says otherwise at the gate |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
