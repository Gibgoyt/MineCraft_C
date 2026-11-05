# tests/run/player/felling

**Purpose:** ctest `run_player_felling`. Tests `run/src/player/felling/` with the hands of `run/src/player/interaction/` and the leaf sweep of `run/src/world/tick/`: every kind of tree the generator draws comes down whole, the limit is measured against the biggest, and what it must leave stays.
**Tier:** test.
**Owns:** the test arena, and a small empty world for each test, given back by the end of it.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`, `run/src/terrain/feature/` for `terrain_feature_paint_tree`.

Needs no card, no display and no input device. What the axe in a real inventory does is in `tests/run/item/equipment/`, with a real game on the card.

## The proof by eye

`fixtures/fell.script` is a person's first few minutes on seed 36, as in `tests/run/playthrough/`: mud, four logs by hand from one mangrove, planks, a table and sticks in the inventory, a wooden axe at the table, then one cut at the foot of another mangrove with the left button. It looks at the tree from the same place before and after. It is not a ctest, because it is aimed by hand from screenshots and a change to terrain or to where wooden tools are made leaves it clicking on nothing:

    tools/test-or-build.sh timeout 400 build/debug/bin/minecraft_c --headless --menu --saves /tmp/fell/saves --settings /tmp/fell/settings.yaml --script tests/run/player/felling/fixtures/fell.script --screenshot-dir /tmp/fell/shots --width 640 --height 480 --render-distance 8

Then read `before.png`, `during.png` and `later.png`, and the line `an axe felled N logs past what it cut` in what it prints.
