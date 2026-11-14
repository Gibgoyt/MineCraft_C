# 011-ahmed-bohemoth-farmers-workbench — phase 3: The final check

**Status:** not started   (updated 2026-11-14). Waits for Ahmed's go on plan 011.

Read first: `plans/011-ahmed-bohemoth-farmers-workbench-phase-00-vision-and-plan-files.md`, then the master `plans/011-ahmed-bohemoth-farmers-workbench.md` (sections 5, 9 and 10), then phases 01 and 02's Issues found.

## Goal

Plan 011 is closed:
- The full debug and release suites pass, apart from failures shown to fail on main itself, each named with its evidence.
- A release build is made.
- `texture_vector check` passes.
- Ahmed's gate notes are written here in his words: the farmer's workbench made, placed, opened, used for a melon, seeds and bone meal, and seen to be its own thing beside the woodworking bench.

## Where this fits

**The vision.** The last step of Ahmed's "separate that entirely from the woodworking bench": he plays it and says whether it is right.

**What this phase must NOT build:**
- No new behaviour; a fix found here is the smallest that makes a test pass, recorded.
- No saplings, regrowth or any out-of-scope item (master section 8).
- Nothing of plan 010.

## Ahmed's words

- "Maybe also add plans to create a farmers workbench and separate that entirely from the woodworking bench??? add a phase in the plan for that too please!"
- "I think for farming bench put that at ./plans/011-* so that I know that we are doing that task later."

---

## Background, explained

Nothing here touches the land or the save format beyond the bench grid's new station number (phase 01), so there is no version, no pin to move and no world to delete. Worlds saved before plan 011 open unchanged: `farming` is on the end of the station numbers.

## Owned paths

This file, and only the smallest fixes a failing test needs, in the paths phases 01 and 02 owned, each recorded.

## Steps

1. **Rebase onto main** if it moved, and record it. *Why: the suites must run on what will merge.*
2. **The debug suite, whole.** A failure is run alone, and against main if it fails alone; record both. *Why: a closed plan says what passes.*
3. **A release build and the release suite.** *Why: the release build is what Ahmed plays.*
4. **`texture_vector check`.** *Why: the new pictures are whole and match their recipes.*
5. **Ahmed's gate**, then his notes here. *Why: he decides when it is right.*

## Todo

From the master (section 9, phase 03):

- [ ] Full debug and release suites; a release build
- [ ] Ahmed's gate notes, in his words

Expanded:

- [ ] `texture_vector check` passes
- [ ] Each failing test run alone and against main, recorded
- [ ] `plans/011-*` phase files' todos and statuses current; the master left as it is (verbatim)
- [ ] `pgrep -af "$PWD/build"` and `pgrep -a Xvfb` print nothing at the end

## Tests

| Test | Checks | Numbers |
|---|---|---|
| Debug suite | Everything | All pass, or each failure shown as load or as main's |
| Release suite | Everything | The same |
| `texture_vector check` | Every picture | Passes |

## Verification

    git log --oneline main..HEAD
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure -j 2
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 3600 ctest --preset release --output-on-failure -j 2
    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_texture_vector
    tools/test-or-build.sh build/debug/bin/tools/texture_vector check --textures textures --materials textures/vector.yaml
    pgrep -af "$PWD/build"    # must print nothing
    pgrep -a Xvfb             # must print nothing

## Gate

Ahmed, in a new world (or any world of the current generator):
1. Collects seeds from grass and makes the farmer's workbench at a crafting table from 3 seeds over 2 logs.
2. Places it beside a woodworking bench and says whether each reads as its own thing.
3. Opens it ("Farmer's Workbench") and makes a melon from nine slices, seeds from a slice, and bone meal from a bone.
4. Checks the hands and the woodworking bench make none of those.

His notes go here, in his words. Any change he asks for is recorded as an Issue found or a new plan.

## Risks and open questions

| Risk or question | Default |
|---|---|
| A suite failure that is main's own | Shown on main, left for its owner, recorded |
| Q1 to Q6 (master section 10) | As answered in phases 01 and 02, or their defaults |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
