# 010-ahmed-bohemoth-initial-game-fixes-round-3 — phase 0: The vision, and the plan files

**Status:** not started   (updated 2026-11-14)

**Read this file before any other file of plan 010,** then the master `plans/010-ahmed-bohemoth-initial-game-fixes-round-3.md`. Every phase file assumes you know what is here.

## Goal

Two things are true when this phase is done:

1. Every agent working on plan 010 can say in a few sentences:
   - what is wrong today: one cut fells a whole tree, and trees stand on top of the dirt;
   - what Ahmed wants: a tree falls only when a whole layer of its trunk is cut, and it leaves a stump that goes into the ground;
   - what the plan changes: roots in generation (phase 01), layer felling (phase 02), version 3;
   - what it must not change: no regrowth, no new items, no farmer's workbench (that is plan 011).
2. The plan files exist: the master, and phase files 01 to 03, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

---

## Part 1: What Ahmed asked for

### His words, 2026-11-14

- "For tree feller I should never just cut on block and the entire tree falls. I have to cut a layer. Like in Hytale ... I have to cut all the blocks that have blocks on top of it, and I leave a stump in the ground in Hytale I really really love that feature."
- "Maybe also add plans to create a farmers workbench and separate that entirely from the woodworking bench??? add a phase in the plan for that too please! There are actually loads of game logic that Hytale has for trees but we will not be doing that right now! Only be doing the tree feller stuff I think."
- "Trees must also have an underground root and not just start on top of dirt ... Look at ~/Pictures/10.png in Hytale the tree only falls if i cut down an entire layer! and 11.png shows the tree stump, i can tell you that a little of that stump goes underground."
- "So we are touching world generation to make trees have a tree stump as well as properly creating tree feller!"
- "I think for farming bench put that at ./plans/011-* so that I know that we are doing that task later."

### The two pictures

- **`~/Pictures/10.png`:** close up on a thick trunk, 2 logs by 2. A notch is cut out of the layer above the ground, and the ringed tops of the logs below it show. The tree still stands. **It falls only when every log of that layer is gone.**
- **`~/Pictures/11.png`:** looking down after the fall. The bottom layer of logs is left as a **stump level with the grass**, rings showing on top, with felled wood lying round it. Ahmed: a little of the stump goes underground.

We take the mechanic only. The names, art and items are our own (master section 0).

## Part 2: The words every phase uses (master 2.1)

| Word | Meaning |
|---|---|
| Ground | Solid, not a log, not leaves |
| Root log | A log with ground under it and ground or the same wood on at least 3 of its 4 sides |
| The tree | Logs of one wood joined to the cut (26 neighbours), up and down, touching leaves somewhere and holding at least one root log |
| Trunk column | A column of the tree holding a root log |
| The trunk | The trunk columns joined by a side or corner to the cut's column |
| Layer h | The tree's logs at z = h in the trunk's columns. **Branches are never in a layer** |
| The stump | What stays at and under the cut layer, still joined to a root log |

**A thin trunk's layer is 1 log, a wide one's is 4, and a mangrove's is its trunk plus any prop root at that height.**

## Part 3: The decisions already made in the master

| Decision | Why |
|---|---|
| Roots are **ordinary logs** of the tree's wood, not a new block | `11.png` shows ringed log tops, and every log has that picture. A new block would mean 8 woods of blocks, pictures, drops and save names. A root is a root by **where it is** |
| Root depth **2**: one log level with the ground's top, one under it | Ahmed: "a little of that stump goes underground". After a cut at the first log above the ground, the stump is the two root logs, flush, as in `11.png` |
| Roots go over solid ground including stone, never over air, water, plants, logs, ores, bedrock or unbreakable blocks | So a root never hangs, never eats an ore, and is always 2 deep where the ground is whole |
| Phase 01 roots, then phase 02 felling | The new feller needs a root log to call something a tree |
| `TERRAIN_GENERATOR_VERSION` 2 → 3, no migration | Every tree's column changes; the repo's rule (`run/src/save/world/README.md`) |
| A walk that is unsure (column not loaded, overflow) fells nothing | Better a tree left standing than a wrong one felled |
| Leaves follow today's leaf rule | Nothing floats, nothing held is taken (Q5) |

## Part 4: Rules every phase follows

1. **Generation stays a pure function of the seed and the climate sample.** A root voxel depends only on what is at that voxel. The box-against-chunk test (`tests/run/terrain/feature/own/`) and 1 against 16 workers give the same bytes.
2. **Only trees change in the land.** Height, density, hydrology, biomes, surface, caves, ores, lakes, plants and snow keep their digests; a change is an Issue found.
3. **Felling is the sim's, with no heap and no waiting**, as today (`run/src/player/felling/README.md`).
4. **Every heavy command goes through `tools/test-or-build.sh`**, one command per call, `timeout` inside the wrapper (`.claude/rules/test-or-build-mutex.md`).
   - After tests, `pgrep -af "$PWD/build"` prints nothing.
   - Never start the game or a server and leave it running; never kill Ahmed's running game.
5. **Nothing in `saves/` is deleted without Ahmed's word.** `test_8-3` is a generator 2 world he plays (Q1).
6. **Never push.**
7. **Commit as Ahmed, with `git commit -F -`**: one-sentence messages, no AI trailers (no `Co-Authored-By`, no anthropic or Claude lines). After each commit, `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` prints nothing.
8. **The repo's own rules:**
   - `plans/README.md`: todos ticked as work lands, issues recorded when they arise;
   - `plans/CODING_STANDARD.md`: split at 1500 lines, hard limit 2000;
   - `plans/REPO_STRUCTURE.md`.

## Part 5: The phases, and what each leaves behind

| Phase | File | What's true when it's done |
|---|---|---|
| 00 | `...-phase-00-vision-and-plan-files.md` | This file, the master, and phase files 01 to 03 exist |
| 01 | `...-phase-01-roots-and-stumps-in-world-generation.md` | Every generated tree's trunk columns go 2 blocks into the ground; version 3 with its pins; the playthrough aims at the first log over the ground; `saves/` listed and Ahmed asked |
| 02 | `...-phase-02-layer-felling.md` | A tree falls only when a whole trunk layer is cut, the stump stays, branches cut alone come down alone, placed logs never fall |
| 03 | `...-phase-03-the-final-check.md` | Full suites, benches, the old worlds dealt with as Ahmed said, his gate notes |

The open questions Q1 to Q6 (master section 10) each have a default, and each is repeated in the phase it affects:
- Q1 old worlds: phases 01 and 03;
- Q2 root depth, Q3 a stump block, Q4 flare: phase 01;
- Q5 leaves, Q6 branch cuts: phase 02.

## Part 6: What is out of scope (master section 8)

- regrowth, saplings from stumps or a bench;
- sap, resin, any new by-product or item;
- any growth "essence";
- **the farmer's workbench, which is plan 011**;
- stumps rotting or growing back;
- a falling animation;
- root flare;
- a stump block.

---

## Todo

- [ ] Read `plans/README.md`, `plans/CODING_STANDARD.md`, `plans/REPO_STRUCTURE.md`, and plan 006's master and phase files as the model
- [ ] Check `010` and `011` are free: `ls plans/ | grep -E '^01[01]'` printed nothing before this plan
- [ ] The master `plans/010-ahmed-bohemoth-initial-game-fixes-round-3.md`, committed alone
- [ ] Phase files 01 to 03 from the template, every box unticked, status `not started`, each with a "Read first" line linking back to this file and the master, one commit each
- [ ] This file restates the pictures, the words of master 2.1, the order, and what is out of scope
- [ ] Every phase file has Goal, Todo, Tests, Verification, Gate, Risks, Commits and Issues found: `grep -L "## Issues found" plans/010-*phase-*.md` prints nothing
- [ ] Report the paths created

## Verification

No build, test or code changes in this phase.

    ls plans/010-*
    grep -L "## Issues found" plans/010-*phase-*.md    # prints nothing
    git status --short                                 # nothing uncommitted

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
