# 011-ahmed-bohemoth-farmers-workbench — phase 0: The vision, and the plan files

**Status:** not started   (updated 2026-11-14). **The plan is for later:** nothing past this phase starts until Ahmed says go.

**Read this file before any other file of plan 011,** then the master `plans/011-ahmed-bohemoth-farmers-workbench.md`.

## Goal

Two things are true when this phase is done:

1. Every agent working on plan 011 can say in a few sentences:
   - what Ahmed asked for: a farmer's workbench, a station entirely separate from the woodworking bench, done later;
   - what the plan builds: the station (phase 01), then the farming recipes moved to it (phase 02);
   - what it must not build: regrowth, saplings from the bench, essences, watering, or anything of plan 010's tree feller.
2. The plan files exist: the master, and phase files 01 to 03, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

---

## Part 1: What Ahmed asked for

His words, 2026-11-14:
- "Maybe also add plans to create a farmers workbench and separate that entirely from the woodworking bench??? add a phase in the plan for that too please! There are actually loads of game logic that Hytale has for trees but we will not be doing that right now! Only be doing the tree feller stuff I think."
- "I think for farming bench put that at ./plans/011-* so that I know that we are doing that task later."

The rest of what he said that day is about trees, and is plan 010:
- "For tree feller I should never just cut on block and the entire tree falls. I have to cut a layer. Like in Hytale ... I have to cut all the blocks that have blocks on top of it, and I leave a stump in the ground in Hytale I really really love that feature."
- "Trees must also have an underground root and not just start on top of dirt ... Look at ~/Pictures/10.png in Hytale the tree only falls if i cut down an entire layer! and 11.png shows the tree stump, i can tell you that a little of that stump goes underground."
- "So we are touching world generation to make trees have a tree stump as well as properly creating tree feller!"

His pictures (`~/Pictures/10.png`, a notched 2 by 2 trunk still standing; `11.png`, the flush stump) are plan 010's. The bench's look is our own, painted by recipe.

## Part 2: What "entirely separate" means here

| Separate | How |
|---|---|
| Its own block | `farmers_workbench_left` and `_right`, 2 by 1 by 1 |
| Its own item | `farmers_workbench` |
| Its own recipe | At the crafting table: 3 seeds over 2 logs (`["SSS", "L L"]`, `group_seeds`, `group_logs`) |
| Its own station | `farming`, number 6, on the end of `GR_STATIONS` so saved grids keep their numbers |
| Its own screen | `UI_SCREEN_FARMING`, "Farmer's Workbench" |
| Its own recipes | Nothing made at `farming` is made at `woodworking`, and nothing the other way, by test |

## Part 3: The decisions already made in the master

| Decision | Why |
|---|---|
| `melon` turns on at `farming` | Its `off` reason was a 3 by 3 and no station; the bench has both |
| `melon_seeds`, `pumpkin_seeds`, `bone_meal` move from the hands | Seeds from crops and what makes crops grow are the farmer's work. **Asked first (Q1)**, since it changes Ahmed's 2026-11-10 order for the hands |
| Food, sugar, dyes, paper and hoes stay | Eating must not wait for a bench; tools are made by their material (Q2 to Q4) |
| A 2 by 1 bench like the leather workbench | Every part of the pattern already exists to copy |

## Part 4: Rules every phase follows

1. **Every heavy command goes through `tools/test-or-build.sh`**, one command per call, `timeout` inside the wrapper (`.claude/rules/test-or-build-mutex.md`). After tests, `pgrep -af "$PWD/build"` prints nothing. Never start the game or a server and leave it running.
2. **Pictures are our own**, `source.kind: procedural`, no pack's pixels and no other game's art, names or text.
3. **Nothing of plan 010** (feller, stumps, roots) is touched here.
4. **Never push.**
5. **Commit as Ahmed, with `git commit -F -`**: one-sentence messages, no AI trailers (no `Co-Authored-By`, no anthropic or Claude lines). After each commit, `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` prints nothing.
6. **The repo's own rules:** `plans/README.md`, `plans/CODING_STANDARD.md`, `plans/REPO_STRUCTURE.md` (section 13, "A new block").

## Part 5: The phases

| Phase | File | What's true when it's done |
|---|---|---|
| 00 | `...-phase-00-vision-and-plan-files.md` | This file, the master, and phase files 01 to 03 exist |
| 01 | `...-phase-01-the-farmers-workbench-station.md` | The bench is made, placed, broken, opened and drawn; `farming` exists; `melon` is made there |
| 02 | `...-phase-02-farming-recipes-move-to-the-bench.md` | Seeds and bone meal are made only at the bench (after Q1); the README and the order walk say so |
| 03 | `...-phase-03-the-final-check.md` | Full suites; Ahmed's gate notes |

Open questions (master section 10):
- Q1 moving seeds and bone meal (phase 02);
- Q2 food, Q3 paper, Q4 hoes (phase 02);
- Q5 the recipe, Q6 the size (phase 01).

## Part 6: Out of scope (master section 8)

- saplings and regrowth;
- essences or conversions;
- watering cans, seed bags, planters;
- new crops in the ground;
- hoes moved;
- plan 010's work.

---

## Todo

- [ ] Read `plans/README.md`, `config/recipes/README.md`, `run/src/world/bench/README.md`, and plan 005 phase 06's file (the stations) as the model
- [ ] The master `plans/011-ahmed-bohemoth-farmers-workbench.md`, committed alone
- [ ] Phase files 01 to 03 from the template, every box unticked, status `not started`, each with a "Read first" line linking back to this file and the master, one commit each
- [ ] Every phase file has Goal, Todo, Tests, Verification, Gate, Risks, Commits and Issues found: `grep -L "## Issues found" plans/011-*phase-*.md` prints nothing
- [ ] Report the paths created

## Verification

No build, test or code changes in this phase.

    ls plans/011-*
    grep -L "## Issues found" plans/011-*phase-*.md    # prints nothing
    git status --short                                 # nothing uncommitted

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
