# tests/run/player/felling

**Purpose:** ctest `run_player_felling`. Tests `run/src/player/felling/` with the hands of `run/src/player/interaction/` and the leaf sweep of `run/src/world/tick/`: every kind of tree the generator draws comes down whole, the limit is measured against the biggest, what it must leave stays, and wherever a tree is cut no wood or vine is left floating.
**Tier:** test.
**Owns:** the test arena, and a small empty world for each test, given back by the end of it.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`, `run/src/terrain/feature/` for `terrain_feature_paint_tree`.

Needs no card, no display and no input device. Bare hands, a pickaxe and an axe each fell a whole tree, sneaking or not, and a log wall with no leaves stays whatever is in hand. What an axe in a real inventory wears, only the cut and nothing for what falls, is in `tests/run/item/equipment/`, with a real game on the card.

## The proof by eye

`fixtures/fell.script` is a person's first minute on seed 36, as in `tests/run/playthrough/`: the mud underfoot, out of the hole to the mangrove, then one cut at its root with the left button and nothing in hand but the mud. It looks up at the tree from the same place before and after, and it saves and opens the world again. `run_playthrough` in `tests/run/playthrough/` runs it and reads the report: the cut and 9 more logs on the day it was written, with the hand holding only mud. The pictures are for a person:

    tools/test-or-build.sh timeout 400 build/debug/bin/minecraft_c --headless --menu --saves /tmp/fell/saves --settings /tmp/fell/settings.yaml --script tests/run/player/felling/fixtures/fell.script --screenshot-dir /tmp/fell/shots --width 640 --height 480 --render-distance 8

Then read `before.png`, `during.png` and `later.png`, and the line `the tree feller felled N logs past what was cut` in what it prints.
