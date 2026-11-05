# tests/run/player/interaction

**Purpose:** ctest `run_player_interaction`. Tests `run/src/player/interaction/`, with what `run/src/player/` and `run/src/game/` do with it.
**Tier:** test.
**Owns:** the test arena, and what its world, game and card blocks take, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; and for the test of the real loop, the real workers and the real `sim` and `gpu` threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/input/fixtures/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/game/` and everything under it, `config/blocks/`, and a card for the tests of the game.

Compute only. No real display, console or input device is opened: pictures go into plain memory, and the program always runs `--headless`.

What it checks:

- **Drop tables for every block:** each block's file in `config/blocks/` is read here, with a scanner of its own, and compared with the generated rows. Then every breakable block is really mined: harvested, it gives what its rows can give, and every row of chance 1 comes out; with too low a tool it breaks for nothing. Then each table is rolled 20000 times and each row's rate and counts are compared with its file. Dirt drops dirt, by name, along with other blocks written out by hand.
- **Timed mining:** hand-worked numbers for tools, tiers and penalties, and every block in every tool's hands against a sum in exact decimals from its file, as well as against the ticks it really took.
- **What starts the count again:** letting go, another target, the block changing, and bedrock never going.
- **The delay and the sweep,** a plant going with its ground, and a stack of them.
- **Placing:** all six faces, what can be overwritten, the body at both heights, a slab under the feet, the world's top, a column not loaded, an empty hand, plants on and off the ground, and the repeat rate.
- **What can be heard:** a hit every 4 ticks from the first, a break, and a place, of each block's family, for every block; what starts the hits again; bedrock; a full ring of them; and the same bytes from the hands with or without anywhere for sounds to go.
- **The ring and the hand:** where a test goes through the player, `pi_drops_to_hand` takes each drop from the ring to the hand at once, as these tests are of the hands: `tests/run/entity/item_drop/` tests an item's way between them. Overflow counted, the one slot's rules, and the same bytes from the same seed with nothing from the heap.
- **Edits are real:** each goes in the log and flags its section for the renderer, save and light.
- **Through the player:** a click between ticks, no one at the controls, and walking up to a wall of dirt on real intents, mining it with a real aim, walking through the gap, and placing it back.
- **The real threads:** a real world on the card, mined through `game_hands_send`, what it drops picked up from the ground, placed back, and the picture changing.
- **Real buttons:** fake kernel events for the left and right buttons, through `input/` and `game_loop_source`, on the real threads: mined, picked up, and placed back by clicks that begin and end between two polls.
- **What goes by itself:** water washing a dandelion away in a running game, and the item coming out through `game_sim_tick`.
- **The program:** a script walks, looks down, mines through grass to dirt, mines a wall of the hole, and places one back, with screenshots before and after each. The same script gives the same bytes twice.
