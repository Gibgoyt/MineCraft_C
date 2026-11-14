# tests/run/player/felling

**Purpose:** ctest `run_player_felling`. Tests `run/src/player/felling/` with the hands of `run/src/player/interaction/` and the leaf sweep of `run/src/world/tick/`: every kind of tree the generator draws comes down when the last log of a trunk layer is cut (a 2 by 2 on its fourth cut, in all 24 orders), leaving its rooted stump flush with the ground; a branch cut drops only what no longer joins a root; a log tower or wall on the ground is never felled; an unsure find (a column not loaded, too many logs) fells nothing; the limit is measured against the biggest tree, the find is timed on it, and wherever a tree is cut no wood or vine is left floating.
**Tier:** test.
**Owns:** the test arena, and a small empty world for each test, given back by the end of it.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`, `run/src/terrain/feature/` for `terrain_feature_paint_tree`.

**Trees stand on dirt** (plan 010 phase 01). `pf_paint` fills the box with dirt up to the tree's own column's top
(`PF_Z - 1`) and every other column's foot before it paints, and `pf_place_tree` puts that ground in the world with the tree.
So each painted tree has its roots, 2 logs under each trunk column, as a generated tree does: phase 02's felling needs them
(a tree must hold a root log), and the limit test counts them in the biggest tree. `pf_trunk` from `PF_Z` draws the same 2
roots into the 6 blocks of dirt `pf_begin` lays under every loaded column. The tests cut whole layers: the stump, the roots
under the first layer over the ground, stays. So none of them depends on the generated land.

Needs no card, no display and no input device. Bare hands, a pickaxe and an axe each fell a whole tree, sneaking or not, and a log wall with no leaves stays whatever is in hand. What an axe in a real inventory wears, only the cut and nothing for what falls, is in `tests/run/item/equipment/`, with a real game on the card.

## The proof by eye

`fixtures/fell.script` is a person's first minute on seed 36, as in `tests/run/playthrough/`: the block underfoot dug and
held, then, at its line `@fell`, a `/tp` to the tree `run_playthrough` finds from the land near the start (the land there moves
with each plan that changes the terrain, so no walk is written down), a look up at it and back, and one cut at its root with the
left button and nothing in hand but what the hole gave. It looks up at the tree from the same place after, and it saves and
opens the world again. `run_playthrough` in `tests/run/playthrough/` writes the `@fell` line for the tree, runs it and reads
the report: the cut was a log and the feller felled at least 2 more, with the hand holding only what the hole gave. The
pictures are for a person: run `run_playthrough` with `MC_PLAYTHROUGH_KEEP=1`, which prints where it kept them.

Then read `before.png`, `during.png` and `later.png`, and the line `the tree feller felled N logs past what was cut` in what it prints.
